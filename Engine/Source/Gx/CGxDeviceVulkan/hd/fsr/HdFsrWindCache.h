// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HdFsrSkinCache.h"
#include <cstdint>
#include <cstring>
namespace HD::Fsr::WindMotion {
constexpr uint32_t UniformBytes=4368,BoneRows=225,WindBytes=4608,WindClockRow=12;
enum class Kind:uint32_t{Grass=1,Tree=2};
struct Uniform{float pixel[16][4],misc[4],vertex[256][4];};static_assert(sizeof(Uniform)==UniformBytes,"native full register block");
struct Pose{Uniform uniform{};float bones[BoneRows][4]{};float wind[WindBytes/16][4]{};};
struct Input{const float(*pixel)[4]=nullptr;const float*misc=nullptr;const float(*vertex)[4]=nullptr;const float(*high)[4]=nullptr;uint32_t highRows=0;const float(*bones)[4]=nullptr;uint32_t boneRows=0;const void*wind=nullptr;uint32_t windBytes=0;Kind kind=Kind::Grass;};
enum class Result{Valid,First,Wrapped,Full,Invalid,Conflict,Shared}; // Shared: see HdFsrRigidMath.h (HD 2026-10-31)
struct Observation{Result result=Result::Invalid;const Pose*previous=nullptr;uint32_t cookie=~uint32_t(0),bytesCopied=0;bool captured=false;};
template<uint32_t Count=1024,uint32_t Probes=64>class Cache{
 static_assert(Count&&!(Count&(Count-1)),"power of two");
 struct Entry{uint64_t owner=0,geometry=0,frame=0,drawn=0;uint32_t index=0,rows=0,high=0;Kind kind=Kind::Grass;uint8_t state=0;bool valid=false,poisoned=false;Pose pose[2];};Entry entries_[Count]{};uint32_t sweep_=0;
public:
 void Clear(){for(auto&e:entries_){e.state=0;e.frame=e.drawn=0;}sweep_=0;}
 void Sweep(uint64_t frame){for(uint32_t n=0;n<32&&n<Count;++n){auto&e=entries_[sweep_++&(Count-1)];if(e.state==1&&frame>e.frame&&frame-e.frame>8)e.state=2;}}
 Observation Observe(uint64_t owner,uint64_t geometry,uint64_t frame,const Input&in){
  Observation out;if(!owner||!frame||!in.pixel||!in.misc||!in.vertex||in.highRows>192||(in.highRows&&!in.high)||in.boneRows>BoneRows||(in.boneRows&&!in.bones)||!Skin::FiniteRows(in.vertex,8)||!Skin::FiniteRows(in.vertex+53,7))return out;
  if(in.kind==Kind::Tree&&(!in.wind||in.windBytes<208||in.windBytes>WindBytes||in.windBytes%16))return out;
  const uint32_t start=uint32_t(Rigid::Mix(owner^Rigid::Mix(geometry)))&(Count-1);Entry*found=nullptr,*free=nullptr;
  for(uint32_t n=0;n<Probes&&n<Count;++n){auto&e=entries_[(start+n)&(Count-1)];if(e.state==1&&e.owner==owner&&e.geometry==geometry){found=&e;break;}if(e.state!=1&&!free)free=&e;if(!e.state)break;}
  if(!found){if(!free){out.result=Result::Full;return out;}found=free;*found=Entry{};found->owner=owner;found->geometry=geometry;found->state=1;}
  auto&e=*found;out.cookie=uint32_t(found-entries_);
  if(e.frame==frame){const auto&now=e.pose[e.index];bool different=e.rows!=in.boneRows||e.high!=in.highRows||e.kind!=in.kind;
   if(in.kind==Kind::Grass)different=different||std::memcmp(now.uniform.vertex,in.vertex,8*16)||std::memcmp(now.uniform.vertex+53,in.vertex+53,7*16);
   else different=different||std::memcmp(now.uniform.vertex+2,in.vertex+2,4*16)||std::memcmp(now.wind,in.wind,in.windBytes)||std::memcmp(now.uniform.vertex+31,in.vertex+31,33*16)||(in.highRows&&std::memcmp(now.uniform.vertex+64,in.high,in.highRows*16));
   if(in.boneRows)different=different||std::memcmp(now.bones,in.bones,in.boneRows*16);
   if(different){out.result=e.valid?Result::Conflict:Result::Shared;e.poisoned=true;e.valid=false;return out;}
  }else{
   if(in.kind==Kind::Tree&&!Skin::FiniteRows(static_cast<const float(*)[4]>(in.wind)+1,in.windBytes/16-1))return out;
   if(in.boneRows&&!Skin::FiniteRows(in.bones,in.boneRows))return out;
   e.valid=!e.poisoned&&e.drawn&&frame>e.frame&&frame-e.drawn==1&&e.rows==in.boneRows&&e.high==in.highRows&&e.kind==in.kind;e.poisoned=false;e.frame=frame;e.rows=in.boneRows;e.high=in.highRows;e.kind=in.kind;e.index^=1;
   auto&now=e.pose[e.index];std::memcpy(now.uniform.pixel,in.pixel,256);std::memcpy(now.uniform.misc,in.misc,16);std::memcpy(now.uniform.vertex,in.vertex,1024);
   if(in.highRows)std::memcpy(now.uniform.vertex+64,in.high,in.highRows*16);std::memset(now.uniform.vertex+64+in.highRows,0,(192-in.highRows)*16);
   if(in.boneRows)std::memcpy(now.bones,in.bones,in.boneRows*16);std::memset(now.bones+in.boneRows,0,(BoneRows-in.boneRows)*16);
   if(in.kind==Kind::Tree)std::memcpy(now.wind,in.wind,in.windBytes);
   out.captured=true;out.bytesCopied=272+1024+in.highRows*16+in.boneRows*16+(in.kind==Kind::Tree?in.windBytes:0);
   if(e.valid&&in.kind==Kind::Tree&&e.pose[e.index^1].wind[WindClockRow][0]>now.wind[WindClockRow][0]){e.valid=false;out.result=Result::Wrapped;return out;}
  }
  out.result=e.valid?Result::Valid:Result::First;out.previous=e.valid?&e.pose[e.index^1]:nullptr;return out;
 }
 void Commit(uint32_t cookie,uint64_t frame){if(cookie<Count){auto&e=entries_[cookie];if(e.state==1&&e.frame==frame)e.drawn=frame;}}
 static constexpr uint64_t Bytes(){return sizeof(Cache);}
};
}
