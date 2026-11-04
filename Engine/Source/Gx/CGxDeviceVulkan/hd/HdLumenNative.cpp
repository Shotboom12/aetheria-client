// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: see HdLumenNative.h. Takes the place of Mado's wxl WorldPass hooks (HD::Lamp::Owner::Prepare, HD::LumenBridge::Frame and
// the stage chain at its three points) on the native hdscene boundary, the way HdNativeFog/HdNativeAO own their passes.
#include "HdLumenNative.h"
#include "HdWxlShim.h"
#include "lamp/HdLampHooks.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdScene.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdGeometryPass.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/WorldClient/HD/HdNativeScene.h"
#ifdef WOWDECOMP_LUMEN
#include "HdLumenBridge.h"
#include "lumen/LvClient.h"
#endif
#include <algorithm>
#include <cstdio>
namespace HD::LumenNative {
namespace {
namespace frame=gxvk::vk::frame;
namespace texture=gxvk::vk::texture;
namespace hdscene=gxvk::vk::hdscene;
namespace api=gxvk::vk::hdeffect;
texture::Image scratch{};
uint64_t scratchLast=0;
bool started=false,startFailed=false,lampInitialized=false;
constexpr uint32_t kPointOpaque=0,kPointBeforeSpells=5,kPointBloom=7; // WXL_VK_POINT_*
bool ArVulkan() {
    return Visuals::Current()==Visuals::AzerothRemastered && g_theGxDevicePtr && g_theGxDevicePtr->DeviceApi()==GxApi_Vulkan;
}
#ifdef WOWDECOMP_LUMEN
void Start() {
    if(started||startFailed)return;
    const auto* table=WxlShim::Api();if(!table)return; // native context not live yet: try again next frame
    // The lamp catalogue is read from Vulkan\lumen\lampdata.bin next to the executable (a catalogue derived from the player's own
    // MPQs; it is never published). Without it Lumen starts without lamps and says so in its status.
    started=HD::Lumen::Client::Startup(table,"Vulkan\\lumen\\lampdata.bin","");
    startFailed=!started;
    char line[400];std::snprintf(line,sizeof line,"HD Lumen (Azeroth Remastered): startup %s, status: %s%s%s",started?"ok":"FAILED",HD::Lumen::Client::Status(),started?"":", reason: ",started?"":HD::Lumen::Client::FailureReason());
    CGxDevice::Log("%s", static_cast<const char*>(line));
}
struct Pending { uint32_t point; api::StageContext stage; };
bool WithDepth(VkCommandBuffer cmd,VkImageView view,VkFormat format,VkImageLayout layout,void* user) {
    auto& pending=*static_cast<Pending*>(user);if(cmd!=pending.stage.cmd)return false;
    pending.stage.depthView=view;pending.stage.depthFormat=format;pending.stage.depthLayout=layout;
    return WxlShim::Run(pending.point,pending.stage)>0;
}
bool Record(const hdscene::Context& c,void* user) {
    scratchLast=c.serial;
    NativeScene::Snapshot snapshot;
    if(!NativeScene::Get(c.serial,snapshot))return false;
    Pending pending{*static_cast<uint32_t*>(user),api::Stage(c,*static_cast<uint32_t*>(user))};
    pending.stage.scene=&snapshot.constants;pending.stage.viewport=snapshot.viewport;pending.stage.sceneValid=1;
    return frame::WithSceneDepth(WithDepth,&pending);
}
void RunPoint(uint32_t point,bool writeBack) {
    if(!started||!ArVulkan()||!WxlShim::Wanted(point))return;
    if(frame::Target()||frame::QueryActive()||!hdscene::Supported())return;
    const auto* image=frame::Scene();if(!image||!image->extent.width||!image->extent.height)return;
    if(!scratch.handle||scratch.extent.width!=image->extent.width||scratch.extent.height!=image->extent.height) {
        frame::RetireImage(scratch,scratchLast);scratchLast=0;
        if(!texture::CreateStorage(image->extent.width,image->extent.height,scratch))return;
    }
    uint32_t p=point;
    hdscene::Apply(scratch,Record,&p,writeBack);
}
#endif
}
void BeginWorld() {
    if(!ArVulkan()) {
#ifdef WOWDECOMP_LUMEN
        if(started) HD::LumenBridge::Frame(); // hands R2's requests back once (the bridge sees the mode is no longer AR)
#endif
        return;
    }
    if(!lampInitialized){HD::Lamp::Native::Init();lampInitialized=true;}
    NativeScene::Capture(); // this frame's projection/view for the stages (same capture WorldFrame takes for the other passes)
    const auto p=DayNight::g_dnInfo.m_cameraPos;
    HD::Lamp::Native::BeginFrame(frame::Serial(),reinterpret_cast<uintptr_t>(World::s_m2Scene),p.x,p.y,p.z);
    HD::Lamp::Native::Tick();
#ifdef WOWDECOMP_LUMEN
    Start();
    if(started) HD::LumenBridge::Frame();
#endif
}
bool GBufferWanted() { return HD::WxlShim::GBufferRequested(); }
void AfterOpaque() {
#ifdef WOWDECOMP_LUMEN
    // The G-buffer for the lamp surfaces: the private six-colour replay of the captured opaque draws (needs the draw capture and the gbuffer shader modules).
    if(HD::WxlShim::GBufferRequested()&&started) gxvk::vk::hdgeometry::Render();
    RunPoint(kPointOpaque,false); // prepares the frame; nothing is written to the colour here
#endif
}
void World() {
#ifdef WOWDECOMP_LUMEN
    RunPoint(kPointBeforeSpells,true);
#endif
}
void Bloom() {
#ifdef WOWDECOMP_LUMEN
    if(started&&HD::Lumen::Client::NeedsAfterSpells()) RunPoint(kPointBloom,true);
#endif
}
bool ComposedFog() {
#ifdef WOWDECOMP_LUMEN
    return started&&Visuals::Current()==Visuals::AzerothRemastered&&HD::Lumen::Client::ComposedFog(frame::Serial());
#else
    return false;
#endif
}
bool ComposedShafts() {
#ifdef WOWDECOMP_LUMEN
    return started&&Visuals::Current()==Visuals::AzerothRemastered&&HD::Lumen::Client::ComposedShafts(frame::Serial());
#else
    return false;
#endif
}
bool Shutdown() {
#ifdef WOWDECOMP_LUMEN
    if(started) {
        HD::Lumen::Client::Stop();
        const uint64_t last=HD::Lumen::Client::LastUsed();
        if(last>frame::Completed()){frame::Flush();if(!frame::WaitSerial(last))return false;}
        if(!HD::Lumen::Client::Shutdown(frame::Completed()))return false;
        started=false;
    }
    startFailed=false;
    if(!gxvk::vk::hdgeometry::Shutdown())return false;
    if(scratch.handle){
        if(scratchLast>frame::Completed()){frame::Flush();if(!frame::WaitSerial(scratchLast))return false;}
        frame::RetireImage(scratch,scratchLast);scratchLast=0;
    }
#endif
    if(lampInitialized){HD::Lamp::Native::Shutdown();lampInitialized=false;}
    return true;
}
}
