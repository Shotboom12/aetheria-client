// Generic debug-UI editor for any table registered against it (WXL-19). See wxl_seyris/EditorApi.hpp
// for the published shape and the design rationale (why tableHandle is void**, why field hints
// exist, why read-only is just a bool rather than a separate code path).

#pragma once

#include "wxl_seyris/EditorApi.hpp"

struct WXL_Api;

namespace wxl_seyris::editor
{
    /// Must be called once, before RegisterTable/DrawPanel are ever reachable.
    void Init(const WXL_Api* api);

    void RegisterTable(const WXL_SeyrisEditorTable* table);

    /// The panel body registered via WXL_Api::UiAddPanel.
    void __cdecl DrawPanel(void* user);
}
