// SPDX-License-Identifier: GPL-3.0-or-later
// Step A: capabilities and API. The runtime is opened from the executable's own folder, the providers are
// enumerated on the real VkDevice, the features the SDK's shaders need are checked as ENABLED (not merely
// offered), and the statuses hdFSRInfo / hdFSRFrameGenerationInfo report the first missing piece.
#ifdef WOWDECOMP_FSR
// The wxl headers first: vulkan/Vk.hpp defines VK_USE_PLATFORM_WIN32_KHR / VK_NO_PROTOTYPES before vulkan.h, and the
// FidelityFX headers include vulkan.h too -- included first they would leave the Win32 types out of every later file.
#include "vulkan/Context.hpp"
#include "vulkan/DeviceCaps.hpp"
#include "native/Present.hpp"
#include "vulkan/Loader.hpp"
#include "wxl/Script.hpp"

#include "HdFsrRuntime.h"
#include "HdFsrCore.h"
#include "HdFsrSettings.h"
#include "HdFsrPass.h"

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>

namespace HD::Fsr::Runtime {
namespace {
Core g_core;   // immovable: the SDK keeps pointers into its create descriptions
Readiness g_up, g_fg;
Decision g_applied;
bool g_started = false;
Options g_lastOptions{};
bool g_loggedOptions = false;
Upscale g_mode = Upscale::Off;
uint32_t g_lastTest = 0;
const char* g_failure = nullptr;   // the pass's last failure, kept until a request changes

// Step D: the FSR swapchain. Wanted while the generation test is asked for; made at the next swapchain create
// (a rebuild is requested when the wish and the swapchain differ), dropped the same way.
bool g_wantSwap = false;
bool g_askedRebuild = false;       // a rebuild was requested for the current wish, not answered yet
uint32_t g_askedGeneration = 0;    // the swapchain generation when it was requested
bool g_swapFailed = false;         // AMD's swapchain could not be made: not tried again until a request changes
bool g_generate = false;
const char* g_fgFailure = nullptr;
char g_fgRate[160]{};              // the generation's measured rates, once the pass reports them
double g_objectRate = -1.0;        // Q1: % of rendered frames with object motion, < 0 = M1 not asked
char g_upText[160]{};

// context::SetSwapchainReplacer: called by the context at every swapchain create / destroy (render thread).
bool ReplaceSwapchain(const VkSwapchainCreateInfoKHR& info, VkSwapchainKHR* out, wxlvk::vk::context::SwapchainFunctions& fns) {
    if (!g_wantSwap || g_swapFailed || !g_core.IsOpen()) return false;
    const wxlvk::vk::context::Handles* h = wxlvk::vk::context::Get();
    const wxlvk::vk::context::FrameGenQueues& q = wxlvk::vk::context::FgQueues();
    if (!h || !q.complete() || h->presentFamily != h->graphicsFamily) return false;
    ffxCreateContextDescFrameGenerationSwapChainVK d{};
    d.physicalDevice = h->physical;
    d.device = h->device;
    VkSwapchainKHR none = VK_NULL_HANDLE;   // nothing to replace: the context made no plain swapchain this time
    d.swapchain = &none;
    d.allocator = nullptr;
    d.createInfo = info;
    d.createInfo.oldSwapchain = VK_NULL_HANDLE;
    // The game presents on its own queue (wxl presents on presentQueue, the graphics queue here): the SDK records
    // the generation there, inside the present call, on this same thread.
    d.gameQueue = VkQueueInfoFFXAPI{h->presentQueue, h->presentFamily, nullptr};
    d.asyncComputeQueue = VkQueueInfoFFXAPI{q.asyncCompute.queue, q.asyncCompute.family, nullptr};
    d.presentQueue = VkQueueInfoFFXAPI{q.present.queue, q.present.family, nullptr};
    d.imageAcquireQueue = VkQueueInfoFFXAPI{q.imageAcquire.queue, q.imageAcquire.family, nullptr};
    if (g_core.CreateSwapchain(d)) {
        g_swapFailed = true;
        wxl::ScriptMgr::Log(WXL_LOG_ERROR, "fsr: AMD frame generation swapchain not created (%s): plain swapchain, generation off",
                            g_core.Error());
        return false;
    }
    const ffxQueryDescSwapchainReplacementFunctionsVK& f = g_core.SwapchainEntryPoints();
    fns.getImages = f.pOutGetSwapchainImagesKHR;
    fns.acquire = f.pOutAcquireNextImageKHR;
    fns.present = f.pOutQueuePresentKHR;
    *out = g_core.Swapchain();
    wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: AMD frame generation swapchain %ux%u created (game queue family %u, present %u.%u, "
                        "acquire %u.%u, async compute %u.%u)", info.imageExtent.width, info.imageExtent.height,
                        h->presentFamily, q.present.family, q.present.index, q.imageAcquire.family, q.imageAcquire.index,
                        q.asyncCompute.family, q.asyncCompute.index);
    return true;
}

void ReleaseSwapchain(VkSwapchainKHR) {
    Pass::DropGeneration();   // the pass's own state; the context itself goes with the swapchain below
    if (g_core.DestroySwapchain())
        wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: AMD frame generation swapchain destroy reported %s", g_core.Error());
    else
        wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: AMD frame generation swapchain destroyed");
}

void DrainSwapchain() {
    if (g_core.WaitForPresents())
        wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: waiting for the generation's presents failed (%s)", g_core.Error());
}

// The folder of the executable that is actually running, plus "\FSR". Never the working directory.
bool RuntimeFolder(wchar_t* out, size_t cap) {
    wchar_t exe[3072]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
    if (!n || n >= std::size(exe)) return false;
    wchar_t* slash = std::wcsrchr(exe, L'\\');
    if (!slash) return false;
    *slash = 0;
    return swprintf_s(out, cap, L"%s\\FSR", exe) > 0;
}

// The formats the upscaler and the generator make their working images in, with the usages they need.
bool FormatsUsable(const wxlvk::vk::context::Handles& h, char* missing, size_t cap) {
    struct Need { VkFormat format; VkFormatFeatureFlags flags; const char* name; };
    constexpr VkFormatFeatureFlags kRw = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    const Need needs[] = {
        { VK_FORMAT_R16G16B16A16_SFLOAT, kRw, "R16G16B16A16_SFLOAT" },
        { VK_FORMAT_R16G16B16A16_UNORM, kRw, "R16G16B16A16_UNORM" },
        { VK_FORMAT_R16G16_SFLOAT, kRw, "R16G16_SFLOAT" },
        { VK_FORMAT_R16_SFLOAT, kRw, "R16_SFLOAT" },
        { VK_FORMAT_R32_SFLOAT, kRw, "R32_SFLOAT" },
        { VK_FORMAT_R32_UINT, kRw, "R32_UINT" },
        { VK_FORMAT_R8_UNORM, kRw, "R8_UNORM" },
        { VK_FORMAT_R8G8_UNORM, kRw, "R8G8_UNORM" },
        { VK_FORMAT_B10G11R11_UFLOAT_PACK32, kRw, "B10G11R11_UFLOAT" },
        { VK_FORMAT_R8G8B8A8_UNORM, kRw, "R8G8B8A8_UNORM" },
    };
    missing[0] = 0;
    if (!h.iapi || !h.iapi->GetPhysicalDeviceFormatProperties) { std::snprintf(missing, cap, "format query unavailable"); return false; }
    bool ok = true;
    for (const Need& n : needs) {
        VkFormatProperties p{};
        h.iapi->GetPhysicalDeviceFormatProperties(h.physical, n.format, &p);
        if ((p.optimalTilingFeatures & n.flags) != n.flags) {
            ok = false;
            const size_t used = std::strlen(missing);
            if (used + 32 < cap) std::snprintf(missing + used, cap - used, "%s%s", used ? ", " : "", n.name);
        }
    }
    return ok;
}

void LogVersions(const char* what, const Versions& v, ffxReturnCode_t rc) {
    if (rc) { wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: %s providers could not be listed (FFX %u)", what, rc); return; }
    for (uint64_t i = 0; i < v.count && i < v.ids.size(); ++i)
        wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: %s provider %llu: id 0x%016llX \"%s\"", what, static_cast<unsigned long long>(i),
                            static_cast<unsigned long long>(v.ids[i]), v.names[i] ? v.names[i] : "?");
}

bool HasAnalytical31(const Versions& v) {
    for (uint64_t i = 0; i < v.count && i < v.ids.size(); ++i)
        if (v.names[i] && std::strstr(v.names[i], "3.1.")) return true;
    return false;
}
}

const Readiness& UpscaleReadiness() { return g_up; }
const Readiness& GenerationReadiness() { return g_fg; }
const Decision& Applied() { return g_applied; }
Upscale Mode() { return g_mode; }
Options Requested() { return g_lastOptions; }
Core* Sdk() { return g_core.IsOpen() ? &g_core : nullptr; }
bool Generate() { return g_generate; }

void NoteGenerationFailure(const char* why) {
    if (!why) return;
    g_fgFailure = why;
    g_applied.generate = false;
    g_applied.generationWhy = why;
    Settings::Publish(g_applied);
}

void NoteGenerationRate(double rendered, double shown) {
    char motion[64];
    if (g_objectRate >= 0) std::snprintf(motion, sizeof motion, "objets suivis %.0f %% des images", g_objectRate);
    else std::snprintf(motion, sizeof motion, "mouvement camera seul");
    std::snprintf(g_fgRate, sizeof g_fgRate,
                  "TEST FG: %.0f images/s rendues + %.0f generees = %.0f affichees (%s)", rendered,
                  shown > rendered ? shown - rendered : 0.0, shown, motion);
}

void NoteObjectMotion(double percent) { g_objectRate = percent; }

void NoteFailure(const char* why) {
    if (!why) return;
    g_failure = why;
    g_applied.upscale = false;
    g_applied.upscaleWhy = why;
    Settings::Publish(g_applied);
}

void Startup() {
    if (g_started) return;
    g_up = g_fg = Readiness{};
    const wxlvk::vk::context::Handles* h = wxlvk::vk::context::Get();
    if (!h || !h->device || !h->physical || !h->iapi || !h->iapi->GetDeviceProcAddr) {
        static bool said = false;   // Frame() asks again until the device exists; one line only
        if (!said) wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: no Vulkan device yet: both FSR options stay unavailable");
        said = true;
        return;
    }
    g_started = true;
    g_up.vulkan = g_fg.vulkan = true;

    // Features: what the device was CREATED with (Caps().enabled), never what the adapter merely offers.
    const wxlvk::vk::DeviceCaps& caps = wxlvk::vk::context::Caps();
    const bool derivatives = (caps.enabled & wxlvk::vk::kCapComputeDerivatives) != 0;
    const bool timeline = (caps.enabled & wxlvk::vk::kCapTimeline) != 0;
    const bool v12 = caps.deviceVersion >= VK_API_VERSION_1_2;
    // R16F / RG16F storage images: the SDK's working images and our motion vectors (HdFsrPrep.comp).
    const bool storageExt = (caps.enabled & wxlvk::vk::kCapStorageImageExt) != 0;
    char missing[256];
    const bool formats = FormatsUsable(*h, missing, sizeof missing);
    g_up.deviceFeatures = v12 && derivatives && storageExt && formats;
    g_fg.deviceFeatures = g_up.deviceFeatures && timeline;
    // Step B is in place (HdFsrPass.cpp, present WorldTarget): the world at the render size, the interface native,
    // the depth normalized from the viewport range. A frame where it cannot run says so (NoteFailure) and stays native.
    g_up.worldTarget = g_up.depth = g_up.nativeUi = true;
    g_fg.worldTarget = g_fg.depth = g_fg.nativeUi = true;
    // Step D: AMD's swapchain needs its three queues (made with the device, Logical.cpp) and the game presenting on
    // its graphics queue.
    g_fg.pacingSwapchain = wxlvk::vk::context::FgQueues().complete() && h->presentFamily == h->graphicsFamily;
    wxl::ScriptMgr::Log(WXL_LOG_INFO,
                        "fsr: device Vulkan %u.%u, compute derivatives %s, timeline semaphores %s, subgroup size control %s, "
                        "FP16 %s, extended storage formats %s; working formats %s%s",
                        VK_API_VERSION_MAJOR(caps.deviceVersion), VK_API_VERSION_MINOR(caps.deviceVersion),
                        derivatives ? "enabled" : "NOT enabled", timeline ? "enabled" : "NOT enabled",
                        (caps.enabled & wxlvk::vk::kCapSubgroupSizeControl) ? "enabled" : "not enabled",
                        (caps.enabled & wxlvk::vk::kCapFloat16) ? "enabled" : "not enabled",
                        storageExt ? "enabled" : "NOT enabled", formats ? "all usable" : "missing: ", formats ? "" : missing);

    wchar_t folder[3100]{};
    if (!RuntimeFolder(folder, std::size(folder)) || !g_core.Open(folder)) {
        wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: runtime not loaded from the executable's FSR folder (%s): native rendering",
                            g_core.Error());
        return;
    }
    VkDeviceContext device{};
    device.vkDevice = h->device;
    device.vkPhysicalDevice = h->physical;
    device.vkDeviceProcAddr = h->iapi->GetDeviceProcAddr;
    Versions up, fg;
    const ffxReturnCode_t upRc = g_core.Enumerate(FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, device, up);
    LogVersions("upscaler", up, upRc);
    const ffxReturnCode_t fgRc = g_core.Enumerate(FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION, device, fg);
    LogVersions("frame generation", fg, fgRc);
    g_up.runtime = !upRc && HasAnalytical31(up);
    g_fg.runtime = !fgRc && HasAnalytical31(fg);
    wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: analytical FSR 3.1 upscaler %s, frame generation %s (FSR 4 ML is not used)",
                        g_up.runtime ? "found" : "NOT found", g_fg.runtime ? "found" : "NOT found");
    if (g_fg.runtime) wxlvk::vk::context::SetSwapchainReplacer(ReplaceSwapchain, ReleaseSwapchain, DrainSwapchain);
    wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: frame generation swapchain %s", g_fg.pacingSwapchain
                        ? "possible (its queues exist)" : "NOT possible: the device has no queues for it");
    // Still missing (their flags stay false, so Decide() keeps both options native outside hdFSRTest): complete
    // motion (objects, bones, wind), the reactive / composition masks, the pacing swapchain, the validation.
}

void NoteMasksReady(bool ready){g_up.reactiveMasks=g_fg.reactiveMasks=ready;}

void Frame() {
    NoteMasksReady(false);
    if (!g_started) Startup();
    const Options o = Settings::Read();
    const uint32_t test = Settings::TestMode();
    if (!g_loggedOptions || o.upscale != g_lastOptions.upscale || o.generate != g_lastOptions.generate ||
        o.quality != g_lastOptions.quality || test != g_lastTest || o.dlss != g_lastOptions.dlss) {
        g_loggedOptions = true;
        wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: requested upscaling %d (quality %u, sharpness %.2f), frame generation %d, test mode %u, upscaler %s",
                            o.upscale ? 1 : 0, o.quality, static_cast<double>(o.sharpness), o.generate ? 1 : 0, test, o.dlss ? "DLSS" : "FSR");
        g_failure = nullptr;   // a new request is tried again
        g_fgFailure = nullptr;
        g_swapFailed = false;
    }
    g_lastOptions = o;
    g_lastTest = test;
    // Each option is decided against its own provider's readiness; one never turns the other on.
    Options upOnly = o, fgOnly = o;
    upOnly.generate = false;
    fgOnly.upscale = false;
    const Decision du = Decide(upOnly, g_up), dg = Decide(fgOnly, g_fg);
    g_applied.upscale = du.upscale;
    g_applied.upscaleWhy = du.upscaleWhy;
    g_applied.generate = dg.generate;
    g_applied.generationWhy = dg.generationWhy;

    // What the world pass does. A test runs only while the upscaling box is ticked, and is named as a test.
    g_mode = du.upscale ? Upscale::Full : Upscale::Off;
    if (o.upscale && test) {
        if (test == 1 && g_up.vulkan && g_core.IsOpen()) {
            g_mode = Upscale::Stretch;
            g_applied.upscaleWhy = "TEST 1: cible monde + etirement simple (pas FSR)";
        } else if (test >= 2 && g_up.runtime && g_up.deviceFeatures) {
            g_mode = test == 2 ? Upscale::CameraOnly : Upscale::CameraOnlyFlipped;
            g_applied.upscaleWhy = test == 2 ? "TEST 2: FSR 3.1, mouvement camera seulement (objets animes: trainees attendues)"
                                             : "TEST 3: FSR 3.1, mouvement camera seulement, jitter inverse (diagnostic)";
            if (g_objectRate >= 0) {   // Q1: the measured truth, not the request
                std::snprintf(g_upText, sizeof g_upText, "TEST %u: FSR 3.1, mouvement camera + objets (suivis %.0f %% des images)%s",
                              test, g_objectRate, test == 3 ? ", jitter inverse" : "");
                g_applied.upscaleWhy = g_upText;
            }
        } else {
            g_applied.upscaleWhy = !g_core.IsOpen() || !g_up.runtime ? "TEST impossible: runtime FSR absent"
                                                                    : "TEST impossible: fonctions GPU indisponibles";
        }
    }
    // HD 2026-11-02 (DLSS): the upscaler's own status is hdDLSSInfo (DLSS, or FSR when DLSS cannot run).
    if (o.dlss && (g_mode == Upscale::CameraOnly || g_mode == Upscale::CameraOnlyFlipped))
        g_applied.upscaleWhy = "TEST 2: DLSS demande (etat dans hdDLSSInfo)";
    if (g_failure && g_mode != Upscale::Off) g_applied.upscaleWhy = g_failure;

    // Step D, as a test only (the motion is the camera's): the swapchain follows the wish, one rebuild per change.
    const bool fgTest = o.generate && test >= 2;
    g_wantSwap = fgTest && g_fg.runtime && g_fg.deviceFeatures && g_fg.pacingSwapchain && g_core.IsOpen() && !g_swapFailed;
    const bool have = wxlvk::vk::context::SwapchainReplaced();
    if (g_wantSwap != have) {
        const wxlvk::vk::context::Handles* now = wxlvk::vk::context::Get();
        const bool standing = now && now->swapchain != VK_NULL_HANDLE;   // minimised: no swapchain at all, wait
        if (g_askedRebuild && g_wantSwap && standing && wxlvk::vk::context::SwapchainGeneration() != g_askedGeneration) {
            // Rebuilt, and still the plain swapchain (its create logged why): no second attempt for this request.
            g_swapFailed = true;
            g_wantSwap = false;
        } else if (!g_askedRebuild) {
            g_askedRebuild = true;
            g_askedGeneration = wxlvk::vk::context::SwapchainGeneration();
            wxl::ScriptMgr::Log(WXL_LOG_INFO, "fsr: swapchain rebuilt %s AMD's frame generation swapchain",
                                g_wantSwap ? "through" : "without");
            wxlvk::native::present::RequestRebuild();
        }
    } else {
        g_askedRebuild = false;
    }
    g_generate = g_wantSwap && have;
    if (!g_generate) g_fgRate[0] = 0;
    if (o.generate && test) {
        const char* why = nullptr;
        if (test < 2) why = "TEST FG impossible: hdFSRTest 2 requis";
        else if (!g_core.IsOpen() || !g_fg.runtime) why = "TEST FG impossible: runtime FSR absent";
        else if (!g_fg.deviceFeatures) why = "TEST FG impossible: fonctions GPU indisponibles";
        else if (!g_fg.pacingSwapchain) why = "TEST FG impossible: pas de files GPU pour la presentation FSR";
        else if (g_swapFailed) why = "TEST FG impossible: swapchain FSR non creee (voir le journal)";
        else if (!have) why = "TEST FG: mise en place de la presentation FSR...";
        else if (g_fgFailure) why = g_fgFailure;
        else why = g_fgRate[0] ? g_fgRate : "TEST FG: generation d'images, mouvement camera seulement (objets animes: artefacts attendus)";
        g_applied.generate = false;   // a test is never reported as the real thing
        g_applied.generationWhy = why;
    }
    Settings::Publish(g_applied);
}

void Shutdown() {
    if (!g_started) return;
    g_started = false;
    // The SDK's contexts free resources the frames in flight may still use: the device is drained first.
    if (const wxlvk::vk::context::Handles* h = wxlvk::vk::context::Get())
        if (h->device && h->dapi) h->dapi->DeviceWaitIdle(h->device);
    Pass::Shutdown();
    // AMD's swapchain goes while its DLL is loaded: through the context, which puts its own entry points back.
    if (wxlvk::vk::context::SwapchainReplaced()) {
        wxlvk::vk::context::DropSwapchainNow();
        // The device itself stays for the process (a graphics restart comes back to it): the plain swapchain is
        // made again at the next present, as after a resize.
        wxlvk::native::present::RequestRebuild();
    }
    wxlvk::vk::context::SetSwapchainReplacer(nullptr, nullptr, nullptr);
    const ffxReturnCode_t rc = g_core.CloseAfterIdle();
    if (rc) wxl::ScriptMgr::Log(WXL_LOG_WARN, "fsr: runtime close failed (%s)", g_core.Error());
    g_up = g_fg = Readiness{};
    g_applied = Decision{};
    g_mode = Upscale::Off;
    g_failure = nullptr;
    g_wantSwap = g_askedRebuild = g_swapFailed = g_generate = false;
    g_fgFailure = nullptr;
    g_fgRate[0] = 0;
}
}
#endif // WOWDECOMP_FSR
