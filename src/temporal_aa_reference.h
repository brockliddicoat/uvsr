#pragma once

#include "temporal_aa_options.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace uvsr
{
    struct TemporalAaJitterSample
    {
        float x;
        float y;
    };

    inline constexpr std::array<TemporalAaJitterSample, 4>
        TemporalAaRotatedGrid4 = {{
            { 0.125f, -0.375f },
            { -0.375f, -0.125f },
            { 0.375f, 0.125f },
            { -0.125f, 0.375f }
        }};

    inline constexpr std::array<TemporalAaJitterSample, 4>
        TemporalAaUniformHelix4 = {{
            { -0.25f, -0.25f },
            { 0.25f, 0.25f },
            { -0.25f, 0.25f },
            { 0.25f, -0.25f }
        }};

    // Filament revision 47c86eec skips 409 radical-inverse entries before
    // constructing its shared Halton (2,3) table.
    [[nodiscard]] inline constexpr float TemporalAaRadicalInverse(
        uint32_t index,
        uint32_t base)
    {
        float factor = 1.f;
        float result = 0.f;
        while (index > 0u)
        {
            factor /= static_cast<float>(base);
            result += factor * static_cast<float>(index % base);
            index /= base;
        }
        return result;
    }

    template<std::size_t Count>
    [[nodiscard]] inline constexpr std::array<
        TemporalAaJitterSample, Count> MakeTemporalAaFilamentHalton23()
    {
        std::array<TemporalAaJitterSample, Count> result{};
        for (std::size_t index = 0; index < Count; ++index)
        {
            const uint32_t filamentIndex =
                static_cast<uint32_t>(index) + 409u;
            result[index] = {
                TemporalAaRadicalInverse(filamentIndex, 2u) - 0.5f,
                TemporalAaRadicalInverse(filamentIndex, 3u) - 0.5f
            };
        }
        return result;
    }

    inline constexpr auto TemporalAaFilamentHalton23 =
        MakeTemporalAaFilamentHalton23<32>();

    // Generated once from Helmer, Christensen, and Kensler's stochastic Sobol
    // (0,2) best-candidate generator at f90b1158. Replace the generator's RNG
    // with `RNG rng(43);`, then run `generate_samples --seq=ssobol --n=32
    // --nd=2 --bn2d` and subtract 0.5 from each coordinate. The seed produces
    // the first point directly. For each later point, --bn2d evaluates 100
    // candidates in the required Sobol stratum and maximizes minimum toroidal
    // distance to the existing points. The fixed table removes runtime state.
    inline constexpr std::array<TemporalAaJitterSample, 32>
        TemporalAaSobol32 = {{
            { -0.163727030f, -0.150207385f },
            { 0.336629748f, 0.339536399f },
            { -0.269704461f, 0.230351552f },
            { 0.225300357f, -0.269102067f },
            { -0.00180649897f, 0.460878044f },
            { 0.477911383f, -0.0206133239f },
            { -0.428817034f, -0.392376602f },
            { 0.0973897949f, 0.0859183744f },
            { -0.246421888f, 0.0318495929f },
            { 0.312270671f, -0.456435293f },
            { -0.358226746f, -0.124909990f },
            { 0.161113590f, 0.383117408f },
            { -0.116007984f, -0.359382242f },
            { 0.379556715f, 0.149445280f },
            { -0.467211872f, 0.285125732f },
            { 0.0295942873f, -0.211078987f },
            { -0.152378082f, 0.365998149f },
            { 0.374861598f, -0.168332800f },
            { -0.282235920f, -0.291959941f },
            { 0.217606395f, 0.212779120f },
            { -0.0323606580f, -0.0334762856f },
            { 0.466017842f, 0.473048568f },
            { -0.405805230f, 0.116170175f },
            { 0.0930275545f, -0.407893479f },
            { -0.218505859f, -0.487654001f },
            { 0.265011579f, 0.00385231944f },
            { -0.342283189f, 0.407048643f },
            { 0.125823259f, -0.0796916559f },
            { -0.0762585402f, 0.182998121f },
            { 0.418778718f, -0.328401715f },
            { -0.473106205f, -0.222323194f },
            { 0.0601801984f, 0.260530889f }
        }};

    [[nodiscard]] inline constexpr uint32_t
        GetTemporalAaJitterSequenceLength(TemporalAaJitterSequence sequence)
    {
        switch (SanitizeTemporalAaJitterSequence(sequence))
        {
        case TemporalAaJitterSequence::RotatedGrid4:
        case TemporalAaJitterSequence::UniformHelix4:
            return 4u;
        case TemporalAaJitterSequence::Halton23x8:
            return 8u;
        case TemporalAaJitterSequence::Halton23x16:
            return 16u;
        case TemporalAaJitterSequence::Halton23x32:
        case TemporalAaJitterSequence::Sobol32:
            return 32u;
        default:
            return 16u;
        }
    }

    template<std::size_t Count>
    [[nodiscard]] inline constexpr TemporalAaJitterSample
        GetCyclicTemporalAaJitter(
            const std::array<TemporalAaJitterSample, Count>& sequence,
            uint64_t frameIndex)
    {
        return sequence[frameIndex % Count];
    }

    [[nodiscard]] inline constexpr TemporalAaJitterSample
        GetTemporalAaJitter(
            TemporalAaJitterSequence sequence,
            uint64_t frameIndex)
    {
        switch (SanitizeTemporalAaJitterSequence(sequence))
        {
        case TemporalAaJitterSequence::RotatedGrid4:
            return GetCyclicTemporalAaJitter(
                TemporalAaRotatedGrid4, frameIndex);
        case TemporalAaJitterSequence::UniformHelix4:
            return GetCyclicTemporalAaJitter(
                TemporalAaUniformHelix4, frameIndex);
        case TemporalAaJitterSequence::Halton23x8:
            return TemporalAaFilamentHalton23[frameIndex % 8u];
        case TemporalAaJitterSequence::Halton23x16:
            return TemporalAaFilamentHalton23[frameIndex % 16u];
        case TemporalAaJitterSequence::Halton23x32:
            return GetCyclicTemporalAaJitter(
                TemporalAaFilamentHalton23, frameIndex);
        case TemporalAaJitterSequence::Sobol32:
            return GetCyclicTemporalAaJitter(
                TemporalAaSobol32, frameIndex);
        default:
            return TemporalAaFilamentHalton23[frameIndex % 16u];
        }
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

    [[nodiscard]] inline bool TemporalAaViewDepthAccepted(
        float expectedViewDepth,
        float previousViewDepth,
        float nearerViewAllowance,
        float fartherViewAllowance,
        float baseViewDepthAllowance = 1e-3f)
    {
        if (!std::isfinite(expectedViewDepth) ||
            !std::isfinite(previousViewDepth) ||
            expectedViewDepth <= 0.f ||
            previousViewDepth <= 0.f)
        {
            return false;
        }
        const float baseAllowance = std::max(
            baseViewDepthAllowance,
            0.f);
        return expectedViewDepth <=
                previousViewDepth + baseAllowance + nearerViewAllowance &&
            previousViewDepth <=
                expectedViewDepth + baseAllowance + fartherViewAllowance;
    }

    [[nodiscard]] inline float TemporalAaViewDepthFootprintCoherence(
        float nearestFootprintViewDepth,
        float farthestFootprintViewDepth)
    {
        if (!std::isfinite(nearestFootprintViewDepth) ||
            !std::isfinite(farthestFootprintViewDepth) ||
            nearestFootprintViewDepth <= 0.f ||
            farthestFootprintViewDepth < nearestFootprintViewDepth)
        {
            return 0.f;
        }

        const float relativeRange =
            (farthestFootprintViewDepth - nearestFootprintViewDepth) /
            std::max(nearestFootprintViewDepth, 1e-3f);
        const float t = std::clamp(
            (relativeRange - 0.005f) / (0.05f - 0.005f),
            0.f,
            1.f);
        const float smooth = t * t * (3.f - 2.f * t);
        return 1.f - smooth;
    }

    [[nodiscard]] inline bool TemporalAaViewDepthFootprintAccepted(
        float expectedViewDepth,
        float filteredPreviousViewDepth,
        float nearestFootprintViewDepth,
        float farthestFootprintViewDepth,
        float nearerViewAllowance = 0.f,
        float fartherViewAllowance = 0.f,
        float baseViewDepthAllowance = 1e-3f)
    {
        return TemporalAaViewDepthFootprintCoherence(
                nearestFootprintViewDepth,
                farthestFootprintViewDepth) > 0.f &&
            TemporalAaViewDepthAccepted(
                expectedViewDepth,
                filteredPreviousViewDepth,
                nearerViewAllowance,
                fartherViewAllowance,
                baseViewDepthAllowance);
    }

    [[nodiscard]] inline float RoundTripTemporalAaPositiveBinary16(
        float value)
    {
        if (!std::isfinite(value) || value <= 0.f)
            return value == 0.f ? 0.f : 65504.f;

        constexpr float maximumHalf = 65504.f;
        constexpr float subnormalSpacing =
            0.000000059604644775390625f;
        if (value >= maximumHalf)
            return maximumHalf;

        const int exponent = std::ilogb(value);
        const float spacing = exponent < -14
            ? subnormalSpacing
            : std::ldexp(1.f, exponent - 10);
        const double scaled = double(value) / double(spacing);
        const double lower = std::floor(scaled);
        const double fraction = scaled - lower;
        const bool roundUp = fraction > 0.5 ||
            (fraction == 0.5 &&
                std::fmod(lower, 2.0) != 0.0);
        return float(
            (lower + (roundUp ? 1.0 : 0.0)) * double(spacing));
    }

    [[nodiscard]] inline float GetTemporalAaStoredDeviceDepthError(
        float deviceDepth,
        bool storedAsBinary16)
    {
        if (!storedAsBinary16)
            return 0.f;
        if (!std::isfinite(deviceDepth) || deviceDepth <= 0.f)
        {
            return std::numeric_limits<float>::infinity();
        }
        return std::max(
            deviceDepth * 0.00048828125f,
            0.0000000298023223876953125f);
    }

    struct TemporalAaViewDepthAllowances
    {
        float nearer = 0.f;
        float farther = 0.f;
    };

    [[nodiscard]] inline TemporalAaViewDepthAllowances
        GetTemporalAaInfiniteReverseZViewAllowances(
            float expectedDeviceDepth,
            float deviceDepthError,
            float nearPlane = 0.1f)
    {
        TemporalAaViewDepthAllowances result;
        if (!std::isfinite(expectedDeviceDepth) ||
            expectedDeviceDepth <= 0.f ||
            expectedDeviceDepth > 1.f ||
            !std::isfinite(deviceDepthError) ||
            deviceDepthError < 0.f ||
            !std::isfinite(nearPlane) ||
            nearPlane <= 0.f)
        {
            return result;
        }

        const float expectedViewDepth =
            nearPlane / expectedDeviceDepth;
        const float nearestPlausibleDeviceDepth = std::min(
            expectedDeviceDepth + deviceDepthError,
            1.f);
        result.nearer = std::max(
            expectedViewDepth -
                nearPlane / nearestPlausibleDeviceDepth,
            0.f);
        if (deviceDepthError < expectedDeviceDepth)
        {
            result.farther = std::max(
                nearPlane /
                    (expectedDeviceDepth - deviceDepthError) -
                    expectedViewDepth,
                0.f);
        }
        else
        {
            result.farther = 65504.f;
        }
        return result;
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
