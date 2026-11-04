#pragma once
#include <cstdarg>
namespace HD::Lumen::Source {
enum class LogLevel:int{Error=0,Info=1,Debug=2};
using LogSink=void(*)(LogLevel,const char*);
void SetLogSink(LogSink);
void LogSetLevel(int);
void LogWrite(LogLevel,const char*,...);
}
#define VF_LOG_ERROR(...) ::HD::Lumen::Source::LogWrite(::HD::Lumen::Source::LogLevel::Error,__VA_ARGS__)
#define VF_LOG_INFO(...) ::HD::Lumen::Source::LogWrite(::HD::Lumen::Source::LogLevel::Info,__VA_ARGS__)
#define VF_LOG_DEBUG(...) ::HD::Lumen::Source::LogWrite(::HD::Lumen::Source::LogLevel::Debug,__VA_ARGS__)
