#include "imgui.h"
#include "imgui_internal.h"
#include "ui_performance_timing_rows.h"

#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

namespace
{
    constexpr float FixedDeltaSeconds = 1.0f / 60.0f;
    constexpr float ComboWidth = 180.0f;
    constexpr float PopupHeight = 100.0f;
    constexpr int MaximumAnimationFrames = 64;

    struct ComboHarness
    {
        ImGuiID comboId = 0;
        ImGuiID popupId = 0;
        int selectedItem = -1;
        int pendingSelectedItem = -1;
        int optionPressCount = 0;
        bool deferSelectionCommit = false;
    };

    struct FrameObservation
    {
        int frame = -1;
        bool popupBegan = false;
        bool optionPressed = false;
        bool popupOpen = false;
        bool transitionActive = false;
        bool popupClosing = false;
        bool popupInteractionReady = true;
        bool popupRollFromBottom = false;
        bool rollClipObserved = false;
        bool rollClipValid = true;
        float popupRollAmount = 1.0f;
        int popupCloseReadyFrame = -1;
        ImGuiID optionId = 0;
        ImGuiID activeId = 0;
        ImRect comboRect;
        ImRect comboVisualBounds{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        bool comboVisualBoundsValid = false;
        ImRect optionRect;
        std::array<ImRect, 3> optionRects{};
        std::array<bool, 3> optionFillRounded{};
        std::array<float, 3> maximumOptionFillAlpha{};
        ImRect sharedHighlightRect;
        ImRect sharedHighlightTargetRect;
        bool sharedHighlightValid = false;
        bool sharedHighlightRounded = false;
        int sharedHighlightPriority = 0;
        float sharedHighlightAlpha = 0.0f;
        float minimumOptionHeight = 0.0f;
        float maximumOptionHeight = 0.0f;
        float minimumOptionGap = 0.0f;
        float maximumOptionGap = 0.0f;
        float popupItemSpacingY = 0.0f;
        float popupRounding = 0.0f;
        ImVec2 popupWindowPadding;
        bool popupStyleRestored = true;
    };

    struct TooltipObservation
    {
        bool submitted = false;
        bool noInputs = false;
        bool alwaysAutoResize = false;
        bool hasVerticalScrollbar = false;
        ImVec2 windowSize;
        ImVec2 windowPosition;
        ImVec2 windowPadding;
        ImVec2 contentSize;
        ImVec2 mousePosition;
        ImRect ownerRect;
        ImRect visualBounds{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        ImRect textBounds{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        std::string renderedText;
        float maximumVertexAlpha = 0.0f;
    };

    struct TooltipDragObservation
    {
        bool retainedTooltipInactive = false;
        bool retainedOwnerReset = false;
        bool dragTooltipActive = false;
        bool dragTooltipNoInputs = false;
    };

    struct ColorPickerObservation
    {
        bool popupOpen = false;
        bool transitionRegistered = false;
        bool transitionTargetOpen = false;
        bool transitionInteractionReady = false;
        bool transitionAtZeroEndpoint = false;
        bool onlyPickerPopupOpen = false;
        ImGuiID popupId = 0;
        int openPopupCount = 0;
        int barCount = 0;
        float contentRight = 0.0f;
        float maximumBottom = 0.0f;
        float colorAlpha = 0.0f;
        float transitionAmount = 0.0f;
        float maximumVertexAlpha = 0.0f;
        bool requestedSurfaceApplied = false;
        bool requestedContentLayerApplied = false;
        bool requestedPickerLayerApplied = false;
        bool opaqueOuterMarginVisible = false;
        bool opaqueOuterRimComplete = false;
        bool translucentSurfaceCoversCenter = false;
        bool opaqueOuterMarginCoversCenter = false;
        bool activeDrawListReported = false;
        bool activeDrawListMatchesPopup = false;
        bool finalCursorLayeredAndClipped = false;
        bool hueBarRoundedAndVisible = false;
        bool alphaBarRoundedAndVisible = false;
        bool alphaBarInteriorCovered = false;
        bool disabledAlphaBarVisible = false;
        bool alphaCheckerEdgeCoverageContinuous = false;
        bool innerWheelGradientOutlineVisible = false;
        bool outerWheelGradientOutlineVisible = false;
        bool hueHollowMarkerVisible = false;
        bool alphaHollowMarkerVisible = false;
        bool currentBarVisible = false;
        bool originalBarVisible = false;
        bool bottomControlsSpanAllBars = false;
        bool sourcePointerVisible = false;
        bool sourcePointerTargetsSwatch = false;
        bool sourcePointerTipAttached = false;
        bool sourcePointerBaseAttached = false;
        bool sourcePointerUsesCarvedFrame = false;
        bool authoredPopupUsesCarvedFrameOnly = false;
        bool contentFrameHalfPixelAligned = false;
        bool selectorMarkerCursorClearance = false;
        bool selectorEndpointExclusionObserved = false;
        bool sourceSwatchShadowVisible = false;
        bool sourceSwatchBorderless = false;
        bool hueBarInteriorOpaque = false;
        bool selectorVisualEndpointsFound = false;
        int subordinatePreviewSquareCount = 0;
        int opaqueSelectorVertexCount = 0;
        float wheelThickness = 0.0f;
        float endpointSnapRadius = 0.0f;
        float endpointMarkerRadius = 0.0f;
        float drawListFringe = 0.0f;
        float sourcePointerTipDistance = FLT_MAX;
        float sourcePointerBaseTopDistance = FLT_MAX;
        float sourcePointerBaseBottomDistance = FLT_MAX;
        float sourcePointerFrameMaximumAlpha = 0.0f;
        float canonicalSourceRight = 0.0f;
        float minimumMarkerCursorGap = FLT_MAX;
        float requiredMarkerCursorGap = 0.0f;
        ImRect popupRect;
        ImRect popupInnerRect;
        ImVec2 popupWindowPadding;
        ImVec2 brightSurfacePadding;
        ImRect viewportRect;
        ImRect sourceSwatchRect;
        ImRect submittedSourceSwatchRect;
        ImRect hueBarRect;
        ImRect alphaBarRect;
        ImRect currentBarRect;
        ImRect originalBarRect;
        ImRect observedHueBarRect{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        ImRect observedOriginalBarRect{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        ImRect fourthRgbInputRect{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        ImRect fourthHsvInputRect{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        ImRect hexInputRect{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        ImRect selectorRect;
        ImVec2 selectorCenter;
        ImVec2 selectorCursorCenter;
        ImRect visualBounds{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        std::array<ImVec2, 3> selectorEndpoints{};
        std::array<bool, 3> selectorEndpointMarkerVisible{};
        ImRect contentLayerRect{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        ImRect expectedPickerLayerRect{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        ImRect observedPickerLayerRect{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
        float brightMarginLeft = -FLT_MAX;
        float brightMarginTop = -FLT_MAX;
        float brightMarginRight = -FLT_MAX;
        float brightMarginBottom = -FLT_MAX;
        int innerWheelOutlineCoveredBins = 0;
        int outerWheelOutlineCoveredBins = 0;
        float innerWheelOutlineTopAlpha = 0.0f;
        float innerWheelOutlineBottomAlpha = 0.0f;
        float outerWheelOutlineTopAlpha = 0.0f;
        float outerWheelOutlineBottomAlpha = 0.0f;
    };

    struct GradientFrameObservation
    {
        float topFillLuminance = 0.0f;
        float bottomFillLuminance = 0.0f;
        float topOutlineLuminance = 0.0f;
        float bottomOutlineLuminance = 0.0f;
        float maximumTopOutlineAlpha = 0.0f;
        float maximumBottomOutlineAlpha = 0.0f;
        float maximumOutlinePremultipliedLuminance = 0.0f;
        int topFillVertexCount = 0;
        int bottomFillVertexCount = 0;
        int coveredFillVertexCount = 0;
        int transparentFillFringeVertexCount = 0;
        int transparentOutlineFringeVertexCount = 0;
        int topOutlineVertexCount = 0;
        int bottomOutlineVertexCount = 0;
        bool fillCoverageBounded = true;
        ImRect outlineBounds{
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX)
        };
    };

    struct SliderFrameObservation
    {
        float animatedCenterX = 0.0f;
        float maximumDrawerTrackAlpha = 0.0f;
        float maximumDisabledTrackAlpha = 0.0f;
        float maximumGrabAlpha = 0.0f;
        float topGrabLuminance = 0.0f;
        float bottomGrabLuminance = 0.0f;
        int topGrabVertexCount = 0;
        int bottomGrabVertexCount = 0;
        int outlineVertexCount = 0;
        bool hasGrabAnimationState = false;
    };

    struct SliderLaneLayoutObservation
    {
        ImRect sliderRect;
        ImRect sliderVisualRect;
        ImRect trackRect;
        ImRect valueRect;
        ImRect toggleRect;
        ImRect sameLineRect;
        ImRect followingRowRect;
        bool valueLaneUsable = false;
        bool trackFaceRounded = false;
        bool valueFaceRounded = false;
        bool sliderHovered = false;
        bool tempInputActive = false;
        ImGuiID sliderId = 0;
        ImGuiID activeId = 0;
    };

    bool Check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

    bool TestPerformanceTimingRowRetentionLifecycle()
    {
        using uvsr::PerformanceTimingRowPresentation;
        using uvsr::PerformanceTimingRowRetention;

        constexpr std::uint32_t CompleteRendererView = 0u;
        constexpr std::uint32_t MaterialPickingView = 10u;
        constexpr std::uint32_t SharedLabelId = 0x85c7f27au;
        PerformanceTimingRowRetention rows;

        const auto firstUnavailable = rows.Resolve(
            MaterialPickingView,
            SharedLabelId,
            41.0,
            false);
        const auto available = rows.Resolve(
            MaterialPickingView,
            SharedLabelId,
            0.375,
            true);

        // Scene loading clears current timer availability, but the session-owned
        // row history remains alive in the UI renderer.
        const auto unavailableAfterSceneLoad = rows.Resolve(
            MaterialPickingView,
            SharedLabelId,
            99.0,
            false);
        const auto sameLabelInDifferentView = rows.Resolve(
            CompleteRendererView,
            SharedLabelId,
            0.0,
            false);

        return
            firstUnavailable.presentation ==
                PerformanceTimingRowPresentation::Hidden &&
            !firstUnavailable.IsVisible() &&
            firstUnavailable.milliseconds == 0.0 &&
            available.presentation ==
                PerformanceTimingRowPresentation::Measurement &&
            available.IsVisible() &&
            available.HasMeasurement() &&
            available.milliseconds == 0.375 &&
            unavailableAfterSceneLoad.presentation ==
                PerformanceTimingRowPresentation::Unavailable &&
            unavailableAfterSceneLoad.IsVisible() &&
            !unavailableAfterSceneLoad.HasMeasurement() &&
            unavailableAfterSceneLoad.milliseconds == 0.0 &&
            sameLabelInDifferentView.presentation ==
                PerformanceTimingRowPresentation::Hidden &&
            !sameLabelInDifferentView.IsVisible();
    }

    bool Near(float left, float right)
    {
        return std::abs(left - right) <= 1e-5f;
    }

    ImVec2 Add(const ImVec2& left, const ImVec2& right)
    {
        return ImVec2(left.x + right.x, left.y + right.y);
    }

    ImVec2 Subtract(const ImVec2& left, const ImVec2& right)
    {
        return ImVec2(left.x - right.x, left.y - right.y);
    }

    ImVec2 Scale(const ImVec2& value, float amount)
    {
        return ImVec2(value.x * amount, value.y * amount);
    }

    bool HasSolidColorTriangleCentroidInRect(
        const ImDrawList& drawList,
        ImU32 color,
        const ImRect& bounds)
    {
        for (const ImDrawCmd& command : drawList.CmdBuffer)
        {
            if (command.UserCallback != nullptr)
                continue;
            for (unsigned int element = 0;
                element + 2 < command.ElemCount;
                element += 3)
            {
                const unsigned int indexOffset =
                    command.IdxOffset + element;
                const unsigned int firstIndex =
                    command.VtxOffset + drawList.IdxBuffer[indexOffset];
                const unsigned int secondIndex =
                    command.VtxOffset + drawList.IdxBuffer[indexOffset + 1];
                const unsigned int thirdIndex =
                    command.VtxOffset + drawList.IdxBuffer[indexOffset + 2];
                if (firstIndex >= (unsigned int)drawList.VtxBuffer.Size ||
                    secondIndex >= (unsigned int)drawList.VtxBuffer.Size ||
                    thirdIndex >= (unsigned int)drawList.VtxBuffer.Size)
                {
                    continue;
                }
                const ImDrawVert& first = drawList.VtxBuffer[firstIndex];
                const ImDrawVert& second = drawList.VtxBuffer[secondIndex];
                const ImDrawVert& third = drawList.VtxBuffer[thirdIndex];
                if (first.col != color ||
                    second.col != color ||
                    third.col != color)
                {
                    continue;
                }
                const ImVec2 centroid(
                    (first.pos.x + second.pos.x + third.pos.x) / 3.0f,
                    (first.pos.y + second.pos.y + third.pos.y) / 3.0f);
                if (bounds.Contains(centroid))
                    return true;
            }
        }
        return false;
    }

    bool HasSolidColorCoverageAtPoint(
        const ImDrawList& drawList,
        ImU32 color,
        const ImVec2& point)
    {
        const auto cross = [](const ImVec2& first,
                               const ImVec2& second,
                               const ImVec2& sample)
        {
            return
                (sample.x - second.x) * (first.y - second.y) -
                (first.x - second.x) * (sample.y - second.y);
        };
        for (const ImDrawCmd& command : drawList.CmdBuffer)
        {
            if (command.UserCallback != nullptr ||
                point.x < command.ClipRect.x ||
                point.y < command.ClipRect.y ||
                point.x >= command.ClipRect.z ||
                point.y >= command.ClipRect.w)
            {
                continue;
            }
            for (unsigned int element = 0;
                element + 2 < command.ElemCount;
                element += 3)
            {
                const unsigned int indexOffset =
                    command.IdxOffset + element;
                const unsigned int indices[3] = {
                    command.VtxOffset + drawList.IdxBuffer[indexOffset],
                    command.VtxOffset + drawList.IdxBuffer[indexOffset + 1],
                    command.VtxOffset + drawList.IdxBuffer[indexOffset + 2]
                };
                if (indices[0] >= (unsigned int)drawList.VtxBuffer.Size ||
                    indices[1] >= (unsigned int)drawList.VtxBuffer.Size ||
                    indices[2] >= (unsigned int)drawList.VtxBuffer.Size)
                {
                    continue;
                }
                const ImDrawVert& first = drawList.VtxBuffer[indices[0]];
                const ImDrawVert& second = drawList.VtxBuffer[indices[1]];
                const ImDrawVert& third = drawList.VtxBuffer[indices[2]];
                if (first.col != color ||
                    second.col != color ||
                    third.col != color)
                {
                    continue;
                }
                const float firstCross = cross(first.pos, second.pos, point);
                const float secondCross = cross(second.pos, third.pos, point);
                const float thirdCross = cross(third.pos, first.pos, point);
                const bool hasNegative =
                    firstCross < -0.01f ||
                    secondCross < -0.01f ||
                    thirdCross < -0.01f;
                const bool hasPositive =
                    firstCross > 0.01f ||
                    secondCross > 0.01f ||
                    thirdCross > 0.01f;
                if (!(hasNegative && hasPositive))
                    return true;
            }
        }
        return false;
    }

    ImRect GetSolidColorTriangleBounds(
        const ImDrawList& drawList,
        ImU32 color,
        const ImRect& centroidBounds)
    {
        ImRect bounds(
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX));
        for (const ImDrawCmd& command : drawList.CmdBuffer)
        {
            if (command.UserCallback != nullptr)
                continue;
            for (unsigned int element = 0;
                element + 2 < command.ElemCount;
                element += 3)
            {
                const unsigned int indexOffset =
                    command.IdxOffset + element;
                const unsigned int indices[3] = {
                    command.VtxOffset + drawList.IdxBuffer[indexOffset],
                    command.VtxOffset + drawList.IdxBuffer[indexOffset + 1],
                    command.VtxOffset + drawList.IdxBuffer[indexOffset + 2]
                };
                if (indices[0] >= (unsigned int)drawList.VtxBuffer.Size ||
                    indices[1] >= (unsigned int)drawList.VtxBuffer.Size ||
                    indices[2] >= (unsigned int)drawList.VtxBuffer.Size)
                {
                    continue;
                }
                const ImDrawVert& first = drawList.VtxBuffer[indices[0]];
                const ImDrawVert& second = drawList.VtxBuffer[indices[1]];
                const ImDrawVert& third = drawList.VtxBuffer[indices[2]];
                if (first.col != color ||
                    second.col != color ||
                    third.col != color)
                {
                    continue;
                }
                const ImVec2 centroid(
                    (first.pos.x + second.pos.x + third.pos.x) / 3.0f,
                    (first.pos.y + second.pos.y + third.pos.y) / 3.0f);
                if (!centroidBounds.Contains(centroid))
                    continue;
                bounds.Add(first.pos);
                bounds.Add(second.pos);
                bounds.Add(third.pos);
            }
        }
        return bounds;
    }

    ImVector<ImRect> CollectWhitePixelTriangleComponents(
        const ImDrawList& drawList)
    {
        ImVector<ImRect> components;
        const ImVec2 whitePixel = drawList._Data->TexUvWhitePixel;
        ImRect current(
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX));
        unsigned int previousIndices[3] = {};
        bool hasCurrent = false;
        const auto finalizeCurrent = [&]()
        {
            if (!current.IsInverted())
                components.push_back(current);
            current = ImRect(
                ImVec2(FLT_MAX, FLT_MAX),
                ImVec2(-FLT_MAX, -FLT_MAX));
            hasCurrent = false;
        };

        for (const ImDrawCmd& command : drawList.CmdBuffer)
        {
            if (command.UserCallback != nullptr)
                continue;
            for (unsigned int element = 0;
                element + 2 < command.ElemCount;
                element += 3)
            {
                const unsigned int indexOffset =
                    command.IdxOffset + element;
                unsigned int indices[3] = {
                    command.VtxOffset + drawList.IdxBuffer[indexOffset],
                    command.VtxOffset + drawList.IdxBuffer[indexOffset + 1],
                    command.VtxOffset + drawList.IdxBuffer[indexOffset + 2]
                };
                bool validWhitePixelTriangle = true;
                for (unsigned int index : indices)
                {
                    if (index >= (unsigned int)drawList.VtxBuffer.Size)
                    {
                        validWhitePixelTriangle = false;
                        break;
                    }
                    const ImDrawVert& vertex = drawList.VtxBuffer[index];
                    validWhitePixelTriangle &=
                        Near(vertex.uv.x, whitePixel.x) &&
                        Near(vertex.uv.y, whitePixel.y);
                }
                if (!validWhitePixelTriangle)
                {
                    if (hasCurrent)
                        finalizeCurrent();
                    continue;
                }

                bool connected = !hasCurrent;
                if (hasCurrent)
                {
                    for (unsigned int index : indices)
                    {
                        for (unsigned int previousIndex : previousIndices)
                            connected |= index == previousIndex;
                    }
                }
                if (hasCurrent && !connected)
                    finalizeCurrent();
                for (unsigned int index : indices)
                    current.Add(drawList.VtxBuffer[index].pos);
                for (int index = 0; index < 3; ++index)
                    previousIndices[index] = indices[index];
                hasCurrent = true;
            }
        }
        if (hasCurrent)
            finalizeCurrent();
        return components;
    }

    ImRect FindRightmostInputFrame(
        const ImVector<ImRect>& components,
        const ImRect& rowBand,
        float rowHeight,
        float minimumWidth)
    {
        ImRect best(
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX));
        for (const ImRect& candidate : components)
        {
            if (!rowBand.Contains(candidate.GetCenter()) ||
                candidate.GetHeight() < rowHeight * 0.70f ||
                candidate.GetHeight() > rowHeight * 1.35f ||
                candidate.GetWidth() < minimumWidth)
            {
                continue;
            }
            if (best.IsInverted() || candidate.Max.x > best.Max.x)
                best = candidate;
        }
        return best;
    }

    int CountPreviewSizedFrames(
        const ImVector<ImRect>& components,
        const ImRect& rowBand,
        float rowHeight,
        float fourthColumnStart)
    {
        int count = 0;
        for (const ImRect& candidate : components)
        {
            if (!rowBand.Contains(candidate.GetCenter()) ||
                candidate.GetCenter().x < fourthColumnStart ||
                candidate.GetHeight() < rowHeight * 0.70f ||
                candidate.GetHeight() > rowHeight * 1.35f ||
                candidate.GetWidth() < rowHeight * 0.70f ||
                candidate.GetWidth() > rowHeight * 1.35f)
            {
                continue;
            }
            ++count;
        }
        return count;
    }

    float Cross(const ImVec2& first, const ImVec2& second)
    {
        return first.x * second.y - first.y * second.x;
    }

    float RayPolygonBoundaryDistance(
        const ImVec2& center,
        const ImVec2& direction,
        const ImVec2* points,
        int pointCount)
    {
        const float directionLengthSquared = ImLengthSqr(direction);
        if (directionLengthSquared <= FLT_EPSILON || pointCount < 3)
            return 0.0f;

        const ImVec2 unitDirection = Scale(
            direction,
            1.0f / ImSqrt(directionLengthSquared));
        float boundaryDistance = FLT_MAX;
        for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex)
        {
            const ImVec2 first = points[pointIndex];
            const ImVec2 second = points[(pointIndex + 1) % pointCount];
            const ImVec2 edge = Subtract(second, first);
            const float denominator = Cross(unitDirection, edge);
            if (ImAbs(denominator) <= FLT_EPSILON)
                continue;

            const ImVec2 delta = Subtract(first, center);
            const float distance = Cross(delta, edge) / denominator;
            const float edgeAmount =
                Cross(delta, unitDirection) / denominator;
            if (distance >= 0.0f &&
                edgeAmount >= -0.0001f &&
                edgeAmount <= 1.0001f)
            {
                boundaryDistance = ImMin(boundaryDistance, distance);
            }
        }
        return boundaryDistance < FLT_MAX ? boundaryDistance : 0.0f;
    }

    ImVec2 MapRoundedSelectorPoint(
        const ImVec2 (&sharpVertices)[3],
        const ImVector<ImVec2>& contour,
        const ImVec2& sharpPosition)
    {
        const ImVec2 center = Scale(
            Add(Add(sharpVertices[0], sharpVertices[1]), sharpVertices[2]),
            1.0f / 3.0f);
        const ImVec2 direction = Subtract(sharpPosition, center);
        if (ImLengthSqr(direction) <= FLT_EPSILON)
            return center;
        const float sharpBoundary = RayPolygonBoundaryDistance(
            center,
            direction,
            sharpVertices,
            3);
        const float roundedBoundary = RayPolygonBoundaryDistance(
            center,
            direction,
            contour.Data,
            contour.Size);
        if (sharpBoundary <= FLT_EPSILON ||
            roundedBoundary <= FLT_EPSILON)
        {
            return sharpPosition;
        }
        return Add(
            center,
            Scale(direction, roundedBoundary / sharpBoundary));
    }

    float Luminance(ImU32 color)
    {
        const ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
        return value.x * 0.2126f +
            value.y * 0.7152f +
            value.z * 0.0722f;
    }

    float Alpha(ImU32 color)
    {
        return ImGui::ColorConvertU32ToFloat4(color).w;
    }

    bool HasCoveredVertexNear(
        const ImDrawList& drawList,
        int firstVertex,
        int lastVertex,
        const ImVec2& point)
    {
        constexpr float PositionTolerance = 1.0f;
        constexpr float MinimumFillAlpha = 0.5f;
        for (int index = firstVertex; index < lastVertex; ++index)
        {
            const ImDrawVert& vertex = drawList.VtxBuffer[index];
            if (std::abs(vertex.pos.x - point.x) <= PositionTolerance &&
                std::abs(vertex.pos.y - point.y) <= PositionTolerance &&
                Alpha(vertex.col) >= MinimumFillAlpha)
            {
                return true;
            }
        }
        return false;
    }

    bool HasCoveredVertexInRect(
        const ImDrawList& drawList,
        const ImRect& bounds,
        float minimumAlpha)
    {
        for (const ImDrawVert& vertex : drawList.VtxBuffer)
        {
            if (bounds.Contains(vertex.pos) &&
                Alpha(vertex.col) >= minimumAlpha)
            {
                return true;
            }
        }
        return false;
    }

    bool HasVisibleVertexNear(
        const ImDrawList& drawList,
        int firstVertex,
        int lastVertex,
        const ImVec2& point)
    {
        constexpr float PositionTolerance = 1.0f;
        constexpr float MinimumVisibleAlpha = 1.0f / 255.0f;
        for (int index = firstVertex; index < lastVertex; ++index)
        {
            const ImDrawVert& vertex = drawList.VtxBuffer[index];
            if (std::abs(vertex.pos.x - point.x) <= PositionTolerance &&
                std::abs(vertex.pos.y - point.y) <= PositionTolerance &&
                Alpha(vertex.col) > MinimumVisibleAlpha)
            {
                return true;
            }
        }
        return false;
    }

    bool FindExactColorBounds(
        const ImDrawList& drawList,
        ImU32 color,
        ImRect& bounds)
    {
        bool found = false;
        bounds = ImRect(
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX));
        for (const ImDrawVert& vertex : drawList.VtxBuffer)
        {
            if (vertex.col != color)
                continue;
            bounds.Min.x = ImMin(bounds.Min.x, vertex.pos.x);
            bounds.Min.y = ImMin(bounds.Min.y, vertex.pos.y);
            bounds.Max.x = ImMax(bounds.Max.x, vertex.pos.x);
            bounds.Max.y = ImMax(bounds.Max.y, vertex.pos.y);
            found = true;
        }
        return found;
    }

    bool HasRoundedFillCorners(
        const ImDrawList& drawList,
        int firstVertex,
        int lastVertex,
        const ImRect& rect)
    {
        return
            !HasVisibleVertexNear(
                drawList, firstVertex, lastVertex, rect.Min) &&
            !HasVisibleVertexNear(
                drawList,
                firstVertex,
                lastVertex,
                ImVec2(rect.Max.x, rect.Min.y)) &&
            !HasVisibleVertexNear(
                drawList,
                firstVertex,
                lastVertex,
                ImVec2(rect.Min.x, rect.Max.y)) &&
            !HasVisibleVertexNear(
                drawList, firstVertex, lastVertex, rect.Max);
    }

    bool HasRoundedCoveredCorners(
        const ImDrawList& drawList,
        const ImRect& rect)
    {
        return
            !HasCoveredVertexNear(
                drawList, 0, drawList.VtxBuffer.Size, rect.Min) &&
            !HasCoveredVertexNear(
                drawList,
                0,
                drawList.VtxBuffer.Size,
                ImVec2(rect.Max.x, rect.Min.y)) &&
            !HasCoveredVertexNear(
                drawList,
                0,
                drawList.VtxBuffer.Size,
                ImVec2(rect.Min.x, rect.Max.y)) &&
            !HasCoveredVertexNear(
                drawList, 0, drawList.VtxBuffer.Size, rect.Max);
    }

    bool HasRoundedBarCoverage(
        const ImDrawList& drawList,
        const ImRect& rect)
    {
        const ImVec2 coveredInterior(
            (rect.Min.x + rect.Max.x) * 0.5f,
            ImLerp(rect.Min.y, rect.Max.y, 0.25f));
        return
            rect.GetWidth() > 0.0f &&
            rect.GetHeight() > 0.0f &&
            HasRoundedCoveredCorners(drawList, rect) &&
            HasCoveredVertexNear(
                drawList,
                0,
                drawList.VtxBuffer.Size,
                coveredInterior);
    }

    bool IsCheckerVertex(const ImDrawVert& vertex)
    {
        const int red =
            (vertex.col >> IM_COL32_R_SHIFT) & 0xff;
        const int green =
            (vertex.col >> IM_COL32_G_SHIFT) & 0xff;
        const int blue =
            (vertex.col >> IM_COL32_B_SHIFT) & 0xff;
        return red == green && green == blue &&
            (red == 128 || red == 204);
    }

    float RoundedRectDistance(
        const ImRect& bounds,
        float rounding,
        const ImVec2& position)
    {
        const ImVec2 center = bounds.GetCenter();
        const ImVec2 size = bounds.GetSize();
        const ImVec2 halfSize(size.x * 0.5f, size.y * 0.5f);
        const ImVec2 roundedHalfSize(
            ImMax(halfSize.x - rounding, 0.0f),
            ImMax(halfSize.y - rounding, 0.0f));
        const ImVec2 offset(
            ImAbs(position.x - center.x) - roundedHalfSize.x,
            ImAbs(position.y - center.y) - roundedHalfSize.y);
        const ImVec2 outside(
            ImMax(offset.x, 0.0f),
            ImMax(offset.y, 0.0f));
        return ImSqrt(ImLengthSqr(outside)) +
            ImMin(ImMax(offset.x, offset.y), 0.0f) - rounding;
    }

    bool HasTightRoundedCheckerCoverage(
        const ImDrawList& drawList,
        const ImRect& rect,
        float rounding)
    {
        if (rect.GetWidth() <= 0.0f ||
            rect.GetHeight() <= 0.0f ||
            rounding <= 1.0f)
        {
            return false;
        }

        bool coveredInside[8] = {};
        bool clearOutside[8] = {};
        const float leftCornerEnd = rect.Min.x + rounding;
        const float rightCornerStart = rect.Max.x - rounding;
        const float topCornerEnd = rect.Min.y + rounding;
        const float bottomCornerStart = rect.Max.y - rounding;
        for (const ImDrawVert& vertex : drawList.VtxBuffer)
        {
            if (!IsCheckerVertex(vertex) ||
                vertex.pos.x < rect.Min.x - 1.5f ||
                vertex.pos.x > rect.Max.x + 1.5f ||
                vertex.pos.y < rect.Min.y - 1.5f ||
                vertex.pos.y > rect.Max.y + 1.5f)
            {
                continue;
            }

            const float distance = RoundedRectDistance(
                rect,
                rounding,
                vertex.pos);
            const float alpha = Alpha(vertex.col);
            const bool tightlyCovered =
                distance >= -0.9f &&
                distance <= -0.25f &&
                alpha >= 0.70f;
            const bool tightlyClear =
                distance >= 0.25f &&
                distance <= 0.9f &&
                alpha <= 0.30f;
            bool regions[8] = {
                vertex.pos.x >= leftCornerEnd &&
                    vertex.pos.x <= rightCornerStart,
                vertex.pos.x >= leftCornerEnd &&
                    vertex.pos.x <= rightCornerStart,
                vertex.pos.y >= topCornerEnd &&
                    vertex.pos.y <= bottomCornerStart,
                vertex.pos.y >= topCornerEnd &&
                    vertex.pos.y <= bottomCornerStart,
                vertex.pos.x < leftCornerEnd &&
                    vertex.pos.y < topCornerEnd,
                vertex.pos.x > rightCornerStart &&
                    vertex.pos.y < topCornerEnd,
                vertex.pos.x < leftCornerEnd &&
                    vertex.pos.y > bottomCornerStart,
                vertex.pos.x > rightCornerStart &&
                    vertex.pos.y > bottomCornerStart
            };
            regions[0] &= vertex.pos.y <= rect.Min.y + 1.5f;
            regions[1] &= vertex.pos.y >= rect.Max.y - 1.5f;
            regions[2] &= vertex.pos.x <= rect.Min.x + 1.5f;
            regions[3] &= vertex.pos.x >= rect.Max.x - 1.5f;
            for (int region = 0; region < IM_ARRAYSIZE(regions); ++region)
            {
                if (!regions[region])
                    continue;
                coveredInside[region] |= tightlyCovered;
                clearOutside[region] |= tightlyClear;
            }
        }

        for (int region = 0; region < IM_ARRAYSIZE(coveredInside); ++region)
        {
            if (!coveredInside[region] || !clearOutside[region])
                return false;
        }
        return true;
    }

    bool HasHollowCircleMarkerAt(
        const ImDrawList& drawList,
        const ImVec2& center,
        float markerRadius,
        float fringe)
    {
        int darkVertices = 0;
        int whiteVertices = 0;
        for (const ImDrawVert& vertex : drawList.VtxBuffer)
        {
            const int red =
                (vertex.col >> IM_COL32_R_SHIFT) & 0xff;
            const int green =
                (vertex.col >> IM_COL32_G_SHIFT) & 0xff;
            const int blue =
                (vertex.col >> IM_COL32_B_SHIFT) & 0xff;
            const bool dark = red == 24 && green == 24 && blue == 24;
            const bool white = red == 255 && green == 255 && blue == 255;
            if (!dark && !white)
                continue;
            const float distance = ImSqrt(ImLengthSqr(
                Subtract(vertex.pos, center)));
            const float minimumRadius = dark
                ? markerRadius + fringe * 1.90f
                : markerRadius + fringe * 0.35f;
            const float maximumRadius = dark
                ? markerRadius + fringe * 2.65f
                : markerRadius + fringe * 1.25f;
            if (distance >= minimumRadius && distance <= maximumRadius)
            {
                darkVertices += dark ? 1 : 0;
                whiteVertices += white ? 1 : 0;
            }
        }
        return darkVertices >= 6 && whiteVertices >= 6;
    }

    bool HasCarvedOutlineCoverage(
        const ImDrawList& drawList,
        const ImRect& bounds,
        float expectedEdgeDistance = 0.5f)
    {
        std::array<bool, 4> edgeCovered = { false, false, false, false };
        const ImRect expandedBounds(
            Subtract(bounds.Min, ImVec2(1.0f, 1.0f)),
            Add(bounds.Max, ImVec2(1.0f, 1.0f)));
        for (const ImDrawVert& vertex : drawList.VtxBuffer)
        {
            const float alpha = Alpha(vertex.col);
            if (alpha <= 0.0f || alpha > 0.145f ||
                !expandedBounds.Contains(vertex.pos))
                continue;
            edgeCovered[0] |= std::abs(
                std::abs(vertex.pos.x - bounds.Min.x) -
                    expectedEdgeDistance) <= 0.15f;
            edgeCovered[1] |= std::abs(
                std::abs(vertex.pos.x - bounds.Max.x) -
                    expectedEdgeDistance) <= 0.15f;
            edgeCovered[2] |= std::abs(
                std::abs(vertex.pos.y - bounds.Min.y) -
                    expectedEdgeDistance) <= 0.15f;
            edgeCovered[3] |= std::abs(
                std::abs(vertex.pos.y - bounds.Max.y) -
                    expectedEdgeDistance) <= 0.15f;
        }
        for (bool covered : edgeCovered)
        {
            if (!covered)
                return false;
        }
        return true;
    }

    bool HasVisibleWheelOutlineCoverage(
        const ImDrawList& drawList,
        const ImVec2& center,
        float radius,
        float fringe,
        int* coveredBinCount,
        float* topMaximumAlpha,
        float* bottomMaximumAlpha)
    {
        constexpr int AngularBinCount = 12;
        std::array<bool, AngularBinCount> coveredBins{};
        float topAlpha = 0.0f;
        float bottomAlpha = 0.0f;
        const float radialTolerance = ImMax(1.25f, fringe * 1.5f);
        for (const ImDrawVert& vertex : drawList.VtxBuffer)
        {
            const float radialDistance = ImSqrt(ImLengthSqr(
                Subtract(vertex.pos, center)));
            if (std::abs(radialDistance - radius) > radialTolerance)
                continue;

            const ImVec4 color = ImGui::ColorConvertU32ToFloat4(vertex.col);
            const float chroma = ImMax(color.x, ImMax(color.y, color.z)) -
                ImMin(color.x, ImMin(color.y, color.z));
            if (color.w < 0.30f || chroma > 0.05f || color.x < 0.90f)
                continue;

            float angle = ImAtan2(
                vertex.pos.y - center.y,
                vertex.pos.x - center.x);
            if (angle < 0.0f)
                angle += IM_PI * 2.0f;
            const int bin = ImClamp(
                int(angle / (IM_PI * 2.0f) * AngularBinCount),
                0,
                AngularBinCount - 1);
            coveredBins[bin] = true;
            if (vertex.pos.y < center.y - radius * 0.65f)
                topAlpha = ImMax(topAlpha, color.w);
            if (vertex.pos.y > center.y + radius * 0.65f)
                bottomAlpha = ImMax(bottomAlpha, color.w);
        }
        int covered = 0;
        for (bool binCovered : coveredBins)
            covered += binCovered ? 1 : 0;
        if (coveredBinCount != nullptr)
            *coveredBinCount = covered;
        if (topMaximumAlpha != nullptr)
            *topMaximumAlpha = topAlpha;
        if (bottomMaximumAlpha != nullptr)
            *bottomMaximumAlpha = bottomAlpha;
        return
            covered >= 10 &&
            topAlpha >= 0.75f &&
            bottomAlpha >= 0.45f &&
            topAlpha - bottomAlpha >= 0.20f;
    }

    bool HasComparisonBarCoverage(
        const ImDrawList& drawList,
        const ImRect& bar)
    {
        bool checker = false;
        bool colorOverlay = false;
        for (const ImDrawVert& vertex : drawList.VtxBuffer)
        {
            if (!bar.Contains(vertex.pos))
                continue;
            checker |= IsCheckerVertex(vertex) && Alpha(vertex.col) > 0.5f;
            const int red =
                (vertex.col >> IM_COL32_R_SHIFT) & 0xff;
            const int green =
                (vertex.col >> IM_COL32_G_SHIFT) & 0xff;
            const int blue =
                (vertex.col >> IM_COL32_B_SHIFT) & 0xff;
            colorOverlay |=
                !(red == green && green == blue) &&
                Alpha(vertex.col) > 0.25f;
        }
        return checker && colorOverlay;
    }

    GradientFrameObservation ObserveGradientFrame(
        const ImDrawList& drawList,
        int firstVertex,
        int fillVertexEnd,
        int lastVertex,
        const ImRect& frame,
        ImU32 topFill,
        ImU32 bottomFill)
    {
        GradientFrameObservation observation;
        fillVertexEnd = ImClamp(fillVertexEnd, firstVertex, lastVertex);
        const ImVec4 topFillValue =
            ImGui::ColorConvertU32ToFloat4(topFill);
        const ImVec4 bottomFillValue =
            ImGui::ColorConvertU32ToFloat4(bottomFill);
        const float gradientExtent = ImMax(frame.GetHeight(), 1.0f);
        const float midpoint = (frame.Min.y + frame.Max.y) * 0.5f;
        for (int index = firstVertex; index < fillVertexEnd; ++index)
        {
            const ImDrawVert& vertex = drawList.VtxBuffer[index];
            const float gradientPosition = ImSaturate(
                (vertex.pos.y - frame.Min.y) / gradientExtent);
            const float expectedAlpha = ImLerp(
                topFillValue.w,
                bottomFillValue.w,
                gradientPosition);
            const float actualAlpha = Alpha(vertex.col);
            constexpr float QuantizationTolerance = 2.0f / 255.0f;
            observation.fillCoverageBounded &=
                actualAlpha <= expectedAlpha + QuantizationTolerance;
            if (actualAlpha <= 1.0f / 255.0f)
            {
                ++observation.transparentFillFringeVertexCount;
                continue;
            }
            if (actualAlpha + QuantizationTolerance < expectedAlpha)
                continue;

            ++observation.coveredFillVertexCount;
            if (vertex.pos.y <= midpoint)
            {
                observation.topFillLuminance += Luminance(vertex.col);
                ++observation.topFillVertexCount;
            }
            else
            {
                observation.bottomFillLuminance += Luminance(vertex.col);
                ++observation.bottomFillVertexCount;
            }
        }
        for (int index = fillVertexEnd; index < lastVertex; ++index)
        {
            const ImDrawVert& vertex = drawList.VtxBuffer[index];
            observation.outlineBounds.Min.x = ImMin(
                observation.outlineBounds.Min.x,
                vertex.pos.x);
            observation.outlineBounds.Min.y = ImMin(
                observation.outlineBounds.Min.y,
                vertex.pos.y);
            observation.outlineBounds.Max.x = ImMax(
                observation.outlineBounds.Max.x,
                vertex.pos.x);
            observation.outlineBounds.Max.y = ImMax(
                observation.outlineBounds.Max.y,
                vertex.pos.y);
            const float alpha = Alpha(vertex.col);
            if (alpha <= 1.0f / 255.0f)
            {
                ++observation.transparentOutlineFringeVertexCount;
                continue;
            }
            observation.maximumOutlinePremultipliedLuminance = ImMax(
                observation.maximumOutlinePremultipliedLuminance,
                Luminance(vertex.col) * alpha);
            if (vertex.pos.y <= frame.Min.y + 2.0f)
            {
                observation.maximumTopOutlineAlpha = ImMax(
                    observation.maximumTopOutlineAlpha,
                    alpha);
            }
            if (vertex.pos.y >= frame.Max.y - 2.0f)
            {
                observation.maximumBottomOutlineAlpha = ImMax(
                    observation.maximumBottomOutlineAlpha,
                    alpha);
            }
            if (vertex.pos.y <= midpoint)
            {
                observation.topOutlineLuminance += Luminance(vertex.col);
                ++observation.topOutlineVertexCount;
            }
            else
            {
                observation.bottomOutlineLuminance += Luminance(vertex.col);
                ++observation.bottomOutlineVertexCount;
            }
        }
        if (observation.topFillVertexCount > 0)
        {
            observation.topFillLuminance /=
                float(observation.topFillVertexCount);
        }
        if (observation.bottomFillVertexCount > 0)
        {
            observation.bottomFillLuminance /=
                float(observation.bottomFillVertexCount);
        }
        if (observation.topOutlineVertexCount > 0)
        {
            observation.topOutlineLuminance /=
                float(observation.topOutlineVertexCount);
        }
        if (observation.bottomOutlineVertexCount > 0)
        {
            observation.bottomOutlineLuminance /=
                float(observation.bottomOutlineVertexCount);
        }
        return observation;
    }

    void QueueMouse(const ImVec2& position, bool down)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(position.x, position.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
    }

    void QueueMouseButton(
        const ImVec2& position,
        ImGuiMouseButton button,
        bool down)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(position.x, position.y);
        io.AddMouseButtonEvent(button, down);
    }

    ImVec2 Center(const ImRect& rect)
    {
        return ImVec2(
            (rect.Min.x + rect.Max.x) * 0.5f,
            (rect.Min.y + rect.Max.y) * 0.5f);
    }

    SliderFrameObservation SubmitGatedSliderFrame(
        const char* ownerName,
        int& storedValue,
        bool gateEnabled,
        float disabledPresentationAmount = 1.0f)
    {
        constexpr int MinimumValue = 0;
        constexpr int MaximumValue = 6;
        constexpr float SliderWidth = 220.0f;
        const ImVec4 drawerTrack(0.13f, 0.17f, 0.22f, 0.79f);

        ImGui::SetUvsrUiBehavior(true, false, true);
        ImGui::SetUvsrSliderTrackColors(
            drawerTrack,
            drawerTrack,
            drawerTrack);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(280.0f, 80.0f), ImGuiCond_Always);
        ImGui::Begin(
            ownerName,
            nullptr,
            ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoBackground);
        ImGui::SetCursorPos(ImVec2(20.0f, 20.0f));
        ImGui::SetNextItemWidth(SliderWidth);

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiID sliderId = window->GetID("##GatedSamples");
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const int firstVertex = drawList->VtxBuffer.Size;
        int presentedValue = gateEnabled ? storedValue : MinimumValue;
        if (!gateEnabled)
        {
            const float presentationAmount = ImSaturate(
                disabledPresentationAmount);
            ImGui::PushStyleVar(
                ImGuiStyleVar_Alpha,
                ImGui::GetStyle().Alpha *
                    (1.0f +
                        (ImGui::GetStyle().DisabledAlpha - 1.0f) *
                            presentationAmount));
            ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
            ImGui::PushUvsrDisabledPresentation(presentationAmount);
            ImGui::BeginDisabled(true);
        }
        if (ImGui::SliderInt(
                "##GatedSamples",
                &presentedValue,
                MinimumValue,
                MaximumValue,
                "%d",
                ImGuiSliderFlags_AlwaysClamp))
        {
            storedValue = presentedValue;
        }
        if (!gateEnabled)
        {
            ImGui::EndDisabled();
            ImGui::PopUvsrDisabledPresentation();
            ImGui::PopStyleVar(2);
        }
        const int lastVertex = drawList->VtxBuffer.Size;
        const ImRect itemRect(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax());

        SliderFrameObservation observation;
        ImGuiStorage& storage = window->StateStorage;
        const ImGuiID animationKey = ImHashStr(
            "##SliderGrabAnimation",
            0,
            sliderId);
        observation.animatedCenterX = storage.GetFloat(
            animationKey,
            -FLT_MAX);
        observation.hasGrabAnimationState =
            observation.animatedCenterX != -FLT_MAX;
        const ImVec2 whitePixel = drawList->_Data->TexUvWhitePixel;
        constexpr float ColorTolerance = 2.0f / 255.0f;
        const float disabledTrackLuminance =
            drawerTrack.x * 0.299f +
            drawerTrack.y * 0.587f +
            drawerTrack.z * 0.114f;
        for (int index = firstVertex; index < lastVertex; ++index)
        {
            const ImDrawVert& vertex = drawList->VtxBuffer[index];
            const ImVec4 color = ImGui::ColorConvertU32ToFloat4(vertex.col);
            const bool usesWhitePixel =
                Near(vertex.uv.x, whitePixel.x) &&
                Near(vertex.uv.y, whitePixel.y);
            if (usesWhitePixel &&
                std::abs(color.x - drawerTrack.x) <= ColorTolerance &&
                std::abs(color.y - drawerTrack.y) <= ColorTolerance &&
                std::abs(color.z - drawerTrack.z) <= ColorTolerance)
            {
                observation.maximumDrawerTrackAlpha = ImMax(
                    observation.maximumDrawerTrackAlpha,
                    color.w);
            }
            if (usesWhitePixel &&
                std::abs(color.x - disabledTrackLuminance) <=
                    ColorTolerance &&
                std::abs(color.y - disabledTrackLuminance) <=
                    ColorTolerance &&
                std::abs(color.z - disabledTrackLuminance) <=
                    ColorTolerance)
            {
                observation.maximumDisabledTrackAlpha = ImMax(
                    observation.maximumDisabledTrackAlpha,
                    color.w);
            }
        }

        if (observation.hasGrabAnimationState)
        {
            const float grabSize = ImMax(
                1.0f,
                ImGui::GetFrameHeight() - 8.0f);
            ImRect grabRect(
                ImVec2(
                    observation.animatedCenterX - grabSize * 0.5f,
                    itemRect.Min.y + 4.0f),
                ImVec2(
                    observation.animatedCenterX + grabSize * 0.5f,
                    itemRect.Max.y - 4.0f));
            const float grabMidpointY =
                (grabRect.Min.y + grabRect.Max.y) * 0.5f;
            constexpr float BoundsTolerance = 1.5f;
            for (int index = firstVertex; index < lastVertex; ++index)
            {
                const ImDrawVert& vertex = drawList->VtxBuffer[index];
                if (vertex.pos.x < grabRect.Min.x - BoundsTolerance ||
                    vertex.pos.x > grabRect.Max.x + BoundsTolerance ||
                    vertex.pos.y < grabRect.Min.y - BoundsTolerance ||
                    vertex.pos.y > grabRect.Max.y + BoundsTolerance)
                {
                    continue;
                }

                const float alpha = Alpha(vertex.col);
                const float luminance = Luminance(vertex.col);
                const bool usesWhitePixel =
                    Near(vertex.uv.x, whitePixel.x) &&
                    Near(vertex.uv.y, whitePixel.y);
                if (usesWhitePixel &&
                    alpha > 1.0f / 255.0f &&
                    luminance >= 0.50f)
                {
                    observation.maximumGrabAlpha = ImMax(
                        observation.maximumGrabAlpha,
                        alpha);
                    if (vertex.pos.y <= grabMidpointY)
                    {
                        observation.topGrabLuminance += luminance;
                        ++observation.topGrabVertexCount;
                    }
                    else
                    {
                        observation.bottomGrabLuminance += luminance;
                        ++observation.bottomGrabVertexCount;
                    }
                    continue;
                }

                const float edgeDistance = ImMin(
                    ImMin(
                        ImFabs(vertex.pos.x - grabRect.Min.x),
                        ImFabs(vertex.pos.x - grabRect.Max.x)),
                    ImMin(
                        ImFabs(vertex.pos.y - grabRect.Min.y),
                        ImFabs(vertex.pos.y - grabRect.Max.y)));
                if (alpha > 1.0f / 255.0f &&
                    luminance < 0.40f &&
                    edgeDistance <= 2.0f)
                {
                    ++observation.outlineVertexCount;
                }
            }
            if (observation.topGrabVertexCount > 0)
            {
                observation.topGrabLuminance /=
                    float(observation.topGrabVertexCount);
            }
            if (observation.bottomGrabVertexCount > 0)
            {
                observation.bottomGrabLuminance /=
                    float(observation.bottomGrabVertexCount);
            }
        }

        ImGui::End();
        ImGui::Render();
        return observation;
    }

    SliderLaneLayoutObservation SubmitSliderLaneLayoutFrame(
        int& value,
        float sliderWidth = 220.0f,
        const char* ownerName = "Slider Value Lane Layout Test",
        const ImVec2& ownerPosition = ImVec2(18.0f, 18.0f))
    {
        ImGui::SetUvsrUiBehavior(true, false, true);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ownerPosition, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(340.0f, 150.0f), ImGuiCond_Always);
        ImGui::SetNextWindowFocus();
        ImGui::Begin(
            ownerName,
            nullptr,
            ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoTitleBar);
        ImGui::SetCursorPos(ImVec2(18.0f, 18.0f));
        ImGui::SetNextItemWidth(sliderWidth);
        const ImGuiID sliderId = ImGui::GetID("##ValueLane");
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const int firstSliderVertex = drawList->VtxBuffer.Size;
        ImGui::SliderInt(
            "##ValueLane",
            &value,
            0,
            100,
            "%d",
            ImGuiSliderFlags_AlwaysClamp);
        const int lastSliderVertex = drawList->VtxBuffer.Size;

        SliderLaneLayoutObservation observation;
        observation.sliderRect = ImRect(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax());
        observation.sliderVisualRect = observation.sliderRect;
        observation.sliderId = sliderId;
        observation.activeId = GImGui->ActiveId;
        observation.sliderHovered = ImGui::IsItemHovered();
        observation.sliderVisualRect.Expand(ImVec2(0.0f, -2.0f));
        const float valueWidth =
            IM_TRUNC(ImGui::GetFrameHeight() * 1.72f) * 2.0f;
        constexpr float BubbleGap = 2.0f;
        observation.valueLaneUsable =
            observation.sliderVisualRect.GetWidth() - valueWidth - BubbleGap >=
                ImGui::GetFrameHeight();
        observation.trackRect = observation.sliderVisualRect;
        if (observation.valueLaneUsable)
        {
            observation.valueRect = observation.sliderVisualRect;
            observation.valueRect.Min.x =
                observation.valueRect.Max.x - valueWidth;
            observation.trackRect.Max.x =
                observation.valueRect.Min.x - BubbleGap;
            observation.trackFaceRounded = HasRoundedFillCorners(
                *drawList,
                firstSliderVertex,
                lastSliderVertex,
                observation.trackRect);
            observation.valueFaceRounded = HasRoundedFillCorners(
                *drawList,
                firstSliderVertex,
                lastSliderVertex,
                observation.valueRect);
        }
        observation.tempInputActive = ImGui::TempInputIsActive(sliderId);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::Button("Reset##ValueLane", ImVec2(42.0f, 0.0f));
        observation.sameLineRect = ImRect(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax());
        ImGui::Button("Following Row##ValueLane", ImVec2(140.0f, 0.0f));
        observation.followingRowRect = ImRect(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax());
        bool toggleValue = false;
        ImGui::Checkbox("##ValueLaneToggleWidth", &toggleValue);
        observation.toggleRect = ImRect(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax());
        ImGui::End();
        ImGui::Render();
        return observation;
    }

    TooltipObservation SubmitTooltipFrame(
        bool stockWidgetRendering,
        bool submitTooltip = true,
        bool motionEnabled = true,
        const char* tooltipText = nullptr,
        bool nestedZeroWindowPadding = false,
        bool captureRenderedText = false,
        bool useDynamicFormat = false)
    {
        static constexpr const char* TooltipText =
            "UVSR tooltip follows the mouse with stock typography.";
        if (tooltipText == nullptr)
            tooltipText = TooltipText;

        ImGui::SetUvsrUiBehavior(
            motionEnabled,
            stockWidgetRendering,
            false);
        ImGui::SetUvsrAuthoredWindowPadding(
            ImGui::GetStyle().WindowPadding);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(
            ImVec2(30.0f, 210.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(240.0f, 60.0f),
            ImGuiCond_Always);
        ImGui::Begin(
            "Tooltip Policy Test Owner",
            nullptr,
            ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar);
        if (nestedZeroWindowPadding)
        {
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(0.0f, 0.0f));
        }
        ImGui::Button("Tooltip Owner", ImVec2(150.0f, 0.0f));
        const ImRect ownerRect(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax());
        std::string renderedTooltipText;
        if (submitTooltip)
        {
            if (captureRenderedText)
                ImGui::LogToBuffer();
            if (useDynamicFormat)
                ImGui::SetItemTooltip("Material: %s", tooltipText);
            else
                ImGui::SetItemTooltip("%s", tooltipText);
            if (captureRenderedText)
            {
                renderedTooltipText.assign(
                    GImGui->LogBuffer.c_str(),
                    GImGui->LogBuffer.size());
                ImGui::LogFinish();
            }
        }
        if (nestedZeroWindowPadding)
            ImGui::PopStyleVar();

        ImGui::End();
        ImGui::Render();

        TooltipObservation observation;
        observation.renderedText = renderedTooltipText;
        observation.ownerRect = ownerRect;
        observation.mousePosition = ImGui::GetIO().MousePos;
        ImGuiWindow* tooltipWindow = GImGui->TooltipPreviousWindow;
        if (tooltipWindow != nullptr &&
            tooltipWindow->Active &&
            !tooltipWindow->Hidden)
        {
            observation.submitted = true;
            observation.noInputs =
                (tooltipWindow->Flags & ImGuiWindowFlags_NoInputs) != 0;
            observation.alwaysAutoResize =
                (tooltipWindow->Flags &
                    ImGuiWindowFlags_AlwaysAutoResize) != 0;
            observation.hasVerticalScrollbar = tooltipWindow->ScrollbarY;
            observation.windowSize = tooltipWindow->SizeFull;
            observation.windowPosition = tooltipWindow->Pos;
            observation.windowPadding = tooltipWindow->WindowPadding;
            observation.contentSize = tooltipWindow->ContentSize;
            const ImVec2 whitePixel =
                tooltipWindow->DrawList->_Data->TexUvWhitePixel;
            for (const ImDrawVert& vertex :
                tooltipWindow->DrawList->VtxBuffer)
            {
                if (Alpha(vertex.col) <= 0.0f)
                    continue;
                observation.visualBounds.Add(vertex.pos);
                if (!Near(vertex.uv.x, whitePixel.x) ||
                    !Near(vertex.uv.y, whitePixel.y))
                {
                    observation.textBounds.Add(vertex.pos);
                }
                observation.maximumVertexAlpha = ImMax(
                    observation.maximumVertexAlpha,
                    Alpha(vertex.col));
            }
        }
        return observation;
    }

    TooltipDragObservation SubmitTooltipDragIsolationFrame()
    {
        ImGui::SetUvsrUiBehavior(true, false, false);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(
            ImVec2(30.0f, 210.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(240.0f, 60.0f),
            ImGuiCond_Always);
        ImGui::Begin(
            "Tooltip Policy Test Owner",
            nullptr,
            ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar);
        ImGui::Button("Tooltip Owner", ImVec2(150.0f, 0.0f));
        ImGui::SetItemTooltip("Retained tooltip must yield to drag/drop.");
        ImGuiWindow* retainedTooltip =
            GImGui->UvsrTooltipState.LastSubmittedWindow;

        GImGui->DragDropActive = true;
        GImGui->DragDropSourceFrameCount = GImGui->FrameCount;
        GImGui->DragDropMouseButton = ImGuiMouseButton_Left;
        GImGui->DragDropWithinSource = true;
        ImGui::SetTooltip("Upstream drag preview");
        GImGui->DragDropWithinSource = false;

        ImGui::End();
        ImGui::Render();

        TooltipDragObservation observation;
        observation.retainedTooltipInactive =
            retainedTooltip == nullptr ||
            !retainedTooltip->Active ||
            retainedTooltip->Hidden;
        observation.retainedOwnerReset =
            GImGui->UvsrTooltipState.OwnerId == 0;
        for (ImGuiWindow* window : GImGui->Windows)
        {
            if (window->Name != nullptr &&
                strncmp(window->Name, "##Tooltip_DragDrop_", 19) == 0 &&
                window->Active)
            {
                observation.dragTooltipActive = true;
                observation.dragTooltipNoInputs =
                    (window->Flags & ImGuiWindowFlags_NoInputs) != 0;
            }
        }
        ImGui::ClearDragDrop();
        return observation;
    }

    FrameObservation SubmitComboFrame(
        ComboHarness& harness,
        float ownerY,
        bool forceOpen)
    {
        FrameObservation observation;
        const float originalItemSpacingY =
            ImGui::GetStyle().ItemSpacing.y;
        const float originalWindowPaddingY =
            ImGui::GetStyle().WindowPadding.y;

        ImGui::NewFrame();
        observation.frame = ImGui::GetFrameCount();
        ImGui::SetNextWindowPos(
            ImVec2(30.0f, ownerY),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(240.0f, 60.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowCollapsed(
            false,
            ImGuiCond_Always);
        const ImGuiWindowFlags ownerFlags =
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;
        const bool ownerVisible = ImGui::Begin(
            "Combo Popup Test Owner",
            nullptr,
            ownerFlags);
        if (ownerVisible)
        {
            harness.comboId = ImGui::GetID("##Mode");
            harness.popupId = ImHashStr(
                "##ComboPopup",
                0,
                harness.comboId);
            if (forceOpen)
                ImGui::OpenPopupEx(
                    harness.popupId,
                    ImGuiPopupFlags_None);

            ImGui::SetNextItemWidth(ComboWidth);
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(ComboWidth, PopupHeight),
                ImVec2(ComboWidth, PopupHeight));
            const ImVec2 comboMin = ImGui::GetCursorScreenPos();
            observation.comboRect = ImRect(
                comboMin,
                ImVec2(
                    comboMin.x + ComboWidth,
                    comboMin.y + ImGui::GetFrameHeight()));

            const char* preview =
                harness.selectedItem == 0 ? "Option A" : "None";
            ImGuiWindow* ownerWindow = ImGui::GetCurrentWindow();
            ImDrawList* ownerDrawList = ownerWindow->DrawList;
            const int firstComboVertex = ownerDrawList->VtxBuffer.Size;
            observation.popupBegan = ImGui::BeginCombo(
                "##Mode",
                preview);
            const int lastComboVertex = ownerDrawList->VtxBuffer.Size;
            const ImVec2 ownerWhitePixel =
                ownerDrawList->_Data->TexUvWhitePixel;
            for (int vertexIndex = firstComboVertex;
                vertexIndex < lastComboVertex;
                ++vertexIndex)
            {
                const ImDrawVert& vertex =
                    ownerDrawList->VtxBuffer[vertexIndex];
                if (!Near(vertex.uv.x, ownerWhitePixel.x) ||
                    !Near(vertex.uv.y, ownerWhitePixel.y) ||
                    Alpha(vertex.col) <= 1.0f / 255.0f ||
                    !observation.comboRect.Contains(vertex.pos))
                {
                    continue;
                }
                observation.comboVisualBounds.Min.x = ImMin(
                    observation.comboVisualBounds.Min.x,
                    vertex.pos.x);
                observation.comboVisualBounds.Min.y = ImMin(
                    observation.comboVisualBounds.Min.y,
                    vertex.pos.y);
                observation.comboVisualBounds.Max.x = ImMax(
                    observation.comboVisualBounds.Max.x,
                    vertex.pos.x);
                observation.comboVisualBounds.Max.y = ImMax(
                    observation.comboVisualBounds.Max.y,
                    vertex.pos.y);
                observation.comboVisualBoundsValid = true;
            }
            if (observation.popupBegan)
            {
                ImGuiWindow* popupWindow = ImGui::GetCurrentWindow();
                ImGuiStyle& style = ImGui::GetStyle();
                observation.popupItemSpacingY = style.ItemSpacing.y;
                observation.popupRounding = popupWindow->WindowRounding;
                observation.popupWindowPadding = popupWindow->WindowPadding;

                constexpr std::array<const char*, 3> OptionLabels = {
                    "Option A",
                    "Option B",
                    "Option C"
                };
                for (int optionIndex = 0;
                    optionIndex < int(OptionLabels.size());
                    ++optionIndex)
                {
                    const int firstVertex =
                        popupWindow->DrawList->VtxBuffer.Size;
                    const bool pressed = ImGui::Selectable(
                        OptionLabels[optionIndex],
                        harness.selectedItem == optionIndex);
                    observation.optionRects[optionIndex] = ImRect(
                        ImGui::GetItemRectMin(),
                        ImGui::GetItemRectMax());
                    const int lastVertex =
                        popupWindow->DrawList->VtxBuffer.Size;
                    const ImVec2 whitePixel =
                        popupWindow->DrawList->_Data->TexUvWhitePixel;
                    for (int vertexIndex = firstVertex;
                        vertexIndex < lastVertex;
                        ++vertexIndex)
                    {
                        const ImDrawVert& vertex =
                            popupWindow->DrawList->VtxBuffer[vertexIndex];
                        if (Near(vertex.uv.x, whitePixel.x) &&
                            Near(vertex.uv.y, whitePixel.y) &&
                            observation.optionRects[optionIndex].Contains(
                                vertex.pos))
                        {
                            observation.maximumOptionFillAlpha[optionIndex] =
                                ImMax(
                                    observation.maximumOptionFillAlpha[
                                        optionIndex],
                                    Alpha(vertex.col));
                        }
                    }
                    observation.optionFillRounded[optionIndex] =
                        HasRoundedFillCorners(
                            *popupWindow->DrawList,
                            firstVertex,
                            lastVertex,
                            observation.optionRects[optionIndex]);
                    const float optionHeight =
                        observation.optionRects[optionIndex].GetHeight();
                    if (optionIndex == 0)
                    {
                        observation.optionPressed = pressed;
                        observation.optionId = ImGui::GetItemID();
                        observation.optionRect =
                            observation.optionRects[optionIndex];
                        observation.minimumOptionHeight = optionHeight;
                        observation.maximumOptionHeight = optionHeight;
                        if (pressed)
                        {
                            harness.pendingSelectedItem = 0;
                            if (!harness.deferSelectionCommit)
                                harness.selectedItem = 0;
                            ++harness.optionPressCount;
                        }
                    }
                    else
                    {
                        observation.minimumOptionHeight = ImMin(
                            observation.minimumOptionHeight,
                            optionHeight);
                        observation.maximumOptionHeight = ImMax(
                            observation.maximumOptionHeight,
                            optionHeight);
                    }
                }
                observation.minimumOptionGap =
                    observation.optionRects[1].Min.y -
                    observation.optionRects[0].Max.y;
                observation.maximumOptionGap =
                    observation.minimumOptionGap;
                const float finalOptionGap =
                    observation.optionRects[2].Min.y -
                    observation.optionRects[1].Max.y;
                observation.minimumOptionGap = ImMin(
                    observation.minimumOptionGap,
                    finalOptionGap);
                observation.maximumOptionGap = ImMax(
                    observation.maximumOptionGap,
                    finalOptionGap);

                observation.activeId = GImGui->ActiveId;
                const int firstSharedHighlightVertex =
                    popupWindow->DrawList->VtxBuffer.Size;
                ImGui::EndCombo();
                const int lastSharedHighlightVertex =
                    popupWindow->DrawList->VtxBuffer.Size;
                const auto highlightKey = [popupWindow](const char* suffix)
                {
                    return ImHashStr(suffix, 0, popupWindow->PopupId);
                };
                ImGuiStorage& popupStorage = popupWindow->StateStorage;
                observation.popupRollAmount = popupStorage.GetFloat(
                    highlightKey("##ComboPopupRollAmount"),
                    1.0f);
                observation.popupClosing = popupStorage.GetInt(
                    highlightKey("##ComboPopupClosing"),
                    0) != 0;
                observation.popupInteractionReady = popupStorage.GetInt(
                    highlightKey("##ComboPopupInteractionReady"),
                    1) != 0;
                observation.popupCloseReadyFrame = popupStorage.GetInt(
                    highlightKey("##ComboPopupCloseReadyFrame"),
                    -1);
                observation.popupRollFromBottom = popupStorage.GetInt(
                    highlightKey("##ComboPopupRollFromBottom"),
                    0) != 0;
                observation.sharedHighlightPriority = popupStorage.GetInt(
                    highlightKey("##UvsrComboHighlightTargetPriority"),
                    0);
                observation.sharedHighlightValid = popupStorage.GetInt(
                    highlightKey("##UvsrComboHighlightValid"),
                    0) != 0;
                const auto readHighlightRect =
                    [&](const char* minimumX,
                        const char* minimumY,
                        const char* maximumX,
                        const char* maximumY)
                    {
                        return ImRect(
                            ImVec2(
                                popupStorage.GetFloat(
                                    highlightKey(minimumX),
                                    0.0f),
                                popupStorage.GetFloat(
                                    highlightKey(minimumY),
                                    0.0f)),
                            ImVec2(
                                popupStorage.GetFloat(
                                    highlightKey(maximumX),
                                    0.0f),
                                popupStorage.GetFloat(
                                    highlightKey(maximumY),
                                    0.0f)));
                    };
                const ImRect currentLocal = readHighlightRect(
                    "##UvsrComboCurrentMinX",
                    "##UvsrComboCurrentMinY",
                    "##UvsrComboCurrentMaxX",
                    "##UvsrComboCurrentMaxY");
                const ImRect targetLocal = readHighlightRect(
                    "##UvsrComboTargetMinX",
                    "##UvsrComboTargetMinY",
                    "##UvsrComboTargetMaxX",
                    "##UvsrComboTargetMaxY");
                observation.sharedHighlightRect = ImRect(
                    ImVec2(
                        currentLocal.Min.x + popupWindow->Pos.x,
                        currentLocal.Min.y + popupWindow->Pos.y),
                    ImVec2(
                        currentLocal.Max.x + popupWindow->Pos.x,
                        currentLocal.Max.y + popupWindow->Pos.y));
                observation.sharedHighlightTargetRect = ImRect(
                    ImVec2(
                        targetLocal.Min.x + popupWindow->Pos.x,
                        targetLocal.Min.y + popupWindow->Pos.y),
                    ImVec2(
                        targetLocal.Max.x + popupWindow->Pos.x,
                        targetLocal.Max.y + popupWindow->Pos.y));
                const ImVec2 popupWhitePixel =
                    popupWindow->DrawList->_Data->TexUvWhitePixel;
                for (int vertexIndex = firstSharedHighlightVertex;
                    vertexIndex < lastSharedHighlightVertex;
                    ++vertexIndex)
                {
                    const ImDrawVert& vertex =
                        popupWindow->DrawList->VtxBuffer[vertexIndex];
                    if (Near(vertex.uv.x, popupWhitePixel.x) &&
                        Near(vertex.uv.y, popupWhitePixel.y))
                    {
                        observation.sharedHighlightAlpha = ImMax(
                            observation.sharedHighlightAlpha,
                            Alpha(vertex.col));
                    }
                }
                observation.sharedHighlightRounded =
                    observation.sharedHighlightValid &&
                    lastSharedHighlightVertex > firstSharedHighlightVertex &&
                    HasRoundedFillCorners(
                        *popupWindow->DrawList,
                        firstSharedHighlightVertex,
                        lastSharedHighlightVertex,
                        observation.sharedHighlightRect);
                if (observation.popupRollAmount < 1.0f)
                {
                    observation.rollClipObserved =
                        popupWindow->DrawList->CmdBuffer.Size > 0;
                    const float popupTop = popupWindow->Pos.y;
                    const float popupBottom =
                        popupWindow->Pos.y + popupWindow->Size.y;
                    const float boundary = IM_ROUND(
                        observation.popupRollFromBottom
                            ? ImLerp(
                                popupBottom,
                                popupTop,
                                observation.popupRollAmount)
                            : ImLerp(
                                popupTop,
                                popupBottom,
                                observation.popupRollAmount));
                    for (const ImDrawCmd& command :
                        popupWindow->DrawList->CmdBuffer)
                    {
                        if (command.ElemCount == 0)
                            continue;
                        observation.rollClipValid &=
                            command.ClipRect.w >= command.ClipRect.y;
                        const bool commandClipValid =
                            observation.popupRollFromBottom
                                ? command.ClipRect.y >= boundary - 0.5f
                                : command.ClipRect.w <= boundary + 0.5f;
                        const bool emptyCommandClip =
                            Near(command.ClipRect.y, command.ClipRect.w);
                        observation.rollClipValid &=
                            commandClipValid || emptyCommandClip;
                    }
                }
                observation.popupStyleRestored =
                    Near(
                        ImGui::GetStyle().ItemSpacing.y,
                        originalItemSpacingY) &&
                    Near(
                        ImGui::GetStyle().WindowPadding.y,
                        originalWindowPaddingY);
            }

            observation.popupOpen = ImGui::IsPopupOpen(
                harness.popupId,
                ImGuiPopupFlags_None);
            observation.transitionActive =
                ImGui::IsComboPopupTransitionActive(harness.comboId);
        }
        ImGui::End();
        ImGui::Render();
        return observation;
    }

    ColorPickerObservation SubmitColorPickerFrame(
        float* color,
        bool forceOpen,
        float displayWidth = 480.0f,
        float ownerWidth = 270.0f,
        float maximumBottom = 330.0f,
        bool scoped = true,
        bool closeActivePicker = false,
        ImVec4 popupSurface =
            ImVec4(0.17f, 0.29f, 0.41f, 0.93f),
        ImVec4 outerMarginLayer =
            ImVec4(0.17f, 0.29f, 0.41f, 1.0f),
        ImVec4 contentLayer =
            ImVec4(0.11f, 0.23f, 0.37f, 0.67f),
        ImVec4 pickerLayer =
            ImVec4(0.43f, 0.19f, 0.31f, 0.71f),
        bool includeAlpha = true,
        const char* label = "##InterfacePrimaryColor",
        bool nestedZeroWindowPadding = false,
        float callerStyleAlpha = 1.0f,
        float sourceVerticalOffset = 0.0f,
        float requestedColorEditWidth = -FLT_MIN,
        float ownerHeight = 104.0f)
    {
        ColorPickerObservation observation;
        observation.maximumBottom = maximumBottom;

        ImGui::GetIO().DisplaySize = ImVec2(displayWidth, 360.0f);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(
            ImVec2(20.0f, 24.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(ownerWidth, ownerHeight),
            ImGuiCond_Always);
        ImGui::Begin(
            "Scoped Color Picker Test Owner",
            nullptr,
            ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImGuiWindow* ownerWindow = ImGui::GetCurrentWindow();
        observation.contentRight = ownerWindow->InnerRect.Max.x;
        observation.canonicalSourceRight = ownerWindow->WorkRect.Max.x;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        observation.viewportRect = ImRect(
            viewport->WorkPos,
            ImVec2(
                viewport->WorkPos.x + viewport->WorkSize.x,
                viewport->WorkPos.y + viewport->WorkSize.y));

        if (closeActivePicker)
            ImGui::CloseUvsrColorPickerPopup();
        if (scoped)
        {
            ImGui::PushUvsrColorPickerPopupContentRight(
                observation.contentRight,
                maximumBottom,
                popupSurface,
                outerMarginLayer,
                contentLayer,
                pickerLayer);
        }
        ImGui::PushID(label);
        observation.popupId = ImGui::GetID("picker");
        if (forceOpen)
        {
            ImGui::OpenPopupEx(
                observation.popupId,
                ImGuiPopupFlags_None);
        }
        ImGui::PopID();
        int nestedStyleVarCount = 0;
        if (nestedZeroWindowPadding)
        {
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(0.0f, 0.0f));
            ++nestedStyleVarCount;
        }
        if (callerStyleAlpha < 1.0f)
        {
            ImGui::PushStyleVar(
                ImGuiStyleVar_Alpha,
                ImClamp(callerStyleAlpha, 0.0f, 1.0f));
            ++nestedStyleVarCount;
        }
        const ImU32 submittedSurfaceColor =
            ImGui::GetColorU32(popupSurface);
        const ImU32 submittedOuterMarginColor =
            ImGui::GetColorU32(outerMarginLayer);
        const ImU32 submittedContentLayerColor =
            ImGui::GetColorU32(contentLayer);
        if (sourceVerticalOffset > 0.0f)
        {
            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() + sourceVerticalOffset);
        }
        ImGui::SetNextItemWidth(requestedColorEditWidth);
        const int firstColorEditVertex =
            ownerWindow->DrawList->VtxBuffer.Size;
        const ImVec2 colorEditPosition = ImGui::GetCursorScreenPos();
        const float colorEditWidth = ImGui::CalcItemWidth();
        const float colorButtonSize = ImGui::GetFrameHeight();
        observation.sourceSwatchRect = ImRect(
            ImVec2(
                colorEditPosition.x + colorEditWidth - colorButtonSize,
                colorEditPosition.y),
            ImVec2(
                colorEditPosition.x + colorEditWidth,
                colorEditPosition.y + colorButtonSize));
        ImGuiColorEditFlags pickerFlags =
            ImGuiColorEditFlags_Float |
            ImGuiColorEditFlags_DisplayRGB;
        if (includeAlpha)
        {
            pickerFlags |= ImGuiColorEditFlags_AlphaBar |
                ImGuiColorEditFlags_AlphaPreviewHalf;
        }
        if (!ImGui::IsUvsrStockWidgetRenderingEnabled())
            pickerFlags |= ImGuiColorEditFlags_PickerHueWheel;
        if (includeAlpha)
        {
            ImGui::ColorEdit4(
                label,
                color,
                pickerFlags);
        }
        else
        {
            ImGui::ColorEdit3(
                label,
                color,
                pickerFlags);
        }
        observation.submittedSourceSwatchRect =
            observation.sourceSwatchRect;
        if (nestedStyleVarCount > 0)
            ImGui::PopStyleVar(nestedStyleVarCount);
        const int lastColorEditVertex =
            ownerWindow->DrawList->VtxBuffer.Size;
        const ImGuiUvsrColorPickerPopupTransition& transition =
            GImGui->UvsrColorPickerPopupTransition;
        if (transition.PopupId == observation.popupId &&
            !transition.SourceRect.IsInverted())
        {
            observation.sourceSwatchRect = transition.SourceRect;
        }
        ImVec4 swatchShadowColor =
            ImGui::GetStyle().Colors[ImGuiCol_BorderShadow];
        swatchShadowColor.w = ImMax(swatchShadowColor.w, 0.42f);
        const ImU32 expectedSwatchShadow =
            ImGui::GetColorU32(swatchShadowColor);
        const ImU32 frameBackground =
            ImGui::GetColorU32(ImGuiCol_FrameBg);
        bool swatchFrameVertexFound = false;
        for (int vertexIndex = firstColorEditVertex;
            vertexIndex < lastColorEditVertex;
            ++vertexIndex)
        {
            const ImDrawVert& vertex =
                ownerWindow->DrawList->VtxBuffer[vertexIndex];
            if (vertex.col == expectedSwatchShadow &&
                (vertex.pos.x > observation.sourceSwatchRect.Max.x + 0.1f ||
                    vertex.pos.y >
                        observation.sourceSwatchRect.Max.y + 0.1f))
            {
                observation.sourceSwatchShadowVisible = true;
            }
            if (vertex.col == frameBackground &&
                observation.sourceSwatchRect.Contains(vertex.pos))
            {
                swatchFrameVertexFound = true;
            }
        }
        observation.sourceSwatchBorderless = !swatchFrameVertexFound;

        char popupName[20];
        ImFormatString(
            popupName,
            IM_ARRAYSIZE(popupName),
            "##Popup_%08x",
            observation.popupId);
        ImGuiWindow* popupWindow = ImGui::FindWindowByName(popupName);
        if (popupWindow && popupWindow->Active)
        {
            observation.popupRect = ImRect(
                popupWindow->Pos,
                ImVec2(
                    popupWindow->Pos.x + popupWindow->Size.x,
                    popupWindow->Pos.y + popupWindow->Size.y));
            observation.popupInnerRect = popupWindow->InnerRect;
            observation.popupWindowPadding = popupWindow->WindowPadding;
            const ImGuiStyle& style = ImGui::GetStyle();
            const float pickerWidth = ImMax(
                1.0f,
                popupWindow->Size.x -
                    popupWindow->WindowPadding.x * 2.0f);
            const float innerSpacing = style.ItemInnerSpacing.x;
            const float itemsWidth = ImMax(
                pickerWidth - innerSpacing * 3.0f,
                4.0f);
            const float fourthColumnOffset = ImClamp(
                IM_TRUNC(itemsWidth * 0.75f) + innerSpacing * 3.0f,
                innerSpacing,
                pickerWidth);
            const float fourthColumnWidth = ImMax(
                pickerWidth - fourthColumnOffset,
                4.0f);
            const float barWidth = ImMax(
                (fourthColumnWidth - innerSpacing * 3.0f) / 4.0f,
                1.0f);
            const float saturationValueSize = ImMax(
                fourthColumnOffset - innerSpacing,
                1.0f);
            observation.barCount = 4;
            const ImVec2 pickerPosition =
                popupWindow->DC.CursorStartPos;
            const float hueBarX =
                pickerPosition.x + fourthColumnOffset;
            const float alphaBarX =
                hueBarX + barWidth + innerSpacing;
            const float currentBarX =
                alphaBarX + barWidth + innerSpacing;
            const float originalBarX =
                currentBarX + barWidth + innerSpacing;
            observation.hueBarRect = ImRect(
                ImVec2(hueBarX, pickerPosition.y),
                ImVec2(
                    hueBarX + barWidth,
                    pickerPosition.y + saturationValueSize));
            observation.alphaBarRect = ImRect(
                ImVec2(alphaBarX, pickerPosition.y),
                ImVec2(
                    alphaBarX + barWidth,
                    pickerPosition.y + saturationValueSize));
            observation.currentBarRect = ImRect(
                ImVec2(currentBarX, pickerPosition.y),
                ImVec2(
                    currentBarX + barWidth,
                    pickerPosition.y + saturationValueSize));
            observation.originalBarRect = ImRect(
                ImVec2(originalBarX, pickerPosition.y),
                ImVec2(
                    originalBarX + barWidth,
                    pickerPosition.y + saturationValueSize));
            const float wheelThickness = saturationValueSize * 0.08f;
            observation.wheelThickness = wheelThickness;
            observation.drawListFringe = ImMax(
                1.0f,
                popupWindow->DrawList->_FringeScale);
            const float wheelOuterRadius =
                saturationValueSize * 0.5f -
                popupWindow->DrawList->_FringeScale;
            const float wheelInnerRadius =
                wheelOuterRadius - wheelThickness;
            const float compactMarkerRadius = ImMax(
                popupWindow->DrawList->_FringeScale * 1.5f,
                ImMin(
                    wheelThickness * 0.28f,
                    style.FrameRounding * 0.75f));
            const float triangleRadius =
                wheelInnerRadius -
                compactMarkerRadius -
                popupWindow->DrawList->_FringeScale * 3.0f;
            observation.endpointSnapRadius = ImMin(
                triangleRadius * 0.25f,
                ImMax(
                    style.GrabMinSize * 0.50f,
                    wheelThickness * 0.75f));
            observation.endpointMarkerRadius = ImLerp(
                compactMarkerRadius,
                observation.endpointSnapRadius,
                0.5f);
            const ImVec2 wheelCenter(
                pickerPosition.x + saturationValueSize * 0.5f,
                pickerPosition.y + saturationValueSize * 0.5f);
            observation.innerWheelGradientOutlineVisible =
                HasVisibleWheelOutlineCoverage(
                    *popupWindow->DrawList,
                    wheelCenter,
                    wheelInnerRadius,
                    observation.drawListFringe,
                    &observation.innerWheelOutlineCoveredBins,
                    &observation.innerWheelOutlineTopAlpha,
                    &observation.innerWheelOutlineBottomAlpha);
            observation.outerWheelGradientOutlineVisible =
                HasVisibleWheelOutlineCoverage(
                    *popupWindow->DrawList,
                    wheelCenter,
                    wheelOuterRadius,
                    observation.drawListFringe,
                    &observation.outerWheelOutlineCoveredBins,
                    &observation.outerWheelOutlineTopAlpha,
                    &observation.outerWheelOutlineBottomAlpha);
            if (includeAlpha)
            {
                observation.alphaBarInteriorCovered = HasCoveredVertexNear(
                    *popupWindow->DrawList,
                    0,
                    popupWindow->DrawList->VtxBuffer.Size,
                    ImVec2(
                        (observation.alphaBarRect.Min.x +
                            observation.alphaBarRect.Max.x) * 0.5f,
                        ImLerp(
                            observation.alphaBarRect.Min.y,
                            observation.alphaBarRect.Max.y,
                            0.25f)));
                observation.alphaCheckerEdgeCoverageContinuous =
                    HasTightRoundedCheckerCoverage(
                        *popupWindow->DrawList,
                        observation.alphaBarRect,
                        ImMin(
                            style.FrameRounding,
                            observation.alphaBarRect.GetWidth() * 0.25f));
            }
            const bool authoredPicker =
                scoped &&
                !ImGui::IsUvsrStockWidgetRenderingEnabled();
            observation.alphaBarRoundedAndVisible =
                authoredPicker &&
                includeAlpha &&
                observation.alphaBarInteriorCovered &&
                observation.alphaCheckerEdgeCoverageContinuous;
            float hue = 0.0f;
            float saturation = 0.0f;
            float value = 0.0f;
            ImGui::ColorConvertRGBtoHSV(
                color[0],
                color[1],
                color[2],
                hue,
                saturation,
                value);
            const ImU32 savedColor = ImGui::ColorConvertFloat4ToU32(
                ImVec4(color[0], color[1], color[2], 0.0f));
            if (savedColor == GImGui->ColorEditSavedColor)
            {
                if (saturation == 0.0f ||
                    (hue == 0.0f && GImGui->ColorEditSavedHue == 1.0f))
                {
                    hue = GImGui->ColorEditSavedHue;
                }
                if (value == 0.0f)
                    saturation = GImGui->ColorEditSavedSat;
            }
            const ImVec2 sharpVertices[3] = {
                ImVec2(triangleRadius, 0.0f),
                ImVec2(
                    triangleRadius * -0.5f,
                    triangleRadius * -0.866025f),
                ImVec2(
                    triangleRadius * -0.5f,
                    triangleRadius * 0.866025f)
            };
            float minimumEdgeLength = FLT_MAX;
            for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
            {
                minimumEdgeLength = ImMin(
                    minimumEdgeLength,
                    ImSqrt(ImLengthSqr(Subtract(
                        sharpVertices[(vertexIndex + 1) % 3],
                        sharpVertices[vertexIndex]))));
            }
            const float tangentDistance = ImClamp(
                ImMax(style.FrameRounding, 0.0f) * 1.75f,
                0.0f,
                minimumEdgeLength * 0.20f);
            const float hueCosine = ImCos(hue * 2.0f * IM_PI);
            const float hueSine = ImSin(hue * 2.0f * IM_PI);
            ImVector<ImVec2> roundedContour;
            int cornerSections = ImClamp(
                popupWindow->DrawList->_CalcCircleAutoSegmentCount(
                    ImMax(style.FrameRounding, 1.0f)) / 3,
                4,
                24);
            if ((cornerSections & 1) != 0)
                ++cornerSections;
            for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
            {
                const ImVec2 vertex = sharpVertices[vertexIndex];
                const ImVec2 previous = sharpVertices[(vertexIndex + 2) % 3];
                const ImVec2 next = sharpVertices[(vertexIndex + 1) % 3];
                const ImVec2 entry = Add(
                    vertex,
                    Scale(
                        Subtract(previous, vertex),
                        tangentDistance / ImMax(
                            ImSqrt(ImLengthSqr(Subtract(previous, vertex))),
                            FLT_EPSILON)));
                const ImVec2 exit = Add(
                    vertex,
                    Scale(
                        Subtract(next, vertex),
                        tangentDistance / ImMax(
                            ImSqrt(ImLengthSqr(Subtract(next, vertex))),
                            FLT_EPSILON)));
                for (int section = 0; section <= cornerSections; ++section)
                {
                    const float amount =
                        float(section) / float(cornerSections);
                    const float inverseAmount = 1.0f - amount;
                    roundedContour.push_back(Add(
                        Add(
                            Scale(entry, inverseAmount * inverseAmount),
                            Scale(vertex, 2.0f * inverseAmount * amount)),
                        Scale(exit, amount * amount)));
                }
            }
            const ImVec2 sharpCursor = ImLerp(
                ImLerp(
                    sharpVertices[2],
                    sharpVertices[0],
                    ImSaturate(saturation)),
                sharpVertices[1],
                1.0f - ImSaturate(value));
            const ImVec2 localCursor = MapRoundedSelectorPoint(
                sharpVertices,
                roundedContour,
                sharpCursor);
            observation.selectorCenter = wheelCenter;
            observation.selectorCursorCenter = Add(wheelCenter, ImVec2(
                localCursor.x * hueCosine - localCursor.y * hueSine,
                localCursor.x * hueSine + localCursor.y * hueCosine));
            observation.selectorRect = ImRect(
                ImVec2(FLT_MAX, FLT_MAX),
                ImVec2(-FLT_MAX, -FLT_MAX));
            for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
            {
                const ImVec2 localEndpoint = MapRoundedSelectorPoint(
                    sharpVertices,
                    roundedContour,
                    sharpVertices[vertexIndex]);
                const ImVec2 endpoint = Add(wheelCenter, ImVec2(
                    localEndpoint.x * hueCosine -
                        localEndpoint.y * hueSine,
                    localEndpoint.x * hueSine +
                        localEndpoint.y * hueCosine));
                observation.selectorEndpoints[vertexIndex] = endpoint;
                observation.selectorRect.Add(endpoint);
            }
            const float hueMarkerY = IM_ROUND(
                pickerPosition.y + hue * saturationValueSize);
            observation.hueHollowMarkerVisible =
                HasHollowCircleMarkerAt(
                    *popupWindow->DrawList,
                    ImVec2(
                        observation.hueBarRect.GetCenter().x,
                        hueMarkerY),
                    observation.endpointMarkerRadius,
                    observation.drawListFringe);
            if (includeAlpha)
            {
                const float alphaMarkerY = IM_ROUND(
                    pickerPosition.y +
                        (1.0f - ImSaturate(color[3])) *
                            saturationValueSize);
                observation.alphaHollowMarkerVisible =
                    HasHollowCircleMarkerAt(
                        *popupWindow->DrawList,
                        ImVec2(
                            observation.alphaBarRect.GetCenter().x,
                            alphaMarkerY),
                        observation.endpointMarkerRadius,
                        observation.drawListFringe);
            }
            observation.currentBarVisible =
                HasComparisonBarCoverage(
                    *popupWindow->DrawList,
                    observation.currentBarRect);
            observation.originalBarVisible =
                HasCoveredVertexInRect(
                    *popupWindow->DrawList,
                    observation.originalBarRect,
                    0.25f);
            observation.bottomControlsSpanAllBars =
                popupWindow->DC.CursorMaxPos.x >=
                    observation.originalBarRect.Max.x - 1.0f;
            observation.authoredPopupUsesCarvedFrameOnly =
                Near(popupWindow->WindowBorderSize, 0.0f);
            const ImU32 expectedSurface = submittedSurfaceColor;
            const ImVec4 expectedSurfaceColor =
                ImGui::ColorConvertU32ToFloat4(expectedSurface);
            const ImU32 expectedContentLayer =
                submittedContentLayerColor;
            const ImU32 expectedOuterMargin =
                submittedOuterMarginColor;
            const ImU32 expectedPickerLayer =
                ImGui::ColorConvertFloat4ToU32(pickerLayer);
            observation.brightSurfacePadding = ImVec2(
                ImMax(
                    0.0f,
                    popupWindow->WindowPadding.x -
                        style.ItemInnerSpacing.x),
                ImMax(
                    0.0f,
                    popupWindow->WindowPadding.y -
                        style.ItemInnerSpacing.y));
            const ImRect expectedContentLayerBounds(
                Add(
                    popupWindow->InnerRect.Min,
                    observation.brightSurfacePadding),
                Subtract(
                    popupWindow->InnerRect.Max,
                    observation.brightSurfacePadding));
            observation.expectedPickerLayerRect =
                expectedContentLayerBounds;
            observation.requestedPickerLayerApplied = false;
            const ImVec4 originalColor = GImGui->ColorPickerRef;
            const ImU32 expectedOriginalBarColor =
                ImGui::ColorConvertFloat4ToU32(ImVec4(
                    originalColor.x,
                    originalColor.y,
                    originalColor.z,
                    includeAlpha ? ImSaturate(originalColor.w) : 1.0f));
            const ImU32 hueStopColors[] = {
                IM_COL32(255, 0, 0, 255),
                IM_COL32(255, 255, 0, 255),
                IM_COL32(0, 255, 0, 255),
                IM_COL32(0, 255, 255, 255),
                IM_COL32(0, 0, 255, 255),
                IM_COL32(255, 0, 255, 255)
            };
            const float inputRowHeight = ImGui::GetFrameHeight();
            const float firstInputRowY =
                pickerPosition.y + saturationValueSize + style.ItemSpacing.y;
            const float inputRowPitch =
                inputRowHeight + style.ItemSpacing.y;
            const ImRect rgbInputBand(
                ImVec2(popupWindow->InnerRect.Min.x, firstInputRowY - 0.5f),
                ImVec2(
                    popupWindow->InnerRect.Max.x,
                    firstInputRowY + inputRowHeight + 0.5f));
            const ImRect hsvInputBand(
                ImVec2(
                    popupWindow->InnerRect.Min.x,
                    firstInputRowY + inputRowPitch - 0.5f),
                ImVec2(
                    popupWindow->InnerRect.Max.x,
                    firstInputRowY + inputRowPitch +
                        inputRowHeight + 0.5f));
            const ImRect hexInputBand(
                ImVec2(
                    popupWindow->InnerRect.Min.x,
                    firstInputRowY + inputRowPitch * 2.0f - 0.5f),
                ImVec2(
                    popupWindow->InnerRect.Max.x,
                    firstInputRowY + inputRowPitch * 2.0f +
                        inputRowHeight + 0.5f));
            const ImRect hueBarSearchRect(
                Subtract(
                    observation.hueBarRect.Min,
                    ImVec2(style.ItemInnerSpacing.x, 0.5f)),
                Add(
                    observation.hueBarRect.Max,
                    ImVec2(style.ItemInnerSpacing.x, 0.5f)));
            const ImRect originalBarSearchRect(
                Subtract(
                    observation.originalBarRect.Min,
                    ImVec2(style.ItemInnerSpacing.x, 0.5f)),
                Add(
                    observation.originalBarRect.Max,
                    ImVec2(style.ItemInnerSpacing.x, 0.5f)));
            ImVec4 saturatedHueColor(0.0f, 0.0f, 0.0f, 1.0f);
            ImGui::ColorConvertHSVtoRGB(
                hue,
                1.0f,
                1.0f,
                saturatedHueColor.x,
                saturatedHueColor.y,
                saturatedHueColor.z);
            const ImU32 selectorEndpointColors[3] = {
                ImGui::ColorConvertFloat4ToU32(saturatedHueColor),
                IM_COL32(0, 0, 0, 255),
                IM_COL32_WHITE
            };
            const ImVec2 theoreticalSelectorEndpoints[3] = {
                observation.selectorEndpoints[0],
                observation.selectorEndpoints[1],
                observation.selectorEndpoints[2]
            };
            float selectorEndpointDistances[3] = {
                FLT_MAX,
                FLT_MAX,
                FLT_MAX
            };
            ImRect sourcePointerBounds(
                ImVec2(FLT_MAX, FLT_MAX),
                ImVec2(-FLT_MAX, -FLT_MAX));
            const ImVec2 expectedPointerTip(
                observation.canonicalSourceRight + ImMax(
                    observation.drawListFringe,
                    style.ItemInnerSpacing.x * 0.25f),
                observation.sourceSwatchRect.GetCenter().y);
            const float expectedPointerHalfHeight = ImMax(
                observation.drawListFringe * 3.0f,
                colorButtonSize * 0.30f);
            const float expectedPointerBaseY = ImClamp(
                observation.sourceSwatchRect.GetCenter().y,
                observation.popupRect.Min.y + style.PopupRounding +
                    expectedPointerHalfHeight + observation.drawListFringe,
                observation.popupRect.Max.y - style.PopupRounding -
                    expectedPointerHalfHeight - observation.drawListFringe);
            const float expectedPointerBaseX =
                observation.popupRect.Min.x +
                    observation.drawListFringe + 1.0f;
            const ImVec2 expectedPointerBaseTop(
                expectedPointerBaseX,
                expectedPointerBaseY - expectedPointerHalfHeight);
            const ImVec2 expectedPointerBaseBottom(
                expectedPointerBaseX,
                expectedPointerBaseY + expectedPointerHalfHeight);
            for (const ImDrawVert& vertex : popupWindow->DrawList->VtxBuffer)
            {
                const float vertexAlpha = Alpha(vertex.col);
                observation.maximumVertexAlpha = ImMax(
                    observation.maximumVertexAlpha,
                    vertexAlpha);
                if (vertexAlpha > 0.0f)
                    observation.visualBounds.Add(vertex.pos);
                observation.requestedSurfaceApplied |=
                    vertex.col == expectedSurface;
                observation.opaqueOuterMarginVisible |=
                    vertex.col == expectedOuterMargin &&
                    observation.popupRect.Contains(vertex.pos) &&
                    (vertex.pos.x <= observation.popupRect.Min.x + 2.5f ||
                        vertex.pos.x >= observation.popupRect.Max.x - 2.5f ||
                        vertex.pos.y <= observation.popupRect.Min.y + 2.5f ||
                        vertex.pos.y >= observation.popupRect.Max.y - 2.5f);
                if (vertex.col == expectedContentLayer)
                {
                    observation.requestedContentLayerApplied = true;
                    if (expectedContentLayerBounds.Contains(vertex.pos))
                        observation.contentLayerRect.Add(vertex.pos);
                }
                if (hueBarSearchRect.Contains(vertex.pos))
                {
                    for (ImU32 hueStopColor : hueStopColors)
                    {
                        if (vertex.col == hueStopColor)
                        {
                            observation.observedHueBarRect.Add(vertex.pos);
                            break;
                        }
                    }
                }
                if (vertex.col == expectedOriginalBarColor &&
                    originalBarSearchRect.Contains(vertex.pos))
                {
                    observation.observedOriginalBarRect.Add(vertex.pos);
                }
                observation.disabledAlphaBarVisible |=
                    !includeAlpha &&
                    observation.alphaBarRect.Contains(vertex.pos) &&
                    vertex.col == IM_COL32(112, 112, 112, 255);
                if (vertex.col == expectedOuterMargin)
                {
                    observation.sourcePointerBaseTopDistance = ImMin(
                        observation.sourcePointerBaseTopDistance,
                        ImSqrt(ImLengthSqr(Subtract(
                            vertex.pos,
                            expectedPointerBaseTop))));
                    observation.sourcePointerBaseBottomDistance = ImMin(
                        observation.sourcePointerBaseBottomDistance,
                        ImSqrt(ImLengthSqr(Subtract(
                            vertex.pos,
                            expectedPointerBaseBottom))));
                }
                if (vertexAlpha >= 0.999f)
                {
                    if (ImLengthSqr(Subtract(vertex.pos, wheelCenter)) <
                        triangleRadius * triangleRadius)
                    {
                        for (int endpointIndex = 0;
                            endpointIndex < 3;
                            ++endpointIndex)
                        {
                            if (vertex.col !=
                                selectorEndpointColors[endpointIndex])
                            {
                                continue;
                            }
                            const float distanceSquared = ImLengthSqr(
                                Subtract(
                                    vertex.pos,
                                    theoreticalSelectorEndpoints[
                                        endpointIndex]));
                            if (distanceSquared <
                                selectorEndpointDistances[endpointIndex])
                            {
                                selectorEndpointDistances[endpointIndex] =
                                    distanceSquared;
                            }
                        }
                    }
                    if (observation.hueBarRect.Contains(vertex.pos) &&
                        vertex.pos.y > observation.hueBarRect.Min.y + 1.0f &&
                        vertex.pos.y < observation.hueBarRect.Max.y - 1.0f)
                    {
                        observation.hueBarInteriorOpaque = true;
                    }
                    if (ImLengthSqr(Subtract(vertex.pos, wheelCenter)) <
                        triangleRadius * triangleRadius * 0.56f)
                    {
                        ++observation.opaqueSelectorVertexCount;
                    }
                }
            if (vertexAlpha > 0.0f &&
                    vertex.pos.x < observation.popupRect.Min.x - 0.1f)
                {
                    sourcePointerBounds.Add(vertex.pos);
                    observation.sourcePointerTipDistance = ImMin(
                        observation.sourcePointerTipDistance,
                        ImSqrt(ImLengthSqr(Subtract(
                            vertex.pos,
                            expectedPointerTip))));
                    const ImVec4 pointerColor =
                        ImGui::ColorConvertU32ToFloat4(vertex.col);
                    const bool matchesSurfaceRgb =
                        std::abs(pointerColor.x - expectedSurfaceColor.x) <=
                            1.0f / 255.0f &&
                        std::abs(pointerColor.y - expectedSurfaceColor.y) <=
                            1.0f / 255.0f &&
                        std::abs(pointerColor.z - expectedSurfaceColor.z) <=
                            1.0f / 255.0f;
                    const ImVec4 outerMarginColor =
                        ImGui::ColorConvertU32ToFloat4(
                            expectedOuterMargin);
                    const bool matchesOuterMarginRgb =
                        std::abs(pointerColor.x - outerMarginColor.x) <=
                            1.0f / 255.0f &&
                        std::abs(pointerColor.y - outerMarginColor.y) <=
                            1.0f / 255.0f &&
                        std::abs(pointerColor.z - outerMarginColor.z) <=
                            1.0f / 255.0f;
                    if (matchesOuterMarginRgb)
                    {
                        observation.sourcePointerUsesCarvedFrame = true;
                        observation.sourcePointerFrameMaximumAlpha = ImMax(
                            observation.sourcePointerFrameMaximumAlpha,
                            pointerColor.w);
                    }
                    if (!matchesSurfaceRgb)
                    {
                        observation.sourcePointerFrameMaximumAlpha = ImMax(
                            observation.sourcePointerFrameMaximumAlpha,
                            pointerColor.w);
                    }
                }
            }
            observation.observedPickerLayerRect =
                GetSolidColorTriangleBounds(
                    *popupWindow->DrawList,
                    expectedPickerLayer,
                    observation.expectedPickerLayerRect);
            constexpr float observedLayerEdgeTolerance = 1.5f;
            observation.requestedPickerLayerApplied =
                authoredPicker &&
                !observation.observedPickerLayerRect.IsInverted() &&
                std::abs(
                    observation.observedPickerLayerRect.Min.x -
                        observation.expectedPickerLayerRect.Min.x) <=
                    observedLayerEdgeTolerance &&
                std::abs(
                    observation.observedPickerLayerRect.Min.y -
                        observation.expectedPickerLayerRect.Min.y) <=
                    observedLayerEdgeTolerance &&
                std::abs(
                    observation.observedPickerLayerRect.Max.x -
                        observation.expectedPickerLayerRect.Max.x) <=
                    observedLayerEdgeTolerance &&
                std::abs(
                    observation.observedPickerLayerRect.Max.y -
                        observation.expectedPickerLayerRect.Max.y) <=
                    observedLayerEdgeTolerance;
            const ImVector<ImRect> whitePixelComponents =
                CollectWhitePixelTriangleComponents(*popupWindow->DrawList);
            observation.fourthRgbInputRect = FindRightmostInputFrame(
                whitePixelComponents,
                rgbInputBand,
                inputRowHeight,
                inputRowHeight * 1.5f);
            observation.fourthHsvInputRect = FindRightmostInputFrame(
                whitePixelComponents,
                hsvInputBand,
                inputRowHeight,
                inputRowHeight * 1.5f);
            observation.hexInputRect = FindRightmostInputFrame(
                whitePixelComponents,
                hexInputBand,
                inputRowHeight,
                inputRowHeight * 1.5f);
            observation.subordinatePreviewSquareCount =
                CountPreviewSizedFrames(
                    whitePixelComponents,
                    rgbInputBand,
                    inputRowHeight,
                    observation.hueBarRect.Min.x) +
                CountPreviewSizedFrames(
                    whitePixelComponents,
                    hsvInputBand,
                    inputRowHeight,
                    observation.hueBarRect.Min.x) +
                CountPreviewSizedFrames(
                    whitePixelComponents,
                    hexInputBand,
                    inputRowHeight,
                    observation.hueBarRect.Min.x);
            const float centerInset = ImMax(
                3.0f,
                style.PopupRounding + 3.0f);
            const ImRect popupCenter(
                Add(
                    observation.popupRect.Min,
                    ImVec2(centerInset, centerInset)),
                Subtract(
                    observation.popupRect.Max,
                    ImVec2(centerInset, centerInset)));
            if (!popupCenter.IsInverted())
            {
                observation.translucentSurfaceCoversCenter =
                    HasSolidColorCoverageAtPoint(
                        *popupWindow->DrawList,
                        expectedSurface,
                        popupCenter.GetCenter());
            }
            const ImVec2 frameSamplePoints[4] = {
                ImVec2(
                    observation.popupRect.GetCenter().x,
                    (observation.popupRect.Min.y +
                        expectedContentLayerBounds.Min.y) * 0.5f),
                ImVec2(
                    observation.popupRect.GetCenter().x,
                    (expectedContentLayerBounds.Max.y +
                        observation.popupRect.Max.y) * 0.5f),
                ImVec2(
                    (observation.popupRect.Min.x +
                        expectedContentLayerBounds.Min.x) * 0.5f,
                    observation.popupRect.GetCenter().y),
                ImVec2(
                    (expectedContentLayerBounds.Max.x +
                        observation.popupRect.Max.x) * 0.5f,
                    observation.popupRect.GetCenter().y)
            };
            observation.opaqueOuterMarginCoversCenter =
                HasSolidColorCoverageAtPoint(
                    *popupWindow->DrawList,
                    expectedOuterMargin,
                    expectedContentLayerBounds.GetCenter());
            observation.opaqueOuterRimComplete =
                authoredPicker;
            for (const ImVec2& samplePoint : frameSamplePoints)
            {
                observation.opaqueOuterRimComplete &=
                    HasSolidColorCoverageAtPoint(
                        *popupWindow->DrawList,
                        expectedOuterMargin,
                        samplePoint);
            }
            observation.hueBarRoundedAndVisible =
                authoredPicker &&
                observation.hueBarInteriorOpaque &&
                observation.hueBarRect.GetWidth() > 0.0f;
            observation.sourcePointerVisible =
                !sourcePointerBounds.IsInverted();
            observation.sourcePointerTipAttached =
                observation.sourcePointerVisible &&
                observation.sourcePointerTipDistance <=
                    style.FrameRounding +
                        observation.drawListFringe * 2.0f;
            observation.sourcePointerBaseAttached =
                observation.sourcePointerBaseTopDistance <=
                    style.FrameRounding +
                        observation.drawListFringe * 2.0f &&
                observation.sourcePointerBaseBottomDistance <=
                    style.FrameRounding +
                        observation.drawListFringe * 2.0f;
            observation.sourcePointerTargetsSwatch =
                observation.sourcePointerTipAttached &&
                sourcePointerBounds.Min.x <=
                    observation.canonicalSourceRight +
                        style.ItemInnerSpacing.x + style.FrameRounding + 1.0f &&
                sourcePointerBounds.Min.y <=
                    observation.sourceSwatchRect.GetCenter().y +
                        style.FrameRounding + 1.0f &&
                sourcePointerBounds.Max.y >=
                    observation.sourceSwatchRect.GetCenter().y -
                        style.FrameRounding - 1.0f;
            observation.selectorVisualEndpointsFound =
                selectorEndpointDistances[0] < FLT_MAX &&
                selectorEndpointDistances[1] < FLT_MAX &&
                selectorEndpointDistances[2] < FLT_MAX;
            const float markerOuterRadius =
                observation.endpointMarkerRadius +
                    observation.drawListFringe * 2.0f;
            const float cursorOuterRadius =
                observation.endpointMarkerRadius + 2.0f;
            observation.requiredMarkerCursorGap =
                observation.drawListFringe;
            observation.selectorMarkerCursorClearance = authoredPicker;
            for (int endpointIndex = 0; endpointIndex < 3; ++endpointIndex)
            {
                const ImVec2 endpoint =
                    theoreticalSelectorEndpoints[endpointIndex];
                const bool markerVisible = HasHollowCircleMarkerAt(
                    *popupWindow->DrawList,
                    endpoint,
                    observation.endpointMarkerRadius,
                    observation.drawListFringe);
                observation.selectorEndpointMarkerVisible[endpointIndex] =
                    markerVisible;
                const float outerGap = ImSqrt(ImLengthSqr(Subtract(
                    endpoint,
                    observation.selectorCursorCenter))) -
                    markerOuterRadius - cursorOuterRadius;
                const bool excluded = outerGap <=
                    observation.requiredMarkerCursorGap;
                observation.selectorEndpointExclusionObserved |= excluded;
                if (excluded)
                {
                    // The active SV cursor uses the same hollow-circle design
                    // and occupies the endpoint exactly, so geometry alone
                    // cannot distinguish it from the intentionally omitted
                    // endpoint marker. Exclusion itself proves the contract.
                    observation.selectorMarkerCursorClearance &= true;
                }
                else
                {
                    observation.minimumMarkerCursorGap = ImMin(
                        observation.minimumMarkerCursorGap,
                        outerGap);
                    observation.selectorMarkerCursorClearance &=
                        markerVisible &&
                        outerGap + 0.01f >=
                            observation.requiredMarkerCursorGap;
                }
            }
            observation.contentFrameHalfPixelAligned =
                authoredPicker &&
                observation.requestedContentLayerApplied &&
                HasCarvedOutlineCoverage(
                    *popupWindow->DrawList,
                    expectedContentLayerBounds,
                    0.0f);
            if (!observation.contentLayerRect.IsInverted() &&
                !observation.hexInputRect.IsInverted())
            {
                observation.brightMarginLeft =
                    pickerPosition.x - observation.contentLayerRect.Min.x;
                observation.brightMarginTop =
                    pickerPosition.y - observation.contentLayerRect.Min.y;
                observation.brightMarginRight =
                    observation.contentLayerRect.Max.x -
                        observation.originalBarRect.Max.x;
                observation.brightMarginBottom =
                    observation.contentLayerRect.Max.y -
                        observation.hexInputRect.Max.y;
            }
            const ImRect popupOuterClip = popupWindow->OuterRectClipped;
            bool popupBodyClipCommandFound = false;
            for (const ImDrawCmd& command :
                popupWindow->DrawList->CmdBuffer)
            {
                if (command.ElemCount == 0)
                    continue;
                popupBodyClipCommandFound |=
                    Near(command.ClipRect.x, popupOuterClip.Min.x) &&
                    Near(command.ClipRect.y, popupOuterClip.Min.y) &&
                    Near(command.ClipRect.z, popupOuterClip.Max.x) &&
                    Near(command.ClipRect.w, popupOuterClip.Max.y);
            }
            observation.finalCursorLayeredAndClipped =
                popupBodyClipCommandFound &&
                HasHollowCircleMarkerAt(
                    *popupWindow->DrawList,
                    observation.selectorCursorCenter,
                    observation.endpointMarkerRadius,
                    observation.drawListFringe);
        }
        ImDrawList* activePickerDrawList =
            ImGui::GetUvsrActiveColorPickerPopupDrawList();
        observation.activeDrawListReported =
            activePickerDrawList != nullptr;
        observation.activeDrawListMatchesPopup =
            popupWindow && popupWindow->Active &&
            activePickerDrawList == popupWindow->DrawList;
        observation.colorAlpha = includeAlpha ? color[3] : 1.0f;
        if (scoped)
            ImGui::PopUvsrColorPickerPopupContentRight();
        ImGui::End();
        ImGui::Render();
        observation.popupOpen = ImGui::IsPopupOpen(
            observation.popupId,
            ImGuiPopupFlags_None);
        observation.openPopupCount = GImGui->OpenPopupStack.Size;
        observation.onlyPickerPopupOpen =
            observation.openPopupCount == 1 &&
            GImGui->OpenPopupStack[0].PopupId == observation.popupId;
        observation.transitionRegistered =
            transition.PopupId == observation.popupId;
        observation.transitionTargetOpen =
            observation.transitionRegistered && transition.TargetOpen;
        observation.transitionAmount = observation.transitionRegistered
            ? transition.Amount
            : (observation.popupOpen ? 1.0f : 0.0f);
        observation.transitionInteractionReady =
            observation.popupOpen &&
            (!observation.transitionRegistered ||
                (transition.TargetOpen && transition.Amount >= 1.0f));
        observation.transitionAtZeroEndpoint =
            observation.transitionRegistered &&
            !transition.TargetOpen &&
            transition.Amount <= 0.0f &&
            transition.ZeroFrame >= 0;
        return observation;
    }
}

int main()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(480.0f, 360.0f);
    io.DeltaTime = FixedDeltaSeconds;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(
        &fontPixels,
        &fontWidth,
        &fontHeight);

    bool passed = true;
    passed &= Check(
        TestPerformanceTimingRowRetentionLifecycle(),
        "a Performance timing row stays omitted until measured, then retains "
        "an unavailable placeholder with a zero backing value across timing "
        "resets without leaking the same label into another view");
    ImGuiStyle& comboStyle = ImGui::GetStyle();
    const float originalPopupRounding = comboStyle.PopupRounding;
    const float originalComboFrameRounding = comboStyle.FrameRounding;
    const float standardItemSpacingY = comboStyle.ItemSpacing.y;
    const float standardWindowPaddingY = comboStyle.WindowPadding.y;
    constexpr float AuthoredCornerRounding = 4.0f;
    comboStyle.PopupRounding = AuthoredCornerRounding;
    comboStyle.FrameRounding = AuthoredCornerRounding;
    ImGui::SetUvsrUiBehavior(true, false, false);
    ComboHarness harness;
    harness.selectedItem = 1;
    harness.deferSelectionCommit = true;
    const ImVec2 outside(-FLT_MAX, -FLT_MAX);

    QueueMouse(outside, false);
    const FrameObservation closed = SubmitComboFrame(
        harness,
        30.0f,
        false);
    passed &= Check(
        !closed.popupBegan &&
            !closed.popupOpen &&
            closed.comboVisualBoundsValid &&
            std::abs(
                closed.comboVisualBounds.Min.y -
                    (closed.comboRect.Min.y + 2.0f)) <= 1.0f &&
            std::abs(
                closed.comboVisualBounds.Max.y -
                    (closed.comboRect.Max.y - 2.0f)) <= 1.0f,
        "the authored closed combo preserves its full hit row while only its "
        "visible frame is inset two pixels on each Y edge");

    QueueMouse(outside, true);
    SubmitComboFrame(harness, 30.0f, false);
    QueueMouse(outside, true);
    const FrameObservation opening = SubmitComboFrame(
        harness,
        30.0f,
        true);
    const bool openingRollCorrect =
        opening.popupBegan &&
        opening.popupOpen &&
        opening.transitionActive &&
        !opening.popupClosing &&
        !opening.popupInteractionReady &&
        Near(opening.popupRollAmount, 0.0f) &&
        !opening.popupRollFromBottom &&
        opening.rollClipObserved &&
        opening.rollClipValid;
    if (!openingRollCorrect)
    {
        std::cerr
            << "authored opening: began=" << opening.popupBegan
            << " open=" << opening.popupOpen
            << " transition=" << opening.transitionActive
            << " closing=" << opening.popupClosing
            << " ready=" << opening.popupInteractionReady
            << " amount=" << opening.popupRollAmount
            << " from-bottom=" << opening.popupRollFromBottom
            << " clip=" << opening.rollClipObserved << '/'
            << opening.rollClipValid
            << " combo=" << harness.comboId
            << " popup=" << harness.popupId
            << '\n';
    }
    passed &= Check(
        openingRollCorrect,
        "the compact authored popup begins a top-down retained roll with its "
        "fixed layout clipped and interaction blocked");

    const ImVec2 openingOptionCenter = Center(opening.optionRect);
    QueueMouse(openingOptionCenter, true);
    const FrameObservation blockedOpeningHold = SubmitComboFrame(
        harness,
        30.0f,
        false);
    if (!blockedOpeningHold.popupBegan ||
        !blockedOpeningHold.popupOpen ||
        !blockedOpeningHold.transitionActive)
    {
        std::cerr
            << "blocked opening: began=" << blockedOpeningHold.popupBegan
            << " open=" << blockedOpeningHold.popupOpen
            << " transition=" << blockedOpeningHold.transitionActive
            << " ready=" << blockedOpeningHold.popupInteractionReady
            << " active=" << blockedOpeningHold.activeId
            << " option=" << blockedOpeningHold.optionId
            << " rect=(" << opening.optionRect.Min.x << ','
            << opening.optionRect.Min.y << " -> "
            << opening.optionRect.Max.x << ','
            << opening.optionRect.Max.y << ") mouse=("
            << openingOptionCenter.x << ',' << openingOptionCenter.y << ")\n";
    }
    passed &= Check(
        blockedOpeningHold.popupBegan &&
            blockedOpeningHold.popupOpen &&
            blockedOpeningHold.transitionActive &&
            !blockedOpeningHold.popupInteractionReady &&
            blockedOpeningHold.activeId != blockedOpeningHold.optionId &&
            !blockedOpeningHold.optionPressed &&
            harness.optionPressCount == 0 &&
            blockedOpeningHold.rollClipObserved &&
            blockedOpeningHold.rollClipValid,
        "an input held across the opening roll is discarded without acquiring "
        "the Selectable active ID");

    bool openingRollMonotonic =
        blockedOpeningHold.popupRollAmount + 1e-5f >=
            opening.popupRollAmount;
    bool openingClipValid =
        opening.rollClipObserved && opening.rollClipValid &&
        blockedOpeningHold.rollClipObserved &&
            blockedOpeningHold.rollClipValid;
    float previousOpeningAmount = blockedOpeningHold.popupRollAmount;
    FrameObservation settledOpening = blockedOpeningHold;
    for (int frame = 0;
        frame < MaximumAnimationFrames &&
            settledOpening.transitionActive;
        ++frame)
    {
        QueueMouse(Center(settledOpening.optionRect), true);
        settledOpening = SubmitComboFrame(
            harness,
            30.0f,
            false);
        openingRollMonotonic &=
            settledOpening.popupRollAmount + 1e-5f >=
                previousOpeningAmount &&
            settledOpening.popupRollAmount <= 1.0f + 1e-5f;
        if (settledOpening.popupRollAmount < 1.0f)
        {
            openingClipValid &=
                settledOpening.rollClipObserved &&
                settledOpening.rollClipValid;
        }
        previousOpeningAmount = settledOpening.popupRollAmount;
    }
    const ImVec2 optionCenter = Center(settledOpening.optionRect);
    QueueMouse(optionCenter, false);
    const FrameObservation blockedOpeningRelease = SubmitComboFrame(
        harness,
        30.0f,
        false);
    if (!settledOpening.popupBegan ||
        !settledOpening.popupOpen ||
        settledOpening.transitionActive ||
        !settledOpening.popupInteractionReady)
    {
        std::cerr
            << "settled opening: began=" << settledOpening.popupBegan
            << " open=" << settledOpening.popupOpen
            << " transition=" << settledOpening.transitionActive
            << " ready=" << settledOpening.popupInteractionReady
            << " amount=" << settledOpening.popupRollAmount
            << " frame=" << settledOpening.frame
            << " release-began=" << blockedOpeningRelease.popupBegan
            << " release-open=" << blockedOpeningRelease.popupOpen
            << '\n';
    }
    passed &= Check(
        openingRollMonotonic &&
            openingClipValid &&
            settledOpening.popupBegan &&
            settledOpening.popupOpen &&
            !settledOpening.transitionActive &&
            settledOpening.popupInteractionReady &&
            Near(settledOpening.popupRollAmount, 1.0f) &&
            !blockedOpeningRelease.optionPressed &&
            harness.optionPressCount == 0,
        "the 180 ms opening roll advances monotonically to a ready full popup, "
        "and releasing an input held while blocked is never replayed");

    const bool authoredPopupGeometryCorrect =
        Near(
            settledOpening.minimumOptionHeight,
            settledOpening.maximumOptionHeight) &&
            Near(
                settledOpening.minimumOptionGap,
                settledOpening.maximumOptionGap) &&
            Near(settledOpening.minimumOptionGap, 0.0f) &&
            Near(
                settledOpening.popupItemSpacingY,
                standardItemSpacingY * 0.25f) &&
            Near(
                settledOpening.popupWindowPadding.y,
                AuthoredCornerRounding + standardItemSpacingY * 0.5f) &&
            Near(
                settledOpening.popupRounding,
                AuthoredCornerRounding) &&
            settledOpening.sharedHighlightValid &&
            settledOpening.sharedHighlightPriority == 1 &&
            settledOpening.sharedHighlightRounded &&
            settledOpening.sharedHighlightAlpha > 0.0f &&
            Near(
                settledOpening.sharedHighlightRect.Min.y,
                settledOpening.optionRects[1].Min.y) &&
            Near(
                settledOpening.sharedHighlightRect.Max.y,
                settledOpening.optionRects[1].Max.y) &&
            settledOpening.popupStyleRestored;
    if (!authoredPopupGeometryCorrect)
    {
        std::cerr
            << "authored popup geometry: height="
            << settledOpening.minimumOptionHeight << '/'
            << settledOpening.maximumOptionHeight
            << " gap=" << settledOpening.minimumOptionGap << '/'
            << settledOpening.maximumOptionGap
            << " item-spacing=" << settledOpening.popupItemSpacingY
            << " padding=" << settledOpening.popupWindowPadding.y
            << " rounding=" << settledOpening.popupRounding
            << " shared-priority="
            << settledOpening.sharedHighlightPriority
            << " shared-rounded="
            << settledOpening.sharedHighlightRounded
            << " style-restored=" << settledOpening.popupStyleRestored
            << '\n';
    }
    passed &= Check(
        authoredPopupGeometryCorrect,
        "authored popup rows keep compact uniform spacing beneath one shared "
        "FrameRounding-selected highlight");

    QueueMouse(optionCenter, false);
    const FrameObservation optionHover = SubmitComboFrame(
        harness,
        30.0f,
        false);
    if (!optionHover.popupOpen ||
        optionHover.sharedHighlightPriority != 2)
    {
        std::cerr
            << "option hover: began=" << optionHover.popupBegan
            << " open=" << optionHover.popupOpen
            << " transition=" << optionHover.transitionActive
            << " priority=" << optionHover.sharedHighlightPriority
            << " target=" << optionHover.sharedHighlightTargetRect.Min.y
            << " current=" << optionHover.sharedHighlightRect.Min.y
            << " option=" << optionHover.optionRects[0].Min.y
            << " mouse=" << optionCenter.x << ',' << optionCenter.y
            << '\n';
    }
    passed &= Check(
        optionHover.popupOpen &&
            !optionHover.transitionActive &&
            optionHover.sharedHighlightValid &&
            optionHover.sharedHighlightPriority == 2 &&
            Near(
                optionHover.sharedHighlightTargetRect.Min.y,
                optionHover.optionRects[0].Min.y) &&
            optionHover.sharedHighlightRect.Min.y >
                optionHover.sharedHighlightTargetRect.Min.y &&
            optionHover.sharedHighlightRect.Min.y <
                settledOpening.sharedHighlightRect.Min.y,
        "one shared rounded highlight glides from the selected row toward the "
        "hovered option after the popup becomes interactive");
    QueueMouse(optionCenter, true);
    const FrameObservation optionDown = SubmitComboFrame(
        harness,
        30.0f,
        false);
    if (optionDown.activeId != optionDown.optionId)
    {
        std::cerr
            << "option down: began=" << optionDown.popupBegan
            << " open=" << optionDown.popupOpen
            << " transition=" << optionDown.transitionActive
            << " active=" << optionDown.activeId
            << " option=" << optionDown.optionId
            << " priority=" << optionDown.sharedHighlightPriority
            << '\n';
    }
    passed &= Check(
        optionDown.popupBegan &&
            optionDown.popupOpen &&
            !optionDown.transitionActive &&
            optionDown.activeId == optionDown.optionId &&
            optionDown.sharedHighlightValid &&
            optionDown.sharedHighlightPriority == 2 &&
            optionDown.sharedHighlightRounded &&
            Near(
                optionDown.sharedHighlightTargetRect.Min.y,
                optionDown.optionRects[0].Min.y) &&
            optionDown.sharedHighlightRect.Min.y >
                optionDown.sharedHighlightTargetRect.Min.y &&
            optionDown.sharedHighlightRect.Min.y <
                settledOpening.sharedHighlightRect.Min.y &&
            !optionDown.optionPressed &&
            harness.optionPressCount == 0,
        "a ready authored Selectable retains native mouse-down ownership while "
        "the shared highlight continues its glide");

    QueueMouse(optionCenter, false);
    const FrameObservation selection = SubmitComboFrame(
        harness,
        30.0f,
        false);
    if (!selection.optionPressed)
    {
        std::cerr
            << "selection: began=" << selection.popupBegan
            << " open=" << selection.popupOpen
            << " transition=" << selection.transitionActive
            << " active=" << selection.activeId
            << " option=" << selection.optionId
            << " closing=" << selection.popupClosing
            << " count=" << harness.optionPressCount
            << '\n';
    }
    passed &= Check(
        selection.popupBegan &&
            selection.optionPressed &&
            harness.optionPressCount == 1 &&
            harness.selectedItem == 1 &&
            harness.pendingSelectedItem == 0 &&
            selection.popupOpen &&
            selection.transitionActive &&
            selection.popupClosing &&
            !selection.popupInteractionReady &&
            Near(selection.popupRollAmount, 1.0f) &&
            selection.sharedHighlightPriority == 2 &&
            Near(
                selection.sharedHighlightTargetRect.Min.y,
                selection.optionRects[0].Min.y) &&
            selection.popupStyleRestored,
        "a ready native Selectable reports exactly once, stages the choice, and "
        "starts a retained roll-up without mutating the committed preview");

    QueueMouse(optionCenter, true);
    const FrameObservation blockedClosingPress = SubmitComboFrame(
        harness,
        30.0f,
        false);
    passed &= Check(
        blockedClosingPress.popupBegan &&
            blockedClosingPress.popupOpen &&
            blockedClosingPress.transitionActive &&
            blockedClosingPress.popupClosing &&
            blockedClosingPress.activeId != blockedClosingPress.optionId &&
            !blockedClosingPress.optionPressed &&
            harness.optionPressCount == 1 &&
            blockedClosingPress.sharedHighlightPriority == 2 &&
            Near(
                blockedClosingPress.sharedHighlightTargetRect.Min.y,
                blockedClosingPress.optionRects[0].Min.y) &&
            blockedClosingPress.popupRollAmount < selection.popupRollAmount &&
            blockedClosingPress.rollClipObserved &&
            blockedClosingPress.rollClipValid,
        "closing blocks a second click and keeps the clicked priority-two "
        "highlight latched over the still-selected old row");
    QueueMouse(optionCenter, false);
    const FrameObservation blockedClosingRelease = SubmitComboFrame(
        harness,
        30.0f,
        false);
    passed &= Check(
        !blockedClosingRelease.optionPressed &&
            harness.optionPressCount == 1 &&
            blockedClosingRelease.popupClosing &&
            blockedClosingRelease.sharedHighlightPriority == 2 &&
            Near(
                blockedClosingRelease.sharedHighlightTargetRect.Min.y,
                blockedClosingRelease.optionRects[0].Min.y),
        "a release discarded during roll-up is not replayed as another selection");

    bool closingRollMonotonic = true;
    bool closingClipValid = true;
    bool closingHighlightLatched = true;
    bool sawIntermediateClosingRoll = false;
    bool sawHiddenClosingEndpoint = false;
    float previousClosingAmount =
        blockedClosingRelease.popupRollAmount;
    FrameObservation afterSelection = blockedClosingRelease;
    for (int frame = 0; frame < MaximumAnimationFrames; ++frame)
    {
        QueueMouse(outside, false);
        const FrameObservation closingFrame = SubmitComboFrame(
            harness,
            30.0f,
            false);
        if (!closingFrame.popupBegan)
        {
            afterSelection = closingFrame;
            break;
        }
        closingRollMonotonic &=
            closingFrame.popupRollAmount <=
                previousClosingAmount + 1e-5f;
        sawIntermediateClosingRoll |=
            closingFrame.popupRollAmount > 0.0f &&
            closingFrame.popupRollAmount < 1.0f;
        if (closingFrame.popupRollAmount < 1.0f)
        {
            closingClipValid &=
                closingFrame.rollClipObserved &&
                closingFrame.rollClipValid;
        }
        closingHighlightLatched &=
            closingFrame.sharedHighlightPriority == 2 &&
            Near(
                closingFrame.sharedHighlightTargetRect.Min.y,
                closingFrame.optionRects[0].Min.y);
        sawHiddenClosingEndpoint |=
            Near(closingFrame.popupRollAmount, 0.0f) &&
            closingFrame.popupCloseReadyFrame >= 0 &&
            closingFrame.transitionActive;
        previousClosingAmount = closingFrame.popupRollAmount;
        afterSelection = closingFrame;
    }
    passed &= Check(
        closingRollMonotonic &&
            closingClipValid &&
            closingHighlightLatched &&
            sawIntermediateClosingRoll &&
            sawHiddenClosingEndpoint &&
            !afterSelection.popupBegan &&
            !afterSelection.popupOpen &&
            !afterSelection.transitionActive &&
            afterSelection.frame > selection.frame &&
            harness.optionPressCount == 1 &&
            harness.selectedItem == 1 &&
            harness.pendingSelectedItem == 0,
        "roll-up remains monotonic and clipped through one hidden endpoint, "
        "then removes the exact popup on a later frame without double selection");

    ImGui::SetUvsrUiBehavior(false, false, false);
    ComboHarness motionDisabledHarness;
    motionDisabledHarness.selectedItem = 1;
    QueueMouse(outside, false);
    const FrameObservation motionDisabledOpening = SubmitComboFrame(
        motionDisabledHarness,
        30.0f,
        true);
    QueueMouse(outside, false);
    const FrameObservation motionDisabledSettledOpening = SubmitComboFrame(
        motionDisabledHarness,
        30.0f,
        false);
    const ImVec2 motionDisabledOptionCenter =
        Center(motionDisabledSettledOpening.optionRect);
    QueueMouse(motionDisabledOptionCenter, true);
    SubmitComboFrame(motionDisabledHarness, 30.0f, false);
    QueueMouse(motionDisabledOptionCenter, false);
    const FrameObservation motionDisabledSelection = SubmitComboFrame(
        motionDisabledHarness,
        30.0f,
        false);
    passed &= Check(
        motionDisabledOpening.popupBegan &&
            motionDisabledOpening.popupOpen &&
            !motionDisabledOpening.transitionActive &&
            motionDisabledOpening.popupInteractionReady &&
            Near(motionDisabledOpening.popupRollAmount, 1.0f) &&
            motionDisabledSettledOpening.popupBegan &&
            motionDisabledSettledOpening.popupOpen &&
            !motionDisabledSettledOpening.transitionActive &&
            motionDisabledSelection.optionPressed &&
            motionDisabledHarness.optionPressCount == 1 &&
            motionDisabledHarness.selectedItem == 0 &&
            !motionDisabledSelection.popupOpen &&
            !motionDisabledSelection.transitionActive,
        "the animation master switch keeps compact authored styling but snaps "
        "popup open and close to immediate native endpoints");

    ImGui::SetUvsrUiBehavior(false, true, false);
    ComboHarness oggHarness;
    oggHarness.selectedItem = 1;
    QueueMouse(outside, false);
    const FrameObservation oggClosed = SubmitComboFrame(
        oggHarness,
        30.0f,
        false);
    QueueMouse(outside, false);
    const FrameObservation oggOpening = SubmitComboFrame(
        oggHarness,
        30.0f,
        true);
    QueueMouse(outside, false);
    const FrameObservation oggSettledOpening = SubmitComboFrame(
        oggHarness,
        30.0f,
        false);
    const bool oggPopupGeometryCorrect =
        !oggClosed.popupBegan &&
            Near(oggClosed.comboRect.GetWidth(), closed.comboRect.GetWidth()) &&
            Near(oggClosed.comboRect.GetHeight(), closed.comboRect.GetHeight()) &&
            oggClosed.comboVisualBoundsValid &&
            oggClosed.comboVisualBounds.GetHeight() >
                closed.comboVisualBounds.GetHeight() + 2.0f &&
            oggOpening.popupBegan &&
            oggOpening.popupOpen &&
            !oggOpening.transitionActive &&
            !oggOpening.popupClosing &&
            oggOpening.popupInteractionReady &&
            Near(oggOpening.popupRollAmount, 1.0f) &&
            oggSettledOpening.popupBegan &&
            oggSettledOpening.popupOpen &&
            !oggSettledOpening.transitionActive &&
            Near(
                oggSettledOpening.minimumOptionHeight,
                oggSettledOpening.maximumOptionHeight) &&
            Near(
                oggSettledOpening.minimumOptionGap,
                oggSettledOpening.maximumOptionGap) &&
            Near(oggSettledOpening.minimumOptionGap, 0.0f) &&
            Near(oggSettledOpening.popupItemSpacingY, standardItemSpacingY) &&
            Near(
                oggSettledOpening.popupWindowPadding.y,
                standardWindowPaddingY) &&
            oggSettledOpening.sharedHighlightAlpha <= 1.0f / 255.0f &&
            !oggSettledOpening.sharedHighlightRounded &&
            !oggSettledOpening.optionFillRounded[1] &&
            oggSettledOpening.popupStyleRestored;
    if (!oggPopupGeometryCorrect)
    {
        std::cerr
            << "Ogg popup geometry: closed=" << oggClosed.popupBegan
            << " closed-size=" << oggClosed.comboRect.GetWidth() << 'x'
            << oggClosed.comboRect.GetHeight()
            << " authored-size=" << closed.comboRect.GetWidth() << 'x'
            << closed.comboRect.GetHeight()
            << " closed-visual=" << oggClosed.comboVisualBoundsValid << ':'
            << oggClosed.comboVisualBounds.GetHeight()
            << " authored-visual=" << closed.comboVisualBoundsValid << ':'
            << closed.comboVisualBounds.GetHeight()
            << " open=" << oggSettledOpening.popupBegan << '/'
            << oggSettledOpening.popupOpen
            << " height=" << oggSettledOpening.minimumOptionHeight << '/'
            << oggSettledOpening.maximumOptionHeight
            << " gap=" << oggSettledOpening.minimumOptionGap << '/'
            << oggSettledOpening.maximumOptionGap
            << " item-spacing=" << oggSettledOpening.popupItemSpacingY
            << " padding=" << oggSettledOpening.popupWindowPadding.y
            << " selected-rounded="
            << oggSettledOpening.optionFillRounded[1]
            << " style-restored=" << oggSettledOpening.popupStyleRestored
            << '\n';
    }
    passed &= Check(
        oggPopupGeometryCorrect,
        "Ogg retains stock popup padding, row spacing, square Selectable "
        "fills, and the full-height stock closed-combo surface");
    const ImVec2 oggOptionCenter = Center(oggSettledOpening.optionRect);
    QueueMouse(oggOptionCenter, true);
    SubmitComboFrame(oggHarness, 30.0f, false);
    QueueMouse(oggOptionCenter, false);
    const FrameObservation oggSelection = SubmitComboFrame(
        oggHarness,
        30.0f,
        false);
    const bool oggSelectionCorrect =
        oggSelection.optionPressed &&
            oggHarness.optionPressCount == 1 &&
            !oggSelection.popupOpen &&
            !oggSelection.transitionActive &&
            !oggSelection.popupClosing;
    if (!oggSelectionCorrect)
    {
        std::cerr
            << "Ogg popup selection: began=" << oggSelection.popupBegan
            << " pressed=" << oggSelection.optionPressed
            << " open=" << oggSelection.popupOpen
            << " active=" << oggSelection.activeId
            << " option=" << oggSelection.optionId
            << " count=" << oggHarness.optionPressCount
            << " selected=" << oggHarness.selectedItem
            << '\n';
    }
    passed &= Check(
        oggSelectionCorrect,
        "Ogg popup interaction and immediate stock dismissal remain intact");

    comboStyle.PopupRounding = originalPopupRounding;
    comboStyle.FrameRounding = originalComboFrameRounding;

    comboStyle.FrameRounding = AuthoredCornerRounding;
    ImGui::SetUvsrUiBehavior(true, false, false);
    float pickerColor[4] = { 0.25f, 0.50f, 0.75f, 0.78f };
    QueueMouse(outside, false);
    const ColorPickerObservation openingPicker =
        SubmitColorPickerFrame(pickerColor, true);
    const float alphaBeforeOpeningInput = pickerColor[3];
    QueueMouse(outside, false);
    const ColorPickerObservation openingPickerNext =
        SubmitColorPickerFrame(pickerColor, false);
    QueueMouse(outside, false);
    ColorPickerObservation positionedPicker =
        SubmitColorPickerFrame(pickerColor, false);
    for (int frame = 0;
        frame < MaximumAnimationFrames &&
            !positionedPicker.transitionInteractionReady;
        ++frame)
    {
        QueueMouse(outside, false);
        positionedPicker = SubmitColorPickerFrame(pickerColor, false);
    }
    const ColorPickerObservation firstSettledPicker = positionedPicker;
    QueueMouse(outside, false);
    const ColorPickerObservation closingPicker =
        SubmitColorPickerFrame(
            pickerColor,
            false,
            480.0f,
            270.0f,
            330.0f,
            true,
            true);
    QueueMouse(outside, false);
    const ColorPickerObservation closingPickerNext =
        SubmitColorPickerFrame(pickerColor, false);
    QueueMouse(outside, false);
    const ColorPickerObservation reversedPicker =
        SubmitColorPickerFrame(pickerColor, true);
    positionedPicker = reversedPicker;
    for (int frame = 0;
        frame < MaximumAnimationFrames &&
            !positionedPicker.transitionInteractionReady;
        ++frame)
    {
        QueueMouse(outside, false);
        positionedPicker = SubmitColorPickerFrame(pickerColor, false);
    }
    passed &= Check(
        openingPicker.popupOpen &&
            openingPicker.transitionRegistered &&
            openingPicker.transitionTargetOpen &&
            openingPicker.transitionAmount > 0.0f &&
            openingPicker.transitionAmount < 1.0f &&
            !openingPicker.transitionInteractionReady &&
            openingPickerNext.transitionAmount >
                openingPicker.transitionAmount &&
            Near(pickerColor[3], alphaBeforeOpeningInput),
        "the authored picker fades and zooms open while reporting its input "
        "gate closed until the fully visible endpoint");
    const bool pickerReversalContract =
        firstSettledPicker.transitionInteractionReady &&
            closingPicker.popupOpen &&
            closingPicker.transitionRegistered &&
            !closingPicker.transitionTargetOpen &&
            closingPicker.transitionAmount <
                firstSettledPicker.transitionAmount &&
            closingPickerNext.transitionAmount <
                closingPicker.transitionAmount &&
            reversedPicker.transitionTargetOpen &&
            reversedPicker.transitionAmount >
                closingPickerNext.transitionAmount &&
            positionedPicker.transitionInteractionReady;
    if (!pickerReversalContract)
    {
        std::cerr << "picker reversal: settled="
            << firstSettledPicker.transitionInteractionReady << ':'
            << firstSettledPicker.transitionAmount
            << " closing=" << closingPicker.popupOpen << '/'
            << closingPicker.transitionRegistered << '/'
            << closingPicker.transitionTargetOpen << ':'
            << closingPicker.transitionAmount
            << " next=" << closingPickerNext.popupOpen << '/'
            << closingPickerNext.transitionTargetOpen << ':'
            << closingPickerNext.transitionAmount
            << " reversed=" << reversedPicker.popupOpen << '/'
            << reversedPicker.transitionTargetOpen << ':'
            << reversedPicker.transitionAmount
            << " final=" << positionedPicker.popupOpen << '/'
            << positionedPicker.transitionInteractionReady << ':'
            << positionedPicker.transitionAmount << '\n';
    }
    passed &= Check(
        pickerReversalContract,
        "the retained picker reverses an in-flight fade and zoom without "
        "resetting or closing the popup");
    const bool pickerVisualTransitionContract =
        openingPicker.maximumVertexAlpha <
                firstSettledPicker.maximumVertexAlpha &&
            openingPicker.visualBounds.GetWidth() <
                firstSettledPicker.visualBounds.GetWidth() &&
            closingPickerNext.maximumVertexAlpha <
                closingPicker.maximumVertexAlpha &&
            reversedPicker.maximumVertexAlpha >
                closingPickerNext.maximumVertexAlpha;
    if (!pickerVisualTransitionContract)
    {
        std::cerr << "picker visuals: opening="
            << openingPicker.maximumVertexAlpha << ':'
            << openingPicker.visualBounds.GetWidth()
            << " settled=" << firstSettledPicker.maximumVertexAlpha << ':'
            << firstSettledPicker.visualBounds.GetWidth()
            << " closing=" << closingPicker.maximumVertexAlpha << ':'
            << closingPicker.visualBounds.GetWidth()
            << " next=" << closingPickerNext.maximumVertexAlpha << ':'
            << closingPickerNext.visualBounds.GetWidth()
            << " reversed=" << reversedPicker.maximumVertexAlpha << ':'
            << reversedPicker.visualBounds.GetWidth() << '\n';
    }
    passed &= Check(
        pickerVisualTransitionContract,
        "picker transition observations cover synchronous fade and zoom on "
        "both opening and reversible closing");
    passed &= Check(
        openingPicker.sourcePointerTipAttached &&
            closingPicker.sourcePointerTipAttached &&
            closingPickerNext.sourcePointerTipAttached &&
            reversedPicker.sourcePointerTipAttached &&
            positionedPicker.sourcePointerTipAttached,
        "the rounded source pointer keeps its unscaled tip attached to the "
        "canonical Settings edge through open, close, and reversal");
    constexpr float PopupPositionTolerance = 1.0f;
    const bool pickerOutsideSettingsContent =
        positionedPicker.popupOpen &&
        positionedPicker.popupRect.GetWidth() > 0.0f &&
        positionedPicker.popupRect.GetHeight() > 0.0f &&
        positionedPicker.popupRect.Min.x + PopupPositionTolerance >=
            positionedPicker.contentRight &&
        positionedPicker.popupRect.Min.x <=
            positionedPicker.contentRight + PopupPositionTolerance;
    const bool pickerInsideViewport =
        positionedPicker.popupRect.Min.x >=
            positionedPicker.viewportRect.Min.x - PopupPositionTolerance &&
        positionedPicker.popupRect.Min.y >=
            positionedPicker.viewportRect.Min.y - PopupPositionTolerance &&
        positionedPicker.popupRect.Max.x <=
            positionedPicker.viewportRect.Max.x + PopupPositionTolerance &&
        positionedPicker.popupRect.Max.y <=
            positionedPicker.viewportRect.Max.y + PopupPositionTolerance;
    passed &= Check(
        pickerOutsideSettingsContent &&
            pickerInsideViewport &&
            positionedPicker.popupRect.Max.y <=
                positionedPicker.maximumBottom + PopupPositionTolerance,
        "the scoped color-picker popup begins at Settings content-right and "
        "stays within the viewport and menu-stack bottom");
    passed &= Check(
        positionedPicker.requestedSurfaceApplied &&
            positionedPicker.translucentSurfaceCoversCenter &&
            !positionedPicker.opaqueOuterMarginCoversCenter &&
            positionedPicker.activeDrawListReported &&
            positionedPicker.activeDrawListMatchesPopup,
        "the scoped picker receives its translucent surface through the center, "
        "keeps the panel frame out of that center, and reports its draw list");
    const bool pickerFrameCoverageCorrect =
        positionedPicker.opaqueOuterRimComplete &&
            positionedPicker.contentFrameHalfPixelAligned;
    if (!pickerFrameCoverageCorrect)
    {
        std::cerr << "picker frame: band/content="
            << positionedPicker.opaqueOuterRimComplete << '/'
            << positionedPicker.contentFrameHalfPixelAligned << '\n';
    }
    passed &= Check(
        pickerFrameCoverageCorrect,
        "the authored popup fills its full Settings-matched frame band and the "
        "inset content outline keeps complete antialias coverage");
    passed &= Check(
        Near(
            positionedPicker.popupWindowPadding.x,
            ImGui::GetStyle().WindowPadding.x +
                ImGui::GetStyle().ItemInnerSpacing.x) &&
            Near(
                positionedPicker.popupWindowPadding.y,
                ImGui::GetStyle().WindowPadding.y +
                    ImGui::GetStyle().ItemInnerSpacing.y),
        "the authored picker uses Regular frame padding plus one Tight control "
        "inset without changing its authored layout width");
    const bool pickerBrightMarginsCorrect =
        positionedPicker.brightMarginLeft >= 3.0f - 0.01f &&
        positionedPicker.brightMarginLeft <= 6.0f + 0.01f &&
        positionedPicker.brightMarginTop >= 3.0f - 0.01f &&
        positionedPicker.brightMarginTop <= 6.0f + 0.01f &&
        positionedPicker.brightMarginRight >= 3.0f - 0.01f &&
        positionedPicker.brightMarginRight <= 6.0f + 0.01f &&
        positionedPicker.brightMarginBottom >= 3.0f - 0.01f &&
        positionedPicker.brightMarginBottom <= 6.0f + 0.01f;
    if (!pickerBrightMarginsCorrect)
    {
        std::cerr << "picker bright margins: "
            << positionedPicker.brightMarginLeft << '/'
            << positionedPicker.brightMarginTop << '/'
            << positionedPicker.brightMarginRight << '/'
            << positionedPicker.brightMarginBottom
            << " contentY=" << positionedPicker.contentLayerRect.Min.y
            << '-' << positionedPicker.contentLayerRect.Max.y
            << " hexY=" << positionedPicker.hexInputRect.Min.y
            << '-' << positionedPicker.hexInputRect.Max.y
            << " popupY=" << positionedPicker.popupRect.Min.y
            << '-' << positionedPicker.popupRect.Max.y << '\n';
    }
    passed &= Check(
        pickerBrightMarginsCorrect,
        "the brightest popup surface leaves three to six pixels around every "
        "control edge");
    const bool pickerDepthLayersCorrect =
        positionedPicker.requestedContentLayerApplied &&
            positionedPicker.requestedPickerLayerApplied &&
            std::abs(
                positionedPicker.contentLayerRect.Min.x -
                    (positionedPicker.popupInnerRect.Min.x +
                        positionedPicker.brightSurfacePadding.x)) <=
                PopupPositionTolerance &&
            std::abs(
                positionedPicker.contentLayerRect.Min.y -
                    (positionedPicker.popupInnerRect.Min.y +
                        positionedPicker.brightSurfacePadding.y)) <=
                PopupPositionTolerance &&
            std::abs(
                positionedPicker.contentLayerRect.Max.x -
                    (positionedPicker.popupInnerRect.Max.x -
                        positionedPicker.brightSurfacePadding.x)) <=
                PopupPositionTolerance &&
            std::abs(
                positionedPicker.contentLayerRect.Max.y -
                    (positionedPicker.popupInnerRect.Max.y -
                        positionedPicker.brightSurfacePadding.y)) <=
                PopupPositionTolerance &&
            std::abs(
                positionedPicker.observedPickerLayerRect.Min.x -
                    positionedPicker.contentLayerRect.Min.x) <=
                PopupPositionTolerance &&
            std::abs(
                positionedPicker.observedPickerLayerRect.Min.y -
                    positionedPicker.contentLayerRect.Min.y) <=
                PopupPositionTolerance &&
            std::abs(
                positionedPicker.observedPickerLayerRect.Max.x -
                    positionedPicker.contentLayerRect.Max.x) <=
                PopupPositionTolerance &&
            std::abs(
                positionedPicker.observedPickerLayerRect.Max.y -
                    positionedPicker.contentLayerRect.Max.y) <=
                PopupPositionTolerance;
    if (!pickerDepthLayersCorrect)
    {
        std::cerr << "picker layers: content/picker="
            << positionedPicker.requestedContentLayerApplied << '/'
            << positionedPicker.requestedPickerLayerApplied
            << " content=(" << positionedPicker.contentLayerRect.Min.x << ','
            << positionedPicker.contentLayerRect.Min.y << ")-("
            << positionedPicker.contentLayerRect.Max.x << ','
            << positionedPicker.contentLayerRect.Max.y << ") inner=("
            << positionedPicker.popupInnerRect.Min.x << ','
            << positionedPicker.popupInnerRect.Min.y << ")-("
            << positionedPicker.popupInnerRect.Max.x << ','
            << positionedPicker.popupInnerRect.Max.y << ") picker=("
            << positionedPicker.observedPickerLayerRect.Min.x << ','
            << positionedPicker.observedPickerLayerRect.Min.y << ")-("
            << positionedPicker.observedPickerLayerRect.Max.x << ','
            << positionedPicker.observedPickerLayerRect.Max.y << ") expected=("
            << positionedPicker.expectedPickerLayerRect.Min.x << ','
            << positionedPicker.expectedPickerLayerRect.Min.y << ")-("
            << positionedPicker.expectedPickerLayerRect.Max.x << ','
            << positionedPicker.expectedPickerLayerRect.Max.y << ")\n";
    }
    passed &= Check(
        pickerDepthLayersCorrect,
        "the authored picker keeps a translucent base, fills the complete frame "
        "band, and expands both bright depth layers around every control");
    if (!positionedPicker.finalCursorLayeredAndClipped)
    {
        std::cerr << "picker final cursor layer: center=("
            << positionedPicker.selectorCursorCenter.x << ','
            << positionedPicker.selectorCursorCenter.y << ") radius="
            << positionedPicker.endpointMarkerRadius
            << " fringe=" << positionedPicker.drawListFringe << '\n';
    }
    passed &= Check(
        positionedPicker.finalCursorLayeredAndClipped,
        "the authored selector cursor retains its final body layer and popup "
        "clip before the attached source pointer is appended");
    passed &= Check(
        positionedPicker.barCount == 4 &&
            positionedPicker.hueBarRect.GetWidth() > 0.0f &&
            positionedPicker.hueBarRect.GetHeight() > 0.0f &&
            positionedPicker.hueBarRect.Min.x >=
                positionedPicker.popupRect.Min.x &&
            positionedPicker.hueBarRect.Max.x <=
                positionedPicker.popupRect.Max.x + PopupPositionTolerance &&
            positionedPicker.alphaBarRect.GetWidth() > 0.0f &&
            positionedPicker.alphaBarRect.GetHeight() > 0.0f &&
            positionedPicker.alphaBarRect.Min.x >=
                positionedPicker.popupRect.Min.x &&
            positionedPicker.alphaBarRect.Max.x <=
                positionedPicker.popupRect.Max.x + PopupPositionTolerance &&
            std::abs(
                positionedPicker.hueBarRect.GetWidth() -
                    positionedPicker.alphaBarRect.GetWidth()) <=
                PopupPositionTolerance &&
            std::abs(
                positionedPicker.alphaBarRect.GetWidth() -
                    positionedPicker.currentBarRect.GetWidth()) <=
                PopupPositionTolerance &&
            std::abs(
                positionedPicker.currentBarRect.GetWidth() -
                    positionedPicker.originalBarRect.GetWidth()) <=
                PopupPositionTolerance &&
            std::abs(
                positionedPicker.originalBarRect.Max.x -
                    (positionedPicker.popupRect.Max.x -
                        positionedPicker.popupWindowPadding.x)) <=
                PopupPositionTolerance,
        "the viewport-fitted authored wheel retains four equal bars whose "
        "outer edge aligns with the fourth input column");
    passed &= Check(
        positionedPicker.hueBarRoundedAndVisible,
        "the authored wheel renders the retained hue bar with rounded coverage");
    passed &= Check(
        positionedPicker.alphaBarRoundedAndVisible,
        "the authored wheel renders the retained alpha bar with rounded coverage");
    const bool wheelGradientOutlinesVisible =
        positionedPicker.innerWheelGradientOutlineVisible &&
            positionedPicker.outerWheelGradientOutlineVisible;
    if (!wheelGradientOutlinesVisible)
    {
        std::cerr << "picker wheel outlines: inner/outer="
            << positionedPicker.innerWheelGradientOutlineVisible << '/'
            << positionedPicker.outerWheelGradientOutlineVisible
            << " bins="
            << positionedPicker.innerWheelOutlineCoveredBins << '/'
            << positionedPicker.outerWheelOutlineCoveredBins
            << " alpha="
            << positionedPicker.innerWheelOutlineTopAlpha << '/'
            << positionedPicker.innerWheelOutlineBottomAlpha << ':'
            << positionedPicker.outerWheelOutlineTopAlpha << '/'
            << positionedPicker.outerWheelOutlineBottomAlpha << '\n';
    }
    passed &= Check(
        wheelGradientOutlinesVisible,
        "the authored hue wheel renders a visible one-pixel white transparency "
        "gradient around both circumferences");
    passed &= Check(
        positionedPicker.alphaCheckerEdgeCoverageContinuous,
        "the alpha checkerboard retains tight coverage at every straight edge "
        "and rounded corner");
    passed &= Check(
        positionedPicker.hueHollowMarkerVisible,
        "the authored hue bar renders one hollow black-and-white circle marker");
    passed &= Check(
        positionedPicker.alphaHollowMarkerVisible,
        "the authored alpha bar renders one hollow black-and-white circle marker");
    passed &= Check(
        positionedPicker.endpointMarkerRadius > 0.0f &&
            positionedPicker.endpointMarkerRadius <
                positionedPicker.endpointSnapRadius,
        "selector endpoint circles stay visibly smaller than their forgiving "
        "full-gamut pointer snap zones");
    const bool pickerMarkerClearanceCorrect =
        positionedPicker.selectorMarkerCursorClearance &&
            positionedPicker.minimumMarkerCursorGap + 0.01f >=
                positionedPicker.requiredMarkerCursorGap;
    passed &= Check(
        pickerMarkerClearanceCorrect,
        "visible selector endpoint markers keep one fringe of outer-stroke "
        "clearance from the active selector cursor");
    const bool comparisonBarsCorrect =
        positionedPicker.currentBarVisible &&
        positionedPicker.originalBarVisible &&
        positionedPicker.bottomControlsSpanAllBars;
    if (!comparisonBarsCorrect)
    {
        std::cerr << "picker comparison bars: current/original/span="
            << positionedPicker.currentBarVisible << '/'
            << positionedPicker.originalBarVisible << '/'
            << positionedPicker.bottomControlsSpanAllBars
            << " currentX=" << positionedPicker.currentBarRect.Min.x
            << '-' << positionedPicker.currentBarRect.Max.x
            << " originalX=" << positionedPicker.originalBarRect.Min.x
            << '-' << positionedPicker.originalBarRect.Max.x << '\n';
    }
    passed &= Check(
        comparisonBarsCorrect,
        "unlabeled Current and Original comparison bars remain visible and "
        "the bottom controls span through the fourth bar");
    const bool pickerPointerCorrect =
        positionedPicker.sourcePointerVisible &&
            positionedPicker.sourcePointerTargetsSwatch &&
            positionedPicker.sourcePointerBaseAttached &&
            positionedPicker.sourcePointerUsesCarvedFrame &&
            positionedPicker.sourcePointerFrameMaximumAlpha >= 0.999f &&
            positionedPicker.authoredPopupUsesCarvedFrameOnly;
    if (!pickerPointerCorrect)
    {
        std::cerr << "picker pointer: visible/target/base/frame/border="
            << positionedPicker.sourcePointerVisible << '/'
            << positionedPicker.sourcePointerTargetsSwatch << '/'
            << positionedPicker.sourcePointerBaseAttached << '/'
            << positionedPicker.sourcePointerUsesCarvedFrame << '/'
            << positionedPicker.authoredPopupUsesCarvedFrameOnly
            << " alpha="
            << positionedPicker.sourcePointerFrameMaximumAlpha << '\n';
    }
    passed &= Check(
        pickerPointerCorrect,
        "the authored picker uses the Settings-matched pointer frame with a "
        "translucent interior aimed horizontally at the source row");
    passed &= Check(
        positionedPicker.sourceSwatchShadowVisible &&
            positionedPicker.sourceSwatchBorderless,
        "the authored source swatch keeps Settings frame parity through a "
        "borderless face and offset shadow");
    passed &= Check(
        positionedPicker.hueBarInteriorOpaque &&
            positionedPicker.opaqueSelectorVertexCount > 32,
        "the translucent authored popup retains fully opaque hue and "
        "saturation/value color meshes at steady state");
    passed &= Check(
        positionedPicker.selectorVisualEndpointsFound,
        "the rounded selector emits exact opaque hue, black, and white "
        "visual endpoints");

    QueueMouseButton(
        Center(positionedPicker.hueBarRect),
        ImGuiMouseButton_Right,
        true);
    const ColorPickerObservation pickerRightInsideDown =
        SubmitColorPickerFrame(pickerColor, false);
    QueueMouseButton(
        Center(positionedPicker.hueBarRect),
        ImGuiMouseButton_Right,
        false);
    const ColorPickerObservation pickerRightInsideUp =
        SubmitColorPickerFrame(pickerColor, false);
    passed &= Check(
        pickerRightInsideDown.popupOpen &&
            pickerRightInsideDown.transitionTargetOpen &&
            pickerRightInsideDown.onlyPickerPopupOpen &&
            pickerRightInsideUp.popupOpen &&
            pickerRightInsideUp.transitionTargetOpen &&
            pickerRightInsideUp.onlyPickerPopupOpen,
        "right-clicking authored picker controls cannot create an options "
        "popup or disturb the retained picker");

    QueueMouseButton(
        Center(positionedPicker.sourceSwatchRect),
        ImGuiMouseButton_Right,
        true);
    const ColorPickerObservation pickerRightSourceDown =
        SubmitColorPickerFrame(pickerColor, false);
    QueueMouseButton(
        Center(positionedPicker.sourceSwatchRect),
        ImGuiMouseButton_Right,
        false);
    const ColorPickerObservation pickerRightSourceUp =
        SubmitColorPickerFrame(pickerColor, false);
    QueueMouse(outside, false);
    const ColorPickerObservation pickerRightSourceReversed =
        SubmitColorPickerFrame(pickerColor, true);
    positionedPicker = pickerRightSourceReversed;
    for (int frame = 0;
        frame < MaximumAnimationFrames &&
            !positionedPicker.transitionInteractionReady;
        ++frame)
    {
        QueueMouse(outside, false);
        positionedPicker = SubmitColorPickerFrame(pickerColor, false);
    }
    passed &= Check(
        pickerRightSourceDown.popupOpen &&
            pickerRightSourceDown.transitionRegistered &&
            !pickerRightSourceDown.transitionTargetOpen &&
            pickerRightSourceDown.onlyPickerPopupOpen &&
            pickerRightSourceUp.popupOpen &&
            pickerRightSourceUp.onlyPickerPopupOpen &&
            pickerRightSourceReversed.popupOpen &&
            pickerRightSourceReversed.transitionTargetOpen &&
            positionedPicker.transitionInteractionReady &&
            positionedPicker.onlyPickerPopupOpen,
        "right-clicking the source swatch cannot create options or bypass the "
        "retained close, and an immediate reopen reverses that close");

    const std::array<float, 4> comparisonBarBaseline = {
        pickerColor[0],
        pickerColor[1],
        pickerColor[2],
        pickerColor[3]
    };
    QueueMouse(Center(positionedPicker.currentBarRect), true);
    SubmitColorPickerFrame(pickerColor, false);
    QueueMouse(Center(positionedPicker.currentBarRect), false);
    SubmitColorPickerFrame(pickerColor, false);
    QueueMouse(Center(positionedPicker.originalBarRect), true);
    SubmitColorPickerFrame(pickerColor, false);
    QueueMouse(Center(positionedPicker.originalBarRect), false);
    const ColorPickerObservation afterComparisonBars =
        SubmitColorPickerFrame(pickerColor, false);
    passed &= Check(
        afterComparisonBars.popupOpen &&
            Near(pickerColor[0], comparisonBarBaseline[0]) &&
            Near(pickerColor[1], comparisonBarBaseline[1]) &&
            Near(pickerColor[2], comparisonBarBaseline[2]) &&
            Near(pickerColor[3], comparisonBarBaseline[3]),
        "Current and Original are display-only bars with no hidden reset or "
        "value mutation hit targets");

    const ImVec2 saturatedDirection = Scale(
        Subtract(
            afterComparisonBars.selectorEndpoints[0],
            afterComparisonBars.selectorRect.GetCenter()),
        1.0f / ImMax(
            ImSqrt(ImLengthSqr(Subtract(
                afterComparisonBars.selectorEndpoints[0],
                afterComparisonBars.selectorRect.GetCenter()))),
            FLT_EPSILON));
    const ImVec2 saturatedTopInput = Add(
        afterComparisonBars.selectorEndpoints[0],
        Scale(
            saturatedDirection,
            afterComparisonBars.endpointSnapRadius * 0.80f));
    QueueMouse(saturatedTopInput, true);
    const ColorPickerObservation saturatedPicker =
        SubmitColorPickerFrame(pickerColor, false);
    QueueMouse(saturatedTopInput, false);
    const ColorPickerObservation afterSaturatedPicker =
        SubmitColorPickerFrame(pickerColor, false);
    float saturatedHue = 0.0f;
    float saturatedAmount = 0.0f;
    float saturatedValue = 0.0f;
    ImGui::ColorConvertRGBtoHSV(
        pickerColor[0],
        pickerColor[1],
        pickerColor[2],
        saturatedHue,
        saturatedAmount,
        saturatedValue);
    if (!(saturatedPicker.popupOpen &&
        saturatedAmount >= 0.999f &&
        saturatedValue >= 0.999f))
    {
        std::cerr << "rounded selector saturated endpoint: rect=("
            << positionedPicker.selectorRect.Min.x << ','
            << positionedPicker.selectorRect.Min.y << ")-("
            << positionedPicker.selectorRect.Max.x << ','
            << positionedPicker.selectorRect.Max.y << ") input=("
            << saturatedTopInput.x << ',' << saturatedTopInput.y
            << ") hsv=" << saturatedHue << ',' << saturatedAmount
            << ',' << saturatedValue << '\n';
    }
    passed &= Check(
        saturatedPicker.popupOpen &&
            saturatedAmount >= 0.999f &&
            saturatedValue >= 0.999f,
        "the rounded selector keeps the fully saturated hue endpoint "
        "pointer-reachable");

    const ImVec2 whiteDirection = Scale(
        Subtract(
            afterSaturatedPicker.selectorEndpoints[2],
            afterSaturatedPicker.selectorRect.GetCenter()),
        1.0f / ImMax(
            ImSqrt(ImLengthSqr(Subtract(
                afterSaturatedPicker.selectorEndpoints[2],
                afterSaturatedPicker.selectorRect.GetCenter()))),
            FLT_EPSILON));
    const ImVec2 whiteInput = Add(
        afterSaturatedPicker.selectorEndpoints[2],
        Scale(
            whiteDirection,
            afterSaturatedPicker.endpointSnapRadius * 0.80f));
    QueueMouse(whiteInput, true);
    const ColorPickerObservation whitePicker =
        SubmitColorPickerFrame(pickerColor, false);
    QueueMouse(whiteInput, false);
    const ColorPickerObservation afterWhitePicker =
        SubmitColorPickerFrame(pickerColor, false);
    if (!(whitePicker.popupOpen &&
        pickerColor[0] >= 0.999f &&
        pickerColor[1] >= 0.999f &&
        pickerColor[2] >= 0.999f))
    {
        std::cerr << "rounded selector white endpoint: input=("
            << whiteInput.x << ',' << whiteInput.y << ") rgb="
            << pickerColor[0] << ',' << pickerColor[1] << ','
            << pickerColor[2] << '\n';
    }
    passed &= Check(
        whitePicker.popupOpen &&
            pickerColor[0] >= 0.999f &&
            pickerColor[1] >= 0.999f &&
            pickerColor[2] >= 0.999f,
        "the rounded selector keeps exact white pointer-reachable");
    const bool whiteEndpointMarkerExcluded =
        afterWhitePicker.selectorEndpointExclusionObserved &&
            afterWhitePicker.selectorMarkerCursorClearance;
    if (!whiteEndpointMarkerExcluded)
    {
        std::cerr << "white endpoint marker exclusion: observed/contract="
            << afterWhitePicker.selectorEndpointExclusionObserved << '/'
            << afterWhitePicker.selectorMarkerCursorClearance
            << " cursor=(" << afterWhitePicker.selectorCursorCenter.x << ','
            << afterWhitePicker.selectorCursorCenter.y << ") endpoints=("
            << afterWhitePicker.selectorEndpoints[0].x << ','
            << afterWhitePicker.selectorEndpoints[0].y << ");("
            << afterWhitePicker.selectorEndpoints[1].x << ','
            << afterWhitePicker.selectorEndpoints[1].y << ");("
            << afterWhitePicker.selectorEndpoints[2].x << ','
            << afterWhitePicker.selectorEndpoints[2].y << ") visible="
            << afterWhitePicker.selectorEndpointMarkerVisible[0] << ','
            << afterWhitePicker.selectorEndpointMarkerVisible[1] << ','
            << afterWhitePicker.selectorEndpointMarkerVisible[2] << '\n';
    }
    passed &= Check(
        whiteEndpointMarkerExcluded,
        "an endpoint marker is omitted whenever its rendered outer stroke "
        "would enter the selector cursor's one-fringe exclusion gap");

    const ImVec2 blackDirection = Scale(
        Subtract(
            afterWhitePicker.selectorEndpoints[1],
            afterWhitePicker.selectorRect.GetCenter()),
        1.0f / ImMax(
            ImSqrt(ImLengthSqr(Subtract(
                afterWhitePicker.selectorEndpoints[1],
                afterWhitePicker.selectorRect.GetCenter()))),
            FLT_EPSILON));
    const ImVec2 blackInput = Add(
        afterWhitePicker.selectorEndpoints[1],
        Scale(
            blackDirection,
            afterWhitePicker.endpointSnapRadius * 0.80f));
    QueueMouse(blackInput, true);
    const ColorPickerObservation blackPicker =
        SubmitColorPickerFrame(pickerColor, false);
    QueueMouse(blackInput, false);
    SubmitColorPickerFrame(pickerColor, false);
    if (!(blackPicker.popupOpen &&
        pickerColor[0] <= 0.001f &&
        pickerColor[1] <= 0.001f &&
        pickerColor[2] <= 0.001f))
    {
        std::cerr << "rounded selector black endpoint: input=("
            << blackInput.x << ',' << blackInput.y << ") rgb="
            << pickerColor[0] << ',' << pickerColor[1] << ','
            << pickerColor[2] << '\n';
    }
    passed &= Check(
        blackPicker.popupOpen &&
            pickerColor[0] <= 0.001f &&
            pickerColor[1] <= 0.001f &&
            pickerColor[2] <= 0.001f,
        "the rounded selector keeps exact black pointer-reachable");

    const ImVec2 alphaBarInput(
        (positionedPicker.alphaBarRect.Min.x +
            positionedPicker.alphaBarRect.Max.x) * 0.5f,
        positionedPicker.alphaBarRect.Min.y +
            (positionedPicker.alphaBarRect.GetHeight() - 1.0f) * 0.75f);
    QueueMouse(alphaBarInput, true);
    const ColorPickerObservation editedPicker =
        SubmitColorPickerFrame(pickerColor, false);
    QueueMouse(alphaBarInput, false);
    SubmitColorPickerFrame(pickerColor, false);
    passed &= Check(
        editedPicker.popupOpen &&
            editedPicker.colorAlpha < positionedPicker.colorAlpha &&
            editedPicker.colorAlpha > 0.15f &&
            editedPicker.colorAlpha < 0.35f,
        "the actual scoped ColorEdit4 popup alpha bar updates the stored alpha");

    QueueMouse(outside, false);
    const ColorPickerObservation targetedClosingPicker =
        SubmitColorPickerFrame(
            pickerColor,
            false,
            480.0f,
            270.0f,
            330.0f,
            true,
            true);
    int zeroEndpointFrameCount =
        targetedClosingPicker.transitionAtZeroEndpoint ? 1 : 0;
    ColorPickerObservation targetedCloseProgress = targetedClosingPicker;
    for (int frame = 0;
        frame < MaximumAnimationFrames &&
            targetedCloseProgress.popupOpen &&
            !targetedCloseProgress.transitionAtZeroEndpoint;
        ++frame)
    {
        QueueMouse(outside, false);
        targetedCloseProgress = SubmitColorPickerFrame(pickerColor, false);
        zeroEndpointFrameCount +=
            targetedCloseProgress.transitionAtZeroEndpoint ? 1 : 0;
    }
    QueueMouse(outside, false);
    const ColorPickerObservation targetedClosedPicker =
        SubmitColorPickerFrame(pickerColor, false);
    passed &= Check(
        targetedClosingPicker.popupOpen &&
            !targetedClosingPicker.transitionTargetOpen &&
            targetedClosingPicker.transitionAmount < 1.0f &&
            targetedCloseProgress.popupOpen &&
            targetedCloseProgress.transitionAtZeroEndpoint &&
            zeroEndpointFrameCount == 1 &&
            !targetedClosedPicker.popupOpen &&
            !targetedClosedPicker.transitionRegistered &&
            !targetedClosedPicker.activeDrawListReported,
        "the targeted picker close API retains the popup through fade and "
        "zoom, submits exactly one zero endpoint, then commits dismissal");

    struct GuardedRgbColor
    {
        float before = 91.25f;
        float color[3] = { 0.35f, 0.45f, 0.65f };
        float after = -73.50f;
    } rgbPickerStorage;
    const float rgbBeforeCanary = rgbPickerStorage.before;
    const float rgbAfterCanary = rgbPickerStorage.after;
    const std::array<float, 3> rgbOriginal = {
        rgbPickerStorage.color[0],
        rgbPickerStorage.color[1],
        rgbPickerStorage.color[2]
    };
    static constexpr const char* MaterialColorLabel =
        "Diffuse Color##MaterialVisibleRgb";
    constexpr float NestedCallerAlpha = 0.37f;
    QueueMouse(outside, false);
    ColorPickerObservation rgbPicker = SubmitColorPickerFrame(
        rgbPickerStorage.color,
        true,
        480.0f,
        270.0f,
        330.0f,
        true,
        false,
        ImVec4(0.17f, 0.29f, 0.41f, 0.93f),
        ImVec4(0.17f, 0.29f, 0.41f, 1.0f),
        ImVec4(0.11f, 0.23f, 0.37f, 0.67f),
        ImVec4(0.43f, 0.19f, 0.31f, 0.71f),
        false,
        MaterialColorLabel,
        true,
        NestedCallerAlpha);
    for (int frame = 0;
        frame < MaximumAnimationFrames &&
            !rgbPicker.transitionInteractionReady;
        ++frame)
    {
        QueueMouse(outside, false);
        rgbPicker = SubmitColorPickerFrame(
            rgbPickerStorage.color,
            false,
            480.0f,
            270.0f,
            330.0f,
            true,
            false,
            ImVec4(0.17f, 0.29f, 0.41f, 0.93f),
            ImVec4(0.17f, 0.29f, 0.41f, 1.0f),
            ImVec4(0.11f, 0.23f, 0.37f, 0.67f),
            ImVec4(0.43f, 0.19f, 0.31f, 0.71f),
            false,
            MaterialColorLabel,
            true,
            NestedCallerAlpha);
    }
    const bool rgbInputRectsObserved =
        !rgbPicker.fourthRgbInputRect.IsInverted() &&
        !rgbPicker.fourthHsvInputRect.IsInverted() &&
        !rgbPicker.hexInputRect.IsInverted();
    constexpr float ObservedFrameEdgeTolerance = 1.5f;
    const bool rgbObservedBarAlignment =
        !rgbPicker.observedHueBarRect.IsInverted() &&
        !rgbPicker.observedOriginalBarRect.IsInverted() &&
        std::abs(
            rgbPicker.fourthRgbInputRect.Min.x -
                rgbPicker.observedHueBarRect.Min.x) <=
            ObservedFrameEdgeTolerance &&
        std::abs(
            rgbPicker.fourthRgbInputRect.Max.x -
                rgbPicker.observedOriginalBarRect.Max.x) <=
            ObservedFrameEdgeTolerance &&
        std::abs(
            rgbPicker.fourthHsvInputRect.Min.x -
                rgbPicker.observedHueBarRect.Min.x) <=
            ObservedFrameEdgeTolerance &&
        std::abs(
            rgbPicker.fourthHsvInputRect.Max.x -
                rgbPicker.observedOriginalBarRect.Max.x) <=
            ObservedFrameEdgeTolerance &&
        std::abs(
            rgbPicker.hexInputRect.Max.x -
                rgbPicker.observedOriginalBarRect.Max.x) <=
            ObservedFrameEdgeTolerance;
    const bool rgbVisibleNestedParity =
        Near(rgbPicker.popupWindowPadding.x, positionedPicker.popupWindowPadding.x) &&
        Near(rgbPicker.popupWindowPadding.y, positionedPicker.popupWindowPadding.y) &&
        Near(rgbPicker.popupRect.GetWidth(), positionedPicker.popupRect.GetWidth()) &&
        Near(rgbPicker.popupRect.GetHeight(), positionedPicker.popupRect.GetHeight()) &&
        Near(
            rgbPicker.hueBarRect.Min.x - rgbPicker.popupRect.Min.x,
            positionedPicker.hueBarRect.Min.x - positionedPicker.popupRect.Min.x) &&
        Near(
            rgbPicker.hueBarRect.Min.y - rgbPicker.popupRect.Min.y,
            positionedPicker.hueBarRect.Min.y - positionedPicker.popupRect.Min.y) &&
        Near(
            rgbPicker.contentLayerRect.GetWidth(),
            positionedPicker.contentLayerRect.GetWidth()) &&
        Near(
            rgbPicker.contentLayerRect.GetHeight(),
            positionedPicker.contentLayerRect.GetHeight());
    const bool rgbPickerContract =
        rgbPicker.popupOpen &&
        rgbPicker.transitionInteractionReady &&
        rgbPicker.barCount == 4 &&
        rgbPicker.hueBarRoundedAndVisible &&
        rgbPicker.alphaBarRect.GetWidth() > 0.0f &&
        rgbPicker.disabledAlphaBarVisible &&
        !rgbPicker.alphaBarRoundedAndVisible &&
        !rgbPicker.alphaHollowMarkerVisible &&
        rgbPicker.currentBarVisible &&
        rgbPicker.originalBarVisible &&
        rgbPicker.bottomControlsSpanAllBars &&
        rgbPicker.subordinatePreviewSquareCount == 0 &&
        rgbInputRectsObserved &&
        rgbObservedBarAlignment &&
        rgbVisibleNestedParity &&
        rgbPicker.requestedSurfaceApplied &&
        rgbPicker.requestedContentLayerApplied &&
        rgbPicker.requestedPickerLayerApplied &&
        rgbPicker.opaqueOuterRimComplete &&
        std::abs(
            rgbPicker.sourcePointerFrameMaximumAlpha -
                NestedCallerAlpha) <= 1.0f / 255.0f + 0.001f &&
        rgbPicker.translucentSurfaceCoversCenter &&
        !rgbPicker.opaqueOuterMarginCoversCenter;
    if (!rgbPickerContract)
    {
        std::cerr << "nested RGB picker: open/ready/bars="
            << rgbPicker.popupOpen << '/'
            << rgbPicker.transitionInteractionReady << '/'
            << rgbPicker.barCount
            << " visual=" << rgbPicker.hueBarRoundedAndVisible << '/'
            << rgbPicker.disabledAlphaBarVisible << '/'
            << rgbPicker.alphaBarRoundedAndVisible << '/'
            << rgbPicker.alphaHollowMarkerVisible << '/'
            << rgbPicker.currentBarVisible << '/'
            << rgbPicker.originalBarVisible
            << " previewSquares="
            << rgbPicker.subordinatePreviewSquareCount
            << " rows/alignment/parity=" << rgbInputRectsObserved << '/'
            << rgbObservedBarAlignment << '/'
            << rgbVisibleNestedParity
            << " layer/rim=" << rgbPicker.requestedPickerLayerApplied << '/'
            << rgbPicker.opaqueOuterRimComplete
            << " surface=" << rgbPicker.translucentSurfaceCoversCenter << '/'
            << rgbPicker.opaqueOuterMarginCoversCenter
            << " padding=" << rgbPicker.popupWindowPadding.x << ','
            << rgbPicker.popupWindowPadding.y
            << " popup=" << rgbPicker.popupRect.GetWidth() << 'x'
            << rgbPicker.popupRect.GetHeight()
            << " rgba=" << positionedPicker.popupRect.GetWidth() << 'x'
            << positionedPicker.popupRect.GetHeight()
            << " rgb4=(" << rgbPicker.fourthRgbInputRect.Min.x << ','
            << rgbPicker.fourthRgbInputRect.Max.x << ") bars=("
            << rgbPicker.observedHueBarRect.Min.x << ','
            << rgbPicker.observedOriginalBarRect.Max.x << ")\n";
    }
    passed &= Check(
        rgbPickerContract,
        "nested visible-label ColorEdit3 matches the RGBA popup, removes row "
        "previews, aligns the observed fourth input to the observed four-bar "
        "group, and gives its full frame band and pointer the same inherited "
        "alpha as the surrounding panel frame");

    QueueMouse(Center(rgbPicker.alphaBarRect), true);
    SubmitColorPickerFrame(
        rgbPickerStorage.color,
        false,
        480.0f,
        270.0f,
        330.0f,
        true,
        false,
        ImVec4(0.17f, 0.29f, 0.41f, 0.93f),
        ImVec4(0.17f, 0.29f, 0.41f, 1.0f),
        ImVec4(0.11f, 0.23f, 0.37f, 0.67f),
        ImVec4(0.43f, 0.19f, 0.31f, 0.71f),
        false,
        MaterialColorLabel,
        true,
        NestedCallerAlpha);
    QueueMouse(Center(rgbPicker.alphaBarRect), false);
    SubmitColorPickerFrame(
        rgbPickerStorage.color,
        false,
        480.0f,
        270.0f,
        330.0f,
        true,
        false,
        ImVec4(0.17f, 0.29f, 0.41f, 0.93f),
        ImVec4(0.17f, 0.29f, 0.41f, 1.0f),
        ImVec4(0.11f, 0.23f, 0.37f, 0.67f),
        ImVec4(0.43f, 0.19f, 0.31f, 0.71f),
        false,
        MaterialColorLabel,
        true,
        NestedCallerAlpha);
    QueueMouse(Center(rgbPicker.fourthRgbInputRect), true);
    SubmitColorPickerFrame(
        rgbPickerStorage.color,
        false,
        480.0f,
        270.0f,
        330.0f,
        true,
        false,
        ImVec4(0.17f, 0.29f, 0.41f, 0.93f),
        ImVec4(0.17f, 0.29f, 0.41f, 1.0f),
        ImVec4(0.11f, 0.23f, 0.37f, 0.67f),
        ImVec4(0.43f, 0.19f, 0.31f, 0.71f),
        false,
        MaterialColorLabel,
        true,
        NestedCallerAlpha);
    QueueMouse(Center(rgbPicker.fourthRgbInputRect), false);
    SubmitColorPickerFrame(
        rgbPickerStorage.color,
        false,
        480.0f,
        270.0f,
        330.0f,
        true,
        false,
        ImVec4(0.17f, 0.29f, 0.41f, 0.93f),
        ImVec4(0.17f, 0.29f, 0.41f, 1.0f),
        ImVec4(0.11f, 0.23f, 0.37f, 0.67f),
        ImVec4(0.43f, 0.19f, 0.31f, 0.71f),
        false,
        MaterialColorLabel,
        true,
        NestedCallerAlpha);
    passed &= Check(
        Near(rgbPickerStorage.before, rgbBeforeCanary) &&
            Near(rgbPickerStorage.after, rgbAfterCanary) &&
            Near(rgbPickerStorage.color[0], rgbOriginal[0]) &&
            Near(rgbPickerStorage.color[1], rgbOriginal[1]) &&
            Near(rgbPickerStorage.color[2], rgbOriginal[2]),
        "disabled RGB alpha lane and fourth component field never access or "
        "mutate storage beyond the true three-float color");
    QueueMouse(outside, false);
    SubmitColorPickerFrame(
        rgbPickerStorage.color,
        false,
        480.0f,
        270.0f,
        330.0f,
        true,
        true,
        ImVec4(0.17f, 0.29f, 0.41f, 0.93f),
        ImVec4(0.17f, 0.29f, 0.41f, 1.0f),
        ImVec4(0.11f, 0.23f, 0.37f, 0.67f),
        ImVec4(0.43f, 0.19f, 0.31f, 0.71f),
        false,
        MaterialColorLabel,
        true,
        NestedCallerAlpha);
    for (int frame = 0;
        frame < MaximumAnimationFrames &&
            GImGui->OpenPopupStack.Size > 0;
        ++frame)
    {
        QueueMouse(outside, false);
        SubmitColorPickerFrame(
            rgbPickerStorage.color,
            false,
            480.0f,
            270.0f,
            330.0f,
            true,
            false,
            ImVec4(0.17f, 0.29f, 0.41f, 0.93f),
            ImVec4(0.17f, 0.29f, 0.41f, 1.0f),
            ImVec4(0.11f, 0.23f, 0.37f, 0.67f),
            ImVec4(0.43f, 0.19f, 0.31f, 0.71f),
            false,
            MaterialColorLabel,
            true,
            NestedCallerAlpha);
    }

    ImGui::SetUvsrUiBehavior(false, false, false);
    float noMotionPickerColor[4] = { 0.25f, 0.50f, 0.75f, 0.78f };
    QueueMouse(outside, false);
    const ColorPickerObservation noMotionPicker = SubmitColorPickerFrame(
        noMotionPickerColor,
        true);
    QueueMouse(outside, false);
    const ColorPickerObservation noMotionClosedPicker = SubmitColorPickerFrame(
        noMotionPickerColor,
        false,
        480.0f,
        270.0f,
        330.0f,
        true,
        true);
    passed &= Check(
        noMotionPicker.popupOpen &&
            noMotionPicker.transitionInteractionReady &&
            noMotionPicker.transitionAmount >= 1.0f &&
            noMotionPicker.maximumVertexAlpha >= 0.999f &&
            !noMotionClosedPicker.popupOpen &&
            !noMotionClosedPicker.transitionRegistered,
        "disabling authored motion snaps picker opening and targeted closing "
        "to their immediate endpoints");

    const ImVec4 defaultPickerSurface(0.17f, 0.29f, 0.41f, 0.93f);
    const ImVec4 defaultOuterMarginLayer(0.17f, 0.29f, 0.41f, 1.0f);
    const ImVec4 defaultContentLayer(0.11f, 0.23f, 0.37f, 0.67f);
    const ImVec4 defaultPickerLayer(0.43f, 0.19f, 0.31f, 0.71f);
    float centeredSourcePickerColor[4] = {
        0.25f,
        0.50f,
        0.75f,
        0.78f
    };
    QueueMouse(outside, false);
    const ColorPickerObservation centeredSourcePicker =
        SubmitColorPickerFrame(
            centeredSourcePickerColor,
            true,
            480.0f,
            270.0f,
            350.0f,
            true,
            false,
            defaultPickerSurface,
            defaultOuterMarginLayer,
            defaultContentLayer,
            defaultPickerLayer,
            true,
            "##CenteredSourceColor",
            false,
            1.0f,
            150.0f,
            -FLT_MIN,
            300.0f);
    passed &= Check(
        centeredSourcePicker.popupOpen &&
            std::abs(
                centeredSourcePicker.popupRect.GetCenter().y -
                    centeredSourcePicker.sourceSwatchRect.GetCenter().y) <=
                PopupPositionTolerance &&
            centeredSourcePicker.sourcePointerBaseAttached,
        "an unconstrained picker centers its body and pointer base on the "
        "controlled color square");
    QueueMouse(outside, false);
    SubmitColorPickerFrame(
        centeredSourcePickerColor,
        false,
        480.0f,
        270.0f,
        350.0f,
        true,
        true,
        defaultPickerSurface,
        defaultOuterMarginLayer,
        defaultContentLayer,
        defaultPickerLayer,
        true,
        "##CenteredSourceColor",
        false,
        1.0f,
        150.0f,
        -FLT_MIN,
        300.0f);
    float lowSourcePickerColor[4] = { 0.25f, 0.50f, 0.75f, 0.78f };
    QueueMouse(outside, false);
    const ColorPickerObservation lowSourcePicker = SubmitColorPickerFrame(
        lowSourcePickerColor,
        true,
        480.0f,
        270.0f,
        330.0f,
        true,
        false,
        defaultPickerSurface,
        defaultOuterMarginLayer,
        defaultContentLayer,
        defaultPickerLayer,
        true,
        "##LowSourceColor",
        false,
        1.0f,
        170.0f,
        -FLT_MIN,
        280.0f);
    const float lowSourcePreferredTop =
        lowSourcePicker.sourceSwatchRect.GetCenter().y -
            lowSourcePicker.popupRect.GetHeight() * 0.5f;
    const float lowSourceClampedTop =
        lowSourcePicker.maximumBottom -
            lowSourcePicker.popupRect.GetHeight();
    passed &= Check(
        lowSourcePicker.popupOpen &&
            std::abs(
                lowSourcePicker.popupRect.Max.y -
                    lowSourcePicker.maximumBottom) <=
                PopupPositionTolerance &&
            std::abs(
                (lowSourcePicker.popupRect.GetCenter().y -
                    lowSourcePicker.sourceSwatchRect.GetCenter().y) -
                    (lowSourceClampedTop - lowSourcePreferredTop)) <=
                PopupPositionTolerance &&
            lowSourcePicker.sourcePointerBaseAttached,
        "a low source clamps the centered body at the Settings bottom while "
        "moving the pointer base to the controlled square");
    QueueMouse(outside, false);
    SubmitColorPickerFrame(
        lowSourcePickerColor,
        false,
        480.0f,
        270.0f,
        330.0f,
        true,
        true,
        defaultPickerSurface,
        defaultOuterMarginLayer,
        defaultContentLayer,
        defaultPickerLayer,
        true,
        "##LowSourceColor",
        false,
        1.0f,
        170.0f,
        -FLT_MIN,
        280.0f);

    float insetSourcePickerColor[4] = { 0.25f, 0.50f, 0.75f, 0.78f };
    QueueMouse(outside, false);
    const ColorPickerObservation insetSourcePicker = SubmitColorPickerFrame(
        insetSourcePickerColor,
        true,
        480.0f,
        270.0f,
        330.0f,
        true,
        false,
        defaultPickerSurface,
        defaultOuterMarginLayer,
        defaultContentLayer,
        defaultPickerLayer,
        true,
        "##InsetSourceColor",
        false,
        1.0f,
        0.0f,
        150.0f);
    passed &= Check(
        insetSourcePicker.popupOpen &&
            insetSourcePicker.sourceSwatchRect.Max.x + 20.0f <
                insetSourcePicker.canonicalSourceRight &&
            insetSourcePicker.sourcePointerTipAttached &&
            insetSourcePicker.sourcePointerTargetsSwatch &&
            Near(
                insetSourcePicker.canonicalSourceRight,
                noMotionPicker.canonicalSourceRight),
        "an inset color control keeps the pointer tip on the same canonical "
        "Settings edge as a full-width control");
    QueueMouse(outside, false);
    SubmitColorPickerFrame(
        insetSourcePickerColor,
        false,
        480.0f,
        270.0f,
        330.0f,
        true,
        true,
        defaultPickerSurface,
        defaultOuterMarginLayer,
        defaultContentLayer,
        defaultPickerLayer,
        true,
        "##InsetSourceColor",
        false,
        1.0f,
        0.0f,
        150.0f);
    ImGui::SetUvsrUiBehavior(true, false, false);

    float scrollClosingPickerColor[4] = {
        0.25f,
        0.50f,
        0.75f,
        0.78f
    };
    const auto submitScrollClosingPicker =
        [&](bool forceOpen, bool closeActivePicker, float sourceOffset)
        {
            return SubmitColorPickerFrame(
                scrollClosingPickerColor,
                forceOpen,
                480.0f,
                270.0f,
                330.0f,
                true,
                closeActivePicker,
                defaultPickerSurface,
                defaultOuterMarginLayer,
                defaultContentLayer,
                defaultPickerLayer,
                true,
                "##ScrollClosingColor",
                false,
                1.0f,
                sourceOffset,
                -FLT_MIN,
                240.0f);
        };
    constexpr float ScrollSourceOffset = 100.0f;
    QueueMouse(outside, false);
    ColorPickerObservation settledScrollClosingPicker =
        submitScrollClosingPicker(true, false, ScrollSourceOffset);
    for (int frame = 0;
        frame < MaximumAnimationFrames &&
            !settledScrollClosingPicker.transitionInteractionReady;
        ++frame)
    {
        QueueMouse(outside, false);
        settledScrollClosingPicker =
            submitScrollClosingPicker(
                false,
                false,
                ScrollSourceOffset);
    }
    constexpr float ScrollCloseSourceOffset = 24.0f;
    QueueMouse(outside, false);
    const ColorPickerObservation movedScrollClosingPicker =
        submitScrollClosingPicker(
            false,
            true,
            ScrollSourceOffset + ScrollCloseSourceOffset);
    const float submittedScrollDelta =
        movedScrollClosingPicker.submittedSourceSwatchRect.GetCenter().y -
            settledScrollClosingPicker.submittedSourceSwatchRect.GetCenter().y;
    const float popupScrollDelta =
        movedScrollClosingPicker.popupRect.Min.y -
            settledScrollClosingPicker.popupRect.Min.y;
    const bool scrollClosingPickerAttached =
        settledScrollClosingPicker.transitionInteractionReady &&
            movedScrollClosingPicker.popupOpen &&
            !movedScrollClosingPicker.transitionTargetOpen &&
            Near(submittedScrollDelta, ScrollCloseSourceOffset) &&
            Near(popupScrollDelta, submittedScrollDelta) &&
            Near(
                movedScrollClosingPicker.sourceSwatchRect.GetCenter().y,
                movedScrollClosingPicker.submittedSourceSwatchRect.GetCenter().y) &&
            movedScrollClosingPicker.sourcePointerTipAttached &&
            movedScrollClosingPicker.sourcePointerTargetsSwatch;
    if (!scrollClosingPickerAttached)
    {
        std::cerr << "scroll-closing picker: ready/open/target="
            << settledScrollClosingPicker.transitionInteractionReady << '/'
            << movedScrollClosingPicker.popupOpen << '/'
            << movedScrollClosingPicker.transitionTargetOpen
            << " source/transition/popup delta="
            << submittedScrollDelta << '/'
            << (movedScrollClosingPicker.sourceSwatchRect.GetCenter().y -
                settledScrollClosingPicker.sourceSwatchRect.GetCenter().y)
            << '/' << popupScrollDelta
            << " pointer="
            << movedScrollClosingPicker.sourcePointerTipAttached << '/'
            << movedScrollClosingPicker.sourcePointerTargetsSwatch << '\n';
    }
    passed &= Check(
        scrollClosingPickerAttached,
        "a same-frame Settings scroll and retained close update one shared "
        "source anchor for popup placement and the attached pointer");
    for (int frame = 0;
        frame < MaximumAnimationFrames &&
            GImGui->OpenPopupStack.Size > 0;
        ++frame)
    {
        QueueMouse(outside, false);
        submitScrollClosingPicker(
            false,
            false,
            ScrollSourceOffset + ScrollCloseSourceOffset);
    }

    float verticallyBlockedPickerColor[4] = {
        0.25f,
        0.50f,
        0.75f,
        0.78f
    };
    QueueMouse(outside, false);
    const ColorPickerObservation verticallyBlockedPicker =
        SubmitColorPickerFrame(
            verticallyBlockedPickerColor,
            true,
            480.0f,
            270.0f,
            40.0f);
    passed &= Check(
        !verticallyBlockedPicker.popupOpen &&
            !verticallyBlockedPicker.activeDrawListReported,
        "a picker lane shorter than the minimum popup height fails closed "
        "instead of escaping below the menu stack");

    float narrowPickerColor[4] = { 0.25f, 0.50f, 0.75f, 0.78f };
    QueueMouse(outside, false);
    const ColorPickerObservation narrowPicker =
        SubmitColorPickerFrame(
            narrowPickerColor,
            true,
            320.0f,
            270.0f);
    passed &= Check(
        !narrowPicker.popupOpen,
        "a viewport without the minimum picker lane withholds the popup "
        "instead of moving it left over Settings content");
    io.DisplaySize = ImVec2(480.0f, 360.0f);

    ImGui::SetUvsrUiBehavior(false, true, false);
    const ImVec4 stockPickerSurface =
        ImGui::GetStyle().Colors[ImGuiCol_PopupBg];
    float oggPickerColor[4] = { 0.25f, 0.50f, 0.75f, 0.78f };
    QueueMouse(outside, false);
    SubmitColorPickerFrame(
        oggPickerColor,
        true,
        480.0f,
        270.0f,
        330.0f,
        true,
        false,
        stockPickerSurface);
    QueueMouse(outside, false);
    const ColorPickerObservation oggScopedPicker = SubmitColorPickerFrame(
        oggPickerColor,
        false,
        480.0f,
        270.0f,
        330.0f,
        true,
        false,
        stockPickerSurface);
    passed &= Check(
        oggScopedPicker.popupOpen &&
            !oggScopedPicker.transitionRegistered &&
            oggScopedPicker.transitionInteractionReady &&
            oggScopedPicker.requestedSurfaceApplied &&
            oggScopedPicker.activeDrawListMatchesPopup &&
            !oggScopedPicker.requestedContentLayerApplied &&
            !oggScopedPicker.requestedPickerLayerApplied &&
            !oggScopedPicker.finalCursorLayeredAndClipped &&
            !oggScopedPicker.hueBarRoundedAndVisible &&
            !oggScopedPicker.alphaBarRoundedAndVisible,
        "the scoped Ogg picker retains the stock PopupBg surface and upstream "
        "square-bar/cursor rendering without authored depth layers while still "
        "participating in safe placement");
    QueueMouse(outside, false);
    const ColorPickerObservation oggClosedPicker = SubmitColorPickerFrame(
        oggPickerColor,
        false,
        480.0f,
        270.0f,
        330.0f,
        true,
        true,
        stockPickerSurface);
    passed &= Check(
        !oggClosedPicker.popupOpen &&
            !oggClosedPicker.transitionRegistered,
        "the Ogg stock picker bypasses retained fade and zoom and dismisses "
        "immediately");

    ImGui::SetUvsrUiBehavior(true, false, false);
    const ImVec4 scopedOnlySurface(0.61f, 0.07f, 0.43f, 0.89f);
    float unscopedPickerColor[4] = { 0.25f, 0.50f, 0.75f, 0.78f };
    QueueMouse(outside, false);
    SubmitColorPickerFrame(
        unscopedPickerColor,
        true,
        480.0f,
        270.0f,
        40.0f,
        false,
        false,
        scopedOnlySurface);
    QueueMouse(outside, false);
    const ColorPickerObservation unscopedPicker = SubmitColorPickerFrame(
        unscopedPickerColor,
        false,
        480.0f,
        270.0f,
        40.0f,
        false,
        false,
        scopedOnlySurface);
    passed &= Check(
        unscopedPicker.popupOpen &&
            !unscopedPicker.transitionRegistered &&
            unscopedPicker.transitionInteractionReady &&
            !unscopedPicker.requestedSurfaceApplied &&
            !unscopedPicker.requestedContentLayerApplied &&
            !unscopedPicker.requestedPickerLayerApplied &&
            !unscopedPicker.activeDrawListReported &&
            !unscopedPicker.finalCursorLayeredAndClipped &&
            !unscopedPicker.hueBarRoundedAndVisible &&
            !unscopedPicker.alphaBarRoundedAndVisible,
        "unscoped ColorEdit popups ignore Settings placement, surface, draw-list, "
        "retained rounded-bar, and final-cursor extensions");
    if (GImGui->OpenPopupStack.Size > 0)
        ImGui::ClosePopupToLevel(0, false);
    comboStyle.FrameRounding = originalComboFrameRounding;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(180.0f, 100.0f), ImGuiCond_Always);
    ImGui::Begin(
        "Tree Arrow Rotation Test",
        nullptr,
        ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove);
    ImDrawList* arrowDrawList = ImGui::GetWindowDrawList();
    const int closedArrowStart = arrowDrawList->VtxBuffer.Size;
    ImGui::BeginUvsrTreeArrowCapture();
    ImGui::RenderArrow(
        arrowDrawList,
        ImVec2(42.0f, 52.0f),
        IM_COL32_WHITE,
        ImGuiDir_Right);
    ImGui::EndUvsrTreeArrowCapture(0.0f);
    const int closedArrowEnd = arrowDrawList->VtxBuffer.Size;
    const int openArrowStart = arrowDrawList->VtxBuffer.Size;
    ImGui::BeginUvsrTreeArrowCapture();
    ImGui::RenderArrow(
        arrowDrawList,
        ImVec2(102.0f, 52.0f),
        IM_COL32_WHITE,
        ImGuiDir_Right);
    ImGui::EndUvsrTreeArrowCapture(1.0f);
    const int openArrowEnd = arrowDrawList->VtxBuffer.Size;
    const auto vertexBounds =
        [arrowDrawList](int firstVertex, int lastVertex)
    {
        ImRect bounds(
            ImVec2(FLT_MAX, FLT_MAX),
            ImVec2(-FLT_MAX, -FLT_MAX));
        for (int vertexIndex = firstVertex;
            vertexIndex < lastVertex;
            ++vertexIndex)
        {
            bounds.Add(arrowDrawList->VtxBuffer[vertexIndex].pos);
        }
        return bounds;
    };
    const ImRect closedArrowBounds = vertexBounds(
        closedArrowStart,
        closedArrowEnd);
    const ImRect openArrowBounds = vertexBounds(
        openArrowStart,
        openArrowEnd);
    const ImVec2 closedArrowCenter = closedArrowBounds.GetCenter();
    const ImVec2 openArrowCenter = openArrowBounds.GetCenter();
    bool arrowVerticesRotated =
        closedArrowEnd - closedArrowStart ==
            openArrowEnd - openArrowStart &&
        closedArrowEnd > closedArrowStart;
    for (int vertexOffset = 0;
        arrowVerticesRotated &&
            closedArrowStart + vertexOffset < closedArrowEnd;
        ++vertexOffset)
    {
        const ImVec2 closedPosition =
            arrowDrawList->VtxBuffer[
                closedArrowStart + vertexOffset].pos;
        const ImVec2 openPosition =
            arrowDrawList->VtxBuffer[
                openArrowStart + vertexOffset].pos;
        const ImVec2 closedOffset(
            closedPosition.x - closedArrowCenter.x,
            closedPosition.y - closedArrowCenter.y);
        const ImVec2 openOffset(
            openPosition.x - openArrowCenter.x,
            openPosition.y - openArrowCenter.y);
        arrowVerticesRotated &=
            std::abs(openOffset.x + closedOffset.y) <= 0.05f &&
            std::abs(openOffset.y - closedOffset.x) <= 0.05f;
    }
    ImGui::End();
    ImGui::Render();
    passed &= Check(
        arrowVerticesRotated,
        "the scoped drawer arrow capture rotates the authored vertices by "
        "exactly 90 degrees between closed and open endpoints");

    ImGui::SetUvsrUiBehavior(false, true, false);
    passed &= Check(
        !ImGui::IsUvsrUiMotionEnabled() &&
            ImGui::IsUvsrStockWidgetRenderingEnabled(),
        "the public runtime policy selects motion-free stock widgets");

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(320.0f, 30.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(220.0f, 120.0f), ImGuiCond_Always);
    ImGui::Begin(
        "Stock Primitive Test",
        nullptr,
        ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove);
    ImGui::BeginDisabled();
    const ImU32 stockDisabledColor =
        ImGui::GetColorU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::SetUvsrUiBehavior(true, false, false);
    const ImU32 customDisabledColor =
        ImGui::GetColorU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::SetUvsrUiBehavior(false, true, false);
    ImGui::EndDisabled();
    const auto colorChannel =
        [](ImU32 color, int shift)
        {
            return int((color >> shift) & 0xffu);
        };
    passed &= Check(
        colorChannel(stockDisabledColor, IM_COL32_R_SHIFT) >
                colorChannel(stockDisabledColor, IM_COL32_G_SHIFT) &&
            colorChannel(customDisabledColor, IM_COL32_R_SHIFT) ==
                colorChannel(customDisabledColor, IM_COL32_G_SHIFT) &&
            colorChannel(customDisabledColor, IM_COL32_G_SHIFT) ==
                colorChannel(customDisabledColor, IM_COL32_B_SHIFT),
        "stock policy preserves upstream disabled colors while custom policy "
        "uses UVSR grayscale");

    ImDrawList* primitiveDrawList = ImGui::GetWindowDrawList();
    const int stockArrowVertexStart = primitiveDrawList->VtxBuffer.Size;
    ImGui::RenderArrow(
        primitiveDrawList,
        ImVec2(340.0f, 65.0f),
        IM_COL32_WHITE,
        ImGuiDir_Down);
    const int stockArrowVertices =
        primitiveDrawList->VtxBuffer.Size - stockArrowVertexStart;
    ImGui::SetUvsrUiBehavior(true, false, false);
    const int customArrowVertexStart = primitiveDrawList->VtxBuffer.Size;
    ImGui::RenderArrow(
        primitiveDrawList,
        ImVec2(370.0f, 65.0f),
        IM_COL32_WHITE,
        ImGuiDir_Down);
    const int customArrowVertices =
        primitiveDrawList->VtxBuffer.Size - customArrowVertexStart;
    ImGui::SetUvsrUiBehavior(false, true, false);
    passed &= Check(
        stockArrowVertices > 0 &&
            customArrowVertices > stockArrowVertices,
        "stock policy restores the upstream triangle arrow primitive");

    const int stockCheckVertexStart = primitiveDrawList->VtxBuffer.Size;
    ImGui::RenderCheckMark(
        primitiveDrawList,
        ImVec2(400.0f, 65.0f),
        IM_COL32_WHITE,
        16.0f);
    const int stockCheckVertices =
        primitiveDrawList->VtxBuffer.Size - stockCheckVertexStart;
    ImGui::SetUvsrUiBehavior(true, false, false);
    const int customCheckVertexStart = primitiveDrawList->VtxBuffer.Size;
    ImGui::RenderCheckMark(
        primitiveDrawList,
        ImVec2(430.0f, 65.0f),
        IM_COL32_WHITE,
        16.0f);
    const int customCheckVertices =
        primitiveDrawList->VtxBuffer.Size - customCheckVertexStart;
    ImGui::SetUvsrUiBehavior(false, true, false);
    passed &= Check(
        customCheckVertices > stockCheckVertices,
        "stock policy removes UVSR's rounded check-mark caps");
    ImGui::End();
    ImGui::Render();

    const ImGuiHoveredFlags originalTooltipHoverFlags =
        comboStyle.HoverFlagsForTooltipMouse;
    comboStyle.HoverFlagsForTooltipMouse = ImGuiHoveredFlags_None;
    QueueMouse(outside, false);
    const TooltipObservation tooltipOwnerProbe =
        SubmitTooltipFrame(false);
    const ImVec2 tooltipOwnerCenter = Center(tooltipOwnerProbe.ownerRect);
    QueueMouse(tooltipOwnerCenter, false);
    SubmitTooltipFrame(false);
    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation authoredTooltipOpening =
        SubmitTooltipFrame(false);
    TooltipObservation authoredTooltip = authoredTooltipOpening;
    for (int frame = 0;
        frame < MaximumAnimationFrames &&
            authoredTooltip.maximumVertexAlpha < 0.999f;
        ++frame)
    {
        QueueMouse(tooltipOwnerCenter, false);
        authoredTooltip = SubmitTooltipFrame(false);
    }
    const bool authoredTooltipContract =
        authoredTooltipOpening.submitted &&
            authoredTooltip.submitted &&
            authoredTooltip.noInputs &&
            authoredTooltip.alwaysAutoResize &&
            !authoredTooltip.hasVerticalScrollbar &&
            std::abs(
                authoredTooltip.windowSize.x -
                ImMin(
                    ImGui::GetFontSize() * 20.0f,
                    ImGui::GetMainViewport()->WorkSize.x * 0.42f)) <= 1.0f &&
            std::abs(
                authoredTooltip.windowSize.y -
                ImMin(
                    ImGui::GetFontSize() * 7.0f,
                    ImGui::GetMainViewport()->WorkSize.y * 0.25f)) <= 1.0f &&
            authoredTooltipOpening.maximumVertexAlpha <
                authoredTooltip.maximumVertexAlpha &&
            authoredTooltipOpening.visualBounds.GetWidth() <
                authoredTooltip.visualBounds.GetWidth();
    if (!authoredTooltipContract)
    {
        std::cerr << "authored tooltip: opening="
            << authoredTooltipOpening.submitted << '/'
            << authoredTooltipOpening.noInputs << '/'
            << authoredTooltipOpening.alwaysAutoResize << ':'
            << authoredTooltipOpening.maximumVertexAlpha << ':'
            << authoredTooltipOpening.visualBounds.GetWidth()
            << " settled=" << authoredTooltip.submitted << '/'
            << authoredTooltip.noInputs << '/'
            << authoredTooltip.alwaysAutoResize << '/'
            << authoredTooltip.hasVerticalScrollbar << ':'
            << authoredTooltip.maximumVertexAlpha << ':'
            << authoredTooltip.visualBounds.GetWidth() << '\n';
    }
    passed &= Check(
        authoredTooltipContract,
        "authored tooltips keep fixed uniform dimensions and no-input semantics "
        "while fade and zoom advance synchronously to the visible endpoint");

    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation shortAuthoredTooltip =
        SubmitTooltipFrame(false, true, true, "Short tooltip.");
    const std::string exactly120Tooltip(120u, 'a');
    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation exactly120AuthoredTooltip = SubmitTooltipFrame(
        false,
        true,
        true,
        exactly120Tooltip.c_str(),
        false,
        true);
    const std::string overLimitTooltip(121u, 'b');
    const std::string expectedOverLimitTooltip =
        std::string(117u, 'b') + "...";
    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation overLimitAuthoredTooltip = SubmitTooltipFrame(
        false,
        true,
        true,
        overLimitTooltip.c_str(),
        false,
        true);
    static constexpr const char* SmilingFaceUtf8 =
        "\xF0\x9F\x99\x82";
    std::string unicodeOverLimitTooltip(116u, 'u');
    for (int index = 0; index < 5; ++index)
        unicodeOverLimitTooltip += SmilingFaceUtf8;
    const std::string expectedUnicodeTooltip =
        std::string(116u, 'u') + SmilingFaceUtf8 + "...";
    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation unicodeAuthoredTooltip = SubmitTooltipFrame(
        false,
        true,
        true,
        unicodeOverLimitTooltip.c_str(),
        false,
        true);
    const std::string dynamicTooltipValue(130u, 'd');
    const std::string expectedDynamicTooltip =
        std::string("Material: ") + std::string(107u, 'd') + "...";
    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation dynamicAuthoredTooltip = SubmitTooltipFrame(
        false,
        true,
        true,
        dynamicTooltipValue.c_str(),
        false,
        true,
        true);
    const std::string stockOverLimitTooltip(121u, 's');
    const std::string expectedStockTooltip =
        std::string(117u, 's') + "...";
    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation boundedStockTooltip = SubmitTooltipFrame(
        true,
        true,
        true,
        stockOverLimitTooltip.c_str(),
        false,
        true);
    const auto codePointCount = [](const std::string& text)
    {
        return ImTextCountCharsFromUtf8(
            text.data(),
            text.data() + text.size());
    };
    const bool boundedTooltipTextContract =
        exactly120AuthoredTooltip.submitted &&
            exactly120AuthoredTooltip.renderedText == exactly120Tooltip &&
            codePointCount(exactly120AuthoredTooltip.renderedText) == 120 &&
            !exactly120AuthoredTooltip.hasVerticalScrollbar &&
        overLimitAuthoredTooltip.submitted &&
            overLimitAuthoredTooltip.renderedText ==
                expectedOverLimitTooltip &&
            codePointCount(overLimitAuthoredTooltip.renderedText) == 120 &&
            !overLimitAuthoredTooltip.hasVerticalScrollbar &&
        unicodeAuthoredTooltip.submitted &&
            unicodeAuthoredTooltip.renderedText == expectedUnicodeTooltip &&
            codePointCount(unicodeAuthoredTooltip.renderedText) == 120 &&
            !unicodeAuthoredTooltip.hasVerticalScrollbar &&
        dynamicAuthoredTooltip.submitted &&
            dynamicAuthoredTooltip.renderedText == expectedDynamicTooltip &&
            codePointCount(dynamicAuthoredTooltip.renderedText) == 120 &&
            !dynamicAuthoredTooltip.hasVerticalScrollbar &&
        boundedStockTooltip.submitted &&
            boundedStockTooltip.renderedText == expectedStockTooltip &&
            codePointCount(boundedStockTooltip.renderedText) == 120 &&
            !boundedStockTooltip.hasVerticalScrollbar;
    if (!boundedTooltipTextContract)
    {
        std::cerr << "bounded tooltips: exact="
            << exactly120AuthoredTooltip.submitted << '/'
            << (exactly120AuthoredTooltip.renderedText == exactly120Tooltip)
            << '/' << exactly120AuthoredTooltip.hasVerticalScrollbar << ':'
            << exactly120AuthoredTooltip.renderedText.size() << ':'
            << codePointCount(exactly120AuthoredTooltip.renderedText) << ':'
            << exactly120AuthoredTooltip.windowSize.y << '/'
            << exactly120AuthoredTooltip.contentSize.y
            << " over=" << overLimitAuthoredTooltip.submitted << '/'
            << (overLimitAuthoredTooltip.renderedText ==
                expectedOverLimitTooltip)
            << '/' << overLimitAuthoredTooltip.hasVerticalScrollbar << ':'
            << overLimitAuthoredTooltip.renderedText.size() << ':'
            << codePointCount(overLimitAuthoredTooltip.renderedText) << ':'
            << overLimitAuthoredTooltip.windowSize.y << '/'
            << overLimitAuthoredTooltip.contentSize.y
            << " unicode=" << unicodeAuthoredTooltip.submitted << '/'
            << (unicodeAuthoredTooltip.renderedText == expectedUnicodeTooltip)
            << '/' << unicodeAuthoredTooltip.hasVerticalScrollbar << ':'
            << unicodeAuthoredTooltip.renderedText.size() << ':'
            << codePointCount(unicodeAuthoredTooltip.renderedText) << ':'
            << unicodeAuthoredTooltip.windowSize.y << '/'
            << unicodeAuthoredTooltip.contentSize.y
            << " dynamic=" << dynamicAuthoredTooltip.submitted << '/'
            << (dynamicAuthoredTooltip.renderedText == expectedDynamicTooltip)
            << '/' << dynamicAuthoredTooltip.hasVerticalScrollbar << ':'
            << dynamicAuthoredTooltip.renderedText.size() << ':'
            << codePointCount(dynamicAuthoredTooltip.renderedText) << ':'
            << dynamicAuthoredTooltip.windowSize.y << '/'
            << dynamicAuthoredTooltip.contentSize.y
            << " stock=" << boundedStockTooltip.submitted << '/'
            << (boundedStockTooltip.renderedText == expectedStockTooltip)
            << '/' << boundedStockTooltip.hasVerticalScrollbar << ':'
            << boundedStockTooltip.renderedText.size() << ':'
            << codePointCount(boundedStockTooltip.renderedText) << ':'
            << boundedStockTooltip.windowSize.y << '/'
            << boundedStockTooltip.contentSize.y << '\n';
    }
    passed &= Check(
        boundedTooltipTextContract,
        "authored and stock tooltip paths preserve exactly 120 code points, "
        "truncate 121-plus ASCII, Unicode, and formatted dynamic text to 117 "
        "plus an ellipsis, and never add a vertical scrollbar");
    passed &= Check(
        shortAuthoredTooltip.submitted &&
            overLimitAuthoredTooltip.submitted &&
            Near(
                shortAuthoredTooltip.windowSize.x,
                overLimitAuthoredTooltip.windowSize.x) &&
            Near(
                shortAuthoredTooltip.windowSize.y,
                overLimitAuthoredTooltip.windowSize.y) &&
            !overLimitAuthoredTooltip.hasVerticalScrollbar,
        "short and over-limit authored tooltips retain one uniform outer size "
        "without a scrollbar");

    static constexpr const char* NestedTooltipText =
        "Identical tooltip text wraps across lines while its owner uses "
        "canonical or nested zero window padding.";
    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation topLevelPaddingTooltip = SubmitTooltipFrame(
        false,
        true,
        false,
        NestedTooltipText,
        false);
    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation nestedPaddingTooltip = SubmitTooltipFrame(
        false,
        true,
        false,
        NestedTooltipText,
        true);
    const ImVec2 topLevelTextOffset = Subtract(
        topLevelPaddingTooltip.textBounds.Min,
        topLevelPaddingTooltip.windowPosition);
    const ImVec2 nestedTextOffset = Subtract(
        nestedPaddingTooltip.textBounds.Min,
        nestedPaddingTooltip.windowPosition);
    const bool nestedTooltipPaddingContract =
        topLevelPaddingTooltip.submitted &&
        nestedPaddingTooltip.submitted &&
        Near(
            topLevelPaddingTooltip.windowSize.x,
            nestedPaddingTooltip.windowSize.x) &&
        Near(
            topLevelPaddingTooltip.windowSize.y,
            nestedPaddingTooltip.windowSize.y) &&
        Near(
            topLevelPaddingTooltip.windowPadding.x,
            nestedPaddingTooltip.windowPadding.x) &&
        Near(
            topLevelPaddingTooltip.windowPadding.y,
            nestedPaddingTooltip.windowPadding.y) &&
        Near(topLevelTextOffset.x, nestedTextOffset.x) &&
        Near(topLevelTextOffset.y, nestedTextOffset.y) &&
        Near(
            topLevelPaddingTooltip.textBounds.GetWidth(),
            nestedPaddingTooltip.textBounds.GetWidth()) &&
        Near(
            topLevelPaddingTooltip.textBounds.GetHeight(),
            nestedPaddingTooltip.textBounds.GetHeight()) &&
        topLevelPaddingTooltip.textBounds.GetHeight() >
            ImGui::GetFontSize() * 1.5f;
    if (!nestedTooltipPaddingContract)
    {
        std::cerr << "nested tooltip: submitted="
            << topLevelPaddingTooltip.submitted << '/'
            << nestedPaddingTooltip.submitted
            << " size=(" << topLevelPaddingTooltip.windowSize.x << ','
            << topLevelPaddingTooltip.windowSize.y << ")/("
            << nestedPaddingTooltip.windowSize.x << ','
            << nestedPaddingTooltip.windowSize.y << ") padding=("
            << topLevelPaddingTooltip.windowPadding.x << ','
            << topLevelPaddingTooltip.windowPadding.y << ")/("
            << nestedPaddingTooltip.windowPadding.x << ','
            << nestedPaddingTooltip.windowPadding.y << ") content=("
            << topLevelPaddingTooltip.contentSize.x << ','
            << topLevelPaddingTooltip.contentSize.y << ")/("
            << nestedPaddingTooltip.contentSize.x << ','
            << nestedPaddingTooltip.contentSize.y << ") text offset=("
            << topLevelTextOffset.x << ',' << topLevelTextOffset.y << ")/("
            << nestedTextOffset.x << ',' << nestedTextOffset.y << ") text size=("
            << topLevelPaddingTooltip.textBounds.GetWidth() << ','
            << topLevelPaddingTooltip.textBounds.GetHeight() << ")/("
            << nestedPaddingTooltip.textBounds.GetWidth() << ','
            << nestedPaddingTooltip.textBounds.GetHeight() << ")\n";
    }
    passed &= Check(
        nestedTooltipPaddingContract,
        "top-level and nested zero-padding tooltips retain identical authored "
        "padding, wrapping, text inset, and fixed outer dimensions");

    const ImVec2 tooltipOwnerLeft(
        tooltipOwnerProbe.ownerRect.Min.x + 8.0f,
        tooltipOwnerCenter.y);
    const ImVec2 tooltipOwnerRight(
        tooltipOwnerProbe.ownerRect.Max.x - 8.0f,
        tooltipOwnerCenter.y);
    QueueMouse(tooltipOwnerLeft, false);
    const TooltipObservation leftFollowingTooltip =
        SubmitTooltipFrame(false);
    QueueMouse(tooltipOwnerRight, false);
    const TooltipObservation rightFollowingTooltip =
        SubmitTooltipFrame(false);
    passed &= Check(
        leftFollowingTooltip.submitted &&
            rightFollowingTooltip.submitted &&
            (std::abs(
                    rightFollowingTooltip.windowPosition.x -
                        leftFollowingTooltip.windowPosition.x) > 1.0f ||
                std::abs(
                    rightFollowingTooltip.windowPosition.y -
                        leftFollowingTooltip.windowPosition.y) > 1.0f) &&
            !ImRect(
                leftFollowingTooltip.windowPosition,
                Add(
                    leftFollowingTooltip.windowPosition,
                    leftFollowingTooltip.windowSize)).Contains(
                        leftFollowingTooltip.mousePosition) &&
            !ImRect(
                rightFollowingTooltip.windowPosition,
                Add(
                    rightFollowingTooltip.windowPosition,
                    rightFollowingTooltip.windowSize)).Contains(
                        rightFollowingTooltip.mousePosition),
        "authored tooltips follow and avoid the mouse through the stock popup "
        "positioning path instead of occupying a fixed viewport slot");

    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation stockTooltip =
        SubmitTooltipFrame(true);
    passed &= Check(
        stockTooltip.submitted &&
            stockTooltip.noInputs &&
            stockTooltip.alwaysAutoResize &&
            !stockTooltip.hasVerticalScrollbar,
        "stock tooltips retain upstream no-input auto-sizing while authored "
        "tooltips own their uniform fixed dimensions");

    QueueMouse(tooltipOwnerCenter, false);
    TooltipObservation settledReversalTooltip =
        SubmitTooltipFrame(false);
    for (int frame = 0;
        frame < MaximumAnimationFrames &&
            settledReversalTooltip.maximumVertexAlpha < 0.999f;
        ++frame)
    {
        QueueMouse(tooltipOwnerCenter, false);
        settledReversalTooltip = SubmitTooltipFrame(false);
    }
    QueueMouse(outside, false);
    const TooltipObservation fadingTooltip =
        SubmitTooltipFrame(false);
    QueueMouse(outside, false);
    const TooltipObservation fadingTooltipNext =
        SubmitTooltipFrame(false);
    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation reversedTooltip =
        SubmitTooltipFrame(false);
    passed &= Check(
        settledReversalTooltip.submitted &&
            fadingTooltip.submitted &&
            fadingTooltipNext.submitted &&
            reversedTooltip.submitted &&
            fadingTooltip.maximumVertexAlpha <
                settledReversalTooltip.maximumVertexAlpha &&
            fadingTooltipNext.maximumVertexAlpha <
                fadingTooltip.maximumVertexAlpha &&
            fadingTooltipNext.visualBounds.GetWidth() <
                fadingTooltip.visualBounds.GetWidth() &&
            reversedTooltip.maximumVertexAlpha >
                fadingTooltipNext.maximumVertexAlpha &&
            reversedTooltip.visualBounds.GetWidth() >
                fadingTooltipNext.visualBounds.GetWidth(),
        "authored tooltip fade and zoom reverse continuously when the owner is "
        "re-engaged mid-flight");

    QueueMouse(tooltipOwnerCenter, false);
    const TooltipObservation motionDisabledTooltip =
        SubmitTooltipFrame(false, true, false);
    passed &= Check(
        motionDisabledTooltip.submitted &&
            motionDisabledTooltip.noInputs &&
            motionDisabledTooltip.maximumVertexAlpha >= 0.999f,
        "disabling authored motion snaps the tooltip to its fully visible "
        "stock-semantics endpoint");

    const TooltipDragObservation dragTooltip =
        SubmitTooltipDragIsolationFrame();
    passed &= Check(
        dragTooltip.retainedTooltipInactive &&
            dragTooltip.retainedOwnerReset &&
            dragTooltip.dragTooltipActive &&
            dragTooltip.dragTooltipNoInputs,
        "a live drag/drop preview replaces the retained authored tooltip and "
        "keeps upstream non-interactive drag behavior");
    comboStyle.HoverFlagsForTooltipMouse = originalTooltipHoverFlags;


    const ImVec4 collapsedSettingsBodyColor(
        0.12f,
        0.24f,
        0.36f,
        0.21f);
    constexpr float CollapsedSettingsOverrideAlpha = 0.73f;
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = collapsedSettingsBodyColor;
    style.Colors[ImGuiCol_TitleBgCollapsed] =
        ImVec4(0.48f, 0.12f, 0.08f, 0.91f);
    style.WindowRounding = AuthoredCornerRounding;
    style.ChildRounding = AuthoredCornerRounding;
    style.PopupRounding = AuthoredCornerRounding;
    style.FrameRounding = AuthoredCornerRounding;
    style.GrabRounding = AuthoredCornerRounding;
    style.ScrollbarRounding = AuthoredCornerRounding;
    style.ScrollbarSize = 12.0f;
    style.TabRounding = AuthoredCornerRounding;
    const ImVec4 authoredScrollbarGrabColor(
        0.13f,
        0.77f,
        0.31f,
        1.0f);
    style.Colors[ImGuiCol_ScrollbarGrab] =
        authoredScrollbarGrabColor;
    style.Colors[ImGuiCol_ScrollbarGrabHovered] =
        authoredScrollbarGrabColor;
    style.Colors[ImGuiCol_ScrollbarGrabActive] =
        authoredScrollbarGrabColor;
    passed &= Check(
        Near(style.WindowRounding, AuthoredCornerRounding) &&
            Near(style.ChildRounding, AuthoredCornerRounding) &&
            Near(style.PopupRounding, AuthoredCornerRounding) &&
            Near(style.FrameRounding, AuthoredCornerRounding) &&
            Near(style.GrabRounding, AuthoredCornerRounding) &&
            Near(style.ScrollbarRounding, AuthoredCornerRounding) &&
            Near(style.ScrollbarSize, 12.0f) &&
            Near(style.TabRounding, AuthoredCornerRounding),
        "authored roots, drawers, popups, controls, grabs, scrollbars, and "
        "tabs share one four-pixel radius and a twelve-pixel scrollbar channel");
    constexpr float TightSpacing = 4.0f;
    const float collapsedSettingsHeight = ImGui::GetFrameHeight();
    const float collapsedPerformanceHeight =
        collapsedSettingsHeight +
        style.WindowPadding.y * 2.0f +
        GImGui->FontSize +
        TightSpacing;
    ImGui::SetUvsrUiBehavior(true, false, false);
    bool currentRootTransitionActive = false;
    float lastPerformanceCollapseAmount = 0.0f;
    float lastPerformanceSummaryTopGap = -1.0f;
    float lastPerformanceSummaryBottomGap = -1.0f;
    ImVec4 lastPerformanceOverlayColor;
    bool lastPerformanceOverlaySubmitted = false;
    const auto resolvePerformanceCollapseAmount = [](
        float expandedHeight,
        float currentHeight,
        float collapsedHeight)
    {
        const float collapseRange = expandedHeight - collapsedHeight;
        if (collapseRange > 0.5f)
        {
            return ImClamp(
                (expandedHeight - currentHeight) / collapseRange,
                0.0f,
                1.0f);
        }
        return currentHeight <= collapsedHeight + 0.5f
            ? 1.0f
            : 0.0f;
    };
    passed &= Check(
        Near(
            resolvePerformanceCollapseAmount(
                collapsedPerformanceHeight,
                collapsedPerformanceHeight,
                collapsedPerformanceHeight),
            1.0f),
        "the initial default-collapsed Performance endpoint is opaque before "
        "ImGui has measured an expanded SizeFull range");
    const auto submitAuthoredRootFrame = [&] (
        const char* name,
        bool collapsedTarget,
        float collapsedHeight,
        float deltaTime)
    {
        io.DeltaTime = deltaTime;
        QueueMouse(outside, false);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(
            ImVec2(30.0f, 30.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(240.0f, 120.0f),
            ImGuiCond_Always);
        ImGui::SetNextUvsrWindowCollapsedHeight(
            collapsedHeight);
        ImGui::SetNextUvsrWindowCollapseTarget(collapsedTarget);
        ImGui::SetNextWindowBgAlpha(CollapsedSettingsOverrideAlpha);
        const bool expanded = ImGui::Begin(
            name,
            nullptr,
            ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove);
        if (std::strcmp(name, "Performance") == 0)
        {
            ImGuiWindow* performance = ImGui::GetCurrentWindow();
            const ImRect bodyRect(
                ImVec2(
                    performance->Pos.x + 0.5f,
                    performance->Pos.y + performance->TitleBarHeight),
                ImVec2(
                    performance->Pos.x + performance->Size.x - 0.5f,
                    performance->Pos.y + performance->Size.y - 0.5f));
            lastPerformanceCollapseAmount =
                resolvePerformanceCollapseAmount(
                    performance->SizeFull.y,
                    performance->Size.y,
                    collapsedHeight);
            lastPerformanceOverlaySubmitted =
                lastPerformanceCollapseAmount > 0.0f;
            lastPerformanceOverlayColor = collapsedSettingsBodyColor;
            lastPerformanceOverlayColor.w =
                lastPerformanceCollapseAmount;
            if (lastPerformanceOverlaySubmitted)
            {
                performance->DrawList->AddRectFilled(
                    bodyRect.Min,
                    bodyRect.Max,
                    ImGui::GetColorU32(lastPerformanceOverlayColor),
                    style.WindowRounding,
                    ImDrawFlags_RoundCornersAll);
            }
            lastPerformanceSummaryTopGap = -1.0f;
            lastPerformanceSummaryBottomGap = -1.0f;
            if (!expanded)
            {
                constexpr const char* Summary = "16.67 ms / 60 fps";
                const ImVec2 summaryTextSize = ImGui::CalcTextSize(Summary);
                const ImVec2 summaryMinimum(
                    performance->Pos.x + style.WindowPadding.x,
                    bodyRect.Min.y + ImMax(
                        0.0f,
                        (bodyRect.GetHeight() - summaryTextSize.y) * 0.5f));
                lastPerformanceSummaryTopGap =
                    summaryMinimum.y - bodyRect.Min.y;
                lastPerformanceSummaryBottomGap =
                    bodyRect.Max.y -
                    (summaryMinimum.y + summaryTextSize.y);
                performance->DrawList->AddText(
                    summaryMinimum,
                    ImGui::GetColorU32(ImGuiCol_Text),
                    Summary);
            }
        }
        currentRootTransitionActive =
            ImGui::IsCurrentUvsrWindowCollapseTransitionActive();
        ImGui::End();
        ImGui::Render();
        return ImGui::FindWindowByName(name);
    };

    ImGuiWindow* settingsWindow = submitAuthoredRootFrame(
        "Settings",
        false,
        collapsedSettingsHeight,
        FixedDeltaSeconds);
    const float expandedSettingsHeight = settingsWindow
        ? settingsWindow->Size.y
        : 0.0f;
    settingsWindow = submitAuthoredRootFrame(
        "Settings",
        true,
        collapsedSettingsHeight,
        1.0f);
    const float largeDeltaCollapseHeight = settingsWindow
        ? settingsWindow->Size.y
        : 0.0f;
    const bool settingsIntermediateReportedActive =
        currentRootTransitionActive;
    settingsWindow = submitAuthoredRootFrame(
        "Settings",
        false,
        collapsedSettingsHeight,
        FixedDeltaSeconds);
    const float reversedExpansionHeight = settingsWindow
        ? settingsWindow->Size.y
        : 0.0f;
    passed &= Check(
        largeDeltaCollapseHeight > collapsedSettingsHeight &&
            largeDeltaCollapseHeight < expandedSettingsHeight &&
            settingsIntermediateReportedActive &&
            reversedExpansionHeight > largeDeltaCollapseHeight &&
            reversedExpansionHeight <= expandedSettingsHeight,
        "authored Settings collapse clamps a large delta to an intermediate "
        "frame and reverses continuously");

    int settingsCollapseFrames = 0;
    while (settingsWindow &&
        !settingsWindow->Collapsed &&
        settingsCollapseFrames < MaximumAnimationFrames)
    {
        settingsWindow = submitAuthoredRootFrame(
            "Settings",
            true,
            collapsedSettingsHeight,
            FixedDeltaSeconds);
        ++settingsCollapseFrames;
    }

    const ImU32 explicitCollapsedBodyColor =
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(
                collapsedSettingsBodyColor.x,
                collapsedSettingsBodyColor.y,
                collapsedSettingsBodyColor.z,
                CollapsedSettingsOverrideAlpha));
    const ImU32 staleCollapsedBodyColor =
        ImGui::ColorConvertFloat4ToU32(collapsedSettingsBodyColor);
    bool foundCollapsedBodySurface = false;
    if (settingsWindow)
    {
        const float collapsedBodyMinY =
            settingsWindow->Pos.y + settingsWindow->TitleBarHeight;
        for (const ImDrawVert& vertex :
            settingsWindow->DrawList->VtxBuffer)
        {
            if (vertex.pos.y <= collapsedBodyMinY)
                continue;
            foundCollapsedBodySurface |=
                vertex.col == explicitCollapsedBodyColor ||
                vertex.col == staleCollapsedBodyColor;
        }
    }
    const bool collapsedSettingsIsTitleOnly =
        settingsWindow != nullptr &&
        settingsWindow->Collapsed &&
        Near(settingsWindow->Size.y, settingsWindow->TitleBarHeight) &&
        Near(settingsWindow->Size.y, collapsedSettingsHeight) &&
        !foundCollapsedBodySurface;
    if (!collapsedSettingsIsTitleOnly)
    {
        std::cerr
            << "collapsed Settings state: window="
            << (settingsWindow != nullptr)
            << " collapsed="
            << (settingsWindow && settingsWindow->Collapsed)
            << " height="
            << (settingsWindow ? settingsWindow->Size.y : 0.0f)
            << " title="
            << (settingsWindow ? settingsWindow->TitleBarHeight : 0.0f)
            << " body-surface="
            << foundCollapsedBodySurface
            << '\n';
    }
    passed &= Check(
        collapsedSettingsIsTitleOnly,
        "collapsed Settings ends at the exact title height without a retained "
        "Settings-body seam");

    ImGuiWindow* performanceWindow = submitAuthoredRootFrame(
        "Performance",
        false,
        collapsedPerformanceHeight,
        FixedDeltaSeconds);
    const float expandedPerformanceHeight = performanceWindow
        ? performanceWindow->Size.y
        : 0.0f;
    performanceWindow = submitAuthoredRootFrame(
        "Performance",
        true,
        collapsedPerformanceHeight,
        1.0f);
    const float intermediatePerformanceCollapseAmount =
        lastPerformanceCollapseAmount;
    const ImVec4 intermediatePerformanceOverlayColor =
        lastPerformanceOverlayColor;
    passed &= Check(
        performanceWindow != nullptr &&
            performanceWindow->Size.y > collapsedPerformanceHeight &&
            performanceWindow->Size.y < expandedPerformanceHeight &&
            currentRootTransitionActive &&
            lastPerformanceOverlaySubmitted &&
            intermediatePerformanceCollapseAmount > 0.0f &&
            intermediatePerformanceCollapseAmount < 1.0f &&
            Near(
                intermediatePerformanceOverlayColor.x,
                collapsedSettingsBodyColor.x) &&
            Near(
                intermediatePerformanceOverlayColor.y,
                collapsedSettingsBodyColor.y) &&
            Near(
                intermediatePerformanceOverlayColor.z,
                collapsedSettingsBodyColor.z),
        "Performance independently uses the generic clamped root-collapse "
        "animation while only its same-RGB body overlay gains opacity");
    ImGui::SetUvsrUiBehavior(false, false, false);
    performanceWindow = submitAuthoredRootFrame(
        "Performance",
        true,
        collapsedPerformanceHeight,
        FixedDeltaSeconds);
    bool collapsedPerformanceHasDirectSummary = false;
    if (performanceWindow)
    {
        const float summaryMinimumY =
            performanceWindow->Pos.y + performanceWindow->TitleBarHeight;
        const ImVec2 whitePixel =
            performanceWindow->DrawList->_Data->TexUvWhitePixel;
        for (const ImDrawVert& vertex :
            performanceWindow->DrawList->VtxBuffer)
        {
            collapsedPerformanceHasDirectSummary |=
                vertex.pos.y > summaryMinimumY &&
                (!Near(vertex.uv.x, whitePixel.x) ||
                    !Near(vertex.uv.y, whitePixel.y));
        }
    }
    passed &= Check(
        performanceWindow != nullptr &&
            performanceWindow->Collapsed &&
            Near(
                performanceWindow->Size.y,
                collapsedPerformanceHeight) &&
            performanceWindow->Size.y >
                performanceWindow->TitleBarHeight &&
            collapsedPerformanceHasDirectSummary &&
            lastPerformanceOverlaySubmitted &&
            Near(lastPerformanceCollapseAmount, 1.0f) &&
            Near(lastPerformanceOverlayColor.w, 1.0f) &&
            lastPerformanceSummaryTopGap >= 0.0f &&
            Near(
                lastPerformanceSummaryTopGap,
                lastPerformanceSummaryBottomGap) &&
            !currentRootTransitionActive,
        "disabling animations mid-collapse snaps an authored root to its "
        "distinct retained summary endpoint with centered text and an opaque "
        "Performance-only body overlay");
    ImGui::SetUvsrUiBehavior(true, false, false);

    const auto submitOggSettingsFrame = [&] (bool collapsed)
    {
        io.DeltaTime = 1.0f;
        QueueMouse(outside, false);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(
            ImVec2(30.0f, 30.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(240.0f, 120.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowCollapsed(collapsed, ImGuiCond_Always);
        ImGui::Begin(
            "Settings",
            nullptr,
            ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove);
        ImGui::End();
        ImGui::Render();
        return ImGui::FindWindowByName("Settings");
    };
    ImGui::SetUvsrUiBehavior(false, true, false);
    ImGuiWindow* oggSettingsWindow = submitOggSettingsFrame(false);
    const bool oggExpandedImmediately =
        oggSettingsWindow != nullptr &&
        !oggSettingsWindow->Collapsed &&
        Near(oggSettingsWindow->Size.y, 120.0f);
    oggSettingsWindow = submitOggSettingsFrame(true);
    passed &= Check(
        oggExpandedImmediately &&
            oggSettingsWindow != nullptr &&
            oggSettingsWindow->Collapsed &&
            Near(
                oggSettingsWindow->Size.y,
                oggSettingsWindow->TitleBarHeight),
        "Ogg Settings collapse remains immediate even for a very large delta");

    struct RootPanelObservation
    {
        ImVec2 position;
        ImVec2 size;
        float titleHeight = 0.0f;
        bool collapsed = false;
        bool topLevel = false;
        bool fullyRounded = false;
        bool rootRoundingUnified = false;
        bool settingsBodySubmitted = false;
        bool settingsChildRoundingUnified = false;
        bool scrollbarStartsAtGeneral = false;
        bool scrollbarUsesOnePixelInset = false;
        ImRect scrollbarFrame;
        bool performanceSelectorSubmitted = false;
        bool performanceSelectorOrdinaryInsetAndWidth = false;
        float topMargin = 0.0f;
        float bottomMargin = 0.0f;
        float leftMargin = 0.0f;
        float rightMargin = 0.0f;
    };
    const float ordinarySettingsControlWidth =
        ImGui::CalcTextSize("Bitmask Directional Visibility").x +
        style.FramePadding.x * 2.0f;
    const auto submitRootPanel = [=](
        const char* name,
        const ImVec2& position,
        const ImVec2& size,
        bool collapsed)
    {
        ImGui::SetNextWindowPos(position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowCollapsed(collapsed, ImGuiCond_Always);
        const bool expanded = ImGui::Begin(
            name,
            nullptr,
            ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove);
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        RootPanelObservation observation;
        if (expanded && std::strcmp(name, "Settings") == 0)
        {
            ImGui::BeginChild(
                "##SettingsBody",
                ImVec2(0.0f, 0.0f),
                ImGuiChildFlags_None,
                ImGuiWindowFlags_AlwaysVerticalScrollbar);
            ImGuiWindow* settingsBody = ImGui::GetCurrentWindow();
            const ImRect settingsBodyRect(
                settingsBody->Pos,
                ImVec2(
                    settingsBody->Pos.x + settingsBody->Size.x,
                    settingsBody->Pos.y + settingsBody->Size.y));
            ImGui::Button("General", ImVec2(-FLT_MIN, 0.0f));
            const ImRect generalRect(
                ImGui::GetItemRectMin(),
                ImGui::GetItemRectMax());
            for (int row = 0; row < 16; ++row)
                ImGui::Text("Scrollable Settings row %d", row);
            const ImRect scrollbarRect = ImGui::GetWindowScrollbarRect(
                settingsBody,
                ImGuiAxis_Y);
            ImGui::EndChild();
            observation.settingsBodySubmitted = true;
            observation.settingsChildRoundingUnified = Near(
                settingsBody->WindowRounding,
                AuthoredCornerRounding);
            observation.scrollbarStartsAtGeneral =
                std::abs(scrollbarRect.Min.y - generalRect.Min.y) <= 1.0f;
            observation.scrollbarFrame = scrollbarRect;
            observation.topMargin =
                settingsBodyRect.Min.y -
                (window->Pos.y + window->TitleBarHeight);
            observation.bottomMargin =
                window->Pos.y + window->Size.y -
                settingsBodyRect.Max.y;
            observation.leftMargin =
                settingsBodyRect.Min.x - window->Pos.x;
            observation.rightMargin =
                window->Pos.x + window->Size.x -
                settingsBodyRect.Max.x;
        }
        else if (expanded)
        {
            ImGui::TextUnformatted("Independent root-panel body");
            ImGui::SetNextItemWidth(ordinarySettingsControlWidth);
            if (ImGui::BeginCombo(
                    "##PerformanceEffect",
                    "Complete Renderer"))
            {
                ImGui::EndCombo();
            }
            const ImRect selectorRect(
                ImGui::GetItemRectMin(),
                ImGui::GetItemRectMax());
            observation.performanceSelectorSubmitted = true;
            observation.performanceSelectorOrdinaryInsetAndWidth =
                std::abs(
                    selectorRect.Min.x -
                    window->WorkRect.Min.x) <= 1.0f &&
                std::abs(
                    selectorRect.GetWidth() -
                    ordinarySettingsControlWidth) <= 1.0f &&
                selectorRect.Max.x < window->WorkRect.Max.x - 0.5f;
        }
        const auto hasRoundedCornerCutouts = [window](const ImRect& rect)
        {
            const int vertexCount = window->DrawList->VtxBuffer.Size;
            return
                !HasCoveredVertexNear(
                    *window->DrawList, 0, vertexCount, rect.Min) &&
                !HasCoveredVertexNear(
                    *window->DrawList,
                    0,
                    vertexCount,
                    ImVec2(rect.Max.x, rect.Min.y)) &&
                !HasCoveredVertexNear(
                    *window->DrawList,
                    0,
                    vertexCount,
                    ImVec2(rect.Min.x, rect.Max.y)) &&
                !HasCoveredVertexNear(
                    *window->DrawList, 0, vertexCount, rect.Max);
        };
        ImRect titleRect = window->TitleBarRect();
        titleRect.Min = ImVec2(
            titleRect.Min.x + 0.5f,
            titleRect.Min.y + 0.5f);
        titleRect.Max = ImVec2(
            titleRect.Max.x - 0.5f,
            titleRect.Max.y - 0.5f);
        const ImRect bodyRect(
            ImVec2(
                window->Pos.x + 0.5f,
                window->Pos.y + window->TitleBarHeight - 1.0f),
            ImVec2(
                window->Pos.x + window->Size.x - 0.5f,
                window->Pos.y + window->Size.y - 0.5f));
        const bool fullyRounded =
            hasRoundedCornerCutouts(titleRect) &&
            (window->Collapsed || hasRoundedCornerCutouts(bodyRect));
        observation.position = window->Pos;
        observation.size = window->Size;
        observation.titleHeight = window->TitleBarHeight;
        observation.collapsed = window->Collapsed;
        observation.topLevel = window->ParentWindow == nullptr;
        observation.fullyRounded = fullyRounded;
        observation.rootRoundingUnified = Near(
            window->WindowRounding,
            AuthoredCornerRounding);
        ImGui::End();
        return observation;
    };

    ImGui::SetUvsrUiBehavior(true, false, false);
    const float rootPanelGap = style.ItemSpacing.y;
    for (int performanceCollapsed = 0;
        performanceCollapsed <= 1;
        ++performanceCollapsed)
    {
        for (int settingsCollapsed = 0;
            settingsCollapsed <= 1;
            ++settingsCollapsed)
        {
            QueueMouse(outside, false);
            ImGui::NewFrame();
            const RootPanelObservation warmPerformanceRoot = submitRootPanel(
                "Performance",
                ImVec2(30.0f, 30.0f),
                ImVec2(240.0f, 92.0f),
                performanceCollapsed != 0);
            submitRootPanel(
                "Settings",
                ImVec2(
                    warmPerformanceRoot.position.x,
                    warmPerformanceRoot.position.y +
                        warmPerformanceRoot.size.y + rootPanelGap),
                ImVec2(240.0f, 112.0f),
                settingsCollapsed != 0);
            ImGui::Render();

            QueueMouse(outside, false);
            ImGui::NewFrame();
            const RootPanelObservation performanceRoot = submitRootPanel(
                "Performance",
                ImVec2(30.0f, 30.0f),
                ImVec2(240.0f, 92.0f),
                performanceCollapsed != 0);
            RootPanelObservation settingsRoot = submitRootPanel(
                "Settings",
                ImVec2(
                    performanceRoot.position.x,
                    performanceRoot.position.y +
                        performanceRoot.size.y + rootPanelGap),
                ImVec2(240.0f, 112.0f),
                settingsCollapsed != 0);
            ImGui::Render();
            if (settingsRoot.settingsBodySubmitted)
            {
                ImDrawData* drawData = ImGui::GetDrawData();
                ImRect scrollbarGrabBounds;
                bool scrollbarGrabFound = false;
                for (int listIndex = 0;
                    listIndex < drawData->CmdListsCount;
                    ++listIndex)
                {
                    if (FindExactColorBounds(
                            *drawData->CmdLists[listIndex],
                            ImGui::GetColorU32(ImGuiCol_ScrollbarGrab),
                            scrollbarGrabBounds))
                    {
                        scrollbarGrabFound = true;
                        break;
                    }
                }
                settingsRoot.scrollbarUsesOnePixelInset =
                    scrollbarGrabFound &&
                    Near(
                        settingsRoot.scrollbarFrame.GetWidth(),
                        style.ScrollbarSize) &&
                    // Exact-color vertices describe the fully covered core of
                    // the antialiased rounded grab, not its geometric edge.
                    // A one-pixel 12px-frame inset therefore presents a roughly
                    // nine-pixel core; the retired stock three-pixel inset is
                    // only about five pixels and cannot satisfy this band.
                    scrollbarGrabBounds.GetWidth() >=
                        style.ScrollbarSize - 4.0f &&
                    scrollbarGrabBounds.GetWidth() <=
                        style.ScrollbarSize - 2.0f &&
                    scrollbarGrabBounds.Min.x -
                            settingsRoot.scrollbarFrame.Min.x >=
                        0.5f &&
                    scrollbarGrabBounds.Min.x -
                            settingsRoot.scrollbarFrame.Min.x <=
                        2.0f &&
                    settingsRoot.scrollbarFrame.Max.x -
                            scrollbarGrabBounds.Max.x >=
                        0.5f &&
                    settingsRoot.scrollbarFrame.Max.x -
                            scrollbarGrabBounds.Max.x <=
                        2.0f &&
                    scrollbarGrabBounds.Min.y -
                            settingsRoot.scrollbarFrame.Min.y >=
                        0.5f &&
                    scrollbarGrabBounds.Min.y -
                            settingsRoot.scrollbarFrame.Min.y <=
                        2.0f;
            }

            const bool independentRootCollapse =
                performanceRoot.topLevel &&
                settingsRoot.topLevel &&
                performanceRoot.fullyRounded &&
                settingsRoot.fullyRounded &&
                performanceRoot.rootRoundingUnified &&
                settingsRoot.rootRoundingUnified &&
                performanceRoot.collapsed ==
                    (performanceCollapsed != 0) &&
                settingsRoot.collapsed == (settingsCollapsed != 0) &&
                Near(performanceRoot.position.x, settingsRoot.position.x) &&
                Near(performanceRoot.size.x, settingsRoot.size.x) &&
                Near(
                    settingsRoot.position.y,
                    performanceRoot.position.y +
                        performanceRoot.size.y + rootPanelGap) &&
                (!performanceRoot.collapsed ||
                    Near(
                        performanceRoot.size.y,
                        performanceRoot.titleHeight)) &&
                (!settingsRoot.collapsed ||
                    Near(settingsRoot.size.y, settingsRoot.titleHeight));
            const bool performanceSelectorLayout =
                performanceRoot.collapsed
                    ? !performanceRoot.performanceSelectorSubmitted
                    : performanceRoot.performanceSelectorSubmitted &&
                        performanceRoot
                            .performanceSelectorOrdinaryInsetAndWidth;
            const bool settingsMarginsPreserved =
                settingsRoot.collapsed ||
                (settingsRoot.settingsBodySubmitted &&
                    settingsRoot.settingsChildRoundingUnified &&
                    settingsRoot.scrollbarStartsAtGeneral &&
                    settingsRoot.scrollbarUsesOnePixelInset &&
                    Near(settingsRoot.topMargin, style.WindowPadding.y) &&
                    Near(settingsRoot.bottomMargin, style.WindowPadding.y) &&
                    Near(settingsRoot.leftMargin, style.WindowPadding.x) &&
                    Near(settingsRoot.rightMargin, style.WindowPadding.x));
            if (!independentRootCollapse ||
                !performanceSelectorLayout ||
                !settingsMarginsPreserved)
            {
                std::cerr
                    << "root collapse combination: performance="
                    << performanceCollapsed
                    << " settings=" << settingsCollapsed
                    << " observed-performance="
                    << performanceRoot.collapsed
                    << " observed-settings=" << settingsRoot.collapsed
                    << " fully-rounded="
                    << performanceRoot.fullyRounded << ','
                    << settingsRoot.fullyRounded
                    << " selector="
                    << performanceRoot.performanceSelectorSubmitted << ','
                    << performanceRoot
                        .performanceSelectorOrdinaryInsetAndWidth
                    << " settings-body="
                    << settingsRoot.settingsBodySubmitted
                    << " child-rounding="
                    << settingsRoot.settingsChildRoundingUnified
                    << " scrollbar-start="
                    << settingsRoot.scrollbarStartsAtGeneral
                    << " scrollbar-inset="
                    << settingsRoot.scrollbarUsesOnePixelInset
                    << " margins="
                    << settingsRoot.topMargin << ','
                    << settingsRoot.rightMargin << ','
                    << settingsRoot.bottomMargin << ','
                    << settingsRoot.leftMargin
                    << '\n';
            }
            passed &= Check(
                independentRootCollapse &&
                    performanceSelectorLayout &&
                    settingsMarginsPreserved,
                "fully rounded Performance and Settings retain one ordinary "
                "gap and one radius; Performance uses the ordinary inset and "
                "fixed Settings control width while Settings keeps root-owned "
                "margins and a "
                "one-pixel-inset scrollbar beginning at General");
        }
    }

    ImGui::SetUvsrUiBehavior(true, false, false);
    const float previousFrameBorderSize = style.FrameBorderSize;
    const float previousFrameRounding = style.FrameRounding;
    style.FrameBorderSize = 0.0f;
    constexpr float DepthRounding = 8.0f;
    style.FrameRounding = DepthRounding;
    QueueMouse(outside, false);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 150.0f), ImGuiCond_Always);
    ImGui::Begin(
        "Bright Custom Amp Header Depth Test",
        nullptr,
        ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar);
    ImDrawList* depthDrawList = ImGui::GetWindowDrawList();
    const ImDrawListFlags previousDepthDrawFlags = depthDrawList->Flags;
    // Force geometry-based line anti-aliasing for this probe. Runtime may use
    // the atlas-backed line path, whose coverage lives in texture alpha rather
    // than transparent vertex colors.
    depthDrawList->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
    const ImVec4 ordinaryHeaderTop(0.20f, 0.22f, 0.24f, 0.80f);
    const ImVec4 ordinaryHeaderBottom(
        ordinaryHeaderTop.x * 0.955f,
        ordinaryHeaderTop.y * 0.955f,
        ordinaryHeaderTop.z * 0.955f,
        ordinaryHeaderTop.w * 0.98f);
    const ImU32 ordinaryHeaderTopColor =
        ImGui::ColorConvertFloat4ToU32(ordinaryHeaderTop);
    const ImU32 ordinaryHeaderBottomColor =
        ImGui::ColorConvertFloat4ToU32(ordinaryHeaderBottom);
    const ImVec4 brightAmpHeaderTop(0.98f, 0.985f, 1.0f, 0.78f);
    const ImVec4 brightAmpHeaderBottom(
        brightAmpHeaderTop.x * 0.975f,
        brightAmpHeaderTop.y * 0.975f,
        brightAmpHeaderTop.z * 0.975f,
        brightAmpHeaderTop.w * 0.98f);
    const ImU32 brightAmpHeaderTopColor =
        ImGui::ColorConvertFloat4ToU32(brightAmpHeaderTop);
    const ImU32 brightAmpHeaderBottomColor =
        ImGui::ColorConvertFloat4ToU32(brightAmpHeaderBottom);
    const ImU32 transparentBrightColor =
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
    passed &= Check(
        ImGui::IsUltraBrightFrameSurface(brightAmpHeaderTopColor) &&
            !ImGui::IsUltraBrightFrameSurface(transparentBrightColor),
        "automatic bright depth requires both ultra-bright RGB and nonzero "
        "packed alpha");
    // Model a hover-darkened color below the bright threshold while the
    // structural scene-translucency policy remains active. Outline polarity
    // must still follow this surface's own luminance.
    const ImVec4 policyHeaderTop(0.62f, 0.64f, 0.66f, 0.90f);
    const ImVec4 policyHeaderBottom(
        policyHeaderTop.x * 0.975f,
        policyHeaderTop.y * 0.975f,
        policyHeaderTop.z * 0.975f,
        policyHeaderTop.w * 0.98f);
    const ImU32 policyHeaderTopColor =
        ImGui::ColorConvertFloat4ToU32(policyHeaderTop);
    const ImU32 policyHeaderBottomColor =
        ImGui::ColorConvertFloat4ToU32(policyHeaderBottom);
    const ImVec2 depthOrigin = ImGui::GetCursorScreenPos();
    const ImRect carvedFrame(
        depthOrigin,
        ImVec2(depthOrigin.x + 120.0f, depthOrigin.y + 28.0f));
    const ImRect raisedFrame(
        ImVec2(depthOrigin.x, depthOrigin.y + 48.0f),
        ImVec2(depthOrigin.x + 120.0f, depthOrigin.y + 76.0f));
    const ImRect brightFrame(
        ImVec2(depthOrigin.x, depthOrigin.y + 96.0f),
        ImVec2(depthOrigin.x + 120.0f, depthOrigin.y + 124.0f));
    const ImRect performanceSeamFrame(
        ImVec2(depthOrigin.x + 150.0f, depthOrigin.y),
        ImVec2(depthOrigin.x + 265.0f, depthOrigin.y + 48.0f));
    const ImRect settingsSeamFrame(
        ImVec2(
            depthOrigin.x + 150.0f,
            depthOrigin.y + 48.0f + style.ItemSpacing.y),
        ImVec2(
            depthOrigin.x + 265.0f,
            depthOrigin.y + 96.0f + style.ItemSpacing.y));

    const int roundedFillProbeStart = depthDrawList->VtxBuffer.Size;
    depthDrawList->AddRectFilled(
        ImVec2(-1000.0f, -1000.0f),
        ImVec2(-880.0f, -972.0f),
        IM_COL32_WHITE,
        DepthRounding);
    const int roundedFillVertexCount =
        depthDrawList->VtxBuffer.Size - roundedFillProbeStart;

    const int carvedVertexStart = depthDrawList->VtxBuffer.Size;
    ImGui::RenderGradientFrame(
        carvedFrame.Min,
        carvedFrame.Max,
        ordinaryHeaderTopColor,
        ordinaryHeaderBottomColor,
        false,
        DepthRounding,
        false);
    const int carvedVertexEnd = depthDrawList->VtxBuffer.Size;
    const int carvedFillVertexEnd =
        carvedVertexStart + roundedFillVertexCount;
    const int raisedVertexStart = carvedVertexEnd;
    ImGui::RenderGradientFrame(
        raisedFrame.Min,
        raisedFrame.Max,
        ordinaryHeaderTopColor,
        ordinaryHeaderBottomColor,
        false,
        DepthRounding,
        true);
    const int raisedVertexEnd = depthDrawList->VtxBuffer.Size;
    const int raisedFillVertexEnd =
        raisedVertexStart + roundedFillVertexCount;
    const int brightVertexStart = raisedVertexEnd;
    ImGui::RenderGradientFrame(
        brightFrame.Min,
        brightFrame.Max,
        brightAmpHeaderTopColor,
        brightAmpHeaderBottomColor,
        false,
        DepthRounding,
        true);
    const int brightVertexEnd = depthDrawList->VtxBuffer.Size;
    const int brightFillVertexEnd =
        brightVertexStart + roundedFillVertexCount;
    ImGui::SetUvsrUiBehavior(true, false, true);
    const int performanceSeamVertexStart = depthDrawList->VtxBuffer.Size;
    ImGui::RenderGradientFrame(
        performanceSeamFrame.Min,
        performanceSeamFrame.Max,
        policyHeaderTopColor,
        policyHeaderBottomColor,
        false,
        DepthRounding,
        true);
    const int performanceSeamVertexEnd = depthDrawList->VtxBuffer.Size;
    const int settingsSeamVertexStart = performanceSeamVertexEnd;
    ImGui::RenderGradientFrame(
        settingsSeamFrame.Min,
        settingsSeamFrame.Max,
        policyHeaderTopColor,
        policyHeaderBottomColor,
        false,
        DepthRounding,
        true);
    const int settingsSeamVertexEnd = depthDrawList->VtxBuffer.Size;
    const GradientFrameObservation carvedDepth = ObserveGradientFrame(
        *depthDrawList,
        carvedVertexStart,
        carvedFillVertexEnd,
        carvedVertexEnd,
        carvedFrame,
        ordinaryHeaderTopColor,
        ordinaryHeaderBottomColor);
    const GradientFrameObservation raisedDepth = ObserveGradientFrame(
        *depthDrawList,
        raisedVertexStart,
        raisedFillVertexEnd,
        raisedVertexEnd,
        raisedFrame,
        ordinaryHeaderTopColor,
        ordinaryHeaderBottomColor);
    const GradientFrameObservation brightDepth = ObserveGradientFrame(
        *depthDrawList,
        brightVertexStart,
        brightFillVertexEnd,
        brightVertexEnd,
        brightFrame,
        brightAmpHeaderTopColor,
        brightAmpHeaderBottomColor);
    const GradientFrameObservation policyDepth = ObserveGradientFrame(
        *depthDrawList,
        performanceSeamVertexStart,
        performanceSeamVertexStart + roundedFillVertexCount,
        performanceSeamVertexEnd,
        performanceSeamFrame,
        policyHeaderTopColor,
        policyHeaderBottomColor);
    const auto hasFourRoundedCorners = [&] (
        int firstVertex,
        int lastVertex,
        const ImRect& frame)
    {
        return
            !HasCoveredVertexNear(
                *depthDrawList, firstVertex, lastVertex, frame.Min) &&
            !HasCoveredVertexNear(
                *depthDrawList,
                firstVertex,
                lastVertex,
                ImVec2(frame.Max.x, frame.Min.y)) &&
            !HasCoveredVertexNear(
                *depthDrawList,
                firstVertex,
                lastVertex,
                ImVec2(frame.Min.x, frame.Max.y)) &&
            !HasCoveredVertexNear(
                *depthDrawList, firstVertex, lastVertex, frame.Max);
    };
    const bool fullyRoundedSeparatedCorners =
        Near(
            settingsSeamFrame.Min.y - performanceSeamFrame.Max.y,
            style.ItemSpacing.y) &&
        hasFourRoundedCorners(
            performanceSeamVertexStart,
            performanceSeamVertexEnd,
            performanceSeamFrame) &&
        hasFourRoundedCorners(
            settingsSeamVertexStart,
            settingsSeamVertexEnd,
            settingsSeamFrame);
    passed &= Check(
        fullyRoundedSeparatedCorners,
        "separated Performance and Settings surfaces independently retain all "
        "four rounded AA corners across the ordinary gap");

    const float brightAmpHeaderFillDelta =
        Luminance(brightAmpHeaderTopColor) -
        Luminance(brightAmpHeaderBottomColor);
    constexpr float ColorQuantizationTolerance = 2.0f / 255.0f;
    passed &= Check(
        std::abs(Alpha(brightAmpHeaderTopColor) - 0.78f) <=
                ColorQuantizationTolerance &&
            Alpha(brightAmpHeaderTopColor) < 1.0f &&
            Alpha(brightAmpHeaderBottomColor) > 0.0f &&
            Alpha(brightAmpHeaderBottomColor) < 1.0f &&
            brightAmpHeaderFillDelta > 0.0f &&
            brightAmpHeaderFillDelta <= 0.04f &&
            carvedDepth.topFillVertexCount > 0 &&
            carvedDepth.bottomFillVertexCount > 0 &&
            raisedDepth.topFillVertexCount > 0 &&
            raisedDepth.bottomFillVertexCount > 0 &&
            brightDepth.topFillVertexCount > 0 &&
            brightDepth.bottomFillVertexCount > 0 &&
            carvedDepth.topFillLuminance >
                carvedDepth.bottomFillLuminance &&
            raisedDepth.topFillLuminance >
                raisedDepth.bottomFillLuminance,
        "an ultra-bright custom Amp header retains raw alpha and subtle "
        "gradient coverage");
    const bool roundedCoveragePreserved =
        roundedFillVertexCount > 4 &&
            carvedDepth.coveredFillVertexCount > 0 &&
            raisedDepth.coveredFillVertexCount > 0 &&
            brightDepth.coveredFillVertexCount > 0 &&
            policyDepth.coveredFillVertexCount > 0 &&
            carvedDepth.transparentFillFringeVertexCount > 0 &&
            raisedDepth.transparentFillFringeVertexCount > 0 &&
            brightDepth.transparentFillFringeVertexCount > 0 &&
            policyDepth.transparentFillFringeVertexCount > 0 &&
            carvedDepth.fillCoverageBounded &&
            raisedDepth.fillCoverageBounded &&
            brightDepth.fillCoverageBounded &&
            policyDepth.fillCoverageBounded &&
            carvedDepth.transparentOutlineFringeVertexCount > 0 &&
            raisedDepth.transparentOutlineFringeVertexCount > 0 &&
            brightDepth.transparentOutlineFringeVertexCount > 0 &&
            policyDepth.transparentOutlineFringeVertexCount > 0;
    if (!roundedCoveragePreserved)
    {
        std::cerr
            << "rounded coverage: probe=" << roundedFillVertexCount
            << " carved-fill=" << carvedDepth.coveredFillVertexCount
            << '/' << carvedDepth.transparentFillFringeVertexCount
            << " raised-fill=" << raisedDepth.coveredFillVertexCount
            << '/' << raisedDepth.transparentFillFringeVertexCount
            << " carved-outline-fringe="
            << carvedDepth.transparentOutlineFringeVertexCount
            << " raised-outline-fringe="
            << raisedDepth.transparentOutlineFringeVertexCount
            << " bright-outline-fringe="
            << brightDepth.transparentOutlineFringeVertexCount
            << " bounded=" << carvedDepth.fillCoverageBounded
            << '/' << raisedDepth.fillCoverageBounded
            << " flags=" << int(depthDrawList->Flags)
            << '\n';
    }
    passed &= Check(
        roundedCoveragePreserved,
        "rounded ultra-bright custom Amp fills and depth outlines preserve "
        "transparent AA fringe "
        "coverage");
    const bool depthRolesCorrect =
        carvedDepth.topOutlineVertexCount > 0 &&
            carvedDepth.bottomOutlineVertexCount > 0 &&
            raisedDepth.topOutlineVertexCount > 0 &&
            raisedDepth.bottomOutlineVertexCount > 0 &&
            brightDepth.topOutlineVertexCount > 0 &&
            brightDepth.bottomOutlineVertexCount > 0 &&
            policyDepth.topOutlineVertexCount > 0 &&
            policyDepth.bottomOutlineVertexCount > 0 &&
            carvedDepth.topOutlineLuminance <
                carvedDepth.bottomOutlineLuminance &&
            raisedDepth.topOutlineLuminance >
                raisedDepth.bottomOutlineLuminance &&
            brightDepth.topOutlineLuminance < 0.03f &&
            brightDepth.topOutlineLuminance <
                brightDepth.bottomOutlineLuminance &&
            brightDepth.bottomOutlineLuminance > 0.07f &&
            brightDepth.bottomOutlineLuminance < 0.20f &&
            policyDepth.topOutlineLuminance >
                policyDepth.bottomOutlineLuminance &&
            std::abs(carvedDepth.maximumBottomOutlineAlpha - 0.070f) <=
                ColorQuantizationTolerance &&
            std::abs(raisedDepth.maximumTopOutlineAlpha - 0.12f) <=
                ColorQuantizationTolerance &&
            std::abs(raisedDepth.maximumBottomOutlineAlpha - 0.10f) <=
                ColorQuantizationTolerance &&
            std::abs(brightDepth.maximumTopOutlineAlpha - 0.24f) <=
                ColorQuantizationTolerance &&
            std::abs(brightDepth.maximumBottomOutlineAlpha - 0.32f) <=
                ColorQuantizationTolerance &&
            std::abs(policyDepth.maximumTopOutlineAlpha - 0.12f) <=
                ColorQuantizationTolerance &&
            std::abs(policyDepth.maximumBottomOutlineAlpha - 0.10f) <=
                ColorQuantizationTolerance &&
            carvedDepth.maximumOutlinePremultipliedLuminance > 0.0f &&
            raisedDepth.maximumOutlinePremultipliedLuminance > 0.0f &&
            brightDepth.maximumOutlinePremultipliedLuminance > 0.0f &&
            policyDepth.maximumOutlinePremultipliedLuminance > 0.0f &&
            carvedDepth.maximumOutlinePremultipliedLuminance <= 0.13f &&
            raisedDepth.maximumOutlinePremultipliedLuminance <= 0.13f &&
            brightDepth.maximumOutlinePremultipliedLuminance <= 0.07f &&
            policyDepth.maximumOutlinePremultipliedLuminance <= 0.13f;
    if (!depthRolesCorrect)
    {
        std::cerr
            << "depth roles: carved-lum="
            << carvedDepth.topOutlineLuminance << '/'
            << carvedDepth.bottomOutlineLuminance
            << " raised-lum="
            << raisedDepth.topOutlineLuminance << '/'
            << raisedDepth.bottomOutlineLuminance
            << " bright-lum="
            << brightDepth.topOutlineLuminance << '/'
            << brightDepth.bottomOutlineLuminance
            << " policy-lum="
            << policyDepth.topOutlineLuminance << '/'
            << policyDepth.bottomOutlineLuminance
            << " max-alpha="
            << carvedDepth.maximumBottomOutlineAlpha << '/'
            << raisedDepth.maximumTopOutlineAlpha << '/'
            << raisedDepth.maximumBottomOutlineAlpha << '/'
            << brightDepth.maximumTopOutlineAlpha << '/'
            << brightDepth.maximumBottomOutlineAlpha
            << '/' << policyDepth.maximumTopOutlineAlpha
            << '/' << policyDepth.maximumBottomOutlineAlpha
            << " premul="
            << carvedDepth.maximumOutlinePremultipliedLuminance << '/'
            << raisedDepth.maximumOutlinePremultipliedLuminance << '/'
            << brightDepth.maximumOutlinePremultipliedLuminance << '/'
            << policyDepth.maximumOutlinePremultipliedLuminance
            << '\n';
    }
    passed &= Check(
        depthRolesCorrect,
        "ordinary raised and carved outlines use inverse polarity, while "
        "only an actually ultra-bright surface automatically uses a transparent "
        "black-to-dark-gray ramp");
    const auto outlineStaysInside = [](const ImRect& outline, const ImRect& frame)
    {
        constexpr float Epsilon = 1e-3f;
        return outline.Min.x >= frame.Min.x - Epsilon &&
            outline.Min.y >= frame.Min.y - Epsilon &&
            outline.Max.x <= frame.Max.x + Epsilon &&
            outline.Max.y <= frame.Max.y + Epsilon;
    };
    const auto depthExtentMatches = [](float left, float right)
    {
        return std::abs(left - right) <= 1e-3f;
    };
    const bool matchingDepthSilhouettes =
        outlineStaysInside(carvedDepth.outlineBounds, carvedFrame) &&
            outlineStaysInside(raisedDepth.outlineBounds, raisedFrame) &&
            outlineStaysInside(brightDepth.outlineBounds, brightFrame) &&
            depthExtentMatches(
                carvedDepth.outlineBounds.GetWidth(),
                raisedDepth.outlineBounds.GetWidth()) &&
            depthExtentMatches(
                carvedDepth.outlineBounds.GetHeight(),
                raisedDepth.outlineBounds.GetHeight());
    if (!matchingDepthSilhouettes)
    {
        std::cerr
            << "depth bounds: carved=("
            << carvedDepth.outlineBounds.Min.x << ','
            << carvedDepth.outlineBounds.Min.y << " -> "
            << carvedDepth.outlineBounds.Max.x << ','
            << carvedDepth.outlineBounds.Max.y << ") frame=("
            << carvedFrame.Min.x << ',' << carvedFrame.Min.y << " -> "
            << carvedFrame.Max.x << ',' << carvedFrame.Max.y
            << ") raised=("
            << raisedDepth.outlineBounds.Min.x << ','
            << raisedDepth.outlineBounds.Min.y << " -> "
            << raisedDepth.outlineBounds.Max.x << ','
            << raisedDepth.outlineBounds.Max.y << ") frame=("
            << raisedFrame.Min.x << ',' << raisedFrame.Min.y << " -> "
            << raisedFrame.Max.x << ',' << raisedFrame.Max.y << ")\n";
    }
    passed &= Check(
        matchingDepthSilhouettes,
        "raised and carved depth roles preserve one identical outer silhouette");
    depthDrawList->Flags = previousDepthDrawFlags;
    ImGui::End();
    ImGui::Render();
    style.FrameBorderSize = previousFrameBorderSize;
    style.FrameRounding = previousFrameRounding;

    ImGui::SetUvsrUiBehavior(true, false, true);
    QueueMouse(outside, false);
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(300.0f, 220.0f), ImGuiCond_Always);
    ImGui::Begin(
        "Slider Geometry Test",
        nullptr,
        ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove);
    const float rowHeight = ImGui::GetFrameHeight();
    const ImRect sliderBounds(
        ImVec2(20.0f, 20.0f),
        ImVec2(220.0f, 20.0f + rowHeight - 4.0f));
    const int minimumValue = 0;
    const int smallMaximum = 2;
    const int largeMaximum = 1000;
    int smallValue = 1;
    int largeValue = 500;
    ImRect smallGrab;
    ImRect largeGrab;
    ImGui::SliderBehavior(
        sliderBounds,
        ImGui::GetID("##SmallRangeSliderGeometry"),
        ImGuiDataType_S32,
        &smallValue,
        &minimumValue,
        &smallMaximum,
        "%d",
        ImGuiSliderFlags_None,
        &smallGrab);
    ImGui::SliderBehavior(
        sliderBounds,
        ImGui::GetID("##LargeRangeSliderGeometry"),
        ImGuiDataType_S32,
        &largeValue,
        &minimumValue,
        &largeMaximum,
        "%d",
        ImGuiSliderFlags_None,
        &largeGrab);
    const float expectedThumbSize = rowHeight - 8.0f;
    passed &= Check(
        Near(smallGrab.GetWidth(), expectedThumbSize) &&
            Near(smallGrab.GetHeight(), expectedThumbSize) &&
            Near(largeGrab.GetWidth(), expectedThumbSize) &&
            Near(largeGrab.GetHeight(), expectedThumbSize),
        "custom slider thumbs match the square toggle-thumb size regardless "
        "of integer range");
    passed &= Check(
        Near(smallGrab.Min.y - sliderBounds.Min.y, 2.0f) &&
            Near(sliderBounds.Max.y - smallGrab.Max.y, 2.0f),
        "custom slider thumbs keep the toggle-matched two-pixel edge inset");

    ImDrawList* controlDrawList = ImGui::GetWindowDrawList();
    const auto verticesContainColor =
        [controlDrawList](int begin, ImU32 color)
        {
            for (int index = begin;
                index < controlDrawList->VtxBuffer.Size;
                ++index)
            {
                if (controlDrawList->VtxBuffer[index].col == color)
                    return true;
            }
            return false;
        };
    const ImVec4 dynamicNegative(0.85f, 0.07f, 0.33f, 0.19f);
    const ImVec4 dynamicPositive(0.05f, 0.91f, 0.42f, 0.23f);
    ImGui::SetUvsrUiAccentColors(dynamicNegative, dynamicPositive);
    const ImU32 dynamicToggleOff =
        ImGui::ColorConvertFloat4ToU32(dynamicNegative);
    const ImU32 dynamicToggleOn =
        ImGui::ColorConvertFloat4ToU32(dynamicPositive);
    ImGui::SetCursorPos(ImVec2(20.0f, 80.0f));
    ImGui::SetUvsrUiBehavior(true, false, false);
    bool toggleOff = false;
    const int toggleOffVertexStart = controlDrawList->VtxBuffer.Size;
    ImGui::Checkbox("##AmpToggleOffColor", &toggleOff);
    ImGui::SetCursorPos(ImVec2(20.0f, 120.0f));
    ImGui::SetUvsrUiBehavior(true, false, true);
    bool toggleOn = true;
    const int toggleOnVertexStart = controlDrawList->VtxBuffer.Size;
    ImGui::Checkbox("##AmpToggleOnColor", &toggleOn);
    const bool exactToggleColors =
        verticesContainColor(toggleOffVertexStart, dynamicToggleOff) &&
        verticesContainColor(toggleOnVertexStart, dynamicToggleOn);
    if (!exactToggleColors)
    {
        std::cerr
            << "toggle color state: alpha=" << ImGui::GetStyle().Alpha
            << " off-start=" << toggleOffVertexStart
            << " on-start=" << toggleOnVertexStart
            << " vertex-count=" << controlDrawList->VtxBuffer.Size
            << '\n';
    }
    passed &= Check(
        exactToggleColors,
        "authored Amp toggle thumbs use the supplied dynamic negative and "
        "positive RGBA endpoints without discarding authored alpha");
    ImGui::End();
    ImGui::Render();

    struct ToggleMotionObservation
    {
        ImRect rect;
        float animatedPosition = 0.0f;
        int pendingValue = -1;
        bool changed = false;
    };
    bool masterSwitchToggle = false;
    const auto submitToggleMotionFrame = [&] (
        const ImVec2& mousePosition,
        bool mouseDown)
    {
        io.DeltaTime = FixedDeltaSeconds;
        QueueMouse(mousePosition, mouseDown);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(
            ImVec2(20.0f, 200.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(180.0f, 70.0f),
            ImGuiCond_Always);
        ImGui::Begin(
            "Toggle Motion Master Test",
            nullptr,
            ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoBackground);
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiID toggleId = window->GetID("##MotionMasterToggle");
        ToggleMotionObservation observation;
        observation.changed = ImGui::Checkbox(
            "##MotionMasterToggle",
            &masterSwitchToggle);
        observation.rect = ImRect(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax());
        observation.animatedPosition = window->StateStorage.GetFloat(
            ImHashStr("##ToggleAnimation", 0, toggleId),
            -1.0f);
        observation.pendingValue = window->StateStorage.GetInt(
            ImHashStr("##TogglePendingValue", 0, toggleId),
            -1);
        ImGui::End();
        ImGui::Render();
        return observation;
    };

    ImGui::SetUvsrUiBehavior(true, false, false);
    const ToggleMotionObservation toggleRest =
        submitToggleMotionFrame(outside, false);
    const ImVec2 toggleCenter = Center(toggleRest.rect);
    submitToggleMotionFrame(toggleCenter, false);
    submitToggleMotionFrame(toggleCenter, true);
    const ToggleMotionObservation toggleInFlight =
        submitToggleMotionFrame(toggleCenter, false);
    const bool toggleInFlightCorrect =
        !masterSwitchToggle &&
        !toggleInFlight.changed &&
        toggleInFlight.pendingValue == 1 &&
        toggleInFlight.animatedPosition > 0.0f &&
        toggleInFlight.animatedPosition < 1.0f;
    if (!toggleInFlightCorrect)
    {
        std::cerr
            << "toggle in-flight: value=" << masterSwitchToggle
            << " changed=" << toggleInFlight.changed
            << " pending=" << toggleInFlight.pendingValue
            << " position=" << toggleInFlight.animatedPosition
            << " rect=(" << toggleRest.rect.Min.x << ','
            << toggleRest.rect.Min.y << " -> "
            << toggleRest.rect.Max.x << ','
            << toggleRest.rect.Max.y << ") mouse=("
            << toggleCenter.x << ',' << toggleCenter.y << ")\n";
    }
    passed &= Check(
        toggleInFlightCorrect,
        "an authored toggle retains its pending value during an enabled "
        "mid-transition frame");
    ImGui::SetUvsrUiBehavior(false, false, false);
    const ToggleMotionObservation toggleSnapped =
        submitToggleMotionFrame(outside, false);
    const bool toggleSnappedCorrect =
        masterSwitchToggle &&
        toggleSnapped.changed &&
        toggleSnapped.pendingValue == -1 &&
        Near(toggleSnapped.animatedPosition, 1.0f);
    if (!toggleSnappedCorrect)
    {
        std::cerr
            << "toggle snapped: value=" << masterSwitchToggle
            << " changed=" << toggleSnapped.changed
            << " pending=" << toggleSnapped.pendingValue
            << " position=" << toggleSnapped.animatedPosition
            << '\n';
    }
    passed &= Check(
        toggleSnappedCorrect,
        "disabling animations mid-toggle snaps to the target and commits the "
        "pending value on that same authored-widget submission");

    const float previousDisabledAlpha = style.DisabledAlpha;
    const float previousStyleAlpha = style.Alpha;
    const float previousGrabRounding = style.GrabRounding;
    const ImVec4 previousSliderGrab = style.Colors[ImGuiCol_SliderGrab];
    const ImVec4 previousSliderGrabActive =
        style.Colors[ImGuiCol_SliderGrabActive];
    const ImVec4 previousSliderFrame = style.Colors[ImGuiCol_FrameBg];
    const ImVec4 previousSliderFrameHovered =
        style.Colors[ImGuiCol_FrameBgHovered];
    const ImVec4 previousSliderFrameActive =
        style.Colors[ImGuiCol_FrameBgActive];
    style.DisabledAlpha = 0.35f;
    style.Alpha = 1.0f;
    style.GrabRounding = 4.0f;
    const ImVec4 primaryAccent(0.18f, 0.68f, 0.96f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = primaryAccent;
    style.Colors[ImGuiCol_SliderGrabActive] =
        primaryAccent;
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.06f, 0.07f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] =
        style.Colors[ImGuiCol_FrameBg];
    style.Colors[ImGuiCol_FrameBgActive] =
        style.Colors[ImGuiCol_FrameBg];

    QueueMouse(outside, false);
    int enabledDepthValue = 3;
    const SliderFrameObservation enabledSlider = SubmitGatedSliderFrame(
        "Enabled Slider Depth Test",
        enabledDepthValue,
        true);
    int disabledDepthValue = 3;
    const SliderFrameObservation disabledSlider = SubmitGatedSliderFrame(
        "Disabled Slider Depth Test",
        disabledDepthValue,
        false);
    const bool sliderDepthVisible =
        enabledSlider.topGrabVertexCount > 0 &&
        enabledSlider.bottomGrabVertexCount > 0 &&
        enabledSlider.topGrabLuminance >
            enabledSlider.bottomGrabLuminance &&
        enabledSlider.outlineVertexCount > 0 &&
        disabledSlider.topGrabVertexCount > 0 &&
        disabledSlider.bottomGrabVertexCount > 0 &&
        disabledSlider.topGrabLuminance >
            disabledSlider.bottomGrabLuminance &&
        disabledSlider.outlineVertexCount > 0;
    if (!sliderDepthVisible)
    {
        std::cerr
            << "slider depth: enabled-count="
            << enabledSlider.topGrabVertexCount << '/'
            << enabledSlider.bottomGrabVertexCount
            << " lum=" << enabledSlider.topGrabLuminance << '/'
            << enabledSlider.bottomGrabLuminance
            << " outline=" << enabledSlider.outlineVertexCount
            << " alpha=" << enabledSlider.maximumGrabAlpha
            << " disabled-count="
            << disabledSlider.topGrabVertexCount << '/'
            << disabledSlider.bottomGrabVertexCount
            << " lum=" << disabledSlider.topGrabLuminance << '/'
            << disabledSlider.bottomGrabLuminance
            << " outline=" << disabledSlider.outlineVertexCount
            << " alpha=" << disabledSlider.maximumGrabAlpha
            << '\n';
    }
    passed &= Check(
        sliderDepthVisible,
        "enabled and disabled Primary Accent authored slider grabs retain a "
        "visible raised gradient and depth outline");
    passed &= Check(
        enabledSlider.maximumDrawerTrackAlpha > 0.70f &&
            disabledSlider.maximumDrawerTrackAlpha == 0.0f &&
            disabledSlider.maximumDisabledTrackAlpha > 0.0f &&
            disabledSlider.maximumDisabledTrackAlpha <
                enabledSlider.maximumDrawerTrackAlpha * 0.60f,
        "authored slider tracks consume the supplied drawer-frame color while "
        "gated controls retain grayscale semantic disabled opacity");
    passed &= Check(
        enabledSlider.maximumGrabAlpha > 0.90f &&
            disabledSlider.maximumGrabAlpha > 0.0f &&
            disabledSlider.maximumGrabAlpha <
                enabledSlider.maximumGrabAlpha * 0.60f &&
            enabledDepthValue == 3 &&
            disabledDepthValue == 3,
        "disabled gated sliders visibly dim while preserving their stored value");

    constexpr int MaximumGatedSampleValue = 6;
    int storedGatedSampleValue = MaximumGatedSampleValue;
    const SliderFrameObservation ungatedSlider = SubmitGatedSliderFrame(
        "Gated Slider Motion Test",
        storedGatedSampleValue,
        true);
    bool monotonicGateTravel = ungatedSlider.hasGrabAnimationState;
    bool monotonicPresentationFade = true;
    bool sawIntermediateGateTravel = false;
    float previousGrabCenter = ungatedSlider.animatedCenterX;
    float previousGrabAlpha = ungatedSlider.maximumGrabAlpha;
    float disabledPresentationLinearAmount = 0.0f;
    SliderFrameObservation gatedSlider;
    for (int frame = 0; frame < MaximumAnimationFrames; ++frame)
    {
        disabledPresentationLinearAmount = ImMin(
            1.0f,
            disabledPresentationLinearAmount +
                FixedDeltaSeconds / 0.280f);
        const float disabledPresentationAmount =
            disabledPresentationLinearAmount *
            disabledPresentationLinearAmount *
            (3.0f - 2.0f * disabledPresentationLinearAmount);
        gatedSlider = SubmitGatedSliderFrame(
            "Gated Slider Motion Test",
            storedGatedSampleValue,
            false,
            disabledPresentationAmount);
        monotonicGateTravel &=
            gatedSlider.hasGrabAnimationState &&
            gatedSlider.animatedCenterX <= previousGrabCenter + 1e-3f;
        monotonicPresentationFade &=
            gatedSlider.maximumGrabAlpha <= previousGrabAlpha +
                2.0f / 255.0f;
        sawIntermediateGateTravel |=
            gatedSlider.animatedCenterX <
                ungatedSlider.animatedCenterX - 0.25f &&
            gatedSlider.animatedCenterX >
                ungatedSlider.animatedCenterX - 150.0f;
        previousGrabCenter = gatedSlider.animatedCenterX;
        previousGrabAlpha = gatedSlider.maximumGrabAlpha;
    }
    int minimumSampleValue = 0;
    const SliderFrameObservation minimumSlider = SubmitGatedSliderFrame(
        "Minimum Slider Endpoint Test",
        minimumSampleValue,
        true);
    passed &= Check(
        monotonicGateTravel &&
            monotonicPresentationFade &&
            sawIntermediateGateTravel &&
            Near(
                gatedSlider.animatedCenterX,
                minimumSlider.animatedCenterX) &&
            storedGatedSampleValue == MaximumGatedSampleValue,
        "a gate-driven effective-value change moves the authored grab "
        "monotonically to one sample while its 280 ms disabled presentation "
        "fades monotonically without mutating stored configuration");
    passed &= Check(
        gatedSlider.maximumGrabAlpha > 0.0f &&
            gatedSlider.maximumGrabAlpha <
                ungatedSlider.maximumGrabAlpha * 0.60f,
        "the gated authored slider reaches the dim endpoint after the scoped "
        "disabled-presentation transition");

    int laneValue = 50;
    QueueMouse(outside, false);
    const SliderLaneLayoutObservation laneBaseline =
        SubmitSliderLaneLayoutFrame(laneValue);
    const bool laneFacesMatchToggleContract =
        laneBaseline.valueLaneUsable &&
        laneBaseline.trackFaceRounded &&
        laneBaseline.valueFaceRounded &&
        Near(
            laneBaseline.valueRect.GetWidth(),
            laneBaseline.toggleRect.GetWidth() * 2.0f) &&
        Near(laneBaseline.sliderRect.GetWidth(), 220.0f) &&
        Near(
            laneBaseline.valueRect.Min.x - laneBaseline.trackRect.Max.x,
            2.0f) &&
        laneBaseline.trackRect.Max.x < laneBaseline.valueRect.Min.x;
    const ImVec2 laneOuterStrip(
        laneBaseline.valueRect.GetCenter().x,
        laneBaseline.sliderRect.Min.y + 0.5f);
    const int laneValueBeforeOuterStrip = laneValue;
    QueueMouse(laneOuterStrip, true);
    const SliderLaneLayoutObservation laneOuterDown =
        SubmitSliderLaneLayoutFrame(laneValue);
    const bool laneOuterStripWasInert =
        !laneOuterDown.tempInputActive &&
        laneValue == laneValueBeforeOuterStrip;
    QueueMouse(laneOuterStrip, false);
    SubmitSliderLaneLayoutFrame(laneValue);

    const ImVec2 laneGapCenter(
        (laneBaseline.trackRect.Max.x + laneBaseline.valueRect.Min.x) * 0.5f,
        laneBaseline.sliderVisualRect.GetCenter().y);
    const int laneValueBeforeGap = laneValue;
    QueueMouse(laneGapCenter, true);
    const SliderLaneLayoutObservation laneGapDown =
        SubmitSliderLaneLayoutFrame(laneValue);
    const bool laneGapWasInert =
        !laneGapDown.tempInputActive &&
        laneValue == laneValueBeforeGap;
    QueueMouse(laneGapCenter, false);
    SubmitSliderLaneLayoutFrame(laneValue);

    const ImVec2 laneTrackCenter(
        ImLerp(
            laneBaseline.trackRect.Min.x,
            laneBaseline.trackRect.Max.x,
            0.75f),
        laneBaseline.trackRect.GetCenter().y);
    const int laneValueBeforeTrack = laneValue;
    QueueMouse(laneTrackCenter, true);
    const SliderLaneLayoutObservation laneTrackDown =
        SubmitSliderLaneLayoutFrame(laneValue);
    const bool laneTrackChangedValue =
        !laneTrackDown.tempInputActive &&
        laneValue != laneValueBeforeTrack;
    QueueMouse(laneTrackCenter, false);
    SubmitSliderLaneLayoutFrame(laneValue);

    int narrowLaneValue = 50;
    QueueMouse(outside, false);
    const SliderLaneLayoutObservation narrowLaneBaseline =
        SubmitSliderLaneLayoutFrame(
            narrowLaneValue,
            46.0f,
            "Narrow Slider Value Lane Layout Test",
            ImVec2(18.0f, 190.0f));
    const ImVec2 narrowTrackPoint(
        narrowLaneBaseline.trackRect.Max.x - 3.0f,
        narrowLaneBaseline.trackRect.GetCenter().y);
    const int narrowLaneValueBeforeTrack = narrowLaneValue;
    QueueMouse(narrowTrackPoint, false);
    SubmitSliderLaneLayoutFrame(
        narrowLaneValue,
        46.0f,
        "Narrow Slider Value Lane Layout Test",
        ImVec2(18.0f, 190.0f));
    QueueMouse(narrowTrackPoint, true);
    const SliderLaneLayoutObservation narrowLaneDown =
        SubmitSliderLaneLayoutFrame(
            narrowLaneValue,
            46.0f,
            "Narrow Slider Value Lane Layout Test",
            ImVec2(18.0f, 190.0f));
    QueueMouse(narrowTrackPoint, true);
    const SliderLaneLayoutObservation narrowLaneHeld =
        SubmitSliderLaneLayoutFrame(
            narrowLaneValue,
            46.0f,
            "Narrow Slider Value Lane Layout Test",
            ImVec2(18.0f, 190.0f));
    const bool narrowLaneFallbackChangedValue =
        !narrowLaneBaseline.valueLaneUsable &&
        !narrowLaneDown.tempInputActive &&
        !narrowLaneHeld.tempInputActive &&
        narrowLaneValue != narrowLaneValueBeforeTrack;
    QueueMouse(narrowTrackPoint, false);
    SubmitSliderLaneLayoutFrame(
        narrowLaneValue,
        46.0f,
        "Narrow Slider Value Lane Layout Test",
        ImVec2(18.0f, 190.0f));

    QueueMouse(outside, false);
    const SliderLaneLayoutObservation laneInputBaseline =
        SubmitSliderLaneLayoutFrame(laneValue);
    const ImVec2 laneCenter = laneInputBaseline.valueRect.GetCenter();
    QueueMouse(laneCenter, true);
    const SliderLaneLayoutObservation laneInput =
        SubmitSliderLaneLayoutFrame(laneValue);
    const bool laneLayoutPreserved =
        laneInput.tempInputActive &&
        Near(
            laneInput.sameLineRect.Min.x,
            laneInputBaseline.sameLineRect.Min.x) &&
        Near(
            laneInput.sameLineRect.Min.y,
            laneInputBaseline.sameLineRect.Min.y) &&
        Near(
            laneInput.followingRowRect.Min.x,
            laneInputBaseline.followingRowRect.Min.x) &&
        Near(
            laneInput.followingRowRect.Min.y,
            laneInputBaseline.followingRowRect.Min.y);
    const bool sliderLaneContractCorrect =
        laneFacesMatchToggleContract &&
        !laneBaseline.tempInputActive &&
        laneOuterStripWasInert &&
        laneGapWasInert &&
        laneTrackChangedValue &&
        narrowLaneFallbackChangedValue &&
        laneLayoutPreserved;
    if (!sliderLaneContractCorrect)
    {
        std::cerr
            << "slider lane: usable=" << laneBaseline.valueLaneUsable
            << " rounded=" << laneBaseline.trackFaceRounded << '/'
            << laneBaseline.valueFaceRounded
            << " widths=" << laneBaseline.valueRect.GetWidth() << '/'
            << laneBaseline.toggleRect.GetWidth()
            << " gap="
            << laneBaseline.valueRect.Min.x - laneBaseline.trackRect.Max.x
            << " outer=" << laneOuterStripWasInert
            << " bubble-gap=" << laneGapWasInert
            << " track=" << laneTrackChangedValue
            << " fallback=" << narrowLaneFallbackChangedValue
            << " input/layout=" << laneInput.tempInputActive << '/'
            << laneLayoutPreserved
            << " hover=" << narrowLaneDown.sliderHovered << '/'
            << laneInput.sliderHovered
            << " active=" << narrowLaneHeld.activeId << '/'
            << narrowLaneDown.sliderId << '/'
            << laneInput.activeId << '/' << laneInput.sliderId
            << " values=" << laneValue << '/' << narrowLaneValue
            << '\n';
    }
    passed &= Check(
        sliderLaneContractCorrect,
        "the authored slider keeps its fixed total width while using a "
        "double-toggle-width rounded value bubble separated from its rounded "
        "track by an inert two-pixel gap, preserves track-only drag and narrow "
        "fallback behavior, and keeps direct value input from disturbing "
        "SameLine or following-row layout");

    style.DisabledAlpha = previousDisabledAlpha;
    style.Alpha = previousStyleAlpha;
    style.GrabRounding = previousGrabRounding;
    style.Colors[ImGuiCol_SliderGrab] = previousSliderGrab;
    style.Colors[ImGuiCol_SliderGrabActive] = previousSliderGrabActive;
    style.Colors[ImGuiCol_FrameBg] = previousSliderFrame;
    style.Colors[ImGuiCol_FrameBgHovered] = previousSliderFrameHovered;
    style.Colors[ImGuiCol_FrameBgActive] = previousSliderFrameActive;

    ImGui::SetUvsrUiBehavior(true, false, false);
    ImGui::DestroyContext();
    if (!passed)
        return 1;

    std::cout << "ImGui UI and dropdown roll contract tests passed\n";
    return 0;
}
