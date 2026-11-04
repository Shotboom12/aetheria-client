// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: the world's sub-pixel jitter for a temporal resolve (FSR/DLSS device tier). Ported from WarcraftXL
// native/render/Jitter.hpp (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL). The jitter shifts a draw's Vulkan viewport
// origin by a sub-pixel offset (8-sample Halton 2/3, one sample per world frame); no projection, constant or culling sees it.
// A draw is jittered when it is inside the world pass, on the back buffer, and (outside the FSR override) its viewport's
// depth range ends below 1. Inert (offsets 0) while nobody asks: Request(true) or SetSample(true,...).
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Vk.hpp"
#include <cstdint>
namespace gxvk::vk::jitter
{
    void Request(bool wanted);                     ///< counted; applies from the next world pass
    bool SetSample(bool enabled, float x, float y);///< the upscaler's own sample (render pixels, |x|,|y| <= .5, not 0,0); false inside the world pass
    void BeginWorld();                             ///< world pass begins: next sample while asked
    void EndWorld();
    void Apply(VkViewport& viewport, bool onBackBuffer); ///< from the draw's viewport site
    void Current(float& x, float& y);
    bool Active();
    void Counts(uint64_t& jittered, uint64_t& skipped);
}
