// HD: Cata 15595's near blocking volume and wide asynchronous guard, typed for 3.3.5 x64.
#pragma once
#include <algorithm>
#include <cmath>
namespace HD::CataWmoStreaming {
struct Point {float x=0,y=0,z=0;};
struct Box {Point b,t;};
inline bool Finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
inline bool Valid(Box b){return Finite(b.b)&&Finite(b.t)&&b.b.x<=b.t.x&&b.b.y<=b.t.y&&b.b.z<=b.t.z;}
inline bool Intersects(Box a,Box b){return a.b.x<=b.t.x&&a.b.y<=b.t.y&&a.b.z<=b.t.z&&a.t.x>=b.b.x&&a.t.y>=b.b.y&&a.t.z>=b.b.z;}
inline Box Cube(Point p,float radius){return {{p.x-radius,p.y-radius,p.z-radius},{p.x+radius,p.y+radius,p.z+radius}};}
inline float GuardDistance(float farClip,float cornerDistance){
 float d=cornerDistance;if(farClip>d)d=1.33f*farClip;else if(d>2*farClip)d=2*farClip;return std::max(d,266.66666f);
}
struct Interest {
 Box guard{},nearby{};bool valid=false;
 void Update(Point p,float farClip,float cornerDistance){
  valid=Finite(p)&&std::isfinite(farClip)&&farClip>0&&farClip<=2500&&std::isfinite(cornerDistance)&&cornerDistance>=0;
  if(!valid)return;guard=Cube(p,GuardDistance(farClip,cornerDistance));nearby=Cube(p,std::min(farClip,380.25f));valid=Valid(guard)&&Valid(nearby);
 }
 bool Prepare(Box box,bool native)const{return valid&&Valid(box)?Intersects(guard,box):native;}
 bool RootWait(Box box)const{return !valid||!Valid(box)||Intersects(nearby,box);}
 // Preserve the 3.3.5 +/-150 collision safety volume as well, including small native farclip values.
 bool GroupWait(Box box,bool nativeCollision)const{return nativeCollision||RootWait(box);}
};
template<class EngineBox>inline Box Convert(const EngineBox&box){return {{box.b.x,box.b.y,box.b.z},{box.t.x,box.t.y,box.t.z}};}
void Initialize();void Shutdown();bool Enabled();void Invalidate();void Update(Point,float farClip,float cornerDistance);
bool Prepare(Box,bool native);bool RootWait(Box);bool GroupWait(Box,bool nativeCollision);
}
