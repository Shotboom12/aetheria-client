// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace HD::Fsr::Validation {
enum Flag:uint32_t {RequestUpscale=1,RequestGeneration=2,Object=4,Rigid=8,Skin=16,Wind=32,Reactive=64,MasksReady=128,UpscaleRecorded=256,GenerationPrepared=512,GenerationConfigured=1024,InputSafe=2048,HistoryReset=4096,SdkFailure=8192,WorldTarget=16384};
struct Status {bool layer=false,messenger=false,syncConfigured=false;uint64_t errors=0,warnings=0;};
struct Sample {double ms=0,intervalMs=-1;uint64_t serial=0;uint32_t flags=0,test=0,quality=1,debug=0,width=0,height=0,displayWidth=0,displayHeight=0;};
using Sink=bool(*)(void*,const char*,size_t);
class Recorder {
public:
 bool Start(const char*session,double monotonicMs,int64_t utcMs,Sink,void*user);
 bool Present(Sample,double monotonicMs,const Status&);
 bool Stop(double monotonicMs,int64_t utcMs,const Status&);
 bool Active()const{return active_;}bool Good()const{return good_;}
 uint64_t Dropped()const{return dropped_;}
private:
 bool BeginBatch(double,const Status&,const char*kind,int64_t utc=0);
 bool Flush(double,const Status&);
 bool Write(const char*,size_t);bool Text(const char*);
 std::array<Sample,4096>rows_{};size_t count_=0;uint64_t dropped_=0,total_=0,lastSerial_=0;
 double start_=0,lastFlush_=0,lastPresent_=-1;int64_t wall_=0;bool active_=false,good_=true;char id_[33]{};Sink sink_=nullptr;void*user_=nullptr;
};
// Render-thread bridge. Enabling is explicit and session-scoped; it never changes rendering policy.
void BeginWorld(uint64_t serial,bool enabled,uint32_t flags,uint32_t test,uint32_t quality,uint32_t debug);
void Dimensions(uint32_t width,uint32_t height,uint32_t displayWidth,uint32_t displayHeight);
void Mark(uint64_t serial,uint32_t flags);
void BeforePresent(uint64_t serial);
void Shutdown();
}
