// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef WOWDECOMP_FSR
#include "HdFsrMotion.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <new>
#include <cstddef>
namespace HD::Fsr::ObjectMotion {
namespace {
#include "shaders/HdFsrMotionCompose.spv.h"
#include "shaders/HdFsrMotionDebug.spv.h"
#include "shaders/HdFsrMotionDebugObject.spv.h" // diagnostic view of object markers (2026-10-31)
}
struct Effect::Impl {
 WXL_VkDeviceInfo device{};const char*error="";uint64_t last=0;
 VkDescriptorSetLayout descriptor{};VkPipelineLayout layout{};VkDescriptorPool pool{};VkPipeline pipelines[3]{};
 struct Slot{VkDescriptorSet sets[2]{};uint64_t serial[2]{};VkQueryPool queries{};bool queried[2]{};};std::array<Slot,8>slots{};
 double timestampPeriod=0;uint32_t timestampBits=0;Stats stats{};
#define MOTION_FUNCTIONS(X) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) X(CmdPipelineBarrier) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch)
#define DECL(name) PFN_vk##name name=nullptr;
 MOTION_FUNCTIONS(DECL)
 PFN_vkCreateQueryPool CreateQueryPool=nullptr;PFN_vkDestroyQueryPool DestroyQueryPool=nullptr;PFN_vkCmdResetQueryPool CmdResetQueryPool=nullptr;PFN_vkCmdWriteTimestamp CmdWriteTimestamp=nullptr;PFN_vkGetQueryPoolResults GetQueryPoolResults=nullptr;
#undef DECL
 void BeginTiming(const Frame&f,uint32_t stage){
  auto&slot=slots[f.slot];if(!slot.queries)return;
  if(slot.queried[stage]){
   uint64_t result[4]{};
   if(GetQueryPoolResults(device.device,slot.queries,stage*2,2,sizeof result,result,2*sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)==VK_SUCCESS&&result[1]&&result[3]){
    const uint64_t mask=timestampBits==64?~uint64_t(0):((uint64_t(1)<<timestampBits)-1);
    stats.gpuMilliseconds[stage]+=double((result[2]-result[0])&mask)*timestampPeriod*1e-6;++stats.gpuSamples[stage];
   }slot.queried[stage]=false;
  }
  if(f.timing){CmdResetQueryPool(f.cmd,slot.queries,stage*2,2);CmdWriteTimestamp(f.cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,slot.queries,stage*2);}
 }
 void EndTiming(const Frame&f,uint32_t stage){auto&slot=slots[f.slot];if(slot.queries&&f.timing){CmdWriteTimestamp(f.cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,slot.queries,stage*2+1);slot.queried[stage]=true;}}
};
bool Effect::Init(const WXL_VkRenderApi*api){
 if(state_)return true;
 if(!api||api->apiVersion!=WXL_VK_RENDER_API_VERSION||api->structSize<offsetof(WXL_VkRenderApi,GetDeviceInfo)+sizeof(api->GetDeviceInfo)||!api->GetDeviceInfo)return false;
 auto*s=new(std::nothrow)Impl;if(!s)return false;s->device.structSize=sizeof s->device;
 if(!api->GetDeviceInfo(&s->device)||!s->device.device||!s->device.getDeviceProcAddr||!s->device.frameSlots||s->device.frameSlots>8){delete s;return false;}
#define LOAD(name) s->name=reinterpret_cast<PFN_vk##name>(s->device.getDeviceProcAddr(s->device.device,"vk" #name));if(!s->name){delete s;return false;}
 MOTION_FUNCTIONS(LOAD)
#undef LOAD
 state_=s;auto fail=[&](){Shutdown(0);return false;};
 VkDescriptorSetLayoutBinding b[2]{};for(uint32_t i=0;i<2;++i){b[i].binding=i;b[i].descriptorCount=1;b[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;b[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
 VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=2;dl.pBindings=b;
 if(s->CreateDescriptorSetLayout(s->device.device,&dl,nullptr,&s->descriptor)!=VK_SUCCESS)return fail();
 VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,16};VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&s->descriptor;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&range;
 if(s->CreatePipelineLayout(s->device.device,&pl,nullptr,&s->layout)!=VK_SUCCESS)return fail();
 const uint32_t* codes[3]={MotionComposeSpv,MotionDebugSpv,MotionDebugObjectSpv};const size_t sizes[3]={sizeof MotionComposeSpv,sizeof MotionDebugSpv,sizeof MotionDebugObjectSpv};
 for(uint32_t i=0;i<3;++i){
  VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=sizes[i];sm.pCode=codes[i];VkShaderModule module{};
  if(s->CreateShaderModule(s->device.device,&sm,nullptr,&module)!=VK_SUCCESS)return fail();
  VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.layout=s->layout;cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=module;cp.stage.pName="main";
  VkResult r=s->CreateComputePipelines(s->device.device,s->device.pipelineCache,1,&cp,nullptr,&s->pipelines[i]);s->DestroyShaderModule(s->device.device,module,nullptr);if(r!=VK_SUCCESS)return fail();
 }
 const uint32_t sets=s->device.frameSlots*2;VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,sets*2};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=sets;dp.poolSizeCount=1;dp.pPoolSizes=&size;
 if(s->CreateDescriptorPool(s->device.device,&dp,nullptr,&s->pool)!=VK_SUCCESS)return fail();
 std::array<VkDescriptorSetLayout,16>layouts;layouts.fill(s->descriptor);std::array<VkDescriptorSet,16>desc{};
 VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=s->pool;ai.descriptorSetCount=sets;ai.pSetLayouts=layouts.data();
 if(s->AllocateDescriptorSets(s->device.device,&ai,desc.data())!=VK_SUCCESS)return fail();
 for(uint32_t i=0;i<s->device.frameSlots;++i)for(uint32_t j=0;j<2;++j)s->slots[i].sets[j]=desc[i*2+j];
 // Optional timestamp pools are made once, never per frame. A read is non-blocking and only after slot completion.
 auto gi=s->device.getInstanceProcAddr;
 auto properties=gi?reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gi(s->device.instance,"vkGetPhysicalDeviceProperties")):nullptr;
 auto families=gi?reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gi(s->device.instance,"vkGetPhysicalDeviceQueueFamilyProperties")):nullptr;
#define OPTIONAL(name) s->name=reinterpret_cast<PFN_vk##name>(s->device.getDeviceProcAddr(s->device.device,"vk" #name));
 OPTIONAL(CreateQueryPool) OPTIONAL(DestroyQueryPool) OPTIONAL(CmdResetQueryPool) OPTIONAL(CmdWriteTimestamp) OPTIONAL(GetQueryPoolResults)
#undef OPTIONAL
 if(properties&&families&&s->CreateQueryPool&&s->DestroyQueryPool&&s->CmdResetQueryPool&&s->CmdWriteTimestamp&&s->GetQueryPoolResults){
  VkPhysicalDeviceProperties props{};properties(s->device.physicalDevice,&props);uint32_t count=0;families(s->device.physicalDevice,&count,nullptr);
  std::array<VkQueueFamilyProperties,64>q{};
  if(count<=q.size()&&s->device.queueFamily<count&&props.limits.timestampComputeAndGraphics){families(s->device.physicalDevice,&count,q.data());s->timestampBits=q[s->device.queueFamily].timestampValidBits;s->timestampPeriod=props.limits.timestampPeriod;}
  if(s->timestampBits&&s->timestampPeriod>0){for(uint32_t i=0;i<s->device.frameSlots;++i){VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qp.queryType=VK_QUERY_TYPE_TIMESTAMP;qp.queryCount=4;
    if(s->CreateQueryPool(s->device.device,&qp,nullptr,&s->slots[i].queries)!=VK_SUCCESS){for(auto&slot:s->slots)if(slot.queries){s->DestroyQueryPool(s->device.device,slot.queries,nullptr);slot.queries=VK_NULL_HANDLE;}break;}
  }}
 }
 return true;
}
namespace {
VkImageMemoryBarrier Image(VkImage image,VkImageLayout before,VkImageLayout after,VkAccessFlags src,VkAccessFlags dst){
 VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=before;b.newLayout=after;b.srcAccessMask=src;b.dstAccessMask=dst;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};return b;
}
}
bool Effect::Compose(const Frame&f,VkImageView object,VkImageView camera,bool reset){
 auto*s=state_;if(!s)return false;
 if(!f.cmd||!f.serial||f.serial<=f.completed||f.slot>=s->device.frameSlots||!f.extent.width||!f.extent.height||f.extent.width>16384||f.extent.height>16384||!object||!camera){s->error="M1 compose frame unavailable";return false;}
 auto&slot=s->slots[f.slot];if(slot.serial[0]==f.serial||slot.serial[0]>f.completed){s->error="M1 compose slot busy/duplicate";return false;}
 if(reset){s->error="";return true;} // no object history on a cut; the existing camera pass owns reset behavior.
 s->BeginTiming(f,0);
 VkDescriptorImageInfo images[2]={{VK_NULL_HANDLE,object,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,camera,VK_IMAGE_LAYOUT_GENERAL}};VkWriteDescriptorSet writes[2]{};
 for(uint32_t i=0;i<2;++i){auto&w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=slot.sets[0];w.dstBinding=i;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&images[i];}
 s->UpdateDescriptorSets(s->device.device,2,writes,0,nullptr);
 VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
 s->CmdPipelineBarrier(f.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);
 s->CmdBindPipeline(f.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipelines[0]);s->CmdBindDescriptorSets(f.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->layout,0,1,&slot.sets[0],0,nullptr);
 s->CmdDispatch(f.cmd,(f.extent.width+7)/8,(f.extent.height+7)/8,1);
 s->EndTiming(f,0);
 slot.serial[0]=s->last=f.serial;s->error="";return true;
}
bool Effect::Debug(const Frame&f,VkImage motion,VkImageView motionView,VkImageView output,float scale){
 auto*s=state_;if(!s)return false;
 if(!f.cmd||!f.serial||f.serial<=f.completed||f.slot>=s->device.frameSlots||!f.extent.width||!f.extent.height||f.extent.width>16384||f.extent.height>16384||!motion||!motionView||!output||!std::isfinite(scale)){s->error="M1 debug frame unavailable";return false;}
 auto&slot=s->slots[f.slot];if(slot.serial[1]==f.serial||slot.serial[1]>f.completed){s->error="M1 debug slot busy/duplicate";return false;}
 s->BeginTiming(f,1);
 VkDescriptorImageInfo images[2]={{VK_NULL_HANDLE,motionView,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,output,VK_IMAGE_LAYOUT_GENERAL}};VkWriteDescriptorSet writes[2]{};
 for(uint32_t i=0;i<2;++i){auto&w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=slot.sets[1];w.dstBinding=i;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&images[i];}
 s->UpdateDescriptorSets(s->device.device,2,writes,0,nullptr);
 auto b=Image(motion,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
 s->CmdPipelineBarrier(f.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
 s->CmdBindPipeline(f.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipelines[1]);s->CmdBindDescriptorSets(f.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->layout,0,1,&slot.sets[1],0,nullptr);
 float push[4]={std::clamp(scale,1.f,1024.f),0,0,0};s->CmdPushConstants(f.cmd,s->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,push);
 s->CmdDispatch(f.cmd,(f.extent.width+7)/8,(f.extent.height+7)/8,1);
 s->EndTiming(f,1);
 b=Image(motion,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
 s->CmdPipelineBarrier(f.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
 slot.serial[1]=s->last=f.serial;s->error="";return true;
}
// Diagnostic (2026-10-31): the object-marker image itself (GENERAL, written by the world pass), at native size.
bool Effect::DebugObjects(const Frame&f,VkImageView objectView,VkImageView output,float scale){
 auto*s=state_;if(!s)return false;
 if(!f.cmd||!f.serial||f.serial<=f.completed||f.slot>=s->device.frameSlots||!f.extent.width||!f.extent.height||f.extent.width>16384||f.extent.height>16384||!objectView||!output||!std::isfinite(scale)){s->error="M1 object debug frame unavailable";return false;}
 auto&slot=s->slots[f.slot];if(slot.serial[1]==f.serial||slot.serial[1]>f.completed){s->error="M1 debug slot busy/duplicate";return false;}
 s->BeginTiming(f,1);
 VkDescriptorImageInfo images[2]={{VK_NULL_HANDLE,objectView,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,output,VK_IMAGE_LAYOUT_GENERAL}};VkWriteDescriptorSet writes[2]{};
 for(uint32_t i=0;i<2;++i){auto&w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=slot.sets[1];w.dstBinding=i;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&images[i];}
 s->UpdateDescriptorSets(s->device.device,2,writes,0,nullptr);
 VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
 s->CmdPipelineBarrier(f.cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);
 s->CmdBindPipeline(f.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipelines[2]);s->CmdBindDescriptorSets(f.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->layout,0,1,&slot.sets[1],0,nullptr);
 float push[4]={std::clamp(scale,1.f,4096.f),0,0,0};s->CmdPushConstants(f.cmd,s->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,push);
 s->CmdDispatch(f.cmd,(f.extent.width+7)/8,(f.extent.height+7)/8,1);
 s->EndTiming(f,1);
 slot.serial[1]=s->last=f.serial;s->error="";return true;
}
bool Effect::Shutdown(uint64_t completed){
 auto*s=state_;if(!s)return true;if(completed<s->last){s->error="M1 shutdown pending serial";return false;}
 for(auto p:s->pipelines)if(p)s->DestroyPipeline(s->device.device,p,nullptr);
 for(auto&slot:s->slots)if(slot.queries)s->DestroyQueryPool(s->device.device,slot.queries,nullptr);
 if(s->pool)s->DestroyDescriptorPool(s->device.device,s->pool,nullptr);if(s->layout)s->DestroyPipelineLayout(s->device.device,s->layout,nullptr);if(s->descriptor)s->DestroyDescriptorSetLayout(s->device.device,s->descriptor,nullptr);
 delete s;state_=nullptr;return true;
}
uint64_t Effect::LastUsed()const{return state_?state_->last:0;}
const char* Effect::Error()const{return state_?state_->error:"M1 not initialized";}
Stats Effect::ReadStats(bool reset){Stats out{};if(state_){out=state_->stats;if(reset)state_->stats={};}return out;}
}
#endif
