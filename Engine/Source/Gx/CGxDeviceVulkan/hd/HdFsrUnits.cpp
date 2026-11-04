// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: see HdFsrUnits.h.
#include "HdFsrUnits.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>
namespace HD::FsrUnits {
namespace {
struct Sphere { float x, y, z, r; };
std::vector<Sphere> moving;
std::unordered_map<uint64_t, C3Vector> last;
unsigned seen = 0;
uint32_t frames = 0;
constexpr float kMovedYards = 0.02f, kMaxDistance = 120.0f;

int32_t Visit(WOWGUID guid, void*) {
    CGObject_C* object = ClntObjMgrObjectPtr(guid, TYPEMASK_OBJECT, __FILE__, __LINE__);
    if (!object || !object->m_obj || !(object->m_obj->m_type & TYPEMASK_UNIT)) return 1;
    CGUnit_C* unit = static_cast<CGUnit_C*>(object);
    if (!unit->m_unit) return 1;
    const C3Vector p = unit->GetPosition();
    const float radius = unit->m_unit->m_unitFieldBoundingradius;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !(radius > 0.05f) || radius > 20.0f) return 1;
    ++seen;
    auto it = last.find(guid.m_guid);
    bool moved = false;
    if (it != last.end()) {
        const float dx = p.x - it->second.x, dy = p.y - it->second.y, dz = p.z - it->second.z;
        moved = dx * dx + dy * dy + dz * dz > kMovedYards * kMovedYards;
        it->second = p;
    } else {
        last.emplace(guid.m_guid, p);
    }
    // A humanoid's bounding radius is about 0.39 yd for 1.9 yd of height: the sphere sits at mid height and covers the body.
    if (moved && moving.size() < 256) moving.push_back({p.x, p.y, p.z + 2.4f * radius, std::max(2.6f * radius, 0.8f)});
    return 1;
}
}

void Collect() {
    moving.clear();
    seen = 0;
    if (++frames % 900 == 0 || last.size() > 4096) last.clear(); // forget units that left view
    ClntObjMgrEnumVisibleObjects(Visit, nullptr);
}

size_t Rects(const float view[16], const float proj[16], const VkViewport& vp, VkExtent2D extent, VkRect2D* out, size_t max) {
    if (!extent.width || !extent.height || !(vp.width > 0) || !(vp.height > 0) || !(proj[0] != 0) || !(proj[5] != 0)) return 0;
    size_t n = 0;
    const float screen = float(extent.width) * float(extent.height);
    for (const Sphere& s : moving) {
        if (n >= max) break;
        // world -> view (row vector * matrix), then the projection's w is the view depth (proj[11] == 1)
        const float vx = s.x * view[0] + s.y * view[4] + s.z * view[8] + view[12];
        const float vy = s.x * view[1] + s.y * view[5] + s.z * view[9] + view[13];
        const float vz = s.x * view[2] + s.y * view[6] + s.z * view[10] + view[14];
        if (!(vz > 0.2f) || vz > kMaxDistance) continue;
        const float cx = vx * proj[0] + vz * proj[8], cy = vy * proj[5] + vz * proj[9];
        const float w = vz; // proj[11] == 1, proj[15] == 0
        const float px = vp.x + (cx / w * 0.5f + 0.5f) * vp.width;
        const float py = vp.y + (0.5f - cy / w * 0.5f) * vp.height;
        const float rx = std::fabs(s.r * proj[0] / w) * 0.5f * vp.width;
        const float ry = std::fabs(s.r * proj[5] / w) * 0.5f * vp.height;
        const float x0 = std::max(px - rx, 0.0f), x1 = std::min(px + rx, float(extent.width));
        const float y0 = std::max(py - ry, 0.0f), y1 = std::min(py + ry, float(extent.height));
        if (!(x1 - x0 >= 2.0f) || !(y1 - y0 >= 2.0f) || (x1 - x0) * (y1 - y0) > 0.35f * screen) continue;
        out[n++] = VkRect2D{VkOffset2D{int32_t(x0), int32_t(y0)}, VkExtent2D{uint32_t(x1 - x0), uint32_t(y1 - y0)}};
    }
    return n;
}

void Counts(unsigned& visible, unsigned& movingCount) { visible = seen; movingCount = unsigned(moving.size()); }
}
