// SPDX-License-Identifier: GPL-3.0-or-later
// HD: the Cata water's dynamic displacement map under Vulkan (tranche 2): the water that answers the characters,
// the rain and the moving ships. Cata Liquid::UpdateDisplMap and its passes (WoW/Source/LiquidSystem/
// LiquidProcTextures.cpp of the 15595 client), at Cata's best ripple detail: two maps of 1024 x 1024 texels (RG16F:
// x the height now, y the one before), 128 x 128 yards around the player, stepped every 25 and 50 ms. Each step adds
// the perturbations (perturbTex quads, ship hulls) to the map and propagates it into its twin (a wave equation).
//
// A BeforeLiquids stage with its own passes. It reads what the engine gathered (GxCataRipples, GxSeam.h) and says
// where the maps are; the two maps are handed to the water through proxy textures (slots 5 and 6).
#pragma once

#include <cstdint>
#include <vector>

#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"

namespace HD::CataWater {
class Ripples {
public:
    bool Initialize(const gxvk::vk::hdeffect::Api* api);
    void Enable(bool enabled);
    void Reset();
    void StopAndDestroy();   ///< after the GPU is idle (CataWaterService.cpp)
    const char* Error() const { return error_; }
    void Record(const gxvk::vk::hdeffect::StageContext& ctx);
    const gxvk::vk::texture::Image* SampledImage(uint32_t map,uint64_t serial) const;
    bool Recorded(uint64_t serial) const { return serial && publishedSerial_==serial; }
    uint64_t LastUsed() const { return lastSerial_; }
    uint64_t Steps() const { return steps_; }

private:
    static constexpr uint32_t kMaps = 2;            // Cata GetRippleDetail() > 1
    static constexpr uint32_t kTexSize = 1024;
    struct Image {
        void* handle = nullptr;
        gxvk::vk::hdeffect::ImageInfo info{};
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet source = VK_NULL_HANDLE;    ///< this image as the propagation's source
    };
    struct Stream {
        void* handle = nullptr;
        gxvk::vk::hdeffect::BufferInfo info{};
        uint64_t lastSerial = 0;
    };
    struct Vertex { float x, y, z, u, v; };

    bool Pipelines();
    bool Maps();
    bool Texture(VkCommandBuffer cmd, uint64_t serial);
    void Retire(uint64_t serial);
    void SetDisplMapInfo(const float center[3]);
    void Clear(VkCommandBuffer cmd);
    void Quads(const float* list, uint32_t count, uint32_t map, uint32_t time, std::vector<Vertex>& out) const;
    void Meshes(uint32_t map, uint32_t time, std::vector<Vertex>& out) const;
    void Perturb(VkCommandBuffer cmd, uint32_t map, const std::vector<Vertex>& quads, const std::vector<Vertex>& mesh);
    void Propagate(VkCommandBuffer cmd, uint32_t map);
    void BeginPass(VkCommandBuffer cmd, uint32_t image, bool clear);

    const gxvk::vk::hdeffect::Api* api_ = nullptr;
    gxvk::vk::hdeffect::DeviceInfo device_{};
    bool enabled_ = false;
    const char* error_ = "";
    VkRenderPass load_ = VK_NULL_HANDLE, clear_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline propagate_ = VK_NULL_HANDLE, perturb_ = VK_NULL_HANDLE, mesh_ = VK_NULL_HANDLE;
    VkShaderModule modules_[5] = {};
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkSampler point_ = VK_NULL_HANDLE, linear_ = VK_NULL_HANDLE;
    VkDescriptorSet textureSet_ = VK_NULL_HANDLE;
    Image images_[2 * kMaps];
    void* textureHandle_ = nullptr;
    gxvk::vk::hdeffect::ImageInfo texture_{};
    bool textureReady_ = false;
    std::vector<Stream> streams_;                   ///< per frame slot: the perturbations' vertices
    uint64_t lastSerial_ = 0, steps_ = 0;

    // Cata's state (LiquidProcTextures.cpp statics).
    uint64_t publishedSerial_ = 0;
    bool dirty_ = true;                             // Liquid::s_displMapDirty
    uint32_t current_[kMaps] = {0, 2};              // s_displMapCurrent: the image each map is in
    uint32_t period_[kMaps] = {25, 50};             // s_displMapPeriod
    uint32_t lastTime_[kMaps] = {};                 // s_displMapLastTime
    float interpolation_[kMaps] = {};               // s_displMapInterpolation
    float pos_[kMaps][3] = {}, rawPos_[kMaps][3] = {};
    float size_[kMaps][2] = {};
    bool started_ = false;
    uint32_t startTime_ = 0;                        // PerturbDisplMap's s_startTime
};
}
