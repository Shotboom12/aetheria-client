#include "LvLamps.h"
#include <algorithm>
namespace HD::Lumen {
bool Lamps::Init(const std::string&p){bool ok=p.empty()||catalog_.Load(p);if(ok)selection_.Reserve(catalog_.LampTotal(),catalog_.WindowTotal());return ok;}
bool Lamps::Resize(const Source::FrameInputs&f){if(!f.viewport.Width||!f.viewport.Height)return false;Source::LampTable empty{};tiles_.Build(empty,f.glProjection,f.viewport,0);return true;}
const Source::LampTable&Lamps::Record(const Source::FrameInputs&f,const Controls&c,const Source::Config&cfg,const Source::FogParams&p,const std::vector<Source::EffectLamp>*effects){auto&lantern=lantern_.Update(f,cfg,c.lamps?p.lanternLight:0);auto&table=selection_.Update(catalog_,f,cfg,c.halos?p.lampHaze:0,c.lamps?p.lampLight:0,c.lamps?p.windowLight:0,p.lanternGlow,&lantern,c.lamps?p.lanternLight:0,effects,c.spells?p.effectLight:0,c.halos?p.farLightCore:0);tiles_.Build(table,f.glProjection,f.viewport,Source::FarCoreAnglePerTexel(f,std::max(1,int(f.viewport.Height/2))));return table;}
void Lamps::Shutdown(){catalog_.Clear();selection_=Source::LampSelection{};tiles_.Empty();lantern_=Source::BeltLantern{};}
}
