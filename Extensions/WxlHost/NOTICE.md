# WxlHost: provenance and licences

| Part | Path | Origin | Licence |
| --- | --- | --- | --- |
| WXL extension API header | `src/wxl/PluginApi.h` | WarcraftXL SDK copy shipped in the Action Camera Suite 1.1.0 TEST source (`HunterVisuals_0.9.3/SDK/wxl/PluginApi.h`, 15405 bytes), unmodified | GPL-3.0-or-later (WarcraftXL) |
| WXL event ids | `src/wxl/Event.hpp` | WarcraftXL core `src/engine/events/Event.hpp` (DevKit copy), unmodified; only the enum numbering is used | GPL-3.0-or-later (WarcraftXL) |
| wxl-seyris-tools | `src/seyris/*` | https://github.com/Morfium-G/wxl-seyris-tools commit `d746d380a6c94d5136eaca3862c9836d0e911223`; x64 port edits: `Cdbc.cpp` reads through `SFile::LoadFile`, `Extension.cpp` exports `SeyrisToolsQuery/Load` instead of `WXL_Query/WXL_Load`, `SeyrisTools.hpp` added; `CrashReporter.cpp` not included | No LICENSE file in the repository. The project owner reports permission from the author to port it to x64 (2026-11-03); no written proof on file |
| Dear ImGui | `src/imgui/*` | https://github.com/ocornut/imgui tag `v1.91.9` (`97428e8ac99e339ce05eee531cf55b77b29ea709`), core files only; `imconfig.h` carries three local `#define`s | MIT (see `src/imgui/LICENSE.txt`) |
| WxlHost itself | `src/Wxl*.{cpp,hpp}` | written for this client | same as the client |

The WXL core itself (`wxl-core`, a 32-bit `Wow.exe` hooking framework) is NOT imported. Only its extension
API shape is implemented here, once, in `WxlApi.cpp`.

Open licence question: the client executable has to stay GPL-2 compatible, and the WarcraftXL headers
above are GPL-3.0-or-later. See `docs/CURRENT-STATUS.md` (licence note) and settle it before any release.
