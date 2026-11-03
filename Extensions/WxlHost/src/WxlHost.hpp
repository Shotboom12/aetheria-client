// WxlHost: the single in-client implementation of the WarcraftXL extension API (WXL_Api, wxl/PluginApi.h).
//
// Our x64 client has no WarcraftXL core and no DLL host; extensions are compiled in (Extensions/<Name>/src,
// Add<Name>Extension). This host serves the WXL_Api table to ported WXL modules so their source needs few
// changes: a module registers its WXL_Query/WXL_Load pair here instead of exporting them from a DLL.
// Only the named-seam half of the hook API is declined (there are no 32-bit Wow.exe addresses to detour).
//
// Threading: everything here runs on the main thread, from ExtensionMgr's render hooks.
#pragma once

#include "wxl/Event.hpp"
#include "wxl/PluginApi.h"

#include <cstdint>

namespace wxlhost
{
    using PluginQueryFn = const WXL_PluginInfo*(__cdecl*)(void);
    using PluginLoadFn  = int(__cdecl*)(const WXL_Api* api);

    /// The service table handed to every plugin. Valid for the process lifetime.
    const WXL_Api* Api();

    /// Registers a plugin. Safe at any time before the first frame (static-init order independent).
    /// query may be null. Plugins load once, in registration order, at the first frame, which is when
    /// the graphics device and every other extension exist.
    void RegisterPlugin(const char* name, PluginQueryFn query, PluginLoadFn load);

    /// Publishes an event to every subscriber, in subscription order.
    /// (Not named Emit: wxl/Event.hpp declares wxl::events::Emit, which argument-dependent lookup would find.)
    void PublishEvent(wxl::events::Event e, const void* args);

    // ---- driven by WxlHost.cpp's render hooks -------------------------------------------------------
    void FrameBegin(float dtSec);
    void FrameEnd();

    // ---- panels (WxlApi.cpp) ------------------------------------------------------------------------
    /// Draws every open panel; called by the overlay inside its ImGui frame.
    void DrawPanels();

    // ---- overlay state (WxlOverlay.cpp) -------------------------------------------------------------
    bool OverlayOpen();
}
