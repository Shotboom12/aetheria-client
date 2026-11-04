// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
class CM2Light;class CM2Lighting;
namespace HD::Lamp::Native {
void Init();void Shutdown();bool Enabled();
void Track(CM2Light*,uint64_t model);void ModelOwner(uint64_t model);void Forget(CM2Light*);void ClearScene();
void Repair(CM2Lighting*); // after lighting callback, BEFORE ComputeElementShaders selects count permutation
void BeginFrame(uint64_t serial,uint64_t scene,float cameraX,float cameraY,float cameraZ);
uint64_t FrameLight(float xyz[3],float&radius);uint32_t SlotMask(const CM2Lighting*,bool enabled);
void SnapshotPixelDraw(uint32_t slotMask);bool IsChosenOwner(uint64_t owner,float x,float y,float z);void SnapshotDraw(uint32_t slotMask);uint32_t DrawSlots();bool ProducerEnabled();
void SetProducerReady(bool);void UnsafeDepthDraw();bool FrameSafe();float Metadata(float native,const char*actualVsMd5,const float rows[][4]);
void Trace(const char*actualVsMd5,const float rows[][4]);void Tick();
}
