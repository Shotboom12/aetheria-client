// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace HD::Fsr::Pass {
// Steps B and C of the integration, on the render thread, around the engine's world scene pass
// (src/native/render/WorldPass.cpp). Both do nothing while Runtime::Mode() is Off.
//
// BeforeWorld: after Runtime::Frame(), BEFORE the HD effects' Resize and the jitter's BeginWorld. Picks the render
//   size from the quality (the SDK's own table), starts the world target (present::WorldTargetBegin) and hands the
//   jitter its FSR sample. The effects then size themselves to the render size through BackBufferInfo.
// AfterWorld: after BeforeUi. Ends the world target, normalizes the depth and makes the camera motion (one compute
//   pass), records the upscale into the native back buffer (or, for hdFSRTest 1 and every frame the upscale cannot
//   be recorded, a plain stretch of the world), before the interface is drawn natively on top.
void MaskBoundary(uint32_t boundary); // R1: 0 capture, 1 compare, 2 capture, 3 compare
void BeforeWorld();
void AfterWorld();
// Render thread, device idle: the pass's own objects and the upscale context (Runtime::Shutdown calls it).
void Shutdown();

// Step D, frame generation (a test, see Runtime::Generate). AfterWorld also copies the HUD-less image, records the
// SDK's prepare and configures AMD's swapchain for the frame.
// BeforePresent: native/Present.cpp, before the frame's submit and present: a frame that was not prepared is
//   configured to be presented alone.
// DropGeneration: AMD's swapchain is being destroyed (device idle, presents drained): the generation context and the
//   HUD-less images go with it.
void BeforePresent();
void DropGeneration();
}
