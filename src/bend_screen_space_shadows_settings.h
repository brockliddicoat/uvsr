#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace uvsr
{
    enum class BendShadowPreset : uint32_t
    {
        BendExact,
        Long,
        MaximumValidation,
        Custom
    };

    enum class BendShadowLength : uint32_t
    {
        Pixels60 = 60,
        Pixels120 = 120,
        Pixels240 = 240,
        Pixels480 = 480,
        Pixels960 = 960
    };

    enum class BendShadowDebugView : uint32_t
    {
        None,
        Edge,
        Thread,
        Wave
    };

    inline constexpr std::array<uint32_t, 5> BendShadowSampleCounts = {
        60u, 120u, 240u, 480u, 960u
    };
    inline constexpr std::array<uint32_t, 3> BendShadowHardSampleCounts = {
        0u, 4u, 8u
    };
    inline constexpr std::array<uint32_t, 3> BendShadowFadeSampleCounts = {
        0u, 8u, 16u
    };

    struct BendScreenSpaceShadowSettings
    {
        bool enabled = false;
        BendShadowPreset preset = BendShadowPreset::BendExact;
        BendShadowLength length = BendShadowLength::Pixels60;
        float surfaceThickness = 0.005f;
        float bilinearThreshold = 0.02f;
        float shadowContrast = 4.f;
        uint32_t hardShadowSamples = 4u;
        uint32_t fadeOutSamples = 8u;
        bool ignoreEdgePixels = false;
        bool usePrecisionOffset = false;
        bool bilinearSamplingOffsetMode = false;
        bool useEarlyOut = false;
        BendShadowDebugView debugView = BendShadowDebugView::None;
    };

    [[nodiscard]] inline constexpr uint32_t GetBendShadowSampleCount(
        BendShadowLength length)
    {
        return static_cast<uint32_t>(length);
    }

    template <size_t Size>
    [[nodiscard]] inline constexpr int FindBendShadowCompiledValue(
        const std::array<uint32_t, Size>& values,
        uint32_t value)
    {
        for (size_t index = 0; index < values.size(); ++index)
        {
            if (values[index] == value)
                return static_cast<int>(index);
        }
        return -1;
    }

    [[nodiscard]] inline constexpr bool IsBendShadowVariantCompiled(
        const BendScreenSpaceShadowSettings& settings)
    {
        const uint32_t sampleCount =
            GetBendShadowSampleCount(settings.length);
        return FindBendShadowCompiledValue(
                   BendShadowSampleCounts, sampleCount) >= 0 &&
               FindBendShadowCompiledValue(
                   BendShadowHardSampleCounts,
                   settings.hardShadowSamples) >= 0 &&
               FindBendShadowCompiledValue(
                   BendShadowFadeSampleCounts,
                   settings.fadeOutSamples) >= 0 &&
               settings.hardShadowSamples + settings.fadeOutSamples <=
                   sampleCount;
    }

    inline void ApplyBendShadowPreset(
        BendScreenSpaceShadowSettings& settings,
        BendShadowPreset preset)
    {
        if (preset == BendShadowPreset::Custom)
        {
            settings.preset = preset;
            return;
        }

        const bool enabled = settings.enabled;
        settings = BendScreenSpaceShadowSettings{};
        settings.enabled = enabled;
        settings.preset = preset;

        if (preset == BendShadowPreset::Long)
            settings.length = BendShadowLength::Pixels240;
        else if (preset == BendShadowPreset::MaximumValidation)
            settings.length = BendShadowLength::Pixels960;
    }
}
