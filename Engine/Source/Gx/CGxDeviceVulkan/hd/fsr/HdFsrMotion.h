// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "wxl/VkRenderApi.h"
#include <cstdint>
namespace HD::Fsr::ObjectMotion {
struct Frame {VkCommandBuffer cmd{};uint64_t serial=0,completed=0;uint32_t slot=0;VkExtent2D extent{};bool timing=false;};
struct Stats {double gpuMilliseconds[2]{};uint64_t gpuSamples[2]{};};
class Effect {
public:
 bool Init(const WXL_VkRenderApi*);
 // Both images are GENERAL, compute writes to camera motion made visible here. Marked xy REPLACES camera xy.
 bool Compose(const Frame&,VkImageView objectMotion,VkImageView cameraMotion,bool reset);
 // Camera motion is SHADER_READ_ONLY on entry/exit, output GENERAL; output fully written at native extent.
 bool Debug(const Frame&,VkImage motionImage,VkImageView motionView,VkImageView output,float scale=32.f);
 // Diagnostic: object marker image (GENERAL) shown at native size; unmarked dark, still cyan, moving red->yellow.
 bool DebugObjects(const Frame&,VkImageView objectMotion,VkImageView output,float scale=512.f);
 bool Shutdown(uint64_t completed);
 uint64_t LastUsed()const;const char* Error()const;
 Stats ReadStats(bool reset=false);
 Effect()=default;Effect(const Effect&)=delete;Effect&operator=(const Effect&)=delete;
private:struct Impl;Impl*state_=nullptr;
};
}
