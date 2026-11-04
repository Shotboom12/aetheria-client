// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: Azeroth Remastered render scale (FSR device tier, step F1). `hdRenderScale` 0.25..1 (default 1 = off, exactly the old frame):
// the 3D world draws into a smaller scene image and is stretched (bilinear) onto the full scene before the world text and the
// interface, which stay at native resolution. Azeroth Remastered + Vulkan only; render thread; called from WorldFrame.
#pragma once
namespace HD::RenderScale {
// After the world's viewport is set, before anything of the world is drawn. Only a full-window world viewport is scaled.
void Begin();
// After the world (and its HD passes, outline and loot beam), before the world text and the interface.
void End();
}
