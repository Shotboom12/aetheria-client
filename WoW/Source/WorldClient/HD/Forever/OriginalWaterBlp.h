#pragma once

// Original CDN BLP2 mip views. No transcoding, pixel edits or mip generation.
// The caller owns the input buffer for the lifetime of every returned view.
#include <array>
#include <cstddef>
#include <cstdint>

namespace HD::ForeverWater {
enum class OriginalFormat { Bgra8888, Dxt5 };
struct OriginalMip {
    const uint8_t* bytes = nullptr;
    uint32_t width = 0, height = 0, size = 0, stride = 0;
};
struct OriginalBlp {
    OriginalFormat format = OriginalFormat::Bgra8888;
    uint32_t width = 0, height = 0, levelCount = 0;
    std::array<OriginalMip, 16> levels{};
};

inline bool ReadOriginalBlp(const uint8_t* data, size_t size, OriginalBlp& output) {
    output = {};
    if (!data || size < 148 || size > 64u * 1024u * 1024u || data[0] != 'B' || data[1] != 'L' || data[2] != 'P' || data[3] != '2') return false;
    auto word = [data](size_t at) {
        return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 |
               uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 3]) << 24;
    };
    if (word(4) != 1) return false;
    OriginalBlp candidate;
    candidate.width = word(12);
    candidate.height = word(16);
    if (!candidate.width || !candidate.height || candidate.width > 4096 || candidate.height > 4096) return false;
    if (data[8] == 3 && data[9] == 8 && data[10] == 8) {
        candidate.format = OriginalFormat::Bgra8888;
    } else if (data[8] == 2 && data[9] == 8 && data[10] == 7) {
        candidate.format = OriginalFormat::Dxt5;
    } else {
        return false; // Refuse unsupported layouts instead of guessing a format.
    }
    bool ended = false;
    std::array<uint32_t, 16> starts{}, ends{};
    uint32_t width = candidate.width, height = candidate.height;
    for (uint32_t level = 0; level < 16; ++level) {
        const uint32_t offset = word(20 + level * 4), bytes = word(84 + level * 4);
        if (!bytes) {
            if (offset) return false;
            ended = true;
            continue;
        }
        if (ended || (level && candidate.levels[level - 1].width == 1 && candidate.levels[level - 1].height == 1)) return false;
        const bool compressed = candidate.format == OriginalFormat::Dxt5;
        const uint32_t stride = compressed ? ((width + 3) / 4) * 16 : width * 4;
        const uint32_t required = stride * (compressed ? (height + 3) / 4 : height);
        if (bytes != required || offset < 148 || offset > size || bytes > size - offset) return false;
        for (uint32_t prior = 0; prior < level; ++prior) {
            if (offset < ends[prior] && offset + bytes > starts[prior]) return false;
        }
        starts[level] = offset;
        ends[level] = offset + bytes;
        candidate.levels[level] = {data + offset, width, height, bytes, stride};
        candidate.levelCount = level + 1;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    if (!candidate.levelCount || (candidate.levelCount > 1 && !data[11])) return false;
    output = candidate;
    return true;
}
} // namespace HD::ForeverWater
