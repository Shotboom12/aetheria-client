# wxl-seyris-tools

A shared toolkit module for [WarcraftXL](https://github.com/WarcraftXL/WarcraftXL) v1.1: a registrable-schema classic-WDBC ("cdbc") reader/writer, a live in-game table editor for it, a settings/config helper, and a crash reporter. Meant for any WXL v1.1 module to depend on instead of reimplementing these cross-cutting pieces itself — [`wxl-equip-extension`](https://github.com/Morfium-G/wxl-equip-extension) is the first real consumer, using it for `ItemDisplayInfoExtra.cdbc`.

Deliberately branded as its own thing, not folded into core — usable by any module, not just ones this project maintains. Design and scope tracked as Epic WXL-12 in the project's ticket system.

---

## What's in here

Four capabilities, each published as its own named interface (never a bare or `wxl.`-prefixed name) via `WXL_Api::PublishInterface`/`GetInterface`:

| Interface | What it does |
|---|---|
| `seyris.cdbc` | Load/query/write a classic-WDBC-format custom table from a registrable field schema. |
| `seyris.editor` | Register a `seyris.cdbc` table with a shared, generic in-game debug-UI editor panel. |
| `seyris.settings` | Read (or create, on first run) a boolean setting in `WTF\WXL\WarcraftXL.ini`. |
| — (internal) | A crash reporter, self-installing via its own setting — not published as an interface, since nothing else needs to call into it. |

Every published interface starts with `structSize`/`major`/`minor`/`patch`/`HasFeature(name)` and only grows by appending new fields at the end — never inserting or reordering — so an older consumer's compiled-in struct layout stays a valid prefix of a newer one. `HasFeature` lets a consumer check for one specific capability by name (e.g. `"cdbc-write"`) rather than reasoning about version numbers directly, and log a precise "what's missing" message instead of crashing on a stale build.

---

## `seyris.cdbc` — the cdbc reader/writer

"cdbc" is just the classic **WDBC** binary format (`WDBC` magic, a fixed header giving record count/field count/record size/string-block size, flat 4-byte-per-column records, a trailing NUL-separated string block) — the same layout every stock 3.3.5a `.dbc` uses. `.cdbc` is purely this project's naming convention for a fully custom table with no Blizzard/retail precedent (as opposed to a `.dbc` this project might also read/patch), following the convention established by Alyst3r's `WotLK-Extensions` project.

A consumer describes only the columns it cares about — by name and column index — not the table's full real layout:

```cpp
constexpr WXL_SeyrisCdbcField kFields[] = {
    {"ID",   0, WXL_CDBC_FIELD_VALUE},
    {"Name", 1, WXL_CDBC_FIELD_STRING},
};
constexpr WXL_SeyrisCdbcDefinition kDef = {
    "MyTable", "DBFilesClient\\MyTable.cdbc", kFields, 2,
};

void* table = cdbc->Load(&kDef, errBuf, sizeof(errBuf));
const void* row = cdbc->FindRow(table, 42);
const char* name = cdbc->GetString(table, row, "Name");
```

Column 0 must be the row id; rows are assumed id-sorted (the classic WDBC convention). The real column count and record stride come from the file's own header at load time, not from the schema — a caller only needs to name what it actually reads.

**Read**: `Load`/`Release`/`RowCount`/`RowAt`/`FindRow`/`Value`/`GetString`/`FieldIndex`, plus `FindRowByField` for tables with more than one row per logical entity (e.g. `ItemDisplayInfoExtra.cdbc`'s N models per item display id) — a resumable linear scan for "every row where `field == value`", not a prebuilt index.

**Write** (gated behind `HasFeature("cdbc-write")`): `RowAdd` (appends a row with the next unused id, every other column zero-filled), `RowUpdateValue`/`RowUpdateString`, `RowDelete`, `Save`, `Reload`. Any of these can reallocate the table's internal storage, so a row pointer obtained before a write is invalid after it — re-fetch via `FindRow`/`FindRowByField`. `Save` unconditionally backs up whatever's currently on disk to `<filename>.bak` first (no setting gates this), requires every real column in the file to be named in the schema it was loaded with (otherwise an unnamed string column's offset could be silently corrupted when the string block is rebuilt), and only works for **loose files** — a cdbc packed inside an MPQ can't be saved back to, since there's no write primitive for the client's own virtual filesystem.

---

## `seyris.editor` — the in-game table editor

A single shared, generic editor panel any module can register a `seyris.cdbc` table with — the table doesn't need to know anything about UI, and the editor doesn't need to know anything about a specific table's meaning beyond optional per-field rendering hints.

```cpp
void* g_table = cdbc->Load(&kDef, err, sizeof(err));

constexpr WXL_SeyrisEditorFlagBit kFlagBits[] = {
    {0x1, "Some flag"},
};
constexpr WXL_SeyrisEditorFieldHint kHints[] = {
    {"Flags", kFlagBits, 1, nullptr, 0},
};
const WXL_SeyrisEditorTable reg = { &kDef, &g_table, /*readOnly=*/0, kHints, 1 };
editor->RegisterTable(&reg);
```

- `tableHandle` is the **address of the registrant's own live handle variable**, not a copy of its value — `Reload()` returns a new handle and invalidates the old one, so the editor writes the new value back through this pointer on a successful save. The registrant's very next query automatically picks up the change, with no callback needed.
- A field with `flagBits` set renders as one checkbox per bit, plus a raw numeric field (decimal or `0x`-prefixed hex) as a cross-check or a fast way to set several bits at once.
- A field with `namedValues` set (and no `flagBits`) keeps a plain numeric input but resolves a label underneath showing what the current value means — the field itself stays freely editable, since an unlisted value (e.g. a modded id) is legitimate, not an error.
- A field with neither hint gets a generic widget picked from its `WXL_SeyrisCdbcFieldType` (text box for a string column, numeric input for a value column).
- `readOnly=1` renders every field as plain text with no New Row/Delete Row/Save controls — a trivial way to register a read-only viewer for a table nothing should edit in-game.
- Duplicate row ids (e.g. from a hand-edited file) are flagged in the row picker, with a shortcut to jump to the first collision.

`RegisterTable` is safe to call regardless of whether the editor's own display toggle is on — a registration made while it's off is just never drawn.

**Enabling the panel**: it only ever draws if `[SeyrisEditor] Enabled=true` in `WTF\WXL\WarcraftXL.ini` (see [Settings](#settings)) — checked once at load. Find it in the F9 debug overlay as "wxl-seyris-tools: cdbc editor" once enabled.

---

## `seyris.settings` — boolean settings

```cpp
bool enabled = settings->GetBool("MyModule", "Enabled", "What this does.", /*default=*/false) != 0;
```

Reads (or creates, on first run, writing the description as a leading comment) a key under `WTF\WXL\WarcraftXL.ini`. Existing file contents always win over the default. Stateless from the caller's side — internally backed by a small C++ class (`wxl_seyris::settings::BoolSetting`) that can't safely cross the DLL boundary, so the published interface just does create-or-read in one call.

Every setting in this codebase (including the ones below) is read once at load — there's no live re-toggle without a client restart.

---

## Crash reporter

A more detailed crash dialog than the client's stock one, self-installing via `SetUnhandledExceptionFilter` if enabled. Internal to this module (not published as an interface) — nothing else needs to call into it, it just installs itself at load if its setting is on.

---

## Settings

All settings live in `WTF\WXL\WarcraftXL.ini`, created automatically with each key's default the first time this module loads:

| Section | Key | Default | Effect |
|---|---|---|---|
| `[SeyrisEditor]` | `Enabled` | `false` | Shows the shared cdbc table editor panel in the F9 debug overlay. |
| `[CrashReporter]` | `Enabled` | `false` | Replaces the client's crash dialog with this module's own, more detailed one. |

---

## Building

Built as part of the WarcraftXL core CMake glob over `extensions/*` — there is no separate build step. Junction this module's `src/` folder into the core checkout's `extensions/` folder once (see the core repo's `CLAUDE.md`), then build the core normally:

```powershell
$env:PATH = "C:\path\to\cmake\bin;$env:PATH"
.\build.ps1
```

**Git hooks**: `.githooks/pre-push` blocks a push containing a bare `TODO`/`FIXME` (not tagged `TODO(WXL-123): ...`) -- worth being extra deliberate about in this module specifically, since it's meant to serve third-party modules whose future needs aren't fully known yet (see `orchestration/docs/principles.md`). Not active by default on a fresh clone (`.git/hooks` is untracked) — enable once with `git config core.hooksPath .githooks`. Override for a genuinely-intentional one: `git push --no-verify`.

---

## Consuming from another module

Copy the relevant header(s) from `src/wxl_seyris/` into your own module's repo — there is no shared include path across modules, each is its own repo by design.

Resolve each interface after `WXL_Load` has returned for every extension, **not** from inside your own `WXL_Load` — extension load order is alphabetical, so a dependency loading later alphabetically isn't available yet at that point, and blocking/polling inside `WXL_Load` would hang the loader (one thread, one loop, nothing else runs concurrently). Defer the `GetInterface` call to the first firing of an event guaranteed to happen after every extension has loaded (e.g. the first `OnFrame`), resolve once, and cache the result:

```cpp
const WXL_SeyrisCdbcApi* g_cdbc = nullptr;

void __cdecl OnFirstFrame(void*, const void*)
{
    static bool ran = false;
    if (ran) return;
    ran = true;

    g_cdbc = (const WXL_SeyrisCdbcApi*)api->GetInterface(WXL_SEYRIS_CDBC_INTERFACE_NAME, WXL_SEYRIS_CDBC_INTERFACE_VERSION);
    if (!g_cdbc || !g_cdbc->HasFeature("cdbc-load"))
    {
        api->Log(WXL_LOG_WARN, "my-module", "wxl-seyris-tools not installed or missing cdbc-load; falling back.");
        return;
    }
    // ... Load() your table here ...
}
```

Treat every absence gracefully (missing module, older version, missing capability) — log a specific, non-technical-user-shareable reason and fall back to whatever behavior makes sense without it, rather than failing to load.
