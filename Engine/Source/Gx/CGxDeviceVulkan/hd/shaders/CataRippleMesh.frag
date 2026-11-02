// SPDX-License-Identifier: GPL-3.0-or-later
// HD: Cata PerturbDisplMapMesh (pixel shader): the mesh's height added to x (additive blend).
#version 450
layout(location = 0) in vec3 uvZ;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(uvZ.z, 0.0, 0.0, 1.0);
}
