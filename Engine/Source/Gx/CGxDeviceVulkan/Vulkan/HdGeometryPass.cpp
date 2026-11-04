#include "Engine/Source/Gx/CGxDeviceVulkan/hd/HdNativeShadowBridge.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdGeometryPass.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdGeometryCoverage.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdCaptureSession.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Manifest.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Shaders.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace gxvk::vk::hdgeometry {
namespace {
constexpr VkFormat formats[6]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM,
 VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT};
struct Slot {
 texture::Image colour[6];VkImage depth=VK_NULL_HANDLE;VkImageView depthView=VK_NULL_HANDLE;
 memory::Block depthMemory;VkFramebuffer fb=VK_NULL_HANDLE;VkRenderPass pass=VK_NULL_HANDLE;
 VkExtent2D extent{};uint64_t used=0,valid=0;
};
Slot slots[frame::kFramesInFlight];
VkDevice probeDevice=VK_NULL_HANDLE;bool probed=false,capable=false;
bool Supported() {
 const auto* h=context::Get();if(!h)return false;
 if(probeDevice!=h->device){probeDevice=h->device;probed=capable=false;}
 if(probed)return capable;probed=true;
 if(!h->independentBlend || context::Caps().deviceVersion<VK_API_VERSION_1_1)return false;
 VkPhysicalDeviceProperties p{};h->iapi->GetPhysicalDeviceProperties(h->physical,&p);
 if(p.limits.maxColorAttachments<6)return false;
 for(auto format:formats){VkFormatProperties f{};h->iapi->GetPhysicalDeviceFormatProperties(h->physical,format,&f);
  const auto need=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  if((f.optimalTilingFeatures&need)!=need)return false;}
 capable=true;return true;
}
void Drop(Slot& s) {
 const auto* h=context::Get();if(!h)return;
 if(s.fb)h->dapi->DestroyFramebuffer(h->device,s.fb,nullptr);
 if(s.pass)h->dapi->DestroyRenderPass(h->device,s.pass,nullptr);
 if(s.depthView)h->dapi->DestroyImageView(h->device,s.depthView,nullptr);
 if(s.depth)h->dapi->DestroyImage(h->device,s.depth,nullptr);
 if(s.depthMemory.memory)memory::Free(s.depthMemory);
 for(auto& image:s.colour)texture::DestroyIdle(image);
 s=Slot{};
}
bool Make(Slot& s,VkExtent2D size) {
 const auto* h=context::Get();if(!h)return false;
 // Query budget only on allocation/resize. Leave room for the second flight slot
 // and existing client resources; checked allocator failure still declines safely.
 VkPhysicalDeviceMemoryProperties props{};VkDeviceSize budgets[VK_MAX_MEMORY_HEAPS]{},usage[VK_MAX_MEMORY_HEAPS]{};
 const bool driver=context::MemoryHeaps(props,budgets,usage);VkDeviceSize available=0;
 for(uint32_t i=0;i<props.memoryHeapCount;++i)if(props.memoryHeaps[i].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT){
  const auto free=driver && budgets[i]? (budgets[i]>usage[i]?budgets[i]-usage[i]:0):props.memoryHeaps[i].size/2;
  available=std::max(available,free);
 }
 const VkDeviceSize estimate=VkDeviceSize(size.width)*size.height*48*frame::kFramesInFlight;
 if(!available || estimate>available)return false;
 for(uint32_t i=0;i<6;++i)if(!texture::CreateTarget(size.width,size.height,1,false,formats[i],s.colour[i]))return false;
 // Own depth rather than using the native size-keyed shared target cache.
 VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;image.imageType=VK_IMAGE_TYPE_2D;
 image.format=frame::DepthFormat();image.extent={size.width,size.height,1};image.mipLevels=image.arrayLayers=1;
 image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
 image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;image.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
 if(h->dapi->CreateImage(h->device,&image,nullptr,&s.depth)!=VK_SUCCESS)return false;
 VkMemoryRequirements req{};h->dapi->GetImageMemoryRequirements(h->device,s.depth,&req);
 if(!memory::Allocate(req,memory::Where::DeviceLocal,s.depthMemory) ||
    h->dapi->BindImageMemory(h->device,s.depth,s.depthMemory.memory,s.depthMemory.offset)!=VK_SUCCESS)return false;
 VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=s.depth;
 view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=image.format;view.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
 if(h->dapi->CreateImageView(h->device,&view,nullptr,&s.depthView)!=VK_SUCCESS)return false;
 VkAttachmentDescription attachments[7]{};VkAttachmentReference colours[6]{};
 for(uint32_t i=0;i<7;++i){auto& a=attachments[i];a.format=i<6?formats[i]:image.format;
  a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=i<6?VK_ATTACHMENT_STORE_OP_STORE:VK_ATTACHMENT_STORE_OP_DONT_CARE;
  a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
  a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;a.finalLayout=i<6?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  if(i<6)colours[i]={i,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};}
 VkAttachmentReference depth{6,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
 VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=6;sub.pColorAttachments=colours;sub.pDepthStencilAttachment=&depth;
 VkSubpassDependency deps[2]{};
 deps[0].srcSubpass=VK_SUBPASS_EXTERNAL;deps[0].dstSubpass=0;
 deps[0].srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
 deps[0].srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
 deps[1].srcSubpass=0;deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
 deps[1].dstStageMask=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
 deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;deps[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
 VkRenderPassCreateInfo pass{};pass.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;pass.attachmentCount=7;pass.pAttachments=attachments;
 pass.subpassCount=1;pass.pSubpasses=&sub;pass.dependencyCount=2;pass.pDependencies=deps;
 if(h->dapi->CreateRenderPass(h->device,&pass,nullptr,&s.pass)!=VK_SUCCESS)return false;
 VkImageView views[7];for(int i=0;i<6;++i)views[i]=s.colour[i].view;views[6]=s.depthView;
 VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fb.renderPass=s.pass;
 fb.attachmentCount=7;fb.pAttachments=views;fb.width=size.width;fb.height=size.height;fb.layers=1;
 if(h->dapi->CreateFramebuffer(h->device,&fb,nullptr,&s.fb)!=VK_SUCCESS)return false;
 s.extent=size;return true;
}
template<size_t N>bool Covered(const char* key,const char*const(&keys)[N]) {
 auto p=std::lower_bound(keys,keys+N,key,[](const char* a,const char* b){return std::strcmp(a,b)<0;});
 return p!=keys+N && !std::strcmp(*p,key);
}
bool Variant(const pipelines::Draw& d) {
 if(d.vertex!=pipelines::VertexProgram::Translated || d.fragment!=pipelines::FragmentProgram::Translated ||
    !d.vertexRecord || !d.fragmentRecord || !Covered(d.vertexRecord->md5,coverage::vertex) || !Covered(d.fragmentRecord->md5,coverage::endpoints))return false;
 for(const auto* r:{d.vertexRecord,d.fragmentRecord}){
  const size_t len=std::strlen(r->spv);if(len<4 || std::strcmp(r->spv+len-4,".spv"))return false;
  char path[96];int n=std::snprintf(path,sizeof path,"gbuffer/%.*s.gbuf.spv",static_cast<int>(len-4),r->spv);
  if(n<=0 || n>=static_cast<int>(sizeof path) || !shaders::Get(path))return false;
 }
 return true;
}
}
bool Available(){return Supported();}
bool Render() {
 const auto fi=frame::Slot();if(fi>=frame::kFramesInFlight)return false;
 auto& slot=slots[fi];slot.valid=0;
 if(frame::Target() || frame::QueryActive() || !Supported())return false;
 const auto serial=frame::Serial();const auto* batch=hdcapture::Ready(serial);const auto* scene=frame::Scene();
 if(!batch || !scene || !scene->extent.width || !scene->extent.height || slot.used>frame::Completed() || slot.used==serial)return false;
 // Resolve all resources before entering the raw pass; never record uploads in a render pass.
 size_t producers=0;for(size_t i=0;i<batch->Count();++i){pipelines::Draw d;if(!hdcapture::Read(i,serial,d))return false;if(Variant(d))++producers;}
 if(!producers)return false;
 if(!slot.fb || slot.extent.width!=scene->extent.width || slot.extent.height!=scene->extent.height){
  Drop(slot);if(!Make(slot,scene->extent)){Drop(slot);return false;}}
 auto cmd=frame::Outside();if(!cmd)return false;const auto* h=context::Get();slot.used=serial;
 VkClearValue clear[7]{};clear[6].depthStencil={1.0f,0};
 VkRenderPassBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;begin.renderPass=slot.pass;
 begin.framebuffer=slot.fb;begin.renderArea.extent=slot.extent;begin.clearValueCount=7;begin.pClearValues=clear;
 pipelines::ForgetBindings();h->dapi->CmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
 bool complete=true;
 for(size_t i=0;i<batch->Count();++i){pipelines::Draw d;
  if(!hdcapture::Read(i,serial,d)){complete=false;break;}
  d.mirror=false;d.geometry=Variant(d)?2:1;
  float pixels[pipelines::kPixelRows*4]{};
  if(d.geometry==2) {
   if(!d.pixelRegs){complete=false;break;}
   std::memcpy(pixels,d.pixelRegs,sizeof pixels);
   // p14 is the supplied geometry-only ownership row. Original draw constants
   // remain immutable. Conservatively tag every skinned model as moving;
   // this includes units/equipment without relying on modern texture heuristics.
   pixels[56]=(d.attribMask & (1u<<2))?1.0f:0.0f;
   pixels[57]=d.geometryInterior?1.0f:0.0f;pixels[58]=static_cast<float>(HD::VulkanShadows::Bridge::Mask(d,serial));
   pixels[59]=HD::VulkanShadows::Bridge::serial==serial?65536.0f+HD::VulkanShadows::Bridge::Mask(d,serial,true):0.0f;
   d.pixelRegs=pixels;
  }
  if(!pipelines::Record(cmd,slot.pass,frame::PassGeneration(),serial,fi,d)){complete=false;break;}
 }
 h->dapi->CmdEndRenderPass(cmd);pipelines::ForgetBindings();
 for(auto& image:slot.colour)image.layout=VK_IMAGE_LAYOUT_GENERAL;
 if(!complete)return false;slot.valid=serial;return true;
}
bool Get(uint64_t serial,HD::VulkanShadows::GeometryInputs& out) {
 out={};if(!serial || frame::Slot()>=frame::kFramesInFlight)return false;
 const auto& slot=slots[frame::Slot()];if(slot.valid!=serial || slot.used!=serial)return false;
 out.valid=1;out.serial=serial;out.shadowEndpointContract=0x53484434;
 out.normalView=slot.colour[2].view;out.shadowLowView=slot.colour[4].view;out.shadowHighView=slot.colour[5].view;
 out.shadowEndpointFormat=VK_FORMAT_R16G16B16A16_SFLOAT;out.layout=VK_IMAGE_LAYOUT_GENERAL;out.extent=slot.extent;return true;
}
bool GetGBuffer(uint64_t serial,GBuffer& out) {
 out={};if(!serial || frame::Slot()>=frame::kFramesInFlight)return false;
 const auto& slot=slots[frame::Slot()];if(slot.valid!=serial || slot.used!=serial)return false;
 out.albedo=slot.colour[1].view;out.normal=slot.colour[2].view;out.light=slot.colour[3].view;out.shadowLow=slot.colour[4].view;out.shadowHigh=slot.colour[5].view;
 out.albedoFormat=formats[1];out.normalFormat=formats[2];out.lightFormat=formats[3];out.shadowFormat=formats[4];
 out.layout=VK_IMAGE_LAYOUT_GENERAL;out.extent=slot.extent;out.serial=serial;return true;
}
bool Shutdown() {
 uint64_t last=0;for(const auto& slot:slots)last=std::max(last,slot.used);
 if(last>frame::Completed()){frame::Flush();if(!frame::WaitSerial(last))return false;}
 for(auto& slot:slots)Drop(slot);probeDevice=VK_NULL_HANDLE;probed=capable=false;return true;
}
}
