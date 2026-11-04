// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: see HdNativeFsr.h. The depth/motion preparation (hd/fsr/shaders/HdFsrPrep.comp), the camera reprojection and the SDK
// frame description follow Mado's Azeroth Remastered HdFsrPass.cpp (GPL-3.0-or-later); the orchestration is Aetheria's own, on
// gxvk::vk::frame::SetUpscaler.
#ifdef AETHERIA_NATIVE_FSR
#include "HdNativeFsr.h"
#include "fsr/HdFsrCore.h"
#include "fsr/shaders/HdFsrPrep.spv.h"
#include "HdFsrCompose.spv.h"
#include "HdNativeDlss.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/CameraCut.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdMotionHistory.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdMotionReplay.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Manifest.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/MotionVariants.hpp"
#include "HdFsrUnits.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Jitter.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture.hpp"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/HD/HdNativeScene.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace HD::NativeFsr {
namespace {
namespace frame = gxvk::vk::frame;
namespace texture = gxvk::vk::texture;
namespace context = gxvk::vk::context;
using HD::Fsr::Core;

struct Push { float reproject[16], depthRange[4], viewport[4], extent[4]; };
static_assert(sizeof(Push) == 112, "HdFsrPrep.comp push constants");
constexpr uint32_t kSlots = frame::kFramesInFlight;

struct State {
    Core core;
    bool coreTried = false, coreOpen = false, prepFailed = false, ctxFailed = false;
    CVar *enabled = nullptr, *quality = nullptr, *sharpness = nullptr, *info = nullptr, *reactiveVar = nullptr, *unitsVar = nullptr, *motionVar = nullptr, *motionInfo = nullptr;
    char lastInfo[160]{};
    // prep pipeline
    VkDevice device = VK_NULL_HANDLE;
    const gxvk::vk::loader::Device* d = nullptr;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE; VkPipeline pipe = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE; VkDescriptorSet sets[kSlots]{}; VkSampler sampler = VK_NULL_HANDLE;
    // object-motion compose pipeline (velocity replay -> motion image); failure only disables object motion
    VkDescriptorSetLayout cdsl = VK_NULL_HANDLE; VkPipelineLayout clayout = VK_NULL_HANDLE; VkPipeline cpipe = VK_NULL_HANDLE;
    VkDescriptorPool cpool = VK_NULL_HANDLE; VkDescriptorSet csets[kSlots]{}; bool composeFailed = false; uint32_t composedFrames = 0; uint32_t composedDraws = 0;
    // images at the render size
    texture::Image depth{}, motion{}; uint64_t imagesLast = 0;
    // F3a reactive mask: opaque-only colour copy (BGRA, render size), the generated mask and an all-zero composition mask (R8)
    texture::Image opaque{}, reactive{}, composition{}; uint64_t opaqueSerial = 0; uint32_t reactiveFrames = 0;
    // F3b-lite: 0xC8-filled host buffer the moving-unit rectangles are copied from (render size in bytes)
    void* unitFill = nullptr; gxvk::vk::hdeffect::BufferInfo unitFillInfo{}; VkExtent2D unitFillExtent{}; uint32_t unitFrames = 0;
    // context
    VkExtent2D ctxRender{}, ctxDisplay{};
    // frame history
    uint32_t jitterIndex = 0;
    bool havePrev = false; double prevViewProj[16]{}; gxvk::vk::cut::Camera prevCamera{};
    uint64_t lastTicks = 0; uint64_t freq = 0;
    bool registered = false; uint64_t lastUsed = 0;
    uint32_t recorded = 0, refused = 0;
    // DLSS in FSR's place this frame (hdDlss 1 and NVIDIA accepted the size); FSR's own context is not used then
    bool dlssActive = false; VkExtent2D dlssRender{}, dlssDisplay{};
};
State g;

void Info(const char* text) {
    if (!g.info || !std::strcmp(g.lastInfo, text)) return;
    std::snprintf(g.lastInfo, sizeof g.lastInfo, "%s", text);
    g.info->Set(g.lastInfo, true, false, false, false);
}
void Register() {
    if (g.enabled) return;
    g.enabled = OptionCVars::Register("hdFsr", "HD: AMD FSR 3.1 upscaling of the 3D world (Azeroth Remastered, camera-only motion): 0 off, 1 on", "0");
    g.quality = OptionCVars::Register("hdFsrQuality", "HD: FSR quality 1 quality / 2 balanced / 3 performance / 4 ultra performance", "1");
    g.sharpness = OptionCVars::Register("hdFsrSharpness", "HD: FSR sharpening 0..1", "0.2");
    g.reactiveVar = OptionCVars::Register("hdFsrReactive", "HD: FSR reactive mask from transparent/translucent draws (water, particles, spell effects): 0 off, 1 on", "1");
    g.unitsVar = OptionCVars::Register("hdFsrUnits", "HD: FSR trusts its history less around moving players and creatures (stands in for object motion vectors): 0 off, 1 on", "1");
    g.motionVar = OptionCVars::Register("hdFsrObjectMotion", "HD: record last frame's bone palettes of skinned draws for FSR object motion (step 1: diagnostics only, nothing is drawn)", "0");
    g.motionInfo = CVar::Register("hdFsrMotionInfo", "HD: object-motion history status (read only)", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    g.info = CVar::Register("hdFsrInfo", "HD: FSR status (read only)", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

void WaitIdle() {
    const context::Handles* h = context::Get();
    if (h && h->device && h->dapi) h->dapi->DeviceWaitIdle(h->device);
}

// --- prep pipeline ---------------------------------------------------------------------------------------
void DestroyCompose() {
    if (!g.d || !g.device) return;
    if (g.cpipe) g.d->DestroyPipeline(g.device, g.cpipe, nullptr);
    if (g.clayout) g.d->DestroyPipelineLayout(g.device, g.clayout, nullptr);
    if (g.cpool) g.d->DestroyDescriptorPool(g.device, g.cpool, nullptr);
    if (g.cdsl) g.d->DestroyDescriptorSetLayout(g.device, g.cdsl, nullptr);
    g.cpipe = VK_NULL_HANDLE; g.clayout = VK_NULL_HANDLE; g.cpool = VK_NULL_HANDLE; g.cdsl = VK_NULL_HANDLE;
    for (auto& s : g.csets) s = VK_NULL_HANDLE;
}
bool EnsureCompose() {
    if (g.cpipe) return true;
    if (g.composeFailed || !g.d || !g.device || !g.sampler) return false;
    const auto& d = *g.d;
    auto fail = [](const char* what) { g.composeFailed = true; DestroyCompose(); CGxDevice::Log("HD FSR: the object-motion compose pass cannot be made (%s)", what); return false; };
    VkDescriptorSetLayoutBinding b[2]{};
    b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[1].binding = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    for (auto& x : b) { x.descriptorCount = 1; x.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; }
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 2; dl.pBindings = b;
    if (d.CreateDescriptorSetLayout(g.device, &dl, nullptr, &g.cdsl) != VK_SUCCESS) return fail("descriptor set layout");
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &g.cdsl;
    if (d.CreatePipelineLayout(g.device, &pl, nullptr, &g.clayout) != VK_SUCCESS) return fail("pipeline layout");
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sm.codeSize = sizeof(HD::NativeFsrShaders::ComposeSpv); sm.pCode = HD::NativeFsrShaders::ComposeSpv;
    VkShaderModule module = VK_NULL_HANDLE;
    if (d.CreateShaderModule(g.device, &sm, nullptr, &module) != VK_SUCCESS) return fail("shader module");
    VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cp.stage.module = module; cp.stage.pName = "main";
    cp.layout = g.clayout;
    const VkResult r = d.CreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &cp, nullptr, &g.cpipe);
    d.DestroyShaderModule(g.device, module, nullptr);
    if (r != VK_SUCCESS) { g.cpipe = VK_NULL_HANDLE; return fail("compute pipeline"); }
    VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSlots}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSlots}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = kSlots; dp.poolSizeCount = 2; dp.pPoolSizes = sizes;
    if (d.CreateDescriptorPool(g.device, &dp, nullptr, &g.cpool) != VK_SUCCESS) return fail("descriptor pool");
    VkDescriptorSetLayout layouts[kSlots]; for (auto& l : layouts) l = g.cdsl;
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = g.cpool; da.descriptorSetCount = kSlots; da.pSetLayouts = layouts;
    if (d.AllocateDescriptorSets(g.device, &da, g.csets) != VK_SUCCESS) return fail("descriptor sets");
    return true;
}
void DestroyPrep() {
    DestroyCompose();
    if (!g.d || !g.device) return;
    if (g.pipe) g.d->DestroyPipeline(g.device, g.pipe, nullptr);
    if (g.layout) g.d->DestroyPipelineLayout(g.device, g.layout, nullptr);
    if (g.pool) g.d->DestroyDescriptorPool(g.device, g.pool, nullptr);
    if (g.dsl) g.d->DestroyDescriptorSetLayout(g.device, g.dsl, nullptr);
    if (g.sampler) g.d->DestroySampler(g.device, g.sampler, nullptr);
    g.pipe = VK_NULL_HANDLE; g.layout = VK_NULL_HANDLE; g.pool = VK_NULL_HANDLE; g.dsl = VK_NULL_HANDLE; g.sampler = VK_NULL_HANDLE;
    for (auto& s : g.sets) s = VK_NULL_HANDLE;
}
bool EnsurePrep() {
    if (g.pipe) return true;
    if (g.prepFailed) return false;
    const context::Handles* h = context::Get();
    if (!h || !h->device || !h->dapi) return false;
    g.device = h->device; g.d = h->dapi;
    const auto& d = *g.d;
    auto fail = [](const char* what) { g.prepFailed = true; DestroyPrep(); CGxDevice::Log("HD FSR: the depth/motion pass cannot be made (%s)", what); return false; };
    VkDescriptorSetLayoutBinding b[3]{};
    b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[1].binding = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[2].binding = 2; b[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    for (auto& x : b) { x.descriptorCount = 1; x.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; }
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 3; dl.pBindings = b;
    if (d.CreateDescriptorSetLayout(g.device, &dl, nullptr, &g.dsl) != VK_SUCCESS) return fail("descriptor set layout");
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &g.dsl; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &range;
    if (d.CreatePipelineLayout(g.device, &pl, nullptr, &g.layout) != VK_SUCCESS) return fail("pipeline layout");
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sm.codeSize = sizeof(HD::Fsr::PrepSpv); sm.pCode = HD::Fsr::PrepSpv;
    VkShaderModule module = VK_NULL_HANDLE;
    if (d.CreateShaderModule(g.device, &sm, nullptr, &module) != VK_SUCCESS) return fail("shader module");
    VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cp.stage.module = module; cp.stage.pName = "main";
    cp.layout = g.layout;
    const VkResult r = d.CreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &cp, nullptr, &g.pipe);
    d.DestroyShaderModule(g.device, module, nullptr);
    if (r != VK_SUCCESS) { g.pipe = VK_NULL_HANDLE; return fail("compute pipeline"); }
    VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSlots}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * kSlots}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = kSlots; dp.poolSizeCount = 2; dp.pPoolSizes = sizes;
    if (d.CreateDescriptorPool(g.device, &dp, nullptr, &g.pool) != VK_SUCCESS) return fail("descriptor pool");
    VkDescriptorSetLayout layouts[kSlots]; for (auto& l : layouts) l = g.dsl;
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = g.pool; da.descriptorSetCount = kSlots; da.pSetLayouts = layouts;
    if (d.AllocateDescriptorSets(g.device, &da, g.sets) != VK_SUCCESS) return fail("descriptor sets");
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_NEAREST; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (d.CreateSampler(g.device, &si, nullptr, &g.sampler) != VK_SUCCESS) return fail("sampler");
    return true;
}

// --- helpers ---------------------------------------------------------------------------------------------
void Mul(const double* a, const double* b, double* out) {
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { double v = 0; for (int k = 0; k < 4; ++k) v += a[r * 4 + k] * b[k * 4 + c]; out[r * 4 + c] = v; }
}
bool Invert(const double* m, double* out) {
    double a[4][8];
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 8; ++c) a[r][c] = c < 4 ? m[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
    for (int c = 0; c < 4; ++c) {
        int p = c; for (int r = c + 1; r < 4; ++r) if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        if (std::fabs(a[p][c]) < 1e-30) return false;
        if (p != c) for (int k = 0; k < 8; ++k) std::swap(a[p][k], a[c][k]);
        const double inv = 1.0 / a[c][c]; for (int k = 0; k < 8; ++k) a[c][k] *= inv;
        for (int r = 0; r < 4; ++r) { if (r == c) continue; const double f = a[r][c]; if (f != 0) for (int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k]; }
    }
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out[r * 4 + c] = a[r][c + 4];
    return true;
}
VkImageMemoryBarrier Barrier(VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = src; b.dstAccessMask = dst; b.oldLayout = from; b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return b;
}
FfxApiResource Res(VkImage image, VkFormat format, VkExtent2D extent, VkImageUsageFlags usage, uint32_t state) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = format; ci.extent = {extent.width, extent.height, 1};
    ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.usage = usage;
    return HD::Fsr::Resource(image, ci, state);
}
bool SameExtent(VkExtent2D a, VkExtent2D b) { return a.width == b.width && a.height == b.height; }

void DropUnitFill() {
    if (g.unitFill) { if (const auto* api = gxvk::vk::hdeffect::Get()) api->RetireBuffer(g.unitFill, g.imagesLast); }
    g.unitFill = nullptr; g.unitFillInfo = {}; g.unitFillExtent = {};
}
void DropMasks() {
    DropUnitFill();
    frame::RetireImage(g.opaque, g.imagesLast);
    frame::RetireImage(g.reactive, g.imagesLast);
    frame::RetireImage(g.composition, g.imagesLast);
    g.opaqueSerial = 0;
}
void DropImages() {
    frame::RetireImage(g.depth, g.imagesLast);
    frame::RetireImage(g.motion, g.imagesLast);
    DropMasks();
}
void DropContext() {
    if (g.core.HasUpscale()) { WaitIdle(); g.core.DestroyUpscale(); }
    g.ctxRender = g.ctxDisplay = VkExtent2D{};
    g.havePrev = false;
}
bool EnsureMasks(VkExtent2D render) {
    if (g.reactive.handle && g.composition.handle && SameExtent(g.reactive.extent, render)) return true;
    frame::RetireImage(g.reactive, g.imagesLast);
    frame::RetireImage(g.composition, g.imagesLast);
    if (!texture::CreateStorage(render.width, render.height, VK_FORMAT_R8_UNORM, g.reactive) ||
        !texture::CreateStorage(render.width, render.height, VK_FORMAT_R8_UNORM, g.composition)) {
        frame::RetireImage(g.reactive, g.imagesLast);
        frame::RetireImage(g.composition, g.imagesLast);
        return false;
    }
    return true;
}
bool EnsureUnitFill(VkExtent2D render) {
    if (g.unitFill && SameExtent(g.unitFillExtent, render)) return true;
    DropUnitFill();
    const auto* api = gxvk::vk::hdeffect::Get();
    if (!api) return false;
    const gxvk::vk::hdeffect::BufferDesc desc{VkDeviceSize(render.width) * render.height, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, gxvk::vk::hdeffect::Host};
    g.unitFill = api->CreateBuffer(&desc, &g.unitFillInfo);
    if (!g.unitFill || !g.unitFillInfo.mapped) { DropUnitFill(); return false; }
    std::memset(g.unitFillInfo.mapped, 0xC8, size_t(desc.size)); // reactive 0.78
    api->FlushBuffer(g.unitFill);
    g.unitFillExtent = render;
    return true;
}
bool EnsureImages(VkExtent2D render) {
    if (g.depth.handle && SameExtent(g.depth.extent, render) && g.motion.handle) return true;
    DropImages();
    if (!texture::CreateStorage(render.width, render.height, VK_FORMAT_R32_SFLOAT, g.depth) ||
        !texture::CreateStorage(render.width, render.height, VK_FORMAT_R16G16_SFLOAT, g.motion)) { DropImages(); return false; }
    return true;
}

// --- the upscaler ----------------------------------------------------------------------------------------
bool Record(const frame::UpscaleInputs& in, void*) {
    if (g.dlssActive) {
        if (!SameExtent(in.renderExtent, g.dlssRender) || !SameExtent(in.displayExtent, g.dlssDisplay)) { ++g.refused; return false; }
    } else if (!g.core.HasUpscale() || !SameExtent(in.renderExtent, g.ctxRender) || !SameExtent(in.displayExtent, g.ctxDisplay)) { ++g.refused; return false; }
    if (!EnsurePrep() || !EnsureImages(in.renderExtent)) { ++g.refused; return false; }
    HD::NativeScene::Snapshot snap;
    if (!HD::NativeScene::Get(in.serial, snap)) { ++g.refused; return false; }
    const float* P = snap.constants.projection;
    const double m22 = P[10], m32 = P[14];
    const double nearPlane = std::fabs(m22) > 1e-12 ? -m32 / m22 : 0.0;
    const double farPlane = std::fabs(1.0 - m22) > 1e-12 ? m32 / (1.0 - m22) : INFINITY;
    const double fov = std::fabs(P[5]) > 1e-12 ? 2.0 * std::atan(1.0 / P[5]) : 0.0;
    const VkViewport& vp = snap.viewport;
    if (!(nearPlane > 0) || !std::isfinite(farPlane) || farPlane <= nearPlane || !(fov > 0) || vp.maxDepth <= vp.minDepth) { ++g.refused; Info("projection not usable: plain stretch"); return false; }

    // viewProj = view * projection (row vectors); the history restarts at a camera cut.
    double view[16], proj[16], now[16], before[16], inverse[16], reproject[16];
    for (int i = 0; i < 16; ++i) { view[i] = snap.constants.view[i]; proj[i] = snap.constants.projection[i]; }
    Mul(view, proj, now);
    double invView[16]; float eye[4] = {0, 0, 0, 1};
    if (Invert(view, invView)) { eye[0] = float(invView[12]); eye[1] = float(invView[13]); eye[2] = float(invView[14]); }
    const auto camera = gxvk::vk::cut::Of(in.serial, snap.constants.view, eye);
    const bool reset = !g.havePrev || gxvk::vk::cut::Between(g.prevCamera, camera);
    std::memcpy(before, reset ? now : g.prevViewProj, sizeof before);
    if (!Invert(now, inverse)) { ++g.refused; return false; }
    Mul(inverse, before, reproject);

    const auto& d = *g.d;
    VkCommandBuffer cmd = in.cmd;
    const uint32_t slot = in.slot % kSlots;
    VkImageMemoryBarrier pre[2] = {
        Barrier(g.depth.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT),
        Barrier(g.motion.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT)};
    d.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, pre);
    VkDescriptorImageInfo images[3]{};
    images[0] = {g.sampler, in.depthView, in.depthLayout};
    images[1] = {VK_NULL_HANDLE, g.depth.view, VK_IMAGE_LAYOUT_GENERAL};
    images[2] = {VK_NULL_HANDLE, g.motion.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = g.sets[slot]; w[i].dstBinding = i; w[i].descriptorCount = 1;
        w[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[i].pImageInfo = &images[i];
    }
    d.UpdateDescriptorSets(g.device, 3, w, 0, nullptr);
    Push push{};
    for (int i = 0; i < 16; ++i) push.reproject[i] = float(reproject[i]);
    push.depthRange[0] = vp.minDepth; push.depthRange[1] = vp.maxDepth;
    // The viewport the world was drawn with: D3D9 shift (in the snapshot) plus this frame's jitter (the draws' viewport origin moved by it).
    push.viewport[0] = vp.x + in.jitter[0]; push.viewport[1] = vp.y + in.jitter[1]; push.viewport[2] = vp.width; push.viewport[3] = vp.height;
    push.extent[0] = float(in.renderExtent.width); push.extent[1] = float(in.renderExtent.height);
    push.extent[2] = 1.0f / float(in.renderExtent.width); push.extent[3] = 1.0f / float(in.renderExtent.height);
    d.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.pipe);
    d.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.layout, 0, 1, &g.sets[slot], 0, nullptr);
    d.CmdPushConstants(cmd, g.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, &push);
    d.CmdDispatch(cmd, (in.renderExtent.width + 7) / 8, (in.renderExtent.height + 7) / 8, 1);
    bool objectMotion = false;
    if (OptionCVars::Bool(g.motionVar, false)) {   // full object motion: the replayed velocity of skinned units overwrites the camera-only motion
        gxvk::vk::hdmotion::Velocity vel;
        if (gxvk::vk::hdmotion::Result(in.serial, vel) && SameExtent(vel.extent, in.renderExtent) && EnsureCompose()) {
            VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            d.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
            VkDescriptorImageInfo ci[2]{};
            ci[0] = {g.sampler, vel.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            ci[1] = {VK_NULL_HANDLE, g.motion.view, VK_IMAGE_LAYOUT_GENERAL};
            VkWriteDescriptorSet cw[2]{};
            for (uint32_t i = 0; i < 2; ++i) {
                cw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; cw[i].dstSet = g.csets[slot]; cw[i].dstBinding = i; cw[i].descriptorCount = 1;
                cw[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; cw[i].pImageInfo = &ci[i];
            }
            d.UpdateDescriptorSets(g.device, 2, cw, 0, nullptr);
            d.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.cpipe);
            d.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.clayout, 0, 1, &g.csets[slot], 0, nullptr);
            d.CmdDispatch(cmd, (in.renderExtent.width + 7) / 8, (in.renderExtent.height + 7) / 8, 1);
            objectMotion = true; ++g.composedFrames; g.composedDraws = vel.draws;
        }
    }
    VkImageMemoryBarrier post[2] = {
        Barrier(g.depth.handle, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
        Barrier(g.motion.handle, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)};
    d.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, post);
    g.depth.layout = g.motion.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    g.imagesLast = in.serial;
    std::memcpy(g.prevViewProj, now, sizeof now);
    g.prevCamera = camera; g.havePrev = true;

    uint64_t t = 0; { LARGE_INTEGER q{}; QueryPerformanceCounter(&q); t = uint64_t(q.QuadPart); if (!g.freq) { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); g.freq = uint64_t(f.QuadPart); } }
    double ms = g.lastTicks && g.freq ? double(t - g.lastTicks) * 1000.0 / double(g.freq) : 16.6667;
    g.lastTicks = t; if (!(ms > 0.01) || ms > 1000.0) ms = 16.6667;

#ifdef AETHERIA_NATIVE_DLSS
    if (g.dlssActive) {   // NVIDIA DLSS on the same depth / motion / jitter / output FSR would have used
        HD::NativeDlss::Frame df;
        df.cmd = cmd; df.serial = in.serial; df.slot = slot;
        df.color = {in.colorImage, in.colorView, in.colorFormat, in.renderExtent.width, in.renderExtent.height};
        df.depth = {g.depth.handle, g.depth.view, VK_FORMAT_R32_SFLOAT, in.renderExtent.width, in.renderExtent.height};
        df.motion = {g.motion.handle, g.motion.view, VK_FORMAT_R16G16_SFLOAT, in.renderExtent.width, in.renderExtent.height};
        df.output = {in.outputImage, in.outputView, in.outputFormat, in.displayExtent.width, in.displayExtent.height};
        df.quality = uint32_t(OptionCVars::Int(g.quality, 1, 4, 1));
        df.jitterX = in.jitter[0]; df.jitterY = in.jitter[1];
        df.mvScaleX = float(in.renderExtent.width); df.mvScaleY = float(in.renderExtent.height);
        df.milliseconds = float(ms); df.reset = reset; df.sharpness = HD::NativeDlss::Sharpness();
        const int dr = HD::NativeDlss::Evaluate(df);
        g.lastUsed = in.serial;
        if (dr < 0) { ++g.refused; return false; }
        if (dr == 0) return false;   // the feature was created in this command buffer: stretch this one frame
        ++g.recorded;
        Info(objectMotion ? "DLSS upscaling, camera + object motion (status in hdDlssInfo)" : "DLSS upscaling, camera-only motion (status in hdDlssInfo)");
        return true;
    }
#endif
    HD::Fsr::Frame f;
    f.id = in.serial; f.cmd = cmd;
    f.render = FfxApiDimensions2D{in.renderExtent.width, in.renderExtent.height};
    constexpr VkImageUsageFlags sampledColor = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    constexpr VkImageUsageFlags storage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    f.color = Res(in.colorImage, in.colorFormat, in.renderExtent, sampledColor, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    f.depth = Res(g.depth.handle, VK_FORMAT_R32_SFLOAT, in.renderExtent, storage, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    f.motion = Res(g.motion.handle, VK_FORMAT_R16G16_SFLOAT, in.renderExtent, storage, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    f.output = Res(in.outputImage, in.outputFormat, in.displayExtent, storage, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    f.jitter = FfxApiFloatCoords2D{in.jitter[0], in.jitter[1]};
    f.motionScale = FfxApiFloatCoords2D{float(in.renderExtent.width), float(in.renderExtent.height)};
    f.milliseconds = float(ms); f.nearPlane = float(nearPlane); f.farPlane = float(farPlane); f.verticalFov = float(fov);
    const float sharp = OptionCVars::Float(g.sharpness, 0.0f, 1.0f, 0.2f);
    f.sharpness = sharp; f.sharpen = sharp > 0; f.reset = reset;
    f.cameraOnlyTest = true; // never complete motion: self-moving models ghost until the object-motion tier
    // Reactive mask: (F3a) translucent draws from the opaque-only copy taken at AfterOpaque, plus (F3b-lite) a rectangle around every unit that
    // moved since the last frame. Always with an all-zero composition mask (the SDK wants both).
    {
        const bool genWanted = OptionCVars::Bool(g.reactiveVar, true) && g.opaque.handle && g.opaqueSerial == in.serial &&
            SameExtent(g.opaque.extent, in.renderExtent) && g.opaque.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkRect2D rects[HD::FsrUnits::kMaxRects];
        const size_t rectCount = OptionCVars::Bool(g.unitsVar, true) ? HD::FsrUnits::Rects(snap.constants.view, snap.constants.projection, vp, in.renderExtent, rects, HD::FsrUnits::kMaxRects) : 0;
        const bool fill = rectCount && EnsureUnitFill(in.renderExtent);
        if ((genWanted || fill) && EnsureMasks(in.renderExtent)) {
            const VkImageLayout reactiveFirst = genWanted ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            VkImageMemoryBarrier toStart[2] = {
                Barrier(g.reactive.handle, VK_IMAGE_LAYOUT_UNDEFINED, reactiveFirst, 0, genWanted ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT),
                Barrier(g.composition.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT)};
            d.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, toStart);
            const VkClearColorValue zero{};
            const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            d.CmdClearColorImage(cmd, g.composition.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
            VkImageMemoryBarrier compDone = Barrier(g.composition.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            d.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &compDone);
            bool generated = false;
            if (genWanted) {
                const FfxApiResource opaqueRes = Res(g.opaque.handle, g.opaque.format, in.renderExtent, sampledColor, FFX_API_RESOURCE_STATE_COMPUTE_READ);
                const FfxApiResource reactiveOut = Res(g.reactive.handle, VK_FORMAT_R8_UNORM, in.renderExtent, storage, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
                generated = g.core.RecordReactive(cmd, opaqueRes, f.color, reactiveOut, f.render) == FFX_API_RETURN_OK;
            } else {
                d.CmdClearColorImage(cmd, g.reactive.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
            }
            VkImageLayout now = reactiveFirst;
            if (fill) {
                if (now != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                    VkImageMemoryBarrier toCopy = Barrier(g.reactive.handle, now, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                    d.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toCopy);
                } else {
                    VkImageMemoryBarrier clearDone = Barrier(g.reactive.handle, now, now, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                    d.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &clearDone);
                }
                VkBufferImageCopy regions[HD::FsrUnits::kMaxRects]{};
                for (size_t i = 0; i < rectCount; ++i) {
                    regions[i].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    regions[i].imageOffset = {rects[i].offset.x, rects[i].offset.y, 0};
                    regions[i].imageExtent = {rects[i].extent.width, rects[i].extent.height, 1};
                }
                d.CmdCopyBufferToImage(cmd, g.unitFillInfo.buffer, g.reactive.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, uint32_t(rectCount), regions);
                now = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                g.unitFrames++;
            }
            const VkAccessFlags writes = now == VK_IMAGE_LAYOUT_GENERAL ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
            VkImageMemoryBarrier reactiveDone = Barrier(g.reactive.handle, now, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, writes, VK_ACCESS_SHADER_READ_BIT);
            d.CmdPipelineBarrier(cmd, now == VK_IMAGE_LAYOUT_GENERAL ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &reactiveDone);
            g.reactive.layout = g.composition.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (genWanted ? generated : true) {
                f.reactive = Res(g.reactive.handle, VK_FORMAT_R8_UNORM, in.renderExtent, storage, FFX_API_RESOURCE_STATE_COMPUTE_READ);
                f.composition = Res(g.composition.handle, VK_FORMAT_R8_UNORM, in.renderExtent, storage, FFX_API_RESOURCE_STATE_COMPUTE_READ);
                f.reactiveMasksReady = true; ++g.reactiveFrames;
            }
        }
    }
    const ffxReturnCode_t rc = g.core.RecordUpscale(f);
    g.lastUsed = in.serial;
    if (rc != FFX_API_RETURN_OK) { ++g.refused; Info(g.core.Error()); return false; }
    ++g.recorded;
    Info(objectMotion ? (f.reactiveMasksReady ? "FSR 3.1 upscaling, camera + object motion + reactive mask + moving units" : "FSR 3.1 upscaling, camera + object motion")
                      : (f.reactiveMasksReady ? "FSR 3.1 upscaling, camera-only motion + reactive mask + moving units" : "FSR 3.1 upscaling, camera-only motion"));
    return true;
}

void Unregister() {
    if (!g.registered) return;
    frame::SetUpscaler(nullptr, nullptr);
    gxvk::vk::jitter::SetSample(false, 0, 0);
    g.registered = false;
}
bool RuntimeFolder(wchar_t* out, size_t cap) {
    wchar_t exe[1024]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
    if (!n || n >= std::size(exe)) return false;
    wchar_t* slash = std::wcsrchr(exe, L'\\'); if (!slash) return false; *slash = 0;
    return swprintf_s(out, cap, L"%s\\FSR", exe) > 0;
}
}

void AfterOpaque() {
    if (g.registered && Visuals::Current() == Visuals::AzerothRemastered && OptionCVars::Bool(g.motionVar, false) && frame::WorldScaled() && !frame::Target() && !frame::QueryActive())
        gxvk::vk::hdmotion::ReplayVelocity();
    if (!g.registered || g.dlssActive || Visuals::Current() != Visuals::AzerothRemastered || !OptionCVars::Bool(g.reactiveVar, true) || !frame::WorldScaled()) return;
    const texture::Image* scene = frame::Scene();
    if (!scene || frame::Target() || frame::QueryActive()) return;
    if (g.opaque.handle && !SameExtent(g.opaque.extent, scene->extent)) frame::RetireImage(g.opaque, g.imagesLast);
    if (!g.opaque.handle && !texture::CreateMipped(scene->extent.width, scene->extent.height, 1, scene->format, g.opaque)) return;
    const VkRect2D rect{VkOffset2D{0, 0}, scene->extent};
    if (frame::CopyFromScene(g.opaque, 0, 0, rect, rect, false)) g.opaqueSerial = frame::Serial();
}

float BeginWorld() {
    if (Visuals::Current() != Visuals::AzerothRemastered) { Unregister(); return 0.0f; }
    Register();
    {   // FSR object motion, step 1 (works with or without hdFsr): previous-frame bone palettes of the skinned draws
        const bool om = OptionCVars::Bool(g.motionVar, false);
        gxvk::vk::hdmotion::SetEnabled(om);
        if (om) {
            namespace mv = gxvk::vk::motionvariants;
            static bool variantsTried = false;
            if (!variantsTried) {
                variantsTried = true;
                const bool ok = mv::Load(gxvk::vk::manifest::Folder());
                const mv::Stats vs = mv::GetStats();
                char line[256];
                std::snprintf(line, sizeof line, "HD FSR object motion: velocity variants %s (own skinned %u; Mado rigid %u, bones %u, skin %u, wind %u, wind+bones %u, grass %u)",
                              ok ? "loaded" : "NOT installed", vs.perKind[8], vs.perKind[0], vs.perKind[1], vs.perKind[2], vs.perKind[3], vs.perKind[4], vs.perKind[5]);
                CGxDevice::Log("%s", static_cast<const char*>(line));
            }
            gxvk::vk::hdmotion::BeginFrame(frame::Serial());
            static uint32_t tick = 0;
            if (++tick % 30 == 0 && g.motionInfo) {
                const auto s = gxvk::vk::hdmotion::GetStats();
                char text[200]; std::snprintf(text, sizeof text, "skinned draws last frame: %u seen, %u kept (%u dropped), %u had a previous-frame twin; %u frames composed (%u draws last)", s.lastObserved, s.lastStored, s.lastDropped, s.lastFrame, g.composedFrames, g.composedDraws);
                g.motionInfo->Set(text, true, false, false, false);
            }
        } else if (g.motionInfo && std::strcmp(g.motionInfo->m_stringValue.m_str ? g.motionInfo->m_stringValue.m_str : "", "off")) {
            g.motionInfo->Set("off", true, false, false, false);
        }
    }
    HD::NativeDlss::Register();
    const bool dlssWanted = HD::NativeDlss::Requested();
    if (!dlssWanted && g.dlssActive) { g.dlssActive = false; Unregister(); HD::NativeDlss::Drop(); }
    const bool on = OptionCVars::Bool(g.enabled, false) || dlssWanted;
    namespace fr = frame;
    const texture::Image* scene = fr::Scene();
    const context::Handles* h = context::Get();
    if (!on || !scene || fr::WorldScaled() || !h) { if (g.registered) { Unregister(); } if (!on) Info("off"); return 0.0f; }
#ifdef AETHERIA_NATIVE_DLSS
    if (dlssWanted) {   // NVIDIA DLSS first; when it cannot run (hdDlssInfo says why) FSR takes over if hdFsr is on
        const VkExtent2D displaySize = scene->extent;
        uint32_t rw = 0, rh = 0;
        const int dq = OptionCVars::Int(g.quality, 1, 4, 1);
        if (HD::NativeDlss::RenderSize(displaySize.width, displaySize.height, uint32_t(dq), rw, rh) && rw && rh && rw < displaySize.width) {
            if (g.core.HasUpscale()) DropContext();
            const float dscale = std::clamp(float(rw) / float(displaySize.width), 0.25f, 0.999f);
            const VkExtent2D drender = fr::WorldScaleExtent(dscale);
            g.dlssActive = true; g.dlssRender = drender; g.dlssDisplay = displaySize;
            // Halton(2,3) jitter, phase count as NVIDIA / AMD advise: 8 * (display / render)^2
            const float ratio = float(displaySize.width) / float(std::max(1u, drender.width));
            const uint32_t phases = std::max(8u, uint32_t(std::ceil(8.0f * ratio * ratio)));
            const uint32_t index = g.jitterIndex++ % phases + 1;
            auto halton = [](uint32_t i, uint32_t b) { float f = 1.0f, r = 0.0f; while (i) { f /= float(b); r += f * float(i % b); i /= b; } return r; };
            if (!gxvk::vk::jitter::SetSample(true, halton(index, 2) - 0.5f, halton(index, 3) - 0.5f)) gxvk::vk::jitter::SetSample(false, 0, 0);
            if (!g.registered) { fr::SetUpscaler(Record, nullptr); g.registered = true; }
            HD::FsrUnits::Collect();
            return dscale;
        }
        g.dlssActive = false;
        if (!OptionCVars::Bool(g.enabled, false)) { Unregister(); return 0.0f; }
    } else g.dlssActive = false;
#endif
    if (!g.coreTried) {
        g.coreTried = true;
        wchar_t folder[1024]{};
        g.coreOpen = RuntimeFolder(folder, std::size(folder)) && g.core.Open(folder);
        if (!g.coreOpen) CGxDevice::Log("HD FSR: runtime not loaded from <exe>\\FSR (%s)", g.core.Error());
    }
    if (!g.coreOpen) { Info("FSR runtime missing: put the FSR folder (amd_fidelityfx_*_vk.dll) beside the executable"); return 0.0f; }
    const VkExtent2D display = scene->extent;
    const int q = OptionCVars::Int(g.quality, 1, 4, 1);
    FfxApiDimensions2D wanted{};
    if (g.core.Resolution(display.width, display.height, uint32_t(q), wanted) != FFX_API_RETURN_OK || !wanted.width || !wanted.height || wanted.width >= display.width) {
        Unregister(); Info("FSR: no render size for this window"); return 0.0f;
    }
    const float scale = std::clamp(float(wanted.width) / float(display.width), 0.25f, 0.999f);
    const VkExtent2D render = fr::WorldScaleExtent(scale);
    if (!SameExtent(render, g.ctxRender) || !SameExtent(display, g.ctxDisplay) || !g.core.HasUpscale()) {
        DropContext();
        HD::Fsr::Config c;
        c.device.vkDevice = h->device; c.device.vkPhysicalDevice = h->physical; c.device.vkDeviceProcAddr = h->iapi->GetDeviceProcAddr;
        c.render = FfxApiDimensions2D{render.width, render.height}; c.display = FfxApiDimensions2D{display.width, display.height};
        c.invertedDepth = false; c.nonLinearSrgb = true; // normalised D3D depth (0 near); display-referred UNORM colour
        const ffxReturnCode_t rc = g.core.CreateUpscale(c);
        g.ctxFailed = rc != FFX_API_RETURN_OK;
        if (g.ctxFailed) { Unregister(); Info(g.core.Error()); return 0.0f; }
        g.ctxRender = render; g.ctxDisplay = display;
        char text[160]; std::snprintf(text, sizeof text, "FSR context %ux%u -> %ux%u (%s)", render.width, render.height, display.width, display.height, g.core.UpscalerName());
        CGxDevice::Log("HD %s", static_cast<const char*>(text)); // const char*: a char array would bind to the va_list overload
    }
    // This frame's jitter sample, applied by the draws' viewport origin (gxvk::vk::jitter); it must be set before the world pass begins.
    FfxApiFloatCoords2D j{};
    if (g.core.Jitter(g.jitterIndex++, render.width, display.width, j) != FFX_API_RETURN_OK || !gxvk::vk::jitter::SetSample(true, j.x, j.y)) gxvk::vk::jitter::SetSample(false, 0, 0);
    if (!g.registered) { fr::SetUpscaler(Record, nullptr); g.registered = true; }
    HD::FsrUnits::Collect(); // who moved since the last frame (for the reactive rectangles)
    return scale;
}

bool Shutdown() {
    gxvk::vk::hdmotion::Shutdown();
    gxvk::vk::hdmotion::ShutdownReplay();
    g.dlssActive = false;
    Unregister();
    if (!g.coreTried && !g.depth.handle) { HD::NativeDlss::Shutdown(); return true; }
    WaitIdle();
    if (g.core.HasUpscale()) g.core.DestroyUpscale();
    DropImages();
    DestroyPrep();
    HD::NativeDlss::Shutdown();
    if (g.coreOpen) g.core.CloseAfterIdle();
    g.coreOpen = g.coreTried = false; g.prepFailed = false; g.composeFailed = false; g.ctxRender = g.ctxDisplay = VkExtent2D{}; g.havePrev = false;
    return true;
}
}
#endif
