#version 450
// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria FSR object motion: turns the velocity vertex variants' two clip positions (location 16 current, 17 previous, both after the
// device's y flip, so +y is down the screen like the UV) into the motion FSR wants: previous UV minus current UV, written to an RG16F target.
layout(location = 16) in vec4 curClip;
layout(location = 17) in vec4 prevClip;
layout(location = 0) out vec4 outMotion;
void main()
{
    if (curClip.w <= 1e-5 || prevClip.w <= 1e-5) { outMotion = vec4(0.0); return; }
    vec2 c = curClip.xy / curClip.w;
    vec2 p = prevClip.xy / prevClip.w;
    outMotion = vec4((p - c) * 0.5, 0.0, 1.0); // alpha 1 marks "an object wrote motion here"
}

