#include "LvFog.h"
#include <algorithm>
namespace HD::Lumen {
Source::FogParams Fog::Record(const Source::FrameInputs&f,const Controls&c,const Source::Config&cfg,const Source::AuthoredFog*authored,const Source::HeightSource*height){HistoryKey key{};key.width=f.viewport.Width;key.height=f.viewport.Height;key.map=f.mapId;key.seconds=f.seconds;std::copy(f.camPos,f.camPos+3,key.position);std::copy(f.toLight,f.toLight+3,key.direction);key.mode=unsigned(c.fog)|(unsigned(c.shafts)<<1)|(unsigned(c.memory)<<2)|(unsigned(c.terrain)<<3)|(unsigned(c.quality)<<4);reset_=guard_.Step(key);if(reset_&(Map|Camera|Clock))terrain_.Forget();
 if(c.terrain&&height&&height->probe)terrain_.Update(f.camPos,f.mapId,f.toLight,*height,{nullptr,nullptr},true);
 auto fog=Source::BuildFogParams(f,cfg,authored);return fog;
}
}
