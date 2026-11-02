#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDCATARIPPLES_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDCATARIPPLES_H

// HD: the Cata water's dynamic displacement map, engine side (tranche 2). Cata 4.3.4 lets the water answer what moves
// in it: the units (CGUnit_C::CreateRipple, its Liquid::HasDynamicDisplMap branch), the rain (CWorldMap::UpdateRipples)
// and the moving ships' hulls (the same function's map object meshes). This gathers them each frame for the device,
// which steps Cata's two maps (Engine/Source/Gx/CGxDeviceVulkan/hd/CataRipples.cpp); the water samples them
// (HdCataWater.cpp). While it runs, the units make no 3.3.5 ring ripples, as in Cata.

class C3Vector;
class CGUnit_C;

namespace HD {
    // Cata Liquid::HasDynamicDisplMap: the device steps the maps (Vulkan, hdCataWater 1) and the Cata water was drawn.
    bool CataRipplesActive();
    void CataRipplesReset();

    // CGUnit_C::CreateRipple, after its liquid checks, while CataRipplesActive(): Cata's unit perturbation.
    void CataRipplesUnit(CGUnit_C* unit, float liquidHeight);

    // HD::CataWaterBeginLiquidPass: this frame's perturbations, the rain and the hulls (Cata UpdateRipples) handed to
    // the device's stage, which runs right after.
    void CataRipplesBeginFrame(const C3Vector& camera);
}

#endif
