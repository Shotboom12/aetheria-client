#include "LvGpu.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <vector>
namespace HD::Lumen {
namespace {
#include "shaders/LampSurfaces.spv.h"
#include "shaders/LampVisibility.spv.h"
#include "shaders/LampHalos.spv.h"
#include "shaders/LampBlur.spv.h"
#include "shaders/FogTemporal.spv.h"
#include "shaders/EdgeOccluders.spv.h"
#include "shaders/MemoryKeep.spv.h"
#include "shaders/EffectGather.spv.h"
#include "shaders/EffectReduce.spv.h"
#include "shaders/Fog1Memory0.spv.h"
#include "shaders/Fog1Memory1.spv.h"
#include "shaders/Fog2Memory0.spv.h"
#include "shaders/Fog2Memory1.spv.h"
#include "shaders/Fog3Memory0.spv.h"
#include "shaders/Fog3Memory1.spv.h"
#include "shaders/MemorySeed.spv.h"
#include "shaders/MemoryResolve.spv.h"
#include "shaders/MemoryScatter.spv.h"
#include "shaders/MemoryRealign.spv.h"
#include "shaders/MemoryFill.spv.h"
#include "shaders/DepthCopy.spv.h"
#include "shaders/SceneComposite.spv.h"

struct Code {const uint32_t*data;size_t size;};
constexpr Code Codes[]={{kLampSurfaces,sizeof(kLampSurfaces)},{kLampVisibility,sizeof(kLampVisibility)},{kLampHalos,sizeof(kLampHalos)},{kLampBlur,sizeof(kLampBlur)},{kFogTemporal,sizeof(kFogTemporal)},{kEdgeOccluders,sizeof(kEdgeOccluders)},{kMemoryKeep,sizeof(kMemoryKeep)},{kEffectGather,sizeof(kEffectGather)},{kEffectReduce,sizeof(kEffectReduce)},{kFog1Memory0,sizeof(kFog1Memory0)},{kFog1Memory1,sizeof(kFog1Memory1)},{kFog2Memory0,sizeof(kFog2Memory0)},{kFog2Memory1,sizeof(kFog2Memory1)},{kFog3Memory0,sizeof(kFog3Memory0)},{kFog3Memory1,sizeof(kFog3Memory1)},{kMemorySeed,sizeof(kMemorySeed)},{kMemoryResolve,sizeof(kMemoryResolve)},{kMemoryScatter,sizeof(kMemoryScatter)},{kMemoryRealign,sizeof(kMemoryRealign)},{kMemoryFill,sizeof(kMemoryFill)},{kDepthCopy,sizeof(kDepthCopy)},{kSceneComposite,sizeof(kSceneComposite)}};
}
struct Gpu::Impl {
 const WXL_VkRenderApi*api=nullptr;WXL_VkDeviceInfo dev{};WXL_VkStageContext ctx{};
 VkDescriptorSetLayout setLayout{};VkPipelineLayout layout{};VkDescriptorPool pool{};
 std::array<VkPipeline,size_t(Kernel::Count)>pipelines{};VkSampler point{},linear{},wrap{};
 struct Slot {void*uniformHandle=nullptr;WXL_VkBufferInfo uniform{};void*uploadHandle=nullptr;WXL_VkBufferInfo upload{};uint64_t serial=0;uint32_t cursor=0;size_t uploaded=0;std::array<VkDescriptorSet,Dispatches>sets{};};
 std::array<Slot,Slots>slots{};Target dummy;std::array<Target,64> retiredVolumes{};VkPhysicalDeviceMemoryProperties memory{};
 PFN_vkCreateQueryPool CreateQueryPool=nullptr;PFN_vkDestroyQueryPool DestroyQueryPool=nullptr;PFN_vkCmdResetQueryPool CmdResetQueryPool=nullptr;PFN_vkCmdWriteTimestamp CmdWriteTimestamp=nullptr;PFN_vkGetQueryPoolResults GetQueryPoolResults=nullptr; // HD G1
 double tsPeriod=0;uint32_t tsBits=0;std::array<VkQueryPool,Slots>tsPool{};std::array<bool,Slots>tsReset{};std::array<uint32_t,Slots>tsCount{};std::array<std::array<uint8_t,Dispatches>,Slots>tsKernel{}; // HD G1
 uint32_t frameSlots=0;VkDeviceSize stride=2048;uint64_t last=0;bool open=false;const char*error="";
 static constexpr size_t UploadBytes=8u*1024*1024;
#define LV_GPU_FUNCTIONS(X) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateSampler) X(DestroySampler) X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) X(CmdPipelineBarrier) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdDispatch) X(CmdCopyBufferToImage) X(CmdCopyImage) X(CmdClearColorImage) X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindImageMemory) X(CreateImageView) X(DestroyImageView) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CmdCopyImageToBuffer)
#define DECL(n) PFN_vk##n n=nullptr;
 LV_GPU_FUNCTIONS(DECL)
#undef DECL
 void Barrier(Target&t,VkImageLayout next,VkAccessFlags dst){
  if(GpuTiming::on)++GpuTiming::barriers; // HD G1
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=t.layout;b.newLayout=next;b.image=t.info.image;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,t.mipLevels,0,1};b.srcAccessMask=t.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;b.dstAccessMask=dst;
  CmdPipelineBarrier(ctx.cmd,t.layout==VK_IMAGE_LAYOUT_UNDEFINED?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);t.layout=next;t.readReady=next==VK_IMAGE_LAYOUT_GENERAL&&dst==VK_ACCESS_SHADER_READ_BIT; /* HD L21 */t.last=ctx.serial;last=std::max(last,ctx.serial);
 }
};
bool Gpu::Init(const WXL_VkRenderApi*api){
 if(s_)return true;
 if(!api||api->apiVersion!=WXL_VK_RENDER_API_VERSION||api->structSize<offsetof(WXL_VkRenderApi,RetireImage)+sizeof(api->RetireImage)||!api->GetDeviceInfo||!api->CreateBuffer||!api->RetireBuffer||!api->CreateImage||!api->RetireImage||!api->FlushBuffer)return false;
 auto*p=new(std::nothrow)Impl;if(!p)return false;s_=p;p->api=api;p->dev.structSize=sizeof(p->dev);
 auto fail=[&](const char*why){p->error=why;lastError_=why;Shutdown(0);return false;};
 if(!api->GetDeviceInfo(&p->dev)||!p->dev.device||!p->dev.getDeviceProcAddr||!p->dev.frameSlots||p->dev.frameSlots>Slots)return fail("unsupported device/frame slots");p->frameSlots=p->dev.frameSlots;
#define LOAD(n) p->n=reinterpret_cast<PFN_vk##n>(p->dev.getDeviceProcAddr(p->dev.device,"vk"#n));if(!p->n)return fail("missing vk"#n);
 LV_GPU_FUNCTIONS(LOAD)
#undef LOAD
 if(!p->dev.getInstanceProcAddr)return fail("missing instance proc address");
 auto mem=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(p->dev.getInstanceProcAddr(p->dev.instance,"vkGetPhysicalDeviceMemoryProperties"));if(!mem)return fail("memory properties unavailable");mem(p->dev.physicalDevice,&p->memory);
 auto props=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(p->dev.getInstanceProcAddr(p->dev.instance,"vkGetPhysicalDeviceProperties"));if(!props)return fail("physical limits unavailable");VkPhysicalDeviceProperties limits{};props(p->dev.physicalDevice,&limits);
 auto queues=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(p->dev.getInstanceProcAddr(p->dev.instance,"vkGetPhysicalDeviceQueueFamilyProperties"));if(!queues)return fail("queue properties unavailable");uint32_t queueCount=0;queues(p->dev.physicalDevice,&queueCount,nullptr);std::vector<VkQueueFamilyProperties> families(queueCount);queues(p->dev.physicalDevice,&queueCount,families.data());if(p->dev.queueFamily>=queueCount||!(families[p->dev.queueFamily].queueFlags&VK_QUEUE_COMPUTE_BIT))return fail("native queue lacks compute");
 { // HD G1: optional timestamp pools (made once, never per frame); without them hdLvGpuTimes writes nothing
  p->CreateQueryPool=reinterpret_cast<PFN_vkCreateQueryPool>(p->dev.getDeviceProcAddr(p->dev.device,"vkCreateQueryPool"));p->DestroyQueryPool=reinterpret_cast<PFN_vkDestroyQueryPool>(p->dev.getDeviceProcAddr(p->dev.device,"vkDestroyQueryPool"));p->CmdResetQueryPool=reinterpret_cast<PFN_vkCmdResetQueryPool>(p->dev.getDeviceProcAddr(p->dev.device,"vkCmdResetQueryPool"));p->CmdWriteTimestamp=reinterpret_cast<PFN_vkCmdWriteTimestamp>(p->dev.getDeviceProcAddr(p->dev.device,"vkCmdWriteTimestamp"));p->GetQueryPoolResults=reinterpret_cast<PFN_vkGetQueryPoolResults>(p->dev.getDeviceProcAddr(p->dev.device,"vkGetQueryPoolResults"));
  if(p->CreateQueryPool&&p->DestroyQueryPool&&p->CmdResetQueryPool&&p->CmdWriteTimestamp&&p->GetQueryPoolResults&&limits.limits.timestampComputeAndGraphics&&families[p->dev.queueFamily].timestampValidBits&&limits.limits.timestampPeriod>0){
   p->tsBits=families[p->dev.queueFamily].timestampValidBits;p->tsPeriod=limits.limits.timestampPeriod;
   for(uint32_t f=0;f<p->frameSlots;++f){VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qp.queryType=VK_QUERY_TYPE_TIMESTAMP;qp.queryCount=2*Dispatches;if(p->CreateQueryPool(p->dev.device,&qp,nullptr,&p->tsPool[f])!=VK_SUCCESS){for(auto&q:p->tsPool)if(q){p->DestroyQueryPool(p->dev.device,q,nullptr);q=VK_NULL_HANDLE;}p->tsBits=0;break;}}
  }
  GpuTiming::available=p->tsBits!=0;
 }
 auto align=std::max<VkDeviceSize>(1,limits.limits.minUniformBufferOffsetAlignment);p->stride=(sizeof(Uniforms)+align-1)/align*align;
 if(limits.limits.maxUniformBufferRange<sizeof(Uniforms)||limits.limits.maxPerStageDescriptorSampledImages<16||limits.limits.maxPerStageDescriptorStorageImages<2)return fail("descriptor limits insufficient");
 std::array<VkDescriptorSetLayoutBinding,22>b{};b[0]={0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};for(uint32_t i=1;i<=3;++i)b[i]={i,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};for(uint32_t i=4;i<20;++i)b[i]={i,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};for(uint32_t i=20;i<22;++i)b[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
 VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};si.bindingCount=uint32_t(b.size());si.pBindings=b.data();if(p->CreateDescriptorSetLayout(p->dev.device,&si,nullptr,&p->setLayout)!=VK_SUCCESS)return fail("descriptor layout");
 VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&p->setLayout;if(p->CreatePipelineLayout(p->dev.device,&li,nullptr,&p->layout)!=VK_SUCCESS)return fail("pipeline layout");
 for(size_t i=0;i<size_t(Kernel::Count);++i){VkShaderModule module{};VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=Codes[i].size;mi.pCode=Codes[i].data;if(p->CreateShaderModule(p->dev.device,&mi,nullptr,&module)!=VK_SUCCESS)return fail("shader module");VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=module;ci.stage.pName="CSMain";ci.layout=p->layout;auto result=p->CreateComputePipelines(p->dev.device,p->dev.pipelineCache,1,&ci,nullptr,&p->pipelines[i]);p->DestroyShaderModule(p->dev.device,module,nullptr);if(result!=VK_SUCCESS)return fail("compute pipeline");}
 const uint32_t count=p->frameSlots*Dispatches;VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count},{VK_DESCRIPTOR_TYPE_SAMPLER,count*3},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,count*16},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,count*2}};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=count;pi.poolSizeCount=4;pi.pPoolSizes=sizes;if(p->CreateDescriptorPool(p->dev.device,&pi,nullptr,&p->pool)!=VK_SUCCESS)return fail("descriptor pool");
 for(uint32_t f=0;f<p->frameSlots;++f){auto&slot=p->slots[f];WXL_VkBufferDesc d{p->stride*Dispatches,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,WXL_VK_MEMORY_STREAM};slot.uniformHandle=api->CreateBuffer(&d,&slot.uniform);d={Impl::UploadBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,WXL_VK_MEMORY_STREAM};slot.uploadHandle=api->CreateBuffer(&d,&slot.upload);if(!slot.uniformHandle||!slot.uploadHandle||!slot.uniform.mapped||!slot.upload.mapped)return fail("frame buffers");std::array<VkDescriptorSetLayout,Dispatches>ls;ls.fill(p->setLayout);VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=p->pool;ai.descriptorSetCount=Dispatches;ai.pSetLayouts=ls.data();if(p->AllocateDescriptorSets(p->dev.device,&ai,slot.sets.data())!=VK_SUCCESS)return fail("descriptor sets");}
 for(int i=0;i<3;++i){VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.magFilter=sc.minFilter=i?VK_FILTER_LINEAR:VK_FILTER_NEAREST;sc.mipmapMode=i?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;sc.addressModeU=sc.addressModeV=sc.addressModeW=i==2?VK_SAMPLER_ADDRESS_MODE_REPEAT:VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sc.maxLod=32;VkSampler*target=i==0?&p->point:i==1?&p->linear:&p->wrap;if(p->CreateSampler(p->dev.device,&sc,nullptr,target)!=VK_SUCCESS)return fail("sampler");}
 if(!Create(p->dummy,1,1,VK_FORMAT_R32G32B32A32_SFLOAT))return fail("dummy image");return true;
}
bool Gpu::Create(Target&t,uint32_t w,uint32_t h,VkFormat format,uint32_t mips){
 if(!s_||!w||!h||w>16384||h>16384||t.handle||!mips||mips>15||((std::max(w,h)>>(mips-1))==0))return false;
 WXL_VkImageDesc d{};d.width=w;d.height=h;d.format=format;d.mipLevels=mips;d.aspect=VK_IMAGE_ASPECT_COLOR_BIT;d.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
 t.handle=s_->api->CreateImage(&d,&t.info);if(!t.handle)return false;t.extent={w,h};t.format=format;t.mipLevels=mips;return true;
}
bool Gpu::CreateVolume(Target&t,uint32_t side,VkFormat format,uint32_t mips){
 if(!s_||t.handle||!side||side>128||!mips||mips>8||(side>>(mips-1))==0)return false;
 VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_3D;ci.format=format;ci.extent={side,side,side};ci.mipLevels=mips;ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 if(s_->CreateImage(s_->dev.device,&ci,nullptr,&t.info.image)!=VK_SUCCESS)return false;
 VkMemoryRequirements req{};s_->GetImageMemoryRequirements(s_->dev.device,t.info.image,&req);uint32_t type=UINT32_MAX;
 for(uint32_t i=0;i<s_->memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(s_->memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=i;break;}
 VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
 if(type==UINT32_MAX||s_->AllocateMemory(s_->dev.device,&ai,nullptr,&t.volumeMemory)!=VK_SUCCESS){s_->DestroyImage(s_->dev.device,t.info.image,nullptr);t={};return false;}
 auto cleanup=[&](){s_->DestroyImage(s_->dev.device,t.info.image,nullptr);s_->FreeMemory(s_->dev.device,t.volumeMemory,nullptr);t={};return false;};
 if(s_->BindImageMemory(s_->dev.device,t.info.image,t.volumeMemory,0)!=VK_SUCCESS)return cleanup();
 VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=t.info.image;vi.viewType=VK_IMAGE_VIEW_TYPE_3D;vi.format=format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,mips,0,1};if(s_->CreateImageView(s_->dev.device,&vi,nullptr,&t.info.view)!=VK_SUCCESS)return cleanup();
 t.handle=reinterpret_cast<void*>(uintptr_t(1));t.extent={side,side};t.depth=side;t.format=format;t.mipLevels=mips;return true;
}
void Gpu::Retire(Target&t){if(!s_||!t.handle)return;
 if(t.volumeMemory){if(t.last>s_->ctx.completedSerial){for(auto&r:s_->retiredVolumes)if(!r.handle){r=t;t={};return;}s_->error="volume retirement backlog full; keep target alive";return;}s_->DestroyImageView(s_->dev.device,t.info.view,nullptr);s_->DestroyImage(s_->dev.device,t.info.image,nullptr);s_->FreeMemory(s_->dev.device,t.volumeMemory,nullptr);}else s_->api->RetireImage(t.handle,t.last);t={};}
bool Gpu::Begin(const WXL_VkStageContext&ctx){if(!s_||!ctx.cmd||ctx.frameSlot>=s_->frameSlots||ctx.frameSlots!=s_->frameSlots||!ctx.serial)return false;auto&slot=s_->slots[ctx.frameSlot];if(slot.serial!=ctx.serial){if(slot.serial>ctx.completedSerial)return false;
  if(auto pool=s_->tsPool[ctx.frameSlot]){const uint32_t fs=ctx.frameSlot; // HD G1: read the finished frame of this slot, then reset its timestamps
   if(s_->tsCount[fs]){const uint64_t mask=s_->tsBits>=64?~uint64_t(0):((uint64_t(1)<<s_->tsBits)-1);for(uint32_t i=0;i<s_->tsCount[fs];++i){uint64_t r[4]{};VkResult got=s_->GetQueryPoolResults(s_->dev.device,pool,2*i,2,sizeof r,r,2*sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);if((got==VK_SUCCESS||got==VK_NOT_READY)&&r[1]&&r[3]){const uint32_t k=s_->tsKernel[fs][i];GpuTiming::ms[k]+=double((r[2]-r[0])&mask)*s_->tsPeriod*1e-6;++GpuTiming::runs[k];}}++GpuTiming::frames;}
   s_->tsCount[fs]=0;s_->tsReset[fs]=false;if(GpuTiming::on){s_->CmdResetQueryPool(ctx.cmd,pool,0,2*Dispatches);s_->tsReset[fs]=true;}}
  slot.serial=ctx.serial;slot.cursor=0;slot.uploaded=0;}s_->ctx=ctx;for(auto&r:s_->retiredVolumes)if(r.handle&&r.last<=ctx.completedSerial)Retire(r);s_->open=true;if(s_->dummy.layout==VK_IMAGE_LAYOUT_UNDEFINED)Clear(s_->dummy,{0,0,0,0});return true;}
bool Gpu::Run(Kernel which,const Uniforms&uniforms,const std::array<Texture,16>&textures,Target&out,Target*second,VkExtent2D dispatch){
 if(!s_||!s_->open||!out.handle||size_t(which)>=size_t(Kernel::Count))return false;auto&slot=s_->slots[s_->ctx.frameSlot];if(slot.cursor>=Dispatches){s_->error="per-frame dispatch budget exceeded";return false;}
 VkFormat format=VK_FORMAT_R16G16B16A16_SFLOAT;
 switch(which){case Kernel::EffectGather:case Kernel::EffectReduce:format=VK_FORMAT_R32G32B32A32_SFLOAT;break;case Kernel::MemorySeed:case Kernel::MemoryScatter:case Kernel::MemoryRealign:format=VK_FORMAT_R32_UINT;break;case Kernel::MemoryKeep:case Kernel::MemoryResolve:case Kernel::MemoryFill:case Kernel::DepthCopy:format=VK_FORMAT_R32_SFLOAT;break;case Kernel::SceneComposite:format=VK_FORMAT_R8G8B8A8_UNORM;break;default:break;}
 if(out.depth!=1||out.format!=format)return false;
 if((which==Kernel::EffectGather)!=(second!=nullptr))return false;
 if(which>=Kernel::Fog1Memory0&&which<=Kernel::Fog3Memory1&&!textures[1].view)return false;
 if(second&&second->format!=VK_FORMAT_R32G32B32A32_SFLOAT)return false;
 if(second&&(!second->handle||second->extent.width!=out.extent.width||second->extent.height!=out.extent.height))return false;
 for(const auto&t:textures)if(t.view&&(t.view==out.info.view||(second&&t.view==second->info.view))){s_->error="input/output image alias rejected";return false;}
 const uint32_t index=slot.cursor++;const VkDeviceSize offset=index*s_->stride;std::memcpy(static_cast<char*>(slot.uniform.mapped)+offset,&uniforms,sizeof uniforms);s_->api->FlushBuffer(slot.uniformHandle);
 s_->Barrier(out,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT);if(second)s_->Barrier(*second,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT);
 std::array<VkDescriptorImageInfo,21>images{};images[0].sampler=s_->point;images[1].sampler=s_->linear;images[2].sampler=s_->wrap;
 for(size_t i=0;i<16;++i){images[3+i].imageView=textures[i].view?textures[i].view:s_->dummy.info.view;images[3+i].imageLayout=textures[i].view?textures[i].layout:VK_IMAGE_LAYOUT_GENERAL;}
 images[19]={VK_NULL_HANDLE,out.info.view,VK_IMAGE_LAYOUT_GENERAL};images[20]={VK_NULL_HANDLE,second?second->info.view:s_->dummy.info.view,VK_IMAGE_LAYOUT_GENERAL};
 VkDescriptorBufferInfo buffer{slot.uniform.buffer,offset,sizeof(Uniforms)};std::array<VkWriteDescriptorSet,22>writes{};for(uint32_t i=0;i<writes.size();++i){auto&w=writes[i];w={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=slot.sets[index];w.dstBinding=i;w.descriptorCount=1;w.descriptorType=i==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i<4?VK_DESCRIPTOR_TYPE_SAMPLER:i<20?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;if(!i)w.pBufferInfo=&buffer;else w.pImageInfo=&images[i-1];}
 s_->UpdateDescriptorSets(s_->dev.device,uint32_t(writes.size()),writes.data(),0,nullptr);s_->CmdBindPipeline(s_->ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s_->pipelines[size_t(which)]);s_->CmdBindDescriptorSets(s_->ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s_->layout,0,1,&slot.sets[index],0,nullptr);const uint32_t tsSlot=s_->ctx.frameSlot,tsAt=s_->tsCount[tsSlot];const bool timed=GpuTiming::on&&s_->tsReset[tsSlot]&&tsAt<Dispatches;if(timed)s_->CmdWriteTimestamp(s_->ctx.cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,s_->tsPool[tsSlot],2*tsAt); // HD G1
 s_->CmdDispatch(s_->ctx.cmd,((dispatch.width?dispatch.width:out.extent.width)+7)/8,((dispatch.height?dispatch.height:out.extent.height)+7)/8,1);
 if(timed){s_->CmdWriteTimestamp(s_->ctx.cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,s_->tsPool[tsSlot],2*tsAt+1);s_->tsKernel[tsSlot][tsAt]=uint8_t(which);s_->tsCount[tsSlot]=tsAt+1;}if(GpuTiming::on)++GpuTiming::dispatches; // HD G1
 s_->Barrier(out,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT);if(second)s_->Barrier(*second,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT);return true;
}
bool Gpu::Upload(Target&t,const void*bytes,size_t count){return UploadMip(t,0,bytes,count);}
bool Gpu::UploadMip(Target&t,uint32_t level,const void*bytes,size_t count){
 if(!s_||!s_->open||!t.handle||!bytes||!count||level>=t.mipLevels)return false;
 uint32_t w=std::max(1u,t.extent.width>>level),h=std::max(1u,t.extent.height>>level),depth=std::max(1u,t.depth>>level);
 size_t texel=t.format==VK_FORMAT_R32G32B32A32_SFLOAT?16:t.format==VK_FORMAT_R16G16B16A16_SFLOAT?8:t.format==VK_FORMAT_R8_UNORM?1:4;
 if(count!=size_t(w)*h*depth*texel)return false;auto&slot=s_->slots[s_->ctx.frameSlot];size_t at=(slot.uploaded+255)&~size_t(255);if(at>Impl::UploadBytes||count>Impl::UploadBytes-at)return false;
 std::memcpy(static_cast<char*>(slot.upload.mapped)+at,bytes,count);slot.uploaded=at+count;s_->api->FlushBuffer(slot.uploadHandle);s_->Barrier(t,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT);VkBufferImageCopy c{};c.bufferOffset=at;c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1};c.imageExtent={w,h,depth};s_->CmdCopyBufferToImage(s_->ctx.cmd,slot.upload.buffer,t.info.image,t.layout,1,&c);s_->Barrier(t,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT);return true;
}
bool Gpu::Copy(Target&source,Target&dest){if(!s_||!s_->open||!source.handle||!dest.handle||source.info.image==dest.info.image||source.format!=dest.format||source.extent.width!=dest.extent.width||source.extent.height!=dest.extent.height||source.depth!=1||dest.depth!=1)return false;s_->Barrier(source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT);s_->Barrier(dest,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT);VkImageCopy c{};c.srcSubresource=c.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.extent={source.extent.width,source.extent.height,1};s_->CmdCopyImage(s_->ctx.cmd,source.info.image,source.layout,dest.info.image,dest.layout,1,&c);s_->Barrier(source,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT);s_->Barrier(dest,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT);return true;}
bool Gpu::CreateReadback(Readback&r,VkDeviceSize bytes){
 if(!s_||r.buffer||!bytes||bytes>512u*1024*1024)return false;VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;if(s_->CreateBuffer(s_->dev.device,&ci,nullptr,&r.buffer)!=VK_SUCCESS)return false;
 VkMemoryRequirements req{};s_->GetBufferMemoryRequirements(s_->dev.device,r.buffer,&req);uint32_t type=UINT32_MAX;for(uint32_t i=0;i<s_->memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(s_->memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;if(s_->memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)break;}
 auto fail=[&](){if(r.buffer)s_->DestroyBuffer(s_->dev.device,r.buffer,nullptr);if(r.memory)s_->FreeMemory(s_->dev.device,r.memory,nullptr);r={};return false;};if(type==UINT32_MAX)return fail();VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;if(s_->AllocateMemory(s_->dev.device,&ai,nullptr,&r.memory)!=VK_SUCCESS)return fail();if(s_->BindBufferMemory(s_->dev.device,r.buffer,r.memory,0)!=VK_SUCCESS)return fail();if(s_->MapMemory(s_->dev.device,r.memory,0,VK_WHOLE_SIZE,0,&r.mapped)!=VK_SUCCESS)return fail();r.bytes=bytes;return true;
}
bool Gpu::CopyReadback(Target&t,Readback&r){
 if(!s_||!s_->open||!t.handle||!r.buffer||r.serial)return false;size_t texel=t.format==VK_FORMAT_R32G32B32A32_SFLOAT?16:t.format==VK_FORMAT_R16G16B16A16_SFLOAT?8:4;VkDeviceSize bytes=VkDeviceSize(t.extent.width)*t.extent.height*texel;if(bytes>r.bytes)return false;
 s_->Barrier(t,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT);VkBufferImageCopy c{};c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageExtent={t.extent.width,t.extent.height,1};s_->CmdCopyImageToBuffer(s_->ctx.cmd,t.info.image,t.layout,r.buffer,1,&c);VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=r.buffer;b.size=bytes;s_->CmdPipelineBarrier(s_->ctx.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);s_->Barrier(t,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT);r.serial=r.last=s_->ctx.serial;r.usedBytes=bytes;return true;
}
const void*Gpu::Ready(const Readback&r,uint64_t done)const{return s_&&r.serial&&r.serial<=done?r.mapped:nullptr;}
bool Gpu::Consume(Readback&r,uint64_t done){if(!s_||!r.serial||r.serial>done)return false;r.serial=0;return true;}
bool Gpu::DestroyReadback(Readback&r,uint64_t done){if(!s_)return !r.buffer;if(r.last>done)return false;if(r.mapped)s_->UnmapMemory(s_->dev.device,r.memory);if(r.buffer)s_->DestroyBuffer(s_->dev.device,r.buffer,nullptr);if(r.memory)s_->FreeMemory(s_->dev.device,r.memory,nullptr);r={};return true;}
bool Gpu::CopyFrameRegion(Target&t,VkRect2D r){if(!s_||!s_->open||!t.handle||!s_->ctx.colorImage||s_->ctx.colorFormat!=t.format||r.offset.x<0||r.offset.y<0||uint64_t(r.offset.x)+r.extent.width>s_->ctx.extent.width||uint64_t(r.offset.y)+r.extent.height>s_->ctx.extent.height||t.extent.width!=r.extent.width||t.extent.height!=r.extent.height)return false;
 VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=b.newLayout=s_->ctx.colorLayout;b.image=s_->ctx.colorImage;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;s_->CmdPipelineBarrier(s_->ctx.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
 s_->Barrier(t,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT);VkImageCopy c{};c.srcSubresource=c.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.srcOffset={r.offset.x,r.offset.y,0};c.extent={r.extent.width,r.extent.height,1};s_->CmdCopyImage(s_->ctx.cmd,s_->ctx.colorImage,s_->ctx.colorLayout,t.info.image,t.layout,1,&c);s_->Barrier(t,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT);return true;}
bool Gpu::CopyFrame(Target&t){
 if(!s_||!s_->open||!t.handle||s_->ctx.colorFormat!=t.format||s_->ctx.extent.width!=t.extent.width||s_->ctx.extent.height!=t.extent.height)return false;
 VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=b.newLayout=s_->ctx.colorLayout;b.image=s_->ctx.colorImage;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;s_->CmdPipelineBarrier(s_->ctx.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
 s_->Barrier(t,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT);VkImageCopy c{};c.srcSubresource=c.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.extent={t.extent.width,t.extent.height,1};s_->CmdCopyImage(s_->ctx.cmd,s_->ctx.colorImage,s_->ctx.colorLayout,t.info.image,t.layout,1,&c);s_->Barrier(t,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT);return true;
}
bool Gpu::Clear(Target&t,std::array<float,4>value){if(!s_||!s_->open||!t.handle)return false;s_->Barrier(t,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT);VkClearColorValue color{};std::copy(value.begin(),value.end(),color.float32);VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,t.mipLevels,0,1};s_->CmdClearColorImage(s_->ctx.cmd,t.info.image,t.layout,&color,1,&range);s_->Barrier(t,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT);return true;}
// HD 2026-11-05 lot L21: a read after the barrier that already made every earlier write visible to shader reads (GENERAL)
// needs no new barrier: read after read. Any write (dispatch output, copy, clear, upload) goes through Barrier, which clears readReady.
Texture Gpu::Sample(Target&t){if(!s_||!s_->open||!t.handle)return {};if(!(t.readReady&&t.layout==VK_IMAGE_LAYOUT_GENERAL))s_->Barrier(t,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT);return {t.info.view,t.layout};}
bool Gpu::Shutdown(uint64_t completed){
 if(!s_)return true;if(s_->last>completed)return false;auto*p=s_;p->ctx.completedSerial=completed;for(auto&r:p->retiredVolumes)Retire(r);Retire(p->dummy);
 for(auto&slot:p->slots){if(slot.uniformHandle)p->api->RetireBuffer(slot.uniformHandle,slot.serial);if(slot.uploadHandle)p->api->RetireBuffer(slot.uploadHandle,slot.serial);}
 for(auto pipe:p->pipelines)if(pipe&&p->DestroyPipeline)p->DestroyPipeline(p->dev.device,pipe,nullptr);
 if(p->pool&&p->DestroyDescriptorPool)p->DestroyDescriptorPool(p->dev.device,p->pool,nullptr);if(p->layout&&p->DestroyPipelineLayout)p->DestroyPipelineLayout(p->dev.device,p->layout,nullptr);if(p->setLayout&&p->DestroyDescriptorSetLayout)p->DestroyDescriptorSetLayout(p->dev.device,p->setLayout,nullptr);
 for(auto sampler:{p->point,p->linear,p->wrap})if(sampler&&p->DestroySampler)p->DestroySampler(p->dev.device,sampler,nullptr);for(auto pool:p->tsPool)if(pool&&p->DestroyQueryPool)p->DestroyQueryPool(p->dev.device,pool,nullptr); /* HD G1 */delete p;s_=nullptr;return true;
}
const char*Gpu::Error()const{return s_?s_->error:lastError_;}uint64_t Gpu::LastUsed()const{return s_?s_->last:0;}const WXL_VkStageContext*Gpu::Context()const{return s_&&s_->open?&s_->ctx:nullptr;}
}
