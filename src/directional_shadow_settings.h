#pragma once

#include "ray_visibility_max_distance.h"

namespace uvsr
{
    inline constexpr float DirectionalShadowMaximumRayBias = 0.1f;

    struct DirectionalShadowSettings
    {
        bool enabled = true;
        float rayBias = 0.002f;
        RayVisibilityMaxDistance maxDistance =
            RayVisibilityMaxDistance::Maximum;
    };

    [[nodiscard]] inline constexpr bool
        IsDirectionalReceiverSampleCountSupported(
            unsigned sampleCount) noexcept
    {
        return sampleCount == 1u || sampleCount == 2u ||
            sampleCount == 4u || sampleCount == 8u ||
            sampleCount == 16u;
    }

    [[nodiscard]] inline constexpr bool IsDirectionalShadowSettingsValid(
        const DirectionalShadowSettings& settings) noexcept
    {
        return settings.rayBias >= 0.f &&
            settings.rayBias <= DirectionalShadowMaximumRayBias &&
            IsRayVisibilityMaxDistanceSupported(settings.maxDistance);
    }
}
