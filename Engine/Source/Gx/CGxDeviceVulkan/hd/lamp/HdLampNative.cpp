// SPDX-License-Identifier: GPL-3.0-or-later
#include "HdLampCore.h"
#include "HdLampHooks.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Types.h"
#include "WoW/Source/Ui/Visuals.h"
#include <cstring>
#include <cstdio>
#include <windows.h>
namespace HD::Lamp::Native {
namespace {
Registry registry;std::array<CM2Light,Registry::Capacity>proxy;
CVar *repair=nullptr,*producer=nullptr,*info=nullptr,*probe=nullptr;bool initialized=false;uint64_t frameSerial=0,chosenKey=0;Sample frozen;uint32_t drawSlots=0;bool safe=true,locked=false,producerReady=false;bool pixelDraw=false;uint64_t modelOwner=0;
uint64_t repairs=0,empty=0,blocks=0,slots=0,actual=0,unused=0,unknown=0;char missing[33]{};ULONGLONG last=0;
struct AuditEntry{const char*hash;uint32_t mask,kind;};
#include "LampShaderAudit.inc"
const AuditEntry*FindAudit(const char*hash){if(!hash)return nullptr;auto p=std::lower_bound(std::begin(LampShaderAudit),std::end(LampShaderAudit),hash,[](const AuditEntry&e,const char*h){return std::strcmp(e.hash,h)<0;});return p!=std::end(LampShaderAudit)&&!std::strcmp(p->hash,hash)?p:nullptr;}
CVar*Var(const char*n,const char*h){if(auto*c=CVar::LookupRegistered(n))return c;return CVar::Register(n,h,0,"0",nullptr,CATEGORY_GRAPHICS,false,nullptr,false);}
// Aetheria: the lamp hooks act only in Azeroth Remastered mode; Stock and standalone Lumenveil never see them.
bool Vulkan(){return initialized&&Visuals::Current()==Visuals::AzerothRemastered&&g_theGxDevicePtr&&g_theGxDevicePtr->DeviceApi()==GxApi_Vulkan;}
uint64_t Key(const CM2Light*l){uintptr_t p=reinterpret_cast<uintptr_t>(l),first=reinterpret_cast<uintptr_t>(proxy.data());
 if(p>=first&&p<first+sizeof proxy&&(p-first)%sizeof(CM2Light)==0){const auto&e=registry.At(uint32_t((p-first)/sizeof(CM2Light)));return e.used?e.key:0;}
 auto*e=registry.FindOwner(p);return e?e->key:0;
}
}
void Init(){if(initialized)return;repair=Var("hdModelLampRepair","HD: bounded real lamp selection for M2, before shader permutation; 0 off");producer=Var("hdLampMaterialDelta","HD: isolate the selected real lamp in material shaders; 0 off");info=Var("hdLampInfo","HD: selected lamps and actual vertex shader consumption");probe=Var("hdLampProbe","HD: explicit test light pinned 4 yards ahead of the camera; 0 off, 1 debug only");initialized=true;}
bool Enabled(){return Vulkan()&&repair&&repair->m_intValue==1;}
bool ProducerEnabled(){auto*sun=CVar::LookupRegistered("hdSunDebug");return producerReady&&Vulkan()&&producer&&producer->m_intValue==1&&(!sun||sun->m_intValue==0);}
void Track(CM2Light*l,uint64_t model){if(!Enabled()&&!ProducerEnabled())return;if(!l||l->m_type!=M2LIGHT_POINT||!l->m_scene)return;Sample s;s.owner=reinterpret_cast<uintptr_t>(l);s.model=model;s.scene=reinterpret_cast<uintptr_t>(l->m_scene);s.position={l->m_pos.x,l->m_pos.y,l->m_pos.z};s.color={l->m_dirColor.x,l->m_dirColor.y,l->m_dirColor.z};s.constant=l->m_constantAttenuation;s.linear=l->m_linearAttenuation;s.quadratic=l->m_quadraticAttenuation;s.visible=l->m_visible!=0;s.stamp=l->m_stamp;registry.Track(s);}
void Forget(CM2Light*l){if(!registry.Count())return;uint64_t key=Key(l);registry.Forget(reinterpret_cast<uintptr_t>(l));if(key&&key==chosenKey){chosenKey=0;frozen={};}}
void ClearScene(){registry.Clear();chosenKey=0;frozen={};frameSerial=0;drawSlots=0;modelOwner=0;pixelDraw=false;}
void Shutdown(){initialized=false;ClearScene();}
void BeginFrame(uint64_t serial,uint64_t scene,float x,float y,float z){if(serial==frameSerial)return;frameSerial=serial;drawSlots=0;safe=true;locked=false;modelOwner=0;pixelDraw=false;
 static bool probeActive=false;if(probeActive){auto*e=registry.FindOwner(1);if(!e)probeActive=false;else if(e->light.scene!=scene){registry.Forget(1);probeActive=false;}}
 if(Enabled()&&probe&&probe->m_intValue==1&&!probeActive&&scene){auto*s=reinterpret_cast<CM2Scene*>(scene);Sample test;test.owner=1;test.scene=scene;test.visible=true;test.position={x+s->m_viewInv.c0*4,y+s->m_viewInv.c1*4,z+s->m_viewInv.c2*4+1};test.color={1.5f,.3f,.1f};test.constant=1;test.linear=.1f;test.quadratic=.01f;registry.Track(test);probeActive=true;}
 if(probeActive&&(!Enabled()||!probe||probe->m_intValue!=1)){registry.Forget(1);probeActive=false;}
 auto pick=registry.Select(scene,{x,y,z},0,chosenKey);if(!pick.count){chosenKey=0;frozen={};return;}const Entry*best=&registry.At(pick.indices[0]);
 for(uint32_t i=0;i<pick.count;++i){const auto&e=registry.At(pick.indices[i]);if(e.key==chosenKey&&pick.scores[i]>=pick.scores[0]*.85f){best=&e;break;}}
 chosenKey=best->key;frozen=best->light;
}
uint64_t FrameLight(float xyz[3],float&radius){if(!chosenKey||!registry.FindKey(chosenKey))return 0;xyz[0]=frozen.position.x;xyz[1]=frozen.position.y;xyz[2]=frozen.position.z;auto*c=CVar::LookupRegistered("hdLocalShadowRange");radius=c&&std::isfinite(c->m_floatValue)?std::clamp(c->m_floatValue,2.0f,24.0f):12;return chosenKey;}
void Repair(CM2Lighting*l){if(!Enabled()||!l||!l->m_scene)return;++repairs;const auto&s=l->m_sphere4;auto selected=registry.Select(reinterpret_cast<uintptr_t>(l->m_scene),{s.c.x,s.c.y,s.c.z},s.r,chosenKey);
 // No replacement if no tracked source is available. An empty native selection remains visible in diagnostics.
 if(!selected.count){++empty;return;}l->m_lightCount=selected.count;
 for(uint32_t i=0;i<selected.count;++i){uint32_t index=selected.indices[i];const auto&e=registry.At(index);auto&p=proxy[index];const auto&a=e.light;
  p.m_pos=C3Vector(a.position.x,a.position.y,a.position.z);p.m_dirColor=C3Vector(a.color.x,a.color.y,a.color.z);p.m_constantAttenuation=a.constant;p.m_linearAttenuation=a.linear;p.m_quadraticAttenuation=a.quadratic;p.m_type=M2LIGHT_POINT;p.m_visible=1;p.m_scene=nullptr;
  p.m_viewPos=p.m_pos*l->m_scene->m_view;l->m_lights[i]=&p;l->m_lightDist[i]=Dist2(a.position,{s.c.x,s.c.y,s.c.z});
 }l->m_flags&=~1u; // CameraSpace must update these proxies for the current actual view.
}
uint32_t SlotMask(const CM2Lighting*l,bool enabled){uint32_t mask=0;if(!enabled||!l||!chosenKey)return 0;
 if(!locked){if(const auto*e=registry.FindKey(chosenKey))frozen=e->light;locked=true;}for(uint32_t i=0;i<std::min(l->m_lightCount,4u);++i)if(l->m_lights[i]&&Key(l->m_lights[i])==chosenKey&&l->m_lights[i]->m_pos.x==frozen.position.x&&l->m_lights[i]->m_pos.y==frozen.position.y&&l->m_lights[i]->m_pos.z==frozen.position.z)mask|=1u<<i;return mask;}
void ModelOwner(uint64_t model){modelOwner=model;}
void SnapshotDraw(uint32_t mask){modelOwner=0;pixelDraw=false;drawSlots=ProducerEnabled()?mask&15u:0;}
void SnapshotPixelDraw(uint32_t mask){SnapshotDraw(mask);pixelDraw=true;}
bool IsChosenOwner(uint64_t owner,float x,float y,float z){const auto*e=registry.FindOwner(owner);if(!e||e->key!=chosenKey)return false;if(!locked){frozen=e->light;locked=true;}return frozen.position.x==x&&frozen.position.y==y&&frozen.position.z==z;}
uint32_t DrawSlots(){return ProducerEnabled()?drawSlots:0;}
void Trace(const char*hash,const float rows[][4]){if((!Enabled()&&!ProducerEnabled())||!hash||!rows)return;const AuditEntry*found=FindAudit(hash);if(!found||found->kind==2)return;++blocks;uint32_t active=0;for(uint32_t i=0;i<4;++i)if(rows[17+i][3]>0&&(rows[17+i][0]!=0||rows[17+i][1]!=0||rows[17+i][2]!=0))active|=1u<<i;slots+=active!=0;actual+=(active&found->mask)!=0;unused+=active&&!(active&found->mask);}
void SetProducerReady(bool ready){producerReady=ready;}
void UnsafeDepthDraw(){safe=false;}
bool FrameSafe(){return safe;}
float Metadata(float native,const char*hash,const float rows[][4]){Trace(hash,rows);if(!ProducerEnabled())return Encode(native,0,false);auto*found=FindAudit(hash);bool supported=found&&(found->kind!=2||pixelDraw);uint32_t mask=supported?DrawSlots():0;return Encode(native,mask,ProducerEnabled()&&supported)+(supported&&pixelDraw?float(PixelBit):0)+(supported&&modelOwner&&frozen.model==modelOwner?float(EmitterBit):0);}
void Tick(){if(!Vulkan()||GetTickCount64()-last<500)return;last=GetTickCount64();char s[480];const auto*sun=CVar::LookupRegistered("hdSunDebug");const char*state=producer&&producer->m_intValue==1?(!producerReady?"backend/shaders missing":sun&&sun->m_intValue!=0?"sun debug overlay active":"ready"):"off";std::snprintf(s,sizeof s,"producer %s; tracked %u, repair %llu (empty %llu), model/WMO draws %llu, RGB slots %llu, shader reads %llu, ignored %llu, unknown %llu [%s], shadow key %llu, overflow %llu",state,registry.Count(),repairs,empty,blocks,slots,actual,unused,unknown,missing,chosenKey,registry.Dropped());if(info)info->Set(s,true,false,false,false);repairs=empty=blocks=slots=actual=unused=unknown=0;}
}
