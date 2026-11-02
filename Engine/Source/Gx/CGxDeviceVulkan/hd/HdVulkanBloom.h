// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"
namespace HD::VulkanBloom {
// Append after the fog/grade reservations, but execute BEFORE Grade. See INTEGRATION.md.
constexpr uint32_t Point=7,PrivatePoint=6;
struct alignas(16) Row {float x=0,y=0,z=0,w=0;};
struct alignas(16) Constants {Row target,a,b,c;uint32_t flags[4]{};};
static_assert(sizeof(Constants)==80 && offsetof(Constants,flags)==64,"GLSL bloom push constants");
struct Settings {
    bool bloom=false,rays=false;int32_t debug=0;
    float threshold=.7f,strength=1,rayStrength=1,farClip=1000;
    float sunDirection[3]={0,0,1},cameraForward[3]={1,0,0};
    float extraJitterX=0,extraJitterY=0; // only a missing projection/viewport offset, in full-size pixels
};
struct Post {float u=.5f,v=.5f,visible=0,minDepth=0,maxDepth=.94f,c2=0,d2=0,farClip=1000;};
bool BuildPost(const gxvk::vk::hdeffect::StageContext& ctx,const Settings& settings,Post& post);
bool IsSky(float raw,const Post& post);
float BrightScale(float luminance,float threshold);
std::array<float,3> ScreenBlend(std::array<float,3> scene,std::array<float,3> light);
class Effect {
public:
    Effect()=default;Effect(const Effect&)=delete;Effect& operator=(const Effect&)=delete;
    bool Init(const gxvk::vk::hdeffect::Api* api);
    bool Resize(uint32_t width,uint32_t height);
    bool Record(const gxvk::vk::hdeffect::StageContext& ctx,const Settings& settings);
    bool Shutdown(uint64_t completedSerial);
    uint64_t LastUsed() const;
    uint64_t ImageAllocations() const;
    const char* Error() const;
private:
    struct Impl;Impl* state_=nullptr;
};
}
