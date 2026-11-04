// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef WOWDECOMP_FSR
#include "../../wxl/src/vulkan/Vk.hpp"
#include "HdFsrWind.h"
#include "HdFsrWindCache.h"
#include "HdFsrRigid.h"
#include "../../wxl/src/vulkan/Memory.hpp"
#include "../../wxl/src/vulkan/Wind.hpp"
#include <new>
#include <chrono>
#include <cstddef>
#include <algorithm>
#include <initializer_list>
namespace HD::Fsr::WindMotion{
namespace{namespace memory=wxlvk::vk::memory;namespace wind=wxlvk::vk::wind;constexpr uint32_t Slots=8,Poses=1024;uint64_t Align(uint64_t n,uint64_t a){return a?((n+a-1)/a)*a:n;}}
static_assert(sizeof(WXL_VkWindParams)<=WindBytes&&offsetof(WXL_VkWindParams,time)==WindClockRow*16,"wind ABI offsets; no guessed x86 addresses");
struct Effect::Impl{
 WXL_VkDeviceInfo device{};Cache<Poses>cache;Frame frame;bool active=false,wasEnabled=false,safe=true,treeReady=false;uint64_t last=0,previousFrame=0,epoch=0,treeRevision=0;
 const char*error="";Stats stats;VkDescriptorSetLayout layout{};VkDescriptorPool pool{};uint64_t strides[3]{},gpuBytes=0;
 struct Slot{memory::Buffer buffers[3];VkDescriptorSet set{};uint64_t serial=0;uint32_t cursor=1;};Slot slots[Slots];
 struct Memo{uint64_t serial=0;DrawData data;};Memo memo[Poses];
 PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout=nullptr;PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout=nullptr;PFN_vkCreateDescriptorPool CreateDescriptorPool=nullptr;PFN_vkDestroyDescriptorPool DestroyDescriptorPool=nullptr;PFN_vkAllocateDescriptorSets AllocateDescriptorSets=nullptr;PFN_vkUpdateDescriptorSets UpdateDescriptorSets=nullptr;
};
Effect&Global(){static Effect effect;return effect;}
bool Effect::Init(const WXL_VkRenderApi*api){
 if(p_)return true;if(!api||api->apiVersion!=WXL_VK_RENDER_API_VERSION||api->structSize<offsetof(WXL_VkRenderApi,GetDeviceInfo)+sizeof(api->GetDeviceInfo)||!api->GetDeviceInfo)return false;
 auto*s=new(std::nothrow)Impl;if(!s)return false;s->device.structSize=sizeof s->device;
 if(!api->GetDeviceInfo(&s->device)||!s->device.device||!s->device.getDeviceProcAddr||!s->device.frameSlots||s->device.frameSlots>Slots){delete s;return false;}
#define LOAD(n) s->n=reinterpret_cast<PFN_vk##n>(s->device.getDeviceProcAddr(s->device.device,"vk" #n));if(!s->n){delete s;return false;}
 LOAD(CreateDescriptorSetLayout) LOAD(DestroyDescriptorSetLayout) LOAD(CreateDescriptorPool) LOAD(DestroyDescriptorPool) LOAD(AllocateDescriptorSets) LOAD(UpdateDescriptorSets)
#undef LOAD
 auto get=s->device.getInstanceProcAddr?reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(s->device.getInstanceProcAddr(s->device.instance,"vkGetPhysicalDeviceProperties")):nullptr;if(!get){delete s;return false;}
 VkPhysicalDeviceProperties props{};get(s->device.physicalDevice,&props);const auto&l=props.limits;
 if(l.maxBoundDescriptorSets<4||l.maxDescriptorSetUniformBuffersDynamic<3||l.maxDescriptorSetStorageBuffersDynamic<2||l.maxPerStageDescriptorUniformBuffers<4||l.maxPerStageDescriptorStorageBuffers<3||l.maxVertexOutputComponents<72||l.maxFragmentInputComponents<72||l.maxPushConstantsSize<64){delete s;return false;}
 s->strides[0]=Align(UniformBytes,l.minUniformBufferOffsetAlignment);s->strides[1]=Align(BoneRows*16,l.minStorageBufferOffsetAlignment);s->strides[2]=Align(WindBytes,l.minUniformBufferOffsetAlignment);
 for(auto stride:s->strides)if(stride*(Poses+1)>0xffffffffULL){delete s;return false;}
 p_=s;auto fail=[&](){Shutdown(0);return false;};
 VkDescriptorSetLayoutBinding b[5]{};for(uint32_t j=0;j<5;++j){b[j].binding=j;b[j].descriptorCount=1;b[j].stageFlags=j?VK_SHADER_STAGE_VERTEX_BIT:VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;b[j].descriptorType=j==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:j==1?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:j==3?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;}
 VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=5;dl.pBindings=b;if(s->CreateDescriptorSetLayout(s->device.device,&dl,nullptr,&s->layout)!=VK_SUCCESS)return fail();
 auto n=s->device.frameSlots;VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,n},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,n},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,2*n},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,n}};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=n;pi.poolSizeCount=4;pi.pPoolSizes=sizes;
 if(s->CreateDescriptorPool(s->device.device,&pi,nullptr,&s->pool)!=VK_SUCCESS)return fail();VkDescriptorSetLayout layouts[Slots];for(auto&v:layouts)v=s->layout;VkDescriptorSet sets[Slots]{};VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=s->pool;ai.descriptorSetCount=n;ai.pSetLayouts=layouts;if(s->AllocateDescriptorSets(s->device.device,&ai,sets)!=VK_SUCCESS)return fail();
 const uint32_t ranges[]={UniformBytes,BoneRows*16,WindBytes};
 for(uint32_t i=0;i<n;++i){auto&slot=s->slots[i];slot.set=sets[i];
  for(uint32_t j=0;j<3;++j){if(!memory::CreateBuffer(s->strides[j]*(Poses+1),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,memory::Where::Stream,slot.buffers[j])||!slot.buffers[j].block.mapped)return fail();std::memset(slot.buffers[j].block.mapped,0,ranges[j]);if(!memory::FlushHost(slot.buffers[j].block))return fail();s->gpuBytes+=slot.buffers[j].size;}
  VkDescriptorBufferInfo info[5]={{slot.buffers[2].handle,0,WindBytes},{slot.buffers[1].handle,0,16},{slot.buffers[0].handle,0,UniformBytes},{slot.buffers[1].handle,0,BoneRows*16},{slot.buffers[2].handle,0,WindBytes}};VkWriteDescriptorSet writes[5]{};
  for(uint32_t j=0;j<5;++j){auto&w=writes[j];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=slot.set;w.dstBinding=j;w.descriptorCount=1;w.descriptorType=b[j].descriptorType;w.pBufferInfo=&info[j];}s->UpdateDescriptorSets(s->device.device,5,writes,0,nullptr);
 }
 return true;
}
void Effect::BeginFrame(const Frame&f){auto*s=p_;if(!s)return;s->active=false;if(!f.enabled){s->wasEnabled=false;return;}
 if(!f.serial||f.serial<=f.completed||f.slot>=s->device.frameSlots||!f.width||!f.height){s->error="M4 invalid frame";return;}auto&slot=s->slots[f.slot];if(slot.serial>f.completed&&slot.serial!=f.serial){s->error="M4 frame slot in flight";return;}if(f.serial==s->previousFrame){s->error="M4 duplicate frame";return;}
 if(f.reset||!s->wasEnabled||!s->previousFrame||f.serial<=s->previousFrame||f.serial-s->previousFrame!=1||f.epoch!=s->epoch||f.width!=s->frame.width||f.height!=s->frame.height){s->cache.Clear();for(auto&m:s->memo)m.serial=0;}
 // Update before ANY draw records this slot's set. Never update a set already bound this frame.
 wind::MotionResources resources{};s->treeReady=wind::MotionResourcesFor(f.slot,resources);s->treeRevision=resources.revision;
 VkDescriptorBufferInfo info[2]={{s->treeReady?resources.block:slot.buffers[2].handle,0,WindBytes},{s->treeReady?resources.data:slot.buffers[1].handle,0,s->treeReady?resources.dataBytes:16}};VkWriteDescriptorSet writes[2]{};
 for(uint32_t j=0;j<2;++j){auto&w=writes[j];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=slot.set;w.dstBinding=j;w.descriptorCount=1;w.descriptorType=j?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&info[j];}s->UpdateDescriptorSets(s->device.device,2,writes,0,nullptr);
 slot.cursor=1;slot.serial=f.serial;s->frame=f;s->epoch=f.epoch;s->previousFrame=f.serial;s->active=s->wasEnabled=s->safe=true;s->cache.Sweep(f.serial);s->error="";++s->stats.frames;
}
void Effect::EndFrame(){if(p_)p_->active=false;}
bool Effect::CanDraw(const wxlvk::draw::Cmd&c,const wind::Draw*d)const{
 const auto*s=p_;if(!s||!s->active||!Rigid::MotionOwner()||!c.vertexRows||!c.depthWrite||!c.depthTest||c.blend>1||!c.colorWrite||c.topology!=3||!std::isfinite(c.viewport.width)||!std::isfinite(c.viewport.height)||std::fabs(c.viewport.width-float(s->frame.width))>.01f||std::fabs(c.viewport.height-float(s->frame.height))>.01f)return false;
 return d?(s->treeReady&&!Rigid::GrassScope()&&std::strncmp(c.vs,"Diffuse_",8)==0):(Rigid::GrassScope()&&(std::strncmp(c.vs,"HdGrass:vs:",11)==0||std::strncmp(c.vs,"DetailDoodad",12)==0));
}
DrawData Effect::Record(const wxlvk::draw::Cmd&c,const float misc[4],bool bones,const wind::Draw*d){DrawData data;auto*s=p_;if(!s||!CanDraw(c,d)||!misc)return data;data.serial=s->frame.serial;++s->stats.records;
 const auto start=s->frame.profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};struct Timer{Impl*s;std::chrono::steady_clock::time_point start;~Timer(){if(s->frame.profile)s->stats.cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}}timer{s,start};
 uint64_t revision=0;const auto*params=d?wind::MotionSnapshot(s->frame.slot,revision):nullptr;if(d&&(!params||revision!=s->treeRevision)){++s->stats.invalid;s->safe=false;Rigid::UnsafeFrame();return data;}
 Input in;in.pixel=c.pixelConst;in.misc=misc;in.vertex=c.vertexRows;in.highRows=!bones&&c.vconst==wxlvk::draw::Cmd::kVertexRegsHigh?std::min(c.highRows,192u):0;in.high=c.vertexConstHigh;in.kind=d?Kind::Tree:Kind::Grass;in.wind=params;in.windBytes=d?sizeof(*params):0;in.boneRows=bones?Rigid::SkeletonRows():0;in.bones=c.paletteRows;
 if((bones&&!in.boneRows)||in.boneRows>c.paletteCount){++s->stats.invalid;return data;}
 uint64_t vb=0,ib=0;std::memcpy(&vb,&c.vertexBuffer,sizeof c.vertexBuffer);std::memcpy(&ib,&c.indexBuffer,sizeof c.indexBuffer);uint64_t geometry=Rigid::Mix(vb)^Rigid::Mix(ib)^Rigid::Mix(c.vertexBufId)^Rigid::Mix(d?Rigid::SkeletonPart():Rigid::MotionPart());
 for(uint64_t v:{uint64_t(uint32_t(c.vertexOffset)),uint64_t(c.vertexStride),uint64_t(c.vertexFormat),uint64_t(in.kind),uint64_t(bones)})geometry=Rigid::Mix(geometry^v);
 for(uint32_t i=0;i<sizeof c.vs&&c.vs[i];++i)geometry=Rigid::Mix(geometry^uint8_t(c.vs[i]));if(d){for(uint32_t v:{uint32_t(d->bias),d->start,d->count,d->on})geometry=Rigid::Mix(geometry^v);++s->stats.tree;}else ++s->stats.grass;
 const auto o=s->cache.Observe(Rigid::MotionOwner(),geometry,s->frame.serial,in);data.cookie=o.cookie;if(o.captured){++s->stats.captures;s->stats.cpuBytes+=o.bytesCopied;}
 if(o.result==Result::Conflict){++s->stats.conflicts;s->safe=false;Rigid::UnsafeFrame();return data;}if(o.result==Result::Shared){++s->stats.conflicts;return data;}if(o.result==Result::Full){++s->stats.capacity;return data;}if(o.result==Result::Invalid){++s->stats.invalid;return data;}if(o.result==Result::Wrapped){++s->stats.wrapped;return data;}if(!o.previous){++s->stats.first;return data;}
 auto&memo=s->memo[o.cookie];if(memo.serial==s->frame.serial){++s->stats.reused;return memo.data;}auto&slot=s->slots[s->frame.slot];if(slot.cursor>Poses){++s->stats.capacity;return data;}
 const void*src[]={&o.previous->uniform,o.previous->bones,o.previous->wind};const uint32_t ranges[]={UniformBytes,BoneRows*16,WindBytes};
 for(uint32_t j=0;j<3;++j){data.offsets[j]=uint32_t(s->strides[j]*slot.cursor);std::memcpy(static_cast<unsigned char*>(slot.buffers[j].block.mapped)+data.offsets[j],src[j],ranges[j]);if(!memory::FlushHost(slot.buffers[j].block)){++slot.cursor;data.offsets[0]=data.offsets[1]=data.offsets[2]=0;++s->stats.invalid;return data;}}
 ++slot.cursor;data.valid=1;memo.serial=s->frame.serial;memo.data=data;++s->stats.uploads;s->stats.gpuBytes+=UniformBytes+BoneRows*16+WindBytes;return data;
}
void Effect::Commit(const DrawData&d){auto*s=p_;if(s&&s->active&&d.serial==s->frame.serial){s->cache.Commit(d.cookie,d.serial);s->last=d.serial;++s->stats.issued;}}
VkDescriptorSetLayout Effect::Layout()const{return p_?p_->layout:VK_NULL_HANDLE;}VkDescriptorSet Effect::Set(uint32_t slot)const{return p_&&slot<p_->device.frameSlots?p_->slots[slot].set:VK_NULL_HANDLE;}
bool Effect::Ready()const{return p_!=nullptr;}bool Effect::Active()const{return p_&&p_->active;}bool Effect::FrameSafe()const{return !p_||!p_->active||p_->safe;}const char*Effect::Error()const{return p_?p_->error:"M4 uninitialized";}
uint64_t Effect::LastUsed()const{return p_?p_->last:0;}uint64_t Effect::CpuMemoryBytes()const{return p_?sizeof(Impl):0;}uint64_t Effect::GpuBufferBytes()const{return p_?p_->gpuBytes:0;}Stats Effect::ReadStats(bool reset){Stats out;if(p_){out=p_->stats;if(reset)p_->stats=Stats{};}return out;}
bool Effect::Shutdown(uint64_t completed){auto*s=p_;if(!s)return true;if(completed<s->last){s->error="M4 pending GPU serial";return false;}if(s->pool)s->DestroyDescriptorPool(s->device.device,s->pool,nullptr);for(auto&slot:s->slots)for(auto&b:slot.buffers)memory::DestroyBuffer(b);if(s->layout)s->DestroyDescriptorSetLayout(s->device.device,s->layout,nullptr);delete s;p_=nullptr;return true;}
}
#endif
