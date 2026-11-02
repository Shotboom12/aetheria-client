#include "WoW/Source/WorldClient/HD/Forever/HdForeverFieldOwner.h"
#include "WoW/Source/WorldClient/HD/HdForeverWater.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/ExternalTextureProxy.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdForeverWaterImages.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/GxSeam.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/Services/Texture.h"
#include "WoW/Source/WorldClient/WaterRipple.h"
#include "WoW/Source/WorldClient/World.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "WoW/Source/WorldClient/WorldScene.h"
#include "Engine/Source/Os/TimeManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace HD::ForeverNative::FieldOwner {
namespace {
namespace frame = gxvk::vk::frame;
namespace proxy = gxvk::vk::external_proxy;
namespace api = gxvk::vk::hdeffect;
constexpr uint64_t kCpuLimit = 24ull * 1024 * 1024; // was 8 MiB for 4 entries; 5 MiB scratch + 8 x ~0.7 MiB
constexpr uint32_t kCascadeCount = 4, kProxyCount = 5;
constexpr uint64_t kQuarterYard = 4;
struct Entry;
struct ProxyRef { Entry* entry = nullptr; uint32_t channel = 0; };
struct ProxySlot { CTexture* texture = nullptr; CGxTex* gx = nullptr; ProxyRef ref{}; bool registered = false; };
struct Entry {
    bool occupied = false, queued = false, cpuAllocated = false;
    Key key{};
    uint64_t requestSerial = 0, publishedSerial = 0, lastResolved = 0;
    uint64_t cpuBytes = 0;
    Metadata metadata{};
    std::array<SampledField, kCascadeCount> fft;
    SampledField ripple;
    std::array<ProxySlot, kProxyCount> proxies{};
    FftField fftCpu{};
    std::vector<FloatMip> rippleCpu;
};
std::array<Entry, MaxEntries> entries{};
bool variantsReady = false, preflightReady = false, registryReady = false, registryFailed = false;
FftRegistry registry{};
uint64_t cpuReserved = 0;

bool Same(const Key& a, const Key& b) { return a.map == b.map && a.profile == b.profile && a.planeQuarterYard == b.planeQuarterYard; }
int32_t Quantize(float z) {
    if (!std::isfinite(z) || std::fabs(z) > 1000000.0f) return INT32_MIN;
    return static_cast<int32_t>(std::lround(z * kQuarterYard));
}
const gxvk::vk::texture::Image* Resolve(void* owner, uint64_t serial) {
    auto* ref = static_cast<ProxyRef*>(owner);
    if (!ref || !ref->entry || !variantsReady || ref->channel >= kProxyCount) return nullptr;
    Entry& e = *ref->entry;
    if (!e.occupied || e.publishedSerial != serial || !serial) return nullptr;
    e.lastResolved = std::max(e.lastResolved, serial);
    return ref->channel < kCascadeCount ? e.fft[ref->channel].Get(frame::Slot(), serial)
                                        : e.ripple.Get(frame::Slot(), serial);
}
void ProxyFill(EGxTexCommand command, uint32_t, uint32_t, uint32_t, uint32_t, void*,
               uint32_t& stride, const void*& texels) {
    static uint32_t white[64];
    if (command != GxTex_Latch) return;
    std::fill(std::begin(white), std::end(white), 0xFFFFFFFFu);
    stride = 8 * sizeof(uint32_t); texels = white;
}
bool EnsureProxies(Entry& e, uint32_t index) {
    static const char* names[kProxyCount] = {"fft0", "fft1", "fft2", "fft3", "ripple"};
    for (uint32_t i = 0; i < kProxyCount; ++i) {
        auto& p = e.proxies[i]; p.ref = {&e, i};
        if (!p.texture) {
            char name[64]; std::snprintf(name, sizeof name, "hdForeverField%u_%s", index, names[i]);
            CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 0);
            p.texture = TextureCreate(8, 8, GxTex_Argb8888, GxTex_Argb8888, flags, nullptr, ProxyFill, name, 0);
        }
        if (p.texture && !p.gx) p.gx = TextureGetGxTex(p.texture, 1, nullptr);
        if (!p.gx) return false;
        if (!p.registered) {
            if (!proxy::Register(p.gx, Resolve, &p.ref)) return false;
            p.registered = true;
        }
    }
    return true;
}
void UnregisterAndClose(Entry& e) {
    for (auto& p : e.proxies) {
        if (p.registered && p.gx) proxy::Unregister(p.gx, &p.ref);
        p.registered = false; p.gx = nullptr;
        if (p.texture) HandleClose(reinterpret_cast<HOBJECT>(p.texture));
        p.texture = nullptr; p.ref = {};
    }
}
bool LoadRegistry() {
    if (registryReady) return true;
    if (registryFailed) return false;
    registryFailed = true;
    const char* paths[] = {"ForeverWater\\forever_data\\hd_forever_fft.txt", "..\\..\\ForeverWater\\forever_data\\hd_forever_fft.txt"};
    std::string text;
    for (const char* path : paths) {
        FILE* f = std::fopen(path, "rb"); if (!f) continue;
        if (std::fseek(f, 0, SEEK_END)) { std::fclose(f); continue; }
        const long n = std::ftell(f);
        if (n <= 0 || n > 256 * 1024 || std::fseek(f, 0, SEEK_SET)) { std::fclose(f); continue; }
        text.resize(static_cast<size_t>(n)); const bool ok = std::fread(text.data(), 1, text.size(), f) == text.size();
        std::fclose(f); if (ok) break; text.clear();
    }
    if (text.empty() || !ParsePinnedFft(text, registry)) return false;
    registryReady = true; registryFailed = false; return true;
}
bool ReserveCpu(Entry& e) {
    // Fixed upper bounds for this owner: 4×(64² RGBA16F mip chains), one 256²
    // RGBA16F ripple output. Reserving all output vectors before evaluation
    // prevents growth of retained outputs during a publication transaction.
    if (e.cpuAllocated) return true;
    constexpr uint64_t scratchReserve = 5ull * 1024 * 1024; // one bounded FFT/ripple evaluation plus parsed registry
    constexpr uint64_t expectedEntryBytes = 4ull * (64u*64u + 32u*32u + 16u*16u + 8u*8u + 4u*4u + 2u*2u + 1u) * 8u + 256ull*256u*8u;
    const uint64_t expectedExtra = expectedEntryBytes > e.cpuBytes ? expectedEntryBytes - e.cpuBytes : 0;
    if (cpuReserved + expectedExtra + scratchReserve > kCpuLimit) return false;
    const auto countBytes = [&]() {
        uint64_t bytes = 0;
        for (const auto& cascade : e.fftCpu.cascades)
            for (const auto& mip : cascade) bytes += uint64_t(mip.rgba.capacity()) * sizeof(uint16_t);
        for (const auto& mip : e.rippleCpu) bytes += uint64_t(mip.rgba.capacity()) * sizeof(uint16_t);
        return bytes;
    };
    const auto releaseOutputs = [&]() {
        for (auto& cascade : e.fftCpu.cascades) std::vector<FloatMip>().swap(cascade);
        std::vector<FloatMip>().swap(e.rippleCpu);
        if (cpuReserved >= e.cpuBytes) cpuReserved -= e.cpuBytes;
        else cpuReserved = 0;
        e.cpuBytes = 0;
        e.cpuAllocated = false;
    };
    try {
        for (auto& cascade : e.fftCpu.cascades) {
            cascade.reserve(7); cascade.resize(7);
            for (uint32_t mip = 0; mip < 7; ++mip) {
                const uint32_t side = 64u >> mip;
                cascade[mip].rgba.reserve(size_t(side) * side * 4);
            }
        }
        e.rippleCpu.reserve(1); e.rippleCpu.resize(1);
        e.rippleCpu[0].rgba.reserve(256u * 256u * 4);
    } catch (...) { /* retain and account any partial vector reservation below */ }
    const uint64_t actual = countBytes();
    // Charge observed capacities before any failure return. If they exceed the
    // shared CPU budget, release this entry's partial/full reservation instead.
    if (actual >= e.cpuBytes) cpuReserved += actual - e.cpuBytes;
    else cpuReserved -= std::min(cpuReserved, e.cpuBytes - actual);
    e.cpuBytes = actual;
    if (cpuReserved > kCpuLimit || scratchReserve > kCpuLimit - cpuReserved) {
        releaseOutputs();
        return false;
    }
    bool complete = e.rippleCpu.size() == 1 && e.rippleCpu[0].rgba.capacity() >= 256u*256u*4;
    for (const auto& cascade : e.fftCpu.cascades) {
        complete = complete && cascade.size() >= 7;
        for (uint32_t mip = 0; mip < 7 && complete; ++mip) {
            const uint32_t side = 64u >> mip;
            complete = cascade[mip].rgba.capacity() >= size_t(side) * side * 4;
        }
    }
    e.cpuAllocated = complete;
    return complete;
}
void ClearPublication(Entry& e) { e.publishedSerial = 0; e.metadata = {}; }
bool ReleaseFields(Entry& e, uint64_t completed) {
    if (e.lastResolved > completed) return false;
    for (auto& f : e.fft) if (!f.Release(completed)) return false;
    if (!e.ripple.Release(completed)) return false;
    return true;
}
}

bool Queue(int32_t map, int32_t profile, float planeZ, uint64_t serial, int32_t* usedPlaneQuarterYard) {
    if (!variantsReady || map < 0 || profile <= 0 || !serial) return false;
    const auto q = Quantize(planeZ); if (q == INT32_MIN) return false;
    const Key key{map, profile, q};
    Entry* chosen = nullptr;
    for (auto& e : entries) if (e.occupied && Same(e.key, key)) { chosen = &e; break; }
    if (!chosen) {
        for (auto& e : entries) if (!e.occupied) { chosen = &e; break; }
        if (!chosen) {
            // Recycle only entries nobody has requested for two frames: recycling one that is still drawn made bodies swap between Forever and stock water.
            const auto completed = frame::Completed();
            for (auto& e : entries) if (e.requestSerial + 2 <= serial && e.lastResolved <= completed && ReleaseFields(e, completed)) {
                ClearPublication(e); e.queued = false; e.occupied = false; chosen = &e; break;
            }
        }
        if (chosen) {
            if (!ReserveCpu(*chosen)) return false;
            chosen->key = key; chosen->occupied = true; chosen->lastResolved = 0;
        } else {
            // Table full: share the nearest-height field of the same liquid profile instead of dropping this body to stock water.
            int64_t best = INT64_MAX;
            for (auto& e : entries) if (e.occupied && e.key.map == map && e.key.profile == profile) {
                const int64_t d = std::llabs(static_cast<int64_t>(e.key.planeQuarterYard) - q);
                if (d < best) { best = d; chosen = &e; }
            }
            if (!chosen) return false;
        }
    }
    if (usedPlaneQuarterYard) *usedPlaneQuarterYard = chosen->key.planeQuarterYard;
    chosen->queued = true; chosen->requestSerial = serial;
    static int queueTrace = 0; static uint64_t lastQueueSerial = 0; const bool newQueueSerial = serial != lastQueueSerial; lastQueueSerial = serial; if (newQueueSerial && queueTrace++ < 120) HD::ForeverWater::Trace("queue serial=%llu map=%d profile=%d plane4=%d", static_cast<unsigned long long>(serial), map, profile, q);
    return true;
}

bool PreparePrevious(const api::StageContext& stage, int32_t selectedMap, bool planeValid,
    float planeZ, float cameraX, float cameraY) {
    static unsigned prepCalls = 0; const bool sample = (prepCalls++ % 30) == 0 && prepCalls < 1800;
    if (!variantsReady) {
        if (sample) HD::ForeverWater::Trace("prepare: variants not ready");
        if (stage.serial) DiscardPrevious(stage.serial);
        return false;
    }
    if (!stage.serial || stage.serial != frame::Serial() || !stage.cmd || stage.frameSlot != frame::Slot() ||
        stage.completedSerial > frame::Completed() || frame::Target() || frame::QueryActive()) {
        if (sample) HD::ForeverWater::Trace("prepare: stage rejected serial=%llu cur=%llu cmd=%d slot=%u/%u done=%llu/%llu target=%d query=%d", static_cast<unsigned long long>(stage.serial), static_cast<unsigned long long>(frame::Serial()), stage.cmd ? 1 : 0, static_cast<unsigned>(stage.frameSlot), static_cast<unsigned>(frame::Slot()), static_cast<unsigned long long>(stage.completedSerial), static_cast<unsigned long long>(frame::Completed()), frame::Target() ? 1 : 0, frame::QueryActive() ? 1 : 0);
        if (stage.serial && stage.serial == frame::Serial()) DiscardPrevious(stage.serial);
        return false;
    }
    (void)planeValid; (void)planeZ; // fields are prepared for every queued water height, not only the selected mirror plane
    if (!std::isfinite(cameraX) || !std::isfinite(cameraY)) {
        DiscardPrevious(stage.serial); return false;
    }
    std::array<Entry*, MaxEntries> work{}; uint32_t count = 0;
    for (auto& e : entries) if (e.occupied && e.queued && e.requestSerial + 1 == stage.serial) {
        e.queued = false;
        if (e.key.map == selectedMap) work[count++] = &e;
        else ClearPublication(e);
    }
    { if (sample) HD::ForeverWater::Trace("prepare serial=%llu queuedForThisFrame=%u", static_cast<unsigned long long>(stage.serial), count); }
    if (!count) return false;
    for (uint32_t i = 0; i < count; ++i) ClearPublication(*work[i]);
    uint64_t reserved = 0;
    if ((!preflightReady && !(preflightReady = PreflightFieldOwner64MiB(reserved))) || !LoadRegistry()) return false;
    (void)reserved;
    const auto* waveSpeedVar = CVar::LookupRegistered("hdWaterWaveSpeed");
    const auto* waveAmpVar = CVar::LookupRegistered("hdWaterWaveAmp");
    const double waveSpeed = waveSpeedVar && std::isfinite(waveSpeedVar->m_floatValue) ? std::max(0.0, std::min(10.0, double(waveSpeedVar->m_floatValue))) : 1.0;
    const float waveAmp = waveAmpVar && std::isfinite(waveAmpVar->m_floatValue) ? std::max(0.0f, std::min(4.0f, waveAmpVar->m_floatValue)) : 1.0f;
    const double seconds = double(OsGetAsyncTimeMs()) * 0.001 * waveSpeed;
    // The current CPU adapter is inferred, not a proven copy of Forever's exact
    // wave parameters. It remains invisible unless matching shader variants pass
    // the manifest/high-pixel gate and callers bind this exact serial.
    const auto* host = api::Get(); if (!host) return false;
    bool allPublished = true;
    // With no live ripples every entry's ripple image is the same calm field: build it once and copy it into the others.
    float liveRipples[128 * 8];
    const bool noRipples = WaterRipple::HdActiveRipples(liveRipples, 128) == 0;
    Entry* calmSource = nullptr; float calmUv[4]{};
    for (uint32_t entryIndex = 0; entryIndex < count; ++entryIndex) {
        Entry& item = *work[entryIndex]; float rippleUv[4]{};
        // The FFT only depends on the profile: evaluate it once per profile and copy it to the other heights' entries.
        bool fftOk = false;
        for (uint32_t prev = 0; prev < entryIndex && !fftOk; ++prev)
            if (work[prev]->key.profile == item.key.profile && work[prev]->fftCpu.count >= 1) { item.fftCpu = work[prev]->fftCpu; fftOk = true; }
        if (!fftOk) fftOk = EvaluatePinnedFft(registry, item.key.profile, 0.0, seconds, waveAmp, item.fftCpu);
        const float entryPlane = static_cast<float>(item.key.planeQuarterYard) / static_cast<float>(kQuarterYard);
        bool rippleOk;
        if (noRipples && calmSource) {
            item.rippleCpu = calmSource->rippleCpu; std::memcpy(rippleUv, calmUv, sizeof rippleUv);
            rippleOk = item.rippleCpu.size() == 1;
        } else {
            rippleOk = CollectRippleField(cameraX, cameraY, entryPlane, 1.0f, item.rippleCpu, rippleUv);
            if (rippleOk && noRipples) { calmSource = &item; std::memcpy(calmUv, rippleUv, sizeof calmUv); }
        }
        bool ready = fftOk && rippleOk &&
            item.fftCpu.count >= 1 && item.fftCpu.count <= kCascadeCount && item.rippleCpu.size() == 1;
        for (auto& field : item.fft) ready = ready && field.Initialize(host);
        ready = ready && item.ripple.Initialize(host);
        for (uint32_t i = 0; i < kCascadeCount && ready; ++i)
            ready = item.fft[i].Upload(stage, item.fftCpu.cascades[i]);
        if (ready) ready = item.ripple.Upload(stage, item.rippleCpu);
        if (ready) {
            item.metadata.inverseTile = item.fftCpu.inverseTile;
            item.metadata.rippleUv = {rippleUv[0], rippleUv[1], rippleUv[2], rippleUv[3]};
            item.metadata.rippleStrength = 2.0f; // HLSL applies 0.5; compensates its signed-pair decode.
            item.publishedSerial = stage.serial; // metadata and all fields become visible together
        }
        else allPublished = false;
    }
    if (sample) HD::ForeverWater::Trace("prepare result serial=%llu count=%u allPublished=%d", static_cast<unsigned long long>(stage.serial), count, allPublished ? 1 : 0);
    return allPublished;
}
void DiscardPrevious(uint64_t currentSerial) {
    if (!currentSerial) return;
    for (auto& e : entries) if (e.occupied && e.queued && e.requestSerial + 1 == currentSerial) {
        e.queued = false; ClearPublication(e);
    }
}
void DiscardFrame(uint64_t serial) {
    if (!serial) return;
    for (auto& e : entries) if (e.occupied && e.publishedSerial == serial) ClearPublication(e);
}

bool Binding(const Key& key, uint64_t serial, CGxTex* out[kProxyCount], Metadata& metadata) {
    if (!out || !variantsReady || !serial) return false;
    for (uint32_t i = 0; i < MaxEntries; ++i) {
        auto& e = entries[i]; if (!e.occupied || !Same(e.key, key) || e.publishedSerial != serial) continue;
        if (!EnsureProxies(e, i)) { ClearPublication(e); return false; }
        for (float value : e.metadata.inverseTile) if (!std::isfinite(value) || value < 0.0f) { ClearPublication(e); return false; }
        for (float value : e.metadata.rippleUv) if (!std::isfinite(value)) { ClearPublication(e); return false; }
        if (!std::isfinite(e.metadata.rippleStrength) || e.metadata.rippleStrength <= 0.0f) { ClearPublication(e); return false; }
        for (uint32_t c = 0; c < kProxyCount; ++c) out[c] = e.proxies[c].gx;
        metadata = e.metadata;
        e.lastResolved = std::max(e.lastResolved, serial); return true;
    }
    return false;
}
void SetMatchingVariantReady(bool ready) { variantsReady = ready; }
uint64_t LastUsed() {
    uint64_t last = 0;
    for (const auto& e : entries) {
        last = std::max(last, e.lastResolved);
        for (const auto& field : e.fft) last = std::max(last, field.LastUsed());
        last = std::max(last, e.ripple.LastUsed());
    }
    return last;
}
bool InvalidateMap(int32_t currentMap, uint64_t completed) {
    bool ok = true;
    for (auto& e : entries) if (e.occupied && e.key.map != currentMap) {
        ClearPublication(e); e.queued = false;
        if (e.lastResolved <= completed && ReleaseFields(e, completed)) e.occupied = false;
        else ok = false;
    }
    return ok;
}
bool Shutdown(uint64_t completed) {
    (void)completed; // RetireDeferred queues each resource against its final-use serial.
    variantsReady = false;
    for (auto& e : entries) {
        ClearPublication(e); e.queued = false;
        UnregisterAndClose(e);
        for (auto& field : e.fft) field.RetireDeferred();
        e.ripple.RetireDeferred();
        e.occupied = false;
    }
    for (auto& e : entries) {
        e.fftCpu = {};
        std::vector<FloatMip>().swap(e.rippleCpu);
        e.cpuAllocated = false; e.cpuBytes = 0;
    }
    cpuReserved = 0; preflightReady = false; registryReady = registryFailed = false; registry = {};
    return true;
}
}
