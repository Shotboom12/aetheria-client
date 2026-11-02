#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdForeverWaterImages.hpp"
namespace HD::ForeverNative {
// Requires the isolated read-only WaterRipple::HdActiveRipples hunk. Uses the
// existing pool state, which stock RenderWaterRipples last advanced; no second tick.
// Output packed RGBA: +slopeX,-slopeX,+slopeY,-slopeY (twice signed magnitude).
bool CollectRippleField(float cameraX, float cameraY, float planeZ, float strength,
    std::vector<FloatMip>& field, float worldToUv[4]);
}
