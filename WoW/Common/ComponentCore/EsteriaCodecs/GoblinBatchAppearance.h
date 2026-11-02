// Generated from the pinned Retail option graph by generate_batch_codecs.py.
#ifndef ESTERIA_GOBLIN_BATCH_APPEARANCE_H
#define ESTERIA_GOBLIN_BATCH_APPEARANCE_H

#include <array>
#include <cstdint>

namespace GoblinBatchAppearance
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
    constexpr bool IsModern(std::array<std::uint8_t, 13> const& fields) { return fields[12] == 0xD3; }
    constexpr void Mark(std::array<std::uint8_t, 13>& fields) { fields[12] = 0xD3; }

    inline constexpr std::array<Option, 12> MaleOptions = {{
        {10, 10, 1}, // Face
        {7, 15, 1}, // Skin Color
        {5, 22, 1}, // Hair Style
        {6, 18, 1}, // Hair Color
        {8, 11, 1}, // Eye Color
        {8, 6, 11}, // Chin
        {11, 10, 1}, // Ears
        {10, 9, 10}, // Earrings
        {9, 11, 1}, // Nose
        {9, 5, 11}, // Nose Ring
        {11, 8, 10}, // Jewelry Color
        {7, 4, 15}, // Eyesight
    }};

    inline constexpr std::array<char const*, 12> MaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Eye Color",
        "Chin",
        "Ears",
        "Earrings",
        "Nose",
        "Nose Ring",
        "Jewelry Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> MaleCapacities = {256, 256, 256, 256, 256, 22, 18, 60, 66, 55, 90, 80, 256};

    inline constexpr std::array<Requirement, 13> MaleRequirements = {{
        {1, 9, 0, 224},
        {1, 10, 0, 224},
        {1, 11, 0, 224},
        {7, 1, 6, 2},
        {7, 2, 6, 767},
        {7, 3, 6, 8},
        {7, 4, 6, 551},
        {7, 5, 6, 1},
        {7, 8, 6, 751},
        {9, 1, 8, 2046},
        {9, 2, 8, 1022},
        {9, 3, 8, 2046},
        {9, 4, 8, 2046},
    }};

    inline constexpr std::array<Eligibility, 129> MaleEligibility = {{
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
        {1, 9, 32u},
        {1, 10, 32u},
        {1, 11, 32u},
        {1, 12, 4294967295u},
        {1, 13, 4294967295u},
        {1, 14, 4294967295u},
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
        {2, 20, 4294967295u},
        {2, 21, 4294967295u},
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
        {3, 11, 4294967295u},
        {3, 12, 4294967295u},
        {3, 13, 4294967295u},
        {3, 14, 4294967295u},
        {3, 15, 4294967295u},
        {3, 16, 4294967295u},
        {3, 17, 4294967295u},
        {4, 0, 32735u},
        {4, 1, 32735u},
        {4, 2, 32735u},
        {4, 3, 32735u},
        {4, 4, 32735u},
        {4, 5, 32735u},
        {4, 6, 32735u},
        {4, 7, 32735u},
        {4, 8, 32735u},
        {4, 9, 32735u},
        {4, 10, 32u},
        {5, 0, 4294967295u},
        {5, 1, 4294967295u},
        {5, 2, 4294967295u},
        {5, 3, 4294967295u},
        {5, 4, 4294967295u},
        {5, 5, 4294967295u},
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
        {7, 0, 4294967295u},
        {7, 1, 4294967295u},
        {7, 2, 4294967295u},
        {7, 3, 4294967295u},
        {7, 4, 4294967295u},
        {7, 5, 4294967295u},
        {7, 6, 4294967295u},
        {7, 7, 4294967295u},
        {7, 8, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
        {8, 2, 4294967295u},
        {8, 3, 4294967295u},
        {8, 4, 4294967295u},
        {8, 5, 4294967295u},
        {8, 6, 4294967295u},
        {8, 7, 4294967295u},
        {8, 8, 4294967295u},
        {8, 9, 4294967295u},
        {8, 10, 4294967295u},
        {9, 0, 4294967295u},
        {9, 1, 4294967295u},
        {9, 2, 4294967295u},
        {9, 3, 4294967295u},
        {9, 4, 4294967295u},
        {10, 0, 4294967295u},
        {10, 1, 4294967295u},
        {10, 2, 4294967295u},
        {10, 3, 4294967295u},
        {10, 4, 4294967295u},
        {10, 5, 4294967295u},
        {10, 6, 4294967295u},
        {10, 7, 4294967295u},
        {11, 0, 4294967295u},
        {11, 1, 30687u},
        {11, 2, 30687u},
        {11, 3, 30687u},
    }};

    inline constexpr std::array<Option, 12> FemaleOptions = {{
        {6, 20, 1}, // Face
        {8, 15, 1}, // Skin Color
        {5, 21, 1}, // Hair Style
        {7, 18, 1}, // Hair Color
        {10, 11, 1}, // Eye Color
        {9, 6, 13}, // Chin
        {8, 6, 15}, // Ears
        {9, 13, 1}, // Earrings
        {11, 9, 1}, // Nose
        {10, 7, 11}, // Nose Ring
        {11, 8, 9}, // Jewelry Color
        {7, 4, 18}, // Eyesight
    }};

    inline constexpr std::array<char const*, 12> FemaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Eye Color",
        "Chin",
        "Ears",
        "Earrings",
        "Nose",
        "Nose Ring",
        "Jewelry Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> FemaleCapacities = {256, 256, 256, 256, 256, 21, 20, 72, 90, 78, 77, 72, 256};

    inline constexpr std::array<Requirement, 20> FemaleRequirements = {{
        {1, 9, 0, 590848},
        {1, 10, 0, 590848},
        {1, 11, 0, 590848},
        {7, 1, 6, 1},
        {7, 2, 6, 7},
        {7, 3, 6, 7},
        {7, 4, 6, 39},
        {7, 5, 6, 2},
        {7, 6, 6, 6},
        {7, 7, 6, 7},
        {7, 8, 6, 1},
        {7, 9, 6, 6},
        {7, 10, 6, 8},
        {7, 12, 6, 32},
        {9, 1, 8, 510},
        {9, 2, 8, 510},
        {9, 3, 8, 454},
        {9, 4, 8, 8},
        {9, 5, 8, 8},
        {9, 6, 8, 8},
    }};

    inline constexpr std::array<Eligibility, 138> FemaleEligibility = {{
        {0, 0, 32735u},
        {0, 1, 32735u},
        {0, 2, 32735u},
        {0, 3, 32735u},
        {0, 4, 32735u},
        {0, 5, 32735u},
        {0, 6, 32735u},
        {0, 7, 32735u},
        {0, 8, 32735u},
        {0, 9, 32735u},
        {0, 10, 32u},
        {0, 11, 32u},
        {0, 12, 32u},
        {0, 13, 32u},
        {0, 14, 32u},
        {0, 15, 32u},
        {0, 16, 32u},
        {0, 17, 32u},
        {0, 18, 32u},
        {0, 19, 32u},
        {1, 0, 4294967295u},
        {1, 1, 4294967295u},
        {1, 2, 4294967295u},
        {1, 3, 4294967295u},
        {1, 4, 4294967295u},
        {1, 5, 4294967295u},
        {1, 6, 4294967295u},
        {1, 7, 4294967295u},
        {1, 8, 4294967295u},
        {1, 9, 32u},
        {1, 10, 32u},
        {1, 11, 32u},
        {1, 12, 4294967295u},
        {1, 13, 4294967295u},
        {1, 14, 4294967295u},
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
        {2, 20, 4294967295u},
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
        {3, 11, 4294967295u},
        {3, 12, 4294967295u},
        {3, 13, 4294967295u},
        {3, 14, 4294967295u},
        {3, 15, 4294967295u},
        {3, 16, 4294967295u},
        {3, 17, 4294967295u},
        {4, 0, 32735u},
        {4, 1, 32735u},
        {4, 2, 32735u},
        {4, 3, 32735u},
        {4, 4, 32735u},
        {4, 5, 32735u},
        {4, 6, 32735u},
        {4, 7, 32735u},
        {4, 8, 32735u},
        {4, 9, 32735u},
        {4, 10, 32u},
        {5, 0, 4294967295u},
        {5, 1, 4294967295u},
        {5, 2, 4294967295u},
        {5, 3, 4294967295u},
        {5, 4, 4294967295u},
        {5, 5, 4294967295u},
        {6, 0, 4294967295u},
        {6, 1, 4294967295u},
        {6, 2, 4294967295u},
        {6, 3, 4294967295u},
        {6, 4, 4294967295u},
        {6, 5, 4294967295u},
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
        {7, 10, 4294967295u},
        {7, 11, 4294967295u},
        {7, 12, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
        {8, 2, 4294967295u},
        {8, 3, 4294967295u},
        {8, 4, 4294967295u},
        {8, 5, 4294967295u},
        {8, 6, 4294967295u},
        {8, 7, 4294967295u},
        {8, 8, 4294967295u},
        {9, 0, 4294967295u},
        {9, 1, 4294967295u},
        {9, 2, 4294967295u},
        {9, 3, 4294967295u},
        {9, 4, 4294967295u},
        {9, 5, 4294967295u},
        {9, 6, 4294967295u},
        {10, 0, 4294967295u},
        {10, 1, 4294967295u},
        {10, 2, 4294967295u},
        {10, 3, 4294967295u},
        {10, 4, 4294967295u},
        {10, 5, 4294967295u},
        {10, 6, 4294967295u},
        {10, 7, 4294967295u},
        {11, 0, 4294967295u},
        {11, 1, 30687u},
        {11, 2, 30687u},
        {11, 3, 30687u},
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
