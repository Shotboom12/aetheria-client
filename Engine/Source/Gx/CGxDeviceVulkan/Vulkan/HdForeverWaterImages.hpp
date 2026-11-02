// Isolated review candidate. Not promoted, compiled, tested or connected to a material.
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture.hpp"
#include <array>
#include <vector>

namespace HD::ForeverNative {
// Exact VkMemoryRequirements preflight for the maximum four-entry owner: four
// 64² FFT fields plus one 256² ripple field, two frame slots, and conservative
// allocator alignment/granularity padding. No memory is allocated by this call.
// Includes bounded CPU scratch/cache reserve; refuse the feature if the 64 MiB
// cap cannot be proven before the first SampledField::Upload().
bool PreflightFieldOwner64MiB(uint64_t& accountedBytes);
struct FloatMip {
    uint32_t width = 0, height = 0;
    std::vector<uint16_t> rgba; // tightly packed RGBA IEEE binary16, in native channel order
};
// Four cascades use four separate owners per profile; never reuse one owner for
// different profiles in a frame. Ripple image has its own independent owner.
class SampledField final {
public:
    SampledField() = default;
    SampledField(const SampledField&) = delete;
    SampledField& operator=(const SampledField&) = delete;
    bool Initialize(const gxvk::vk::hdeffect::Api* api);
    // Render thread, frame::Outside() at the owner-controlled pre-liquid stage.
    // A slot is immutable once published this serial. No queue submission or waits.
    bool Upload(const gxvk::vk::hdeffect::StageContext& frame, const std::vector<FloatMip>& levels);
    const gxvk::vk::texture::Image* Get(uint32_t slot, uint64_t serial) const;
    // Unregister external proxies first. Caller must establish completed >= LastUsed.
    bool Release(uint64_t completed);
    // Shutdown path: frame::RetireImage/RetireBuffer defer destruction until their serial completes.
    void RetireDeferred();
    uint64_t LastUsed() const;
private:
    struct Slot {
        gxvk::vk::texture::Image image{};
        void* staging = nullptr;
        gxvk::vk::hdeffect::BufferInfo transfer{};
        uint64_t used = 0, valid = 0;
    };
    std::array<Slot, 8> slots_{};
    const gxvk::vk::hdeffect::Api* api_ = nullptr;
    gxvk::vk::hdeffect::DeviceInfo device_{};
    PFN_vkCmdPipelineBarrier barrier_ = nullptr;
    PFN_vkCmdCopyBufferToImage copy_ = nullptr;
};
}
