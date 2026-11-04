// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef WOWDECOMP_FSR
#include "vulkan/Context.hpp"
#include "wxl/Script.hpp"
#include "HdFsrValidation.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace HD::Fsr::Validation {
namespace {
 Recorder recorder;Sample pending;FILE*file=nullptr;bool wasEnabled=false,failed=false,wrote=false;
 double Now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
 int64_t Utc(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
 Status Snapshot(){auto s=wxlvk::vk::context::ReadValidationStatus();return{s.layer,s.messenger,s.syncConfigured,s.errors,s.warnings};}
 bool Write(void*,const char*p,size_t n){wrote=true;return file&&std::fwrite(p,1,n,file)==n;}
 void Stop(){if(recorder.Active()){bool ok=recorder.Stop(Now(),Utc(),Snapshot());wxl::ScriptMgr::Log(ok?WXL_LOG_INFO:WXL_LOG_WARN,"fsr: V1 capture ended (%s); NOT a validation approval",ok?"complete":"I/O or sequence failure");}if(file){std::fclose(file);file=nullptr;}}
}
void BeginWorld(uint64_t serial,bool enabled,uint32_t flags,uint32_t test,uint32_t quality,uint32_t debug){
 if(!enabled){if(wasEnabled)Stop();wasEnabled=false;failed=false;pending={};return;}
 if(!wasEnabled){
  wasEnabled=true;const char*id=std::getenv("WAYSTONE_FSR_V1_SESSION");file=std::fopen("Logs/FsrValidation.jsonl","ab");
  if(!file||!recorder.Start(id,Now(),Utc(),Write,nullptr)){Stop();failed=true;wxl::ScriptMgr::Log(WXL_LOG_WARN,"fsr: V1 cannot start: require a valid prepared session id and writable Logs folder; toggle hdFSRValidation 0 before retry");}
  else {wxl::ScriptMgr::Log(WXL_LOG_INFO,"fsr: V1 capture begin session %s; no normal-mode flags changed",id);const auto s=Snapshot();wxl::ScriptMgr::Log(WXL_LOG_INFO,"fsr: V1 layer=%u messenger=%u sync_configured=%u",s.layer?1u:0u,s.messenger?1u:0u,s.syncConfigured?1u:0u);}
 }
 if(failed)return;
 pending={};pending.serial=serial;pending.flags=flags;pending.test=test;pending.quality=quality;pending.debug=debug;
}
void Dimensions(uint32_t w,uint32_t h,uint32_t dw,uint32_t dh){if(recorder.Active()){pending.width=w;pending.height=h;pending.displayWidth=dw;pending.displayHeight=dh;pending.flags|=WorldTarget;}}
void Mark(uint64_t serial,uint32_t flags){if(recorder.Active()&&serial==pending.serial)pending.flags|=flags;}
void BeforePresent(uint64_t serial){
 if(!recorder.Active()||failed)return;
 // One main present per serial. A present without BeginWorld is deliberately recorded with no FSR flags.
 if(pending.serial!=serial){pending={};pending.serial=serial;}
 if(!recorder.Present(pending,Now(),Snapshot())){failed=true;wxl::ScriptMgr::Log(WXL_LOG_WARN,"fsr: V1 capture failed (sequence/I/O); report is incomplete");}
 if(file&&wrote){if(std::fflush(file))failed=true;wrote=false;} // only after a two-second batch/header
}
void Shutdown(){Stop();pending={};wasEnabled=failed=false;}
}
#endif
