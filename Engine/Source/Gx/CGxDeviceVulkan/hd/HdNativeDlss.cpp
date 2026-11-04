// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: see HdNativeDlss.h. NGX start-up, creation, evaluation and the extension negotiation follow Mado's Azeroth Remastered
// hd/dlss/HdDlss.cpp (GPL-3.0-or-later); the device glue (context handles, images, logging, cvars) is Aetheria's.
#ifdef AETHERIA_NATIVE_DLSS
#include "HdNativeDlss.h"
#include "dlss/shaders/HdDlssRcas.spv.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Loader.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture.hpp"
#include "WoW/Source/Ui/OptionCVars.h"
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <string>
namespace HD::NativeDlss {
namespace {
namespace context = gxvk::vk::context;
namespace frame = gxvk::vk::frame;
namespace texture = gxvk::vk::texture;

constexpr const char* kProjectId = "6f1d2c8a-5b47-4e39-9a0e-3c7b21d4f855";   // Mado's project id for this client family
constexpr const char* kEngineVersion = "3.3.5a-aetheria";

struct State {
    bool started = false, startFailed = false;
    NVSDK_NGX_Parameter* params = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    uint32_t fRenderW = 0, fRenderH = 0, fDisplayW = 0, fDisplayH = 0, fQuality = ~0u, fPreset = ~0u;
    bool featureFailed = false;
    uint32_t qDisplayW = 0, qDisplayH = 0, qQuality = ~0u, qRenderW = 0, qRenderH = 0; bool qOk = false;
    uint64_t evaluated = 0, failed = 0, lastSerial = 0;
    char status[200]{};
    CVar *want = nullptr, *sharp = nullptr, *preset = nullptr, *flip = nullptr, *logLevel = nullptr, *info = nullptr;
    char lastInfo[200]{};
};
State s;

wchar_t g_folder[3100]{};
wchar_t* g_paths[1] = {g_folder};
std::mutex g_logMutex;
std::vector<std::string> g_logLines;
uint64_t g_logCount = 0;
constexpr uint64_t kLoggedLines = 400;
std::vector<std::string> g_instanceNames, g_deviceNames;

void Logf(const char* fmt, ...) {
    char buf[400];
    va_list args; va_start(args, fmt); std::vsnprintf(buf, sizeof buf, fmt, args); va_end(args);
    CGxDevice::Log("HD dlss: %s", static_cast<const char*>(buf));   // const char*: a char array would bind to the va_list overload
}
void NVSDK_CONV OnNgxLog(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (++g_logCount > kLoggedLines || !message) return;
    std::string line(message);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    g_logLines.push_back(std::move(line));
}
void Flush() {
    std::vector<std::string> lines;
    { std::lock_guard<std::mutex> lock(g_logMutex); lines.swap(g_logLines); }
    for (const std::string& l : lines) Logf("NGX: %s", l.c_str());
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
    if (s.info && std::strcmp(s.lastInfo, text)) { std::snprintf(s.lastInfo, sizeof s.lastInfo, "%s", text); s.info->Set(s.lastInfo, true, false, false, false); }
}
bool Folder() {
    if (g_folder[0]) return true;
    wchar_t exe[3072]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
    if (!n || n >= std::size(exe)) return false;
    wchar_t* slash = std::wcsrchr(exe, L'\\');
    if (!slash) return false;
    *slash = 0;
    if (swprintf_s(g_folder, std::size(g_folder), L"%s\\DLSS", exe) <= 0) { g_folder[0] = 0; return false; }
    CreateDirectoryW(g_folder, nullptr);
    return true;
}
uint32_t LogLevel() { return s.logLevel ? uint32_t(OptionCVars::Int(s.logLevel, 0, 2, 1)) : 1u; }
NVSDK_NGX_FeatureCommonInfo CommonInfo(uint32_t level) {
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = g_paths;
    info.PathListInfo.Length = 1;
    info.LoggingInfo.LoggingCallback = level ? OnNgxLog : nullptr;
    info.LoggingInfo.MinimumLoggingLevel = level == 2 ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE : level == 1 ? NVSDK_NGX_LOGGING_LEVEL_ON : NVSDK_NGX_LOGGING_LEVEL_OFF;
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
bool Listed(const char* const* names, uint32_t count, const char* name) {
    for (uint32_t i = 0; i < count; ++i) if (names[i] && std::strcmp(names[i], name) == 0) return true;
    return false;
}
void WaitIdle() {
    const context::Handles* h = context::Get();
    if (h && h->device && h->dapi) h->dapi->DeviceWaitIdle(h->device);
}

// --- RCAS after DLSS (DLSS 310 has no sharpening of its own) ---------------------------------------------------
constexpr uint32_t kSlots = frame::kFramesInFlight;
struct Sharp {
    texture::Image image{};
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE; VkPipeline pipe = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE; VkDescriptorSet sets[kSlots]{};
    bool failed = false; float logged = -1.0f; uint64_t lastUsed = 0;
};
Sharp sh;
struct SharpPush { float con; uint32_t width, height; float boost; };
bool SharpFail(const char* what) { sh.failed = true; Logf("the sharpening cannot be made (%s): DLSS stays unsharpened", what); return false; }

void DestroySharp() {
    const context::Handles* h = context::Get();
    if (h && h->device && h->dapi) {
        const auto& d = *h->dapi;
        if (sh.pipe) d.DestroyPipeline(h->device, sh.pipe, nullptr);
        if (sh.layout) d.DestroyPipelineLayout(h->device, sh.layout, nullptr);
        if (sh.pool) d.DestroyDescriptorPool(h->device, sh.pool, nullptr);
        if (sh.dsl) d.DestroyDescriptorSetLayout(h->device, sh.dsl, nullptr);
    }
    frame::RetireImage(sh.image, sh.lastUsed);
    sh = Sharp{};
}
bool EnsureSharpPipeline(const context::Handles* h) {
    if (sh.pipe) return true;
    const auto& d = *h->dapi;
    VkDescriptorSetLayoutBinding b[2]{};
    for (uint32_t i = 0; i < 2; ++i) { b[i].binding = i; b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; b[i].descriptorCount = 1; b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; }
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 2; dl.pBindings = b;
    if (d.CreateDescriptorSetLayout(h->device, &dl, nullptr, &sh.dsl) != VK_SUCCESS) return SharpFail("descriptor set layout");
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SharpPush)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &sh.dsl; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &range;
    if (d.CreatePipelineLayout(h->device, &pl, nullptr, &sh.layout) != VK_SUCCESS) return SharpFail("pipeline layout");
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sm.codeSize = sizeof(HD::Dlss::RcasSpv); sm.pCode = HD::Dlss::RcasSpv;
    VkShaderModule module = VK_NULL_HANDLE;
    if (d.CreateShaderModule(h->device, &sm, nullptr, &module) != VK_SUCCESS) return SharpFail("shader module");
    VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cp.stage.module = module; cp.stage.pName = "main";
    cp.layout = sh.layout;
    const VkResult r = d.CreateComputePipelines(h->device, VK_NULL_HANDLE, 1, &cp, nullptr, &sh.pipe);
    d.DestroyShaderModule(h->device, module, nullptr);
    if (r != VK_SUCCESS) { sh.pipe = VK_NULL_HANDLE; return SharpFail("compute pipeline"); }
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * kSlots};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = kSlots; dp.poolSizeCount = 1; dp.pPoolSizes = &size;
    if (d.CreateDescriptorPool(h->device, &dp, nullptr, &sh.pool) != VK_SUCCESS) return SharpFail("descriptor pool");
    VkDescriptorSetLayout layouts[kSlots]; for (auto& l : layouts) l = sh.dsl;
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = sh.pool; da.descriptorSetCount = kSlots; da.pSetLayouts = layouts;
    if (d.AllocateDescriptorSets(h->device, &da, sh.sets) != VK_SUCCESS) return SharpFail("descriptor sets");
    return true;
}
bool EnsureSharpImage(uint32_t w, uint32_t hh, VkFormat format) {
    if (sh.image.handle && sh.image.extent.width == w && sh.image.extent.height == hh && sh.image.format == format) return true;
    frame::RetireImage(sh.image, sh.lastUsed);
    if (!texture::CreateStorage(w, hh, format, sh.image)) { sh.image = texture::Image{}; return SharpFail("image"); }
    return true;
}
bool SharpReady(const context::Handles* h, const Frame& f) {
    if (sh.failed || f.sharpness <= 0.0f || !h || !h->device || !h->dapi) return false;
    return EnsureSharpPipeline(h) && EnsureSharpImage(f.output.width, f.output.height, f.output.format);
}
VkImageMemoryBarrier SharpBarrier(VkImageLayout from, VkAccessFlags src, VkAccessFlags dst) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = src; b.dstAccessMask = dst; b.oldLayout = from; b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = sh.image.handle;
    b.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return b;
}
void RecordSharpen(const context::Handles* h, const Frame& f) {
    const auto& d = *h->dapi;
    const VkImageMemoryBarrier written = SharpBarrier(VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    d.CmdPipelineBarrier(f.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &written);
    const VkDescriptorSet set = sh.sets[f.slot % kSlots];
    VkDescriptorImageInfo images[2]{{VK_NULL_HANDLE, sh.image.view, VK_IMAGE_LAYOUT_GENERAL}, {VK_NULL_HANDLE, f.output.view, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet w[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = set; w[i].dstBinding = i;
        w[i].descriptorCount = 1; w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[i].pImageInfo = &images[i];
    }
    d.UpdateDescriptorSets(h->device, 2, w, 0, nullptr);
    const float s02 = std::clamp(f.sharpness, 0.0f, 2.0f);
    const float s01 = std::min(s02, 1.0f);
    const SharpPush push{std::exp2(-(2.0f - 2.0f * s01)), f.output.width, f.output.height, s02 > 1.0f ? s02 : 1.0f};
    d.CmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sh.pipe);
    d.CmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sh.layout, 0, 1, &set, 0, nullptr);
    d.CmdPushConstants(f.cmd, sh.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, &push);
    d.CmdDispatch(f.cmd, (f.output.width + 7) / 8, (f.output.height + 7) / 8, 1);
    sh.lastUsed = f.serial;
    if (s02 != sh.logged) { sh.logged = s02; Logf("sharpening %.2f (RCAS strength %.3f, reinforced x%.2f)", s02, push.con, push.boost); }
}

void ReleaseFeature() {
    if (!s.feature) return;
    const NVSDK_NGX_Result rc = NVSDK_NGX_VULKAN_ReleaseFeature(s.feature);
    char t[200];
    if (NVSDK_NGX_FAILED(rc)) Logf("feature release failed (%s)", ResultText(rc, t, sizeof t));
    else Logf("feature released (%llu frame(s) upscaled, %llu failed)", static_cast<unsigned long long>(s.evaluated), static_cast<unsigned long long>(s.failed));
    s.feature = nullptr;
    s.fRenderW = s.fRenderH = s.fDisplayW = s.fDisplayH = 0;
    s.fQuality = s.fPreset = ~0u;
}
bool Start() {
    if (s.started) return true;
    if (s.startFailed) return false;
    const context::Handles* h = context::Get();
    if (!h || !h->instance || !h->device || !h->physical || !h->iapi || !h->iapi->GetDeviceProcAddr) { Status("DLSS unavailable: no Vulkan device yet"); return false; }
    s.startFailed = true;
    if (!Folder()) { Status("DLSS unavailable: executable folder not found"); return false; }
    const NVSDK_NGX_FeatureCommonInfo info = CommonInfo(LogLevel());
    char t[200];
    NVSDK_NGX_Result rc = NVSDK_NGX_VULKAN_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion, g_folder,
        h->instance, h->physical, h->device, gxvk::vk::loader::Api().GetInstanceProcAddr, h->iapi->GetDeviceProcAddr, &info);
    Flush();
    if (NVSDK_NGX_FAILED(rc)) {
        Logf("NGX init failed (%s): FSR stays the upscaler", ResultText(rc, t, sizeof t));
        Status("DLSS unavailable: NGX did not start (see gx.log, is the DLSS folder with nvngx_dlss.dll beside the exe?), FSR used");
        return false;
    }
    rc = NVSDK_NGX_VULKAN_GetCapabilityParameters(&s.params);
    if (NVSDK_NGX_FAILED(rc) || !s.params) {
        Logf("NGX capability parameters failed (%s)", ResultText(rc, t, sizeof t));
        NVSDK_NGX_VULKAN_Shutdown1(h->device); s.params = nullptr;
        Status("DLSS unavailable: NGX refused its parameters (see gx.log), FSR used");
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
    Logf("NGX started; Super Resolution available %d, driver update needed %d (minimum %u.%u), feature init result %s", available, needsDriver, minMajor, minMinor,
         ResultText(NVSDK_NGX_Result(initResult), t, sizeof t));
    if (!available) {
        NVSDK_NGX_VULKAN_DestroyParameters(s.params); s.params = nullptr;
        NVSDK_NGX_VULKAN_Shutdown1(h->device);
        Status(needsDriver ? "DLSS unavailable: NVIDIA driver too old (see gx.log), FSR used" : "DLSS unavailable on this GPU / system (see gx.log), FSR used");
        return false;
    }
    s.started = true; s.startFailed = false;
    return true;
}
// hdFsrQuality 1 quality / 2 balanced / 3 performance / 4 ultra performance
NVSDK_NGX_PerfQuality_Value Quality(uint32_t q) {
    switch (q) {
    case 2: return NVSDK_NGX_PerfQuality_Value_Balanced;
    case 3: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case 4: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    default: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    }
}
const char* QualityName(uint32_t q) { return q == 2 ? "balanced" : q == 3 ? "performance" : q == 4 ? "ultra performance" : "quality"; }
uint32_t Preset() { return s.preset ? uint32_t(OptionCVars::Int(s.preset, 0, 15, 0)) : 0u; }
}

void Register() {
    if (s.want) return;
    s.want = OptionCVars::Register("hdDlss", "HD: NVIDIA DLSS Super Resolution in FSR's place (Azeroth Remastered, RTX GPU, needs the DLSS folder beside the exe): 0 off, 1 on", "0");
    s.preset = OptionCVars::Register("hdDlssPreset", "HD: DLSS render preset, 0 NVIDIA default / 10 J / 11 K / 12 L / 13 M", "0");
    s.sharp = OptionCVars::Register("hdDlssSharpness", "HD: sharpening after DLSS 0..2 (0 none, 1 = FSR's maximum, up to 2 reinforced)", "0.3");
    s.flip = OptionCVars::Register("hdDlssJitterFlip", "HD: DLSS jitter sign inverted (diagnostic)", "0");
    s.logLevel = OptionCVars::Register("hdDlssLog", "HD: NVIDIA NGX messages in gx.log, 0 off / 1 on / 2 verbose; read when DLSS starts", "1");
    s.info = CVar::Register("hdDlssInfo", "HD: DLSS status (read only)", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}
bool Requested() { Register(); return OptionCVars::Bool(s.want, false); }
float Sharpness() { return OptionCVars::Float(s.sharp, 0.0f, 2.0f, 0.3f); }

void AddInstanceExtensions(const std::vector<VkExtensionProperties>& available, std::vector<const char*>& names) {
    char v[8]{};
    if (GetEnvironmentVariableA("AETHERIA_DLSS_EXT", v, sizeof v) == 1 && v[0] == '0') return;
    if (!Folder()) return;
    const NVSDK_NGX_FeatureCommonInfo info = CommonInfo(0);
    const NVSDK_NGX_FeatureDiscoveryInfo d = Discovery(info);
    uint32_t count = 0; VkExtensionProperties* props = nullptr;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&d, &count, &props))) return;
    g_instanceNames.clear();
    for (uint32_t i = 0; i < count && props; ++i) g_instanceNames.emplace_back(props[i].extensionName);
    for (const std::string& n : g_instanceNames) {
        bool offered = false, already = false;
        for (const VkExtensionProperties& a : available) if (n == a.extensionName) offered = true;
        for (const char* e : names) if (e && n == e) already = true;
        if (offered && !already) names.push_back(n.c_str());
    }
}
void AddDeviceExtensions(PFN_vkEnumerateDeviceExtensionProperties enumerate, VkInstance instance, VkPhysicalDevice physical,
                         const char** names, uint32_t& count, uint32_t capacity) {
    char v[8]{};
    if (GetEnvironmentVariableA("AETHERIA_DLSS_EXT", v, sizeof v) == 1 && v[0] == '0') return;
    if (!enumerate || !instance || !physical || !Folder()) return;
    const NVSDK_NGX_FeatureCommonInfo info = CommonInfo(0);
    const NVSDK_NGX_FeatureDiscoveryInfo d = Discovery(info);
    uint32_t n = 0; VkExtensionProperties* props = nullptr;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, physical, &d, &n, &props))) return;
    uint32_t offeredCount = 0;
    std::vector<VkExtensionProperties> offered;
    if (enumerate(physical, nullptr, &offeredCount, nullptr) == VK_SUCCESS && offeredCount) {
        offered.resize(offeredCount);
        if (enumerate(physical, nullptr, &offeredCount, offered.data()) != VK_SUCCESS) offered.clear(); else offered.resize(offeredCount);
    }
    g_deviceNames.clear();
    for (uint32_t i = 0; i < n && props; ++i) g_deviceNames.emplace_back(props[i].extensionName);
    for (const std::string& e : g_deviceNames) {
        bool have = false;
        for (const VkExtensionProperties& a : offered) if (e == a.extensionName) have = true;
        if (have && !Listed(names, count, e.c_str()) && count < capacity) names[count++] = e.c_str();
    }
}

bool RenderSize(uint32_t displayW, uint32_t displayH, uint32_t quality, uint32_t& w, uint32_t& h) {
    if (!Start()) { Flush(); return false; }
    if (s.featureFailed) { Status("DLSS: feature creation refused (see gx.log), FSR used; set hdDlss 0 then 1 to retry"); return false; }
    if (displayW == s.qDisplayW && displayH == s.qDisplayH && quality == s.qQuality) { w = s.qRenderW; h = s.qRenderH; return s.qOk; }
    s.qDisplayW = displayW; s.qDisplayH = displayH; s.qQuality = quality; s.qOk = false;
    unsigned ow = 0, oh = 0, maxW = 0, maxH = 0, minW = 0, minH = 0; float sharp = 0; char t[200];
    const NVSDK_NGX_Result rc = NGX_DLSS_GET_OPTIMAL_SETTINGS(s.params, displayW, displayH, Quality(quality), &ow, &oh, &maxW, &maxH, &minW, &minH, &sharp);
    Flush();
    if (NVSDK_NGX_FAILED(rc) || !ow || !oh || ow > displayW || oh > displayH) {
        Logf("no render size for %ux%u %s (%s, %ux%u)", displayW, displayH, QualityName(quality), ResultText(rc, t, sizeof t), ow, oh);
        Status("DLSS unavailable: render size refused (see gx.log), FSR used");
        return false;
    }
    Logf("render size %ux%u for %ux%u (%s)", ow, oh, displayW, displayH, QualityName(quality));
    s.qRenderW = w = ow; s.qRenderH = h = oh; s.qOk = true;
    return true;
}

int Evaluate(const Frame& f) {
    if (!s.started || !s.params || !f.cmd) return -1;
    const context::Handles* h = context::Get();
    if (!h || !h->device) return -1;
    char t[200];
    const uint32_t preset = Preset();
    const bool same = s.feature && s.fRenderW == f.color.width && s.fRenderH == f.color.height && s.fDisplayW == f.output.width &&
                      s.fDisplayH == f.output.height && s.fQuality == f.quality && s.fPreset == preset;
    if (!same) {
        if (s.featureFailed && s.fRenderW == f.color.width && s.fRenderH == f.color.height && s.fDisplayW == f.output.width &&
            s.fDisplayH == f.output.height && s.fQuality == f.quality && s.fPreset == preset) return -1;
        if (s.feature) { WaitIdle(); ReleaseFeature(); }
        s.fRenderW = f.color.width; s.fRenderH = f.color.height; s.fDisplayW = f.output.width; s.fDisplayH = f.output.height;
        s.fQuality = f.quality; s.fPreset = preset;
        for (const char* key : {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                                NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                                NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality})
            NVSDK_NGX_Parameter_SetUI(s.params, key, preset);
        NVSDK_NGX_DLSS_Create_Params c{};
        c.Feature.InWidth = f.color.width; c.Feature.InHeight = f.color.height;
        c.Feature.InTargetWidth = f.output.width; c.Feature.InTargetHeight = f.output.height;
        c.Feature.InPerfQualityValue = Quality(f.quality);
        c.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;   // motion at the render size, no jitter; depth 0 near; display-referred colour
        c.InEnableOutputSubrects = false;
        const NVSDK_NGX_Result rc = NGX_VULKAN_CREATE_DLSS_EXT1(h->device, f.cmd, 1, 1, &s.feature, s.params, &c);
        Flush();
        if (NVSDK_NGX_FAILED(rc) || !s.feature) {
            s.feature = nullptr; s.featureFailed = true;
            Logf("feature %ux%u -> %ux%u (%s, preset %u) not created (%s)", f.color.width, f.color.height, f.output.width, f.output.height, QualityName(f.quality), preset, ResultText(rc, t, sizeof t));
            Status("DLSS: feature creation refused (see gx.log), FSR used; set hdDlss 0 then 1 to retry");
            return -1;
        }
        s.featureFailed = false;
        Logf("feature %ux%u -> %ux%u created (%s, preset %u)", f.color.width, f.color.height, f.output.width, f.output.height, QualityName(f.quality), preset);
        char text[200];
        std::snprintf(text, sizeof text, "DLSS active: %s, %ux%u -> %ux%u, preset %u", QualityName(f.quality), f.color.width, f.color.height, f.output.width, f.output.height, preset);
        Status(text);
        return 0;
    }
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto R = [&](const Image& i, bool rw) { return NVSDK_NGX_Create_ImageView_Resource_VK(i.view, i.image, range, i.format, i.width, i.height, rw); };
    const bool sharpen = SharpReady(h, f);
    Image target = f.output;
    if (sharpen) {
        target.image = sh.image.handle; target.view = sh.image.view;
        const VkImageMemoryBarrier discard = SharpBarrier(VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        h->dapi->CmdPipelineBarrier(f.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &discard);
    }
    NVSDK_NGX_Resource_VK color = R(f.color, false), depth = R(f.depth, false), motion = R(f.motion, false), output = R(target, true);
    NVSDK_NGX_VK_DLSS_Eval_Params e{};
    e.Feature.pInColor = &color; e.Feature.pInOutput = &output;
    e.pInDepth = &depth; e.pInMotionVectors = &motion;
    const float sign = OptionCVars::Bool(s.flip, false) ? -1.0f : 1.0f;
    e.InJitterOffsetX = sign * f.jitterX; e.InJitterOffsetY = sign * f.jitterY;
    e.InRenderSubrectDimensions.Width = f.color.width; e.InRenderSubrectDimensions.Height = f.color.height;
    e.InReset = f.reset ? 1 : 0;
    e.InMVScaleX = f.mvScaleX; e.InMVScaleY = f.mvScaleY;
    e.InPreExposure = 1.0f; e.InExposureScale = 1.0f; e.InFrameTimeDeltaInMsec = f.milliseconds;
    const NVSDK_NGX_Result rc = NGX_VULKAN_EVALUATE_DLSS_EXT(f.cmd, s.feature, s.params, &e);
    Flush();
    if (NVSDK_NGX_FAILED(rc)) {
        if (s.failed++ < 8) Logf("evaluate not recorded (%s): plain stretch this frame", ResultText(rc, t, sizeof t));
        Status("DLSS: evaluation refused (see gx.log), image stretched");
        return -1;
    }
    ++s.evaluated;
    if (sharpen) RecordSharpen(h, f);
    char text[200];
    std::snprintf(text, sizeof text, "%s, sharpness %.2f", s.status, sharpen ? std::min(f.sharpness, 2.0f) : 0.0f);
    if (std::strcmp(text, s.lastInfo) && s.info) { std::snprintf(s.lastInfo, sizeof s.lastInfo, "%s", text); s.info->Set(s.lastInfo, true, false, false, false); }
    return 1;
}

void Drop() {
    s.featureFailed = false;
    s.fRenderW = s.fRenderH = s.fDisplayW = s.fDisplayH = 0;
    s.fQuality = s.fPreset = ~0u;
    if (s.feature || sh.image.handle || sh.pipe) { WaitIdle(); ReleaseFeature(); DestroySharp(); }
    sh.failed = false;
    if (s.lastInfo[0] && std::strcmp(s.lastInfo, "off")) Status("off");
}
void Shutdown() {
    Flush();
    const context::Handles* h = context::Get();
    ReleaseFeature();
    DestroySharp();
    if (s.params) NVSDK_NGX_VULKAN_DestroyParameters(s.params);
    s.params = nullptr;
    if (s.started && h && h->device) { const NVSDK_NGX_Result rc = NVSDK_NGX_VULKAN_Shutdown1(h->device); char t[200]; Logf("NGX shut down (%s)", ResultText(rc, t, sizeof t)); }
    Flush();
    CVar *w = s.want, *sp = s.sharp, *pr = s.preset, *fl = s.flip, *lg = s.logLevel, *in = s.info;
    s = State{};
    s.want = w; s.sharp = sp; s.preset = pr; s.flip = fl; s.logLevel = lg; s.info = in;
}
}
#endif
