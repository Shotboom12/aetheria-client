#ifndef ENGINE_SOURCE_RETAIL_RACEPOLICY_H
#define ENGINE_SOURCE_RETAIL_RACEPOLICY_H
#include <cstdint>
#include "Engine/Source/Retail/SourceContext.h"

namespace Retail {
// Ethereal inherited an NPC barefoot flag, but its player body has complete shoe surfaces.
inline bool BareFeet(uint32_t gameRace, uint32_t raceFlags) {
    return gameRace != 77 && gameRace != 78 && (raceFlags & 2) != 0;
}
// Nightborne anatomy uses the installed Night Elf helmet family; only the new Ethereal needs this file alias.
inline uint32_t HelmetModelRace(uint32_t gameRace) { return gameRace == 77 || gameRace == 78 ? 4 : gameRace; }
// Local normal-elf identities share a Retail model with Illidari. Keep the
// table's option order stable for saved looks; hide only their DH options.
inline bool NormalElfDemonAccessory(uint32_t localRace, uint32_t optionId) {
    return ((localRace == 10 || localRace == 24)
        && (optionId == 117 || optionId == 118 || optionId == 126 || optionId == 127))
        || (localRace == 4 && (optionId == 47 || optionId == 48 || optionId == 56 || optionId == 57));
}

// Kyrian match same-sex Humans; Venthyr stand just 1% above same-sex Night Elves. Donor
// bounds include wings/headdresses and cannot be used as a player's body height.
inline constexpr bool NativeHumanoidRace(uint32_t race) { return race == 60 || race == 61; }
inline constexpr float NativeHumanoidScale(uint32_t race, uint32_t sex) {
    const float humanHead = sex == 1 ? 1.711866260f : 1.847610235f;
    const float nightElfHead = sex == 1 ? 2.041548491f : 2.257565975f;
    return race == 60 ? humanHead / 3.511086226f
        : race == 61 ? (nightElfHead * 1.01f) / 2.702763081f : 1.0f;
}

inline uint32_t CharacterModel(uint32_t, uint32_t, uint32_t defaultModel) { return defaultModel; }
// Helmet masks describe visual anatomy, not faction/skill eligibility. Always bound the shift:
// target custom races above 31 cannot index the installed 32-bit HelmetGeosetVisData directly.
inline uint32_t HelmetMaskRace(uint32_t retailRace) {
    switch (retailRace) {
        case 0xf0030000u: return 4;          // Ethereal's Nightborne-compatible equipment anatomy.
        case 0xf0010000u: return 1;
        case 0xf0020000u: return 11;
        case 27: return 4;                  // Nightborne -> Night Elf
        case 28: return 6;                  // Highmountain -> Tauren
        case 29: return 10;                 // Void Elf -> Blood Elf
        case 30: return 11;                 // Lightforged -> Draenei
        case 31: return 8;                  // Zandalari -> Troll
        case 32: return 1;                  // Kul Tiran -> Human
        case 34: case 84: case 85: return 3; // Dark Iron / Earthen -> Dwarf
        case 35: return 9;                  // Vulpera -> Goblin
        case 36: return 2;                  // Mag'har -> Orc
        case 37: return 7;                  // Mechagnome -> Gnome
        case 86: case 91: return 4;         // Haranir -> Night Elf (both factions)
        case 75: case 76: return 1;         // Dracthyr visage; authored Human fallback
        default: return retailRace < 32 ? retailRace : 0;
    }
}

inline bool UsesLegacyClosedFeet(uint32_t retailRace) {
    switch (retailRace) {
        case 1: case 2: case 3: case 4: case 5: case 7: case 9: case 10:
        case 24: case 25: case 26: case 27: case 29: case 32: case 34: case 36:
        case 75: case 76:
            return true;
        default: return false; // Preserve hooves, exposed paws/toes and authored mechanical/Feet choices.
    }
}
}
#endif
