#pragma once

#include <algorithm>

namespace uvsr
{
    inline constexpr float UiSpacingBasePixels = 4.f;

    struct UiSpacingTokens
    {
        float tight = UiSpacingBasePixels;
        float regular = UiSpacingBasePixels * 2.f;
        float section = UiSpacingBasePixels * 4.f;
    };

    [[nodiscard]] constexpr UiSpacingTokens ResolveUiSpacingTokens(
        float displayScale)
    {
        const float safeScale = std::clamp(displayScale, 0.5f, 4.f);
        const float tight = UiSpacingBasePixels * safeScale;
        return { tight, tight * 2.f, tight * 4.f };
    }

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

    // The Performance and Settings stack uses the full work area while the
    // command interface is closed, then eases toward the reserved command-lane
    // boundary as the interface appears. The command rectangle itself remains
    // stable at the bottom edge.
    [[nodiscard]] constexpr float ResolveSettingsMaximumBottom(
        const CommandInterfaceLayout& layout,
        const UiCommandLayoutRect& workRectangle,
        float margin,
        float commandAppearance)
    {
        const float safeMargin = std::max(0.f, margin);
        const float fullHeightBottom = std::max(
            workRectangle.minY + safeMargin,
            workRectangle.maxY - safeMargin);
        const float appearance = std::clamp(commandAppearance, 0.f, 1.f);
        return fullHeightBottom +
            (layout.settingsMaximumBottom - fullHeightBottom) * appearance;
    }

    // Keep the detached Performance window within the shared stack cap while
    // reserving both the inter-panel gap and enough space for the independently
    // scrollable Settings window.
    [[nodiscard]] constexpr float ResolvePerformanceMaximumWindowHeight(
        float stackMaximumBottom,
        float performanceWindowTop,
        float settingsBodyReserve,
        float panelGap)
    {
        const float safeSettingsBodyReserve =
            std::max(0.f, settingsBodyReserve);
        const float safePanelGap = std::max(0.f, panelGap);
        return std::max(
            1.f,
            stackMaximumBottom - performanceWindowTop -
                safeSettingsBodyReserve - safePanelGap);
    }

    // Resolve one full-width command lane in the same coordinate space as
    // ImGui's main viewport. At full command appearance, Settings may use
    // vertical space only through settingsMaximumBottom, leaving one explicit
    // Tight panel gap above the lane. ResolveSettingsMaximumBottom controls the
    // smooth transition between full-height and reserved panel-stack layouts.
    [[nodiscard]] constexpr CommandInterfaceLayout
        ResolveCommandInterfaceLayout(
            const UiCommandLayoutRect& workRectangle,
            float margin,
            float panelToCommandGap,
            float compactWidth,
            float minimumRenderableWidth,
            float reservedHeight,
            float minimumCommandHeight,
            float minimumSettingsHeight)
    {
        const float safeMargin = std::max(0.f, margin);
        const float safePanelToCommandGap =
            std::max(0.f, panelToCommandGap);
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
            result.top - safePanelToCommandGap);

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
