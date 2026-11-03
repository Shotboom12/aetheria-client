// WxlHost overlay: the Dear ImGui context, its input, and its drawing through the engine's Gx layer.
#pragma once

#include <cstdint>

namespace wxlhost::overlay
{
    /// Reads [WxlHost] from WTF\WXL\WarcraftXL.ini and installs the window-message filter. Idempotent.
    void Install();

    bool IsOpen();
    void SetOpen(bool open);

    /// Builds and draws the overlay for this frame (a no-op while shut). Called at the end of the paint
    /// handler (RenderScript::OnFrameEnd), before the scene is presented.
    void OnFrameEnd(float dtSec);

    /// OsWindowProc filter (see patches/Input-message-filter.patch). Returns true when the message was
    /// consumed, with *result the window procedure's return value.
    bool HandleMessage(uint32_t message, uintptr_t wparam, intptr_t lparam, intptr_t* result);
}
