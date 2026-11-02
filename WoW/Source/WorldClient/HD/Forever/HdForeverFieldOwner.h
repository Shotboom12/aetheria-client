#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"
#include "WoW/Source/WorldClient/HD/Forever/HdForeverFftCpu.h"
#include "WoW/Source/WorldClient/HD/Forever/HdForeverRippleCpu.h"
#include <array>

class CGxTex;
namespace HD::ForeverNative::FieldOwner {
constexpr uint32_t MaxEntries = 12; // was 4: a bounded budget, not a 64-bit limit; each entry costs ~0.7 MiB CPU and 5 small GPU textures
struct Key { int32_t map = -1, profile = -1, planeQuarterYard = 0; };
struct Metadata {
    std::array<float, 4> inverseTile{};
    std::array<float, 4> rippleUv{};
    float rippleStrength = 0.0f;
};
// Request admission is bounded. A false return means the caller must keep stock water.
// When the table is full the nearest-height entry of the same liquid profile is shared; usedPlaneQuarterYard receives the key's plane to bind with.
bool Queue(int32_t map, int32_t profile, float planeZ, uint64_t bindSerial, int32_t* usedPlaneQuarterYard = nullptr);
// Call only inside the valid capture callback, with that callback's current stage context.
// Drains requests from serial-1 only, after checking the selected map and plane.
bool PreparePrevious(const gxvk::vk::hdeffect::StageContext& stage, int32_t selectedMap,
    bool planeValid, float selectedPlaneZ, float cameraX, float cameraY);
void DiscardPrevious(uint64_t currentSerial);
void DiscardFrame(uint64_t serial);
// Returns proxies and constants from the same exact published key/frame serial.
bool Binding(const Key& key, uint64_t serial, CGxTex* out[5], Metadata& metadata);
// Off by default; true only after both bytecode shaders pass existing manifest/high-pixel checks.
void SetMatchingVariantReady(bool ready);
uint64_t LastUsed();
// Invalidates pending work/publication and retires only resources safe at completedSerial.
bool InvalidateMap(int32_t currentMap, uint64_t completedSerial);
bool Shutdown(uint64_t completedSerial);
}
