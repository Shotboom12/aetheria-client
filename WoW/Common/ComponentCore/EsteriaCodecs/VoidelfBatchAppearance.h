// Generated from the pinned Retail option graph by generate_batch_codecs.py.
#ifndef ESTERIA_VOIDELF_BATCH_APPEARANCE_H
#define ESTERIA_VOIDELF_BATCH_APPEARANCE_H

#include <array>
#include <cstdint>

namespace VoidelfBatchAppearance
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
    constexpr bool IsModern(std::array<std::uint8_t, 13> const& fields) { return fields[12] == 0xDF; }
    constexpr void Mark(std::array<std::uint8_t, 13>& fields) { fields[12] = 0xDF; }

    inline constexpr std::array<Option, 15> MaleOptions = {{
        {6, 28, 1}, // Face
        {5, 30, 1}, // Skin Color
        {8, 13, 1}, // Hair Style
        {7, 16, 1}, // Hair Color
        {11, 8, 1}, // Facial Hair
        {9, 12, 1}, // Eye Color
        {5, 1, 30}, // Secondary Skin Color
        {5, 1, 30}, // Secondary Hair Color
        {11, 7, 8}, // Horns
        {7, 3, 16}, // Ears
        {10, 12, 1}, // Blindfold
        {6, 2, 28}, // Tentacles
        {8, 4, 13}, // Eyesight
        {9, 7, 12}, // Tattoo
        {10, 7, 12}, // Tattoo Color
    }};

    inline constexpr std::array<char const*, 15> MaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Facial Hair",
        "Eye Color",
        "Secondary Skin Color",
        "Secondary Hair Color",
        "Horns",
        "Ears",
        "Blindfold",
        "Tentacles",
        "Eyesight",
        "Tattoo",
        "Tattoo Color",
    };

    inline constexpr std::array<std::uint16_t, 13> MaleCapacities = {256, 256, 256, 256, 256, 30, 56, 48, 52, 84, 84, 56, 256};

    inline constexpr std::array<Requirement, 43> MaleRequirements = {{
        {1, 0, 0, 255},
        {1, 1, 0, 255},
        {1, 2, 0, 255},
        {1, 3, 0, 255},
        {1, 4, 0, 255},
        {1, 5, 0, 255},
        {1, 6, 0, 268435200},
        {1, 7, 0, 268435200},
        {1, 8, 0, 268435200},
        {1, 9, 0, 268435200},
        {1, 10, 0, 268435200},
        {1, 11, 0, 268435200},
        {1, 12, 0, 268435200},
        {1, 13, 0, 268435200},
        {1, 14, 0, 268435200},
        {1, 15, 0, 268435200},
        {1, 16, 0, 38797312},
        {1, 17, 0, 38797312},
        {1, 18, 0, 38797312},
        {1, 19, 0, 268435200},
        {1, 20, 0, 268435200},
        {1, 21, 0, 268435200},
        {1, 22, 0, 268435200},
        {5, 0, 1, 1065353215},
        {5, 1, 1, 1065353215},
        {5, 2, 1, 1065353215},
        {5, 3, 1, 1065353215},
        {5, 4, 1, 1065353215},
        {5, 5, 1, 1065353215},
        {5, 6, 1, 1065353215},
        {5, 7, 1, 1065353215},
        {5, 8, 1, 1065353215},
        {5, 9, 1, 8388608},
        {5, 10, 1, 1065353215},
        {5, 11, 1, 1065353215},
        {11, 1, 2, 2788},
        {14, 0, 13, 1},
        {14, 1, 13, 126},
        {14, 2, 13, 126},
        {14, 3, 13, 126},
        {14, 4, 13, 126},
        {14, 5, 13, 126},
        {14, 6, 13, 126},
    }};

    inline constexpr std::array<Eligibility, 151> MaleEligibility = {{
        {0, 0, 4294967295u},
        {0, 1, 4294967295u},
        {0, 2, 4294967295u},
        {0, 3, 4294967295u},
        {0, 4, 4294967295u},
        {0, 5, 4294967295u},
        {0, 6, 4294967295u},
        {0, 7, 4294967295u},
        {0, 8, 32735u},
        {0, 9, 32735u},
        {0, 10, 32735u},
        {0, 11, 32735u},
        {0, 12, 32735u},
        {0, 13, 32735u},
        {0, 14, 30687u},
        {0, 15, 30687u},
        {0, 16, 30687u},
        {0, 17, 30687u},
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
        {1, 16, 32u},
        {1, 17, 32u},
        {1, 18, 32u},
        {1, 19, 4294967295u},
        {1, 20, 4294967295u},
        {1, 21, 4294967295u},
        {1, 22, 4294967295u},
        {1, 23, 30687u},
        {1, 24, 2048u},
        {1, 25, 2048u},
        {1, 26, 2048u},
        {1, 27, 2048u},
        {1, 28, 2048u},
        {1, 29, 2048u},
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
        {3, 12, 4294967295u},
        {3, 13, 4294967295u},
        {3, 14, 4294967295u},
        {3, 15, 4294967295u},
        {4, 0, 4294967295u},
        {4, 1, 4294967295u},
        {4, 2, 4294967295u},
        {4, 3, 4294967295u},
        {4, 4, 4294967295u},
        {4, 5, 4294967295u},
        {4, 6, 4294967295u},
        {4, 7, 4294967295u},
        {5, 0, 30687u},
        {5, 1, 30687u},
        {5, 2, 30687u},
        {5, 3, 30687u},
        {5, 4, 30687u},
        {5, 5, 30687u},
        {5, 6, 30687u},
        {5, 7, 32u},
        {5, 8, 30687u},
        {5, 9, 30687u},
        {5, 10, 2048u},
        {5, 11, 2048u},
        {6, 0, 4294967295u},
        {7, 0, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 2048u},
        {8, 2, 2048u},
        {8, 3, 2048u},
        {8, 4, 2048u},
        {8, 5, 2048u},
        {8, 6, 2048u},
        {9, 0, 4294967295u},
        {9, 1, 4294967295u},
        {9, 2, 4294967295u},
        {10, 0, 4294967295u},
        {10, 1, 2048u},
        {10, 2, 2048u},
        {10, 3, 2048u},
        {10, 4, 2048u},
        {10, 5, 2048u},
        {10, 6, 2048u},
        {10, 7, 2048u},
        {10, 8, 2048u},
        {10, 9, 2048u},
        {10, 10, 2048u},
        {10, 11, 2048u},
        {11, 0, 4294967295u},
        {11, 1, 4294967295u},
        {12, 0, 4294967295u},
        {12, 1, 30687u},
        {12, 2, 30687u},
        {12, 3, 30687u},
        {13, 0, 4294967295u},
        {13, 1, 2048u},
        {13, 2, 2048u},
        {13, 3, 2048u},
        {13, 4, 2048u},
        {13, 5, 2048u},
        {13, 6, 2048u},
        {14, 0, 4294967295u},
        {14, 1, 2048u},
        {14, 2, 2048u},
        {14, 3, 2048u},
        {14, 4, 2048u},
        {14, 5, 2048u},
        {14, 6, 2048u},
    }};

    inline constexpr std::array<Option, 17> FemaleOptions = {{
        {6, 28, 1}, // Face
        {5, 30, 1}, // Skin Color
        {11, 11, 1}, // Hair Style
        {7, 16, 1}, // Hair Color
        {8, 12, 1}, // Eye Color
        {11, 7, 11}, // Horns
        {10, 5, 12}, // Earrings
        {5, 1, 60}, // Secondary Skin Color
        {9, 12, 1}, // Blindfold
        {5, 1, 60}, // Secondary Hair Color
        {5, 1, 60}, // Jewelry Color
        {6, 3, 28}, // Ears
        {5, 2, 30}, // Tentacles
        {7, 4, 16}, // Eyesight
        {10, 12, 1}, // Blindfold Color
        {8, 7, 12}, // Tattoo
        {9, 7, 12}, // Tattoo Color
    }};

    inline constexpr std::array<char const*, 17> FemaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Eye Color",
        "Horns",
        "Earrings",
        "Secondary Skin Color",
        "Blindfold",
        "Secondary Hair Color",
        "Jewelry Color",
        "Ears",
        "Tentacles",
        "Eyesight",
        "Blindfold Color",
        "Tattoo",
        "Tattoo Color",
    };

    inline constexpr std::array<std::uint16_t, 13> FemaleCapacities = {256, 256, 256, 256, 256, 60, 84, 64, 84, 84, 60, 77, 256};

    inline constexpr std::array<Requirement, 43> FemaleRequirements = {{
        {1, 0, 0, 255},
        {1, 1, 0, 255},
        {1, 2, 0, 255},
        {1, 3, 0, 255},
        {1, 4, 0, 255},
        {1, 5, 0, 255},
        {1, 6, 0, 268435200},
        {1, 7, 0, 268435200},
        {1, 8, 0, 268435200},
        {1, 9, 0, 268435200},
        {1, 10, 0, 268435200},
        {1, 11, 0, 268435200},
        {1, 12, 0, 268435200},
        {1, 13, 0, 268435200},
        {1, 14, 0, 268435200},
        {1, 15, 0, 268435200},
        {1, 16, 0, 69468160},
        {1, 17, 0, 69468160},
        {1, 18, 0, 69468160},
        {1, 19, 0, 268435200},
        {1, 20, 0, 268435200},
        {1, 21, 0, 268435200},
        {1, 22, 0, 268435200},
        {4, 0, 1, 1065353215},
        {4, 1, 1, 1065353215},
        {4, 2, 1, 1065353215},
        {4, 3, 1, 1065353215},
        {4, 4, 1, 1065353215},
        {4, 5, 1, 1065353215},
        {4, 6, 1, 1065353215},
        {4, 7, 1, 1065353215},
        {4, 8, 1, 1065353215},
        {4, 9, 1, 8388608},
        {4, 10, 1, 1065353215},
        {4, 11, 1, 1065353215},
        {12, 1, 2, 335},
        {16, 0, 15, 1},
        {16, 1, 15, 126},
        {16, 2, 15, 126},
        {16, 3, 15, 126},
        {16, 4, 15, 126},
        {16, 5, 15, 126},
        {16, 6, 15, 126},
    }};

    inline constexpr std::array<Eligibility, 159> FemaleEligibility = {{
        {0, 0, 4294967295u},
        {0, 1, 4294967295u},
        {0, 2, 4294967295u},
        {0, 3, 4294967295u},
        {0, 4, 4294967295u},
        {0, 5, 4294967295u},
        {0, 6, 4294967295u},
        {0, 7, 4294967295u},
        {0, 8, 32735u},
        {0, 9, 32735u},
        {0, 10, 32735u},
        {0, 11, 32735u},
        {0, 12, 32735u},
        {0, 13, 32735u},
        {0, 14, 30687u},
        {0, 15, 30687u},
        {0, 16, 30687u},
        {0, 17, 30687u},
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
        {1, 16, 32u},
        {1, 17, 32u},
        {1, 18, 32u},
        {1, 19, 4294967295u},
        {1, 20, 4294967295u},
        {1, 21, 4294967295u},
        {1, 22, 4294967295u},
        {1, 23, 30687u},
        {1, 24, 2048u},
        {1, 25, 2048u},
        {1, 26, 2048u},
        {1, 27, 2048u},
        {1, 28, 2048u},
        {1, 29, 2048u},
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
        {3, 11, 4294967295u},
        {3, 12, 4294967295u},
        {3, 13, 4294967295u},
        {3, 14, 4294967295u},
        {3, 15, 4294967295u},
        {4, 0, 30687u},
        {4, 1, 30687u},
        {4, 2, 30687u},
        {4, 3, 30687u},
        {4, 4, 30687u},
        {4, 5, 30687u},
        {4, 6, 30687u},
        {4, 7, 32u},
        {4, 8, 30687u},
        {4, 9, 30687u},
        {4, 10, 2048u},
        {4, 11, 2048u},
        {5, 0, 4294967295u},
        {5, 1, 2048u},
        {5, 2, 2048u},
        {5, 3, 2048u},
        {5, 4, 2048u},
        {5, 5, 2048u},
        {5, 6, 2048u},
        {6, 0, 4294967295u},
        {6, 1, 4294967295u},
        {6, 2, 4294967295u},
        {6, 3, 4294967295u},
        {6, 4, 4294967295u},
        {7, 0, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 2048u},
        {8, 2, 2048u},
        {8, 3, 2048u},
        {8, 4, 2048u},
        {8, 5, 2048u},
        {8, 6, 2048u},
        {8, 7, 2048u},
        {8, 8, 2048u},
        {8, 9, 2048u},
        {8, 10, 2048u},
        {8, 11, 2048u},
        {9, 0, 4294967295u},
        {10, 0, 4294967295u},
        {11, 0, 4294967295u},
        {11, 1, 4294967295u},
        {11, 2, 4294967295u},
        {12, 0, 4294967295u},
        {12, 1, 4294967295u},
        {13, 0, 4294967295u},
        {13, 1, 30687u},
        {13, 2, 30687u},
        {13, 3, 30687u},
        {14, 0, 30719u},
        {14, 1, 2048u},
        {14, 2, 2048u},
        {14, 3, 2048u},
        {14, 4, 2048u},
        {14, 5, 2048u},
        {14, 6, 2048u},
        {14, 7, 2048u},
        {14, 8, 2048u},
        {14, 9, 2048u},
        {14, 10, 2048u},
        {14, 11, 2048u},
        {15, 0, 4294967295u},
        {15, 1, 2048u},
        {15, 2, 2048u},
        {15, 3, 2048u},
        {15, 4, 2048u},
        {15, 5, 2048u},
        {15, 6, 2048u},
        {16, 0, 4294967295u},
        {16, 1, 2048u},
        {16, 2, 2048u},
        {16, 3, 2048u},
        {16, 4, 2048u},
        {16, 5, 2048u},
        {16, 6, 2048u},
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
