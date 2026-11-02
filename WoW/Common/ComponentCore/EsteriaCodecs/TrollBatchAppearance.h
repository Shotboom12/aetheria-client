// Generated from the pinned Retail option graph by generate_batch_codecs.py.
#ifndef ESTERIA_TROLL_BATCH_APPEARANCE_H
#define ESTERIA_TROLL_BATCH_APPEARANCE_H

#include <array>
#include <cstdint>

namespace TrollBatchAppearance
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
    constexpr bool IsModern(std::array<std::uint8_t, 13> const& fields) { return fields[12] == 0xD2; }
    constexpr void Mark(std::array<std::uint8_t, 13>& fields) { fields[12] = 0xD2; }

    inline constexpr std::array<Option, 13> MaleOptions = {{
        {10, 10, 1}, // Face
        {8, 12, 1}, // Skin Color
        {7, 18, 1}, // Hair Style
        {5, 25, 1}, // Hair Color
        {11, 10, 1}, // Tusks
        {9, 11, 1}, // Eye Color
        {10, 10, 10}, // Face Paint
        {6, 20, 1}, // Face Paint Color
        {9, 6, 11}, // Earrings
        {6, 3, 20}, // Piercing
        {8, 4, 12}, // Body Paint
        {11, 7, 10}, // Body Paint Color
        {7, 4, 18}, // Eyesight
    }};

    inline constexpr std::array<char const*, 13> MaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Tusks",
        "Eye Color",
        "Face Paint",
        "Face Paint Color",
        "Earrings",
        "Piercing",
        "Body Paint",
        "Body Paint Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> MaleCapacities = {256, 256, 256, 256, 256, 25, 60, 72, 48, 66, 100, 70, 256};

    inline constexpr std::array<Requirement, 30> MaleRequirements = {{
        {1, 6, 0, 352},
        {1, 7, 0, 352},
        {1, 8, 0, 352},
        {7, 0, 6, 1},
        {7, 1, 6, 126},
        {7, 2, 6, 126},
        {7, 3, 6, 126},
        {7, 4, 6, 126},
        {7, 5, 6, 126},
        {7, 6, 6, 126},
        {7, 7, 6, 126},
        {7, 8, 6, 126},
        {7, 9, 6, 126},
        {7, 10, 6, 126},
        {7, 11, 6, 126},
        {7, 12, 6, 126},
        {7, 13, 6, 126},
        {7, 14, 6, 896},
        {7, 15, 6, 896},
        {7, 16, 6, 896},
        {7, 17, 6, 896},
        {7, 18, 6, 896},
        {7, 19, 6, 896},
        {11, 0, 10, 1},
        {11, 1, 10, 14},
        {11, 2, 10, 14},
        {11, 3, 10, 14},
        {11, 4, 10, 14},
        {11, 5, 10, 14},
        {11, 6, 10, 14},
    }};

    inline constexpr std::array<Eligibility, 140> MaleEligibility = {{
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
        {1, 6, 32u},
        {1, 7, 32u},
        {1, 8, 32u},
        {1, 9, 4294967295u},
        {1, 10, 4294967295u},
        {1, 11, 4294967295u},
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
        {3, 18, 4294967295u},
        {3, 19, 4294967295u},
        {3, 20, 4294967295u},
        {3, 21, 4294967295u},
        {3, 22, 4294967295u},
        {3, 23, 4294967295u},
        {3, 24, 4294967295u},
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
        {5, 0, 32735u},
        {5, 1, 32735u},
        {5, 2, 32735u},
        {5, 3, 32735u},
        {5, 4, 32735u},
        {5, 5, 32735u},
        {5, 6, 32735u},
        {5, 7, 32735u},
        {5, 8, 32735u},
        {5, 9, 32735u},
        {5, 10, 32u},
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
        {7, 9, 4294967295u},
        {7, 10, 4294967295u},
        {7, 11, 4294959136u},
        {7, 12, 4294959136u},
        {7, 13, 4294959136u},
        {7, 14, 4294967295u},
        {7, 15, 4294967295u},
        {7, 16, 4294967295u},
        {7, 17, 4294967295u},
        {7, 18, 4294967295u},
        {7, 19, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
        {8, 2, 4294967295u},
        {8, 3, 4294967295u},
        {8, 4, 4294967295u},
        {8, 5, 4294967295u},
        {9, 0, 4294967295u},
        {9, 1, 4294967295u},
        {9, 2, 4294967295u},
        {10, 0, 4294967295u},
        {10, 1, 4294967295u},
        {10, 2, 4294967295u},
        {10, 3, 4294967295u},
        {11, 0, 4294967295u},
        {11, 1, 4294967295u},
        {11, 2, 4294967295u},
        {11, 3, 4294967295u},
        {11, 4, 4294967295u},
        {11, 5, 4294967295u},
        {11, 6, 4294967295u},
        {12, 0, 4294967295u},
        {12, 1, 30687u},
        {12, 2, 30687u},
        {12, 3, 30687u},
    }};

    inline constexpr std::array<Option, 13> FemaleOptions = {{
        {7, 12, 1}, // Face
        {8, 12, 1}, // Skin Color
        {6, 17, 1}, // Hair Style
        {5, 25, 1}, // Hair Color
        {10, 6, 8}, // Tusks
        {9, 11, 1}, // Eye Color
        {9, 4, 11}, // Face Paint
        {11, 7, 1}, // Face Paint Color
        {10, 8, 1}, // Earrings
        {6, 3, 17}, // Piercing
        {7, 4, 12}, // Body Paint
        {11, 7, 7}, // Body Paint Color
        {8, 4, 12}, // Eyesight
    }};

    inline constexpr std::array<char const*, 13> FemaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Tusks",
        "Eye Color",
        "Face Paint",
        "Face Paint Color",
        "Earrings",
        "Piercing",
        "Body Paint",
        "Body Paint Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> FemaleCapacities = {256, 256, 256, 256, 256, 25, 51, 48, 48, 44, 48, 49, 256};

    inline constexpr std::array<Requirement, 17> FemaleRequirements = {{
        {1, 6, 0, 2624},
        {1, 7, 0, 2624},
        {1, 8, 0, 2624},
        {7, 0, 6, 1},
        {7, 1, 6, 14},
        {7, 2, 6, 14},
        {7, 3, 6, 14},
        {7, 4, 6, 14},
        {7, 5, 6, 14},
        {7, 6, 6, 14},
        {11, 0, 10, 1},
        {11, 1, 10, 14},
        {11, 2, 10, 14},
        {11, 3, 10, 14},
        {11, 4, 10, 14},
        {11, 5, 10, 14},
        {11, 6, 10, 14},
    }};

    inline constexpr std::array<Eligibility, 120> FemaleEligibility = {{
        {0, 0, 32735u},
        {0, 1, 32735u},
        {0, 2, 32735u},
        {0, 3, 32735u},
        {0, 4, 32735u},
        {0, 5, 32735u},
        {0, 6, 32u},
        {0, 7, 32u},
        {0, 8, 32u},
        {0, 9, 32u},
        {0, 10, 32u},
        {0, 11, 32u},
        {1, 0, 4294967295u},
        {1, 1, 4294967295u},
        {1, 2, 4294967295u},
        {1, 3, 4294967295u},
        {1, 4, 4294967295u},
        {1, 5, 4294967295u},
        {1, 6, 32u},
        {1, 7, 32u},
        {1, 8, 32u},
        {1, 9, 4294967295u},
        {1, 10, 4294967295u},
        {1, 11, 4294967295u},
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
        {3, 18, 4294967295u},
        {3, 19, 4294967295u},
        {3, 20, 4294967295u},
        {3, 21, 4294967295u},
        {3, 22, 4294967295u},
        {3, 23, 4294967295u},
        {3, 24, 4294967295u},
        {4, 0, 4294967295u},
        {4, 1, 4294967295u},
        {4, 2, 4294967295u},
        {4, 3, 4294967295u},
        {4, 4, 4294967295u},
        {4, 5, 4294967295u},
        {5, 0, 32735u},
        {5, 1, 32735u},
        {5, 2, 32735u},
        {5, 3, 32735u},
        {5, 4, 32735u},
        {5, 5, 32735u},
        {5, 6, 32735u},
        {5, 7, 32735u},
        {5, 8, 32735u},
        {5, 9, 32735u},
        {5, 10, 32u},
        {6, 0, 4294967295u},
        {6, 1, 4294967295u},
        {6, 2, 4294967295u},
        {6, 3, 4294967295u},
        {7, 0, 4294967295u},
        {7, 1, 4294967295u},
        {7, 2, 4294967295u},
        {7, 3, 4294967295u},
        {7, 4, 4294967295u},
        {7, 5, 4294967295u},
        {7, 6, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
        {8, 2, 4294967295u},
        {8, 3, 4294967295u},
        {8, 4, 4294967295u},
        {8, 5, 4294967295u},
        {8, 6, 4294967295u},
        {8, 7, 4294967295u},
        {9, 0, 4294967295u},
        {9, 1, 4294967295u},
        {9, 2, 4294967295u},
        {10, 0, 4294967295u},
        {10, 1, 4294967295u},
        {10, 2, 4294967295u},
        {10, 3, 4294967295u},
        {11, 0, 4294967295u},
        {11, 1, 4294967295u},
        {11, 2, 4294967295u},
        {11, 3, 4294967295u},
        {11, 4, 4294967295u},
        {11, 5, 4294967295u},
        {11, 6, 4294967295u},
        {12, 0, 4294967295u},
        {12, 1, 30687u},
        {12, 2, 30687u},
        {12, 3, 30687u},
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
