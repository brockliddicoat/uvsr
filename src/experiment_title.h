#pragma once

#include <algorithm>
#include <string_view>

namespace uvsr
{
    inline bool IsValidExperimentTitle(std::string_view description) noexcept
    {
        // Keep this explicit ASCII comparison aligned with the launcher. C/C++
        // locale character helpers would accept a broader and environment-
        // dependent alphabet, while experiment titles are build provenance.
        return !description.empty() && std::all_of(
            description.begin(),
            description.end(),
            [](unsigned char character)
            {
                return character >= 'a' && character <= 'z';
            });
    }
}
