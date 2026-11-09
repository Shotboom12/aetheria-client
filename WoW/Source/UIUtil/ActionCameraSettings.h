#pragma once
#include <cstdint>
#include "Engine/Source/Tempest/C3Vector.h"

namespace ActionCamera {
    struct UserSettings {
        bool shoulderEnabled = false;
        float shoulderOffset = 0.75f;
        bool combatEnabled = false;
        float combatOffset = 1.0f;
        bool dialogueEnabled = false;
        float dialogueOffset = 0.35f;
        bool groundMountEnabled = false;
        float groundMountOffset = 0.75f;
        bool flyingMountEnabled = false;
        float flyingMountOffset = 0.75f;
        bool vehicleEnabled = false;
        float vehicleOffset = 0.5f;
        bool cleanDialogueVisuals = false;
        bool preciseUnitHover = false;
        int unitHoverWidth = 80;
        bool preciseObjectHover = false;
        int objectHoverWidth = 75;
        bool footPlacementEnabled = false;
        bool movingFootPlacementEnabled = false;
        bool slopeAlignmentEnabled = false;
        bool pelvisCompensationEnabled = false;
        bool footprintProbesEnabled = false;
        bool lanternEnabled = false;
        bool lanternNightOnly = false;
        int lanternStartHour = 20;
        int lanternEndHour = 6;
        int lanternSlot = 0;
        C3Vector lanternOffset{0.0f, 0.0f, 0.0f};
        C3Vector lanternRotation{0.0f, 0.0f, 0.0f};
        float lanternScale = 1.0f;
        int reticleShape = 8;
        int reticleTheme = 0;
        bool quiverEnabled = false;
        int quiverChoice = 0;
        int quiverVisibility = 0;
        C3Vector quiverOffset{0.0f, 0.0f, 0.0f};
        C3Vector quiverRotation{0.0f, 0.0f, 0.0f};
        float quiverScale = 1.0f;
    };
    inline UserSettings& GetUserSettings() {
        static UserSettings settings;
        return settings;
    }
    inline UserSettings& userSettings = GetUserSettings();

    inline uint32_t rangedShotSerial = 0;
    inline bool suppressDialogueVisuals = false;

    inline bool& shoulderEnabled = userSettings.shoulderEnabled;
    inline float& shoulderOffset = userSettings.shoulderOffset;
    inline bool& combatEnabled = userSettings.combatEnabled;
    inline float& combatOffset = userSettings.combatOffset;
    inline bool& dialogueEnabled = userSettings.dialogueEnabled;
    inline float& dialogueOffset = userSettings.dialogueOffset;
    inline bool& groundMountEnabled = userSettings.groundMountEnabled;
    inline float& groundMountOffset = userSettings.groundMountOffset;
    inline bool& flyingMountEnabled = userSettings.flyingMountEnabled;
    inline float& flyingMountOffset = userSettings.flyingMountOffset;
    inline bool& vehicleEnabled = userSettings.vehicleEnabled;
    inline float& vehicleOffset = userSettings.vehicleOffset;
    inline bool& cleanDialogueVisuals = userSettings.cleanDialogueVisuals;
    inline bool& preciseUnitHover = userSettings.preciseUnitHover;
    inline int& unitHoverWidth = userSettings.unitHoverWidth;
    inline bool& preciseObjectHover = userSettings.preciseObjectHover;
    inline int& objectHoverWidth = userSettings.objectHoverWidth;
}
