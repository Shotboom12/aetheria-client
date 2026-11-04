#include "LvControls.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace HD::Lumen {
namespace {float finite(float x,float fallback,float a,float b){return std::isfinite(x)?std::clamp(x,a,b):fallback;}}
void Controls::Clamp(){quality=std::clamp(quality,1,3);lampCount=std::clamp(lampCount,0,128);lantern=std::clamp(lantern,0,2);lampGain=finite(lampGain,1,0,4);haloGain=finite(haloGain,1,0,4);spellGain=finite(spellGain,1,0,4);density=finite(density,1,0,4);shaftGain=finite(shaftGain,1,0,4);temporal=finite(temporal,.85f,0,.95f);for(auto&c:lanternColour)c=finite(c,0,0,4);}
const ControlSpec*ControlSpecs(size_t&n){static const ControlSpec s[]={
 {"hdLvLamps",0,0,1,"Lampes : référence / candidat"},{"hdLvFog",0,0,1,"Brouillard : référence / candidat"},{"hdLvHalos",0,0,1,"Halos candidat"},{"hdLvAmbiance",0,0,1,"Profils numériques régionaux"},{"hdLvSpells",0,0,1,"Lumières de sorts"},{"hdLvShafts",0,0,1,"Rayons volumétriques"},{"hdLvMemory",0,0,1,"Mémoire des occluders"},{"hdLvTerrain",0,0,1,"Occlusion terrain"},
 {"hdLvQuality",2,1,3,"Qualité"},{"hdLvLampCount",32,0,128,"Budget lampes"},{"hdLvLampGain",1,0,4,"Intensité lampes"},{"hdLvHaloGain",1,0,4,"Intensité halos"},{"hdLvSpellGain",1,0,4,"Intensité sorts"},{"hdLvDensity",1,0,4,"Densité"},{"hdLvShaftGain",1,0,4,"Intensité rayons"},{"hdLvTemporal",.85f,0,.95f,"Stabilisation"},{"hdLvLantern",0,0,2,"Lanterne : arrêt / auto / marche"},{"hdLvTiles",1,0,1,"Listes par tuile"},{"hdLvBlur",1,0,1,"Flou bilatéral"}};n=sizeof s/sizeof*s;return s;}
Source::Config SourceConfig(const Controls& input,const Source::Config* numeric){auto c=input;c.Clamp();Source::Config s=numeric?*numeric:Source::Config{};
 s.enable=c.lamps||c.fog||c.halos||c.spells;s.hooks=false;s.overlay=false;s.quality=c.quality;s.lampCount=(c.lamps||c.halos)?c.lampCount:0;
 s.lampLight*=c.lampGain;s.lampFog*=c.haloGain;s.effectLight=c.spells?s.effectLight*c.spellGain:0;s.lanternMode=(c.lamps||c.halos)?c.lantern:0;s.corpseLight=0;
 s.density*=c.density;s.lightShafts=c.shafts;s.shafts*=c.shaftGain;s.shaftMemory=c.memory;s.terrainShadows=c.terrain;s.temporal=c.temporal;std::copy(c.lanternColour.begin(),c.lanternColour.end(),s.nativeLanternColour);
 // Do not import upstream exposure, white balance, day/night darkening, weather replacement or water/material overrides.
 s.exposure=s.classicExposure=s.saturation=s.skySaturation=s.hazeSaturation=s.lightSaturation=s.skyBrightness=s.landBrightness=1;
 s.temperature=s.tint=s.skyTemperature=s.skyTint=s.lightBalance=s.nightDarkness=0;s.lowSunLight=0;
 s.weatherTint=0;s.weatherSky=0;s.sunAndMoon=0;s.skyboxMoons=false;s.glowCompensation=false;s.relief=false;s.terrainMaterials=false;
 s.waterReflect=s.waterGlitter=s.waterWaves=s.waterRain=s.waterClarity=s.wetSurfaces=s.puddles=s.surfaceSheen=0;s.waterMemory=false;s.liquidDepth=false;s.underwater=false;s.farClipMax=0;
 return s;
}
uint32_t HistoryGuard::Step(const HistoryKey& k){uint32_t r=valid_?0:First;if(valid_){if(k.width!=prev_.width||k.height!=prev_.height)r|=Extent;if(k.map!=prev_.map)r|=Map;if(!std::isfinite(k.seconds)||k.seconds<prev_.seconds||k.seconds-prev_.seconds>.5)r|=Clock;if(k.mode!=prev_.mode)r|=Mode;if(k.jitterConvention!=prev_.jitterConvention)r|=Jitter;float d=0,dot=0;for(int i=0;i<3;++i){float a=k.position[i]-prev_.position[i];d+=a*a;dot+=k.direction[i]*prev_.direction[i];}if(!std::isfinite(d)||d>625)r|=Camera;if(!std::isfinite(dot)||dot<.999f)r|=Light;}prev_=k;valid_=true;return r;}
bool MapNativeProjection(const float*p,float lo,float hi,float*g,DepthMapping&m){if(!p||!g||!std::isfinite(lo)||!std::isfinite(hi)||lo<0||hi>1||hi<=lo)return false;for(int i=0;i<16;++i)if(!std::isfinite(p[i]))return false;
 // The upstream screen reconstruction assumes perspective, positive view Z, w=z, and no oblique near plane.
 if(std::abs(p[11]-1)>1e-5f||std::abs(p[15])>1e-5f||std::abs(p[2])>1e-6f||std::abs(p[6])>1e-6f||std::abs(p[0])<1e-6f||std::abs(p[5])<1e-6f)return false;
 std::copy(p,p+16,g);g[10]=2*p[10]-p[11];g[14]=2*p[14]-p[15];m.atInfinity=lo+(hi-lo)*p[10];m.perInverseDepth=(hi-lo)*p[14];m.reversed=p[14]>0;m.deepest=hi>=.99903f?.99903f:hi+.00001f;
 // Reverse depth needs the IsSky/comparison kernels adapted as a unit. Fail closed until that path exists.
 return !m.reversed&&p[14]<0;
}
int ReadbackRing::Acquire(uint64_t){for(unsigned i=0;i<Count;++i)if(!serials_[i]&&!reserved_[i]){reserved_[i]=true;return int(i);}return -1;}
bool ReadbackRing::Submit(unsigned i,uint64_t s){if(i>=Count||!reserved_[i]||!s)return false;reserved_[i]=false;serials_[i]=s;return true;}
int ReadbackRing::OldestReady(uint64_t done)const{int index=-1;uint64_t oldest=UINT64_MAX;for(unsigned i=0;i<Count;++i)if(serials_[i]&&serials_[i]<=done&&serials_[i]<oldest){oldest=serials_[i];index=int(i);}return index;}
void ReadbackRing::Consume(unsigned i){if(i<Count){serials_[i]=0;reserved_[i]=false;}}
bool ReadbackRing::Empty()const{for(unsigned i=0;i<Count;++i)if(serials_[i]||reserved_[i])return false;return true;}
}
