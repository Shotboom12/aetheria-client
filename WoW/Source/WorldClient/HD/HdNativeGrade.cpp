// SPDX-License-Identifier: GPL-3.0-or-later
// Grading adapted from pinned Remastered source; native Aetheria lifecycle, no WXL loader.
#include "WoW/Source/WorldClient/HD/HdNativeGrade.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/HdVulkanGrade.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Types.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/HD/HdForever.h"
#include "WoW/Source/DB/Rec/LightRec.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>
namespace HD::NativeGrade {
namespace {
using namespace HD::VulkanGrade;
namespace frame=gxvk::vk::frame;
namespace texture=gxvk::vk::texture;
namespace hdscene=gxvk::vk::hdscene;
Effect effect;texture::Image scratch{};uint64_t scratchLast=0;uint32_t cached=0;
CVar *enabled=nullptr,*strength=nullptr,*debug=nullptr,*forced=nullptr,*info=nullptr;
bool initialized=false;Selection selected;Parameters current;std::set<int32_t> missing;char lastInfo[240]{};
void Info(const char* value){if(info && std::strcmp(lastInfo,value)){std::snprintf(lastInfo,sizeof lastInfo,"%s",value);info->Set(lastInfo,true,false,false,false);}}
CVar* Var(const char* name,const char* help,const char* value,uint32_t flags=1){if(auto* old=CVar::LookupRegistered(name))return old;return CVar::Register(name,help,flags,value,nullptr,CATEGORY_GRAPHICS,false,nullptr,false);}
float Clamp(float value,float lo,float hi){return std::isfinite(value)?std::clamp(value,lo,hi):lo;}
    // One light's grading (clear weather blended to storm), as weights. No Forever grading = identity (0).
    void AddLight(LutWeights& luts, const LightRec* light, int32_t time, float storm, float weight) {
        for (int32_t pass = 0; pass < 2; pass++) {
            float w = weight * (pass == 0 ? 1.0f - storm : storm);
            if (w <= 0.0f) continue;
            int32_t a, b;
            float t;
            if (HD::ForeverGradingAt(light, pass == 0 ? 0 : 2, time, &a, &b, &t)) {
                luts.Add(a, w * (1.0f - t));
                luts.Add(b, w * t);
            } else {
                luts.Add(0, w);
            }
        }
    }

    // Same light choice as the fog (HdFog CurrentForeverFog): the zone light, then the nearby area lights
    // blended in by distance like DayNight::BlendAreaLight, farthest first so the nearest wins.
    void CurrentLuts(LutWeights& luts) {
        const DayNight::DNInfo& info = DayNight::g_dnInfo;
        const TSGrowableArray<const LightRec*>& lights = DayNight::g_areaLights;
        if (!lights.m_count) return;
        int32_t time = static_cast<int32_t>(info.m_dayProgression * 2880.0f);
        float storm = Clamp(info.m_stormBlend, 0.0f, 1.0f);
        AddLight(luts, lights.m_data[0], time, storm, 1.0f);

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
        for (int32_t i = 1; i < count; i++) {
            Near n = nearLights[i];
            int32_t j = i - 1;
            while (j >= 0 && nearLights[j].distSq < n.distSq) { nearLights[j + 1] = nearLights[j]; j--; }
            nearLights[j + 1] = n;
        }
        for (int32_t i = 0; i < count; i++) {
            luts.Scale(1.0f - nearLights[i].weight);
            AddLight(luts, nearLights[i].light, time, storm, nearLights[i].weight);
        }
    }


bool Cache(int32_t id) {
    if(effect.HasLut(id))return true;if(id<0 || cached>=64 || missing.count(id))return false;
    Texels pixels{};
    if(id==0)Identity(pixels);else {
        char paths[2][96];std::snprintf(paths[0],sizeof paths[0],"forever_data\\grading\\%d.blp",id);std::snprintf(paths[1],sizeof paths[1],"..\\..\\forever_data\\grading\\%d.blp",id);
        FILE* file=nullptr;for(int i=0;i<2 && !file;++i)file=std::fopen(paths[i],"rb");
        if(!file){if(missing.size()<256)missing.insert(id);return false;}
        // Bounded BLP read; the reference needs only the first uncompressed mip.
        std::vector<uint8_t> data(2*1024*1024);size_t got=std::fread(data.data(),1,data.size(),file);bool bad=std::ferror(file)!=0;std::fclose(file);
        if(bad || !DecodeLut(data.data(),got,pixels)){if(missing.size()<256)missing.insert(id);return false;}
    }
    if(!effect.AddLut(id,pixels))return false;++cached;return true;
}

bool Record(const hdscene::Context& c,void*) {
    scratchLast=c.serial;
    const auto stage=gxvk::vk::hdeffect::Stage(c,HD::VulkanGrade::Point);
    return effect.Record(stage,selected.a,selected.b,current);
}
bool Init() {
    if(initialized)return true;
    if(!hdscene::Supported()){Info("native grade: compute/storage format unavailable");return false;}
    if(!effect.Init(gxvk::vk::hdeffect::Get())){Info(effect.Error());return false;}
    if(!Cache(0)){effect.Shutdown(0);Info("native grade: identity LUT unavailable");return false;}
    initialized=true;return true;
}
}
void World() {
    if(!g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi()!=GxApi_Vulkan)return;
    if(!enabled){
        enabled=Var("hdGrade","HD: world colour grading, 0 off / 1 on","0");
        strength=Var("hdGradeStrength","HD: grading strength 0..2","1");
        debug=Var("hdGradeDebug","HD: left original / right graded","0",0);
        forced=Var("hdGradeLut","HD: positive LUT ID overrides zone choice","0");
        info=Var("hdGradeInfo","HD: grading status","off",0);
    }
    if(!enabled || !enabled->m_intValue){Info("off");return;}
    if(frame::Target() || frame::QueryActive() || !Init())return;
    const auto* scene=frame::Scene();if(!scene || !scene->extent.width || !scene->extent.height)return;
    LutWeights weights;if(forced && forced->m_intValue>0)weights.Add(forced->m_intValue,1);else CurrentLuts(weights);
    selected=Select(weights);current={};current.blend=selected.blend;
    current.strength=Clamp(strength?strength->m_floatValue:1,0,2);current.debug=debug?float(debug->m_intValue):0;
    if(!selected.hasGrade && current.debug<=.5f){Info("native grade: no LUT for this light");return;}
    if(!Cache(selected.a)||!Cache(selected.b)){Info("native grade: LUT unavailable/cache full; image unchanged");return;}
    if(!scratch.handle || scratch.extent.width!=scene->extent.width || scratch.extent.height!=scene->extent.height){
        // Keep the old image until the last recorded frame completes; no GPU wait during resize.
        frame::RetireImage(scratch,scratchLast);scratchLast=0;
        if(!texture::CreateStorage(scene->extent.width,scene->extent.height,scratch)){Info("native grade: scratch allocation failed");return;}
    }
    if(!effect.Resize(scene->extent.width,scene->extent.height)){Info(effect.Error());return;}
    if(!hdscene::Apply(scratch,Record,nullptr)){Info(effect.Error());return;}
    Info("native grade active");
}
bool Shutdown() {
    if(!initialized && !scratch.handle)return true;
    const uint64_t last=std::max(scratchLast,effect.LastUsed());
    if(last>frame::Completed()){
        frame::Flush();if(!frame::WaitSerial(last))return false;
    }
    if(!effect.Shutdown(frame::Completed()))return false;
    frame::RetireImage(scratch,scratchLast);scratchLast=0;
    missing.clear();cached=0;initialized=false;Info("off");return true;
}
}
