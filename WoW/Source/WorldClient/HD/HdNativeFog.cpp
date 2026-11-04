#include "WoW/Source/WorldClient/HD/HdNativeWaterPlane.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Native Aetheria owner; fog formulas/light blending from pinned Remastered source.
#include "WoW/Source/WorldClient/HD/HdNativeFog.h"
#include "WoW/Source/WorldClient/HD/HdNativeScene.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/HdVulkanFog.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h"
#include "Engine/Source/ShaderEffect/ShadowCache.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdForever.h"
#include "WoW/Source/DB/Rec/LightRec.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace HD::NativeFog {
namespace {
using namespace HD::VulkanFog;
namespace frame=gxvk::vk::frame;
namespace texture=gxvk::vk::texture;
namespace hdscene=gxvk::vk::hdscene;
namespace api=gxvk::vk::hdeffect;
Effect effect;Parameters current;ShadowMaps currentMaps;
texture::Image scratch{};uint64_t scratchLast=0;
bool initialized=false,wanted=false;
CVar *enabled=nullptr,*density=nullptr,*sunScale=nullptr,*ambient=nullptr,*shadow=nullptr,*debug=nullptr,*info=nullptr;
char lastInfo[256]{};float extraX=0,extraY=0;
float Clamp(float value,float lo,float hi){return std::isfinite(value)?std::clamp(value,lo,hi):lo;}
void Info(const char* value){if(info && std::strcmp(lastInfo,value)){std::snprintf(lastInfo,sizeof lastInfo,"%s",value);info->Set(lastInfo,true,false,false,false);}}
CVar* Var(const char* name,const char* help,const char* value,uint32_t flags=1){if(auto* old=CVar::LookupRegistered(name))return old;return CVar::Register(name,help,flags,value,nullptr,CATEGORY_GRAPHICS,false,nullptr,false);}
    // HD: blend two Forever fogs, t = 0 keeps a, 1 gives b
    void LerpFog(HD::ForeverFog& a, const HD::ForeverFog& b, float t) {
        for (int i = 0; i < 3; i++) HD::LerpFogLayer(a.layers[i], b.layers[i], t);
        if (b.count > a.count) a.count = b.count;
    }

    // HD: Forever's fog for one light, clear weather blended to storm like the client's colours.
    bool LightFog(const LightRec* light, int32_t time, float storm, HD::ForeverFog& out) {
        if (!HD::ForeverFogAt(light, 0, time, &out)) return false;
        if (storm > 0.0f) {
            HD::ForeverFog stormFog;
            if (HD::ForeverFogAt(light, 2, time, &stormFog)) LerpFog(out, stormFog, storm);
        }
        return true;
    }

    // HD: the fog where the camera is: the zone light, then the nearby area lights blended in by
    // distance (same falloff as DayNight::BlendAreaLight). Lights Forever does not have fade the fog out.
    // Returns how much Forever fog there is here (0 = none).
    float CurrentForeverFog(HD::ForeverFog& fog) {
        const DayNight::DNInfo& info = DayNight::g_dnInfo;
        const TSGrowableArray<const LightRec*>& lights = DayNight::g_areaLights;
        if (!lights.m_count) return 0.0f;
        int32_t time = static_cast<int32_t>(info.m_dayProgression * 2880.0f);
        float storm = info.m_stormBlend;

        bool have = LightFog(lights.m_data[0], time, storm, fog);
        float strength = have ? 1.0f : 0.0f;

        struct Near { const LightRec* light; float distSq; float weight; };
        Near nearLights[32];
        int32_t count = 0;
        for (uint32_t i = 1; i < lights.m_count && count < 32; i++) {
            const LightRec* light = lights.m_data[i];
            float dx = info.m_lightRefPos.x - light->m_gameCoords[0];
            float dy = info.m_lightRefPos.y - light->m_gameCoords[1];
            float dz = info.m_lightRefPos.z - light->m_gameCoords[2];
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq >= light->m_gameFalloffEnd * light->m_gameFalloffEnd) continue;
            float distance = sqrtf(distSq);
            float weight = 1.0f;
            if (distance > light->m_gameFalloffStart) {
                float span = light->m_gameFalloffEnd - light->m_gameFalloffStart;
                weight = span > 0.0f ? Clamp(1.0f - (distance - light->m_gameFalloffStart) / span, 0.0f, 1.0f) : 0.0f;
            }
            if (weight <= 0.0f) continue;
            nearLights[count].light = light;
            nearLights[count].distSq = distSq;
            nearLights[count].weight = weight;
            count++;
        }
        // farthest first, so the nearest light wins
        for (int32_t i = 1; i < count; i++) {
            Near n = nearLights[i];
            int32_t j = i - 1;
            while (j >= 0 && nearLights[j].distSq < n.distSq) { nearLights[j + 1] = nearLights[j]; j--; }
            nearLights[j + 1] = n;
        }
        for (int32_t i = 0; i < count; i++) {
            HD::ForeverFog other;
            float w = nearLights[i].weight;
            if (LightFog(nearLights[i].light, time, storm, other)) {
                if (!have) { fog = other; have = true; strength = w; }
                else { LerpFog(fog, other, w); strength += (1.0f - strength) * w; }
            } else {
                strength *= 1.0f - w;
            }
        }
        return have ? strength : 0.0f;
    }


bool BuildLive() {
    current={};currentMaps={};wanted=enabled && enabled->m_intValue!=0;
    if(!wanted) {Info("off");return false;}
    HD::ForeverFog fog{};float weight=CurrentForeverFog(fog);
    int mode=debug?std::clamp(debug->m_intValue,0,4):0;
    if(weight<=0 && !mode) {wanted=false;Info("Vulkan fog: no fog data for this light");return false;}
    auto& dn=DayNight::g_dnInfo;
    current.r[5]={dn.m_cameraPos.x,dn.m_cameraPos.y,dn.m_cameraPos.z,dn.m_farClip>50?dn.m_farClip:1000};
    auto sun=DayNight::g_planets.m_sun.m_position-dn.m_cameraPos;
    float length=std::sqrt(sun.x*sun.x+sun.y*sun.y+sun.z*sun.z);
    if(length<1e-4f) {sun.x=0;sun.y=0;sun.z=1;length=1;}
    sun.x/=length;sun.y/=length;sun.z/=length;current.r[6]={sun.x,sun.y,sun.z,0};
    current.r[7].z=float(mode);current.r[8]={5000,1.0f/5000,0,0};current.extra={extraX,extraY,0,0};
    float den=Clamp(density?density->m_floatValue:1,0,5)*weight;
    float lit=Clamp(sunScale?sunScale->m_floatValue:1,0,5)*.88f;
    float base=Clamp(ambient?ambient->m_floatValue:1,0,5)*.034f;
    for(int i=0;i<3 && i<fog.count;++i) {
        const auto& l=fog.layers[i];if(!l.flags || l.density<=0) continue;
        auto& a=current.r[9+i*5];auto& b=current.r[10+i*5];auto& c=current.r[11+i*5];auto& d=current.r[12+i*5];auto& e=current.r[13+i*5];
        a={std::pow(l.emissive[0],2.2f)*base,std::pow(l.emissive[1],2.2f)*base,std::pow(l.emissive[2],2.2f)*base,l.density*.0035f*den};
        b={std::pow(l.diffuse[0],2.2f)*l.intensity*lit,std::pow(l.diffuse[1],2.2f)*l.intensity*lit,std::pow(l.diffuse[2],2.2f)*l.intensity*lit,l.g};
        c={l.start,l.strength,l.exponent,(l.flags&1)?1.0f:0};
        float rel=(l.flags&2)?dn.m_playerPos.z:0;
        d={l.topRate*.01f,l.topHeight+rel,l.bottomRate*.01f,l.bottomHeight+rel};
        e={std::pow(l.shadow[0],2.2f)*base,std::pow(l.shadow[1],2.2f)*base,std::pow(l.shadow[2],2.2f)*base,l.shadowMul};
    }
    current.r[33].z=Clamp(sun.z*4+.2f,0,1);
    return true;
}
ShadowMaps NativeMaps(uint64_t serial) {
    ShadowMaps result;
    if(!shadow || !shadow->m_intValue || CShadowCache::GetShadowMode()<=SHADOW_MEDIUM)return result;
    auto* device=static_cast<CGxDeviceVulkan*>(g_theGxDevicePtr);
    for(uint32_t i=0;i<3;++i){
        auto& band=CShadowCache::s_extShadowBands[i];
        if(band.m_current>=2)return {};
        CTexture* texture=band.m_textures[band.m_current];if(!texture)return {};
        if(texture->m_asyncObject)return {};
        CGxTex* gx=texture->m_gxTex; // Already-loaded shadow target only; no async wait/atlas reload.
        // Never create or refill an engine texture from the compute callback. Preparation is outside it.
        if(!gx || gx->m_needsCreation || gx->m_needsUpdate || !gx->m_apiSpecificData)return {};
        const auto* image=device->ITexImage(gx);
        if(!image || !image->view || image->cube || image->layers!=1 || image->levels!=1
           || image->format!=VK_FORMAT_R32_SFLOAT || image==frame::Target()
           || image->layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)return {};
        result.view[i]=image->view;result.layout[i]=image->layout;
        device->ITexTouch(gx); // Existing native retirement tracks this frame before the image can be released.
    }
    result.serial=serial;result.comparison=false;
    static_assert(sizeof CShadowCache::s_shadowMatrices==12*sizeof(Row),"native shadow matrix rows");
    std::memcpy(&current.r[24],&CShadowCache::s_shadowMatrices[3].x,9*sizeof(Row));
    return result;
}
struct Pending {api::StageContext stage;bool called=false;};
bool WithDepth(VkCommandBuffer cmd,VkImageView view,VkFormat format,VkImageLayout layout,void* user) {
    auto& pending=*static_cast<Pending*>(user);if(cmd!=pending.stage.cmd)return false;
    pending.called=true;pending.stage.depthView=view;pending.stage.depthFormat=format;pending.stage.depthLayout=layout;
    return effect.Record(pending.stage,current,currentMaps);
}
bool Record(const hdscene::Context& c,void*) {
    scratchLast=c.serial;NativeScene::Snapshot snapshot;
    if(!NativeScene::Get(c.serial,snapshot))return false;
    Pending pending{api::Stage(c,HD::VulkanFog::Point)};
    pending.stage.scene=&snapshot.constants;pending.stage.viewport=snapshot.viewport;pending.stage.sceneValid=1;
    return frame::WithSceneDepth(WithDepth,&pending);
}
bool Init() {
    if(initialized)return true;
    if(!hdscene::Supported()){Info("native fog: compute/storage unavailable");return false;}
    if(!effect.Init(api::Get())){Info(effect.Error());return false;}
    initialized=true;return true;
}
}
void World() {
    if(!g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi()!=GxApi_Vulkan)return;
    if(!enabled) {
        enabled=Var("hdFog","HD: global volumetric fog, 0 off / 1 on","0");
        density=Var("hdFogDensity","HD: fog density multiplier 0..5","1");sunScale=Var("hdFogSun","HD: sun scattering 0..5","1");
        ambient=Var("hdFogAmbient","HD: ambient fog 0..5","1");
        shadow=Var("hdFogShadow","HD: use available native floating-point sun shadow maps","0");
        debug=Var("hdFogDebug","HD: 1 distance, 2 depth, 3 fog alone, 4 shadows","0",0);
        info=Var("hdFogInfo","HD: native fog status","off",0);
    }
    if(frame::Target() || frame::QueryActive() || !BuildLive() || !Init())return;
    const auto* image=frame::Scene();if(!image || !image->extent.width || !image->extent.height)return;
    if(!effect.Resize(image->extent.width,image->extent.height)){Info(effect.Error());return;}
    currentMaps=NativeMaps(frame::Serial());
    float waterHeight=0;const bool haveWaterPlane=NativeWaterPlane::Get(waterHeight);current.extra.z=haveWaterPlane?waterHeight:0;current.extra.w=haveWaterPlane?1.0f:0.0f;
    if(!scratch.handle || scratch.extent.width!=image->extent.width || scratch.extent.height!=image->extent.height) {
        frame::RetireImage(scratch,scratchLast);scratchLast=0;
        if(!texture::CreateStorage(image->extent.width,image->extent.height,scratch)){Info("native fog: scratch allocation failed");return;}
    }
    if(!hdscene::Apply(scratch,Record,nullptr)){Info("native fog skipped: current scene/depth/projection unavailable");return;}
    Info(currentMaps.serial?"native fog active with native sun shadow maps; same-frame water-plane clipping available":
         "native fog active; compatible sun shadow maps unavailable; same-frame water-plane clipping available");
}
// HD 2026-11-03: read-only views for hd/lumen (the Lumenveil snapshot of Azeroth Remastered), same functions BuildLive uses.
bool CurrentScatter(uint64_t serial,VkImageView& view,VkExtent2D& extent){return effect.CurrentScatter(serial,view,extent);}
float ForeverFogHere(HD::ForeverFog& fog){fog={};return CurrentForeverFog(fog);}
bool ReferenceRequested(){return enabled&&enabled->m_intValue!=0;}
bool Shutdown() {
    if(!initialized && !scratch.handle)return true;
    const auto last=std::max(scratchLast,effect.LastUsed());
    if(last>frame::Completed()){frame::Flush();if(!frame::WaitSerial(last))return false;}
    if(!effect.Shutdown(frame::Completed()))return false;
    frame::RetireImage(scratch,scratchLast);scratchLast=0;initialized=false;wanted=false;Info("off");return true;
}
}
