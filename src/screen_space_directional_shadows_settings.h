#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace uvsr
{
    enum class ScreenSpaceShadowPreset : uint32_t
    {
        Default,
        Long,
        MaximumValidation,
        Custom
    };

    enum class ScreenSpaceShadowLength : uint32_t
    {
        Pixels60 = 60,
        Pixels120 = 120,
        Pixels240 = 240,
        Pixels480 = 480,
        Pixels960 = 960
    };

    enum class ScreenSpaceShadowDebugView : uint32_t
    {
        None,
        Edge,
        Thread,
        Wave
    };

    inline constexpr std::array<uint32_t, 5>
        ScreenSpaceShadowTraceReaches = {
            60u, 120u, 240u, 480u, 960u
        };
    inline constexpr std::array<uint32_t, 3>
        ScreenSpaceShadowHardSampleCounts = {
            0u, 4u, 8u
        };
    inline constexpr std::array<uint32_t, 3>
        ScreenSpaceShadowFadeSampleCounts = {
            0u, 8u, 16u
        };

    struct ScreenSpaceDirectionalShadowSettings
    {
        bool enabled = false;
        ScreenSpaceShadowPreset preset = ScreenSpaceShadowPreset::Default;
        ScreenSpaceShadowLength length =
            ScreenSpaceShadowLength::Pixels60;
        float surfaceThickness = 0.005f;
        float bilinearThreshold = 0.02f;
        float shadowContrast = 4.f;
        uint32_t hardShadowSamples = 4u;
        uint32_t fadeOutSamples = 8u;
        bool ignoreEdgePixels = false;
        bool usePrecisionOffset = false;
        bool bilinearSamplingOffsetMode = false;
        bool useEarlyOut = false;
        ScreenSpaceShadowDebugView debugView =
            ScreenSpaceShadowDebugView::None;
    };

    [[nodiscard]] inline constexpr uint32_t
        GetScreenSpaceShadowTraceReach(
            ScreenSpaceShadowLength length)
    {
        return static_cast<uint32_t>(length);
    }

    template <size_t Size>
    [[nodiscard]] inline constexpr int
        FindScreenSpaceShadowSupportedValue(
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

    [[nodiscard]] inline constexpr bool
        IsScreenSpaceShadowConfigurationSupported(
            const ScreenSpaceDirectionalShadowSettings& settings)
    {
        return FindScreenSpaceShadowSupportedValue(
                   ScreenSpaceShadowTraceReaches,
                   GetScreenSpaceShadowTraceReach(settings.length)) >= 0 &&
               FindScreenSpaceShadowSupportedValue(
                   ScreenSpaceShadowHardSampleCounts,
                   settings.hardShadowSamples) >= 0 &&
               FindScreenSpaceShadowSupportedValue(
                   ScreenSpaceShadowFadeSampleCounts,
                   settings.fadeOutSamples) >= 0 &&
               settings.hardShadowSamples +
                       settings.fadeOutSamples <=
                   GetScreenSpaceShadowTraceReach(settings.length);
    }

    inline void ApplyScreenSpaceShadowPreset(
        ScreenSpaceDirectionalShadowSettings& settings,
        ScreenSpaceShadowPreset preset)
    {
        if (preset == ScreenSpaceShadowPreset::Custom)
        {
            settings.preset = preset;
            return;
        }

        const bool enabled = settings.enabled;
        settings = ScreenSpaceDirectionalShadowSettings{};
        settings.enabled = enabled;
        settings.preset = preset;

        if (preset == ScreenSpaceShadowPreset::Long)
            settings.length = ScreenSpaceShadowLength::Pixels240;
        else if (preset == ScreenSpaceShadowPreset::MaximumValidation)
            settings.length = ScreenSpaceShadowLength::Pixels960;
    }
}
