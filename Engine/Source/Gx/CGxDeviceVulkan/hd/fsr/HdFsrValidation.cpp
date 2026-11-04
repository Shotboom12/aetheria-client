// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef WOWDECOMP_FSR
#include "HdFsrValidation.h"
#include <cmath>
#include <cstdio>
#include <cstring>
namespace HD::Fsr::Validation {
bool Recorder::Write(const char*p,size_t n){if(!good_||!sink_||!sink_(user_,p,n)){good_=false;return false;}return true;}
bool Recorder::Text(const char*p){return Write(p,std::strlen(p));}
bool Recorder::Start(const char*id,double now,int64_t utc,Sink sink,void*user){
 if(active_||!id||std::strlen(id)!=32||!std::isfinite(now)||now<0||utc<=0||!sink)return false;
 for(unsigned i=0;i<32;++i)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return false;
 std::memcpy(id_,id,33);sink_=sink;user_=user;start_=lastFlush_=now;lastPresent_=-1;wall_=utc;lastSerial_=total_=dropped_=0;count_=0;good_=true;active_=true;
 char line[256];const int n=std::snprintf(line,sizeof line,"{\"schema\":1,\"kind\":\"begin\",\"session\":\"%s\",\"utc_ms\":%lld}\n",id_,static_cast<long long>(utc));return Write(line,size_t(n));
}
bool Recorder::BeginBatch(double now,const Status&s,const char*kind,int64_t utc){
 char line[512];const int n=std::snprintf(line,sizeof line,"{\"schema\":1,\"kind\":\"%s\",\"session\":\"%s\",\"elapsed_ms\":%.6f,\"utc_ms\":%lld,\"validation\":{\"layer\":%s,\"messenger\":%s,\"sync_configured\":%s,\"errors\":%llu,\"warnings\":%llu},\"dropped\":%llu,\"frames\":%llu",kind,id_,now-start_,static_cast<long long>(utc),s.layer?"true":"false",s.messenger?"true":"false",s.syncConfigured?"true":"false",static_cast<unsigned long long>(s.errors),static_cast<unsigned long long>(s.warnings),static_cast<unsigned long long>(dropped_),static_cast<unsigned long long>(total_));return n>0&&size_t(n)<sizeof line&&Write(line,size_t(n));
}
bool Recorder::Flush(double now,const Status&s){
 if(!BeginBatch(now,s,"batch")||!Text(",\"samples\":["))return false;
 for(size_t i=0;i<count_;++i){const Sample&r=rows_[i];char line[384];int n=std::snprintf(line,sizeof line,"%s[%.6f,%.6f,%llu,%u,%u,%u,%u,%u,%u,%u,%u]",i?",":"",r.ms,r.intervalMs,static_cast<unsigned long long>(r.serial),r.flags,r.test,r.quality,r.debug,r.width,r.height,r.displayWidth,r.displayHeight);if(n<=0||size_t(n)>=sizeof line||!Write(line,size_t(n)))return false;}
 if(!Text("]}\n"))return false;count_=0;lastFlush_=now;return true;
}
bool Recorder::Present(Sample row,double now,const Status&s){
 if(!active_||!good_)return false;
 if(!std::isfinite(now)||now<start_||(lastPresent_>=0&&now<lastPresent_)||!row.serial||row.serial<=lastSerial_){good_=false;return false;}
 row.ms=now-start_;row.intervalMs=lastPresent_<0?-1:now-lastPresent_;lastPresent_=now;lastSerial_=row.serial;++total_;
 if(count_==rows_.size()){++dropped_;return Flush(now,s);}rows_[count_++]=row;
 return now-lastFlush_>=2000?Flush(now,s):true;
}
bool Recorder::Stop(double now,int64_t utc,const Status&s){
 if(!active_)return good_;active_=false;
 if(!good_||!std::isfinite(now)||now<start_||(lastPresent_>=0&&now<lastPresent_)||utc<=0){good_=false;return false;}
 return Flush(now,s)&&BeginBatch(now,s,"end",utc)&&Text("}\n");
}
}
#endif
