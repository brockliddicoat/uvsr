#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    enum class RayVisibilityMaxDistance : uint32_t
    {
        Maximum,
        Meters32,
        Meters16,
        Meters8,
        Meters4,
        Meters2,
        Count
    };

    inline constexpr std::array<const char*, 6>
        RayVisibilityMaxDistanceLabels = {
            "Max", "32m", "16m", "8m", "4m", "2m"
        };

    [[nodiscard]] inline constexpr bool
        IsRayVisibilityMaxDistanceSupported(
            RayVisibilityMaxDistance maxDistance)
    {
        return maxDistance >= RayVisibilityMaxDistance::Maximum &&
            maxDistance < RayVisibilityMaxDistance::Count;
    }

    [[nodiscard]] inline constexpr std::string_view
        GetRayVisibilityMaxDistanceLabel(
            RayVisibilityMaxDistance maxDistance)
    {
        return IsRayVisibilityMaxDistanceSupported(maxDistance)
            ? RayVisibilityMaxDistanceLabels[std::size_t(maxDistance)]
            : "";
    }

    [[nodiscard]] inline constexpr float ResolveRayVisibilityMaxDistance(
        RayVisibilityMaxDistance maxDistance,
        float sceneDiagonal)
    {
        switch (maxDistance)
        {
        case RayVisibilityMaxDistance::Meters32: return 32.f;
        case RayVisibilityMaxDistance::Meters16: return 16.f;
        case RayVisibilityMaxDistance::Meters8: return 8.f;
        case RayVisibilityMaxDistance::Meters4: return 4.f;
        case RayVisibilityMaxDistance::Meters2: return 2.f;
        default: return std::max(sceneDiagonal * 2.f, 1.f);
        }
    }
}
