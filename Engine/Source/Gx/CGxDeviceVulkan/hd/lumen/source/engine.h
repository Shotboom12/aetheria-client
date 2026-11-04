// Lumenveil frame-data POD, isolated from every hook and 12340 binary address.
#pragma once
#include <cstdint>
#include "fog_data.h"
namespace HD::Lumen::Source {
struct Viewport {uint32_t X=0,Y=0,Width=0,Height=0;float MinZ=0,MaxZ=1;};
enum class WeatherKind
{
    Clear,
    Rain,
    Snow,
    Sand,
};

constexpr int kMaxCorpseInputs = 16;
constexpr int kMaxUnitInputs = 32;

struct UnitInput
{
    float feet[3];
    float boundingRadius;
    int target;
    uint64_t guid;
};

struct FrameInputs
{
    float cameraRelativeView[16];
    float glProjection[16];
    float camPos[3];
    float camTarget[3];
    Viewport viewport;
    double seconds;
    float dayFraction;
    float toLight[3];
    bool lightIsMoon;
    float sunElevation;
    float moonElevation;
    WeatherKind weather;
    float weatherIntensity;
    uint32_t fogColor;
    uint32_t sunColor;
    uint32_t directColor;
    uint32_t ambientColor;
    float fogStart;
    float fogEnd;
    float zoneFogDistance;
    float farClip;
    float clientFarClip;
    float clientGlowAmount;
    bool inLiquid;
    int mapId;
    int zoneId;
    uint32_t skyHorizonColor;
    uint32_t skyAboveHorizonColor;
    float dryLand;
    float greenLand;
    LightParamsSelection lightParams;
    float indoors;
    bool playerKnown;
    float playerPos[3];
    float playerFacing;
    uint32_t playerMoveFlags;
    uint32_t playerMountDisplay;
    uint32_t playerClass;
    bool targetKnown;
    float targetPos[3];
    float targetBoundingRadius;
    int corpseCount;
    float corpsePos[kMaxCorpseInputs][3];
    uint32_t corpseIds[kMaxCorpseInputs];
    int unitCount;
    UnitInput units[kMaxUnitInputs];
};

namespace engine {constexpr uint32_t kMovementSwimming=0x00200000u;}
}
