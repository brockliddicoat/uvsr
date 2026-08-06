#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    enum class RayTracedSkyVisibilityNoisePattern : uint32_t
    {
        PermutatedWhiteNoise,
        VoidClusterBlueNoise,
        Count
    };

    inline constexpr int32_t RayTracedSkyVisibilityMinimumSampleRateLog2 = 0;
    inline constexpr int32_t RayTracedSkyVisibilityMaximumSampleRateLog2 = 6;
    inline constexpr uint32_t RayTracedSkyVisibilityMaximumSamplesPerPixel =
        1u << uint32_t(RayTracedSkyVisibilityMaximumSampleRateLog2);
    inline constexpr float RayTracedSkyVisibilityMaximumRayBias = 0.1f;
    inline constexpr std::array<std::string_view, 7>
        RayTracedSkyVisibilitySampleRateLabels = {
            "1", "2", "4", "8", "16", "32", "64"
        };

    struct RayTracedSkyVisibilitySettings
    {
        bool enabled = false;
        int32_t sampleRateLog2 = 0;
        float rayBias = 0.002f;
        RayTracedSkyVisibilityNoisePattern noisePattern =
            RayTracedSkyVisibilityNoisePattern::VoidClusterBlueNoise;
        bool animateSamples = true;
    };

    [[nodiscard]] inline constexpr bool
        IsRayTracedSkyVisibilitySampleRateSupported(int32_t sampleRateLog2)
    {
        return sampleRateLog2 >=
                RayTracedSkyVisibilityMinimumSampleRateLog2 &&
            sampleRateLog2 <=
                RayTracedSkyVisibilityMaximumSampleRateLog2;
    }

    [[nodiscard]] inline constexpr bool
        IsRayTracedSkyVisibilityNoisePatternSupported(
            RayTracedSkyVisibilityNoisePattern noisePattern)
    {
        return noisePattern >=
                RayTracedSkyVisibilityNoisePattern::PermutatedWhiteNoise &&
            noisePattern < RayTracedSkyVisibilityNoisePattern::Count;
    }

    [[nodiscard]] inline constexpr uint32_t
        ResolveRayTracedSkyVisibilitySampleCount(int32_t sampleRateLog2)
    {
        return IsRayTracedSkyVisibilitySampleRateSupported(sampleRateLog2)
            ? 1u << uint32_t(sampleRateLog2)
            : 1u;
    }

    [[nodiscard]] inline constexpr std::string_view
        GetRayTracedSkyVisibilitySampleRateLabel(int32_t sampleRateLog2)
    {
        return IsRayTracedSkyVisibilitySampleRateSupported(sampleRateLog2)
            ? RayTracedSkyVisibilitySampleRateLabels[std::size_t(
                sampleRateLog2 -
                RayTracedSkyVisibilityMinimumSampleRateLog2)]
            : std::string_view{};
    }

    [[nodiscard]] inline constexpr bool
        IsRayTracedSkyVisibilityConfigurationSupported(
            const RayTracedSkyVisibilitySettings& settings)
    {
        return IsRayTracedSkyVisibilitySampleRateSupported(
                settings.sampleRateLog2) &&
            IsRayTracedSkyVisibilityNoisePatternSupported(
                settings.noisePattern) &&
            settings.rayBias >= 0.f &&
            settings.rayBias <= RayTracedSkyVisibilityMaximumRayBias;
    }
}
