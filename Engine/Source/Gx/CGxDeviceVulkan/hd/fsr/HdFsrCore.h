// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <windows.h>
#include <api/include/ffx_api.h>
#include <api/include/vk/ffx_api_vk.h>
#include <upscalers/include/ffx_upscale.h>
#include <framegeneration/include/ffx_framegeneration.h>
#include <framegeneration/include/vk/ffx_api_framegeneration_vk.h>

namespace HD::Fsr {
// The SDK owns its internal resources. The caller owns every external image, all barriers,
// submission/fences, and the engine's binding-cache invalidation after any SDK dispatch.
// This class is immovable: AMD requires create-description pointers to remain alive.
// CloseAfterIdle MUST be called before destroying VkDevice; no GPU work at DLL detach.
struct Config {
    VkDeviceContext device{};
    FfxApiDimensions2D render{}, display{};
    bool invertedDepth=false, infiniteDepth=false, hdr=false, nonLinearSrgb=false;
    uint32_t backBufferFormat=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    ffxApiMessage message=nullptr;
};
struct Frame {
    uint64_t id=0;
    VkCommandBuffer cmd=VK_NULL_HANDLE;
    FfxApiDimensions2D render{};
    FfxApiResource color{},depth{},motion{},reactive{},composition{},exposure{},output{};
    FfxApiFloatCoords2D jitter{}; // actual render pixel offset, in the FSR convention; not NDC
    FfxApiFloatCoords2D motionScale{1,1}; // previousUV-currentUV times render extent, or pixel MV with 1,1
    float milliseconds=16.6667f, preExposure=1, nearPlane=.1f, farPlane=1000;
    float verticalFov=1, metersPerUnit=.9144f, sharpness=.2f;
    float position[3]{},up[3]{0,1,0},right[3]{1,0,0},forward[3]{0,0,-1};
    bool reactiveMasksReady=false; // R1: set only after both current-frame masks were filled
    bool reset=false, sharpen=true;
    bool completeMotion=false; // camera + real previous object/bone/wind positions; never claim true for camera-only MV
    bool cameraOnlyTest=false; // explicit hdFSRTest mode: camera-only MV accepted, reported as a test, never as complete
};
struct Versions {
    std::array<uint64_t,16> ids{};
    std::array<const char*,16> names{};
    uint64_t count=0;
};
class Core {
public:
    Core()=default;
    ~Core()=default; // deliberately no Vulkan / DLL operations during static or DLL destruction
    Core(const Core&)=delete; Core& operator=(const Core&)=delete;
    bool Open(const wchar_t* absoluteRuntimeDirectory);
    ffxReturnCode_t Enumerate(uint64_t createType,const VkDeviceContext&,Versions&);
    ffxReturnCode_t CreateUpscale(const Config&);
    ffxReturnCode_t CreateFrameGeneration(const Config&);
    // Caller has drained the GPU work that used it; configured disabled first when a swapchain still presents.
    ffxReturnCode_t DestroyFrameGeneration();
    bool HasGeneration()const{return fgContext_!=nullptr;}
    // Caller has drained the GPU work that used the context (resize, quality change, FSR turned off).
    ffxReturnCode_t DestroyUpscale();
    bool HasUpscale()const{return upContext_!=nullptr;}
    ffxReturnCode_t Jitter(uint32_t index,uint32_t renderWidth,uint32_t displayWidth,FfxApiFloatCoords2D&);
    ffxReturnCode_t Resolution(uint32_t width,uint32_t height,uint32_t quality,FfxApiDimensions2D&);
    ffxReturnCode_t RecordUpscale(const Frame&);
    // Aetheria F3a: the SDK's own reactive-mask generator (opaque-only colour vs the finished colour, both at render size; `out` is R8, UAV state).
    ffxReturnCode_t RecordReactive(VkCommandBuffer cmd,const FfxApiResource& opaqueOnly,const FfxApiResource& preUpscale,const FfxApiResource& out,FfxApiDimensions2D render);
    ffxReturnCode_t ConfigureGeneration(const ffxConfigureDescFrameGeneration&);
    ffxReturnCode_t RecordPrepare(const Frame&);
    ffxReturnCode_t RecordGenerated(const ffxDispatchDescFrameGeneration&);
    static ffxReturnCode_t GenerateCallback(ffxDispatchDescFrameGeneration*,void*);
    ffxReturnCode_t CreateSwapchain(ffxCreateContextDescFrameGenerationSwapChainVK&);
    ffxReturnCode_t SwapchainFunctions(ffxQueryDescSwapchainReplacementFunctionsVK&);
    ffxReturnCode_t RegisterUi(const FfxApiResource&,uint32_t flags);
    ffxReturnCode_t WaitForPresents();
    // Device idle: the presents drained, the generation context, the swapchain context, then the replacement
    // VkSwapchainKHR itself (AMD leaves it alive after DestroyContext). Nothing left to destroy is not an error.
    ffxReturnCode_t DestroySwapchain();
    bool HasSwapchain()const{return swapContext_!=nullptr;}
    VkSwapchainKHR Swapchain()const{return swapHandle_;}
    const ffxQueryDescSwapchainReplacementFunctionsVK& SwapchainEntryPoints()const{return swapFunctions_;}
    // Presents to the real swapchain since it was made (rendered and generated together); 0 without one.
    uint64_t PresentCount()const;
    // Caller must drain submitted game/FG work before CloseAfterIdle. WaitForPresents alone
    // does not drain the game's queue. Recreate contexts on any flag/size/device change.
    ffxReturnCode_t CloseAfterIdle();
    const char* UpscalerName()const{return upName_.data();}
    const char* GeneratorName()const{return fgName_.data();}
    const char* Error()const;
    bool IsOpen()const{return module_!=nullptr;}
private:
    struct Persistent {
        Config config{};
        ffxCreateBackendVKDesc backend{};
        ffxOverrideVersion choice{};
        ffxCreateContextDescUpscaleVersion upVersion{};
        ffxCreateContextDescFrameGenerationVersion fgVersion{};
        ffxCreateContextDescUpscale upscale{};
        ffxCreateContextDescFrameGeneration generation{};
    }up_,fg_;
    HMODULE module_=nullptr;
    PfnFfxCreateContext create_=nullptr;PfnFfxDestroyContext destroy_=nullptr;
    PfnFfxQuery query_=nullptr;PfnFfxDispatch dispatch_=nullptr;PfnFfxConfigure configure_=nullptr;
    ffxContext upContext_=nullptr,fgContext_=nullptr,swapContext_=nullptr;
    ffxCreateContextDescFrameGenerationSwapChainVK swapDesc_{};
    ffxCreateContextDescFrameGenerationSwapChainVersionVK swapVersion_{};
    ffxQueryDescSwapchainReplacementFunctionsVK swapFunctions_{};
    VkSwapchainKHR swapHandle_=VK_NULL_HANDLE; // the replacement swapchain; swapDesc_.swapchain points here
    std::array<char,160>upName_{},fgName_{},error_{};
    mutable std::mutex errorMutex_;
    uint64_t upId_=0,preparedId_=0;bool upSeen_=false,prepared_=false;
    ffxReturnCode_t SelectAnalytical(uint64_t,const VkDeviceContext&,uint64_t&,char*,size_t);
    ffxReturnCode_t Fail(const char*,ffxReturnCode_t=FFX_API_RETURN_ERROR_PARAMETER);
    bool Valid(const Frame&,const Config&,bool exactRender)const;
};
// Resource helper uses VkImage, NEVER casts a VkImageView to VkImage.
inline FfxApiResource Resource(VkImage image,const VkImageCreateInfo& info,uint32_t state) {
    return ffxApiGetResourceVK(reinterpret_cast<void*>(image),ffxApiGetImageResourceDescriptionVK(image,info,0),state);
}
}
