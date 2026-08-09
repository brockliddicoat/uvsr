#pragma once

#include "noise_settings.h"
#include "ray_visibility_max_distance.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    inline constexpr int32_t HeitzRatioEstimatorMinimumSampleRateLog2 = 0;
    inline constexpr int32_t HeitzRatioEstimatorMaximumSampleRateLog2 = 6;
    inline constexpr uint32_t HeitzRatioEstimatorMaximumSamplesPerPixel =
        1u << uint32_t(HeitzRatioEstimatorMaximumSampleRateLog2);
    inline constexpr float HeitzRatioEstimatorMaximumRayBias = 0.1f;
    inline constexpr float HeitzShadowHitDistanceInvalid = 0.f;
    inline constexpr float HeitzShadowHitDistanceMaximum = 65472.f;
    inline constexpr float HeitzShadowHitDistanceMiss = 65504.f;
    inline constexpr std::array<std::string_view, 7>
        HeitzRatioEstimatorSampleRateLabels = {
            "1", "2", "4", "8", "16", "32", "64"
        };

    struct HeitzRatioEstimatorShadowSettings
    {
        bool enabled = false;
        bool hardShadows = false;
        bool useRatioEstimator = true;
        bool outputHitDistance = false;
        int32_t sampleRateLog2 = 1;
        float rayBias = 0.002f;
        RayVisibilityMaxDistance maxDistance =
            RayVisibilityMaxDistance::Maximum;
        NoiseOverrideSettings noise;
    };

    struct DirectionalShadowSettings
    {
        HeitzRatioEstimatorShadowSettings ratioEstimator;
    };

    [[nodiscard]] inline constexpr bool
        IsHeitzRatioEstimatorSampleRateSupported(int32_t sampleRateLog2)
    {
        return sampleRateLog2 >=
                HeitzRatioEstimatorMinimumSampleRateLog2 &&
            sampleRateLog2 <=
                HeitzRatioEstimatorMaximumSampleRateLog2;
    }

    [[nodiscard]] inline constexpr uint32_t
        ResolveHeitzRatioEstimatorSampleCount(int32_t sampleRateLog2)
    {
        return IsHeitzRatioEstimatorSampleRateSupported(sampleRateLog2)
            ? 1u << uint32_t(sampleRateLog2)
            : 1u;
    }

    [[nodiscard]] inline constexpr uint32_t ResolveHeitzShadowTraceCount(
        const HeitzRatioEstimatorShadowSettings& settings,
        bool stochastic)
    {
        return stochastic && settings.useRatioEstimator
            ? ResolveHeitzRatioEstimatorSampleCount(
                settings.sampleRateLog2)
            : 1u;
    }

    [[nodiscard]] inline constexpr std::string_view
        GetHeitzRatioEstimatorSampleRateLabel(int32_t sampleRateLog2)
    {
        return IsHeitzRatioEstimatorSampleRateSupported(sampleRateLog2)
            ? HeitzRatioEstimatorSampleRateLabels[std::size_t(
                sampleRateLog2 -
                HeitzRatioEstimatorMinimumSampleRateLog2)]
            : std::string_view{};
    }

    [[nodiscard]] inline constexpr bool
        IsHeitzRatioEstimatorConfigurationSupported(
            const HeitzRatioEstimatorShadowSettings& settings)
    {
        return IsHeitzRatioEstimatorSampleRateSupported(
                settings.sampleRateLog2) &&
            IsValidNoiseSettings(settings.noise.custom) &&
            IsRayVisibilityMaxDistanceSupported(settings.maxDistance) &&
            settings.rayBias >= 0.f &&
            settings.rayBias <= HeitzRatioEstimatorMaximumRayBias;
    }
}
