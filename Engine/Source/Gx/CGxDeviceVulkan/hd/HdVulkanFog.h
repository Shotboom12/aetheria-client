// SPDX-License-Identifier: GPL-3.0-or-later
// HD: standalone Vulkan port of HdFog.cpp's three-layer, 32-step integration.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"

namespace HD::VulkanFog {
// Append this exact point in wxl as documented; there is deliberately no BeforeUi fallback.
constexpr uint32_t Point = 5;
struct alignas(16) Row {float x=0,y=0,z=0,w=0;};
struct alignas(16) Parameters {
    std::array<Row,34> r{}; // exact D3D9 c0..c33
    Row viewport; // normalized offset xy, inverse normalized size zw
    Row extra; // missing projection jitter xy in full-resolution pixels; zero by default
    uint32_t flags[4]{}; // 0: frame working image is BGRA; remaining reserved
};
static_assert(sizeof(Parameters)==592 && offsetof(Parameters,viewport)==544,"std140 fog layout");
struct ShadowMaps {
    uint64_t serial=0;
    VkImageView view[3]{};
    VkImageLayout layout[3]{};
    bool comparison=false; // depth maps: linear hardware compare; raw R32F: point compare in shader
};
// Fill projection, camera rotation and viewport from THIS frame; keep layer/lighting rows.
// Refuses reversed-Z/orthographic projections instead of silently changing the D3D9 formula.
bool Prepare(const gxvk::vk::hdeffect::StageContext& ctx,Parameters& parameters);
float Phase(float cosine,float g);
float TonePeak(float peak);
class Effect {
public:
    bool Init(const gxvk::vk::hdeffect::Api* api);
    bool Resize(uint32_t width,uint32_t height);
    bool Record(const gxvk::vk::hdeffect::StageContext& ctx,const Parameters& parameters,const ShadowMaps& maps={});
    bool Shutdown(uint64_t completedSerial);
    uint64_t LastUsed() const;
    // This frame's fog transmittance image (RGBA16F, GENERAL), for hd/lumen's halos; false when this frame's pass did not share it.
    bool CurrentScatter(uint64_t serial,VkImageView& view,VkExtent2D& extent) const;
    const char* Error() const;
    Effect()=default;
    Effect(const Effect&)=delete;
    Effect& operator=(const Effect&)=delete;
private:
    struct Impl;
    Impl* state_=nullptr;
};
}
