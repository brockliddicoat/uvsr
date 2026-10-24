#pragma once

#include <cstdint>

namespace uvsr
{
    enum class AntiAliasingQuality : uint32_t
    {
        Low,
        Medium,
        High,
        Ultra,
        Count
    };

    inline constexpr float FastApproximateAaMinimumEdgeSharpness = 2.f;
    inline constexpr float FastApproximateAaMaximumEdgeSharpness = 8.f;
    inline constexpr float FastApproximateAaDefaultEdgeSharpness = 8.f;
    inline constexpr float FastApproximateAaMinimumEdgeThreshold = 0.08f;
    inline constexpr float FastApproximateAaMaximumEdgeThreshold = 0.25f;
    inline constexpr float FastApproximateAaDefaultEdgeThreshold = 0.08f;
    inline constexpr float FastApproximateAaMinimumDarkEdgeThreshold = 0.04f;
    inline constexpr float FastApproximateAaMaximumDarkEdgeThreshold = 0.06f;
    inline constexpr float FastApproximateAaDefaultDarkEdgeThreshold = 0.04f;

    struct FastApproximateAaQualityPreset
    {
        float edgeSharpness;
        float edgeThreshold;
        float darkEdgeThreshold;
    };

    struct FastApproximateAaSettings
    {
        bool enabled = false;
        AntiAliasingQuality quality = AntiAliasingQuality::Ultra;
        float edgeSharpness = FastApproximateAaDefaultEdgeSharpness;
        float edgeThreshold = FastApproximateAaDefaultEdgeThreshold;
        float darkEdgeThreshold =
            FastApproximateAaDefaultDarkEdgeThreshold;

        [[nodiscard]] constexpr bool operator==(
            const FastApproximateAaSettings& other) const
        {
            return enabled == other.enabled &&
                quality == other.quality &&
                edgeSharpness == other.edgeSharpness &&
                edgeThreshold == other.edgeThreshold &&
                darkEdgeThreshold == other.darkEdgeThreshold;
        }

    };

    struct AntiAliasingSettings
    {
        FastApproximateAaSettings fastApproximate;
    };

    struct ResolvedAntiAliasingSettings
    {
        bool fastApproximateEnabled = false;
        float fastApproximateEdgeSharpness = FastApproximateAaDefaultEdgeSharpness;
        float fastApproximateEdgeThreshold = FastApproximateAaDefaultEdgeThreshold;
        float fastApproximateDarkEdgeThreshold = FastApproximateAaDefaultDarkEdgeThreshold;
    };

    [[nodiscard]] inline constexpr AntiAliasingQuality
        SanitizeAntiAliasingQuality(AntiAliasingQuality value)
    {
        return value < AntiAliasingQuality::Count
            ? value
            : AntiAliasingQuality::Medium;
    }

    [[nodiscard]] inline constexpr float ClampFastApproximateAaEdgeSharpness(
        float value)
    {
        return value >= FastApproximateAaMinimumEdgeSharpness
            ? value <= FastApproximateAaMaximumEdgeSharpness
                ? value
                : FastApproximateAaMaximumEdgeSharpness
            : FastApproximateAaMinimumEdgeSharpness;
    }

    [[nodiscard]] inline constexpr float ClampFastApproximateAaEdgeThreshold(
        float value)
    {
        return value >= FastApproximateAaMinimumEdgeThreshold
            ? value <= FastApproximateAaMaximumEdgeThreshold
                ? value
                : FastApproximateAaMaximumEdgeThreshold
            : FastApproximateAaMinimumEdgeThreshold;
    }

    [[nodiscard]] inline constexpr float
        ClampFastApproximateAaDarkEdgeThreshold(float value)
    {
        return value >= FastApproximateAaMinimumDarkEdgeThreshold
            ? value <= FastApproximateAaMaximumDarkEdgeThreshold
                ? value
                : FastApproximateAaMaximumDarkEdgeThreshold
            : FastApproximateAaMinimumDarkEdgeThreshold;
    }

    [[nodiscard]] inline constexpr FastApproximateAaQualityPreset
        GetFastApproximateAaQualityPreset(AntiAliasingQuality quality)
    {
        switch (SanitizeAntiAliasingQuality(quality))
        {
        case AntiAliasingQuality::Low:
            return { 2.f, 0.25f, 0.06f };
        case AntiAliasingQuality::Medium:
            return { 4.f, 0.1875f, 0.055f };
        case AntiAliasingQuality::High:
            return { 8.f, 0.125f, 0.05f };
        case AntiAliasingQuality::Ultra:
            return { 8.f, 0.08f, 0.04f };
        default:
            return { 4.f, 0.1875f, 0.055f };
        }
    }

    inline constexpr void ApplyFastApproximateAaQualityPreset(
        FastApproximateAaSettings& settings,
        AntiAliasingQuality quality)
    {
        settings.quality = SanitizeAntiAliasingQuality(quality);
        const FastApproximateAaQualityPreset preset =
            GetFastApproximateAaQualityPreset(settings.quality);
        settings.edgeSharpness = preset.edgeSharpness;
        settings.edgeThreshold = preset.edgeThreshold;
        settings.darkEdgeThreshold = preset.darkEdgeThreshold;
    }

    [[nodiscard]] inline constexpr bool
        MatchesFastApproximateAaQualityPreset(
            const FastApproximateAaSettings& settings)
    {
        const FastApproximateAaQualityPreset preset =
            GetFastApproximateAaQualityPreset(settings.quality);
        return settings.edgeSharpness == preset.edgeSharpness &&
            settings.edgeThreshold == preset.edgeThreshold &&
            settings.darkEdgeThreshold == preset.darkEdgeThreshold;
    }

    [[nodiscard]] inline constexpr ResolvedAntiAliasingSettings
        ResolveAntiAliasingSettings(const AntiAliasingSettings& settings)
    {
        const auto& fxaa = settings.fastApproximate;
        return { fxaa.enabled,
            ClampFastApproximateAaEdgeSharpness(fxaa.edgeSharpness),
            ClampFastApproximateAaEdgeThreshold(fxaa.edgeThreshold),
            ClampFastApproximateAaDarkEdgeThreshold(fxaa.darkEdgeThreshold) };
    }
}
