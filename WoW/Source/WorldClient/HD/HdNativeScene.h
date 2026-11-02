#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"
namespace HD::NativeScene {
    struct Snapshot {
        gxvk::vk::hdeffect::SceneConstants constants{};
        VkViewport viewport{};
        uint64_t serial=0;
    };
    void Capture();
    bool Get(uint64_t serial, Snapshot& out);
}
