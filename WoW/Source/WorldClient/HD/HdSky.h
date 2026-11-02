#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDSKY_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDSKY_H

// Waystone HD: WoW Forever's sky dome and cloud shaders (dnsky, dnclouds) on the 3.3.5 sky (not part of the original
// client). 3.3.5 draws the sky with the fixed-function pipeline; these shaders draw the same meshes, colours and
// cloud texture, and add what Forever's shaders add:
//   - sky: a glow toward the sun (the moon at night), growing as the cube from the edge of a cone to the sun,
//     stronger at and below the horizon, mixed in linear light; then a 1-step dither against banding;
//   - clouds: the sun shining through thin cloud near the sun (base x (1 + sun x N.L^2 x (1 - alpha) x strength)).
// The sun, moon, stars, glares and zone skyboxes stay as they are (Forever's planet shader is the same modulate).
// Forever has no volumetric or ray-marched clouds: its only cloud shader is this textured dome.
//   hdSky             1 = Forever sky and clouds (default)
//                     0 = Blizzard's fixed-function sky, untouched
//                     2 = our shaders with the Forever terms off: must look exactly like 0 (proves the swap)
//                     3 = test view: sky = glow amount in grey, clouds = sun scatter in red
//   hdSkyGlow         glow strength above the horizon (Forever DNSky r0.y; the value Forever uses is not in its
//                     shaders or data we have: tune against a Forever capture), 0 = no glow
//   hdSkyGlowHorizon  glow strength at and below the horizon (Forever DNSky r2.w)
//   hdSkyGlowSize     glow cone half-angle in degrees (Forever DNSky r0.x = its cosine)
//   hdSkyCloudSun     sun scatter strength in thin cloud (Forever DNClouds r0.w), 0 = off
//   hdSkyDither       dither amplitude in 1/255 steps (Forever DNSky/DNClouds r3.x), 0 = off
//   hdSkyInfo         read only, status:  /run print(GetCVar("hdSkyInfo"))
// The colours are the client's: with hdForever 1 they are Forever's LightData colours (sky bands, sun colour for the
// glow, cloud sun colour for the scatter).

#include <cstdint>

namespace HD {
    enum SkyPass {
        SkyPass_Dome = 0,   // DNSky::Render (vertices: position, colour)
        SkyPass_Clouds = 1, // DNClouds::Render (vertices: position, colour, texture coordinate)
    };

    // Called by DNSky::Render / DNClouds::Render after their render states, before the draw (inside their RsPush):
    // binds our shaders and constants and returns true, or returns false and touches nothing (hdSky 0, shaders not
    // compiled yet or not usable): the fixed-function draw then happens as before.
    bool SkyBeginDraw(int32_t pass);

    // Called after the draw when SkyBeginDraw returned true: gives the client its shader constants back
    // (the shaders themselves are unbound by the caller's RsPop).
    void SkyEndDraw();
}

#endif
