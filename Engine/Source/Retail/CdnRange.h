#pragma once
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace Retail {
// Require an exact HTTP 206 response. Never silently accept a whole CDN archive.
inline bool ReadCdnRange(const std::wstring& url,uint64_t offset,uint32_t length,
                         std::vector<uint8_t>& bytes,std::string& error) {
    bytes.clear();error.clear();
    if(!length || length>256u*1024*1024 || offset>UINT64_MAX-length) {error="Invalid CDN range bounds";return false;}
    struct Internet {
        HINTERNET handle=nullptr;
        ~Internet(){if(handle)WinHttpCloseHandle(handle);}
    } session,connection,request;
    const auto fail=[&](const char* message) {error=std::string(message)+" ("+std::to_string(GetLastError())+")";bytes.clear();return false;};
    URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
    parts.dwHostNameLength=DWORD(-1);parts.dwUrlPathLength=DWORD(-1);parts.dwExtraInfoLength=DWORD(-1);
    if(!WinHttpCrackUrl(url.c_str(),0,0,&parts))return fail("Cannot parse CDN URL");
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);
    if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    session.handle=WinHttpOpen(L"Esteria Retail Asset Cache/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session.handle)return fail("Cannot open CDN HTTP session");
    WinHttpSetTimeouts(session.handle,10000,10000,20000,30000);
    connection.handle=WinHttpConnect(session.handle,host.c_str(),parts.nPort,0);
    if(!connection.handle)return fail("Cannot connect to CDN");
    request.handle=WinHttpOpenRequest(connection.handle,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,parts.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0);
    if(!request.handle)return fail("Cannot create CDN request");
    const uint64_t end=offset+length-1;
    const std::wstring range=L"Range: bytes="+std::to_wstring(offset)+L"-"+std::to_wstring(end)+L"\r\nAccept-Encoding: identity\r\n";
    if(!WinHttpSendRequest(request.handle,range.c_str(),DWORD(range.size()),WINHTTP_NO_REQUEST_DATA,0,0,0)
        || !WinHttpReceiveResponse(request.handle,nullptr))return fail("CDN range request failed");
    DWORD status=0,size=sizeof(status);
    if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX) || status!=206) {
        error="CDN refused an exact range (HTTP "+std::to_string(status)+"); whole-archive fallback is disabled";return false;
    }
    wchar_t contentRange[128]{};size=sizeof(contentRange);
    unsigned long long first=0,last=0,total=0;
    if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_CONTENT_RANGE,WINHTTP_HEADER_NAME_BY_INDEX,
        contentRange,&size,WINHTTP_NO_HEADER_INDEX)
        || swscanf_s(contentRange,L"bytes %llu-%llu/%llu",&first,&last,&total)!=3
        || first!=offset || last!=end || total<=last) {error="CDN returned a mismatched Content-Range";return false;}
    DWORD contentLength=0;size=sizeof(contentLength);
    if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_CONTENT_LENGTH|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&contentLength,&size,WINHTTP_NO_HEADER_INDEX) || contentLength!=length) {
        error="CDN returned a mismatched Content-Length";return false;
    }
    bytes.resize(length);
    size_t received=0;
    while(received<length) {
        DWORD read=0;
        if(!WinHttpReadData(request.handle,bytes.data()+received,DWORD(length-received),&read))return fail("Cannot read CDN range");
        if(!read) {error="CDN range was truncated";bytes.clear();return false;}
        received+=read;
    }
    uint8_t extra;DWORD read=0;
    if(!WinHttpReadData(request.handle,&extra,1,&read))return fail("Cannot finish CDN range");
    if(read) {error="CDN range exceeded its declared length";bytes.clear();return false;}
    return true;
}
}
#endif
