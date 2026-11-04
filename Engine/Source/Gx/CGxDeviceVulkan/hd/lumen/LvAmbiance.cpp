#include "LvAmbiance.h"
namespace HD::Lumen {
bool Ambiance::Init(const std::string&t){Shutdown();return t.empty()||Source::ParseRegionBook(t,book_,problems_);}
Source::Config Ambiance::Record(const Source::FrameInputs&f,const Controls&c,const Source::Config&base){if(!c.ambiance)return SourceConfig(c,&base);auto weights=Source::SlotWeightsAt(f.dayFraction,f.weather,f.weatherIntensity);auto layer=Source::RegionLayer(book_,f.zoneId,weights);auto blended=blender_.Update(f.zoneId,layer,f.seconds);auto numeric=Source::ComposeLook(base,blended);return SourceConfig(c,&numeric);}
void Ambiance::Shutdown(){book_={};blender_.Reset();problems_.clear();}
}
