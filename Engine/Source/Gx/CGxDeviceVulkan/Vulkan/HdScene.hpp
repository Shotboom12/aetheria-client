#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"

// Aetheria-native compute boundary. Does not replace the renderer or process UI/present implicitly.
namespace gxvk::vk::hdscene
{
    struct Context
    {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkExtent2D extent{};
        uint32_t slot = 0;
        uint64_t serial = 0, completed = 0;
    };
    // The callback records compute commands only; the supplied RGBA8 image is in GENERAL.
    // Prepare pipelines/descriptors/LUTs first. A false result discards the scratch result.
    using Record = bool (*)(const Context&, void*);

    // Read-only capability gate; no commands/resources are created.
    bool Supported();
    // scratch must come from texture::CreateStorage, match the scene extent, and be owned by caller.
    // Caller retires it using frame::RetireImage at its last used serial, including failed callbacks.
    // Call at the world boundary before chat bubbles/UI; offscreen previews and active queries skip.
    // No allocation, blocking wait, submit, or frame presentation occurs here.
    bool Apply(texture::Image& scratch, Record record, void* user, bool writeBack = true);
}
