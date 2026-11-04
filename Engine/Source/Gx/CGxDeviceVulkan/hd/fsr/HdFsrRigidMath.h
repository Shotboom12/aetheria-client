// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif
namespace HD::Fsr::Rigid {
struct Matrix { float m[16]{}; };
inline bool Finite(const Matrix&a){for(float x:a.m)if(!std::isfinite(x))return false;return true;}
inline Matrix Multiply(const Matrix&a,const Matrix&b){Matrix c;
#if (defined(_M_X64) || defined(__SSE2__)) && !defined(FSR_M2_REFERENCE_BENCH)
 const auto b0=_mm_loadu_ps(b.m),b1=_mm_loadu_ps(b.m+4),b2=_mm_loadu_ps(b.m+8),b3=_mm_loadu_ps(b.m+12);
 for(int r=0;r<4;++r){const float*p=a.m+4*r;auto x=_mm_add_ps(_mm_mul_ps(_mm_set1_ps(p[0]),b0),_mm_mul_ps(_mm_set1_ps(p[1]),b1));x=_mm_add_ps(x,_mm_mul_ps(_mm_set1_ps(p[2]),b2));x=_mm_add_ps(x,_mm_mul_ps(_mm_set1_ps(p[3]),b3));_mm_storeu_ps(c.m+4*r,x);}
#else
 for(int r=0;r<4;++r)for(int k=0;k<4;++k){double v=0;for(int j=0;j<4;++j)v+=double(a.m[r*4+j])*b.m[j*4+k];c.m[r*4+k]=float(v);}
#endif
 return c;}
inline bool Inverse(const Matrix&a,Matrix&out){
 if(!Finite(a))return false;double x[4][8]{};for(int r=0;r<4;++r)for(int c=0;c<4;++c){x[r][c]=a.m[4*r+c];x[r][c+4]=r==c?1.:0.;}
 for(int c=0;c<4;++c){int pivot=c;for(int r=c+1;r<4;++r)if(std::fabs(x[r][c])>std::fabs(x[pivot][c]))pivot=r;
  if(std::fabs(x[pivot][c])<1e-14)return false;if(pivot!=c)for(int k=0;k<8;++k)std::swap(x[pivot][k],x[c][k]);
  double d=x[c][c];for(int k=0;k<8;++k)x[c][k]/=d;for(int r=0;r<4;++r)if(r!=c){d=x[r][c];for(int k=0;k<8;++k)x[r][k]-=d*x[c][k];}}
 for(int r=0;r<4;++r)for(int c=0;c<4;++c)out.m[4*r+c]=float(x[r][c+4]);
 if(!Finite(out))return false;const Matrix identity=Multiply(a,out);for(int r=0;r<4;++r)for(int c=0;c<4;++c)if(std::fabs(identity.m[4*r+c]-(r==c?1.f:0.f))>0.002f)return false;return true;
}
// The shader's projection is c2..c5, NOT c0..c3. c31..c33 maps local -> view, row-major.
// gl_Position = (D3D clip.x,-D3D clip.y,D3D clip.z,D3D clip.w); depth stays 0..1.
inline bool FromRows(const float(*rows)[4],Matrix&out){
 if(!rows)return false;Matrix p,mv;for(int r=0;r<4;++r)for(int c=0;c<4;++c)p.m[r*4+c]=rows[r+2][c];
 for(int r=0;r<3;++r)for(int c=0;c<4;++c)mv.m[r*4+c]=rows[r+31][c];mv.m[15]=1;
 out=Multiply(p,mv);for(int c=0;c<4;++c)out.m[4+c]=-out.m[4+c];return Finite(out);
}
inline bool Reprojection(const Matrix&current,const Matrix&previous,float columnMajor[16]){
 Matrix inverse;if(!Inverse(current,inverse)||!Finite(previous))return false;const Matrix t=Multiply(previous,inverse);if(!Finite(t))return false;
 for(int r=0;r<4;++r)for(int c=0;c<4;++c)columnMajor[c*4+r]=t.m[r*4+c];return true;
}
// Fast affine inverse: the pose is 3x4. A general projection inverse is shared by all draws using it.
inline bool InverseAffine(const float(*rows)[4],Matrix&out){
 const double a=rows[31][0],b=rows[31][1],c=rows[31][2],d=rows[32][0],e=rows[32][1],f=rows[32][2],g=rows[33][0],h=rows[33][1],i=rows[33][2];
 const double co[9]={e*i-f*h,c*h-b*i,b*f-c*e,f*g-d*i,a*i-c*g,c*d-a*f,d*h-e*g,b*g-a*h,a*e-b*d};
 const double det=a*co[0]+b*co[3]+c*co[6];if(!std::isfinite(det)||std::fabs(det)<1e-14)return false;
 out=Matrix{};out.m[15]=1;for(int r=0;r<3;++r){double t=0;for(int col=0;col<3;++col){const double v=co[r*3+col]/det;out.m[r*4+col]=float(v);t-=v*rows[col+31][3];}out.m[r*4+3]=float(t);}return Finite(out);
}
inline bool FromRowsWithProjection(const float(*rows)[4],const Matrix&projection,const Matrix&inverseProjection,Matrix&now,Matrix&inverseNow){
 Matrix mv,affine;for(int r=0;r<3;++r)for(int c=0;c<4;++c)mv.m[r*4+c]=rows[r+31][c];mv.m[15]=1;
 if(!InverseAffine(rows,affine))return false;
 now=Multiply(projection,mv);for(int c=0;c<4;++c)now.m[4+c]=-now.m[4+c];
 inverseNow=Multiply(affine,inverseProjection);for(int r=0;r<4;++r)inverseNow.m[r*4+1]=-inverseNow.m[r*4+1];return Finite(now)&&Finite(inverseNow);
}
inline uint64_t Mix(uint64_t x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
enum class HistoryResult { Valid, First, Full, Conflict, Singular, Shared };
// HD 2026-10-31 (ghosting): Conflict = a key seen again this frame with another matrix AFTER its first draw this frame wrote a
// motion vector (the frame is unsafe). Shared = the same, but the first draw this frame was already a zero marker (the key was
// poisoned last frame): every draw of this key is camera-only, no pixel holds a doubtful vector, the frame stays safe.
template<uint32_t Count=32768,uint32_t Probes=64>class History {
 static_assert(Count&&!(Count&(Count-1)),"power-of-two capacity");
 struct Entry{uint64_t owner=0,part=0,frame=0,drawn=0;Matrix current,previous;uint8_t state=0;bool valid=false,poisoned=false;};
 Entry entries_[Count]{};uint32_t sweep_=0;
public:
 void Clear(){for(auto&e:entries_)e=Entry{};sweep_=0;}
 void Sweep(uint64_t frame){for(uint32_t n=0;n<128&&n<Count;++n){auto&e=entries_[sweep_++&(Count-1)];if(e.state==1&&frame>e.frame&&frame-e.frame>8)e.state=2;}}
 HistoryResult Observe(uint64_t owner,uint64_t part,uint64_t frame,const Matrix&now,float out[16],const Matrix*inverseNow=nullptr,uint32_t*cookie=nullptr){
  if(cookie)*cookie=~uint32_t(0);std::memset(out,0,64);if(!owner||!frame||!Finite(now))return HistoryResult::Singular;
  const uint32_t start=uint32_t(Mix(owner^Mix(part)))&(Count-1);Entry*free=nullptr;Entry*found=nullptr;
  for(uint32_t n=0;n<Probes&&n<Count;++n){auto&e=entries_[(start+n)&(Count-1)];if(e.state==1&&e.owner==owner&&e.part==part){found=&e;break;}if(e.state!=1&&!free)free=&e;if(!e.state)break;}
  if(!found){if(!free)return HistoryResult::Full;found=free;*found=Entry{};found->state=1;found->owner=owner;found->part=part;}
  Entry&e=*found;
  if(cookie)*cookie=uint32_t(found-entries_);
  if(e.frame==frame){if(std::memcmp(e.current.m,now.m,64)){const bool wroteMotion=e.valid;e.valid=false;e.poisoned=true;return wroteMotion?HistoryResult::Conflict:HistoryResult::Shared;}}
  else{e.valid=!e.poisoned&&e.drawn&&e.frame<frame&&frame-e.drawn==1;e.poisoned=false;e.previous=e.current;e.current=now;e.frame=frame;}
  if(!e.valid)return HistoryResult::First;
  if(inverseNow){const Matrix t=Multiply(e.previous,*inverseNow);if(!Finite(t))return HistoryResult::Singular;for(int r=0;r<4;++r)for(int c=0;c<4;++c)out[c*4+r]=t.m[r*4+c];return HistoryResult::Valid;}
  return Reprojection(now,e.previous,out)?HistoryResult::Valid:HistoryResult::Singular;
 }
 void Commit(uint32_t cookie,uint64_t frame){if(cookie<Count){auto&e=entries_[cookie];if(e.state==1&&e.frame==frame)e.drawn=frame;}}
 static constexpr uint64_t Bytes(){return sizeof(History);}
};
}
