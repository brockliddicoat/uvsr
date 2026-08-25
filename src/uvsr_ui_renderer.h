#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace uvsr
{
    inline constexpr float UiBackgroundBlurPixels = 4.f;
    inline constexpr float UiPanelShadowBlurPixels = 10.f;
    inline constexpr float UiPanelShadowOpacity = 0.34f;
    inline constexpr float UiPanelShadowOffsetYPixels = 3.f;
    inline constexpr std::size_t UiPerformanceTitleBackdropIndex = 0u;
    inline constexpr std::size_t UiPerformanceBodyBackdropIndex = 1u;
    inline constexpr std::size_t UiSettingsTitleBackdropIndex = 2u;
    inline constexpr std::size_t UiSettingsBodyBackdropIndex = 3u;
    inline constexpr std::size_t UiCommandBackdropIndex = 4u;
    inline constexpr std::size_t UiBackdropRectCount = 5u;
    inline constexpr std::uint32_t UiBackdropCornersAll = 0xFu;

    struct UiBackdropExclusionRect
    {
        float minX = 0.f;
        float minY = 0.f;
        float maxX = 0.f;
        float maxY = 0.f;
    };

    struct UiBackdropRect
    {
        float minX = 0.f;
        float minY = 0.f;
        float maxX = 0.f;
        float maxY = 0.f;
        float rounding = 0.f;
        std::uint32_t cornerMask = UiBackdropCornersAll;
        float opacity = 1.f;
        float shadowBlur = 0.f;
        float shadowOpacity = 0.f;
        float shadowOffsetY = 0.f;
        std::vector<UiBackdropExclusionRect> compositeExclusions;
        bool composite = true;
        bool visible = false;
    };
}
