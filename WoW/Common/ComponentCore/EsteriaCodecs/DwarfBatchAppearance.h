// Generated from the pinned Retail option graph by generate_batch_codecs.py.
#ifndef ESTERIA_DWARF_BATCH_APPEARANCE_H
#define ESTERIA_DWARF_BATCH_APPEARANCE_H

#include <array>
#include <cstdint>

namespace DwarfBatchAppearance
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
    constexpr bool IsModern(std::array<std::uint8_t, 13> const& fields) { return fields[12] == 0xDB; }
    constexpr void Mark(std::array<std::uint8_t, 13>& fields) { fields[12] = 0xDB; }

    inline constexpr std::array<Option, 13> MaleOptions = {{
        {6, 20, 1}, // Face
        {9, 15, 1}, // Skin Color
        {5, 22, 1}, // Hair Style
        {7, 20, 1}, // Hair Color
        {11, 10, 1}, // Mustache
        {10, 11, 1}, // Beard
        {8, 20, 1}, // Eye Color
        {11, 8, 10}, // Earrings
        {9, 6, 15}, // Nose Ring
        {8, 1, 20}, // Eyebrows
        {10, 8, 11}, // Tattoo
        {6, 4, 20}, // Tattoo Color
        {7, 4, 20}, // Eyesight
    }};

    inline constexpr std::array<char const*, 13> MaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Mustache",
        "Beard",
        "Eye Color",
        "Earrings",
        "Nose Ring",
        "Eyebrows",
        "Tattoo",
        "Tattoo Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> MaleCapacities = {256, 256, 256, 256, 256, 22, 80, 80, 20, 90, 88, 80, 256};

    inline constexpr std::array<Requirement, 3> MaleRequirements = {{
        {1, 9, 0, 527360},
        {1, 10, 0, 527360},
        {1, 11, 0, 527360},
    }};

    inline constexpr std::array<Eligibility, 149> MaleEligibility = {{
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
        {3, 18, 4294967295u},
        {3, 19, 4294967295u},
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
        {6, 14, 32735u},
        {6, 15, 32735u},
        {6, 16, 32735u},
        {6, 17, 32735u},
        {6, 18, 32735u},
        {6, 19, 32u},
        {7, 0, 4294967295u},
        {7, 1, 4294967295u},
        {7, 2, 4294967295u},
        {7, 3, 4294967295u},
        {7, 4, 4294967295u},
        {7, 5, 4294967295u},
        {7, 6, 4294967295u},
        {7, 7, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
        {8, 2, 4294967295u},
        {8, 3, 4294967295u},
        {8, 4, 4294967295u},
        {8, 5, 4294967295u},
        {9, 0, 4294967295u},
        {10, 0, 4294967295u},
        {10, 1, 4294967295u},
        {10, 2, 4294967295u},
        {10, 3, 4294967295u},
        {10, 4, 4294967295u},
        {10, 5, 4294967295u},
        {10, 6, 4294967295u},
        {10, 7, 4294967295u},
        {11, 0, 4294967295u},
        {11, 1, 4294967295u},
        {11, 2, 4294967295u},
        {11, 3, 4294967295u},
        {12, 0, 4294967295u},
        {12, 1, 30687u},
        {12, 2, 30687u},
        {12, 3, 30687u},
    }};

    inline constexpr std::array<Option, 10> FemaleOptions = {{
        {6, 20, 1}, // Face
        {9, 15, 1}, // Skin Color
        {5, 26, 1}, // Hair Style
        {7, 20, 1}, // Hair Color
        {8, 20, 1}, // Eye Color
        {10, 8, 1}, // Earrings
        {10, 6, 8}, // Piercings
        {11, 8, 1}, // Tattoo
        {11, 4, 8}, // Tattoo Color
        {9, 4, 15}, // Eyesight
    }};

    inline constexpr std::array<char const*, 10> FemaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Eye Color",
        "Earrings",
        "Piercings",
        "Tattoo",
        "Tattoo Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> FemaleCapacities = {256, 256, 256, 256, 256, 26, 20, 20, 20, 60, 48, 32, 256};

    inline constexpr std::array<Requirement, 10> FemaleRequirements = {{
        {1, 9, 0, 99328},
        {1, 10, 0, 99328},
        {1, 11, 0, 99328},
        {5, 1, 2, 67108861},
        {5, 2, 2, 67108861},
        {5, 3, 2, 67108861},
        {5, 4, 2, 67108861},
        {5, 5, 2, 67108861},
        {5, 6, 2, 67108861},
        {5, 7, 2, 61235321},
    }};

    inline constexpr std::array<Eligibility, 131> FemaleEligibility = {{
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
        {2, 21, 4294967295u},
        {2, 22, 4294967295u},
        {2, 23, 4294967295u},
        {2, 24, 4294967295u},
        {2, 25, 4294967295u},
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
        {4, 10, 32735u},
        {4, 11, 32735u},
        {4, 12, 32735u},
        {4, 13, 32735u},
        {4, 14, 32735u},
        {4, 15, 32735u},
        {4, 16, 32735u},
        {4, 17, 32735u},
        {4, 18, 32735u},
        {4, 19, 32u},
        {5, 0, 4294967295u},
        {5, 1, 4294967295u},
        {5, 2, 4294967295u},
        {5, 3, 4294967295u},
        {5, 4, 4294967295u},
        {5, 5, 4294967295u},
        {5, 6, 4294967295u},
        {5, 7, 4294967295u},
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
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
        {8, 2, 4294967295u},
        {8, 3, 4294967295u},
        {9, 0, 4294967295u},
        {9, 1, 30687u},
        {9, 2, 30687u},
        {9, 3, 30687u},
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
