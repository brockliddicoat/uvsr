#pragma once

#include <algorithm>

namespace uvsr
{
    struct UiCommandLayoutRect
    {
        float minX = 0.f;
        float minY = 0.f;
        float maxX = 0.f;
        float maxY = 0.f;
    };

    struct CommandInterfaceLayout
    {
        float left = 0.f;
        float top = 0.f;
        float bottom = 0.f;
        float width = 0.f;
        float height = 0.f;
        float settingsMaximumBottom = 0.f;
        bool compact = false;
        bool fits = false;
    };

    [[nodiscard]] constexpr bool IsUiCommandLayoutRectValid(
        const UiCommandLayoutRect& rectangle)
    {
        return rectangle.maxX > rectangle.minX &&
            rectangle.maxY > rectangle.minY;
    }

    [[nodiscard]] constexpr UiCommandLayoutRect
        ResolveCommandInterfaceRect(
            const CommandInterfaceLayout& layout)
    {
        return {
            layout.left,
            layout.top,
            layout.left + layout.width,
            layout.bottom
        };
    }

    // Reserve one full-width command lane in the same coordinate space as
    // ImGui's main viewport. Settings may use vertical space only through
    // settingsMaximumBottom, leaving one additional margin above the command
    // lane. The reservation is independent of Settings visibility so opening
    // either surface never moves the other and same-frame command output cannot
    // create an overlap. "compactWidth" classifies compact presentation while
    // "minimumRenderableWidth" and "minimumSettingsHeight" account for ImGui's
    // enforced root-window envelopes.
    [[nodiscard]] constexpr CommandInterfaceLayout
        ResolveCommandInterfaceLayout(
            const UiCommandLayoutRect& workRectangle,
            float margin,
            float compactWidth,
            float minimumRenderableWidth,
            float reservedHeight,
            float minimumCommandHeight,
            float minimumSettingsHeight)
    {
        const float safeMargin = std::max(0.f, margin);
        const float safeCompactWidth =
            std::max(0.f, compactWidth);
        const float safeMinimumRenderableWidth =
            std::max(0.f, minimumRenderableWidth);
        const float safeMinimumCommandHeight =
            std::max(0.f, minimumCommandHeight);
        const float safeMinimumSettingsHeight =
            std::max(0.f, minimumSettingsHeight);
        const float safeReservedHeight =
            std::max(
                safeMinimumCommandHeight,
                std::max(0.f, reservedHeight));
        const float innerLeft =
            workRectangle.minX + safeMargin;
        const float innerRight =
            workRectangle.maxX - safeMargin;
        const float innerTop =
            workRectangle.minY + safeMargin;
        const float innerBottom =
            workRectangle.maxY - safeMargin;

        CommandInterfaceLayout result;
        result.left = innerLeft;
        result.bottom = innerBottom;
        result.width = std::max(
            0.f,
            innerRight - innerLeft);
        result.compact =
            result.width < safeCompactWidth;

        const float availableHeight = std::max(
            0.f,
            innerBottom - innerTop);
        result.height = std::min(
            safeReservedHeight,
            availableHeight);
        result.top = result.bottom - result.height;
        result.settingsMaximumBottom = std::max(
            innerTop,
            result.top - safeMargin);

        const bool workRectangleValid =
            IsUiCommandLayoutRectValid(workRectangle);
        const float resultRight = result.left + result.width;
        const bool horizontallyInside =
            result.left >= innerLeft &&
            resultRight <= innerRight;
        const float settingsAvailableHeight =
            result.settingsMaximumBottom - innerTop;
        result.fits =
            workRectangleValid &&
            result.width >= safeMinimumRenderableWidth &&
            result.height >= safeMinimumCommandHeight &&
            settingsAvailableHeight >= safeMinimumSettingsHeight &&
            horizontallyInside;
        return result;
    }
}
