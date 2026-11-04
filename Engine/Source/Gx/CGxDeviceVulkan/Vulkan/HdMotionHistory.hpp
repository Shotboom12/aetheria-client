// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria FSR object motion, step 1: last frame's vertex constants of the skinned draws, matched to this frame's draws.
// Nothing is drawn here and nothing changes while disabled. A skinned draw (blend weights + indices in its vertex record) carries its bone
// palette in its vertex register file; the same unit's batch in the previous frame carries the previous palette. The velocity replay
// (a later step) feeds that previous palette to the velocity vertex variants. Render thread only.
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Pipelines.hpp"
#include <cstdint>

namespace gxvk::vk::hdmotion
{
    struct Stats
    {
        uint32_t observed  = 0;   ///< skinned draws recorded this frame
        uint32_t matched   = 0;   ///< of them, with a previous-frame twin
        uint32_t stored    = 0;   ///< draws kept for the next frame
        uint32_t dropped   = 0;   ///< skinned draws not kept (table full)
        uint32_t lastFrame = 0;   ///< matched draws of the previous frame (stable figure for the status line)
        uint32_t lastObserved = 0, lastStored = 0, lastDropped = 0;   ///< the previous frame's totals (the current ones restart at BeginFrame)
    };

    /// Turns the history on or off (off forgets everything). Cheap when off: Observe returns at once.
    void SetEnabled(bool enabled);
    bool Enabled();

    /// The world frame starts: this frame's table becomes the previous one. `serial` is the frame's serial.
    void BeginFrame(uint64_t serial);

    /// From the device's draw path, after a draw was recorded on the scene target (same site as hdcapture::Capture).
    void Observe(const pipelines::Draw& draw, uint64_t serial, bool sceneTarget, bool queryActive);

    /// Previous-frame constants of a draw like `draw`, for the replay: the nth draw (in capture order) with the same key takes the nth
    /// previous twin. BeginReplay() restarts the counting. False when the draw is not skinned or has no twin.
    struct Previous { const float* vertexRegs = nullptr; uint32_t rows = 0; };
    void BeginReplay();
    bool PreviousFor(const pipelines::Draw& draw, Previous& out);

    /// The draws of this frame that have a previous-frame twin, for the velocity replay (opaque skinned draws only). Each comes back as an
    /// owned copy with vertexRegs/pixelRegs pointing at this table's copies and motionPrevRegs at the twin's previous register file; valid
    /// until the next BeginFrame.
    size_t MatchedCount();
    bool MatchedDraw(size_t index, pipelines::Draw& out);

    Stats GetStats();
    void Shutdown();
}
