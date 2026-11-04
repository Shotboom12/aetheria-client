// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef WOWDECOMP_FSR
#include "../../wxl/src/vulkan/Vk.hpp"
#include "HdFsrSkin.h"
#include "HdFsrSkinCache.h"
#include "HdFsrRigid.h"
#include "../../wxl/src/vulkan/Memory.hpp"
#include "../../wxl/src/draw/Cmd.hpp"
#include <new>
#include <chrono>
#include <cstddef>
#include <algorithm>
namespace HD::Fsr::Skin {
namespace {namespace memory=wxlvk::vk::memory;constexpr uint32_t Slots=8,Poses=2048;
 uint64_t Align(uint64_t n,uint64_t a){return a?((n+a-1)/a)*a:n;}
}
struct Effect::Impl {
 WXL_VkDeviceInfo device{};Cache<Poses>cache;Frame frame;bool active=false,wasEnabled=false,safe=true,usedFrame=false;uint64_t last=0,previousFrame=0,epoch=0;
 const char*error="";Stats stats;VkDescriptorSetLayout layout{};VkDescriptorPool pool{};uint64_t uniformStride=0,boneStride=0,totalGpuBytes=0;
 struct Slot{memory::Buffer uniform,bones;VkDescriptorSet set{};uint64_t serial=0;uint32_t cursor=1;};Slot slots[Slots];
 struct Memo{uint64_t serial=0;DrawData data;};Memo memo[Poses];
 PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout=nullptr;PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout=nullptr;PFN_vkCreateDescriptorPool CreateDescriptorPool=nullptr;PFN_vkDestroyDescriptorPool DestroyDescriptorPool=nullptr;PFN_vkAllocateDescriptorSets AllocateDescriptorSets=nullptr;PFN_vkUpdateDescriptorSets UpdateDescriptorSets=nullptr;
};
Effect&Global(){static Effect global;return global;}
bool Effect::Init(const WXL_VkRenderApi*api){
 if(p_)return true;
 if(!api||api->apiVersion!=WXL_VK_RENDER_API_VERSION||api->structSize<offsetof(WXL_VkRenderApi,GetDeviceInfo)+sizeof(api->GetDeviceInfo)||!api->GetDeviceInfo)return false;
 auto*s=new(std::nothrow)Impl;if(!s)return false;s->device.structSize=sizeof s->device;
 if(!api->GetDeviceInfo(&s->device)||!s->device.device||!s->device.getDeviceProcAddr||!s->device.frameSlots||s->device.frameSlots>Slots){delete s;return false;}
#define LOAD(n) s->n=reinterpret_cast<PFN_vk##n>(s->device.getDeviceProcAddr(s->device.device,"vk" #n));if(!s->n){delete s;return false;}
 LOAD(CreateDescriptorSetLayout) LOAD(DestroyDescriptorSetLayout) LOAD(CreateDescriptorPool) LOAD(DestroyDescriptorPool) LOAD(AllocateDescriptorSets) LOAD(UpdateDescriptorSets)
#undef LOAD
 auto get=s->device.getInstanceProcAddr?reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(s->device.getInstanceProcAddr(s->device.instance,"vkGetPhysicalDeviceProperties")):nullptr;
 if(!get){delete s;return false;}VkPhysicalDeviceProperties properties{};get(s->device.physicalDevice,&properties);const auto&l=properties.limits;
 if(l.maxBoundDescriptorSets<4||l.maxDescriptorSetUniformBuffersDynamic<2||l.maxDescriptorSetStorageBuffersDynamic<2||l.maxPerStageDescriptorUniformBuffers<2||l.maxPerStageDescriptorStorageBuffers<2||l.maxVertexOutputComponents<72||l.maxFragmentInputComponents<72||l.maxPushConstantsSize<64){delete s;return false;}
 s->uniformStride=Align(UniformBytes,l.minUniformBufferOffsetAlignment);s->boneStride=Align(MaxRows*16,l.minStorageBufferOffsetAlignment);
 if(s->uniformStride*(Poses+1)>0xffffffffULL||s->boneStride*(Poses+1)>0xffffffffULL){delete s;return false;}
 p_=s;auto fail=[&](){Shutdown(0);return false;};
 VkDescriptorSetLayoutBinding b[2]={{2,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr}};VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=2;dl.pBindings=b;
 if(s->CreateDescriptorSetLayout(s->device.device,&dl,nullptr,&s->layout)!=VK_SUCCESS)return fail();
 VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,s->device.frameSlots},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,s->device.frameSlots}};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=s->device.frameSlots;pi.poolSizeCount=2;pi.pPoolSizes=sizes;
 if(s->CreateDescriptorPool(s->device.device,&pi,nullptr,&s->pool)!=VK_SUCCESS)return fail();
 VkDescriptorSetLayout layouts[Slots];VkDescriptorSet sets[Slots]{};for(auto&v:layouts)v=s->layout;VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=s->pool;ai.descriptorSetCount=s->device.frameSlots;ai.pSetLayouts=layouts;
 if(s->AllocateDescriptorSets(s->device.device,&ai,sets)!=VK_SUCCESS)return fail();
 for(uint32_t i=0;i<s->device.frameSlots;++i){auto&slot=s->slots[i];slot.set=sets[i];
  if(!memory::CreateBuffer(s->uniformStride*(Poses+1),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,memory::Where::Stream,slot.uniform)||!memory::CreateBuffer(s->boneStride*(Poses+1),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,memory::Where::Stream,slot.bones)||!slot.uniform.block.mapped||!slot.bones.block.mapped)return fail();
  // Offset zero is permanently immutable dummy data. No-history draws skip the previous function and use it safely.
  std::memset(slot.uniform.block.mapped,0,UniformBytes);std::memset(slot.bones.block.mapped,0,MaxRows*16);
  if(!memory::FlushHost(slot.uniform.block)||!memory::FlushHost(slot.bones.block))return fail();
  VkDescriptorBufferInfo buffers[2]={{slot.uniform.handle,0,UniformBytes},{slot.bones.handle,0,MaxRows*16}};VkWriteDescriptorSet writes[2]{};
  for(uint32_t j=0;j<2;++j){auto&w=writes[j];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=slot.set;w.dstBinding=j+2;w.descriptorCount=1;w.descriptorType=j?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;w.pBufferInfo=&buffers[j];}
  s->UpdateDescriptorSets(s->device.device,2,writes,0,nullptr);s->totalGpuBytes+=slot.uniform.size+slot.bones.size;
 }
 return true;
}
void Effect::BeginFrame(const Frame&f){auto*s=p_;if(!s)return;s->active=false;
 if(!f.enabled){s->wasEnabled=false;return;}
 if(!f.serial||f.serial<=f.completed||f.slot>=s->device.frameSlots||!f.width||!f.height){s->error="M3 invalid frame";return;}
 auto&slot=s->slots[f.slot];if(slot.serial>f.completed&&slot.serial!=f.serial){s->error="M3 frame slot still in flight";return;}
 if(f.serial==s->previousFrame){s->error="M3 duplicate BeginFrame";return;}
 if(f.reset||!s->wasEnabled||!s->previousFrame||f.serial<=s->previousFrame||f.serial-s->previousFrame!=1||f.epoch!=s->epoch||f.width!=s->frame.width||f.height!=s->frame.height){s->cache.Clear();for(auto&memo:s->memo)memo.serial=0;}
 slot.cursor=1;slot.serial=f.serial;s->frame=f;s->epoch=f.epoch;s->previousFrame=f.serial;s->active=s->wasEnabled=s->safe=true;s->usedFrame=false;s->cache.Sweep(f.serial);++s->stats.frames;s->error="";
}
void Effect::EndFrame(){if(p_)p_->active=false;}
bool Effect::CanDraw(const wxlvk::draw::Cmd&c,bool wind)const{const auto*s=p_;return s&&s->active&&!wind&&Rigid::SkeletonOwner()&&Rigid::SkeletonRows()&&Rigid::SkeletonRows()<=MaxRows&&c.vertexRows&&c.paletteRows&&c.paletteCount>=Rigid::SkeletonRows()&&c.vconst==wxlvk::draw::Cmd::kVertexRegsHigh&&c.depthWrite&&c.depthTest&&c.blend<=1&&c.colorWrite&&c.topology==3&&std::strncmp(c.vs,"Diffuse_",8)==0&&std::isfinite(c.viewport.width)&&std::isfinite(c.viewport.height)&&std::fabs(c.viewport.width-float(s->frame.width))<=.01f&&std::fabs(c.viewport.height-float(s->frame.height))<=.01f;}
const char*Effect::WhyNot(const wxlvk::draw::Cmd&c,bool wind)const{const auto*s=p_;
 if(!s)return "M3 not initialised";if(!s->active)return "M3 inactive";if(wind)return "wind (M4)";if(!Rigid::SkeletonOwner())return Rigid::DiagScopeKind()==2?"WMO":"no skeleton scope";
 if(!Rigid::SkeletonRows())return "no palette (bones 0, >75 or bad combo)";if(Rigid::SkeletonRows()>MaxRows)return "palette too big";if(!c.vertexRows)return "no vertex rows";
 if(!c.paletteRows)return "no palette rows in draw";if(c.paletteCount<Rigid::SkeletonRows())return "palette count < rows";if(c.vconst!=wxlvk::draw::Cmd::kVertexRegsHigh)return "not 256-register slice";
 if(!c.depthWrite||!c.depthTest)return "no depth write/test";if(c.blend>1)return "blended";if(!c.colorWrite)return "no colour write";if(c.topology!=3)return "not triangle list";
 if(std::strncmp(c.vs,"Diffuse_",8))return "vertex shader family";
 if(!std::isfinite(c.viewport.width)||!std::isfinite(c.viewport.height)||std::fabs(c.viewport.width-float(s->frame.width))>.01f||std::fabs(c.viewport.height-float(s->frame.height))>.01f)return "viewport not world size";return "ok";
}
DrawData Effect::Record(const wxlvk::draw::Cmd&c,const float misc[4]){
 DrawData data;auto*s=p_;if(!s||!s->active||!CanDraw(c,false)||!misc)return data;data.serial=s->frame.serial;++s->stats.records;
 const auto start=s->frame.profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
 struct Timer{Impl*s;std::chrono::steady_clock::time_point start;~Timer(){if(s->frame.profile)s->stats.cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}}timer{s,start};
 Input input{c.pixelConst,misc,c.vertexRows,c.paletteRows,Rigid::SkeletonRows(),c.paletteSerial};const auto observation=s->cache.Observe(Rigid::SkeletonOwner(),Rigid::SkeletonPart(),s->frame.serial,input);data.cookie=observation.cookie;
 if(observation.captured){++s->stats.captures;s->stats.cpuBytes+=observation.bytesCopied;}
 if(observation.result==Result::Conflict){++s->stats.conflicts;s->safe=false;Rigid::UnsafeFrame();return data;}
 if(observation.result==Result::Shared){++s->stats.conflicts;return data;} // HD 2026-10-31: camera-only draw, the frame stays safe
 if(observation.result==Result::Full){++s->stats.cacheFull;return data;}
 if(observation.result==Result::Invalid){++s->stats.invalid;return data;}
 if(!observation.previous){++s->stats.first;return data;}
 auto&memo=s->memo[observation.cookie];if(memo.serial==s->frame.serial){++s->stats.reused;data=memo.data;return data;}
 auto&slot=s->slots[s->frame.slot];if(slot.cursor>Poses){++s->stats.ringFull;return data;}
 data.offsets[0]=uint32_t(s->uniformStride*slot.cursor);data.offsets[1]=uint32_t(s->boneStride*slot.cursor);++slot.cursor;
 auto*regs=static_cast<unsigned char*>(slot.uniform.block.mapped)+data.offsets[0];auto*bones=static_cast<unsigned char*>(slot.bones.block.mapped)+data.offsets[1];
 std::memcpy(regs,&observation.previous->uniform,UniformBytes);std::memcpy(bones,observation.previous->bones,MaxRows*16);
 if(!memory::FlushHost(slot.uniform.block)||!memory::FlushHost(slot.bones.block)){data.offsets[0]=data.offsets[1]=0;++s->stats.invalid;return data;}
 data.valid=1;memo.serial=s->frame.serial;memo.data=data;++s->stats.uploads;s->stats.gpuBytes+=UniformBytes+MaxRows*16;return data;
}
void Effect::Commit(const DrawData&d){auto*s=p_;if(s&&s->active&&d.serial==s->frame.serial){s->cache.Commit(d.cookie,d.serial);s->last=d.serial;++s->stats.issued;}}
VkDescriptorSetLayout Effect::Layout()const{return p_?p_->layout:VK_NULL_HANDLE;}VkDescriptorSet Effect::Set(uint32_t slot)const{return p_&&slot<p_->device.frameSlots?p_->slots[slot].set:VK_NULL_HANDLE;}
bool Effect::Ready()const{return p_!=nullptr;}bool Effect::Active()const{return p_&&p_->active;}bool Effect::FrameSafe()const{return !p_||!p_->active||p_->safe;}const char*Effect::Error()const{return p_?p_->error:"M3 not initialized";}
uint64_t Effect::LastUsed()const{return p_?p_->last:0;}uint64_t Effect::CpuMemoryBytes()const{return p_?sizeof(Impl):0;}uint64_t Effect::GpuBufferBytes()const{return p_?p_->totalGpuBytes:0;}
Stats Effect::ReadStats(bool reset){Stats s;if(p_){s=p_->stats;if(reset)p_->stats=Stats{};}return s;}
bool Effect::Shutdown(uint64_t completed){auto*s=p_;if(!s)return true;if(completed<s->last){s->error="M3 pending GPU serial";return false;}
 if(s->pool)s->DestroyDescriptorPool(s->device.device,s->pool,nullptr);for(auto&slot:s->slots){memory::DestroyBuffer(slot.uniform);memory::DestroyBuffer(slot.bones);}if(s->layout)s->DestroyDescriptorSetLayout(s->device.device,s->layout,nullptr);delete s;p_=nullptr;return true;}
}
#endif
