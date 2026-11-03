// WxlHost: the WXL_Api service table (see WxlHost.hpp).
//
//   Log               Extensions.log, through ExtensionMgr
//   Subscribe/Emit    flat per-event vectors, no unsubscribe (as in the WXL core)
//   HookAttach*       declined: there are no Wow.exe addresses in this client, and no named seams are
//                     published yet (a module that needs one is a port task, not a host feature)
//   Publish/GetInterface   a small registry, exact name + version match
//   Ui*               forwarded to Dear ImGui; valid only inside a panel body, as in WXL
//   panel registry    title, body, user, open flag; UiSetPanelOpen/UiIsPanelOpen/UiPanelCount/UiPanelTitle
//                     work outside a frame too

#include "WxlHost.hpp"

#include "WoW/Source/Extension/ExtensionMgr.h"

#include "imgui/imgui.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wxlhost
{
    namespace
    {
        struct Subscriber
        {
            WXL_EventFn handler;
            void*       user;
        };

        struct Published
        {
            std::string name;
            uint32_t    version;
            void*       iface;
        };

        struct Panel
        {
            std::string title;
            WXL_PanelFn fn;
            void*       user;
            bool        open;
        };

        struct Plugin
        {
            std::string   name;
            PluginQueryFn query;
            PluginLoadFn  load;
        };

        constexpr size_t kEventCount = static_cast<size_t>(wxl::events::Event::Count);

        std::vector<Subscriber>& Subs(uint32_t event)
        {
            static std::vector<Subscriber> s_subs[kEventCount];
            static std::vector<Subscriber> s_none;
            return event < kEventCount ? s_subs[event] : s_none;
        }

        std::vector<Published>& PublishedList()
        {
            static std::vector<Published> s_list;
            return s_list;
        }

        std::vector<Panel>& Panels()
        {
            static std::vector<Panel> s_panels;
            return s_panels;
        }

        std::vector<Plugin>& Plugins()
        {
            static std::vector<Plugin> s_plugins;
            return s_plugins;
        }

        bool g_pluginsLoaded = false;
        bool g_inPanel       = false;

        const char* LevelName(int level)
        {
            switch (level)
            {
                case WXL_LOG_TRACE: return "TRACE";
                case WXL_LOG_DEBUG: return "DEBUG";
                case WXL_LOG_INFO:  return "INFO";
                case WXL_LOG_WARN:  return "WARN";
                case WXL_LOG_ERROR: return "ERROR";
                default:            return "?";
            }
        }

        void __cdecl ApiLog(int level, const char* tag, const char* fmt, ...)
        {
            char text[1024];
            va_list args;
            va_start(args, fmt);
            std::vsnprintf(text, sizeof text, fmt ? fmt : "", args);
            va_end(args);
            sExtensionMgr->Log("WXL %s [%s] %s", LevelName(level), tag ? tag : "?", text);
        }

        void __cdecl ApiSubscribe(uint32_t event, WXL_EventFn handler, void* user)
        {
            if (!handler || event >= kEventCount) return;
            Subs(event).push_back({ handler, user });
        }

        void __cdecl ApiEmit(uint32_t event, const void* args)
        {
            if (event >= kEventCount) return;
            // Index loop: a handler may subscribe another handler while the event is being published.
            std::vector<Subscriber>& list = Subs(event);
            for (size_t i = 0; i < list.size(); ++i)
                list[i].handler(list[i].user, args);
        }

        int __cdecl ApiHookAttach(const char* name, uintptr_t, void*, void**, int)
        {
            sExtensionMgr->Log("WXL WARN HookAttach('%s') declined: no Wow.exe addresses in the x64 client", name ? name : "?");
            return 0;
        }

        int __cdecl ApiHookAttachByName(const char* pointName, void*, void**, int)
        {
            sExtensionMgr->Log("WXL WARN HookAttachByName('%s') declined: no such seam", pointName ? pointName : "?");
            return 0;
        }

        void __cdecl ApiPublishInterface(const char* name, uint32_t version, void* iface)
        {
            if (!name || !iface) return;
            for (Published& p : PublishedList())
            {
                if (p.version == version && p.name == name)
                {
                    p.iface = iface; // republish replaces, same as a second WXL extension would
                    return;
                }
            }
            PublishedList().push_back({ name, version, iface });
        }

        void* __cdecl ApiGetInterface(const char* name, uint32_t version)
        {
            if (!name) return nullptr;
            for (const Published& p : PublishedList())
                if (p.version == version && p.name == name) return p.iface;
            return nullptr;
        }

        Panel* FindPanel(const char* title)
        {
            if (!title) return nullptr;
            for (Panel& p : Panels())
                if (p.title == title) return &p;
            return nullptr;
        }

        void __cdecl ApiUiAddPanel(const char* title, WXL_PanelFn fn, void* user)
        {
            if (!title || !fn) return;
            if (Panel* existing = FindPanel(title))
            {
                existing->fn   = fn;
                existing->user = user;
                return;
            }
            Panels().push_back({ title, fn, user, true });
        }

        int __cdecl ApiUiIsOpen(void) { return OverlayOpen() ? 1 : 0; }

        // The drawing calls do nothing outside a panel body (the WXL contract); the guard also keeps a
        // plugin that calls them from WXL_Load from touching ImGui before the context exists.
        void __cdecl ApiUiText(const char* text)
        {
            if (g_inPanel) ImGui::TextUnformatted(text ? text : "");
        }

        void __cdecl ApiUiTextWrapped(const char* text)
        {
            if (g_inPanel) ImGui::TextWrapped("%s", text ? text : "");
        }

        void __cdecl ApiUiTextColored(const float rgba[4], const char* text)
        {
            if (!g_inPanel) return;
            const ImVec4 colour = rgba ? ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]) : ImVec4(1, 1, 1, 1);
            ImGui::TextColored(colour, "%s", text ? text : "");
        }

        void __cdecl ApiUiSeparator(void)
        {
            if (g_inPanel) ImGui::Separator();
        }

        int __cdecl ApiUiButton(const char* label)
        {
            return g_inPanel && ImGui::Button(label ? label : "") ? 1 : 0;
        }

        int __cdecl ApiUiCheckbox(const char* label, int* value)
        {
            if (!g_inPanel || !value) return 0;
            bool b = *value != 0;
            const bool changed = ImGui::Checkbox(label ? label : "", &b);
            if (changed) *value = b ? 1 : 0;
            return changed ? 1 : 0;
        }

        int __cdecl ApiUiSliderFloat(const char* label, float* value, float lo, float hi)
        {
            return g_inPanel && value && ImGui::SliderFloat(label ? label : "", value, lo, hi) ? 1 : 0;
        }

        int __cdecl ApiUiSliderInt(const char* label, int* value, int lo, int hi)
        {
            return g_inPanel && value && ImGui::SliderInt(label ? label : "", value, lo, hi) ? 1 : 0;
        }

        int __cdecl ApiUiColorEdit(const char* label, float rgba[4])
        {
            return g_inPanel && rgba && ImGui::ColorEdit4(label ? label : "", rgba) ? 1 : 0;
        }

        void __cdecl ApiUiSameLine(void)
        {
            if (g_inPanel) ImGui::SameLine();
        }

        int __cdecl ApiUiCombo(const char* label, int* index, const char* const* items, int count)
        {
            if (!g_inPanel || !index || !items || count <= 0) return 0;
            if (*index < 0 || *index >= count) *index = *index < 0 ? 0 : count - 1; // clamp, per the header
            return ImGui::Combo(label ? label : "", index, items, count) ? 1 : 0;
        }

        int __cdecl ApiUiCollapsingHeader(const char* label)
        {
            return g_inPanel && ImGui::CollapsingHeader(label ? label : "") ? 1 : 0;
        }

        int __cdecl ApiUiInputText(const char* label, char* buf, size_t bufSize)
        {
            if (!g_inPanel || !buf || bufSize == 0) return 0;
            const bool changed = ImGui::InputText(label ? label : "", buf, bufSize);
            buf[bufSize - 1] = '\0';
            return changed ? 1 : 0;
        }

        void __cdecl ApiUiSetPanelOpen(const char* title, int open)
        {
            if (Panel* p = FindPanel(title)) p->open = open != 0;
        }

        int __cdecl ApiUiIsPanelOpen(const char* title)
        {
            const Panel* p = FindPanel(title);
            return p && p->open ? 1 : 0;
        }

        int __cdecl ApiUiPanelCount(void)
        {
            return static_cast<int>(Panels().size());
        }

        const char* __cdecl ApiUiPanelTitle(int index)
        {
            if (index < 0 || static_cast<size_t>(index) >= Panels().size()) return nullptr;
            return Panels()[static_cast<size_t>(index)].title.c_str();
        }

        // Field order is the ABI (wxl/PluginApi.h): do not reorder.
        const WXL_Api g_api = {
            sizeof(WXL_Api), WXL_API_VERSION,
            &ApiLog, &ApiSubscribe, &ApiEmit, &ApiHookAttach, &ApiHookAttachByName,
            &ApiPublishInterface, &ApiGetInterface,
            &ApiUiAddPanel, &ApiUiIsOpen, &ApiUiText, &ApiUiSeparator, &ApiUiButton, &ApiUiCheckbox,
            &ApiUiSliderFloat, &ApiUiSliderInt, &ApiUiColorEdit, &ApiUiSameLine, &ApiUiCombo,
            &ApiUiCollapsingHeader, &ApiUiInputText, &ApiUiTextWrapped,
            &ApiUiSetPanelOpen, &ApiUiIsPanelOpen, &ApiUiPanelCount, &ApiUiPanelTitle,
            &ApiUiTextColored,
        };

        void LoadPluginsOnce()
        {
            if (g_pluginsLoaded) return;
            g_pluginsLoaded = true;

            for (const Plugin& plugin : Plugins())
            {
                const WXL_PluginInfo* info = plugin.query ? plugin.query() : nullptr;
                if (plugin.query && !info)
                {
                    sExtensionMgr->Log("WXL: plugin '%s' declined to load", plugin.name.c_str());
                    continue;
                }
                if (info && (info->apiVersion != WXL_API_VERSION || info->clientBuild != WXL_CLIENT_BUILD))
                {
                    sExtensionMgr->Log("WXL: plugin '%s' refused: api %u / client build %u, host serves %d / %d",
                                       plugin.name.c_str(), info->apiVersion, info->clientBuild,
                                       WXL_API_VERSION, WXL_CLIENT_BUILD);
                    continue;
                }

                const int ok = plugin.load(&g_api);
                sExtensionMgr->Log("WXL: plugin '%s' %s", plugin.name.c_str(), ok ? "loaded" : "failed to load");
            }
        }
    }

    const WXL_Api* Api()
    {
        return &g_api;
    }

    void RegisterPlugin(const char* name, PluginQueryFn query, PluginLoadFn load)
    {
        if (!load) return;
        Plugins().push_back({ name ? name : "?", query, load });
    }

    void PublishEvent(wxl::events::Event e, const void* args)
    {
        ApiEmit(static_cast<uint32_t>(e), args);
    }

    void DrawPanels()
    {
        g_inPanel = true;
        // Index loop: a panel body (a launcher) may register or toggle panels.
        for (size_t i = 0; i < Panels().size(); ++i)
        {
            Panel& panel = Panels()[i];
            if (!panel.open) continue;

            // First use only: a cascade, so panels that all start visible do not stack on one spot.
            const float cascade = 24.0f * static_cast<float>(i % 8);
            ImGui::SetNextWindowPos(ImVec2(60.0f + cascade, 60.0f + cascade), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(440.0f, 320.0f), ImGuiCond_FirstUseEver);

            bool open = true;
            if (ImGui::Begin(panel.title.c_str(), &open))
                panel.fn(panel.user);
            ImGui::End();

            // The panel vector may have grown inside the body; re-index before writing back.
            Panels()[i].open = open;
        }
        g_inPanel = false;
    }

    void FrameBegin(float dtSec)
    {
        LoadPluginsOnce();

        wxl::events::UpdateArgs args = { dtSec, 0 };
        PublishEvent(wxl::events::Event::OnUpdate, &args);
    }

    void FrameEnd()
    {
        wxl::events::FrameArgs args = { nullptr };
        PublishEvent(wxl::events::Event::OnFrame, &args);
    }
}
