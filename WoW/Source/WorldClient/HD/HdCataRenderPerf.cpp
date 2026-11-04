// HD: main-thread CPU times, inclusive scopes; no Vulkan queries or presentation waits.
#include "HdCataRenderPerf.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Event/EvtPerf.h"
#include <string>
namespace HD::CataRenderPerf {
namespace {
constexpr size_t NS=size_t(Stage::Count),NC=size_t(Counter::Count);
struct Timing {uint64_t ns=0,calls=0,maximum=0;};
std::array<Timing,NS> timing{};std::array<uint64_t,NC> counters{};
bool enabled=false,initialized=false;uint64_t window=0,frames=0,lastFrame=0;CVar*info=nullptr;FILE*logFile=nullptr;
constexpr const char* names[NS]={"render","prepareMap","wmoPrepare","terrain","wmoDraw","horizon","horizonCull","sortCull","entities","detailDoodads","m2Animate","shadows","rootWait","groupWait",
 "frame","worldFrame","m2Opaque","m2Alpha","drawLists","hdOpaque","liquid","spells","postFx",
 "grassLights","grassConstants","grassDraw","grassFill",
 "cullVolumes","cullChunks","cullGroups","cullLiquid","cullEntitys","cullDoodads","cullOcclusion","cullHorizon","cullChunkDoodads","cullChunkPrep",
 "loopWait","loopUpdate","loopTimer","loopInput","loopPoll","loopQueue","loopIdle","loopPaint","present",
 "idleWorld","idleUi","luaGc","pollNet"};
static_assert(size_t(Stage::Present)-size_t(Stage::LoopWait)==EVTPERF_PRESENT,"Stage loop order must follow EvtPerfPhase");
constexpr const char* counterNames[NC]={"wmoDefs","wmoGroups","rootWaits","groupWaits","asyncReads","horizonAll","horizonCandidates","horizonFallbacks","horizonMismatch","grassChunks","grassDraws","grassFills","grassSingleModel","grassMixedModels","grassInsts","grassPackVertices","grassPackAcquires","grassPackFull","grassPackIndices","grassPackIndexFull","grassPackEvicts","grassFillSkipped","cullChunksTested","cullChunksKept","cullClipUpdates","cullDoodadsTested"};
// Spike recorder: per scheduler pass sums; a pass much longer than the usual one is written to
// Logs/CataRenderSpikes.log with every stage and counter of that pass (lines kept in memory, written every 2 s).
std::array<uint64_t,NS> passNs{};std::array<uint64_t,NC> passCounters{};uint64_t lastPass=0,enabledAt=0;double usualMs=0.0;
std::string spikes;unsigned spikeLines=0,spikeDropped=0;FILE*spikeFile=nullptr;
constexpr unsigned kSpikeLinesPerWindow=40;
uint64_t Now(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
void Reset(){timing={};counters={};frames=0;window=Now();}
void ResetPass(){passNs={};passCounters={};}
void CloseLog(){if(logFile){std::fclose(logFile);logFile=nullptr;}if(spikeFile){std::fclose(spikeFile);spikeFile=nullptr;}}
void WriteSpikes(){
 if(spikes.empty()&&!spikeDropped)return;
 if(!spikeFile)spikeFile=std::fopen("Logs/CataRenderSpikes.log","ab");
 if(spikeFile){
  std::fputs(spikes.c_str(),spikeFile);
  if(spikeDropped)std::fprintf(spikeFile,"(%u more spike(s) in this window not written)\r\n",spikeDropped);
  std::fflush(spikeFile);
 }
 spikes.clear();spikeLines=0;spikeDropped=0;
}
// Start of a main-thread scheduler pass: the previous pass (sleep included) is checked against the usual pass time.
void LoopStart(){
 if(!enabled){lastPass=0;return;}
 const uint64_t now=Now();
 if(lastPass){
  const double ms=(now-lastPass)/1e6;
  const bool sleeping=passNs[size_t(Stage::LoopWait)]>uint64_t(ms*0.5e6); // FPS cap or background: not a hitch
  if(usualMs>0.0&&ms>25.0&&ms>2.0*usualMs&&!sleeping){
   if(spikeLines<kSpikeLinesPerWindow){
    char line[256];
    std::snprintf(line,sizeof line,"t=%.3fs pass_ms=%.2f usual_ms=%.2f |",(lastPass-enabledAt)/1e9,ms,usualMs);
    spikes+=line;
    for(size_t i=0;i<NS;++i)if(passNs[i]>=200000){std::snprintf(line,sizeof line," %s=%.2f",names[i],passNs[i]/1e6);spikes+=line;}
    spikes+=" |";
    for(size_t i=0;i<NC;++i)if(passCounters[i]){std::snprintf(line,sizeof line," %s=%llu",counterNames[i],static_cast<unsigned long long>(passCounters[i]));spikes+=line;}
    spikes+="\r\n";++spikeLines;
   }else ++spikeDropped;
  }else usualMs=usualMs>0.0?usualMs*0.95+ms*0.05:ms; // spikes do not raise the usual time
 }
 lastPass=now;ResetPass();
}
uint64_t HookBegin(){return enabled?Now():0;}
void HookEnd(uint32_t phase,uint64_t start){if(phase<EVTPERF_COUNT)End(Stage(size_t(Stage::LoopWait)+phase),start);}
bool Toggle(CVar*,const char*,const char*s,void*){
 if(!s||!(std::strcmp(s,"0")==0||std::strcmp(s,"1")==0))return false;
 enabled=s[0]=='1';Reset();lastFrame=0;lastPass=0;usualMs=0.0;enabledAt=Now();ResetPass();
 if(!enabled)WriteSpikes();
 if(!enabled){CloseLog();if(info)info->Set("off",true,false,false,false);}return true;
}
double Ms(Stage s,double f){return timing[size_t(s)].ns/1e6/f;}
}
void Initialize(){
 if(initialized)return;
 CVar*toggle=CVar::Register("hdCataRenderPerf","HD: CPU stages; Logs/CataRenderPerf.log every 2s, no FPS limit changes",1,"0",Toggle,CATEGORY_GRAPHICS,false,nullptr,false);
 info=CVar::Register("hdCataRenderPerfInfo","HD: last CPU measurements (milliseconds per scene render)",0,"off",nullptr,CATEGORY_GRAPHICS,false,nullptr,false); if (info) info->m_flags &= ~0x1u; /* status only: not archived to Config.wtf */
 initialized=true;enabled=toggle&&toggle->m_intValue!=0;Reset();enabledAt=Now();
 g_evtPerf={HookBegin,HookEnd,LoopStart};
}
void Shutdown(){g_evtPerf={};WriteSpikes();enabled=false;initialized=false;CloseLog();info=nullptr;Reset();}
bool Enabled(){return enabled;}
uint64_t Begin(){return enabled?Now():0;}
void End(Stage stage,uint64_t start){
 if(!enabled||!start||size_t(stage)>=NS)return;
 const auto elapsed=Now()-start;auto&t=timing[size_t(stage)];t.ns+=elapsed;++t.calls;if(elapsed>t.maximum)t.maximum=elapsed;
 passNs[size_t(stage)]+=elapsed;
}
void Add(Counter c,uint64_t n){if(enabled&&size_t(c)<NC){counters[size_t(c)]+=n;passCounters[size_t(c)]+=n;}}
// Wall time from one scene render to the next: everything the client does for one image, present and caps included.
void FrameStart(){
 if(!enabled){lastFrame=0;return;}
 const uint64_t now=Now();if(lastFrame)End(Stage::Frame,lastFrame);lastFrame=now;
}
void FrameEnd(){
 if(!enabled)return;++frames;const uint64_t now=Now();if(now-window<2000000000ull)return;
 const double f=frames?double(frames):1.0;char text[768];
 std::snprintf(text,sizeof text,"image %.2fms; world %.2f (scene %.2f, M2 %.2f+%.2f, lists %.2f, grass %.2f, HD %.2f, liquid %.2f, spells %.2f, post %.2f); outside world %.2f; grass: lights %.2f, consts %.2f, draw %.2f, fill %.2f, %.0f chunks %.0f draws %.1f fills; frames %llu",
 Ms(Stage::Frame,f),Ms(Stage::WorldFrame,f),Ms(Stage::Render,f),Ms(Stage::M2Opaque,f),Ms(Stage::M2Alpha,f),Ms(Stage::DrawLists,f),Ms(Stage::DetailDoodads,f),
 Ms(Stage::HdOpaque,f),Ms(Stage::Liquid,f),Ms(Stage::Spells,f),Ms(Stage::PostFx,f),Ms(Stage::Frame,f)-Ms(Stage::WorldFrame,f),
 Ms(Stage::GrassLights,f),Ms(Stage::GrassConstants,f),Ms(Stage::GrassDraw,f),Ms(Stage::GrassFill,f),
 counters[size_t(Counter::GrassChunks)]/f,counters[size_t(Counter::GrassDraws)]/f,counters[size_t(Counter::GrassFills)]/f,static_cast<unsigned long long>(frames));
 if(info)info->Set(text,true,false,false,false);
 if(!logFile)logFile=std::fopen("Logs/CataRenderPerf.log","ab");
 if(logFile){
  std::fprintf(logFile,"window_ms=%.3f scenes=%llu; inclusive CPU scopes, render excludes present/cap\r\n",(now-window)/1e6,static_cast<unsigned long long>(frames));
  for(size_t i=0;i<NS;++i)std::fprintf(logFile,"%s ms/scene=%.5f max_call_ms=%.5f calls=%llu\r\n",names[i],timing[i].ns/1e6/f,timing[i].maximum/1e6,static_cast<unsigned long long>(timing[i].calls));
  for(size_t i=0;i<NC;++i)std::fprintf(logFile,"%s=%llu ",counterNames[i],static_cast<unsigned long long>(counters[i]));
  std::fputs("\r\n",logFile);std::fflush(logFile);
 }
 WriteSpikes();
 Reset();
}
}
