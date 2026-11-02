// Generated from the pinned Retail option graph by generate_batch_codecs.py.
#ifndef ESTERIA_VULPERA_BATCH_APPEARANCE_H
#define ESTERIA_VULPERA_BATCH_APPEARANCE_H

#include <array>
#include <cstdint>

namespace VulperaBatchAppearance
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
    constexpr bool IsModern(std::array<std::uint8_t, 13> const& fields) { return fields[12] == 0xD5; }
    constexpr void Mark(std::array<std::uint8_t, 13>& fields) { fields[12] = 0xD5; }

    inline constexpr std::array<Option, 10> MaleOptions = {{
        {7, 6, 1}, // Face
        {6, 8, 1}, // Fur Color
        {7, 1, 6}, // Hair Style
        {8, 6, 1}, // Ears
        {9, 6, 1}, // Snout
        {5, 33, 1}, // Eye Color
        {10, 2, 4}, // Earrings
        {11, 3, 1}, // Pattern
        {10, 4, 1}, // Eyesight
        {11, 3, 3}, // Eye Style
    }};

    inline constexpr std::array<char const*, 10> MaleLabels = {
        "Face",
        "Fur Color",
        "Hair Style",
        "Ears",
        "Snout",
        "Eye Color",
        "Earrings",
        "Pattern",
        "Eyesight",
        "Eye Style",
    };

    inline constexpr std::array<std::uint16_t, 13> MaleCapacities = {256, 256, 256, 256, 256, 33, 8, 6, 6, 6, 8, 9, 256};

    inline constexpr std::array<Requirement, 2> MaleRequirements = {{
        {9, 1, 5, 536838144},
        {9, 2, 5, 536838144},
    }};

    inline constexpr std::array<Eligibility, 72> MaleEligibility = {{
        {0, 0, 4294967295u},
        {0, 1, 4294967295u},
        {0, 2, 4294967295u},
        {0, 3, 4294967295u},
        {0, 4, 4294967295u},
        {0, 5, 4294967295u},
        {1, 0, 4294967295u},
        {1, 1, 4294967295u},
        {1, 2, 4294967295u},
        {1, 3, 4294967295u},
        {1, 4, 4294967295u},
        {1, 5, 4294967295u},
        {1, 6, 4294967295u},
        {1, 7, 4294967295u},
        {2, 0, 4294967295u},
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
        {5, 10, 32735u},
        {5, 11, 32735u},
        {5, 12, 32735u},
        {5, 13, 32735u},
        {5, 14, 32u},
        {5, 15, 4294967295u},
        {5, 16, 4294967295u},
        {5, 17, 4294967295u},
        {5, 18, 4294967295u},
        {5, 19, 4294967295u},
        {5, 20, 4294967295u},
        {5, 21, 4294967295u},
        {5, 22, 4294967295u},
        {5, 23, 4294967295u},
        {5, 24, 4294967295u},
        {5, 25, 4294967295u},
        {5, 26, 4294967295u},
        {5, 27, 4294967295u},
        {5, 28, 4294967295u},
        {5, 29, 4294967295u},
        {5, 30, 4294967295u},
        {5, 31, 4294967295u},
        {5, 32, 4294967295u},
        {6, 0, 4294967295u},
        {6, 1, 4294967295u},
        {7, 0, 4294967295u},
        {7, 1, 4294967295u},
        {7, 2, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 30687u},
        {8, 2, 30687u},
        {8, 3, 30687u},
        {9, 0, 4294967295u},
        {9, 1, 4294967295u},
        {9, 2, 4294967295u},
    }};

    inline constexpr std::array<Option, 10> FemaleOptions = {{
        {8, 6, 1}, // Face
        {6, 8, 1}, // Fur Color
        {8, 1, 6}, // Hair Style
        {7, 8, 1}, // Ears
        {11, 3, 1}, // Pattern
        {9, 6, 1}, // Snout
        {5, 33, 1}, // Eye Color
        {10, 2, 4}, // Earrings
        {10, 4, 1}, // Eyesight
        {11, 3, 3}, // Eye Style
    }};

    inline constexpr std::array<char const*, 10> FemaleLabels = {
        "Face",
        "Fur Color",
        "Hair Style",
        "Ears",
        "Pattern",
        "Snout",
        "Eye Color",
        "Earrings",
        "Eyesight",
        "Eye Style",
    };

    inline constexpr std::array<std::uint16_t, 13> FemaleCapacities = {256, 256, 256, 256, 256, 33, 8, 8, 6, 6, 8, 9, 256};

    inline constexpr std::array<Requirement, 2> FemaleRequirements = {{
        {9, 1, 6, 536838144},
        {9, 2, 6, 536838144},
    }};

    inline constexpr std::array<Eligibility, 74> FemaleEligibility = {{
        {0, 0, 4294967295u},
        {0, 1, 4294967295u},
        {0, 2, 4294967295u},
        {0, 3, 4294967295u},
        {0, 4, 4294967295u},
        {0, 5, 4294967295u},
        {1, 0, 4294967295u},
        {1, 1, 4294967295u},
        {1, 2, 4294967295u},
        {1, 3, 4294967295u},
        {1, 4, 4294967295u},
        {1, 5, 4294967295u},
        {1, 6, 4294967295u},
        {1, 7, 4294967295u},
        {2, 0, 4294967295u},
        {3, 0, 4294967295u},
        {3, 1, 4294967295u},
        {3, 2, 4294967295u},
        {3, 3, 4294967295u},
        {3, 4, 4294967295u},
        {3, 5, 4294967295u},
        {3, 6, 4294967295u},
        {3, 7, 4294967295u},
        {4, 0, 4294967295u},
        {4, 1, 4294967295u},
        {4, 2, 4294967295u},
        {5, 0, 4294967295u},
        {5, 1, 4294967295u},
        {5, 2, 4294967295u},
        {5, 3, 4294967295u},
        {5, 4, 4294967295u},
        {5, 5, 4294967295u},
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
        {6, 14, 32u},
        {6, 15, 4294967295u},
        {6, 16, 4294967295u},
        {6, 17, 4294967295u},
        {6, 18, 4294967295u},
        {6, 19, 4294967295u},
        {6, 20, 4294967295u},
        {6, 21, 4294967295u},
        {6, 22, 4294967295u},
        {6, 23, 4294967295u},
        {6, 24, 4294967295u},
        {6, 25, 4294967295u},
        {6, 26, 4294967295u},
        {6, 27, 4294967295u},
        {6, 28, 4294967295u},
        {6, 29, 4294967295u},
        {6, 30, 4294967295u},
        {6, 31, 4294967295u},
        {6, 32, 4294967295u},
        {7, 0, 4294967295u},
        {7, 1, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 30687u},
        {8, 2, 30687u},
        {8, 3, 30687u},
        {9, 0, 4294967295u},
        {9, 1, 4294967295u},
        {9, 2, 4294967295u},
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
