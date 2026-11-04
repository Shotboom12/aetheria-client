#include "LvSpellLights.h"
namespace HD::Lumen {
void SpellLights::Consume(const Source::EffectMeasurement&m,const Source::LampCatalog&cat,bool suppress){if(m.frame.seconds<measured_)return;pending_=Source::ClusterEffects(m,suppress?cat.Map(m.frame.mapId):nullptr,&clusters_);measured_=m.frame.seconds;measuredMap_=m.frame.mapId;}
void SpellLights::Record(const Source::FrameInputs&f){bool valid=measured_>=0&&measuredMap_==f.mapId&&measured_<=f.seconds&&f.seconds-measured_<=Source::kEffectStaleSeconds;tracker_.Update(valid?&pending_:nullptr,f.camPos,f.seconds,f.mapId,valid?measured_:-1,f.units,f.unitCount);pending_.clear();measured_=-1;}
}
