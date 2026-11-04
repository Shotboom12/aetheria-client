#pragma once
#include "source/config.h"
#include "source/engine.h"
#include <array>
#include <cstdint>
namespace HD::Lumen {
struct Controls {
 bool lamps=false,fog=false,halos=false,ambiance=false,spells=false,shafts=false,memory=false,terrain=false;
 int quality=2,lampCount=32,lantern=0;bool tileLists=true,blur=true;
 float lampGain=1,haloGain=1,spellGain=1,density=1,shaftGain=1,temporal=.85f;
 // Preserved palette: these values belong to the player/native scene, not a foreign grade.
 std::array<float,3> lanternColour{1,.55f,.2f};
 void Clamp();
};
struct ControlSpec {const char*name;float initial,low,high;const char*label;};
const ControlSpec* ControlSpecs(size_t& count);
Source::Config SourceConfig(const Controls&,const Source::Config* numericAmbiance=nullptr);
enum ResetReason:uint32_t {First=1,Extent=2,Map=4,Clock=8,Camera=16,Mode=32,Light=64,Jitter=128};
struct HistoryKey {uint32_t width=0,height=0,mode=0;int map=0;double seconds=0;float position[3]{},direction[3]{0,0,1};uint32_t jitterConvention=0;};
class HistoryGuard {public:uint32_t Step(const HistoryKey&);void Reset(){valid_=false;}private:HistoryKey prev_{};bool valid_=false;};
struct DepthMapping {float atInfinity=0,perInverseDepth=0,deepest=1;bool reversed=false;};
// Native wxl projection is D3D 0..1. Upstream FrameInputs uses the equivalent GL -1..1 z mapping.
bool MapNativeProjection(const float* d3dProjection,float minZ,float maxZ,float* glProjection,DepthMapping&);
// Generic async CPU metadata ring: never waits and never consumes an unfinished or overwritten capture.
class ReadbackRing {
public:static constexpr unsigned Count=3;int Acquire(uint64_t completed);bool Submit(unsigned,uint64_t serial);int OldestReady(uint64_t completed)const;void Consume(unsigned);bool Empty()const;
private:std::array<uint64_t,Count> serials_{};std::array<bool,Count> reserved_{};
};
}
