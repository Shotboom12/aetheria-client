#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDMAPOBJ_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDMAPOBJ_H

// Waystone HD: our own building (WMO / MapObj) shaders (not part of the original client).
// They are the 3.3.5 MapObj shaders rewritten in HLSL (checked against Blizzard's bytecode for all 7 vertex families
// x 90 slots and 7 pixel families x 16 permutations), compiled when the game starts, with WoW Forever's lighting on
// exterior-lit batches (SetLighting 1 or 2), the same lighting as hdTerrainLight:
//   - the ambient comes from three colours: sky (roof facing up), horizon (walls), ground (overhangs); the client's
//     ambient is the sky colour, horizon and ground are it times Forever's own horizon / sky and ground / sky (the
//     same ratios as the terrain, HD::TerrainHemisphere);
//   - a soft wrap of the sun over that ambient (x0.7 facing away from the sun .. x1.1 facing it);
//   - the sun is cut fully by the shadow, the ambient is not touched by it (3.3.5 darkens everything to 70%).
//   Interior batches (unlit / MOCV, SetLighting 0 and 3) and the 3.3.5 two-pass interior / exterior blend of the
//   transition batches stay exactly Blizzard's. The vertex colour (MOCV) keeps the 3.3.5 rule of each path.
//   hdWmoLight 1 = Forever lighting on exterior-lit batches (default)
//              2 = our shaders with the 3.3.5 lighting: must look exactly like 0 (proves the swap changes nothing)
//              3 = test view: exterior-lit batches show only the sun after the shadow (black in shadow)
//              0 = Blizzard's shaders
//   hdLightAmbient (HdTerrain's) ambient strength in Forever lighting, 1 = Forever's (a test knob, keep it at 1)
//   hdWmoInfo  read only, compile result and how many shaders hold our code:  /run print(GetCVar("hdWmoInfo"))

#include <cstdint>

class CShaderEffect;

namespace HD {
    // Starts compiling our MapObj shaders on a background thread (once; called from CMapObj::Initialize).
    void MapObjShadersStart();

    // Called in CMapObj::SelectWorldShaders just before CShaderEffect::SetShaders, every batch: gives the vertex and
    // pixel shader about to be bound our code or Blizzard's (always both together), to match hdWmoLight, and sets
    // the constants our shaders read (vertex c49..c53, pixel c15). lightingMode = CMapObj::s_lightingMode.
    void MapObjShaders(CShaderEffect* effect, uint32_t vertexPermute, uint32_t pixelPermute, int32_t lightingMode);
}

#endif
