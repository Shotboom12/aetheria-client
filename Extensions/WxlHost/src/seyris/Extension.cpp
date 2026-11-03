// wxl-seyris-tools -- shared toolkit for other WXL v1.1 modules (Epic WXL-12): settings/config
// (this file, plus Settings.{hpp,cpp}/CrashReporter.cpp), a registrable-schema cdbc reader, and
// later a runtime handle store. Each capability is published as its own named interface
// ("seyris.cdbc", "seyris.settings", ...) rather than one interface for the whole module -- see
// wxl_seyris/CdbcApi.hpp for the full versioning/growth convention shared by all of them.

#include "wxl/PluginApi.h"

#include "wxl/Event.hpp"
#include "SeyrisTools.hpp"

#include "Cdbc.hpp"
#include "Controls.hpp"
#include "Editor.hpp"
#include "PanelLauncher.hpp"
#include "Settings.hpp"
#include "wxl_seyris/CdbcApi.hpp"
#include "wxl_seyris/EditorApi.hpp"
#include "wxl_seyris/SettingsApi.hpp"

#include <cstring>

namespace
{
    const WXL_PluginInfo kInfo = {
        sizeof(WXL_PluginInfo),
        WXL_API_VERSION,
        "wxl-seyris-tools",
        1, // pluginVersion: opaque to the core, bump manually as this module grows
        WXL_CLIENT_BUILD,
    };

    const WXL_Api* g_api = nullptr;

    int __cdecl CdbcHasFeature(const char* name)
    {
        if (!name) return 0;
        return std::strcmp(name, "cdbc-load") == 0
            || std::strcmp(name, "cdbc-find-by-field") == 0
            || std::strcmp(name, "cdbc-write") == 0;
    }

    WXL_SeyrisCdbcApi g_cdbcApi = {
        sizeof(WXL_SeyrisCdbcApi),
        1, 3, 0, // major.minor.patch -- minor bumped: write support added (WXL-17)
        &CdbcHasFeature,
        &wxl_seyris::cdbc::Load,
        &wxl_seyris::cdbc::Release,
        &wxl_seyris::cdbc::RowCount,
        &wxl_seyris::cdbc::RowAt,
        &wxl_seyris::cdbc::FindRow,
        &wxl_seyris::cdbc::Value,
        &wxl_seyris::cdbc::GetString,
        &wxl_seyris::cdbc::FieldIndex,
        &wxl_seyris::cdbc::FindRowByField,
        &wxl_seyris::cdbc::RowAdd,
        &wxl_seyris::cdbc::RowUpdateValue,
        &wxl_seyris::cdbc::RowUpdateString,
        &wxl_seyris::cdbc::RowDelete,
        &wxl_seyris::cdbc::Save,
        &wxl_seyris::cdbc::Reload,
    };

    // Self-test (WXL-12 milestone 2): registers a small schema against a real, already-known-good
    // classic DBC (ChrRaces.dbc -- the race-code reading investigated and confirmed by hand earlier
    // this week, see offsets/game/DB2.hpp's chrraces::kOffRecordPrefix) and logs what the reader
    // finds. Proves the parser against real data before wxl-equip-extension depends on it.
    //
    // Run from the first OnFrame, not from WXL_Load: WXL_Load fires "early enough to precede the
    // background file reader" (PluginApi.h), i.e. before archives are necessarily mounted -- the
    // same load-order-timing concern WXL-12 designed the deferred-GetInterface-resolution rule for
    // applies just as much to file reads, so this waits for the first real frame like a consumer of
    // seyris.cdbc will (see WXL-12 milestone 3).
    void RunCdbcSelfTest(const WXL_Api* api)
    {
        constexpr WXL_SeyrisCdbcField kFields[] = {
            {"ID", 0, WXL_CDBC_FIELD_VALUE},
            {"ClientPrefix", 6, WXL_CDBC_FIELD_STRING}, // offset 0x18 into the in-memory record / 4
        };
        const WXL_SeyrisCdbcDefinition def = {
            "ChrRaces", "DBFilesClient\\ChrRaces.dbc", kFields, 2,
        };

        char err[256] = {};
        void* table = wxl_seyris::cdbc::Load(&def, err, sizeof(err));
        if (!table)
        {
            api->Log(WXL_LOG_WARN, "wxl-seyris-tools", "cdbc self-test: %s", err);
            return;
        }

        const uint32_t rows = wxl_seyris::cdbc::RowCount(table);
        api->Log(WXL_LOG_INFO, "wxl-seyris-tools", "cdbc self-test: ChrRaces.dbc loaded, %u rows", rows);

        for (uint32_t id = 1; id <= 8; ++id)
        {
            const void* row = wxl_seyris::cdbc::FindRow(table, id);
            if (!row) continue;
            const char* prefix = wxl_seyris::cdbc::GetString(table, row, "ClientPrefix");
            api->Log(WXL_LOG_INFO, "wxl-seyris-tools", "cdbc self-test: race %u ClientPrefix='%s'", id, prefix);
        }

        wxl_seyris::cdbc::Release(table);
    }

    // OnFrame has no Subscribe counterpart to unsubscribe with (extensions are never unloaded, and
    // Emit walks a flat vector -- see PluginApi.h), so this guards itself to run once.
    void __cdecl OnFirstFrame(void* /*user*/, const void* /*args*/)
    {
        static bool ran = false;
        if (ran) return;
        ran = true;
        RunCdbcSelfTest(g_api);
    }

    void __cdecl OnFramePanels(void* /*user*/, const void* /*args*/)
    {
        wxl_seyris::panels::OnFrame();
    }

    int __cdecl SettingsHasFeature(const char* name)
    {
        return name && (std::strcmp(name, "get-bool") == 0 || std::strcmp(name, "get-float") == 0);
    }

    int __cdecl SettingsGetBool(const char* ns, const char* key, const char* description, int defaultValue)
    {
        return wxl_seyris::settings::BoolSetting(ns, key, description, defaultValue != 0).Value() ? 1 : 0;
    }

    float __cdecl SettingsGetFloat(const char* ns, const char* key, const char* description, float defaultValue)
    {
        return wxl_seyris::settings::FloatSetting(ns, key, description, defaultValue).Value();
    }

    WXL_SeyrisSettingsApi g_settingsApi = {
        sizeof(WXL_SeyrisSettingsApi),
        1, 1, 0, // major.minor.patch
        &SettingsHasFeature,
        &SettingsGetBool,
        &SettingsGetFloat,
    };

    int __cdecl EditorHasFeature(const char* name)
    {
        return name && std::strcmp(name, "register-table") == 0;
    }

    WXL_SeyrisControlsApi g_controlsApi = {
        sizeof(WXL_SeyrisControlsApi),
        1, 0, 0, // major.minor.patch
        &wxl_seyris::controls::HasFeature,
        &wxl_seyris::controls::AddQuickFeature,
        &wxl_seyris::controls::AddPage,
    };

    WXL_SeyrisEditorApi g_editorApi = {
        sizeof(WXL_SeyrisEditorApi),
        1, 0, 0, // major.minor.patch
        &EditorHasFeature,
        &wxl_seyris::editor::RegisterTable,
    };
}

// x64 port: no DLL exports. WxlHost registers these two entry points (see SeyrisTools.hpp) and calls
// them at its first frame, in the same order the WXL core would have loaded the extension.
const WXL_PluginInfo* __cdecl SeyrisToolsQuery(void)
{
    return &kInfo;
}

int __cdecl SeyrisToolsLoad(const WXL_Api* api)
{
    if (!api) return 0;
    g_api = api;

    api->PublishInterface(WXL_SEYRIS_CDBC_INTERFACE_NAME, WXL_SEYRIS_CDBC_INTERFACE_VERSION, &g_cdbcApi);
    api->PublishInterface(WXL_SEYRIS_SETTINGS_INTERFACE_NAME, WXL_SEYRIS_SETTINGS_INTERFACE_VERSION, &g_settingsApi);
    api->PublishInterface(WXL_SEYRIS_EDITOR_INTERFACE_NAME, WXL_SEYRIS_EDITOR_INTERFACE_VERSION, &g_editorApi);
    // The quick views and the page window: hosted here, filled by the modules.
    api->PublishInterface(WXL_SEYRIS_CONTROLS_INTERFACE_NAME, WXL_SEYRIS_CONTROLS_INTERFACE_VERSION, &g_controlsApi);
    wxl_seyris::controls::Register(api);
    api->Subscribe(static_cast<uint32_t>(wxl::events::Event::OnFrame), &OnFirstFrame, nullptr);
    api->Subscribe(static_cast<uint32_t>(wxl::events::Event::OnFrame), &OnFramePanels, nullptr);
    wxl_seyris::panels::Register(api);

    wxl_seyris::editor::Init(api);

    // Dev-only, checked once at load like CrashReporter's own toggle -- no live re-toggle without a
    // restart, same as every other BoolSetting in this codebase. RegisterTable calls are still
    // accepted either way (cheap bookkeeping); this only gates whether the panel is ever drawn.
    const bool editorEnabled = wxl_seyris::settings::BoolSetting(
        "SeyrisEditor", "Enabled", "If true, shows the cdbc table editor in the F9 debug overlay.", false).Value();
    if (editorEnabled)
        api->UiAddPanel("wxl-seyris-tools: cdbc editor", &wxl_seyris::editor::DrawPanel, nullptr);

    api->Log(WXL_LOG_INFO, "wxl-seyris-tools", "v1.1 loaded (WXL_Load reached).");
    return 1;
}
