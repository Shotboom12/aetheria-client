// SPDX-License-Identifier: GPL-3.0-or-later
// HD: Cata PropagateDisplMap (pixel shader), the wave step of the displacement map: x the height now, y the one
// before. new = (four neighbours' x) / 2 - y, faded to 0 over the map's outer 1/16 and damped by 0.97; the old x
// becomes y. The map moved with the player since the last step: the previous one is read `offset` further.
#version 450
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(push_constant) uniform Push { vec2 texel; vec2 offset; } pc;
layout(location = 0) out vec4 outColor;
void main() {
    vec2 uv = gl_FragCoord.xy * pc.texel + pc.offset;
    vec2 edge = min((1.0 - uv) * 16.0, uv * 16.0);
    float fade = clamp(min(edge.y, edge.x), 0.0, 1.0) * 0.97;
    float sum = texture(prev, uv + vec2(-pc.texel.x, 0.0)).x + texture(prev, uv + vec2(0.0, -pc.texel.y)).x +
                texture(prev, uv + vec2(pc.texel.x, 0.0)).x + texture(prev, uv + vec2(0.0, pc.texel.y)).x;
    vec4 c = texture(prev, uv);
    outColor = vec4(fade * (sum * 0.5 - c.y), c.x, 0.0, 1.0);
}
