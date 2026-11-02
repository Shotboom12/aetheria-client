#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdMirrorPass.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdMirrorMath.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdCaptureSession.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Manifest.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Shaders.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace gxvk::vk::hdmirror {
namespace {
struct Slot { texture::Image image; uint64_t used=0,valid=0; Snapshot stamp{}; };
Slot slots[frame::kFramesInFlight];
struct RestoreScene { ~RestoreScene() { frame::SetTarget(nullptr,0); } };
bool CopySky(const texture::Image& sky,texture::Image& target){
    const auto* h=context::Get();
    if(!h || !h->dapi || !h->dapi->CmdBlitImage || sky.cube || !sky.extent.width || !sky.extent.height ||
       sky.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
       (sky.format!=VK_FORMAT_R8G8B8A8_UNORM && sky.format!=VK_FORMAT_B8G8R8A8_UNORM))return false;
    auto cmd=frame::Outside();if(!cmd)return false;
    auto barrier=[&](VkImage image,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst){
        VkImageMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.image=image;b.oldLayout=from;b.newLayout=to;b.srcAccessMask=src;b.dstAccessMask=dst;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        h->dapi->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,0,nullptr,0,nullptr,1,&b);
    };
    barrier(sky.handle,sky.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    barrier(target.handle,target.layout,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        target.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageBlit blit{};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.dstSubresource=blit.srcSubresource;
    blit.srcOffsets[1]={static_cast<int32_t>(sky.extent.width),static_cast<int32_t>(sky.extent.height),1};
    blit.dstOffsets[1]={static_cast<int32_t>(target.extent.width),static_cast<int32_t>(target.extent.height),1};
    h->dapi->CmdBlitImage(cmd,sky.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,target.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
    barrier(sky.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,sky.layout,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
    barrier(target.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    target.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;return true;
}
bool ProgramReady(const pipelines::Draw& d) {
    using P=pipelines::VertexProgram;
    if (d.vertex==P::FixedFunction || d.vertex==P::FixedFunctionSemantic) return true;
    if (d.vertex!=P::Translated || !d.vertexRecord) return false;
    char path[96];const int n=std::snprintf(path,sizeof path,"mirror/%s",d.vertexRecord->spv);
    return n>0 && n<static_cast<int>(sizeof path) && shaders::Get(path)!=VK_NULL_HANDLE;
}
bool Transform(pipelines::Draw& d, const float clip[16], float sx,float sy,VkExtent2D extent) {
    using P=pipelines::VertexProgram;
    if (d.vertex==P::Translated) { d.mirror=true;std::memcpy(d.xform,clip,sizeof d.xform); }
    else if (d.vertex==P::FixedFunction || d.vertex==P::FixedFunctionSemantic) {
        // Ff/FfSem read World*View*Proj from push.xform and perform Y conversion afterwards.
        float composed[16];
        for(int r=0;r<4;++r)for(int c=0;c<4;++c){
            double v=0;for(int k=0;k<4;++k)v+=double(d.xform[r*4+k])*clip[k*4+c];
            composed[r*4+c]=static_cast<float>(v);if(!std::isfinite(composed[r*4+c]))return false;
        }
        std::memcpy(d.xform,composed,sizeof composed);d.mirror=false;
    } else return false;
    if(d.cull==1)d.cull=2;else if(d.cull==2)d.cull=1;
    if (!std::isfinite(d.viewport.x) || !std::isfinite(d.viewport.y) ||
        !std::isfinite(d.viewport.width) || !std::isfinite(d.viewport.height)) return false;
    d.viewport.x*=sx;d.viewport.y*=sy;d.viewport.width*=sx;d.viewport.height*=sy;
    if(d.viewport.width<=0 || d.viewport.height<=0)return false;
    const double x0=std::max(0.0,std::floor(double(d.scissor.offset.x)*sx));
    const double y0=std::max(0.0,std::floor(double(d.scissor.offset.y)*sy));
    const double x1=std::min(double(extent.width),std::ceil((double(d.scissor.offset.x)+d.scissor.extent.width)*sx));
    const double y1=std::min(double(extent.height),std::ceil((double(d.scissor.offset.y)+d.scissor.extent.height)*sy));
    if(x1<=x0 || y1<=y0)return false;
    d.scissor={{static_cast<int32_t>(x0),static_cast<int32_t>(y0)},
               {static_cast<uint32_t>(x1-x0),static_cast<uint32_t>(y1-y0)}};
    return true;
}
}
bool Render(const float view[16],const float projection[16],float height,float eyeZ,const texture::Image* sky,const float* fogColor) {
    const auto slotIndex=frame::Slot();
    if(slotIndex>=frame::kFramesInFlight)return false;
    auto& slot=slots[slotIndex];slot.valid=0;
    if(frame::Target() || frame::QueryActive())return false;
    const auto serial=frame::Serial();const auto* batch=hdcapture::Ready(serial);
    const auto* scene=frame::Scene();
    if(!batch || !scene || scene->extent.width<16 || scene->extent.height<16 || slot.used>frame::Completed())return false;
    float clip[16];if(!ClipTransform(view,projection,height,eyeZ,clip))return false;
    float clear[4]={0,0,0,1};
    if(fogColor)for(int i=0;i<3;++i){if(!std::isfinite(fogColor[i]))return false;clear[i]=std::clamp(fogColor[i],0.0f,1.0f);}
    const VkExtent2D extent={std::max(1u,scene->extent.width/2),std::max(1u,scene->extent.height/2)};
    const float sx=float(extent.width)/scene->extent.width,sy=float(extent.height)/scene->extent.height;
    const auto count=batch->Count();
    // Preflight modules/transforms before touching render targets. Pipeline failure later leaves valid=0.
    for(size_t i=0;i<count;++i){pipelines::Draw d;
        if(!hdcapture::Read(i,serial,d) || !ProgramReady(d) || !Transform(d,clip,sx,sy,extent))return false;
    }
    if(!slot.image.handle || slot.image.extent.width!=extent.width || slot.image.extent.height!=extent.height){
        frame::RetireImage(slot.image,slot.used);slot.used=0;
        if(!texture::CreateTarget(extent.width,extent.height,1,false,VK_FORMAT_R8G8B8A8_UNORM,slot.image))return false;
        slot.image.isolatedDepth=true;
    }
    RestoreScene restore;slot.used=serial;
    if(sky && !CopySky(*sky,slot.image))return false;
    frame::SetTarget(&slot.image,0);
    frame::Clear(sky?2:3,clear,nullptr);
    auto cmd=frame::Pass();if(!cmd)return false;
    for(size_t i=0;i<count;++i){pipelines::Draw d;
        if(!hdcapture::Read(i,serial,d) || !Transform(d,clip,sx,sy,extent) ||
           !pipelines::Record(cmd,frame::RenderPass(),frame::PassGeneration(),serial,slotIndex,d))return false;
    }
    // End the pass now so its native colour attachment is shader-readable before exposure.
    frame::SetTarget(nullptr,0);
    if(slot.image.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)return false;
    slot.stamp = {}; slot.stamp.image = &slot.image; slot.stamp.serial = serial; slot.stamp.height = height;
    std::memcpy(slot.stamp.view, view, sizeof slot.stamp.view);
    std::memcpy(slot.stamp.projection, projection, sizeof slot.stamp.projection);
    slot.stamp.sourceExtent = scene->extent;
    slot.valid=serial;return true;
}
const texture::Image* Get(){
    const auto index=frame::Slot();if(index>=frame::kFramesInFlight)return nullptr;
    const auto& s=slots[index];return s.valid && s.valid==frame::Serial()?&s.image:nullptr;
}
bool GetSnapshot(uint64_t serial, Snapshot& out) {
    out = {};
    if (!serial || serial != frame::Serial()) return false;
    const auto index = frame::Slot();
    if (index >= frame::kFramesInFlight) return false;
    const auto& slot = slots[index];
    if (slot.valid != serial || slot.stamp.serial != serial ||
        slot.image.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return false;
    out = slot.stamp; return true;
}
void Shutdown(){for(auto& s:slots){frame::RetireImage(s.image,s.used);s.used=s.valid=0;}}
void Invalidate(){for(auto& s:slots)s.valid=0;}
}
