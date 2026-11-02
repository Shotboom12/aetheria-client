// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria-native owner for the pinned Remastered bloom/ray effect.
#include "WoW/Source/WorldClient/HD/HdNativeBloom.h"
#include "WoW/Source/WorldClient/HD/HdNativeScene.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/HdVulkanBloom.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace HD::NativeBloom
{
namespace
{
    namespace frame=gxvk::vk::frame;
    namespace texture=gxvk::vk::texture;
    namespace hdscene=gxvk::vk::hdscene;
    namespace api=gxvk::vk::hdeffect;
    VulkanBloom::Effect effect;
    VulkanBloom::Settings config;
    texture::Image scratch{};
    uint64_t scratchLast=0;
    bool initialized=false;
    CVar *bloom=nullptr,*strength=nullptr,*threshold=nullptr,*rays=nullptr,*rayStrength=nullptr,*debug=nullptr,*info=nullptr;
    char lastInfo[256]{};
    void Info(const char* value) {
        if(info && std::strcmp(lastInfo,value)) {
            std::snprintf(lastInfo,sizeof lastInfo,"%s",value);info->Set(lastInfo,true,false,false,false);
        }
    }
    CVar* Var(const char* name,const char* help,const char* value,uint32_t flags=1) {
        if(auto* old=CVar::LookupRegistered(name))return old;
        return CVar::Register(name,help,flags,value,nullptr,CATEGORY_GRAPHICS,false,nullptr,false);
    }
    float Value(CVar* var,float fallback,float hi) {
        const float value=var?var->m_floatValue:fallback;
        return std::isfinite(value)?std::clamp(value,0.0f,hi):fallback;
    }
    struct Pending { api::StageContext stage; bool called=false; };
    bool WithDepth(VkCommandBuffer cmd,VkImageView view,VkFormat format,VkImageLayout layout,void* user) {
        auto& pending=*static_cast<Pending*>(user);
        if(cmd!=pending.stage.cmd)return false;
        pending.stage.depthView=view;pending.stage.depthFormat=format;pending.stage.depthLayout=layout;
        pending.called=true;
        return effect.Record(pending.stage,config);
    }
    bool Record(const hdscene::Context& c,void*) {
        scratchLast=c.serial;
        Pending pending{api::Stage(c,VulkanBloom::Point)};
        NativeScene::Snapshot snapshot;
        pending.stage.sceneValid=NativeScene::Get(c.serial,snapshot);
        pending.stage.scene=&snapshot.constants;pending.stage.viewport=snapshot.viewport;
        if(config.rays) {
            const bool result=frame::WithSceneDepth(WithDepth,&pending);
            if(pending.called)return result;
        }
        // Bloom is independent of depth. The effect explicitly skips rays if depth is absent.
        return effect.Record(pending.stage,config);
    }
    bool Init() {
        if(initialized)return true;
        if(!hdscene::Supported()){Info("native bloom: compute/storage unavailable");return false;}
        if(!effect.Init(api::Get())){Info(effect.Error());return false;}
        initialized=true;return true;
    }
}

void CaptureScene()
{
    NativeScene::Capture();
}

void World()
{
    if(!g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi()!=GxApi_Vulkan)return;
    if(!bloom) {
        bloom=Var("hdBloom","HD: bright world halos","0");strength=Var("hdBloomStrength","HD: halo strength 0..5","1");
        threshold=Var("hdBloomThreshold","HD: bright threshold 0..1","0.7");rays=Var("hdRays","HD: sun rays","0");
        rayStrength=Var("hdRaysStrength","HD: rays strength 0..5","1");debug=Var("hdBloomDebug","HD: 1 mask, 2 rays, 3 halo","0",0);
        info=Var("hdBloomInfo","HD: native bloom status","off",0);
    }
    config={};config.bloom=bloom && bloom->m_intValue;config.rays=rays && rays->m_intValue;
    if(!config.bloom && !config.rays){Info("off");return;}
    if(frame::Target() || frame::QueryActive() || !Init())return;
    const auto* image=frame::Scene();if(!image || !image->extent.width || !image->extent.height)return;
    config.strength=Value(strength,1,5);config.threshold=Value(threshold,.7f,1);config.rayStrength=Value(rayStrength,1,5);
    config.debug=debug?std::clamp(debug->m_intValue,0,3):0;
    const auto& dn=DayNight::g_dnInfo;const auto sun=DayNight::g_planets.m_sun.m_position-dn.m_cameraPos;
    config.sunDirection[0]=sun.x;config.sunDirection[1]=sun.y;config.sunDirection[2]=sun.z;
    config.cameraForward[0]=dn.m_cameraDir.x;config.cameraForward[1]=dn.m_cameraDir.y;config.cameraForward[2]=dn.m_cameraDir.z;
    config.farClip=dn.m_farClip>50?dn.m_farClip:1000;
    // Allocate/resize before any compute recording, and retain old targets until their serial completes.
    if(!effect.Resize(image->extent.width,image->extent.height)){Info(effect.Error());return;}
    if(!scratch.handle || scratch.extent.width!=image->extent.width || scratch.extent.height!=image->extent.height) {
        frame::RetireImage(scratch,scratchLast);scratchLast=0;
        if(!texture::CreateStorage(image->extent.width,image->extent.height,scratch)){Info("native bloom: scratch allocation failed");return;}
    }
    hdscene::Apply(scratch,Record,nullptr);Info(effect.Error());
}

bool Shutdown()
{
    if(!initialized && !scratch.handle)return true;
    const auto last=std::max(scratchLast,effect.LastUsed());
    if(last>frame::Completed()){frame::Flush();if(!frame::WaitSerial(last))return false;}
    if(!effect.Shutdown(frame::Completed()))return false;
    frame::RetireImage(scratch,scratchLast);scratchLast=0;initialized=false;Info("off");return true;
}
}
