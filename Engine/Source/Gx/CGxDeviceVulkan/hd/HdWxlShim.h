// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: the WarcraftXL render-API table (subset, compat/wxl/VkRenderApi.h) implemented over the native hdscene boundary,
// so hd/lumen's GPU executor runs on Aetheria's own Vulkan device. Render thread only. No wxl-vulkan-api code is used.
//
// What it is: stages registered at a point run when the native glue reaches that point (HdLumenNative: after the opaque
// world, after the world before the spells, bloom). What it is not: wxl's render-pass points. Every stage runs outside any
// pass on the compute boundary, with a copy of the scene colour in GENERAL, and what it writes is blitted back.
// Known differences from wxl, all on the safe side: no G-buffer (GetGBuffer returns 0, so the lamp surfaces keep the
// reference lighting), no separate opaque depth (hd/lumen then copies the depth itself, its own fallback), no jitter.
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"
#include <cstdint>
struct WXL_VkRenderApi;
namespace HD::WxlShim {
// The table, or nullptr while the native Vulkan context is not live.
const WXL_VkRenderApi* Api();
// Does any registered stage of `point` want to run this frame (its Wanted callback)?
bool Wanted(uint32_t point);
// Runs the point's stages in `order`, with the native stage context. Returns how many ran.
uint32_t Run(uint32_t point, const gxvk::vk::hdeffect::StageContext& native);
uint32_t StageCount();
// How many times a consumer asked for the G-buffer or the opaque depth (the native device has neither; diagnostics only).
uint32_t UnservedRequests();
// A consumer holds a G-buffer request (served from the six-colour geometry replay, Vulkan/HdGeometryPass): HdLumenNative then runs the replay.
bool GBufferRequested();
}
