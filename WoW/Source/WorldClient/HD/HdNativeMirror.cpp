#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdNativeWaterService.h"
#include "WoW/Source/WorldClient/HD/HdNativeMirror.h"
#include "WoW/Source/WorldClient/HD/HdForeverWater.h"
#include "WoW/Source/WorldClient/HD/HdNativeScene.h"
#include "WoW/Source/WorldClient/HD/HdNativeWaterPlane.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdMirrorPass.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/GxSeam.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
namespace HD::NativeMirror {
namespace {
CVar* info=nullptr;
void Info(const char* value){if(info)info->Set(value,true,false,false,false);}
}
void BeforeLiquids(){
    namespace mirror=gxvk::vk::hdmirror;
    mirror::Invalidate();
    if(!g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi()!=GxApi_Vulkan)return;
    if(!info)info=CVar::Register("hdMirrorInfo","Native mirror target status; not water material readiness",0,"off",nullptr,CATEGORY_GRAPHICS,false,nullptr,false); if (info) info->m_flags &= ~0x1u; /* status only: not archived to Config.wtf */
    const auto* wanted=CVar::LookupRegistered("hdMirrorCapture");
    const auto* water=CVar::LookupRegistered("hdCataWater");
    static CVar* forever=nullptr; if(!forever)forever=CVar::Register("hdWaterMirror","HD: also render the planar mirror pass for Forever water (needs the VulkanShaders\\mirror variants; off by default)",1,"0",nullptr,CATEGORY_GRAPHICS,false,nullptr,false);
    const bool foreverMirror=forever && forever->m_intValue!=0 && HD::ForeverWater::Requested();
    if((!wanted || !wanted->m_intValue) && (!water || !water->m_intValue) && !foreverMirror){Info("off");return;}
    float height;
    NativeScene::Snapshot snapshot;
    if(!NativeWaterPlane::Get(height) || !NativeScene::Get(gxvk::vk::frame::Serial(),snapshot)){
        Info("no same-frame plane/camera");return;
    }
    const auto& fog=DayNight::g_dnInfo.m_fog.m_color;
    const float background[4]={fog.r/255.0f,fog.g/255.0f,fog.b/255.0f,1};
    const bool ok=mirror::Render(snapshot.constants.view,snapshot.constants.projection,height,g_gxCataPlane.m_eyeZ,HD::NativeWaterService::SkyImage(),background);
    Info(ok?"reflection target ready; material service decides availability":"reflection unavailable; original water retained");
}
void Shutdown(){gxvk::vk::hdmirror::Shutdown();}
}
