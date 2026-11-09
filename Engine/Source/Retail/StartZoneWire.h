#ifndef CHARACTER_START_ZONE_WIRE_H
#define CHARACTER_START_ZONE_WIRE_H

#include <cstddef>
#include <cstdint>

// Shared verbatim with the server copy at src/server/shared/StartZoneWire.h.
namespace CharacterStartZone
{
    inline constexpr uint8_t Version = 1;
    inline constexpr size_t PayloadSize = 2;
    inline constexpr uint32_t FooterMagic = 0x314E5A53; // "SZN1"
    inline constexpr size_t FooterSize = PayloadSize + 4;

    enum Zone : uint8_t
    {
        RaceDefault = 0,
        Northshire = 1,
        ColdridgeValley = 2,
        Shadowglen = 3,
        AmmenVale = 4,
        ValleyOfTrials = 5,
        CampNarache = 6,
        Deathknell = 7,
        SunstriderIsle = 8,
        Count = 9,
    };

    enum Faction : uint8_t
    {
        NoFaction = 0,
        Alliance = 1,
        Horde = 2,
    };

    constexpr bool Selectable(uint8_t zone)
    {
        return zone < Count;
    }

    constexpr uint8_t StartRace(uint8_t zone)
    {
        constexpr uint8_t races[Count] = { 0, 1, 3, 4, 11, 2, 6, 5, 10 };
        return Selectable(zone) ? races[zone] : 0;
    }

    constexpr Faction StartFaction(uint8_t zone)
    {
        return zone >= Northshire && zone <= AmmenVale ? Alliance
            : zone >= ValleyOfTrials && zone <= SunstriderIsle ? Horde : NoFaction;
    }

    inline void WriteFooter(uint8_t* out, uint8_t zone)
    {
        out[0] = Version;
        out[1] = zone;
        out[2] = static_cast<uint8_t>(FooterMagic);
        out[3] = static_cast<uint8_t>(FooterMagic >> 8);
        out[4] = static_cast<uint8_t>(FooterMagic >> 16);
        out[5] = static_cast<uint8_t>(FooterMagic >> 24);
    }

    template <class Packet>
    bool ReadTail(Packet& packet, bool& present, uint8_t& zone)
    {
        size_t const read = packet.rpos();
        if (read > packet.size())
            return false;
        size_t const remaining = packet.size() - read;
        if (remaining < FooterSize)
            return true;
        uint8_t const* tail = packet.contents() + packet.size() - FooterSize;
        uint32_t const magic = uint32_t(tail[PayloadSize]) | uint32_t(tail[PayloadSize + 1]) << 8
            | uint32_t(tail[PayloadSize + 2]) << 16 | uint32_t(tail[PayloadSize + 3]) << 24;
        if (magic != FooterMagic)
            return true;
        if (tail[0] != Version || !Selectable(tail[1]))
            return false;
        zone = tail[1];
        packet.resize(packet.size() - FooterSize);
        packet.rpos(read);
        present = true;
        return true;
    }
}

#endif
