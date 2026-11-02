#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/CataSceneCapture.h"
namespace HD::NativeWaterCapture {
    void BeforeLiquids();
    const CataWater::SceneCapture::Snapshot* Get(uint64_t serial,VkExtent2D extent);
    const gxvk::vk::texture::Image* SampledImage(uint32_t kind); // 0 colour, 1 raw depth, 2 blur
    bool Shutdown();
}
