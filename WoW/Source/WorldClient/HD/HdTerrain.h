#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDTERRAIN_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDTERRAIN_H

// Waystone HD: our own terrain shaders (not part of the original client).
// They are the 3.3.5 terrain shaders rewritten in HLSL (checked against Blizzard's bytecode for all 128 vertex and
// 224 pixel permutations), compiled when the game starts, with WoW Forever's terrain lighting added:
//   - the ambient comes from three colours: sky (ground facing up), horizon (walls), ground (overhangs);
//     the client's ambient is the sky colour, horizon and ground are it times Forever's own horizon / sky and
//     ground / sky (LightData HorizonAmbientColor, GroundAmbientColor at this hour, forever_data\hd_forever_ambient.txt),
//     so with hdForeverLight 1 the three colours are exactly Forever's;
//   - a soft wrap of the sun over that ambient (x0.7 facing away from the sun .. x1.1 facing it);
//   - the sun is cut fully by the shadow, the ambient is not touched by it (3.3.5 darkens everything to 70%).
//   hdTerrainLight 1 = Forever lighting (default)
//                  2 = our shaders with the 3.3.5 lighting: must look exactly like 0 (proves the swap changes nothing)
//                  3 = test view: only the sun after the shadow (black in shadow), proves our shader is the one drawing
//                  0 = Blizzard's shaders
//   hdLightAmbient ambient strength in Forever lighting, 1 = Forever's (a test knob, keep it at 1)
//   hdTerrainInfo  read only, compile result and how many shaders hold our code:  /run print(GetCVar("hdTerrainInfo"))
//   hdLights (HdLights.h) with hdTerrainLight 1: lamps and fires light the ground per pixel, WoW Forever's maths

#include <cstdint>

class C3Vector;     // HD: hdLights
class CAaSphere;
class CGxShader;
class CM2Scene;

namespace HD {
    enum TerrainShaderKind {
        TerrainVertex = 0,   // CMap::vertexShader_Terrain[128]
        TerrainPixel1 = 1,   // CMap::pixelShader_Terrain1[32] (no dynamic shadow)
        TerrainPixel3 = 2,   // CMap::pixelShader_Terrain3[96] (Terrain3 or Terrain3_pcf, read from the shader's name)
    };

    // Starts compiling our terrain shaders on a background thread (once; called from CMap::Initialize).
    void TerrainShadersStart();

    // Called where the client picks a terrain shader: gives it our code or Blizzard's, to match hdTerrainLight.
    void TerrainShader(CGxShader* shader, int32_t kind, int32_t permutation);

    // Sets vertex constants c49..c53 and pixel constants c16.. for our shaders (called after the client's per-chunk
    // terrain constants). HD: hdLights picks the point lights for the chunk's sphere from the scene's lights, with
    // positions relative to camera (CWorldScene::s_activeWorldView).
    void TerrainVertexConstants(const CAaSphere* sphere, CM2Scene* scene, const C3Vector* camera);

    // Forever's horizon / sky and ground / sky ambient ratios (per channel) where the camera is, the ones the terrain
    // uses (redone at most every 50 ms); also used by the buildings (HdMapObj.cpp).
    void TerrainHemisphere(float* horizon, float* ground);

    // hdLightAmbient: ambient strength in Forever lighting (1 = Forever's), for the terrain and the buildings.
    float LightAmbientStrength();
}

#endif
