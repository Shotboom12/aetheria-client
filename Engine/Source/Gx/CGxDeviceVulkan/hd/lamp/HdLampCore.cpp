// SPDX-License-Identifier: GPL-3.0-or-later
#include "HdLampCore.h"
namespace HD::Lamp {
float Score(const Sample&l,Vec p,float radius){
 if(!l.owner||!l.scene||!l.visible||!Finite(l.position)||!Finite(l.color)||!Finite(p)||!std::isfinite(radius)||radius<0||radius>256)return 0;
 if(!std::isfinite(l.constant)||!std::isfinite(l.linear)||!std::isfinite(l.quadratic)||l.constant<0||l.linear<0||l.quadratic<0)return 0;
 float peak=std::max({l.color.x,l.color.y,l.color.z});if(!(peak>0))return 0;
 float d2=Dist2(l.position,p);if(!std::isfinite(d2)||d2>(32+radius)*(32+radius))return 0;
 float d=std::max(std::sqrt(d2)-radius,.05f),den=l.constant+l.linear*d+l.quadratic*d*d;
 if(!std::isfinite(den)||den<=0)return 0;float s=peak/den;return std::isfinite(s)&&s>=.002f?s:0;
}
const Entry*Registry::FindOwner(uint64_t owner)const{if(owner)for(const auto&e:entries)if(e.used&&e.light.owner==owner)return &e;return nullptr;}
const Entry*Registry::FindKey(uint64_t key)const{if(key)for(const auto&e:entries)if(e.used&&e.key==key)return &e;return nullptr;}
void Registry::Unlink(uint32_t i){auto&e=entries[i];int*p=&heads[e.cell];while(*p>=0){if(uint32_t(*p)==i){*p=e.next;break;}p=&entries[*p].next;}e.next=-1;}
uint32_t Registry::Track(const Sample&s){
 if(!s.owner||!s.scene||!Finite(s.position)||std::fabs(s.position.x)>100000||std::fabs(s.position.y)>100000||std::fabs(s.position.z)>100000||!Finite(s.color)||!std::isfinite(s.constant)||!std::isfinite(s.linear)||!std::isfinite(s.quadratic)||s.constant<0||s.linear<0||s.quadratic<0){Forget(s.owner);return ~0u;}
 uint32_t i=Capacity;for(uint32_t n=0;n<Capacity;++n)if(entries[n].used&&entries[n].light.owner==s.owner){i=n;break;}
 if(i==Capacity){for(uint32_t n=0;n<Capacity;++n)if(!entries[n].used){i=n;break;}if(i==Capacity){++dropped;return ~0u;}entries[i].used=true;entries[i].key=++nextKey;++count;}
 else{Unlink(i);if(entries[i].light.scene!=s.scene)entries[i].key=++nextKey;}
 auto&e=entries[i];e.light=s;e.cell=Cell(int(std::floor(s.position.x/32)),int(std::floor(s.position.y/32)));e.next=heads[e.cell];heads[e.cell]=int(i);return i;
}
void Registry::Forget(uint64_t owner){for(uint32_t i=0;i<Capacity;++i)if(entries[i].used&&entries[i].light.owner==owner){Unlink(i);entries[i]={};--count;return;}}
void Registry::Clear(){for(auto&e:entries)e={};heads.fill(-1);count=0;dropped=0;/* never reuse generation keys */}
Selection Registry::Select(uint64_t scene,Vec p,float radius,uint64_t preferred)const{
 Selection result;if(!scene||!Finite(p)||!std::isfinite(radius)||radius<0||radius>256||std::fabs(p.x)>100000||std::fabs(p.y)>100000)return result;
 int minX=int(std::floor((p.x-radius-32)/32)),maxX=int(std::floor((p.x+radius+32)/32)),minY=int(std::floor((p.y-radius-32)/32)),maxY=int(std::floor((p.y+radius+32)/32));
 for(int x=minX;x<=maxX;++x)for(int y=minY;y<=maxY;++y)for(int n=heads[Cell(x,y)];n>=0;n=entries[n].next){const auto&e=entries[n];if(e.light.scene!=scene)continue;float score=Score(e.light,p,radius);if(!(score>0))continue;
  // Keep a selected shadow light only while it has real influence, never insert an unrelated fake light.
  if(e.key==preferred)score*=1.15f;uint32_t at=result.count;if(at==4){if(score<=result.scores[3])continue;at=3;}else ++result.count;
  while(at&&score>result.scores[at-1]){result.indices[at]=result.indices[at-1];result.scores[at]=result.scores[at-1];--at;}result.indices[at]=uint32_t(n);result.scores[at]=score;
 }return result;
}
}
