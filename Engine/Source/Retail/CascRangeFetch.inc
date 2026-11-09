// Local Windows adaptation for pinned CascLib 38a3466. Preserve its decoding and
// strict BLTE validation; replace whole-archive acquisition with exact byte ranges.
#include "Engine/Source/Retail/CdnRange.h"
static bool IsValidCascRange(const std::vector<uint8_t>& bytes, const BYTE* key)
{
    if (bytes.size() < 8 || memcmp(bytes.data(), "BLTE", 4))
        return false;
    const DWORD header = (DWORD(bytes[4]) << 24) | (DWORD(bytes[5]) << 16)
        | (DWORD(bytes[6]) << 8) | bytes[7];
    if (header && (header < 12 || header > bytes.size()))
        return false;
    BYTE hash[MD5_HASH_SIZE];
    CascHash_MD5(bytes.data(), header ? header : bytes.size(), hash);
    return !memcmp(hash, key, sizeof(hash));
}

static DWORD FetchCascAssetRange(TCascStorage* hs,LPBYTE fileKey,PCASC_EKEY_ENTRY entry,
    CASC_PATH<TCHAR>& local,PCASC_ARCHIVE_INFO info)
{
    info->ArchiveIndex=DWORD(entry->StorageOffset >> hs->FileOffsetBits);
    info->ArchiveOffs=DWORD(entry->StorageOffset & ((ValueOne64 << hs->FileOffsetBits)-1));
    info->EncodedSize=entry->EncodedSize;
    const LPBYTE archiveKey=hs->ArchivesKey.pbData+MD5_HASH_SIZE*info->ArchiveIndex;
    memcpy(info->ArchiveKey,archiveKey,MD5_HASH_SIZE);
    // One encoded asset per immutable EKey, independent of the source archive offset.
    local.Create(hs->szRootPath,_T("ranges"),NULL);
    local.AppendEKey(fileKey);local.AppendString(_T(".range"),false);
    if(FileAlreadyExists(local)) {
        TFileStream* stream=FileStream_OpenFile(local,BASE_PROVIDER_FILE|STREAM_PROVIDER_FLAT);
        ULONGLONG length=0,offset=0;
        std::vector<uint8_t> cached;
        bool valid=stream && FileStream_GetSize(stream,&length) && length==entry->EncodedSize;
        if(valid) {
            cached.resize(entry->EncodedSize);
            valid=FileStream_Read(stream,&offset,cached.data(),entry->EncodedSize)
                && IsValidCascRange(cached,fileKey);
        }
        if(stream)FileStream_Close(stream);
        if(valid) {info->ArchiveOffs=0;return ERROR_SUCCESS;}
        DeleteFile(LPCTSTR(local));
    }
    if(!(hs->dwFeatures&CASC_FEATURE_ONLINE))return ERROR_FILE_NOT_FOUND;
    LPCTSTR servers=hs->szCdnServers;
    TCHAR host[MAX_PATH]={0};
    while((servers=ExtractCdnServerName(host,_countof(host),servers))!=NULL) {
        CASC_PATH<TCHAR> remote(URL_SEP_CHAR);
        remote.Create(host,hs->szCdnPath,GetSubFolder(PathTypeData),NULL);remote.AppendEKey(archiveKey);
        std::basic_string<TCHAR> narrow=LPCTSTR(remote);
        std::wstring url(narrow.begin(),narrow.end());
        if(url.find(L"://")==std::wstring::npos)url=L"http://"+url;
        std::vector<uint8_t> data;std::string error;
        if(!Retail::ReadCdnRange(url,info->ArchiveOffs,entry->EncodedSize,data,error)) {
            std::fprintf(stderr,"Retail CDN range: %s\n",error.c_str());continue;
        }
        if(!IsValidCascRange(data,fileKey)) {
            std::fprintf(stderr,"Retail CDN range: downloaded asset failed EKey validation\n");
            continue;
        }
        TCHAR temporary[MAX_PATH*4];
        CascStrPrintf(temporary,_countof(temporary),_T("%s.%lu.tmp"),LPCTSTR(local),GetCurrentProcessId());
        DWORD result=SaveLocalFile(temporary,data.data(),data.size());
        if(result==ERROR_SUCCESS && !MoveFileEx(temporary,local,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))result=GetLastError();
        if(result!=ERROR_SUCCESS) {DeleteFile(temporary);return result;}
        info->ArchiveOffs=0;
        return ERROR_SUCCESS;
    }
    return ERROR_READ_FAULT;
}
