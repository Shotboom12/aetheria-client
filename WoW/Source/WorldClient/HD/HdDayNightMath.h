// HD: pure, allocation-free time-of-day interpolation. Native weather is never filtered here.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace HD::DayNightMath {
constexpr double Day=2880.0;
inline double Wrap(double x,double period=1.0){return x-std::floor(x/period)*period;}
inline double Shortest(double d){d-=std::floor(d+.5);return d==-.5?.5:d;}
inline float Ease(float t){t=std::clamp(t,0.f,1.f);return t*t*(3.f-2.f*t);}
inline float AmbientRatio(float sky,float hemisphere){
 if(!std::isfinite(sky)||!std::isfinite(hemisphere))return 1.f;
 const float raw=std::clamp(hemisphere/std::max(sky,.001f),0.f,4.f);
 return 1.f+(raw-1.f)*Ease(sky*(255.f/2.f));
}
inline uint32_t SkyTime(uint32_t length,float phase){
 if(!length||!std::isfinite(phase))return 0;
 return std::min(uint32_t(double(length)*Wrap(phase)),length-1);
}
struct Clock {
 double phase=0,target=0,lastRaw=0;uint32_t tick=0;int32_t map=0;bool valid=false;
 void Reset(){valid=false;}
 float Update(float raw,uint32_t now,int32_t mapId,float halfLife,float minutesPerSecond){
  if(!std::isfinite(raw))return valid?float(Wrap(phase)):0.f;
  if(!valid||map!=mapId||!std::isfinite(halfLife)||halfLife<=0){phase=target=lastRaw=Wrap(raw);tick=now;map=mapId;valid=true;return halfLife>0?float(phase):raw;}
  const double wrapped=Wrap(raw);target+=Shortest(wrapped-lastRaw);lastRaw=wrapped;
  uint32_t elapsed=now-tick;tick=now;
  if(elapsed>60000)return float(Wrap(phase));
  const double dt=std::min(elapsed,250u)*.001;
  double advance=(target-phase)*(-std::expm1(-.6931471805599453094*dt/halfLife));
  const double rate=std::isfinite(minutesPerSecond)?std::clamp(double(minutesPerSecond),.1,120.):10.;
  const double limit=dt*rate/1440.;phase+=std::clamp(advance,-limit,limit);
  float result=float(Wrap(phase));return result>=1.f?std::nextafter(1.f,0.f):result;
 }
};
// Retains the original interval search in diagnostic mode. Fractions and eased endpoints in smooth mode.
inline void Range(const int32_t* times,int32_t count,float time,int32_t& index,float& t,int32_t& next,bool smooth){
 index=next=0;t=0;if(!times||count<=0||!std::isfinite(time))return;
 if(count==1)return;
 if(smooth)time=float(Wrap(time,Day));
 do{int32_t start=times[index];next=(index+1)%count;int32_t end=times[next];
  if(end>start){if(time>=start&&time<=end){t=(time-start)/(end-start);if(smooth)t=Ease(t);return;}}
  else if(time<=end||time>=start){end+=2880;if(time<start)time+=2880;t=(time-start)/(end-start);if(smooth)t=Ease(t);return;}
  ++index;
 }while(index<count);
 index=next=0;t=0;
}
inline float DayWeight(float phase,float widthMinutes){
 const float m=float(Wrap(phase))*1440.f,w=std::clamp(widthMinutes,1.f,120.f);
 return Ease((m-(290.f-w*.5f))/w)*(1.f-Ease((m-(1330.f-w*.5f))/w));
}
struct Direction {double x,y,z;};
inline double Dot(Direction a,Direction b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Direction Normalize(Direction v){double n=std::sqrt(Dot(v,v));return n>1e-12?Direction{v.x/n,v.y/n,v.z/n}:Direction{0,0,1};}
inline Direction Blend(Direction from,Direction to,float weight){
 if(weight<=0)return from;if(weight>=1)return to;
 Direction a=Normalize(from),b=Normalize(to);double d=std::clamp(Dot(a,b),-1.,1.);
 if(d>.9995)return Normalize({a.x+(b.x-a.x)*weight,a.y+(b.y-a.y)*weight,a.z+(b.z-a.z)*weight});
 Direction tangent{b.x-a.x*d,b.y-a.y*d,b.z-a.z*d};double n=std::sqrt(Dot(tangent,tangent));
 if(n<1e-8){tangent=std::abs(a.z)<.9?Direction{-a.y,a.x,0}:Direction{0,-a.z,a.y};tangent=Normalize(tangent);}
 else tangent={tangent.x/n,tangent.y/n,tangent.z/n};
 const double angle=std::acos(d)*weight,c=std::cos(angle),s=std::sin(angle);
 return {a.x*c+tangent.x*s,a.y*c+tangent.y*s,a.z*c+tangent.z*s};
}
}
