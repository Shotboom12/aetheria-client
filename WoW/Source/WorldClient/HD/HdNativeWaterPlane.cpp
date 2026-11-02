#include "WoW/Source/WorldClient/HD/HdNativeWaterPlane.h"
#include "WoW/Source/WorldClient/HD/HdForeverWater.h"
#include "WoW/Source/WorldClient/HD/HdCataMirror.h"
#include "Engine/Source/Gx/GxSeam.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include <cmath>
namespace HD::NativeWaterPlane {
namespace {uint64_t serial=0;float planeHeight=0;bool valid=false;}
void BeforeLiquids() {
    valid=false;serial=0;
    if(!g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi()!=GxApi_Vulkan || gxvk::vk::frame::Target())return;
    auto* fog=CVar::LookupRegistered("hdFog");
    auto* water=CVar::LookupRegistered("hdCataWater");
    const auto* mirror=CVar::LookupRegistered("hdMirrorCapture");
    if(!(fog&&fog->m_intValue) && !(water&&water->m_intValue) && !(mirror&&mirror->m_intValue) && !HD::ForeverWater::Requested())return;
    const auto wanted=g_gxCataPlane.m_wanted;
    g_gxCataPlane.m_wanted=1;
    HD::CataMirrorChoosePlane(); // Actual queued flat, transparent water; camera must be above it.
    g_gxCataPlane.m_wanted=wanted; // Plane selection alone must not widen culling or claim a reflection.
    if(!g_gxCataPlane.m_valid || !std::isfinite(g_gxCataPlane.m_height))return;
    planeHeight=g_gxCataPlane.m_height;serial=gxvk::vk::frame::Serial();valid=true;
}
bool Get(float& height) {
    if(!valid || serial!=gxvk::vk::frame::Serial())return false;
    height=planeHeight;return true;
}
}
