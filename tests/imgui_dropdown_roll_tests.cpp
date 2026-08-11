#include "imgui.h"
#include "imgui_internal.h"

#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <iostream>

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
        ImVec2 windowSize;
        ImVec2 contentSize;
    };

    struct ColorPickerObservation
    {
        bool popupOpen = false;
        ImGuiID popupId = 0;
        float contentRight = 0.0f;
        float maximumBottom = 0.0f;
        float colorAlpha = 0.0f;
        bool requestedSurfaceApplied = false;
        bool activeDrawListReported = false;
        bool activeDrawListMatchesPopup = false;
        bool finalCursorLayeredAndClipped = false;
        bool hueBarRoundedAndVisible = false;
        bool alphaBarRoundedAndVisible = false;
        bool hueMarkerPairVisible = false;
        bool alphaMarkerPairVisible = false;
        ImRect popupRect;
        ImRect viewportRect;
        ImRect hueBarRect;
        ImRect alphaBarRect;
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

    bool Near(float left, float right)
    {
        return std::abs(left - right) <= 1e-5f;
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

    bool HasBlackAndWhiteMarkerVertices(
        const ImDrawList& drawList,
        const ImRect& region)
    {
        constexpr ImU32 RgbMask = ~IM_COL32_A_MASK;
        constexpr ImU32 BlackRgb = IM_COL32(0, 0, 0, 0);
        constexpr ImU32 WhiteRgb = IM_COL32(255, 255, 255, 0);
        bool hasBlack = false;
        bool hasWhite = false;
        for (const ImDrawVert& vertex : drawList.VtxBuffer)
        {
            if (!region.Contains(vertex.pos) || Alpha(vertex.col) < 0.5f)
                continue;
            const ImU32 rgb = vertex.col & RgbMask;
            hasBlack |= rgb == BlackRgb;
            hasWhite |= rgb == WhiteRgb;
        }
        return hasBlack && hasWhite;
    }

    bool HasVerticalBarMarkerPair(
        const ImDrawList& drawList,
        const ImRect& bar,
        float markerY)
    {
        const float markerHalfHeight =
            float(int(bar.GetWidth() * 0.20f)) + 2.0f;
        const ImRect leftMarkerRegion(
            ImVec2(bar.Min.x - 3.5f, markerY - markerHalfHeight),
            ImVec2(bar.Min.x - 0.1f, markerY + markerHalfHeight));
        const ImRect rightMarkerRegion(
            ImVec2(bar.Max.x + 0.1f, markerY - markerHalfHeight),
            ImVec2(bar.Max.x + 3.5f, markerY + markerHalfHeight));
        return
            HasBlackAndWhiteMarkerVertices(drawList, leftMarkerRegion) &&
            HasBlackAndWhiteMarkerVertices(drawList, rightMarkerRegion);
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
        bool stockWidgetRendering)
    {
        static constexpr const char* TooltipText =
            "UVSR tooltip policy regression text stays on one upstream "
            "line but wraps across several lines in the Amp presentation";

        ImGui::SetUvsrUiBehavior(
            !stockWidgetRendering,
            stockWidgetRendering,
            false);
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
        ImGui::SetTooltip("%s", TooltipText);

        TooltipObservation observation;
        ImGuiWindow* tooltipWindow = GImGui->TooltipPreviousWindow;
        if (tooltipWindow != nullptr)
        {
            observation.submitted = true;
            observation.windowSize = tooltipWindow->SizeFull;
            observation.contentSize = ImVec2(
                tooltipWindow->DC.CursorMaxPos.x -
                    tooltipWindow->DC.CursorStartPos.x,
                tooltipWindow->DC.CursorMaxPos.y -
                    tooltipWindow->DC.CursorStartPos.y);
        }

        ImGui::End();
        ImGui::Render();
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
        float (&color)[4],
        bool forceOpen,
        float displayWidth = 480.0f,
        float ownerWidth = 270.0f,
        float maximumBottom = 330.0f,
        bool scoped = true,
        bool closeActivePicker = false,
        ImVec4 popupBackground =
            ImVec4(0.17f, 0.29f, 0.41f, 0.93f))
    {
        ColorPickerObservation observation;
        observation.maximumBottom = maximumBottom;

        ImGui::GetIO().DisplaySize = ImVec2(displayWidth, 360.0f);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(
            ImVec2(20.0f, 24.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(ownerWidth, 104.0f),
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
                popupBackground);
        }
        ImGui::PushID("##InterfacePrimaryColor");
        observation.popupId = ImGui::GetID("picker");
        if (forceOpen)
        {
            ImGui::OpenPopupEx(
                observation.popupId,
                ImGuiPopupFlags_None);
        }
        ImGui::PopID();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGuiColorEditFlags pickerFlags =
            ImGuiColorEditFlags_Float |
            ImGuiColorEditFlags_DisplayRGB |
            ImGuiColorEditFlags_AlphaBar |
            ImGuiColorEditFlags_AlphaPreviewHalf;
        if (!ImGui::IsUvsrStockWidgetRenderingEnabled())
            pickerFlags |= ImGuiColorEditFlags_PickerHueWheel;
        ImGui::ColorEdit4(
            "##InterfacePrimaryColor",
            color,
            pickerFlags);

        char popupName[20];
        ImFormatString(
            popupName,
            IM_ARRAYSIZE(popupName),
            "##Popup_%08x",
            observation.popupId);
        ImGuiWindow* popupWindow = ImGui::FindWindowByName(popupName);
        observation.popupOpen = ImGui::IsPopupOpen(
            observation.popupId,
            ImGuiPopupFlags_None);
        if (popupWindow && popupWindow->Active)
        {
            observation.popupRect = ImRect(
                popupWindow->Pos,
                ImVec2(
                    popupWindow->Pos.x + popupWindow->Size.x,
                    popupWindow->Pos.y + popupWindow->Size.y));
            const ImGuiStyle& style = ImGui::GetStyle();
            const float squareSize = ImGui::GetFrameHeight();
            const float pickerWidth = ImMax(
                1.0f,
                popupWindow->Size.x - style.WindowPadding.x * 2.0f);
            const float saturationValueSize = ImMax(
                squareSize,
                pickerWidth -
                    2.0f *
                        (squareSize + style.ItemInnerSpacing.x));
            const ImVec2 pickerPosition =
                popupWindow->DC.CursorStartPos;
            const float hueBarX =
                pickerPosition.x + saturationValueSize +
                    style.ItemInnerSpacing.x;
            const float alphaBarX =
                pickerPosition.x + saturationValueSize +
                squareSize +
                    2.0f * style.ItemInnerSpacing.x;
            observation.hueBarRect = ImRect(
                ImVec2(hueBarX, pickerPosition.y),
                ImVec2(
                    hueBarX + squareSize,
                    pickerPosition.y + saturationValueSize));
            observation.alphaBarRect = ImRect(
                ImVec2(alphaBarX, pickerPosition.y),
                ImVec2(
                    alphaBarX + squareSize,
                    pickerPosition.y + saturationValueSize));
            observation.hueBarRoundedAndVisible =
                HasRoundedBarCoverage(
                    *popupWindow->DrawList,
                    observation.hueBarRect);
            observation.alphaBarRoundedAndVisible =
                HasRoundedBarCoverage(
                    *popupWindow->DrawList,
                    observation.alphaBarRect);
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
            const float hueMarkerY = IM_ROUND(
                pickerPosition.y + hue * saturationValueSize);
            const float alphaMarkerY = IM_ROUND(
                pickerPosition.y +
                    (1.0f - ImSaturate(color[3])) *
                        saturationValueSize);
            observation.hueMarkerPairVisible =
                HasVerticalBarMarkerPair(
                    *popupWindow->DrawList,
                    observation.hueBarRect,
                    hueMarkerY);
            observation.alphaMarkerPairVisible =
                HasVerticalBarMarkerPair(
                    *popupWindow->DrawList,
                    observation.alphaBarRect,
                    alphaMarkerY);
            const ImU32 expectedSurface = ImGui::GetColorU32(
                popupBackground);
            for (const ImDrawVert& vertex : popupWindow->DrawList->VtxBuffer)
            {
                observation.requestedSurfaceApplied |=
                    vertex.col == expectedSurface;
            }
            const ImRect popupOuterClip = popupWindow->OuterRectClipped;
            ImVec4 finalCommandClip;
            bool foundFinalCommand = false;
            for (int commandIndex = popupWindow->DrawList->CmdBuffer.Size - 1;
                commandIndex >= 0;
                --commandIndex)
            {
                const ImDrawCmd& command =
                    popupWindow->DrawList->CmdBuffer[commandIndex];
                if (command.ElemCount == 0)
                    continue;
                finalCommandClip = command.ClipRect;
                foundFinalCommand = true;
                break;
            }
            bool finalOpaqueVertexIsWhite = false;
            for (int vertexIndex = popupWindow->DrawList->VtxBuffer.Size - 1;
                vertexIndex >= 0;
                --vertexIndex)
            {
                const ImDrawVert& vertex =
                    popupWindow->DrawList->VtxBuffer[vertexIndex];
                if (Alpha(vertex.col) == 0)
                    continue;
                finalOpaqueVertexIsWhite = vertex.col == IM_COL32_WHITE;
                break;
            }
            observation.finalCursorLayeredAndClipped =
                foundFinalCommand &&
                finalOpaqueVertexIsWhite &&
                Near(finalCommandClip.x, popupOuterClip.Min.x) &&
                Near(finalCommandClip.y, popupOuterClip.Min.y) &&
                Near(finalCommandClip.z, popupOuterClip.Max.x) &&
                Near(finalCommandClip.w, popupOuterClip.Max.y);
        }
        ImDrawList* activePickerDrawList =
            ImGui::GetUvsrActiveColorPickerPopupDrawList();
        observation.activeDrawListReported =
            activePickerDrawList != nullptr;
        observation.activeDrawListMatchesPopup =
            popupWindow && popupWindow->Active &&
            activePickerDrawList == popupWindow->DrawList;
        observation.colorAlpha = color[3];
        if (scoped)
            ImGui::PopUvsrColorPickerPopupContentRight();
        ImGui::End();
        ImGui::Render();
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
    SubmitColorPickerFrame(pickerColor, true);
    QueueMouse(outside, false);
    const ColorPickerObservation positionedPicker =
        SubmitColorPickerFrame(pickerColor, false);
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
    const bool pickerAboveMenuStackBottom =
        positionedPicker.popupRect.Max.y <=
            positionedPicker.maximumBottom + PopupPositionTolerance;
    passed &= Check(
        pickerOutsideSettingsContent &&
            pickerInsideViewport &&
            pickerAboveMenuStackBottom,
        "the scoped color-picker popup begins at Settings content-right and "
        "remains clamped within both the viewport and menu-stack bottom");
    passed &= Check(
        positionedPicker.requestedSurfaceApplied &&
            positionedPicker.activeDrawListReported &&
            positionedPicker.activeDrawListMatchesPopup,
        "the scoped picker alone receives its caller-owned surface and reports "
        "its current-frame draw list for the shared appearance transform");
    passed &= Check(
        positionedPicker.finalCursorLayeredAndClipped,
        "authored picker cursors occupy the final popup draw layer while "
        "remaining clipped to the popup outer rectangle");
    passed &= Check(
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
                positionedPicker.popupRect.Max.x + PopupPositionTolerance,
        "the viewport-fitted authored wheel retains separate hue and alpha bars");
    passed &= Check(
        positionedPicker.hueBarRoundedAndVisible &&
            positionedPicker.alphaBarRoundedAndVisible &&
            positionedPicker.hueMarkerPairVisible &&
            positionedPicker.alphaMarkerPairVisible,
        "the authored wheel renders both retained bars with rounded coverage "
        "and paired black-and-white markers on both sides");

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
    const ColorPickerObservation targetedClosedPicker =
        SubmitColorPickerFrame(
            pickerColor,
            false,
            480.0f,
            270.0f,
            330.0f,
            true,
            true);
    passed &= Check(
        !targetedClosedPicker.popupOpen &&
            !targetedClosedPicker.activeDrawListReported,
        "the targeted picker close API dismisses the registered popup and "
        "invalidates its current-frame draw-list report");

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
            oggScopedPicker.requestedSurfaceApplied &&
            oggScopedPicker.activeDrawListMatchesPopup &&
            !oggScopedPicker.finalCursorLayeredAndClipped &&
            !oggScopedPicker.hueBarRoundedAndVisible &&
            !oggScopedPicker.alphaBarRoundedAndVisible,
        "the scoped Ogg picker retains the stock PopupBg surface and upstream "
        "square-bar/cursor rendering while still participating in safe placement");
    QueueMouse(outside, false);
    SubmitColorPickerFrame(
        oggPickerColor,
        false,
        480.0f,
        270.0f,
        330.0f,
        true,
        true,
        stockPickerSurface);

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
            !unscopedPicker.requestedSurfaceApplied &&
            !unscopedPicker.activeDrawListReported &&
            !unscopedPicker.finalCursorLayeredAndClipped &&
            !unscopedPicker.hueBarRoundedAndVisible &&
            !unscopedPicker.alphaBarRoundedAndVisible,
        "unscoped ColorEdit popups ignore Settings placement, surface, draw-list, "
        "retained rounded-bar, and final-cursor extensions");
    if (GImGui->OpenPopupStack.Size > 0)
        ImGui::ClosePopupToLevel(0, false);
    comboStyle.FrameRounding = originalComboFrameRounding;

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

    const TooltipObservation ampTooltip =
        SubmitTooltipFrame(false);
    SubmitTooltipFrame(true);
    const TooltipObservation stockTooltip =
        SubmitTooltipFrame(true);
    passed &= Check(
        ampTooltip.submitted &&
            stockTooltip.submitted &&
            stockTooltip.windowSize.x >
                ampTooltip.windowSize.x + GImGui->FontSize &&
            stockTooltip.contentSize.x >
                ampTooltip.contentSize.x + GImGui->FontSize &&
            ampTooltip.contentSize.y >
                stockTooltip.contentSize.y + GImGui->FontSize,
        "stock policy restores upstream auto-sized unwrapped tooltips while "
        "custom policy retains UVSR's fixed wrapped tooltip");


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
