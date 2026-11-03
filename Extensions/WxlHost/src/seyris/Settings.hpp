// wxl-seyris-tools: internal settings registry, backed by WTF\WXL\WarcraftXL.ini. Port of
// wxl-equip-extension's legacy-pre-1.1-src/Settings.hpp, moved to its intended shared home (see
// that file's own comment: "this belongs in core, not in an equipment-specific module"). Not a
// published interface by itself -- Extension.cpp exposes a stateless GetBool() on "seyris.settings"
// instead, since a C++ class can't safely cross the DLL boundary (see wxl_seyris/SettingsApi.hpp).
//
// Distinct from wxl-dehardcoding's WTF\WXL\Settings.ini -- do not point another writer at that
// file.

#pragma once

namespace wxl_seyris::settings
{
    /**
     * @brief A registered boolean setting, backed by WTF\WXL\WarcraftXL.ini.
     *
     * Construct one instance per (ns, key) query, e.g.:
     *   BoolSetting enabled("MyModule", "Enabled", "What this does.", true);
     * On construction, writes the [namespace] section with the description as a leading comment
     * and the default value if the key is not already present, then reads and caches the resolved
     * value (existing file contents always win over the default).
     */
    class BoolSetting
    {
    public:
        BoolSetting(const char* ns, const char* key, const char* description, bool defaultValue) noexcept;

        bool Value() const noexcept { return value_; }

    private:
        bool value_;
    };

    /**
     * @brief A registered float setting, same file and same create-or-read behaviour as
     *        BoolSetting. A present but unparsable value reads as the default (and is left as is).
     */
    class FloatSetting
    {
    public:
        FloatSetting(const char* ns, const char* key, const char* description, float defaultValue) noexcept;

        float Value() const noexcept { return value_; }

    private:
        float value_;
    };
}
