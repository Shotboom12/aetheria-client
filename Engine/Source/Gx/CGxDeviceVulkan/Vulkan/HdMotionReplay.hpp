// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria FSR object motion, step 3: the private velocity replay. At the opaque boundary of a world frame the skinned draws that have a
// previous-frame twin (hdmotion::MatchedDraw) are drawn again, with our velocity variants of their vertex modules (current and previous clip
// position from the current and previous register files) and the built-in velocity fragment, into a private RGBA16F target (rg = previous UV
// minus current UV, a = covered). The scene's depth is copied in first, so what a wall hides writes nothing. The main scene pass is not touched.
// Render thread only; needs the scene target and no active query.
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Vk.hpp"
#include <cstdint>

namespace gxvk::vk::hdmotion
{
    struct Velocity
    {
        VkImage     image  = VK_NULL_HANDLE;
        VkImageView view   = VK_NULL_HANDLE;   ///< SHADER_READ_ONLY_OPTIMAL when read
        VkExtent2D  extent{};
        uint32_t    draws  = 0;                ///< draws replayed
    };

    /// Replays this frame's matched skinned draws. True when a velocity image was produced for frame::Serial().
    bool ReplayVelocity();

    /// This frame's velocity image, if ReplayVelocity produced one for `serial`.
    bool Result(uint64_t serial, Velocity& out);

    /// Device teardown: waits for the frames that used the targets, then destroys them. False when a serial could not be waited for.
    bool ShutdownReplay();
}
