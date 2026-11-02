// Procedural CPU port of candidate HdWaterRipples.cpp's analytic ring kernel.
// No Cata solver, shader-readiness change, perturbation texture or D3D9 resource.
#include "WoW/Source/WorldClient/HD/Forever/HdForeverRippleCpu.h"
#include "WoW/Source/WorldClient/HD/Forever/FieldEncoding.hpp"
#include "WoW/Source/WorldClient/WaterRipple.h"
#include <algorithm>
#include <cmath>

namespace HD::ForeverNative {
bool CollectRippleField(float cameraX, float cameraY, float planeZ, float strength,
    std::vector<FloatMip>& field, float uv[4]) {
    if (!uv || !std::isfinite(cameraX) || !std::isfinite(cameraY) || !std::isfinite(planeZ) ||
        !std::isfinite(strength) || strength < 0 || strength > 4) return false;
    // Keep the same 32-yard coverage while halving spatial resolution. The owner
    // preflight accounts this one-level RGBA16F image before any Vulkan allocation.
    constexpr uint32_t Side = 256, MaxRipples = 128;
    constexpr float Area = 32, HalfArea = Area / 2, Texel = Area / Side;
    const float cx = std::floor(cameraX / Texel) * Texel, cy = std::floor(cameraY / Texel) * Texel;
    if (!std::isfinite(cx) || !std::isfinite(cy)) return false;
    uv[0] = uv[1] = 1.0f / Area; uv[2] = 0.5f - cx / Area; uv[3] = 0.5f - cy / Area;
    float live[MaxRipples * 8]{};
    const int count = WaterRipple::HdActiveRipples(live, MaxRipples);
    if (count < 0 || count > int(MaxRipples)) return false;
    std::vector<float> pixels(size_t(Side) * Side * 4, 0);
    size_t budget = 2u * 1024u * 1024u; // CPU fallback admission bound; no partially published field
    for (int i = 0; i < count; ++i) {
        const float* r = live + i * 8;
        bool finite = true; for (int c = 0; c < 8; ++c) finite = finite && std::isfinite(r[c]);
        // Do not apply ripples from a stacked floor or another liquid plane.
        if (!finite || r[2] <= 0 || r[2] > 64 || r[3] <= 0 || std::fabs(r[7] - planeZ) > 0.25f) continue;
        const float dx = r[0] - cx, dy = r[1] - cy, radius = r[2] * 0.75f;
        const float width = 0.1f + 0.12f * radius, reach = radius + 3 * width;
        if (std::fabs(dx) > HalfArea + reach || std::fabs(dy) > HalfArea + reach) continue;
        const float fx = std::cos(r[4]), fy = -std::sin(r[4]);
        auto index = [=](float world) { return int(std::clamp((world + HalfArea) / Texel, 0.0f, float(Side))); };
        const int x0 = index(dx-reach), x1 = std::min(int(Side), index(dx+reach)+1);
        const int y0 = index(dy-reach), y1 = std::min(int(Side), index(dy+reach)+1);
        const size_t work = size_t(std::max(0,x1-x0))*std::max(0,y1-y0);
        if (work > budget) return false;
        budget -= work;
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
            const float wx = (x + 0.5f) * Texel - HalfArea, wy = (y + 0.5f) * Texel - HalfArea;
            const float lx = wx-dx, ly = wy-dy, distance = std::max(1e-4f, std::hypot(lx,ly));
            const float packet = (distance-radius)/width;
            float envelope = std::exp2(-1.4427f*packet*packet) * std::min(r[3],1.0f);
            if (r[5] != 0) envelope *= std::clamp(0.35f+0.65f*(lx*fx+ly*fy)/distance,0.0f,1.0f);
            const float k = 6.2831853f/(width*1.6f), phase = k*(distance-radius);
            const float slope = envelope*(-2*packet/width*std::cos(phase)-k*std::sin(phase));
            const float edge = std::clamp((HalfArea-std::max(std::fabs(wx),std::fabs(wy)))*0.5f,0.0f,1.0f);
            const float gain = slope/distance*(strength*0.04f*edge);
            const float sx = lx*gain, sy = ly*gain;
            auto* out = pixels.data()+(size_t(y)*Side+x)*4;
            out[0] += std::max(sx,0.0f); out[1] += std::max(-sx,0.0f);
            out[2] += std::max(sy,0.0f); out[3] += std::max(-sy,0.0f);
        }
    }
    // Empty input publishes an actual calm field, never a stale previous-frame ring.
    return EncodeField(Side,Side,pixels,false,field);
}
}
