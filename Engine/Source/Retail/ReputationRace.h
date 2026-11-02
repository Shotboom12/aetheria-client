#pragma once
#include <cstdint>

namespace Retail {
// Match the server's legacy eligibility mask. Actual race identity and faction
// remain unchanged, as do the character's earned reputation offsets.
inline constexpr uint32_t ReputationRaceMask(uint32_t race) {
    switch (race) {
        case 12: case 19: case 25: case 30: race=1; break;
        case 13: case 21: case 24: race=4; break;
        case 16: case 29: race=3; break;
        case 20: case 28: race=2; break;
        case 22: race=8; break;
        case 26: race=10; break;
        case 27: race=11; break;
        case 31: race=6; break;
        case 75: race=1; break;
        case 74: case 76: race=2; break;
        case 60: race=11; break;
        case 61: race=5; break;
        case 46: race=6; break;
        case 47: race=7; break;
        case 50: race=4; break;
        case 51: race=8; break;
        case 52: race=4; break;
        case 53: case 54: race=10; break;
        case 55: race=3; break;
        case 56: race=1; break;
        case 63: case 68: case 70: case 71: race=2; break;
        case 69: case 72: race=1; break;
        case 62: case 65: case 67: race=1; break;
        case 64: case 66: race=2; break;
        default: break;
    }
    return race && race<=32 ? uint32_t{1}<<(race-1) : 0;
}
}
