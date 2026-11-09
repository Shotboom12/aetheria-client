#include "Extensions/WxlHost/src/WxlHost.hpp"

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/ItemRec.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Extension/ScriptDefines/LuaScript.h"
#include "WoW/Source/UIUtil/ActionCameraSettings.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/Ui/WorldFrame.h"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ActionCamera {
    void RegisterWaistLantern();
    void NotifyActionCameraProfileApplied();
    void UpdateWaistLantern();
    void DrawWaistLantern(const WXL_Api* api);
    void UpdateHunterQuiver();
    void DrawHunterQuiver(const WXL_Api* api);
    const char* LocalFootIKRig();
    const char* ProbeLocalFootGround();
    void RegisterFootPlacement();
    void EnableFootPlacement(bool enabled);
    void EnableMovingFootPlacement(bool enabled);
    void EnableFootSlopeAlignment(bool enabled);
    void EnableFootPelvisCompensation(bool enabled);
    void EnableFootprintProbes(bool enabled);
}

namespace
{
    int ReadRangedFeedback(lua_State* state)
    {
        lua_pushnumber(state, ActionCamera::rangedShotSerial);
        const auto* item = lua_isnumber(state, 1) ? g_itemDB.GetRecord(lua_tointeger(state, 1)) : nullptr;
        if (item && item->m_classID == 2)
            lua_pushinteger(state, item->m_subclassID);
        else
            lua_pushnil(state);
        return 2;
    }
    constexpr int kFirstEditableView = 1; // Native camera view A.
    constexpr int kLastEditableView = 5;  // Native camera view E.
    constexpr float kRadiansToDegrees = 57.29577951308232f;
    constexpr const char* kViewNames[] = { "", "A", "B", "C", "D", "E" };

    const WXL_Api* s_api = nullptr;
    int s_selectedView = 2;
    bool s_overlayWasOpen = false;
    void EnsureProfileLoaded(lua_State* state);
    void CaptureProfileValues(float* values);
    bool ProfileValuesChanged(const float* values);
    void SaveCurrentProfile(lua_State* state, const float* values);
    constexpr const char* kReticleShapes[] = {
        "simple", "arc", "brackets", "compass", "diamond", "dot", "leaf",
        "nexus", "ring", "runic", "spire", "split", "star"
    };
    constexpr const char* kReticleThemes[] = { "neutral", "bronze" };
    constexpr int kShapeCount = sizeof(kReticleShapes) / sizeof(kReticleShapes[0]);
    constexpr int kThemeCount = sizeof(kReticleThemes) / sizeof(kReticleThemes[0]);
    int& s_reticleShape = ActionCamera::userSettings.reticleShape;
    int& s_reticleTheme = ActionCamera::userSettings.reticleTheme;
    bool s_reticleAvailable = false;
    bool s_reticlePending = false;
    bool s_profileLoaded = false;
    uint32_t s_lastProfileSave = 0;
    bool s_haveLastProfileSnapshot = false;
    constexpr int kProfileValueCount = 61;
    constexpr int kPresetCount = 5;
    float s_lastProfileSnapshot[kProfileValueCount]{};
    int s_selectedPreset = 0;
    char s_presetNames[kPresetCount][48] = { "Preset 1", "Preset 2", "Preset 3", "Preset 4", "Preset 5" };
    const char* s_presetOptions[kPresetCount] = {
        s_presetNames[0], s_presetNames[1], s_presetNames[2], s_presetNames[3], s_presetNames[4]
    };
    char s_presetName[48] = "Preset 1";

    bool ReadReticleChoice(lua_State* state, const char* name, const char* const* choices, int count, int& index)
    {
        lua_getglobal(state, name);
        const char* value = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : nullptr;
        bool found = false;
        for (int i = 0; value && i < count; ++i) {
            if (std::strcmp(value, choices[i]) == 0) {
                index = i;
                found = true;
                break;
            }
        }
        lua_pop(state, 1);
        return found;
    }

    void __cdecl OnUpdate(void*, const void*)
    {
        const bool open = wxlhost::OverlayOpen();
        auto* profileState = FrameScript_GetContext();
        if (profileState) {
            lua_pushboolean(profileState, open ? 1 : 0);
            lua_setfield(profileState, LUA_GLOBALSINDEX, "AetheriaActionCameraNativeMenuOpen");
            EnsureProfileLoaded(profileState);
            if (s_profileLoaded) {
                ReadReticleChoice(profileState, "AetheriaActionCameraReticleShape", kReticleShapes, kShapeCount, s_reticleShape);
                ReadReticleChoice(profileState, "AetheriaActionCameraReticleTheme", kReticleThemes, kThemeCount, s_reticleTheme);
            }
            const uint32_t now = OsGetAsyncTimeMs();
            if (s_profileLoaded && now - s_lastProfileSave >= 500) {
                float values[kProfileValueCount]{};
                CaptureProfileValues(values);
                if (ProfileValuesChanged(values)) {
                    SaveCurrentProfile(profileState, values);
                    std::memcpy(s_lastProfileSnapshot, values, sizeof(values));
                    s_haveLastProfileSnapshot = true;
                }
                s_lastProfileSave = now;
            }
        }
        ActionCamera::UpdateWaistLantern();
        ActionCamera::UpdateHunterQuiver();
        ActionCamera::suppressDialogueVisuals = false;
        if (ActionCamera::cleanDialogueVisuals && CGWorldFrame::GetActiveCamera()) {
            if (auto* state = FrameScript_GetContext()) {
                lua_pushliteral(state, "AetheriaActionCameraDialogueOpen");
                lua_rawget(state, LUA_GLOBALSINDEX);
                ActionCamera::suppressDialogueVisuals = lua_isboolean(state, -1) && lua_toboolean(state, -1);
                lua_pop(state, 1);
            }
        }
        if (open && !s_overlayWasOpen && CGWorldFrame::GetActiveCamera() && FrameScript_GetContext())
            FrameScript_Execute(
                "if AetheriaActionCameraReleaseMouseLook then AetheriaActionCameraReleaseMouseLook() end",
                "AetheriaActionCamera", nullptr);
        s_overlayWasOpen = open;
        auto* state = open && CGWorldFrame::GetActiveCamera() ? FrameScript_GetContext() : nullptr;
        if (state && s_reticlePending) {
            char script[220];
            std::snprintf(script, sizeof(script),
                "if SlashCmdList and SlashCmdList.AETHERIARETICLE then SlashCmdList.AETHERIARETICLE('%s %s') end",
                kReticleShapes[s_reticleShape], kReticleThemes[s_reticleTheme]);
            FrameScript_Execute(script, "AetheriaActionCamera", nullptr);
        }
        s_reticlePending = false;
        s_reticleAvailable = state &&
            ReadReticleChoice(state, "AetheriaActionCameraReticleShape", kReticleShapes, kShapeCount, s_reticleShape) &&
            ReadReticleChoice(state, "AetheriaActionCameraReticleTheme", kReticleThemes, kThemeCount, s_reticleTheme);
    }

    const WXL_PluginInfo s_pluginInfo = {
        sizeof(WXL_PluginInfo),
        WXL_API_VERSION,
        "Aetheria Action Camera Profiles",
        0x00010000,
        WXL_CLIENT_BUILD,
    };

    CVar* ViewSetting(int view, int component)
    {
        if (view < kFirstEditableView || view > kLastEditableView || component < 0 || component >= 3)
            return nullptr;

        return s_cvCameraViewData[view][component];
    }

    bool StoreSetting(CVar* setting, float value)
    {
        if (!setting)
            return false;

        char text[32];
        std::snprintf(text, sizeof(text), "%.3f", static_cast<double>(value));
        return setting->Set(text, true, false, false, true);
    }

    void CaptureProfileValues(float* v)
    {
        const auto& s = ActionCamera::userSettings;
        v[0] = s.shoulderEnabled; v[1] = s.shoulderOffset;
        v[2] = s.combatEnabled; v[3] = s.combatOffset;
        v[4] = s.dialogueEnabled; v[5] = s.dialogueOffset;
        v[6] = s.groundMountEnabled; v[7] = s.groundMountOffset;
        v[8] = s.flyingMountEnabled; v[9] = s.flyingMountOffset;
        v[10] = s.vehicleEnabled; v[11] = s.vehicleOffset;
        v[12] = s.cleanDialogueVisuals; v[13] = s.preciseUnitHover; v[14] = static_cast<float>(s.unitHoverWidth);
        v[15] = s.preciseObjectHover; v[16] = static_cast<float>(s.objectHoverWidth);
        v[17] = s.footPlacementEnabled; v[18] = s.movingFootPlacementEnabled;
        v[19] = s.slopeAlignmentEnabled; v[20] = s.pelvisCompensationEnabled; v[21] = s.footprintProbesEnabled;
        v[22] = s.lanternEnabled; v[23] = s.lanternNightOnly;
        v[24] = static_cast<float>(s.lanternStartHour); v[25] = static_cast<float>(s.lanternEndHour);
        v[26] = static_cast<float>(s.lanternSlot);
        v[27] = s.lanternOffset.x; v[28] = s.lanternOffset.y; v[29] = s.lanternOffset.z;
        v[30] = s.lanternRotation.x; v[31] = s.lanternRotation.y; v[32] = s.lanternRotation.z;
        v[33] = s.lanternScale; v[34] = static_cast<float>(s.reticleShape); v[35] = static_cast<float>(s.reticleTheme);
        v[36] = s.quiverEnabled; v[37] = static_cast<float>(s.quiverChoice); v[38] = static_cast<float>(s.quiverVisibility);
        v[39] = s.quiverOffset.x; v[40] = s.quiverOffset.y; v[41] = s.quiverOffset.z;
        v[42] = s.quiverRotation.x; v[43] = s.quiverRotation.y; v[44] = s.quiverRotation.z; v[45] = s.quiverScale;
        for (int view = kFirstEditableView; view <= kLastEditableView; ++view)
            for (int component = 0; component < 3; ++component) {
                const auto* setting = ViewSetting(view, component);
                if (!setting) return;
                v[46 + (view - 1) * 3 + component] = setting->m_floatValue;
            }
    }

    bool ProfileCVarsReady()
    {
        for (int view = kFirstEditableView; view <= kLastEditableView; ++view)
            for (int component = 0; component < 3; ++component)
                if (!ViewSetting(view, component)) return false;
        return true;
    }

    float ClampProfileValue(float value, float low, float high, float fallback)
    {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }

    bool PushUserProfile(lua_State* state, bool create)
    {
        lua_getfield(state, LUA_GLOBALSINDEX, "AetheriaActionCameraProfile");
        if (lua_istable(state, -1)) return true;
        lua_pop(state, 1);
        if (!create) return false;
        lua_newtable(state);
        lua_pushvalue(state, -1);
        lua_setfield(state, LUA_GLOBALSINDEX, "AetheriaActionCameraProfile");
        return true;
    }

    bool ReadValuesTable(lua_State* state, int table, float* values)
    {
        if (!lua_istable(state, table)) return false;
        for (int i = 0; i < kProfileValueCount; ++i) {
            lua_rawgeti(state, table, i + 1);
            const bool valid = lua_isnumber(state, -1) != 0;
            values[i] = valid ? static_cast<float>(lua_tonumber(state, -1)) : 0.0f;
            lua_pop(state, 1);
            if (!valid) return false;
        }
        return true;
    }

    void PushValuesTable(lua_State* state, const float* values)
    {
        lua_newtable(state);
        const int table = lua_gettop(state);
        for (int i = 0; i < kProfileValueCount; ++i) {
            lua_pushnumber(state, values[i]);
            lua_rawseti(state, table, i + 1);
        }
    }

    void WriteProfileField(lua_State* state, int profile, const char* field, const float* values)
    {
        PushValuesTable(state, values);
        lua_setfield(state, profile, field);
    }

    void SaveCurrentProfile(lua_State* state, const float* values)
    {
        if (!PushUserProfile(state, true)) return;
        const int profile = lua_gettop(state);
        WriteProfileField(state, profile, "current", values);
        lua_pop(state, 1);
    }

    void RefreshPresetNames(lua_State* state)
    {
        if (!PushUserProfile(state, false)) return;
        const int profile = lua_gettop(state);
        lua_getfield(state, profile, "presets");
        if (lua_istable(state, -1)) {
            const int presets = lua_gettop(state);
            for (int i = 0; i < kPresetCount; ++i) {
                lua_rawgeti(state, presets, i + 1);
                if (lua_istable(state, -1)) {
                    lua_getfield(state, -1, "name");
                    const char* name = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : nullptr;
                    if (name && *name)
                        std::snprintf(s_presetNames[i], sizeof(s_presetNames[i]), "%s", name);
                    lua_pop(state, 1);
                }
                lua_pop(state, 1);
            }
        }
        lua_pop(state, 2);
    }

    void CopySelectedPresetName()
    {
        std::snprintf(s_presetName, sizeof(s_presetName), "%s", s_presetNames[s_selectedPreset]);
    }

    void ApplyProfileValues(const float* v)
    {
        auto& s = ActionCamera::userSettings;
        const auto b = [v](int i) { return v[i] >= 0.5f; };
        const auto integer = [v](int i, int lo, int hi) {
            return static_cast<int>(ClampProfileValue(v[i], static_cast<float>(lo), static_cast<float>(hi), static_cast<float>(lo)));
        };
        s.shoulderEnabled = b(0); s.shoulderOffset = ClampProfileValue(v[1], -1.5f, 1.5f, 0.75f);
        s.combatEnabled = b(2); s.combatOffset = ClampProfileValue(v[3], -1.5f, 1.5f, 1.0f);
        s.dialogueEnabled = b(4); s.dialogueOffset = ClampProfileValue(v[5], -1.5f, 1.5f, 0.35f);
        s.groundMountEnabled = b(6); s.groundMountOffset = ClampProfileValue(v[7], -1.5f, 1.5f, 0.75f);
        s.flyingMountEnabled = b(8); s.flyingMountOffset = ClampProfileValue(v[9], -1.5f, 1.5f, 0.75f);
        s.vehicleEnabled = b(10); s.vehicleOffset = ClampProfileValue(v[11], -1.5f, 1.5f, 0.5f);
        s.cleanDialogueVisuals = b(12); s.preciseUnitHover = b(13); s.unitHoverWidth = integer(14, 40, 140);
        s.preciseObjectHover = b(15); s.objectHoverWidth = integer(16, 30, 140);
        s.footPlacementEnabled = b(17); s.movingFootPlacementEnabled = b(18); s.slopeAlignmentEnabled = b(19);
        s.pelvisCompensationEnabled = b(20); s.footprintProbesEnabled = b(21);
        s.lanternEnabled = b(22); s.lanternNightOnly = b(23); s.lanternStartHour = integer(24, 0, 23);
        s.lanternEndHour = integer(25, 0, 23); s.lanternSlot = integer(26, 0, 2);
        s.lanternOffset = C3Vector(ClampProfileValue(v[27], -1, 1, 0), ClampProfileValue(v[28], -1, 1, 0), ClampProfileValue(v[29], -1, 1, 0));
        s.lanternRotation = C3Vector(ClampProfileValue(v[30], -180, 180, 0), ClampProfileValue(v[31], -180, 180, 0), ClampProfileValue(v[32], -180, 180, 0));
        s.lanternScale = ClampProfileValue(v[33], 0.25f, 2, 1);
        s.reticleShape = integer(34, 0, kShapeCount - 1); s.reticleTheme = integer(35, 0, kThemeCount - 1);
        s.quiverEnabled = b(36); s.quiverChoice = integer(37, 0, 15); s.quiverVisibility = integer(38, 0, 2);
        s.quiverOffset = C3Vector(ClampProfileValue(v[39], -1, 1, 0), ClampProfileValue(v[40], -1, 1, 0), ClampProfileValue(v[41], -1, 1, 0));
        s.quiverRotation = C3Vector(ClampProfileValue(v[42], -180, 180, 0), ClampProfileValue(v[43], -180, 180, 0), ClampProfileValue(v[44], -180, 180, 0));
        s.quiverScale = ClampProfileValue(v[45], 0.25f, 2, 1);
        for (int view = kFirstEditableView; view <= kLastEditableView; ++view) {
            StoreSetting(ViewSetting(view, 0), ClampProfileValue(v[46 + (view - 1) * 3], 0, 50, 15));
            StoreSetting(ViewSetting(view, 1), ClampProfileValue(v[47 + (view - 1) * 3], MIN_PITCH_ANGLE * kRadiansToDegrees, MAX_PITCH_ANGLE * kRadiansToDegrees, 0));
            StoreSetting(ViewSetting(view, 2), ClampProfileValue(v[48 + (view - 1) * 3], 0, 360, 0));
        }
        ActionCamera::NotifyActionCameraProfileApplied();
    }

    bool ReadCurrentProfile(lua_State* state, float* values)
    {
        if (!PushUserProfile(state, false)) return false;
        const int profile = lua_gettop(state);
        lua_getfield(state, profile, "current");
        const bool found = ReadValuesTable(state, lua_gettop(state), values);
        lua_pop(state, 2);
        return found;
    }

    bool LoadPresetValues(lua_State* state, int slot, float* values)
    {
        if (!PushUserProfile(state, false)) return false;
        const int profile = lua_gettop(state);
        lua_getfield(state, profile, "presets");
        bool found = false;
        if (lua_istable(state, -1)) {
            lua_rawgeti(state, -1, slot + 1);
            if (lua_istable(state, -1)) {
                lua_getfield(state, -1, "values");
                found = ReadValuesTable(state, lua_gettop(state), values);
                lua_pop(state, 1);
            }
            lua_pop(state, 1);
        }
        lua_pop(state, 2);
        return found;
    }

    void SavePreset(lua_State* state, int slot, const char* name, const float* values)
    {
        if (!PushUserProfile(state, true)) return;
        const int profile = lua_gettop(state);
        lua_getfield(state, profile, "presets");
        if (!lua_istable(state, -1)) {
            lua_pop(state, 1);
            lua_newtable(state);
            lua_pushvalue(state, -1);
            lua_setfield(state, profile, "presets");
        }
        const int presets = lua_gettop(state);
        lua_pushinteger(state, slot + 1);
        lua_newtable(state);
        const int preset = lua_gettop(state);
        lua_pushstring(state, name && *name ? name : s_presetNames[slot]);
        lua_setfield(state, preset, "name");
        PushValuesTable(state, values);
        lua_setfield(state, preset, "values");
        lua_rawset(state, presets);
        lua_pop(state, 2);
    }

    void EnsureProfileLoaded(lua_State* state)
    {
        if (s_profileLoaded || !state || !ProfileCVarsReady() || !PushUserProfile(state, false)) return;
        lua_pop(state, 1);
        float values[kProfileValueCount]{};
        if (ReadCurrentProfile(state, values)) {
            ApplyProfileValues(values);
            char script[160];
            std::snprintf(script, sizeof(script),
                "if SlashCmdList and SlashCmdList.AETHERIARETICLE then SlashCmdList.AETHERIARETICLE('%s %s') end",
                kReticleShapes[ActionCamera::userSettings.reticleShape], kReticleThemes[ActionCamera::userSettings.reticleTheme]);
            FrameScript_Execute(script, "AetheriaActionCamera", nullptr);
        } else {
            CaptureProfileValues(values);
            SaveCurrentProfile(state, values);
        }
        RefreshPresetNames(state);
        CopySelectedPresetName();
        std::memcpy(s_lastProfileSnapshot, values, sizeof(values));
        s_haveLastProfileSnapshot = true;
        s_profileLoaded = true;
    }

    bool ProfileValuesChanged(const float* values)
    {
        if (!s_haveLastProfileSnapshot) return true;
        for (int i = 0; i < kProfileValueCount; ++i)
            if (std::fabs(values[i] - s_lastProfileSnapshot[i]) > 0.0001f) return true;
        return false;
    }

    void SavePresetFromPanel()
    {
        auto* state = FrameScript_GetContext();
        if (!state || !ProfileCVarsReady()) return;
        float values[kProfileValueCount]{};
        CaptureProfileValues(values);
        SavePreset(state, s_selectedPreset, s_presetName, values);
        std::snprintf(s_presetNames[s_selectedPreset], sizeof(s_presetNames[s_selectedPreset]), "%s", s_presetName);
    }

    int SaveCurrentProfileLua(lua_State* state)
    {
        if (state && ProfileCVarsReady()) {
            float values[kProfileValueCount]{};
            CaptureProfileValues(values);
            SaveCurrentProfile(state, values);
            std::memcpy(s_lastProfileSnapshot, values, sizeof(values));
            s_haveLastProfileSnapshot = true;
        }
        return 0;
    }

    class ActionCameraProfileLifecycle final : public LuaScript {
    public:
        ActionCameraProfileLifecycle() : LuaScript("AetheriaActionCameraProfile") {}
        void OnFrameScriptInitialized(lua_State*, FrameScriptContext) override {
            s_profileLoaded = false;
            s_haveLastProfileSnapshot = false;
            s_lastProfileSave = 0;
        }
        void OnFrameScriptDestroy(lua_State* state) override {
            SaveCurrentProfileLua(state);
            s_profileLoaded = false;
            s_haveLastProfileSnapshot = false;
            s_lastProfileSave = 0;
        }
    };

    void LoadPresetFromPanel()
    {
        auto* state = FrameScript_GetContext();
        if (!state) return;
        float values[kProfileValueCount]{};
        if (!LoadPresetValues(state, s_selectedPreset, values)) return;
        ApplyProfileValues(values);
        SaveCurrentProfile(state, values);
        std::memcpy(s_lastProfileSnapshot, values, sizeof(values));
        s_haveLastProfileSnapshot = true;
        std::snprintf(s_presetName, sizeof(s_presetName), "%s", s_presetNames[s_selectedPreset]);
        char script[160];
        std::snprintf(script, sizeof(script),
            "if SlashCmdList and SlashCmdList.AETHERIARETICLE then SlashCmdList.AETHERIARETICLE('%s %s') end",
            kReticleShapes[ActionCamera::userSettings.reticleShape], kReticleThemes[ActionCamera::userSettings.reticleTheme]);
        FrameScript_Execute(script, "AetheriaActionCamera", nullptr);
    }

    void DrawPanel(void*)
    {
        if (!s_api)
            return;

        ActionCamera::DrawWaistLantern(s_api);
        ActionCamera::DrawHunterQuiver(s_api);
        if (s_api->UiCollapsingHeader("Foot IK rig diagnostics")) {
            int footPlacement = ActionCamera::userSettings.footPlacementEnabled ? 1 : 0;
            if (s_api->UiCheckbox("Experimental standing foot placement", &footPlacement))
                ActionCamera::EnableFootPlacement(footPlacement != 0);
            int movingFootPlacement = ActionCamera::userSettings.movingFootPlacementEnabled ? 1 : 0;
            if (footPlacement && s_api->UiCheckbox("Experimental walking and running foot placement", &movingFootPlacement))
                ActionCamera::EnableMovingFootPlacement(movingFootPlacement != 0);
            int slopeAlignment = ActionCamera::userSettings.slopeAlignmentEnabled ? 1 : 0;
            if (footPlacement && s_api->UiCheckbox("Experimental standing slope alignment", &slopeAlignment))
                ActionCamera::EnableFootSlopeAlignment(slopeAlignment != 0);
            int pelvisCompensation = ActionCamera::userSettings.pelvisCompensationEnabled ? 1 : 0;
            if (footPlacement && s_api->UiCheckbox("Experimental standing pelvis compensation", &pelvisCompensation))
                ActionCamera::EnableFootPelvisCompensation(pelvisCompensation != 0);
            int footprintProbes = ActionCamera::userSettings.footprintProbesEnabled ? 1 : 0;
            if (footPlacement && s_api->UiCheckbox("Experimental standing sole edge probes", &footprintProbes))
                ActionCamera::EnableFootprintProbes(footprintProbes != 0);
            static const char* probe = "No ground sample yet.";
            static int liveProbe = 0;
            static uint32_t lastProbe = 0;
            s_api->UiText(ActionCamera::LocalFootIKRig());
            s_api->UiCheckbox("Live ground samples", &liveProbe);
            const uint32_t now = OsGetAsyncTimeMs();
            if (s_api->UiButton("Sample ground under feet") || (liveProbe && now - lastProbe >= 100)) {
                probe = ActionCamera::ProbeLocalFootGround();
                lastProbe = now;
            }
            s_api->UiText(probe);
        }

        if (s_reticleAvailable) {
            if (s_api->UiCombo("Reticle shape", &s_reticleShape, kReticleShapes, kShapeCount))
                s_reticlePending = true;
            if (s_api->UiCombo("Reticle color", &s_reticleTheme, kReticleThemes, kThemeCount))
                s_reticlePending = true;
        } else {
            s_api->UiText("Reticle controls require the Aetheria Action Camera addon in game.");
        }
        if (s_api->UiButton("Start Action Camera (mouse look + reticle)"))
            FrameScript_Execute("if SlashCmdList and SlashCmdList.AETHERIAACTIONCAM then SlashCmdList.AETHERIAACTIONCAM('') end",
                "AetheriaActionCamera", nullptr);
        s_api->UiText("Starting waits until this panel closes. Open the panel again to stop mouse look.");
        if (s_api->UiCollapsingHeader("Saved profiles")) {
            if (s_api->UiCombo("Preset slot", &s_selectedPreset, s_presetOptions, kPresetCount))
                CopySelectedPresetName();
            if (s_api->UiInputText("Preset name", s_presetName, sizeof(s_presetName))) {
                if (!s_presetName[0]) CopySelectedPresetName();
            }
            if (s_api->UiButton("Save current settings to preset"))
                SavePresetFromPanel();
            if (s_api->UiButton("Load selected preset"))
                LoadPresetFromPanel();
            s_api->UiText("Current settings save automatically per character when changed.");
        }
        s_api->UiSeparator();
        int cleanDialogue = ActionCamera::cleanDialogueVisuals ? 1 : 0;
        if (s_api->UiCheckbox("Hide NPC overhead names and selection rings in dialogue", &cleanDialogue))
            ActionCamera::cleanDialogueVisuals = cleanDialogue != 0;
        int preciseHover = ActionCamera::preciseUnitHover ? 1 : 0;
        if (s_api->UiCheckbox("Tighter character hover", &preciseHover))
            ActionCamera::preciseUnitHover = preciseHover != 0;
        if (ActionCamera::preciseUnitHover)
            s_api->UiSliderInt("Character hover width (%)", &ActionCamera::unitHoverWidth, 40, 140);
        int preciseObjectHover = ActionCamera::preciseObjectHover ? 1 : 0;
        if (s_api->UiCheckbox("Tighter world-object hover", &preciseObjectHover))
            ActionCamera::preciseObjectHover = preciseObjectHover != 0;
        if (ActionCamera::preciseObjectHover)
            s_api->UiSliderInt("World-object hover width (%)", &ActionCamera::objectHoverWidth, 30, 140);
        s_api->UiText("Edit built-in camera views A-E. Values save to this client and are included in presets.");
        int shoulderEnabled = ActionCamera::shoulderEnabled ? 1 : 0;
        if (s_api->UiCheckbox("Shoulder framing", &shoulderEnabled))
            ActionCamera::shoulderEnabled = shoulderEnabled != 0;
        if (ActionCamera::shoulderEnabled) {
            s_api->UiSliderFloat("Shoulder offset", &ActionCamera::shoulderOffset, -1.5f, 1.5f);
            const auto drawProfile = [](const char* toggle, const char* slider, bool& enabled, float& offset) {
                int value = enabled ? 1 : 0;
                if (s_api->UiCheckbox(toggle, &value))
                    enabled = value != 0;
                if (enabled)
                    s_api->UiSliderFloat(slider, &offset, -1.5f, 1.5f);
            };
            drawProfile("Combat framing", "Combat offset", ActionCamera::combatEnabled, ActionCamera::combatOffset);
            drawProfile("Dialogue framing", "Dialogue offset", ActionCamera::dialogueEnabled, ActionCamera::dialogueOffset);
            drawProfile("Ground mount framing", "Ground mount offset",
                ActionCamera::groundMountEnabled, ActionCamera::groundMountOffset);
            drawProfile("Flying mount framing", "Flying mount offset",
                ActionCamera::flyingMountEnabled, ActionCamera::flyingMountOffset);
            drawProfile("Vehicle framing", "Vehicle offset", ActionCamera::vehicleEnabled, ActionCamera::vehicleOffset);
        }
        s_api->UiSeparator();

        if (s_api->UiSliderInt("Camera view (A-E)", &s_selectedView, kFirstEditableView, kLastEditableView))
            s_selectedView = s_selectedView < kFirstEditableView ? kFirstEditableView : s_selectedView;
        s_api->UiText(kViewNames[s_selectedView]);

        CVar* distance = ViewSetting(s_selectedView, 0);
        CVar* pitch = ViewSetting(s_selectedView, 1);
        CVar* yaw = ViewSetting(s_selectedView, 2);
        if (!distance || !pitch || !yaw)
        {
            s_api->UiText("Camera settings are not initialized yet.");
            return;
        }

        float distanceValue = distance->m_floatValue;
        if (s_api->UiSliderFloat("Distance##ActionCamera", &distanceValue, 0.0f, 50.0f))
            StoreSetting(distance, distanceValue);

        const float pitchMin = MIN_PITCH_ANGLE * kRadiansToDegrees;
        const float pitchMax = MAX_PITCH_ANGLE * kRadiansToDegrees;
        float pitchValue = pitch->m_floatValue;
        if (s_api->UiSliderFloat("Pitch##ActionCamera", &pitchValue, pitchMin, pitchMax))
            StoreSetting(pitch, pitchValue);

        float yawValue = yaw->m_floatValue;
        if (s_api->UiSliderFloat("Yaw##ActionCamera", &yawValue, 0.0f, 360.0f))
            StoreSetting(yaw, yawValue);

        s_api->UiSeparator();
        if (s_api->UiButton("Apply selected view"))
        {
            CGCamera* camera = CGWorldFrame::GetActiveCamera();
            if (camera)
            {
                const int blendStyle = s_cvCameraViewBlendStyle ? s_cvCameraViewBlendStyle->m_intValue : 1;
                camera->InitializeView();
                camera->SetView(s_selectedView, blendStyle, 0);
            }
            else
            {
                s_api->UiText("Enter the world before applying a camera view.");
            }
        }
    }

    const WXL_PluginInfo* __cdecl QueryPlugin()
    {
        return &s_pluginInfo;
    }

    int __cdecl LoadPlugin(const WXL_Api* api)
    {
        if (!api || api->apiVersion != WXL_API_VERSION || api->structSize < sizeof(WXL_Api) || !api->Log ||
            !api->UiAddPanel || !api->UiText || !api->UiSliderFloat || !api->UiSliderInt ||
            !api->UiButton || !api->UiSeparator || !api->UiCheckbox || !api->Subscribe || !api->UiCombo ||
            !api->UiCollapsingHeader || !api->UiInputText)
            return 0;

        s_api = api;
        s_api->Subscribe(static_cast<uint32_t>(wxl::events::Event::OnUpdate), OnUpdate, nullptr);
        s_api->UiAddPanel("Action Camera Profiles", DrawPanel, nullptr);
        s_api->Log(WXL_LOG_INFO, "ActionCameraPort", "Native camera profile controls registered");
        return 1;
    }
}

void AddActionCameraNamePlateCompatibility();

void AddActionCameraPortExtension()
{
    new ActionCameraProfileLifecycle();
    sExtensionMgr->RegisterLuaFunction("AetheriaSaveActionCameraProfile", SaveCurrentProfileLua, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("AetheriaReadRangedFeedback", ReadRangedFeedback, FRAMESCRIPT_CONTEXTS_GAME);
    ActionCamera::RegisterWaistLantern();
    ActionCamera::RegisterFootPlacement();
    AddActionCameraNamePlateCompatibility();
    wxlhost::RegisterPlugin("aetheria-action-camera-profiles", &QueryPlugin, &LoadPlugin);
}
