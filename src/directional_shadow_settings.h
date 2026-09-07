#pragma once

#include "ray_visibility_max_distance.h"
#include <cstdint>

namespace uvsr
{
    inline constexpr float DirectionalShadowMaximumRayBias = 0.1f;

    struct DirectionalShadowSettings
    {
        bool enabled = true;
        bool hardShadows = false;
        std::uint32_t samplesPerPixel = 4u;
        float rayBias = 0.002f;
        RayVisibilityMaxDistance maxDistance =
            RayVisibilityMaxDistance::Maximum;
    };



    [[nodiscard]] inline constexpr std::uint32_t ResolveRayShadowSampleCount(
        const DirectionalShadowSettings& settings) noexcept
    {
        return settings.hardShadows ? 1u : settings.samplesPerPixel;
    }

    [[nodiscard]] inline constexpr float ResolveShadowEmitterSize(
        float authoredSize, bool hardShadows) noexcept
    {
        return hardShadows ? 0.f : authoredSize;
    }

    [[nodiscard]] inline constexpr bool IsDirectionalShadowSettingsValid(
        const DirectionalShadowSettings& settings) noexcept
    {
        return settings.samplesPerPixel >= 1u && settings.samplesPerPixel <= 64u &&
            (settings.samplesPerPixel & (settings.samplesPerPixel - 1u)) == 0u &&
            settings.rayBias >= 0.f &&
            settings.rayBias <= DirectionalShadowMaximumRayBias &&
            IsRayVisibilityMaxDistanceSupported(settings.maxDistance);
    }
}
