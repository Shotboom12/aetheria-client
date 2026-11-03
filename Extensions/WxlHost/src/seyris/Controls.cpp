#include "Controls.hpp"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wxl_seyris::controls
{
    namespace
    {
        constexpr const char* kQuickTitle = "WXL quick view";
        constexpr const char* kPagesTitle = "WXL pages";
        constexpr int kMaxStrips = 6;

        struct Entry
        {
            std::string      group, name, key;   // key = "group/name", the saved identity
            WXL_SeyrisDrawFn draw = nullptr;
            void*            user = nullptr;
        };

        struct View
        {
            std::string              name;
            int                      strips = 4;
            std::vector<std::string> keys = std::vector<std::string>(kMaxStrips); // "" = empty strip
        };

        const WXL_Api*     g_api = nullptr;
        std::vector<Entry> g_features, g_pages;
        std::vector<View>  g_views;
        int                g_currentView = 0;
        std::string        g_currentPage;
        bool               g_loaded = false;
        char               g_newName[64] = "";

        // --- persistence: <client>\WTF\WXL\QuickViews.ini -------------------------------------------
        std::string IniPath()
        {
            static std::string path;
            if (!path.empty()) return path;
            char exe[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, exe, MAX_PATH);
            std::string dir(exe);
            dir = dir.substr(0, dir.find_last_of("\\/"));
            CreateDirectoryA((dir + "\\WTF").c_str(), nullptr);
            CreateDirectoryA((dir + "\\WTF\\WXL").c_str(), nullptr);
            path = dir + "\\WTF\\WXL\\QuickViews.ini";
            return path;
        }

        std::string ReadString(const char* section, const char* key, const char* fallback = "")
        {
            char text[256] = {};
            GetPrivateProfileStringA(section, key, fallback, text, sizeof(text), IniPath().c_str());
            return text;
        }

        void WriteString(const char* section, const char* key, const std::string& value)
        {
            WritePrivateProfileStringA(section, key, value.c_str(), IniPath().c_str());
        }

        std::string SectionOf(const std::string& view) { return "QuickView." + view; }

        void SaveIndex();

        void SaveView(const View& v)
        {
            const std::string section = SectionOf(v.name);
            WriteString(section.c_str(), "Strips", std::to_string(v.strips));
            for (int s = 0; s < kMaxStrips; ++s)
                WriteString(section.c_str(), ("Strip" + std::to_string(s + 1)).c_str(), v.keys[s]);
            SaveIndex();
        }

        void SaveIndex()
        {
            WriteString("QuickViews", "Count", std::to_string(g_views.size()));
            for (size_t i = 0; i < g_views.size(); ++i)
                WriteString("QuickViews", ("View" + std::to_string(i + 1)).c_str(), g_views[i].name);
            WriteString("QuickViews", "Current", g_views[g_currentView].name);
        }

        void Load()
        {
            g_loaded = true;
            const int count = static_cast<int>(GetPrivateProfileIntA("QuickViews", "Count", 0, IniPath().c_str()));
            const std::string current = ReadString("QuickViews", "Current");
            for (int i = 0; i < count && i < 64; ++i)
            {
                View v;
                v.name = ReadString("QuickViews", ("View" + std::to_string(i + 1)).c_str());
                if (v.name.empty()) continue;
                const std::string section = SectionOf(v.name);
                v.strips = static_cast<int>(GetPrivateProfileIntA(section.c_str(), "Strips", 4, IniPath().c_str()));
                if (v.strips < 1) v.strips = 1;
                if (v.strips > kMaxStrips) v.strips = kMaxStrips;
                for (int s = 0; s < kMaxStrips; ++s) v.keys[s] = ReadString(section.c_str(), ("Strip" + std::to_string(s + 1)).c_str());
                if (v.name == current) g_currentView = static_cast<int>(g_views.size());
                g_views.push_back(v);
            }
            if (g_views.empty())
            {
                View v;
                v.name = "Default";
                g_views.push_back(v);
                g_currentView = 0;
            }
            g_currentPage = ReadString("Pages", "Current");
        }

        const Entry* Find(const std::vector<Entry>& list, const std::string& key)
        {
            for (const Entry& e : list) if (e.key == key) return &e;
            return nullptr;
        }

        int Add(std::vector<Entry>& list, const char* group, const char* name, WXL_SeyrisDrawFn draw, void* user)
        {
            if (!group || !name || !draw || !*name) return 0;
            Entry e;
            e.group = group;
            e.name = name;
            e.key = e.group + "/" + e.name;
            if (Find(list, e.key)) return 0;
            e.draw = draw;
            e.user = user;
            list.push_back(e);
            return 1;
        }

        // --- the quick-view window --------------------------------------------------------------------
        void __cdecl DrawQuick(void* /*user*/)
        {
            if (!g_loaded) Load();
            View& view = g_views[g_currentView];

            std::vector<const char*> names;
            for (const View& v : g_views) names.push_back(v.name.c_str());
            int pick = g_currentView;
            if (g_api->UiCombo("view##qv", &pick, names.data(), static_cast<int>(names.size())) && pick != g_currentView)
            {
                g_currentView = pick;
                SaveIndex();
                return; // draw the other view next frame
            }
            g_api->UiSameLine();
            if (g_views.size() > 1 && g_api->UiButton("Delete view##qv"))
            {
                WritePrivateProfileStringA(SectionOf(view.name).c_str(), nullptr, nullptr, IniPath().c_str());
                g_views.erase(g_views.begin() + g_currentView);
                g_currentView = 0;
                SaveIndex();
                return;
            }
            g_api->UiInputText("##qvname", g_newName, sizeof(g_newName));
            g_api->UiSameLine();
            if (g_api->UiButton("Save as new view##qv") && g_newName[0])
            {
                bool taken = false;
                for (const View& v : g_views) taken = taken || v.name == g_newName;
                if (!taken)
                {
                    View copy = view;
                    copy.name = g_newName;
                    g_views.push_back(copy);
                    g_currentView = static_cast<int>(g_views.size()) - 1;
                    SaveView(copy);
                    SaveIndex();
                    g_newName[0] = '\0';
                    return;
                }
            }
            if (g_api->UiSliderInt("strips##qv", &view.strips, 1, kMaxStrips)) SaveView(view);
            g_api->UiSeparator();

            // The dropdown: every registered feature; one feature shows in one strip at a time (two
            // copies of the same controls would share their ids).
            std::vector<std::string> labels = { "(empty)" };
            for (const Entry& e : g_features) labels.push_back(e.group + ": " + e.name);
            std::vector<const char*> items;
            for (const std::string& l : labels) items.push_back(l.c_str());

            for (int s = 0; s < view.strips; ++s)
            {
                int index = 0;
                for (size_t f = 0; f < g_features.size(); ++f) if (g_features[f].key == view.keys[s]) index = static_cast<int>(f) + 1;
                char label[32];
                std::snprintf(label, sizeof(label), "##strip%d", s);
                if (g_api->UiCombo(label, &index, items.data(), static_cast<int>(items.size())))
                {
                    const std::string key = index > 0 ? g_features[index - 1].key : "";
                    for (int o = 0; o < kMaxStrips; ++o) if (o != s && !key.empty() && view.keys[o] == key) view.keys[o].clear();
                    view.keys[s] = key;
                    SaveView(view);
                }
                if (view.keys[s].empty()) { g_api->UiSeparator(); continue; }
                if (const Entry* e = Find(g_features, view.keys[s])) e->draw(e->user);
                else
                {
                    const std::string missing = "(not available now: " + view.keys[s] + ")";
                    g_api->UiText(missing.c_str());
                }
                g_api->UiSeparator();
            }
        }

        // --- the page window -----------------------------------------------------------------------
        void __cdecl DrawPages(void* /*user*/)
        {
            if (!g_loaded) Load();
            if (g_pages.empty()) { g_api->UiText("(no pages registered)"); return; }
            std::vector<std::string> labels;
            for (const Entry& e : g_pages) labels.push_back(e.group + ": " + e.name);
            std::vector<const char*> items;
            for (const std::string& l : labels) items.push_back(l.c_str());
            int index = 0;
            for (size_t p = 0; p < g_pages.size(); ++p) if (g_pages[p].key == g_currentPage) index = static_cast<int>(p);
            if (g_api->UiCombo("page##pages", &index, items.data(), static_cast<int>(items.size())))
                WriteString("Pages", "Current", g_pages[index].key);
            g_currentPage = g_pages[index].key;
            g_api->UiSeparator();
            g_pages[index].draw(g_pages[index].user);
        }
    }

    void Register(const WXL_Api* api)
    {
        g_api = api;
        api->UiAddPanel(kQuickTitle, &DrawQuick, nullptr);
        api->UiAddPanel(kPagesTitle, &DrawPages, nullptr);
    }

    int __cdecl HasFeature(const char* name)
    {
        return name && (std::strcmp(name, "quick-features") == 0 || std::strcmp(name, "pages") == 0);
    }

    int __cdecl AddQuickFeature(const char* group, const char* name, WXL_SeyrisDrawFn draw, void* user)
    {
        return Add(g_features, group, name, draw, user);
    }

    int __cdecl AddPage(const char* group, const char* name, WXL_SeyrisDrawFn draw, void* user)
    {
        return Add(g_pages, group, name, draw, user);
    }
}
