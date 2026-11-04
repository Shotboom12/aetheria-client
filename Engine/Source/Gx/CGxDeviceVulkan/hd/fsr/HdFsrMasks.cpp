// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef WOWDECOMP_FSR
#include "HdFsrMasks.h"
#include "HdFsrMasksMath.h"
#include <array>
#include <cstddef>
#include <new>
namespace HD::Fsr::Masks {
namespace {
#include "shaders/HdFsrMasks.spv.h"
#include "shaders/HdFsrMasksDebug.spv.h"
VkImageMemoryBarrier Barrier(VkImage image,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst){
 VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=from;b.newLayout=to;
 b.srcAccessMask=src;b.dstAccessMask=dst;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
 b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};return b;
}
struct Push {float scale,threshold;uint32_t view;float cutoff;};static_assert(sizeof(Push)==16,"R1 push layout");
}
struct Effect::Impl {
 WXL_VkRenderApi api{};WXL_VkDeviceInfo device{};const char*error="";uint64_t last=0,finished=0;
 VkExtent2D extent{};VkFormat format{};Sequence sequence;float scale=.9f,threshold=.5f/255.f,cutoff=0.f;Stats stats;
 struct Owned {void*handle=nullptr;Image image{};VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;};Owned snapshot,reactive,composition;
 struct Slot {VkDescriptorSet sets[3]{};uint64_t serial[3]{};};std::array<Slot,8>slots{};
 VkSampler sampler{};VkDescriptorSetLayout descriptors[2]{};VkPipelineLayout layouts[2]{};VkPipeline pipes[2]{};VkDescriptorPool pool{};
#define MASK_FUNCTIONS(X) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) X(CreateSampler) X(DestroySampler) X(CmdPipelineBarrier) X(CmdClearColorImage) X(CmdCopyImage) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch)
#define DECL(name) PFN_vk##name name=nullptr;
 MASK_FUNCTIONS(DECL)
#undef DECL
 bool Reject(const char*why){error=why;sequence.Fail();++stats.rejected;return false;}
 void Drop(Owned&i){if(i.handle)api.RetireImage(i.handle,last);i={};}
 bool Make(Owned&i,VkFormat fmt,VkImageUsageFlags usage){
  WXL_VkImageDesc desc{};desc.format=fmt;desc.width=extent.width;desc.height=extent.height;desc.mipLevels=1;desc.usage=usage;desc.aspect=VK_IMAGE_ASPECT_COLOR_BIT;
  WXL_VkImageInfo info{};i.handle=api.CreateImage(&desc,&info);
  i.image={info.image,info.view,fmt,extent};return i.handle&&info.image&&info.view;
 }
 void Transition(VkCommandBuffer cmd,Owned&i,VkImageLayout to,VkAccessFlags dst){
  const bool undefined=i.layout==VK_IMAGE_LAYOUT_UNDEFINED;
  auto b=Barrier(i.image.image,i.layout,to,undefined?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,dst);
  CmdPipelineBarrier(cmd,undefined?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);i.layout=to;
 }
 bool FrameValid(const Frame&f){return f.cmd&&f.serial&&f.serial>f.completed&&f.slot<device.frameSlots&&sequence.serial==f.serial&&!sequence.failed&&snapshot.handle&&reactive.handle&&composition.handle;}
 bool SourceValid(const Image&i){return i.image&&i.view&&i.format==format&&i.extent.width==extent.width&&i.extent.height==extent.height&&i.image!=snapshot.image.image&&i.image!=reactive.image.image&&i.image!=composition.image.image;}
 void Bind(const Frame&f,uint32_t which,uint32_t setIndex,const VkDescriptorImageInfo*images,uint32_t count,uint32_t view){
  auto set=slots[f.slot].sets[setIndex];VkWriteDescriptorSet writes[4]{};
  for(uint32_t j=0;j<count;++j){auto&w=writes[j];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=j;w.descriptorCount=1;w.descriptorType=j<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&images[j];}
  UpdateDescriptorSets(device.device,count,writes,0,nullptr);CmdBindPipeline(f.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipes[which]);CmdBindDescriptorSets(f.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layouts[which],0,1,&set,0,nullptr);
  Push push{scale,threshold,view,cutoff};CmdPushConstants(f.cmd,layouts[which],VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,&push);
  slots[f.slot].serial[setIndex]=f.serial;last=f.serial;
 }
};
bool Effect::Init(const WXL_VkRenderApi*api){
 if(state_)return true;
 if(!api||api->apiVersion!=WXL_VK_RENDER_API_VERSION||api->structSize<offsetof(WXL_VkRenderApi,RetireImage)+sizeof(api->RetireImage)||!api->GetDeviceInfo||!api->CreateImage||!api->RetireImage)return false;
 auto*s=new(std::nothrow)Impl;if(!s)return false;
 s->api.structSize=api->structSize;s->api.apiVersion=api->apiVersion;s->api.GetDeviceInfo=api->GetDeviceInfo;s->api.CreateImage=api->CreateImage;s->api.RetireImage=api->RetireImage;
 s->device.structSize=sizeof s->device;
 if(!api->GetDeviceInfo(&s->device)||!s->device.device||!s->device.physicalDevice||!s->device.getDeviceProcAddr||!s->device.getInstanceProcAddr||!s->device.frameSlots||s->device.frameSlots>8){delete s;return false;}
#define LOAD(name) s->name=reinterpret_cast<PFN_vk##name>(s->device.getDeviceProcAddr(s->device.device,"vk" #name));if(!s->name){delete s;return false;}
 MASK_FUNCTIONS(LOAD)
#undef LOAD
 state_=s;auto fail=[&](){Shutdown(0);return false;};
 auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(s->device.getInstanceProcAddr(s->device.instance,"vkGetPhysicalDeviceFormatProperties"));
 if(!properties)return fail();VkFormatProperties r8{};properties(s->device.physicalDevice,VK_FORMAT_R8_UNORM,&r8);
 const VkFormatFeatureFlags required=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
 if((r8.optimalTilingFeatures&required)!=required)return fail();
 VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_NEAREST;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
 if(s->CreateSampler(s->device.device,&si,nullptr,&s->sampler)!=VK_SUCCESS)return fail();
 const uint32_t*code[2]={HdFsrMasksSpv,HdFsrMasksDebugSpv};const size_t bytes[2]={sizeof HdFsrMasksSpv,sizeof HdFsrMasksDebugSpv};
 for(uint32_t i=0;i<2;++i){
  VkDescriptorSetLayoutBinding binding[4]{};for(uint32_t j=0;j<4;++j){binding[j].binding=j;binding[j].descriptorCount=1;binding[j].descriptorType=j<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;binding[j].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
  VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=i==0?4:3;dl.pBindings=binding;
  if(s->CreateDescriptorSetLayout(s->device.device,&dl,nullptr,&s->descriptors[i])!=VK_SUCCESS)return fail();
  VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&s->descriptors[i];pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&range;
  if(s->CreatePipelineLayout(s->device.device,&pl,nullptr,&s->layouts[i])!=VK_SUCCESS)return fail();
  VkShaderModule module{};VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=bytes[i];sm.pCode=code[i];
  if(s->CreateShaderModule(s->device.device,&sm,nullptr,&module)!=VK_SUCCESS)return fail();
  VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.layout=s->layouts[i];cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=module;cp.stage.pName="main";
  VkResult result=s->CreateComputePipelines(s->device.device,s->device.pipelineCache,1,&cp,nullptr,&s->pipes[i]);s->DestroyShaderModule(s->device.device,module,nullptr);if(result!=VK_SUCCESS)return fail();
 }
 uint32_t count=s->device.frameSlots*3;VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,count*2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,count*2}};
 VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=count;dp.poolSizeCount=2;dp.pPoolSizes=sizes;if(s->CreateDescriptorPool(s->device.device,&dp,nullptr,&s->pool)!=VK_SUCCESS)return fail();
 std::array<VkDescriptorSetLayout,24>dsl{};std::array<VkDescriptorSet,24>sets{};for(uint32_t i=0;i<count;++i)dsl[i]=s->descriptors[i%3==2?1:0];
 VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};alloc.descriptorPool=s->pool;alloc.descriptorSetCount=count;alloc.pSetLayouts=dsl.data();if(s->AllocateDescriptorSets(s->device.device,&alloc,sets.data())!=VK_SUCCESS)return fail();
 for(uint32_t i=0;i<count;++i)s->slots[i/3].sets[i%3]=sets[i];return true;
}
bool Effect::Resize(VkExtent2D extent,VkFormat format){
 auto*s=state_;if(!s)return false;
 // The source image is the LDR main world target; reject sRGB/HDR until its colour-space contract is ported.
 if(!extent.width||!extent.height||extent.width>16384||extent.height>16384||(format!=VK_FORMAT_R8G8B8A8_UNORM&&format!=VK_FORMAT_B8G8R8A8_UNORM))return s->Reject("R1 unsupported size/colour format");
 if(s->snapshot.handle&&s->extent.width==extent.width&&s->extent.height==extent.height&&s->format==format)return true;
 s->sequence.Fail();s->finished=0;s->Drop(s->snapshot);s->Drop(s->reactive);s->Drop(s->composition);s->extent=extent;s->format=format;
 auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(s->device.getInstanceProcAddr(s->device.instance,"vkGetPhysicalDeviceFormatProperties"));VkFormatProperties props{};properties(s->device.physicalDevice,format,&props);
 const auto required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
 if((props.optimalTilingFeatures&required)!=required)return s->Reject("R1 source format not copyable/sampleable");
 bool ok=s->Make(s->snapshot,format,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT)&&s->Make(s->reactive,VK_FORMAT_R8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT)&&s->Make(s->composition,VK_FORMAT_R8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
 if(!ok){s->Drop(s->snapshot);s->Drop(s->reactive);s->Drop(s->composition);return s->Reject("R1 image allocation failed");}
 ++s->stats.resizes;s->error="";return true;
}
void Effect::BeginFrame(uint64_t serial,float scale,float threshold,float cutoff){if(auto*s=state_){s->sequence.Begin(serial);s->scale=Scale(scale);s->threshold=Threshold(threshold);s->cutoff=Cutoff(cutoff);s->finished=0;s->error="";if(serial<=s->last)s->Reject("R1 repeated/nonmonotonic frame");}}
void Effect::FailFrame(){if(state_)state_->Reject("R1 boundary/source unavailable");}
bool Effect::Capture(const Frame&f,uint32_t segment,const Image&source){
 auto*s=state_;if(!s)return false;
 if(segment>1||!s->FrameValid(f)||!s->SourceValid(source)||!s->sequence.Accept(f.serial,segment*2))return s->Reject("R1 capture order/frame/source invalid");
 // Do not allocate here. The two passes can be recorded while earlier frames use these same images:
 // all consumers/writers are on the ONE ordered main queue; transitions cover previous SDK reads too.
 if(segment==0){for(auto*i:{&s->reactive,&s->composition}){s->Transition(f.cmd,*i,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT);VkClearColorValue zero{};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};s->CmdClearColorImage(f.cmd,i->image.image,i->layout,&zero,1,&range);s->Transition(f.cmd,*i,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);}}
 s->Transition(f.cmd,s->snapshot,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT);
 VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={s->extent.width,s->extent.height,1};s->CmdCopyImage(f.cmd,source.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s->snapshot.image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
 s->Transition(f.cmd,s->snapshot,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_READ_BIT);s->last=f.serial;++s->stats.captures;return true;
}
bool Effect::Compare(const Frame&f,uint32_t segment,const Image&source){
 auto*s=state_;if(!s)return false;
 if(segment>1||!s->FrameValid(f)||!s->SourceValid(source)||!s->sequence.Accept(f.serial,segment*2+1))return s->Reject("R1 compare order/frame/source invalid");
 auto&slot=s->slots[f.slot];if(slot.serial[segment]>f.completed)return s->Reject("R1 descriptor slot pending/duplicate");
 // Includes segment 0 -> 1 accumulation (shader read/write -> shader read/write).
 s->Transition(f.cmd,s->reactive,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);s->Transition(f.cmd,s->composition,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
 VkDescriptorImageInfo images[4]={{s->sampler,s->snapshot.image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{s->sampler,source.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,s->reactive.image.view,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,s->composition.image.view,VK_IMAGE_LAYOUT_GENERAL}};
 s->Bind(f,0,segment,images,4,0);s->CmdDispatch(f.cmd,(s->extent.width+7)/8,(s->extent.height+7)/8,1);++s->stats.comparisons;return true;
}
bool Effect::Finish(const Frame&f,Image&reactive,Image&composition){
 reactive={};composition={};auto*s=state_;if(!s)return false;
 if(!s->FrameValid(f)||!s->sequence.Ready(f.serial)||s->finished==f.serial)return s->Reject("R1 incomplete/stale/duplicate mask pair");
 s->Transition(f.cmd,s->reactive,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_READ_BIT);s->Transition(f.cmd,s->composition,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_READ_BIT);
 reactive=s->reactive.image;composition=s->composition.image;s->finished=s->last=f.serial;++s->stats.ready;s->error="";return true;
}
bool Effect::Debug(const Frame&f,const Image&output,uint32_t view){
 auto*s=state_;if(!s)return false;
 if(!s->FrameValid(f)||s->finished!=f.serial||(view!=2&&view!=3)||!output.image||!output.view||output.format!=VK_FORMAT_R8G8B8A8_UNORM||!output.extent.width||!output.extent.height||output.extent.width>16384||output.extent.height>16384||output.image==s->snapshot.image.image||output.image==s->reactive.image.image||output.image==s->composition.image.image)return s->Reject("R1 debug unavailable");
 if(s->slots[f.slot].serial[2]>f.completed)return s->Reject("R1 debug descriptor slot pending");
 // Earlier SDK consumers and earlier blit reads of output precede this diagnostic write.
 VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
 s->CmdPipelineBarrier(f.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
 VkDescriptorImageInfo images[3]={{s->sampler,s->reactive.image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{s->sampler,s->composition.image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,output.view,VK_IMAGE_LAYOUT_GENERAL}};
 s->Bind(f,1,2,images,3,view);s->CmdDispatch(f.cmd,(output.extent.width+7)/8,(output.extent.height+7)/8,1);return true;
}
bool Effect::Shutdown(uint64_t completed){
 auto*s=state_;if(!s)return true;if(completed<s->last){s->error="R1 shutdown pending serial";return false;}
 s->Drop(s->snapshot);s->Drop(s->reactive);s->Drop(s->composition);
 for(auto p:s->pipes)if(p)s->DestroyPipeline(s->device.device,p,nullptr);if(s->pool)s->DestroyDescriptorPool(s->device.device,s->pool,nullptr);
 for(auto p:s->layouts)if(p)s->DestroyPipelineLayout(s->device.device,p,nullptr);for(auto d:s->descriptors)if(d)s->DestroyDescriptorSetLayout(s->device.device,d,nullptr);
 if(s->sampler)s->DestroySampler(s->device.device,s->sampler,nullptr);delete s;state_=nullptr;return true;
}
uint64_t Effect::LastUsed()const{return state_?state_->last:0;}
const char*Effect::Error()const{return state_?state_->error:"R1 not initialized";}
Stats Effect::ReadStats()const{return state_?state_->stats:Stats{};}
}
#endif
