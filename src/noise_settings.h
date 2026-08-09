#pragma once

#include <cstdint>

namespace uvsr
{
    enum class NoisePattern : uint32_t
    {
        SpatialWhite = 0u,
        SpatialBlue = 1u,
        SpatiotemporalBlue = 2u,
        Count
    };

    enum class NoiseResolution : uint32_t
    {
        Size64 = 64u,
        Size128 = 128u,
        Size256 = 256u,
        Size512 = 512u
    };

    struct NoiseSettings
    {
        NoisePattern pattern = NoisePattern::SpatiotemporalBlue;
        NoiseResolution resolution = NoiseResolution::Size128;
        bool animate = true;
    };

    struct NoiseOverrideSettings
    {
        bool specifyNoise = false;
        NoiseSettings custom = {};
    };

    [[nodiscard]] constexpr bool IsValidNoisePattern(
        NoisePattern pattern)
    {
        return static_cast<uint32_t>(pattern) <
            static_cast<uint32_t>(NoisePattern::Count);
    }

    [[nodiscard]] constexpr bool IsValidNoiseResolution(
        NoiseResolution resolution)
    {
        switch (resolution)
        {
        case NoiseResolution::Size64:
        case NoiseResolution::Size128:
        case NoiseResolution::Size256:
        case NoiseResolution::Size512:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] constexpr uint32_t GetNoiseResolutionValue(
        NoiseResolution resolution)
    {
        return IsValidNoiseResolution(resolution)
            ? static_cast<uint32_t>(resolution)
            : 0u;
    }

    [[nodiscard]] constexpr uint32_t GetNoiseLayerCount(
        NoisePattern pattern)
    {
        switch (pattern)
        {
        case NoisePattern::SpatialWhite:
        case NoisePattern::SpatialBlue:
            return 1u;
        case NoisePattern::SpatiotemporalBlue:
            return 64u;
        default:
            return 0u;
        }
    }

    [[nodiscard]] constexpr const char* GetNoisePatternLabel(
        NoisePattern pattern)
    {
        switch (pattern)
        {
        case NoisePattern::SpatialWhite:
            return "Spatial White";
        case NoisePattern::SpatialBlue:
            return "Spatial Blue";
        case NoisePattern::SpatiotemporalBlue:
            return "Spatiotemporal Blue";
        default:
            return "";
        }
    }

    [[nodiscard]] constexpr const char* GetNoiseResolutionLabel(
        NoiseResolution resolution)
    {
        switch (resolution)
        {
        case NoiseResolution::Size64:
            return "64x64";
        case NoiseResolution::Size128:
            return "128x128";
        case NoiseResolution::Size256:
            return "256x256";
        case NoiseResolution::Size512:
            return "512x512";
        default:
            return "";
        }
    }

    [[nodiscard]] constexpr const char* GetNoiseAssetFileName(
        NoisePattern pattern,
        NoiseResolution resolution)
    {
        switch (pattern)
        {
        case NoisePattern::SpatialWhite:
            switch (resolution)
            {
            case NoiseResolution::Size64:
                return "spatial-white-64x64x1-r8.bin";
            case NoiseResolution::Size128:
                return "spatial-white-128x128x1-r8.bin";
            case NoiseResolution::Size256:
                return "spatial-white-256x256x1-r8.bin";
            case NoiseResolution::Size512:
                return "spatial-white-512x512x1-r8.bin";
            default:
                return "";
            }

        case NoisePattern::SpatialBlue:
            switch (resolution)
            {
            case NoiseResolution::Size64:
                return "spatial-blue-64x64x1-r8.bin";
            case NoiseResolution::Size128:
                return "spatial-blue-128x128x1-r8.bin";
            case NoiseResolution::Size256:
                return "spatial-blue-256x256x1-r8.bin";
            case NoiseResolution::Size512:
                return "spatial-blue-512x512x1-r8.bin";
            default:
                return "";
            }

        case NoisePattern::SpatiotemporalBlue:
            switch (resolution)
            {
            case NoiseResolution::Size64:
                return "spatiotemporal-blue-64x64x64-r8.bin";
            case NoiseResolution::Size128:
                return "spatiotemporal-blue-128x128x64-r8.bin";
            case NoiseResolution::Size256:
                return "spatiotemporal-blue-256x256x64-r8.bin";
            case NoiseResolution::Size512:
                return "spatiotemporal-blue-512x512x64-r8.bin";
            default:
                return "";
            }

        default:
            return "";
        }
    }

    [[nodiscard]] constexpr bool IsValidNoiseSettings(
        const NoiseSettings& settings)
    {
        return IsValidNoisePattern(settings.pattern) &&
            IsValidNoiseResolution(settings.resolution);
    }

    [[nodiscard]] constexpr NoiseSettings ResolveNoiseSettings(
        const NoiseSettings& global,
        const NoiseOverrideSettings& overrideSettings)
    {
        return overrideSettings.specifyNoise
            ? overrideSettings.custom
            : global;
    }

    [[nodiscard]] constexpr bool operator==(
        const NoiseSettings& left,
        const NoiseSettings& right)
    {
        return left.pattern == right.pattern &&
            left.resolution == right.resolution &&
            left.animate == right.animate;
    }

    [[nodiscard]] constexpr bool operator!=(
        const NoiseSettings& left,
        const NoiseSettings& right)
    {
        return !(left == right);
    }

    [[nodiscard]] constexpr bool operator==(
        const NoiseOverrideSettings& left,
        const NoiseOverrideSettings& right)
    {
        return left.specifyNoise == right.specifyNoise &&
            left.custom == right.custom;
    }

    [[nodiscard]] constexpr bool operator!=(
        const NoiseOverrideSettings& left,
        const NoiseOverrideSettings& right)
    {
        return !(left == right);
    }
}
