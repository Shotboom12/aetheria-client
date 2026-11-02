#pragma once
#include "WoW/Source/Object/Types.h"
#include "WoW/Source/Object/ObjectClient/Types.h"

namespace NativeMountFlight {

inline bool LockVariation(uint32_t display) {
    return display == 200003 || display == 200004;
}

// Use actual direction for every locomotion refresh, not the incoming clip's
// direction. Profiles are verified against each imported model's original IDs.
inline bool Resolve(uint32_t display, uint32_t movement, uint32_t& anim) {
    if (!(movement & MOVEMENTFLAG_FLYING)) return false;
    uint32_t hover, forward, backward;
    switch (display) {
        case 200000:             // Mastercraft Gravewing, model 3852506
            hover = ANIM_FLY_STAND;
            forward = ANIM_FLY;
            backward = ANIM_SWIM_BACKWARDS;
            break;
        case 200002:             // Primeval Skyfriend, model 7553115
            hover = ANIM_FLY_STAND;
            forward = ANIM_FLY_RUN;
            backward = ANIM_SWIM_BACKWARDS;
            break;
        case 200003: case 200004: // Hex eagles
        case 200006:             // Argent Hippogryph, model 124410
            hover = ANIM_FLY_STAND;
            forward = ANIM_FLY_RUN;
            backward = ANIM_FLY_WALKBACKWARDS;
            break;
        case 200008:             // Azure Drake, model 234564: no 229/234/242 family
        case 200011:             // Sunreaver Dragonhawk, model 234547
            hover = ANIM_SWIM_IDLE;
            forward = ANIM_SWIM;
            backward = ANIM_SWIM_BACKWARDS;
            break;
        default: return false;
    }
    switch (anim) {
        case ANIM_STAND: case ANIM_WALK: case ANIM_RUN: case ANIM_WALKBACKWARDS:
        case ANIM_SWIM_IDLE: case ANIM_SWIM: case ANIM_SWIM_LEFT:
        case ANIM_SWIM_RIGHT: case ANIM_SWIM_BACKWARDS: case ANIM_FLY:
        case ANIM_FLY_STAND: case ANIM_FLY_WALK: case ANIM_FLY_RUN:
        case ANIM_FLY_WALKBACKWARDS: case ANIM_FLY_FLY:
        case ANIM_FLY_SWIM_IDLE: case ANIM_FLY_SWIM: case ANIM_FLY_SWIM_LEFT:
        case ANIM_FLY_SWIM_RIGHT: case ANIM_FLY_SWIM_BACKWARDS:
            break;
        default: return false; // Keep takeoff, landing, spells, attacks and emotes.
    }
    // All seven source models author distinct 43/44 clips (no 272/273 family).
    // Match stock precedence: Q/E, including diagonal travel, uses the lateral pose.
    const bool left = (movement & MOVEMENTFLAG_STRAFE_LEFT) != 0;
    const bool right = (movement & MOVEMENTFLAG_STRAFE_RIGHT) != 0;
    if (left != right) anim = left ? ANIM_SWIM_LEFT : ANIM_SWIM_RIGHT;
    else if (movement & MOVEMENTFLAG_BACKWARD) anim = backward;
    else if (movement & (MOVEMENTFLAG_FORWARD | MOVEMENTFLAG_ASCENDING | MOVEMENTFLAG_DESCENDING)) anim = forward;
    else anim = hover;
    return true;
}

}
