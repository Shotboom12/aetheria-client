// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: the upscaler seam of the render-scale world (FSR/DLSS device tier, step F2). See Frame.hpp (SetUpscaler).
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame/Internal.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Jitter.hpp"

namespace gxvk::vk::frame
{
    namespace
    {
        using namespace detail;

        void Barrier(VkCommandBuffer cmd, const texture::Image& img, VkImageLayout from, VkImageLayout to,
                     VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess)
        {
            VkImageMemoryBarrier b = {};
            b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask       = srcAccess;
            b.dstAccessMask       = dstAccess;
            b.oldLayout           = from;
            b.newLayout           = to;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image               = img.handle;
            b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, img.levels, 0, img.layers };
            s.dapi->CmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
        }

        struct Pending
        {
            UpscaleInputs in;
            UpscaleRecord record = nullptr;
            void*         user   = nullptr;
        };

        bool WithDepth(VkCommandBuffer cmd, VkImageView depth, VkFormat format, VkImageLayout layout, void* user)
        {
            Pending& p = *static_cast<Pending*>(user);
            if (cmd != p.in.cmd) return false;
            p.in.depthView = depth; p.in.depthFormat = format; p.in.depthLayout = layout;
            return p.record(p.in, p.user);
        }
    }

    void SetUpscaler(UpscaleRecord record, void* user)
    {
        s.upscaleRecord = record;
        s.upscaleUser   = record ? user : nullptr;
    }
}

namespace gxvk::vk::frame::detail
{
    // Called by EndWorldScale with the world pass ended and worldActive still set. True: the full scene now holds the upscaled
    // world (the caller does not stretch). False: nothing was written to the scene; the caller falls back to the bilinear stretch.
    bool RunUpscaler()
    {
        if (!s.upscaleRecord || !s.open || s.worldScene.handle == VK_NULL_HANDLE || s.scene.handle == VK_NULL_HANDLE) return false;
        texture::Image& world = s.worldScene;
        texture::Image& full  = s.scene;
        if (world.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || full.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return false;

        texture::Image& out = s.upscaleOut;
        if (out.handle != VK_NULL_HANDLE && (out.extent.width != full.extent.width || out.extent.height != full.extent.height))
            gxvk::vk::frame::RetireImage(out, s.serial);
        if (out.handle == VK_NULL_HANDLE && !texture::CreateStorage(full.extent.width, full.extent.height, VK_FORMAT_R8G8B8A8_UNORM, out)) return false;

        VkCommandBuffer cmd = gxvk::vk::frame::Outside();
        if (!cmd) return false;
        constexpr VkPipelineStageFlags kAll = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        Barrier(cmd, out, out.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? out.layout : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                kAll, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        out.layout = VK_IMAGE_LAYOUT_GENERAL;

        Pending p;
        p.record = s.upscaleRecord; p.user = s.upscaleUser;
        p.in.cmd = cmd; p.in.serial = s.serial; p.in.completed = s.completed; p.in.slot = s.fi;
        p.in.colorImage = world.handle; p.in.colorView = world.view; p.in.colorFormat = world.format; p.in.colorLayout = world.layout;
        p.in.renderExtent = world.extent;
        p.in.outputImage = out.handle; p.in.outputView = out.view; p.in.outputFormat = out.format; p.in.displayExtent = out.extent;
        gxvk::vk::jitter::Current(p.in.jitter[0], p.in.jitter[1]);
        const bool ok = gxvk::vk::frame::WithSceneDepth(WithDepth, &p); // worldActive: the depth is the world image's

        // The result to the scene (RGBA -> BGRA by the blit, as hdscene::Apply writes back), and the output image to its rest layout.
        Barrier(cmd, out, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        if (ok)
        {
            Barrier(cmd, full, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kAll,
                    VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkImageBlit region = {};
            region.srcSubresource = region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.srcOffsets[1] = region.dstOffsets[1] = { static_cast<int32_t>(full.extent.width), static_cast<int32_t>(full.extent.height), 1 };
            s.dapi->CmdBlitImage(cmd, out.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, full.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 1, &region, VK_FILTER_NEAREST);
            Barrier(cmd, full, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT, kAll, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
        }
        Barrier(cmd, out, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, kAll, VK_ACCESS_MEMORY_READ_BIT);
        out.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return ok;
    }
}
