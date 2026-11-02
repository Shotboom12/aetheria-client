#pragma once
#include <cstdint>

namespace LegacyFlightBounds {

inline bool ExpandedWorldArea(int32_t continent, int32_t rootArea) {
    if (continent == 0 || continent == 1) return true;
    if (continent != 530) return false;
    switch (rootArea) {
        case 3430: case 3433: case 3487: // Eversong, Ghostlands, Silvermoon
        case 3524: case 3525: case 3557: // Azuremyst, Bloodmyst, Exodar
        case 4080: return true;          // Quel'Danas
        default: return false;           // Keep authored Outland flight bounds.
    }
}

inline bool IsNoFlyPlaceholder(const int16_t* bounds) {
    if (!bounds) return false;
    // Actual Silvermoon ADTs use this flat ceiling below normal terrain. It
    // prohibited flight before these world areas were explicitly enabled.
    for (unsigned i = 0; i < 9; ++i)
        if (bounds[i] != 3 || bounds[i + 9] != -1000) return false;
    return true;
}

inline bool ExpandPlaceholder(int16_t* bounds, bool eligibleArea) {
    if (!eligibleArea || !IsNoFlyPlaceholder(bounds)) return false;
    // No usable authored upper ceiling exists here. Use the encoded maximum;
    // preserve the lower surface and all non-placeholder ceilings.
    for (unsigned i = 0; i < 9; ++i) bounds[i] = INT16_MAX;
    return true;
}

}
