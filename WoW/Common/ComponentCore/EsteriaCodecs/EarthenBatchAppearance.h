// Generated from the pinned Retail option graph by generate_batch_codecs.py.
#ifndef ESTERIA_EARTHEN_BATCH_APPEARANCE_H
#define ESTERIA_EARTHEN_BATCH_APPEARANCE_H

#include <array>
#include <cstdint>

namespace EarthenBatchAppearance
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
    constexpr bool IsModern(std::array<std::uint8_t, 13> const& fields) { return fields[12] == 0xD9; }
    constexpr void Mark(std::array<std::uint8_t, 13>& fields) { fields[12] = 0xD9; }

    inline constexpr std::array<Option, 18> MaleOptions = {{
        {5, 14, 1}, // Skin Color
        {7, 2, 42}, // Belt
        {9, 11, 1}, // Beard Style
        {10, 11, 1}, // Hair Style
        {11, 10, 1}, // Face
        {6, 14, 1}, // Gem Color
        {7, 14, 1}, // Hair Color
        {8, 14, 1}, // Eye Color
        {6, 4, 14}, // Eyesight
        {11, 1, 50}, // Horn Style
        {11, 1, 50}, // Hand FX
        {8, 2, 42}, // Eyebrows
        {11, 5, 10}, // Right Shoulder
        {9, 5, 11}, // Left Shoulder
        {7, 3, 14}, // Torso
        {10, 5, 11}, // Arms
        {8, 3, 14}, // Legs
        {5, 5, 14}, // Hands
    }};

    inline constexpr std::array<char const*, 18> MaleLabels = {
        "Skin Color",
        "Belt",
        "Beard Style",
        "Hair Style",
        "Face",
        "Gem Color",
        "Hair Color",
        "Eye Color",
        "Eyesight",
        "Horn Style",
        "Hand FX",
        "Eyebrows",
        "Right Shoulder",
        "Left Shoulder",
        "Torso",
        "Arms",
        "Legs",
        "Hands",
    };

    inline constexpr std::array<std::uint16_t, 13> MaleCapacities = {256, 256, 256, 256, 256, 70, 56, 84, 84, 55, 55, 50, 256};

    inline constexpr std::array<Requirement, 0> MaleRequirements = {{
    }};

    inline constexpr std::array<Eligibility, 124> MaleEligibility = {{
        {0, 0, 4294967295u},
        {0, 1, 4294967295u},
        {0, 2, 4294967295u},
        {0, 3, 4294967295u},
        {0, 4, 4294967295u},
        {0, 5, 4294967295u},
        {0, 6, 4294967295u},
        {0, 7, 4294967295u},
        {0, 8, 4294967295u},
        {0, 9, 4294967295u},
        {0, 10, 4294967295u},
        {0, 11, 4294967295u},
        {0, 12, 4294967295u},
        {0, 13, 4294967295u},
        {1, 0, 4294967295u},
        {1, 1, 4294967295u},
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
        {3, 0, 4294967295u},
        {3, 1, 4294967295u},
        {3, 2, 4294967295u},
        {3, 3, 4294967295u},
        {3, 4, 4294967295u},
        {3, 5, 4294967295u},
        {3, 6, 4294967295u},
        {3, 7, 4294967295u},
        {3, 8, 4294967295u},
        {3, 9, 4294967295u},
        {3, 10, 4294967295u},
        {4, 0, 4294967295u},
        {4, 1, 4294967295u},
        {4, 2, 4294967295u},
        {4, 3, 4294967295u},
        {4, 4, 4294967295u},
        {4, 5, 4294967295u},
        {4, 6, 4294967295u},
        {4, 7, 4294967295u},
        {4, 8, 4294967295u},
        {4, 9, 4294967295u},
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
        {6, 13, 4294967295u},
        {7, 0, 32735u},
        {7, 1, 32735u},
        {7, 2, 32735u},
        {7, 3, 32735u},
        {7, 4, 32735u},
        {7, 5, 32735u},
        {7, 6, 32735u},
        {7, 7, 32735u},
        {7, 8, 32735u},
        {7, 9, 32735u},
        {7, 10, 32735u},
        {7, 11, 32735u},
        {7, 12, 32735u},
        {7, 13, 32735u},
        {8, 0, 4294967295u},
        {8, 1, 30687u},
        {8, 2, 30687u},
        {8, 3, 30687u},
        {9, 0, 4294967295u},
        {10, 0, 4294967295u},
        {11, 0, 4294967295u},
        {11, 1, 4294967295u},
        {12, 0, 4294967295u},
        {12, 1, 4294967295u},
        {12, 2, 4294967295u},
        {12, 3, 4294967295u},
        {12, 4, 4294967295u},
        {13, 0, 4294967295u},
        {13, 1, 4294967295u},
        {13, 2, 4294967295u},
        {13, 3, 4294967295u},
        {13, 4, 4294967295u},
        {14, 0, 4294967295u},
        {14, 1, 4294967295u},
        {14, 2, 4294967295u},
        {15, 0, 4294967295u},
        {15, 1, 4294967295u},
        {15, 2, 4294967295u},
        {15, 3, 4294967295u},
        {15, 4, 4294967295u},
        {16, 0, 4294967295u},
        {16, 1, 4294967295u},
        {16, 2, 4294967295u},
        {17, 0, 4294967295u},
        {17, 1, 4294967295u},
        {17, 2, 4294967295u},
        {17, 3, 4294967295u},
        {17, 4, 4294967295u},
    }};

    inline constexpr std::array<Option, 18> FemaleOptions = {{
        {5, 14, 1}, // Skin Color
        {9, 11, 1}, // Hair Style
        {10, 11, 1}, // Beard Style
        {10, 4, 11}, // Eyesight
        {6, 14, 1}, // Hair Color
        {7, 14, 1}, // Gem Color
        {8, 14, 1}, // Eye Color
        {11, 10, 1}, // Face
        {7, 2, 42}, // Belt
        {10, 1, 44}, // Horn Style
        {10, 1, 44}, // Hand FX
        {8, 2, 42}, // Eyebrows
        {11, 5, 10}, // Right Shoulder
        {9, 5, 11}, // Left Shoulder
        {7, 3, 14}, // Torso
        {5, 4, 14}, // Arms
        {6, 4, 14}, // Hands
        {8, 3, 14}, // Legs
    }};

    inline constexpr std::array<char const*, 18> FemaleLabels = {
        "Skin Color",
        "Hair Style",
        "Beard Style",
        "Eyesight",
        "Hair Color",
        "Gem Color",
        "Eye Color",
        "Face",
        "Belt",
        "Horn Style",
        "Hand FX",
        "Eyebrows",
        "Right Shoulder",
        "Left Shoulder",
        "Torso",
        "Arms",
        "Hands",
        "Legs",
    };

    inline constexpr std::array<std::uint16_t, 13> FemaleCapacities = {256, 256, 256, 256, 256, 56, 56, 84, 84, 55, 44, 50, 256};

    inline constexpr std::array<Requirement, 0> FemaleRequirements = {{
    }};

    inline constexpr std::array<Eligibility, 122> FemaleEligibility = {{
        {0, 0, 4294967295u},
        {0, 1, 4294967295u},
        {0, 2, 4294967295u},
        {0, 3, 4294967295u},
        {0, 4, 4294967295u},
        {0, 5, 4294967295u},
        {0, 6, 4294967295u},
        {0, 7, 4294967295u},
        {0, 8, 4294967295u},
        {0, 9, 4294967295u},
        {0, 10, 4294967295u},
        {0, 11, 4294967295u},
        {0, 12, 4294967295u},
        {0, 13, 4294967295u},
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
        {3, 0, 4294967295u},
        {3, 1, 30687u},
        {3, 2, 30687u},
        {3, 3, 30687u},
        {4, 0, 4294967295u},
        {4, 1, 4294967295u},
        {4, 2, 4294967295u},
        {4, 3, 4294967295u},
        {4, 4, 4294967295u},
        {4, 5, 4294967295u},
        {4, 6, 4294967295u},
        {4, 7, 4294967295u},
        {4, 8, 4294967295u},
        {4, 9, 4294967295u},
        {4, 10, 4294967295u},
        {4, 11, 4294967295u},
        {4, 12, 4294967295u},
        {4, 13, 4294967295u},
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
        {6, 0, 32735u},
        {6, 1, 32735u},
        {6, 2, 32735u},
        {6, 3, 32735u},
        {6, 4, 32735u},
        {6, 5, 32735u},
        {6, 6, 32735u},
        {6, 7, 32735u},
        {6, 8, 32735u},
        {6, 9, 32735u},
        {6, 10, 32735u},
        {6, 11, 32735u},
        {6, 12, 32735u},
        {6, 13, 32735u},
        {7, 0, 4294967295u},
        {7, 1, 4294967295u},
        {7, 2, 4294967295u},
        {7, 3, 4294967295u},
        {7, 4, 4294967295u},
        {7, 5, 4294967295u},
        {7, 6, 4294967295u},
        {7, 7, 4294967295u},
        {7, 8, 4294967295u},
        {7, 9, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
        {9, 0, 4294967295u},
        {10, 0, 4294967295u},
        {11, 0, 4294967295u},
        {11, 1, 4294967295u},
        {12, 0, 4294967295u},
        {12, 1, 4294967295u},
        {12, 2, 4294967295u},
        {12, 3, 4294967295u},
        {12, 4, 4294967295u},
        {13, 0, 4294967295u},
        {13, 1, 4294967295u},
        {13, 2, 4294967295u},
        {13, 3, 4294967295u},
        {13, 4, 4294967295u},
        {14, 0, 4294967295u},
        {14, 1, 4294967295u},
        {14, 2, 4294967295u},
        {15, 0, 4294967295u},
        {15, 1, 4294967295u},
        {15, 2, 4294967295u},
        {15, 3, 4294967295u},
        {16, 0, 4294967295u},
        {16, 1, 4294967295u},
        {16, 2, 4294967295u},
        {16, 3, 4294967295u},
        {17, 0, 4294967295u},
        {17, 1, 4294967295u},
        {17, 2, 4294967295u},
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
