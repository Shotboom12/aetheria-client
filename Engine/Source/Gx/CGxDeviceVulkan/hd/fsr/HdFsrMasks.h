// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "wxl/VkRenderApi.h"
#include <cstdint>
namespace HD::Fsr::Masks {
struct Image {VkImage image{};VkImageView view{};VkFormat format{};VkExtent2D extent{};};
struct Frame {VkCommandBuffer cmd{};uint64_t serial=0,completed=0;uint32_t slot=0;};
struct Stats {uint64_t captures=0,comparisons=0,ready=0,rejected=0,resizes=0;};
class Effect {
public:
 bool Init(const WXL_VkRenderApi*);
 // Call at the explicit size/format boundary BEFORE capture; old images use RetireImage(lastSerial).
 bool Resize(VkExtent2D,VkFormat);
 void BeginFrame(uint64_t serial,float reactiveScale=.9f,float compositionThreshold=.5f/255.f,float reactiveCutoff=0.f);
 void FailFrame();
 // Four calls, in order: Capture(0), Compare(0), Capture(1), Compare(1).
 // The caller owns source layouts: TRANSFER_SRC for Capture, SHADER_READ_ONLY for Compare.
 // Both segments target the MAIN world only. All effects on source colour are read-only.
 bool Capture(const Frame&,uint32_t segment,const Image& source);
 bool Compare(const Frame&,uint32_t segment,const Image& source);
 // Only after both segments. Makes the two R8 masks SHADER_READ_ONLY for the SDK.
 bool Finish(const Frame&,Image& reactive,Image& composition);
 // Diagnostic writes only the separate RGBA8 output, GENERAL on entry/exit.
 bool Debug(const Frame&,const Image& output,uint32_t view);
 bool Shutdown(uint64_t completed);
 uint64_t LastUsed()const;const char* Error()const;Stats ReadStats()const;
 Effect()=default;Effect(const Effect&)=delete;Effect&operator=(const Effect&)=delete;
private:struct Impl;Impl*state_=nullptr;
};
}
