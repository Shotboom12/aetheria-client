// Isolated review candidate using the native HdVulkanGrade upload precedent.
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdForeverWaterImages.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include <algorithm>
#include <cstring>

namespace HD::ForeverNative {
namespace api = gxvk::vk::hdeffect;
namespace texture = gxvk::vk::texture;
namespace {
constexpr uint64_t kOwnerCap = 64ull * 1024 * 1024;
constexpr uint64_t kCpuBound = 8ull * 1024 * 1024; // bounded outputs, ripple scratch, FFT scratch/spectra and metadata
bool AddRequirement(uint64_t& sum, const VkMemoryRequirements& req, VkDeviceSize granularity) {
    if (!req.size || !req.alignment) return false;
    // Worst-case placement padding, even though the shared allocator can often
    // reuse a span. This intentionally errs high for mixed buffer/image heaps.
    const uint64_t pad = uint64_t(req.alignment - 1) + uint64_t(granularity - 1);
    if (uint64_t(req.size) > UINT64_MAX - pad || sum > UINT64_MAX - uint64_t(req.size) - pad) return false;
    sum += uint64_t(req.size) + pad;
    return true;
}
}
bool PreflightFieldOwner64MiB(uint64_t& accountedBytes) {
    accountedBytes = 0;
    const auto* host = api::Get();
    api::DeviceInfo info{}; info.structSize = sizeof info;
    if (!host || !host->GetDeviceInfo || !host->GetDeviceInfo(&info) || !info.device || !info.physicalDevice ||
        !info.getDeviceProcAddr || !info.getInstanceProcAddr || info.frameSlots != 2) return false;
    auto gd = info.getDeviceProcAddr;
    auto gi = info.getInstanceProcAddr;
    auto createImage = reinterpret_cast<PFN_vkCreateImage>(gd(info.device, "vkCreateImage"));
    auto imageReq = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(gd(info.device, "vkGetImageMemoryRequirements"));
    auto destroyImage = reinterpret_cast<PFN_vkDestroyImage>(gd(info.device, "vkDestroyImage"));
    auto createBuffer = reinterpret_cast<PFN_vkCreateBuffer>(gd(info.device, "vkCreateBuffer"));
    auto bufferReq = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(gd(info.device, "vkGetBufferMemoryRequirements"));
    auto destroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(gd(info.device, "vkDestroyBuffer"));
    auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gi(info.instance, "vkGetPhysicalDeviceProperties"));
    if (!createImage || !imageReq || !destroyImage || !createBuffer || !bufferReq || !destroyBuffer || !getProperties) return false;
    VkPhysicalDeviceProperties properties{}; getProperties(info.physicalDevice, &properties);
    const auto granularity = properties.limits.bufferImageGranularity;
    if (!granularity) return false;
    uint64_t deviceBytes = 0, hostBytes = 0;
    // Maximum retained owner: four keys, two frame slots, four 64² full-mip
    // cascades and one one-level 256² ripple per key. GPU and staging each count.
    constexpr uint32_t entries = 4, slots = 2, cascades = 4;
    constexpr uint32_t fftSide = 64, fftMips = 7, rippleSide = 256;
    for (uint32_t entry = 0; entry < entries; ++entry) for (uint32_t slot = 0; slot < slots; ++slot) {
        for (uint32_t field = 0; field < cascades + 1; ++field) {
            const bool ripple = field == cascades;
            const uint32_t side = ripple ? rippleSide : fftSide;
            const uint32_t levels = ripple ? 1 : fftMips;
            VkImageCreateInfo image{}; image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            image.extent = {side, side, 1}; image.mipLevels = levels; image.arrayLayers = 1;
            image.samples = VK_SAMPLE_COUNT_1_BIT; image.tiling = VK_IMAGE_TILING_OPTIMAL;
            image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            image.sharingMode = VK_SHARING_MODE_EXCLUSIVE; image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImage handle = VK_NULL_HANDLE;
            if (createImage(info.device, &image, nullptr, &handle) != VK_SUCCESS || !handle) return false;
            VkMemoryRequirements req{}; imageReq(info.device, handle, &req); destroyImage(info.device, handle, nullptr);
            if (!AddRequirement(deviceBytes, req, granularity)) return false;
            VkDeviceSize bytes = ripple ? VkDeviceSize(rippleSide) * rippleSide * 8 : 0;
            if (!ripple) for (uint32_t mip = 0; mip < fftMips; ++mip) {
                const uint32_t n = fftSide >> mip; bytes += VkDeviceSize(n) * n * 8;
            }
            VkBufferCreateInfo buffer{}; buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer.size = bytes; buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VkBuffer bufferHandle = VK_NULL_HANDLE;
            if (createBuffer(info.device, &buffer, nullptr, &bufferHandle) != VK_SUCCESS || !bufferHandle) return false;
            VkMemoryRequirements bufferMemory{}; bufferReq(info.device, bufferHandle, &bufferMemory); destroyBuffer(info.device, bufferHandle, nullptr);
            if (!AddRequirement(hostBytes, bufferMemory, granularity)) return false;
        }
    }
    // The verified allocator uses 16 MiB DeviceLocal and 8 MiB HostVisible
    // blocks. Round each aggregate requirement up to whole blocks, which
    // charges the owner for allocator slack rather than only logical payload.
    constexpr uint64_t deviceBlock = 16ull * 1024 * 1024, hostBlock = 8ull * 1024 * 1024;
    const uint64_t deviceReserved = ((deviceBytes + deviceBlock - 1) / deviceBlock) * deviceBlock;
    const uint64_t hostReserved = ((hostBytes + hostBlock - 1) / hostBlock) * hostBlock;
    if (deviceReserved > UINT64_MAX - hostReserved || deviceReserved + hostReserved > UINT64_MAX - kCpuBound) return false;
    const uint64_t total = deviceReserved + hostReserved + kCpuBound;
    accountedBytes = total;
    return total <= kOwnerCap;
}
bool SampledField::Initialize(const api::Api* host) {
    if (api_) return api_ == host;
    if (!host || host->apiVersion != api::ApiVersion || host->structSize < sizeof(api::Api) ||
        !host->GetDeviceInfo || !host->CreateBuffer || !host->RetireBuffer || !host->FlushBuffer) return false;
    api::DeviceInfo info{}; info.structSize = sizeof info;
    if (!host->GetDeviceInfo(&info) || !info.device || !info.getDeviceProcAddr || !info.getInstanceProcAddr ||
        !info.frameSlots || info.frameSlots > slots_.size()) return false;
    const auto formats = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(info.getInstanceProcAddr(info.instance, "vkGetPhysicalDeviceFormatProperties"));
    if (!formats || !info.physicalDevice) return false;
    VkFormatProperties properties{}; formats(info.physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
    const auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & required) != required) return false;
    auto barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(info.getDeviceProcAddr(info.device, "vkCmdPipelineBarrier"));
    auto copy = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(info.getDeviceProcAddr(info.device, "vkCmdCopyBufferToImage"));
    if (!barrier || !copy) return false;
    device_ = info; barrier_ = barrier; copy_ = copy; api_ = host;
    return true;
}
bool SampledField::Upload(const api::StageContext& ctx, const std::vector<FloatMip>& levels) {
    if (!api_ || ctx.structSize < sizeof ctx || !ctx.cmd || !ctx.serial || ctx.frameSlots != device_.frameSlots ||
        ctx.frameSlot >= device_.frameSlots || levels.empty() || levels.size() > 10) return false;
    if (ctx.serial != gxvk::vk::frame::Serial() || ctx.frameSlot != gxvk::vk::frame::Slot() ||
        ctx.completedSerial > gxvk::vk::frame::Completed()) return false;
    if (gxvk::vk::frame::Target() || gxvk::vk::frame::QueryActive()) return false;
    auto& slot = slots_[ctx.frameSlot];
    if (slot.used == ctx.serial) return slot.valid == ctx.serial; // immutable within a frame
    if (slot.used > ctx.completedSerial || ctx.serial < slot.used) return false;
    slot.valid = 0;
    const auto width = levels.front().width, height = levels.front().height;
    if (!width || !height || width > 512 || height > 512) return false;
    std::array<VkBufferImageCopy, 10> copies{};
    VkDeviceSize total = 0;
    for (size_t i = 0; i < levels.size(); ++i) {
        const auto w = std::max(1u, width >> i), h = std::max(1u, height >> i);
        if (i && levels[i-1].width == 1 && levels[i-1].height == 1) return false;
        if (levels[i].width != w || levels[i].height != h || levels[i].rgba.size() != size_t(w) * h * 4) return false;
        auto& region = copies[i]; region.bufferOffset = total;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(i), 0, 1};
        region.imageExtent = {w, h, 1};
        total += VkDeviceSize(levels[i].rgba.size()) * sizeof(uint16_t);
    }
    if (!total || total > 8u * 1024u * 1024u) return false;
    const bool resize = slot.image.extent.width != width || slot.image.extent.height != height || slot.image.levels != levels.size();
    if (resize) {
        gxvk::vk::frame::RetireImage(slot.image, slot.used);
        slot.image = {};
    }
    if (!slot.image.handle && !texture::CreateMipped(width, height, uint32_t(levels.size()), VK_FORMAT_R16G16B16A16_SFLOAT, slot.image)) return false;
    if (slot.transfer.size < total || !slot.staging || !slot.transfer.mapped || !slot.transfer.buffer) {
        if (slot.staging) api_->RetireBuffer(slot.staging, slot.used);
        slot.staging = nullptr; slot.transfer = {};
        api::BufferDesc desc{total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, api::Host};
        slot.staging = api_->CreateBuffer(&desc, &slot.transfer);
        if (!slot.staging || !slot.transfer.mapped || !slot.transfer.buffer) {
            if (slot.staging) api_->RetireBuffer(slot.staging, 0);
            slot.staging = nullptr; slot.transfer = {}; return false;
        }
    }
    if (slot.image.layout != VK_IMAGE_LAYOUT_UNDEFINED && slot.image.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return false;
    auto* mapped = static_cast<uint8_t*>(slot.transfer.mapped);
    for (size_t i = 0; i < levels.size(); ++i)
        std::memcpy(mapped + copies[i].bufferOffset, levels[i].rgba.data(), levels[i].rgba.size() * sizeof(uint16_t));
    api_->FlushBuffer(slot.staging);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; host.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = slot.transfer.buffer; host.size = total;
    barrier_(ctx.cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);
    VkImageMemoryBarrier image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    image.image = slot.image.handle; image.oldLayout = slot.image.layout; image.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image.srcQueueFamilyIndex = image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, uint32_t(levels.size()), 0, 1};
    image.srcAccessMask = slot.image.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT;
    image.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    const auto before = slot.image.layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    slot.used = ctx.serial; // retire safely even if the frame is subsequently abandoned
    barrier_(ctx.cmd, before, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image);
    copy_(ctx.cmd, slot.transfer.buffer, slot.image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, uint32_t(levels.size()), copies.data());
    image.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; image.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; image.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier_(ctx.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image);
    slot.image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; slot.valid = ctx.serial;
    return true;
}
const texture::Image* SampledField::Get(uint32_t slot, uint64_t serial) const {
    if (!api_ || !serial || slot >= device_.frameSlots || slots_[slot].valid != serial) return nullptr;
    return &slots_[slot].image;
}
uint64_t SampledField::LastUsed() const {
    uint64_t last = 0; for (const auto& slot : slots_) last = std::max(last, slot.used); return last;
}
bool SampledField::Release(uint64_t completed) {
    if (!api_) return true;
    if (LastUsed() > completed) return false;
    for (auto& slot : slots_) {
        if (slot.staging) api_->RetireBuffer(slot.staging, slot.used);
        gxvk::vk::frame::RetireImage(slot.image, slot.used); slot = {};
    }
    api_ = nullptr; barrier_ = nullptr; copy_ = nullptr; device_ = {};
    return true;
}
void SampledField::RetireDeferred() {
    if (!api_) return;
    for (auto& slot : slots_) {
        if (slot.staging) api_->RetireBuffer(slot.staging, slot.used);
        gxvk::vk::frame::RetireImage(slot.image, slot.used); slot = {};
    }
    api_ = nullptr; barrier_ = nullptr; copy_ = nullptr; device_ = {};
}
}
