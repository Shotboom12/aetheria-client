// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
namespace HD::Fsr::ObjectMotion {
inline bool Accept(float x,float y,float marker) {return marker==1.0f&&std::isfinite(x)&&std::isfinite(y);}
inline bool AttachmentLimit(uint32_t colours,uint32_t fragmentOutputs,uint32_t fragmentCombined) {
 return colours>=7&&fragmentOutputs>=7&&fragmentCombined>=7;
}
// Marked xy is TOTAL previous UV - current UV (camera + object), never an offset added to camera motion.
constexpr uint32_t AttachmentLocation=6;
constexpr uint32_t BytesPerPixel=8;
}
