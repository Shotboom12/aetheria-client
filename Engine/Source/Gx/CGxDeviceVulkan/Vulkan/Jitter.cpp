// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: see Jitter.hpp. After WarcraftXL native/render/Jitter.cpp (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL).
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Jitter.hpp"
#include <cmath>
namespace
{
    constexpr uint32_t kSamples = 8;
    float Halton(uint32_t i, uint32_t base)
    {
        float f = 1.0f, r = 0.0f;
        for (; i > 0; i /= base) { f /= static_cast<float>(base); r += f * static_cast<float>(i % base); }
        return r;
    }
    struct State
    {
        bool inWorld = false, active = false, override_ = false;
        float overrideX = 0, overrideY = 0;
        uint32_t requests = 0, index = 0;
        float x = 0, y = 0;
        uint64_t jittered = 0, skipped = 0;
    };
    State g;
}
namespace gxvk::vk::jitter
{
    void BeginWorld()
    {
        g.inWorld = true;
        g.active = g.requests > 0;
        if (g.override_) { g.active = true; g.x = g.overrideX; g.y = g.overrideY; return; }
        if (!g.active) { g.x = g.y = 0.0f; return; }
        g.index = g.index % kSamples + 1;
        g.x = Halton(g.index, 2) - 0.5f;
        g.y = Halton(g.index, 3) - 0.5f;
    }
    void EndWorld() { g.inWorld = false; }
    bool SetSample(bool enabled, float x, float y)
    {
        if (g.inWorld) return false;
        if (enabled && (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > .5f || std::abs(y) > .5f || (x == 0 && y == 0))) return false;
        g.override_ = enabled; g.overrideX = enabled ? x : 0; g.overrideY = enabled ? y : 0; return true;
    }
    void Apply(VkViewport& viewport, bool onBackBuffer)
    {
        if (!g.inWorld || !g.active || !onBackBuffer) return;
        // Under the upscaler's override the sky / full-range draws are jittered too (AMD: jitter all rendering).
        if (viewport.maxDepth >= 0.999f && !g.override_) { ++g.skipped; return; }
        viewport.x += g.x; viewport.y += g.y; ++g.jittered;
    }
    void Current(float& x, float& y) { x = g.active ? g.x : 0.0f; y = g.active ? g.y : 0.0f; }
    bool Active() { return g.active; }
    void Request(bool wanted) { if (wanted) ++g.requests; else if (g.requests > 0) --g.requests; }
    void Counts(uint64_t& j, uint64_t& s) { j = g.jittered; s = g.skipped; }
}
