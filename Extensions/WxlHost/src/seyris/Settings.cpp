#include "Settings.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace wxl_seyris::settings
{
    namespace
    {
        constexpr char kConfigDir[]         = "WTF\\WXL";
        constexpr char kDefaultConfigPath[] = "WTF\\WXL\\WarcraftXL.ini";
        // Leading ".\\" is load-bearing, not decorative: GetPrivateProfileString/Int treat a bare
        // filename with NO path separator at all as special-cased -- they look in the WINDOWS
        // directory instead of the current one (a legacy quirk of that whole API family, unrelated
        // to how fopen or GetFileAttributesA resolve relative paths). Without it, every read here
        // would silently check a file in C:\Windows that never exists, always report "key absent",
        // and re-append a duplicate default entry on every single query -- confirmed for real
        // (2026-08-25): WriteDefault's own fopen calls have no such quirk and correctly wrote to
        // the real root file each time, which is exactly what made the duplication visible.
        constexpr char kRootConfigPath[]    = ".\\WarcraftXL.ini";
        // GetPrivateProfileIntA's default, distinct from any real bool value (0/1), used to
        // detect "key absent" instead of "key present and false".
        constexpr int kAbsentSentinel = 2;

        // WXL-40: an owner can manually drop WarcraftXL.ini directly in the client root instead of
        // WTF\WXL. Detected once via a single GetFileAttributesA and cached -- every BoolSetting
        // construction resolves to the SAME path, not a fresh disk check per setting. Root wins if
        // a real file (not a same-named directory) is found there; otherwise falls back to the
        // original WTF\WXL location unchanged, which WriteDefault below still creates (directory
        // included) if it doesn't exist yet either -- exactly the prior default behavior when no
        // root override is present.
        const char* ConfigPath() noexcept
        {
            static const char* const path = []() -> const char* {
                const DWORD attrs = GetFileAttributesA(kRootConfigPath);
                const bool rootExists = attrs != INVALID_FILE_ATTRIBUTES &&
                                         !(attrs & FILE_ATTRIBUTE_DIRECTORY);
                return rootExists ? kRootConfigPath : kDefaultConfigPath;
            }();
            return path;
        }

        // Adds "; description\nkey=default\n" for a missing key, under [ns] -- reusing an
        // existing [ns] header if one is already in the file instead of blindly appending a new
        // one. Found 2026-08-18: the old append-only version created a fresh "[ns]" block per
        // missing key, so two settings registered under the same namespace on the same run (or
        // across runs) produced duplicate headers -- and GetPrivateProfileString/Int do NOT merge
        // duplicate section names, they only search the FIRST occurrence of a matching header. A
        // key that ends up under a later duplicate becomes permanently invisible to future reads,
        // which re-triggers this same "missing key" path and appends yet another duplicate on
        // every single launch, forever.
        void WriteDefault(const char* ns, const char* key, const char* description, const char* defaultValue) noexcept
        {
            const char* configPath = ConfigPath();
            // Only create WTF\WXL when that's actually where we're writing -- a root-placed ini
            // (WXL-40) has no reason to leave an empty WTF\WXL directory behind.
            if (configPath == kDefaultConfigPath)
            {
                CreateDirectoryA("WTF", nullptr);
                CreateDirectoryA(kConfigDir, nullptr);
            }

            std::string content;
#pragma warning(suppress: 4996)
            if (FILE* in = std::fopen(configPath, "rb"))
            {
                std::fseek(in, 0, SEEK_END);
                const long size = std::ftell(in);
                std::fseek(in, 0, SEEK_SET);
                if (size > 0)
                {
                    content.resize(static_cast<size_t>(size));
                    std::fread(content.data(), 1, static_cast<size_t>(size), in);
                }
                std::fclose(in);
            }

            char entry[1024];
            std::snprintf(entry, sizeof(entry), "; %s\n%s=%s\n", description, key, defaultValue);

            char header[256];
            std::snprintf(header, sizeof(header), "[%s]", ns);
            const size_t headerPos = content.find(header);

            if (headerPos == std::string::npos)
            {
                content += "\n[";
                content += ns;
                content += "]\n";
                content += entry;
            }
            else
            {
                size_t insertPos = content.find('\n', headerPos);
                insertPos = (insertPos == std::string::npos) ? content.size() : insertPos + 1;
                content.insert(insertPos, entry);
            }

#pragma warning(suppress: 4996)
            FILE* out = std::fopen(configPath, "wb");
            if (!out) return;
            std::fwrite(content.data(), 1, content.size(), out);
            std::fclose(out);
        }
    }

    BoolSetting::BoolSetting(const char* ns, const char* key, const char* description, bool defaultValue) noexcept
    {
        const int existing = static_cast<int>(GetPrivateProfileIntA(ns, key, kAbsentSentinel, ConfigPath()));
        if (existing == kAbsentSentinel)
        {
            WriteDefault(ns, key, description, defaultValue ? "1" : "0");
            value_ = defaultValue;
        }
        else
        {
            value_ = existing != 0;
        }
    }

    FloatSetting::FloatSetting(const char* ns, const char* key, const char* description, float defaultValue) noexcept
        : value_(defaultValue)
    {
        char text[64] = {};
        GetPrivateProfileStringA(ns, key, "", text, sizeof(text), ConfigPath());
        if (!text[0])
        {
            char formatted[32];
            std::snprintf(formatted, sizeof(formatted), "%g", defaultValue);
            WriteDefault(ns, key, description, formatted);
            return;
        }
        char* end = nullptr;
        const float parsed = std::strtof(text, &end);
        if (end != text) value_ = parsed;
    }
}
