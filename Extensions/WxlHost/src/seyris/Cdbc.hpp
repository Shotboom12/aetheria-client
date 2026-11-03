// wxl-seyris-tools: internal implementation behind "seyris.cdbc" (see wxl_seyris/CdbcApi.hpp for
// the published shape). Classic WDBC binary format: 'WDBC' magic, fixed header, fixed-width records
// (one uint32 slot per column), trailing string block for string-typed fields -- see
// orchestration/docs/r&d/researched/dbc-format.md. ".cdbc" is the same format under a
// non-default-table naming convention, not a new format.

#pragma once

#include "wxl_seyris/CdbcApi.hpp"

namespace wxl_seyris::cdbc
{
    void*       Load(const WXL_SeyrisCdbcDefinition* def, char* errorBuf, size_t errorBufSize);
    void        Release(void* table);
    uint32_t    RowCount(void* table);
    const void* RowAt(void* table, uint32_t index);
    const void* FindRow(void* table, uint32_t id);
    uint32_t    Value(void* table, const void* row, const char* field, uint32_t element);
    const char* GetString(void* table, const void* row, const char* field);
    size_t      FieldIndex(void* table, const char* field);
    const void* FindRowByField(void* table, const char* field, uint32_t value, const void* after);

    void*       RowAdd(void* table);
    int         RowUpdateValue(void* table, void* row, const char* field, uint32_t value);
    int         RowUpdateString(void* table, void* row, const char* field, const char* value);
    int         RowDelete(void* table, void* row);
    int         Save(void* table, char* errorBuf, size_t errorBufSize);
    void*       Reload(void* table, char* errorBuf, size_t errorBufSize);
}
