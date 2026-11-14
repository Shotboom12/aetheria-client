#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdGeometryPass.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
// Native lifecycle for the pinned XeGTAO-based effect (algorithm upstream: MIT).
#include "WoW/Source/WorldClient/HD/HdNativeAO.h"
#include "WoW/Source/WorldClient/HD/HdNativeScene.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/HdVulkanAO.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "WoW/Source/WorldClient/World.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace HD::NativeAO {
namespace {
namespace frame=gxvk::vk::frame;
namespace texture=gxvk::vk::texture;
namespace hdscene=gxvk::vk::hdscene;
namespace api=gxvk::vk::hdeffect;
VulkanAO::Effect effect;VulkanAO::Settings settings;
texture::Image scratch{};uint64_t scratchLast=0;int32_t lastMap=-1;
bool initialized=false,lastNormals=false;
CVar *enabled=nullptr,*radius=nullptr,*strength=nullptr,*half=nullptr,*quality=nullptr,*temporal=nullptr,*normals=nullptr,*debug=nullptr,*info=nullptr;
char lastInfo[256]{};
void Info(const char* value){if(info && std::strcmp(lastInfo,value)){std::snprintf(lastInfo,sizeof lastInfo,"%s",value);info->Set(lastInfo,true,false,false,false);}}
CVar* Var(const char* name,const char* help,const char* value,uint32_t flags=1){if(auto* old=CVar::LookupRegistered(name))return old;return CVar::Register(name,help,flags,value,nullptr,CATEGORY_GRAPHICS,false,nullptr,false);}
struct Pending {api::StageContext stage;};
bool WithDepth(VkCommandBuffer cmd,VkImageView view,VkFormat format,VkImageLayout layout,void* user) {
    auto& pending=*static_cast<Pending*>(user);if(cmd!=pending.stage.cmd)return false;
    pending.stage.depthView=view;pending.stage.depthFormat=format;pending.stage.depthLayout=layout;
    return effect.Record(pending.stage,settings);
}
bool Record(const hdscene::Context& c,void*) {
    scratchLast=c.serial;NativeScene::Snapshot snapshot;
    if(!NativeScene::Get(c.serial,snapshot))return false;
    Pending pending{api::Stage(c,VulkanAO::Point)};
    pending.stage.scene=&snapshot.constants;pending.stage.viewport=snapshot.viewport;pending.stage.sceneValid=1;
    return frame::WithSceneDepth(WithDepth,&pending);
}
bool Init() {
    if(initialized)return true;
    if(!hdscene::Supported()){Info("native AO: compute/storage unavailable");return false;}
    if(!effect.Init(api::Get())){Info(effect.Error());return false;}
    initialized=true;return true;
}
}
void World() {
    if(!g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi()!=GxApi_Vulkan)return;
    if(!enabled){
        enabled=Var("hdAO","HD: ambient occlusion 0 off / 1 on","0");radius=Var("hdAORadius","HD: radius in yards 0.25..6","1");
        strength=Var("hdAOStrength","HD: AO strength 0..3","0.85");half=Var("hdAOHalfRes","HD: bilateral half resolution","1"); /* HD lot A1 (Mado Nov 6): half resolution by default */
        quality=Var("hdAOQuality","HD: 0 low / 1 medium / 2 high / 3 ultra","1");temporal=Var("hdAOTemporal","HD: temporal accumulation","1");
        normals=Var("hdAONormals","HD: request geometry-buffer normals with depth-normal fallback","0");
        debug=Var("hdAODebug","HD: show AO alone","0",0);info=Var("hdAOInfo","HD: native AO status","off",0);
    }
    if(!enabled || !enabled->m_intValue){Info("off");return;}
    if(frame::Target() || frame::QueryActive() || !Init())return;
    const auto* image=frame::Scene();if(!image || !image->extent.width || !image->extent.height)return;
    settings={};settings.enabled=true;
    settings.radius=VulkanAO::ClampSetting(radius?radius->m_floatValue:1,1,.25f,6);
    settings.strength=VulkanAO::ClampSetting(strength?strength->m_floatValue:.85f,.85f,0,3);
    settings.quality=quality?uint32_t(std::clamp(quality->m_intValue,0,3)):1;
    settings.halfResolution=half && half->m_intValue;settings.temporal=temporal && temporal->m_intValue;settings.debug=debug && debug->m_intValue;
    VulkanShadows::GeometryInputs geometry;
    settings.useGBufferNormals=normals && normals->m_intValue && gxvk::vk::hdgeometry::Get(frame::Serial(),geometry);
    if(settings.useGBufferNormals!=lastNormals){effect.ResetHistory();lastNormals=settings.useGBufferNormals;}
    const int32_t map=World::QueryMapId();if(map!=lastMap){effect.ResetHistory();lastMap=map;}
    if(!effect.Resize(image->extent.width,image->extent.height,settings.halfResolution)){Info(effect.Error());return;}
    if(!scratch.handle || scratch.extent.width!=image->extent.width || scratch.extent.height!=image->extent.height){
        frame::RetireImage(scratch,scratchLast);scratchLast=0;
        if(!texture::CreateStorage(image->extent.width,image->extent.height,scratch)){Info("native AO: scratch allocation failed");return;}
    }
    if(!hdscene::Apply(scratch,Record,nullptr)){Info("native AO skipped: current scene/depth/projection unavailable");return;}
    Info(settings.useGBufferNormals?"native GTAO active with tagged geometry normals":"native GTAO active with depth normals");
}
bool Shutdown() {
    if(!initialized && !scratch.handle)return true;
    const auto last=std::max(scratchLast,effect.LastUsed());
    if(last>frame::Completed()){frame::Flush();if(!frame::WaitSerial(last))return false;}
    if(!effect.Shutdown(frame::Completed()))return false;
    frame::RetireImage(scratch,scratchLast);scratchLast=0;lastMap=-1;initialized=false;Info("off");return true;
}
}
