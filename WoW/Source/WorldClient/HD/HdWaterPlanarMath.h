#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDWATERPLANARMATH_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDWATERPLANARMATH_H

#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/Tempest/CFrustum.h"

namespace HD { namespace PlanarMath {
    inline C44Matrix ReflectedView(const C44Matrix& view) {
        C44Matrix mirror = view;
        mirror.c0 = -view.c0; mirror.c1 = -view.c1; mirror.c2 = -view.c2;
        return mirror;
    }
    inline C3Vector ReflectedCamera(const C3Vector& camera, float height) {
        return C3Vector(camera.x, camera.y, 2.0f * height - camera.z);
    }
    inline C44Matrix MainToMirror(const C44Matrix& view, const C44Matrix& projection,
                                  const C3Vector& camera, float height) {
        C44Matrix shift;
        shift.Translate(camera - ReflectedCamera(camera, height));
        return view.Inverse() * shift * ReflectedView(view) * projection;
    }
    inline C4Plane ClipPlane(const C44Matrix& reflectedView, const C44Matrix& projection,
                              float mirrorCameraZ, float height, float bias) {
        C44Matrix inverse = (reflectedView * projection).Inverse();
        C4Vector relative;
        relative.x = relative.y = 0.0f;
        relative.z = 1.0f; relative.w = mirrorCameraZ - height - bias;
        C4Vector clip = relative * inverse.Transpose();
        C4Plane plane;
        plane.n = C3Vector(clip.x, clip.y, clip.z); plane.d = clip.w;
        return plane;
    }
    inline void ReflectFrustum(CFrustum& frustum, float height) {
        // Preserve the inward normals. Rebuilding planes from mirrored corners reverses their winding.
        for (auto& plane : frustum.planes) {
            plane.d += 2.0f * height * plane.n.z;
            plane.n.z = -plane.n.z;
        }
        for (auto& corner : frustum.corners) corner.z = 2.0f * height - corner.z;
        frustum.lookPos = ReflectedCamera(frustum.lookPos, height);
        frustum.lookAt = ReflectedCamera(frustum.lookAt, height);
    }
} }

#endif
