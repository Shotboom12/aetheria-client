#pragma once
#include "source/effect_lights.h"
namespace HD::Lumen {
class SpellLights {
public:void Init(){Shutdown();}void Shutdown(){tracker_.Reset();pending_.clear();measured_=-1;}
 // The exact capture FrameInputs must travel with each asynchronous GPU readback.
 void Consume(const Source::EffectMeasurement&,const Source::LampCatalog&,bool suppressLampFlames=true);
 void Record(const Source::FrameInputs&);
 const std::vector<Source::EffectLamp>& Lights()const{return tracker_.Lights();}
 const Source::EffectClusterStats& Stats()const{return clusters_;}
private:Source::EffectTracker tracker_;Source::EffectClusterStats clusters_{};std::vector<Source::EffectCluster>pending_;double measured_=-1;int measuredMap_=-1;
};
}
