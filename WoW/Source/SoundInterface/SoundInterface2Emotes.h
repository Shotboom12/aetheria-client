#ifndef WOW_SOURCE_SOUNDINTERFACE_SOUNDINTERFACE2EMOTES_H
#define WOW_SOURCE_SOUNDINTERFACE_SOUNDINTERFACE2EMOTES_H

#include <cstddef>
#include <cstdint>

// Aetheria: stock 3.3.5a stores 22 races here, which silently drops text-emote sounds of custom races
// (Kyrian 60, Venthyr 61, Hozen 112, ...). Raised to 128; the layout is private to this file.
constexpr uint32_t kTextEmoteMaxRaces = 128;

struct TEXTEMOTELOOKUP {
    int32_t m_soundIds[kTextEmoteMaxRaces][2];
    int32_t m_emotesTextId;

    TEXTEMOTELOOKUP();
};

#endif
