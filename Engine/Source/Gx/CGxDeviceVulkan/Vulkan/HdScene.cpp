#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdScene.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"

namespace gxvk::vk::hdscene
{
    namespace
    {
        void Barrier(const loader::Device& api, VkCommandBuffer cmd, const texture::Image& image,
                     VkImageLayout from, VkImageLayout to, VkPipelineStageFlags src, VkAccessFlags read,
                     VkPipelineStageFlags dst, VkAccessFlags write)
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = read; b.dstAccessMask = write;
            b.oldLayout = from; b.newLayout = to;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image.handle; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            api.CmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
        }
    }

    bool Supported()
    {
        const auto* h = context::Get();
        if (!h || !h->iapi || !h->dapi || !h->dapi->CmdBlitImage || !h->dapi->CmdPipelineBarrier) return false;
        // Queue/format properties are fixed for a physical adapter. Do not re-query the driver for
        // every post-processing stage each frame. Device and adapter changes invalidate this cache.
        static VkDevice device = VK_NULL_HANDLE;
        static VkPhysicalDevice physical = VK_NULL_HANDLE;
        static uint32_t family = UINT32_MAX;
        static bool known = false, available = false;
        if (known && device == h->device && physical == h->physical && family == h->graphicsFamily) return available;
        device = h->device; physical = h->physical; family = h->graphicsFamily; known = true; available = false;
        uint32_t count = 0;
        h->iapi->GetPhysicalDeviceQueueFamilyProperties(h->physical, &count, nullptr);
        VkQueueFamilyProperties families[64]{};
        if (!count || count > 64 || h->graphicsFamily >= count) return false;
        h->iapi->GetPhysicalDeviceQueueFamilyProperties(h->physical, &count, families);
        if (!(families[h->graphicsFamily].queueFlags & VK_QUEUE_COMPUTE_BIT)) return false;
        VkFormatProperties rgba{}, bgra{};
        h->iapi->GetPhysicalDeviceFormatProperties(h->physical, VK_FORMAT_R8G8B8A8_UNORM, &rgba);
        h->iapi->GetPhysicalDeviceFormatProperties(h->physical, VK_FORMAT_B8G8R8A8_UNORM, &bgra);
        const VkFormatFeatureFlags blit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
        available = (rgba.optimalTilingFeatures & (blit | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) == (blit | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
            && (bgra.optimalTilingFeatures & blit) == blit;
        return available;
    }

    bool Apply(texture::Image& scratch, Record record, void* user, bool writeBack)
    {
        if (!record || !Supported() || frame::Target() || frame::QueryActive()) return false;
        const auto* scene = frame::Scene();
        if (!scene || !scene->handle || !scratch.handle || scratch.handle == scene->handle
            || scratch.format != VK_FORMAT_R8G8B8A8_UNORM || scratch.cube || scratch.levels != 1 || scratch.layers != 1
            || scene->format != VK_FORMAT_B8G8R8A8_UNORM || !scene->extent.width || !scene->extent.height
            || scene->extent.width != scratch.extent.width || scene->extent.height != scratch.extent.height) return false;
        // Outside closes the world pass. QueryActive was checked because Outside can otherwise return
        // the upload pre-pass, which is ordered BEFORE the world and cannot read its finished pixels.
        VkCommandBuffer cmd = frame::Outside();
        if (!cmd) return false;
        const auto* h = context::Get();
        if (!h || scene->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return false;
        const VkRect2D rect{{0,0}, scene->extent};
        if (!frame::CopyFromScene(scratch, 0, 0, rect, rect, false)) return false;
        const auto& api = *h->dapi;
        Barrier(api, cmd, scratch, scratch.layout, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        scratch.layout = VK_IMAGE_LAYOUT_GENERAL;
        const Context ctx{cmd, scratch.handle, scratch.view, scratch.extent, frame::Slot(), frame::Serial(), frame::Completed()};
        const bool recorded = record(ctx, user);
        Barrier(api, cmd, scratch, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        if (recorded && writeBack)
        {
            Barrier(api, cmd, *scene, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkImageBlit region{};
            region.srcSubresource = region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            region.srcOffsets[1] = region.dstOffsets[1] = {int32_t(scene->extent.width), int32_t(scene->extent.height), 1};
            api.CmdBlitImage(cmd, scratch.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             scene->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
            Barrier(api, cmd, *scene, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
        }
        Barrier(api, cmd, scratch, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT);
        scratch.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return recorded;
    }
}
