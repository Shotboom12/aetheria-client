// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: see HdMotionHistory.hpp.
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdMotionHistory.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Manifest.hpp"
#include <cstring>
#include <unordered_map>
#include <vector>

namespace gxvk::vk::hdmotion
{
    namespace
    {
        constexpr uint32_t kMaxEntries = 1024;           // skinned draws kept per frame
        constexpr size_t   kMaxFloats  = 6u * 1024 * 1024; // 24 MB of constants per frame at most

        struct Entry { uint64_t key; uint32_t occurrence; uint32_t rows; size_t offset; int32_t twin; };
        constexpr size_t kPixelFloats = size_t(pipelines::kPixelRows) * 4;

        struct Table
        {
            std::vector<Entry>  entries;
            std::vector<float>  pool;
            std::vector<pipelines::Draw> draws;   // the draw as recorded (GPU handles borrowed for this frame), constants re-pointed in MatchedDraw
            std::vector<float>  pixels;           // kPixelFloats per entry
            std::unordered_map<uint64_t, uint32_t> counts;   // key -> how many already stored (the next occurrence)
            std::unordered_map<uint64_t, uint32_t> first;    // key -> index of occurrence 0 (occurrences are consecutive per key by insertion order)
            std::unordered_map<uint64_t, std::vector<uint32_t>> byKey;
            void Clear() { entries.clear(); pool.clear(); draws.clear(); pixels.clear(); counts.clear(); first.clear(); byKey.clear(); }
        };

        bool enabled = false;
        uint64_t frameSerial = 0;
        Table current, previous;
        std::unordered_map<uint64_t, uint32_t> replayCounts;
        Stats stats;

        uint64_t Mix(uint64_t h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); return h; }

        bool Skinned(const pipelines::Draw& d)
        {
            if (d.vertex != pipelines::VertexProgram::Translated || !d.vertexRecord || !d.vertexRegs) return false;
            if (d.vertexRows < pipelines::kVertexRowsLarge) return false;   // no bone palette in a small register file
            const uint32_t need = 1u << manifest::kBlendIndices0;   // the velocity generator accepts any record with blend indices
            return (d.vertexRecord->inputMask & need) == need;
        }

        // Stable across frames for one batch of one model: the geometry it draws from and the programs and first texture it draws with.
        uint64_t KeyOf(const pipelines::Draw& d)
        {
            uint64_t h = 0xcbf29ce484222325ull;
            h = Mix(h, d.streamCount ? reinterpret_cast<uint64_t>(d.streams[0].buffer) : 0);
            h = Mix(h, d.streamCount ? uint64_t(d.streams[0].offset) : 0);
            h = Mix(h, reinterpret_cast<uint64_t>(d.indexBuffer));
            h = Mix(h, d.firstIndex);
            h = Mix(h, d.count);
            h = Mix(h, reinterpret_cast<uint64_t>(d.vertexRecord));
            h = Mix(h, reinterpret_cast<uint64_t>(d.fragmentRecord));
            h = Mix(h, (d.textureMask & 1u) ? d.textures[0].imageId : 0u);
            return h;
        }
    }

    void SetEnabled(bool on)
    {
        if (on == enabled) return;
        enabled = on;
        if (!on) { current.Clear(); previous.Clear(); replayCounts.clear(); stats = Stats(); }
    }

    bool Enabled() { return enabled; }

    void BeginFrame(uint64_t serial)
    {
        if (!enabled) return;
        stats.lastFrame = stats.matched;
        stats.lastObserved = stats.observed; stats.lastStored = stats.stored; stats.lastDropped = stats.dropped;
        std::swap(current, previous);
        current.Clear();
        stats.observed = stats.matched = stats.stored = stats.dropped = 0;
        frameSerial = serial;
        // Not the frame right after the previous one (a loading screen, a pause): the old palettes describe another moment.
        // The caller's BeginFrame is once per world frame, so a gap is only possible when a frame was skipped entirely.
    }

    void Observe(const pipelines::Draw& d, uint64_t serial, bool sceneTarget, bool queryActive)
    {
        if (!enabled || !sceneTarget || queryActive || !serial) return;
        if (!Skinned(d)) return;
        if (!d.depthTest || !d.depthWrite || d.blend > 1 || !d.pixelRegs) return;   // opaque / alpha-keyed world geometry only, like hdcapture
        ++stats.observed;
        const size_t floats = size_t(d.vertexRows) * 4;
        if (current.entries.size() >= kMaxEntries || current.pool.size() + floats > kMaxFloats) { ++stats.dropped; return; }
        const uint64_t key = KeyOf(d);
        const uint32_t occurrence = current.counts[key]++;
        Entry e{key, occurrence, d.vertexRows, current.pool.size(), -1};
        current.pool.insert(current.pool.end(), d.vertexRegs, d.vertexRegs + floats);
        current.pixels.insert(current.pixels.end(), d.pixelRegs, d.pixelRegs + kPixelFloats);
        current.draws.push_back(d);
        auto it = previous.byKey.find(key);
        if (it != previous.byKey.end() && occurrence < it->second.size()) { e.twin = int32_t(it->second[occurrence]); ++stats.matched; }
        current.byKey[key].push_back(uint32_t(current.entries.size()));
        current.entries.push_back(e);
        ++stats.stored;
    }

    void BeginReplay() { replayCounts.clear(); }

    bool PreviousFor(const pipelines::Draw& d, Previous& out)
    {
        out = Previous();
        if (!enabled || !Skinned(d)) return false;
        const uint64_t key = KeyOf(d);
        const uint32_t occurrence = replayCounts[key]++;
        auto it = previous.byKey.find(key);
        if (it == previous.byKey.end() || occurrence >= it->second.size()) return false;
        const Entry& e = previous.entries[it->second[occurrence]];
        out.vertexRegs = previous.pool.data() + e.offset;
        out.rows = e.rows;
        return true;
    }

    size_t MatchedCount()
    {
        size_t n = 0;
        for (const Entry& e : current.entries) if (e.twin >= 0) ++n;
        return n;
    }

    bool MatchedDraw(size_t index, pipelines::Draw& out)
    {
        if (!enabled) return false;
        size_t seen = 0;
        for (size_t i = 0; i < current.entries.size(); ++i)
        {
            const Entry& e = current.entries[i];
            if (e.twin < 0) continue;
            if (seen++ != index) continue;
            const Entry& before = previous.entries[size_t(e.twin)];
            if (before.rows != e.rows) return false;
            out = current.draws[i];
            out.vertexRegs = current.pool.data() + e.offset;
            out.pixelRegs = current.pixels.data() + i * kPixelFloats;
            out.hdPixelHighRegs = nullptr;
            out.pixelExtra = nullptr;
            out.pixelExtraRows = 0;
            out.motionPrevRegs = previous.pool.data() + before.offset;
            return true;
        }
        return false;
    }

    Stats GetStats() { return stats; }

    void Shutdown() { enabled = false; current.Clear(); previous.Clear(); replayCounts.clear(); stats = Stats(); }
}
