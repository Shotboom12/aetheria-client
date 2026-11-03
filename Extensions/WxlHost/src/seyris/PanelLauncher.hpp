// wxl-seyris-tools: the "WXL panels" launcher. Core shows every registered debug panel when the
// overlay (F9) opens; with many modules that buries the screen. This adds one launcher panel that
// lists every registered panel with a checkbox, and on the first frame (every extension loaded by
// then) hides all the others, so they open only when asked for. Both are WarcraftXL.ini settings
// ([SeyrisPanels]). Needs a core with UiSetPanelOpen/UiPanelCount/UiPanelTitle; without them it
// stays out of the way.
#pragma once

#include "wxl/PluginApi.h"

namespace wxl_seyris::panels
{
    /// Registers the launcher panel. Call from WXL_Load.
    void Register(const WXL_Api* api);

    /// Every frame (OnFrame): the first one hides the other panels; afterwards it keeps the
    /// launcher itself available whenever the overlay is closed.
    void OnFrame();
}
