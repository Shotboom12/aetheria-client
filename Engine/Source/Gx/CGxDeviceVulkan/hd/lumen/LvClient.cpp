#include "LvClient.h"
#ifdef LV_CLIENT_TEST_CVARS
#include "ClientCvarShim.h"
#else
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#endif
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <chrono>
namespace HD::Lumen::Client {
namespace {
Suite suite;const WXL_VkRenderApi*api=nullptr;void*stages[4]{};bool gbuffer=false,opaqueDepth=false;bool frameWanted=false;
struct Advanced {int index;CVar*var;};std::vector<Advanced>advanced;
CVar*Register(const char*n,const char*h,const char*v,bool save=true){if(auto*found=CVar::LookupRegistered(n))return found;return CVar::Register(n,h,save?1:0,v,nullptr,CATEGORY_GRAPHICS,false,nullptr,false);}
void StatusValue(const char*n,const char*v){auto*c=CVar::LookupRegistered(n);if(!c)c=Register(n,"Lumen native status",v,false);if(c&&(!c->m_stringValue.m_str||std::strcmp(c->m_stringValue.m_str,v))){c->SetReadOnly(false);c->InternalSet(v,true,false,false,false);}if(c)c->SetReadOnly(true);}
float Value(const char*n,float fallback){auto*c=CVar::LookupRegistered(n);return c&&std::isfinite(c->m_floatValue)?c->m_floatValue:fallback;}
void Publish(){const auto&a=suite.Available();StatusValue("hdLvReady",a.backend?"1":"0");struct P{const char*n;bool b;};P p[]={{"Lamps",a.lamps},{"Fog",a.fog},{"Halos",a.halos},{"Ambiance",a.ambiance},{"Spells",a.spells},{"Shafts",a.shafts},{"Memory",a.memory},{"Terrain",a.terrain},{"Blur",a.blur}};for(auto&v:p){char key[64];std::snprintf(key,sizeof key,"hdLv%sReady",v.n);StatusValue(key,v.b?"1":"0");}StatusValue("hdLvInfo",suite.Status());{const auto&s=suite.SpellStats();char t[240];std::snprintf(t,sizeof t,"lumieres de sorts %zu, groupes %d, cellules %d, sans couleur %d, ecartes meteo %d, pluie %d, sprites %d",suite.SpellLightCount(),s.clusters,s.cells,s.colourless,s.suppressedCells,s.fallingDropped,s.sprites);StatusValue("hdLvSpellInfo",t);
 // HD L9: spell light diagnostic written to Logs\\lumen-sorts.txt (only while spells are asked), throttled.
 static double lastLog=-100;static int lines=0;static unsigned lastMeasured=0;
 const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
 const bool active=suite.SpellLightCount()>0||s.clusters>0||s.cells>0;
 if(Value("hdLvSpells",0)!=0&&lines<4000&&(now-lastLog>(active?0.2:5.0))){
  if(std::FILE*f=std::fopen("Logs\\lumen-sorts.txt","a")){
   std::fprintf(f,"t %.1f etat \"%s\" mesures %u (+%u) sautees %u lumieres %zu groupes %d cellules %d sans_couleur %d flammes %d pluie %d sprites %d seuil %.2f fond %.2f anneau %d\n",now,suite.Status(),suite.SpellMeasurements(),suite.SpellMeasurements()-lastMeasured,suite.SpellSkipped(),suite.SpellLightCount(),s.clusters,s.cells,s.colourless,s.suppressedCells,s.fallingDropped,s.sprites,s.least,s.background,s.ringCells);
   for(const auto&e:suite.SpellLamps())std::fprintf(f,"   sort pos %.2f %.2f %.2f couleur %.2f %.2f %.2f intensite %.2f portee %.1f\n",e.position[0],e.position[1],e.position[2],e.colour[0],e.colour[1],e.colour[2],e.intensity,e.surfaceReach);
   std::fclose(f);++lines;lastLog=now;lastMeasured=suite.SpellMeasurements();}}
 // HD L17: read-only fog journal, Logs\\lumen-brume.txt, once a second while hdLvFog is on (hdLvFogLog 0 stops it).
 // It changes nothing on screen: it writes what the fog received (For Ever layers or the game's own fog) and the sun light it ends with.
 {static double lastFog=-100;static int fogLines=0;static CVar*fogLog=Register("hdLvFogLog","Lumen: fog journal Logs\\lumen-brume.txt, 1 on / 0 off","1");(void)fogLog;
  if(Value("hdLvFog",0)!=0&&Value("hdLvFogLog",1)!=0&&fogLines<6000&&now-lastFog>=1.0){
   if(std::FILE*f=std::fopen("Logs\\lumen-brume.txt","a")){
    const auto&n=suite.NativeUsed();const auto&in=suite.FrameUsed();const auto&p=suite.FogUsed();const auto&cfg=suite.ConfigUsed();
    auto lum=[](const float*c){return .2126f*c[0]+.7152f*c[1]+.0722f*c[2];};
    float sun=0,amb=0;
    for(int i=0;i<Source::kFogLayers;++i){const auto&l=p.layers[i];float path=std::max(0.f,150.f-l.start);if(l.endDistance<l.start)path=0;
     float h=std::clamp(std::exp((l.upperHeight-p.referenceZ)*l.upperFalloff),0.f,1.f)*std::clamp(std::exp((p.referenceZ-l.lowerHeight)*l.lowerFalloff),0.f,1.f);
     float op=1.f-std::exp(-std::max(l.density,0.f)*h*path);if(!std::isfinite(op))op=0;sun+=lum(l.diffuse)*op;amb+=lum(l.emissive)*op;}
    const char*place=n.journalPlace[0]?n.journalPlace:"(lieu inconnu)";
    std::fprintf(f,"t %.1f heure %.2f %s | source %s | soleil visible %.2f lune %d | reglages SunScatter %.2f ViewLight %.2f AntiGlare %.2f ScatterNuit %.2f Aube %.2f Jour %.2f Soir %.2f | LUMIERE SOLEIL PRES %.4f AMBIANTE PRES %.4f\n",
     now,24.0*(in.dayFraction-std::floor(in.dayFraction)),place,n.hasAuthored?"ForEver":"jeu 3.3.5 (pas de donnees ForEver)",p.lightVisibility,in.lightIsMoon?1:0,
     cfg.sunScatter,cfg.viewLight,cfg.antiGlare,cfg.scatterNight,cfg.scatterDawn,cfg.scatterDay,cfg.scatterDusk,sun,amb);
    if(n.hasAuthored)for(int i=0;i<n.authored.layerCount;++i){const auto&a=n.authored.layers[i];
     std::fprintf(f,"   ForEver recu couche %d debut %.0f densite %.5f intensite %.2f g %.2f couleur soleil %.2f %.2f %.2f\n",i,a.start,a.density,a.intensity,a.g,a.diffuse[0],a.diffuse[1],a.diffuse[2]);}
    for(int i=0;i<Source::kFogLayers;++i){const auto&l=p.layers[i];
     std::fprintf(f,"   final couche %d debut %.0f densite %.6f g %.2f soleil %.4f %.4f %.4f ambiant %.4f %.4f %.4f\n",i,l.start,l.density,l.g,l.diffuse[0],l.diffuse[1],l.diffuse[2],l.emissive[0],l.emissive[1],l.emissive[2]);}
    std::fclose(f);++fogLines;lastFog=now;}}}
 }}
bool SafeSetting(const char*key){std::string n=key;std::transform(n.begin(),n.end(),n.begin(),[](unsigned char c){return char(std::tolower(c));});const char*exact[]={"quality","lampcount","temporal","lanternmode","lantern","shaftmemory","lightshafts","terrainshadows","hazesaturation"};for(auto*x:exact)if(n==x)return false;const char*prefix[]={"lamp","lantern","windowlight","effectlight","firestrength","shaft","moonshaft","lowsunshafts","farlights","density","fogceiling","haze","groundfog","farfog","aerialperspective","sunscatter","ambient","drift","bank","wind","stormwind","morningmist","mistheight","eveninghaze","weatherfog","weathershafts","cloudshadow","cloudcover","cloudsize","twilight","dusthaze","humidhaze","horizonfog",/* HD L18: L14/L15 settings were never registered */"viewlight","antiglare","scatternight","scatterdawn","scatterday","scatterdusk"};for(auto*x:prefix)if(n.compare(0,std::strlen(x),x)==0)return true;return false;}
Controls Read(){Controls c;c.lamps=Value("hdLvLamps",0)!=0;c.fog=Value("hdLvFog",0)!=0;c.halos=Value("hdLvHalos",0)!=0;c.ambiance=Value("hdLvAmbiance",0)!=0;c.spells=Value("hdLvSpells",0)!=0;c.shafts=Value("hdLvShafts",0)!=0;c.memory=Value("hdLvMemory",0)!=0;c.terrain=Value("hdLvTerrain",0)!=0;c.quality=int(std::clamp(Value("hdLvQuality",2),1.f,3.f));c.lampCount=int(std::clamp(Value("hdLvLampCount",32),0.f,128.f));c.lantern=int(std::clamp(Value("hdLvLantern",0),0.f,2.f));c.tileLists=Value("hdLvTiles",1)!=0;c.blur=Value("hdLvBlur",1)!=0;c.lampGain=Value("hdLvLampGain",1);c.haloGain=Value("hdLvHaloGain",1);c.spellGain=Value("hdLvSpellGain",1);c.density=Value("hdLvDensity",1);c.shaftGain=Value("hdLvShaftGain",1);c.temporal=Value("hdLvTemporal",.85f);c.lanternColour={Value("hdLvLanternR",1),Value("hdLvLanternG",.55f),Value("hdLvLanternB",.2f)};c.Clamp();return c;}
int __cdecl Wanted(void*){return frameWanted?1:0;}
void __cdecl Run(void*,const WXL_VkStageContext*ctx){if(ctx){suite.Record(*ctx);Publish();}}
void __cdecl Finish(void*,const WXL_VkStageContext*ctx){if(ctx){suite.AfterReferenceFog(*ctx);Publish();}}
}
static char failure[200]="";
bool Startup(const WXL_VkRenderApi*a,const char*path,const char*regions){if(api)return api==a;if(!a||a->structSize<offsetof(WXL_VkRenderApi,UnregisterStage)+sizeof(a->UnregisterStage)||!a->RegisterStage||!a->UnregisterStage)return false;api=a;
 size_t count=0;auto*spec=ControlSpecs(count);for(size_t i=0;i<count;++i){char value[32];std::snprintf(value,sizeof value,"%.9g",spec[i].initial);Register(spec[i].name,spec[i].label,value);}Register("hdLvLanternR","Lantern red","1");Register("hdLvLanternG","Lantern green",".55");Register("hdLvLanternB","Lantern blue",".2");
 Source::Config defaults;for(int i=0;i<Source::LiveSettingCount();++i){auto&s=Source::LiveSettingAt(i);if(!SafeSetting(s.key))continue;std::string name="hdLvCfg_"+std::string(s.key);char value[32];std::snprintf(value,sizeof value,"%.9g",Source::LiveSettingValue(defaults,i));if(auto*v=Register(name.c_str(),s.key,value))advanced.push_back({i,v});}
 if(!suite.Init(a,path,regions)){std::snprintf(failure,sizeof failure,"%s",suite.Status());Publish();Shutdown(0);return false;}const uint32_t points[]={0,5,5,7};for(unsigned i=0;i<4;++i){WXL_VkStageDesc d{};d.structSize=sizeof d;d.point=points[i];d.order=i==2?110:90;d.flags=WXL_VK_STAGE_NEEDS_DEPTH|WXL_VK_STAGE_WRITES_COLOR;d.Wanted=Wanted;d.Run=i==2?Finish:Run;stages[i]=a->RegisterStage(&d,"HD.Lumen");if(!stages[i]){Shutdown(0);return false;}}
 Publish();return true;
}
bool Prepare(NativeFrame frame,VkExtent2D extent,VkRect2D world,uint64_t done){if(!api)return false;auto controls=Read();bool needGbuffer=controls.lamps||controls.spells;bool needDepth=controls.fog&&controls.shafts&&!frame.environment.inLiquid;
 if(api->structSize>=offsetof(WXL_VkRenderApi,GetGBuffer)+sizeof(api->GetGBuffer)&&api->RequestGBuffer&&gbuffer!=needGbuffer){api->RequestGBuffer(needGbuffer?1:0);gbuffer=needGbuffer;}
 if(api->structSize>=offsetof(WXL_VkRenderApi,GetOpaqueDepth)+sizeof(api->GetOpaqueDepth)&&api->RequestOpaqueDepth&&opaqueDepth!=needDepth){api->RequestOpaqueDepth(needDepth?1:0);opaqueDepth=needDepth;}
 frameWanted=controls.lamps||controls.fog||controls.halos||controls.spells;for(auto&v:advanced)if(std::isfinite(v.var->m_floatValue))Source::SetLiveSettingValue(frame.numericBase,v.index,v.var->m_floatValue);bool result=suite.Prepare(frame,controls,extent,world,done);if(!result)frameWanted=false;Publish();return result;}
bool Shutdown(uint64_t done){if(!api)return true;if(suite.LastUsed()>done)return false;frameWanted=false;for(auto&s:stages)if(s){api->UnregisterStage(s);s=nullptr;}if(!suite.Shutdown(done))return false;if(gbuffer&&api->RequestGBuffer)api->RequestGBuffer(0);gbuffer=false;if(opaqueDepth&&api->RequestOpaqueDepth)api->RequestOpaqueDepth(0);opaqueDepth=false;advanced.clear();api=nullptr;Publish();return true;}
bool NeedsAfterSpells(){return frameWanted&&suite.NeedsAfterSpells();}bool NeedsBeforeSpells(){return frameWanted&&suite.NeedsBeforeSpells();}bool ComposedFog(uint64_t s){return suite.ComposedFog(s);}bool ComposedShafts(uint64_t s){return suite.ComposedShafts(s);}const char*Status(){return suite.Status();}void Stop(){frameWanted=false;}uint64_t LastUsed(){return suite.LastUsed();}bool SafeAdvancedSetting(const char*k){return k&&SafeSetting(k);}
const char* FailureReason(){return failure;}
const Source::LampCatalog* Catalogue(){return api?&suite.Catalogue():nullptr;} // HD 2026-11-02 lot L2
}
