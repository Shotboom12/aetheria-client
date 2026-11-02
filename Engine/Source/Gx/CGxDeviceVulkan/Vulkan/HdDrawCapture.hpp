// Aetheria native replay preparation. No WXL ABI and no automatic GPU replay.
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Pipelines.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <cstdlib>

namespace gxvk::vk::hdcapture {
// CPU constants are owned; GPU geometry, texture views and manifest records are borrowed.
// The owner must invalidate on any destructive resource mutation before replay.
class Batch final {
public:
    enum class State { Empty, Recording, Sealed, Invalid };
    static constexpr size_t MaxDraws = 8192;
    static constexpr size_t MaxConstantBytes = 64u * 1024u * 1024u;
    Batch() = default;
    ~Batch();
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
    void Begin(uint64_t serial, uint64_t resourceEpoch);
    bool Append(const pipelines::Draw& draw, uint64_t serial, uint64_t resourceEpoch);
    bool Seal(uint64_t serial, uint64_t resourceEpoch);
    void Invalidate();
    // Call BEFORE an in-place write, image replacement or destruction. Conservative: any
    // reference to the resource invalidates the whole mirror, never a partial reflected world.
    void BeforeBufferMutation(VkBuffer buffer);
    void BeforeImageMutation(uint32_t imageId);
    void Reset();
    void Release(); // Device shutdown: also free the fixed-capacity metadata table.
    // Returns an independent Draw value with pointers into this batch. Valid only until Reset,
    // Begin, destruction or invalidation. No cross-frame use, even if GPU handles still exist.
    bool Read(size_t index, uint64_t serial, uint64_t resourceEpoch, pipelines::Draw& out) const;
    size_t Count() const { return state_ == State::Sealed ? count_ : 0; }
    size_t ConstantBytes() const { return bytes_; }
    State Status() const { return state_; }
private:
    struct Free { void operator()(float* p) const { std::free(p); } };
    using Floats = std::unique_ptr<float, Free>;
    struct Saved {
        pipelines::Draw draw;
        Floats vertex, pixel, high;
    };
    Saved* draws_ = nullptr;
    size_t count_ = 0;
    uint64_t serial_ = 0, epoch_ = 0;
    size_t bytes_ = 0;
    State state_ = State::Empty;
    bool Matches(uint64_t serial, uint64_t epoch) const;
};
}
