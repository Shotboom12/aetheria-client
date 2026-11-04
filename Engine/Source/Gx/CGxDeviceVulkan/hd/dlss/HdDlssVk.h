// SPDX-License-Identifier: GPL-3.0-or-later
// HD 2026-11-02 (DLSS): the Vulkan extensions NVIDIA NGX asks for, added where the instance and the device are
// created (wxl/src/vulkan/context/Instance.cpp and Logical.cpp). vulkan.h must already be included (vulkan/Vk.hpp).
// Only extensions the loader / adapter really offer are added, never one twice: a machine without them (AMD, old
// driver) gets exactly the device it had, and DLSS reports itself unavailable. WAYSTONE_DLSS_EXT=0 skips both.
#pragma once
#include <cstdint>
#include <vector>
namespace HD::Dlss {
void AddInstanceExtensions(const std::vector<VkExtensionProperties>& available, std::vector<const char*>& names);
void AddDeviceExtensions(PFN_vkEnumerateDeviceExtensionProperties enumerate, VkInstance instance, VkPhysicalDevice physical,
                         const char** names, uint32_t& count, uint32_t capacity);
}
