#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Vk.hpp"
#include <cstdint>
namespace HD::VulkanShadows {
constexpr uint32_t ShadowPoint=9; // Native private opaque-world boundary, no extension ABI.
// Must be supplied by actual geometry endpoint attachments. No depth-normal approximation
// can claim this producer contract; leave valid=0 until the full provider exists.
struct GeometryInputs {
 uint32_t structSize=sizeof(GeometryInputs),valid=0;
 uint64_t serial=0;
 uint32_t shadowEndpointContract=0;
 VkImageView shadowLowView=VK_NULL_HANDLE,shadowHighView=VK_NULL_HANDLE,normalView=VK_NULL_HANDLE;
 VkFormat shadowEndpointFormat=VK_FORMAT_UNDEFINED;
 VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
 VkExtent2D extent{};
};
}
