// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HdFsrPolicy.h"
namespace HD::Fsr::Settings {
void Register(); // once after native CVars initialize, before WaystoneHD loads
Options Read(); // render thread, once before starting the world target
void Publish(const Decision&); // requested values remain intact; status reports applied state
// hdFSRTest: 0 normal (Decide), 1 world target + plain stretch (no AMD code), 2 FSR with camera-only motion,
// 3 as 2 with the jitter's sign flipped (diagnostic). Only with hdFSR 1; never archived.
uint32_t TestMode();
bool ObjectMotion();bool RigidMotion();bool SkeletalMotion();bool WindMotion();
uint32_t DebugView();
bool ValidationCapture();
bool ReactiveMasks();float ReactiveScale();float CompositionThreshold();float ReactiveCutoff();bool DebugCheck();
}
