#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace uvsr
{
    [[nodiscard]] constexpr char FoldSceneLightNameCharacter(
        char character)
    {
        return character >= 'A' && character <= 'Z'
            ? static_cast<char>(character - 'A' + 'a')
            : character;
    }

    [[nodiscard]] constexpr bool SceneLightNameEquals(
        std::string_view left,
        std::string_view right)
    {
        if (left.size() != right.size())
            return false;

        for (std::size_t characterIndex = 0;
            characterIndex < left.size();
            ++characterIndex)
        {
            if (FoldSceneLightNameCharacter(left[characterIndex]) !=
                FoldSceneLightNameCharacter(right[characterIndex]))
            {
                return false;
            }
        }
        return true;
    }

    // Imported Sponza variants use inconsistent node casing and the legacy
    // HDRI_SKY spelling. Normalize only the two renderer-owned light identities;
    // preserve every authored lamp name verbatim.
    [[nodiscard]] inline std::string NormalizeSceneLightName(
        std::string_view name)
    {
        if (SceneLightNameEquals(name, "sun") ||
            SceneLightNameEquals(name, "sun_1"))
        {
            return "sun_1";
        }

        if (SceneLightNameEquals(name, "hdri_sky") ||
            SceneLightNameEquals(name, "hdri_sky_1"))
        {
            return "hdri_sky_1";
        }

        return std::string(name);
    }
}
