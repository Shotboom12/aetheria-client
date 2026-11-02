// Generated from the pinned Retail option graph by generate_batch_codecs.py.
#ifndef ESTERIA_UNDEAD_BATCH_APPEARANCE_H
#define ESTERIA_UNDEAD_BATCH_APPEARANCE_H

#include <array>
#include <cstdint>

namespace UndeadBatchAppearance
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
    constexpr bool IsModern(std::array<std::uint8_t, 13> const& fields) { return fields[12] == 0xD1; }
    constexpr void Mark(std::array<std::uint8_t, 13>& fields) { fields[12] = 0xD1; }

    inline constexpr std::array<Option, 9> MaleOptions = {{
        {8, 11, 1}, // Face
        {10, 3, 8}, // Skin Type
        {5, 48, 1}, // Skin Color
        {6, 21, 1}, // Hair Style
        {7, 17, 1}, // Hair Color
        {9, 11, 1}, // Jaw Features
        {11, 5, 1}, // Face Features
        {10, 8, 1}, // Eye Color
        {11, 4, 5}, // Eyesight
    }};

    inline constexpr std::array<char const*, 9> MaleLabels = {
        "Face",
        "Skin Type",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Jaw Features",
        "Face Features",
        "Eye Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> MaleCapacities = {256, 256, 256, 256, 256, 48, 21, 17, 11, 11, 24, 20, 256};

    inline constexpr std::array<Requirement, 50> MaleRequirements = {{
        {2, 0, 1, 1},
        {2, 1, 1, 1},
        {2, 2, 1, 1},
        {2, 3, 1, 1},
        {2, 4, 1, 1},
        {2, 5, 1, 1},
        {2, 6, 1, 1},
        {2, 7, 1, 1},
        {2, 8, 1, 1},
        {2, 9, 1, 1},
        {2, 10, 1, 1},
        {2, 11, 1, 4},
        {2, 12, 1, 4},
        {2, 13, 1, 4},
        {2, 14, 1, 4},
        {2, 15, 1, 4},
        {2, 16, 1, 4},
        {2, 17, 1, 4},
        {2, 18, 1, 4},
        {2, 19, 1, 4},
        {2, 20, 1, 4},
        {2, 21, 1, 4},
        {2, 22, 1, 2},
        {2, 23, 1, 2},
        {2, 24, 1, 2},
        {2, 25, 1, 2},
        {2, 26, 1, 2},
        {2, 27, 1, 2},
        {2, 28, 1, 2},
        {2, 29, 1, 2},
        {2, 30, 1, 2},
        {2, 31, 1, 2},
        {2, 32, 1, 2},
        {2, 33, 1, 1},
        {2, 34, 1, 1},
        {2, 35, 1, 1},
        {2, 36, 1, 1},
        {2, 37, 1, 1},
        {2, 38, 1, 4},
        {2, 39, 1, 4},
        {2, 40, 1, 4},
        {2, 41, 1, 4},
        {2, 42, 1, 4},
        {2, 43, 1, 2},
        {2, 44, 1, 2},
        {2, 45, 1, 2},
        {2, 46, 1, 2},
        {2, 47, 1, 2},
        {6, 2, 5, 675},
        {6, 3, 5, 1372},
    }};

    inline constexpr std::array<Eligibility, 128> MaleEligibility = {{
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
        {1, 0, 4294967295u},
        {1, 1, 4294967295u},
        {1, 2, 4294967295u},
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
        {2, 26, 4294967295u},
        {2, 27, 4294967295u},
        {2, 28, 4294967295u},
        {2, 29, 4294967295u},
        {2, 30, 4294967295u},
        {2, 31, 4294967295u},
        {2, 32, 4294967295u},
        {2, 33, 4294967295u},
        {2, 34, 4294967295u},
        {2, 35, 4294967295u},
        {2, 36, 4294967295u},
        {2, 37, 4294967295u},
        {2, 38, 4294967295u},
        {2, 39, 4294967295u},
        {2, 40, 4294967295u},
        {2, 41, 4294967295u},
        {2, 42, 4294967295u},
        {2, 43, 4294967295u},
        {2, 44, 4294967295u},
        {2, 45, 4294967295u},
        {2, 46, 4294967295u},
        {2, 47, 4294967295u},
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
        {4, 14, 4294967295u},
        {4, 15, 4294967295u},
        {4, 16, 4294967295u},
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
        {7, 0, 32735u},
        {7, 1, 32735u},
        {7, 2, 32735u},
        {7, 3, 32735u},
        {7, 4, 32735u},
        {7, 5, 32735u},
        {7, 6, 32u},
        {7, 7, 32735u},
        {8, 0, 4294967295u},
        {8, 1, 30687u},
        {8, 2, 30687u},
        {8, 3, 30687u},
    }};

    inline constexpr std::array<Option, 9> FemaleOptions = {{
        {8, 10, 1}, // Face
        {10, 3, 5}, // Skin Type
        {5, 48, 1}, // Skin Color
        {6, 19, 1}, // Hair Style
        {7, 17, 1}, // Hair Color
        {11, 4, 1}, // Face Features
        {10, 5, 1}, // Jaw Features
        {9, 8, 1}, // Eye Color
        {11, 4, 4}, // Eyesight
    }};

    inline constexpr std::array<char const*, 9> FemaleLabels = {
        "Face",
        "Skin Type",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Face Features",
        "Jaw Features",
        "Eye Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> FemaleCapacities = {256, 256, 256, 256, 256, 48, 19, 17, 10, 8, 15, 16, 256};

    inline constexpr std::array<Requirement, 48> FemaleRequirements = {{
        {2, 0, 1, 1},
        {2, 1, 1, 1},
        {2, 2, 1, 1},
        {2, 3, 1, 1},
        {2, 4, 1, 1},
        {2, 5, 1, 1},
        {2, 6, 1, 1},
        {2, 7, 1, 1},
        {2, 8, 1, 1},
        {2, 9, 1, 1},
        {2, 10, 1, 1},
        {2, 11, 1, 4},
        {2, 12, 1, 4},
        {2, 13, 1, 4},
        {2, 14, 1, 4},
        {2, 15, 1, 4},
        {2, 16, 1, 4},
        {2, 17, 1, 4},
        {2, 18, 1, 4},
        {2, 19, 1, 4},
        {2, 20, 1, 4},
        {2, 21, 1, 4},
        {2, 22, 1, 2},
        {2, 23, 1, 2},
        {2, 24, 1, 2},
        {2, 25, 1, 2},
        {2, 26, 1, 2},
        {2, 27, 1, 2},
        {2, 28, 1, 2},
        {2, 29, 1, 2},
        {2, 30, 1, 2},
        {2, 31, 1, 2},
        {2, 32, 1, 2},
        {2, 33, 1, 1},
        {2, 34, 1, 1},
        {2, 35, 1, 1},
        {2, 36, 1, 1},
        {2, 37, 1, 1},
        {2, 38, 1, 4},
        {2, 39, 1, 4},
        {2, 40, 1, 4},
        {2, 41, 1, 4},
        {2, 42, 1, 4},
        {2, 43, 1, 2},
        {2, 44, 1, 2},
        {2, 45, 1, 2},
        {2, 46, 1, 2},
        {2, 47, 1, 2},
    }};

    inline constexpr std::array<Eligibility, 118> FemaleEligibility = {{
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
        {1, 0, 4294967295u},
        {1, 1, 4294967295u},
        {1, 2, 4294967295u},
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
        {2, 26, 4294967295u},
        {2, 27, 4294967295u},
        {2, 28, 4294967295u},
        {2, 29, 4294967295u},
        {2, 30, 4294967295u},
        {2, 31, 4294967295u},
        {2, 32, 4294967295u},
        {2, 33, 4294967295u},
        {2, 34, 4294967295u},
        {2, 35, 4294967295u},
        {2, 36, 4294967295u},
        {2, 37, 4294967295u},
        {2, 38, 4294967295u},
        {2, 39, 4294967295u},
        {2, 40, 4294967295u},
        {2, 41, 4294967295u},
        {2, 42, 4294967295u},
        {2, 43, 4294967295u},
        {2, 44, 4294967295u},
        {2, 45, 4294967295u},
        {2, 46, 4294967295u},
        {2, 47, 4294967295u},
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
        {4, 14, 4294967295u},
        {4, 15, 4294967295u},
        {4, 16, 4294967295u},
        {5, 0, 4294967295u},
        {5, 1, 4294967295u},
        {5, 2, 4294967295u},
        {5, 3, 4294967295u},
        {6, 0, 4294967295u},
        {6, 1, 4294967295u},
        {6, 2, 4294967295u},
        {6, 3, 4294967295u},
        {6, 4, 4294967295u},
        {7, 0, 32735u},
        {7, 1, 32735u},
        {7, 2, 32735u},
        {7, 3, 32735u},
        {7, 4, 32735u},
        {7, 5, 32735u},
        {7, 6, 32u},
        {7, 7, 32735u},
        {8, 0, 4294967295u},
        {8, 1, 30687u},
        {8, 2, 30687u},
        {8, 3, 30687u},
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
