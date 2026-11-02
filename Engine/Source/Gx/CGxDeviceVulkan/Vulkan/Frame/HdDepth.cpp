#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame/Internal.hpp"

namespace gxvk::vk::frame
{
    bool WithSceneDepth(SceneDepthRecord record, void* user)
    {
        using namespace detail;
        if (!record || gxvk::vk::frame::Target() || QueryActive()) return false;
        VkCommandBuffer cmd=Outside();
        if (!cmd || !s.open || !s.sceneTouched || !s.scene.handle) return false;
        DepthBuffer* depth=nullptr;
        for (uint32_t i=0;i<s.fbCount;++i) {
            const auto& fb=s.fbs[i];
            if (fb.image==s.scene.handle && fb.layer==0 && fb.depth<s.depthCount) {
                depth=&s.depths[fb.depth];break;
            }
        }
        if (!depth || !depth->initialised || !depth->sampledView || depth->lastUsed!=s.serial
            || depth->extent.width!=s.scene.extent.width || depth->extent.height!=s.scene.extent.height) return false;
        VkImageMemoryBarrier barrier{};
        barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=depth->image;
        // Vulkan 1.0 requires both depth and stencil to transition together for D24S8.
        barrier.subresourceRange={VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT|
            (s.depthFormat==VK_FORMAT_D24_UNORM_S8_UINT?VK_IMAGE_ASPECT_STENCIL_BIT:0)),0,1,0,1};
        s.dapi->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        const bool recorded=record(cmd,depth->sampledView,s.depthFormat,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,user);
        barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barrier.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        s.dapi->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        return recorded;
    }
}
