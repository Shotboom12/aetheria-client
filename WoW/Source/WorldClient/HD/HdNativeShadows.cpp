// SPDX-License-Identifier: GPL-3.0-or-later
#include "WoW/Source/WorldClient/HD/HdNativeShadows.h"
#include "WoW/Source/WorldClient/HD/HdNativeScene.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/HdVulkanShadows.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/HdNativeShadowBridge.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdGeometryPass.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/ShaderEffect/ShadowCache.h"
#include "Engine/Source/ShaderEffect/ShaderEffect.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace HD::NativeShadows {
namespace {
namespace frame=gxvk::vk::frame;
namespace texture=gxvk::vk::texture;
namespace hdscene=gxvk::vk::hdscene;
namespace api=gxvk::vk::hdeffect;
namespace bridge=VulkanShadows::Bridge;
VulkanShadows::Effect effect;VulkanShadows::Settings settings;VulkanShadows::NativeMaps maps;
texture::Image scratch;uint64_t scratchLast=0;
bool initialized=false,mode=false,wanted=false;EShadowMode oldMode{};uint32_t oldPcf=0;
CVar *sun=nullptr,*contact=nullptr,*quality=nullptr,*strength=nullptr,*length=nullptr,*debug=nullptr,*info=nullptr;
char lastInfo[256]{};
CVar* Var(const char* n,const char* h,const char* v,uint32_t flags=1){if(auto* p=CVar::LookupRegistered(n))return p;return CVar::Register(n,h,flags,v,nullptr,CATEGORY_GRAPHICS,false,nullptr,false);}
void Info(const char* text){if(info && std::strcmp(lastInfo,text)){std::snprintf(lastInfo,sizeof lastInfo,"%s",text);info->Set(lastInfo,true,false,false,false);}}
bool Native(){return g_theGxDevicePtr && g_theGxDevicePtr->DeviceApi()==GxApi_Vulkan;}
void Mode(bool on){
 if(mode==on)return;
 if(on){oldMode=CShadowCache::s_shadowMode;oldPcf=CShaderEffect::s_usePcfFiltering;
  CShaderEffect::s_usePcfFiltering=0;CShadowCache::SetShadowMode(std::max(oldMode,SHADOW_MEDIUM_HIGH));}
 else{CShaderEffect::s_usePcfFiltering=oldPcf;CShadowCache::SetShadowMode(oldMode);}
 CShadowCache::NotifyTexturesRecreate();mode=on;
}
struct Pending{api::StageContext stage;VulkanShadows::GeometryInputs geometry;};
bool Depth(VkCommandBuffer cmd,VkImageView view,VkFormat format,VkImageLayout layout,void* user){
 auto& p=*static_cast<Pending*>(user);if(cmd!=p.stage.cmd)return false;
 p.stage.depthView=view;p.stage.depthFormat=format;p.stage.depthLayout=layout;
 return effect.Record(p.stage,settings,maps,p.geometry);
}
bool Record(const hdscene::Context& c,void*){
 scratchLast=c.serial;NativeScene::Snapshot snapshot;if(!NativeScene::Get(c.serial,snapshot))return false;
 Pending p{api::Stage(c,VulkanShadows::ShadowPoint),{}};
 if(!gxvk::vk::hdgeometry::Get(c.serial,p.geometry))return false;
 p.stage.scene=&snapshot.constants;p.stage.viewport=snapshot.viewport;p.stage.sceneValid=1;
 return frame::WithSceneDepth(Depth,&p);
}
}
void BeginWorld(){
 if(!sun){sun=Var("hdSunShadows","HD: native soft sun shadows","0");contact=Var("hdContactShadow","HD: screen-space contact shadows","0");
  quality=Var("hdSunQuality","HD: sun shadow quality 0 or 1","1");strength=Var("hdSunStrength","HD: shadow replacement strength 0..1","1");
  length=Var("hdSunContactLength","HD: contact length in yards 0..4","1.5");debug=Var("hdSunDebug","HD: shadow diagnostics 0..7","0",0);info=Var("hdSunInfo","HD: native shadow status","off",0); if (info) info->m_flags &= ~0x1u; /* status only: not archived to Config.wtf */}
 wanted=Native() && gxvk::vk::hdgeometry::Available() && ((sun && sun->m_intValue) || (contact && contact->m_intValue));
 // The original PCF setting/mode are restored when replacement stops.
 Mode(wanted);maps={};bridge::serial=0;for(auto& id:bridge::ids)id=0;
 if(!wanted)Info("off");
}
void PrepareMaps(){
 if(!wanted || !Native())return;
 const auto serial=frame::Serial();if(!serial || frame::Target() || frame::QueryActive())return;
 // Finish the main attachment pass before inspecting the cached native targets.
 if(!frame::Outside())return;
 maps={};maps.serial=serial;
 for(int i=0;i<3;++i)maps.towardLight[i]=-(&CShadowCache::s_sunDirViewSpace.x)[i];
 if(!sun || !sun->m_intValue || CShadowCache::s_recreateTextures || CShaderEffect::s_usePcfFiltering)return;
 CTexture* textures[5]={CShadowCache::s_extDynShadow.m_textures[0],nullptr,nullptr,nullptr,CShadowCache::s_intDynShadow};
 for(int i=0;i<3;++i){auto& b=CShadowCache::s_extShadowBands[i];if(b.m_current<2)textures[i+1]=b.m_textures[b.m_current];}
 std::memcpy(maps.rows,&CShadowCache::s_shadowMatrices[0].x,12*4*sizeof(float));
 std::memcpy(maps.rows[12],&CShadowCache::s_shadowMatrices[0].x,3*4*sizeof(float));
 auto* device=static_cast<CGxDeviceVulkan*>(g_theGxDevicePtr);
 for(uint32_t i=0;i<5;++i){auto* t=textures[i];if(!t || t->m_asyncObject)continue;
  auto* gx=t->m_gxTex;if(!gx || gx->m_needsCreation || gx->m_needsUpdate || !gx->m_apiSpecificData)continue;
  const auto* image=device->ITexImage(gx);
  if(!image || !image->id || !image->view || image->cube || image->layers!=1 || image->levels!=1 ||
     !image->extent.width || image->format!=VK_FORMAT_R32_SFLOAT || image==frame::Target() || image->layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)continue;
  const float radius=i==0 || i==4?CShadowCache::s_extDynShadow.m_radius:CShadowCache::s_extShadowBands[i-1].m_radius;
  if(!std::isfinite(radius) || radius<=0)continue;
  device->ITexTouch(gx);maps.views[i]=image->view;maps.layouts[i]=image->layout;
  maps.meta[i][0]=radius;maps.meta[i][1]=1.0f/image->extent.width;maps.meta[i][2]=1;maps.meta[i][3]=4000;
  maps.captured|=1u<<i;bridge::ids[i]=image->id;
 }
 bridge::serial=serial;
}
void World(){
 if(!wanted || !Native() || frame::Target() || frame::QueryActive())return;
 VulkanShadows::GeometryInputs geometry;if(!gxvk::vk::hdgeometry::Get(frame::Serial(),geometry)){Info("shadow geometry unavailable");return;}
 if(!initialized){if(!effect.Init(api::Get())){Info(effect.Error());return;}initialized=true;}
 const auto* image=frame::Scene();if(!image)return;
 settings={};settings.enabled=true;settings.contact=contact && contact->m_intValue;
 settings.quality=quality?uint32_t(std::clamp(quality->m_intValue,0,1)):1;
 settings.debug=debug?uint32_t(std::clamp(debug->m_intValue,0,7)):0;
 settings.strength=strength?strength->m_floatValue:1;settings.contactLength=length?length->m_floatValue:1.5f;
 if(!effect.Resize(image->extent.width,image->extent.height)){Info(effect.Error());return;}
 if(!scratch.handle || scratch.extent.width!=image->extent.width || scratch.extent.height!=image->extent.height){
  frame::RetireImage(scratch,scratchLast);scratchLast=0;
  if(!texture::CreateStorage(image->extent.width,image->extent.height,scratch)){Info("shadow scratch allocation failed");return;}}
 if(!hdscene::Apply(scratch,Record,nullptr)){Info("shadow frame/depth unavailable");return;}
 Info("native tagged sun/contact shadow pass active");
}
bool Shutdown(){
 const auto last=std::max(scratchLast,effect.LastUsed());
 if(last>frame::Completed()){frame::Flush();if(!frame::WaitSerial(last))return false;}
 if(!effect.Shutdown(frame::Completed()))return false;
 frame::RetireImage(scratch,scratchLast);scratchLast=0;initialized=false;wanted=false;maps={};bridge::serial=0;
 for(auto& id:bridge::ids)id=0;Mode(false);Info("off");return true;
}
}
