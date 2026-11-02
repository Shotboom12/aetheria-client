#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDCATAWATER_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDCATAWATER_H

// HD: Cataclysm 4.3.4's water material on the 3.3.5 water, under Vulkan (hdCataWater 1; step C of the port).
// Flat outdoor water of a Cata "water" material (LiquidType material 1: lakes, rivers, oceans) is drawn the way
// Cata's CMaterialWater::SetupRender does at water detail 2, above the water: its shaders CataProcWater /
// CataProcWaterAbove, its LiquidType row's 18 floats, its lighting and fog, its transforms and screen constants, and
// its samplers: s0 the scene copied before the water, s1 the reflection (Engine/Source/Gx/CGxDeviceVulkan/hd/), s2 the
// depth ramp, s3 the scene again (Cata blurs it; step D), s5 the surface normals, s6 the scene depth, s7 the foam.
// Map object (WMO) water too, with its instance transform. Everything else (the camera under water, magma, slime,
// D3D9, missing files) keeps the 3.3.5 water.
//
// Files next to WowClient64.exe, copied from the private Cata runtime by the install script:
//   CataWater\DBFilesClient\LiquidType.dbc   Cata's (45 fields), read here only, never by the 3.3.5 DB
//   CataWater\shaders.txt                    "<vertex|pixel> <family> <permutation> <relative path>" per line
//   CataWater\bytecode\...                   the shaders' DX9 bytecode (their SPIR-V is in the device's manifest)
//   CataWater\Textures\...                   the BLPs the rows name (surface normals, foam)

#include <cstdint>

class C3Vector;
class C44Matrix;
class CAaSphere;

namespace Liquid {
    class CMaterialSettings;
    class IEnvironment;
    class IGeomFactory;
}

namespace HD {
    bool CataWaterRippleMaterialReady();
    // CWorldScene::RenderLiquidPass, Vulkan seam, before the device's BeforeLiquids stages: forgets the previous
    // frame's inputs (the device's bridge stage sets them again) and notes whether the camera is in a liquid.
    void CataWaterBeginLiquidPass();

    // Liquid::CMaterialProcWater::Render, first thing: draws the instance with the Cata material and returns true,
    // or returns false and the 3.3.5 material draws it.
    bool CataWaterRender(Liquid::IEnvironment* environment, Liquid::IGeomFactory* geomFactory, const C3Vector& cameraPos,
                         const C44Matrix* transform, const CAaSphere* bounds, Liquid::CMaterialSettings* settings);

    // HD: Liquid::CMaterialMagma::Render, first thing: hdCataMagma 1 under Vulkan draws lava and slime (LiquidType
    // material 2) the way Cata 4.3.4's CMaterialMagma does at water detail > 0 (shaders CataProcMagma, the row's three
    // layers and the noise volume, all in CataWater\) and returns true; false = the 3.3.5 magma draws it.
    bool CataMagmaRender(Liquid::IEnvironment* environment, Liquid::IGeomFactory* geomFactory, const C3Vector& cameraPos,
                         const C44Matrix* transform, const CAaSphere* bounds, Liquid::CMaterialSettings* settings);
}

#endif
