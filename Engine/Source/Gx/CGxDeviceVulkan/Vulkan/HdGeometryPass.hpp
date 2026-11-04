// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/HdShadowInputs.h"
namespace gxvk::vk::hdgeometry {
bool Available();
// Private opaque replay: no main colour/depth attachment is written.
bool Render();
bool Get(uint64_t serial,HD::VulkanShadows::GeometryInputs& out);
// The six-colour replay read as a G-buffer (colour 0 is the scene colour copy, not exported): albedo RGBA8, normal RGBA16F, light RGBA8 and
// the two shadow endpoints RGBA16F, all GENERAL, valid only for the frame that completed a full replay.
struct GBuffer {
 VkImageView albedo=VK_NULL_HANDLE,normal=VK_NULL_HANDLE,light=VK_NULL_HANDLE,shadowLow=VK_NULL_HANDLE,shadowHigh=VK_NULL_HANDLE;
 VkFormat albedoFormat=VK_FORMAT_UNDEFINED,normalFormat=VK_FORMAT_UNDEFINED,lightFormat=VK_FORMAT_UNDEFINED,shadowFormat=VK_FORMAT_UNDEFINED;
 VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;VkExtent2D extent{};uint64_t serial=0;
};
bool GetGBuffer(uint64_t serial,GBuffer& out);
bool Shutdown();
}
