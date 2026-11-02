// Generated from the pinned Retail option graph by generate_batch_codecs.py.
#ifndef ESTERIA_KULTIRAN_BATCH_APPEARANCE_H
#define ESTERIA_KULTIRAN_BATCH_APPEARANCE_H

#include <array>
#include <cstdint>

namespace KultiranBatchAppearance
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
    constexpr bool IsModern(std::array<std::uint8_t, 13> const& fields) { return fields[12] == 0xE1; }
    constexpr void Mark(std::array<std::uint8_t, 13>& fields) { fields[12] = 0xE1; }

    inline constexpr std::array<Option, 9> MaleOptions = {{
        {6, 7, 1}, // Face
        {8, 6, 1}, // Skin Color
        {9, 6, 1}, // Hair Style
        {5, 13, 1}, // Hair Color
        {11, 4, 1}, // Mustache
        {10, 6, 1}, // Beard
        {7, 7, 1}, // Eye Color
        {11, 4, 4}, // Eyesight
        {8, 2, 6}, // Ears
    }};

    inline constexpr std::array<char const*, 9> MaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Mustache",
        "Beard",
        "Eye Color",
        "Eyesight",
        "Ears",
    };

    inline constexpr std::array<std::uint16_t, 13> MaleCapacities = {256, 256, 256, 256, 256, 13, 7, 7, 12, 6, 6, 16, 256};

    inline constexpr std::array<Requirement, 0> MaleRequirements = {{
    }};

    inline constexpr std::array<Eligibility, 55> MaleEligibility = {{
        {0, 0, 4294967295u},
        {0, 1, 4294967295u},
        {0, 2, 4294967295u},
        {0, 3, 4294967295u},
        {0, 4, 4294967295u},
        {0, 5, 4294967295u},
        {0, 6, 4294967295u},
        {1, 0, 4294967295u},
        {1, 1, 4294967295u},
        {1, 2, 4294967295u},
        {1, 3, 4294967295u},
        {1, 4, 4294967295u},
        {1, 5, 4294967295u},
        {2, 0, 4294967295u},
        {2, 1, 4294967295u},
        {2, 2, 4294967295u},
        {2, 3, 4294967295u},
        {2, 4, 4294967295u},
        {2, 5, 4294967295u},
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
        {6, 0, 32735u},
        {6, 1, 32735u},
        {6, 2, 32735u},
        {6, 3, 32735u},
        {6, 4, 32735u},
        {6, 5, 32735u},
        {6, 6, 32u},
        {7, 0, 4294967295u},
        {7, 1, 30687u},
        {7, 2, 30687u},
        {7, 3, 30687u},
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
    }};

    inline constexpr std::array<Option, 9> FemaleOptions = {{
        {7, 7, 1}, // Face
        {11, 6, 1}, // Skin Color
        {6, 10, 1}, // Hair Style
        {5, 13, 1}, // Hair Color
        {8, 7, 1}, // Eye Color
        {9, 7, 1}, // Earrings
        {10, 7, 1}, // Necklace
        {11, 4, 6}, // Eyesight
        {7, 2, 7}, // Ears
    }};

    inline constexpr std::array<char const*, 9> FemaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Eye Color",
        "Earrings",
        "Necklace",
        "Eyesight",
        "Ears",
    };

    inline constexpr std::array<std::uint16_t, 13> FemaleCapacities = {256, 256, 256, 256, 256, 13, 10, 14, 7, 7, 7, 24, 256};

    inline constexpr std::array<Requirement, 0> FemaleRequirements = {{
    }};

    inline constexpr std::array<Eligibility, 63> FemaleEligibility = {{
        {0, 0, 4294967295u},
        {0, 1, 4294967295u},
        {0, 2, 4294967295u},
        {0, 3, 4294967295u},
        {0, 4, 4294967295u},
        {0, 5, 4294967295u},
        {0, 6, 4294967295u},
        {1, 0, 4294967295u},
        {1, 1, 4294967295u},
        {1, 2, 4294967295u},
        {1, 3, 4294967295u},
        {1, 4, 4294967295u},
        {1, 5, 4294967295u},
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
        {4, 0, 32735u},
        {4, 1, 32735u},
        {4, 2, 32735u},
        {4, 3, 32735u},
        {4, 4, 32735u},
        {4, 5, 32735u},
        {4, 6, 32u},
        {5, 0, 4294967295u},
        {5, 1, 4294967295u},
        {5, 2, 4294967295u},
        {5, 3, 4294967295u},
        {5, 4, 4294967295u},
        {5, 5, 4294967295u},
        {5, 6, 4294967295u},
        {6, 0, 4294967295u},
        {6, 1, 4294967295u},
        {6, 2, 4294967295u},
        {6, 3, 4294967295u},
        {6, 4, 4294967295u},
        {6, 5, 4294967295u},
        {6, 6, 4294967295u},
        {7, 0, 4294967295u},
        {7, 1, 30687u},
        {7, 2, 30687u},
        {7, 3, 30687u},
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
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
