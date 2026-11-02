// Generated from the pinned Retail option graph by generate_batch_codecs.py.
#ifndef ESTERIA_DRAENEI_BATCH_APPEARANCE_H
#define ESTERIA_DRAENEI_BATCH_APPEARANCE_H

#include <array>
#include <cstdint>

namespace DraeneiBatchAppearance
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
    constexpr bool IsModern(std::array<std::uint8_t, 13> const& fields) { return fields[12] == 0xDD; }
    constexpr void Mark(std::array<std::uint8_t, 13>& fields) { fields[12] = 0xDD; }

    inline constexpr std::array<Option, 12> MaleOptions = {{
        {8, 20, 1}, // Face
        {7, 21, 1}, // Skin Color
        {6, 23, 1}, // Hair Style
        {9, 13, 1}, // Hair Color
        {10, 11, 1}, // Facial Hair
        {11, 7, 8}, // Eye Color
        {8, 2, 20}, // Circlet
        {9, 3, 13}, // Horn Decoration
        {11, 8, 1}, // Tendrils
        {5, 30, 1}, // Jewelry Color
        {7, 2, 21}, // Tail
        {10, 4, 11}, // Eyesight
    }};

    inline constexpr std::array<char const*, 12> MaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Facial Hair",
        "Eye Color",
        "Circlet",
        "Horn Decoration",
        "Tendrils",
        "Jewelry Color",
        "Tail",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> MaleCapacities = {256, 256, 256, 256, 256, 30, 23, 42, 40, 39, 44, 56, 256};

    inline constexpr std::array<Requirement, 36> MaleRequirements = {{
        {1, 14, 0, 542720},
        {1, 15, 0, 542720},
        {1, 16, 0, 542720},
        {6, 1, 2, 8351159},
        {7, 1, 2, 262144},
        {7, 2, 2, 2097152},
        {9, 0, 6, 1},
        {9, 1, 6, 1},
        {9, 2, 6, 1},
        {9, 3, 6, 1},
        {9, 4, 6, 1},
        {9, 5, 6, 1},
        {9, 6, 6, 1},
        {9, 7, 6, 1},
        {9, 8, 6, 1},
        {9, 9, 6, 1},
        {9, 10, 6, 1},
        {9, 11, 6, 1},
        {9, 12, 6, 1},
        {9, 13, 6, 1},
        {9, 14, 6, 1},
        {9, 15, 6, 2},
        {9, 16, 6, 2},
        {9, 17, 6, 2},
        {9, 18, 6, 2},
        {9, 19, 6, 2},
        {9, 20, 6, 2},
        {9, 21, 6, 2},
        {9, 22, 6, 2},
        {9, 23, 6, 2},
        {9, 24, 6, 2},
        {9, 25, 6, 2},
        {9, 26, 6, 2},
        {9, 27, 6, 2},
        {9, 28, 6, 2},
        {9, 29, 6, 2},
    }};

    inline constexpr std::array<Eligibility, 144> MaleEligibility = {{
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
        {1, 9, 4294967295u},
        {1, 10, 4294967295u},
        {1, 11, 4294967295u},
        {1, 12, 4294967295u},
        {1, 13, 4294967295u},
        {1, 14, 32u},
        {1, 15, 32u},
        {1, 16, 32u},
        {1, 17, 4294967295u},
        {1, 18, 4294967295u},
        {1, 19, 4294967295u},
        {1, 20, 4294967295u},
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
        {4, 4, 4294967295u},
        {4, 5, 4294967295u},
        {4, 6, 4294967295u},
        {4, 7, 4294967295u},
        {4, 8, 4294967295u},
        {4, 9, 4294967295u},
        {4, 10, 4294967295u},
        {5, 0, 32735u},
        {5, 1, 32u},
        {5, 2, 32735u},
        {5, 3, 32735u},
        {5, 4, 32735u},
        {5, 5, 32735u},
        {5, 6, 32735u},
        {6, 0, 4294967295u},
        {6, 1, 4294967295u},
        {7, 0, 4294967295u},
        {7, 1, 4294967295u},
        {7, 2, 4294967295u},
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
        {9, 3, 4294967295u},
        {9, 4, 4294967295u},
        {9, 5, 4294967295u},
        {9, 6, 4294967295u},
        {9, 7, 4294967295u},
        {9, 8, 4294967295u},
        {9, 9, 4294967295u},
        {9, 10, 4294967295u},
        {9, 11, 4294967295u},
        {9, 12, 4294967295u},
        {9, 13, 4294967295u},
        {9, 14, 4294967295u},
        {9, 15, 4294967295u},
        {9, 16, 4294967295u},
        {9, 17, 4294967295u},
        {9, 18, 4294967295u},
        {9, 19, 4294967295u},
        {9, 20, 4294967295u},
        {9, 21, 4294967295u},
        {9, 22, 4294967295u},
        {9, 23, 4294967295u},
        {9, 24, 4294967295u},
        {9, 25, 4294967295u},
        {9, 26, 4294967295u},
        {9, 27, 4294967295u},
        {9, 28, 4294967295u},
        {9, 29, 4294967295u},
        {10, 0, 4294967295u},
        {10, 1, 4294967295u},
        {11, 0, 4294967295u},
        {11, 1, 30687u},
        {11, 2, 30687u},
        {11, 3, 30687u},
    }};

    inline constexpr std::array<Option, 12> FemaleOptions = {{
        {6, 20, 1}, // Face
        {7, 19, 1}, // Skin Color
        {5, 24, 1}, // Hair Style
        {9, 13, 1}, // Hair Color
        {10, 11, 1}, // Horns
        {11, 7, 1}, // Eye Color
        {8, 2, 15}, // Circlet
        {9, 3, 13}, // Hair Decoration
        {7, 2, 19}, // Tendrils
        {8, 15, 1}, // Jewelry Color
        {11, 6, 7}, // Tail
        {10, 4, 11}, // Eyesight
    }};

    inline constexpr std::array<char const*, 12> FemaleLabels = {
        "Face",
        "Skin Color",
        "Hair Style",
        "Hair Color",
        "Horns",
        "Eye Color",
        "Circlet",
        "Hair Decoration",
        "Tendrils",
        "Jewelry Color",
        "Tail",
        "Eyesight",
    };

    inline constexpr std::array<std::uint16_t, 13> FemaleCapacities = {256, 256, 256, 256, 256, 24, 20, 38, 30, 39, 44, 42, 256};

    inline constexpr std::array<Requirement, 6> FemaleRequirements = {{
        {1, 12, 0, 279552},
        {1, 13, 0, 279552},
        {1, 14, 0, 279552},
        {6, 1, 2, 16717775},
        {7, 1, 2, 2097152},
        {7, 2, 2, 4194304},
    }};

    inline constexpr std::array<Eligibility, 126> FemaleEligibility = {{
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
        {1, 9, 4294967295u},
        {1, 10, 4294967295u},
        {1, 11, 4294967295u},
        {1, 12, 32u},
        {1, 13, 32u},
        {1, 14, 32u},
        {1, 15, 4294967295u},
        {1, 16, 4294967295u},
        {1, 17, 4294967295u},
        {1, 18, 4294967295u},
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
        {4, 4, 4294967295u},
        {4, 5, 4294967295u},
        {4, 6, 4294967295u},
        {4, 7, 4294967295u},
        {4, 8, 4294967295u},
        {4, 9, 4294967295u},
        {4, 10, 4294967295u},
        {5, 0, 32735u},
        {5, 1, 32u},
        {5, 2, 32735u},
        {5, 3, 32735u},
        {5, 4, 32735u},
        {5, 5, 32735u},
        {5, 6, 32735u},
        {6, 0, 4294967295u},
        {6, 1, 4294967295u},
        {7, 0, 4294967295u},
        {7, 1, 4294967295u},
        {7, 2, 4294967295u},
        {8, 0, 4294967295u},
        {8, 1, 4294967295u},
        {9, 0, 4294967295u},
        {9, 1, 4294967295u},
        {9, 2, 4294967295u},
        {9, 3, 4294967295u},
        {9, 4, 4294967295u},
        {9, 5, 4294967295u},
        {9, 6, 4294967295u},
        {9, 7, 4294967295u},
        {9, 8, 4294967295u},
        {9, 9, 4294967295u},
        {9, 10, 4294967295u},
        {9, 11, 4294967295u},
        {9, 12, 4294967295u},
        {9, 13, 4294967295u},
        {9, 14, 4294967295u},
        {10, 0, 4294967295u},
        {10, 1, 4294967295u},
        {10, 2, 4294967295u},
        {10, 3, 4294967295u},
        {10, 4, 4294967295u},
        {10, 5, 4294967295u},
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
