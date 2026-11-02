#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture.hpp"
namespace gxvk::vk::hdmirror {
// Render-thread only. Never reports a partial or previous frame as a valid mirror.
bool Render(const float view[16], const float projection[16], float height, float eyeZ, const texture::Image* sky=nullptr, const float* fogColor=nullptr);
const texture::Image* Get();
struct Snapshot {
    const texture::Image* image = nullptr;
    uint64_t serial = 0;
    float height = 0;
    float view[16]{}, projection[16]{};
    VkExtent2D sourceExtent{};
};
// New proposed contract: consumer must compare the production plane/camera and serial.
bool GetSnapshot(uint64_t serial, Snapshot& out);
void Invalidate();
void Shutdown(); // After frame idle, before image allocator/context shutdown.
}
