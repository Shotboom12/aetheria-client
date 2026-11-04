// SPDX-License-Identifier: GPL-3.0-or-later
// HD 2026-11-02 (DLSS): the DLSS console variables. No Vulkan here (included next to ConsoleVar.h).
#pragma once
#include <cstdint>
namespace HD::Dlss::Settings {
void Register();          // from HD::Fsr::Settings::Register, once the native CVars exist
bool Requested();         // hdDLSS 1: DLSS Super Resolution replaces the FSR upscaler (default 0)
uint32_t Preset();        // hdDLSSPreset: 0 NVIDIA default, otherwise NVSDK_NGX_DLSS_Hint_Render_Preset value (10 J, 11 K, 12 L, 13 M)
float Sharpness();         // hdDLSSSharpness 0..2 (default 1): 0..1 FSR's RCAS curve, above 1 reinforced past FSR's maximum
bool JitterFlip();        // hdDLSSJitterFlip 1: jitter sign inverted (diagnostic, not archived)
uint32_t LogLevel();      // hdDLSSLog: NGX messages into the journal, 0 off / 1 on (default) / 2 verbose
void Publish(const char* status); // hdDLSSInfo (not archived), written only when it changes
}
