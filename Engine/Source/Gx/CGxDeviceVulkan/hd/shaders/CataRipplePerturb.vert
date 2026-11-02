// SPDX-License-Identifier: GPL-3.0-or-later
// HD: Cata's dynamic displacement map (CataRipples.cpp), PerturbDisplMap / PerturbDisplMapMesh (vertex shaders):
// a perturbation's corner, already in Vulkan NDC, its height in z: o0 = (x, y, 0, 1), o1 = (uv, z).
#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 0) out vec3 outUvZ;
void main() {
    gl_Position = vec4(inPos.xy, 0.0, 1.0);
    outUvZ = vec3(inUv, inPos.z);
}
