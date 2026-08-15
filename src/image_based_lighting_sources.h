#pragma once

#include <array>
#include <cstdint>

namespace uvsr
{
    enum class ImageBasedLightingSource : uint32_t
    {
        Kloppenheim03Day,
        SnowField2BrightOvercast,
        FarmFieldSoftDay,
        Kloppenheim07Night,
        QwantaniStarryNight,
        QuadrangleCloudyLegacy,
        Count
    };

    struct ImageBasedLightingSourceInfo
    {
        const char* displayName = "";
        const char* relativePath = "";
        float defaultExposureStops = 0.f;
        bool night = false;
    };

    inline constexpr std::array<
        ImageBasedLightingSourceInfo,
        uint32_t(ImageBasedLightingSource::Count)>
        ImageBasedLightingSourceCatalog = {{
        {
            "Day",
            "kloppenheim_03_puresky/kloppenheim_03_puresky_2k.hdr",
            -2.75f,
            false
        },
        {
            "Bright Overcast",
            "snow_field_2_puresky/snow_field_2_puresky_2k.hdr",
            -2.5f,
            false
        },
        {
            "Soft Day",
            "farm_field_puresky/farm_field_puresky_2k.hdr",
            -3.25f,
            false
        },
        {
            "Night",
            "kloppenheim_07_puresky/kloppenheim_07_puresky_2k.hdr",
            -5.f,
            true
        },
        {
            "Starry Night",
            "qwantani_night_puresky/qwantani_night_puresky_2k.hdr",
            -6.5f,
            true
        },
        {
            "Cloudy",
            "quadrangle_cloudy/quadrangle_cloudy_1k.hdr",
            -3.f,
            false
        }
    }};

    [[nodiscard]] inline constexpr const ImageBasedLightingSourceInfo&
        GetImageBasedLightingSourceInfo(ImageBasedLightingSource source)
    {
        const uint32_t index = uint32_t(source);
        return ImageBasedLightingSourceCatalog[
            index < ImageBasedLightingSourceCatalog.size() ? index : 0u];
    }
}
