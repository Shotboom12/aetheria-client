#include "log.h"
#include <cstdio>
namespace HD::Lumen::Source {
namespace {LogSink sink=nullptr;int threshold=1;}
void SetLogSink(LogSink s){sink=s;}
void LogSetLevel(int level){threshold=level;}
void LogWrite(LogLevel level,const char*fmt,...){if(!sink||int(level)>threshold)return;char line[2048];va_list args;va_start(args,fmt);vsnprintf(line,sizeof line,fmt,args);va_end(args);sink(level,line);}
}
