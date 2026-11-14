// HD: fixed-storage CPU instrumentation for the Cata optimization integration.
#pragma once
#include <cstdint>
namespace HD::CataRenderPerf {
enum class Stage:uint32_t {Render,PrepareMap,WmoPrepare,Terrain,WmoDraw,Horizon,HorizonCull,SortCull,Entities,DetailDoodads,M2Animate,Shadows,RootWait,GroupWait,
 // Whole image: wall time between two scene renders, and the world frame's parts outside CWorldScene::Render.
 Frame,WorldFrame,M2Opaque,M2Alpha,DrawLists,HdOpaque,Liquid,Spells,PostFx,
 // Inside the grass pass: per-chunk lights, shader constants, draws, and vertex/index refills.
 GrassLights,GrassConstants,GrassDraw,GrassFill,
 // Inside sortCull (CWorldScene::CullSortTable): its parts, summed over the 64 distance buckets.
 CullVolumes,CullChunks,CullGroups,CullLiquid,CullEntitys,CullDoodads,CullOcclusion,CullHorizon,
 // Inside cullChunks: the chunk's doodads (CullDoodadsExterior) and its render prep (terrain batch, grass creation).
 CullChunkDoodads,CullChunkPrep,
 // Main-thread scheduler pass (Engine/Source/Event/EvtPerf.h, same order as EvtPerfPhase): sleep, update, timers, input,
 // poll handlers, queued events, idle handlers, paint, and the present inside the paint.
 LoopWait,LoopUpdate,LoopTimer,LoopInput,LoopPoll,LoopQueue,LoopIdle,LoopPaint,Present,
 // Inside them: CGWorldFrame::Update and CGGameUI::Idle (idle), the Lua GC step in CGGameUI::Idle, PollNet (poll).
 IdleWorld,IdleUi,LuaGc,PollNet,
 // HD 2026-11-04 lot S1: native shadows (CShadowCache::Update) split: dynamic part, band gather+query, terrain query per band, render per band.
 ShadowDyn,ShadowGather,ShadowTerrainQ0,ShadowTerrainQ1,ShadowTerrainQ2,ShadowBand0,ShadowBand1,ShadowBand2,
 // HD 2026-11-05 lot H2: grass vertex/index bake alone (FillVertices + FillIndices). Before H2 they also added to
 // GrassFill inside DetailDoodadPrepareBuffers, so a refill was counted twice there.
 GrassBake,Count};
enum class Counter:uint32_t {WmoDefs,WmoGroups,RootWaits,GroupWaits,AsyncReads,HorizonAll,HorizonCandidates,HorizonFallbacks,HorizonMismatch,GrassChunks,GrassDraws,GrassFills,
 // Grass batches by models: one model (the Cata instancing can take them) or several sharing a texture (it cannot).
 GrassSingleModel,GrassMixedModels,GrassInsts,
 // hdGrassPack: vertices given to acquired pairs, and batches that did not fit (stream buffers instead).
 GrassPackVertices,GrassPackAcquires,GrassPackFull,GrassPackIndices,GrassPackIndexFull,GrassPackEvicts,GrassFillSkipped,
 // sortCull work: chunks tested, chunks kept, horizon buffer updates, doodads tested.
 CullChunksTested,CullChunksKept,CullClipUpdates,CullDoodadsTested,
 // HD 2026-11-04 lot C2: terrain batches without a pool buffer (vertices rebuilt every frame), and AllocBuf failures.
 TerrainStreamed,TerrainPoolFull,
 // HD 2026-11-04 lot I1: M2 elements by batching verdict (same order as HDM2Count, Engine/Source/Model2/HdM2Count.h),
 // batched draws and their instances, then doodads added to the M2 scene by distance (yards).
 M2Batchable,M2NoCacheFlag,M2NoModelFlag,M2OneBoneOnly,M2NoBatchFlag,M2LocalLights,M2Transparent,M2Projected,M2BatchDraws,M2BatchInstances,
 DoodadsTo100,Doodads100To300,Doodads300To600,DoodadsBeyond600,Count};
void Initialize(); void Shutdown(); bool Enabled();
uint64_t Begin(); void End(Stage,uint64_t start); void Add(Counter,uint64_t amount=1); void FrameEnd(); void FrameStart();
struct Scope {
 Stage stage;uint64_t start;
 explicit Scope(Stage s):stage(s),start(Begin()){}
 ~Scope(){if(start)End(stage,start);}
 Scope(const Scope&)=delete;Scope&operator=(const Scope&)=delete;
};
struct FrameScope {
 uint64_t start=(FrameStart(),Begin());
 ~FrameScope(){if(start){End(Stage::Render,start);FrameEnd();}}
};
}
