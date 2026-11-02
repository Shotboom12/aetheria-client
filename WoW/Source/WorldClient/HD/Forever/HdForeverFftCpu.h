// Review candidate. Pinned numeric data; inferred spectrum, not exact Forever simulation.
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdForeverWaterImages.hpp"
#include <map>
#include <string>
namespace HD::ForeverNative {
struct FftTile { float size=0, seed=0, wind=0, gamma=0, group[2][3]{}; };
struct FftRegistry {
    std::map<int32_t,FftTile> tiles;
    std::map<int32_t,std::vector<int32_t>> links;
};
bool ParsePinnedFft(const std::string& exactText, FftRegistry& registry);
struct FftField {
    std::array<std::vector<FloatMip>,4> cascades;
    std::array<float,4> inverseTile{};
    uint32_t count=0;
};
// Pure CPU reference adapter. Caller schedules once/profile/frame, not per draw.
// Spectrum caching/budgeted admission belongs in the future owning service.
// Unmapped profiles fail; no hand-made lake/ocean fallback.
bool EvaluatePinnedFft(const FftRegistry&, int32_t profile, double headingRadians,
    double seconds, float amplitude, FftField& output);
}
