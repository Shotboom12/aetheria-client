// SPDX-License-Identifier: GPL-3.0-or-later
// HD: the Cata water's dynamic displacement map, tranche 2 (see CataRipples.h). A BeforeLiquids stage with its own
// render passes, outside the world pass (as CataMirror.cpp). Every map is left in SHADER_READ_ONLY_OPTIMAL between
// passes; the passes' dependencies order a step's reads after the one before it and the water's reads after both.
#include "CataRipples.h"

#include <cmath>
#include <cstring>

#include "Engine/Source/Gx/GxSeam.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Pipelines.hpp"
#include "shaders/CataRippleFull.vert.spv.h"
#include "shaders/CataRippleMesh.frag.spv.h"
#include "shaders/CataRipplePerturb.frag.spv.h"
#include "shaders/CataRipplePerturb.vert.spv.h"
#include "shaders/CataRipplePropagate.frag.spv.h"

namespace HD::CataWater {
namespace {
namespace context = gxvk::vk::context;
namespace pipelines = gxvk::vk::pipelines;

constexpr VkFormat kMapFormat = VK_FORMAT_R16G16_SFLOAT;      // Cata GxTex_Gr1616F
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr float kDisplSize = 128.0f;                         // CWorldScene::Render: SetDisplMapInfo(s_playerPos, 128 x 128)
constexpr float kDisplMapScale[2] = {1.0f, 1.0f};            // s_displMapScale
constexpr float kPerturbAmplitudeScale[2] = {1.25f, 0.25f};  // s_perturbAmplitudeScale
constexpr uint32_t kMaxPerturbations = 256;                  // Liquid::CreatePerturbator / AddPerturbation

struct Op {
    enum Kind { Perturb, Propagate } kind;
    uint32_t map, image, source;
    uint32_t quadFirst, quadCount, meshFirst, meshCount;
    float offset[2];
};

// Cata's height of a perturbation: amplitude scaled for its map, clamped, and for a kept one, its period's sine.
float Height(float amplitude, uint32_t period, uint32_t map, uint32_t elapsed) {
    float z = kPerturbAmplitudeScale[map] * amplitude;
    if (z < -1.0f) z = -1.0f;
    else if (z >= 1.0f) z = 1.0f;
    if (period) z = static_cast<float>(std::sin(static_cast<double>(elapsed % period) * 6.2831855f / static_cast<double>(period))) * z;
    return z;
}
}

bool Ripples::Initialize(const gxvk::vk::hdeffect::Api* api) {
    const context::Handles* h = context::Get();
    if (api_ || !api || api!=gxvk::vk::hdeffect::Get() || !api->GetDeviceInfo || !api->CreateImage || !api->RetireImage || !api->CreateBuffer ||
        !api->RetireBuffer || !api->FlushBuffer || !h || !h->dapi) {
        error_ = "ripples: API missing, or already initialized";
        return false;
    }
    device_.structSize = sizeof device_;
    if (!api->GetDeviceInfo(&device_) || !device_.device || !device_.frameSlots || device_.frameSlots > 8 ||
        !device_.getInstanceProcAddr || !device_.physicalDevice) {
        error_ = "ripples: device unavailable";
        return false;
    }
    auto formats = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
        device_.getInstanceProcAddr(device_.instance, "vkGetPhysicalDeviceFormatProperties"));
    VkFormatProperties caps = {};
    if (formats) formats(device_.physicalDevice, kMapFormat, &caps);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((caps.optimalTilingFeatures & need) != need) {
        error_ = "ripples: RG16F maps unsupported";
        return false;
    }
    api_ = api;
    const gxvk::vk::loader::Device& d = *h->dapi;

    // Two passes over one RG16F map: LOAD (a perturbation adds to it; a propagation overwrites it) and CLEAR.
    VkAttachmentDescription att = {};
    att.format = kMapFormat;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency dep[2] = {};
    // Before: the earlier passes' writes (this map's, or the source's) and the reads of this map (the water last
    // frame, a propagation) are done.
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    // After: the next pass's fragments and the water's read it.
    dep[1].srcSubpass = 0;
    dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &att;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 2;
    rp.pDependencies = dep;
    if (d.CreateRenderPass(device_.device, &rp, nullptr, &load_) != VK_SUCCESS) load_ = VK_NULL_HANDLE;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (d.CreateRenderPass(device_.device, &rp, nullptr, &clear_) != VK_SUCCESS) clear_ = VK_NULL_HANDLE;
    if (!load_ || !clear_ || !Pipelines()) {
        StopAndDestroy();
        error_ = "ripples: passes or pipelines failed";
        return false;
    }
    streams_.assign(device_.frameSlots, Stream());

    error_ = "";
    return true;
}

bool Ripples::Pipelines() {
    const gxvk::vk::loader::Device& d = *context::Get()->dapi;
    const struct { const uint32_t* code; size_t size; } spv[5] = {
        {kCataRippleFullVertSpv, sizeof kCataRippleFullVertSpv}, {kCataRipplePropagateSpv, sizeof kCataRipplePropagateSpv},
        {kCataRipplePerturbVertSpv, sizeof kCataRipplePerturbVertSpv}, {kCataRipplePerturbSpv, sizeof kCataRipplePerturbSpv},
        {kCataRippleMeshSpv, sizeof kCataRippleMeshSpv}};
    for (uint32_t i = 0; i < 5; ++i) {
        VkShaderModuleCreateInfo module = {};
        module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module.codeSize = spv[i].size;
        module.pCode = spv[i].code;
        if (d.CreateShaderModule(device_.device, &module, nullptr, &modules_[i]) != VK_SUCCESS) return false;
    }
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo setLayout = {};
    setLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayout.bindingCount = 1;
    setLayout.pBindings = &binding;
    if (d.CreateDescriptorSetLayout(device_.device, &setLayout, nullptr, &setLayout_) != VK_SUCCESS) return false;
    VkPushConstantRange range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo layout = {};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &setLayout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    if (d.CreatePipelineLayout(device_.device, &layout, nullptr, &layout_) != VK_SUCCESS) return false;

    // The samplers: the propagation reads texel centres (nearest), perturbTex is Cata's GxTex_LinearMipNearest.
    VkSamplerCreateInfo sampler = {};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0.0f;
    if (d.CreateSampler(device_.device, &sampler, nullptr, &point_) != VK_SUCCESS) return false;
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.maxLod = 16.0f;
    if (d.CreateSampler(device_.device, &sampler, nullptr, &linear_) != VK_SUCCESS) return false;
    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * kMaps + 1};
    VkDescriptorPoolCreateInfo pool = {};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 2 * kMaps + 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    if (d.CreateDescriptorPool(device_.device, &pool, nullptr, &pool_) != VK_SUCCESS) return false;
    VkDescriptorSetLayout layouts[2 * kMaps + 1];
    VkDescriptorSet sets[2 * kMaps + 1];
    for (auto& l : layouts) l = setLayout_;
    VkDescriptorSetAllocateInfo allocate = {};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = pool_;
    allocate.descriptorSetCount = 2 * kMaps + 1;
    allocate.pSetLayouts = layouts;
    if (d.AllocateDescriptorSets(device_.device, &allocate, sets) != VK_SUCCESS) return false;
    for (uint32_t i = 0; i < 2 * kMaps; ++i) images_[i].source = sets[i];
    textureSet_ = sets[2 * kMaps];

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].pName = "main";
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkVertexInputBindingDescription vertexBinding = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                                       {1, 0, VK_FORMAT_R32G32_SFLOAT, 3 * sizeof(float)}};
    VkPipelineVertexInputStateCreateInfo none = {};
    none.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineVertexInputStateCreateInfo vertices = none;
    vertices.vertexBindingDescriptionCount = 1;
    vertices.pVertexBindingDescriptions = &vertexBinding;
    vertices.vertexAttributeDescriptionCount = 2;
    vertices.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly = {};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport = {};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;   // Cata: GxRs_Culling 0
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment = {};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
    VkPipelineColorBlendStateCreateInfo blend = {};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout_;
    info.renderPass = load_;
    info.basePipelineIndex = -1;

    // The propagation: a full-map triangle, written as it is.
    stages[0].module = modules_[0];
    stages[1].module = modules_[1];
    info.pVertexInputState = &none;
    if (d.CreateGraphicsPipelines(device_.device, VK_NULL_HANDLE, 1, &info, nullptr, &propagate_) != VK_SUCCESS) {
        propagate_ = VK_NULL_HANDLE;
        return false;
    }
    // The perturbations: added (Cata GxRs_BlendingMode 3 with an alpha of 1).
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    info.pVertexInputState = &vertices;
    stages[0].module = modules_[2];
    stages[1].module = modules_[3];
    if (d.CreateGraphicsPipelines(device_.device, VK_NULL_HANDLE, 1, &info, nullptr, &perturb_) != VK_SUCCESS) {
        perturb_ = VK_NULL_HANDLE;
        return false;
    }
    stages[1].module = modules_[4];
    if (d.CreateGraphicsPipelines(device_.device, VK_NULL_HANDLE, 1, &info, nullptr, &mesh_) != VK_SUCCESS) {
        mesh_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool Ripples::Maps() {
    if (images_[0].framebuffer) return true;
    const context::Handles* h = context::Get();
    if (!h || !h->dapi) return false;
    const gxvk::vk::loader::Device& d = *h->dapi;
    for (uint32_t i = 0; i < 2 * kMaps; ++i) {
        Image& image = images_[i];
        gxvk::vk::hdeffect::ImageDesc desc{};
        desc.format = kMapFormat;
        desc.width = desc.height = kTexSize;
        desc.mipLevels = 1;
        desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        image.handle = api_->CreateImage(&desc, &image.info);
        VkFramebufferCreateInfo fb = {};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = load_;   // compatible with clear_: same attachment
        fb.attachmentCount = 1;
        fb.pAttachments = &image.info.view;
        fb.width = fb.height = kTexSize;
        fb.layers = 1;
        if (!image.handle || d.CreateFramebuffer(device_.device, &fb, nullptr, &image.framebuffer) != VK_SUCCESS) {
            image.framebuffer = VK_NULL_HANDLE;
            Retire(0);
            error_ = "ripples: map allocation failed";
            return false;
        }
        VkDescriptorImageInfo source = {point_, image.info.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = image.source;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &source;
        d.UpdateDescriptorSets(device_.device, 1, &write, 0, nullptr);
    }
    dirty_ = true;
    return true;
}

// XTextures\Ripples\perturbTex, from the engine's decoded levels: uploaded once.
bool Ripples::Texture(VkCommandBuffer cmd, uint64_t serial) {
    if (textureReady_) return true;
    const GxCataRipples& in = g_gxCataRipples;
    if (textureHandle_ || !in.m_texels || !in.m_texWidth || !in.m_texHeight || !in.m_texLevels || in.m_texLevels > 16 ||
        in.m_texWidth > 4096 || in.m_texHeight > 4096) {
        return false;
    }
    const gxvk::vk::loader::Device& d = *context::Get()->dapi;
    VkDeviceSize total = 0;
    VkBufferImageCopy regions[16] = {};
    for (uint32_t level = 0; level < in.m_texLevels; ++level) {
        const uint32_t w = in.m_texWidth >> level ? in.m_texWidth >> level : 1;
        const uint32_t h = in.m_texHeight >> level ? in.m_texHeight >> level : 1;
        regions[level].bufferOffset = total;
        regions[level].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        regions[level].imageExtent = {w, h, 1};
        total += VkDeviceSize(w) * h * 4;
    }
    gxvk::vk::hdeffect::BufferDesc bufferDesc{total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, gxvk::vk::hdeffect::Host};
    gxvk::vk::hdeffect::BufferInfo staging{};
    void* stagingHandle = api_->CreateBuffer(&bufferDesc, &staging);
    if (!stagingHandle || !staging.mapped) {
        if (stagingHandle) api_->RetireBuffer(stagingHandle, 0);
        return false;
    }
    memcpy(staging.mapped, in.m_texels, static_cast<size_t>(total));
    api_->FlushBuffer(stagingHandle);
    gxvk::vk::hdeffect::ImageDesc desc{};
    desc.format = kTextureFormat;
    desc.width = in.m_texWidth;
    desc.height = in.m_texHeight;
    desc.mipLevels = in.m_texLevels;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    textureHandle_ = api_->CreateImage(&desc, &texture_);
    if (!textureHandle_) {
        api_->RetireBuffer(stagingHandle, 0);
        return false;
    }
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture_.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, in.m_texLevels, 0, 1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    d.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    d.CmdCopyBufferToImage(cmd, staging.buffer, texture_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, in.m_texLevels, regions);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    d.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    api_->RetireBuffer(stagingHandle, serial);   // freed once this frame is done
    VkDescriptorImageInfo image = {linear_, texture_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = textureSet_;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    d.UpdateDescriptorSets(device_.device, 1, &write, 0, nullptr);
    textureReady_ = true;
    return true;
}

void Ripples::Enable(bool enabled) {
    if (!api_) return;
    if (enabled != enabled_) dirty_ = true;   // a fresh start: Cata clears the maps when they are (re)made
    enabled_ = enabled;
    g_gxCataRipples.m_active = enabled ? 1 : 0;
}

// Cata Liquid::SetDisplMapInfo: each map centred on the player, its corner snapped to its texels.
void Ripples::SetDisplMapInfo(const float center[3]) {
    for (uint32_t i = 0; i < kMaps; ++i) {
        const float texSize = static_cast<float>(kTexSize);
        const float width = kDisplSize * kDisplMapScale[i];
        const float height = kDisplMapScale[i] * kDisplSize;
        pos_[i][0] = center[0] - width * 0.5f;
        pos_[i][1] = center[1] - height * 0.5f;
        pos_[i][2] = center[2];
        size_[i][0] = width;
        size_[i][1] = height;
        pos_[i][0] = std::floor(pos_[i][0] * (texSize / width)) / (texSize / width);
        pos_[i][1] = std::floor(pos_[i][1] * (texSize / height)) / (texSize / height);
    }
}

void Ripples::BeginPass(VkCommandBuffer cmd, uint32_t image, bool clear) {
    const gxvk::vk::loader::Device& d = *context::Get()->dapi;
    VkClearValue zero = {};
    VkRenderPassBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = clear ? clear_ : load_;
    begin.framebuffer = images_[image].framebuffer;
    begin.renderArea.extent = {kTexSize, kTexSize};
    begin.clearValueCount = clear ? 1 : 0;
    begin.pClearValues = clear ? &zero : nullptr;
    d.CmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = {0.0f, 0.0f, static_cast<float>(kTexSize), static_cast<float>(kTexSize), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {kTexSize, kTexSize}};
    d.CmdSetViewport(cmd, 0, 1, &vp);
    d.CmdSetScissor(cmd, 0, 1, &scissor);
}

// Cata Liquid::ClearDisplMap: every map to 0.
void Ripples::Clear(VkCommandBuffer cmd) {
    const gxvk::vk::loader::Device& d = *context::Get()->dapi;
    for (uint32_t i = 0; i < 2 * kMaps; ++i) {
        BeginPass(cmd, i, true);
        d.CmdEndRenderPass(cmd);
    }
}

// Cata Liquid::PerturbDisplMap: each perturbation (x, y, radius, amplitude, period) a quad of perturbTex around its
// texel, in the map as it lies now (rawPos). In Vulkan's NDC (y down, the map's first row on top as in D3D's), without
// D3D9's half-texel shift.
void Ripples::Quads(const float* list, uint32_t count, uint32_t map, uint32_t time, std::vector<Vertex>& out) const {
    if (!list || !count) return;
    const float texSize = static_cast<float>(kTexSize);
    const float texel = 1.0f / texSize;
    const uint32_t elapsed = time - startTime_;
    for (uint32_t i = 0; i < count && i < kMaxPerturbations; ++i) {
        const float* p = list + 5 * i;
        const float amplitude = p[3];
        if (!(std::fabs(amplitude) >= 0.001f)) continue;
        float sizeY = std::floor(texSize * (p[2] / size_[map][1]));
        sizeY = sizeY >= 0.5f ? sizeY : 0.5f;
        float sizeX = std::floor(texSize * (p[2] / size_[map][0]));
        sizeX = sizeX >= 0.5f ? sizeX : 0.5f;
        sizeX *= texel;
        sizeY *= texel;
        float u = (p[0] - rawPos_[map][0]) / size_[map][0];
        float v = (p[1] - rawPos_[map][1]) / size_[map][1];
        u = std::floor(texSize * u) * texel;
        v = std::floor(texSize * v) * texel;
        if (!std::isfinite(u) || !std::isfinite(v) || u + sizeX < 0.0f || v + sizeY < 0.0f || u - sizeX > 1.0f || v - sizeY > 1.0f) continue;
        const float z = Height(amplitude, static_cast<uint32_t>(p[4]), map, elapsed);
        const float x0 = (u - sizeX) * 2.0f - 1.0f, x1 = (u + sizeX) * 2.0f - 1.0f;
        const float y0 = (v - sizeY) * 2.0f - 1.0f, y1 = (v + sizeY) * 2.0f - 1.0f;
        const Vertex c[4] = {{x0, y0, z, 0, 0}, {x1, y0, z, 1, 0}, {x0, y1, z, 0, 1}, {x1, y1, z, 1, 1}};
        const uint32_t order[6] = {0, 2, 1, 1, 2, 3};   // Cata's quadIndices
        for (uint32_t k : order) out.push_back(c[k]);
    }
}

// Cata Liquid::PerturbDisplMapMesh: the ships' hulls (world x, y per triangle corner), their height over the map.
void Ripples::Meshes(uint32_t map, uint32_t time, std::vector<Vertex>& out) const {
    const GxCataRipples& in = g_gxCataRipples;
    if (!in.m_meshes || !in.m_meshVerts || !in.m_meshCount) return;
    const uint32_t elapsed = time - startTime_;
    for (uint32_t m = 0; m < in.m_meshCount && m < 16; ++m) {
        const float* mesh = in.m_meshes + 4 * m;
        const uint32_t first = static_cast<uint32_t>(mesh[0]), corners = static_cast<uint32_t>(mesh[1]);
        if (!(std::fabs(mesh[2]) >= 0.001f) || corners % 3 || corners > 3 * 7500) continue;
        const float z = Height(mesh[2], static_cast<uint32_t>(mesh[3]), map, elapsed);
        for (uint32_t k = 0; k < corners; ++k) {
            const float* p = in.m_meshVerts + 2 * (first + k);
            const float x = ((p[0] - rawPos_[map][0]) / size_[map][0]) * 2.0f - 1.0f;
            const float y = ((p[1] - rawPos_[map][1]) / size_[map][1]) * 2.0f - 1.0f;
            out.push_back({x, y, z, 0, 0});
        }
    }
}

void Ripples::Record(const gxvk::vk::hdeffect::StageContext& ctx) {
    GxCataRipples& io = g_gxCataRipples;
    publishedSerial_=0;io.m_ready=0;
    const context::Handles* h = context::Get();
    if (!enabled_ || !io.m_wanted || ctx.structSize < sizeof ctx || !ctx.serial || ctx.frameSlots!=device_.frameSlots || ctx.frameSlot >= streams_.size() || !ctx.cmd || !h || !h->dapi)
        return;
    const gxvk::vk::loader::Device& d = *h->dapi;
    Stream& stream = streams_[ctx.frameSlot];
    if (stream.lastSerial > ctx.completedSerial) {
        error_ = "ripples: slot still in flight";
        return;
    }
    lastSerial_=ctx.serial; // Covers partial uploads/clears too, not just successful publication.
    if (!Maps()) return;
    const bool texture = Texture(ctx.cmd, ctx.serial);
    if(!texture){error_="ripples: perturbation texture unavailable";return;}
    const uint32_t time = io.m_time;
    if (!started_) {
        started_ = true;
        startTime_ = time;
    }
    SetDisplMapInfo(io.m_center);
    if (dirty_) {
        Clear(ctx.cmd);
        dirty_ = false;
        for (uint32_t i = 0; i < kMaps; ++i) lastTime_[i] = time - period_[i];
    }

    // Cata Liquid::UpdateDisplMap, its draws first written down with their vertices (the map moves between steps).
    std::vector<Vertex> quads, meshes;
    std::vector<Op> ops;
    auto perturb = [&](uint32_t map, const float* list, uint32_t count, bool hulls) {
        Op op = {Op::Perturb, map, current_[map], 0, static_cast<uint32_t>(quads.size()), 0, static_cast<uint32_t>(meshes.size()), 0, {0, 0}};
        if (texture) Quads(list, count, map, time, quads);
        if (hulls) Meshes(map, time, meshes);
        op.quadCount = static_cast<uint32_t>(quads.size()) - op.quadFirst;
        op.meshCount = static_cast<uint32_t>(meshes.size()) - op.meshFirst;
        if (op.quadCount || op.meshCount) ops.push_back(op);
    };
    // This frame's events (the rain's drops: surface 0), into each map as it is now.
    perturb(0, io.m_events, io.m_eventCount, false);
    for (uint32_t i = 0; i < kMaps; ++i) {
        const uint32_t period = period_[i];
        const uint32_t elapsed = time - lastTime_[i];
        const uint32_t steps = elapsed / period;
        interpolation_[i] = static_cast<float>(elapsed % period) / static_cast<float>(period);
        if (elapsed < period) continue;
        uint32_t count;
        if (steps > 2) {
            count = 2;
            lastTime_[i] = time;
        } else {
            count = steps;
            if (steps < 2) lastTime_[i] += steps * period;
            else lastTime_[i] = time;
        }
        while (count--) {
            // The kept perturbations: the units on surface 0 (map 0), the hulls on surface 1 (map 1).
            if (i == 0) perturb(0, io.m_units, io.m_unitCount, false);
            else perturb(1, nullptr, 0, true);
            // Cata Liquid::PropagateDisplMap: into the map's twin, the previous one read where the map was.
            const uint32_t previous = current_[i];
            current_[i] = ((previous - 1) & 1) + 2 * i;
            float dx = pos_[i][0] - rawPos_[i][0];
            float dy = pos_[i][1] - rawPos_[i][1];
            if (dy * dy + dx * dx >= 10000.0f) dx = dy = 0.0f;
            memcpy(rawPos_[i], pos_[i], sizeof rawPos_[i]);
            Op op = {Op::Propagate, i, current_[i], previous, 0, 0, 0, 0, {dx / size_[i][0], dy / size_[i][1]}};
            ops.push_back(op);
            ++steps_;
        }
    }

    // The vertices: this slot's stream buffer, grown when needed.
    const size_t bytes = (quads.size() + meshes.size()) * sizeof(Vertex);
    if (bytes) {
        if (!stream.handle || stream.info.size < bytes) {
            if (stream.handle) api_->RetireBuffer(stream.handle, stream.lastSerial);
            stream = Stream();
            gxvk::vk::hdeffect::BufferDesc desc{VkDeviceSize(bytes < 65536 ? 65536 : bytes * 2), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gxvk::vk::hdeffect::Stream};
            stream.handle = api_->CreateBuffer(&desc, &stream.info);
            if (!stream.handle || !stream.info.mapped) {
                if (stream.handle) api_->RetireBuffer(stream.handle, 0);
                stream = Stream();
            }
        }
        if (stream.handle) {
            if (!quads.empty()) memcpy(stream.info.mapped, quads.data(), quads.size() * sizeof(Vertex));
            if (!meshes.empty())
                memcpy(static_cast<uint8_t*>(stream.info.mapped) + quads.size() * sizeof(Vertex), meshes.data(), meshes.size() * sizeof(Vertex));
            api_->FlushBuffer(stream.handle);
            stream.lastSerial = ctx.serial;
        }
    }

    const float texel[2] = {1.0f / kTexSize, 1.0f / kTexSize};
    for (const Op& op : ops) {
        if (op.kind == Op::Perturb) {
            if (!stream.handle) continue;
            BeginPass(ctx.cmd, op.image, false);
            const VkDeviceSize offset = 0;
            d.CmdBindVertexBuffers(ctx.cmd, 0, 1, &stream.info.buffer, &offset);
            if (op.quadCount) {
                d.CmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, perturb_);
                d.CmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &textureSet_, 0, nullptr);
                d.CmdDraw(ctx.cmd, op.quadCount, 1, op.quadFirst, 0);
            }
            if (op.meshCount) {
                d.CmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_);
                d.CmdDraw(ctx.cmd, op.meshCount, 1, static_cast<uint32_t>(quads.size()) + op.meshFirst, 0);
            }
            d.CmdEndRenderPass(ctx.cmd);
        } else {
            BeginPass(ctx.cmd, op.image, false);
            d.CmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, propagate_);
            d.CmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &images_[op.source].source, 0, nullptr);
            const float push[4] = {texel[0], texel[1], op.offset[0], op.offset[1]};
            d.CmdPushConstants(ctx.cmd, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, push);
            d.CmdDraw(ctx.cmd, 3, 1, 0, 0);
            d.CmdEndRenderPass(ctx.cmd);
        }
    }
    lastSerial_ = ctx.serial;

    // Where the maps are, for the water (Cata SetDisplacementConstants), and their images on the proxies.
    for (uint32_t i = 0; i < kMaps; ++i) {
        memcpy(io.m_rawPos[i], rawPos_[i], sizeof rawPos_[i]);
        io.m_size[i][0] = size_[i][0];
        io.m_size[i][1] = size_[i][1];
        io.m_texSize[i] = kTexSize;
        io.m_interpolation[i] = interpolation_[i];
        const Image& image = images_[current_[i]];
        auto* native=static_cast<gxvk::vk::texture::Image*>(image.handle);
        native->layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    publishedSerial_=ctx.serial;
    io.m_ready = io.m_proxy[0] && io.m_proxy[1] ? 1 : 0;
}

void Ripples::Retire(uint64_t serial) {
    const context::Handles* h = context::Get();
    for (Image& image : images_) {
        if (image.framebuffer && h && h->dapi) h->dapi->DestroyFramebuffer(device_.device, image.framebuffer, nullptr);
        if (image.handle) api_->RetireImage(image.handle, serial);
        const VkDescriptorSet keep = image.source;
        image = Image();
        image.source = keep;
    }
}

void Ripples::StopAndDestroy() {
    publishedSerial_=0;
    enabled_ = false;
    g_gxCataRipples.m_active = 0;
    g_gxCataRipples.m_ready = 0;
    if (!api_) return;
    const context::Handles* h = context::Get();
    Retire(lastSerial_);
    for (Stream& s : streams_)
        if (s.handle) api_->RetireBuffer(s.handle, s.lastSerial);
    streams_.clear();
    if (textureHandle_) api_->RetireImage(textureHandle_, lastSerial_);
    textureHandle_ = nullptr;
    textureReady_ = false;
    if (h && h->dapi) {
        const gxvk::vk::loader::Device& d = *h->dapi;
        if (propagate_) d.DestroyPipeline(device_.device, propagate_, nullptr);
        if (perturb_) d.DestroyPipeline(device_.device, perturb_, nullptr);
        if (mesh_) d.DestroyPipeline(device_.device, mesh_, nullptr);
        for (VkShaderModule& m : modules_)
            if (m) d.DestroyShaderModule(device_.device, m, nullptr);
        if (layout_) d.DestroyPipelineLayout(device_.device, layout_, nullptr);
        if (pool_) d.DestroyDescriptorPool(device_.device, pool_, nullptr);
        if (setLayout_) d.DestroyDescriptorSetLayout(device_.device, setLayout_, nullptr);
        if (point_) d.DestroySampler(device_.device, point_, nullptr);
        if (linear_) d.DestroySampler(device_.device, linear_, nullptr);
        if (load_) d.DestroyRenderPass(device_.device, load_, nullptr);
        if (clear_) d.DestroyRenderPass(device_.device, clear_, nullptr);
    }
    propagate_ = perturb_ = mesh_ = VK_NULL_HANDLE;
    for (VkShaderModule& m : modules_) m = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    point_ = linear_ = VK_NULL_HANDLE;
    load_ = clear_ = VK_NULL_HANDLE;
    for (Image& image : images_) image = Image();
    api_ = nullptr;lastSerial_=steps_=0;started_=false;dirty_=true;current_[0]=0;current_[1]=2;
}
void Ripples::Reset(){dirty_=true;started_=false;publishedSerial_=0;g_gxCataRipples.m_ready=0;}
const gxvk::vk::texture::Image* Ripples::SampledImage(uint32_t map,uint64_t serial) const {
    if(!enabled_ || map>=kMaps || !serial || serial!=publishedSerial_ || !g_gxCataRipples.m_ready || api_!=gxvk::vk::hdeffect::Get())return nullptr;
    const auto* image=static_cast<const gxvk::vk::texture::Image*>(images_[current_[map]].handle);
    return image && image->layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL?image:nullptr;
}

}
