#pragma once

#include <algorithm>
#include <cstdint>

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

    [[nodiscard]] inline constexpr bool
        IsRayVisibilityMaxDistanceSupported(
            RayVisibilityMaxDistance maxDistance)
    {
        return maxDistance >= RayVisibilityMaxDistance::Maximum &&
            maxDistance < RayVisibilityMaxDistance::Count;
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
