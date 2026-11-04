// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: NVIDIA DLSS Super Resolution (NGX SDK 310.9.16, ThirdParty/DLSS) in the place of FSR on the native upscaler seam, Azeroth
// Remastered only. Follows Mado's hd/dlss/HdDlss.cpp (GPL-3.0-or-later) on Aetheria's device: the same inputs HdNativeFsr already makes
// (render-size colour, normalised depth R32F with 0 near, camera + object motion RG16F as previous minus current UV without jitter,
// the measured jitter) and a display-size RGBA8 storage output. Sharpening is FSR's RCAS (hd/dlss/shaders/HdDlssRcas).
// cvars: hdDlss 0/1 (default 0; takes FSR's place, FSR frame-less), hdDlssSharpness 0..2, hdDlssPreset, hdDlssLog, status hdDlssInfo.
// Built only with -DAETHERIA_NATIVE_DLSS=ON (needs AETHERIA_NATIVE_FSR). nvngx_dlss.dll is loaded from "<exe folder>\DLSS".
#pragma once
#ifdef AETHERIA_NATIVE_DLSS
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Vk.hpp"
#include <cstdint>
#include <vector>
namespace HD::NativeDlss {
struct Image { VkImage image = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkFormat format = VK_FORMAT_UNDEFINED; uint32_t width = 0, height = 0; };
struct Frame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    Image color, depth, motion, output;  // colour/depth/motion SHADER_READ_ONLY_OPTIMAL, output GENERAL
    uint32_t quality = 1;                // hdFsrQuality 1 quality / 2 balanced / 3 performance / 4 ultra performance
    float jitterX = 0, jitterY = 0;      // render pixels
    float mvScaleX = 1, mvScaleY = 1;    // UV motion to render pixels
    float milliseconds = 16.6667f;
    bool reset = false;
    float sharpness = 0;                 // 0..2
    uint32_t slot = 0;
    uint64_t serial = 0;
};
// Where the Vulkan instance / device are created (Context/Instance.cpp, Context/Logical.cpp): adds the extensions NGX asks for.
void AddInstanceExtensions(const std::vector<VkExtensionProperties>& available, std::vector<const char*>& names);
void AddDeviceExtensions(PFN_vkEnumerateDeviceExtensionProperties enumerate, VkInstance instance, VkPhysicalDevice physical,
                         const char** names, uint32_t& count, uint32_t capacity);
bool Requested();                        // hdDlss 1 (registers the cvars on first use)
void Register();
float Sharpness();
// The render size NVIDIA chooses for this display and quality; false: DLSS cannot run (hdDlssInfo says why) and FSR is used.
bool RenderSize(uint32_t displayWidth, uint32_t displayHeight, uint32_t quality, uint32_t& width, uint32_t& height);
// 1 upscaled into frame.output; 0 feature created this frame (stretch); -1 failure.
int Evaluate(const Frame& frame);
void Drop();      // hdDlss off: release the feature after a device drain
void Shutdown();  // device idle: feature, parameters, NGX
}
#else
namespace HD::NativeDlss {
inline void Register() {}
inline bool Requested() { return false; }
inline void Drop() {}
inline void Shutdown() {}
}
#endif
