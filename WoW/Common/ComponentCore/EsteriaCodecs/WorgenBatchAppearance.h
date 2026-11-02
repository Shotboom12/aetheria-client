// Generated from the pinned Retail option graph by generate_batch_codecs.py.
#ifndef ESTERIA_WORGEN_BATCH_APPEARANCE_H
#define ESTERIA_WORGEN_BATCH_APPEARANCE_H

#include <array>
#include <cstdint>

namespace WorgenBatchAppearance
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
    constexpr bool IsModern(std::array<std::uint8_t, 13> const& fields) { return fields[12] == 0xDE; }
    constexpr void Mark(std::array<std::uint8_t, 13>& fields) { fields[12] = 0xDE; }

    inline constexpr std::array<Option, 7> MaleOptions = {{
        {6, 19, 1}, // Face
        {7, 19, 1}, // Fur Color
        {8, 13, 1}, // Hair Style
        {9, 12, 1}, // Facial Hair
        {10, 12, 1}, // Ears
        {5, 26, 1}, // Eye Color
        {11, 4, 1}, // Eyesight
    }};

    inline constexpr std::array<char const*, 7> MaleLabels = {
        "Face",
        "Fur Color",
        "Hair Style",
        "Facial Hair",
        "Ears",
        "Eye Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> MaleCapacities = {256, 256, 256, 256, 256, 26, 19, 19, 13, 12, 12, 4, 256};

    inline constexpr std::array<Requirement, 19> MaleRequirements = {{
        {1, 0, 0, 492543},
        {1, 1, 0, 492543},
        {1, 2, 0, 492543},
        {1, 3, 0, 492543},
        {1, 4, 0, 492543},
        {1, 5, 0, 31776},
        {1, 6, 0, 31776},
        {1, 7, 0, 31776},
        {1, 8, 0, 492543},
        {1, 9, 0, 492543},
        {1, 10, 0, 492543},
        {1, 11, 0, 492543},
        {1, 12, 0, 492543},
        {1, 13, 0, 492543},
        {1, 14, 0, 492543},
        {1, 15, 0, 492543},
        {1, 16, 0, 492543},
        {1, 17, 0, 492543},
        {1, 18, 0, 492543},
    }};

    inline constexpr std::array<Eligibility, 105> MaleEligibility = {{
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
        {0, 10, 32u},
        {0, 11, 32u},
        {0, 12, 32u},
        {0, 13, 32u},
        {0, 14, 32u},
        {0, 15, 32735u},
        {0, 16, 32735u},
        {0, 17, 32u},
        {0, 18, 32u},
        {1, 0, 32735u},
        {1, 1, 32735u},
        {1, 2, 32735u},
        {1, 3, 32735u},
        {1, 4, 32735u},
        {1, 5, 32u},
        {1, 6, 32u},
        {1, 7, 32u},
        {1, 8, 32735u},
        {1, 9, 32735u},
        {1, 10, 32735u},
        {1, 11, 32735u},
        {1, 12, 32735u},
        {1, 13, 32735u},
        {1, 14, 32735u},
        {1, 15, 32735u},
        {1, 16, 32735u},
        {1, 17, 32735u},
        {1, 18, 32735u},
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
        {5, 14, 32735u},
        {5, 15, 32735u},
        {5, 16, 32735u},
        {5, 17, 32735u},
        {5, 18, 32735u},
        {5, 19, 32735u},
        {5, 20, 32735u},
        {5, 21, 32735u},
        {5, 22, 32735u},
        {5, 23, 32735u},
        {5, 24, 32735u},
        {5, 25, 32u},
        {6, 0, 4294967295u},
        {6, 1, 30687u},
        {6, 2, 30687u},
        {6, 3, 30687u},
    }};

    inline constexpr std::array<Option, 6> FemaleOptions = {{
        {5, 32, 1}, // Face
        {7, 23, 1}, // Fur Color
        {8, 16, 1}, // Hair Style
        {9, 12, 1}, // Ears
        {6, 26, 1}, // Eye Color
        {10, 4, 1}, // Eyesight
    }};

    inline constexpr std::array<char const*, 6> FemaleLabels = {
        "Face",
        "Fur Color",
        "Hair Style",
        "Ears",
        "Eye Color",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> FemaleCapacities = {256, 256, 256, 256, 256, 32, 26, 23, 16, 12, 4, 1, 256};

    inline constexpr std::array<Requirement, 3> FemaleRequirements = {{
        {1, 9, 0, 144703488},
        {1, 10, 0, 144703488},
        {1, 11, 0, 144703488},
    }};

    inline constexpr std::array<Eligibility, 113> FemaleEligibility = {{
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
        {0, 10, 32735u},
        {0, 11, 32735u},
        {0, 12, 32735u},
        {0, 13, 32735u},
        {0, 14, 32u},
        {0, 15, 32u},
        {0, 16, 32u},
        {0, 17, 32u},
        {0, 18, 32u},
        {0, 19, 32u},
        {0, 20, 32u},
        {0, 21, 32u},
        {0, 22, 32u},
        {0, 23, 32u},
        {0, 24, 32u},
        {0, 25, 32u},
        {0, 26, 32u},
        {0, 27, 32u},
        {0, 28, 32735u},
        {0, 29, 32u},
        {0, 30, 32735u},
        {0, 31, 32u},
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
        {1, 15, 4294967295u},
        {1, 16, 4294967295u},
        {1, 17, 4294967295u},
        {1, 18, 4294967295u},
        {1, 19, 4294967295u},
        {1, 20, 4294967295u},
        {1, 21, 4294967295u},
        {1, 22, 4294967295u},
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
        {4, 19, 32735u},
        {4, 20, 32735u},
        {4, 21, 32735u},
        {4, 22, 32735u},
        {4, 23, 32735u},
        {4, 24, 32735u},
        {4, 25, 32u},
        {5, 0, 4294967295u},
        {5, 1, 30687u},
        {5, 2, 30687u},
        {5, 3, 30687u},
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
