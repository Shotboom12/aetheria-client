#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDDETAILDOODAD_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDDETAILDOODAD_H

// Waystone HD: our own grass (detail doodad) shaders (not part of the original client).
// They are the 3.3.5 DetailDoodad shaders rewritten in HLSL (checked against Blizzard's bytecode for all 6 vertex
// and 8 pixel slots), compiled when the game starts, with WoW Forever's grass lighting (its detaildoodad shader
// lights grass exactly like its terrain, so the grass matches hdTerrainLight):
//   - the ambient comes from three colours: sky (ground facing up), horizon (walls), ground (overhangs); the client's
//     ambient is the sky colour, horizon and ground are it times Forever's own horizon / sky and ground / sky (the
//     same ratios as the terrain, HD::TerrainHemisphere); the normal is the terrain's under the tuft, as in 3.3.5;
//   - a soft wrap of the sun over that ambient (x0.7 facing away from the sun .. x1.1 facing it);
//   - the sun is cut fully by the shadow (player shadow, sun bands and the baked terrain shadow), the ambient is not
//     touched by it (3.3.5 darkens everything to 70%).
//   The fade at the end of the grass distance, the texture, the vertex colour and the fog stay exactly Blizzard's.
//   hdGrassLight 1 = Forever lighting (default)
//                2 = our shaders with the 3.3.5 lighting: must look exactly like 0 (proves the swap changes nothing)
//                3 = test view: only the sun after the shadow (black in shadow, no texture), proves our shader draws
//                0 = Blizzard's shaders
//   hdLightAmbient (HdTerrain's) ambient strength in Forever lighting, 1 = Forever's (a test knob, keep it at 1)
//   hdGrassWind, hdGrassWindSpeed, hdGrassWindDirection, hdGrassWindStrength, hdGrassReact, hdGrassReactRadius:
//                HD: wind in the grass and grass parting around the player, see HdGrassWind.h
//   hdGrassInfo  read only, compile result and how many shaders hold our code:  /run print(GetCVar("hdGrassInfo"))

#include <cstdint>

class C44Matrix;

namespace HD {
    // Starts compiling our grass shaders on a background thread (once; called from CDetailDoodad::Initialize).
    void DetailDoodadShadersStart();

    // Called in CDetailDoodad::InitializeRenderState just before the grass pixel shader is bound, once per frame:
    // gives the 6 vertex and 8 pixel grass shaders our code or Blizzard's (all of them together), to match
    // hdGrassLight, and sets pixel constant c15. With hdGrassLight 0 and nothing of ours in place it does nothing.
    void DetailDoodadShaders();

    // Called in CDetailDoodad::SetupVertexShader after the client's grass constants: vertex constants c49..c52 of our
    // vertex shader, HD: and c53..c59 grass wind for this chunk (HdGrassWind.h; world = the chunk's grass world
    // matrix) (nothing when Blizzard's shaders are in place).
    void DetailDoodadVertexConstants(const C44Matrix& world);

    // c68 for one batch: how its plant bends (windClass from HD::GrassWindClassOf). Not in the donor.
    void DetailDoodadBatchConstants(uint16_t windClass);

    // Called in CDetailDoodad::Destroy before the shaders are released: gives them Blizzard's code back.
    void DetailDoodadShadersForget();

    // hdGrassBuffers (DetailDoodad.cpp): called in CWorldScene::RenderDetailDoodads before the first grass draw,
    // fills the persistent grass buffers of the frame so no fill lands between two draws of the same pool.
    void DetailDoodadPrepareBuffers();

    // HD: hdGrassKeep. Frames a chunk's grass stays (buffers filled, instances placed) after it was last drawn,
    // instead of 3.3.5's 10 (buffers) and 20 (instances): turning the camera then finds it ready. 0 = native.
    uint32_t DetailDoodadKeepFrames();

    // HD 2026-11-04 lot C2: grass clock, +1 per CMap::ProcessDetailDoodadUpdateList. CDetailDoodadInst::m_age holds
    // the clock of the instance's last draw; its age (3.3.5's frame counter) is g_detailDoodadClock - m_age.
    extern uint32_t g_detailDoodadClock;
}

#endif

namespace HD {
class GrassConstantsScope {
 float vertex[44]{},pixel[4]{};bool valid=false;
public:
 GrassConstantsScope();~GrassConstantsScope();
 GrassConstantsScope(const GrassConstantsScope&)=delete;
 GrassConstantsScope& operator=(const GrassConstantsScope&)=delete;
};
}
