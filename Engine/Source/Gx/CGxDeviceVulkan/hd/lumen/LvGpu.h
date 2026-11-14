// Native Vulkan executor for the isolated Lumenveil source port. No queue submission or GPU wait.
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/compat/wxl/VkRenderApi.h" // Aetheria: the subset Mado's wxl header gave, implemented by hd/HdWxlShim
#include <array>
#include <cstdint>
#include <cstddef>
namespace HD::Lumen {
enum class Kernel:uint32_t {LampSurfaces,LampVisibility,LampHalos,LampBlur,FogTemporal,EdgeOccluders,MemoryKeep,EffectGather,EffectReduce,Fog1Memory0,Fog1Memory1,Fog2Memory0,Fog2Memory1,Fog3Memory0,Fog3Memory1,MemorySeed,MemoryResolve,MemoryScatter,MemoryRealign,MemoryFill,DepthCopy,SceneComposite,Count};
// HD G1 2026-11-05: GPU time of each kernel (timestamps), only while hdLvGpuTimes is 1 (LvClient.cpp). Nothing on screen changes.
namespace GpuTiming {inline bool on=false,available=false;inline double ms[size_t(Kernel::Count)]{};inline uint64_t runs[size_t(Kernel::Count)]{};inline uint64_t frames=0,barriers=0,dispatches=0,surfacesSkipped=0;}
struct alignas(16) Uniforms {std::array<std::array<float,4>,128> r{};};
static_assert(sizeof(Uniforms)==2048,"source c0..c127 register contract");
struct Texture {VkImageView view{};VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL;};
struct Target {void* handle=nullptr;WXL_VkImageInfo info{};VkExtent2D extent{};VkFormat format=VK_FORMAT_UNDEFINED;VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;uint64_t last=0; uint32_t depth=1,mipLevels=1; VkDeviceMemory volumeMemory{}; bool readReady=false; /* HD 2026-11-05 lot L21: the last barrier left it GENERAL for shader reads, nothing wrote it since */};
struct Readback {VkBuffer buffer{};VkDeviceMemory memory{};void*mapped=nullptr;VkDeviceSize bytes=0,usedBytes=0;uint64_t serial=0,last=0;};
class Gpu {
public:
 static constexpr uint32_t Dispatches=48,Slots=8;
 bool Init(const WXL_VkRenderApi*);
 bool Create(Target&,uint32_t,uint32_t,VkFormat,uint32_t mips=1);
 bool CreateVolume(Target&,uint32_t side,VkFormat,uint32_t mips=1);
 void Retire(Target&);
 bool Begin(const WXL_VkStageContext&);
 bool Run(Kernel,const Uniforms&,const std::array<Texture,16>&,Target&,Target* second=nullptr,VkExtent2D dispatch={});
 bool Upload(Target&,const void*,size_t);
 bool CopyFrame(Target&);
 bool CopyFrameRegion(Target&,VkRect2D);
 bool Copy(Target& source,Target& dest);
 bool UploadMip(Target&,uint32_t level,const void*,size_t);
 bool CreateReadback(Readback&,VkDeviceSize);
 bool CopyReadback(Target&,Readback&);
 const void* Ready(const Readback&,uint64_t completed)const;
 bool Consume(Readback&,uint64_t completed);
 bool DestroyReadback(Readback&,uint64_t completed);
 bool Clear(Target&,std::array<float,4>);
 Texture Sample(Target&);
 bool Shutdown(uint64_t completed);
 const char* Error()const;
 uint64_t LastUsed()const;
 const WXL_VkStageContext* Context()const;
 Gpu()=default;Gpu(const Gpu&)=delete;Gpu&operator=(const Gpu&)=delete;
private:struct Impl;Impl*s_=nullptr;const char*lastError_="not initialized";
};
}
