#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdDrawCapture.hpp"
#include <cstring>
#include <new>
#include <utility>

namespace gxvk::vk::hdcapture {
namespace {
constexpr size_t HighRows = 192;
template<class Owner> bool Copy(const float* src, size_t count, Owner& dst) {
    if (!src) return true;
    dst.reset(static_cast<float*>(std::malloc(count * sizeof(float))));
    if (!dst) return false;
    std::memcpy(dst.get(), src, count * sizeof(float));
    return true;
}
}
bool Batch::Matches(uint64_t serial, uint64_t epoch) const {
    return serial != 0 && serial == serial_ && epoch == epoch_;
}
void Batch::Reset() {
    for (size_t i = 0; i < count_; ++i) draws_[i].~Saved();
    count_ = 0;
    bytes_ = 0; serial_ = epoch_ = 0; state_ = State::Empty;
}
void Batch::Release() { Reset(); std::free(draws_); draws_=nullptr; }
Batch::~Batch() { Release(); }
void Batch::Invalidate() {
    // No partial mirror may consume earlier entries after overflow or a resource hazard.
    state_ = State::Invalid;
}
void Batch::BeforeBufferMutation(VkBuffer buffer) {
    if (!buffer || (state_ != State::Recording && state_ != State::Sealed)) return;
    for (size_t entry = 0; entry < count_; ++entry) {
        const auto& saved = draws_[entry];
        const auto& d = saved.draw;
        if (d.indexed && d.indexBuffer == buffer) { Invalidate(); return; }
        for (uint32_t i = 0; i < d.streamCount; ++i)
            if (d.streams[i].buffer == buffer) { Invalidate(); return; }
    }
}
void Batch::BeforeImageMutation(uint32_t id) {
    if (!id || (state_ != State::Recording && state_ != State::Sealed)) return;
    for (size_t entry = 0; entry < count_; ++entry)
        for (uint32_t i = 0; i < pipelines::kTextureStages; ++i)
            if ((draws_[entry].draw.textureMask & (1u << i)) && draws_[entry].draw.textures[i].imageId == id) {
                Invalidate(); return;
            }
}
void Batch::Begin(uint64_t serial, uint64_t epoch) {
    Reset();
    if (!serial) { Invalidate(); return; }
    serial_ = serial; epoch_ = epoch; state_ = State::Recording;
}
bool Batch::Append(const pipelines::Draw& d, uint64_t serial, uint64_t epoch) {
    if (state_ != State::Recording) return false;
    if (!Matches(serial, epoch) || count_ >= MaxDraws ||
        d.streamCount > pipelines::kMaxStreams || d.vertexRows > pipelines::kVertexRowsXL) {
        Invalidate(); return false;
    }
    // The native uniform writer accepts exactly these three vertex-file layouts.
    if (d.vertexRows != pipelines::kVertexRowsSmall && d.vertexRows != pipelines::kVertexRowsLarge &&
        d.vertexRows != pipelines::kVertexRowsXL) { Invalidate(); return false; }
    const size_t floats = (d.vertexRegs ? size_t(d.vertexRows) * 4 : 0) +
        (d.pixelRegs ? pipelines::kPixelRows * 4 : 0) + (d.hdPixelHighRegs ? HighRows * 4 : 0);
    const size_t needed = floats * sizeof(float);
    if (needed > MaxConstantBytes - bytes_) { Invalidate(); return false; }
    Saved saved;
    saved.draw = d;
    if (!Copy(d.vertexRegs, size_t(d.vertexRows) * 4, saved.vertex) ||
        !Copy(d.pixelRegs, pipelines::kPixelRows * 4, saved.pixel) ||
        !Copy(d.hdPixelHighRegs, HighRows * 4, saved.high)) { Invalidate(); return false; }
    // Never retain the caller's stack pixel rows or mutable vertex/pixel shadow pointers.
    saved.draw.vertexRegs = saved.vertex.get();
    saved.draw.pixelRegs = saved.pixel.get();
    saved.draw.hdPixelHighRegs = saved.high.get();
    // The client's global throwing AND nothrow new terminate on OOM. Optional capture must
    // decline instead, so its fixed-capacity table and constants use checked malloc.
    if (!draws_) draws_ = static_cast<Saved*>(std::malloc(MaxDraws * sizeof(Saved)));
    if (!draws_) { Invalidate(); return false; }
    new (&draws_[count_]) Saved(std::move(saved));
    ++count_;
    bytes_ += needed;
    return true;
}
bool Batch::Seal(uint64_t serial, uint64_t epoch) {
    if (state_ != State::Recording) return false;
    if (!Matches(serial, epoch)) { Invalidate(); return false; }
    state_ = State::Sealed;
    return true;
}
bool Batch::Read(size_t index, uint64_t serial, uint64_t epoch, pipelines::Draw& out) const {
    if (state_ != State::Sealed || !Matches(serial, epoch) || index >= count_) return false;
    out = draws_[index].draw;
    return true;
}
}
