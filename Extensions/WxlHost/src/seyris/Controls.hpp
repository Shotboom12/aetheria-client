// "seyris.controls": the quick views and the page window (see wxl_seyris/ControlsApi.hpp).
#pragma once

#include "wxl/PluginApi.h"
#include "wxl_seyris/ControlsApi.hpp"

namespace wxl_seyris::controls
{
    /// The two overlay windows. From WXL_Load.
    void Register(const WXL_Api* api);

    int __cdecl HasFeature(const char* name);
    int __cdecl AddQuickFeature(const char* group, const char* name, WXL_SeyrisDrawFn draw, void* user);
    int __cdecl AddPage(const char* group, const char* name, WXL_SeyrisDrawFn draw, void* user);
}
