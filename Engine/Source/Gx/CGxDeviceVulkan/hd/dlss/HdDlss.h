// SPDX-License-Identifier: GPL-3.0-or-later
// HD 2026-11-02 (DLSS): NVIDIA DLSS Super Resolution (SDK 310.9.16) in place of the FSR upscaler.
// It reuses everything the FSR pass already makes (hd/fsr/HdFsrPass.cpp): the world at the render size, the
// normalized depth (R32F, 0 near), the motion vectors (RG16F, previous minus current UV, no jitter), the measured
// jitter and the output image (RGBA8, storage). Frame generation stays FSR's (DLSS FG needs an RTX 40).
// Render thread only. vulkan.h must already be included (the wxl headers first, as in HdFsrPass.cpp).
#pragma once
#include <cstdint>
namespace HD::Dlss {
struct Image {VkImage image=VK_NULL_HANDLE;VkImageView view=VK_NULL_HANDLE;VkFormat format=VK_FORMAT_UNDEFINED;uint32_t width=0,height=0;};
struct Frame {
    VkCommandBuffer cmd=VK_NULL_HANDLE;
    Image color,depth,motion,output;   // color/depth/motion SHADER_READ_ONLY_OPTIMAL, output GENERAL (NVIDIA's states)
    uint32_t quality=1;                // hdFSRQuality: 0 DLAA, 1 Quality, 2 Balanced, 3 Performance
    float jitterX=0,jitterY=0;         // render pixels, the convention the FSR pass hands to FSR
    float mvScaleX=1,mvScaleY=1;       // UV motion to render pixels
    float milliseconds=16.6667f;
    bool reset=false;
    float sharpness=0;                 // hdDLSSSharpness 0..2: RCAS after DLSS (0..1 FSR's curve, above 1 reinforced); 0 = none
    uint32_t slot=0;                   // present::Slot(): the frame in flight (one descriptor set each)
};
// The render size NVIDIA chooses for this display size and quality (its own table). False: DLSS cannot run now
// (hdDLSSInfo says why) and the caller uses FSR.
bool RenderSize(uint32_t displayWidth,uint32_t displayHeight,uint32_t quality,uint32_t& width,uint32_t& height);
// 1: the upscale is recorded into frame.output. 0: the DLSS feature was created in this command buffer, nothing to
// show yet (plain stretch this frame). -1: failure (logged, plain stretch).
int Evaluate(const Frame& frame);
// hdDLSS unticked (or upscaling off): the feature is released after a device drain, a refused one may be retried.
void Drop();
// Device idle, before the device goes (HdFsrPass Shutdown): feature, parameters, NGX itself.
void Shutdown();
}
