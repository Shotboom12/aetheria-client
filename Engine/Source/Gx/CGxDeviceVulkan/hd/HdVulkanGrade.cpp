// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated port of the existing client's HdGrade.cpp. No water or fog module dependency.
#include "HdVulkanGrade.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <new>

namespace HD::VulkanGrade {
namespace {
#include "shaders/grade/HdVulkanGrade.spv.h"
float Sat(float v) {return std::clamp(v,0.0f,1.0f);}
}
void LutWeights::Add(int32_t lut,float w) {
    if(w<=0) return;
    for(int32_t i=0;i<count;++i) if(id[i]==lut) {weight[i]+=w;return;}
    if(count<16) {id[count]=lut;weight[count]=w;++count;}
}
void LutWeights::Scale(float f) {for(int32_t i=0;i<count;++i) weight[i]*=f;}
Selection Select(const LutWeights& weights) {
    int32_t first=-1,second=-1;
    for(int32_t i=0;i<weights.count;++i) {
        if(first<0 || weights.weight[i]>weights.weight[first]) {second=first;first=i;}
        else if(second<0 || weights.weight[i]>weights.weight[second]) second=i;
    }
    Selection out;out.a=first>=0?weights.id[first]:0;out.b=second>=0?weights.id[second]:out.a;
    float a=first>=0?weights.weight[first]:1,b=second>=0?weights.weight[second]:0;
    out.blend=a+b>0?b/(a+b):0;out.hasGrade=first>=0 && (out.a!=0 || out.b!=0);return out;
}
void Identity(Texels& pixels) {
    for(uint32_t y=0;y<LutHeight;++y) for(uint32_t x=0;x<LutWidth;++x) {
        uint8_t* t=&pixels[(size_t(y)*LutWidth+x)*4];
        t[0]=uint8_t(((x/32)*255+15)/31);t[1]=uint8_t((y*255+15)/31);t[2]=uint8_t(((x%32)*255+15)/31);t[3]=255;
    }
}
bool DecodeLut(const uint8_t* data,size_t size,Texels& pixels) {
    if(!data || size<148 || std::memcmp(data,"BLP2",4) || data[8]!=3) return false;
    // BLP offsets are FILE-FORMAT uint32 fields, not x86 object offsets. Windows target is little endian.
    uint32_t w=0,h=0,offset=0;std::memcpy(&w,data+12,4);std::memcpy(&h,data+16,4);std::memcpy(&offset,data+20,4);
    if(w!=LutWidth || h!=LutHeight || offset<148 || size_t(offset)>size || LutBytes>size-size_t(offset)) return false;
    std::memcpy(pixels.data(),data+offset,LutBytes);return true;
}
std::array<float,3> Sample(const Texels& lut,std::array<float,3> rgb) {
    float r=Sat(rgb[0])*31,g=Sat(rgb[1])*31,b=Sat(rgb[2])*31;
    uint32_t lo[3]={uint32_t(r),uint32_t(g),uint32_t(b)},hi[3];float share[3]={r-lo[0],g-lo[1],b-lo[2]};
    for(int k=0;k<3;++k)hi[k]=std::min(lo[k]+1,31u);
    std::array<float,3> out{};
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        float w=(x?share[0]:1-share[0])*(y?share[1]:1-share[1])*(z?share[2]:1-share[2]);
        size_t offset=(size_t(y?hi[1]:lo[1])*1024+(z?hi[2]:lo[2])*32+(x?hi[0]:lo[0]))*4;
        for(int k=0;k<3;++k)out[k]+=lut[offset+2-k]*(w/255.0f);
    }
    return out;
}
struct Effect::Impl {
    const gxvk::vk::hdeffect::Api* api=nullptr;gxvk::vk::hdeffect::DeviceInfo device{};
    const char* error="";uint32_t width=0,height=0;uint64_t last=0;
    VkDescriptorSetLayout setLayout{};VkPipelineLayout layout{};VkPipeline pipeline{};VkDescriptorPool pool{};VkSampler linear{};
    struct Slot {VkDescriptorSet set{};uint64_t serial=0;};std::array<Slot,8> slots{};
    struct Lut {void* handle=nullptr;gxvk::vk::hdeffect::ImageInfo image{};void* staging=nullptr;gxvk::vk::hdeffect::BufferInfo buffer{};uint64_t serial=0;bool uploaded=false;};
    std::map<int32_t,Lut> luts;
#define GRADE_FUNCTIONS(X) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CreateSampler) X(DestroySampler) X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) \
    X(CmdPipelineBarrier) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdDispatch) X(CmdPushConstants) X(CmdCopyBufferToImage)
#define DECL(n) PFN_vk##n n=nullptr;
    GRADE_FUNCTIONS(DECL)
#undef DECL
};
bool Effect::Init(const gxvk::vk::hdeffect::Api* api) {
    if(state_) return true;
    if(!api || api->apiVersion!=gxvk::vk::hdeffect::ApiVersion || api->structSize<offsetof(gxvk::vk::hdeffect::Api,RetireImage)+sizeof(api->RetireImage) ||
       !api->GetDeviceInfo || !api->CreateBuffer || !api->RetireBuffer || !api->FlushBuffer || !api->CreateImage || !api->RetireImage) return false;
    auto* s=new(std::nothrow) Impl();if(!s)return false;s->device.structSize=sizeof s->device;
    if(!api->GetDeviceInfo(&s->device)||!s->device.device||!s->device.getDeviceProcAddr||!s->device.frameSlots||s->device.frameSlots>8) {delete s;return false;}
#define LOAD(n) s->n=reinterpret_cast<PFN_vk##n>(s->device.getDeviceProcAddr(s->device.device,"vk" #n));if(!s->n){delete s;return false;}
    GRADE_FUNCTIONS(LOAD)
#undef LOAD
    s->api=api;state_=s;auto fail=[&](){Shutdown(0);return false;};
    VkDescriptorSetLayoutBinding bindings[3]{};
    for(uint32_t i=0;i<3;++i){auto& b=bindings[i];b.binding=i;b.descriptorCount=1;b.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;b.descriptorType=i?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;}
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=3;sl.pBindings=bindings;
    if(s->CreateDescriptorSetLayout(s->device.device,&sl,nullptr,&s->setLayout)!=VK_SUCCESS) return fail();
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Parameters)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&s->setLayout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&range;
    if(s->CreatePipelineLayout(s->device.device,&pl,nullptr,&s->layout)!=VK_SUCCESS) return fail();
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=sizeof kHdVulkanGrade;sm.pCode=kHdVulkanGrade;
    VkShaderModule module{};if(s->CreateShaderModule(s->device.device,&sm,nullptr,&module)!=VK_SUCCESS) return fail();
    VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=module;cp.stage.pName="main";cp.layout=s->layout;
    VkResult result=s->CreateComputePipelines(s->device.device,s->device.pipelineCache,1,&cp,nullptr,&s->pipeline);
    s->DestroyShaderModule(s->device.device,module,nullptr);if(result!=VK_SUCCESS)return fail();
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_LINEAR;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(s->CreateSampler(s->device.device,&si,nullptr,&s->linear)!=VK_SUCCESS)return fail();
    uint32_t n=s->device.frameSlots;VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,n},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,n*2}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=n;pi.poolSizeCount=2;pi.pPoolSizes=sizes;
    if(s->CreateDescriptorPool(s->device.device,&pi,nullptr,&s->pool)!=VK_SUCCESS)return fail();
    std::array<VkDescriptorSetLayout,8> layouts{};layouts.fill(s->setLayout);std::array<VkDescriptorSet,8> sets{};
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=s->pool;ai.descriptorSetCount=n;ai.pSetLayouts=layouts.data();
    if(s->AllocateDescriptorSets(s->device.device,&ai,sets.data())!=VK_SUCCESS)return fail();
    for(uint32_t i=0;i<n;++i)s->slots[i].set=sets[i];return true;
}
bool Effect::Resize(uint32_t w,uint32_t h) {if(!state_||!w||!h||w>16384||h>16384)return false;state_->width=w;state_->height=h;return true;}
bool Effect::HasLut(int32_t id) const {return state_ && state_->luts.find(id)!=state_->luts.end();}
bool Effect::AddLut(int32_t id,const Texels& pixels) {
    auto* s=state_;if(!s || id<0)return false;if(HasLut(id))return true;
    Impl::Lut lut;
    gxvk::vk::hdeffect::ImageDesc desc{VK_FORMAT_R8G8B8A8_UNORM,LutWidth,LutHeight,1,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT};
    lut.handle=s->api->CreateImage(&desc,&lut.image);if(!lut.handle){s->error="grade LUT image allocation failed";return false;}
    gxvk::vk::hdeffect::BufferDesc bd{LutBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,gxvk::vk::hdeffect::Host};lut.staging=s->api->CreateBuffer(&bd,&lut.buffer);
    if(!lut.staging || !lut.buffer.mapped){if(lut.staging)s->api->RetireBuffer(lut.staging,0);s->api->RetireImage(lut.handle,0);s->error="grade LUT upload allocation failed";return false;}
    auto* rgba=static_cast<uint8_t*>(lut.buffer.mapped);
    for(size_t i=0;i<LutBytes;i+=4){rgba[i]=pixels[i+2];rgba[i+1]=pixels[i+1];rgba[i+2]=pixels[i];rgba[i+3]=pixels[i+3];}
    s->api->FlushBuffer(lut.staging);
    try {s->luts.emplace(id,lut);}catch(...) {s->api->RetireBuffer(lut.staging,0);s->api->RetireImage(lut.handle,0);s->error="grade LUT CPU cache allocation failed";return false;}
    s->error="";return true;
}
bool Effect::Record(const gxvk::vk::hdeffect::StageContext& ctx,int32_t a,int32_t b,const Parameters& parameters) {
    auto* s=state_;if(!s)return false;
    if(ctx.structSize<sizeof ctx || ctx.point!=Point || !ctx.cmd || !ctx.colorView || !ctx.serial || ctx.colorFormat!=VK_FORMAT_R8G8B8A8_UNORM ||
       ctx.colorLayout!=VK_IMAGE_LAYOUT_GENERAL || ctx.frameSlots!=s->device.frameSlots || ctx.frameSlot>=s->device.frameSlots || ctx.extent.width!=s->width || ctx.extent.height!=s->height) {s->error="grade frame/extent unavailable";return false;}
    auto& slot=s->slots[ctx.frameSlot];if(slot.serial==ctx.serial || slot.serial>ctx.completedSerial){s->error="grade slot in flight/duplicate";return false;}
    auto ia=s->luts.find(a),ib=s->luts.find(b);if(ia==s->luts.end()||ib==s->luts.end()){s->error="grade LUT not prepared";return false;}
    Parameters p=parameters;if(!std::isfinite(p.blend)||!std::isfinite(p.strength)||!std::isfinite(p.debug)){s->error="grade non-finite parameters";return false;}
    p.blend=Sat(p.blend);p.strength=std::clamp(p.strength,0.0f,2.0f);p.swapRB=ctx.swapRB;
    for(auto* lut:{&ia->second,&ib->second}) {
        if(!lut->uploaded) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=lut->image.image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            s->CmdPipelineBarrier(ctx.cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={LutWidth,LutHeight,1};
            s->CmdCopyBufferToImage(ctx.cmd,lut->buffer.buffer,lut->image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            s->CmdPipelineBarrier(ctx.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            lut->uploaded=true;
        }
        lut->serial=ctx.serial;
    }
    VkDescriptorImageInfo images[3]={{VK_NULL_HANDLE,ctx.colorView,ctx.colorLayout},{s->linear,ia->second.image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{s->linear,ib->second.image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[3]{};for(uint32_t i=0;i<3;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=slot.set;w.dstBinding=i;w.descriptorCount=1;w.descriptorType=i?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&images[i];}
    s->UpdateDescriptorSets(s->device.device,3,writes,0,nullptr);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    s->CmdPipelineBarrier(ctx.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    s->CmdBindPipeline(ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipeline);s->CmdBindDescriptorSets(ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->layout,0,1,&slot.set,0,nullptr);
    s->CmdPushConstants(ctx.cmd,s->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof p,&p);s->CmdDispatch(ctx.cmd,(s->width+7)/8,(s->height+7)/8,1);
    barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT;
    s->CmdPipelineBarrier(ctx.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    slot.serial=s->last=ctx.serial;s->error="";return true;
}
bool Effect::Shutdown(uint64_t completed) {
    auto* s=state_;if(!s)return true;if(completed<s->last){s->error="grade shutdown pending serial";return false;}
    for(auto& entry:s->luts){auto& lut=entry.second;if(lut.staging)s->api->RetireBuffer(lut.staging,lut.serial);if(lut.handle)s->api->RetireImage(lut.handle,lut.serial);}
    if(s->pool)s->DestroyDescriptorPool(s->device.device,s->pool,nullptr);if(s->linear)s->DestroySampler(s->device.device,s->linear,nullptr);
    if(s->pipeline)s->DestroyPipeline(s->device.device,s->pipeline,nullptr);if(s->layout)s->DestroyPipelineLayout(s->device.device,s->layout,nullptr);
    if(s->setLayout)s->DestroyDescriptorSetLayout(s->device.device,s->setLayout,nullptr);delete s;state_=nullptr;return true;
}
uint64_t Effect::LastUsed() const {return state_?state_->last:0;}
const char* Effect::Error() const {return state_?state_->error:"grade not initialized";}
}
