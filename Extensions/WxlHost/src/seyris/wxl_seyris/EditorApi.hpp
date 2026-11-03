// Published as "seyris.editor" via WXL_Api::PublishInterface/GetInterface. Not a core header --
// copy this file into a consuming module's own repo, same as CdbcApi.hpp/SettingsApi.hpp (see
// CdbcApi.hpp for the full rationale and the additive-growth/versioning convention this mirrors).
//
// Registration/drawing is deliberately kept separate from seyris.cdbc itself -- the reader/writer
// has no business knowing about UI. A registered table's live handle is shared with the registrant
// (see WXL_SeyrisEditorTable::tableHandle below) rather than owned by the editor, so a Save+Reload
// done here immediately affects whatever the registrant does with that table next -- for
// wxl-equip-extension's ItemDisplayInfoExtra.cdbc, that means an edit made in the editor shows up
// on a currently-worn item without any extra plumbing.
//
// WXL-61 grew this file three ways, all additive (new members appended at the end of each struct,
// same convention as WXL_SeyrisCdbcApi -- a pre-existing positional initializer keeps compiling
// unchanged, the new members simply zero-initialize):
//   1/2. WXL_SeyrisEditorFieldHint gained `controlKind` (a real TextField/FlagField/NumberField
//        taxonomy instead of inferring the control purely from which of flagBits/namedValues was
//        set) and a ranged/stepped NUMBER shape (hasRange/isFloat/rangeMin/rangeMax/step), rendered
//        as an ImGui slider -- the piece WXL-60's FMOD DSP params need. `previewFn` generalizes the
//        old static `namedValues` table into an attachable "compute a preview string" callback;
//        namedValues keeps working as a convenience when previewFn is left NULL.
//   5.   WXL_SeyrisEditorTable gained a registrant-supplied row-picker label: `rowLabelFn` (fully
//        custom) or `labelFields` (editor concatenates those columns itself). Both NULL keeps the
//        pre-existing Comment/first-string-column heuristic.
//   6.   WXL_SeyrisEditorTable gained a `source`/`sourceModule` data-source badge -- STOCK_DBC for
//        an actual Blizzard WotLK 3.3.5 DBC (even one read through this module's own cdbc loader
//        rather than a hypothetical stock-DBC backend, e.g. SoundFilter.dbc for WXL-60), CUSTOM
//        (the default) for a module-invented schema, shown as "Custom DBC - Source: <module>".

#pragma once

#include <stdint.h>

#include "CdbcApi.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#define WXL_SEYRIS_EDITOR_INTERFACE_NAME    "seyris.editor"
#define WXL_SEYRIS_EDITOR_INTERFACE_VERSION 1u

typedef struct WXL_SeyrisEditorFlagBit
{
    uint32_t    mask;
    const char* label;
} WXL_SeyrisEditorFlagBit;

// A single known value for a WXL_CDBC_FIELD_VALUE field, purely informational -- the field stays
// freely editable (e.g. a modded attach point outside the known set is legitimate, not an error),
// this only resolves a label under the field showing what the currently-entered number means, or
// "Unknown" if it doesn't match any entry.
typedef struct WXL_SeyrisEditorNamedValue
{
    uint32_t    value;
    const char* label;
} WXL_SeyrisEditorNamedValue;

// Which widget a field renders as. AUTO (0) preserves the pre-WXL-61 behavior of inferring it from
// flagBits/namedValues/the field's own WXL_SeyrisCdbcFieldType -- existing hints that never set
// this (it zero-initializes) are unaffected. The other values let a registrant say so explicitly,
// and NUMBER is what unlocks the ranged/stepped rendering below.
enum WXL_SeyrisEditorControlKind
{
    WXL_EDITOR_CONTROL_AUTO   = 0,
    WXL_EDITOR_CONTROL_TEXT   = 1,
    WXL_EDITOR_CONTROL_FLAGS  = 2, // checkboxes; driven by flagBits same as before controlKind existed
    WXL_EDITOR_CONTROL_NUMBER = 3, // numeric, optionally ranged/stepped -- see hasRange below
};

// Attachable preview generator (WXL-61 item 1): returns a short string to show under the field, or
// NULL/empty for "nothing to show this frame". Supersedes the old fixed namedValues lookup table
// without removing it -- when previewFn is set it wins outright; a table-driven lookup can still be
// expressed by just keeping namedValues and leaving previewFn NULL. `user` is whatever the
// registrant passed in previewUser, handed back unchanged (closure data).
typedef const char* (__cdecl* WXL_SeyrisEditorPreviewFn)(void* table, const void* row,
                                                          const char* field, void* user);

// Optional per-field rendering hint. See WXL_SeyrisEditorControlKind for the widget this selects.
// A WXL_CDBC_FIELD_VALUE field with flagBits set renders as flagBitCount checkboxes instead of a
// plain numeric input (plus one raw-number field showing the combined value as text, editable,
// decimal or 0x-prefixed hex -- a cross-check for "did I tick the right boxes", and a fast way to
// set several bits at once). Fields with neither flagBits, a ranged NUMBER, nor a preview get a
// generic widget picked from their WXL_SeyrisCdbcFieldType -- this is what keeps the editor itself
// table-agnostic while still letting a registrant call out the fields that need it (e.g. a packed
// flags column, an attach-point id, or an FMOD DSP parameter with a documented range).
typedef struct WXL_SeyrisEditorFieldHint
{
    const char*                       field; // matches a WXL_SeyrisCdbcField.name in the same table
    const WXL_SeyrisEditorFlagBit*    flagBits;
    uint32_t                          flagBitCount;
    const WXL_SeyrisEditorNamedValue* namedValues;
    uint32_t                          namedValueCount;

    // --- appended for WXL-61 items 1/2 -- zero-valued (AUTO, hasRange=0, previewFn=NULL) for any
    // pre-existing positional initializer, i.e. identical to pre-WXL-61 behavior. -------------------

    uint32_t controlKind; // WXL_SeyrisEditorControlKind

    // WXL_EDITOR_CONTROL_NUMBER only. hasRange=0 -> plain text input (today's behavior, still
    // honoring isFloat for display/parsing). hasRange!=0 -> an ImGui slider: UiSliderFloat if
    // isFloat, else UiSliderInt.
    int   hasRange;
    int   isFloat;   // reinterpret the field's raw uint32 as IEEE-754 float bits, not an integer --
                      // needed for e.g. SoundFilterElem's Params0..8 (WXL-60), stored as float bits
                      // in a WXL_CDBC_FIELD_VALUE column same as any other raw uint32.
    float rangeMin;
    float rangeMax;
    float step;       // advisory only for now -- WXL_Api has no stepped-slider primitive yet (plain
                       // UiSliderFloat/UiSliderInt); still reported for the registrant's own use and
                       // so a future stepped-slider addition has it available without another bump.

    WXL_SeyrisEditorPreviewFn previewFn;
    void*                     previewUser;
} WXL_SeyrisEditorFieldHint;

// WXL-61 item 5: a fully custom row-picker label. Must return a pointer valid until the editor's
// next call (a static/thread-local buffer is fine -- the panel is single-threaded, drawn on the
// main thread same as the detour work it's editing).
typedef const char* (__cdecl* WXL_SeyrisEditorRowLabelFn)(void* table, const void* row, void* user);

// WXL-61 item 6: does this table's underlying file already exist as a stock Blizzard WotLK 3.3.5
// DBC (badge: "WotLK 3.3.5 DBC"), or is it a module-invented schema (badge: "Custom DBC - Source:
// <sourceModule>")? This describes the *data*, not the backend -- SoundFilter.dbc (WXL-60) is
// STOCK_DBC even though it's read through this same seyris.cdbc loader, not a hypothetical
// stock-DBC-aware backend (WXL-43).
enum WXL_SeyrisEditorSource
{
    WXL_EDITOR_SOURCE_CUSTOM    = 0,
    WXL_EDITOR_SOURCE_STOCK_DBC = 1,
};

typedef struct WXL_SeyrisEditorTable
{
    // Must stay valid for as long as this registration lives -- a static/global, same lifetime
    // requirement any cdbc definition already has.
    const WXL_SeyrisCdbcDefinition* definition;

    // Address of the REGISTRANT's own live table-handle variable (whatever Load()/Reload()
    // returned), not a copy of its current value. Reload() invalidates the old handle and returns
    // a new one -- the editor writes the new value back through this pointer on a successful save,
    // so the registrant's own next query automatically picks up the change.
    void** tableHandle;

    // Non-zero: every field renders read-only (plain text), no New Row/Delete Row/Save controls.
    int readOnly;

    // Optional, may be NULL/0-count.
    const WXL_SeyrisEditorFieldHint* fieldHints;
    uint32_t                         fieldHintCount;

    // --- appended for WXL-61 item 5 (registrant-supplied row-picker label). Winner order:
    // rowLabelFn (if set) > labelFields/labelFieldCount (if non-empty, the editor concatenates
    // those columns' values itself, space-separated, after "ID <n>: ") > the pre-existing
    // Comment/first-string-column heuristic, unchanged, when neither is supplied. ------------------

    WXL_SeyrisEditorRowLabelFn rowLabelFn;
    void*                      rowLabelUser;
    const char* const*         labelFields;
    uint32_t                   labelFieldCount;

    // --- appended for WXL-61 item 6 (data-source badge). source=CUSTOM(0) is the default -- no
    // behavior change for an existing registrant unless sourceModule is also set. ------------------

    uint32_t    source; // WXL_SeyrisEditorSource
    const char* sourceModule; // shown for CUSTOM (e.g. "wxl-equip-extension"); ignored for STOCK_DBC
} WXL_SeyrisEditorTable;

typedef struct WXL_SeyrisEditorApi
{
    uint32_t structSize;
    uint32_t major, minor, patch;

    /// Named capability check, independent of major/minor/patch. Always safe to call.
    int (__cdecl* HasFeature)(const char* name);

    /**
     * @brief Registers a table for the shared debug-UI editor panel.
     *
     * Safe to call regardless of whether the editor's dev-only toggle is currently on -- if it's
     * off, the registration is simply never drawn. The struct pointed to by @p table is read once
     * (copied internally); it does not need to outlive the call, but @p table->definition and
     * @p table->fieldHints do need to stay valid for the whole session.
     */
    void (__cdecl* RegisterTable)(const WXL_SeyrisEditorTable* table);
} WXL_SeyrisEditorApi;

#ifdef __cplusplus
}
#endif
