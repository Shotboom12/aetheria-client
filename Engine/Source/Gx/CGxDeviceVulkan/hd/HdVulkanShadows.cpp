// SPDX-License-Identifier: GPL-3.0-or-later
// HD: Vulkan AO port. Original equations/constants: WoW/Source/WorldClient/HD/HdAO.cpp.
#include "HdVulkanShadows.h"
#include <cstring>
#include <new>
#include <cstdlib>
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"
#include <cstdio>
#include <fstream>
#include <string>


namespace HD::VulkanShadows {
namespace {
#include "shaders/shadows/HdSunContact.spv.h"
#include "shaders/shadows/HdSunBlur.spv.h"
#include "shaders/shadows/HdSunApply.spv.h"
constexpr uint32_t MaxSlots = 8, PassCount = 4;
}
struct Effect::Impl {
    const gxvk::vk::hdeffect::Api* api = nullptr;
    gxvk::vk::hdeffect::DeviceInfo device{};
    const char* error = "";
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkPipeline pipeline[3]{};
    struct Slot {
        void* handles[2]{};
        gxvk::vk::hdeffect::ImageInfo images[2]{};
        VkDescriptorSet sets[PassCount]{};
        void* uniform=nullptr;gxvk::vk::hdeffect::BufferInfo buffer{};
        bool initialized = false;
        uint64_t serial = 0;
    };
    std::array<Slot,MaxSlots> slots{};
    uint32_t width = 0,height = 0,aoWidth = 0,aoHeight = 0;
    bool half = true;
    uint32_t failedWidth=0,failedHeight=0;
    bool failedHalf=true;
    uint64_t lastSerial = 0, allocations = 0;
#define AO_FUNCTIONS(X) \
    X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CreateSampler) X(DestroySampler) X(CreateShaderModule) X(DestroyShaderModule) \
    X(CreateComputePipelines) X(DestroyPipeline) X(CmdPipelineBarrier) X(CmdBindPipeline) \
    X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch)
#define AO_DECLARE(n) PFN_vk##n n = nullptr;
    AO_FUNCTIONS(AO_DECLARE)
#undef AO_DECLARE
    void Barrier(VkCommandBuffer cmd,const Slot& slot) {
        std::array<VkImageMemoryBarrier,2> barriers{};
        for(uint32_t i=0;i<2;++i) {
            auto& b=barriers[i]; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout=slot.initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout=VK_IMAGE_LAYOUT_GENERAL; b.image=slot.images[i].image;
            b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.levelCount=b.subresourceRange.layerCount=1;
            b.srcAccessMask=slot.initialized?VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT:0;
            b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        }
        CmdPipelineBarrier(cmd,slot.initialized?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,2,barriers.data());
    }
    void Between(VkCommandBuffer cmd) {
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);
    }
};

bool Effect::Init(const gxvk::vk::hdeffect::Api* api) {
    if(state_) return true;
    // Imported shadow modules are SPIR-V 1.3 / Vulkan 1.1. Never feed them to a 1.0 device.
    if(gxvk::vk::context::Caps().deviceVersion<VK_API_VERSION_1_1 || !gxvk::vk::hdscene::Supported())return false;
    if(!api || api->structSize<offsetof(gxvk::vk::hdeffect::Api,RetireImage)+sizeof(api->RetireImage) ||
        api->apiVersion!=gxvk::vk::hdeffect::ApiVersion || !api->GetDeviceInfo || !api->CreateImage || !api->RetireImage || !api->CreateBuffer || !api->RetireBuffer || !api->FlushBuffer) return false;
    void* storage=std::malloc(sizeof(Impl)); if(!storage) return false;
    auto* s=new(storage) Impl();
    s->api=api; s->device.structSize=sizeof s->device;
    if(!api->GetDeviceInfo(&s->device) || !s->device.device || !s->device.getDeviceProcAddr || !s->device.getInstanceProcAddr ||
        !s->device.frameSlots || s->device.frameSlots>MaxSlots) { s->~Impl();std::free(s);return false; }
#define AO_LOAD(n) s->n=reinterpret_cast<PFN_vk##n>(s->device.getDeviceProcAddr(s->device.device,"vk" #n)); \
    if(!s->n) { s->~Impl();std::free(s);return false; }
    AO_FUNCTIONS(AO_LOAD)
#undef AO_LOAD
    state_=s;
    auto fail=[&](const char* why) { s->error=why;Shutdown(0);return false; };
    auto formats=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(s->device.getInstanceProcAddr(s->device.instance,"vkGetPhysicalDeviceFormatProperties"));
    if(!formats) return fail("format query missing");
    VkFormatProperties format{};formats(s->device.physicalDevice,VK_FORMAT_R8G8B8A8_UNORM,&format);
    constexpr auto required=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if((format.optimalTilingFeatures&required)!=required) return fail("RGBA8 storage/sample format unavailable");
    std::array<VkDescriptorSetLayoutBinding,13> bindings{};
    for(uint32_t i=0;i<13;++i) {
        bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].descriptorType=i<10?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:(i<12?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    }
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sl.bindingCount=13;sl.pBindings=bindings.data();
    if(s->CreateDescriptorSetLayout(s->device.device,&sl,nullptr,&s->setLayout)!=VK_SUCCESS) return fail("descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,16};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount=1;pl.pSetLayouts=&s->setLayout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
    if(s->CreatePipelineLayout(s->device.device,&pl,nullptr,&s->pipelineLayout)!=VK_SUCCESS) return fail("pipeline layout");
    const uint32_t* code[3]={kHdSunContact,kHdSunBlur,kHdSunApply};
    const size_t bytes[3]={sizeof kHdSunContact,sizeof kHdSunBlur,sizeof kHdSunApply};
    for(uint32_t i=0;i<3;++i) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=bytes[i];ci.pCode=code[i];
        VkShaderModule module=VK_NULL_HANDLE;
        if(s->CreateShaderModule(s->device.device,&ci,nullptr,&module)!=VK_SUCCESS) return fail("shader module");
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module=module;cp.stage.pName="main";cp.layout=s->pipelineLayout;
        VkResult result=s->CreateComputePipelines(s->device.device,s->device.pipelineCache,1,&cp,nullptr,&s->pipeline[i]);
        s->DestroyShaderModule(s->device.device,module,nullptr);
        if(result!=VK_SUCCESS) return fail("compute pipeline");
    }
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(s->CreateSampler(s->device.device,&sampler,nullptr,&s->sampler)!=VK_SUCCESS) return fail("nearest sampler");
    uint32_t count=PassCount*s->device.frameSlots;
    VkDescriptorPoolSize sizes[3]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,10*count},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2*count},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=count;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
    if(s->CreateDescriptorPool(s->device.device,&pool,nullptr,&s->pool)!=VK_SUCCESS) return fail("descriptor pool");
    std::array<VkDescriptorSetLayout,MaxSlots*PassCount> layouts{};layouts.fill(s->setLayout);
    std::array<VkDescriptorSet,MaxSlots*PassCount> sets{};
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool=s->pool;alloc.descriptorSetCount=count;alloc.pSetLayouts=layouts.data();
    if(s->AllocateDescriptorSets(s->device.device,&alloc,sets.data())!=VK_SUCCESS) return fail("descriptor sets");
    for(uint32_t i=0;i<s->device.frameSlots;++i) for(uint32_t j=0;j<PassCount;++j) s->slots[i].sets[j]=sets[i*PassCount+j];
    for(uint32_t i=0;i<s->device.frameSlots;++i){
        gxvk::vk::hdeffect::BufferDesc desc{sizeof(Constants),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,gxvk::vk::hdeffect::Host};
        auto& slot=s->slots[i];slot.uniform=api->CreateBuffer(&desc,&slot.buffer);
        if(!slot.uniform || !slot.buffer.mapped)return fail("shadow constant buffer");
    }
    return true;
}
bool Effect::Resize(uint32_t w,uint32_t h) {
    constexpr bool half=false;
    auto* s=state_;if(!s) return false;
    if(!w || !h || w>16384 || h>16384) {s->error="invalid extent";return false;}
    if(s->width==w && s->height==h && s->half==half) return true;
    if(s->failedWidth==w && s->failedHeight==h && s->failedHalf==half) return false; // no per-frame OOM retry
    const uint32_t aw=half?(w+1)/2:w,ah=half?(h+1)/2:h;
    std::array<std::array<void*,2>,MaxSlots> handles{};
    std::array<std::array<gxvk::vk::hdeffect::ImageInfo,2>,MaxSlots> images{};
    bool okay=true;
    gxvk::vk::hdeffect::ImageDesc desc{VK_FORMAT_R8G8B8A8_UNORM,aw,ah,1,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT,VK_IMAGE_ASPECT_COLOR_BIT};
    for(uint32_t i=0;i<s->device.frameSlots && okay;++i) for(uint32_t j=0;j<2;++j) {
        handles[i][j]=s->api->CreateImage(&desc,&images[i][j]);
        if(!handles[i][j]) {okay=false;break;} ++s->allocations;
    }
    if(!okay) {
        for(auto& slot:handles) for(auto handle:slot) if(handle) s->api->RetireImage(handle,0);
        s->failedWidth=w;s->failedHeight=h;s->failedHalf=half;
        s->error="AO image allocation failed (previous extent retained)";return false;
    }
    for(uint32_t i=0;i<s->device.frameSlots;++i) {
        auto& slot=s->slots[i];
        for(uint32_t j=0;j<2;++j) {
            if(slot.handles[j]) s->api->RetireImage(slot.handles[j],slot.serial);
            slot.handles[j]=handles[i][j];slot.images[j]=images[i][j];
        }
        slot.initialized=false; // descriptor sets are updated only once this slot's fence completed.
    }
    s->width=w;s->height=h;s->aoWidth=aw;s->aoHeight=ah;s->half=half;s->failedWidth=s->failedHeight=0;s->error="";return true;
}
bool BuildConstants(const gxvk::vk::hdeffect::StageContext& ctx,const Settings& settings,const NativeMaps& maps,Constants& out) {
 if(!ctx.sceneValid || !ctx.scene || !ctx.extent.width || !ctx.extent.height)return false;
 const float* p=ctx.scene->projection;const auto& vp=ctx.viewport;
 for(int i=0;i<16;++i)if(!std::isfinite(p[i]))return false;
 if(p[0]==0||p[5]==0||p[11]!=1||p[15]!=0||p[10]<=0||p[14]>=0||vp.width<=0||vp.height<=0||vp.maxDepth<=vp.minDepth)return false;
 auto clamp=[](float v,float d,float a,float b){return std::isfinite(v)?std::clamp(v,a,b):d;};
 Constants c{};c.screen[0]=1.0f/ctx.extent.width;c.screen[1]=1.0f/ctx.extent.height;c.screen[2]=1/(vp.maxDepth-vp.minDepth);c.screen[3]=-vp.minDepth*c.screen[2];
 c.view[0]=2/p[0];c.view[1]=-2/p[5];c.view[2]=(-1-p[8])/p[0];c.view[3]=(1-p[9])/p[5];
 c.rect[0]=vp.x*c.screen[0];c.rect[1]=vp.y*c.screen[1];c.rect[2]=ctx.extent.width/vp.width;c.rect[3]=ctx.extent.height/vp.height;
 c.projection[0]=p[0];c.projection[1]=p[5];c.projection[2]=p[8];c.projection[3]=p[9];
 c.depth[0]=p[10];c.depth[1]=-p[14];c.depth[2]=.99999f;c.depth[3]=clamp(settings.tagTolerance,Settings{}.tagTolerance,.001f,1)*0.49f/16; // log2 units -> tag units (the tag spans 16 log2 units)
 for(int i=0;i<3;++i)c.sun[i]=maps.towardLight[i];c.sun[3]=clamp(settings.contactLength,1.5f,0,4);
 c.settings[0]=std::tan(clamp(settings.sunAngle,.00465f,0,.03f));c.settings[1]=clamp(settings.normalBias,.015f,0,.15f);
 c.settings[2]=clamp(settings.maxPenumbra,1.5f,.05f,4);c.settings[3]=clamp(settings.strength,1,0,1);
 c.contact[0]=clamp(settings.contactThickness,.08f,.005f,.3f);c.contact[1]=clamp(settings.contactStart,.035f,.005f,.2f);c.contact[2]=clamp(settings.contactStrength,1,0,1);c.contact[3]=clamp(settings.grassShade,1,0,1);c.jitter[3]=4; // grass: shade in the ground's shadow, up to 4 yd in front of it
 c.jitter[0]=settings.extraJitterX;c.jitter[1]=settings.extraJitterY;c.jitter[2]=ctx.swapRB?1.0f:0.0f;
 std::memcpy(c.rows,maps.rows,sizeof c.rows);std::memcpy(c.maps,maps.meta,sizeof c.maps);
 const float* f=reinterpret_cast<const float*>(&c);for(size_t i=0;i<sizeof c/sizeof(float);++i)if(!std::isfinite(f[i]))return false;
 float l=c.sun[0]*c.sun[0]+c.sun[1]*c.sun[1]+c.sun[2]*c.sun[2];if(l<.01f)return false;
 out=c;return true;
}
bool Effect::Record(const gxvk::vk::hdeffect::StageContext& ctx,const Settings& settings,const NativeMaps& maps,const GeometryInputs& gb) {
 auto* s=state_;if(!s||!settings.enabled)return false;
 if(ctx.structSize<offsetof(gxvk::vk::hdeffect::StageContext,jitter)||!ctx.cmd||!ctx.colorView||!ctx.depthView||!ctx.serial||
  ctx.point!=ShadowPoint||ctx.frameSlot>=s->device.frameSlots||ctx.frameSlots!=s->device.frameSlots||
  ctx.extent.width!=s->width||ctx.extent.height!=s->height||ctx.colorFormat!=VK_FORMAT_R8G8B8A8_UNORM||ctx.colorLayout!=VK_IMAGE_LAYOUT_GENERAL||
  gb.structSize<sizeof gb||!gb.valid||gb.serial!=ctx.serial||gb.shadowEndpointContract!=0x53484434||!gb.shadowLowView||!gb.shadowHighView||!gb.normalView||
  gb.shadowEndpointFormat!=VK_FORMAT_R16G16B16A16_SFLOAT||gb.layout!=VK_IMAGE_LAYOUT_GENERAL||gb.extent.width!=s->width||gb.extent.height!=s->height||maps.serial!=ctx.serial){s->error="shadow frame/G-buffer/native maps unavailable";return false;}
 auto& slot=s->slots[ctx.frameSlot];if(slot.serial==ctx.serial||slot.serial>ctx.completedSerial){s->error="shadow slot duplicate/in flight";return false;}
 Constants c{};if(!BuildConstants(ctx,settings,maps,c)){s->error="shadow projection/depth/constants unsupported";return false;}
 for(int i=0;i<5;++i)if(c.maps[i][2]>.5f&&(!maps.views[i]||(maps.layouts[i]!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&maps.layouts[i]!=VK_IMAGE_LAYOUT_GENERAL))){s->error="shadow map not sampleable";return false;}
 std::memcpy(slot.buffer.mapped,&c,sizeof c);s->api->FlushBuffer(slot.uniform);
 s->Barrier(ctx.cmd,slot);
 VkMemoryBarrier start{VK_STRUCTURE_TYPE_MEMORY_BARRIER};start.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;start.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT;
 s->CmdPipelineBarrier(ctx.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&start,0,nullptr,0,nullptr);
 for(uint32_t pass=0;pass<4;++pass){
  uint32_t src=pass==2?1:0,dst=pass==1?1:0;
  VkDescriptorImageInfo images[12]{};
  images[0]={s->sampler,ctx.depthView,ctx.depthLayout};images[1]={s->sampler,gb.normalView,gb.layout};
  images[2]={s->sampler,gb.shadowLowView,gb.layout};images[3]={s->sampler,gb.shadowHighView,gb.layout};
  for(int i=0;i<5;++i)images[4+i]=c.maps[i][2]>.5f?VkDescriptorImageInfo{s->sampler,maps.views[i],maps.layouts[i]}:VkDescriptorImageInfo{s->sampler,ctx.depthView,ctx.depthLayout};
  images[9]={s->sampler,slot.images[src].view,VK_IMAGE_LAYOUT_GENERAL};images[10]={VK_NULL_HANDLE,slot.images[dst].view,VK_IMAGE_LAYOUT_GENERAL};images[11]={VK_NULL_HANDLE,ctx.colorView,ctx.colorLayout};
  VkDescriptorBufferInfo buffer{slot.buffer.buffer,0,sizeof c};VkWriteDescriptorSet writes[13]{};
  for(uint32_t i=0;i<13;++i){auto&w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=slot.sets[pass];w.dstBinding=i;w.descriptorCount=1;
   w.descriptorType=i<10?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:(i<12?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);if(i<12)w.pImageInfo=&images[i];else w.pBufferInfo=&buffer;}
  s->UpdateDescriptorSets(s->device.device,13,writes,0,nullptr);
  uint32_t flags[4]={settings.debug,pass==2?1u:0u,std::min(settings.quality,1u),settings.contact?1u:0u};uint32_t pipeline=pass==0?0:(pass==3?2:1);
  s->CmdBindPipeline(ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipeline[pipeline]);s->CmdBindDescriptorSets(ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipelineLayout,0,1,&slot.sets[pass],0,nullptr);
  s->CmdPushConstants(ctx.cmd,s->pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof flags,flags);s->CmdDispatch(ctx.cmd,(s->width+7)/8,(s->height+7)/8,1);s->Between(ctx.cmd);
 }
 slot.initialized=true;slot.serial=s->lastSerial=ctx.serial;s->error="";return true;
}
bool Effect::Shutdown(uint64_t completed) {
    auto* s=state_;if(!s) return true;
    if(completed<s->lastSerial) {s->error="shutdown pending completed serial";return false;}
    for(auto& slot:s->slots) {for(auto handle:slot.handles)if(handle)s->api->RetireImage(handle,slot.serial);if(slot.uniform)s->api->RetireBuffer(slot.uniform,slot.serial);}
    if(s->pool) s->DestroyDescriptorPool(s->device.device,s->pool,nullptr);
    if(s->sampler) s->DestroySampler(s->device.device,s->sampler,nullptr);
    for(auto pipeline:s->pipeline) if(pipeline) s->DestroyPipeline(s->device.device,pipeline,nullptr);
    if(s->pipelineLayout) s->DestroyPipelineLayout(s->device.device,s->pipelineLayout,nullptr);
    if(s->setLayout) s->DestroyDescriptorSetLayout(s->device.device,s->setLayout,nullptr);
    s->~Impl();std::free(s);state_=nullptr;return true;
}
uint64_t Effect::LastUsed() const {return state_?state_->lastSerial:0;}
uint64_t Effect::ImageAllocations() const {return state_?state_->allocations:0;}
const char* Effect::Error() const {return state_?state_->error:"not initialized";}



}
