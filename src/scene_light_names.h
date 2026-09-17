#pragma once

#include "array_view.h"

namespace uvsr
{
    [[nodiscard]] constexpr char FoldSceneLightNameCharacter(
        char character)
    {
        return character >= 'A' && character <= 'Z'
            ? static_cast<char>(character - 'A' + 'a')
            : character;
    }

    [[nodiscard]] inline bool SceneLightNameEquals(
        ArrayView<const char> left,
        ArrayView<const char> right)
    {
        if (!left.IsValid() || !right.IsValid() || left.count != right.count)
            return false;

        for (size_t characterIndex = 0;
            characterIndex < left.count;
            ++characterIndex)
        {
            if (FoldSceneLightNameCharacter(left.data[characterIndex]) !=
                FoldSceneLightNameCharacter(right.data[characterIndex]))
            {
                return false;
            }
        }
        return true;
    }

    // normalize only the two renderer-owned identities. other names keep their
    // original borrowed storage; replacements borrow static literals.
    [[nodiscard]] inline ArrayView<const char> NormalizeSceneLightName(
        ArrayView<const char> name)
    {
        if (SceneLightNameEquals(name, {"sun", 3}) ||
            SceneLightNameEquals(name, {"sun_1", 5}))
        {
            return {"sun_1", 5};
        }

        if (SceneLightNameEquals(name, {"hdri_sky", 8}) ||
            SceneLightNameEquals(name, {"hdri_sky_1", 10}))
        {
            return {"hdri_sky_1", 10};
        }

        return name;
    }
}
