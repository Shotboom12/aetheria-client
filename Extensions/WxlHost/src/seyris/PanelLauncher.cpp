#include "PanelLauncher.hpp"

#include "Settings.hpp"

#include <cstddef>
#include <cstring>

namespace wxl_seyris::panels
{
    namespace
    {
        constexpr const char* kTitle = "WXL panels";

        const WXL_Api* g_api = nullptr;
        bool g_hideOthers = true;
        bool g_firstFrameDone = false;

        bool CoreHasPanelRegistry(const WXL_Api* api)
        {
            return api->structSize >= offsetof(WXL_Api, UiPanelTitle) + sizeof(api->UiPanelTitle)
                && api->UiSetPanelOpen && api->UiIsPanelOpen && api->UiPanelCount && api->UiPanelTitle;
        }

        void __cdecl DrawLauncher(void* /*user*/)
        {
            const int count = g_api->UiPanelCount();
            if (count <= 1) { g_api->UiText("(no other panels registered)"); return; }
            for (int i = 0; i < count; ++i)
            {
                const char* title = g_api->UiPanelTitle(i);
                if (!title || std::strcmp(title, kTitle) == 0) continue;
                int open = g_api->UiIsPanelOpen(title);
                if (g_api->UiCheckbox(title, &open)) g_api->UiSetPanelOpen(title, open);
            }
        }
    }

    void Register(const WXL_Api* api)
    {
        g_api = api;
        if (!CoreHasPanelRegistry(api))
        {
            api->Log(WXL_LOG_INFO, "wxl-seyris-tools", "panels: the core has no panel registry calls; launcher not used.");
            return;
        }
        const bool enabled = settings::BoolSetting(
            "SeyrisPanels", "Launcher", "If true, the F9 overlay shows a 'WXL panels' launcher listing every module's panel.", true).Value();
        if (!enabled) { g_api = nullptr; return; }
        g_hideOthers = settings::BoolSetting(
            "SeyrisPanels", "HideOthersOnStart", "If true, every other panel starts hidden and is opened from the launcher.", true).Value();
        api->UiAddPanel(kTitle, &DrawLauncher, nullptr);
    }

    void OnFrame()
    {
        if (!g_api) return;
        if (!g_firstFrameDone)
        {
            g_firstFrameDone = true;
            if (g_hideOthers)
                for (int i = 0; i < g_api->UiPanelCount(); ++i)
                {
                    const char* title = g_api->UiPanelTitle(i);
                    if (title && std::strcmp(title, kTitle) != 0) g_api->UiSetPanelOpen(title, 0);
                }
        }
        // Closed with its [x]: back for the next time the overlay opens.
        if (!g_api->UiIsOpen() && !g_api->UiIsPanelOpen(kTitle)) g_api->UiSetPanelOpen(kTitle, 1);
    }
}
