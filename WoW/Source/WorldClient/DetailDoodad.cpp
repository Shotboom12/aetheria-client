#include "DetailDoodad.h"
#include <algorithm>
#include <cstdlib>
#include <vector>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include <cmath>
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Base/ObjectAlloc.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxMatrixStack.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/ShaderEffect/ShaderEffect.h"
#include "Engine/Source/ShaderEffect/ShadowCache.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C33Matrix.h"
#include "Engine/Source/Tempest/CImVector.h"
#include <cstring>
#include <unordered_map>
#include <vector>
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/WorldClient/GrassDensity.h"
#include "WoW/Source/WorldClient/HD/HdGrassWind.h"
#include "WoW/Source/WorldClient/WorldParam.h"
#include "WoW/Source/WorldClient/CDetailDoodadData.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdDetailDoodad.h" // HD: hdGrassLight
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapChunk.h"
#include "WoW/Source/WorldClient/MapLight.h"
#include "WoW/Source/WorldClient/MapObjGroup.h"
#include "WoW/Source/WorldClient/Types.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/WorldClient/HD/HdCataRenderPerf.h" // HD: hdCataRenderPerf
#include "WoW/Source/WorldClient/SortTable.h"

// STATIC: 0x00D1C4C4
static uint32_t s_indexBufferSize;

// STATIC: 0x00D1C4C8
static uint32_t s_vertexBufferSize;

// STATIC: 0x00D1C4DC
static uint32_t unk_D1C4DC;

// STATIC: 0x00D1C4E8
static uint32_t unk_D1C4E8;

// HD: hdGrassBuffers -- number of persistent vertex/index buffer pairs for the grass batches. The 3.3.5 client
// keeps 128; past that each batch gets a stream buffer and refills its vertices every frame (long view distance:
// hundreds of refills a frame). 0 = native 128.
static CVar* s_hdGrassBuffers;
static CVar* s_hdGrassKeep;

// HD: hdGrassFillBudget, grass batches filled per frame at most (0 = no limit). The sort list runs near to far, so
// the near grass is filled first and a far batch past the budget waits a frame or two (not drawn meanwhile).
static CVar* s_hdGrassFillBudget;
static uint32_t s_hdFillsLeft = 0xFFFFFFFFu;

static bool HdGrassBuffersChanged(CVar*, const char*, const char* value, void*) {
    int32_t pairs = value ? atoi(value) : -1;

    if (pairs != 0 && (pairs < 128 || pairs > 4096)) {
        return false;
    }

    CDetailDoodad::s_bPoolsDirty = 1; // pools rebuilt by the native path (UpdatePools), like a density change
    return true;
}

static uint32_t HdGrassBufferPairs() {
    int32_t pairs = s_hdGrassBuffers ? s_hdGrassBuffers->m_intValue : 0;
    return pairs >= 128 && pairs <= 4096 ? static_cast<uint32_t>(pairs) : 128;
}

// HD: hdGrassPack. The pools keep hdGrassBuffers pairs' worth of bytes, but a batch takes only the vertices and
// indices it uses (in units of kHdPackUnit) instead of a whole 4096-item pair, and there are kHdPackObjects
// times more buffer objects; so the same memory keeps several times more grass ready. 0 = native fixed pairs.
static CVar* s_hdGrassPack;
static bool s_hdPacked; // the current pools were made for hdGrassPack
constexpr uint32_t kHdPackUnit = 64;
constexpr uint32_t kHdPackObjects = 4;
constexpr uint32_t kHdPackIndexScale = 2; // a grass batch has about 1.5 indices per vertex: twice the index space (2 bytes each)

struct HdPackSpace {
    struct Range {
        uint32_t begin;
        uint32_t count;
    };

    std::vector<Range> free; // sorted by begin, neighbours merged

    void Reset(uint32_t units) {
        free.clear();

        if (units) {
            free.push_back({ 0, units });
        }
    }

    bool Take(uint32_t units, uint32_t& begin) {
        for (size_t i = 0; i < free.size(); i++) {
            if (free[i].count >= units) {
                begin = free[i].begin;
                free[i].begin += units;
                free[i].count -= units;

                if (!free[i].count) {
                    free.erase(free.begin() + i);
                }

                return true;
            }
        }

        return false;
    }

    void Give(uint32_t begin, uint32_t units) {
        std::vector<Range>::iterator it = std::lower_bound(free.begin(), free.end(), begin, [](const Range& r, uint32_t b) { return r.begin < b; });
        it = free.insert(it, { begin, units });

        if (it + 1 != free.end() && it->begin + it->count == (it + 1)->begin) {
            it->count += (it + 1)->count;
            free.erase(it + 1);
        }

        if (it != free.begin() && (it - 1)->begin + (it - 1)->count == it->begin) {
            (it - 1)->count += it->count;
            free.erase(it);
        }
    }
};

static HdPackSpace s_hdVertexSpace;
static HdPackSpace s_hdIndexSpace;

static bool HdGrassPackChanged(CVar*, const char*, const char* value, void*) {
    if (!value || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0)) {
        return false;
    }

    CDetailDoodad::s_bPoolsDirty = 1;
    return true;
}

static uint32_t HdPackUnits(uint32_t items) {
    return (items + kHdPackUnit - 1) / kHdPackUnit;
}

// A pair in the free list holds no range, a pair a batch holds always holds one (placed by HdPackPlace).
static void HdPackGiveBack(CGxBuf* buf, HdPackSpace& space) {
    if (buf) {
        space.Give(buf->m_index / (buf->m_itemSize * kHdPackUnit), HdPackUnits(buf->m_itemCount));
    }
}

// Places the pair just taken from the free list on ranges that fit vertexCount / indexCount; false (nothing taken)
// when the pools are full.
static bool HdPackPlace(CGxBuf* vertexBuf, uint32_t vertexCount, CGxBuf* indexBuf, uint32_t indexCount) {
    uint32_t vertexUnits = HdPackUnits(vertexCount ? vertexCount : 1);
    uint32_t indexUnits = HdPackUnits(indexCount ? indexCount : 1);
    uint32_t vertexBegin = 0;
    uint32_t indexBegin = 0;

    if (!s_hdVertexSpace.Take(vertexUnits, vertexBegin)) {
        return false;
    }

    if (!s_hdIndexSpace.Take(indexUnits, indexBegin)) {
        s_hdVertexSpace.Give(vertexBegin, vertexUnits);
        HD::CataRenderPerf::Add(HD::CataRenderPerf::Counter::GrassPackIndexFull);
        return false;
    }

    vertexBuf->m_itemCount = vertexUnits * kHdPackUnit;
    vertexBuf->m_size = vertexBuf->m_itemCount * vertexBuf->m_itemSize;
    vertexBuf->m_index = vertexBegin * kHdPackUnit * vertexBuf->m_itemSize;
    indexBuf->m_itemCount = indexUnits * kHdPackUnit;
    indexBuf->m_size = indexBuf->m_itemCount * indexBuf->m_itemSize;
    indexBuf->m_index = indexBegin * kHdPackUnit * indexBuf->m_itemSize;
    HD::CataRenderPerf::Add(HD::CataRenderPerf::Counter::GrassPackVertices, vertexCount);
    HD::CataRenderPerf::Add(HD::CataRenderPerf::Counter::GrassPackIndices, indexCount);
    HD::CataRenderPerf::Add(HD::CataRenderPerf::Counter::GrassPackAcquires);
    return true;
}

void HD::DetailDoodadBuffersFreed(CGxBuf* vertexBuf, CGxBuf* indexBuf) {
    if (!s_hdPacked) {
        return;
    }

    HdPackGiveBack(vertexBuf, s_hdVertexSpace);
    HdPackGiveBack(indexBuf, s_hdIndexSpace);
}

// STATIC: 0x00D1C504
TSGrowableArray<CGxBuf*> CDetailDoodad::s_freeGxBufs;

// Not in the original binary: the client keeps 128 baked grass batches; past that a batch is baked into a stream buffer
// again every frame it is drawn, which is what made long grass distances slow (seyris-living-azeroth's instanced grass
// worked around it the same way: the plants drawn from buffers that stay filled). The pool now grows to the batches
// in range, in groups of 128 pairs, each group a stock-sized pool of its own (a Vulkan dynamic pool copies itself
// whole when a lock overlaps this frame's draws: small pools keep that cheap), up to grassBufferMemory.
static CVar* s_cvarGrassBufferMemory;
static std::vector<CGxPool*> s_extraPools;   // vertex, index, vertex, index ... for the groups after the first

static bool GrassBufferMemoryChanged(CVar*, const char*, const char*, void*) {
    CDetailDoodad::s_bPoolsDirty = 1;
    return true;
}

void CDetailDoodad::RegisterCVars() {
    s_cvarGrassBufferMemory = CVar::Register("grassBufferMemory", "Most memory (MB) the kept grass may use: grass reaches only as far as it holds at the current density", 1, "256", GrassBufferMemoryChanged, CATEGORY_GRAPHICS, false, nullptr, false);
}

// Not in the original binary: frames an undrawn chunk keeps its baked grass. The client gives it 10; while a quarter
// of the pool is free, 300 (about 5 s), so turning around does not bake everything behind you again.
// Not in the original binary: chunks baked again per frame for grass density, so walking does not hitch.
static uint32_t s_rebakesLeft = 4;

bool CDetailDoodad::TakeRebake() {
    if (!s_rebakesLeft) {
        return false;
    }
    s_rebakesLeft--;
    return true;
}

uint32_t CDetailDoodad::KeepFrames() {
    return CDetailDoodad::s_freeGxBufs.Count() * 4 > unk_D1C4DC ? 300 : 10;
}

// STATIC: 0x00D1C488
CGxShader* CDetailDoodad::s_pixelShader[8];

// STATIC: 0x00D1C4A8
CGxShader* CDetailDoodad::s_vertexShader[6];

// STATIC: 0x00D1C4C0
int32_t CDetailDoodad::s_bPoolsDirty;

// STATIC: 0x00D1C4CC
uint32_t CDetailDoodad::s_idxBufItems;

// STATIC: 0x00D1C4D0
uint32_t CDetailDoodad::s_vtxBufItems;

// STATIC: 0x00D1C4D4
CGxPool* CDetailDoodad::s_gxIndexPool;

// STATIC: 0x00D1C4D8
CGxPool* CDetailDoodad::s_gxVertexPool;

// STATIC: 0x00D1C4E0
CGxTex* CDetailDoodad::s_alphaRampTexture;

// STATIC: 0x00D1C4E4
uint32_t* CDetailDoodad::s_instHeap;

// STATIC: 0x00D1C4F0
int32_t CDetailDoodad::s_bUseShaders;

// STATIC: 0x00D1C4F4
TSGrowableArray<CDetailDoodadData*> CDetailDoodad::s_doodadList;

// STATIC: 0x00D1C518
CDetailDoodad::SShaderConstants CDetailDoodad::s_shaderConstants;

// VERIFY: 1a974b50 auto
// OFFSET: 0x007B1000
static void __stdcall BuildInstanceMatrix(C33Matrix& result, C33Matrix& rotation, const C33Matrix& basis, float angle, const C3Vector& axis) {
    float c = cos(angle);
    float s = sin(angle);
    float zz = axis.z * axis.z;
    float zs = s * axis.z;

    rotation.a0 = c;
    rotation.a1 = zs;
    rotation.b0 = -zs;
    rotation.b1 = c;
    rotation.c2 = zz * (1.0f - c) + c;

    result.a0 = c * basis.a0 + zs * basis.b0;
    result.a1 = rotation.a0 * basis.a1 + rotation.a1 * basis.b1;
    result.a2 = rotation.a0 * basis.a2 + rotation.a1 * basis.b2;
    result.b0 = rotation.b1 * basis.b0 + basis.a0 * rotation.b0;
    result.b1 = rotation.b1 * basis.b1 + basis.a1 * rotation.b0;
    result.b2 = rotation.b0 * basis.a2 + rotation.b1 * basis.b2;
    result.c0 = basis.c0 * rotation.c2;
    result.c1 = basis.c1 * rotation.c2;
    result.c2 = basis.c2 * rotation.c2;
}

// VERIFY: 15a97eda manual
// OFFSET: 0x007B2CA0
static void AcquireBuffers(uint32_t vertexCount, CGxBuf** vertexBuf, uint32_t indexCount, CGxBuf** indexBuf) {
    uint32_t count = CDetailDoodad::s_freeGxBufs.Count();

    // HD: hdGrassPack, the pair is placed on ranges of its own size. Pools full: the grass longest off screen gives
    // its buffers back first (the update list holds only off-screen grass while the visible grass is being drawn,
    // oldest first); nothing left to give -> stream buffers as when no pair is left.
    if (count && s_hdPacked) {
        CDetailDoodadInst* oldest = CMap::s_detailDoodadUpdateList.Head();

        while (!HdPackPlace(CDetailDoodad::s_freeGxBufs[count - 2], vertexCount, CDetailDoodad::s_freeGxBufs[count - 1], indexCount)) {
            // m_age >= 2: not drawn this frame nor the last one (freeing bytes drawn this frame would copy the pool)
            while (oldest && (oldest->m_age < 2 || !oldest->HasBatches())) {
                oldest = CMap::s_detailDoodadUpdateList.Next(oldest);
            }

            if (!oldest) {
                HD::CataRenderPerf::Add(HD::CataRenderPerf::Counter::GrassPackFull);
                count = 0;
                break;
            }

            oldest->FreeBuffers(); // pushes its pairs on the free list: take the top pair again
            oldest = CMap::s_detailDoodadUpdateList.Next(oldest);
            count = CDetailDoodad::s_freeGxBufs.Count();
            HD::CataRenderPerf::Add(HD::CataRenderPerf::Counter::GrassPackEvicts);
        }
    }

    if (count) {
        *vertexBuf = CDetailDoodad::s_freeGxBufs[count - 2];
        *indexBuf = CDetailDoodad::s_freeGxBufs[count - 1];
        CDetailDoodad::s_freeGxBufs.SetCount(count - 2);
        (*vertexBuf)->m_hasData = 0;
        (*indexBuf)->m_hasData = 0;
    } else {
        *vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, sizeof(CGxVertexPNCT), vertexCount);
        *indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, sizeof(uint16_t), indexCount);
    }
}

// HD: hdGrassBuffers. Acquire and fill the persistent buffers of every grass batch about to be drawn BEFORE
// the first grass draw of the frame. The Vulkan device copies a whole dynamic pool (copy on write) when a lock
// overlaps bytes a draw of this frame already bound, and with hundreds of batches it counts the pool as wholly
// drawn: a fill between two draws (a chunk coming into view) then copied the large pool -- the camera stutter.
// Stream buffers are left to Draw, as before.
void HD::DetailDoodadPrepareBuffers() {
    if (!s_hdGrassBuffers || s_hdGrassBuffers->m_intValue == 0) {
        return;
    }

    HD::CataRenderPerf::Scope hdPerf(HD::CataRenderPerf::Stage::GrassFill);
    int32_t budget = s_hdGrassFillBudget ? s_hdGrassFillBudget->m_intValue : 0;
    s_hdFillsLeft = budget > 0 ? static_cast<uint32_t>(budget) : 0xFFFFFFFFu;

    for (CDetailDoodadInst* inst = CWorldScene::sortTable.m_detailDoodadInstList.Head(); inst; inst = CWorldScene::sortTable.m_detailDoodadInstList.Next(inst)) {
        for (uint32_t i = 0; i < kDetailDoodadBatches; i++) {
            CDetailDoodadInstBatch& batch = inst->m_batches[i];

            if (!batch.m_texture) {
                continue;
            }

            if (HD::CataRenderPerf::Enabled()) {
                bool mixed = false;

                for (uint32_t j = 1; j < batch.m_adds.Count() && !mixed; j++) {
                    mixed = batch.m_adds[j].m_doodadId != batch.m_adds[0].m_doodadId;
                }

                HD::CataRenderPerf::Add(mixed ? HD::CataRenderPerf::Counter::GrassMixedModels : HD::CataRenderPerf::Counter::GrassSingleModel);
            }

            bool needsFill = !batch.m_vertexBuf || batch.m_vertexBuf->m_pool->m_usage == GxPoolUsage_Stream
                || !batch.m_vertexBuf->m_hasData || !batch.m_vertexBuf->m_valid || !batch.m_indexBuf->m_hasData || !batch.m_indexBuf->m_valid;

            if (needsFill && !s_hdFillsLeft) {
                HD::CataRenderPerf::Add(HD::CataRenderPerf::Counter::GrassFillSkipped); // HD: hdGrassFillBudget
                continue;
            }

            // A batch left on stream buffers by an earlier frame takes a freed pair here too, so that Draw
            // never takes one (and fills it) between two draws.
            if (!batch.m_vertexBuf || batch.m_vertexBuf->m_pool->m_usage == GxPoolUsage_Stream) {
                if (!CDetailDoodad::s_freeGxBufs.Count()) {
                    continue;
                }

                AcquireBuffers(batch.m_vertexCount, &batch.m_vertexBuf, batch.m_indexCount, &batch.m_indexBuf);
            }

            if (batch.m_vertexBuf->m_pool->m_usage == GxPoolUsage_Stream) {
                continue;
            }

            if (needsFill && s_hdFillsLeft != 0xFFFFFFFFu) {
                s_hdFillsLeft--;
            }

            if (!batch.m_vertexBuf->m_hasData || !batch.m_vertexBuf->m_valid) {
                batch.FillVertices();
            }

            if (!batch.m_indexBuf->m_hasData || !batch.m_indexBuf->m_valid) {
                batch.FillIndices();
            }
        }
    }
}

uint32_t HD::DetailDoodadKeepFrames() {
    int32_t frames = s_hdGrassKeep ? s_hdGrassKeep->m_intValue : 0;
    return frames >= 20 && frames <= 10000 ? static_cast<uint32_t>(frames) : 0;
}

// VERIFY: c83ff776 auto
// OFFSET: 0x007B0D40
int32_t CDetailDoodadInst::HasBatches() {
    for (uint32_t i = 0; i < kDetailDoodadBatches; i++) {
        if (this->m_batches[i].m_vertexBuf) {
            return 1;
        }
    }

    return 0;
}

// VERIFY: e98d17d0 auto
// OFFSET: 0x007B10E0
void CDetailDoodad::SetupVertexShader(const C44Matrix& world, uint32_t index) {
    uint32_t shadow = CShadowCache::GetShadowValue();

    if (shadow > 2) {
        shadow = 2;
    }

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, CDetailDoodad::s_vertexShader[3 * index + shadow]);

    C44Matrix view;
    CGxMatrixStack& stack = g_theGxDevicePtr->m_xforms[GxXform_View];
    view = stack.m_mtx[stack.m_level];

    CDetailDoodad::s_shaderConstants.m_worldView = world * view;
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&CDetailDoodad::s_shaderConstants), sizeof(CDetailDoodad::s_shaderConstants) / sizeof(C4Vector));
    HD::DetailDoodadVertexConstants(world); // HD: constants of our grass vertex shader, grass wind included (nothing with Blizzard's)
}

// VERIFY: f3428d3d manual
// OFFSET: 0x007B11B0
void CDetailDoodad::CreateAlphaRampTexture(const void*& texels) {
    static CImVector s_rampTexels[8][64];

    CImVector color;
    color.b = 255;
    color.g = 255;
    color.r = 255;

    for (uint32_t i = 0; i < 64; i++) {
        color.a = i * 4;

        for (uint32_t row = 0; row < 8; row++) {
            s_rampTexels[row][63 - i] = color;
        }
    }

    texels = s_rampTexels;
}

// VERIFY: e5f01c86 manual
// OFFSET: 0x007B1270
void CDetailDoodad::UpdateAlphaRampTexture(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t face, uint32_t level, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    switch (command) {
        case GxTex_Latch:
            CDetailDoodad::CreateAlphaRampTexture(texels);
            texelStrideInBytes = width * 4;
            break;

        default:
            break;
    }
}

// VERIFY: c5224796 manual
// OFFSET: 0x007B12B0
void CDetailDoodadInstBatch::FillIndices() {
    HD::CataRenderPerf::Scope hdPerf(HD::CataRenderPerf::Stage::GrassFill);
    uint16_t* indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(this->m_indexBuf));
    uint32_t index = 0;
    uint32_t base = 0;

    for (uint32_t i = 0; i < this->m_adds.Count(); i++) {
        CM2Model* model = CDetailDoodad::s_doodadList[this->m_adds[i].m_doodadId]->m_model;

        if (!(model->m_fFlags & 1)) {
            model->WaitForLoad(nullptr);
        }

        M2SkinProfile* skin = model->m_shared->m_skinData;

        for (uint32_t j = 0; j < skin->m_indices.m_count; j++) {
            indices[index++] = static_cast<uint16_t>(base + skin->m_indices[j]);
        }

        base += skin->m_vertices.m_count;
    }

    CGxBuf* buf = this->m_indexBuf;
    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->m_hasData = 1;
}

// VERIFY: 039ae168 manual
// OFFSET: 0x007B15D0
void CDetailDoodad::InitializeShaderConstants() {
    memset(&CDetailDoodad::s_shaderConstants, 0, sizeof(CDetailDoodad::s_shaderConstants));

    CGxDevice* device = g_theGxDevicePtr;
    C44Matrix view;
    CGxMatrixStack& stack = device->m_xforms[GxXform_View];
    view = stack.m_mtx[stack.m_level];

    SShaderConstants& consts = CDetailDoodad::s_shaderConstants;
    consts.m_worldView = C44Matrix();
    consts.m_projection = device->m_projNative;

    if (device->DeviceApi() == GxApi_OpenGl) {
        consts.m_projection.c0 *= -1.0f;
        consts.m_projection.c1 *= -1.0f;
        consts.m_projection.c2 *= -1.0f;
        consts.m_projection.c3 *= -1.0f;
    }

    C3Vector sunDir(0.0f, 0.0f, 0.0f);
    CM2Lighting lighting(sunDir);
    lighting.AddLight(&CMap::s_mapLight->m_light);

    if (lighting.GetSunLight(sunDir, consts.m_sunAmbient, consts.m_sunDiffuse, consts.m_sunSpecular)) {
        C33Matrix rotation(view);
        C3Vector dir = -sunDir * rotation;
        consts.m_sunDir.x = dir.x;
        consts.m_sunDir.y = dir.y;
        consts.m_sunDir.z = dir.z;
        consts.m_sunDir.w = 1.0f;
        consts.m_sunSpecularW = 0.0f;
    } else {
        consts.m_sunDir.x = 0.0f;
        consts.m_sunDir.y = 0.0f;
        consts.m_sunDir.z = 0.0f;
        consts.m_sunDir.w = 1.0f;
        consts.m_sunDiffuse = C3Vector(0.0f, 0.0f, 0.0f);
        consts.m_sunDiffuseW = 0.0f;
        consts.m_sunAmbient = C3Vector(1.0f, 1.0f, 1.0f);
        consts.m_sunAmbientW = 1.0f;
        consts.m_sunSpecular = C3Vector(0.0f, 0.0f, 0.0f);
        consts.m_sunSpecularW = 0.0f;
    }

    for (int32_t i = 0; i < 3; i++) {
        SShaderConstants::SPointLight& light = consts.m_pointLights[i];
        light.m_position = C3Vector(0.0f, 0.0f, 0.0f);
        light.m_positionW = 0.0f;
        light.m_color = C3Vector(0.0f, 0.0f, 0.0f);
        light.m_colorW = 0.0f;
        light.m_attenuation = C3Vector(1.0f, 0.0f, 0.0f);
        light.m_attenuationW = 0.0f;
    }

    if (g_theGxDevicePtr->MasterEnable(GxMasterEnable_Fog)) {
        DayNight::DNInfo* info = DayNight::GetInfo();
        CImVector color = info->m_fog.m_color;
        float end = info->m_fog.m_end;
        float range = 1.0f / (end - info->m_fog.m_start);
        consts.m_fog.x = -(CShaderEffect::s_fogMul * range);
        consts.m_fog.y = end * range;
        consts.m_fog.z = info->m_fog.m_density;
        consts.m_fog.w = 0.0f;

        if (g_theGxDevicePtr->Caps().unk_0134) {
            GxRsSet(GxRs_FogColor, *reinterpret_cast<int32_t*>(&color));
        } else {
            C4Vector fogColor(color);
            fogColor.w = static_cast<float>(static_cast<uint32_t>(CWorld::detailDoodadAlphaRef)) * 255.0f;
            g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 2, &fogColor.x, 1);
        }

        if (g_theGxDevicePtr->Caps().unk_0138) {
            GxRsSet(GxRs_Fog, 1);
        }
    } else {
        consts.m_fog.x = 0.0f;
        consts.m_fog.y = 1.0f;
        consts.m_fog.z = 1.0f;
        consts.m_fog.w = 0.0f;

        if (g_theGxDevicePtr->Caps().unk_0138) {
            GxRsSet(GxRs_Fog, 0);
        }
    }

    float dist = CWorld::detailDoodadDist;
    float fade = 1.0f / (dist - dist * 0.85f);
    consts.m_fade.x = -1.0f * fade;
    consts.m_fade.y = dist * fade;
    consts.m_fade.z = 0.0f;
    consts.m_fade.w = 0.0f;
}

// VERIFY: 7ec422be manual
// OFFSET: 0x007B1B10
void CDetailDoodadData::ModelLoadedCallback(CM2Model* model, void* arg) {
    CDetailDoodadData* data = static_cast<CDetailDoodadData*>(arg);
    CM2Model* doodadModel = data->m_model;

    if (!(doodadModel->m_fFlags & 1)) {
        doodadModel->WaitForLoad(nullptr);
    }

    data->m_texture = CMap::LoadTexture(doodadModel->m_shared->m_data->m_textures[0].m_filename.Data());
}

// VERIFY: 69235ba2 manual
// OFFSET: 0x007B1B50
void CDetailDoodadInstBatch::FillVertices() {
    HD::CataRenderPerf::Scope hdPerf(HD::CataRenderPerf::Stage::GrassFill);
    HD::CataRenderPerf::Add(HD::CataRenderPerf::Counter::GrassFills);
    CGxBuf* buf = this->m_vertexBuf;
    CGxVertexPNCT* vertex = reinterpret_cast<CGxVertexPNCT*>(g_theGxDevicePtr->BufLock(buf));

    C3Vector axis(0.0f, 0.0f, 1.0f);
    C33Matrix basis(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    uint8_t built = 0;
    uint32_t cell = 0xFFFF;
    C33Matrix rotation(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    C33Matrix matrices[4] = {
        C33Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        C33Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        C33Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        C33Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f)
    };

    for (uint32_t i = 0; i < this->m_adds.Count(); i++) {
        CDetailDoodadInstAdd& add = this->m_adds[i];
        CDetailDoodadData* data = CDetailDoodad::s_doodadList[add.m_doodadId];
        CM2Model* model = data->m_model;

        if (!(model->m_fFlags & 1)) {
            model->WaitForLoad(nullptr);
        }

        M2Data* m2Data = model->m_shared->m_data;
        model = data->m_model;

        if (!(model->m_fFlags & 1)) {
            model->WaitForLoad(nullptr);
        }

        M2SkinProfile* skin = model->m_shared->m_skinData;
        uint32_t vertexCount = skin->m_vertices.m_count;
        CImVector color = add.m_color;

        if (!CDetailDoodad::s_bUseShaders) {
            uint32_t scale = 76 * color.a + 45874;
            color.r = static_cast<uint8_t>((scale * color.r) >> 16);
            color.g = static_cast<uint8_t>((scale * color.g) >> 16);
            color.b = static_cast<uint8_t>((scale * color.b) >> 16);
            color.a = 0xFF;
        }

        if (g_theGxDevicePtr->Caps().m_colorFormat == GxCF_rgba) {
            CImVector swapped;
            swapped.a = color.a;
            swapped.g = color.g;
            swapped.r = color.b;
            swapped.b = color.r;
            color = swapped;
        }

        if (data->m_record->m_flags & 1) {
            if ((add.m_triIndex & 0xFC) != cell) {
                cell = add.m_triIndex & 0xFC;
                built = 0;
            }

            uint32_t fan = add.m_triIndex & 3;

            if (!(built & (1 << fan))) {
                const C3Vector& normal = add.m_normal;
                float inv = 1.0f / sqrt(-normal.y * -normal.y + normal.z * normal.z);
                C3Vector tangent(0.0f * inv, normal.z * inv, -normal.y * inv);
                C3Vector binormal(normal.y * tangent.z - normal.z * tangent.y, normal.z * tangent.x - tangent.z * normal.x, tangent.y * normal.x - tangent.x * normal.y);
                basis = C33Matrix(binormal.x, binormal.y, binormal.z, tangent.x, tangent.y, tangent.z, normal.x, normal.y, normal.z);
                BuildInstanceMatrix(matrices[fan], rotation, basis, add.m_rotation, axis);
                built |= 1 << fan;
            }

            const C33Matrix& matrix = matrices[fan];

            for (uint32_t j = 0; j < vertexCount; j++) {
                const M2Vertex& source = m2Data->m_vertices[skin->m_vertices[j]];
                C3Vector position = source.m_position * matrix;
                vertex->m_position.x = position.x * add.m_scale + add.m_position.x;
                vertex->m_position.y = position.y * add.m_scale + add.m_position.y;
                vertex->m_position.z = position.z * add.m_scale + add.m_position.z;
                vertex->m_normal = add.m_normal;
                vertex->m_color = color;
                vertex->m_texture = source.m_texcoord[0];
                vertex++;
            }
        } else {
            float c = cos(add.m_rotation);
            float s = sin(add.m_rotation);

            for (uint32_t j = 0; j < vertexCount; j++) {
                const M2Vertex& source = m2Data->m_vertices[skin->m_vertices[j]];
                vertex->m_position.x = (source.m_position.x * c - source.m_position.y * s) * add.m_scale + add.m_position.x;
                vertex->m_position.y = (source.m_position.y * c + source.m_position.x * s) * add.m_scale + add.m_position.y;
                vertex->m_position.z = source.m_position.z * add.m_scale + add.m_position.z;
                vertex->m_normal = add.m_normal;
                vertex->m_color = color;
                vertex->m_texture = source.m_texcoord[0];
                vertex++;
            }
        }
    }

    buf = this->m_vertexBuf;
    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->m_hasData = 1;
}

// VERIFY: a2c3ec0e manual
// OFFSET: 0x007B1380
void CDetailDoodad::DestroyModels() {
    for (uint32_t i = 0; i < CDetailDoodad::s_doodadList.Count(); i++) {
        if (CDetailDoodad::s_doodadList[i]) {
            CDetailDoodadData* data = CDetailDoodad::s_doodadList[i];

            if (data) {
                if (data->m_texture) {
                    HandleClose(reinterpret_cast<HOBJECT>(data->m_texture));
                    data->m_texture = nullptr;
                }

                if (data->m_model) {
                    data->m_model->Release();
                    data->m_model = nullptr;
                }

                data->m_record = nullptr;
                SMemFree(data, "delete", SLOG_FUNCTION, 0);
            }
        }

        CDetailDoodad::s_doodadList[i] = nullptr;
    }
}

// VERIFY: 9944e10b manual
// OFFSET: 0x007B2760
void CDetailDoodad::Initialize() {
    HD::CataRenderPerf::Initialize();
    CDetailDoodad::s_instHeap = new (__FILE__, __LINE__) uint32_t(ObjectAllocAddHeap(sizeof(CDetailDoodadInst), 16, "WDETAILDOODADINST", true));

    uint32_t count = g_groundEffectDoodadDB.m_maxID + 1;
    CDetailDoodad::s_doodadList.SetCount(count);

    for (uint32_t i = 0; i < CDetailDoodad::s_doodadList.Count(); i++) {
        CDetailDoodad::s_doodadList[i] = nullptr;
    }

    for (uint32_t i = 0; i < count; i++) {
        GroundEffectDoodadRec* record = g_groundEffectDoodadDB.GetRecord(i);

        if (record) {
            CDetailDoodadData* data = new (__FILE__, __LINE__) CDetailDoodadData();
            data->m_record = record;
            CDetailDoodad::s_doodadList[i] = data;
        }
    }

    GxTexCreate(64, 8, GxTex_Argb8888, CGxTexFlags(), nullptr, CDetailDoodad::UpdateAlphaRampTexture, CDetailDoodad::s_alphaRampTexture);

    memset(CDetailDoodad::s_vertexShader, 0, sizeof(CDetailDoodad::s_vertexShader));
    memset(CDetailDoodad::s_pixelShader, 0, sizeof(CDetailDoodad::s_pixelShader));

    CDetailDoodad::s_vtxBufItems = 0;
    CDetailDoodad::s_idxBufItems = 0;
    s_vertexBufferSize = 0;
    s_indexBufferSize = 0;
    CDetailDoodad::s_freeGxBufs.m_count = 0;
    unk_D1C4DC = 0;
    CDetailDoodad::s_gxVertexPool = nullptr;
    CDetailDoodad::s_gxIndexPool = nullptr;
    CDetailDoodad::s_bPoolsDirty = 1;

    if (!s_hdGrassFillBudget) {
        s_hdGrassFillBudget = CVar::Register("hdGrassFillBudget", "HD: grass batches filled per frame at most, near first (0 = no limit; with hdGrassBuffers)", 1, "60", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    }

    if (!s_hdGrassPack) {
        s_hdGrassPack = CVar::Register("hdGrassPack", "HD: grass buffers take only the size they use (more grass kept ready in the same memory), 0 = native fixed pairs", 1, "1", HdGrassPackChanged, CATEGORY_GRAPHICS, false, nullptr, false);
    }

    if (!s_hdGrassKeep) {
        s_hdGrassKeep = CVar::Register("hdGrassKeep", "HD: frames grass stays ready after leaving the view, 0 = native (10 buffers, 20 instances), 20..10000", 1, "300", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    }

    if (!s_hdGrassBuffers) {
        s_hdGrassBuffers = CVar::Register("hdGrassBuffers", "HD: persistent grass buffer pairs, 0 = native 128 (stream refills past that), 128..4096", 1, "2000", HdGrassBuffersChanged, CATEGORY_GRAPHICS, false, nullptr, false);
    }

    CDetailDoodad::s_bUseShaders = 1;

    if (!(CWorld::enables & Enable_PixelShaders)) {
        CDetailDoodad::s_bUseShaders = 0;
    }

    if (!(CWorld::enables2 & Enable_VertexShader)) {
        CDetailDoodad::s_bUseShaders = 0;
    } else if (CDetailDoodad::s_bUseShaders) {
        g_theGxDevicePtr->ShaderCreate(CDetailDoodad::s_vertexShader, GxSh_Vertex, "Shaders\\Vertex", "DetailDoodad", sizeof(CDetailDoodad::s_vertexShader) / sizeof(CDetailDoodad::s_vertexShader[0]));
        g_theGxDevicePtr->ShaderCreate(CDetailDoodad::s_pixelShader, GxSh_Pixel, "Shaders\\Pixel", "DetailDoodad", sizeof(CDetailDoodad::s_pixelShader) / sizeof(CDetailDoodad::s_pixelShader[0]));
        HD::DetailDoodadShadersStart(); // HD: compile our grass shaders (Forever lighting) in the background
    }
}

// VERIFY: 2ff521ae auto
// OFFSET: 0x007B29B0
void CDetailDoodad::FreeGxBufMat() {
    for (uint32_t i = 0; i < CDetailDoodad::s_freeGxBufs.Count(); i++) {
        CGxBuf* buf = CDetailDoodad::s_freeGxBufs[i];

        if (buf && buf != g_theGxDevicePtr->BufStream(buf->m_pool->m_target, 0, 0)) {
            g_theGxDevicePtr->BufDestroy(buf);
        }
    }

    CDetailDoodad::s_freeGxBufs.SetCount(0);
    unk_D1C4DC = 0;

    for (CGxPool* pool : s_extraPools) {
        g_theGxDevicePtr->PoolDestroy(pool);
    }
    s_extraPools.clear();

    if (CDetailDoodad::s_gxVertexPool) {
        g_theGxDevicePtr->PoolDestroy(CDetailDoodad::s_gxVertexPool);
    }

    if (CDetailDoodad::s_gxIndexPool) {
        g_theGxDevicePtr->PoolDestroy(CDetailDoodad::s_gxIndexPool);
    }

    CDetailDoodad::s_vtxBufItems = 0;
    CDetailDoodad::s_idxBufItems = 0;
    s_vertexBufferSize = 0;
    s_indexBufferSize = 0;
    unk_D1C4DC = 0;
    CDetailDoodad::s_gxVertexPool = nullptr;
    CDetailDoodad::s_gxIndexPool = nullptr;
    CDetailDoodad::s_bPoolsDirty = 1;
}

// VERIFY: 08827cd3 manual
// OFFSET: 0x007B2A80
void CDetailDoodad::UpdatePools() {
    if (!CDetailDoodad::s_bPoolsDirty) {
        return;
    }

    CMap::ClearChunkDetailDoodads();
    CDetailDoodad::FreeGxBufMat();

    uint32_t vertexItems = World::s_detailDoodadDensity << 6;
    uint32_t indexItems = World::s_detailDoodadDensity << 6;
    CDetailDoodad::s_vtxBufItems = vertexItems;
    CDetailDoodad::s_idxBufItems = indexItems;

    if (vertexItems > 4096) {
        vertexItems = 4096;
        CDetailDoodad::s_vtxBufItems = 4096;
    }

    if (indexItems > 4096) {
        indexItems = 4096;
        CDetailDoodad::s_idxBufItems = 4096;
    }

    s_vertexBufferSize = sizeof(CGxVertexPNCT) * vertexItems;
    s_indexBufferSize = sizeof(uint16_t) * indexItems;

    uint32_t pairs = HdGrassBufferPairs(); // HD: hdGrassBuffers (native 128)
    CDetailDoodad::s_gxVertexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Dynamic, pairs * sizeof(CGxVertexPNCT) * vertexItems, GxPoolHintBit_Unk3, "CDetailDoodad_vtx");
    bool hdPack = s_hdGrassPack && s_hdGrassPack->m_intValue != 0 && s_hdGrassBuffers && s_hdGrassBuffers->m_intValue != 0;
    uint32_t indexScale = hdPack ? kHdPackIndexScale : 1; // HD: hdGrassPack
    CDetailDoodad::s_gxIndexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Index, GxPoolUsage_Dynamic, indexScale * pairs * sizeof(uint16_t) * indexItems, GxPoolHintBit_Unk0, "CDetailDoodad_idx");

    // HD: hdGrassPack (only with hdGrassBuffers): more buffer objects over the same bytes, placed when acquired
    s_hdPacked = hdPack;
    uint32_t objects = s_hdPacked ? pairs * kHdPackObjects : pairs;
    s_hdVertexSpace.Reset(s_hdPacked ? pairs * vertexItems / kHdPackUnit : 0);
    s_hdIndexSpace.Reset(s_hdPacked ? indexScale * pairs * indexItems / kHdPackUnit : 0);

    CDetailDoodad::s_freeGxBufs.Reserve(2 * objects, 0);

    for (uint32_t i = 0; i < 2 * objects; i += 2) {
        uint32_t pair = s_hdPacked ? 0 : (i >> 1); // packed objects get their range when acquired
        *CDetailDoodad::s_freeGxBufs.New() = g_theGxDevicePtr->BufCreate(CDetailDoodad::s_gxVertexPool, sizeof(CGxVertexPNCT), CDetailDoodad::s_vtxBufItems, sizeof(CGxVertexPNCT) * CDetailDoodad::s_vtxBufItems * pair);
        *CDetailDoodad::s_freeGxBufs.New() = g_theGxDevicePtr->BufCreate(CDetailDoodad::s_gxIndexPool, sizeof(uint16_t), CDetailDoodad::s_idxBufItems, sizeof(uint16_t) * CDetailDoodad::s_idxBufItems * pair);
        unk_D1C4E8 += 2;
    }

    unk_D1C4DC = 2 * objects;
    CDetailDoodad::s_bPoolsDirty = 0;
}

// VERIFY: 564457f1 manual
// OFFSET: 0x007B2D30
int32_t CDetailDoodad::InitializeRenderState() {
    s_rebakesLeft = 4;  // not in the original binary: the next frame's density rebakes
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, 2);
    GxRsSet(GxRs_AlphaRef, CWorld::detailDoodadAlphaRef);
    GxRsSet(GxRs_DepthWrite, 1);

    if (CDetailDoodad::s_bUseShaders) {
        uint32_t shadow = CShadowCache::GetShadowValue();
        // HD: hdGrassLight puts our shader code (Forever lighting) into all the grass shaders before one is bound
        HD::DetailDoodadShaders();
        g_theGxDevicePtr->RsSet(GxRs_PixelShader, CDetailDoodad::s_pixelShader[4 * ((CWorld::enables2 >> 1) & 1) + shadow]);
        CDetailDoodad::InitializeShaderConstants();
        CShadowCache::SetShadowMapDetailDoodad();
        return 1;
    }

    GxRsSet(GxRs_ColorOp0, 0);
    GxRsSet(GxRs_AlphaOp0, 0);
    GxRsSet(GxRs_MatDiffuse, static_cast<int32_t>(0xFFFFFFFF));
    g_theGxDevicePtr->RsSet(GxRs_Texture1, CDetailDoodad::s_alphaRampTexture);
    GxRsSet(GxRs_ColorOp1, 0);
    GxRsSet(GxRs_AlphaOp1, 0);
    GxRsSet(GxRs_TexGen1, 3);
    GxRsSet(GxRs_TextureShader1, 1);

    C44Matrix matrix;
    float dist = CWorld::detailDoodadDist;
    matrix.Scale(1.0f / (dist * 0.15f));
    matrix.RotateAroundY(1.5707964f);
    matrix.Translate(C3Vector(0.0f, 0.0f, dist * -0.85f));

    CGxMatrixStack& stack = g_theGxDevicePtr->m_xforms[GxXform_Tex1];
    stack.m_dirty = 1;
    stack.m_flags[stack.m_level] &= ~CGxMatrixStack::F_Identity;
    stack.m_mtx[stack.m_level] = matrix;

    return 0;
}

// VERIFY: 168f79b0 auto
// OFFSET: 0x007B3050
int32_t CDetailDoodadData::Load() {
    if (!this->m_model) {
        char path[260];
        size_t length = SStrCopy(path, "World\\NoDXT\\Detail\\", STORM_MAX_STR);
        SStrCopy(path + length, this->m_record->m_doodadpath, STORM_MAX_STR);

        this->m_model = World::s_m2Scene->CreateModel(path, 0);

        if (!this->m_model) {
            return 0;
        }

        this->m_model->SetLoadedCallback(CDetailDoodadData::ModelLoadedCallback, this);
    }

    return 1;
}

// VERIFY: 50911cf6 manual
// OFFSET: 0x007B30D0
void CDetailDoodadInst::FreeBuffers() {
    for (uint32_t i = 0; i < kDetailDoodadBatches; i++) {
        this->m_batches[i].FreeBuffers();
    }
}

// Not in the original binary (seyris-living-azeroth's GrassDoodads.cpp): where a blade's root and tip are on its texture,
// from the model's own geometry: the average texture V of the vertices in the lowest and in the highest 12% of its height.
// False for a model with no height (a flat decal) or no usable V range.
static bool ScanRootTip(CM2Model* model, float& rootV, float& tipV) {
    M2Data* m2Data = model->m_shared->m_data;
    M2SkinProfile* skin = model->m_shared->m_skinData;
    const uint32_t count = skin->m_vertices.m_count;
    if (count < 3) return false;
    float minZ = 1e30f, maxZ = -1e30f;
    for (uint32_t j = 0; j < count; j++) {
        const float z = m2Data->m_vertices[skin->m_vertices[j]].m_position.z;
        minZ = std::min(minZ, z);
        maxZ = std::max(maxZ, z);
    }
    const float height = maxZ - minZ;
    if (height < 0.05f) return false;
    const float band = height * 0.12f;
    float rootSum = 0.0f, tipSum = 0.0f;
    uint32_t rootN = 0, tipN = 0;
    for (uint32_t j = 0; j < count; j++) {
        const M2Vertex& v = m2Data->m_vertices[skin->m_vertices[j]];
        if (v.m_position.z <= minZ + band) {
            rootSum += v.m_texcoord[0].y;
            rootN++;
        }
        if (v.m_position.z >= maxZ - band) {
            tipSum += v.m_texcoord[0].y;
            tipN++;
        }
    }
    if (!rootN || !tipN) return false;
    rootV = rootSum / rootN;
    tipV = tipSum / tipN;
    return true;
}

// VERIFY: b905f512 manual
// OFFSET: 0x007B31E0
void CDetailDoodadInst::AddDoodad(uint32_t id, const C3Vector& position, float rotation, float scale, const C3Vector& normal, uint16_t triIndex, const CImVector& color) {
    CDetailDoodadData* data = CDetailDoodad::s_doodadList[id];
    CM2Model* model = data->m_model;

    if (!(model->m_fFlags & 1)) {
        model->WaitForLoad(nullptr);
    }

    M2SkinProfile* skin = model->m_shared->m_skinData;
    uint32_t vertexCount = skin->m_vertices.m_count;
    uint32_t indexCount = skin->m_indices.m_count;
    // Not in the original binary: a doodad with a GroundEffectDoodadWind.cdbc row bends its own way (the batch's c68),
    // so it shares a batch only with doodads of its class.
    // Its mapping from the model's own geometry is read once per doodad.
    static std::unordered_map<uint32_t, uint16_t> s_windClasses;
    auto cached = s_windClasses.find(id);
    if (cached == s_windClasses.end()) {
        float rootV = 0.0f, tipV = 0.0f;
        const bool mapped = ScanRootTip(model, rootV, tipV);
        cached = s_windClasses.emplace(id, HD::GrassWindClassOf(id, mapped, rootV, tipV)).first;
    }
    const uint16_t windClass = cached->second;

    uint32_t i;

    for (i = 0; i < kDetailDoodadBatches; i++) {
        CDetailDoodadInstBatch& batch = this->m_batches[i];

        if (!HandleCompare(reinterpret_cast<HOBJECT>(batch.m_texture), reinterpret_cast<HOBJECT>(data->m_texture)) && batch.m_windClass == windClass && vertexCount + batch.m_vertexCount < CDetailDoodad::s_vtxBufItems && indexCount + batch.m_indexCount < CDetailDoodad::s_idxBufItems) {
            break;
        }
    }

    if (i == kDetailDoodadBatches) {
        for (i = 0; i < kDetailDoodadBatches; i++) {
            if (!this->m_batches[i].m_texture) {
                break;
            }
        }

        if (i == kDetailDoodadBatches) {
            return;
        }

        this->m_batches[i].m_texture = data->m_texture;
        this->m_batches[i].m_windClass = windClass;
    }

    CDetailDoodadInstBatch& batch = this->m_batches[i];
    CDetailDoodadInstAdd* add = batch.m_adds.New();
    add->m_doodadId = id;
    add->m_position = position;
    add->m_rotation = rotation;
    add->m_scale = scale;
    add->m_normal = normal;
    add->m_triIndex = triIndex;
    add->m_color = color;

    batch.m_vertexCount += vertexCount;
    batch.m_indexCount += indexCount;
}

// VERIFY: 9bfece74 manual
// OFFSET: 0x007B3390
void CDetailDoodadInstBatch::Draw() {
    if (!this->m_texture) {
        return;
    }

    CGxTex* gxTex = TextureGetGxTex(this->m_texture, 0, nullptr);

    if (!gxTex) {
        return;
    }

    // HD: hdGrassBuffers. DetailDoodadPrepareBuffers already handed out and filled the persistent pairs; a
    // batch still without one draws from stream buffers, and a persistent pair is never filled here, after
    // a draw of this frame (the Vulkan device would copy the whole dynamic pool). Native path otherwise.
    bool hdPrepared = s_hdGrassBuffers && s_hdGrassBuffers->m_intValue != 0;

    if (!this->m_vertexBuf || this->m_vertexBuf->m_pool->m_usage == GxPoolUsage_Stream) {
        if (hdPrepared && !s_hdFillsLeft) {
            return; // HD: hdGrassFillBudget spent this frame, drawn once filled on a later one
        }

        // Not in the original binary: past 280 yards a batch with no kept buffer is not drawn this frame (the near grass is prepared first, so
        // it is the far grass that waits). Refilling stream buffers for hundreds of batches every frame grew the stream ring past the GPU's memory.
        if (hdPrepared && CWorld::detailDoodadDist > 280.0f) {
            return;
        }

        if (hdPrepared && s_hdFillsLeft != 0xFFFFFFFFu) {
            s_hdFillsLeft--;
        }

        if (hdPrepared) {
            this->m_vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, sizeof(CGxVertexPNCT), this->m_vertexCount);
            this->m_indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, sizeof(uint16_t), this->m_indexCount);
        } else {
            AcquireBuffers(this->m_vertexCount, &this->m_vertexBuf, this->m_indexCount, &this->m_indexBuf);
        }
    }

    bool streamed = this->m_vertexBuf->m_pool->m_usage == GxPoolUsage_Stream;

    if (!this->m_vertexBuf->m_hasData || !this->m_vertexBuf->m_valid) {
        if (hdPrepared && !streamed) {
            return; // HD: lost since the prefill (device reset); filled before the draws of the next frame
        }

        this->FillVertices();
    }

    GxPrimVertexPtr(this->m_vertexBuf, GxVBF_PNCT);

    if (!this->m_indexBuf->m_hasData || !this->m_indexBuf->m_valid) {
        if (hdPrepared && !streamed) {
            return;
        }

        this->FillIndices();
    }

    g_theGxDevicePtr->PrimIndexPtr(this->m_indexBuf);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
    HD::DetailDoodadBatchConstants(this->m_windClass);  // not in the original binary: how this batch's plant bends

    CGxBatch batch;
    batch.m_primType = GxPrim_Triangles;
    batch.m_start = 0;
    batch.m_count = this->m_indexCount;
    batch.m_minIndex = 0;
    batch.m_maxIndex = static_cast<uint16_t>(this->m_vertexCount - 1);
    HD::CataRenderPerf::Add(HD::CataRenderPerf::Counter::GrassDraws);
    g_theGxDevicePtr->Draw(&batch, 1);
}

// VERIFY: 84475972 auto
// OFFSET: 0x007B3480
void CDetailDoodad::Destroy() {
    CDetailDoodad::DestroyModels();
    CDetailDoodad::FreeGxBufMat();

    if (CDetailDoodad::s_alphaRampTexture) {
        GxTexDestroy(CDetailDoodad::s_alphaRampTexture);
    }

    CDetailDoodad::s_alphaRampTexture = nullptr;

    if (CDetailDoodad::s_instHeap) {
        SMemFree(CDetailDoodad::s_instHeap, "delete", SLOG_FUNCTION, 0);
    }

    CDetailDoodad::s_instHeap = nullptr;

    HD::DetailDoodadShadersForget(); // HD: Blizzard's code back into the grass shaders before they are released

    for (uint32_t i = 0; i < sizeof(CDetailDoodad::s_vertexShader) / sizeof(CDetailDoodad::s_vertexShader[0]); i++) {
        if (CDetailDoodad::s_vertexShader[i]) {
            g_theGxDevicePtr->ShaderDestroy(CDetailDoodad::s_vertexShader[i]);
        }
    }

    for (uint32_t i = 0; i < sizeof(CDetailDoodad::s_pixelShader) / sizeof(CDetailDoodad::s_pixelShader[0]); i++) {
        if (CDetailDoodad::s_pixelShader[i]) {
            g_theGxDevicePtr->ShaderDestroy(CDetailDoodad::s_pixelShader[i]);
        }
    }
}

// VERIFY: d3db66d2 auto
// OFFSET: 0x007B3530
int32_t CDetailDoodad::LoadModel(uint32_t id) {
    CDetailDoodadData* data = CDetailDoodad::s_doodadList[id];

    if (!data) {
        return 0;
    }

    if (!data->m_model) {
        data->Load();
        return 0;
    }

    return data->m_model->IsLoaded(0, 0);
}

// VERIFY: f62a81be manual
// OFFSET: 0x007B3560
void CDetailDoodadInst::Free() {
    if (this->m_chunk) {
        this->m_chunk->m_detailDoodadInst = nullptr;
        this->m_chunk = nullptr;
    }

    for (uint32_t i = 0; i < kDetailDoodadBatches; i++) {
        this->m_batches[i].Free();
    }
}

// VERIFY: 104e29d5 auto
// OFFSET: 0x007B36B0
void CDetailDoodadInst::DrawBatches() {
    for (uint32_t i = 0; i < kDetailDoodadBatches; i++) {
        if (this->m_batches[i].m_texture) {
            this->m_batches[i].Draw();
        }
    }
}

// INLINED
CDetailDoodadInstBatch::CDetailDoodadInstBatch() {
    this->m_texture = nullptr;
    this->m_vertexCount = 0;
    this->m_indexCount = 0;
    this->m_vertexBuf = nullptr;
    this->m_indexBuf = nullptr;
    this->m_windClass = 0;
}

// VERIFY: 9e2e57b8 manual
// OFFSET: 0x007B36E0
CDetailDoodadInst::CDetailDoodadInst() {
    this->m_age = 0;
    this->m_densityLevel = 0;
    this->m_chunk = nullptr;
}

// VERIFY: 166795d2 manual
// OFFSET: 0x007B3780
CDetailDoodadInst::~CDetailDoodadInst() {
    this->Free();
}

// VERIFY: dfac07db auto
// OFFSET: 0x007B3910
CDetailDoodadInst* CDetailDoodad::AllocInst() {
    uint32_t memHandle;
    void* object = nullptr;

    if (!ObjectAlloc(*CDetailDoodad::s_instHeap, &memHandle, &object, false)) {
        return nullptr;
    }

    CDetailDoodadInst* inst = new (object) CDetailDoodadInst();
    inst->m_objectIndex = memHandle;

    return inst;
}

// VERIFY: d19634a7 auto
// OFFSET: 0x007B3960
void CDetailDoodad::FreeInst(CDetailDoodadInst* inst) {
    uint32_t* heap = CDetailDoodad::s_instHeap;
    inst->~CDetailDoodadInst();
    ObjectFree(*heap, inst->m_objectIndex);
}
