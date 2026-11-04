// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace HD::Fsr {
struct Options {bool upscale=false,generate=false;uint32_t quality=1;float sharpness=.2f;bool dlss=false;}; // dlss: HD 2026-11-02, hdDLSS (WOWDECOMP_DLSS only)
struct Readiness {
 bool vulkan=false,runtime=false,deviceFeatures=false,worldTarget=false,depth=false,motion=false;
 bool nativeUi=false,reactiveMasks=false,upscaleValidated=false,generationValidated=false,pacingSwapchain=false;
};
struct Decision {bool upscale=false,generate=false;const char* upscaleWhy="desactive";const char* generationWhy="desactive";};
inline Decision Decide(const Options& o,const Readiness& r){
 Decision d;
 auto common=[&]()->const char*{
  if(!r.vulkan)return "Vulkan requis";if(!r.runtime)return "runtime FSR absent";
  if(!r.deviceFeatures)return "fonctions GPU indisponibles";if(!r.worldTarget)return "cible monde separee manquante";
  if(!r.depth)return "profondeur normalisee manquante";if(!r.motion)return "mouvement complet manquant";
  if(!r.nativeUi)return "interface native manquante";return nullptr;
 };
 const char* why=common();
 if(o.upscale){d.upscaleWhy=why?why:!r.reactiveMasks?"masques transparents manquants":!r.upscaleValidated?"validation upscaler requise":"actif";d.upscale=!why&&r.reactiveMasks&&r.upscaleValidated;}
 if(o.generate){d.generationWhy=why?why:!r.pacingSwapchain?"presentation FSR manquante":!r.generationValidated?"validation FG requise":"actif";d.generate=!why&&r.pacingSwapchain&&r.generationValidated;}
 return d; // FG never forces upscaling; upscaling never silently enables FG.
}
// Motion uses top-left origin UV, previous minus current, without camera jitter.
// clip is AFTER wxl's Y-flip, BEFORE viewport mapping, for one previous/current vertex pair.
struct Clip {double x,y,z,w;};
inline bool Motion(const Clip& now,const Clip& before,double& x,double& y){
 if(!std::isfinite(now.x)||!std::isfinite(now.y)||!std::isfinite(now.w)||
    !std::isfinite(before.x)||!std::isfinite(before.y)||!std::isfinite(before.w)||now.w<=1e-7||before.w<=1e-7){x=y=0;return false;}
 x=.5*(before.x/before.w-now.x/now.w);y=.5*(before.y/before.w-now.y/now.w);
 if(!std::isfinite(x)||!std::isfinite(y)){x=y=0;return false;}return true;
}
inline bool NormalizedDepth(float raw,float minimum,float maximum,float& result){
 if(!std::isfinite(raw)||!std::isfinite(minimum)||!std::isfinite(maximum)||maximum<=minimum)return false;
 result=std::clamp((raw-minimum)/(maximum-minimum),0.f,1.f);return true;
}
// These calculations are for tests / planning. Use Core::Resolution / Core::Jitter at runtime.
inline uint32_t PhaseCount(uint32_t renderWidth,uint32_t displayWidth){
 if(!renderWidth||displayWidth<renderWidth||displayWidth>16384)return 0;
 return uint32_t(8.0*double(displayWidth)*displayWidth/(double(renderWidth)*renderWidth));
}
}
