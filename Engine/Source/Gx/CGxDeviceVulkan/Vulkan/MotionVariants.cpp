// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: see MotionVariants.hpp.
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/MotionVariants.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Log.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace gxvk::vk::motionvariants
{
    namespace
    {
        struct Paths { std::string first, second; };
        std::unordered_map<std::string, Paths> table[size_t(Kind::Count)];
        bool loaded = false;
        Stats stats;

        bool KindOf(const char* s, Kind& k)
        {
            struct Name { const char* text; Kind kind; };
            static const Name names[] = {
                {"vel", Kind::Rigid}, {"velbones", Kind::Bones}, {"skinbones", Kind::SkinBones}, {"wind", Kind::Wind},
                {"windbones", Kind::WindBones}, {"grass", Kind::Grass}, {"velfrag", Kind::PixelVelocity}, {"velzero", Kind::PixelZero},
                {"ownvel", Kind::OwnVelocity}};
            for (const Name& n : names) if (!std::strcmp(s, n.text)) { k = n.kind; return true; }
            return false;
        }
        bool Md5(const char* s)
        {
            if (std::strlen(s) != 32) return false;
            for (int i = 0; i < 32; ++i) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
            return true;
        }
    }

    bool Load(const char* folder)
    {
        Unload();
        if (!folder || !*folder) return false;
        std::string path = std::string(folder) + "/velocity/velocity-index.txt";
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) { path = std::string(folder) + "\\velocity\\velocity-index.txt"; f = std::fopen(path.c_str(), "rb"); }
        if (!f) return false;
        char line[512];
        while (std::fgets(line, sizeof line, f))
        {
            char* hash = std::strchr(line, '#'); if (hash) *hash = 0;
            char kind[24] = {}, md5[40] = {}, p1[200] = {}, p2[200] = {};
            const int n = std::sscanf(line, "%23s %39s %199s %199s", kind, md5, p1, p2);
            if (n < 3) continue;
            ++stats.lines;
            Kind k;
            if (!KindOf(kind, k) || !Md5(md5)) { ++stats.skipped; continue; }
            table[size_t(k)][md5] = Paths{p1, n >= 4 ? p2 : std::string()};
            ++stats.perKind[size_t(k)];
        }
        std::fclose(f);
        loaded = true;
        return true;
    }

    void Unload()
    {
        for (auto& t : table) t.clear();
        loaded = false;
        stats = Stats();
    }

    bool Loaded() { return loaded; }

    const char* Path(const char* md5, Kind kind)
    {
        if (!loaded || !md5 || kind >= Kind::Count) return nullptr;
        auto it = table[size_t(kind)].find(md5);
        if (it == table[size_t(kind)].end()) return nullptr;
        return it->second.second.empty() ? it->second.first.c_str() : it->second.second.c_str();
    }

    const char* BasePath(const char* md5, Kind kind)
    {
        if (!loaded || !md5 || kind >= Kind::Count) return nullptr;
        auto it = table[size_t(kind)].find(md5);
        return it == table[size_t(kind)].end() ? nullptr : it->second.first.c_str();
    }

    Stats GetStats() { return stats; }
}
