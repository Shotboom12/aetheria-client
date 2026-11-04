// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: see HdWxlShim.h. The table follows WarcraftXL's wxl/VkRenderApi.h (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL).
#include "HdWxlShim.h"
#include "compat/wxl/VkRenderApi.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdGeometryPass.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
namespace HD::WxlShim {
namespace {
namespace eff=gxvk::vk::hdeffect;
struct Stage { WXL_VkStageDesc desc{}; uint32_t sequence=0; char name[48]{}; };
std::vector<std::unique_ptr<Stage>> stages;
uint32_t sequenceCounter=0,unserved=0,gbufferRequests=0;
int __cdecl GetDeviceInfo(WXL_VkDeviceInfo* out){
    const eff::Api* api=eff::Get();if(!api||!out||out->structSize<sizeof(WXL_VkDeviceInfo))return 0;
    eff::DeviceInfo d{};d.structSize=sizeof d;if(!api->GetDeviceInfo(&d))return 0;
    out->structSize=sizeof(WXL_VkDeviceInfo);out->instance=d.instance;out->physicalDevice=d.physicalDevice;out->device=d.device;
    out->queueFamily=d.queueFamily;out->frameSlots=d.frameSlots;out->getInstanceProcAddr=d.getInstanceProcAddr;
    out->getDeviceProcAddr=d.getDeviceProcAddr;out->pipelineCache=d.pipelineCache;return 1;
}
void* __cdecl CreateBuffer(const WXL_VkBufferDesc* desc,WXL_VkBufferInfo* out){
    const eff::Api* api=eff::Get();if(!api||!desc||!out)return nullptr;
    const eff::BufferDesc d{desc->size,desc->usage,desc->memory};eff::BufferInfo info{};
    void* handle=api->CreateBuffer(&d,&info);if(!handle)return nullptr;
    out->buffer=info.buffer;out->size=info.size;out->mapped=info.mapped;return handle;
}
void __cdecl RetireBuffer(void* buffer,uint64_t serial){if(const eff::Api* api=eff::Get())api->RetireBuffer(buffer,serial);}
void __cdecl FlushBuffer(void* buffer){if(const eff::Api* api=eff::Get())api->FlushBuffer(buffer);}
void* __cdecl CreateImage(const WXL_VkImageDesc* desc,WXL_VkImageInfo* out){
    const eff::Api* api=eff::Get();if(!api||!desc||!out)return nullptr;
    const eff::ImageDesc d{desc->format,desc->width,desc->height,desc->mipLevels?desc->mipLevels:1u,desc->usage,desc->aspect};eff::ImageInfo info{};
    void* handle=api->CreateImage(&d,&info);if(!handle)return nullptr;
    out->image=info.image;out->view=info.view;return handle;
}
void __cdecl RetireImage(void* image,uint64_t serial){if(const eff::Api* api=eff::Get())api->RetireImage(image,serial);}
void* __cdecl RegisterStage(const WXL_VkStageDesc* desc,const char* name){
    if(!desc||desc->structSize<sizeof(WXL_VkStageDesc)||!desc->Run||desc->point>=WXL_VK_POINT_COUNT)return nullptr;
    auto s=std::make_unique<Stage>();s->desc=*desc;s->sequence=++sequenceCounter;std::snprintf(s->name,sizeof s->name,"%s",name?name:"?");
    Stage* raw=s.get();stages.push_back(std::move(s));return raw;
}
void __cdecl UnregisterStage(void* handle){
    // Single render thread, never called from inside Run: the stage can go at once.
    stages.erase(std::remove_if(stages.begin(),stages.end(),[&](const std::unique_ptr<Stage>& s){return s.get()==handle;}),stages.end());
}
void __cdecl RequestGBuffer(int wanted){if(wanted)++gbufferRequests;else if(gbufferRequests)--gbufferRequests;}
// The six-colour geometry replay (colour 1 albedo, 2 normal, 3 light, 4/5 shadow endpoints) is wxl's G-buffer. valid=0 until a full replay of this frame
// exists; the interior volumes (wxl's wmogroups) are not produced, so interiorCount is 0.
int __cdecl GetGBuffer(WXL_VkGBuffer* out){
    if(!out||out->structSize<sizeof(uint32_t))return 0;
    gxvk::vk::hdgeometry::GBuffer g;
    const bool ok=gxvk::vk::hdgeometry::GetGBuffer(gxvk::vk::frame::Serial(),g);
    WXL_VkGBuffer d{};
    d.structSize=out->structSize;d.valid=ok?1u:0u;d.serial=ok?g.serial:0;
    if(ok){d.albedoView=g.albedo;d.normalView=g.normal;d.lightView=g.light;d.albedoFormat=g.albedoFormat;d.normalFormat=g.normalFormat;d.lightFormat=g.lightFormat;
        d.layout=g.layout;d.extent=g.extent;d.shadowLowView=g.shadowLow;d.shadowHighView=g.shadowHigh;d.shadowEndpointFormat=g.shadowFormat;d.shadowEndpointContract=0x53484434;}
    const size_t n=out->structSize<sizeof d?out->structSize:sizeof d;
    std::memcpy(out,&d,n);out->structSize=static_cast<uint32_t>(n);
    return ok?1:0;
}
void __cdecl RequestOpaqueDepth(int wanted){if(wanted)++unserved;}
int __cdecl GetOpaqueDepth(WXL_VkPreviousDepth* out){if(out)out->valid=0;return 0;} // the stage copies the depth itself
void Mul(const float* a,const float* b,float* r){for(int i=0;i<4;++i)for(int j=0;j<4;++j){float s=0;for(int k=0;k<4;++k)s+=a[i*4+k]*b[k*4+j];r[i*4+j]=s;}}
bool Invert(const float* m,float* out){
    double a[4][8];for(int i=0;i<4;++i)for(int j=0;j<4;++j){a[i][j]=m[i*4+j];a[i][4+j]=i==j?1:0;}
    for(int c=0;c<4;++c){int p=c;for(int r=c+1;r<4;++r)if(std::fabs(a[r][c])>std::fabs(a[p][c]))p=r;if(std::fabs(a[p][c])<1e-12)return false;
        if(p!=c)for(int j=0;j<8;++j)std::swap(a[p][j],a[c][j]);
        const double d=a[c][c];for(int j=0;j<8;++j)a[c][j]/=d;
        for(int r=0;r<4;++r)if(r!=c){const double f=a[r][c];if(f!=0)for(int j=0;j<8;++j)a[r][j]-=f*a[c][j];}}
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)out[i*4+j]=float(a[i][4+j]);return true;
}
}
const WXL_VkRenderApi* Api(){
    static const WXL_VkRenderApi table{sizeof(WXL_VkRenderApi),WXL_VK_RENDER_API_VERSION,GetDeviceInfo,CreateBuffer,RetireBuffer,FlushBuffer,
        CreateImage,RetireImage,RegisterStage,UnregisterStage,RequestGBuffer,GetGBuffer,RequestOpaqueDepth,GetOpaqueDepth};
    return eff::Get()?&table:nullptr;
}
bool Wanted(uint32_t point){
    for(const auto& s:stages)if(s->desc.point==point&&(!s->desc.Wanted||s->desc.Wanted(s->desc.user)))return true;
    return false;
}
uint32_t Run(uint32_t point,const eff::StageContext& n){
    if(!n.cmd||!n.colorView)return 0;
    std::vector<Stage*> order;for(auto& s:stages)if(s->desc.point==point)order.push_back(s.get());
    if(order.empty())return 0;
    std::stable_sort(order.begin(),order.end(),[](const Stage* a,const Stage* b){return a->desc.order!=b->desc.order?a->desc.order<b->desc.order:a->sequence<b->sequence;});
    WXL_VkSceneConstants scene{};
    if(n.scene&&n.sceneValid){
        std::memcpy(scene.view,n.scene->view,sizeof scene.view);std::memcpy(scene.projection,n.scene->projection,sizeof scene.projection);
        Mul(scene.view,scene.projection,scene.viewProj);
        float inv[16];if(Invert(scene.view,inv)){scene.cameraPos[0]=inv[12];scene.cameraPos[1]=inv[13];scene.cameraPos[2]=inv[14];scene.cameraPos[3]=1;}
        scene.viewport[0]=n.viewport.width;scene.viewport[1]=n.viewport.height;
        scene.viewport[2]=n.viewport.width>0?1.f/n.viewport.width:0.f;scene.viewport[3]=n.viewport.height>0?1.f/n.viewport.height:0.f;
    }
    WXL_VkStageContext c{};
    c.structSize=sizeof c;c.point=point;c.serial=n.serial;c.completedSerial=n.completedSerial;c.frameSlot=n.frameSlot;c.frameSlots=n.frameSlots;c.cmd=n.cmd;
    c.extent=n.extent;c.colorImage=n.colorImage;c.colorView=n.colorView;c.colorFormat=n.colorFormat;c.colorLayout=n.colorLayout;c.swapRB=n.swapRB;
    c.depthView=n.depthView;c.depthFormat=n.depthFormat;c.depthLayout=n.depthLayout;c.scene=&scene;c.viewport=n.viewport;c.sceneValid=n.sceneValid;
    c.jitter[0]=n.jitter[0];c.jitter[1]=n.jitter[1];
    uint32_t ran=0;
    for(Stage* s:order){if(s->desc.Wanted&&!s->desc.Wanted(s->desc.user))continue;s->desc.Run(s->desc.user,&c);++ran;}
    return ran;
}
uint32_t StageCount(){return uint32_t(stages.size());}
uint32_t UnservedRequests(){return unserved;}
bool GBufferRequested(){return gbufferRequests!=0;}
}
