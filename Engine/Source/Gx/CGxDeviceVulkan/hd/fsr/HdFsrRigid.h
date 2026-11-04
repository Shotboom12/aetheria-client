// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace wxlvk::draw {struct Cmd;}
namespace HD::Fsr::Rigid {
struct Stats {uint64_t draws=0,writers=0,zero=0,unsafe=0,first=0,full=0,singular=0,conflict=0,scopeMiss=0,frames=0,shared=0;double cpuMs=0;}; // shared: conflicts that kept the frame safe (2026-10-31)
// No engine class is resized: instance identity is a lifetime token in a bounded side table.
class Scope {
public:Scope(const void*owner,const void*part,bool rigid,bool mapObject=false,uint32_t boneRows=0,const void*palettePart=nullptr,bool grass=false);~Scope();Scope(const Scope&)=delete;Scope&operator=(const Scope&)=delete;
private:uint64_t owner_=0;uintptr_t part_=0;bool rigid_=false;uint32_t kind_=0,rows_=0;uintptr_t palette_=0;
};
void Forget(const void*owner); // constructor/destructor/reload/owner-specific reset; no pointer dereference
void InvalidateAll(); // explicit teleport notification: conservatively invalidates every cached previous pose
void BeginFrame(bool enabled,uint64_t serial,uint32_t width,uint32_t height,bool reset,bool profile,bool rigidWriters=true,bool skeletal=false,bool wind=false);
uint64_t MotionOwner();uintptr_t MotionPart();bool GrassScope();
uint64_t ResetEpoch();uint64_t SkeletonOwner();uintptr_t SkeletonPart();uint32_t SkeletonRows();
void EndFrame();bool Active();bool FrameSafe();void UnsafeFrame();
// Called once while choosing a pipeline; fills 64 bytes, with an all-zero matrix on invalid history.
bool Prepare(const wxlvk::draw::Cmd&,bool wind,float columnMajor[16],uint32_t*cookie=nullptr);
void Issued(uint32_t mode,uint32_t cookie=~uint32_t(0)); // commit history only AFTER a real draw; no commit on texture/pipeline skip
bool WroteMotion(); // a mode-2 writer was issued this frame (fix 2026-10-31)
Stats ReadStats(bool reset);uint64_t MemoryBytes();
// DIAG M3 (2026-10-31): the M2 file of the current DrawBatch and its bone counts, and why a draw is no rigid writer.
struct DiagModel{const char*name_;uint32_t section_,bones_;DiagModel(const char*name,uint32_t sectionBones,uint32_t modelBones);~DiagModel();DiagModel(const DiagModel&)=delete;DiagModel&operator=(const DiagModel&)=delete;};
const char*DiagModelName();uint32_t DiagSectionBones();uint32_t DiagModelBones();uint32_t DiagScopeKind();
const char*WhyNoRigid(const wxlvk::draw::Cmd&,bool wind);
// DIAG conflits (2026-10-31): read only. Each distinct (model, vs, kind, what differs) key drawn twice in one frame.
uint32_t DiagConflictCount();bool DiagConflictText(uint32_t index,char*out,unsigned size);
}
