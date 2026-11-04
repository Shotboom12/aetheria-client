#pragma once
#include "LvSuite.h"
namespace DayNight {struct DNInfo;struct DNPlanets;}
namespace HD {struct ForeverFog;}
namespace HD::Lumen {
// Typed client bridge: no offsets, no process memory patch, no writes to weather/time/old CVars.
void EnvironmentFromDayNight(const DayNight::DNInfo&,double monotonicSeconds,int map,int zone,const float toLight[3],Source::WeatherKind,NativeFrame&);
void EnvironmentFromPlanets(const DayNight::DNInfo&,const DayNight::DNPlanets&,float nativeDayWeight,double monotonicSeconds,int map,int zone,Source::WeatherKind,NativeFrame&);
void AdaptForeverFog(const HD::ForeverFog&,float sourceWeight,const float player[3],NativeFrame&,float density=1,float sun=1,float ambient=1);
// Match only a UNIQUE emitter position within tolerance; ambiguous/not found is a failure, never a guessed colour.
bool MatchCatalogueEmitter(const Source::LampCatalog&,int map,const float emitter[3],uint32_t& identity,float tolerance=.05f);
}
