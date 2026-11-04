// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace HD::Fsr::Masks {
// AMD FSR3 auto-reactive, USE_COMPONENTS_MAX, no tone-map flag. Values below `cutoff` become 0 (cutoff 0 = exactly AMD without the threshold flag).
// The binary composition estimate is ours: it is NOT material alpha coverage.
inline float Scale(float v){return std::isfinite(v)?std::clamp(v,0.f,2.f):.9f;}
inline float Threshold(float v){return std::isfinite(v)?std::clamp(v,0.f,1.f):.5f/255.f;}
inline float Cutoff(float v){return std::isfinite(v)?std::clamp(v,0.f,1.f):0.f;}
inline float Delta(const float* before,const float* after){float d=0;for(int i=0;i<3;++i){if(!std::isfinite(before[i])||!std::isfinite(after[i]))return 1;d=std::max(d,std::fabs(after[i]-before[i]));}return d;}
inline float Reactive(float delta,float scale,float cutoff=0.f){const float r=std::clamp(delta*Scale(scale),0.f,1.f);return r<Cutoff(cutoff)?0.f:r;}
inline float Composition(float delta,float threshold){const float t=Threshold(threshold);return std::clamp((delta-.5f*t)/std::max(t,1.f/255.f),0.f,1.f);}
struct Sequence {
 uint64_t serial=0;uint32_t step=0;bool failed=true;
 void Begin(uint64_t id){serial=id;step=0;failed=id==0;}
 bool Accept(uint64_t id,uint32_t expected){if(failed||id!=serial||expected!=step||step>=4){failed=true;return false;}++step;return true;}
 bool Ready(uint64_t id)const{return !failed&&serial==id&&step==4;}
 void Fail(){failed=true;}
};
}
