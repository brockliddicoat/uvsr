#pragma once

#include <algorithm>
#include <cstdint>

namespace uvsr
{
    enum class PixelZoomMode : uint32_t
    {
        Off = 0u,
        Zoom2x = 2u,
        Zoom3x = 3u,
        Zoom4x = 4u,
        Zoom5x = 5u
    };

    struct PixelZoomLayout
    {
        uint32_t sourceWidth = 0u;
        uint32_t sourceHeight = 0u;
        uint32_t panelMinX = 0u;
        uint32_t panelMinY = 0u;
        uint32_t panelWidth = 0u;
        uint32_t panelHeight = 0u;
        uint32_t zoomFactor = 0u;
    };

    struct CenterMaterialPick
    {
        uint32_t x = 0u;
        uint32_t y = 0u;
        bool valid = false;
    };

    [[nodiscard]] constexpr CenterMaterialPick
        ResolveCenterMaterialPick(
            uint32_t sourceWidth,
            uint32_t sourceHeight)
    {
        return {
            sourceWidth / 2u,
            sourceHeight / 2u,
            sourceWidth > 0u && sourceHeight > 0u
        };
    }

    [[nodiscard]] constexpr uint32_t GetPixelZoomFactor(
        PixelZoomMode mode)
    {
        return static_cast<uint32_t>(mode);
    }

    [[nodiscard]] constexpr bool IsPixelZoomEnabled(PixelZoomMode mode)
    {
        return GetPixelZoomFactor(mode) >= 2u;
    }

    [[nodiscard]] constexpr const char* GetPixelZoomAreaLabel(
        PixelZoomMode mode)
    {
        switch (mode)
        {
        case PixelZoomMode::Zoom2x:
            return "4x";
        case PixelZoomMode::Zoom3x:
            return "9x";
        case PixelZoomMode::Zoom4x:
            return "16x";
        case PixelZoomMode::Zoom5x:
            return "25x";
        default:
            return "";
        }
    }

    inline constexpr uint32_t PixelZoomPanelWidthPercent = 28u;

    [[nodiscard]] constexpr PixelZoomLayout ResolvePixelZoomLayout(
        uint32_t sourceWidth,
        uint32_t sourceHeight,
        uint32_t panelMargin,
        PixelZoomMode mode)
    {
        PixelZoomLayout result;
        result.sourceWidth = sourceWidth;
        result.sourceHeight = sourceHeight;
        result.panelWidth =
            sourceWidth == 0u
                ? 0u
                : std::max(
                    1u,
                    static_cast<uint32_t>(
                        (static_cast<uint64_t>(sourceWidth) *
                            PixelZoomPanelWidthPercent +
                            50u) /
                        100u));
        result.panelHeight =
            sourceWidth == 0u || sourceHeight == 0u
                ? 0u
                : std::max(
                    1u,
                    static_cast<uint32_t>(
                        (static_cast<uint64_t>(result.panelWidth) *
                            sourceHeight +
                            sourceWidth / 2u) /
                        sourceWidth));
        result.panelMinX =
            sourceWidth >= result.panelWidth + panelMargin
                ? sourceWidth - result.panelWidth - panelMargin
                : 0u;
        result.panelMinY = panelMargin;
        result.zoomFactor = GetPixelZoomFactor(mode);
        return result;
    }
}
