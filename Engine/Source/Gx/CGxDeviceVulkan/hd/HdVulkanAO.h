// SPDX-License-Identifier: GPL-3.0-or-later
// HD: XeGTAO AO module; existing engine entry points unchanged. Defaults off.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"

namespace HD::VulkanAO {
constexpr uint32_t Point=0;
constexpr float GBufferTagTolerance=.03f;
struct Settings {
    bool enabled = false, halfResolution = false, debug = false, useGBufferNormals = false;
    float radius = 1.0f, strength = .85f;
    uint32_t quality = 1; // HD: XeGTAO low 1x2, medium 2x2, high 3x3, ultra 9x3
    bool temporal = false; // HD (Claude): accumulate over frames with camera reprojection (cvar default on)
    // Extra PROJECTION jitter only, in full-resolution pixels. ctx.viewport already includes
    // wxl's placement jitter and D3D9 centre shift; never pass either a second time here.
    float extraJitterX = 0, extraJitterY = 0;
};
struct alignas(16) Constants {
    float screen[4], view[4], rect[4], depth[4], params[4], blur[4], full[4];
    uint32_t flags[4]; // pass, debug, valid G-buffer normals, half-resolution
};
static_assert(sizeof(Constants) == 128 && offsetof(Constants,flags) == 112, "GLSL push layout");
inline float ClampSetting(float v, float fallback, float lo, float hi) {
    return std::isfinite(v) ? std::clamp(v,lo,hi) : fallback;
}
inline bool BuildConstants(const gxvk::vk::hdeffect::StageContext& ctx, const Settings& settings, Constants& out) {
    if (!ctx.sceneValid || !ctx.scene || !ctx.extent.width || !ctx.extent.height || ctx.extent.width > 16384 ||
        ctx.extent.height > 16384 || !std::isfinite(settings.extraJitterX) || !std::isfinite(settings.extraJitterY)) return false;
    const float* p = ctx.scene->projection;
    for (uint32_t i = 0; i < 16; ++i) if (!std::isfinite(p[i])) return false;
    const auto& vp = ctx.viewport;
    if (!std::isfinite(vp.x) || !std::isfinite(vp.y) || !std::isfinite(vp.width) || !std::isfinite(vp.height) ||
        !std::isfinite(vp.minDepth) || !std::isfinite(vp.maxDepth) || vp.width <= 0 || vp.height <= 0 ||
        vp.maxDepth <= vp.minDepth || p[0] == 0 || p[5] == 0 || p[11] != 1 || p[15] != 0 || p[10] <= 0 || p[14] >= 0)
        return false; // D3D9's perspective + forward-Z contract; fail closed on reversed/orthographic depth.
    const uint32_t width = settings.halfResolution ? (ctx.extent.width + 1) / 2 : ctx.extent.width;
    const uint32_t height = settings.halfResolution ? (ctx.extent.height + 1) / 2 : ctx.extent.height;
    Constants c{};
    c.screen[0] = 1.0f / float(width); c.screen[1] = 1.0f / float(height);
    c.screen[2] = 1.0f / (vp.maxDepth - vp.minDepth); c.screen[3] = -vp.minDepth * c.screen[2];
    c.view[0] = 2 / p[0]; c.view[1] = -2 / p[5]; c.view[2] = (-1-p[8]) / p[0]; c.view[3] = (1-p[9]) / p[5];
    c.rect[0] = vp.x / float(ctx.extent.width); c.rect[1] = vp.y / float(ctx.extent.height);
    c.rect[2] = float(ctx.extent.width) / vp.width; c.rect[3] = float(ctx.extent.height) / vp.height;
    c.depth[0] = p[10]; c.depth[1] = -p[14]; c.depth[2] = .99999f; c.depth[3] = GBufferTagTolerance;
    c.params[0] = ClampSetting(settings.radius,1.0f,.25f,6); c.params[1] = ClampSetting(settings.strength,.85f,0,3);
    c.params[2] = 2.2f; c.params[3] = 3.3f; // HD: upstream final power and depth mip offset
    c.blur[2] = 150; c.blur[3] = .01f;
    c.full[0] = 1.0f / float(ctx.extent.width); c.full[1] = 1.0f / float(ctx.extent.height);
    c.full[2] = settings.extraJitterX; c.full[3] = settings.extraJitterY;
    c.flags[1] = settings.debug; c.flags[3] = settings.halfResolution;
    const float* rows[] = {c.screen,c.view,c.rect,c.depth,c.params,c.blur,c.full};
    for (auto row : rows) for (uint32_t i = 0; i < 4; ++i) if (!std::isfinite(row[i])) return false;
    out = c; return true;
}

// Owns pipelines, descriptors and five R32 depth levels and two RGBA8 AO/edges images per frame slot, plus two shared
// RGBA16F history images (ping-pong across frames; queue order + the per-frame barrier keep them safe). Render-thread only.
// Init/Resize allocate; Record performs NO allocations, waits or queue submissions.
// Explicit Shutdown is required before the Vulkan device goes away. No implicit GPU destruction.
class Effect {
public:
    bool Init(const gxvk::vk::hdeffect::Api* api);
    bool Resize(uint32_t fullWidth, uint32_t fullHeight, bool halfResolution = false);
    bool Record(const gxvk::vk::hdeffect::StageContext& ctx, const Settings& settings);
    bool Shutdown(uint64_t completedSerial);
    void ResetHistory(); // Owner calls on map changes; no allocation or GPU destruction.
    uint64_t LastUsed() const;
    uint64_t ImageAllocations() const;
    const char* Error() const;
    Effect() = default;
    Effect(const Effect&) = delete;
    Effect& operator=(const Effect&) = delete;
private:
    struct Impl;
    Impl* state_ = nullptr;
};

}
