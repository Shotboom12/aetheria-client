// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdTime.cpp; the cvars and their defaults (the donor pack's) are the donor's. hdTime works in every
// visual set; the sun, shadow, fog and day/night smoothing behaviour stays gated on visuals 2.
// Mado's 2026-11-03 update (continuous day/night clock, shadow cache fixes, texel snap, caster fade, sun glare, fog
// features) is merged here behind the same Visuals::AzerothRemastered gate: Stock Vulkan and Lumenveil keep the
// original native behaviour (the accessors below return the original value when not in Azeroth Remastered).
#include "WoW/Source/WorldClient/HD/HdTime.h"

#include <algorithm>
#include <cmath>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Os/TimeManager.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdDayNightMath.h"
#include "WoW/Source/WorldClient/HD/HdWeatherBlend.h"

namespace {
    CVar* s_hdTime = nullptr;
    CVar* s_hdSunPath = nullptr;
    CVar* s_hdShadowSun = nullptr;
    CVar* s_hdShadowBillboard = nullptr;
    CVar* s_hdSunGlareDim = nullptr;
    CVar* s_hdShadowSunMin = nullptr;
    CVar* s_hdShadowSunCacheFix = nullptr;
    CVar* s_hdShadowTexelSnap = nullptr;
    CVar* s_hdShadowCasterFade = nullptr;
    CVar* s_hdFogContinuousFlags = nullptr;
    CVar* s_hdShadowSunRefresh = nullptr;
    CVar *s_daySmooth = nullptr, *s_dayHalfLife = nullptr, *s_dayRate = nullptr, *s_dayWindow = nullptr;
    HD::DayNightMath::Clock s_dayClock;
    HD::WeatherBlendState s_weatherBlend;
    bool s_dayEnabled = false;
    float s_visualDay = 0.0f;
    uint64_t s_dayFrame = 0;

    bool On() {
        return Visuals::Current() == Visuals::AzerothRemastered;
    }

    // Forced minutes since midnight (0 to 1439), or -1 when the override is off.
    int32_t ForcedMinutes() {
        if (!s_hdTime) return -1;  // any visuals: the override is the time of day, not a look
        float hours = s_hdTime->m_floatValue;
        if (hours < 0.0f) return -1;
        if (hours >= 24.0f) hours = 0.0f;
        int32_t minutes = static_cast<int32_t>(hours * 60.0f + 0.5f);
        return minutes >= 1440 ? 0 : minutes;
    }

    bool Flag(CVar* cvar, bool fallback) {
        return cvar ? cvar->m_intValue != 0 : fallback;
    }
}

void HD::RegisterTimeCVars() {
    if (s_hdTime) return;
    s_hdTime = OptionCVars::Register("hdTime", "HD: -1 = real time, 0 to 24 = force this hour on this screen only", "-1");
    s_hdSunPath = OptionCVars::Register("hdSunPath", "HD: 0 = original sun, 1 = the sun sits where the world light comes from (like WoW Forever)", "1");
    s_hdShadowSun = OptionCVars::Register("hdShadowSun", "HD: 0 = original short shadows, 1 = shadows cast at the real sun height (long at dawn and dusk, like WoW Forever)", "1");
    s_hdShadowBillboard = OptionCVars::Register("hdShadowBillboard", "HD: 0 = original tree shadows (turn with the camera), 1 = tree leaf cards face the sun in the shadows (still), 2 = test: leaf cards cast no shadow", "1");
    s_hdSunGlareDim = OptionCVars::Register("hdSunGlareDim", "HD: how much looking at the sun darkens the whole world, 0 to 1 (0 = never: it flickered through the leaves, 1 = original)", "0");
    s_hdShadowSunMin = OptionCVars::Register("hdShadowSunMin", "HD: with hdShadowSun 1, the lowest sun height in degrees above the horizon for the shadow maps (0 = no limit)", "0");
    s_hdFogContinuousFlags = OptionCVars::Register("hdFogContinuousFlags", "HD: 1 blends fog shadow/relative-height features continuously; 0 diagnostic original flags", "1");
    s_hdShadowSunCacheFix = OptionCVars::Register("hdShadowSunCacheFix", "HD: 1 keeps sun direction and cached shadow texture coherent; 0 diagnostic original path", "1");
    s_hdShadowCasterFade = OptionCVars::Register("hdShadowCasterFade", "HD: 1 = bushes and trees stay in the near shadow maps (their shadows no longer blink as you move), 0 = original", "1");
    s_hdShadowTexelSnap = OptionCVars::Register("hdShadowTexelSnap", "HD: 1 = shadow map centres stay on the texel grid (shadows no longer swim when you move), 0 = original", "1");
    s_hdShadowSunRefresh = OptionCVars::Register("hdShadowSunRefresh", "HD: refresh real-sun cached maps after this angle in degrees (.01..5)", "0.5");
    s_daySmooth = OptionCVars::Register("hdDayNightSmooth", "HD: continuous visual day/night clock and palette interpolation; 0 diagnostic native timing", "1");
    s_dayHalfLife = OptionCVars::Register("hdDayNightHalfLife", "HD: visual clock correction half-life in seconds; native weather unchanged", "6");
    s_dayRate = OptionCVars::Register("hdDayNightMaxRate", "HD: maximum visual catch-up rate in game minutes per real second", "10");
    s_dayWindow = OptionCVars::Register("hdDayNightSunBlend", "HD: sun/moon handover width in game minutes", "30");
}

void HD::ApplyTimeOverride(DayNight::DNInfo* info) {
    if (!info) return;
    int32_t minutes = ForcedMinutes();
    ++s_dayFrame;
    if (minutes >= 0) {
        info->m_timeOfDay = static_cast<uint32_t>(minutes);
        info->m_dayProgression = minutes * (1.0f / 1440.0f);
    }
    // Continuous visual clock: Azeroth Remastered only. Elsewhere the native stepped clock is left alone.
    s_dayEnabled = On() && Flag(s_daySmooth, false);
    const float halfLife = s_dayHalfLife && std::isfinite(s_dayHalfLife->m_floatValue) ? std::clamp(s_dayHalfLife->m_floatValue, 0.1f, 120.0f) : 6.0f;
    const float rate = s_dayRate && std::isfinite(s_dayRate->m_floatValue) ? std::clamp(s_dayRate->m_floatValue, 0.1f, 120.0f) : 10.0f;
    s_visualDay = s_dayClock.Update(info->m_dayProgression, OsGetAsyncTimeMs(), DayNight::s_mapId, s_dayEnabled ? halfLife : 0.0f, rate);
    if (s_dayEnabled) info->m_dayProgression = s_visualDay;
}

int32_t HD::OverrideMinutes(int32_t realMinutes) {
    int32_t minutes = ForcedMinutes();
    if (s_dayEnabled && s_dayClock.valid) return int32_t(s_visualDay * 1440.0f);
    return minutes < 0 ? realMinutes : minutes;
}

bool HD::SunFollowsLight() {
    return On() && OptionCVars::Bool(s_hdSunPath, false);
}

float HD::ShadowHeightScale() {
    return On() && OptionCVars::Bool(s_hdShadowSun, false) ? 1.0f : 5.0f;
}

bool HD::ShadowFullCasters() {
    return ShadowHeightScale() < 5.0f;
}

int32_t HD::ShadowBillboardMode() {
    return On() ? OptionCVars::Int(s_hdShadowBillboard, 0, 2, 0) : 0;
}

float HD::SunGlareDim() {
    if (!On() || !s_hdSunGlareDim) return 1.0f;  // original: looking at the sun darkens the world fully
    float dim = s_hdSunGlareDim->m_floatValue;
    return dim < 0.0f ? 0.0f : (dim > 1.0f ? 1.0f : dim);
}

float HD::ShadowMinElevation() {
    if (!On() || !s_hdShadowSunMin) return 0.0f;
    float degrees = s_hdShadowSunMin->m_floatValue;
    return degrees < 0.0f ? 0.0f : (degrees > 60.0f ? 60.0f : degrees);
}

// HD: the render cache reads these once at CShadowQuery::Update, not for every tile/vertex.
bool HD::ShadowCasterFade() { return On() && Flag(s_hdShadowCasterFade, true); }
bool HD::ShadowTexelSnap() { return On() && Flag(s_hdShadowTexelSnap, true); }
bool HD::ShadowSunCacheFix() { return On() && Flag(s_hdShadowSunCacheFix, false); }
float HD::ShadowSunRefreshDegrees() {
    float v = s_hdShadowSunRefresh ? s_hdShadowSunRefresh->m_floatValue : 0.5f;
    return std::isfinite(v) ? std::clamp(v, 0.01f, 5.0f) : 0.5f;
}
float HD::SmoothWeatherBlend(float target, uint32_t now, int32_t mapId) {
    return target;  // HD: native weather is mandatory, even with an old saved nonzero cvar.
}
void HD::ResetWeatherBlend() { s_weatherBlend.Reset(); }

bool HD::SmoothFogFeatures() { return On() && Flag(s_hdFogContinuousFlags, false); }

bool HD::DayNightSmoothingEnabled() { return s_dayEnabled; }
float HD::DayNightBandTime(float ticks) {
    if (s_dayEnabled) return std::isfinite(ticks) ? ticks : 0.0f;
    return float(int32_t(ticks));
}
float HD::DayNightBandTime(float ticks, float nativeRounded) {
    if (s_dayEnabled) return std::isfinite(ticks) ? ticks : 0.0f;
    return nativeRounded;  // preserve even a negative native midpoint-rounding result at midnight.
}
float HD::DayNightBlendWeight() {
    const float width = s_dayWindow && std::isfinite(s_dayWindow->m_floatValue) ? std::clamp(s_dayWindow->m_floatValue, 1.0f, 120.0f) : 30.0f;
    return HD::DayNightMath::DayWeight(s_visualDay, width);
}
void HD::ResetDayNight() {
    s_dayClock.Reset();
    s_dayEnabled = false;
    s_visualDay = 0.0f;
    ++s_dayFrame;
}

uint64_t HD::DayNightFrameSerial() { return s_dayFrame; }
