#include "WoW/Source/WorldClient/HD/HdCataWater.h"
#include "WoW/Source/WorldClient/World.h"
// HD: the Cata water's dynamic displacement map, engine side (HdCataRipples.h, tranche 2). Ports of Cata 15595's
// CGUnit_C::CreateRipple (its dynamic map branch) and CWorldMap::UpdateRipples, writing GxCataRipples (GxSeam.h).
#include "WoW/Source/WorldClient/HD/HdCataRipples.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include "Engine/Source/Gx/GxSeam.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/Tempest/CAaBox.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/Types.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/WorldClient/CWorldMath.h"
#include "WoW/Source/WorldClient/FrameTime.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapObj.h"
#include "WoW/Source/WorldClient/MapObjDef.h"
#include "WoW/Source/WorldClient/MapObjGroup.h"
#include "WoW/Source/WorldClient/MapWeather.h"
#include "WoW/Source/WorldClient/World.h"

namespace {
    constexpr uint32_t kMaxUnits = 256;     // Liquid::CreatePerturbator, unless it is the active player's
    constexpr uint32_t kMaxEvents = 256;    // Liquid::AddPerturbation
    constexpr uint32_t kMaxMeshes = 16;     // Liquid::CreatePerturbatorMesh

    // This frame's unit perturbations (Cata keeps one per unit, its amplitude zeroed at each CreateRipple: a unit that
    // stops calling it stops perturbing), by unit: x, y, radius, amplitude, period.
    struct Unit { float p[5]; bool player; };
    std::map<const CGUnit_C*, Unit> s_units;
    uint32_t s_others = 0;   // of them, not the active player's
    std::vector<float> s_unitList, s_events;

    // A ship's hull under the water line (Cata DisplMapPerturbationMesh), kept with its map object definition.
    struct Mesh {
        const CMapObj* owner = nullptr;
        std::vector<C3Vector> vertices;    // the map object's space, scaled by 0.75
        std::vector<uint16_t> indices;
        C44Matrix transform;               // last frame's placement
        float amplitude = 0.0f;
        uint32_t period = 1000;
        bool seen = false;
    };
    std::map<const CMapObjDef*, Mesh> s_meshes;
    std::vector<float> s_meshVerts, s_meshList;

    bool s_wanted = false;
    int32_t s_rippleMapId=-1;
    uint32_t s_lastDrawn = 0;

    // XTextures\Ripples\perturbTex: Cata's runtime BLP2 (DXT1), decoded once to RGBA8 levels for the device.
    std::vector<uint8_t> s_texels;
    uint32_t s_texWidth = 0, s_texHeight = 0, s_texLevels = 0;
    bool s_texTried = false;

    uint32_t Word(const uint8_t* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }

    FILE* OpenRuntime(const char* relative) {
        char path[512];
        snprintf(path, sizeof(path), "CataWater\\%s", relative);
        FILE* file = fopen(path, "rb");
        if (file) return file;
        snprintf(path, sizeof(path), "..\\..\\CataWater\\%s", relative);
        return fopen(path, "rb");
    }

    void DecodeDxt1Block(const uint8_t* block, uint8_t* out, uint32_t stride, uint32_t w, uint32_t h) {
        const uint32_t c0 = block[0] | (block[1] << 8), c1 = block[2] | (block[3] << 8);
        uint8_t rgb[4][4];
        auto expand = [](uint32_t c, uint8_t* o) {
            o[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
            o[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
            o[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
            o[3] = 255;
        };
        expand(c0, rgb[0]);
        expand(c1, rgb[1]);
        for (uint32_t k = 0; k < 3; k++) {
            if (c0 > c1) {
                rgb[2][k] = static_cast<uint8_t>((2 * rgb[0][k] + rgb[1][k]) / 3);
                rgb[3][k] = static_cast<uint8_t>((rgb[0][k] + 2 * rgb[1][k]) / 3);
            } else {
                rgb[2][k] = static_cast<uint8_t>((rgb[0][k] + rgb[1][k]) / 2);
                rgb[3][k] = 0;
            }
        }
        rgb[2][3] = 255;
        rgb[3][3] = c0 > c1 ? 255 : 0;
        const uint32_t bits = Word(block + 4);
        for (uint32_t y = 0; y < 4 && y < h; y++)
            for (uint32_t x = 0; x < 4 && x < w; x++)
                memcpy(out + y * stride + x * 4, rgb[(bits >> (2 * (4 * y + x))) & 3], 4);
    }

    void LoadPerturbTex() {
        if (s_texTried) return;
        s_texTried = true;
        FILE* file = OpenRuntime("Textures\\XTEXTURES\\Ripples\\PERTURBTEX.BLP");
        if (!file) return;
        std::vector<uint8_t> d;
        fseek(file, 0, SEEK_END);
        const long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (size > 148 && size < 4 * 1024 * 1024) {
            d.resize(static_cast<size_t>(size));
            if (fread(d.data(), 1, d.size(), file) != d.size()) d.clear();
        }
        fclose(file);
        // BLP2, compression 2 (DXT), alpha encoding 0 or 1 bit: DXT1, as the runtime's file is.
        if (d.size() < 148 || memcmp(d.data(), "BLP2", 4) || d[8] != 2 || (d[10] != 0 && d[10] != 1 && d[9] > 1)) return;
        const uint32_t width = Word(d.data() + 12), height = Word(d.data() + 16);
        if (!width || !height || width > 1024 || height > 1024) return;
        std::vector<uint8_t> out;
        uint32_t levels = 0;
        for (uint32_t level = 0; level < 16; level++) {
            const uint32_t offset = Word(d.data() + 20 + 4 * level), bytes = Word(d.data() + 84 + 4 * level);
            const uint32_t w = width >> level ? width >> level : 1, h = height >> level ? height >> level : 1;
            const uint32_t blocks = ((w + 3) / 4) * ((h + 3) / 4);
            if (!bytes || uint64_t(offset) + bytes > d.size() || bytes < blocks * 8) break;
            const size_t base = out.size();
            out.resize(base + size_t(w) * h * 4);
            for (uint32_t by = 0; by < (h + 3) / 4; by++)
                for (uint32_t bx = 0; bx < (w + 3) / 4; bx++)
                    DecodeDxt1Block(d.data() + offset + 8 * (by * ((w + 3) / 4) + bx), out.data() + base + (size_t(by) * 4 * w + bx * 4) * 4,
                                    w * 4, w - bx * 4, h - by * 4);
            levels++;
            if (w == 1 && h == 1) break;
        }
        if (!levels) return;
        s_texels.swap(out);
        s_texWidth = width;
        s_texHeight = height;
        s_texLevels = levels;
    }

    // Cata CM2Model::GetBonePosition(-1): the root bone's pivot where it is animated, only on a loaded model (no wait).
    bool RootBonePosition(CM2Model* model, C3Vector& out) {
        if (!model || !(model->m_fFlags & 1) || !model->m_shared || !model->m_shared->m_data || !model->m_scene) return false;
        M2Data* data = model->m_shared->m_data;
        if (!data->m_bones.m_count) return false;
        model->Animate();
        if (!model->m_boneMatrices) return false;
        out = data->m_bones[0].m_pivot * model->m_boneMatrices[0] * model->m_scene->m_viewInv;
        return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
    }

    // The active world map's moving ships (Cata CWorldMap::UpdateRipples, its mapObjDef loop): a moved map object
    // (UpdateMoved's flag) across the water line at 0 gets a hull mesh once; its amplitude follows its speed.
    void UpdateMeshes(uint32_t elapsed) {
        for (auto& each : s_meshes) each.second.seen = false;
        for (CMapObjDef* def = CMap::mapObjDefHash.m_fulllist.Head(); def; def = CMap::mapObjDefHash.m_fulllist.Next(def)) {
            if (!(def->m_flags & MAPOBJ_FLAG_NO_DEPTH_SORT) || !def->m_owner) continue;   // Cata 0x200: moved
            CAaBox bounds = def->m_bbox;
            if (!(bounds.b.z <= -3.0f && bounds.t.z >= 3.0f)) continue;
            auto found = s_meshes.find(def);
            if (found != s_meshes.end() && found->second.owner != def->m_owner) {   // another definition at that address
                s_meshes.erase(found);
                found = s_meshes.end();
            }
            if (found == s_meshes.end()) {
                if (s_meshes.size() >= kMaxMeshes) continue;
                bounds.t.z = -2.0f;
                bounds.b.z = -3.0f;
                CAaBox localBox;
                memset(&localBox, 0, sizeof(localBox));
                CWorldMath::TransformAABox(def->m_invMat, bounds, localBox);
                World::TriData::statusFlags = 0;
                World::TriData::nBatches = 0;
                World::TriData::faceIndexCursor = 0;
                World::TriData::indexCursor = 0;
                World::TriData::matrixPoolCount = 0;
                World::TriData tris;
                def->m_owner->GetTris(localBox, 0x40010, &tris, def);
                if (!World::TriData::nBatches) continue;
                Mesh mesh;
                mesh.owner = def->m_owner;
                uint32_t baseVertex = 0;
                for (uint32_t b = 0; b < World::TriData::nBatches && b < 32; b++) {
                    const World::TriData::Batch& batch = World::TriData::batches[b];
                    const uint32_t minIndex = batch.m_minVertexIndex, maxIndex = batch.m_maxVertexIndex;
                    if (!batch.m_vertexList || !batch.m_indices || maxIndex < minIndex ||
                        mesh.vertices.size() - minIndex + maxIndex + 1 >= 5000 || mesh.indices.size() + 3 * batch.m_faceCount >= 7500) {
                        continue;
                    }
                    for (uint32_t v = minIndex; v <= maxIndex; v++) {
                        const C3Vector& vert = batch.m_vertexList[v];
                        mesh.vertices.push_back(C3Vector(vert.x * 0.75f, vert.y * 0.75f, vert.z * 0.75f));
                    }
                    const uint16_t* indices = batch.m_indices;
                    for (uint32_t f = 0; f < batch.m_faceCount; f++, indices += 3)
                        for (uint32_t k = 0; k < 3; k++)
                            mesh.indices.push_back(static_cast<uint16_t>(baseVertex + indices[k] - minIndex));
                    baseVertex = static_cast<uint32_t>(mesh.vertices.size());
                }
                mesh.amplitude = 1.0f;
                mesh.period = 1000;
                mesh.transform = def->m_mat;
                found = s_meshes.emplace(def, std::move(mesh)).first;
            }
            Mesh& mesh = found->second;
            mesh.seen = true;
            const float dx = mesh.transform.d0 - def->m_mat.d0;
            const float dy = mesh.transform.d1 - def->m_mat.d1;
            float speed = static_cast<float>(sqrt(dx * dx + dy * dy) / static_cast<double>(elapsed) * 100.0);
            if (speed > 1.0f) speed = 1.0f;
            mesh.amplitude = speed * 0.80000001f;
            mesh.transform = def->m_mat;
        }
        for (auto it = s_meshes.begin(); it != s_meshes.end();) {   // Cata frees a mesh with its definition
            if (it->second.seen) ++it;
            else it = s_meshes.erase(it);
        }
    }

    // The hulls' triangles in world x, y (Cata PerturbDisplMapMesh's transform, done here), for the device.
    void PublishMeshes() {
        s_meshVerts.clear();
        s_meshList.clear();
        for (const auto& each : s_meshes) {
            const Mesh& mesh = each.second;
            if (fabsf(mesh.amplitude) < 0.001f || mesh.indices.empty()) continue;
            const C44Matrix& m = mesh.transform;
            const uint32_t first = static_cast<uint32_t>(s_meshVerts.size() / 2);
            for (uint16_t index : mesh.indices) {
                if (index >= mesh.vertices.size()) { s_meshVerts.push_back(0.0f); s_meshVerts.push_back(0.0f); continue; }
                const C3Vector& p = mesh.vertices[index];
                s_meshVerts.push_back(p.z * m.c0 + p.x * m.a0 + p.y * m.b0 + m.d0);
                s_meshVerts.push_back(p.y * m.b1 + p.z * m.c1 + p.x * m.a1 + m.d1);
            }
            const float entry[4] = {static_cast<float>(first), static_cast<float>(mesh.indices.size()), mesh.amplitude,
                                    static_cast<float>(mesh.period)};
            s_meshList.insert(s_meshList.end(), entry, entry + 4);
        }
    }

    // Cata CWorldMap::UpdateRipples: the hulls every frame, the rain's drops every 33 ms around the player.
    void UpdateRipples(const C3Vector& pos) {
        static uint32_t s_lastTime = FrameTime::s_curTimeMs;
        const uint32_t now = FrameTime::s_curTimeMs;
        const uint32_t elapsed = now - s_lastTime;
        if (now == s_lastTime) return;
        UpdateMeshes(elapsed);
        if (elapsed < 33) return;
        uint32_t steps = elapsed / 33;
        if (steps > 4) {
            steps = 4;
            s_lastTime = now;
        } else if (steps < 4) {
            s_lastTime += 33 * steps;
        } else {
            s_lastTime = now;
        }
        const Weather* weather = World::s_weather;
        if (!weather || weather->GetType() != 1 || weather->m_intensity <= 0.2f) return;
        for (uint32_t i = 20 * steps; i; i--) {
            const float amplitude = static_cast<float>(rand() % 200) * 0.001f + 0.2f;
            const float dx = (static_cast<float>(rand() % 1000) * 0.0020000001f - 1.0f) * 50.0f;
            const float dy = (static_cast<float>(rand() % 1000) * 0.0020000001f - 1.0f) * 50.0f;
            if (s_events.size() / 5 >= kMaxEvents) continue;
            const float drop[5] = {pos.x + dx, pos.y + dy, 0.30000001f, -amplitude, 0.0f};
            s_events.insert(s_events.end(), drop, drop + 5);
        }
    }
}

void HD::CataRipplesReset(){
    s_units.clear();s_others=0;s_unitList.clear();s_events.clear();
    s_meshes.clear();s_meshVerts.clear();s_meshList.clear();s_wanted=false;s_rippleMapId=-1;
    s_lastDrawn=g_gxCataWater.m_drawn;
    auto& io=g_gxCataRipples;io.m_ready=io.m_wanted=0;
    io.m_unitCount=io.m_eventCount=io.m_meshCount=0;
    io.m_units=io.m_events=io.m_meshVerts=io.m_meshes=nullptr;
}
bool HD::CataRipplesActive() {
    return s_wanted && HD::CataWaterRippleMaterialReady() && g_gxCataRipples.m_active && g_gxCataRipples.m_ready && s_rippleMapId==World::QueryMapId();
}

void HD::CataRipplesUnit(CGUnit_C* unit, float liquidHeight) {
    if (!unit || !unit->m_movement || !unit->m_unit) return;
    if (unit->m_unit->m_unitFieldHealth <= 0) return;
    const uint32_t flags = unit->m_movement->m_flags;
    const bool falling = (flags & MOVEMENTFLAG_FALLING) != 0;   // Cata 0x800
    const bool swimming = (flags & MOVEMENTFLAG_SWIMMING) != 0; // Cata 0x100000
    const bool jumping = unit->CGUnit::IsJumping() != 0;
    const bool combat = unit->IsInCombat() != 0;
    if (!combat && !(flags & 0x3F) && !falling && !jumping && !swimming && !unit->m_movementData.IsOnFallingSpline()) return;
    CM2Model* model = unit->GetObjectModel();
    if (!model) return;
    const CAaBox box = model->GetCurrentBoundingBox();
    if (box.IsEmpty() || !(box.t.z > box.b.z)) return;
    C3Vector position;
    if (!RootBonePosition(model, position)) position = unit->GetPosition();
    float t = (liquidHeight - box.b.z) / (box.t.z - box.b.z);
    if (t > 1.0f) t = 1.0f;
    else if (0.0f >= t) return;
    float amplitude = sinf(t * 3.1415927f);
    if (!falling && !jumping) {
        if (swimming && !(flags & 0x3F) && !combat) {
            amplitude = 0.0f;
        } else {
            float scale = unit->m_movement->m_currentSpeed * 0.083333336f + 0.5f;
            if (0.0f > scale) amplitude = 0.0f;
            else amplitude = (scale >= 1.0f ? 1.0f : scale) * amplitude;
        }
    }
    const bool player = unit->IsActivePlayer();
    auto found = s_units.find(unit);
    if (found == s_units.end()) {
        if (amplitude <= 0.0f) return;   // Cata makes no perturbator for a still unit
        if (!player && s_others >= kMaxUnits) return;
        if (!player) s_others++;
        found = s_units.emplace(unit, Unit()).first;
    }
    const CAaSphere sphere = model->GetCurrentBoundingSphere();
    Unit& u = found->second;
    u.p[0] = position.x;
    u.p[1] = position.y;
    u.p[2] = sphere.r * 0.1f + 0.2f;
    u.p[3] = amplitude;
    u.p[4] = 800.0f;
    u.player = player;
}

void HD::CataRipplesBeginFrame(const C3Vector& camera) {
    s_rippleMapId=World::QueryMapId();
    GxCataRipples& io = g_gxCataRipples;
    io.m_ready = 0;
    // Wanted when the Cata water was drawn last frame (it draws without the maps until they are there).
    const uint32_t drawn = g_gxCataWater.m_drawn;
    s_wanted = io.m_active && g_gxCataWater.m_enabled && drawn != s_lastDrawn;
    s_lastDrawn = drawn;
    io.m_wanted = s_wanted ? 1 : 0;
    io.m_unitCount = io.m_eventCount = io.m_meshCount = 0;
    s_events.clear();
    if (!s_wanted) {
        s_units.clear();
        s_others = 0;
        return;
    }

    // Cata CWorldScene::Render: SetDisplMapInfo(s_playerPos, ...): the maps follow the player (the camera without one).
    C3Vector center = camera;
    if (CGPlayer_C* player = ClntObjMgrGetActivePlayerObj()) center = static_cast<CGUnit_C*>(player)->GetPosition();
    io.m_time = FrameTime::s_curTimeMs;
    io.m_center[0] = center.x;
    io.m_center[1] = center.y;
    io.m_center[2] = center.z;

    s_unitList.clear();
    for (const auto& each : s_units) s_unitList.insert(s_unitList.end(), each.second.p, each.second.p + 5);
    s_units.clear();   // the units that are still in the water call CreateRipple again before the next frame
    s_others = 0;
    UpdateRipples(center);
    PublishMeshes();

    LoadPerturbTex();
    io.m_units = s_unitList.data();
    io.m_unitCount = static_cast<uint32_t>(s_unitList.size() / 5);
    io.m_events = s_events.data();
    io.m_eventCount = static_cast<uint32_t>(s_events.size() / 5);
    io.m_meshVerts = s_meshVerts.data();
    io.m_meshes = s_meshList.data();
    io.m_meshCount = static_cast<uint32_t>(s_meshList.size() / 4);
    io.m_texels = s_texels.empty() ? nullptr : s_texels.data();
    io.m_texWidth = s_texWidth;
    io.m_texHeight = s_texHeight;
    io.m_texLevels = s_texLevels;
}
