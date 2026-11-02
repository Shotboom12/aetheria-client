#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdGeometryPass.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
// HD: Vulkan AO port. XeGTAO (MIT) algorithm; existing engine entry points preserved.
#include "HdVulkanAO.h"
#include <cstring>
#include <new>



namespace HD::VulkanAO {
namespace {
#include "shaders/ao/HdAOPrefilter.spv.h"
#include "shaders/ao/HdAOMip.spv.h"
#include "shaders/ao/HdAOMainLow.spv.h"
#include "shaders/ao/HdAOGenerate.spv.h"
#include "shaders/ao/HdAOMainHigh.spv.h"
#include "shaders/ao/HdAOMainUltra.spv.h"
#include "shaders/ao/HdAOBlur.spv.h"
#include "shaders/ao/HdAOApply.spv.h"
#include "shaders/ao/HdAOTemporal.spv.h"
constexpr uint32_t MaxSlots = 8, PassCount = 11, PipelineCount = 9;
// HD (Claude): row-vector 4x4 helpers in double; world positions are large, the products are not.
bool Invert(const double* m,double* out) {
    double a[4][8];
    for(int r=0;r<4;++r)for(int k=0;k<8;++k)a[r][k]=k<4?m[r*4+k]:(k-4==r?1:0);
    for(int col=0;col<4;++col){int pivot=col;for(int r=col+1;r<4;++r)if(std::fabs(a[r][col])>std::fabs(a[pivot][col]))pivot=r;
        if(std::fabs(a[pivot][col])<1e-12)return false;if(pivot!=col)for(int k=0;k<8;++k)std::swap(a[pivot][k],a[col][k]);
        double d=1/a[col][col];for(int k=0;k<8;++k)a[col][k]*=d;
        for(int r=0;r<4;++r)if(r!=col){double f=a[r][col];for(int k=0;k<8;++k)a[r][k]-=f*a[col][k];}}
    for(int r=0;r<4;++r)for(int k=0;k<4;++k)out[r*4+k]=a[r][k+4];return true;
}
void Multiply(const double* a,const double* b,double* out) {
    for(int r=0;r<4;++r)for(int k=0;k<4;++k){double v=0;for(int i=0;i<4;++i)v+=a[r*4+i]*b[i*4+k];out[r*4+k]=v;}
}
}
struct Effect::Impl {
    const gxvk::vk::hdeffect::Api* api = nullptr;
    gxvk::vk::hdeffect::DeviceInfo device{};
    const char* error = "";
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkPipeline pipeline[PipelineCount]{};
    struct Slot {
        void* handles[7]{};
        gxvk::vk::hdeffect::ImageInfo images[7]{};
        VkDescriptorSet sets[PassCount]{};
        bool initialized = false;
        uint64_t serial = 0;
    };
    std::array<Slot,MaxSlots> slots{};
    uint32_t width = 0,height = 0,aoWidth = 0,aoHeight = 0;
    bool half = false;
    uint32_t failedWidth=0,failedHeight=0;
    bool failedHalf=true;
    uint64_t lastSerial = 0, allocations = 0;
    void* history[2]{};
    gxvk::vk::hdeffect::ImageInfo historyInfo[2]{};
    bool historyLayout = false, havePrevious = false;
    uint32_t historyIndex = 0;
    uint64_t historySerial = 0;
    double previousView[16]{}, previousProjection[16]{};
    VkViewport previousViewport{};
    float previousJitter[2]{};
    // View position -> previous frame's work pixel (x*w, y*w) and w = previous view depth, from that frame's camera.
    bool Reprojection(const gxvk::vk::hdeffect::StageContext& ctx,bool halfResolution,float out[12]) const {
        double view[16],inverse[16],a[16],m[16];
        for(int i=0;i<16;++i)view[i]=ctx.scene->view[i];
        if(!havePrevious||!Invert(view,inverse))return false;
        Multiply(inverse,previousView,a);Multiply(a,previousProjection,m);
        const VkViewport& vp=previousViewport;
        const double ax=.5*vp.width,bx=vp.x+previousJitter[0]-.5+.5*vp.width,ay=-.5*vp.height,by=vp.y+previousJitter[1]-.5+.5*vp.height;
        for(int k=0;k<4;++k){double w=m[k*4+3],x=ax*m[k*4]+bx*w,y=ay*m[k*4+1]+by*w;
            if(halfResolution){x=(x-w)*.5;y=(y-w)*.5;} // full pixel index -> work pixel (centre 2p+1)
            out[k]=float(x);out[4+k]=float(y);out[8+k]=float(w);}
        for(int i=0;i<12;++i)if(!std::isfinite(out[i]))return false;
        return true;
    }
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
        std::array<VkImageMemoryBarrier,9> barriers{};
        for(uint32_t i=0;i<9;++i) {
            const bool ready=i<7?slot.initialized:historyLayout;
            auto& b=barriers[i]; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout=ready?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout=VK_IMAGE_LAYOUT_GENERAL; b.image=i<7?slot.images[i].image:historyInfo[i-7].image;
            b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.levelCount=b.subresourceRange.layerCount=1;
            b.srcAccessMask=ready?VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT:0;
            b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        }
        // HD: the shared history was last touched by an earlier frame's compute; submission order makes this cover it.
        CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,9,barriers.data());
        historyLayout=true;
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
    if(!api || api->structSize<offsetof(gxvk::vk::hdeffect::Api,RetireImage)+sizeof(api->RetireImage) ||
        api->apiVersion!=gxvk::vk::hdeffect::ApiVersion || !api->GetDeviceInfo || !api->CreateImage || !api->RetireImage) return false;
    auto* s=new(std::nothrow) Impl(); if(!s) return false;
    s->api=api; s->device.structSize=sizeof s->device;
    if(!api->GetDeviceInfo(&s->device) || !s->device.device || !s->device.getDeviceProcAddr || !s->device.getInstanceProcAddr ||
        !s->device.frameSlots || s->device.frameSlots>MaxSlots) { delete s;return false; }
#define AO_LOAD(n) s->n=reinterpret_cast<PFN_vk##n>(s->device.getDeviceProcAddr(s->device.device,"vk" #n)); \
    if(!s->n) { delete s;return false; }
    AO_FUNCTIONS(AO_LOAD)
#undef AO_LOAD
    state_=s;
    auto fail=[&](const char* why) { s->error=why;Shutdown(0);return false; };
    auto formats=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(s->device.getInstanceProcAddr(s->device.instance,"vkGetPhysicalDeviceFormatProperties"));
    if(!formats) return fail("format query missing");
    VkFormatProperties format{};formats(s->device.physicalDevice,VK_FORMAT_R8G8B8A8_UNORM,&format);
    constexpr auto required=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if((format.optimalTilingFeatures&required)!=required) return fail("RGBA8 storage/sample format unavailable");
    formats(s->device.physicalDevice,VK_FORMAT_R32_SFLOAT,&format);
    if((format.optimalTilingFeatures&required)!=required)return fail("R32 depth storage/sample format unavailable");
    formats(s->device.physicalDevice,VK_FORMAT_R16G16B16A16_SFLOAT,&format);
    if((format.optimalTilingFeatures&required)!=required)return fail("RGBA16F history storage/sample format unavailable");
    std::array<VkDescriptorSetLayoutBinding,11> bindings{};
    for(uint32_t i=0;i<11;++i) {
        bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].descriptorType=i<8||i==10?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sl.bindingCount=11;sl.pBindings=bindings.data();
    if(s->CreateDescriptorSetLayout(s->device.device,&sl,nullptr,&s->setLayout)!=VK_SUCCESS) return fail("descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Constants)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount=1;pl.pSetLayouts=&s->setLayout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
    if(s->CreatePipelineLayout(s->device.device,&pl,nullptr,&s->pipelineLayout)!=VK_SUCCESS) return fail("pipeline layout");
    const uint32_t* code[PipelineCount]={kHdAOPrefilter,kHdAOMip,kHdAOMainLow,kHdAOGenerate,kHdAOMainHigh,kHdAOMainUltra,kHdAOBlur,kHdAOApply,kHdAOTemporal};
    const size_t bytes[PipelineCount]={sizeof kHdAOPrefilter,sizeof kHdAOMip,sizeof kHdAOMainLow,sizeof kHdAOGenerate,sizeof kHdAOMainHigh,sizeof kHdAOMainUltra,sizeof kHdAOBlur,sizeof kHdAOApply,sizeof kHdAOTemporal};
    for(uint32_t i=0;i<PipelineCount;++i) {
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
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,9*count},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2*count}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=count;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
    if(s->CreateDescriptorPool(s->device.device,&pool,nullptr,&s->pool)!=VK_SUCCESS) return fail("descriptor pool");
    std::array<VkDescriptorSetLayout,MaxSlots*PassCount> layouts{};layouts.fill(s->setLayout);
    std::array<VkDescriptorSet,MaxSlots*PassCount> sets{};
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool=s->pool;alloc.descriptorSetCount=count;alloc.pSetLayouts=layouts.data();
    if(s->AllocateDescriptorSets(s->device.device,&alloc,sets.data())!=VK_SUCCESS) return fail("descriptor sets");
    for(uint32_t i=0;i<s->device.frameSlots;++i) for(uint32_t j=0;j<PassCount;++j) s->slots[i].sets[j]=sets[i*PassCount+j];
    return true;
}

// HD: independent one-mip images avoid custom mip-view retirement and support ceil-halving odd extents.
bool Effect::Resize(uint32_t w,uint32_t h,bool half){
    auto*s=state_;if(!s)return false;if(!w||!h||w>16384||h>16384){s->error="invalid extent";return false;}
    if(s->width==w&&s->height==h&&s->half==half)return true;
    if(s->failedWidth==w&&s->failedHeight==h&&s->failedHalf==half)return false;
    uint32_t aw=half?(w+1)/2:w,ah=half?(h+1)/2:h;
    std::array<std::array<void*,7>,MaxSlots>handles{};std::array<std::array<gxvk::vk::hdeffect::ImageInfo,7>,MaxSlots>images{};bool okay=true;
    for(uint32_t i=0;i<s->device.frameSlots&&okay;++i)for(uint32_t j=0;j<7;++j){
        uint32_t level=j<5?j:0,block=1u<<level;
        gxvk::vk::hdeffect::ImageDesc desc{j<5?VK_FORMAT_R32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM,(aw+block-1)/block,(ah+block-1)/block,1,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT,VK_IMAGE_ASPECT_COLOR_BIT};
        handles[i][j]=s->api->CreateImage(&desc,&images[i][j]);if(!handles[i][j]){okay=false;break;}++s->allocations;
    }
    void* history[2]{};gxvk::vk::hdeffect::ImageInfo historyInfo[2]{};
    for(uint32_t j=0;j<2&&okay;++j){gxvk::vk::hdeffect::ImageDesc desc{VK_FORMAT_R16G16B16A16_SFLOAT,aw,ah,1,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT,VK_IMAGE_ASPECT_COLOR_BIT};
        history[j]=s->api->CreateImage(&desc,&historyInfo[j]);if(!history[j])okay=false;else ++s->allocations;}
    if(!okay){for(auto&slot:handles)for(auto handle:slot)if(handle)s->api->RetireImage(handle,0);
        for(auto handle:history)if(handle)s->api->RetireImage(handle,0);
        s->failedWidth=w;s->failedHeight=h;s->failedHalf=half;s->error="GTAO allocation failed; previous extent retained";return false;}
    for(uint32_t i=0;i<s->device.frameSlots;++i){auto&slot=s->slots[i];for(uint32_t j=0;j<7;++j){if(slot.handles[j])s->api->RetireImage(slot.handles[j],slot.serial);slot.handles[j]=handles[i][j];slot.images[j]=images[i][j];}slot.initialized=false;}
    for(uint32_t j=0;j<2;++j){if(s->history[j])s->api->RetireImage(s->history[j],s->lastSerial);s->history[j]=history[j];s->historyInfo[j]=historyInfo[j];}
    s->historyLayout=false;s->historySerial=0;s->historyIndex=0;
    s->width=w;s->height=h;s->aoWidth=aw;s->aoHeight=ah;s->half=half;s->failedWidth=s->failedHeight=0;s->error="";return true;
}
bool Effect::Record(const gxvk::vk::hdeffect::StageContext&ctx,const Settings&settings){
    auto*s=state_;if(!s||!settings.enabled)return false;
    if(ctx.structSize<sizeof ctx||!ctx.cmd||!ctx.colorView||!ctx.depthView||!ctx.serial||ctx.point!=Point||ctx.frameSlot>=s->device.frameSlots||ctx.frameSlots!=s->device.frameSlots||ctx.extent.width!=s->width||ctx.extent.height!=s->height||settings.halfResolution!=s->half||ctx.colorFormat!=VK_FORMAT_R8G8B8A8_UNORM||ctx.colorLayout!=VK_IMAGE_LAYOUT_GENERAL||
       (ctx.depthLayout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&ctx.depthLayout!=VK_IMAGE_LAYOUT_GENERAL&&ctx.depthLayout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)){
        s->error="GTAO inputs/extent/layout not ready";return false;}
    auto&slot=s->slots[ctx.frameSlot];if(ctx.serial<=s->lastSerial||slot.serial>ctx.completedSerial){s->error="GTAO duplicate/backdated/in-flight slot";return false;}
    Constants c;if(!BuildConstants(ctx,settings,c)){s->error="GTAO requires forward-Z perspective depth";return false;}
    VkImageView normal=ctx.depthView;VkImageLayout normalLayout=ctx.depthLayout;
    if(settings.useGBufferNormals){
        HD::VulkanShadows::GeometryInputs geometry;
        if(!gxvk::vk::hdgeometry::Get(ctx.serial,geometry) || geometry.extent.width!=ctx.extent.width || geometry.extent.height!=ctx.extent.height){
            s->error="current-frame geometry normals unavailable";return false;}
        normal=geometry.normalView;normalLayout=geometry.layout;c.flags[2]=1;
    }
    // HD (Claude): passes 0-4 depth mips, 5 GTAO, 6-8 denoise, 9 temporal (optional), 10 apply.
    const bool temporal=settings.temporal;const uint32_t previous=s->historyIndex,current=previous^1u;
    float reprojection[12]{};
    const bool reuse=temporal&&s->historySerial&&ctx.serial==s->historySerial+1&&s->Reprojection(ctx,settings.halfResolution,reprojection);
    c.blur[0]=temporal?float(ctx.serial%64):0; // noise frame index; 0 = the fixed pattern
    slot.serial=s->lastSerial=ctx.serial;s->Barrier(ctx.cmd,slot);
    for(uint32_t pass=0;pass<PassCount;++pass){
        if(pass==9&&!temporal)continue;
        uint32_t src=pass==7||pass>=9?6:5,dst=pass==6||pass==8?6:5;
        VkImageView ao=pass==10&&temporal?s->historyInfo[current].view:slot.images[src].view;
        VkImageView output=pass==9?s->historyInfo[current].view:slot.images[pass<5?pass:dst].view;
        VkDescriptorImageInfo images[11]{};images[0]={s->sampler,ctx.depthView,ctx.depthLayout};
        for(uint32_t i=0;i<5;++i)images[i+1]={s->sampler,slot.images[i].view,VK_IMAGE_LAYOUT_GENERAL};
        images[6]={s->sampler,normal,normalLayout};images[7]={s->sampler,ao,VK_IMAGE_LAYOUT_GENERAL};
        images[8]={VK_NULL_HANDLE,output,VK_IMAGE_LAYOUT_GENERAL};images[9]={VK_NULL_HANDLE,ctx.colorView,ctx.colorLayout};
        images[10]={s->sampler,s->historyInfo[previous].view,VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[11]{};for(uint32_t i=0;i<11;++i){auto&d=writes[i];d.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;d.dstSet=slot.sets[pass];d.dstBinding=i;d.descriptorCount=1;d.descriptorType=i<8||i==10?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;d.pImageInfo=&images[i];}
        s->UpdateDescriptorSets(s->device.device,11,writes,0,nullptr);
        uint32_t pipeline=pass==0?0:pass<5?1:pass==5?2+std::min(settings.quality,3u):pass<9?6:pass==9?8:7;
        Constants push=c;push.flags[0]=pass<5?pass:(pass==8?1:0);uint32_t block=pass<5?1u<<pass:1;
        if(pass==9){std::memcpy(push.screen,reprojection,16);std::memcpy(push.depth,reprojection+4,16);std::memcpy(push.params,reprojection+8,16);
            push.blur[0]=16;push.blur[1]=reuse?1.0f:0.0f;push.blur[2]=.02f;push.blur[3]=.04f;} // max frames, history usable, clamp margin, depth tolerance
        uint32_t w=pass==10?s->width:(s->aoWidth+block-1)/block,h=pass==10?s->height:(s->aoHeight+block-1)/block;
        s->CmdBindPipeline(ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipeline[pipeline]);s->CmdBindDescriptorSets(ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipelineLayout,0,1,&slot.sets[pass],0,nullptr);
        s->CmdPushConstants(ctx.cmd,s->pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,&push);s->CmdDispatch(ctx.cmd,(w+7)/8,(h+7)/8,1);s->Between(ctx.cmd);
    }
    slot.initialized=true;
    if(temporal){s->historyIndex=current;s->historySerial=ctx.serial;}else s->historySerial=0;
    for(int i=0;i<16;++i){s->previousView[i]=ctx.scene->view[i];s->previousProjection[i]=ctx.scene->projection[i];}
    s->previousViewport=ctx.viewport;s->previousJitter[0]=settings.extraJitterX;s->previousJitter[1]=settings.extraJitterY;s->havePrevious=true;
    s->error="";return true;
}
bool Effect::Shutdown(uint64_t completed) {
    auto* s=state_;if(!s) return true;
    if(completed<s->lastSerial) {s->error="shutdown pending completed serial";return false;}
    for(auto& slot:s->slots) for(auto handle:slot.handles) if(handle) s->api->RetireImage(handle,slot.serial);
    for(auto handle:s->history) if(handle) s->api->RetireImage(handle,s->lastSerial);
    if(s->pool) s->DestroyDescriptorPool(s->device.device,s->pool,nullptr);
    if(s->sampler) s->DestroySampler(s->device.device,s->sampler,nullptr);
    for(auto pipeline:s->pipeline) if(pipeline) s->DestroyPipeline(s->device.device,pipeline,nullptr);
    if(s->pipelineLayout) s->DestroyPipelineLayout(s->device.device,s->pipelineLayout,nullptr);
    if(s->setLayout) s->DestroyDescriptorSetLayout(s->device.device,s->setLayout,nullptr);
    delete s;state_=nullptr;return true;
}
void Effect::ResetHistory(){if(state_){state_->historySerial=0;state_->havePrevious=false;}}
uint64_t Effect::LastUsed() const {return state_?state_->lastSerial:0;}
uint64_t Effect::ImageAllocations() const {return state_?state_->allocations:0;}
const char* Effect::Error() const {return state_?state_->error:"not initialized";}

}
