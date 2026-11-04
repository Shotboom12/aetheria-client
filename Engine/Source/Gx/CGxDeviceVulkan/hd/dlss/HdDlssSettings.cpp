// SPDX-License-Identifier: GPL-3.0-or-later
// HD 2026-11-02 (DLSS): console variables, same pattern as hd/fsr/HdFsrSettings.cpp.
#ifdef WOWDECOMP_DLSS
#include "HdDlssSettings.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace HD::Dlss::Settings {
namespace {
CVar *sharp=nullptr,*want=nullptr,*preset=nullptr,*flip=nullptr,*logLevel=nullptr,*info=nullptr;
char previous[200]{};
CVar* Var(const char* name,const char* help,const char* value,uint32_t flags=1){
 if(auto* p=CVar::LookupRegistered(name))return p;
 return CVar::Register(name,help,flags,value,nullptr,CATEGORY_GRAPHICS,false,nullptr,false);
}
}
void Register(){
 if(want)return;
 want=Var("hdDLSS","HD: NVIDIA DLSS Super Resolution instead of the FSR upscaler, 0 off (default) / 1 on; FSR frame generation stays independent","0");
 preset=Var("hdDLSSPreset","HD: DLSS render preset, 0 NVIDIA default / 10 J / 11 K / 12 L / 13 M","0");
 sharp=Var("hdDLSSSharpness","HD: sharpening after DLSS 0..2, 0 none / 1 FSR's maximum (default) / up to 2 reinforced","1");
 flip=Var("hdDLSSJitterFlip","HD: DLSS jitter sign inverted (diagnostic, not archived)","0",0);
 logLevel=Var("hdDLSSLog","HD: NVIDIA NGX messages in the journal, 0 off / 1 on (default) / 2 verbose; read when DLSS starts","1");
 info=Var("hdDLSSInfo","HD: applied DLSS status (not archived)","desactive",0);
}
bool Requested(){return want&&want->m_intValue!=0;}
uint32_t Preset(){return preset?uint32_t(std::clamp(preset->m_intValue,0,15)):0u;}
float Sharpness(){const float v=sharp?sharp->m_floatValue:1.f;return std::isfinite(v)?std::clamp(v,0.f,2.f):1.f;}
bool JitterFlip(){return flip&&flip->m_intValue!=0;}
uint32_t LogLevel(){return logLevel?uint32_t(std::clamp(logLevel->m_intValue,0,2)):1u;}
void Publish(const char* text){
 if(info&&text&&std::strcmp(previous,text)){info->Set(text,true,false,false,false);std::snprintf(previous,sizeof previous,"%s",text);}
}
}
#endif // WOWDECOMP_DLSS
