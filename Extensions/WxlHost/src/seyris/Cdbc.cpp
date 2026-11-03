#include "Cdbc.hpp"

// x64 port: the client's SFile layer is reached through the in-tree BlizzardCore API instead of
// wxl::game::io (the WXL SDK's fixed-address wrappers, 32-bit Wow.exe only).
#include "Engine/Source/BlizzardCore/SFile2-Core.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>

namespace wxl_seyris::cdbc
{
    namespace
    {
        constexpr uint32_t kHeaderSize = 20; // magic + recordCount + fieldCount + recordSize + stringBlockSize

        struct FieldEntry
        {
            const char* name;
            uint32_t    index;
            uint32_t    type;
        };

        struct Table
        {
            std::vector<uint8_t> records;     // recordCount * recordSize bytes, flat
            std::vector<uint8_t> stringBlock; // offset 0 is always a leading NUL (the empty string)
            std::vector<FieldEntry> fields;
            uint32_t    recordCount = 0;
            uint32_t    columnCount = 0; // recordSize / 4 -- the table's real column count, may exceed fields.size()
            uint32_t    recordSize  = 0; // bytes per record
            const char* name        = ""; // static storage in the caller (string literal), safe to keep as-is
            std::string filename;         // owned copy -- needed by Save()/Reload(), not just transiently by Load()
        };

        void SetError(char* errorBuf, size_t errorBufSize, const char* fmt, ...)
        {
            if (!errorBuf || errorBufSize == 0) return;
            va_list args;
            va_start(args, fmt);
            std::vsnprintf(errorBuf, errorBufSize, fmt, args);
            va_end(args);
        }

        // Reads a whole file through the client's SFile layer: the mounted archive chain, plus
        // whatever loose-file override the client itself resolved AT STARTUP. This is the only path
        // that can reach a cdbc packed inside an MPQ -- but see the WXL-60 note on ReadGameFile
        // below for why it is NOT tried first any more.
        bool ReadViaClientIo(const char* path, std::vector<uint8_t>& out)
        {
            void* data = nullptr;
            uint32_t size = 0;
            if (!SFile::LoadFile(path, &data, &size, 0, nullptr) || !data)
                return false;

            bool ok = false;
            if (size > 0)
            {
                out.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
                ok = true;
            }
            SFileUnloadFile(data);
            return ok;
        }

        // Reads a whole loose file straight off disk via the CRT, relative to the process working
        // directory (the client root -- the same assumption Settings.cpp and Save() below already
        // rely on). Cannot see inside an MPQ; that is ReadViaClientIo's job. Tried FIRST by
        // ReadGameFile below -- see that function's comment for why.
        bool ReadViaFopen(const char* path, std::vector<uint8_t>& out)
        {
            if (!path || !*path) return false;
#pragma warning(suppress: 4996)
            FILE* f = std::fopen(path, "rb");
            if (!f) return false;

            bool ok = false;
            if (std::fseek(f, 0, SEEK_END) == 0)
            {
                const long size = std::ftell(f);
                if (size > 0 && std::fseek(f, 0, SEEK_SET) == 0)
                {
                    out.resize(static_cast<size_t>(size));
                    const size_t got = std::fread(out.data(), 1, out.size(), f);
                    ok = (got == out.size());
                    if (!ok) out.clear();
                }
            }
            std::fclose(f);
            return ok;
        }

        // WXL-60 finding (2026-09-05): a cdbc that exists BOTH inside an MPQ and, after this
        // reader's own Save() writes it, as a loose DBFilesClient file -- e.g. SoundFilter.dbc --
        // kept reading back the stale MPQ-packed bytes on the very next Reload(), even though the
        // fresh file was sitting right there on disk. Root cause: the client's own SFile virtual
        // filesystem resolves/caches its archive+loose-file view once at startup and never rescans;
        // a loose file that didn't exist yet at that point stays invisible to ReadViaClientIo for
        // the rest of the session (a full restart is the only fix at the client-engine level -- the
        // same behaviour the owner separately observed for a texture added to disk after a model
        // referencing it was already loaded). This reader controls its own reads, though, so it can
        // just not depend on the client's stale view: try the loose file on disk FIRST, unconditionally.
        // This is also the objectively correct precedence, not a workaround -- a loose file is
        // supposed to override an MPQ-packed one on this client, full stop.
        bool ReadGameFile(const char* path, std::vector<uint8_t>& out)
        {
            // 1. Direct CRT fopen, path byte-for-byte unchanged (Blizzard-style '\' separators):
            //    always current, never subject to the client's startup-time file-list cache above.
            if (ReadViaFopen(path, out)) return true;

            // 2. Client SFile: the only path that can reach a cdbc packed inside an MPQ with no
            //    loose override at all.
            if (ReadViaClientIo(path, out)) return true;

            // 3. Last resort, reachable only once every attempt above has already failed: retry
            //    both of the above with '/' for any CRT/platform that rejects a backslash in a
            //    loose path (observed on at least one non-Windows runtime).
            if (path)
            {
                std::string alt(path);
                std::replace(alt.begin(), alt.end(), '\\', '/');
                if (alt != path)
                {
                    if (ReadViaFopen(alt.c_str(), out)) return true;
                    if (ReadViaClientIo(alt.c_str(), out)) return true;
                }
            }

            return false;
        }

        const FieldEntry* FindField(const Table* table, const char* field)
        {
            if (!table || !field) return nullptr;
            for (const FieldEntry& f : table->fields)
                if (std::strcmp(f.name, field) == 0)
                    return &f;
            return nullptr;
        }

        uint32_t ReadU32(const uint8_t* p)
        {
            uint32_t v;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        void WriteU32(uint8_t* p, uint32_t v)
        {
            std::memcpy(p, &v, sizeof(v));
        }
    }

    void* Load(const WXL_SeyrisCdbcDefinition* def, char* errorBuf, size_t errorBufSize)
    {
        if (!def || !def->filename)
        {
            SetError(errorBuf, errorBufSize, "cdbc: null definition or filename");
            return nullptr;
        }

        std::vector<uint8_t> bytes;
        if (!ReadGameFile(def->filename, bytes))
        {
            SetError(errorBuf, errorBufSize,
                      "cdbc: '%s' (%s) could not be opened -- not in any mounted archive, and no loose "
                      "file found on disk relative to the client root",
                      def->name, def->filename);
            return nullptr;
        }
        if (bytes.size() < kHeaderSize)
        {
            SetError(errorBuf, errorBufSize, "cdbc: '%s' is smaller than a WDBC header", def->name);
            return nullptr;
        }
        if (bytes[0] != 'W' || bytes[1] != 'D' || bytes[2] != 'B' || bytes[3] != 'C')
        {
            SetError(errorBuf, errorBufSize, "cdbc: '%s' has no WDBC magic", def->name);
            return nullptr;
        }

        const uint32_t recordCount     = ReadU32(bytes.data() + 4);
        const uint32_t fieldCount      = ReadU32(bytes.data() + 8);
        const uint32_t recordSize      = ReadU32(bytes.data() + 12);
        const uint32_t stringBlockSize = ReadU32(bytes.data() + 16);

        if (recordSize == 0 || recordSize % 4 != 0)
        {
            SetError(errorBuf, errorBufSize,
                      "cdbc: '%s' has a record size (%u) this reader can't handle (v1 supports only flat, uniform 4-byte columns)",
                      def->name, recordSize);
            return nullptr;
        }
        if (fieldCount * 4 != recordSize)
        {
            SetError(errorBuf, errorBufSize,
                      "cdbc: '%s' header field count (%u) disagrees with record size (%u bytes) -- not a flat uint32-column table",
                      def->name, fieldCount, recordSize);
            return nullptr;
        }

        const uint64_t recordsBytes = static_cast<uint64_t>(recordCount) * recordSize;
        const uint64_t need = static_cast<uint64_t>(kHeaderSize) + recordsBytes + stringBlockSize;
        if (bytes.size() < need)
        {
            SetError(errorBuf, errorBufSize, "cdbc: '%s' is truncated (have %zu bytes, need %llu)",
                      def->name, bytes.size(), static_cast<unsigned long long>(need));
            return nullptr;
        }

        const uint32_t columnCount = recordSize / 4;
        for (uint32_t i = 0; i < def->fieldCount; ++i)
        {
            if (def->fields[i].index >= columnCount)
            {
                SetError(errorBuf, errorBufSize,
                          "cdbc: '%s' field '%s' has index %u but the table only has %u columns",
                          def->name, def->fields[i].name, def->fields[i].index, columnCount);
                return nullptr;
            }
        }

        auto* table = new Table();
        table->records.assign(bytes.begin() + kHeaderSize, bytes.begin() + kHeaderSize + recordsBytes);
        table->stringBlock.assign(bytes.begin() + kHeaderSize + recordsBytes, bytes.end());
        table->recordCount = recordCount;
        table->columnCount = columnCount;
        table->recordSize  = recordSize;
        table->name        = def->name ? def->name : "";
        table->filename     = def->filename;
        table->fields.reserve(def->fieldCount);
        for (uint32_t i = 0; i < def->fieldCount; ++i)
            table->fields.push_back({def->fields[i].name, def->fields[i].index, def->fields[i].type});

        return table;
    }

    void Release(void* handle)
    {
        delete static_cast<Table*>(handle);
    }

    uint32_t RowCount(void* handle)
    {
        auto* table = static_cast<Table*>(handle);
        return table ? table->recordCount : 0;
    }

    const void* RowAt(void* handle, uint32_t index)
    {
        auto* table = static_cast<Table*>(handle);
        if (!table || index >= table->recordCount) return nullptr;
        return table->records.data() + static_cast<uint64_t>(index) * table->recordSize;
    }

    const void* FindRow(void* handle, uint32_t id)
    {
        auto* table = static_cast<Table*>(handle);
        if (!table || table->recordCount == 0) return nullptr;

        uint32_t lo = 0, hi = table->recordCount; // [lo, hi)
        while (lo < hi)
        {
            const uint32_t mid = lo + (hi - lo) / 2;
            const uint8_t* row = table->records.data() + static_cast<uint64_t>(mid) * table->recordSize;
            const uint32_t rowId = ReadU32(row);
            if (rowId == id) return row;
            if (rowId < id) lo = mid + 1;
            else hi = mid;
        }
        return nullptr;
    }

    uint32_t Value(void* handle, const void* row, const char* field, uint32_t element)
    {
        auto* table = static_cast<Table*>(handle);
        if (!table || !row || element != 0) return 0;
        const FieldEntry* f = FindField(table, field);
        if (!f) return 0;
        return ReadU32(static_cast<const uint8_t*>(row) + static_cast<uint64_t>(f->index) * 4);
    }

    const char* GetString(void* handle, const void* row, const char* field)
    {
        static const char kEmpty[] = "";
        auto* table = static_cast<Table*>(handle);
        if (!table || !row) return kEmpty;
        const FieldEntry* f = FindField(table, field);
        if (!f) return kEmpty;

        const uint32_t offset = ReadU32(static_cast<const uint8_t*>(row) + static_cast<uint64_t>(f->index) * 4);
        if (offset >= table->stringBlock.size()) return kEmpty;
        return reinterpret_cast<const char*>(table->stringBlock.data() + offset);
    }

    size_t FieldIndex(void* handle, const char* field)
    {
        auto* table = static_cast<Table*>(handle);
        if (!table || !field) return static_cast<size_t>(-1);
        for (size_t i = 0; i < table->fields.size(); ++i)
            if (std::strcmp(table->fields[i].name, field) == 0)
                return i;
        return static_cast<size_t>(-1);
    }

    const void* FindRowByField(void* handle, const char* field, uint32_t value, const void* after)
    {
        auto* table = static_cast<Table*>(handle);
        if (!table) return nullptr;
        const FieldEntry* f = FindField(table, field);
        if (!f) return nullptr;

        const uint8_t* base = table->records.data();
        uint32_t startIndex = 0;
        if (after)
        {
            const auto* afterPtr = static_cast<const uint8_t*>(after);
            startIndex = static_cast<uint32_t>((afterPtr - base) / table->recordSize) + 1;
        }

        for (uint32_t i = startIndex; i < table->recordCount; ++i)
        {
            const uint8_t* row = base + static_cast<uint64_t>(i) * table->recordSize;
            if (ReadU32(row + static_cast<uint64_t>(f->index) * 4) == value)
                return row;
        }
        return nullptr;
    }

    // ─── Write support (WXL-17) ─────────────────────────────────────────────────────────────────
    // Every mutating call below (RowAdd/RowUpdateString/RowDelete) can reallocate/shift table->records
    // or table->stringBlock -- ANY previously-returned row pointer for this table must be treated as
    // invalid after any of them, not just after Save/Reload. Callers needing a row across a mutation
    // must re-fetch it (FindRow/FindRowByField) afterward.

    void* RowAdd(void* handle)
    {
        auto* table = static_cast<Table*>(handle);
        if (!table) return nullptr;

        // id = one past the highest existing id (not the lowest free one -- gaps from deleted rows
        // are left alone, matching how every other id-assigning system in this project works), or 1
        // if the table is currently empty. Every other column stays zero-filled (every string field
        // reads as "" via offset 0).
        uint32_t nextId = 1;
        for (uint32_t i = 0; i < table->recordCount; ++i)
        {
            const uint32_t id = ReadU32(table->records.data() + static_cast<uint64_t>(i) * table->recordSize);
            if (id >= nextId) nextId = id + 1;
        }

        const size_t offset = table->records.size();
        table->records.resize(offset + table->recordSize, 0);
        WriteU32(table->records.data() + offset, nextId);
        table->recordCount += 1;
        return table->records.data() + offset;
    }

    int RowUpdateValue(void* handle, void* row, const char* field, uint32_t value)
    {
        auto* table = static_cast<Table*>(handle);
        if (!table || !row) return 0;
        const FieldEntry* f = FindField(table, field);
        if (!f) return 0;
        WriteU32(static_cast<uint8_t*>(row) + static_cast<uint64_t>(f->index) * 4, value);
        return 1;
    }

    int RowUpdateString(void* handle, void* row, const char* field, const char* value)
    {
        auto* table = static_cast<Table*>(handle);
        if (!table || !row) return 0;
        const FieldEntry* f = FindField(table, field);
        if (!f) return 0;

        // Appended, not patched in place -- the old bytes become orphaned dead space, reclaimed the
        // next time Save() rebuilds the string block from scratch. Simpler and just as correct as
        // in-place patching, which isn't possible anyway once a string grows.
        if (!value) value = "";
        const uint32_t newOffset = static_cast<uint32_t>(table->stringBlock.size());
        const size_t len = std::strlen(value) + 1; // include the NUL
        table->stringBlock.insert(table->stringBlock.end(), value, value + len);
        WriteU32(static_cast<uint8_t*>(row) + static_cast<uint64_t>(f->index) * 4, newOffset);
        return 1;
    }

    int RowDelete(void* handle, void* row)
    {
        auto* table = static_cast<Table*>(handle);
        if (!table || !row) return 0;

        uint8_t* base = table->records.data();
        auto* r = static_cast<uint8_t*>(row);
        if (r < base || r >= base + table->records.size()) return 0; // not a row from this table
        const size_t offset = static_cast<size_t>(r - base);
        if (offset % table->recordSize != 0) return 0; // misaligned -- not a valid row pointer

        table->records.erase(table->records.begin() + static_cast<long>(offset),
                              table->records.begin() + static_cast<long>(offset + table->recordSize));
        table->recordCount -= 1;
        return 1;
    }

    int Save(void* handle, char* errorBuf, size_t errorBufSize)
    {
        auto* table = static_cast<Table*>(handle);
        if (!table)
        {
            SetError(errorBuf, errorBufSize, "cdbc: Save() called with a null table");
            return 0;
        }
        // Every real column must be named, or rebuilding the string block below would silently
        // corrupt any string-typed column this schema doesn't know about. Fail loudly instead --
        // this is the scoping call WXL-17 made: Save() is for tables a schema fully describes
        // (true for any table this project defines itself), not partial views over a stock DBC
        // with columns nobody's named (that's WXL-23's separate, differently-shaped problem).
        if (table->fields.size() != table->columnCount)
        {
            SetError(errorBuf, errorBufSize,
                      "cdbc: '%s' cannot be saved -- schema names %zu of %u real columns; Save() requires "
                      "every column to be named so the string block can be rebuilt safely",
                      table->name, table->fields.size(), table->columnCount);
            return 0;
        }

        // RowAdd only ever appends, so the record order may no longer be ID-sorted -- restore that
        // invariant here, since FindRow's binary search depends on it (FindRowByField doesn't, so
        // this isn't required just to keep FindRowByField-only tables like ItemDisplayInfoExtra.cdbc
        // working, but it's cheap and correct to do unconditionally rather than leave a landmine for
        // any table that does use FindRow).
        if (table->recordCount > 1)
        {
            std::vector<uint32_t> order(table->recordCount);
            for (uint32_t i = 0; i < table->recordCount; ++i) order[i] = i;
            std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
                return ReadU32(table->records.data() + static_cast<uint64_t>(a) * table->recordSize)
                     < ReadU32(table->records.data() + static_cast<uint64_t>(b) * table->recordSize);
            });
            std::vector<uint8_t> sorted;
            sorted.reserve(table->records.size());
            for (uint32_t idx : order)
            {
                const uint8_t* row = table->records.data() + static_cast<uint64_t>(idx) * table->recordSize;
                sorted.insert(sorted.end(), row, row + table->recordSize);
            }
            table->records = std::move(sorted);
        }

        // Rebuild the string block from scratch, remapping every string-typed field's offset --
        // reclaims dead space from RowUpdateString's append-only writes and keeps this correct
        // regardless of how many edits happened since Load().
        std::vector<uint8_t> newStrings;
        newStrings.push_back(0); // offset 0 = empty string, matching Load()'s own convention
        for (uint32_t i = 0; i < table->recordCount; ++i)
        {
            uint8_t* row = table->records.data() + static_cast<uint64_t>(i) * table->recordSize;
            for (const FieldEntry& f : table->fields)
            {
                if (f.type != WXL_CDBC_FIELD_STRING) continue;
                const uint32_t oldOffset = ReadU32(row + static_cast<uint64_t>(f.index) * 4);
                const char* s = (oldOffset < table->stringBlock.size())
                               ? reinterpret_cast<const char*>(table->stringBlock.data() + oldOffset) : "";
                const uint32_t newOffset = static_cast<uint32_t>(newStrings.size());
                const size_t len = std::strlen(s) + 1;
                newStrings.insert(newStrings.end(), s, s + len);
                WriteU32(row + static_cast<uint64_t>(f.index) * 4, newOffset);
            }
        }
        table->stringBlock = std::move(newStrings);

        std::vector<uint8_t> out;
        out.reserve(kHeaderSize + table->records.size() + table->stringBlock.size());
        out.push_back('W'); out.push_back('D'); out.push_back('B'); out.push_back('C');
        uint8_t hdr[16];
        WriteU32(hdr + 0,  table->recordCount);
        WriteU32(hdr + 4,  table->columnCount);
        WriteU32(hdr + 8,  table->recordSize);
        WriteU32(hdr + 12, static_cast<uint32_t>(table->stringBlock.size()));
        out.insert(out.end(), hdr, hdr + 16);
        out.insert(out.end(), table->records.begin(), table->records.end());
        out.insert(out.end(), table->stringBlock.begin(), table->stringBlock.end());

        // Only loose files are writable: wxl::game::io has no write primitive (the client's own
        // file-open is read-only from an extension's side), and a virtual path resolved from inside
        // an MPQ isn't a real filesystem path fopen can target anyway. Every cdbc file this project
        // has worked with all session has been a loose DBFilesClient file -- the only case that
        // actually matters for custom, actively-authored content -- so this is a deliberate scoping
        // choice, not an oversight: writing into an MPQ-packed cdbc simply isn't supported.
        //
        // Always back up whatever's currently on disk first, unconditionally -- no setting gates
        // this, cheap insurance with no real downside (WXL-16 decision, 2026-08-16).
        CopyFileA(table->filename.c_str(), (table->filename + ".bak").c_str(), FALSE);

#pragma warning(suppress: 4996)
        FILE* f = std::fopen(table->filename.c_str(), "wb");
        if (!f)
        {
            SetError(errorBuf, errorBufSize, "cdbc: '%s' could not be opened for writing at '%s' "
                      "(only loose files are writable, not files packed inside an MPQ)",
                      table->name, table->filename.c_str());
            return 0;
        }
        const size_t written = std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        if (written != out.size())
        {
            SetError(errorBuf, errorBufSize, "cdbc: '%s' write was incomplete (%zu of %zu bytes)",
                      table->name, written, out.size());
            return 0;
        }
        return 1;
    }

    void* Reload(void* handle, char* errorBuf, size_t errorBufSize)
    {
        auto* table = static_cast<Table*>(handle);
        if (!table)
        {
            SetError(errorBuf, errorBufSize, "cdbc: Reload() called with a null table");
            return nullptr;
        }

        std::vector<WXL_SeyrisCdbcField> fields;
        fields.reserve(table->fields.size());
        for (const FieldEntry& f : table->fields)
            fields.push_back(WXL_SeyrisCdbcField{f.name, f.index, f.type});

        const WXL_SeyrisCdbcDefinition def = {
            table->name, table->filename.c_str(), fields.data(), static_cast<uint32_t>(fields.size()),
        };
        void* fresh = Load(&def, errorBuf, errorBufSize);
        if (!fresh) return nullptr;

        delete table; // old handle, and every row pointer ever returned for it, are now invalid
        return fresh;
    }
}
