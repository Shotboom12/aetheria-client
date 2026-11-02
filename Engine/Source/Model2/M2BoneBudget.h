#pragma once
#include <cstdio>
#include <set>
#include <string>
#include <windows.h>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include <cstring>
// Preserve the established 75-bone path for converted character assets. Native
// Retail Vulkan shaders have 512 vertex registers, with 31 reserved: 160 bones.
inline unsigned M2BoneLimit(char const* model) {
    bool retail=model && ((!_strnicmp(model,"retail",6) && (model[6]=='/' || model[6]=='\\')) ||
        (!_strnicmp(model,"mountstudio",11) && (model[11]=='/' || model[11]=='\\')));
    if(retail && g_theGxDevicePtr && g_theGxDevicePtr->DeviceApi()==GxApi_Vulkan) {
        unsigned constants=g_theGxDevicePtr->Caps().m_shaderConstants[GxSh_Vertex];
        return constants>31 ? (constants-31)/3 : 0;
    }
    return 75;
}
// Vertex constants 0..30 are reserved. Each bone consumes three of the
// remaining 225 vector registers, so a draw can upload at most 75 bones.
inline void M2ReportBoneLimit(char const* model,unsigned bones,unsigned instances) {
    static auto* reported=new std::set<std::string>;
    std::string key=(model?model:"<unknown>")+std::string(":")+std::to_string(bones)+":"+std::to_string(instances);
    if(reported->size()>=128 || !reported->insert(key).second)return;
    char path[MAX_PATH]{};GetModuleFileNameA(nullptr,path,MAX_PATH);
    char* slash=strrchr(path,'\\');if(!slash)return;strcpy_s(slash+1,MAX_PATH-(slash-path)-1,"EsteriaRenderer.log");
    FILE* file=nullptr;if(!fopen_s(&file,path,"a")){
        fprintf(file,"Bone register budget: model=%s bones=%u instances=%u limit=%u\n",model?model:"?",bones,instances,M2BoneLimit(model));
        fclose(file);
    }
}
inline void M2ReportIndexRange(char const* model,unsigned section,unsigned start,unsigned count,unsigned total) {
    static auto* reported=new std::set<std::string>;
    std::string key=(model?model:"<unknown>")+std::string(":index:")+std::to_string(section)+":"+std::to_string(start)+":"+std::to_string(count)+":"+std::to_string(total);
    if(reported->size()>=256 || !reported->insert(key).second)return;
    char path[MAX_PATH]{};GetModuleFileNameA(nullptr,path,MAX_PATH);
    char* slash=strrchr(path,'\\');if(!slash)return;strcpy_s(slash+1,MAX_PATH-(slash-path)-1,"EsteriaRenderer.log");
    FILE* file=nullptr;if(!fopen_s(&file,path,"a")){
        fprintf(file,"Skipped invalid skin index range: model=%s section=%u start=%u count=%u total=%u\n",model?model:"?",section,start,count,total);
        fclose(file);
    }
}
