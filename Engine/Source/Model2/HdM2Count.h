// HD lot I1 (HD 2026-11-04): counters only, for hdCataRenderPerf (WoW/Source/WorldClient/HD/HdCataRenderPerf.cpp).
// The engine cannot include WoW headers, so the counts live here; HdCataRenderPerf switches them on, reads and clears them.
// Nothing here changes what is drawn: with hdCataRenderPerf 0 every Add is one test of a bool.
#pragma once
#include <cstdint>
namespace HDM2Count {
// M2 elements built by CM2Scene::Animate, by the reason CM2Model::IsBatchDoodadCompatible would give (first failing test),
// then the draws CM2SceneRender::DrawBatchDoodad issues and the instances they carry.
enum : uint32_t { Batchable, NoCacheFlag, NoModelFlag, OneBoneOnly, NoBatchFlag, LocalLights, Transparent, Projected, BatchDraws, BatchInstances, Count };
inline bool on = false;
inline uint64_t n[Count]{};
inline void Add(uint32_t what, uint64_t k = 1) { if (on) n[what] += k; }
}
