// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HdFsrPolicy.h"
namespace HD::Fsr {
class Core;
namespace Runtime {
// Step A of the integration: the AMD runtime is loaded and asked for its providers on the
// device the game created, the device's features and formats are checked, and both statuses say what stands.
// Nothing here changes an image: no context is created, no dispatch is recorded.
void Startup();  // render thread, once the Vulkan device exists (CGxDeviceVulkan::DeviceCreate)
void NoteMasksReady(bool ready); // render thread; current frame only, reset by Frame()
void Frame();    // render thread, once per world frame before BeginWorld: reads the requests, publishes the statuses
void Shutdown(); // render thread, before the device is destroyed (CGxDeviceVulkan::DeviceDestroy); idempotent
// What is known so far, for the upscaler and for frame generation separately (their providers differ).
const Readiness& UpscaleReadiness();
const Readiness& GenerationReadiness();
// The decision of the latest Frame(): what is applied, never just what is asked.
const Decision& Applied();

// Step B / C (HdFsrPass.cpp): what the world pass does this frame. Decided by Frame() from the requests, the
// readiness and hdFSRTest; a test mode is always reported as a test in hdFSRInfo, never as FSR "actif".
enum class Upscale { Off, Stretch, CameraOnly, CameraOnlyFlipped, Full };
Upscale Mode();
Options Requested();   // the latest requests Frame() read
Core* Sdk();           // null until the runtime is loaded
// The pass could not do what Mode() asked this frame (static text): hdFSRInfo says so, the frame falls back.
void NoteFailure(const char* why);

// Step D (HdFsrPass.cpp, the FSR swapchain): frame generation this frame -- only as hdFSRTest 2/3 with
// hdFSRFrameGeneration 1, once AMD's swapchain stands (the swapchain is rebuilt through it when asked for).
bool Generate();
// The generation could not run this frame (static text): hdFSRFrameGenerationInfo says so until a request changes.
void NoteGenerationFailure(const char* why);
// The generation ran (rendered frames / presents counted by AMD's swapchain over the last interval): the status
// shows both, the generated ones being presents minus rendered frames -- never a faster simulation.
void NoteGenerationRate(double renderedPerSecond, double shownPerSecond);
// HD 2026-11-01 (Q1): share of the rendered frames whose object motion (M1) was composed, measured by the pass
// over 120 frames; negative = M1 not asked. The statuses say it instead of "camera only".
void NoteObjectMotion(double percent);
}
}
