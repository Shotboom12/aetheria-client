// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: see HdMotionReplay.hpp. Patterned on hdgeometry (Vulkan/HdGeometryPass.cpp): a private pass, its own depth, pipelines::Record.
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdMotionReplay.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdMotionHistory.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/MotionVariants.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Manifest.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Memory.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Pipelines.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture.hpp"
#include <algorithm>
#include <vector>

namespace gxvk::vk::hdmotion
{
    namespace
    {
        constexpr VkFormat kColour = VK_FORMAT_R16G16B16A16_SFLOAT;

        struct Slot
        {
            texture::Image colour;
            VkImage        depth     = VK_NULL_HANDLE;
            VkImageView    depthView = VK_NULL_HANDLE;
            memory::Block  depthMemory;
            VkFramebuffer  fb        = VK_NULL_HANDLE;
            VkRenderPass   pass      = VK_NULL_HANDLE;
            VkExtent2D     extent{};
            bool           depthReady = false;   ///< the depth image has been moved to SHADER_READ_ONLY once
            uint32_t       draws      = 0;
            uint64_t       used = 0, valid = 0;
        };
        Slot slots[frame::kFramesInFlight];
        std::vector<pipelines::Draw> list;

        void Drop(Slot& s)
        {
            const auto* h = context::Get();
            if (h)
            {
                if (s.fb) h->dapi->DestroyFramebuffer(h->device, s.fb, nullptr);
                if (s.pass) h->dapi->DestroyRenderPass(h->device, s.pass, nullptr);
                if (s.depthView) h->dapi->DestroyImageView(h->device, s.depthView, nullptr);
                if (s.depth) h->dapi->DestroyImage(h->device, s.depth, nullptr);
                if (s.depthMemory.memory) memory::Free(s.depthMemory);
            }
            texture::DestroyIdle(s.colour);
            s = Slot{};
        }

        bool Make(Slot& s, VkExtent2D size)
        {
            const auto* h = context::Get();
            if (!h) return false;
            if (!texture::CreateTarget(size.width, size.height, 1, false, kColour, s.colour)) return false;
            const VkFormat depthFormat = frame::DepthFormat();
            if (depthFormat == VK_FORMAT_UNDEFINED) return false;
            VkImageCreateInfo image{};
            image.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image.imageType   = VK_IMAGE_TYPE_2D;
            image.format      = depthFormat;
            image.extent      = { size.width, size.height, 1 };
            image.mipLevels   = image.arrayLayers = 1;
            image.samples     = VK_SAMPLE_COUNT_1_BIT;
            image.tiling      = VK_IMAGE_TILING_OPTIMAL;
            image.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (h->dapi->CreateImage(h->device, &image, nullptr, &s.depth) != VK_SUCCESS) return false;
            VkMemoryRequirements req{};
            h->dapi->GetImageMemoryRequirements(h->device, s.depth, &req);
            if (!memory::Allocate(req, memory::Where::DeviceLocal, s.depthMemory) ||
                h->dapi->BindImageMemory(h->device, s.depth, s.depthMemory.memory, s.depthMemory.offset) != VK_SUCCESS) return false;
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = s.depth;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = depthFormat;
            view.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            if (h->dapi->CreateImageView(h->device, &view, nullptr, &s.depthView) != VK_SUCCESS) return false;

            VkAttachmentDescription attachments[2]{};
            attachments[0].format = kColour;
            attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            attachments[1].format = depthFormat;
            attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;          // the scene's depth, copied in just before
            attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;   // where frame::CopyTargetDepth leaves it
            attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;     // where it expects it next frame
            const VkAttachmentReference colour{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            const VkAttachmentReference depth{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
            VkSubpassDescription sub{};
            sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            sub.colorAttachmentCount = 1;
            sub.pColorAttachments = &colour;
            sub.pDepthStencilAttachment = &depth;
            VkSubpassDependency deps[2]{};
            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
            deps[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            deps[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
            deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            VkRenderPassCreateInfo pass{};
            pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            pass.attachmentCount = 2; pass.pAttachments = attachments;
            pass.subpassCount = 1; pass.pSubpasses = &sub;
            pass.dependencyCount = 2; pass.pDependencies = deps;
            if (h->dapi->CreateRenderPass(h->device, &pass, nullptr, &s.pass) != VK_SUCCESS) return false;
            const VkImageView views[2] = { s.colour.view, s.depthView };
            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = s.pass;
            fb.attachmentCount = 2; fb.pAttachments = views;
            fb.width = size.width; fb.height = size.height; fb.layers = 1;
            if (h->dapi->CreateFramebuffer(h->device, &fb, nullptr, &s.fb) != VK_SUCCESS) return false;
            s.extent = size;
            return true;
        }
    }

    bool ReplayVelocity()
    {
        if (!Enabled() || !motionvariants::Loaded()) return false;
        const uint32_t fi = frame::Slot();
        if (fi >= frame::kFramesInFlight) return false;
        Slot& slot = slots[fi];
        slot.valid = 0;
        if (frame::Target() || frame::QueryActive()) return false;
        const texture::Image* scene = frame::Scene();
        const uint64_t serial = frame::Serial();
        if (!scene || !scene->extent.width || !scene->extent.height || slot.used > frame::Completed() || slot.used == serial) return false;

        // The draws first: nothing is recorded unless at least one can be replayed.
        list.clear();
        const size_t matched = MatchedCount();
        for (size_t i = 0; i < matched; ++i)
        {
            pipelines::Draw d;
            if (!MatchedDraw(i, d) || d.vertex != pipelines::VertexProgram::Translated || !d.vertexRecord || d.fragment != pipelines::FragmentProgram::Translated) continue;
            if (!motionvariants::Path(d.vertexRecord->md5, motionvariants::Kind::OwnVelocity)) continue;
            d.geometry = 3; d.mirror = false;
            d.blend = 0; d.colourMask = 0xF;
            d.depthTest = true; d.depthWrite = true; d.depthFunc = 0; // LESS_OR_EQUAL against the copied scene depth
            d.depthBias = -8.0f;                                      // the replayed vertex shader may differ from the scene's in the last bit
            list.push_back(d);
        }
        if (list.empty()) return false;

        if (!slot.fb || slot.extent.width != scene->extent.width || slot.extent.height != scene->extent.height)
        {
            Drop(slot);
            if (!Make(slot, scene->extent)) { Drop(slot); return false; }
        }
        const auto* h = context::Get();
        VkCommandBuffer cmd = frame::Outside();
        if (!h || !cmd) return false;
        if (!slot.depthReady)
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = slot.depth;
            // Vulkan 1.0 transitions depth and stencil together for D24S8.
            b.subresourceRange = { VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT | (frame::DepthFormat() == VK_FORMAT_D24_UNORM_S8_UINT ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)), 0, 1, 0, 1 };
            h->dapi->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
            slot.depthReady = true;
        }
        if (!frame::CopyTargetDepth(slot.depth)) return false;   // the world's depth: walls hide what is behind them
        cmd = frame::Outside();
        if (!cmd) return false;
        slot.used = serial;

        VkClearValue clear[2]{};
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = slot.pass;
        begin.framebuffer = slot.fb;
        begin.renderArea.extent = slot.extent;
        begin.clearValueCount = 2; begin.pClearValues = clear;
        pipelines::ForgetBindings();
        h->dapi->CmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        uint32_t drawn = 0;
        for (const pipelines::Draw& d : list)
            if (pipelines::Record(cmd, slot.pass, frame::PassGeneration(), serial, fi, d)) ++drawn;
        h->dapi->CmdEndRenderPass(cmd);
        pipelines::ForgetBindings();
        slot.colour.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        slot.draws = drawn;
        if (!drawn) return false;
        slot.valid = serial;
        return true;
    }

    bool Result(uint64_t serial, Velocity& out)
    {
        out = Velocity();
        const uint32_t fi = frame::Slot();
        if (!serial || fi >= frame::kFramesInFlight) return false;
        const Slot& slot = slots[fi];
        if (slot.valid != serial || slot.used != serial || !slot.colour.handle) return false;
        out.image = slot.colour.handle; out.view = slot.colour.view; out.extent = slot.extent; out.draws = slot.draws;
        return true;
    }

    bool ShutdownReplay()
    {
        uint64_t last = 0;
        for (const Slot& s : slots) last = std::max(last, s.used);
        if (last > frame::Completed()) { frame::Flush(); if (!frame::WaitSerial(last)) return false; }
        for (Slot& s : slots) Drop(s);
        list.clear();
        return true;
    }
}
