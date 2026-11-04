// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria FSR object motion, stage F3b-lite: where the units that moved by themselves are on screen this frame.
// The device has no per-object velocity buffer (the full path needs previous bone/instance transforms and velocity outputs in the
// model draw shaders). Until then FSR is told to trust its history less inside a screen rectangle around every unit (players,
// creatures) that moved since the last frame: the reactive mask. Render thread. Azeroth Remastered + native FSR only.
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Vk.hpp"
#include <cstddef>
namespace HD::FsrUnits {
constexpr size_t kMaxRects = 48;
// Visible units (object manager), their motion since the previous call (position change by guid). Call once per world frame.
void Collect();
// The moving units' screen rectangles in render pixels (clipped to `extent`). `view`/`projection` are the world's matrices (row
// vectors, D3D depth), `viewport` the world's viewport. Returns how many were written to `out` (at most `max`).
size_t Rects(const float view[16], const float projection[16], const VkViewport& viewport, VkExtent2D extent, VkRect2D* out, size_t max);
// Units seen and moving at the last Collect (diagnostics).
void Counts(unsigned& visible, unsigned& moving);
}
