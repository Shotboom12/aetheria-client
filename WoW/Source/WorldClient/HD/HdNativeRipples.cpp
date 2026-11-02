#include "WoW/Source/WorldClient/HD/HdNativeRipples.h"
#include "WoW/Source/WorldClient/HD/HdCataRipples.h"
#include "WoW/Source/WorldClient/HD/HdCataWater.h"
#include "WoW/Source/WorldClient/World.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/CataRipples.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Pipelines.hpp"
#include "Engine/Source/Gx/GxSeam.h"
#include <cmath>
namespace HD::NativeRipples {
namespace {
CataWater::Ripples effect;
bool initialized=false,failed=false,centerValid=false;
int32_t lastMap=-1;
float center[3]{};
}
void BeginWorld(bool enabled){
    const auto map=World::QueryMapId();if(map!=lastMap){Reset();lastMap=map;}
    if(enabled && !initialized && !failed){
        initialized=effect.Initialize(gxvk::vk::hdeffect::Get());failed=!initialized;
        if(failed)effect.StopAndDestroy();
    }
    if(initialized)effect.Enable(enabled);
    else g_gxCataRipples.m_active=0;
    if(!enabled || !initialized)g_gxCataRipples.m_ready=0;
}
void Reset(){centerValid=false;if(initialized)effect.Reset();HD::CataRipplesReset();}
void BeforeLiquids(){
    namespace frame=gxvk::vk::frame;
    if(!initialized || !g_gxCataRipples.m_active || !g_gxCataRipples.m_wanted || frame::Target() || frame::QueryActive())return;
    if(!HD::CataWaterRippleMaterialReady()){g_gxCataRipples.m_ready=0;return;}
    const auto& p=g_gxCataRipples.m_center;
    for(int i=0;i<3;++i)if(!std::isfinite(p[i])){Reset();return;}
    if(centerValid && (std::fabs(p[0]-center[0])>64 || std::fabs(p[1]-center[1])>64 || std::fabs(p[2]-center[2])>64))effect.Reset();
    for(int i=0;i<3;++i)center[i]=p[i];centerValid=true;
    auto cmd=frame::Outside();if(!cmd){g_gxCataRipples.m_ready=0;return;}
    gxvk::vk::hdeffect::StageContext ctx{};
    ctx.structSize=sizeof ctx;ctx.point=10;ctx.serial=frame::Serial();ctx.completedSerial=frame::Completed();
    ctx.frameSlot=frame::Slot();ctx.frameSlots=frame::kFramesInFlight;ctx.cmd=cmd;
    effect.Record(ctx);
    if(!effect.Recorded(ctx.serial))effect.Reset();
    // Ripple passes bind their own raw graphics state. Force the next native draw to rebind all.
    gxvk::vk::pipelines::ForgetBindings();
}
const gxvk::vk::texture::Image* SampledImage(uint32_t map){return initialized?effect.SampledImage(map,gxvk::vk::frame::Serial()):nullptr;}
bool Shutdown(){
    if(initialized && effect.LastUsed()>gxvk::vk::frame::Completed() && !gxvk::vk::frame::WaitSerial(effect.LastUsed()))return false;
    effect.StopAndDestroy();initialized=failed=centerValid=false;lastMap=-1;HD::CataRipplesReset();return true;
}
}
