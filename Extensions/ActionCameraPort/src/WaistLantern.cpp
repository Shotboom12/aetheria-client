#include "Extensions/WxlHost/src/WxlHost.hpp"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/ItemDisplayInfoRec.h"
#include "WoW/Source/Game/GameTime.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Extension/ScriptDefines/LuaScript.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/UIUtil/ActionCameraSettings.h"
#include "WoW/Common/ComponentCore/Types.h"

#include <string>
#include <cmath>
#include <algorithm>

namespace ActionCamera {
namespace {
    constexpr uint32_t kDisplay = 7557;
    constexpr uint32_t kAnchors[] = { 53, 10, 9 };
    constexpr const char* kSlots[] = { "Belt", "Right hip", "Left hip" };
    constexpr const char* kFolder = "Item\\ObjectComponents\\Weapon\\";
    CM2Model* lantern = nullptr; // Retains the creation reference independently of its parent.
    CM2Model* quiver = nullptr;
    bool& quiverEnabled = userSettings.quiverEnabled;
    // Cosmetic display IDs from the suite's audited Wrath quiver catalog.
    constexpr uint32_t kQuiverDisplays[] = {
        3337, 1244, 21328, 21712, 21321, 21332, 21318, 21330,
        21322, 21331, 21329, 31162, 40337, 40338, 47447, 56653
    };
    constexpr const char* kQuiverNames[] = {
        "Item 1281 / 2002", "Item 2003", "Item 2101", "Item 2662 / 19319",
        "Item 3573", "Item 3605", "Item 5439", "Item 7278",
        "Item 7371", "Item 8217", "Item 11362", "Item 18714",
        "Item 29143", "Item 29144", "Item 34105", "Item 44448"
    };
    constexpr int kQuiverCount = sizeof(kQuiverDisplays) / sizeof(kQuiverDisplays[0]);
    static_assert(kQuiverCount == sizeof(kQuiverNames) / sizeof(kQuiverNames[0]));
    int& quiverChoice = userSettings.quiverChoice;
    int attachedQuiverChoice = -1;
    C3Vector& quiverOffset = userSettings.quiverOffset;
    C3Vector& quiverRotation = userSettings.quiverRotation;
    float& quiverScale = userSettings.quiverScale;
    bool quiverPlacementDirty = true;
    bool quiverProfileLoaded = false;
    int& quiverVisibility = userSettings.quiverVisibility;
    constexpr const char* kQuiverVisibility[] = { "Always", "Weapons sheathed", "Weapons drawn" };
    const char* quiverStatus = "Off";
    bool& enabled = userSettings.lanternEnabled;
    bool& nightOnly = userSettings.lanternNightOnly;
    int& slot = userSettings.lanternSlot;
    int& startHour = userSettings.lanternStartHour;
    int& endHour = userSettings.lanternEndHour;
    int manualOverride = -1;
    C3Vector& offset = userSettings.lanternOffset;
    C3Vector& rotation = userSettings.lanternRotation;
    float& scale = userSettings.lanternScale;
    bool transformDirty = true;
    const char* status = "Off";

    bool WantsLantern() {
        if (manualOverride >= 0)
            return manualOverride != 0;
        const int hour = g_clientGameTime.m_hour;
        const bool inWindow = startHour < endHour ? hour >= startHour && hour < endHour
            : hour >= startHour || hour < endHour;
        return enabled && (!nightOnly || inWindow);
    }

    int ToggleLantern(lua_State*) {
        manualOverride = WantsLantern() ? 0 : 1;
        return 0;
    }

    void ApplyCosmeticTransform(CM2Model* model, const C3Vector& offset, const C3Vector& rotation, float scale) {
        constexpr float radians = 0.01745329252f;
        C44Matrix transform;
        transform.Identity();
        transform.RotateAroundX(rotation.x * radians);
        transform.RotateAroundY(rotation.y * radians);
        transform.RotateAroundZ(rotation.z * radians);
        transform.Scale(scale);
        transform.Translate(offset);
        model->SetWorldTransform(transform);
    }

    void ApplyTransform() {
        auto* parent = lantern ? lantern->m_attachParent : nullptr;
        if (!parent || !(parent->m_fFlags & 1) || !parent->m_shared || !parent->m_shared->m_data)
            return;
        const auto anchor = lantern->m_attachmentId;
        const auto& data = *parent->m_shared->m_data;
        const uint16_t attachmentIndex = anchor < data.m_attachmentIndicesById.m_count
            ? data.m_attachmentIndicesById[anchor] : 0xFFFF;
        if (attachmentIndex >= data.m_attachments.m_count)
            return;
        const auto& attachment = data.m_attachments[attachmentIndex];
        lantern->SetAttachmentFrame(attachment.m_bone, attachment.m_position + offset, scale, rotation);
        transformDirty = false;
    }

    void ClearCosmetic(CM2Model*& model) {
        if (!model)
            return;
        if (model->m_attachParent)
            model->DetachFromParent();
        model->Release();
        model = nullptr;
    }

    void ClearLantern() { ClearCosmetic(lantern); }

    lua_State* PushQuiverProfile() {
        auto* state = FrameScript_GetContext();
        if (!state || !CGWorldFrame::GetActiveCamera())
            return nullptr;
        lua_pushliteral(state, "AetheriaActionCameraHunterProfile");
        lua_rawget(state, LUA_GLOBALSINDEX);
        if (lua_istable(state, -1))
            return state;
        lua_pop(state, 1);
        return nullptr;
    }

    void TransferQuiverProfile(bool save) {
        auto* state = PushQuiverProfile();
        if (!state)
            return;
        int index = 0;
        const auto field = [&](float value, float low, float high) {
            ++index;
            if (save) {
                lua_pushnumber(state, value);
                lua_rawseti(state, -2, index);
                return value;
            }
            lua_rawgeti(state, -1, index);
            const float stored = lua_type(state, -1) == LUA_TNUMBER
                ? static_cast<float>(lua_tonumber(state, -1)) : value;
            lua_pop(state, 1);
            return std::isfinite(stored) ? std::clamp(stored, low, high) : value;
        };
        quiverEnabled = field(quiverEnabled ? 1.0f : 0.0f, 0, 1) >= 0.5f;
        quiverChoice = static_cast<int>(field(static_cast<float>(quiverChoice), 0, kQuiverCount - 1));
        quiverVisibility = static_cast<int>(field(static_cast<float>(quiverVisibility), 0, 2));
        quiverOffset.x = field(quiverOffset.x, -1, 1);
        quiverOffset.y = field(quiverOffset.y, -1, 1);
        quiverOffset.z = field(quiverOffset.z, -1, 1);
        quiverRotation.x = field(quiverRotation.x, -180, 180);
        quiverRotation.y = field(quiverRotation.y, -180, 180);
        quiverRotation.z = field(quiverRotation.z, -180, 180);
        quiverScale = field(quiverScale, 0.25f, 2);
        lua_pop(state, 1);
        quiverProfileLoaded = true;
    }

    class LanternLifecycle : public LuaScript {
    public:
        LanternLifecycle() : LuaScript("ActionCameraWaistLantern") {}
        void OnFrameScriptDestroy(lua_State*) override {
            ClearLantern();
            ClearCosmetic(quiver);
            quiverProfileLoaded = false;
            quiverPlacementDirty = true;
        }
    };
}

void UpdateHunterQuiver() {
    if (!quiverProfileLoaded)
        TransferQuiverProfile(false);
    constexpr uint32_t anchor = ATTACH_SHEATH_MAINHAND;
    constexpr uint32_t hunterClass = 3;
    auto* player = quiverEnabled && CGWorldFrame::GetActiveCamera() ? ClntObjMgrGetActivePlayerObj() : nullptr;
    if (player && ((quiverVisibility == 1 && player->m_sheatheState != 0) ||
        (quiverVisibility == 2 && player->m_sheatheState == 0))) {
        ClearCosmetic(quiver);
        quiverStatus = "Hidden by sheath preference";
        return;
    }
    auto* parent = player && player->m_unit->m_unitFieldBytes0[1] == hunterClass ? player->GetObjectModel() : nullptr;
    if (quiver && (quiver->m_attachParent != parent || quiver->m_attachmentId != anchor ||
        attachedQuiverChoice != quiverChoice))
        ClearCosmetic(quiver);
    if (!parent || !parent->m_scene || !parent->IsLoaded(0, 0) || !parent->HasAttachment(anchor)) {
        quiverStatus = quiverEnabled ? "Waiting for a supported hunter model" : "Off";
        return;
    }
    for (auto* child = parent->m_attachList; child; child = child->m_attachNext) {
        if (child != quiver && child->m_attachmentId == anchor) {
            ClearCosmetic(quiver);
            quiverStatus = "Stock equipment owns the quiver slot";
            return;
        }
    }
    if (quiver) {
        if (quiverPlacementDirty) {
            ApplyCosmeticTransform(quiver, quiverOffset, quiverRotation, quiverScale);
            quiverPlacementDirty = false;
        }
        return;
    }

    constexpr const char* folder = "Item\\ObjectComponents\\Quiver\\";
    ItemDisplayInfoRec displayRecord{};
    const auto* display = g_itemDisplayInfoDB.GetRecord(kQuiverDisplays[quiverChoice], &displayRecord) ? &displayRecord : nullptr;
    if (display && display->m_modelName[0] && *display->m_modelName[0]) {
        const auto path = std::string(folder) + display->m_modelName[0];
        quiver = parent->m_scene->CreateModel(path.c_str(), 0);
        if (quiver) {
            if (display->m_modelTexture[0] && *display->m_modelTexture[0]) {
                CStatus textureStatus;
                const auto texturePath = std::string(folder) + display->m_modelTexture[0] + ".blp";
                if (auto* texture = TextureCreate(texturePath.c_str(), CGxTexFlags(), &textureStatus, 1)) {
                    quiver->ReplaceTexture(2, texture);
                    HandleClose(reinterpret_cast<HOBJECT>(texture));
                }
            }
            quiver->AttachToParent(parent, anchor, nullptr, 0);
            if (quiver->m_attachParent == parent) {
                attachedQuiverChoice = quiverChoice;
                ApplyCosmeticTransform(quiver, quiverOffset, quiverRotation, quiverScale);
                quiverPlacementDirty = false;
                quiverStatus = "Cosmetic quiver attached";
                return;
            }
            ClearCosmetic(quiver);
        }
    }
    quiverEnabled = false;
    quiverStatus = "Cosmetic quiver unavailable";
}

void RegisterWaistLantern() {
    new LanternLifecycle();
    sExtensionMgr->RegisterLuaFunction("AetheriaToggleWaistLantern", ToggleLantern, FRAMESCRIPT_CONTEXTS_GAME);
}

void NotifyActionCameraProfileApplied() {
    manualOverride = -1;
    transformDirty = true;
    quiverPlacementDirty = true;
    quiverProfileLoaded = true;
}

void UpdateWaistLantern() {
    auto* player = WantsLantern() && CGWorldFrame::GetActiveCamera()
        ? ClntObjMgrGetActivePlayerObj() : nullptr;
    auto* parent = player ? player->GetObjectModel() : nullptr;
    const uint32_t anchor = kAnchors[slot];
    if (lantern && (lantern->m_attachParent != parent || lantern->m_attachmentId != anchor))
        ClearLantern();
    if (!parent || !parent->m_scene || !parent->IsLoaded(0, 0)) {
        status = enabled ? "Waiting for player model or night schedule" : "Off";
        return;
    }
    for (auto* child = parent->m_attachList; child; child = child->m_attachNext) {
        if (child != lantern && child->m_attachmentId == anchor) {
            ClearLantern();
            status = "Attachment occupied";
            return;
        }
    }
    if (lantern) {
        if (transformDirty)
            ApplyTransform();
        status = "Attached";
        return;
    }
    if (!parent->HasAttachment(anchor)) {
        status = "Attachment unavailable on this model";
        return;
    }
    ItemDisplayInfoRec displayRecord{};
    const auto* display = g_itemDisplayInfoDB.GetRecord(kDisplay, &displayRecord) ? &displayRecord : nullptr;
    if (!display || !display->m_modelName[0] || !*display->m_modelName[0]) {
        status = "Everglow Lantern display unavailable";
        return;
    }
    const auto modelPath = std::string(kFolder) + display->m_modelName[0];
    lantern = parent->m_scene->CreateModel(modelPath.c_str(), 0);
    if (!lantern) {
        enabled = false;
        manualOverride = -1;
        status = "Lantern model failed to load";
        return;
    }
    if (display->m_modelTexture[0] && *display->m_modelTexture[0]) {
        CStatus textureStatus;
        const auto path = std::string(kFolder) + display->m_modelTexture[0] + ".blp";
        if (auto* texture = TextureCreate(path.c_str(), CGxTexFlags(), &textureStatus, 1)) {
            lantern->ReplaceTexture(2, texture);
            HandleClose(reinterpret_cast<HOBJECT>(texture));
        }
    }
    lantern->AttachToParent(parent, anchor, nullptr, 0);
    if (lantern->m_attachParent != parent) {
        ClearLantern();
        enabled = false;
        manualOverride = -1;
        status = "Lantern attachment failed";
        return;
    }
    ApplyTransform();
    status = "Attached";
}

void DrawWaistLantern(const WXL_Api* api) {
    int on = enabled ? 1 : 0;
    if (api->UiCheckbox("Wear Everglow Lantern", &on)) {
        enabled = on != 0;
        manualOverride = -1;
    }
    int scheduled = nightOnly ? 1 : 0;
    if (api->UiCheckbox("Lantern only during scheduled hours", &scheduled))
        nightOnly = scheduled != 0;
    if (nightOnly) {
        api->UiSliderInt("Lantern start hour", &startHour, 0, 23);
        api->UiSliderInt("Lantern end hour", &endHour, 0, 23);
        api->UiText("Equal hours keep the lantern available all day.");
    }
    api->UiCombo("Lantern attachment", &slot, kSlots, 3);
    if (api->UiCollapsingHeader("Lantern placement")) {
        if (api->UiSliderFloat("Lantern X", &offset.x, -1.0f, 1.0f)) transformDirty = true;
        if (api->UiSliderFloat("Lantern Y", &offset.y, -1.0f, 1.0f)) transformDirty = true;
        if (api->UiSliderFloat("Lantern Z", &offset.z, -1.0f, 1.0f)) transformDirty = true;
        if (api->UiSliderFloat("Lantern pitch", &rotation.x, -180.0f, 180.0f)) transformDirty = true;
        if (api->UiSliderFloat("Lantern roll", &rotation.y, -180.0f, 180.0f)) transformDirty = true;
        if (api->UiSliderFloat("Lantern yaw", &rotation.z, -180.0f, 180.0f)) transformDirty = true;
        if (api->UiSliderFloat("Lantern scale", &scale, 0.25f, 2.0f)) transformDirty = true;
    }
    if (manualOverride >= 0 && api->UiButton("Resume lantern schedule"))
        manualOverride = -1;
    api->UiText(status);
}

void DrawHunterQuiver(const WXL_Api* api) {
    const bool previousEnabled = quiverEnabled;
    const int previousChoice = quiverChoice;
    const int previousVisibility = quiverVisibility;
    const C3Vector previousOffset = quiverOffset;
    const C3Vector previousRotation = quiverRotation;
    const float previousScale = quiverScale;
    int on = quiverEnabled ? 1 : 0;
    if (api->UiCheckbox("Hunter cosmetic quiver", &on))
        quiverEnabled = on != 0;
    if (quiverEnabled) {
        api->UiCombo("Quiver model", &quiverChoice, kQuiverNames, kQuiverCount);
        api->UiCombo("Quiver visibility", &quiverVisibility, kQuiverVisibility, 3);
        if (api->UiCollapsingHeader("Quiver placement")) {
            if (api->UiSliderFloat("Quiver X", &quiverOffset.x, -1.0f, 1.0f)) quiverPlacementDirty = true;
            if (api->UiSliderFloat("Quiver Y", &quiverOffset.y, -1.0f, 1.0f)) quiverPlacementDirty = true;
            if (api->UiSliderFloat("Quiver Z", &quiverOffset.z, -1.0f, 1.0f)) quiverPlacementDirty = true;
            if (api->UiSliderFloat("Quiver pitch", &quiverRotation.x, -180.0f, 180.0f)) quiverPlacementDirty = true;
            if (api->UiSliderFloat("Quiver roll", &quiverRotation.y, -180.0f, 180.0f)) quiverPlacementDirty = true;
            if (api->UiSliderFloat("Quiver yaw", &quiverRotation.z, -180.0f, 180.0f)) quiverPlacementDirty = true;
            if (api->UiSliderFloat("Quiver scale", &quiverScale, 0.25f, 2.0f)) quiverPlacementDirty = true;
        }
    }
    if (previousEnabled != quiverEnabled || previousChoice != quiverChoice ||
        previousVisibility != quiverVisibility || previousOffset != quiverOffset ||
        previousRotation != quiverRotation || previousScale != quiverScale)
        TransferQuiverProfile(true);
    api->UiText(quiverStatus);
}
}
