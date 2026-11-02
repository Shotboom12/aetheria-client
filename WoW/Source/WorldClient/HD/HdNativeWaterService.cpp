#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/WorldClient/HD/HdNativeRipples.h"
#include "WoW/Source/WorldClient/HD/HdNativeWaterService.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/HD/HdCataWater.h"
#include "WoW/Source/WorldClient/HD/HdForeverWater.h"
#include "WoW/Source/WorldClient/HD/Forever/HdForeverFieldOwner.h"
#include "WoW/Source/WorldClient/HD/HdNativeWaterCapture.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdMirrorPass.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdScene.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/GxSeam.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
namespace HD::NativeWaterService {
namespace {
CVar* wanted=nullptr;
int32_t lastMap=-1;bool wideCapture=false;
void Clear(){g_gxCataWater.m_ready=g_gxCataWater.m_mirror=g_gxCataWater.m_sky=0;}
bool Sampled(const gxvk::vk::texture::Image* image){
    return image && image->handle && image->view && image->id && image->layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
}
void BeginWorld(){
    Clear();
    const auto map=World::QueryMapId();if(map!=lastMap){g_gxCataPlane.m_valid=0;lastMap=map;}
    HD::ForeverNative::FieldOwner::InvalidateMap(map,gxvk::vk::frame::Completed());
    g_gxCataSky.m_tex=nullptr; // Each world scope must produce its own sky; never borrow a previous frame.
    if(!wanted){
        wanted=CVar::LookupRegistered("hdCataWater");
        if(!wanted)wanted=CVar::Register("hdCataWater","Cataclysm water material with native capture, reflection and ripple services",1,"0",nullptr,CATEGORY_GRAPHICS,false,nullptr,false);
    }
    const bool remastered = Visuals::Current() == Visuals::AzerothRemastered;
    const bool foreverRequested = remastered && HD::ForeverWater::Requested();
    const bool cataRequested = remastered && wanted && wanted->m_intValue && !foreverRequested;
    const bool servicesRequested = remastered && (cataRequested || foreverRequested);
    g_gxCataWater.m_enabled=cataRequested && g_theGxDevicePtr &&
        g_theGxDevicePtr->DeviceApi()==GxApi_Vulkan && !gxvk::vk::frame::Target() &&
        !gxvk::vk::frame::QueryActive() && gxvk::vk::hdscene::Supported();
    const auto* diagnostic=CVar::LookupRegistered("hdMirrorCapture");
    const bool primary=g_theGxDevicePtr && g_theGxDevicePtr->DeviceApi()==GxApi_Vulkan && !gxvk::vk::frame::Target();
    g_gxCataPlane.m_wanted=primary && (servicesRequested || (diagnostic && diagnostic->m_intValue));
    wideCapture=g_gxCataPlane.m_wanted && !CWorldScene::s_viewerMapObjDef;
    // Native ripple readiness decides whether unit rings are replaced; failures keep the base material.
    NativeRipples::BeginWorld(g_gxCataWater.m_enabled!=0);
}
void BeforeLiquids(){if(g_gxCataWater.m_enabled){HD::CataWaterBeginLiquidPass();NativeRipples::BeforeLiquids();}}
void AfterServices(){
    Clear();
    if(!g_gxCataWater.m_enabled && !HD::ForeverWater::Requested())return;
    const auto* color=NativeWaterCapture::SampledImage(0);
    const auto* depth=NativeWaterCapture::SampledImage(1);
    if(!Sampled(color) || !Sampled(depth))return;
    g_gxCataWater.m_ready=1;
    g_gxCataWater.m_sky=Sampled(SkyImage())?1:0;
    // No-sky outdoor mirrors use the actual world fog colour; interiors retain the fallback path.
    g_gxCataWater.m_mirror=wideCapture && Sampled(gxvk::vk::hdmirror::Get())?1:0;
}
void BeforeUi(){Clear();}
const gxvk::vk::texture::Image* MirrorImage(){
    const auto* image=gxvk::vk::hdmirror::Get();
    return g_gxCataWater.m_mirror && image && image->handle && image->view && image->id &&
        image->layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? image : nullptr;
}
void Shutdown(){HD::ForeverWater::Shutdown();NativeRipples::Shutdown();Clear();g_gxCataPlane.m_wanted=g_gxCataPlane.m_valid=0;wideCapture=false;lastMap=-1;g_gxCataWater.m_enabled=0;g_gxCataRipples.m_active=g_gxCataRipples.m_ready=0;g_gxCataSky.m_tex=nullptr;}
}
