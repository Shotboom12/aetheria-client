// HD: main-thread CPU times, inclusive scopes; no Vulkan queries or presentation waits.
#include "HdCataRenderPerf.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
namespace HD::CataRenderPerf {
namespace {
constexpr size_t NS=size_t(Stage::Count),NC=size_t(Counter::Count);
struct Timing {uint64_t ns=0,calls=0,maximum=0;};
std::array<Timing,NS> timing{};std::array<uint64_t,NC> counters{};
bool enabled=false,initialized=false;uint64_t window=0,frames=0,lastFrame=0;CVar*info=nullptr;FILE*logFile=nullptr;
constexpr const char* names[NS]={"render","prepareMap","wmoPrepare","terrain","wmoDraw","horizon","horizonCull","sortCull","entities","detailDoodads","m2Animate","shadows","rootWait","groupWait",
 "frame","worldFrame","m2Opaque","m2Alpha","drawLists","hdOpaque","liquid","spells","postFx",
 "grassLights","grassConstants","grassDraw","grassFill",
 "cullVolumes","cullChunks","cullGroups","cullLiquid","cullEntitys","cullDoodads","cullOcclusion","cullHorizon","cullChunkDoodads","cullChunkPrep"};
constexpr const char* counterNames[NC]={"wmoDefs","wmoGroups","rootWaits","groupWaits","asyncReads","horizonAll","horizonCandidates","horizonFallbacks","horizonMismatch","grassChunks","grassDraws","grassFills","grassSingleModel","grassMixedModels","grassInsts","grassPackVertices","grassPackAcquires","grassPackFull","grassPackIndices","grassPackIndexFull","grassPackEvicts","grassFillSkipped","cullChunksTested","cullChunksKept","cullClipUpdates","cullDoodadsTested"};
uint64_t Now(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
void Reset(){timing={};counters={};frames=0;window=Now();}
void CloseLog(){if(logFile){std::fclose(logFile);logFile=nullptr;}}
bool Toggle(CVar*,const char*,const char*s,void*){
 if(!s||!(std::strcmp(s,"0")==0||std::strcmp(s,"1")==0))return false;
 enabled=s[0]=='1';Reset();lastFrame=0;
 if(!enabled){CloseLog();if(info)info->Set("off",true,false,false,false);}return true;
}
double Ms(Stage s,double f){return timing[size_t(s)].ns/1e6/f;}
}
void Initialize(){
 if(initialized)return;
 CVar*toggle=CVar::Register("hdCataRenderPerf","HD: CPU stages; Logs/CataRenderPerf.log every 2s, no FPS limit changes",1,"0",Toggle,CATEGORY_GRAPHICS,false,nullptr,false);
 info=CVar::Register("hdCataRenderPerfInfo","HD: last CPU measurements (milliseconds per scene render)",0,"off",nullptr,CATEGORY_GRAPHICS,false,nullptr,false); if (info) info->m_flags &= ~0x1u; /* status only: not archived to Config.wtf */
 initialized=true;enabled=toggle&&toggle->m_intValue!=0;Reset();
}
void Shutdown(){enabled=false;initialized=false;CloseLog();info=nullptr;Reset();}
bool Enabled(){return enabled;}
uint64_t Begin(){return enabled?Now():0;}
void End(Stage stage,uint64_t start){
 if(!enabled||!start||size_t(stage)>=NS)return;
 const auto elapsed=Now()-start;auto&t=timing[size_t(stage)];t.ns+=elapsed;++t.calls;if(elapsed>t.maximum)t.maximum=elapsed;
}
void Add(Counter c,uint64_t n){if(enabled&&size_t(c)<NC)counters[size_t(c)]+=n;}
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
 Reset();
}
}
