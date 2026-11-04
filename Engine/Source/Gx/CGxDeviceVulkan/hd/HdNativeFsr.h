// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: camera-only AMD FSR 3.1 upscaling for Azeroth Remastered on the native upscaler seam (FSR device tier, step F2b).
// cvars: hdFsr (0/1, default 0), hdFsrQuality 1 quality / 2 balanced / 3 performance / 4 ultra performance, hdFsrSharpness 0..1.
// Motion vectors come from the depth and the camera matrices only (Mado's HdFsrPrep shader): models that move by themselves ghost
// until the object-motion tier (F3). Built only with -DAETHERIA_NATIVE_FSR=ON (needs ThirdParty\FidelityFX headers; the SDK DLLs are
// loaded from "<exe folder>\FSR" at run time). Without the option every call below is an inline no-op.
#pragma once
#ifdef AETHERIA_NATIVE_FSR
namespace HD::NativeFsr {
// Render thread, in WorldFrame via HD::RenderScale::Begin, after the world viewport is set and before anything is drawn.
// Reads the cvars, (re)creates the upscale context when sizes change, chooses this frame's jitter sample and registers the
// frame's upscaler. Returns the render scale to draw the world at (0 < s < 1), or 0 when FSR is not running this frame.
float BeginWorld();
// The opaque world is drawn (WorldFrame, AR): keeps an opaque-only copy of the render-size colour for the reactive-mask generator (hdFsrReactive 1).
void AfterOpaque();
// Device teardown: unregisters the upscaler, waits for the GPU, destroys the context and the helper images.
bool Shutdown();
}
#else
namespace HD::NativeFsr {
inline float BeginWorld() { return 0.0f; }
inline void AfterOpaque() {}
inline bool Shutdown() { return true; }
}
#endif
