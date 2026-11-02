// SPDX-License-Identifier: GPL-3.0-or-later
// HD: from ChatGPT's Cata water kit (2026-10-26), reviewed; owned and enabled by CataWaterService.cpp.
#include "CataSceneCapture.h"
#include <array>
#include <cstddef>

#include "shaders/CataBlur.spv.h"   // HD

namespace HD::CataWater {
bool SceneCapture::Initialize(const gxvk::vk::hdeffect::Api* api, const std::vector<uint32_t>& spv) {
    if (api_ || !api || api->apiVersion != gxvk::vk::hdeffect::ApiVersion ||
        api->structSize < offsetof(gxvk::vk::hdeffect::Api, RetireImage) + sizeof(api->RetireImage) ||
        !api->GetDeviceInfo || !api->CreateImage || !api->RetireImage ||
        spv.size() < 5 || spv.size() > 1024 * 1024 || spv[0] != 0x07230203) {
        error_ = "capture API/SPIR-V invalid, or service already initialized"; return false;
    }
    api_ = api; device_.structSize = sizeof device_;
    if (!api_->GetDeviceInfo(&device_) || !device_.device || !device_.getDeviceProcAddr || !device_.getInstanceProcAddr ||
        !device_.frameSlots || device_.frameSlots > 8) {
        error_ = "capture device unavailable"; api_ = nullptr; return false;
    }
#define CATA_LOAD(name) name = reinterpret_cast<PFN_vk##name>(device_.getDeviceProcAddr(device_.device, "vk" #name)); \
    if (!name) { error_ = "missing vk" #name; api_ = nullptr; return false; }
    CATA_VK_FUNCTIONS(CATA_LOAD)
#undef CATA_LOAD
    auto formats = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
        device_.getInstanceProcAddr(device_.instance, "vkGetPhysicalDeviceFormatProperties"));
    if (!formats) { error_ = "format query unavailable"; api_ = nullptr; return false; }
    for (VkFormat format : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32_SFLOAT}) {
        VkFormatProperties caps{}; formats(device_.physicalDevice, format, &caps);
        auto required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((caps.optimalTilingFeatures & required) != static_cast<VkFormatFeatureFlags>(required)) {
            error_ = "capture storage/sample format unsupported"; api_ = nullptr; return false;
        }
    }
    auto failed = [&]() { error_ = "capture pipeline/resource creation failed"; StopAndDestroy(0); return false; };
    std::array<VkDescriptorSetLayoutBinding,4> bindings{};
    for (uint32_t i = 0; i < 4; ++i) {
        bindings[i].binding = i; bindings[i].descriptorCount = 1; bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].descriptorType = i == 1 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 4; layout.pBindings = bindings.data();
    if (CreateDescriptorSetLayout(device_.device,&layout,nullptr,&descriptorLayout_) != VK_SUCCESS) return failed();
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,12};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &descriptorLayout_; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &push;
    if (CreatePipelineLayout(device_.device,&pl,nullptr,&pipelineLayout_) != VK_SUCCESS) return failed();
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize = spv.size() * sizeof(uint32_t); shader.pCode = spv.data();
    if (CreateShaderModule(device_.device,&shader,nullptr,&module) != VK_SUCCESS) return failed();
    VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cp.stage.module = module; cp.stage.pName = "main"; cp.layout = pipelineLayout_;
    VkResult result = CreateComputePipelines(device_.device,device_.pipelineCache,1,&cp,nullptr,&pipeline_);
    DestroyShaderModule(device_.device,module,nullptr);
    if (result != VK_SUCCESS) return failed();
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_NEAREST; sampler.minFilter = VK_FILTER_NEAREST; sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (CreateSampler(device_.device,&sampler,nullptr,&sampler_) != VK_SUCCESS) return failed();
    std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,3 * device_.frameSlots},
                                           {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,device_.frameSlots}}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = device_.frameSlots; pool.poolSizeCount = 2; pool.pPoolSizes = sizes.data();
    if (CreateDescriptorPool(device_.device,&pool,nullptr,&pool_) != VK_SUCCESS) return failed();
    slots_.resize(device_.frameSlots);
    std::vector<VkDescriptorSetLayout> layouts(device_.frameSlots,descriptorLayout_);
    std::vector<VkDescriptorSet> sets(device_.frameSlots);
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = pool_; allocate.descriptorSetCount = device_.frameSlots; allocate.pSetLayouts = layouts.data();
    if (AllocateDescriptorSets(device_.device,&allocate,sets.data()) != VK_SUCCESS) return failed();
    for (size_t i = 0; i < slots_.size(); ++i) slots_[i].descriptors = sets[i];
    blurReady_ = BlurPipeline();   // HD: optional, StopAndDestroy frees whatever it made
    error_.clear(); return true; // remains disabled until the owner opts in
}
// HD: Cata's KawaseBlur pipeline: one combined sampler (the source, linear clamp), one storage image (the target).
bool SceneCapture::BlurPipeline() {
    std::array<VkDescriptorSetLayoutBinding,2> bindings{};
    for (uint32_t i = 0; i < 2; ++i) {
        bindings[i].binding = i; bindings[i].descriptorCount = 1; bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 2; layout.pBindings = bindings.data();
    if (CreateDescriptorSetLayout(device_.device,&layout,nullptr,&blurLayout_) != VK_SUCCESS) return false;
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,20};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &blurLayout_; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &push;
    if (CreatePipelineLayout(device_.device,&pl,nullptr,&blurPipelineLayout_) != VK_SUCCESS) return false;
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize = sizeof kCataBlurSpv; shader.pCode = kCataBlurSpv;
    if (CreateShaderModule(device_.device,&shader,nullptr,&module) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cp.stage.module = module; cp.stage.pName = "main"; cp.layout = blurPipelineLayout_;
    VkResult result = CreateComputePipelines(device_.device,device_.pipelineCache,1,&cp,nullptr,&blurPipeline_);
    DestroyShaderModule(device_.device,module,nullptr);
    if (result != VK_SUCCESS) return false;
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR; sampler.minFilter = VK_FILTER_LINEAR; sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (CreateSampler(device_.device,&sampler,nullptr,&linear_) != VK_SUCCESS) return false;
    const uint32_t sets = 5 * device_.frameSlots;
    std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,sets},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,sets}}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = sets; pool.poolSizeCount = 2; pool.pPoolSizes = sizes.data();
    if (CreateDescriptorPool(device_.device,&pool,nullptr,&blurPool_) != VK_SUCCESS) return false;
    std::vector<VkDescriptorSetLayout> layouts(sets,blurLayout_);
    std::vector<VkDescriptorSet> allocated(sets);
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = blurPool_; allocate.descriptorSetCount = sets; allocate.pSetLayouts = layouts.data();
    if (AllocateDescriptorSets(device_.device,&allocate,allocated.data()) != VK_SUCCESS) return false;
    for (size_t i = 0; i < slots_.size(); ++i)
        for (size_t p = 0; p < 5; ++p) slots_[i].blurSets[p] = allocated[i * 5 + p];
    return true;
}
// HD: Cata KawaseBlur(scene, 512x512, 0.25): the stretch into the target, then four-tap passes ping-ponged
// target -> temp -> target ..., each 0.25 texel further out (0, 0.25, 0.5, 0.75). Cata runs a 5th pass that only
// writes the temp; the target, which the water samples, holds the 4th, so the 5th is left out. The copy is already
// readable (Barrier(after)).
bool SceneCapture::Blur(VkCommandBuffer cmd, Slot& slot) {
    if (!blurReady_ || !slot.blurHandle[0] || !slot.blurHandle[1]) return false;
    const VkImageView src[5] = {slot.color.view, slot.blur[0].view, slot.blur[1].view, slot.blur[0].view, slot.blur[1].view};
    std::array<VkDescriptorImageInfo,10> images{};
    std::array<VkWriteDescriptorSet,10> writes{};
    for (uint32_t p = 0; p < 5; ++p) {
        images[2 * p] = {linear_,src[p],VK_IMAGE_LAYOUT_GENERAL};
        images[2 * p + 1] = {VK_NULL_HANDLE,slot.blur[p == 0 ? 0 : (p & 1)].view,VK_IMAGE_LAYOUT_GENERAL};
        for (uint32_t b = 0; b < 2; ++b) {
            auto& w = writes[2 * p + b]; w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = slot.blurSets[p]; w.dstBinding = b;
            w.descriptorCount = 1; w.pImageInfo = &images[2 * p + b];
            w.descriptorType = b == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        }
    }
    UpdateDescriptorSets(device_.device,10,writes.data(),0,nullptr);
    std::array<VkImageMemoryBarrier,2> barriers{};
    for (size_t i = 0; i < 2; ++i) {
        auto& b = barriers[i]; b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = slot.blurInitialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.image = slot.blur[i].image; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; b.subresourceRange.levelCount = b.subresourceRange.layerCount = 1;
        b.srcAccessMask = slot.blurInitialized ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    CmdPipelineBarrier(cmd,slot.blurInitialized ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,2,barriers.data());
    CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,blurPipeline_);
    const float texel = 1.0f / kBlurSize;
    for (uint32_t p = 0; p < 5; ++p) {
        if (p) {   // the previous pass's writes, read now; its reads, before this pass overwrites its source's twin
            VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            m.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; m.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&m,0,nullptr,0,nullptr);
        }
        CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,blurPipelineLayout_,0,1,&slot.blurSets[p],0,nullptr);
        struct { float texel[2], offset[2]; uint32_t taps; } push{{texel,texel},{0,0},p == 0 ? 1u : 4u};
        if (p > 1) push.offset[0] = push.offset[1] = (p - 1) * 0.25f * texel;
        CmdPushConstants(cmd,blurPipelineLayout_,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,&push);
        CmdDispatch(cmd,(kBlurSize + 7) / 8,(kBlurSize + 7) / 8,1);
    }
    VkMemoryBarrier done{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    done.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; done.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       0,1,&done,0,nullptr,0,nullptr);
    slot.blurInitialized = true;
    return true;
}
bool SceneCapture::Prepare(uint32_t index,uint64_t completed,VkExtent2D extent) {
    if(!api_ || index>=slots_.size() || !extent.width || !extent.height || extent.width>16384 || extent.height>16384)return false;
    auto& slot=slots_[index];slot.valid=false;
    if(slot.lastSerial>completed){error_="capture preparation slot still in flight";return false;}
    return Targets(slot,extent);
}
void SceneCapture::Retire(Slot& s) {
    if (s.colorHandle) api_->RetireImage(s.colorHandle,s.lastSerial);
    if (s.depthHandle) api_->RetireImage(s.depthHandle,s.lastSerial);
    for (uint32_t i = 0; i < 2; ++i) if (s.blurHandle[i]) api_->RetireImage(s.blurHandle[i],s.lastSerial);   // HD
    s.blurHandle[0] = s.blurHandle[1] = nullptr; s.blur[0] = s.blur[1] = {}; s.blurInitialized = false;
    s.colorHandle = s.depthHandle = nullptr; s.color = {}; s.depth = {}; s.extent = {};
    s.valid = s.initialized = s.sampled = false; s.snapshot = {};
}
bool SceneCapture::Targets(Slot& slot, VkExtent2D extent) {
    if (slot.colorHandle && slot.depthHandle && slot.extent.width == extent.width && slot.extent.height == extent.height) return true;
    Retire(slot);
    gxvk::vk::hdeffect::ImageDesc desc{}; desc.width = extent.width; desc.height = extent.height; desc.mipLevels = 1;
    desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    desc.format = VK_FORMAT_R8G8B8A8_UNORM; slot.colorHandle = api_->CreateImage(&desc,&slot.color);
    desc.format = VK_FORMAT_R32_SFLOAT; slot.depthHandle = api_->CreateImage(&desc,&slot.depth);
    if (!slot.colorHandle || !slot.depthHandle) { Retire(slot); error_ = "capture image allocation failed"; return false; }
    if (blurReady_) {   // HD: the blur's pair; without it the copy stays usable
        desc.width = desc.height = kBlurSize; desc.format = VK_FORMAT_R8G8B8A8_UNORM;
        for (uint32_t i = 0; i < 2; ++i) slot.blurHandle[i] = api_->CreateImage(&desc,&slot.blur[i]);
    }
    slot.extent = extent; return true;
}
void SceneCapture::Barrier(VkCommandBuffer cmd, const Slot& slot, bool before) {
    std::array<VkImageMemoryBarrier,2> barriers{};
    for (size_t i = 0; i < barriers.size(); ++i) {
        auto& b = barriers[i]; b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = before && !slot.initialized ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL; b.image = i ? slot.depth.image : slot.color.image;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; b.subresourceRange.levelCount = b.subresourceRange.layerCount = 1;
        b.srcAccessMask = before ? (slot.initialized ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0) : VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = before ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
    }
    VkPipelineStageFlags src = before ? (slot.initialized ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
                                      : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkPipelineStageFlags dst = before ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    CmdPipelineBarrier(cmd,src,dst,0,0,nullptr,0,nullptr,2,barriers.data());
}
void SceneCapture::Record(const gxvk::vk::hdeffect::StageContext& ctx) {
    if (!enabled_ || ctx.structSize < sizeof ctx || ctx.frameSlot >= slots_.size() || ctx.frameSlots != slots_.size() ||
        ctx.point != CapturePoint || !ctx.serial || !ctx.sceneValid || !ctx.scene || !ctx.cmd ||
        !ctx.colorView || !ctx.depthView || ctx.colorFormat != VK_FORMAT_R8G8B8A8_UNORM ||
        !ctx.extent.width || !ctx.extent.height || ctx.extent.width > 16384 || ctx.extent.height > 16384) {
        error_ = "capture frame inputs incomplete"; return;
    }
    Slot& slot = slots_[ctx.frameSlot]; slot.valid = false;
    if (slot.lastSerial > ctx.completedSerial) { error_ = "capture slot is still in flight"; return; }
    if (!slot.colorHandle || !slot.depthHandle || slot.extent.width!=ctx.extent.width || slot.extent.height!=ctx.extent.height) {error_="capture targets not prepared";return;}
    if(slot.sampled) SamplingLayout(slot,ctx.cmd,false);
    std::array<VkDescriptorImageInfo,4> images{{
        {VK_NULL_HANDLE,ctx.colorView,ctx.colorLayout}, {sampler_,ctx.depthView,ctx.depthLayout},
        {VK_NULL_HANDLE,slot.color.view,VK_IMAGE_LAYOUT_GENERAL}, {VK_NULL_HANDLE,slot.depth.view,VK_IMAGE_LAYOUT_GENERAL}}};
    std::array<VkWriteDescriptorSet,4> writes{};
    for (uint32_t i = 0; i < 4; ++i) {
        auto& w = writes[i]; w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = slot.descriptors; w.dstBinding = i;
        w.descriptorCount = 1; w.descriptorType = i == 1 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w.pImageInfo = &images[i];
    }
    UpdateDescriptorSets(device_.device,4,writes.data(),0,nullptr);
    Barrier(ctx.cmd,slot,true);
    CmdBindPipeline(ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_);
    CmdBindDescriptorSets(ctx.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout_,0,1,&slot.descriptors,0,nullptr);
    const uint32_t push[3] = {ctx.extent.width,ctx.extent.height,ctx.swapRB};
    CmdPushConstants(ctx.cmd,pipelineLayout_,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,push);
    CmdDispatch(ctx.cmd,(ctx.extent.width + 7) / 8,(ctx.extent.height + 7) / 8,1);
    Barrier(ctx.cmd,slot,false);
    const bool blurred = Blur(ctx.cmd,slot);   // HD
    slot.snapshot.blurred = blurred ? slot.blur[0].view : VK_NULL_HANDLE;
    slot.snapshot.blurredImage = blurred ? slot.blur[0].image : VK_NULL_HANDLE;
    slot.initialized = slot.valid = true; slot.lastSerial = lastSerial_ = ctx.serial;
    slot.snapshot.serial = ctx.serial; slot.snapshot.extent = ctx.extent;
    slot.snapshot.color = slot.color.view; slot.snapshot.rawDepth = slot.depth.view;
    slot.snapshot.colorImage = slot.color.image; slot.snapshot.rawDepthImage = slot.depth.image;   // HD: step C
    slot.snapshot.scene = *ctx.scene; slot.snapshot.viewport = ctx.viewport;
    slot.snapshot.jitter[0] = ctx.jitter[0]; slot.snapshot.jitter[1] = ctx.jitter[1];
    error_.clear();
}
void SceneCapture::SamplingLayout(Slot& slot,VkCommandBuffer cmd,bool sampled) {
    if (slot.sampled==sampled) return;
    // These opaque handles come only from our native hdeffect image factory, not a foreign ABI.
    auto* color=static_cast<gxvk::vk::texture::Image*>(slot.colorHandle);
    auto* depth=static_cast<gxvk::vk::texture::Image*>(slot.depthHandle);
    auto* blur=slot.snapshot.blurred ? static_cast<gxvk::vk::texture::Image*>(slot.blurHandle[0]) : nullptr;
    gxvk::vk::texture::Image* images[3]={color,depth,blur};
    VkImageMemoryBarrier barriers[3]{};uint32_t count=0;
    const auto oldLayout=sampled?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const auto newLayout=sampled?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
    for (auto* image:images) {
        if (!image) continue;
        auto& b=barriers[count++];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask=sampled?(VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT):VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask=sampled?VK_ACCESS_SHADER_READ_BIT:(VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT);
        b.oldLayout=oldLayout;b.newLayout=newLayout;b.image=image->handle;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        image->layout=newLayout;
    }
    CmdPipelineBarrier(cmd,sampled?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        sampled?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,count,barriers);
    slot.sampled=sampled;slot.snapshot.layout=newLayout;
}
bool SceneCapture::MakeSampled(VkCommandBuffer cmd,uint64_t serial,VkExtent2D extent) {
    if (!cmd || !Get(serial,extent) || api_!=gxvk::vk::hdeffect::Get()) return false;
    for (auto& slot:slots_) if(slot.valid && slot.snapshot.serial==serial &&
        slot.extent.width==extent.width && slot.extent.height==extent.height) {
        SamplingLayout(slot,cmd,true);return true;
    }
    return false;
}
const gxvk::vk::texture::Image* SceneCapture::SampledImage(uint64_t serial,VkExtent2D extent,uint32_t kind) const {
    if (kind>2 || !Get(serial,extent) || api_!=gxvk::vk::hdeffect::Get()) return nullptr;
    for (const auto& slot:slots_) if(slot.valid && slot.sampled && slot.snapshot.serial==serial &&
        slot.extent.width==extent.width && slot.extent.height==extent.height) {
        if(kind==2 && !slot.snapshot.blurred)return nullptr;
        return static_cast<const gxvk::vk::texture::Image*>(kind==0?slot.colorHandle:kind==1?slot.depthHandle:slot.blurHandle[0]);
    }
    return nullptr;
}
const SceneCapture::Snapshot* SceneCapture::Get(uint64_t serial, VkExtent2D extent) const {
    if (!enabled_) return nullptr;
    for (const auto& slot : slots_) if (slot.valid && slot.snapshot.serial == serial &&
        slot.extent.width == extent.width && slot.extent.height == extent.height) return &slot.snapshot;
    return nullptr;
}
bool SceneCapture::StopAndDestroy(uint64_t completed) {
    enabled_ = false;
    if (!api_) return true;
    if (completed < lastSerial_) { error_ = "capture shutdown pending GPU serial completion"; return false; }
    for (auto& slot : slots_) Retire(slot);
    if (pool_) DestroyDescriptorPool(device_.device,pool_,nullptr);
    if (sampler_) DestroySampler(device_.device,sampler_,nullptr);
    if (pipeline_) DestroyPipeline(device_.device,pipeline_,nullptr);
    if (pipelineLayout_) DestroyPipelineLayout(device_.device,pipelineLayout_,nullptr);
    if (descriptorLayout_) DestroyDescriptorSetLayout(device_.device,descriptorLayout_,nullptr);
    if (blurPool_) DestroyDescriptorPool(device_.device,blurPool_,nullptr);   // HD: the blur
    if (linear_) DestroySampler(device_.device,linear_,nullptr);
    if (blurPipeline_) DestroyPipeline(device_.device,blurPipeline_,nullptr);
    if (blurPipelineLayout_) DestroyPipelineLayout(device_.device,blurPipelineLayout_,nullptr);
    if (blurLayout_) DestroyDescriptorSetLayout(device_.device,blurLayout_,nullptr);
    blurPool_ = VK_NULL_HANDLE; linear_ = VK_NULL_HANDLE; blurPipeline_ = VK_NULL_HANDLE;
    blurPipelineLayout_ = VK_NULL_HANDLE; blurLayout_ = VK_NULL_HANDLE; blurReady_ = false;
    pool_ = VK_NULL_HANDLE; sampler_ = VK_NULL_HANDLE; pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE; descriptorLayout_ = VK_NULL_HANDLE;
    slots_.clear(); api_ = nullptr; device_ = {}; lastSerial_ = 0;
    return true;
}
}
