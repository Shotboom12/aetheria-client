// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/HdShadowInputs.h"
namespace HD::VulkanShadows {
struct Settings {
 bool enabled=false, contact=true; uint32_t quality=1, debug=0;
 float strength=1, sunAngle=.00465f, maxPenumbra=1.5f, normalBias=.015f;
 float contactLength=1.5f, contactThickness=.08f, contactStart=.035f, contactStrength=1.0f, tagTolerance=.03f, grassShade=1.0f;
 float extraJitterX=0,extraJitterY=0;
};
struct NativeMaps {
 VkImageView views[5]{};VkImageLayout layouts[5]{};
 const void* images[5]{}; // HD: wxlvk image, layout re-read at Record (a map can still be a render target at capture)
 uint32_t captured=0,late=0;
 float rows[15][4]{},meta[5][4]{},towardLight[4]{};
 uint64_t serial=0;
};
struct alignas(16) Constants {
 float screen[4],view[4],rect[4],projection[4],depth[4],sun[4],settings[4],contact[4],jitter[4];
 float rows[15][4],maps[5][4];
};
static_assert(sizeof(Constants)==464,"GLSL UBO; no x86 offsets");
inline float Penumbra(float receiver,float blocker,float angularTan,float radius) {
 return std::max(0.0f,receiver-blocker)*angularTan/std::max(2*radius,.001f);
}
inline float DepthTag(float z) {return .505f+.49f*std::clamp((std::log2(std::max(z,.0625f))+4)/16,0.0f,1.0f);}
bool BuildConstants(const gxvk::vk::hdeffect::StageContext&,const Settings&,const NativeMaps&,Constants&);
class Effect {
public:
 bool Init(const gxvk::vk::hdeffect::Api*);
 bool Resize(uint32_t,uint32_t);
 bool Record(const gxvk::vk::hdeffect::StageContext&,const Settings&,const NativeMaps&,const GeometryInputs&);
 bool Shutdown(uint64_t completedSerial);
 uint64_t LastUsed()const;uint64_t ImageAllocations()const;const char*Error()const;
 Effect()=default;Effect(const Effect&)=delete;Effect&operator=(const Effect&)=delete;
private:struct Impl;Impl*state_=nullptr;
};
}
