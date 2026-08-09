#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace uvsr
{
    enum class DenoisingEffect : uint8_t
    {
        AmbientOcclusion,
        DiffuseGi,
        Shadows,
        SkyVisibility
    };

    enum class DenoisingMethodChoice : uint8_t
    {
        None,
        Reblur,
        Relax,
        Sigma
    };

    enum class DenoisingQuality : uint8_t
    {
        Performance,
        Balanced,
        Quality,
        Ultra
    };

    enum class DenoisingResolution : uint8_t
    {
        Quarter,
        Half,
        Full
    };

    struct DenoisingSignalSettings
    {
        DenoisingMethodChoice method = DenoisingMethodChoice::None;
        DenoisingQuality quality = DenoisingQuality::Balanced;
        DenoisingResolution resolution = DenoisingResolution::Half;
        uint32_t historyLength = 16;
        float disocclusionThreshold = 0.01f;
        float antiLagStrength = 0.5f;

        [[nodiscard]] bool operator==(
            const DenoisingSignalSettings& other) const noexcept
        {
            return method == other.method &&
                quality == other.quality &&
                resolution == other.resolution &&
                historyLength == other.historyLength &&
                disocclusionThreshold == other.disocclusionThreshold &&
                antiLagStrength == other.antiLagStrength;
        }

        [[nodiscard]] bool operator!=(
            const DenoisingSignalSettings& other) const noexcept
        {
            return !(*this == other);
        }
    };

    struct DenoisingSettings
    {
        DenoisingSignalSettings ambientOcclusion;
        DenoisingSignalSettings diffuseGi;
        DenoisingSignalSettings shadows;
        DenoisingSignalSettings skyVisibility;
    };

    [[nodiscard]] constexpr float GetDenoisingResolutionScale(
        DenoisingResolution resolution) noexcept
    {
        switch (resolution)
        {
        case DenoisingResolution::Quarter: return 0.25f;
        case DenoisingResolution::Full: return 1.f;
        default: return 0.5f;
        }
    }

    [[nodiscard]] constexpr bool SupportsDenoisingMethod(
        DenoisingEffect effect,
        DenoisingMethodChoice method) noexcept
    {
        if (method == DenoisingMethodChoice::None)
            return true;

        switch (effect)
        {
        case DenoisingEffect::AmbientOcclusion:
            return method == DenoisingMethodChoice::Reblur;
        case DenoisingEffect::DiffuseGi:
        case DenoisingEffect::SkyVisibility:
            return method == DenoisingMethodChoice::Reblur ||
                method == DenoisingMethodChoice::Relax;
        case DenoisingEffect::Shadows:
            return method == DenoisingMethodChoice::Sigma;
        default:
            return false;
        }
    }

    [[nodiscard]] constexpr const char* GetDenoisingMethodLabel(
        DenoisingMethodChoice method) noexcept
    {
        switch (method)
        {
        case DenoisingMethodChoice::Reblur: return "ReBLUR";
        case DenoisingMethodChoice::Relax: return "ReLAX";
        case DenoisingMethodChoice::Sigma: return "SIGMA";
        default: return "None";
        }
    }

    [[nodiscard]] constexpr const char* GetDenoisingQualityLabel(
        DenoisingQuality quality) noexcept
    {
        switch (quality)
        {
        case DenoisingQuality::Performance: return "Performance";
        case DenoisingQuality::Quality: return "Quality";
        case DenoisingQuality::Ultra: return "Ultra";
        default: return "Balanced";
        }
    }

    [[nodiscard]] constexpr const char* GetDenoisingResolutionLabel(
        DenoisingResolution resolution) noexcept
    {
        switch (resolution)
        {
        case DenoisingResolution::Quarter: return "Quarter";
        case DenoisingResolution::Full: return "Full";
        default: return "Half";
        }
    }

    [[nodiscard]] inline DenoisingSignalSettings SanitizeDenoisingSettings(
        DenoisingEffect effect,
        DenoisingSignalSettings settings) noexcept
    {
        if (!SupportsDenoisingMethod(effect, settings.method))
            settings.method = DenoisingMethodChoice::None;
        if (settings.quality > DenoisingQuality::Ultra)
            settings.quality = DenoisingQuality::Balanced;
        if (settings.resolution > DenoisingResolution::Full)
            settings.resolution = DenoisingResolution::Half;
        settings.historyLength = std::clamp(settings.historyLength, 1u, 255u);
        settings.disocclusionThreshold =
            std::isfinite(settings.disocclusionThreshold)
            ? std::clamp(settings.disocclusionThreshold, 0.001f, 0.1f)
            : 0.01f;
        settings.antiLagStrength = std::isfinite(settings.antiLagStrength)
            ? std::clamp(settings.antiLagStrength, 0.f, 1.f)
            : 0.5f;
        return settings;
    }
}
