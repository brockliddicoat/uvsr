#pragma once

#include <algorithm>

namespace uvsr
{
    inline constexpr float UiMinimumDisplayScale = 0.5f;
    inline constexpr float UiMaximumDisplayScale = 4.f;
    inline constexpr float UiSpacingBasePixels = 4.f;

    struct UiSpacingTokens
    {
        float tight = UiSpacingBasePixels;
        float regular = UiSpacingBasePixels * 2.f;
        float section = UiSpacingBasePixels * 4.f;
    };

    struct UiLayoutRect
    {
        float minX = 0.f;
        float minY = 0.f;
        float maxX = 0.f;
        float maxY = 0.f;
    };

    [[nodiscard]] constexpr UiSpacingTokens ResolveUiSpacingTokens(
        float displayScale)
    {
        const float tight = UiSpacingBasePixels *
            std::clamp(displayScale, UiMinimumDisplayScale, UiMaximumDisplayScale);
        return { tight, tight * 2.f, tight * 4.f };
    }

    [[nodiscard]] constexpr float ResolvePerformanceMaximumWindowHeight(
        float stackMaximumBottom,
        float performanceWindowTop,
        float settingsBodyReserve,
        float panelGap)
    {
        return std::max(
            1.f,
            stackMaximumBottom - performanceWindowTop -
                std::max(0.f, settingsBodyReserve) -
                std::max(0.f, panelGap));
    }
}
