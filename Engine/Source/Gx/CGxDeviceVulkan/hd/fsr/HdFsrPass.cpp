#include "HdFsrRigid.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Steps B and C: the world at the FSR render size with the interface native (present WorldTarget), the depth
// normalized from the viewport range and the camera's motion (HdFsrPrep.comp), and the upscale itself -- in the
// explicit hdFSRTest modes only, since the motion is the camera's alone (Runtime::Mode()).
#ifdef WOWDECOMP_FSR
#include "../../wxl/src/vulkan/Manifest.hpp"
// The wxl headers first (see HdFsrRuntime.cpp: vulkan/Vk.hpp sets the Win32 platform before vulkan.h).
#include "vulkan/Context.hpp"
#include "vulkan/Loader.hpp"
#include "vulkan/Pipelines.hpp"
#include "native/Present.hpp"
#include "native/render/Jitter.hpp"
#include "stages/Chain.hpp"
#include "common/CameraCut.hpp"
#include "wxl/Script.hpp"

#include "HdFsrPass.h"
#include "HdFsrRuntime.h"
#include "HdFsrCore.h"
#include "HdFsrSettings.h"
#include "HdFsrMotion.h"
#include "HdFsrSkin.h"
#include "HdFsrWind.h"
#include "HdFsrMasks.h"
#include "HdFsrValidation.h"
#include "WoW/Source/WorldClient/HD/HdCataRenderPerf.h"
#include "shaders/HdFsrPrep.spv.h"
#ifdef WOWDECOMP_DLSS
#include "../dlss/HdDlss.h"
#include "../dlss/HdDlssSettings.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <utility>

namespace HD::Fsr::Pass {
namespace {
namespace present = wxlvk::native::present;
namespace context = wxlvk::vk::context;
namespace jitter = wxlvk::native::render::jitter;
using Device = wxlvk::vk::loader::Device;

struct Push {
    float reproject[16];   // row-major, row vectors: NDC now -> clip before
    float depthRange[4];
    float viewport[4];
    float extent[4];
};
static_assert(sizeof(Push) == 112, "HdFsrPrep.comp's push constants");

constexpr uint32_t kSlots = 2;            // present's frames in flight: one descriptor set each
constexpr uint32_t kLoggedMessages = 200; // SDK messages logged one by one; the rest are counted, never dropped silently

struct State {
    VkDevice device = VK_NULL_HANDLE;
    const Device* d = nullptr;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet sets[kSlots] = {};
    VkSampler sampler = VK_NULL_HANDLE;
    bool prepFailed = false;

    Runtime::Upscale mode = Runtime::Upscale::Off;   // what BeforeWorld began this frame
    bool dlss = false;                               // HD 2026-11-02: DLSS records the upscale this frame, not FSR
    bool on = false;                                 // a world set or a context may exist
    VkExtent2D render{}, native{};
    uint32_t quality = 0;
    VkExtent2D ctxRender{}, ctxNative{};
    bool ctxFailed = false;

    bool havePrev = false;
    double prevViewProj[16]{};
    wxlvk::cut::Camera prevCamera{};
    uint32_t jitterIndex = 0;
    LARGE_INTEGER freq{}, last{};

    bool loggedCamera = false;
    bool objectRequested=false, motionDebug=false, motionInitFailed=false;
    uint64_t m1Composes=0,m1Debugs=0,m1Errors=0;
    double m1Cpu[2]{};uint64_t m1CpuSamples[2]{};LARGE_INTEGER m1Log{};
    uint64_t upscaled = 0, stretched = 0, fellBack = 0;
    uint32_t omFrames = 0, omComposed = 0;   // Q1: object motion share for the statuses

    // Step D: frame generation (a test: camera-only motion). The context lives while AMD's swapchain stands.
    bool gen = false;                      // what BeforeWorld began this frame
    VkExtent2D fgDisplay{};
    VkFormat fgFormat = VK_FORMAT_UNDEFINED;
    bool fgFailed = false;                 // the context could not be made at this size / format
    bool fgThisFrame = false;              // prepared and configured on this frame (BeforePresent reads it)
    bool fgEnabled = false;                // the swapchain was last configured with generation on
    uint64_t fgFrames = 0;                 // rendered frames handed to the generation
    uint64_t rateFrames = 0, ratePresents = 0;
    LARGE_INTEGER rateStart{};
    uint64_t logFrames = 0, logPresents = 0;
    LARGE_INTEGER logStart{};
};
State g;
ObjectMotion::Effect objectMotionEffect;
Masks::Effect masksEffect;bool masksRequested=false,masksActive=false,masksInitFailed=false;uint64_t masksErrors=0;VkExtent2D masksSize{};VkFormat masksFormat{};
uint64_t g_messages = 0;

void OnMessage(uint32_t type, const wchar_t* message) {
    ++g_messages;
    if (g_messages > kLoggedMessages) {
        if (g_messages % 1000 == 0)
            wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: SDK: %llu messages so far (only the first %u are written one by one)",
                                static_cast<unsigned long long>(g_messages), kLoggedMessages);
        return;
    }
    char text[512]{};
    if (message) WideCharToMultiByte(CP_UTF8, 0, message, -1, text, int(sizeof text) - 1, nullptr, nullptr);
    wxl::ScriptMgr::Log(type == FFX_API_MESSAGE_TYPE_ERROR ? WXL_LOG_ERROR : WXL_LOG_WARN, "fsr: SDK %s: %s",
                        type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", text);
}

bool Same(VkExtent2D a, VkExtent2D b) { return a.width == b.width && a.height == b.height; }

// --- the prep pipeline ------------------------------------------------------------------------------------

void DestroyPrep() {
    if (!g.d || !g.device) return;
    if (g.pipe) g.d->DestroyPipeline(g.device, g.pipe, nullptr);
    if (g.layout) g.d->DestroyPipelineLayout(g.device, g.layout, nullptr);
    if (g.pool) g.d->DestroyDescriptorPool(g.device, g.pool, nullptr);
    if (g.dsl) g.d->DestroyDescriptorSetLayout(g.device, g.dsl, nullptr);
    if (g.sampler) g.d->DestroySampler(g.device, g.sampler, nullptr);
    g.pipe = VK_NULL_HANDLE; g.layout = VK_NULL_HANDLE; g.pool = VK_NULL_HANDLE; g.dsl = VK_NULL_HANDLE;
    g.sampler = VK_NULL_HANDLE;
    for (auto& s : g.sets) s = VK_NULL_HANDLE;
}

bool EnsurePrep() {
    if (g.pipe) return true;
    if (g.prepFailed) return false;
    const context::Handles* h = context::Get();
    if (!h || !h->device || !h->dapi) return false;
    g.device = h->device;
    g.d = h->dapi;
    const Device& d = *g.d;
    auto fail = [](const char* what) {
        g.prepFailed = true;
        DestroyPrep();
        wxl::ScriptMgr::Log(WXL_LOG_ERROR, "fsr: the depth / motion pass cannot be made (%s): the upscale stays off", what);
        return false;
    };
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
    sm.codeSize = sizeof(PrepSpv); sm.pCode = PrepSpv;
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
    VkDescriptorSetLayout layouts[kSlots] = {g.dsl, g.dsl};
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = g.pool; da.descriptorSetCount = kSlots; da.pSetLayouts = layouts;
    if (d.AllocateDescriptorSets(g.device, &da, g.sets) != VK_SUCCESS) return fail("descriptor sets");
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (d.CreateSampler(g.device, &si, nullptr, &g.sampler) != VK_SUCCESS) return fail("sampler");
    return true;
}

// --- matrices ---------------------------------------------------------------------------------------------

// a * b, 4x4 row-major.
void Mul(const double* a, const double* b, double* out) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            double v = 0;
            for (int k = 0; k < 4; ++k) v += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = v;
        }
}

// Gauss-Jordan with partial pivoting, in double (world coordinates reach 17000 yards).
bool Invert(const double* m, double* out) {
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 8; ++c) a[r][c] = c < 4 ? m[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
    for (int c = 0; c < 4; ++c) {
        int p = c;
        for (int r = c + 1; r < 4; ++r) if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        if (std::fabs(a[p][c]) < 1e-30) return false;
        if (p != c) for (int k = 0; k < 8; ++k) std::swap(a[p][k], a[c][k]);
        const double inv = 1.0 / a[c][c];
        for (int k = 0; k < 8; ++k) a[c][k] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double f = a[r][c];
            if (f != 0) for (int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
        }
    }
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out[r * 4 + c] = a[r][c + 4];
    return true;
}

// --- images and barriers ----------------------------------------------------------------------------------

FfxApiResource Res(const present::WorldImageDesc& i, uint32_t state) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = i.format;
    ci.extent = VkExtent3D{i.extent.width, i.extent.height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = i.usage;
    return Resource(i.image, ci, state);
}

VkImageMemoryBarrier ImageBarrier(VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    return b;
}

// --- the upscale context ------------------------------------------------------------------------------------

void WaitIdle() {
    const context::Handles* h = context::Get();
    if (h && h->device && h->dapi) h->dapi->DeviceWaitIdle(h->device);
}

void DropContext() {
    Core* sdk = Runtime::Sdk();
    if (!sdk || !sdk->HasUpscale()) return;
    WaitIdle();   // the SDK frees resources the frames in flight may still use
    if (sdk->DestroyUpscale()) wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: upscale context destroy failed (%s)", sdk->Error());
    g.ctxRender = g.ctxNative = VkExtent2D{};
}

bool EnsureContext(Core& sdk) {
    if (sdk.HasUpscale() && Same(g.ctxRender, g.render) && Same(g.ctxNative, g.native)) return true;
    if (g.ctxFailed && Same(g.ctxRender, g.render) && Same(g.ctxNative, g.native)) return false;
    DropContext();
    const context::Handles* h = context::Get();
    if (!h || !h->device || !h->iapi) return false;
    Config c;
    c.device.vkDevice = h->device;
    c.device.vkPhysicalDevice = h->physical;
    c.device.vkDeviceProcAddr = h->iapi->GetDeviceProcAddr;
    c.render = FfxApiDimensions2D{g.render.width, g.render.height};
    c.display = FfxApiDimensions2D{g.native.width, g.native.height};
    c.invertedDepth = false;    // the normalized depth is D3D's, 0 near (checked per frame from the projection)
    c.nonLinearSrgb = true;     // the world colour is the engine's display-referred values in a UNORM image
    c.message = Settings::DebugCheck() ? OnMessage : nullptr;      // hdFSRDebugCheck 1: the SDK's debug checking on, every message goes to the log
    g.ctxRender = g.render;
    g.ctxNative = g.native;
    const ffxReturnCode_t rc = sdk.CreateUpscale(c);
    g.ctxFailed = rc != FFX_API_RETURN_OK;
    if (g.ctxFailed) {
        wxl::ScriptMgr::Log(WXL_LOG_ERROR, "fsr: upscale context %ux%u -> %ux%u not created (%s)", g.render.width,
                            g.render.height, g.native.width, g.native.height, sdk.Error());
        return false;
    }
    wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: upscale context %ux%u -> %ux%u (%s)", g.render.width, g.render.height,
                        g.native.width, g.native.height, sdk.UpscalerName());
    return true;
}

// M1 timings are separate from existing HD stages: no changes to the optimizer's enums or scopes in FSR-OFF.
bool EnsureObjectMotionEffect(){
    if(g.motionInitFailed)return false;
    const auto* api=wxl::ScriptMgr::GetInterface<const WXL_VkRenderApi>(WXL_VK_RENDER_API_NAME,WXL_VK_RENDER_API_VERSION);
    if(objectMotionEffect.Init(api))return true;
    g.motionInitFailed=true;
    if(g.m1Errors++<8)wxl::ScriptMgr::Log(WXL_LOG_WARN,"fsr: M1 pipeline unavailable; camera vectors and normal world retained");return false;
}
void NoteM1Cpu(uint32_t stage,LARGE_INTEGER start){
    if(!start.QuadPart)return;LARGE_INTEGER now{};QueryPerformanceCounter(&now);if(!g.freq.QuadPart)QueryPerformanceFrequency(&g.freq);
    g.m1Cpu[stage]+=double(now.QuadPart-start.QuadPart)*1000./double(g.freq.QuadPart);++g.m1CpuSamples[stage];
}
ObjectMotion::Frame M1Frame(const present::WorldImages& im,VkExtent2D size){
    ObjectMotion::Frame f;f.cmd=im.cmd;f.serial=present::Serial();f.completed=present::Completed();f.slot=present::Slot();f.extent=size;f.timing=HD::CataRenderPerf::Enabled();return f;
}
void LogM1(){
    if(!HD::CataRenderPerf::Enabled()||(!g.objectRequested&&!g.motionDebug))return;
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);if(!g.freq.QuadPart)QueryPerformanceFrequency(&g.freq);
    if(!g.m1Log.QuadPart){g.m1Log=now;return;}if(double(now.QuadPart-g.m1Log.QuadPart)/double(g.freq.QuadPart)<2.)return;
    const auto stats=objectMotionEffect.ReadStats(true);
    wxl::ScriptMgr::Log(WXL_LOG_INFO,"fsr: M1 compose %llu, debug %llu; velocity writers in M2 scope below; camera fallback for unmarked/reset/unsafe pixels; CPU compose %.4f ms/%llu, debug %.4f ms/%llu; GPU compose %.4f ms/%llu, debug %.4f ms/%llu (0 samples = unavailable); M1 measures rendered frames only; generation counters remain separate",
        static_cast<unsigned long long>(g.m1Composes),static_cast<unsigned long long>(g.m1Debugs),
        g.m1CpuSamples[0]?g.m1Cpu[0]/double(g.m1CpuSamples[0]):0.,static_cast<unsigned long long>(g.m1CpuSamples[0]),
        g.m1CpuSamples[1]?g.m1Cpu[1]/double(g.m1CpuSamples[1]):0.,static_cast<unsigned long long>(g.m1CpuSamples[1]),
        stats.gpuSamples[0]?stats.gpuMilliseconds[0]/double(stats.gpuSamples[0]):0.,static_cast<unsigned long long>(stats.gpuSamples[0]),
        stats.gpuSamples[1]?stats.gpuMilliseconds[1]/double(stats.gpuSamples[1]):0.,static_cast<unsigned long long>(stats.gpuSamples[1]));
    if(Settings::RigidMotion()){
        const auto rigid=Rigid::ReadStats(true);
        wxl::ScriptMgr::Log(WXL_LOG_INFO,"fsr: M2 rendered frames %llu, draw prepares %llu, velocity-capable draws %llu, zero-marker draws %llu, unsafe draws %llu, first/reset draws %llu, scope rejects %llu, capacity rejects %llu, singular %llu, conflicts %llu; CPU prepare+scope %.4f ms/frame; fixed cache %llu bytes; generated frames NOT counted",
            static_cast<unsigned long long>(rigid.frames),static_cast<unsigned long long>(rigid.draws),static_cast<unsigned long long>(rigid.writers),static_cast<unsigned long long>(rigid.zero),static_cast<unsigned long long>(rigid.unsafe),static_cast<unsigned long long>(rigid.first),static_cast<unsigned long long>(rigid.scopeMiss),static_cast<unsigned long long>(rigid.full),static_cast<unsigned long long>(rigid.singular),static_cast<unsigned long long>(rigid.conflict),rigid.frames?rigid.cpuMs/double(rigid.frames):0.,static_cast<unsigned long long>(Rigid::MemoryBytes()));
        // HD 2026-10-31 (ghosting): conflicts split; a "shared" one no longer drops every object's motion for the whole frame.
        if(rigid.conflict)wxl::ScriptMgr::Log(WXL_LOG_INFO,"fsr: M2 conflicts %llu: %llu shared (those draws camera-only, frame kept), %llu unsafe (frame camera-only)",
            static_cast<unsigned long long>(rigid.conflict),static_cast<unsigned long long>(rigid.shared),static_cast<unsigned long long>(rigid.conflict-rigid.shared));
        static uint32_t conflictNotes=0;static uint32_t conflictWindows=0;char conflictLine[600];
        while(conflictNotes<Rigid::DiagConflictCount()&&Rigid::DiagConflictText(conflictNotes,conflictLine,sizeof conflictLine)){wxl::ScriptMgr::Log(WXL_LOG_WARN,"fsr: M2 DIAG conflict %s",conflictLine);++conflictNotes;}
        if(conflictNotes&&++conflictWindows%15==0)for(uint32_t i=0;i<conflictNotes;++i)if(Rigid::DiagConflictText(i,conflictLine,sizeof conflictLine))wxl::ScriptMgr::Log(WXL_LOG_INFO,"fsr: M2 DIAG conflict totals %s",conflictLine);
    }
    if(Settings::SkeletalMotion()){
        const auto sk=Skin::Global().ReadStats(true);
        wxl::ScriptMgr::Log(WXL_LOG_INFO,"fsr: M3 CPU snapshots %llu, GPU uploads %llu, reused submeshes %llu, first/reset %llu, cache/ring full %llu/%llu, conflicts %llu, invalid %llu; CPU snapshot+upload %.4f ms/rendered frame; CPU/GPU bytes per frame %llu/%llu; fixed CPU %llu GPU buffers %llu bytes; no generated-frame counter",
            (unsigned long long)sk.captures,(unsigned long long)sk.uploads,(unsigned long long)sk.reused,(unsigned long long)sk.first,(unsigned long long)sk.cacheFull,(unsigned long long)sk.ringFull,(unsigned long long)sk.conflicts,(unsigned long long)sk.invalid,sk.frames?sk.cpuMs/double(sk.frames):0.,(unsigned long long)(sk.frames?sk.cpuBytes/sk.frames:0),(unsigned long long)(sk.frames?sk.gpuBytes/sk.frames:0),(unsigned long long)Skin::Global().CpuMemoryBytes(),(unsigned long long)Skin::Global().GpuBufferBytes());
    }
    if(Settings::WindMotion()){const auto wm=WindMotion::Global().ReadStats(true);wxl::ScriptMgr::Log(WXL_LOG_INFO,"fsr: M4 frames %llu, tree/grass %llu/%llu, captures/uploads/reuse %llu/%llu/%llu, first %llu, wraps %llu, capacity %llu, conflicts %llu, invalid %llu; CPU %.4f ms/rendered frame; CPU/GPU bytes per frame %llu/%llu; fixed CPU %llu GPU buffers %llu; generated frames excluded",(unsigned long long)wm.frames,(unsigned long long)wm.tree,(unsigned long long)wm.grass,(unsigned long long)wm.captures,(unsigned long long)wm.uploads,(unsigned long long)wm.reused,(unsigned long long)wm.first,(unsigned long long)wm.wrapped,(unsigned long long)wm.capacity,(unsigned long long)wm.conflicts,(unsigned long long)wm.invalid,wm.frames?wm.cpuMs/double(wm.frames):0.,(unsigned long long)(wm.frames?wm.cpuBytes/wm.frames:0),(unsigned long long)(wm.frames?wm.gpuBytes/wm.frames:0),(unsigned long long)WindMotion::Global().CpuMemoryBytes(),(unsigned long long)WindMotion::Global().GpuBufferBytes());}
    {uint64_t jittered=0,skipped=0;jitter::Counts(jittered,skipped);static uint64_t lastJ=0,lastS=0; // Q1: jitter coverage (skipped must stay 0 under FSR)
     wxl::ScriptMgr::Log(WXL_LOG_INFO,"fsr: jitter on %llu world draw(s), %llu skipped since the last line",(unsigned long long)(jittered-lastJ),(unsigned long long)(skipped-lastS));lastJ=jittered;lastS=skipped;}
    g.m1Log=now;g.m1Composes=g.m1Debugs=0;g.m1Cpu[0]=g.m1Cpu[1]=0;g.m1CpuSamples[0]=g.m1CpuSamples[1]=0;
}

// --- frame generation ---------------------------------------------------------------------------------------

void DropGenerationContext() {
    Core* sdk = Runtime::Sdk();
    if (sdk && sdk->HasGeneration()) {
        WaitIdle();
        if (sdk->DestroyFrameGeneration())
            wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: frame generation context destroy failed (%s)", sdk->Error());
        else
            wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: frame generation context destroyed (%llu rendered frame(s) handed to it)",
                                static_cast<unsigned long long>(g.fgFrames));
    }
    g.fgDisplay = VkExtent2D{};
    g.fgFormat = VK_FORMAT_UNDEFINED;
    g.fgEnabled = g.fgThisFrame = false;
    g.rateStart.QuadPart = g.logStart.QuadPart = 0;
}

bool EnsureGeneration(Core& sdk, VkExtent2D display, VkFormat format) {
    if (sdk.HasGeneration() && Same(g.fgDisplay, display) && g.fgFormat == format) return true;
    if (g.fgFailed && Same(g.fgDisplay, display) && g.fgFormat == format) return false;
    DropGenerationContext();
    const context::Handles* h = context::Get();
    if (!h || !h->device || !h->iapi) return false;
    Config c;
    c.device.vkDevice = h->device;
    c.device.vkPhysicalDevice = h->physical;
    c.device.vkDeviceProcAddr = h->iapi->GetDeviceProcAddr;
    // The largest render size is the display: one context serves every quality and the generation without upscaling.
    c.render = FfxApiDimensions2D{display.width, display.height};
    c.display = c.render;
    c.invertedDepth = false;
    c.backBufferFormat = ffxApiGetSurfaceFormatVK(format);
    c.message = Settings::DebugCheck() ? OnMessage : nullptr;
    g.fgDisplay = display;
    g.fgFormat = format;
    const ffxReturnCode_t rc = sdk.CreateFrameGeneration(c);
    g.fgFailed = rc != FFX_API_RETURN_OK;
    if (g.fgFailed) {
        wxl::ScriptMgr::Log(WXL_LOG_ERROR, "fsr: frame generation context %ux%u (%s) not created (%s)", display.width,
                            display.height, wxlvk::vk::FormatName(format), sdk.Error());
        return false;
    }
    wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: frame generation context %ux%u (%s, %s)", display.width, display.height,
                        wxlvk::vk::FormatName(format), sdk.GeneratorName());
    return true;
}

// Generation on (this frame's prepare recorded, frameID = its serial) or off (presented alone).
ffxReturnCode_t ConfigureGeneration(Core& sdk, bool on, uint64_t frameId, const FfxApiResource& hudless, VkExtent2D display) {
    ffxConfigureDescFrameGeneration c{};
    c.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    c.swapChain = reinterpret_cast<void*>(sdk.Swapchain());
    c.frameGenerationEnabled = on;
    c.allowAsyncWorkloads = false;   // the context is made without async support
    c.frameGenerationCallback = on ? Core::GenerateCallback : nullptr;
    c.frameGenerationCallbackUserContext = on ? &sdk : nullptr;
    c.presentCallback = nullptr;     // no separate interface surface: the HUD-less image tells the interface apart
    c.HUDLessColor = on ? hudless : FfxApiResource{};
    c.flags = 0;
    c.onlyPresentGenerated = false;
    c.generationRect = FfxApiRect2D{0, 0, int32_t(display.width), int32_t(display.height)};
    c.frameID = frameId;
    return sdk.ConfigureGeneration(c);
}

// Rendered frames against AMD's own present count, over two-second windows (status) and thirty-second ones (log).
void MeasureGeneration(Core& sdk) {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    if (!g.freq.QuadPart) QueryPerformanceFrequency(&g.freq);
    const uint64_t presents = sdk.PresentCount();
    if (!g.rateStart.QuadPart) { g.rateStart = t; g.rateFrames = g.fgFrames; g.ratePresents = presents; }
    if (!g.logStart.QuadPart) { g.logStart = t; g.logFrames = g.fgFrames; g.logPresents = presents; }
    const double rateSeconds = double(t.QuadPart - g.rateStart.QuadPart) / double(g.freq.QuadPart);
    if (rateSeconds >= 2.0) {
        const double rendered = double(g.fgFrames - g.rateFrames) / rateSeconds;
        const double shown = presents >= g.ratePresents ? double(presents - g.ratePresents) / rateSeconds : 0.0;
        Runtime::NoteGenerationRate(rendered, shown);
        g.rateStart = t; g.rateFrames = g.fgFrames; g.ratePresents = presents;
    }
    const double logSeconds = double(t.QuadPart - g.logStart.QuadPart) / double(g.freq.QuadPart);
    if (logSeconds >= 30.0) {
        const uint64_t rendered = g.fgFrames - g.logFrames;
        const uint64_t shown = presents >= g.logPresents ? presents - g.logPresents : 0;
        wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: frame generation over %.1f s: %llu rendered frame(s) (%.1f/s), %llu present(s) "
                            "by AMD's swapchain (%.1f/s), so %llu generated (%.1f/s); generated frames are not simulated",
                            logSeconds, static_cast<unsigned long long>(rendered), double(rendered) / logSeconds,
                            static_cast<unsigned long long>(shown), double(shown) / logSeconds,
                            static_cast<unsigned long long>(shown > rendered ? shown - rendered : 0),
                            double(shown > rendered ? shown - rendered : 0) / logSeconds);
        g.logStart = t; g.logFrames = g.fgFrames; g.logPresents = presents;
    }
}

void TurnOff() {
    Runtime::NoteObjectMotion(-1.0);g.omFrames=g.omComposed=0;
    if (!g.on) return;
    g.on = false;
    jitter::SetFsrSample(false, 0, 0);
    present::WorldTargetDrop();
    DropContext();
#ifdef WOWDECOMP_DLSS
    HD::Dlss::Drop();
#endif
    g.havePrev = false;
    g.ctxFailed = false;
    wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: world target off (%llu frame(s) upscaled, %llu stretched, %llu fell back)",
                        static_cast<unsigned long long>(g.upscaled), static_cast<unsigned long long>(g.stretched),
                        static_cast<unsigned long long>(g.fellBack));
}
}

void BeforeWorld() {
    const auto v1Options=Settings::Read();
    const uint32_t v1Flags=(v1Options.upscale?Validation::RequestUpscale:0u)|(v1Options.generate?Validation::RequestGeneration:0u)|(Settings::ObjectMotion()?Validation::Object:0u)|(Settings::RigidMotion()?Validation::Rigid:0u)|(Settings::SkeletalMotion()?Validation::Skin:0u)|(Settings::WindMotion()?Validation::Wind:0u)|(Settings::ReactiveMasks()?Validation::Reactive:0u);
    Validation::BeginWorld(present::Serial(),Settings::ValidationCapture(),v1Flags,Settings::TestMode(),v1Options.quality,Settings::DebugView());
    masksRequested=masksActive=false;Runtime::NoteMasksReady(false);
    if(!Settings::ReactiveMasks())masksInitFailed=false;
    Rigid::EndFrame();Skin::Global().EndFrame();WindMotion::Global().EndFrame();
    g.mode = Runtime::Upscale::Off;
    g.gen = false;
    g.dlss = false;
    g.objectRequested=Settings::ObjectMotion();g.motionDebug=Settings::DebugView()>=1&&Settings::DebugView()<=3; // HD 2026-10-31: views 1..3 = motion (ours), 4..5 = R1 masks
    if(!g.objectRequested&&!g.motionDebug)g.motionInitFailed=false;
    const Runtime::Upscale want = Runtime::Mode();
    const bool gen = Runtime::Generate();
    Core* sdk = Runtime::Sdk();
    if ((want == Runtime::Upscale::Off && !gen) || !sdk) { TurnOff(); return; }
    const bool fsr = want != Runtime::Upscale::Off && want != Runtime::Upscale::Stretch;
    // A failure is reported on the request it belongs to: the upscaling's, or the generation's alone.
    auto fail = [&](const char* upscaleWhy, const char* generationWhy) {
        if (want != Runtime::Upscale::Off) Runtime::NoteFailure(upscaleWhy);
        if (gen) Runtime::NoteGenerationFailure(generationWhy);
        jitter::SetFsrSample(false, 0, 0);
    };

    VkFormat format{};
    VkExtent2D native{};
    if (present::WorldTargetActive() || !present::FrameOpen() || !present::BackBufferInfo(format, native)) {
        jitter::SetFsrSample(false, 0, 0);   // no world target this frame: no FSR sample either
        return;
    }
    const Options o = Runtime::Requested();
    // Generation without upscaling: the world at the native size, its depth and motion made as with the upscale.
    FfxApiDimensions2D render{native.width, native.height};
    // HD 2026-11-02 (DLSS): with hdDLSS the render size is NVIDIA's for the same quality; when DLSS cannot run
    // (hdDLSSInfo says why) the frame is FSR's, exactly as before.
    bool dlss = false;
#ifdef WOWDECOMP_DLSS
    if (!fsr || !o.dlss) HD::Dlss::Drop();
    else dlss = HD::Dlss::RenderSize(native.width, native.height, o.quality, render.width, render.height);
#endif
    if (want != Runtime::Upscale::Off && !dlss &&
        (sdk->Resolution(native.width, native.height, o.quality, render) || !render.width || !render.height)) {
        fail("TEST impossible: taille de rendu inconnue", "TEST FG impossible: taille de rendu inconnue");
        return;
    }
    const VkExtent2D r{render.width, render.height};
    const uint32_t quality = want != Runtime::Upscale::Off ? (o.quality | (dlss ? 0x100u : 0u)) : ~0u;
    if (!Same(r, g.render) || !Same(native, g.native) || quality != g.quality) {
        g.havePrev = false;
        g.motionInitFailed=false;
        g.jitterIndex = 0;
        wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: render size %ux%u for %ux%u (%s)", r.width, r.height, native.width,
                            native.height, want != Runtime::Upscale::Off ? (o.quality == 0 ? (dlss ? "quality 0, DLSS" : "quality 0") : o.quality == 1
                            ? (dlss ? "quality 1, DLSS" : "quality 1") : o.quality == 2 ? (dlss ? "quality 2, DLSS" : "quality 2")
                            : (dlss ? "quality 3, DLSS" : "quality 3")) : "frame generation alone");
    }
    g.render = r;
    g.native = native;
    g.quality = quality;
    g.on = true;

    if ((fsr || gen) && !EnsurePrep()) {
        fail("TEST impossible: passe profondeur/mouvement non creee", "TEST FG impossible: passe profondeur/mouvement non creee");
        present::WorldTargetDrop();
        return;
    }
    if (fsr && !dlss && !EnsureContext(*sdk)) {
        fail("TEST impossible: contexte FSR non cree (voir le journal)", "TEST FG: mise a l'echelle FSR non creee (voir le journal)");
        present::WorldTargetDrop();
        return;
    }
    if ((!fsr || dlss) && sdk->HasUpscale()) DropContext();   // upscaling turned off with the generation still on, or DLSS in FSR's place
    if (!present::WorldTargetBegin(r,g.objectRequested)) {
        fail("TEST impossible: cible monde non creee (voir le journal)", "TEST FG impossible: cible monde non creee (voir le journal)");
        return;
    }
    masksRequested=Settings::ReactiveMasks()&&(fsr||gen);
    if(masksRequested){
        if(!Same(masksSize,r)||masksFormat!=format){masksInitFailed=false;masksSize=r;masksFormat=format;}
        const auto*api=wxl::ScriptMgr::GetInterface<const WXL_VkRenderApi>(WXL_VK_RENDER_API_NAME,WXL_VK_RENDER_API_VERSION);
        masksActive=!masksInitFailed&&masksEffect.Init(api)&&masksEffect.Resize(r,format);
        if(!masksActive)masksInitFailed=true; // no repeated creation attempts every failed frame; toggle OFF to retry.
        if(masksActive)masksEffect.BeginFrame(present::Serial(),Settings::ReactiveScale(),Settings::CompositionThreshold(),Settings::ReactiveCutoff());
    }
    const bool rigidRequest=g.objectRequested&&Settings::RigidMotion()&&(fsr||gen);
    const bool skinRequest=g.objectRequested&&Settings::SkeletalMotion()&&(fsr||gen);
    const bool windRequest=g.objectRequested&&Settings::WindMotion()&&(fsr||gen);
    static VkPhysicalDevice rigidPhysical=VK_NULL_HANDLE;static bool rigidCaps=false;
    const auto*rigidContext=context::Get();
    if((rigidRequest||skinRequest||windRequest)&&rigidContext&&rigidContext->iapi&&rigidContext->iapi->GetPhysicalDeviceProperties&&rigidContext->physical!=rigidPhysical){
        rigidPhysical=rigidContext->physical;VkPhysicalDeviceProperties limits{};rigidContext->iapi->GetPhysicalDeviceProperties(rigidPhysical,&limits);
        rigidCaps=limits.limits.maxVertexOutputComponents>=72&&limits.limits.maxFragmentInputComponents>=72&&limits.limits.maxPushConstantsSize>=64;
        if(!rigidCaps)wxl::ScriptMgr::Log(WXL_LOG_WARN,"fsr: M2 requires 72 vertex/fragment varying components and 64 push bytes; camera motion retained");
    }
    const bool rigidAssets=!rigidRequest||(rigidCaps&&wxlvk::vk::manifest::EnableVelocity());
    static bool skinFailed=false;
    if(!skinRequest)skinFailed=false;
    bool skinAssets=false;
    if(skinRequest&&rigidCaps&&!skinFailed){
        const auto*api=wxl::ScriptMgr::GetInterface<const WXL_VkRenderApi>(WXL_VK_RENDER_API_NAME,WXL_VK_RENDER_API_VERSION);
        skinAssets=wxlvk::vk::manifest::EnableSkinVelocity()&&Skin::Global().Init(api);
        if(!skinAssets){skinFailed=true;wxl::ScriptMgr::Log(WXL_LOG_WARN,"fsr: M3 unavailable, current colour and camera motion retained; inspect assets/device and restart");}
    }
    static bool windFailed=false;if(!windRequest)windFailed=false;bool windAssets=false;
    if(windRequest&&rigidCaps&&!windFailed){const auto*api=wxl::ScriptMgr::GetInterface<const WXL_VkRenderApi>(WXL_VK_RENDER_API_NAME,WXL_VK_RENDER_API_VERSION);windAssets=wxlvk::vk::manifest::EnableWindVelocity()&&WindMotion::Global().Init(api);if(!windAssets){windFailed=true;wxl::ScriptMgr::Log(WXL_LOG_WARN,"fsr: M4 unavailable; current wind/grass colour and camera motion retained; inspect assets/device and restart");}}
    Rigid::BeginFrame((rigidRequest&&rigidAssets)||skinAssets||windAssets,present::Serial(),r.width,r.height,!g.havePrev,HD::CataRenderPerf::Enabled(),rigidRequest&&rigidAssets,skinAssets||windAssets,windAssets);
    WindMotion::Frame windFrame;windFrame.serial=present::Serial();windFrame.completed=present::Completed();windFrame.epoch=Rigid::ResetEpoch();windFrame.slot=present::Slot();windFrame.width=r.width;windFrame.height=r.height;windFrame.enabled=windAssets;windFrame.reset=!g.havePrev;windFrame.profile=HD::CataRenderPerf::Enabled();WindMotion::Global().BeginFrame(windFrame);
    Skin::Frame skinFrame;skinFrame.serial=present::Serial();skinFrame.completed=present::Completed();skinFrame.epoch=Rigid::ResetEpoch();skinFrame.slot=present::Slot();skinFrame.width=r.width;skinFrame.height=r.height;skinFrame.enabled=skinAssets;skinFrame.reset=!g.havePrev;skinFrame.profile=HD::CataRenderPerf::Enabled();Skin::Global().BeginFrame(skinFrame);
    Validation::Dimensions(r.width,r.height,native.width,native.height);
    g.mode = want;
    g.gen = gen;
    g.dlss = dlss;

    // The jitter: FSR's own sequence and phase count, applied by the existing owner (a viewport displacement in
    // render pixels). What was really applied is measured again at AfterOpaqueWorld (SceneNow) and handed to FSR.
    if (fsr) {
        FfxApiFloatCoords2D j{};
        const bool ok = !sdk->Jitter(g.jitterIndex++, r.width, native.width, j) && jitter::SetFsrSample(true, j.x, j.y);
        if (!ok) jitter::SetFsrSample(false, 0, 0);   // e.g. a (0, 0) sample: the frame is drawn without jitter
    } else {
        jitter::SetFsrSample(false, 0, 0);
    }
}

// The generation's part of the frame, after the world is on the native back buffer and before the interface:
// the HUD-less copy, the prepare (depth, motion, camera) recorded into the frame, the swapchain configured for it.
void RecordGeneration(Core& sdk, const present::WorldImages& im, Frame f, const WXL_VkSceneConstants& scene) {
    const context::Handles* h = context::Get();
    if (!sdk.HasSwapchain() || !h || !h->swapchain) return;   // the swapchain is being rebuilt
    if (!present::PresentCopiesBackBuffer()) {
        Runtime::NoteGenerationFailure("TEST FG impossible: gamma plein ecran ou presentation mise a l'echelle");
        return;
    }
    if (h->format != im.backFormat) {
        Runtime::NoteGenerationFailure("TEST FG impossible: format de la swapchain different du back buffer");
        return;
    }
    if (!EnsureGeneration(sdk, im.native, im.backFormat)) {
        Runtime::NoteGenerationFailure("TEST FG impossible: contexte de generation non cree (voir le journal)");
        return;
    }
    const uint64_t id = present::Serial();
    present::WorldImageDesc hud;
    if (!present::WorldTargetHudless(im, uint32_t(id & 1), hud)) {
        static uint32_t said = 0;
        if (said++ < 8) wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: no HUD-less image this frame: presented without generation");
        return;
    }
    // The camera's frame in the world (row vectors: the view's first three columns are its right, up and forward).
    const float* V = scene.view;
    const float axes[3][3] = {{V[0], V[4], V[8]}, {V[1], V[5], V[9]}, {V[2], V[6], V[10]}};
    float* out[3] = {f.right, f.up, f.forward};
    for (int a = 0; a < 3; ++a) {
        const float l = std::sqrt(axes[a][0] * axes[a][0] + axes[a][1] * axes[a][1] + axes[a][2] * axes[a][2]);
        for (int k = 0; k < 3; ++k) out[a][k] = l > 1e-6f ? axes[a][k] / l : 0.0f;
    }
    for (int k = 0; k < 3; ++k) f.position[k] = scene.cameraPos[k];
    f.id = id;
    const ffxReturnCode_t rc = sdk.RecordPrepare(f);
    wxlvk::vk::pipelines::InvalidateBindings();   // the SDK bound its own pipelines and sets
    if (rc) {
        static uint32_t said = 0;
        if (said++ < 8) wxl::ScriptMgr::Log(WXL_LOG_ERROR, "fsr: generation prepare not recorded (%s)", sdk.Error());
        Runtime::NoteGenerationFailure("TEST FG: preparation refusee (voir le journal)");
        return;
    }
    if (ConfigureGeneration(sdk, true, id, Res(hud, FFX_API_RESOURCE_STATE_COMPUTE_READ), im.native)) {
        static uint32_t said = 0;
        if (said++ < 8) wxl::ScriptMgr::Log(WXL_LOG_ERROR, "fsr: generation not configured (%s)", sdk.Error());
        Runtime::NoteGenerationFailure("TEST FG: configuration refusee (voir le journal)");
        return;
    }
    Validation::Mark(id,Validation::GenerationPrepared|Validation::GenerationConfigured);
    g.fgThisFrame = true;
    g.fgEnabled = true;
    ++g.fgFrames;
    MeasureGeneration(sdk);
}

void MaskBoundary(uint32_t boundary){
    if(!masksRequested||!masksActive)return;
    VkCommandBuffer cmd{};present::WorldImageDesc source;
    bool ok=boundary<4&&present::WorldTargetMaskSource((boundary&1)?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,cmd,source);
    if(ok){Masks::Frame f{cmd,present::Serial(),present::Completed(),present::Slot()};Masks::Image image{source.image,source.view,source.format,source.extent};
        ok=(boundary&1)?masksEffect.Compare(f,boundary/2,image):masksEffect.Capture(f,boundary/2,image);
    }
    if(!ok){masksActive=false;masksEffect.FailFrame();}
    wxlvk::vk::pipelines::InvalidateBindings();
}

void AfterWorld() {
    struct RigidEnd {~RigidEnd(){WindMotion::Global().EndFrame();Skin::Global().EndFrame();Rigid::EndFrame();}} rigidEnd;
    if (!present::WorldTargetActive()) return;
    present::WorldImages im;
    if (!present::WorldTargetEnd(im)) return;
    const Runtime::Upscale mode = g.mode;
    const bool gen = g.gen;
    const bool dlss = g.dlss;
    g.mode = Runtime::Upscale::Off;
    g.gen = false;
    g.dlss = false;
    Core* sdk = Runtime::Sdk();
    const bool fsr = mode != Runtime::Upscale::Off && mode != Runtime::Upscale::Stretch;

    auto stretch = [&](bool fallback) {
        present::WorldTargetBlit(im, false, VK_IMAGE_LAYOUT_UNDEFINED);
        if (fallback) { ++g.fellBack; g.havePrev = false; }
        else ++g.stretched;
    };
    if (!sdk || !g.pipe || (!fsr && !gen) || (fsr && !dlss && !sdk->HasUpscale())) {
        stretch(mode != Runtime::Upscale::Stretch);
        return;
    }

    Masks::Image reactive{},composition{};
    const bool masksReady=masksRequested&&masksActive&&masksEffect.Finish({im.cmd,present::Serial(),present::Completed(),present::Slot()},reactive,composition);
    Runtime::NoteMasksReady(masksReady);
    if(masksReady)Validation::Mark(present::Serial(),Validation::MasksReady);
    if(masksRequested&&!masksReady){
        if(masksErrors++<8)wxl::ScriptMgr::Log(WXL_LOG_WARN,"fsr: R1 masks incomplete (%s), plain stretch and no FG for this frame",masksEffect.Error());
        stretch(true);return;
    }

    // The camera of this frame, as the world was drawn with it (AfterOpaqueWorld).
    WXL_VkSceneConstants scene{};
    VkViewport vp{};
    float applied[2]{};
    if (!im.depth.image || !wxlvk::stages::chain::WorldSceneOf(present::Serial(), scene, vp, applied)) {
        static bool said = false;
        if (!said) wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: a frame without its world depth or camera: plain stretch (logged once)");
        said = true;
        stretch(true);
        return;
    }
    const float* P = scene.projection;
    const double m22 = P[10], m32 = P[14];
    const double nearPlane = std::fabs(m22) > 1e-12 ? -m32 / m22 : 0.0;
    const double farPlane = std::fabs(1.0 - m22) > 1e-12 ? m32 / (1.0 - m22) : INFINITY;
    const double fov = std::fabs(P[5]) > 1e-12 ? 2.0 * std::atan(1.0 / P[5]) : 0.0;
    if (!g.loggedCamera) {
        g.loggedCamera = true;
        wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: camera near %.4f far %.2f vertical fov %.2f deg; viewport %.3f,%.3f %.0fx%.0f "
                            "depth %.3f..%.3f; jitter applied %.4f,%.4f", nearPlane, farPlane, fov * 57.29578,
                            static_cast<double>(vp.x), static_cast<double>(vp.y), static_cast<double>(vp.width),
                            static_cast<double>(vp.height), static_cast<double>(vp.minDepth),
                            static_cast<double>(vp.maxDepth), static_cast<double>(applied[0]), static_cast<double>(applied[1]));
    }
    if (!(nearPlane > 0) || !std::isfinite(farPlane) || farPlane <= nearPlane || !(fov > 0) || vp.maxDepth <= vp.minDepth) {
        static bool said = false;
        if (!said) wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: projection not usable (near %.4f far %.4f fov %.4f): plain stretch "
                                       "(logged once; an inverted or infinite depth would need its own context flags)",
                                       nearPlane, farPlane, fov);
        said = true;
        stretch(true);
        return;
    }

    // Reprojection NDC now -> clip before, in double; the history restarts at a cut (common/CameraCut.hpp).
    double now[16], before[16], inverse[16], reproject[16];
    for (int i = 0; i < 16; ++i) now[i] = scene.viewProj[i];
    const wxlvk::cut::Camera camera = wxlvk::cut::Of(present::Serial(), scene.view, scene.cameraPos);
    const bool reset = !g.havePrev || wxlvk::cut::Between(g.prevCamera, camera);
    if (reset) std::memcpy(before, now, sizeof now);
    else std::memcpy(before, g.prevViewProj, sizeof before);
    if (!Invert(now, inverse)) { stretch(true); return; }
    Mul(inverse, before, reproject);

    const Device& d = *g.d;
    VkCommandBuffer cmd = im.cmd;
    const uint32_t slot = present::Slot() % kSlots;

    // The two inputs, discarded and written whole; the previous frame's reads of them are compute.
    VkImageMemoryBarrier pre[3] = {
        ImageBarrier(im.fsrDepth.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT),
        ImageBarrier(im.motion.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT),
        // The output: written by the SDK, last read by the copy onto the back buffer.
        ImageBarrier(im.output.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
    };
    d.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, fsr ? 3u : 2u, pre);

    VkDescriptorImageInfo images[3]{};
    images[0] = {g.sampler, im.depth.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    images[1] = {VK_NULL_HANDLE, im.fsrDepth.view, VK_IMAGE_LAYOUT_GENERAL};
    images[2] = {VK_NULL_HANDLE, im.motion.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = g.sets[slot];   // this slot's previous frame has completed: present waited its fence
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo = &images[i];
    }
    d.UpdateDescriptorSets(g.device, 3, w, 0, nullptr);

    Push push{};
    for (int i = 0; i < 16; ++i) push.reproject[i] = static_cast<float>(reproject[i]);
    push.depthRange[0] = vp.minDepth;
    push.depthRange[1] = vp.maxDepth;
    push.viewport[0] = vp.x;
    push.viewport[1] = vp.y;
    push.viewport[2] = vp.width;
    push.viewport[3] = vp.height;
    push.extent[0] = float(g.render.width);
    push.extent[1] = float(g.render.height);
    push.extent[2] = 1.0f / float(g.render.width);
    push.extent[3] = 1.0f / float(g.render.height);
    d.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.pipe);
    d.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.layout, 0, 1, &g.sets[slot], 0, nullptr);
    d.CmdPushConstants(cmd, g.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, &push);
    d.CmdDispatch(cmd, (g.render.width + 7) / 8, (g.render.height + 7) / 8, 1);
    // Our own pipeline and set were bound: the draws' tracking starts over even when no SDK dispatch follows.
    wxlvk::vk::pipelines::InvalidateBindings();

    // M1 composes total object UV motion after the unchanged camera pass, before the SDK's read transition.
    bool omThisFrame=false;
    if(g.objectRequested&&im.objectMotion.view&&!reset&&Rigid::FrameSafe()&&Skin::Global().FrameSafe()&&WindMotion::Global().FrameSafe()&&EnsureObjectMotionEffect()){
        LARGE_INTEGER begin{};if(HD::CataRenderPerf::Enabled())QueryPerformanceCounter(&begin);
        if(objectMotionEffect.Compose(M1Frame(im,g.render),im.objectMotion.view,im.motion.view,false)){
            ++g.m1Composes;omThisFrame=true;
            if(Rigid::Active()&&Skin::Global().Active()&&WindMotion::Global().Active())Validation::Mark(present::Serial(),Validation::InputSafe);
        }
        else if(g.m1Errors++<8)wxl::ScriptMgr::Log(WXL_LOG_WARN,"fsr: M1 compose skipped (%s); camera motion retained",objectMotionEffect.Error());
        NoteM1Cpu(0,begin);
    }

    // Q1 (HD 2026-11-01): the statuses report the measured share of frames with object motion.
    if(g.objectRequested){++g.omFrames;if(omThisFrame)++g.omComposed;if(g.omFrames>=120){Runtime::NoteObjectMotion(100.0*g.omComposed/g.omFrames);g.omFrames=g.omComposed=0;}}
    else{Runtime::NoteObjectMotion(-1.0);g.omFrames=g.omComposed=0;}

    VkImageMemoryBarrier post[2] = {
        ImageBarrier(im.fsrDepth.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
        ImageBarrier(im.motion.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
    };
    d.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, post);
    // The motion of the next frame is made against this camera, whatever the upscale below does.
    std::memcpy(g.prevViewProj, now, sizeof now);
    g.prevCamera = camera;
    g.havePrev = true;

    // The frame's time, for the SDK's own history weighting.
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    if (!g.freq.QuadPart) QueryPerformanceFrequency(&g.freq);
    double ms = g.last.QuadPart && g.freq.QuadPart ? double(t.QuadPart - g.last.QuadPart) * 1000.0 / double(g.freq.QuadPart) : 16.6667;
    g.last = t;
    if (!(ms > 0.01) || ms > 1000.0) ms = 16.6667;

    const Options o = Runtime::Requested();
    Frame f;
    f.id = present::Serial();
    f.cmd = cmd;
    f.render = FfxApiDimensions2D{g.render.width, g.render.height};
    f.color = Res(im.colour, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    f.depth = Res(im.fsrDepth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    f.motion = Res(im.motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    f.output = Res(im.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    if(masksReady){
        present::WorldImageDesc r{},c{};r.image=reactive.image;r.view=reactive.view;r.format=reactive.format;r.extent=reactive.extent;r.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        c.image=composition.image;c.view=composition.view;c.format=composition.format;c.extent=composition.extent;c.usage=r.usage;
        f.reactive=Res(r,FFX_API_RESOURCE_STATE_COMPUTE_READ);f.composition=Res(c,FFX_API_RESOURCE_STATE_COMPUTE_READ);
        f.reactiveMasksReady=true;
    }
    // The raster displacement actually applied (right, down), as FSR's jitterOffset; hdFSRTest 3 flips it to check.
    const float sign = mode == Runtime::Upscale::CameraOnlyFlipped ? -1.0f : 1.0f;
    f.jitter = FfxApiFloatCoords2D{sign * applied[0], sign * applied[1]};
    f.motionScale = FfxApiFloatCoords2D{float(g.render.width), float(g.render.height)};
    f.milliseconds = float(ms);
    f.nearPlane = float(nearPlane);
    f.farPlane = float(farPlane);
    f.verticalFov = float(fov);
    f.sharpness = o.sharpness;
    f.sharpen = o.sharpness > 0;
    f.reset = reset;
    if(reset)Validation::Mark(f.id,Validation::HistoryReset);
    f.cameraOnlyTest = true;   // never completeMotion: animated models, bones and wind move with the camera only here

    bool dlssDone = false;
#ifdef WOWDECOMP_DLSS
    // HD 2026-11-02 (DLSS): the same inputs FSR gets, in NVIDIA's states (colour/depth/motion SHADER_READ_ONLY,
    // output GENERAL from the barrier above), into the same output image and the same copy onto the back buffer.
    if (fsr && dlss) {
        dlssDone = true;
        HD::Dlss::Frame df;
        df.cmd = cmd;
        df.color = HD::Dlss::Image{im.colour.image, im.colour.view, im.colour.format, im.colour.extent.width, im.colour.extent.height};
        df.depth = HD::Dlss::Image{im.fsrDepth.image, im.fsrDepth.view, im.fsrDepth.format, im.fsrDepth.extent.width, im.fsrDepth.extent.height};
        df.motion = HD::Dlss::Image{im.motion.image, im.motion.view, im.motion.format, im.motion.extent.width, im.motion.extent.height};
        df.output = HD::Dlss::Image{im.output.image, im.output.view, im.output.format, im.output.extent.width, im.output.extent.height};
        df.quality = o.quality;
        df.jitterX = f.jitter.x;
        df.jitterY = f.jitter.y;
        df.mvScaleX = f.motionScale.x;
        df.mvScaleY = f.motionScale.y;
        df.milliseconds = f.milliseconds;
        df.reset = f.reset;
        df.sharpness = HD::Dlss::Settings::Sharpness();   // HD 2026-11-02: FSR's RCAS after DLSS (DLSS 310 has none), own slider 0..2
        df.slot = present::Slot();
        const int rc = HD::Dlss::Evaluate(df);
        // NGX bound its own pipelines and descriptor sets: the draws' tracking starts over.
        wxlvk::vk::pipelines::InvalidateBindings();
        if (rc > 0) {
            present::WorldTargetBlit(im, true, VK_IMAGE_LAYOUT_GENERAL);
            ++g.upscaled;Validation::Mark(f.id,Validation::UpscaleRecorded);
        } else {
            if (rc < 0) Validation::Mark(f.id,Validation::SdkFailure);
            stretch(true);   // 0: the feature was just created in this command buffer; the history restarts next frame
        }
    }
#endif
    if (dlssDone) {
    } else if (fsr) {
        const ffxReturnCode_t rc = sdk->RecordUpscale(f);
        // The SDK bound its own pipelines and descriptor sets: the draws' tracking starts over.
        wxlvk::vk::pipelines::InvalidateBindings();
        if (rc) {
            Validation::Mark(f.id,Validation::SdkFailure);
            static uint32_t said = 0;
            if (said++ < 8) wxl::ScriptMgr::Log(WXL_LOG_ERROR, "fsr: upscale not recorded (%s): plain stretch this frame", sdk->Error());
            stretch(true);
        } else {
            present::WorldTargetBlit(im, true, VK_IMAGE_LAYOUT_GENERAL);
            ++g.upscaled;Validation::Mark(f.id,Validation::UpscaleRecorded);
        }
    } else {
        // Generation alone: the world was drawn at the native size, copied as it is.
        present::WorldTargetBlit(im, false, VK_IMAGE_LAYOUT_UNDEFINED);
        ++g.stretched;
    }
    // A diagnostic scene must never replace the normal HUD-less scene used by frame generation.
    // Disable FG explicitly to inspect motion; generation itself remains unchanged.
    if(g.motionDebug&&!gen&&EnsureObjectMotionEffect()&&present::WorldTargetDebugOutput(im)){
        LARGE_INTEGER begin{};if(HD::CataRenderPerf::Enabled())QueryPerformanceCounter(&begin);
        // Diagnostic modes (HD, 2026-10-31): 2 = final vectors x16 (small/distant motion visible), 3 = object markers only.
        const uint32_t view=Settings::DebugView();
        const bool shown=view==3&&g.objectRequested&&im.objectMotion.view?objectMotionEffect.DebugObjects(M1Frame(im,im.native),im.objectMotion.view,im.output.view,512.f)
                        :objectMotionEffect.Debug(M1Frame(im,im.native),im.motion.image,im.motion.view,im.output.view,view==2?512.f:32.f);
        if(shown){
            present::WorldTargetBlit(im,true,VK_IMAGE_LAYOUT_GENERAL);++g.m1Debugs;
        }else if(g.m1Errors++<8)wxl::ScriptMgr::Log(WXL_LOG_WARN,"fsr: M1 debug skipped (%s)",objectMotionEffect.Error());
        NoteM1Cpu(1,begin);
        wxlvk::vk::pipelines::InvalidateBindings();
    }
    if(masksReady&&!gen&&Settings::DebugView()>=4&&present::WorldTargetDebugOutput(im)){ // HD 2026-10-31: R1 views moved to 4/5 (2/3 are our motion views); masks API keeps 2/3
        if(masksEffect.Debug({im.cmd,present::Serial(),present::Completed(),present::Slot()},{im.output.image,im.output.view,im.output.format,im.output.extent},Settings::DebugView()-2))present::WorldTargetBlit(im,true,VK_IMAGE_LAYOUT_GENERAL);
        wxlvk::vk::pipelines::InvalidateBindings();
    }
    LogM1();
    if (gen) RecordGeneration(*sdk, im, f, scene);
}

void BeforePresent() {
    Validation::BeforePresent(present::Serial());
    const bool ran = g.fgThisFrame;
    g.fgThisFrame = false;
    Core* sdk = Runtime::Sdk();
    if (!sdk || !sdk->HasGeneration() || !sdk->HasSwapchain()) { g.fgEnabled = false; return; }
    if (ran || !g.fgEnabled) return;
    // This frame was not prepared (no world drawn, a failure, the generation just turned off): presented alone,
    // never interpolated from a stale prepare.
    if (ConfigureGeneration(*sdk, false, present::Serial(), FfxApiResource{}, g.fgDisplay))
        wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: generation could not be switched off for a frame (%s)", sdk->Error());
    g.fgEnabled = false;
}

void DropGeneration() {
    // Called by the swapchain's release (device idle, presents drained by Core::DestroySwapchain itself).
    DropGenerationContext();
    present::WorldTargetHudlessDrop();
}

void Shutdown() {
    Validation::Shutdown();
    if (g.on || g.upscaled || g.stretched || g.fellBack)
        wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: %llu frame(s) upscaled, %llu stretched, %llu fell back; %llu SDK message(s)",
                            static_cast<unsigned long long>(g.upscaled), static_cast<unsigned long long>(g.stretched),
                            static_cast<unsigned long long>(g.fellBack), static_cast<unsigned long long>(g_messages));
    if (g.fgFrames)
        wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: %llu rendered frame(s) handed to the frame generation",
                            static_cast<unsigned long long>(g.fgFrames));
    jitter::SetFsrSample(false, 0, 0);
    if (Core* sdk = Runtime::Sdk()) {   // the caller waited for the device
        sdk->DestroyUpscale();
        sdk->DestroyFrameGeneration();
    }
#ifdef WOWDECOMP_DLSS
    HD::Dlss::Shutdown();   // the caller waited for the device
#endif
    // The caller already waited for the device, as for the existing prep and AMD contexts.
    WindMotion::Global().Shutdown(~uint64_t(0));
    Skin::Global().Shutdown(~uint64_t(0));
    objectMotionEffect.Shutdown(~uint64_t(0));
    masksEffect.Shutdown(~uint64_t(0));masksActive=masksRequested=masksInitFailed=false;masksSize={};masksFormat=VK_FORMAT_UNDEFINED;
    DestroyPrep();
    g = State{};
}
}
#endif // WOWDECOMP_FSR
