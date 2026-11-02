#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdDrawCapture.hpp"
namespace gxvk::vk::hdcapture {
// Render-thread only. Capture is requested explicitly for an opaque-world scope.
void BeginScene(bool requested);
void Capture(const pipelines::Draw& draw, uint64_t serial, bool sceneTarget, bool queryActive);
void EndScene(uint64_t serial);
void BeforeBufferMutation(VkBuffer buffer);
void BeforeImageMutation(uint32_t imageId);
void ResetDevice();
const Batch* Ready(uint64_t serial);
bool Read(size_t index, uint64_t serial, pipelines::Draw& out);
}
