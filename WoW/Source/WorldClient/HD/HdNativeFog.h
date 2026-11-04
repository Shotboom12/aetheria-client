#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Vk.hpp"
namespace HD { struct ForeverFog; }
namespace HD::NativeFog {
void World(); bool Shutdown();
// hd/lumen's read-only views (HdLumenBridge): the shared fog transmittance of this frame, the Forever fog where the camera is, hdFog on.
bool CurrentScatter(uint64_t serial,VkImageView& view,VkExtent2D& extent);
float ForeverFogHere(HD::ForeverFog& fog);
bool ReferenceRequested();
}
