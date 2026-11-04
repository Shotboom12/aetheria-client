// SPDX-License-Identifier: GPL-3.0-or-later
// HD 2026-11-02 (DLSS): NVIDIA DLSS Super Resolution through NGX (SDK 310.9.16, ThirdParty/DLSS), in place of
// the FSR upscaler, on the inputs the FSR pass already makes. See HdDlss.h. NGX is started only when hdDLSS is
// first asked for; its DLL (nvngx_dlss.dll) is looked for in the executable's DLSS folder, then the executable's.
#ifdef WOWDECOMP_DLSS
// The wxl headers first: vulkan/Vk.hpp sets the Win32 platform / VK_NO_PROTOTYPES before vulkan.h (as HdFsrRuntime.cpp).
#include "vulkan/Context.hpp"
#include "vulkan/Loader.hpp"
#include "wxl/Script.hpp"

#include "HdDlss.h"
#include "HdDlssVk.h"
#include "HdDlssSettings.h"
#include "shaders/HdDlssRcas.spv.h"

#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace HD::Dlss {
namespace {
namespace context = wxlvk::vk::context;

// NVIDIA: an application without an NVIDIA id uses a project id of its own with NVSDK_NGX_ENGINE_TYPE_CUSTOM.
constexpr const char* kProjectId = "6f1d2c8a-5b47-4e39-9a0e-3c7b21d4f855";
constexpr const char* kEngineVersion = "3.3.5a-waystone";

struct State {
    bool started = false, startFailed = false;
    NVSDK_NGX_Parameter* params = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    uint32_t fRenderW = 0, fRenderH = 0, fDisplayW = 0, fDisplayH = 0, fQuality = ~0u, fPreset = ~0u;
    bool featureFailed = false;
    // RenderSize's cache: NVIDIA's table asked again only when the display or the quality change.
    uint32_t qDisplayW = 0, qDisplayH = 0, qQuality = ~0u, qRenderW = 0, qRenderH = 0;
    bool qOk = false;
    uint64_t evaluated = 0, failed = 0;
    char status[200]{};
};
State s;

wchar_t g_folder[3100]{};     // <exe>\DLSS: NGX's search path and its data / log folder
wchar_t* g_paths[1] = {g_folder};

// NGX may log from any thread: its lines are queued here and written by the render thread (Flush).
std::mutex g_logMutex;
std::vector<std::string> g_logLines;
uint64_t g_logCount = 0;
constexpr uint64_t kLoggedLines = 400;

void NVSDK_CONV OnNgxLog(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (++g_logCount > kLoggedLines || !message) return;
    std::string line(message);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    g_logLines.push_back(std::move(line));
}

void Flush() {
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        lines.swap(g_logLines);
    }
    for (const std::string& l : lines) wxl::ScriptMgr::Log(WXL_LOG_INFO, "dlss: NGX: %s", l.c_str());
}

const char* ResultText(NVSDK_NGX_Result rc, char* out, size_t cap) {
    const wchar_t* w = GetNGXResultAsString(rc);
    char text[160]{};
    if (w) WideCharToMultiByte(CP_UTF8, 0, w, -1, text, int(sizeof text) - 1, nullptr, nullptr);
    std::snprintf(out, cap, "0x%08X %s", static_cast<unsigned>(rc), text);
    return out;
}

void Status(const char* text) {
    std::snprintf(s.status, sizeof s.status, "%s", text);
    Settings::Publish(s.status);
}

// The folder of the executable that is actually running, plus "\DLSS" (never the working directory).
bool Folder() {
    if (g_folder[0]) return true;
    wchar_t exe[3072]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
    if (!n || n >= std::size(exe)) return false;
    wchar_t* slash = std::wcsrchr(exe, L'\\');
    if (!slash) return false;
    *slash = 0;
    if (swprintf_s(g_folder, std::size(g_folder), L"%s\\DLSS", exe) <= 0) { g_folder[0] = 0; return false; }
    CreateDirectoryW(g_folder, nullptr);   // NGX writes its logs there; already there is fine
    return true;
}

NVSDK_NGX_FeatureCommonInfo CommonInfo(uint32_t level) {
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = g_paths;
    info.PathListInfo.Length = 1;
    info.LoggingInfo.LoggingCallback = level ? OnNgxLog : nullptr;
    info.LoggingInfo.MinimumLoggingLevel = level == 2 ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE
                                         : level == 1 ? NVSDK_NGX_LOGGING_LEVEL_ON : NVSDK_NGX_LOGGING_LEVEL_OFF;
    info.LoggingInfo.DisableOtherLoggingSinks = false;
    return info;
}

NVSDK_NGX_FeatureDiscoveryInfo Discovery(const NVSDK_NGX_FeatureCommonInfo& info) {
    NVSDK_NGX_FeatureDiscoveryInfo d{};
    d.SDKVersion = NVSDK_NGX_Version_API;
    d.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    d.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    d.Identifier.v.ProjectDesc.ProjectId = kProjectId;
    d.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    d.Identifier.v.ProjectDesc.EngineVersion = kEngineVersion;
    d.ApplicationDataPath = g_folder;
    d.FeatureInfo = &info;
    return d;
}

bool ExtensionsWanted() {
    char v[8]{};
    return !(GetEnvironmentVariableA("WAYSTONE_DLSS_EXT", v, sizeof v) == 1 && v[0] == '0');
}

bool Listed(const char* const* names, uint32_t count, const char* name) {
    for (uint32_t i = 0; i < count; ++i)
        if (names[i] && std::strcmp(names[i], name) == 0) return true;
    return false;
}

// NGX's extension arrays belong to NGX: the names are copied and kept for the process.
std::vector<std::string> g_instanceNames, g_deviceNames;

void WaitIdle() {
    const context::Handles* h = context::Get();
    if (h && h->device && h->dapi) h->dapi->DeviceWaitIdle(h->device);
}

// --- the sharpening after DLSS (2026-11-02) -----------------------------------------------------------
// DLSS 310 has no sharpening; FSR's own RCAS is the one the player tuned. DLSS then writes into an image of ours
// at the display size, and RCAS (shaders/HdDlssRcas.comp) reads it and writes the pass's output.
constexpr uint32_t kSharpSlots = 2;
struct Sharp {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t width = 0, height = 0;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet sets[kSharpSlots] = {};
    bool failed = false;        // not retried until Drop: DLSS stays unsharpened meanwhile (logged once)
    float logged = -1.0f;       // the strength last written to the journal
};
Sharp sh;
struct SharpPush { float con; uint32_t width, height; float boost; };

void DestroySharpImage(const context::Handles* h) {
    if (!h || !h->device || !h->dapi) return;
    if (sh.view) h->dapi->DestroyImageView(h->device, sh.view, nullptr);
    if (sh.image) h->dapi->DestroyImage(h->device, sh.image, nullptr);
    if (sh.memory) h->dapi->FreeMemory(h->device, sh.memory, nullptr);
    sh.view = VK_NULL_HANDLE; sh.image = VK_NULL_HANDLE; sh.memory = VK_NULL_HANDLE;
    sh.width = sh.height = 0;
}

// Device idle (the caller waited): everything of the sharpening.
void DestroySharp() {
    const context::Handles* h = context::Get();
    if (h && h->device && h->dapi) {
        DestroySharpImage(h);
        const auto& d = *h->dapi;
        if (sh.pipe) d.DestroyPipeline(h->device, sh.pipe, nullptr);
        if (sh.layout) d.DestroyPipelineLayout(h->device, sh.layout, nullptr);
        if (sh.pool) d.DestroyDescriptorPool(h->device, sh.pool, nullptr);
        if (sh.dsl) d.DestroyDescriptorSetLayout(h->device, sh.dsl, nullptr);
    }
    sh = Sharp{};
}

bool SharpFail(const char* what) {
    sh.failed = true;
    wxl::ScriptMgr::Log(WXL_LOG_ERROR, "dlss: the sharpening cannot be made (%s): DLSS stays unsharpened", what);
    return false;
}

bool EnsureSharpPipeline(const context::Handles* h) {
    if (sh.pipe) return true;
    const auto& d = *h->dapi;
    VkDescriptorSetLayoutBinding b[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        b[i].binding = i; b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[i].descriptorCount = 1; b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 2; dl.pBindings = b;
    if (d.CreateDescriptorSetLayout(h->device, &dl, nullptr, &sh.dsl) != VK_SUCCESS) return SharpFail("descriptor set layout");
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SharpPush)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &sh.dsl; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &range;
    if (d.CreatePipelineLayout(h->device, &pl, nullptr, &sh.layout) != VK_SUCCESS) return SharpFail("pipeline layout");
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sm.codeSize = sizeof(RcasSpv); sm.pCode = RcasSpv;
    VkShaderModule module = VK_NULL_HANDLE;
    if (d.CreateShaderModule(h->device, &sm, nullptr, &module) != VK_SUCCESS) return SharpFail("shader module");
    VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cp.stage.module = module; cp.stage.pName = "main";
    cp.layout = sh.layout;
    const VkResult r = d.CreateComputePipelines(h->device, VK_NULL_HANDLE, 1, &cp, nullptr, &sh.pipe);
    d.DestroyShaderModule(h->device, module, nullptr);
    if (r != VK_SUCCESS) { sh.pipe = VK_NULL_HANDLE; return SharpFail("compute pipeline"); }
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * kSharpSlots};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = kSharpSlots; dp.poolSizeCount = 1; dp.pPoolSizes = &size;
    if (d.CreateDescriptorPool(h->device, &dp, nullptr, &sh.pool) != VK_SUCCESS) return SharpFail("descriptor pool");
    VkDescriptorSetLayout layouts[kSharpSlots] = {sh.dsl, sh.dsl};
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = sh.pool; da.descriptorSetCount = kSharpSlots; da.pSetLayouts = layouts;
    if (d.AllocateDescriptorSets(h->device, &da, sh.sets) != VK_SUCCESS) return SharpFail("descriptor sets");
    return true;
}

// DLSS's own output at the display size, made again when the display size changes (after a device drain).
bool EnsureSharpImage(const context::Handles* h, uint32_t w, uint32_t hh, VkFormat format) {
    if (sh.image && sh.width == w && sh.height == hh) return true;
    if (sh.image) { WaitIdle(); DestroySharpImage(h); }
    const auto& d = *h->dapi;
    VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.imageType = VK_IMAGE_TYPE_2D; ic.format = format; ic.extent = VkExtent3D{w, hh, 1};
    ic.mipLevels = 1; ic.arrayLayers = 1; ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (d.CreateImage(h->device, &ic, nullptr, &sh.image) != VK_SUCCESS) { sh.image = VK_NULL_HANDLE; return SharpFail("image"); }
    VkMemoryRequirements req{};
    d.GetImageMemoryRequirements(h->device, sh.image, &req);
    VkPhysicalDeviceMemoryProperties mp{};
    h->iapi->GetPhysicalDeviceMemoryProperties(h->physical, &mp);
    uint32_t type = ~0u;
    for (uint32_t i = 0; i < mp.memoryTypeCount && type == ~0u; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) type = i;
    for (uint32_t i = 0; i < mp.memoryTypeCount && type == ~0u; ++i)
        if (req.memoryTypeBits & (1u << i)) type = i;
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = req.size; ma.memoryTypeIndex = type;
    if (type == ~0u || d.AllocateMemory(h->device, &ma, nullptr, &sh.memory) != VK_SUCCESS) {
        sh.memory = VK_NULL_HANDLE; DestroySharpImage(h); return SharpFail("memory");
    }
    if (d.BindImageMemory(h->device, sh.image, sh.memory, 0) != VK_SUCCESS) { DestroySharpImage(h); return SharpFail("memory binding"); }
    VkImageViewCreateInfo vc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vc.image = sh.image; vc.viewType = VK_IMAGE_VIEW_TYPE_2D; vc.format = format;
    vc.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (d.CreateImageView(h->device, &vc, nullptr, &sh.view) != VK_SUCCESS) { sh.view = VK_NULL_HANDLE; DestroySharpImage(h); return SharpFail("image view"); }
    sh.width = w; sh.height = hh;
    wxl::ScriptMgr::Log(WXL_LOG_INFO, "dlss: sharpening image %ux%u made (%llu KiB)", w, hh,
                        static_cast<unsigned long long>(req.size / 1024));
    return true;
}

bool SharpReady(const context::Handles* h, const Frame& f) {
    if (sh.failed || f.sharpness <= 0.0f) return false;
    if (!h || !h->device || !h->dapi || !h->iapi || !h->physical) return false;
    return EnsureSharpPipeline(h) && EnsureSharpImage(h, f.output.width, f.output.height, f.output.format);
}

VkImageMemoryBarrier SharpBarrier(VkImageLayout from, VkAccessFlags src, VkAccessFlags dst) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = src; b.dstAccessMask = dst;
    b.oldLayout = from; b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = sh.image;
    b.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return b;
}

// After DLSS wrote sh.image: RCAS from it into the pass's output (GENERAL, the caller's barrier made it so).
void RecordSharpen(const context::Handles* h, const Frame& f) {
    const auto& d = *h->dapi;
    const VkImageMemoryBarrier written = SharpBarrier(VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                                                      VK_ACCESS_SHADER_READ_BIT);
    d.CmdPipelineBarrier(f.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &written);
    const VkDescriptorSet set = sh.sets[f.slot % kSharpSlots];
    VkDescriptorImageInfo images[2]{{VK_NULL_HANDLE, sh.view, VK_IMAGE_LAYOUT_GENERAL}, {VK_NULL_HANDLE, f.output.view, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet w[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = set; w[i].dstBinding = i;
        w[i].descriptorCount = 1; w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[i].pImageInfo = &images[i];
    }
    d.UpdateDescriptorSets(h->device, 2, w, 0, nullptr);
    // 0..1: FSR's own curve (FSR's 1 is the strongest it gives). Above 1: RCAS at FSR's maximum, its change from the
    // DLSS image pushed further (x1..x2), clamped to the displayable range by the shader.
    const float s02 = f.sharpness < 0.0f ? 0.0f : f.sharpness > 2.0f ? 2.0f : f.sharpness;
    const float s01 = s02 > 1.0f ? 1.0f : s02;
    const SharpPush push{std::exp2(-(2.0f - 2.0f * s01)), f.output.width, f.output.height, s02 > 1.0f ? s02 : 1.0f};
    d.CmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sh.pipe);
    d.CmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sh.layout, 0, 1, &set, 0, nullptr);
    d.CmdPushConstants(f.cmd, sh.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, &push);
    d.CmdDispatch(f.cmd, (f.output.width + 7) / 8, (f.output.height + 7) / 8, 1);
    if (s02 != sh.logged) {
        sh.logged = s02;
        wxl::ScriptMgr::Log(WXL_LOG_INFO, "dlss: sharpening %.2f (RCAS strength %.3f, reinforced x%.2f)", s02, push.con, push.boost);
    }
}

void ReleaseFeature() {
    if (!s.feature) return;
    const NVSDK_NGX_Result rc = NVSDK_NGX_VULKAN_ReleaseFeature(s.feature);
    char t[200];
    if (NVSDK_NGX_FAILED(rc)) wxl::ScriptMgr::Log(WXL_LOG_WARN, "dlss: feature release failed (%s)", ResultText(rc, t, sizeof t));
    else wxl::ScriptMgr::Log(WXL_LOG_INFO, "dlss: feature released (%llu frame(s) upscaled, %llu failed)",
                             static_cast<unsigned long long>(s.evaluated), static_cast<unsigned long long>(s.failed));
    s.feature = nullptr;
    s.fRenderW = s.fRenderH = s.fDisplayW = s.fDisplayH = 0;
    s.fQuality = s.fPreset = ~0u;
}

// NGX on the game's own device, then its capabilities. Once per device; a failure is not retried (logged, status).
bool Start() {
    if (s.started) return true;
    if (s.startFailed) return false;
    const context::Handles* h = context::Get();
    if (!h || !h->instance || !h->device || !h->physical || !h->iapi || !h->iapi->GetDeviceProcAddr) {
        Status("DLSS indisponible: pas encore de peripherique Vulkan");
        return false;
    }
    s.startFailed = true;   // until proven otherwise
    if (!Folder()) { Status("DLSS indisponible: dossier de l'executable introuvable"); return false; }
    const NVSDK_NGX_FeatureCommonInfo info = CommonInfo(Settings::LogLevel());
    char t[200];
    NVSDK_NGX_Result rc = NVSDK_NGX_VULKAN_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion, g_folder,
        h->instance, h->physical, h->device, wxlvk::vk::loader::Api().GetInstanceProcAddr, h->iapi->GetDeviceProcAddr, &info);
    Flush();
    if (NVSDK_NGX_FAILED(rc)) {
        wxl::ScriptMgr::Log(WXL_LOG_ERROR, "dlss: NGX init failed (%s): FSR stays the upscaler", ResultText(rc, t, sizeof t));
        Status("DLSS indisponible: NGX non initialise (voir le journal), FSR utilise");
        return false;
    }
    rc = NVSDK_NGX_VULKAN_GetCapabilityParameters(&s.params);
    if (NVSDK_NGX_FAILED(rc) || !s.params) {
        wxl::ScriptMgr::Log(WXL_LOG_ERROR, "dlss: NGX capability parameters failed (%s)", ResultText(rc, t, sizeof t));
        NVSDK_NGX_VULKAN_Shutdown1(h->device);
        s.params = nullptr;
        Status("DLSS indisponible: parametres NGX refuses (voir le journal), FSR utilise");
        return false;
    }
    int available = 0, needsDriver = 0, initResult = 0;
    unsigned minMajor = 0, minMinor = 0;
    NVSDK_NGX_Parameter_GetI(s.params, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    NVSDK_NGX_Parameter_GetI(s.params, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
    NVSDK_NGX_Parameter_GetUI(s.params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minMajor);
    NVSDK_NGX_Parameter_GetUI(s.params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minMinor);
    NVSDK_NGX_Parameter_GetI(s.params, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &initResult);
    Flush();
    wxl::ScriptMgr::Log(WXL_LOG_INFO, "dlss: NGX started (project id, custom engine); Super Resolution available %d, driver "
                        "update needed %d (minimum %u.%u), feature init result %s", available, needsDriver, minMajor, minMinor,
                        ResultText(NVSDK_NGX_Result(initResult), t, sizeof t));
    if (!available) {
        NVSDK_NGX_VULKAN_DestroyParameters(s.params);
        s.params = nullptr;
        NVSDK_NGX_VULKAN_Shutdown1(h->device);
        Status(needsDriver ? "DLSS indisponible: pilote NVIDIA trop ancien (voir le journal), FSR utilise"
                           : "DLSS indisponible sur ce GPU / ce systeme (voir le journal), FSR utilise");
        return false;
    }
    s.started = true;
    s.startFailed = false;
    return true;
}

NVSDK_NGX_PerfQuality_Value Quality(uint32_t q) {
    switch (q) {
    case 0: return NVSDK_NGX_PerfQuality_Value_DLAA;
    case 2: return NVSDK_NGX_PerfQuality_Value_Balanced;
    case 3: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    default: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    }
}
const char* QualityName(uint32_t q) { return q == 0 ? "DLAA" : q == 2 ? "Equilibre" : q == 3 ? "Performance" : "Qualite"; }
}

// --- the Vulkan extensions (before the instance / the device exist) -------------------------------------------

void AddInstanceExtensions(const std::vector<VkExtensionProperties>& available, std::vector<const char*>& names) {
    if (!ExtensionsWanted()) { wxl::ScriptMgr::Log(WXL_LOG_INFO, "dlss: WAYSTONE_DLSS_EXT=0, no instance extension added"); return; }
    if (!Folder()) return;
    const NVSDK_NGX_FeatureCommonInfo info = CommonInfo(0);
    const NVSDK_NGX_FeatureDiscoveryInfo d = Discovery(info);
    uint32_t count = 0;
    VkExtensionProperties* props = nullptr;
    char t[200];
    const NVSDK_NGX_Result rc = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&d, &count, &props);
    if (NVSDK_NGX_FAILED(rc)) {
        wxl::ScriptMgr::Log(WXL_LOG_WARN, "dlss: instance extension query failed (%s): none added", ResultText(rc, t, sizeof t));
        return;
    }
    g_instanceNames.clear();
    for (uint32_t i = 0; i < count && props; ++i) g_instanceNames.emplace_back(props[i].extensionName);
    for (const std::string& n : g_instanceNames) {
        bool offered = false, already = false;
        for (const VkExtensionProperties& a : available) if (n == a.extensionName) offered = true;
        for (const char* e : names) if (e && n == e) already = true;
        if (offered && !already) names.push_back(n.c_str());
        wxl::ScriptMgr::Log(offered ? WXL_LOG_INFO : WXL_LOG_WARN, "dlss: instance extension %s %s", n.c_str(),
                            already ? "already enabled" : offered ? "added" : "NOT offered by the loader: DLSS will be unavailable");
    }
}

void AddDeviceExtensions(PFN_vkEnumerateDeviceExtensionProperties enumerate, VkInstance instance, VkPhysicalDevice physical,
                         const char** names, uint32_t& count, uint32_t capacity) {
    if (!ExtensionsWanted()) { wxl::ScriptMgr::Log(WXL_LOG_INFO, "dlss: WAYSTONE_DLSS_EXT=0, no device extension added"); return; }
    if (!enumerate || !instance || !physical || !Folder()) return;
    const NVSDK_NGX_FeatureCommonInfo info = CommonInfo(0);
    const NVSDK_NGX_FeatureDiscoveryInfo d = Discovery(info);
    uint32_t n = 0;
    VkExtensionProperties* props = nullptr;
    char t[200];
    const NVSDK_NGX_Result rc = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, physical, &d, &n, &props);
    if (NVSDK_NGX_FAILED(rc)) {
        wxl::ScriptMgr::Log(WXL_LOG_WARN, "dlss: device extension query failed (%s): none added, DLSS will be unavailable",
                            ResultText(rc, t, sizeof t));
        return;
    }
    uint32_t offeredCount = 0;
    std::vector<VkExtensionProperties> offered;
    if (enumerate(physical, nullptr, &offeredCount, nullptr) == VK_SUCCESS && offeredCount) {
        offered.resize(offeredCount);
        if (enumerate(physical, nullptr, &offeredCount, offered.data()) != VK_SUCCESS) offered.clear();
        else offered.resize(offeredCount);
    }
    g_deviceNames.clear();
    for (uint32_t i = 0; i < n && props; ++i) g_deviceNames.emplace_back(props[i].extensionName);
    for (const std::string& e : g_deviceNames) {
        bool have = false;
        for (const VkExtensionProperties& a : offered) if (e == a.extensionName) have = true;
        const bool already = Listed(names, count, e.c_str());
        const char* what = already ? "already enabled" : !have ? "NOT offered by the adapter: DLSS will be unavailable"
                         : count >= capacity ? "NOT added (no room in the list): DLSS will be unavailable" : "added";
        if (have && !already && count < capacity) names[count++] = e.c_str();
        wxl::ScriptMgr::Log(have && count <= capacity ? WXL_LOG_INFO : WXL_LOG_WARN, "dlss: device extension %s %s", e.c_str(), what);
    }
}

// --- the render thread ----------------------------------------------------------------------------------------

bool RenderSize(uint32_t displayW, uint32_t displayH, uint32_t quality, uint32_t& w, uint32_t& h) {
    if (!Start()) { Flush(); return false; }
    // A feature NVIDIA refused stays refused until DLSS is unticked (Drop): FSR meanwhile, never a retry every frame.
    if (s.featureFailed) { Status("DLSS: creation refusee (voir le journal), FSR utilise; decocher puis recocher pour reessayer"); return false; }
    if (displayW == s.qDisplayW && displayH == s.qDisplayH && quality == s.qQuality) {
        w = s.qRenderW; h = s.qRenderH;
        return s.qOk;
    }
    s.qDisplayW = displayW; s.qDisplayH = displayH; s.qQuality = quality; s.qOk = false;
    unsigned ow = 0, oh = 0, maxW = 0, maxH = 0, minW = 0, minH = 0;
    float sharp = 0;
    char t[200];
    const NVSDK_NGX_Result rc = NGX_DLSS_GET_OPTIMAL_SETTINGS(s.params, displayW, displayH, Quality(quality), &ow, &oh,
                                                             &maxW, &maxH, &minW, &minH, &sharp);
    Flush();
    if (NVSDK_NGX_FAILED(rc) || !ow || !oh || ow > displayW || oh > displayH) {
        wxl::ScriptMgr::Log(WXL_LOG_ERROR, "dlss: no render size for %ux%u %s (%s, %ux%u)", displayW, displayH, QualityName(quality),
                            ResultText(rc, t, sizeof t), ow, oh);
        Status("DLSS indisponible: taille de rendu refusee (voir le journal), FSR utilise");
        return false;
    }
    wxl::ScriptMgr::Log(WXL_LOG_INFO, "dlss: render size %ux%u for %ux%u (%s; NVIDIA range %ux%u .. %ux%u)", ow, oh, displayW,
                        displayH, QualityName(quality), minW, minH, maxW, maxH);
    s.qRenderW = w = ow; s.qRenderH = h = oh; s.qOk = true;
    return true;
}

int Evaluate(const Frame& f) {
    if (!s.started || !s.params || !f.cmd) return -1;
    const context::Handles* h = context::Get();
    if (!h || !h->device) return -1;
    char t[200];
    const uint32_t preset = Settings::Preset();
    const bool same = s.feature && s.fRenderW == f.color.width && s.fRenderH == f.color.height && s.fDisplayW == f.output.width
                      && s.fDisplayH == f.output.height && s.fQuality == f.quality && s.fPreset == preset;
    if (!same) {
        if (s.featureFailed && s.fRenderW == f.color.width && s.fRenderH == f.color.height && s.fDisplayW == f.output.width
            && s.fDisplayH == f.output.height && s.fQuality == f.quality && s.fPreset == preset) return -1;
        if (s.feature) { WaitIdle(); ReleaseFeature(); }
        s.fRenderW = f.color.width; s.fRenderH = f.color.height; s.fDisplayW = f.output.width; s.fDisplayH = f.output.height;
        s.fQuality = f.quality; s.fPreset = preset;
        for (const char* key : {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                                NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                                NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality})
            NVSDK_NGX_Parameter_SetUI(s.params, key, preset);
        NVSDK_NGX_DLSS_Create_Params c{};
        c.Feature.InWidth = f.color.width;
        c.Feature.InHeight = f.color.height;
        c.Feature.InTargetWidth = f.output.width;
        c.Feature.InTargetHeight = f.output.height;
        c.Feature.InPerfQualityValue = Quality(f.quality);
        // Motion at the render size, without jitter; depth 0 near (not inverted); display-referred colour (not HDR).
        c.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        c.InEnableOutputSubrects = false;
        const NVSDK_NGX_Result rc = NGX_VULKAN_CREATE_DLSS_EXT1(h->device, f.cmd, 1, 1, &s.feature, s.params, &c);
        Flush();
        if (NVSDK_NGX_FAILED(rc) || !s.feature) {
            s.feature = nullptr;
            s.featureFailed = true;
            wxl::ScriptMgr::Log(WXL_LOG_ERROR, "dlss: feature %ux%u -> %ux%u (%s, preset %u) not created (%s)", f.color.width,
                                f.color.height, f.output.width, f.output.height, QualityName(f.quality), preset, ResultText(rc, t, sizeof t));
            Status("DLSS: creation refusee (voir le journal), FSR utilise; decocher puis recocher pour reessayer");
            return -1;
        }
        s.featureFailed = false;
        wxl::ScriptMgr::Log(WXL_LOG_INFO, "dlss: feature %ux%u -> %ux%u created (%s, preset %u%s)", f.color.width, f.color.height,
                            f.output.width, f.output.height, QualityName(f.quality), preset, preset ? "" : " = NVIDIA default");
        char presetName[16] = "par defaut";
        if (preset) std::snprintf(presetName, sizeof presetName, "%c", char('A' + preset - 1));
        char text[200];
        std::snprintf(text, sizeof text, "DLSS actif: %s, %ux%u -> %ux%u, preset %s", QualityName(f.quality), f.color.width,
                      f.color.height, f.output.width, f.output.height, presetName);
        Status(text);
        return 0;   // the creation's own work is in this command buffer: the first evaluate is next frame
    }
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto R = [&](const Image& i, bool rw) {
        return NVSDK_NGX_Create_ImageView_Resource_VK(i.view, i.image, range, i.format, i.width, i.height, rw);
    };
    // Sharpened: DLSS into our image, then RCAS into the output. The image is discarded and written whole; its
    // last reader was the previous frame's RCAS (compute, earlier in the queue).
    const bool sharpen = SharpReady(h, f);
    Image target = f.output;
    if (sharpen) {
        target.image = sh.image; target.view = sh.view;
        const VkImageMemoryBarrier discard = SharpBarrier(VK_IMAGE_LAYOUT_UNDEFINED, 0,
                                                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        h->dapi->CmdPipelineBarrier(f.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                                    0, nullptr, 1, &discard);
    }
    NVSDK_NGX_Resource_VK color = R(f.color, false), depth = R(f.depth, false), motion = R(f.motion, false), output = R(target, true);
    NVSDK_NGX_VK_DLSS_Eval_Params e{};
    e.Feature.pInColor = &color;
    e.Feature.pInOutput = &output;
    e.pInDepth = &depth;
    e.pInMotionVectors = &motion;
    const float sign = Settings::JitterFlip() ? -1.0f : 1.0f;
    e.InJitterOffsetX = sign * f.jitterX;
    e.InJitterOffsetY = sign * f.jitterY;
    e.InRenderSubrectDimensions.Width = f.color.width;
    e.InRenderSubrectDimensions.Height = f.color.height;
    e.InReset = f.reset ? 1 : 0;
    e.InMVScaleX = f.mvScaleX;
    e.InMVScaleY = f.mvScaleY;
    e.InPreExposure = 1.0f;
    e.InExposureScale = 1.0f;
    e.InFrameTimeDeltaInMsec = f.milliseconds;
    const NVSDK_NGX_Result rc = NGX_VULKAN_EVALUATE_DLSS_EXT(f.cmd, s.feature, s.params, &e);
    Flush();
    if (NVSDK_NGX_FAILED(rc)) {
        if (s.failed++ < 8) wxl::ScriptMgr::Log(WXL_LOG_ERROR, "dlss: evaluate not recorded (%s): plain stretch this frame",
                                                ResultText(rc, t, sizeof t));
        Status("DLSS: evaluation refusee (voir le journal), image etiree");
        return -1;
    }
    ++s.evaluated;
    if (sharpen) RecordSharpen(h, f);
    char text[200];
    if (sharpen) std::snprintf(text, sizeof text, "%s, nettete %.2f", s.status, f.sharpness > 2.0f ? 2.0f : f.sharpness);
    else std::snprintf(text, sizeof text, "%s, nettete %s", s.status, sh.failed ? "refusee (voir le journal)" : "0");
    Settings::Publish(text);
    return 1;
}

void Drop() {
    s.featureFailed = false;   // unticked: the next request tries again
    s.fRenderW = s.fRenderH = s.fDisplayW = s.fDisplayH = 0;
    s.fQuality = s.fPreset = ~0u;
    if (s.feature || sh.image || sh.pipe) {
        WaitIdle();   // NGX frees what the frames in flight may still read; so does the sharpening
        ReleaseFeature();
        DestroySharp();
    }
    sh.failed = false;
    if (std::strcmp(s.status, "desactive")) Status("desactive");
}

void Shutdown() {
    Flush();
    const context::Handles* h = context::Get();
    ReleaseFeature();   // the caller waited for the device
    DestroySharp();
    if (s.params) NVSDK_NGX_VULKAN_DestroyParameters(s.params);
    s.params = nullptr;
    if (s.started && h && h->device) {
        const NVSDK_NGX_Result rc = NVSDK_NGX_VULKAN_Shutdown1(h->device);
        char t[200];
        wxl::ScriptMgr::Log(NVSDK_NGX_FAILED(rc) ? WXL_LOG_WARN : WXL_LOG_INFO, "dlss: NGX shut down (%s)", ResultText(rc, t, sizeof t));
    }
    Flush();
    s = State{};
}
}
#endif // WOWDECOMP_DLSS
