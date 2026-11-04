// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "wxl/VkRenderApi.h"
#include <cstdint>
namespace wxlvk::draw{struct Cmd;}
namespace wxlvk::vk::wind{struct Draw;}
namespace HD::Fsr::WindMotion {
struct Frame{uint64_t serial=0,completed=0,epoch=0;uint32_t slot=0,width=0,height=0;bool enabled=false,reset=false,profile=false;};
struct DrawData{uint32_t offsets[3]{},valid=0,cookie=~uint32_t(0);uint64_t serial=0;};
struct Stats{uint64_t frames=0,records=0,captures=0,uploads=0,reused=0,first=0,wrapped=0,capacity=0,conflicts=0,invalid=0,cpuBytes=0,gpuBytes=0,tree=0,grass=0,issued=0;double cpuMs=0;};
class Effect{
public:
 bool Init(const WXL_VkRenderApi*);void BeginFrame(const Frame&);void EndFrame();
 bool CanDraw(const wxlvk::draw::Cmd&,const wxlvk::vk::wind::Draw*)const;
 DrawData Record(const wxlvk::draw::Cmd&,const float misc[4],bool bones,const wxlvk::vk::wind::Draw*);void Commit(const DrawData&);
 VkDescriptorSetLayout Layout()const;VkDescriptorSet Set(uint32_t slot)const;bool Ready()const;bool Active()const;bool FrameSafe()const;const char*Error()const;
 uint64_t LastUsed()const;uint64_t CpuMemoryBytes()const;uint64_t GpuBufferBytes()const;Stats ReadStats(bool reset);bool Shutdown(uint64_t completed);
 Effect()=default;Effect(const Effect&)=delete;Effect&operator=(const Effect&)=delete;
private:struct Impl;Impl*p_=nullptr;
};Effect&Global();
}
