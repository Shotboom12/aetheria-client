#include "Editor.hpp"

#include "Cdbc.hpp"
#include "wxl/PluginApi.h"

#include <cstdio>
#include <cstdlib> // strtof, for parsing a NUMBER field with isFloat (WXL-61 item 2)
#include <cstring> // also used for the float-bits reinterpret below (WXL-61 item 2)
#include <string>
#include <unordered_map>
#include <vector>

namespace wxl_seyris::editor
{
    namespace
    {
        struct RegisteredTable
        {
            const WXL_SeyrisCdbcDefinition*  definition  = nullptr;
            void**                           tableHandle = nullptr;
            bool                              readOnly    = false;
            const WXL_SeyrisEditorFieldHint*  fieldHints  = nullptr;
            uint32_t                          fieldHintCount = 0;

            // WXL-61 item 5: registrant-supplied row-picker label.
            WXL_SeyrisEditorRowLabelFn rowLabelFn     = nullptr;
            void*                      rowLabelUser   = nullptr;
            const char* const*         labelFields    = nullptr;
            uint32_t                   labelFieldCount = 0;

            // WXL-61 item 6: data-source badge.
            uint32_t    source       = WXL_EDITOR_SOURCE_CUSTOM;
            const char* sourceModule = nullptr;
        };

        const WXL_Api*               g_api = nullptr;
        std::vector<RegisteredTable> g_tables;

        // Selection persists across frames (immediate-mode panel, redrawn every frame the overlay
        // is open) -- these are the only pieces of state that need to survive between calls.
        int g_selectedTable    = 0;
        int g_selectedRow      = 0;
        int g_lastSelTable     = -1;
        int g_lastSelRow       = -1;

        struct EditBuffer { std::string field; char text[256]; };
        std::vector<EditBuffer> g_buffers; // rebuilt only when the (table, row) selection changes

        void CopyBounded(char* dst, size_t dstSize, const char* src)
        {
            if (!dstSize) return;
            if (!src) { dst[0] = '\0'; return; }
            std::strncpy(dst, src, dstSize - 1);
            dst[dstSize - 1] = '\0';
        }

        // Column 0 is always the row id by cdbc convention, but a registrant can name that field
        // anything -- find whichever field maps to index 0 rather than assuming a name.
        const char* IdFieldName(const RegisteredTable& rt)
        {
            for (uint32_t i = 0; i < rt.definition->fieldCount; ++i)
                if (rt.definition->fields[i].index == 0) return rt.definition->fields[i].name;
            return nullptr;
        }

        // Which field's value to show after the id in the row-picker label. "Comment" if the schema
        // has one (the explicit opt-in every read-write table so far uses), otherwise the first
        // string column that isn't the id -- so a table like InternalRaceNames (ID, InternalName)
        // shows the name it's actually organised by without needing a dummy Comment column. nullptr
        // if there's no string column at all (label stays "ID %u").
        const char* RowLabelFieldName(const RegisteredTable& rt)
        {
            for (uint32_t i = 0; i < rt.definition->fieldCount; ++i)
                if (std::strcmp(rt.definition->fields[i].name, "Comment") == 0)
                    return rt.definition->fields[i].name;
            for (uint32_t i = 0; i < rt.definition->fieldCount; ++i)
            {
                const WXL_SeyrisCdbcField& f = rt.definition->fields[i];
                if (f.index != 0 && f.type == WXL_CDBC_FIELD_STRING) return f.name;
            }
            return nullptr;
        }

        const WXL_SeyrisEditorFieldHint* FindHint(const RegisteredTable& rt, const char* field)
        {
            for (uint32_t i = 0; i < rt.fieldHintCount; ++i)
                if (std::strcmp(rt.fieldHints[i].field, field) == 0) return &rt.fieldHints[i];
            return nullptr;
        }

        const WXL_SeyrisCdbcField* FindField(const RegisteredTable& rt, const char* name)
        {
            for (uint32_t i = 0; i < rt.definition->fieldCount; ++i)
                if (std::strcmp(rt.definition->fields[i].name, name) == 0) return &rt.definition->fields[i];
            return nullptr;
        }

        bool IsRangedNumber(const WXL_SeyrisEditorFieldHint* hint)
        {
            return hint && hint->controlKind == WXL_EDITOR_CONTROL_NUMBER && hint->hasRange;
        }

        bool IsFloatField(const WXL_SeyrisEditorFieldHint* hint)
        {
            return hint && hint->controlKind == WXL_EDITOR_CONTROL_NUMBER && hint->isFloat;
        }

        // WXL-61 item 2: SoundFilterElem's Params0..8 (and any future NUMBER field with isFloat set)
        // store an IEEE-754 float bit-for-bit in the same raw uint32 a plain integer column would
        // use (WXL_CDBC_FIELD_VALUE never distinguishes the two -- "caller reinterprets", see
        // CdbcApi.hpp). These two just do that reinterpretation, not a numeric conversion.
        float BitsToFloat(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof(f)); return f; }
        uint32_t FloatToBits(float f) { uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); return bits; }

        char* FindBuffer(const char* field)
        {
            for (EditBuffer& eb : g_buffers)
                if (eb.field == field) return eb.text;
            return nullptr;
        }

        void RefreshBuffers(const RegisteredTable& rt, void* table, const void* row)
        {
            g_buffers.clear();
            g_buffers.reserve(rt.definition->fieldCount);
            for (uint32_t i = 0; i < rt.definition->fieldCount; ++i)
            {
                const WXL_SeyrisCdbcField& f = rt.definition->fields[i];
                EditBuffer eb;
                eb.field = f.name;
                if (f.type == WXL_CDBC_FIELD_STRING)
                    CopyBounded(eb.text, sizeof(eb.text), wxl_seyris::cdbc::GetString(table, row, f.name));
                else if (IsFloatField(FindHint(rt, f.name)))
                    std::snprintf(eb.text, sizeof(eb.text), "%g",
                                  BitsToFloat(wxl_seyris::cdbc::Value(table, row, f.name, 0)));
                else
                    std::snprintf(eb.text, sizeof(eb.text), "%u", wxl_seyris::cdbc::Value(table, row, f.name, 0));
                g_buffers.push_back(eb);
            }
        }

        uint32_t ParseUint(const char* text)
        {
            uint32_t v = 0;
            for (const char* p = text; *p; ++p)
            {
                if (*p < '0' || *p > '9') break;
                v = v * 10 + static_cast<uint32_t>(*p - '0');
            }
            return v;
        }

        // For a NUMBER field with isFloat set (WXL-61 item 2), e.g. SoundFilterElem's Params0..8.
        float ParseFloat(const char* text)
        {
            return std::strtof(text, nullptr);
        }

        // Decimal, or 0x/0X-prefixed hex -- for the raw-flags-value override field.
        uint32_t ParseFlexibleUint(const char* text)
        {
            if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            {
                uint32_t v = 0;
                for (const char* p = text + 2; *p; ++p)
                {
                    uint32_t d;
                    if (*p >= '0' && *p <= '9')      d = static_cast<uint32_t>(*p - '0');
                    else if (*p >= 'a' && *p <= 'f')  d = static_cast<uint32_t>(*p - 'a') + 10;
                    else if (*p >= 'A' && *p <= 'F')  d = static_cast<uint32_t>(*p - 'A') + 10;
                    else break;
                    v = v * 16 + d;
                }
                return v;
            }
            return ParseUint(text);
        }

        // Purely informational: resolves a hint's namedValues to a label, or "Unknown" -- the field
        // itself always stays freely editable (a modded value outside the known set is legitimate).
        const char* ResolveNamedValue(const WXL_SeyrisEditorFieldHint* hint, uint32_t value)
        {
            if (!hint) return nullptr;
            for (uint32_t i = 0; i < hint->namedValueCount; ++i)
                if (hint->namedValues[i].value == value) return hint->namedValues[i].label;
            return hint->namedValueCount > 0 ? "Unknown" : nullptr;
        }

        // WXL-61 item 1: a field's preview line -- an attached previewFn wins outright, otherwise
        // fall back to the pre-existing static namedValues lookup (only meaningful for a
        // WXL_CDBC_FIELD_VALUE field; harmless no-op otherwise since ParseUint("") is just 0).
        const char* ResolvePreview(const WXL_SeyrisEditorFieldHint* hint, void* table, const void* row,
                                    const char* field, uint32_t rawValue)
        {
            if (!hint) return nullptr;
            if (hint->previewFn) return hint->previewFn(table, row, field, hint->previewUser);
            return ResolveNamedValue(hint, rawValue);
        }

        // WXL-61 item 5: the row-picker label for one row, in priority order -- a fully custom
        // callback, then a registrant-supplied column list (concatenated space-separated), then the
        // pre-existing Comment/first-string-column heuristic. Returns "" (not a field value) when
        // nothing applies, same as the old code's "no label" case.
        std::string ComputeRowLabel(const RegisteredTable& rt, void* table, const void* row)
        {
            namespace c = wxl_seyris::cdbc;

            if (rt.rowLabelFn)
            {
                const char* s = rt.rowLabelFn(table, row, rt.rowLabelUser);
                return s ? std::string(s) : std::string();
            }

            if (rt.labelFieldCount > 0 && rt.labelFields)
            {
                std::string out;
                for (uint32_t i = 0; i < rt.labelFieldCount; ++i)
                {
                    const char* fname = rt.labelFields[i];
                    const WXL_SeyrisCdbcField* f = fname ? FindField(rt, fname) : nullptr;
                    if (!f) continue;
                    if (!out.empty()) out += ' ';
                    if (f->type == WXL_CDBC_FIELD_STRING)
                        out += c::GetString(table, row, fname);
                    else
                    {
                        char num[32];
                        std::snprintf(num, sizeof(num), "%u", c::Value(table, row, fname, 0));
                        out += num;
                    }
                }
                return out;
            }

            if (const char* labelField = RowLabelFieldName(rt))
            {
                const char* s = c::GetString(table, row, labelField);
                if (s) return std::string(s);
            }
            return std::string();
        }
    }

    void Init(const WXL_Api* api)
    {
        g_api = api;
    }

    void RegisterTable(const WXL_SeyrisEditorTable* t)
    {
        if (!t || !t->definition || !t->tableHandle) return;
        RegisteredTable rt;
        rt.definition       = t->definition;
        rt.tableHandle      = t->tableHandle;
        rt.readOnly         = t->readOnly != 0;
        rt.fieldHints       = t->fieldHints;
        rt.fieldHintCount   = t->fieldHintCount;
        rt.rowLabelFn       = t->rowLabelFn;
        rt.rowLabelUser     = t->rowLabelUser;
        rt.labelFields      = t->labelFields;
        rt.labelFieldCount  = t->labelFieldCount;
        rt.source           = t->source;
        rt.sourceModule     = t->sourceModule;
        g_tables.push_back(rt);
    }

    void __cdecl DrawPanel(void* /*user*/)
    {
        namespace c = wxl_seyris::cdbc;

        if (g_tables.empty())
        {
            g_api->UiText("No tables registered.");
            return;
        }

        std::vector<const char*> tableNames;
        tableNames.reserve(g_tables.size());
        for (const RegisteredTable& t : g_tables) tableNames.push_back(t.definition->name);
        g_api->UiCombo("Table", &g_selectedTable, tableNames.data(), static_cast<int>(tableNames.size()));

        RegisteredTable& rt = g_tables[static_cast<size_t>(g_selectedTable)];

        // WXL-61 item 6: data-source badge -- shown regardless of load state, since it describes
        // the registration, not the currently-loaded handle.
        if (rt.source == WXL_EDITOR_SOURCE_STOCK_DBC)
            g_api->UiText("WotLK 3.3.5 DBC");
        else if (rt.sourceModule && rt.sourceModule[0])
        {
            char badge[160];
            std::snprintf(badge, sizeof(badge), "Custom DBC - Source: %s", rt.sourceModule);
            g_api->UiText(badge);
        }

        void* table = *rt.tableHandle;
        if (!table)
        {
            g_api->UiText("Table not currently loaded.");
            return;
        }

        g_api->UiSeparator();

        const uint32_t rowCount = c::RowCount(table);
        if (rowCount == 0)
        {
            g_api->UiText("No rows.");
            return;
        }

        const char* idField = IdFieldName(rt);

        std::vector<std::string> labelStorage;
        std::vector<const char*> labels;
        labelStorage.reserve(rowCount);
        labels.reserve(rowCount);

        std::unordered_map<uint32_t, uint32_t> seenIds; // id -> first row index seen at
        int firstCollisionRow = -1;
        uint32_t collisionCount = 0;

        for (uint32_t i = 0; i < rowCount; ++i)
        {
            const void* row = c::RowAt(table, i);
            char buf[192];
            const uint32_t id = idField ? c::Value(table, row, idField, 0) : i;
            const std::string labelText = ComputeRowLabel(rt, table, row);
            if (!labelText.empty())
                std::snprintf(buf, sizeof(buf), "ID %u: %s", id, labelText.c_str());
            else
                std::snprintf(buf, sizeof(buf), "ID %u", id);
            labelStorage.emplace_back(buf);

            if (idField)
            {
                auto it = seenIds.find(id);
                if (it != seenIds.end())
                {
                    ++collisionCount;
                    if (firstCollisionRow < 0) firstCollisionRow = static_cast<int>(it->second);
                }
                else
                {
                    seenIds.emplace(id, i);
                }
            }
        }
        for (const std::string& s : labelStorage) labels.push_back(s.c_str());

        g_api->UiCombo("Row", &g_selectedRow, labels.data(), static_cast<int>(labels.size()));
        if (g_selectedRow < 0 || static_cast<uint32_t>(g_selectedRow) >= rowCount) g_selectedRow = 0;

        if (collisionCount > 0)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "%u duplicate ID(s) found -- not supported, please fix.", collisionCount);
            g_api->UiText(msg);
            if (g_api->UiButton("Jump to first collision")) g_selectedRow = firstCollisionRow;
        }

        const void* row = c::RowAt(table, static_cast<uint32_t>(g_selectedRow));
        const bool selectionChanged = (g_selectedTable != g_lastSelTable) || (g_selectedRow != g_lastSelRow);
        if (selectionChanged)
        {
            RefreshBuffers(rt, table, row);
            g_lastSelTable = g_selectedTable;
            g_lastSelRow   = g_selectedRow;
        }

        g_api->UiSeparator();

        for (uint32_t i = 0; i < rt.definition->fieldCount; ++i)
        {
            const WXL_SeyrisCdbcField& f = rt.definition->fields[i];
            const WXL_SeyrisEditorFieldHint* hint = FindHint(rt, f.name);

            if (hint && hint->flagBitCount > 0)
            {
                uint32_t v = c::Value(table, row, f.name, 0);
                bool changed = false;
                for (uint32_t b = 0; b < hint->flagBitCount; ++b)
                {
                    int on = (v & hint->flagBits[b].mask) != 0;
                    if (rt.readOnly)
                    {
                        g_api->UiText(hint->flagBits[b].label);
                        continue;
                    }
                    if (g_api->UiCheckbox(hint->flagBits[b].label, &on))
                    {
                        changed = true;
                        if (on) v |= hint->flagBits[b].mask;
                        else    v &= ~hint->flagBits[b].mask;
                    }
                }

                char* rawBuf = FindBuffer(f.name);
                if (changed)
                {
                    c::RowUpdateValue(table, const_cast<void*>(row), f.name, v);
                    if (rawBuf) std::snprintf(rawBuf, sizeof(EditBuffer::text), "%u", v);
                }

                // Raw-number cross-check/bulk-set, decimal or 0x-prefixed hex: lets someone used to
                // the numbers type e.g. "96" to set two bits at once, or verify the checkboxes above
                // add up to what they expect.
                if (!rt.readOnly && rawBuf)
                {
                    char label[80];
                    std::snprintf(label, sizeof(label), "%s (raw value)", f.name);
                    if (g_api->UiInputText(label, rawBuf, sizeof(EditBuffer::text)))
                        c::RowUpdateValue(table, const_cast<void*>(row), f.name, ParseFlexibleUint(rawBuf));
                }
                continue;
            }

            char* buf = FindBuffer(f.name);
            if (!buf) continue;

            if (rt.readOnly)
            {
                char line[300];
                std::snprintf(line, sizeof(line), "%s: %s", f.name, buf);
                g_api->UiTextWrapped(line);
            }
            else if (f.type == WXL_CDBC_FIELD_STRING)
            {
                if (g_api->UiInputText(f.name, buf, sizeof(EditBuffer::text)))
                    c::RowUpdateString(table, const_cast<void*>(row), f.name, buf);
            }
            else if (IsRangedNumber(hint)) // WXL-61 item 2: ranged/stepped NUMBER -> ImGui slider
            {
                const uint32_t raw = c::Value(table, row, f.name, 0);
                if (hint->isFloat)
                {
                    float v = BitsToFloat(raw);
                    if (g_api->UiSliderFloat(f.name, &v, hint->rangeMin, hint->rangeMax))
                    {
                        c::RowUpdateValue(table, const_cast<void*>(row), f.name, FloatToBits(v));
                        std::snprintf(buf, sizeof(EditBuffer::text), "%g", v);
                    }
                }
                else
                {
                    int v = static_cast<int>(raw);
                    if (g_api->UiSliderInt(f.name, &v, static_cast<int>(hint->rangeMin),
                                            static_cast<int>(hint->rangeMax)))
                    {
                        c::RowUpdateValue(table, const_cast<void*>(row), f.name, static_cast<uint32_t>(v));
                        std::snprintf(buf, sizeof(EditBuffer::text), "%u", static_cast<uint32_t>(v));
                    }
                }
            }
            else if (IsFloatField(hint)) // NUMBER, isFloat, but no range -- plain float text input
            {
                if (g_api->UiInputText(f.name, buf, sizeof(EditBuffer::text)))
                    c::RowUpdateValue(table, const_cast<void*>(row), f.name, FloatToBits(ParseFloat(buf)));
            }
            else
            {
                if (g_api->UiInputText(f.name, buf, sizeof(EditBuffer::text)))
                    c::RowUpdateValue(table, const_cast<void*>(row), f.name, ParseUint(buf));
            }

            // WXL-61 item 1: previewFn (if attached) wins outright; otherwise the pre-existing
            // static namedValues lookup, unchanged. previewRaw is read straight from the table
            // (post-write, if this field just changed above) rather than reparsing buf.
            const uint32_t previewRaw = (f.type == WXL_CDBC_FIELD_STRING) ? 0 : c::Value(table, row, f.name, 0);
            if (const char* preview = ResolvePreview(hint, table, row, f.name, previewRaw))
            {
                char line[192];
                std::snprintf(line, sizeof(line), "  -> %s", preview);
                g_api->UiTextWrapped(line);
            }
        }

        g_api->UiSeparator();

        // Available even for read-only tables: picks up an edit made outside the game (an external
        // DBC editor doing a bulk change) without needing to restart -- exactly the manual hot-reload
        // the owner originally asked for, just as a button instead of a slash command (see WXL-21).
        if (g_api->UiButton("Reload from Disk"))
        {
            char err[256] = {};
            void* fresh = c::Reload(table, err, sizeof(err));
            if (fresh)
            {
                *rt.tableHandle = fresh;
                g_lastSelTable = -1;
                g_api->Log(WXL_LOG_INFO, "wxl-seyris-tools", "editor: reloaded '%s'", rt.definition->name);
            }
            else
            {
                g_api->Log(WXL_LOG_WARN, "wxl-seyris-tools",
                           "editor: reload failed for '%s': %s", rt.definition->name, err);
            }
        }

        if (rt.readOnly) return;

        g_api->UiSameLine();
        if (g_api->UiButton("New Row"))
        {
            c::RowAdd(table); // row/buffer pointers may now be stale -- nothing below reuses them
            g_lastSelTable = -1; // force a full refresh next frame
        }
        g_api->UiSameLine();
        if (g_api->UiButton("Delete Row"))
        {
            c::RowDelete(table, const_cast<void*>(row));
            g_selectedRow  = 0;
            g_lastSelTable = -1;
        }
        g_api->UiSameLine();
        if (g_api->UiButton("Save"))
        {
            char err[256] = {};
            if (c::Save(table, err, sizeof(err)))
            {
                void* fresh = c::Reload(table, err, sizeof(err));
                if (fresh)
                {
                    *rt.tableHandle = fresh;
                    g_lastSelTable = -1; // the old handle's rows are gone -- force a full refresh
                    g_api->Log(WXL_LOG_INFO, "wxl-seyris-tools", "editor: saved '%s'", rt.definition->name);
                }
                else
                {
                    g_api->Log(WXL_LOG_WARN, "wxl-seyris-tools",
                               "editor: '%s' saved but reload failed: %s", rt.definition->name, err);
                }
            }
            else
            {
                g_api->Log(WXL_LOG_WARN, "wxl-seyris-tools", "editor: save failed for '%s': %s",
                           rt.definition->name, err);
            }
        }
    }
}
