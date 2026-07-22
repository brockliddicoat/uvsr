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

    struct PixelZoomSourcePixel
    {
        uint32_t x = 0u;
        uint32_t y = 0u;
    };

    [[nodiscard]] constexpr uint32_t GetPixelZoomFactor(
        PixelZoomMode mode)
    {
        return static_cast<uint32_t>(mode);
    }

    [[nodiscard]] constexpr bool IsPixelZoomEnabled(PixelZoomMode mode)
    {
        return GetPixelZoomFactor(mode) >= 2u;
    }

    [[nodiscard]] constexpr bool IsPixelZoomCompositionIdle(
        PixelZoomMode requested,
        PixelZoomMode rendered,
        PixelZoomMode pending,
        float visibility,
        float levelTransitionProgress)
    {
        if (!IsPixelZoomEnabled(requested))
        {
            return visibility <= 0.f &&
                !IsPixelZoomEnabled(rendered) &&
                !IsPixelZoomEnabled(pending);
        }

        return visibility >= 1.f &&
            levelTransitionProgress >= 1.f &&
            rendered == requested &&
            pending == requested;
    }

    [[nodiscard]] constexpr PixelZoomMode AdvancePixelZoomMode(
        PixelZoomMode mode)
    {
        switch (mode)
        {
        case PixelZoomMode::Off:
            return PixelZoomMode::Zoom2x;
        case PixelZoomMode::Zoom2x:
            return PixelZoomMode::Zoom3x;
        case PixelZoomMode::Zoom3x:
            return PixelZoomMode::Zoom4x;
        case PixelZoomMode::Zoom4x:
            return PixelZoomMode::Zoom5x;
        default:
            return PixelZoomMode::Off;
        }
    }

    [[nodiscard]] constexpr const char* GetPixelZoomButtonLabel(
        PixelZoomMode)
    {
        return "Zoom";
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

    inline constexpr float PixelZoomFadeDurationSeconds = 0.18f;
    inline constexpr float PixelZoomMinimumWindowScale = 0.86f;
    inline constexpr float PixelZoomLevelTransitionDurationSeconds = 0.18f;
    inline constexpr uint32_t PixelZoomPanelWidthPercent = 28u;

    [[nodiscard]] constexpr float AdvancePixelZoomVisibility(
        float visibility,
        bool visible,
        float deltaTimeSeconds)
    {
        const float fadeStep =
            std::min(std::max(deltaTimeSeconds, 0.f), 0.05f) /
            PixelZoomFadeDurationSeconds;
        return std::clamp(
            visibility + (visible ? fadeStep : -fadeStep),
            0.f,
            1.f);
    }

    [[nodiscard]] constexpr float SmoothPixelZoomVisibility(float visibility)
    {
        const float clamped = std::clamp(visibility, 0.f, 1.f);
        return clamped * clamped * (3.f - 2.f * clamped);
    }

    [[nodiscard]] constexpr float AdvancePixelZoomLevelTransition(
        float progress,
        float deltaTimeSeconds)
    {
        const float transitionStep =
            std::min(std::max(deltaTimeSeconds, 0.f), 0.05f) /
            PixelZoomLevelTransitionDurationSeconds;
        return std::clamp(progress + transitionStep, 0.f, 1.f);
    }

    [[nodiscard]] constexpr bool
        ShouldSwitchPixelZoomLevel(float transitionProgress)
    {
        return transitionProgress >= 0.5f;
    }

    [[nodiscard]] constexpr float ResolvePixelZoomLevelTransitionScale(
        float transitionProgress)
    {
        const float progress = std::clamp(
            transitionProgress,
            0.f,
            1.f);
        const float midpointAmount =
            progress <= 0.5f
                ? progress * 2.f
                : (1.f - progress) * 2.f;
        const float easedMidpointAmount =
            SmoothPixelZoomVisibility(midpointAmount);
        return
            1.f -
            (1.f - PixelZoomMinimumWindowScale) *
                easedMidpointAmount;
    }

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

    [[nodiscard]] constexpr PixelZoomLayout
        ResolveAnimatedPixelZoomLayout(
            const PixelZoomLayout& finalLayout,
            float easedVisibility,
            float levelTransitionScale = 1.f)
    {
        PixelZoomLayout result = finalLayout;
        const float visibility = std::clamp(easedVisibility, 0.f, 1.f);
        const float appearanceScale =
            PixelZoomMinimumWindowScale +
            (1.f - PixelZoomMinimumWindowScale) * visibility;
        const float scale =
            appearanceScale *
            std::clamp(levelTransitionScale, 0.f, 1.f);
        const uint32_t minimumPanelWidth = std::min(
            finalLayout.panelWidth,
            std::max(1u, finalLayout.zoomFactor));
        const uint32_t minimumPanelHeight = std::min(
            finalLayout.panelHeight,
            std::max(1u, finalLayout.zoomFactor));
        result.panelWidth = std::max(
            minimumPanelWidth,
            static_cast<uint32_t>(
                static_cast<float>(finalLayout.panelWidth) * scale + 0.5f));
        result.panelHeight = std::max(
            minimumPanelHeight,
            static_cast<uint32_t>(
                static_cast<float>(finalLayout.panelHeight) * scale + 0.5f));
        result.panelMinX +=
            (finalLayout.panelWidth - result.panelWidth) / 2u;
        result.panelMinY +=
            (finalLayout.panelHeight - result.panelHeight) / 2u;
        return result;
    }

    [[nodiscard]] constexpr int32_t FloorDivide(
        int32_t numerator,
        int32_t denominator)
    {
        if (numerator >= 0)
            return numerator / denominator;
        return -((-numerator + denominator - 1) / denominator);
    }

    [[nodiscard]] constexpr PixelZoomSourcePixel
        ResolvePixelZoomSourcePixel(
            const PixelZoomLayout& layout,
            uint32_t panelPixelX,
            uint32_t panelPixelY)
    {
        const int32_t factor =
            std::max(1, static_cast<int32_t>(layout.zoomFactor));
        const int32_t groupOriginX =
            (static_cast<int32_t>(layout.panelWidth) - factor) / 2;
        const int32_t groupOriginY =
            (static_cast<int32_t>(layout.panelHeight) - factor) / 2;
        const int32_t sourceOffsetX = FloorDivide(
            static_cast<int32_t>(panelPixelX) - groupOriginX,
            factor);
        const int32_t sourceOffsetY = FloorDivide(
            static_cast<int32_t>(panelPixelY) - groupOriginY,
            factor);
        const int32_t maximumSourceX =
            std::max(0, static_cast<int32_t>(layout.sourceWidth) - 1);
        const int32_t maximumSourceY =
            std::max(0, static_cast<int32_t>(layout.sourceHeight) - 1);

        PixelZoomSourcePixel result;
        result.x = static_cast<uint32_t>(std::clamp(
            static_cast<int32_t>(layout.sourceWidth / 2u) +
                sourceOffsetX,
            0,
            maximumSourceX));
        result.y = static_cast<uint32_t>(std::clamp(
            static_cast<int32_t>(layout.sourceHeight / 2u) +
                sourceOffsetY,
            0,
            maximumSourceY));
        return result;
    }
}
