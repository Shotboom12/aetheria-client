// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace HD::Lamp {
struct Vec {float x=0,y=0,z=0;};
inline bool Finite(Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline float Dist2(Vec a,Vec b){float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z;}
struct Sample {uint64_t owner=0,scene=0,model=0;Vec position,color;float constant=0,linear=.7f,quadratic=.03f;uint32_t stamp=0;bool visible=false;};
struct Entry {Sample light;uint64_t key=0;int next=-1;uint32_t cell=0;bool used=false;};
struct Selection {uint32_t count=0;std::array<uint32_t,4>indices{};std::array<float,4>scores{};};
// Fixed storage, render thread only. Values copied at actual animation; no stale light pointer is dereferenced.
class Registry {
public:
 static constexpr uint32_t Capacity=1024,Cells=4096;
 Registry(){heads.fill(-1);}
 uint32_t Track(const Sample&);void Forget(uint64_t owner);void Clear();
 const Entry*FindOwner(uint64_t owner)const;const Entry*FindKey(uint64_t key)const;
 const Entry&At(uint32_t i)const{return entries[i];}
 Selection Select(uint64_t scene,Vec center,float radius,uint64_t preferred=0)const;
 uint32_t Count()const{return count;}uint64_t Dropped()const{return dropped;}
private:
 std::array<Entry,Capacity>entries{};std::array<int,Cells>heads{};uint64_t nextKey=0,dropped=0;uint32_t count=0;
 void Unlink(uint32_t);static uint32_t Cell(int x,int y){return (uint32_t(x)&63u)+64u*(uint32_t(y)&63u);}
};
float Score(const Sample&,Vec center,float radius);
// Private metadata keeps p14.w's native low 17 bits intact; float remains exactly integral (<2^22).
constexpr uint32_t EmitterBit=1u<<23,PixelBit=1u<<22,SlotShift=17,EnableBit=1u<<21,NativeBits=(1u<<17)-1;
inline float Encode(float native,uint32_t mask,bool enabled){uint32_t n=std::isfinite(native)&&native>=0?uint32_t(std::min(native,float(NativeBits))):0;return float((n&NativeBits)|((enabled?(mask&15u):0u)<<SlotShift)|(enabled?EnableBit:0));}
inline uint32_t Slots(float encoded){return std::isfinite(encoded)&&encoded>=0&&encoded<float(1u<<24)?(uint32_t(encoded)>>SlotShift)&15u:0;}
}
