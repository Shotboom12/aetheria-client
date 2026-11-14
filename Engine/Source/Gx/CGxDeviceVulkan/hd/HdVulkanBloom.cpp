// SPDX-License-Identifier: GPL-3.0-or-later
// HdBloom.cpp formulas and HDR intermediate formats, isolated from water/fog/grade modules.
#include "HdVulkanBloom.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace HD::VulkanBloom {
namespace {
#include "shaders/bloom/HdBloomHalf.spv.h"
#include "shaders/bloom/HdBloomBright.spv.h"
#include "shaders/bloom/HdBloomDown.spv.h"
#include "shaders/bloom/HdBloomBlur.spv.h"
#include "shaders/bloom/HdBloomRayMask.spv.h"
#include "shaders/bloom/HdBloomRadial.spv.h"
#include "shaders/bloom/HdBloomComposite.spv.h"
constexpr uint32_t MaxSlots=8,MaxPasses=14,ImageCount=7;
float Sat(float x){return std::clamp(x,0.0f,1.0f);}
uint32_t Dimension(uint32_t size,uint32_t divisor){return std::max(size/divisor,1u);}
}
float BrightScale(float l,float threshold){float k=std::max(threshold*.5f,.05f),soft=std::clamp(l-threshold+k,0.0f,2*k);soft=soft*soft/(4*k+1e-4f);return std::max(soft,l-threshold)/std::max(l,1e-4f);}
bool IsSky(float raw,const Post& p){float z=Sat((raw-p.minDepth)/std::max(p.maxDepth-p.minDepth,1e-4f));float d=z-p.c2;float zv=std::abs(p.d2/(std::abs(d)<1e-7f?1e-7f:d));return raw>=.99999f || zv>=p.farClip*.98f;}
std::array<float,3> ScreenBlend(std::array<float,3> scene,std::array<float,3> light){for(int k=0;k<3;++k){float l=Sat(light[k]);scene[k]=l+scene[k]*(1-l);}return scene;}
bool BuildPost(const gxvk::vk::hdeffect::StageContext& ctx,const Settings& s,Post& post){
    if(!ctx.sceneValid || !ctx.scene || !ctx.extent.width || !ctx.extent.height)return false;
    const float* p=ctx.scene->projection;const auto& vp=ctx.viewport;
    for(int i=0;i<16;++i)if(!std::isfinite(p[i]))return false;
    if(p[0]==0 || p[5]==0 || p[11]!=1 || p[15]!=0 || p[10]<=0 || p[14]>=0 || !std::isfinite(vp.minDepth)||!std::isfinite(vp.maxDepth)||vp.maxDepth<=vp.minDepth ||
       !std::isfinite(vp.x)||!std::isfinite(vp.y)||!std::isfinite(vp.width)||!std::isfinite(vp.height)||vp.width<=0||vp.height<=0 ||
       !std::isfinite(s.farClip)||s.farClip<=0 || !std::isfinite(s.extraJitterX)||!std::isfinite(s.extraJitterY))return false;
    float sx=s.sunDirection[0],sy=s.sunDirection[1],sz=s.sunDirection[2];
    float fx=s.cameraForward[0],fy=s.cameraForward[1],fz=s.cameraForward[2];
    if(!std::isfinite(sx)||!std::isfinite(sy)||!std::isfinite(sz)||!std::isfinite(fx)||!std::isfinite(fy)||!std::isfinite(fz))return false;
    float sl=std::sqrt(sx*sx+sy*sy+sz*sz);if(!std::isfinite(sl))return false;if(sl<1e-4f){sx=0;sy=0;sz=1;sl=1;}sx/=sl;sy/=sl;sz/=sl;
    float fl=std::sqrt(fx*fx+fy*fy+fz*fz);if(!std::isfinite(fl))return false;if(fl<1e-4f){fx=1;fy=0;fz=0;fl=1;}fx/=fl;fy/=fl;fz/=fl;
    float rx=fy,ry=-fx,rl=std::sqrt(rx*rx+ry*ry);if(rl<1e-4f){rx=0;ry=-1;rl=1;}rx/=rl;ry/=rl;
    float ux=ry*fz,uy=-rx*fz,uz=rx*fy-ry*fx;
    float vx=sx*rx+sy*ry,vy=sx*ux+sy*uy+sz*uz,vz=sx*fx+sy*fy+sz*fz;
    Post out;out.minDepth=vp.minDepth;out.maxDepth=vp.maxDepth;out.c2=p[10];out.d2=p[14];out.farClip=s.farClip;
    if(vz>.05f){
        float nx=p[0]*vx/vz+p[8],ny=p[5]*vy/vz+p[9];
        out.u=(vp.x+(nx*.5f+.5f)*vp.width+s.extraJitterX)/ctx.extent.width;
        out.v=(vp.y+(.5f-ny*.5f)*vp.height+s.extraJitterY)/ctx.extent.height;
        float edge=std::max(std::abs(nx),std::abs(ny));out.visible=Sat(sz*4+.2f)*Sat((1.6f-edge)/.6f);
    }
    if(!std::isfinite(out.u)||!std::isfinite(out.v))return false;post=out;return true;
}
struct Effect::Impl {
    const gxvk::vk::hdeffect::Api* api=nullptr;gxvk::vk::hdeffect::DeviceInfo device{};const char* error="";
    uint32_t width=0,height=0,failedWidth=0,failedHeight=0;uint64_t last=0,allocations=0;
    VkDescriptorSetLayout setLayout{};VkPipelineLayout layout{};VkDescriptorPool pool{};VkSampler linear{},point{};VkPipeline pipeline[7]{};
    struct Slot{std::array<void*,ImageCount> handles{};std::array<gxvk::vk::hdeffect::ImageInfo,ImageCount> images{};VkDescriptorSet sets[MaxPasses]{};uint64_t serial=0;bool initialized=false;};
    std::array<Slot,MaxSlots> slots{};
#define BLOOM_FUNCTIONS(X) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
 X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateSampler) X(DestroySampler) \
 X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) X(CmdPipelineBarrier) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch)
#define DECL(n) PFN_vk##n n=nullptr;
    BLOOM_FUNCTIONS(DECL)
#undef DECL
    void Between(VkCommandBuffer cmd){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);}
};
bool Effect::Init(const gxvk::vk::hdeffect::Api* api){
    if(state_)return true;
    if(!api || api->apiVersion!=gxvk::vk::hdeffect::ApiVersion || api->structSize<offsetof(gxvk::vk::hdeffect::Api,RetireImage)+sizeof(api->RetireImage) || !api->GetDeviceInfo || !api->CreateImage || !api->RetireImage)return false;
    auto*s=new(std::nothrow)Impl();if(!s)return false;s->device.structSize=sizeof s->device;
    if(!api->GetDeviceInfo(&s->device)||!s->device.device||!s->device.getDeviceProcAddr||!s->device.getInstanceProcAddr||!s->device.frameSlots||s->device.frameSlots>MaxSlots){delete s;return false;}
#define LOAD(n) s->n=reinterpret_cast<PFN_vk##n>(s->device.getDeviceProcAddr(s->device.device,"vk" #n));if(!s->n){delete s;return false;}
    BLOOM_FUNCTIONS(LOAD)
#undef LOAD
    s->api=api;state_=s;auto fail=[&](){Shutdown(0);return false;};
    auto formats=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(s->device.getInstanceProcAddr(s->device.instance,"vkGetPhysicalDeviceFormatProperties"));if(!formats)return fail();
    constexpr auto required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    for(auto format:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_SFLOAT}){VkFormatProperties props{};formats(s->device.physicalDevice,format,&props);if((props.optimalTilingFeatures&required)!=required)return fail();}
    VkDescriptorSetLayoutBinding bindings[4]{};for(uint32_t i=0;i<4;++i){auto&b=bindings[i];b.binding=i;b.descriptorCount=1;b.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;b.descriptorType=i<3?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;}
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=4;sl.pBindings=bindings;if(s->CreateDescriptorSetLayout(s->device.device,&sl,nullptr,&s->setLayout)!=VK_SUCCESS)return fail();
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Constants)};VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&s->setLayout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&range;
    if(s->CreatePipelineLayout(s->device.device,&pl,nullptr,&s->layout)!=VK_SUCCESS)return fail();
    const uint32_t*code[7]={kHdBloomHalf,kHdBloomBright,kHdBloomDown,kHdBloomBlur,kHdBloomRayMask,kHdBloomRadial,kHdBloomComposite};
    const size_t bytes[7]={sizeof kHdBloomHalf,sizeof kHdBloomBright,sizeof kHdBloomDown,sizeof kHdBloomBlur,sizeof kHdBloomRayMask,sizeof kHdBloomRadial,sizeof kHdBloomComposite};
    for(uint32_t i=0;i<7;++i){VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=bytes[i];sm.pCode=code[i];VkShaderModule module{};
        if(s->CreateShaderModule(s->device.device,&sm,nullptr,&module)!=VK_SUCCESS)return fail();VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=module;cp.stage.pName="main";cp.layout=s->layout;
        auto result=s->CreateComputePipelines(s->device.device,s->device.pipelineCache,1,&cp,nullptr,&s->pipeline[i]);s->DestroyShaderModule(s->device.device,module,nullptr);if(result!=VK_SUCCESS)return fail();}
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_LINEAR;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(s->CreateSampler(s->device.device,&si,nullptr,&s->linear)!=VK_SUCCESS)return fail();si.magFilter=si.minFilter=VK_FILTER_NEAREST;if(s->CreateSampler(s->device.device,&si,nullptr,&s->point)!=VK_SUCCESS)return fail();
    uint32_t count=s->device.frameSlots*MaxPasses;VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,count*3},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,count}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.poolSizeCount=2;pi.pPoolSizes=sizes;pi.maxSets=count;if(s->CreateDescriptorPool(s->device.device,&pi,nullptr,&s->pool)!=VK_SUCCESS)return fail();
    std::array<VkDescriptorSetLayout,MaxPasses*MaxSlots> layouts{};layouts.fill(s->setLayout);std::array<VkDescriptorSet,MaxPasses*MaxSlots> sets{};
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=s->pool;ai.descriptorSetCount=count;ai.pSetLayouts=layouts.data();if(s->AllocateDescriptorSets(s->device.device,&ai,sets.data())!=VK_SUCCESS)return fail();
    for(uint32_t i=0;i<s->device.frameSlots;++i)for(uint32_t j=0;j<MaxPasses;++j)s->slots[i].sets[j]=sets[i*MaxPasses+j];return true;
}
bool Effect::Resize(uint32_t w,uint32_t h){
    auto*s=state_;if(!s)return false;if(!w||!h||w>16384||h>16384){s->error="bloom invalid extent";return false;}
    if(w==s->width && h==s->height)return true;if(w==s->failedWidth && h==s->failedHeight)return false;
    std::array<std::array<void*,ImageCount>,MaxSlots> handles{};std::array<std::array<gxvk::vk::hdeffect::ImageInfo,ImageCount>,MaxSlots> images{};bool okay=true;
    for(uint32_t i=0;i<s->device.frameSlots && okay;++i)for(uint32_t j=0;j<ImageCount;++j){uint32_t divisor=j==0?2:(j==3||j==4?8:4);
        gxvk::vk::hdeffect::ImageDesc desc{j==0?VK_FORMAT_R8G8B8A8_UNORM:VK_FORMAT_R16G16B16A16_SFLOAT,Dimension(w,divisor),Dimension(h,divisor),1,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT};
        handles[i][j]=s->api->CreateImage(&desc,&images[i][j]);if(!handles[i][j]){okay=false;break;}++s->allocations;}
    if(!okay){for(auto&slot:handles)for(auto handle:slot)if(handle)s->api->RetireImage(handle,0);s->failedWidth=w;s->failedHeight=h;s->error="bloom resize failed; previous targets retained, retry blocked for this size";return false;}
    for(uint32_t i=0;i<s->device.frameSlots;++i){auto&slot=s->slots[i];for(auto handle:slot.handles)if(handle)s->api->RetireImage(handle,slot.serial);slot.handles=handles[i];slot.images=images[i];slot.initialized=false;}
    s->width=w;s->height=h;s->failedWidth=s->failedHeight=0;s->error="";return true;
}
bool Effect::Record(const gxvk::vk::hdeffect::StageContext& ctx,const Settings& settings){
    auto*s=state_;if(!s)return false;
    if(ctx.structSize<sizeof ctx || ctx.point!=Point || !ctx.cmd || !ctx.colorView || !ctx.serial || !s->width || !s->height || ctx.frameSlots!=s->device.frameSlots || ctx.frameSlot>=s->device.frameSlots ||
       ctx.extent.width!=s->width || ctx.extent.height!=s->height || ctx.colorFormat!=VK_FORMAT_R8G8B8A8_UNORM || ctx.colorLayout!=VK_IMAGE_LAYOUT_GENERAL){s->error="bloom frame/extent unavailable";return false;}
    auto&slot=s->slots[ctx.frameSlot];if(slot.serial==ctx.serial || slot.serial>ctx.completedSerial){s->error="bloom slot in flight/duplicate";return false;}
    if(!std::isfinite(settings.threshold)||!std::isfinite(settings.strength)||!std::isfinite(settings.rayStrength)){s->error="bloom non-finite settings";return false;}
    Settings config=settings;config.threshold=Sat(config.threshold);config.strength=std::clamp(config.strength,0.0f,5.0f);config.rayStrength=std::clamp(config.rayStrength,0.0f,5.0f);config.debug=std::clamp(config.debug,0,3);
    // HD 2026-11-05 lot B1: a halo of strength 0 adds exactly nothing (composite: light 0 + scene*(1-0) = scene, alpha kept),
    // so its 11 passes are skipped. Rays and the debug views keep their own path; the after-spells boundary is untouched.
    if(config.strength<=0.0f && config.debug==0)config.bloom=false;
    bool readableDepth=ctx.depthView && (ctx.depthLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || ctx.depthLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL || ctx.depthLayout==VK_IMAGE_LAYOUT_GENERAL);
    Post post;bool havePost=BuildPost(ctx,config,post);bool rays=config.rays && readableDepth && havePost && (post.visible>.001f || config.debug>0);
    if(!config.bloom && !rays){s->error=config.rays?"bloom skipped: rays need forward scene depth and visible sun":"bloom off";return false;}
    std::array<VkImageMemoryBarrier,ImageCount> barriers{};for(uint32_t i=0;i<ImageCount;++i){auto&b=barriers[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.oldLayout=slot.initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;b.image=slot.images[i].image;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.srcAccessMask=slot.initialized?VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT:0;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;}
    s->CmdPipelineBarrier(ctx.cmd,slot.initialized?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,ImageCount,barriers.data());
    uint32_t passIndex=0;Constants c{};c.flags[0]=ctx.swapRB;c.flags[1]=ctx.swapRB;
    auto image=[&](uint32_t i){return VkDescriptorImageInfo{s->linear,slot.images[i].view,VK_IMAGE_LAYOUT_GENERAL};};
    auto run=[&](uint32_t pass,int32_t dst,VkDescriptorImageInfo t0,VkDescriptorImageInfo t1,VkDescriptorImageInfo t2){
        uint32_t divisor=dst<0?1:(dst==0?2:(dst==3||dst==4?8:4)),w=Dimension(s->width,divisor),h=Dimension(s->height,divisor);
        c.target={1.0f/w,1.0f/h,0,0};VkDescriptorImageInfo imgs[4]={t0,t1,t2,{VK_NULL_HANDLE,dst<0?ctx.colorView:slot.images[dst].view,VK_IMAGE_LAYOUT_GENERAL}};
        VkWriteDescriptorSet writes[4]{};auto set=slot.sets[passIndex++];for(uint32_t j=0;j<4;++j){auto&d=writes[j];d.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;d.dstSet=set;d.dstBinding=j;d.descriptorCount=1;d.descriptorType=j<3?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;d.pImageInfo=&imgs[j];}
        s->UpdateDescriptorSets(s->device.device,4,writes,0,nullptr);s->CmdBindPipeline(ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipeline[pass]);s->CmdBindDescriptorSets(ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->layout,0,1,&set,0,nullptr);s->CmdPushConstants(ctx.cmd,s->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof c,&c);s->CmdDispatch(ctx.cmd,(w+7)/8,(h+7)/8,1);s->Between(ctx.cmd);
    };
    VkDescriptorImageInfo frame{s->linear,ctx.colorView,ctx.colorLayout};run(0,0,frame,frame,frame);
    auto blur=[&](uint32_t first,float spread,uint32_t divisor){c.a={spread/Dimension(s->width,divisor),0,0,0};run(3,first+1,image(first),image(first),image(first));c.a={0,spread/Dimension(s->height,divisor),0,0};run(3,first,image(first+1),image(first+1),image(first+1));};
    if(config.bloom){
        c.a={1.0f/Dimension(s->width,2),1.0f/Dimension(s->height,2),0,0};c.b={config.threshold,std::max(config.threshold*.5f,.05f),0,0};run(1,1,image(0),image(0),image(0));blur(1,1,4);blur(1,2,4);
        c.a={1.0f/Dimension(s->width,4),1.0f/Dimension(s->height,4),0,0};run(2,3,image(1),image(1),image(1));blur(3,1,8);blur(3,2,8);
    }
    if(rays){c.a={post.u,post.v,float(s->width)/s->height,.6f};c.b={.5f,post.farClip,0,0};c.c={post.minDepth,post.maxDepth,post.c2,post.d2};
        VkDescriptorImageInfo depth{s->point,ctx.depthView,ctx.depthLayout};run(4,5,image(0),depth,image(0));c.a={post.u,post.v,.9f,.97f};c.b={1.5f,0,0,0};run(5,6,image(5),image(5),image(5));}
    uint32_t halo=config.bloom?1:6,wide=config.bloom?3:6,ray=rays?(config.debug==1?5:6):halo;
    c.a={config.bloom?config.strength:0,rays?config.rayStrength*(config.debug>0?1:post.visible):0,float(config.debug),0};c.b={post.u,post.v,float(s->width)/s->height,0};run(6,-1,image(halo),image(wide),image(ray));
    slot.initialized=true;slot.serial=s->last=ctx.serial;s->error=config.rays&&!rays?"Vulkan bloom; rays unavailable (depth/projection/sun)":(rays?(config.bloom?"Vulkan bloom + rays: original formulas":"Vulkan rays: original formulas"):"Vulkan bloom: original formulas");return true;
}
bool Effect::Shutdown(uint64_t completed){auto*s=state_;if(!s)return true;if(completed<s->last){s->error="bloom shutdown pending serial";return false;}
    for(auto&slot:s->slots)for(auto handle:slot.handles)if(handle)s->api->RetireImage(handle,slot.serial);
    if(s->pool)s->DestroyDescriptorPool(s->device.device,s->pool,nullptr);if(s->linear)s->DestroySampler(s->device.device,s->linear,nullptr);if(s->point)s->DestroySampler(s->device.device,s->point,nullptr);
    for(auto pipeline:s->pipeline)if(pipeline)s->DestroyPipeline(s->device.device,pipeline,nullptr);if(s->layout)s->DestroyPipelineLayout(s->device.device,s->layout,nullptr);if(s->setLayout)s->DestroyDescriptorSetLayout(s->device.device,s->setLayout,nullptr);delete s;state_=nullptr;return true;
}
uint64_t Effect::LastUsed() const{return state_?state_->last:0;}
uint64_t Effect::ImageAllocations() const{return state_?state_->allocations:0;}
const char* Effect::Error() const{return state_?state_->error:"bloom not initialized";}
}
