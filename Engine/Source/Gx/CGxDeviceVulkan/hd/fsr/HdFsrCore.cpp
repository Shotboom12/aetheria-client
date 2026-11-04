// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef WOWDECOMP_FSR
#include "HdFsrCore.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
namespace HD::Fsr {
namespace {
bool ImageAtLeast(const FfxApiResource& r,uint32_t w,uint32_t h){
    return r.resource&&r.description.type==FFX_API_RESOURCE_TYPE_TEXTURE2D&&r.description.width>=w&&r.description.height>=h;
}
}
ffxReturnCode_t Core::Fail(const char* msg,ffxReturnCode_t code){std::lock_guard<std::mutex> guard(errorMutex_);std::snprintf(error_.data(),error_.size(),"%s (FFX %u)",msg,code);return code;}
const char* Core::Error()const{thread_local std::array<char,160>copy{};std::lock_guard<std::mutex> guard(errorMutex_);copy=error_;return copy.data();}
bool Core::Open(const wchar_t* dir){
    if(module_||!dir)return false;
    const size_t n=std::wcslen(dir);if(n<3||n>3000||!((dir[1]==L':'&&(dir[2]==L'\\'||dir[2]==L'/'))||(dir[0]==L'\\'&&dir[1]==L'\\')))return false;
    wchar_t path[3072]{};if(swprintf_s(path,L"%s\\amd_fidelityfx_loader_vk.dll",dir)<0)return false;
    module_=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module_){Fail("FSR loader missing",FFX_API_RETURN_NO_PROVIDER);return false;}
#define FSR_PROC(member,type,name) member=reinterpret_cast<type>(GetProcAddress(module_,name));
    FSR_PROC(create_,PfnFfxCreateContext,"ffxCreateContext") FSR_PROC(destroy_,PfnFfxDestroyContext,"ffxDestroyContext")
    FSR_PROC(query_,PfnFfxQuery,"ffxQuery") FSR_PROC(dispatch_,PfnFfxDispatch,"ffxDispatch") FSR_PROC(configure_,PfnFfxConfigure,"ffxConfigure")
#undef FSR_PROC
    if(!create_||!destroy_||!query_||!dispatch_||!configure_){FreeLibrary(module_);module_=nullptr;Fail("Incomplete FSR exports");return false;}
    return true;
}
ffxReturnCode_t Core::Enumerate(uint64_t type,const VkDeviceContext& device,Versions& out){
    if(!module_||!device.vkDevice||!device.vkPhysicalDevice||!device.vkDeviceProcAddr)return Fail("Invalid Vulkan device");
    out={};out.count=out.ids.size();auto copy=device;
    ffxQueryDescGetVersions q{};q.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;q.createDescType=type;q.device=&copy;
    q.outputCount=&out.count;q.versionIds=out.ids.data();q.versionNames=out.names.data();
    auto rc=query_(nullptr,&q.header);if(rc||out.count>out.ids.size())return Fail("Version enumeration",rc?rc:FFX_API_RETURN_ERROR_PARAMETER);return rc;
}
ffxReturnCode_t Core::SelectAnalytical(uint64_t type,const VkDeviceContext& d,uint64_t& id,char* name,size_t n){
    Versions v;auto rc=Enumerate(type,d,v);if(rc)return rc;
    for(uint64_t i=0;i<v.count;++i)if(v.names[i]&&std::strstr(v.names[i],"3.1.")){
        id=v.ids[i];std::snprintf(name,n,"%s",v.names[i]);return FFX_API_RETURN_OK;}
    return Fail("Analytical FSR 3.1 provider unavailable",FFX_API_RETURN_NO_PROVIDER);
}
ffxReturnCode_t Core::CreateUpscale(const Config& c){
    if(upContext_||!c.render.width||!c.render.height||c.render.width>c.display.width||c.render.height>c.display.height)return Fail("Upscale dimensions/context");
    up_.config=c;auto rc=SelectAnalytical(FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE,c.device,up_.choice.versionId,upName_.data(),upName_.size());if(rc)return rc;
    up_.backend.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK,nullptr};up_.backend.deviceContext=c.device;
    up_.upVersion.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION,&up_.backend.header};up_.upVersion.version=FFX_UPSCALER_VERSION;
    up_.choice.header={FFX_API_DESC_TYPE_OVERRIDE_VERSION,&up_.upVersion.header};
    auto& d=up_.upscale;d={};d.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE,&up_.choice.header};d.maxRenderSize=c.render;d.maxUpscaleSize=c.display;d.fpMessage=c.message;
    d.flags=FFX_UPSCALE_ENABLE_AUTO_EXPOSURE|(c.message?FFX_UPSCALE_ENABLE_DEBUG_CHECKING:0)|
      (c.invertedDepth?FFX_UPSCALE_ENABLE_DEPTH_INVERTED:0)|(c.infiniteDepth?FFX_UPSCALE_ENABLE_DEPTH_INFINITE:0)|
      (c.hdr?FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE:0)|(c.nonLinearSrgb?FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE:0);
    rc=create_(&upContext_,&d.header,nullptr);if(rc)return Fail("Create upscale",rc);upSeen_=false;return rc;
}
ffxReturnCode_t Core::DestroyUpscale(){
    if(!upContext_)return FFX_API_RETURN_OK;
    auto rc=destroy_(&upContext_,nullptr);if(rc)return Fail("Destroy upscale",rc);upContext_=nullptr;upSeen_=false;return rc;
}
ffxReturnCode_t Core::CreateFrameGeneration(const Config& c){
    if(fgContext_||!c.render.width||!c.render.height||c.render.width>c.display.width||c.render.height>c.display.height)return Fail("FG dimensions/context");
    fg_.config=c;auto rc=SelectAnalytical(FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,c.device,fg_.choice.versionId,fgName_.data(),fgName_.size());if(rc)return rc;
    fg_.backend.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK,nullptr};fg_.backend.deviceContext=c.device;
    fg_.fgVersion.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION,&fg_.backend.header};fg_.fgVersion.version=FFX_FRAMEGENERATION_VERSION;
    fg_.choice.header={FFX_API_DESC_TYPE_OVERRIDE_VERSION,&fg_.fgVersion.header};
    auto& d=fg_.generation;d={};d.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,&fg_.choice.header};
    d.displaySize=c.display;d.maxRenderSize=c.render;d.backBufferFormat=c.backBufferFormat;
    d.flags=(c.message?FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING:0)|(c.invertedDepth?FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED:0)|
      (c.infiniteDepth?FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE:0)|(c.hdr?FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE:0);
    rc=create_(&fgContext_,&d.header,nullptr);if(rc){fgContext_=nullptr;return Fail("Create FG",rc);}prepared_=false;
    if(c.message){
        // The generator's create description has no message member: its debug checks report through the global one.
        ffxConfigureDescGlobalDebug g{};g.header.type=FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG;g.effectId=FFX_API_EFFECT_ID_FRAMEGENERATION;
        g.fpMessage=c.message;g.debugLevel=FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS;
        if(configure_(&fgContext_,&g.header))Fail("FG debug messages not routed");
    }
    return FFX_API_RETURN_OK;
}
ffxReturnCode_t Core::DestroyFrameGeneration(){
    if(!fgContext_)return FFX_API_RETURN_OK;
    if(swapContext_&&swapHandle_){
        // AMD's order: generation disabled on the swapchain (it then presents the back buffer alone), then destroyed.
        ffxConfigureDescFrameGeneration off{};off.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        off.swapChain=reinterpret_cast<void*>(swapHandle_);off.frameGenerationEnabled=false;
        configure_(&fgContext_,&off.header);
        WaitForPresents();
    }
    auto rc=destroy_(&fgContext_,nullptr);if(rc)return Fail("Destroy FG",rc);fgContext_=nullptr;prepared_=false;return rc;
}
ffxReturnCode_t Core::Resolution(uint32_t w,uint32_t h,uint32_t quality,FfxApiDimensions2D& out){
    if(!module_||quality>4||!w||!h)return Fail("Quality/dimensions");
    ffxQueryDescUpscaleGetRenderResolutionFromQualityMode q{};q.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
    q.displayWidth=w;q.displayHeight=h;q.qualityMode=quality;q.pOutRenderWidth=&out.width;q.pOutRenderHeight=&out.height;return query_(nullptr,&q.header);
}
ffxReturnCode_t Core::Jitter(uint32_t index,uint32_t rw,uint32_t dw,FfxApiFloatCoords2D& out){
    if(!module_||!rw||dw<rw)return Fail("Jitter dimensions");
    int32_t count=0;ffxQueryDescUpscaleGetJitterPhaseCount a{};a.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
    a.renderWidth=rw;a.displayWidth=dw;a.pOutPhaseCount=&count;auto rc=query_(upContext_?&upContext_:nullptr,&a.header);if(rc||count<=0)return Fail("Jitter phases",rc?rc:FFX_API_RETURN_ERROR_PARAMETER);
    ffxQueryDescUpscaleGetJitterOffset b{};b.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;b.index=int32_t(index%uint32_t(count));b.phaseCount=count;b.pOutX=&out.x;b.pOutY=&out.y;
    return query_(upContext_?&upContext_:nullptr,&b.header);
}
bool Core::Valid(const Frame& f,const Config& c,bool exactRender)const{
    // The upscaler's context is made for one render size; the generator's for a largest one (its display).
    const bool size=exactRender?f.render.width==c.render.width&&f.render.height==c.render.height
                               :f.render.width<=c.render.width&&f.render.height<=c.render.height;
    const bool anyMask=f.reactive.resource||f.composition.resource||f.reactiveMasksReady;
    const auto maskValid=[&](const FfxApiResource&r){return ImageAtLeast(r,f.render.width,f.render.height)&&r.description.width==f.render.width&&r.description.height==f.render.height&&r.description.format==FFX_API_SURFACE_FORMAT_R8_UNORM&&r.state==FFX_API_RESOURCE_STATE_COMPUTE_READ&&r.resource!=f.color.resource&&r.resource!=f.output.resource&&r.resource!=f.depth.resource&&r.resource!=f.motion.resource;};
    if(anyMask&&(!f.reactiveMasksReady||!maskValid(f.reactive)||!maskValid(f.composition)||f.reactive.resource==f.composition.resource))return false;
    return (f.completeMotion||f.cameraOnlyTest)&&f.cmd&&ImageAtLeast(f.depth,f.render.width,f.render.height)&&ImageAtLeast(f.motion,f.render.width,f.render.height)&&f.render.width&&f.render.height&&
      size&&std::isfinite(f.milliseconds)&&f.milliseconds>0&&
      std::isfinite(f.preExposure)&&f.preExposure>0&&std::isfinite(f.nearPlane)&&f.nearPlane>0&&
      ((c.infiniteDepth&&std::isinf(f.farPlane))||(std::isfinite(f.farPlane)&&f.farPlane>f.nearPlane))&&
      std::isfinite(f.verticalFov)&&f.verticalFov>0&&f.verticalFov<3.141593f&&
      std::isfinite(f.metersPerUnit)&&f.metersPerUnit>0&&std::isfinite(f.jitter.x)&&std::isfinite(f.jitter.y)&&
      std::isfinite(f.motionScale.x)&&std::isfinite(f.motionScale.y)&&f.motionScale.x!=0&&f.motionScale.y!=0&&
      std::isfinite(f.sharpness)&&f.sharpness>=0&&f.sharpness<=1;
}
ffxReturnCode_t Core::RecordUpscale(const Frame& f){
    if(!upContext_||!Valid(f,up_.config,true)||!ImageAtLeast(f.color,f.render.width,f.render.height)||!ImageAtLeast(f.output,up_.config.display.width,up_.config.display.height)||f.color.resource==f.output.resource)return Fail("Incomplete upscale input");
    ffxDispatchDescUpscale d{};d.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;d.commandList=f.cmd;
    d.color=f.color;d.depth=f.depth;d.motionVectors=f.motion;d.exposure=f.exposure;d.reactive=f.reactive;d.transparencyAndComposition=f.composition;d.output=f.output;
    d.jitterOffset=f.jitter;d.motionVectorScale=f.motionScale;d.renderSize=f.render;d.upscaleSize=up_.config.display;
    d.enableSharpening=f.sharpen;d.sharpness=f.sharpness;d.frameTimeDelta=f.milliseconds;d.preExposure=f.preExposure;
    d.reset=f.reset||!upSeen_||f.id!=upId_+1;d.cameraNear=f.nearPlane;d.cameraFar=f.farPlane;d.cameraFovAngleVertical=f.verticalFov;
    d.viewSpaceToMetersFactor=f.metersPerUnit;d.flags=up_.config.nonLinearSrgb?FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB:0;
    auto rc=dispatch_(&upContext_,&d.header);if(rc)return Fail("Dispatch upscale",rc);upId_=f.id;upSeen_=true;return rc;
}
ffxReturnCode_t Core::RecordReactive(VkCommandBuffer cmd,const FfxApiResource& opaqueOnly,const FfxApiResource& preUpscale,const FfxApiResource& out,FfxApiDimensions2D render){
    if(!upContext_||!cmd||!opaqueOnly.resource||!preUpscale.resource||!out.resource||!render.width||!render.height)return Fail("Reactive mask inputs");
    ffxDispatchDescUpscaleGenerateReactiveMask d{};d.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE_GENERATEREACTIVEMASK;d.commandList=cmd;
    d.colorOpaqueOnly=opaqueOnly;d.colorPreUpscale=preUpscale;d.outReactive=out;d.renderSize=render;
    d.scale=1.0f;d.cutoffThreshold=0.2f;d.binaryValue=1.0f; // AMD's documented starting values
    d.flags=FFX_UPSCALE_AUTOREACTIVEFLAGS_APPLY_TONEMAP|FFX_UPSCALE_AUTOREACTIVEFLAGS_APPLY_INVERSETONEMAP|FFX_UPSCALE_AUTOREACTIVEFLAGS_APPLY_THRESHOLD|FFX_UPSCALE_AUTOREACTIVEFLAGS_USE_COMPONENTS_MAX;
    auto rc=dispatch_(&upContext_,&d.header);return rc?Fail("Generate reactive mask",rc):rc;
}
ffxReturnCode_t Core::ConfigureGeneration(const ffxConfigureDescFrameGeneration& d){
    if(!fgContext_||d.header.type!=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION)return Fail("FG configuration");
    if(d.frameGenerationEnabled&&!d.swapChain&&!(d.flags&FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY))return Fail("FG needs pacing swapchain");
    auto rc=configure_(&fgContext_,&d.header);return rc?Fail("Configure FG",rc):rc;
}
ffxReturnCode_t Core::RecordPrepare(const Frame& f){
    if(!fgContext_||!Valid(f,fg_.config,false))return Fail("Incomplete FG preparation");
    ffxDispatchDescFrameGenerationPrepareV2 d{};d.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    d.frameID=f.id;d.commandList=f.cmd;d.renderSize=f.render;d.jitterOffset=f.jitter;d.motionVectorScale=f.motionScale;
    d.frameTimeDelta=f.milliseconds;d.reset=f.reset||!prepared_||f.id!=preparedId_+1;d.cameraNear=f.nearPlane;d.cameraFar=f.farPlane;
    d.cameraFovAngleVertical=f.verticalFov;d.viewSpaceToMetersFactor=f.metersPerUnit;d.depth=f.depth;d.motionVectors=f.motion;
    std::copy_n(f.position,3,d.cameraPosition);std::copy_n(f.up,3,d.cameraUp);std::copy_n(f.right,3,d.cameraRight);std::copy_n(f.forward,3,d.cameraForward);
    auto rc=dispatch_(&fgContext_,&d.header);if(rc)return Fail("Prepare FG",rc);preparedId_=f.id;prepared_=true;return rc;
}
ffxReturnCode_t Core::RecordGenerated(const ffxDispatchDescFrameGeneration& d){
    // A pacing-thread callback may consume frame N after the render thread prepared N+1.
    // Do not read/compare the render-thread-only preparedId_ here. SDK frame IDs own that selection.
    if(!fgContext_||d.header.type!=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION||!d.commandList||d.numGeneratedFrames!=1||!ImageAtLeast(d.presentColor,fg_.config.display.width,fg_.config.display.height)||!ImageAtLeast(d.outputs[0],fg_.config.display.width,fg_.config.display.height)||d.presentColor.resource==d.outputs[0].resource)return Fail("Incomplete FG dispatch");
    auto rc=dispatch_(&fgContext_,&d.header);return rc?Fail("Generate frame",rc):rc;
}
ffxReturnCode_t Core::GenerateCallback(ffxDispatchDescFrameGeneration* d,void* p){
    return d&&p?static_cast<Core*>(p)->RecordGenerated(*d):FFX_API_RETURN_ERROR_PARAMETER;
}
ffxReturnCode_t Core::CreateSwapchain(ffxCreateContextDescFrameGenerationSwapChainVK& d){
    if(!module_||swapContext_||swapHandle_||!d.swapchain||!d.createInfo.surface)return Fail("FG swapchain description");
    if(d.createInfo.oldSwapchain&&d.createInfo.oldSwapchain!=*d.swapchain)return Fail("FG swapchain: oldSwapchain differs");
    swapDesc_=d;swapHandle_=*d.swapchain;swapDesc_.swapchain=&swapHandle_;   // our storage: it outlives the context
    swapVersion_.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_VK,swapDesc_.header.pNext};
    swapVersion_.version=FFX_FRAMEGENERATION_SWAPCHAIN_VK_VERSION;
    swapDesc_.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK,&swapVersion_.header};
    auto rc=create_(&swapContext_,&swapDesc_.header,nullptr);
    if(rc){swapContext_=nullptr;swapHandle_=VK_NULL_HANDLE;swapDesc_={};return Fail("Create FG swapchain",rc);}
    rc=SwapchainFunctions(swapFunctions_);
    if(rc||!swapFunctions_.pOutDestroySwapchainFFXAPI||!swapFunctions_.pOutGetSwapchainImagesKHR||!swapFunctions_.pOutAcquireNextImageKHR||
       !swapFunctions_.pOutQueuePresentKHR||!swapHandle_){
        const ffxReturnCode_t code=rc?rc:FFX_API_RETURN_ERROR_PARAMETER;DestroySwapchain();return Fail("Query FG swapchain functions",code);}
    *d.swapchain=swapHandle_;return rc;
}
ffxReturnCode_t Core::SwapchainFunctions(ffxQueryDescSwapchainReplacementFunctionsVK& q){
    if(!swapContext_)return Fail("No FG swapchain");q={};q.header.type=FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;return query_(&swapContext_,&q.header);
}
ffxReturnCode_t Core::RegisterUi(const FfxApiResource& image,uint32_t flags){
    if(!swapContext_)return Fail("No FG swapchain");ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceVK d{};
    d.header.type=FFX_API_CONFIGURE_DESC_TYPE_FGSWAPCHAIN_REGISTERUIRESOURCE_VK;d.uiResource=image;d.flags=flags;return configure_(&swapContext_,&d.header);
}
ffxReturnCode_t Core::WaitForPresents(){
    if(!swapContext_)return FFX_API_RETURN_OK;ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK d{};
    d.header.type=FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;return dispatch_(&swapContext_,&d.header);
}
uint64_t Core::PresentCount()const{
    return swapContext_&&swapHandle_&&swapFunctions_.pOutGetLastPresentCountFFXAPI?swapFunctions_.pOutGetLastPresentCountFFXAPI(swapHandle_):0;
}
ffxReturnCode_t Core::DestroySwapchain(){
    if(!module_)return FFX_API_RETURN_OK;
    ffxReturnCode_t first=FFX_API_RETURN_OK;
    auto keep=[&](ffxReturnCode_t rc,const char* what){if(rc&&!first){first=rc;Fail(what,rc);}};
    keep(WaitForPresents(),"Wait FG presents");
    keep(DestroyFrameGeneration(),"Destroy FG");
    if(swapContext_){keep(destroy_(&swapContext_,nullptr),"Destroy FG swapchain context");swapContext_=nullptr;}
    // AMD 2.4 explicitly leaves the replacement VkSwapchainKHR alive after DestroyContext.
    // Destroy that object BEFORE unloading the DLL containing its virtual methods. Its last
    // argument is unused by this provider (AMD's own sample passes null).
    if(swapHandle_&&swapFunctions_.pOutDestroySwapchainFFXAPI)
        swapFunctions_.pOutDestroySwapchainFFXAPI(swapDesc_.device,swapHandle_,swapDesc_.allocator,nullptr);
    else if(swapHandle_)keep(FFX_API_RETURN_ERROR_PARAMETER,"Missing FSR swapchain destructor");
    swapHandle_=VK_NULL_HANDLE;swapDesc_={};swapFunctions_={};
    return first;
}
ffxReturnCode_t Core::CloseAfterIdle(){
    if(!module_)return FFX_API_RETURN_OK;
    ffxReturnCode_t rc=DestroySwapchain();if(rc)return rc;
    for(auto* context:{&fgContext_,&upContext_})if(*context){rc=destroy_(context,nullptr);if(rc)return Fail("Destroy FSR context",rc);*context=nullptr;}
    FreeLibrary(module_);module_=nullptr;upSeen_=prepared_=false;return FFX_API_RETURN_OK;
}
}
#endif // WOWDECOMP_FSR
