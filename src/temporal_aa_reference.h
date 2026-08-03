#pragma once

#include "temporal_aa_options.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace uvsr
{
    struct TemporalAaJitterSample
    {
        float x;
        float y;
    };

    inline constexpr std::array<TemporalAaJitterSample, 8>
        TemporalAaHalton23 = {{
            { 0.0f / 8.0f, 0.0f / 9.0f },
            { 4.0f / 8.0f, 3.0f / 9.0f },
            { 2.0f / 8.0f, 6.0f / 9.0f },
            { 6.0f / 8.0f, 1.0f / 9.0f },
            { 1.0f / 8.0f, 4.0f / 9.0f },
            { 5.0f / 8.0f, 7.0f / 9.0f },
            { 3.0f / 8.0f, 2.0f / 9.0f },
            { 7.0f / 8.0f, 5.0f / 9.0f }
        }};

    [[nodiscard]] inline constexpr TemporalAaJitterSample
        GetTemporalAaJitter(uint64_t frameIndex)
    {
        const TemporalAaJitterSample sample =
            TemporalAaHalton23[frameIndex % TemporalAaHalton23.size()];
        return { sample.x - 0.5f, sample.y - 0.5f };
    }

    [[nodiscard]] inline constexpr TemporalAaJitterSample
        GetTemporalAaCurrentToPreviousJitter(
            TemporalAaJitterSample currentJitter,
            TemporalAaJitterSample previousJitter)
    {
        return {
            previousJitter.x - currentJitter.x,
            previousJitter.y - currentJitter.y
        };
    }

    [[nodiscard]] inline bool IsTemporalAaMotionValid(
        const std::array<float, 4>& packedMotion)
    {
        return packedMotion[3] > 0.5f &&
            std::all_of(
                packedMotion.begin(),
                packedMotion.end(),
                [](float value) { return std::isfinite(value); });
    }

    struct TemporalAaReverseZFootprint
    {
        float farthestValidDeviceDepth = 1.f;
        float nearestValidDeviceDepth = 0.f;
        uint32_t validMask = 0u;
        uint32_t backgroundMask = 0u;
    };

    [[nodiscard]] inline TemporalAaReverseZFootprint
        ReduceTemporalAaReverseZFootprint(
            const std::array<float, 4>& deviceDepths)
    {
        TemporalAaReverseZFootprint result;
        for (uint32_t lane = 0u; lane < deviceDepths.size(); ++lane)
        {
            const float depth = deviceDepths[lane];
            if (std::isfinite(depth) && depth == 0.f)
                result.backgroundMask |= 1u << lane;
            if (std::isfinite(depth) && depth > 0.f && depth <= 1.f)
            {
                result.validMask |= 1u << lane;
                result.farthestValidDeviceDepth =
                    std::min(result.farthestValidDeviceDepth, depth);
                result.nearestValidDeviceDepth =
                    std::max(result.nearestValidDeviceDepth, depth);
            }
        }
        return result;
    }

    [[nodiscard]] inline constexpr bool
        TemporalAaFootprintHasConsistentGeometry(
            const TemporalAaReverseZFootprint& footprint)
    {
        return footprint.validMask == 0xfu &&
            footprint.backgroundMask == 0u;
    }

    [[nodiscard]] inline constexpr uint64_t
        GetTemporalAaHistoryBytes(uint32_t width, uint32_t height)
    {
        // Two RGBA16F color histories plus two R32F depth histories.
        return uint64_t(width) * uint64_t(height) * 24u;
    }

    [[nodiscard]] inline constexpr uint64_t
        GetTemporalAaMinimumHistoryBytes(
            uint32_t width,
            uint32_t height,
            uint32_t colorBytesPerPixel,
            uint32_t depthBytesPerPixel)
    {
        return uint64_t(width) * uint64_t(height) * 2u *
            uint64_t(colorBytesPerPixel + depthBytesPerPixel);
    }

    [[nodiscard]] inline constexpr uint64_t
        GetTemporalAaResidentHistoryBytes(
            uint32_t width,
            uint32_t height,
            uint32_t minimumColorBytesPerPixel,
            uint32_t minimumDepthBytesPerPixel)
    {
        return GetTemporalAaHistoryBytes(width, height) +
            GetTemporalAaMinimumHistoryBytes(
                width,
                height,
                minimumColorBytesPerPixel,
                minimumDepthBytesPerPixel);
    }

    inline constexpr float TemporalAaDefaultSharpness = 0.5f;
    inline constexpr float TemporalAaMinimumSharpness = 0.f;
    inline constexpr float TemporalAaMaximumSharpness = 1.f;
    inline constexpr float TemporalAaSharpenThreshold = 0.001f;

    struct TemporalAaSharpenWeights
    {
        float center;
        float lateral;
    };

    [[nodiscard]] inline constexpr float ClampTemporalAaSharpness(
        float sharpness)
    {
        return sharpness < TemporalAaMinimumSharpness
            ? TemporalAaMinimumSharpness
            : sharpness > TemporalAaMaximumSharpness
                ? TemporalAaMaximumSharpness
                : sharpness;
    }

    [[nodiscard]] inline constexpr bool ShouldSharpenTemporalAa(
        bool enabled,
        float sharpness)
    {
        return enabled &&
            ClampTemporalAaSharpness(sharpness) >=
                TemporalAaSharpenThreshold;
    }

    [[nodiscard]] inline constexpr TemporalAaSharpenWeights
        GetTemporalAaSharpenWeights(float sharpness)
    {
        const float clamped = ClampTemporalAaSharpness(sharpness);
        return { 1.f + clamped, 0.25f * clamped };
    }

}
