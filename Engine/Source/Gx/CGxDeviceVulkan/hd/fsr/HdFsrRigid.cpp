// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef WOWDECOMP_FSR
#include "HdFsrRigid.h"
#include "HdFsrRigidMath.h"
#include "../../wxl/src/draw/Cmd.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <limits>
#include <cstring>
#include <cstdio>
#include <initializer_list>
namespace HD::Fsr::Rigid {
namespace {
struct Context{uint64_t owner=0;uintptr_t part=0;bool rigid=false;uint32_t kind=0,rows=0;uintptr_t palette=0;};thread_local Context current;
struct Life{uintptr_t pointer=0;uint64_t token=0;uint8_t state=0;};constexpr uint32_t LifeCount=16384;
Life lives[LifeCount]{};std::mutex lifeMutex;uint64_t nextToken=0;std::atomic<bool>ever{false},invalidated{false};
std::atomic<uint64_t> epoch{0};bool rigidWriters=true,collectSkin=false,collectWind=false;
History<> history;bool active=false,wasEnabled=false,safe=true,profile=false,wrote=false;uint64_t serial=0;uint32_t width=0,height=0;Stats stats;
Matrix lastProjection,inverseProjection;bool haveProjection=false;
uint64_t Acquire(const void*owner){
 if(!owner)return 0;std::lock_guard<std::mutex>lock(lifeMutex);const uintptr_t pointer=reinterpret_cast<uintptr_t>(owner);const uint32_t start=uint32_t(Mix(uint64_t(pointer)))&(LifeCount-1);Life*free=nullptr;
 for(uint32_t n=0;n<128;++n){auto&e=lives[(start+n)&(LifeCount-1)];if(e.state==1&&e.pointer==pointer)return e.token;if(e.state!=1&&!free)free=&e;if(!e.state)break;}
 if(!free||nextToken==(std::numeric_limits<uint64_t>::max)())return 0;
 *free=Life{pointer,++nextToken,1};return free->token;
}
uint64_t Geometry(const wxlvk::draw::Cmd&c){
 uint64_t vb=0,ib=0;static_assert(sizeof c.vertexBuffer<=sizeof vb,"x64 Vulkan handle");std::memcpy(&vb,&c.vertexBuffer,sizeof c.vertexBuffer);std::memcpy(&ib,&c.indexBuffer,sizeof c.indexBuffer);
 uint64_t h=Mix(vb)^Mix(ib)^Mix(uint64_t(current.part));
 for(uint64_t x:{uint64_t(uint32_t(c.vertexOffset)),uint64_t(c.firstIndex),uint64_t(c.indexCount),uint64_t(c.vertexStride),uint64_t(c.vertexFormat),uint64_t(c.topology)})h=Mix(h^x);
 for(uint32_t i=0;i<sizeof c.vs&&c.vs[i];++i)h=Mix(h^uint8_t(c.vs[i]));return h;
}
struct CpuScope{std::chrono::steady_clock::time_point start;bool on;CpuScope():on(profile&&active){if(on)start=std::chrono::steady_clock::now();}~CpuScope(){if(on)stats.cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}};
}
Scope::Scope(const void*owner,const void*part,bool rigid,bool mapObject,uint32_t rows,const void*palette,bool grass):owner_(current.owner),part_(current.part),rigid_(current.rigid),kind_(current.kind),rows_(current.rows),palette_(current.palette){
 CpuScope timing;current=Context{};if(active&&(rigid||(collectSkin&&rows)||(collectWind&&owner&&!mapObject))){current.owner=Acquire(owner);current.part=reinterpret_cast<uintptr_t>(part);current.rigid=rigid&&current.owner!=0;current.rows=rows;current.palette=reinterpret_cast<uintptr_t>(palette);current.kind=grass?3u:mapObject?2u:1u;if(!current.owner)++stats.full;}
}
Scope::~Scope(){current={owner_,part_,rigid_,kind_,rows_,palette_};}
void Forget(const void*owner){
 if(!owner||!ever.load(std::memory_order_relaxed))return;std::lock_guard<std::mutex>lock(lifeMutex);uintptr_t pointer=reinterpret_cast<uintptr_t>(owner);uint32_t start=uint32_t(Mix(uint64_t(pointer)))&(LifeCount-1);
 for(uint32_t n=0;n<128;++n){auto&e=lives[(start+n)&(LifeCount-1)];if(e.state==1&&e.pointer==pointer){e.state=2;return;}if(!e.state)return;}
}
void InvalidateAll(){if(ever.load(std::memory_order_relaxed)){epoch.fetch_add(1,std::memory_order_acq_rel);invalidated.store(true,std::memory_order_release);}}
void BeginFrame(bool enabled,uint64_t frame,uint32_t w,uint32_t h,bool reset,bool profiling,bool writers,bool skeletal,bool wind){
 rigidWriters=writers;collectSkin=skeletal;collectWind=wind;
 if(!enabled){active=false;wasEnabled=false;profile=false;return;}ever.store(true,std::memory_order_relaxed);
 const bool dirty=invalidated.exchange(false,std::memory_order_acq_rel);
 if(reset||!wasEnabled||!serial||frame<=serial||frame-serial!=1||width!=w||height!=h||dirty)history.Clear();
 serial=frame;width=w;height=h;active=true;wasEnabled=true;safe=true;wrote=false;profile=profiling;history.Sweep(frame);++stats.frames;
}
uint64_t MotionOwner(){return active&&collectWind&&(current.kind==1||current.kind==3)?current.owner:0;}
uintptr_t MotionPart(){return current.part;}bool GrassScope(){return current.kind==3;}
uint64_t ResetEpoch(){return epoch.load(std::memory_order_acquire);}
uint64_t SkeletonOwner(){return active&&collectSkin&&current.kind==1?current.owner:0;}
uintptr_t SkeletonPart(){return current.palette;}uint32_t SkeletonRows(){return SkeletonOwner()?current.rows:0;}
void EndFrame(){active=false;}
bool Active(){return active;}bool FrameSafe(){return !active||(safe&&!invalidated.load(std::memory_order_acquire));}
void UnsafeFrame(){if(active){safe=false;++stats.unsafe;}}
// DIAG conflits (HD 2026-10-31): read only, changes no decision. The first draw of each key this frame leaves the hashes of its
// projection (rows 2..5) and pose (rows 31..33); a later conflicting draw says which of the two differs. Names are copied.
namespace{
struct SeenDraw{uint64_t frame=0,projection=0,pose=0;};SeenDraw seenDraws[32768];
struct ConflictNote{uint64_t key=0,hits=0,unsafe=0;char model[64]{};char vs[48]{};uint32_t kind=0,section=0,bones=0,indices=0;bool projection=false,pose=false;};
constexpr uint32_t NoteCapacity=32;ConflictNote notes[NoteCapacity];std::atomic<uint32_t>noteCount{0};
uint64_t HashFloats(const float*f,uint32_t n){uint64_t h=0x9e3779b97f4a7c15ULL;for(uint32_t i=0;i<n;++i){uint32_t b;std::memcpy(&b,f+i,4);h=Mix(h^b);}return h;}
void DiagObserve(HistoryResult r,uint32_t cookie,const wxlvk::draw::Cmd&c){
 if(cookie>=32768||!c.vertexRows)return;const uint64_t hp=HashFloats(&c.vertexRows[2][0],16),hm=HashFloats(&c.vertexRows[31][0],12);auto&s=seenDraws[cookie];
 if(r!=HistoryResult::Conflict&&r!=HistoryResult::Shared){if(s.frame!=serial){s.frame=serial;s.projection=hp;s.pose=hm;}return;}
 const bool p=s.frame==serial&&s.projection!=hp,m=s.frame==serial&&s.pose!=hm;const char*model=DiagModelName();
 uint64_t key=Mix((uint64_t(current.kind)<<56)^(uint64_t(p)<<60)^(uint64_t(m)<<61));for(const char*q=model;q&&*q;++q)key=Mix(key^uint8_t(*q));for(uint32_t i=0;i<sizeof c.vs&&c.vs[i];++i)key=Mix(key^uint8_t(c.vs[i]));
 const uint32_t count=noteCount.load(std::memory_order_relaxed);uint32_t i=0;for(;i<count;++i)if(notes[i].key==key)break;
 if(i==count){if(count>=NoteCapacity)return;auto&n=notes[count];n=ConflictNote{};n.key=key;
  for(uint32_t k=0;model&&model[k]&&k+1<sizeof n.model;++k)n.model[k]=model[k];for(uint32_t k=0;k<sizeof c.vs&&c.vs[k]&&k+1<sizeof n.vs;++k)n.vs[k]=c.vs[k];
  n.kind=current.kind;n.section=DiagSectionBones();n.bones=DiagModelBones();n.indices=c.indexCount;n.projection=p;n.pose=m;noteCount.store(count+1,std::memory_order_release);}
 ++notes[i].hits;if(r==HistoryResult::Conflict)++notes[i].unsafe;
}
}
uint32_t DiagConflictCount(){return noteCount.load(std::memory_order_acquire);}
bool DiagConflictText(uint32_t i,char*out,unsigned size){
 if(i>=DiagConflictCount()||!out||!size)return false;const auto&n=notes[i];
 std::snprintf(out,size,"#%u %s model \"%s\" vs=\"%s\" (section bones %u, model bones %u, indices %u): differs from the first draw this frame in projection %s, pose %s; %llu hit(s), %llu made the frame unsafe",
  i+1,n.kind==2?"WMO":n.kind==3?"grass":n.kind==1?"M2":"?",n.model[0]?n.model:"-",n.vs,n.section,n.bones,n.indices,n.projection?"yes":"no",n.pose?"yes":"no",(unsigned long long)n.hits,(unsigned long long)n.unsafe);return true;
}
bool Prepare(const wxlvk::draw::Cmd&c,bool wind,float out[16],uint32_t*cookie){
 if(cookie)*cookie=~uint32_t(0);std::memset(out,0,64);if(!active||!rigidWriters)return false;CpuScope timing;++stats.draws;
 if(wind||!current.rigid||!current.owner||!c.vertexRows||!c.depthWrite||!c.depthTest||c.blend>1||!c.colorWrite||c.topology!=3||!std::isfinite(c.viewport.width)||!std::isfinite(c.viewport.height)||std::fabs(c.viewport.width-float(width))>.01f||std::fabs(c.viewport.height-float(height))>.01f){
  ++stats.scopeMiss;return false;
 }
 if((current.kind==1&&std::strncmp(c.vs,"Diffuse_",8))||(current.kind==2&&std::strncmp(c.vs,"MapObj",6))||!current.kind){++stats.scopeMiss;return false;}
#ifdef FSR_M2_REFERENCE_BENCH
 Matrix matrix;if(!FromRows(c.vertexRows,matrix)){++stats.singular;return true;}
 const auto result=history.Observe(current.owner,Geometry(c),serial,matrix,out,nullptr,cookie);
#else
 Matrix projection;std::memcpy(projection.m,c.vertexRows+2,64);
 if(!haveProjection||std::memcmp(projection.m,lastProjection.m,64)){
  haveProjection=Inverse(projection,inverseProjection);lastProjection=projection;
 }
 Matrix matrix,inverse;
 if(!haveProjection||!FromRowsWithProjection(c.vertexRows,lastProjection,inverseProjection,matrix,inverse)){++stats.singular;return true;}
 uint32_t diagCookie=~uint32_t(0);uint32_t*observed=cookie?cookie:&diagCookie;
 const auto result=history.Observe(current.owner,Geometry(c),serial,matrix,out,&inverse,observed);DiagObserve(result,*observed,c);
#endif
 switch(result){case HistoryResult::Valid:break;case HistoryResult::First:++stats.first;break;case HistoryResult::Full:++stats.full;break;case HistoryResult::Singular:++stats.singular;break;case HistoryResult::Conflict:++stats.conflict;UnsafeFrame();break;case HistoryResult::Shared:++stats.conflict;++stats.shared;break;}
 // Even first/singular history uses the same shader with ZERO transform: fragment clears its marker.
 return true;
}
// FIX HD 2026-10-31: attachment 6 is cleared every world frame, so a depth writer without a marker output is only unsafe once a motion writer (mode 2 rigid, 3 skin or 4 wind) has marked pixels it could cover (sky/ff draws issued before the objects are safe).
void Issued(uint32_t mode,uint32_t cookie){if(!active)return;history.Commit(cookie,serial);if(mode==2||mode==3||mode==4){++stats.writers;wrote=true;}else if(mode==1)++stats.zero;else if(wrote)UnsafeFrame();}
bool WroteMotion(){return active&&wrote;}
Stats ReadStats(bool reset){const Stats out=stats;if(reset)stats=Stats{};return out;}
uint64_t MemoryBytes(){return sizeof history+sizeof lives;}
// DIAG M3 (HD 2026-10-31): read only, changes no decision.
namespace{thread_local const char*diagName=nullptr;thread_local uint32_t diagSection=0,diagBones=0;}
DiagModel::DiagModel(const char*name,uint32_t sectionBones,uint32_t modelBones):name_(diagName),section_(diagSection),bones_(diagBones){diagName=name;diagSection=sectionBones;diagBones=modelBones;}
DiagModel::~DiagModel(){diagName=name_;diagSection=section_;diagBones=bones_;}
const char*DiagModelName(){return diagName;}uint32_t DiagSectionBones(){return diagSection;}uint32_t DiagModelBones(){return diagBones;}uint32_t DiagScopeKind(){return current.kind;}
const char*WhyNoRigid(const wxlvk::draw::Cmd&c,bool wind){
 if(!active)return "rigid inactive";if(!rigidWriters)return "M2 off";if(wind)return "wind (M4)";if(!current.kind)return "no model scope";
 if(!current.rigid)return current.kind==1?"not single-bone":"WMO not owner";if(!current.owner)return "no owner token";if(!c.vertexRows)return "no vertex rows";
 if(!c.depthWrite||!c.depthTest)return "no depth write/test";if(c.blend>1)return "blended";if(!c.colorWrite)return "no colour write";if(c.topology!=3)return "not triangle list";
 if(!std::isfinite(c.viewport.width)||!std::isfinite(c.viewport.height)||std::fabs(c.viewport.width-float(width))>.01f||std::fabs(c.viewport.height-float(height))>.01f)return "viewport not world size";
 if((current.kind==1&&std::strncmp(c.vs,"Diffuse_",8))||(current.kind==2&&std::strncmp(c.vs,"MapObj",6)))return "vertex shader family";return "ok";
}
}
#endif
