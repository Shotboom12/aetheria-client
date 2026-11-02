#ifndef ENGINE_SOURCE_GX_GX_SEAM_H
#define ENGINE_SOURCE_GX_GX_SEAM_H

#include <cstdint>

// HD: the points where the Vulkan device (CGxDeviceVulkan, adapted from wxl-vulkan-api) brackets an engine
// function: wxl's hook points, as direct calls. The function asks GxSeamEnter first thing; when a bracket is
// attached it calls the bracket (GxSeamBracket) and returns. The bracket's innermost "original" calls the same
// function again with GxSeam::m_reenter set, so that call runs the function's own body. Nothing is attached
// unless the Vulkan device is up, so under D3D9 or OpenGL every check is false.
enum EGxSeam {
    GxSeam_WorldOnRender = 0,   // CGWorldFrame::OnWorldRender           void(worldFrame, unused)
    GxSeam_DetailDoodads = 1,   // CWorldScene::RenderDetailDoodads      void()
    GxSeam_LiquidPass = 2,      // CWorldScene::RenderLiquidPass         void()
    GxSeam_DrawBatchDoodad = 3, // CM2SceneRender::DrawBatchDoodad       uint32_t(render, unused, elements, indices)
    GxSeam_WmoExterior = 4,     // CMapObj::ExteriorRender               void(mapObj, unused, group, index)
    GxSeam_WmoInterior = 5,     // CMapObj::InteriorRender               void(mapObj, unused, group, index)
    GxSeams_Last = 6
};

struct GxSeam {
    void* m_bracket;
    int32_t m_reenter;
};

extern GxSeam g_gxSeams[GxSeams_Last];

inline bool GxSeamEnter(EGxSeam seam) {
    GxSeam& s = g_gxSeams[seam];

    if (s.m_reenter) {
        s.m_reenter = 0;
        return false;
    }

    return s.m_bracket != nullptr;
}

template <class Fn>
inline Fn GxSeamBracket(EGxSeam seam) {
    return reinterpret_cast<Fn>(g_gxSeams[seam].m_bracket);
}

// HD: while the Vulkan device is up, whether it can draw a shader's DX9 bytecode (its translated module is in
// Vulkan\shaders\translated\manifest.txt); null otherwise. The HD materials ask it before swapping their code in.
extern int32_t (*g_gxShaderKnown)(const void* code, uint32_t size);

// HD: the Cata water's reflection plane, shared by the engine (WoW/Source/WorldClient/HD/HdCataMirror.cpp) and the
// Vulkan device (CGxDeviceVulkan/hd/CataMirror.cpp). The device sets m_wanted while hdCataWater is on and its mirror
// is up; the engine then picks the plane of the nearest flat outdoor water each frame, before the liquid pass, and
// widens its culling to everything a mirror over that water can see. m_frame counts the engine's choices.
struct GxCataPlane {
    int32_t m_wanted;   // device: a mirror wants a plane
    int32_t m_valid;    // engine: m_height is this frame's plane
    float m_height;     // world z of the water surface
    float m_eyeZ;       // engine: the camera's world z when it chose (CWorldScene::s_activeWorldView)
    uint32_t m_frame;   // engine: incremented at every choice
};

extern GxCataPlane g_gxCataPlane;

// HD: the Cata water material's inputs under Vulkan (step C). The engine (WoW/Source/WorldClient/HD/HdCataWater.cpp)
// makes three small textures and puts them in m_proxy; the device (CGxDeviceVulkan/hd/CataWaterService.cpp) makes a
// draw that binds one sample this frame's image instead: 0 the scene copied before the water, 1 the reflection,
// 2 the copy's depth. Each frame the device sets m_ready (and m_mirror) at BeforeLiquids, before the engine draws its
// liquids, and clears them at BeforeUi; the engine draws the Cata material only while m_enabled and m_ready.
struct GxCataWater {
    int32_t m_enabled;   // device: hdCataWater 1 and the services are up
    int32_t m_ready;     // device: this frame's scene copy (colour and depth) is there
    int32_t m_mirror;    // device: this frame's reflection is there, drawn for g_gxCataPlane.m_height
    void* m_proxy[3];    // engine: the CGxTex* standing for the scene colour, the reflection, the depth
    uint32_t m_drawn;     // engine: water instances drawn with the Cata material (the device logs these)
    uint32_t m_kept[12];  // engine: instances left to the 3.3.5 material, by reason (HdCataWater.cpp CataWaterKept)
    char m_info[256];     // engine: hdCataWaterInfo's text
    void* m_proxySky;     // engine: the CGxTex* standing for the sky's reflection (the water without a mirror)
    int32_t m_sky;        // device: this frame's sky reflection is there
    void* m_proxyBlur;    // HD: engine: the CGxTex* standing for Cata's blurred back buffer (s3 without a mirror)
};

extern GxCataWater g_gxCataWater;

// HD: the sky for the Cata water's reflections (Cata draws the water without a mirror with a reflection of the sky
// alone). The engine (HdCataWater.cpp) fills it before each liquid pass from DayNight's sky dome (DNSky), reduced to
// its rings, top first; the device (CGxDeviceVulkan/hd/CataMirror.cpp) draws it under the mirror and alone.
struct GxCataSky {
    uint32_t m_count;           // rings, 0: no sky this frame
    float m_sinElevation[8];    // the sine of each ring's elevation seen from the camera
    uint32_t m_color[8];        // its mean colour, r | g << 8 | b << 16
    void* m_tex;                // HD: tranche 3, the CGxTex* render target the engine drew the game's sky into this frame,
                                // mirrored and upside down (HdCataWater.cpp); nullptr: the rings only
};

extern GxCataSky g_gxCataSky;

// HD: Cata's dynamic displacement map (the water that answers the characters, tranche 2): Cata Liquid::UpdateDisplMap
// and its perturbations (LiquidProcTextures.cpp). The engine (WoW/Source/WorldClient/HD/HdCataRipples.cpp) gathers
// before each liquid pass what perturbs the water this frame; the device (CGxDeviceVulkan/hd/CataRipples.cpp) runs the
// maps at BeforeLiquids and says where they are; the engine's water (HdCataWater.cpp) samples them on s4 and s8.
// The arrays are the engine's and stay valid until the next liquid pass.
struct GxCataRipples {
    // engine
    int32_t m_wanted;                 // the Cata water was drawn last frame (Cata Liquid::NeedDynDisplMap)
    uint32_t m_time;                  // FrameTime, ms
    float m_center[3];                // the player (Cata CWorldScene::s_playerPos)
    const float* m_units;             // surface 0, kept: x, y, radius, amplitude, period ms (the units, Cata CGUnit_C)
    uint32_t m_unitCount;
    const float* m_events;            // surface 0, this frame only: x, y, radius, amplitude, 0 (the rain's drops)
    uint32_t m_eventCount;
    const float* m_meshVerts;         // surface 1: x, y of each triangle corner (the moving ships' hulls)
    const float* m_meshes;            // per mesh: first corner, corners, amplitude, period ms
    uint32_t m_meshCount;
    const uint8_t* m_texels;          // XTextures\Ripples\perturbTex, RGBA8, its levels one after the other
    uint32_t m_texWidth, m_texHeight, m_texLevels;
    void* m_proxy[2];                 // the CGxTex* standing for the two maps
    // device
    int32_t m_active;                 // the service runs (hdCataWater 1)
    int32_t m_ready;                  // this frame's maps are there, m_proxy[] stand for them
    float m_rawPos[2][3];             // each map's corner in the world (Cata GetDisplMapInfoRaw)
    float m_size[2][2];               // its size in yards
    uint32_t m_texSize[2];            // its texels a side
    float m_interpolation[2];         // between its last two steps (Cata GetDynamicDisplMapInterpolation)
};

extern GxCataRipples g_gxCataRipples;

#endif
