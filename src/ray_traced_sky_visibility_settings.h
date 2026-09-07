#pragma once

#include "noise_settings.h"
#include "ray_visibility_max_distance.h"

#include <cstdint>

namespace uvsr
{
    inline constexpr int32_t RayTracedSkyVisibilityMinimumSampleRateLog2 = 0;
    inline constexpr int32_t RayTracedSkyVisibilityMaximumSampleRateLog2 = 6;
    inline constexpr uint32_t RayTracedSkyVisibilityMaximumSamplesPerPixel =
        1u << uint32_t(RayTracedSkyVisibilityMaximumSampleRateLog2);
    inline constexpr float RayTracedSkyVisibilityMaximumRayBias = 0.1f;
    struct RayTracedSkyVisibilitySettings
    {
        bool enabled = true;
        bool applyToDiffuseIbl = true;
        bool applyToSpecularIbl = true;
        int32_t sampleRateLog2 = 1;
        float rayBias = 0.002f;
        RayVisibilityMaxDistance maxDistance =
            RayVisibilityMaxDistance::Maximum;
        NoiseOverrideSettings noise;
    };

    [[nodiscard]] inline constexpr bool
        IsRayTracedSkyVisibilitySampleRateSupported(int32_t sampleRateLog2)
    {
        return sampleRateLog2 >=
                RayTracedSkyVisibilityMinimumSampleRateLog2 &&
            sampleRateLog2 <=
                RayTracedSkyVisibilityMaximumSampleRateLog2;
    }

    [[nodiscard]] inline constexpr uint32_t
        ResolveRayTracedSkyVisibilitySampleCount(int32_t sampleRateLog2)
    {
        return IsRayTracedSkyVisibilitySampleRateSupported(sampleRateLog2)
            ? 1u << uint32_t(sampleRateLog2)
            : 1u;
    }

    [[nodiscard]] inline constexpr bool
        HasRayTracedSkyVisibilityConsumer(
            const RayTracedSkyVisibilitySettings& settings)
    {
        return settings.applyToDiffuseIbl || settings.applyToSpecularIbl;
    }

    [[nodiscard]] inline constexpr bool
        IsRayTracedSkyVisibilityConfigurationSupported(
            const RayTracedSkyVisibilitySettings& settings)
    {
        return IsRayTracedSkyVisibilitySampleRateSupported(
                settings.sampleRateLog2) &&
            IsValidNoiseSettings(settings.noise.custom) &&
            IsRayVisibilityMaxDistanceSupported(settings.maxDistance) &&
            settings.rayBias >= 0.f &&
            settings.rayBias <= RayTracedSkyVisibilityMaximumRayBias;
    }
}
