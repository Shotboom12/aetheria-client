// SPDX-License-Identifier: GPL-3.0-or-later
#include "HdFsrSettings.h"
#ifdef WOWDECOMP_DLSS
#include "../dlss/HdDlssSettings.h"
#endif
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace HD::Fsr::Settings {
namespace {
 CVar *up=nullptr,*fg=nullptr,*quality=nullptr,*sharp=nullptr,*info=nullptr,*fgInfo=nullptr,*test=nullptr,*objectMotion=nullptr,*debugView=nullptr,*rigidMotion=nullptr,*skeletalMotion=nullptr,*windMotion=nullptr;
 CVar *reactiveMasks=nullptr,*reactiveScale=nullptr,*compositionThreshold=nullptr,*reactiveCutoff=nullptr,*debugCheck=nullptr;
 CVar*validationCapture=nullptr;
 char previousUp[160]{},previousFg[160]{};
 CVar* Var(const char* name,const char* help,const char* value,uint32_t flags=1){
  if(auto* p=CVar::LookupRegistered(name))return p;
  return CVar::Register(name,help,flags,value,nullptr,CATEGORY_GRAPHICS,false,nullptr,false);
 }
 void Status(CVar* p,char* previous,size_t n,const char* text){
  if(p&&text&&std::strcmp(previous,text)){p->Set(text,true,false,false,false);std::snprintf(previous,n,"%s",text);}
 }
}
void Register(){
 if(up)return;
 validationCapture=Var("hdFSRValidation","HD: V1 telemetry for an explicitly prepared isolated session; never approves normal FSR", "0",0);
 reactiveMasks=Var("hdFSRReactiveMasks","HD: R1 colour-difference reactive/composition masks (test only, default off)","0");
 reactiveScale=Var("hdFSRReactiveScale","HD: R1 auto-reactive scale 0..2, default 0.9","0.9");
 compositionThreshold=Var("hdFSRCompositionThreshold","HD: R1 changed-colour threshold 0..1; not true material alpha, default 0.1 (AMD starting value)","0.1");
 debugCheck=Var("hdFSRDebugCheck","HD: AMD SDK debug checking 1 on (messages in the log) / 0 off (no checking cost); applied when the FSR/FG context is recreated; default 0 for play","0");
 reactiveCutoff=Var("hdFSRReactiveCutoff","HD: R1 reactive values below this become 0 (0..1, default 0.1; 0 = no cutoff)","0.1");
 windMotion=Var("hdFSRWindMotion","HD: M4 previous tree wind / grass phases (M1 and M2/M4 assets; default off)","0");
 skeletalMotion=Var("hdFSRSkeletalMotion","HD: M3 per-instance previous bone palettes (M1 + M2/M3 shader assets required); wind excluded","0");
 rigidMotion=Var("hdFSRRigidMotion","HD: M2 rigid object velocity (requires M1 + validated shader overlay); no skeletal/wind velocities","0");
 objectMotion=Var("hdFSRObjectMotion","HD: M1 optional object motion attachment/composition; no object writers before M2", "0");
 debugView=Var("hdFSRDebugView","HD: 0 normal / 1 final motion vectors / 2 same x16 / 3 object markers only / 4 R1 reactive / 5 R1 composition (diagnostic, not archived)","0",0);
 up=Var("hdFSR","HD: FSR temporal upscaling request, 0 off / 1 on","0");
 fg=Var("hdFSRFrameGeneration","HD: FSR frame generation request, independent of upscaling","0");
 quality=Var("hdFSRQuality","HD: FSR 0 Native AA / 1 Quality / 2 Balanced / 3 Performance","1");
 sharp=Var("hdFSRSharpness","HD: FSR sharpening strength 0..1","0.2");
 info=Var("hdFSRInfo","HD: applied FSR upscaling status (not archived)","non branche",0);
 fgInfo=Var("hdFSRFrameGenerationInfo","HD: applied FSR generation status (not archived)","non branche",0);
#ifdef WOWDECOMP_DLSS
 HD::Dlss::Settings::Register(); // HD 2026-11-02: hdDLSS & co.
#endif
 test=Var("hdFSRTest","HD: FSR test mode (not archived): 0 off, 1 world target + plain stretch, 2 FSR camera-only motion, 3 as 2 with jitter sign flipped","0",0);
}
Options Read(){
 Options o;if(!up)return o;
 o.upscale=up->m_intValue!=0;o.generate=fg&&fg->m_intValue!=0;
 o.quality=uint32_t(std::clamp(quality?quality->m_intValue:1,0,3));
 const float s=sharp?sharp->m_floatValue:.2f;o.sharpness=std::isfinite(s)?std::clamp(s,0.f,1.f):.2f;
#ifdef WOWDECOMP_DLSS
 // HD 2026-11-02: hdDLSS 1 is an upscaling request of its own (the same world target, depth, motion and jitter),
 // DLSS taking the FSR upscaler's place in the pass.
 if(HD::Dlss::Settings::Requested()){o.upscale=true;o.dlss=true;}
#endif
 return o;
}
uint32_t TestMode(){return test?uint32_t(std::clamp(test->m_intValue,0,3)):0u;}
bool WindMotion(){return windMotion&&windMotion->m_intValue!=0;}
bool SkeletalMotion(){return skeletalMotion&&skeletalMotion->m_intValue!=0;}
bool RigidMotion(){return rigidMotion&&rigidMotion->m_intValue!=0;}
bool ObjectMotion(){return objectMotion&&objectMotion->m_intValue!=0;}
uint32_t DebugView(){return debugView?uint32_t(std::clamp(debugView->m_intValue,0,5)):0u;}
bool ValidationCapture(){return validationCapture&&validationCapture->m_intValue!=0;}
bool ReactiveMasks(){return reactiveMasks&&reactiveMasks->m_intValue!=0;}
float ReactiveScale(){const float v=reactiveScale?reactiveScale->m_floatValue:.9f;return std::isfinite(v)?std::clamp(v,0.f,2.f):.9f;}
float CompositionThreshold(){const float v=compositionThreshold?compositionThreshold->m_floatValue:.1f;return std::isfinite(v)?std::clamp(v,0.f,1.f):.1f;}
bool DebugCheck(){return debugCheck&&debugCheck->m_intValue!=0;}
float ReactiveCutoff(){const float v=reactiveCutoff?reactiveCutoff->m_floatValue:.1f;return std::isfinite(v)?std::clamp(v,0.f,1.f):.1f;}
void Publish(const Decision& d){Status(info,previousUp,sizeof previousUp,d.upscaleWhy);Status(fgInfo,previousFg,sizeof previousFg,d.generationWhy);}
}
