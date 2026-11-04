// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef HD_FSR_MOTION_GLSL
#define HD_FSR_MOTION_GLSL
// Both clips MUST include their own model/bones/wind and camera, exclude jitter,
// and be AFTER the translated shader's Vulkan Y flip. UV origin is top-left.
// Fragment input positions need normal perspective interpolation; do not interpolate UV differences.
vec2 HdFsrMotion(vec4 currentClip,vec4 previousClip,out bool valid){
 valid=currentClip.w>1e-7&&previousClip.w>1e-7;
 if(!valid)return vec2(0);
 vec2 value=.5*(previousClip.xy/previousClip.w-currentClip.xy/currentClip.w);
 valid=!any(isnan(value))&&!any(isinf(value));return valid?value:vec2(0);
}
#endif
