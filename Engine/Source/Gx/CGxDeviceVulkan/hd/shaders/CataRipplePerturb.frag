// SPDX-License-Identifier: GPL-3.0-or-later
// HD: Cata PerturbDisplMap (pixel shader): perturbTex's red times the perturbation's height, added to x
// (additive blend); y unchanged.
#version 450
layout(set = 0, binding = 0) uniform sampler2D perturbTex;
layout(location = 0) in vec3 uvZ;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(texture(perturbTex, uvZ.xy).x * uvZ.z, 0.0, 0.0, 1.0);
}
