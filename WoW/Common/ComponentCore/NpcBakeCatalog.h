#ifndef WOW_COMMON_COMPONENTCORE_NPCBAKECATALOG_H
#define WOW_COMMON_COMPONENTCORE_NPCBAKECATALOG_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>

inline std::map<uint32_t, std::string> ParseNpcBakeCatalog(const unsigned char* bytes, size_t size) {
    std::map<uint32_t, std::string> result;
    if (!bytes || size < 12) return result;
    uint32_t header[3];
    std::memcpy(header, bytes, sizeof(header));
    if (header[0] != 0x424E4541 || header[1] != 1 || header[2] > 4096 ||
        size != 12 + size_t(header[2]) * 68) return result;
    for (uint32_t i = 0; i < header[2]; ++i) {
        const unsigned char* row = bytes + 12 + size_t(i) * 68;
        uint32_t id;
        std::memcpy(&id, row, sizeof(id));
        const char* name = reinterpret_cast<const char*>(row + 4);
        if (!id || !*name || !std::memchr(name, 0, 64)) return {};
        for (const char* c = name; *c; ++c)
            if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.')) return {};
        if (!result.emplace(id, name).second) return {};
    }
    return result;
}

#endif
