// Generated from the pinned Retail Tauren option graph by generate_tauren_codec.py.
#ifndef ESTERIA_TAUREN_APPEARANCE_H
#define ESTERIA_TAUREN_APPEARANCE_H

#include <array>
#include <cstdint>

namespace TaurenAppearance
{
    struct Option
    {
        std::uint16_t field;
        std::uint16_t count;
        std::uint16_t factor;
    };

    struct Requirement
    {
        std::uint16_t option;
        std::uint16_t value;
        std::uint16_t required;
        std::uint64_t mask;
    };

    struct Eligibility { std::uint16_t option, value; std::uint32_t classes; };
    constexpr bool IsModern(std::array<std::uint8_t, 13> const& fields) { return fields[12] == 0xC6; }
    constexpr void Mark(std::array<std::uint8_t, 13>& fields) { fields[12] = 0xC6; }

    inline constexpr std::array<Option, 17> MaleOptions = {{
        {9, 10, 1}, // Face
        {5, 25, 1}, // Skin Color
        {6, 17, 1}, // Horn Style
        {11, 6, 1}, // Horn Color
        {9, 4, 10}, // Foremane
        {8, 11, 1}, // Hair
        {7, 13, 1}, // Facial Hair
        {10, 8, 1}, // Eye Color
        {8, 4, 11}, // Face Paint
        {5, 3, 25}, // Headdress
        {10, 5, 8}, // Nose Ring
        {11, 3, 36}, // Necklace
        {10, 1, 40}, // Jewelry Color
        {9, 2, 40}, // Flower
        {7, 4, 13}, // Body Paint
        {11, 6, 6}, // Paint Color
        {6, 4, 17}, // Eyesight
    }};

    inline constexpr std::array<char const*, 17> MaleLabels = {
        "Face",
        "Skin Color",
        "Horn Style",
        "Horn Color",
        "Foremane",
        "Hair",
        "Facial Hair",
        "Eye Color",
        "Face Paint",
        "Headdress",
        "Nose Ring",
        "Necklace",
        "Jewelry Color",
        "Flower",
        "Body Paint",
        "Paint Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> MaleCapacities = {256, 256, 256, 256, 256, 75, 68, 52, 44, 80, 40, 108, 256};

    inline constexpr std::array<Requirement, 3> MaleRequirements = {{
        {1, 19, 0, 672},
        {1, 20, 0, 672},
        {1, 21, 0, 672},
    }};

    inline constexpr std::array<Eligibility, 126> MaleEligibility = {{
        {0, 0, 32735u},
        {0, 1, 32735u},
        {0, 2, 32735u},
        {0, 3, 32735u},
        {0, 4, 32735u},
        {0, 5, 32u},
        {0, 6, 32u},
        {0, 7, 32u},
        {0, 8, 32u},
        {0, 9, 32u},
        {1, 0, 4294967295u},
        {1, 1, 4294967295u},
        {1, 2, 4294967295u},
        {1, 3, 4294967295u},
        {1, 4, 4294967295u},
        {1, 5, 4294967295u},
        {1, 6, 4294967295u},
        {1, 7, 4294967295u},
        {1, 8, 4294967295u},
        {1, 9, 4294967295u},
        {1, 10, 4294967295u},
        {1, 11, 4294967295u},
        {1, 12, 4294967295u},
        {1, 13, 4294967295u},
        {1, 14, 4294967295u},
        {1, 15, 4294967295u},
        {1, 16, 4294967295u},
        {1, 17, 4294967295u},
        {1, 18, 4294967295u},
        {1, 19, 32u},
        {1, 20, 32u},
        {1, 21, 32u},
        {1, 22, 4294967295u},
        {1, 23, 4294967295u},
        {1, 24, 4294967295u},
        {2, 0, 4294967295u},
        {2, 1, 4294967295u},
        {2, 2, 4294967295u},
        {2, 3, 4294967295u},
        {2, 4, 4294967295u},
        {2, 5, 4294967295u},
        {2, 6, 4294967295u},
        {2, 7, 4294967295u},
        {2, 8, 4294967295u},
        {2, 9, 4294967295u},
        {2, 10, 4294967295u},
        {2, 11, 4294967295u},
        {2, 12, 4294967295u},
        {2, 13, 4294967295u},
        {2, 14, 4294967295u},
        {2, 15, 4294967295u},
        {2, 16, 4294967295u},
        {3, 0, 4294967295u},
        {3, 1, 4294967295u},
        {3, 2, 4294967295u},
        {3, 3, 4294967295u},
        {3, 4, 4294967295u},
        {3, 5, 4294967295u},
        {4, 0, 4294967295u},
        {4, 1, 4294967295u},
        {4, 2, 4294967295u},
        {4, 3, 4294967295u},
        {5, 0, 4294967295u},
        {5, 1, 4294967295u},
        {5, 2, 4294967295u},
        {5, 3, 4294967295u},
        {5, 4, 4294967295u},
        {5, 5, 4294967295u},
        {5, 6, 4294967295u},
        {5, 7, 4294967295u},
        {5, 8, 4294967295u},
        {5, 9, 4294967295u},
        {5, 10, 4294967295u},
        {6, 0, 4294967295u},
        {6, 1, 4294967295u},
        {6, 2, 4294967295u},
        {6, 3, 4294967295u},
        {6, 4, 4294967295u},
        {6, 5, 4294967295u},
        {6, 6, 4294967295u},
        {6, 7, 4294967295u},
        {6, 8, 4294967295u},
        {6, 9, 4294967295u},
        {6, 10, 4294967295u},
        {6, 11, 4294967295u},
        {6, 12, 4294967295u},
        {7, 0, 32735u},
        {7, 1, 32735u},
        {7, 2, 32735u},
        {7, 3, 32735u},
        {7, 4, 32735u},
        {7, 5, 32735u},
        {7, 6, 32735u},
        {7, 7, 32u},
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
        {8, 2, 4294967295u},
        {8, 3, 4294967295u},
        {9, 0, 4294967295u},
        {9, 1, 4294967295u},
        {9, 2, 4294967295u},
        {10, 0, 4294967295u},
        {10, 1, 4294967295u},
        {10, 2, 4294967295u},
        {10, 3, 4294967295u},
        {10, 4, 4294967295u},
        {11, 0, 4294967295u},
        {11, 1, 4294967295u},
        {11, 2, 4294967295u},
        {12, 0, 4294967295u},
        {13, 0, 4294967295u},
        {13, 1, 4294967295u},
        {14, 0, 4294967295u},
        {14, 1, 4294967295u},
        {14, 2, 4294967295u},
        {14, 3, 4294967295u},
        {15, 0, 4294967295u},
        {15, 1, 4294967295u},
        {15, 2, 4294967295u},
        {15, 3, 4294967295u},
        {15, 4, 4294967295u},
        {15, 5, 4294967295u},
        {16, 0, 4294967295u},
        {16, 1, 30687u},
        {16, 2, 30687u},
        {16, 3, 30687u},
    }};

    inline constexpr std::array<Option, 17> FemaleOptions = {{
        {8, 8, 1}, // Face
        {6, 17, 1}, // Skin Color
        {5, 20, 1}, // Horn Style
        {6, 3, 17}, // Horn Color
        {5, 3, 20}, // Foremane
        {7, 15, 1}, // Hair
        {9, 8, 1}, // Eye Color
        {11, 4, 5}, // Face Paint
        {11, 3, 20}, // Headdress
        {11, 5, 1}, // Earrings
        {10, 4, 6}, // Nose Ring
        {8, 4, 8}, // Necklace
        {8, 1, 32}, // Jewelry Color
        {10, 2, 24}, // Flower
        {9, 4, 8}, // Body Paint
        {10, 6, 1}, // Paint Color
        {7, 4, 15}, // Eyesight
    }};

    inline constexpr std::array<char const*, 17> FemaleLabels = {
        "Face",
        "Skin Color",
        "Horn Style",
        "Horn Color",
        "Foremane",
        "Hair",
        "Eye Color",
        "Face Paint",
        "Headdress",
        "Earrings",
        "Nose Ring",
        "Necklace",
        "Jewelry Color",
        "Flower",
        "Body Paint",
        "Paint Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> FemaleCapacities = {256, 256, 256, 256, 256, 60, 51, 60, 32, 32, 48, 60, 256};

    inline constexpr std::array<Requirement, 3> FemaleRequirements = {{
        {1, 11, 0, 208},
        {1, 12, 0, 208},
        {1, 13, 0, 208},
    }};

    inline constexpr std::array<Eligibility, 111> FemaleEligibility = {{
        {0, 0, 32735u},
        {0, 1, 32735u},
        {0, 2, 32735u},
        {0, 3, 32735u},
        {0, 4, 32u},
        {0, 5, 32u},
        {0, 6, 32u},
        {0, 7, 32u},
        {1, 0, 4294967295u},
        {1, 1, 4294967295u},
        {1, 2, 4294967295u},
        {1, 3, 4294967295u},
        {1, 4, 4294967295u},
        {1, 5, 4294967295u},
        {1, 6, 4294967295u},
        {1, 7, 4294967295u},
        {1, 8, 4294967295u},
        {1, 9, 4294967295u},
        {1, 10, 4294967295u},
        {1, 11, 32u},
        {1, 12, 32u},
        {1, 13, 32u},
        {1, 14, 4294967295u},
        {1, 15, 4294967295u},
        {1, 16, 4294967295u},
        {2, 0, 4294967295u},
        {2, 1, 4294967295u},
        {2, 2, 4294967295u},
        {2, 3, 4294967295u},
        {2, 4, 4294967295u},
        {2, 5, 4294967295u},
        {2, 6, 4294967295u},
        {2, 7, 4294967295u},
        {2, 8, 4294967295u},
        {2, 9, 4294967295u},
        {2, 10, 4294967295u},
        {2, 11, 4294967295u},
        {2, 12, 4294967295u},
        {2, 13, 4294967295u},
        {2, 14, 4294967295u},
        {2, 15, 4294967295u},
        {2, 16, 4294967295u},
        {2, 17, 4294967295u},
        {2, 18, 4294967295u},
        {2, 19, 4294967295u},
        {3, 0, 4294967295u},
        {3, 1, 4294967295u},
        {3, 2, 4294967295u},
        {4, 0, 4294967295u},
        {4, 1, 4294967295u},
        {4, 2, 4294967295u},
        {5, 0, 4294967295u},
        {5, 1, 4294967295u},
        {5, 2, 4294967295u},
        {5, 3, 4294967295u},
        {5, 4, 4294967295u},
        {5, 5, 4294967295u},
        {5, 6, 4294967295u},
        {5, 7, 4294967295u},
        {5, 8, 4294967295u},
        {5, 9, 4294967295u},
        {5, 10, 4294967295u},
        {5, 11, 4294967295u},
        {5, 12, 4294967295u},
        {5, 13, 4294967295u},
        {5, 14, 4294967295u},
        {6, 0, 32735u},
        {6, 1, 32735u},
        {6, 2, 32735u},
        {6, 3, 32735u},
        {6, 4, 32735u},
        {6, 5, 32735u},
        {6, 6, 32735u},
        {6, 7, 32u},
        {7, 0, 4294967295u},
        {7, 1, 4294967295u},
        {7, 2, 4294967295u},
        {7, 3, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
        {8, 2, 4294967295u},
        {9, 0, 4294967295u},
        {9, 1, 4294967295u},
        {9, 2, 4294967295u},
        {9, 3, 4294967295u},
        {9, 4, 4294967295u},
        {10, 0, 4294967295u},
        {10, 1, 4294967295u},
        {10, 2, 4294967295u},
        {10, 3, 4294967295u},
        {11, 0, 4294967295u},
        {11, 1, 4294967295u},
        {11, 2, 4294967295u},
        {11, 3, 4294967295u},
        {12, 0, 4294967295u},
        {13, 0, 4294967295u},
        {13, 1, 4294967295u},
        {14, 0, 4294967295u},
        {14, 1, 4294967295u},
        {14, 2, 4294967295u},
        {14, 3, 4294967295u},
        {15, 0, 4294967295u},
        {15, 1, 4294967295u},
        {15, 2, 4294967295u},
        {15, 3, 4294967295u},
        {15, 4, 4294967295u},
        {15, 5, 4294967295u},
        {16, 0, 4294967295u},
        {16, 1, 30687u},
        {16, 2, 30687u},
        {16, 3, 30687u},
    }};

    template<class Options, class Requirements, class EligibilityTable>
    constexpr bool ValidateProfile(std::array<std::uint8_t, 13> fields, Options const& options,
        Requirements const& requirements, std::array<std::uint16_t, 13> const& capacities,
        EligibilityTable const& eligibility, unsigned characterClass)
    {
        if (!IsModern(fields) || characterClass > 11) return false;

        for (unsigned i = 5; i < fields.size(); ++i)
            if (fields[i] >= capacities[i])
                return false;
        auto choice = [&](unsigned index)
        {
            Option const& option = options[index];
            return (fields[option.field] / option.factor) % option.count;
        };
        if (characterClass)
            for (Eligibility const& entry : eligibility)
                if (choice(entry.option) == entry.value && !(entry.classes & (std::uint32_t{1} << (characterClass - 1))))
                    return false;
        for (Requirement const& requirement : requirements)
            if (choice(requirement.option) == requirement.value
                && !(requirement.mask & (std::uint64_t{1} << choice(requirement.required))))
                return false;
        return true;
    }

    constexpr bool Validate(unsigned gender, std::array<std::uint8_t, 13> const& fields, unsigned characterClass = 0)
    {
        if (gender == 0)
            return ValidateProfile(fields, MaleOptions, MaleRequirements, MaleCapacities, MaleEligibility, characterClass);
        if (gender == 1)
            return ValidateProfile(fields, FemaleOptions, FemaleRequirements, FemaleCapacities, FemaleEligibility, characterClass);
        return false;
    }

    constexpr unsigned Choice(std::array<std::uint8_t, 13> const& fields, Option option)
    { return fields[option.field] / option.factor % option.count; }

    template<class Options, class EligibilityTable, class Requirements>
    constexpr std::array<std::uint8_t, 13> NormalizeProfile(std::array<std::uint8_t, 13> fields,
        unsigned characterClass, Options const& options, EligibilityTable const& eligibility,
        Requirements const& requirements)
    {
        if (!IsModern(fields)) fields = {};
        Mark(fields);
        for (unsigned pass = 0; pass < options.size(); ++pass)
            for (unsigned index = 0; index < options.size(); ++index)
            {
                auto allowed = [&](unsigned value)
                {
                    for (auto rule : eligibility)
                        if (rule.option == index && rule.value == value && characterClass
                            && !(rule.classes & (std::uint32_t(1) << (characterClass - 1)))) return false;
                    for (auto rule : requirements)
                        if (rule.option == index && rule.value == value
                            && !(rule.mask & (std::uint64_t(1) << Choice(fields, options[rule.required])))) return false;
                    return true;
                };
                if (!allowed(Choice(fields, options[index])))
                    for (unsigned value = 0; value < options[index].count; ++value)
                        if (allowed(value))
                        {
                            auto option = options[index];
                            fields[option.field] = static_cast<std::uint8_t>(fields[option.field]
                                - Choice(fields, option) * option.factor + value * option.factor);
                            break;
                        }
            }
        return fields;
    }
    constexpr std::array<std::uint8_t, 13> Normalize(unsigned gender,
        std::array<std::uint8_t, 13> fields, unsigned characterClass)
    {
        if (!Validate(gender, fields)) fields = {};
        if (gender == 0) return NormalizeProfile(fields, characterClass, MaleOptions, MaleEligibility, MaleRequirements);
        if (gender == 1) return NormalizeProfile(fields, characterClass, FemaleOptions, FemaleEligibility, FemaleRequirements);
        return fields;
    }

    constexpr std::array<std::uint8_t, 13> Fields(std::uint8_t skin, std::uint8_t face,
        std::uint8_t hairStyle, std::uint8_t hairColor, std::uint8_t facialStyle, std::uint64_t extra)
    {
        std::array<std::uint8_t, 13> fields = {skin, face, hairStyle, hairColor, facialStyle};
        for (unsigned i = 0; i < 8; ++i)
            fields[5 + i] = static_cast<std::uint8_t>(extra >> (i * 8));
        return fields;
    }

    constexpr std::uint64_t Extra(std::array<std::uint8_t, 13> const& fields)
    {
        std::uint64_t extra = 0;
        for (unsigned i = 0; i < 8; ++i)
            extra |= std::uint64_t{fields[5 + i]} << (i * 8);
        return extra;
    }
}

#endif
