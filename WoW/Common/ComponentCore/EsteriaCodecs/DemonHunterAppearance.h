// Generated from pinned Retail 12.1.0.69933 DH catalogs and this project's race map.
#pragma once
#include <array>
#include <cstdint>

namespace DemonHunterAppearance
{
    struct Option { std::uint16_t field, count, factor; };
    struct Requirement { std::uint16_t option, value, required; std::uint64_t mask; };

    inline constexpr std::array<Option, 12> BloodElfMaleOptions = {{
        {1, 20, 1}, // "Face"
        {0, 24, 1}, // "Skin Color"
        {2, 21, 1}, // "Hair Style"
        {3, 20, 1}, // "Hair Color"
        {4, 13, 1}, // "Facial Hair"
        {5, 3, 4}, // "Ears"
        {0, 7, 24}, // "Horns"
        {1, 12, 20}, // "Blindfold"
        {2, 7, 21}, // "Tattoo"
        {3, 6, 20}, // "Tattoo Color"
        {4, 15, 13}, // "Eye Color"
        {5, 4, 1}, // "Eyesight"
    }};
    inline constexpr std::array<char const*, 12> BloodElfMaleLabels = {{
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Facial Hair",
        "Ears",
        "Horns",
        "Blindfold",
        "Tattoo",
        "Tattoo Color",
        "Eye Color",
        "Eyesight",
    }};
    inline constexpr std::array<Requirement, 24> BloodElfMaleRequirements = {{
        {1, 10, 0, 151552},
        {1, 11, 0, 151552},
        {1, 12, 0, 151552},
        {9, 0, 8, 1},
        {9, 1, 8, 126},
        {9, 2, 8, 126},
        {9, 3, 8, 126},
        {9, 4, 8, 126},
        {9, 5, 8, 126},
        {10, 0, 1, 16252927},
        {10, 1, 1, 16252927},
        {10, 2, 1, 16252927},
        {10, 3, 1, 16252927},
        {10, 4, 1, 16252927},
        {10, 5, 1, 16252927},
        {10, 6, 1, 16252927},
        {10, 7, 1, 16252927},
        {10, 8, 1, 524288},
        {10, 9, 1, 16252927},
        {10, 10, 1, 16252927},
        {10, 11, 1, 16252927},
        {10, 12, 1, 16252927},
        {10, 13, 1, 16252927},
        {10, 14, 1, 16252927},
    }};
    inline constexpr std::array<std::uint16_t, 13> BloodElfMaleCapacities = {168, 240, 147, 120, 195, 12, 1, 1, 1, 1, 1, 1, 1};

    inline constexpr std::array<Option, 16> BloodElfFemaleOptions = {{
        {1, 20, 1}, // "Face"
        {0, 24, 1}, // "Skin Color"
        {2, 25, 1}, // "Hair Style"
        {3, 20, 1}, // "Hair Color"
        {0, 7, 24}, // "Horns"
        {4, 15, 1}, // "Eye Color"
        {1, 12, 20}, // "Blindfold"
        {6, 3, 4}, // "Ears"
        {4, 14, 15}, // "Earrings"
        {5, 4, 7}, // "Necklace"
        {2, 7, 25}, // "Tattoo"
        {3, 7, 20}, // "Tattoo Color"
        {5, 4, 28}, // "Armbands"
        {5, 2, 112}, // "Jewelry Color"
        {5, 7, 1}, // "Bracelets"
        {6, 4, 1}, // "Eyesight"
    }};
    inline constexpr std::array<char const*, 16> BloodElfFemaleLabels = {{
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Horns",
        "Eye Color",
        "Blindfold",
        "Ears",
        "Earrings",
        "Necklace",
        "Tattoo",
        "Tattoo Color",
        "Armbands",
        "Jewelry Color",
        "Bracelets",
        "Eyesight",
    }};
    inline constexpr std::array<Requirement, 29> BloodElfFemaleRequirements = {{
        {1, 11, 0, 271360},
        {1, 12, 0, 271360},
        {1, 13, 0, 271360},
        {5, 0, 1, 16776191},
        {5, 1, 1, 16776191},
        {5, 2, 1, 16776191},
        {5, 3, 1, 16776191},
        {5, 4, 1, 16776191},
        {5, 5, 1, 16776191},
        {5, 6, 1, 16776191},
        {5, 7, 1, 16776191},
        {5, 8, 1, 1024},
        {5, 9, 1, 16776191},
        {5, 10, 1, 16776191},
        {5, 11, 1, 16776191},
        {5, 12, 1, 16776191},
        {5, 13, 1, 16776191},
        {5, 14, 1, 16776191},
        {8, 5, 7, 1},
        {8, 6, 7, 1},
        {8, 9, 7, 1},
        {8, 10, 7, 1},
        {11, 0, 10, 1},
        {11, 1, 10, 126},
        {11, 2, 10, 126},
        {11, 3, 10, 126},
        {11, 4, 10, 126},
        {11, 5, 10, 126},
        {11, 6, 10, 126},
    }};
    inline constexpr std::array<std::uint16_t, 13> BloodElfFemaleCapacities = {168, 240, 175, 140, 210, 224, 12, 1, 1, 1, 1, 1, 1};

    inline constexpr std::array<Option, 19> NightElfMaleOptions = {{
        {1, 24, 1}, // "Face"
        {0, 23, 1}, // "Skin Color"
        {2, 17, 1}, // "Hair Style"
        {3, 17, 1}, // "Hair Color"
        {5, 5, 49}, // "Sideburns"
        {6, 5, 6}, // "Mustache"
        {4, 8, 1}, // "Beard"
        {4, 7, 8}, // "Horns"
        {2, 12, 17}, // "Blindfold"
        {1, 9, 24}, // "Eyebrows"
        {6, 5, 30}, // "Ears"
        {6, 6, 1}, // "Scars"
        {3, 9, 17}, // "Eye Color"
        {7, 2, 5}, // "Vines"
        {7, 5, 1}, // "Vine Color"
        {0, 11, 23}, // "Tattoo"
        {5, 7, 1}, // "Tattoo Color"
        {5, 7, 7}, // "Markings"
        {4, 4, 56}, // "Eyesight"
    }};
    inline constexpr std::array<char const*, 19> NightElfMaleLabels = {{
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Sideburns",
        "Mustache",
        "Beard",
        "Horns",
        "Blindfold",
        "Eyebrows",
        "Ears",
        "Scars",
        "Eye Color",
        "Vines",
        "Vine Color",
        "Tattoo",
        "Tattoo Color",
        "Markings",
        "Eyesight",
    }};
    inline constexpr std::array<Requirement, 21> NightElfMaleRequirements = {{
        {1, 9, 0, 9728},
        {1, 10, 0, 9728},
        {1, 11, 0, 9728},
        {12, 0, 1, 7864319},
        {12, 1, 1, 7864319},
        {12, 2, 1, 7864319},
        {12, 3, 1, 7864319},
        {12, 4, 1, 524288},
        {12, 5, 1, 7864319},
        {12, 5, 8, 1},
        {12, 6, 1, 7864319},
        {12, 7, 1, 7864319},
        {12, 7, 8, 64},
        {12, 8, 1, 7864319},
        {16, 0, 15, 1921},
        {16, 1, 15, 126},
        {16, 2, 15, 126},
        {16, 3, 15, 126},
        {16, 4, 15, 126},
        {16, 5, 15, 126},
        {16, 6, 15, 126},
    }};
    inline constexpr std::array<std::uint16_t, 13> NightElfMaleCapacities = {253, 216, 204, 153, 224, 245, 150, 10, 1, 1, 1, 1, 1};

    inline constexpr std::array<Option, 21> NightElfFemaleOptions = {{
        {1, 18, 1}, // "Face"
        {0, 23, 1}, // "Skin Color"
        {2, 20, 1}, // "Hair Style"
        {3, 17, 1}, // "Hair Color"
        {4, 2, 128}, // "Vines"
        {5, 5, 42}, // "Vine Color"
        {6, 4, 5}, // "Eyebrows"
        {6, 5, 1}, // "Ears"
        {1, 12, 18}, // "Blindfold"
        {5, 6, 7}, // "Scars"
        {4, 8, 16}, // "Eye Color"
        {0, 11, 23}, // "Markings"
        {3, 10, 17}, // "Markings Color"
        {6, 3, 80}, // "Headdress"
        {6, 4, 20}, // "Earrings"
        {7, 4, 1}, // "Nose Ring"
        {7, 4, 4}, // "Necklace"
        {5, 7, 1}, // "Horns"
        {2, 11, 20}, // "Tattoo"
        {4, 16, 1}, // "Tattoo Color"
        {7, 4, 16}, // "Eyesight"
    }};
    inline constexpr std::array<char const*, 21> NightElfFemaleLabels = {{
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Vines",
        "Vine Color",
        "Eyebrows",
        "Ears",
        "Blindfold",
        "Scars",
        "Eye Color",
        "Markings",
        "Markings Color",
        "Headdress",
        "Earrings",
        "Nose Ring",
        "Necklace",
        "Horns",
        "Tattoo",
        "Tattoo Color",
        "Eyesight",
    }};
    inline constexpr std::array<Requirement, 37> NightElfFemaleRequirements = {{
        {1, 9, 0, 98816},
        {1, 10, 0, 98816},
        {1, 11, 0, 98816},
        {10, 0, 1, 7864319},
        {10, 1, 1, 7864319},
        {10, 2, 1, 7864319},
        {10, 3, 1, 7864319},
        {10, 4, 1, 524288},
        {10, 5, 1, 7864319},
        {10, 6, 1, 7864319},
        {10, 7, 1, 7864319},
        {12, 0, 11, 1},
        {12, 1, 11, 2046},
        {12, 2, 11, 2046},
        {12, 3, 11, 2046},
        {12, 4, 11, 2046},
        {12, 5, 11, 2046},
        {12, 6, 11, 2046},
        {12, 7, 11, 2046},
        {12, 8, 11, 2046},
        {12, 9, 11, 2046},
        {19, 0, 18, 1},
        {19, 1, 18, 126},
        {19, 2, 18, 126},
        {19, 3, 18, 126},
        {19, 4, 18, 126},
        {19, 5, 18, 126},
        {19, 6, 18, 126},
        {19, 7, 18, 1920},
        {19, 8, 18, 1920},
        {19, 9, 18, 1920},
        {19, 10, 18, 1920},
        {19, 11, 18, 1920},
        {19, 12, 18, 1920},
        {19, 13, 18, 1920},
        {19, 14, 18, 1920},
        {19, 15, 18, 1920},
    }};
    inline constexpr std::array<std::uint16_t, 13> NightElfFemaleCapacities = {253, 216, 220, 170, 256, 210, 240, 64, 1, 1, 1, 1, 1};

    constexpr unsigned Choice(std::array<std::uint8_t, 13> const& fields, Option const& option)
    { return (fields[option.field] / option.factor) % option.count; }

    template<class Options, class Requirements>
    constexpr bool ValidateProfile(std::array<std::uint8_t, 13> const& fields, Options const& options,
        Requirements const& requirements, std::array<std::uint16_t, 13> const& capacities)
    {
        for (unsigned i = 0; i < capacities.size(); ++i)
            if (fields[i] >= capacities[i]) return false;
        for (Requirement const& rule : requirements)
            if (Choice(fields, options[rule.option]) == rule.value
                && !(rule.mask & (std::uint64_t{1} << Choice(fields, options[rule.required])))) return false;
        return true;
    }

    constexpr bool Validate(unsigned race, unsigned gender, std::array<std::uint8_t, 13> const& fields)
    {
        if (race == 17 && gender == 0)
            return ValidateProfile(fields, BloodElfMaleOptions, BloodElfMaleRequirements, BloodElfMaleCapacities);
        if (race == 17 && gender == 1)
            return ValidateProfile(fields, BloodElfFemaleOptions, BloodElfFemaleRequirements, BloodElfFemaleCapacities);
        if (race == 21 && gender == 0)
            return ValidateProfile(fields, NightElfMaleOptions, NightElfMaleRequirements, NightElfMaleCapacities);
        if (race == 21 && gender == 1)
            return ValidateProfile(fields, NightElfFemaleOptions, NightElfFemaleRequirements, NightElfFemaleCapacities);
        return false;
    }
}
