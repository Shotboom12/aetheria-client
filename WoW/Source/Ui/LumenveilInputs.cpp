#include "WoW/Source/Ui/WorldFrame.h"

#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Lumenveil/Core/client_world.h"
#include "Engine/Source/Lumenveil/Core/engine.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "WoW/Source/Game/GameTime.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapWeather.h"
#include "WoW/Source/WorldClient/World.h"

#include <algorithm>
#include <cmath>

namespace
{
bool s_clientWorldEnabled = false;
bool s_clientWorldFailed = false;

uint32_t PackColor(const CImVector& color)
{
    return (static_cast<uint32_t>(color.a) << 24) | (static_cast<uint32_t>(color.r) << 16) |
           (static_cast<uint32_t>(color.g) << 8) | static_cast<uint32_t>(color.b);
}

void CopyVector(float out[3], const C3Vector& value)
{
    out[0] = value.x;
    out[1] = value.y;
    out[2] = value.z;
}

// The unit vector from the camera toward a sky body (DayNight::SetPlanets places them around the camera).
// False when the body sits on the camera (not yet placed).
bool DirectionToward(const C3Vector& body, const C3Vector& camera, float out[3])
{
    const float x = body.x - camera.x;
    const float y = body.y - camera.y;
    const float z = body.z - camera.z;
    const float length = std::sqrt(x * x + y * y + z * z);
    if (!(length > 0.0001f) || !std::isfinite(length))
        return false;
    out[0] = x / length;
    out[1] = y / length;
    out[2] = z / length;
    return true;
}

// The client's own sun/moon switch (4:50 to 22:10), as DayNight::SetPlanets uses it.
constexpr float kSunFromDay = 0.2013889f;
constexpr float kSunUntilDay = 0.9236111f;
}

namespace engine
{
StockFog ReadStockFog()
{
    StockFog fog = {};
    if (!g_theGxDevicePtr)
        return fog;

    const float start = g_theGxDevicePtr->m_appRenderStates[GxRs_FogStart].m_value.m_data.f[0];
    float end = g_theGxDevicePtr->m_appRenderStates[GxRs_FogEnd].m_value.m_data.f[0];
    float startValue = start;
    // The device fog state can read start 1 / end 0 (fog switched off by another module). Lumenveil would take that as the
    // client's fog and make its derived layers about 900x denser, so fall back to the day/night fog.
    if (!(end > startValue) || end < 100.0f)
    {
        DayNight::DNInfo* info = DayNight::GetInfo();
        if (info)
        {
            if (info->m_fog.m_end > info->m_fog.m_start && info->m_fog.m_end >= 100.0f)
            {
                startValue = info->m_fog.m_start;
                end = info->m_fog.m_end;
            }
            else
            {
                end = info->m_bands.m_fogEnd > 100.0f ? info->m_bands.m_fogEnd : info->m_farClip;
                startValue = end * info->m_bands.m_fogStartMul;
            }
        }
    }
    for (int i = 0; i < kDayNightFogGroupCount; ++i)
    {
        fog.start[i] = startValue;
        fog.end[i] = end;
    }
    return fog;
}

void WriteStockFog(const StockFog& fog)
{
    if (!g_theGxDevicePtr)
        return;
    // The 3.3.5 client exposes the active world fog as one Gx render-state pair. Keep the
    // saved group-1 values synchronized so the renderer can restore the original state.
    GxRsSet(GxRs_FogStart, fog.start[kFrameInputsFogGroup]);
    GxRsSet(GxRs_FogEnd, fog.end[kFrameInputsFogGroup]);
}

bool CameraInLiquid()
{
    return World::SceneCamLiquidStatus(nullptr) != 0;
}

bool ProbeGroundHeight(void*, float x, float y, float top, float bottom, float& height)
{
    if (!(top >= bottom) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(top) || !std::isfinite(bottom))
        return false;

    const C3Vector query(x, y, top);
    float ground = 0.0f;
    if (!CMap::QueryHeight(query, top - bottom, &ground) || ground > top || ground < bottom)
        return false;
    height = ground;
    return true;
}

bool ReadClientWorld(FrameInputs& out)
{
    CGCamera* camera = CGWorldFrame::GetActiveCamera();
    DayNight::DNInfo* info = DayNight::GetInfo();
    if (!camera || !info || !g_theGxDevicePtr)
        return false;

    CopyVector(out.camPos, info->m_cameraPos);
    out.camTarget[0] = out.camPos[0] + info->m_cameraDir.x * 100.0f;
    out.camTarget[1] = out.camPos[1] + info->m_cameraDir.y * 100.0f;
    out.camTarget[2] = out.camPos[2] + info->m_cameraDir.z * 100.0f;
    out.dayFraction = std::clamp(info->m_dayProgression, 0.0f, 1.0f);
    // out.seconds stays as BuildFrameInputs set it (a monotonic clock); DNInfo::m_timeSec is the glow-fade timer.
    out.farClip = info->m_farClip;
    out.clientFarClip = info->m_farClip;
    out.weatherIntensity = std::clamp(info->m_weatherIntensity, 0.0f, 1.0f);
    // No weather object (some maps) reads as clear weather rather than skipping the frame.
    const uint32_t weatherType = World::s_weather ? World::s_weather->GetType() : 0;
    out.weather = weatherType == 1 ? WeatherKind::Rain : weatherType == 2 ? WeatherKind::Snow :
                  weatherType == 3 ? WeatherKind::Sand : WeatherKind::Clear;
    out.inLiquid = CameraInLiquid();
    out.mapId = World::QueryMapId();
    out.zoneId = CGGameUI::m_areaID;
    out.fogColor = PackColor(info->m_fog.m_color);
    out.fogStart = info->m_fog.m_start;
    out.fogEnd = info->m_fog.m_end;
    // The client's fog can read start 1 / end 0 (no usable range), which makes the derived fog layers about 900x denser.
    // Use the zone's own fog distances then.
    if (!(out.fogEnd > out.fogStart) || out.fogEnd < 100.0f)
    {
        const float zoneEnd = info->m_bands.m_fogEnd > 100.0f ? info->m_bands.m_fogEnd : info->m_farClip;
        out.fogEnd = zoneEnd;
        out.fogStart = zoneEnd * info->m_bands.m_fogStartMul;
    }
    out.zoneFogDistance = info->m_bands.m_fogEnd;
    out.clientGlowAmount = info->m_bands.m_glow;
    out.sunColor = PackColor(info->m_light1.m_diffuse);
    out.directColor = PackColor(info->m_light1.m_diffuse);
    out.ambientColor = PackColor(info->m_light1.m_ambient);
    // DNInfo::m_light1.m_dir is the direction the world light travels (DayNight::SetDirection: always downward), so the
    // fallback toward the light is its negation. Lumenveil wants the sky body itself: its shafts and glow start at the
    // sun or moon disc on screen, and its day/night weights read the real sun and moon heights.
    out.toLight[0] = -info->m_light1.m_dir.x;
    out.toLight[1] = -info->m_light1.m_dir.y;
    out.toLight[2] = -info->m_light1.m_dir.z;
    const float lightLength = std::sqrt(out.toLight[0] * out.toLight[0] + out.toLight[1] * out.toLight[1] +
                                        out.toLight[2] * out.toLight[2]);
    if (lightLength > 0.0001f)
        for (float& component : out.toLight)
            component /= lightLength;
    out.lightIsMoon = !(info->m_dayProgression >= kSunFromDay && info->m_dayProgression <= kSunUntilDay);
    float toSun[3] = {};
    float toMoon[3] = {};
    const bool sunKnown = DirectionToward(DayNight::g_planets.m_sun.m_position, info->m_cameraPos, toSun);
    const bool moonKnown = DirectionToward(DayNight::g_planets.m_moon.m_position, info->m_cameraPos, toMoon);
    out.sunElevation = sunKnown ? toSun[2] : (out.lightIsMoon ? -out.toLight[2] : out.toLight[2]);
    out.moonElevation = moonKnown ? toMoon[2] : (out.lightIsMoon ? out.toLight[2] : -out.toLight[2]);
    const float* body = out.lightIsMoon ? (moonKnown ? toMoon : nullptr) : (sunKnown ? toSun : nullptr);
    if (body)
    {
        out.toLight[0] = body[0];
        out.toLight[1] = body[1];
        out.toLight[2] = body[2];
    }
    out.skyHorizonColor = PackColor(info->m_bands.m_skyColors[0]);
    out.skyAboveHorizonColor = PackColor(info->m_bands.m_skyColors[1]);
    out.dryLand = 1.0f;
    out.greenLand = 1.0f;
    out.indoors = std::clamp(info->m_interiorFogBlend, 0.0f, 1.0f);
    out.lightParams.stormBlend = std::clamp(info->m_stormBlend, 0.0f, 1.0f);

    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();
    out.playerKnown = player != nullptr;
    if (player)
    {
        const C3Vector position = player->GetPosition();
        CopyVector(out.playerPos, position);
        out.playerFacing = player->GetFacing();
        // Movement flags (MOVEMENTFLAG_SWIMMING 0x00200000 = engine::kMovementSwimming), not UNIT_FIELD_FLAGS.
        out.playerMoveFlags = player->m_movementData.m_flags;
    }
    else
    {
        CopyVector(out.playerPos, info->m_playerPos);
        out.playerFacing = info->m_faceAngle;
        out.playerMoveFlags = 0;
    }

    out.playerMountDisplay = 0;
    out.playerClass = 0;
    out.targetKnown = false;
    out.targetPos[0] = out.targetPos[1] = out.targetPos[2] = 0.0f;
    out.targetBoundingRadius = 0.0f;
    out.corpseCount = 0;
    out.unitCount = 0;
    return true;
}
}

ClientClockState GameClock::Read()
{
    const int minute = g_clientGameTime.GetHourAndMinutes();
    return { minute >= 0 && minute < 1440, minute, g_clientGameTime.m_minutesPerSecond };
}

bool GameClock::Set(int minute, float speed)
{
    if (minute < 0 || minute >= 1440 || !std::isfinite(speed) || speed < 0.0f || speed > 60.0f)
        return false;
    WowTime time = g_clientGameTime;
    time.SetHourAndMinutes(minute);
    g_clientGameTime.GameTimeSetTime(time, true);
    g_clientGameTime.GameTimeSetMinutesPerSecond(speed);
    return true;
}

ClientWeatherState GameWeather::Read()
{
    if (!World::s_weather)
        return {};
    const uint32_t type = World::s_weather->GetType();
    const int mapped = type <= 3 ? static_cast<int>(type) : 0;
    return { true, mapped, World::s_weather->m_targetIntensity, World::s_weather->m_intensity };
}

bool GameWeather::Set(int type, float grade, bool instant)
{
    if (!World::s_weather || type < 0 || type > 3 || !std::isfinite(grade) || grade < 0.0f || grade > 1.0f)
        return false;
    const float transition = instant ? 0.0f : 1.0f;
    World::s_weather->SetType(static_cast<uint32_t>(type), grade, nullptr, instant, transition);
    return true;
}

bool ClientWorldMatchesClient()
{
    // This adapter calls the active 3.3.5 source APIs directly; no executable-address patch is used.
    return true;
}

void EnableClientWorld()
{
    s_clientWorldEnabled = true;
    s_clientWorldFailed = false;
}

bool ClientWorldEnabled()
{
    return s_clientWorldEnabled;
}

bool ClientWorldFailed()
{
    return s_clientWorldFailed;
}

int CurrentAreaId()
{
    return CGGameUI::m_areaID;
}
