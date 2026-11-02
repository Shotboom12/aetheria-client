// SPDX-License-Identifier: GPL-3.0-or-later
// Resource descriptors adapted from WarcraftXL VkRenderApi.h, Copyright (C) 2026 WarcraftXL.
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdScene.hpp"

// Native resource subset for imported grading/bloom/fog effects. No extension loader or WXL backend.
namespace gxvk::vk::hdeffect
{
    constexpr uint32_t ApiVersion = 1;
    enum MemoryClass : uint32_t { DeviceLocal, Host, Stream };
    struct DeviceInfo {
        uint32_t structSize;
        VkInstance instance; VkPhysicalDevice physicalDevice; VkDevice device;
        uint32_t queueFamily, frameSlots;
        PFN_vkGetInstanceProcAddr getInstanceProcAddr;
        PFN_vkGetDeviceProcAddr getDeviceProcAddr;
        VkPipelineCache pipelineCache;
    };
    struct BufferDesc { VkDeviceSize size; VkBufferUsageFlags usage; uint32_t memory; };
    struct BufferInfo { VkBuffer buffer; VkDeviceSize size; void* mapped; };
    struct ImageDesc { VkFormat format; uint32_t width,height,mipLevels; VkImageUsageFlags usage; VkImageAspectFlags aspect; };
    struct ImageInfo { VkImage image; VkImageView view; };
    struct SceneConstants { float projection[16]{}, view[16]{}; };
    struct StageContext {
        uint32_t structSize,point;
        uint64_t serial,completedSerial;
        uint32_t frameSlot,frameSlots;
        VkCommandBuffer cmd;
        VkExtent2D extent;
        VkImage colorImage; VkImageView colorView; VkFormat colorFormat; VkImageLayout colorLayout;
        uint32_t swapRB;
        VkImageView depthView = VK_NULL_HANDLE;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        VkImageLayout depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        const SceneConstants* scene = nullptr;
        VkViewport viewport{};
        uint32_t sceneValid = 0;
        float jitter[2]{};
    };
    struct Api {
        uint32_t apiVersion,structSize;
        int (*GetDeviceInfo)(DeviceInfo*);
        void* (*CreateBuffer)(const BufferDesc*,BufferInfo*);
        void (*RetireBuffer)(void*,uint64_t);
        void (*FlushBuffer)(void*);
        void* (*CreateImage)(const ImageDesc*,ImageInfo*);
        void (*RetireImage)(void*,uint64_t);
    };
    const Api* Get(); // Render thread only, valid while native Vulkan context lives.
    StageContext Stage(const hdscene::Context&, uint32_t point);
}
