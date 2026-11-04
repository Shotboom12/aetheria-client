// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace HD::Lamp {
inline uint32_t PackChannel(float actual,float low,float high){auto q=[](float v){return uint32_t(int(std::round(std::clamp(v,-1.0f,1.0f)*511))+511);};return q(actual)|(q(low)<<10)|(q(high)<<20);}
inline std::array<float,3> UnpackChannel(uint32_t v){return{(float(v&1023u)-511)/511,(float((v>>10)&1023u)-511)/511,(float((v>>20)&1023u)-511)/511};}
inline float CombinedDelta(float actual,float low,float high,float sunVisibility,float sunStrength){return actual+std::clamp(sunStrength,0.0f,1.0f)*(low+(high-low)*std::clamp(sunVisibility,0.0f,1.0f)-actual);}
}
