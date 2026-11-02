#pragma once

#include <array>
#include <cstdint>

namespace OrcPostureAppearance
{
    constexpr bool Uses(unsigned race)
    {
        return race == 2 || race == 14;
    }

    // Version 1 uses the existing DHC1 transport, leaving the five native bytes intact.
    inline constexpr std::uint32_t Marker = 0x800000;
    inline constexpr unsigned UprightDisplay = 59000;

    constexpr bool Validate(unsigned race, unsigned gender, std::array<std::uint8_t, 13> const& fields)
    {
        if (!Uses(race) || gender > 1)
            return false;
        for (unsigned i = 8; i < fields.size(); ++i)
            if (fields[i])
                return false;
        // Unextended characters keep their original appearance and hunched posture.
        if (!fields[5] && !fields[6] && !fields[7])
            return true;
        return gender == 0 && fields[5] == 0 && fields[6] <= 1 && fields[7] == 0x80;
    }

    constexpr std::uint32_t Encode(unsigned posture)
    {
        return Marker | ((posture & 1) << 8);
    }

    constexpr bool Upright(unsigned race, unsigned gender, std::uint32_t extra)
    {
        return Uses(race) && gender == 0 && extra == Encode(1);
    }
}
