// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <xmmintrin.h>
namespace HD::ClipSpans {
inline void Raise(float* buffer,int first,int last,float height){
 if(last-first<7){for(int i=first;i<=last;++i)if(height>buffer[i])buffer[i]=height;return;}
 const __m128 h=_mm_set1_ps(height);int i=first;
 for(;i+3<=last;i+=4){__m128 old=_mm_loadu_ps(buffer+i),m=_mm_cmpgt_ps(h,old);_mm_storeu_ps(buffer+i,_mm_or_ps(_mm_and_ps(m,h),_mm_andnot_ps(m,old)));}
 for(;i<=last;++i)if(height>buffer[i])buffer[i]=height;
}
inline bool AnyBelow(const float* buffer,int first,int last,float height){
 if(first>last)return false;
 if(height>buffer[first])return true;
 int i=first+1;
 if(last-first>=7){const __m128 h=_mm_set1_ps(height);for(;i+3<=last;i+=4)if(_mm_movemask_ps(_mm_cmpgt_ps(h,_mm_loadu_ps(buffer+i))))return true;}
 for(;i<=last;++i)if(height>buffer[i])return true;return false;
}
}
