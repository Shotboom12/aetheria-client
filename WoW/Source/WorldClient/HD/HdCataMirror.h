#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDCATAMIRROR_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDCATAMIRROR_H

// HD: the engine side of the Cata water's reflection under Vulkan (step B). The device draws the mirror
// (Engine/Source/Gx/CGxDeviceVulkan/hd/CataMirror.cpp) by replaying the frame's opaque world with a mirrored
// camera; the engine gives it the plane and makes sure the world it captured holds what the mirror sees.
class C3Vector;
class C44Matrix;
class CAaBox;
namespace Liquid { class IGeomFactory; }

namespace HD {
// Before the liquid pass (CWorldScene::RenderLiquidPass, Vulkan seam): the plane of the nearest flat outdoor
// water queued this frame, into g_gxCataPlane. Nothing while no mirror wants one.
void CataMirrorChoosePlane();
// In CWorldScene::Render, once the main frustum's planes are set: while a plane was found last frame, the culling
// frustum becomes a tall box around the main frustum's ground footprint (a mirror over horizontal water sees the
// same ground area, mirrored in height only), so objects off the screen are drawn and captured too. It is written
// as the frustum's corners, so the copies that rebuild their planes from them (CFrustum::Transform for the map
// object batches) keep it: setting only the planes was lost there (boats popping out, 2026-10-26).
void CataMirrorWidenCulling();
// CWorldScene::FrustumSet(corners, rect) and FrustumSet(rect), after they set the planes: the same widening when they
// rebuilt the MAIN frustum (s_frustumCorners, the whole screen). Every exterior cull that rebuilds its frustum goes
// through them (map object groups, their batches through the group frustum lists, the sort table); a portal's
// sub-rectangle and the horizon's own corners keep their frustum.
void CataMirrorWidenFrustumSet(const C3Vector* corners, float minX, float minY, float maxX, float maxY);
// The world box of a map object group's water (a CMeshGeomFactory instance: its vertices stay in the group's space,
// the instance transform places them). False for any other water (chunk water bakes its transform in). The plane
// choice and HdCataWater's "is this the mirrored water" test both use it: the instance's bounds are the whole
// group's sphere (CWorldScene liquid queueing), not the water's, so their centre is not the water height.
bool CataMirrorMeshWaterBox(const Liquid::IGeomFactory* factory, const C44Matrix& transform, CAaBox& box);
// HD L19 (2026-11-03): the lowest and highest water of a chunk water instance (its 2x2 chunk batch) seen by this
// frame's CataMirrorChoosePlane, found by the instance's geometry factory. False for map object water and for water
// the choice did not see. HdCataWater uses it so that a batch whose water spans the plane takes the reflection too.
bool CataMirrorInstanceRange(const Liquid::IGeomFactory* factory, float& low, float& high);
// HD L19b (2026-11-04): true when this chunk water batch is joined to the plane's water: it touches the plane, or it
// sits next to (borders within a yard) a joined batch and its water heights meet that batch's (a stream climbing out
// of the lake, batch after batch). Chunk water only; computed by CataMirrorChoosePlane once the plane is chosen.
bool CataMirrorInChain(const Liquid::IGeomFactory* factory);
}

#endif
