// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HdFsrRigidMath.h"
#include <cstdint>
#include <cstring>
namespace HD::Fsr::Skin {
constexpr uint32_t MaxRows=225,UniformBytes=1296;
inline bool FiniteRows(const float(*rows)[4],uint32_t count){
#if (defined(_M_X64)||defined(__SSE2__)) && !defined(FSR_M3_SCALAR_REFERENCE)
 const auto mask=_mm_set1_epi32(0x7f800000);auto bad=_mm_setzero_si128();
 for(uint32_t i=0;i<count;++i){auto bits=_mm_castps_si128(_mm_loadu_ps(rows[i]));bad=_mm_or_si128(bad,_mm_cmpeq_epi32(_mm_and_si128(bits,mask),mask));}
 return _mm_movemask_epi8(bad)==0;
#else
 for(uint32_t i=0;i<count;++i)for(float v:rows[i])if(!std::isfinite(v))return false;return true;
#endif
}

struct Uniform {float pixel[16][4];float misc[4];float vertex[64][4];};
static_assert(sizeof(Uniform)==UniformBytes,"same std140 block as the 64-row native bone-palette variant");
struct Pose {Uniform uniform{};float bones[MaxRows][4]{};};
struct Input {const float(*pixel)[4]=nullptr;const float*misc=nullptr;const float(*vertex)[4]=nullptr;const float(*bones)[4]=nullptr;uint32_t rows=0;uint64_t paletteSerial=0;};
enum class Result {Valid,First,Full,Conflict,Invalid,Shared}; // Shared: see HdFsrRigidMath.h (HD 2026-10-31)
struct Observation {Result result=Result::Invalid;uint32_t cookie=~uint32_t(0);const Pose*previous=nullptr;bool captured=false;uint32_t bytesCopied=0;};
template<uint32_t Count=2048,uint32_t Probes=64>class Cache {
 static_assert(Count&&!(Count&(Count-1)),"power of two");
 struct Entry {uint64_t owner=0,part=0,frame=0,drawn=0,paletteSerial=0;uint32_t rows=0,index=0;uint8_t state=0;bool valid=false,poisoned=false;Pose pose[2];};
 Entry entries_[Count]{};uint32_t sweep_=0;
public:
 void Clear(){for(auto&e:entries_){e.state=0;e.frame=e.drawn=0;}sweep_=0;}
 void Sweep(uint64_t frame){for(uint32_t n=0;n<16&&n<Count;++n){auto&e=entries_[sweep_++&(Count-1)];if(e.state==1&&frame>e.frame&&frame-e.frame>8)e.state=2;}}
 Observation Observe(uint64_t owner,uint64_t part,uint64_t frame,const Input&in){
  Observation out;if(!owner||!frame||!in.rows||in.rows>MaxRows||!in.pixel||!in.misc||!in.vertex||!in.bones)return out;
  const uint32_t start=uint32_t(Rigid::Mix(owner^Rigid::Mix(part)^Rigid::Mix(in.rows)))&(Count-1);Entry*found=nullptr,*free=nullptr;
  for(uint32_t n=0;n<Probes&&n<Count;++n){auto&e=entries_[(start+n)&(Count-1)];if(e.state==1&&e.owner==owner&&e.part==part&&e.rows==in.rows){found=&e;break;}if(e.state!=1&&!free)free=&e;if(!e.state)break;}
  if(!found){if(!free){out.result=Result::Full;return out;}found=free;*found=Entry{};found->owner=owner;found->part=part;found->rows=in.rows;found->state=1;}
  Entry&e=*found;out.cookie=uint32_t(found-entries_);
  if(e.frame!=frame||e.paletteSerial!=in.paletteSerial){
   if(!FiniteRows(in.bones,in.rows))return out;
  }
  if(!FiniteRows(in.vertex+2,4))return out;
  if(e.frame==frame){
   const Pose&now=e.pose[e.index];const bool differentProjection=std::memcmp(now.uniform.vertex+2,in.vertex+2,64)!=0;
   const bool differentBones=e.paletteSerial!=in.paletteSerial&&std::memcmp(now.bones,in.bones,in.rows*16)!=0;
   if(differentProjection||differentBones){out.result=e.valid?Result::Conflict:Result::Shared;e.poisoned=true;e.valid=false;return out;}
   e.paletteSerial=in.paletteSerial;
  }else{
   e.valid=!e.poisoned&&e.drawn&&e.frame<frame&&frame-e.drawn==1;e.poisoned=false;e.frame=frame;e.index^=1;e.paletteSerial=in.paletteSerial;
   Pose&now=e.pose[e.index];std::memcpy(now.uniform.pixel,in.pixel,256);std::memcpy(now.uniform.misc,in.misc,16);std::memcpy(now.uniform.vertex,in.vertex,1024);std::memcpy(now.bones,in.bones,in.rows*16);
   out.captured=true;out.bytesCopied=UniformBytes+in.rows*16;
  }
  out.result=e.valid?Result::Valid:Result::First;out.previous=e.valid?&e.pose[e.index^1]:nullptr;return out;
 }
 void Commit(uint32_t cookie,uint64_t frame){if(cookie<Count){auto&e=entries_[cookie];if(e.state==1&&e.frame==frame)e.drawn=frame;}}
 static constexpr uint32_t Capacity(){return Count;}
 static constexpr uint64_t Bytes(){return sizeof(Cache);}
};
}
