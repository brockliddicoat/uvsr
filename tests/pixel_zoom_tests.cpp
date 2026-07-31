#include "pixel_zoom.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
    bool Check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    }
}

int main(int argc, char** argv)
{
    using namespace uvsr;

    bool passed = true;
    PixelZoomMode mode = PixelZoomMode::Off;
    mode = AdvancePixelZoomMode(mode);
    passed &= Check(mode == PixelZoomMode::Zoom2x,
        "Off advances to 2x");
    mode = AdvancePixelZoomMode(mode);
    passed &= Check(mode == PixelZoomMode::Zoom3x,
        "2x advances to 3x");
    mode = AdvancePixelZoomMode(mode);
    passed &= Check(mode == PixelZoomMode::Zoom4x,
        "3x advances to 4x");
    mode = AdvancePixelZoomMode(mode);
    passed &= Check(mode == PixelZoomMode::Zoom5x,
        "4x advances to 5x");
    mode = AdvancePixelZoomMode(mode);
    passed &= Check(mode == PixelZoomMode::Off,
        "5x advances to Off");
    mode = AdvancePixelZoomMode(mode);
    passed &= Check(mode == PixelZoomMode::Zoom2x,
        "Off re-enables at 2x");

    for (PixelZoomMode zoomMode : {
            PixelZoomMode::Zoom2x,
            PixelZoomMode::Zoom3x,
            PixelZoomMode::Zoom4x,
            PixelZoomMode::Zoom5x })
    {
        const PixelZoomLayout layout = ResolvePixelZoomLayout(
            1902u,
            1069u,
            10u,
            zoomMode);
        const uint32_t factor = GetPixelZoomFactor(zoomMode);
        passed &= Check(
            layout.panelMinX == 1359u &&
                layout.panelMinY == 10u &&
                layout.panelWidth == 533u &&
                layout.panelHeight == 300u,
            "zoom panel uses the top-right Settings margin and 28-percent width");
        passed &= Check(
            std::abs(
                static_cast<int64_t>(layout.panelWidth) * 100 -
                static_cast<int64_t>(layout.sourceWidth) *
                    PixelZoomPanelWidthPercent) <= 50 &&
                std::abs(
                    static_cast<int64_t>(layout.panelWidth) *
                        layout.sourceHeight -
                    static_cast<int64_t>(layout.panelHeight) *
                        layout.sourceWidth) <=
                    static_cast<int64_t>(layout.sourceWidth / 2u),
            "zoom width rounds to 28 percent and height preserves source aspect");

        const PixelZoomLayout openingLayout =
            ResolveAnimatedPixelZoomLayout(layout, 0.f);
        const PixelZoomLayout midpointLayout =
            ResolveAnimatedPixelZoomLayout(layout, 0.5f);
        const PixelZoomLayout completeLayout =
            ResolveAnimatedPixelZoomLayout(layout, 1.f);
        passed &= Check(
            openingLayout.panelWidth == 458u &&
                openingLayout.panelHeight == 258u &&
                openingLayout.panelMinX == 1396u &&
                openingLayout.panelMinY == 31u,
            "zoom begins from the centered 86-percent window scale");
        passed &= Check(
            midpointLayout.panelWidth > openingLayout.panelWidth &&
                midpointLayout.panelHeight > openingLayout.panelHeight &&
                midpointLayout.panelWidth < layout.panelWidth &&
                midpointLayout.panelHeight < layout.panelHeight,
            "zoom grows smoothly between its opening and final bounds");
        passed &= Check(
            completeLayout.panelMinX == layout.panelMinX &&
                completeLayout.panelMinY == layout.panelMinY &&
                completeLayout.panelWidth == layout.panelWidth &&
                completeLayout.panelHeight == layout.panelHeight,
            "zoom reaches the exact final 28-percent aspect-matched bounds");

        const PixelZoomLayout levelTransitionMidpoint =
            ResolveAnimatedPixelZoomLayout(
                layout,
                1.f,
                ResolvePixelZoomLevelTransitionScale(0.5f));
        passed &= Check(
            levelTransitionMidpoint.panelMinX ==
                    openingLayout.panelMinX &&
                levelTransitionMidpoint.panelMinY ==
                    openingLayout.panelMinY &&
                levelTransitionMidpoint.panelWidth ==
                    openingLayout.panelWidth &&
                levelTransitionMidpoint.panelHeight ==
                    openingLayout.panelHeight,
            "level changes reuse the accepted centered zoom scale at midpoint");

        const uint32_t groupOriginX =
            (layout.panelWidth - factor) / 2u;
        const uint32_t groupOriginY =
            (layout.panelHeight - factor) / 2u;
        uint32_t centerPixelCount = 0u;
        for (uint32_t y = 0u; y < layout.panelHeight; ++y)
        {
            for (uint32_t x = 0u; x < layout.panelWidth; ++x)
            {
                const PixelZoomSourcePixel sourcePixel =
                    ResolvePixelZoomSourcePixel(layout, x, y);
                if (sourcePixel.x == layout.sourceWidth / 2u &&
                    sourcePixel.y == layout.sourceHeight / 2u)
                {
                    ++centerPixelCount;
                }
            }
        }
        passed &= Check(
            centerPixelCount == factor * factor,
            "the center source pixel expands to an exact NxN group");

        const PixelZoomSourcePixel centerGroupFirst =
            ResolvePixelZoomSourcePixel(
                layout,
                groupOriginX,
                groupOriginY);
        const PixelZoomSourcePixel centerGroupLast =
            ResolvePixelZoomSourcePixel(
                layout,
                groupOriginX + factor - 1u,
                groupOriginY + factor - 1u);
        const PixelZoomSourcePixel nextGroup =
            ResolvePixelZoomSourcePixel(
                layout,
                groupOriginX + factor,
                groupOriginY);
        passed &= Check(
            centerGroupFirst.x == layout.sourceWidth / 2u &&
                centerGroupFirst.y == layout.sourceHeight / 2u &&
                centerGroupLast.x == centerGroupFirst.x &&
                centerGroupLast.y == centerGroupFirst.y,
            "every destination pixel in the center group loads one texel");
        passed &= Check(
            nextGroup.x == centerGroupFirst.x + 1u &&
                nextGroup.y == centerGroupFirst.y,
            "the next exact group advances by one source texel");
    }

    passed &= Check(
        !IsPixelZoomEnabled(PixelZoomMode::Off) &&
            IsPixelZoomEnabled(PixelZoomMode::Zoom2x),
        "only magnifying modes enable zoom work");

    const MaterialInspectorLayout inspectorAbove =
        ResolveMaterialInspectorLayout(
            1902u,
            1069u,
            10u,
            0.f,
            false,
            0.f,
            1.f);
    const MaterialInspectorLayout inspectorMidpoint =
        ResolveMaterialInspectorLayout(
            1902u,
            1069u,
            10u,
            0.5f,
            false,
            0.f,
            1.f);
    const MaterialInspectorLayout inspectorPreShifted =
        ResolveMaterialInspectorLayout(
            1902u,
            1069u,
            10u,
            1.f,
            false,
            0.f,
            1.f);
    const MaterialInspectorLayout inspectorBelow =
        ResolveMaterialInspectorLayout(
            1902u,
            1069u,
            10u,
            1.f,
            true,
            1.f,
            1.f);
    passed &= Check(
        inspectorAbove.panelMinX == 1359.f &&
            inspectorAbove.panelMinY == 10.f &&
            inspectorAbove.panelWidth == 533.f &&
            inspectorAbove.panelMaxHeight == 1049.f,
        "the zoom-free material inspector occupies the zoom panel anchor and width");
    passed &= Check(
        inspectorMidpoint.panelMinX == 1359.f &&
            inspectorMidpoint.panelMinY == 154.5f &&
            inspectorMidpoint.panelWidth == 533.f &&
            inspectorMidpoint.panelMaxHeight == 904.5f,
        "the hidden pre-shift moves vertically at the resting zoom width");
    passed &= Check(
        inspectorPreShifted.panelMinX == 1359.f &&
            inspectorPreShifted.panelMinY == 299.f &&
            inspectorPreShifted.panelWidth == 533.f &&
            inspectorPreShifted.panelMaxHeight == 760.f,
        "the completed pre-shift matches zoom's first visible bottom edge");
    passed &= Check(
        inspectorBelow.panelMinX == 1359.f &&
            inspectorBelow.panelMinY == 320.f &&
            inspectorBelow.panelWidth == 533.f &&
            inspectorBelow.panelMaxHeight == 739.f,
        "the material inspector rests one shared margin below visible zoom");

    const MaterialInspectorLayout inspectorDuringZoom =
        ResolveMaterialInspectorLayout(
            1902u,
            1069u,
            10u,
            1.f,
            true,
            0.5f,
            1.f);
    const PixelZoomLayout restingZoomLayout =
        ResolvePixelZoomLayout(
            1902u,
            1069u,
            10u,
            PixelZoomMode::Zoom2x);
    const PixelZoomLayout zoomDuringPresentation =
        ResolveAnimatedPixelZoomLayout(
            restingZoomLayout,
            0.5f,
            1.f);
    passed &= Check(
        inspectorDuringZoom.panelMinX ==
                float(restingZoomLayout.panelMinX) &&
            inspectorDuringZoom.panelWidth ==
                float(restingZoomLayout.panelWidth) &&
            inspectorDuringZoom.panelMinY ==
                float(
                    zoomDuringPresentation.panelMinY +
                    zoomDuringPresentation.panelHeight +
                    10u),
        "the inspector keeps resting width while following zoom's presented bottom");

    const MaterialInspectorLayout inspectorDuringLevelPulse =
        ResolveMaterialInspectorLayout(
            1902u,
            1069u,
            10u,
            1.f,
            true,
            1.f,
            PixelZoomMinimumWindowScale);
    passed &= Check(
        inspectorDuringLevelPulse.panelMinX == 1359.f &&
            inspectorDuringLevelPulse.panelMinY == 299.f &&
            inspectorDuringLevelPulse.panelWidth == 533.f &&
            inspectorDuringLevelPulse.panelMinX +
                inspectorDuringLevelPulse.panelWidth == 1892.f,
        "zoom level pulses move only the inspector's vertical rail");

    const float materialOpeningAppearance =
        AdvanceMaterialInspectorAppearance(
            0.f,
            true,
            true,
            PixelZoomFadeDurationSeconds * 0.25f);
    const float materialClosingAppearance =
        AdvanceMaterialInspectorAppearance(
            1.f,
            false,
            true,
            PixelZoomFadeDurationSeconds * 0.25f);
    passed &= Check(
        materialOpeningAppearance > 0.f &&
            materialOpeningAppearance < 1.f &&
            materialClosingAppearance > 0.f &&
            materialClosingAppearance < 1.f,
        "Amp retains interior material appearance values while opening and closing");
    passed &= Check(
        AdvanceMaterialInspectorAppearance(
            0.f,
            true,
            false,
            0.f) == 1.f &&
            AdvanceMaterialInspectorAppearance(
                1.f,
                false,
                false,
                0.f) == 0.f,
        "OG applies material appearance endpoints immediately");
    passed &= Check(
        IsMaterialInspectorPresentationActive(
            false,
            materialClosingAppearance) &&
            !IsMaterialInspectorPresentationActive(false, 0.f),
        "a closing material inspector remains presented until its fade completes");
    passed &= Check(
        IsMaterialInspectorAppearanceIdle(true, 1.f) &&
            !IsMaterialInspectorAppearanceIdle(true, 0.f) &&
            IsMaterialInspectorAppearanceIdle(false, 0.f) &&
            !IsMaterialInspectorAppearanceIdle(false, 1.f),
        "material appearance idleness follows the requested endpoint");
    float completedMaterialOpening = 0.f;
    float completedMaterialClosing = 1.f;
    for (int step = 0; step < 12; ++step)
    {
        completedMaterialOpening =
            AdvanceMaterialInspectorAppearance(
                completedMaterialOpening,
                true,
                true,
                1.f / 60.f);
        completedMaterialClosing =
            AdvanceMaterialInspectorAppearance(
                completedMaterialClosing,
                false,
                true,
                1.f / 60.f);
    }
    passed &= Check(
        completedMaterialOpening == 1.f &&
            completedMaterialClosing == 0.f,
        "material appearance reaches exact endpoints within its accepted duration");

    passed &= Check(
        AdvanceMaterialInspectorZoomPlacement(
            0.f,
            false,
            true,
            false,
            true,
            0.f) == 1.f,
        "a hidden inspector primes its placement to the requested zoom endpoint");
    const float inspectorOpeningPlacement =
        AdvanceMaterialInspectorZoomPlacement(
            0.f,
            true,
            true,
            false,
            true,
            PixelZoomFadeDurationSeconds * 0.25f);
    passed &= Check(
        inspectorOpeningPlacement > 0.f &&
            inspectorOpeningPlacement < 1.f,
        "Amp moves a visible inspector smoothly before zoom opens");
    passed &= Check(
        AdvanceMaterialInspectorZoomPlacement(
            1.f,
            true,
            false,
            true,
            true,
            PixelZoomFadeDurationSeconds) == 1.f,
        "Amp holds the inspector below zoom until its closing fade completes");
    passed &= Check(
        AdvanceMaterialInspectorZoomPlacement(
            1.f,
            true,
            false,
            false,
            false,
            PixelZoomFadeDurationSeconds) == 0.f,
        "OG applies the zoom-free inspector position immediately");
    passed &= Check(
        ShouldDelayPixelZoomForMaterialInspector(
            true,
            true,
            true,
            inspectorOpeningPlacement) &&
            !ShouldDelayPixelZoomForMaterialInspector(
                true,
                true,
                false,
                0.f) &&
            !ShouldDelayPixelZoomForMaterialInspector(
                false,
                true,
                true,
                0.f),
        "only a moving visible Amp inspector delays zoom opening");

    const CenterMaterialPick evenCenter =
        ResolveCenterMaterialPick(1920u, 1080u);
    const CenterMaterialPick oddCenter =
        ResolveCenterMaterialPick(1901u, 1069u);
    const CenterMaterialPick missingCenter =
        ResolveCenterMaterialPick(0u, 1080u);
    passed &= Check(
        evenCenter.valid &&
            evenCenter.x == 960u &&
            evenCenter.y == 540u &&
            oddCenter.valid &&
            oddCenter.x == 950u &&
            oddCenter.y == 534u,
        "center material picks use the same integer center texel convention as zoom");
    passed &= Check(
        !missingCenter.valid,
        "zero-sized material targets reject center picking");

    passed &= Check(
        !IsPixelZoomCompositionIdle(
            PixelZoomMode::Off,
            PixelZoomMode::Zoom5x,
            PixelZoomMode::Zoom5x,
            1.f,
            1.f) &&
        !IsPixelZoomCompositionIdle(
            PixelZoomMode::Off,
            PixelZoomMode::Zoom5x,
            PixelZoomMode::Zoom5x,
            0.f,
            1.f),
        "requesting Off remains active until the rendered zoom is removed");
    passed &= Check(
        IsPixelZoomCompositionIdle(
            PixelZoomMode::Off,
            PixelZoomMode::Off,
            PixelZoomMode::Off,
            0.f,
            1.f),
        "zoom Off is idle only at its nonexistent endpoint");
    passed &= Check(
        !IsPixelZoomCompositionIdle(
            PixelZoomMode::Zoom3x,
            PixelZoomMode::Zoom2x,
            PixelZoomMode::Zoom3x,
            1.f,
            1.f) &&
        !IsPixelZoomCompositionIdle(
            PixelZoomMode::Zoom3x,
            PixelZoomMode::Zoom3x,
            PixelZoomMode::Zoom3x,
            0.75f,
            1.f) &&
        !IsPixelZoomCompositionIdle(
            PixelZoomMode::Zoom3x,
            PixelZoomMode::Zoom3x,
            PixelZoomMode::Zoom3x,
            1.f,
            0.75f),
        "enabled zoom stays active through mode, fade, and level motion");
    passed &= Check(
        IsPixelZoomCompositionIdle(
            PixelZoomMode::Zoom3x,
            PixelZoomMode::Zoom3x,
            PixelZoomMode::Zoom3x,
            1.f,
            1.f),
        "enabled zoom is idle only at its exact requested endpoint");
    passed &= Check(
        std::string(GetPixelZoomButtonLabel(PixelZoomMode::Off)) == "Zoom" &&
            std::string(
                GetPixelZoomButtonLabel(PixelZoomMode::Zoom2x)) == "Zoom" &&
            std::string(
                GetPixelZoomButtonLabel(PixelZoomMode::Zoom3x)) == "Zoom" &&
            std::string(
                GetPixelZoomButtonLabel(PixelZoomMode::Zoom4x)) == "Zoom" &&
            std::string(
                GetPixelZoomButtonLabel(PixelZoomMode::Zoom5x)) == "Zoom",
        "the footer label stays Zoom throughout the mode cycle");
    passed &= Check(
        std::string(GetPixelZoomAreaLabel(PixelZoomMode::Off)).empty() &&
            std::string(
                GetPixelZoomAreaLabel(PixelZoomMode::Zoom2x)) == "4x" &&
            std::string(
                GetPixelZoomAreaLabel(PixelZoomMode::Zoom3x)) == "9x" &&
            std::string(
                GetPixelZoomAreaLabel(PixelZoomMode::Zoom4x)) == "16x" &&
            std::string(
                GetPixelZoomAreaLabel(PixelZoomMode::Zoom5x)) == "25x",
        "the magnifier descriptor reports the exact destination pixel area");

    passed &= Check(
        ResolvePixelZoomLevelTransitionScale(0.f) == 1.f &&
            ResolvePixelZoomLevelTransitionScale(0.25f) == 0.93f &&
            ResolvePixelZoomLevelTransitionScale(0.5f) ==
                PixelZoomMinimumWindowScale &&
            ResolvePixelZoomLevelTransitionScale(0.75f) == 0.93f &&
            ResolvePixelZoomLevelTransitionScale(1.f) == 1.f,
        "level transition eases symmetrically through the 86-percent midpoint");
    passed &= Check(
        !ShouldSwitchPixelZoomLevel(0.499f) &&
            ShouldSwitchPixelZoomLevel(0.5f),
        "level transition switches exact factors only at its midpoint");

    float levelTransitionProgress = 0.f;
    for (int frame = 0; frame < 12; ++frame)
    {
        levelTransitionProgress = AdvancePixelZoomLevelTransition(
            levelTransitionProgress,
            1.f / 60.f);
    }
    passed &= Check(
        levelTransitionProgress == 1.f &&
            ResolvePixelZoomLevelTransitionScale(
                levelTransitionProgress) == 1.f,
        "level transition reaches an exact stable endpoint");

    float visibility = 0.f;
    for (int frame = 0; frame < 12; ++frame)
    {
        visibility = AdvancePixelZoomVisibility(
            visibility, true, 1.f / 60.f);
    }
    passed &= Check(
        visibility == 1.f &&
            SmoothPixelZoomVisibility(visibility) == 1.f,
        "zoom reaches an exact fully visible endpoint after fading in");
    for (int frame = 0; frame < 12; ++frame)
    {
        visibility = AdvancePixelZoomVisibility(
            visibility, false, 1.f / 60.f);
    }
    passed &= Check(
        visibility == 0.f &&
            SmoothPixelZoomVisibility(visibility) == 0.f,
        "zoom reaches an exact nonexistent endpoint after fading out");

    if (argc != 2)
    {
        std::cerr << "usage: uvsr_pixel_zoom_tests <source-root>\n";
        return 2;
    }
    const std::filesystem::path sourceRoot = argv[1];
    const std::string shader =
        ReadTextFile(sourceRoot / "src" / "pixel_zoom_ps.hlsl");
    const std::string application =
        ReadTextFile(sourceRoot / "src" / "uvsr.cpp");
    passed &= Check(
        shader.find("t_Source.Load(") != std::string::npos &&
            shader.find(".Sample") == std::string::npos &&
            shader.find("SamplerState") == std::string::npos,
        "zoom shader uses integer texel loads without a sampler");
    passed &= Check(
        shader.find("discard;") != std::string::npos &&
            shader.find("outlineCoverage") != std::string::npos &&
            shader.find("g_PixelZoom.outlineWidth") != std::string::npos,
        "zoom shader cuts the rounded silhouette before layering its outline");
    passed &= Check(
        shader.find("saturate(g_PixelZoom.opacity)") != std::string::npos &&
            application.find(
                "SmoothPixelZoomVisibility(m_PixelZoomVisibility)") !=
                std::string::npos &&
            application.find(
                "ResolveAnimatedPixelZoomLayout(") !=
                std::string::npos &&
            application.find(
                "ResolvePixelZoomLevelTransitionScale(") !=
                std::string::npos &&
            application.find(
                "ShouldSwitchPixelZoomLevel(") !=
                std::string::npos,
        "zoom appearance and exact level changes share eased transition state");
    passed &= Check(
        shader.find("shadowDistance") != std::string::npos &&
            shader.find(
                "if (!insidePanelBounds || signedDistance > 0.0)") !=
                std::string::npos &&
            application.find(
                ".setSrcBlend(nvrhi::BlendFactor::SrcAlpha)") !=
                std::string::npos,
        "the fading shadow is alpha blended only outside the zoom cutout");
    passed &= Check(
        application.find(
            "if (pixelZoomPassActive && m_PixelZoomPass)") !=
                std::string::npos,
        "zoom skips its render pass after the fade-out reaches zero");
    const size_t zoomRoundingPosition = application.find(
        "ImGui::GetStyle().WindowRounding,");
    const size_t zoomOpacityPosition = zoomRoundingPosition ==
        std::string::npos
        ? std::string::npos
        : application.find("pixelZoomOpacity", zoomRoundingPosition);
    passed &= Check(
        zoomRoundingPosition != std::string::npos &&
            zoomOpacityPosition != std::string::npos &&
            zoomOpacityPosition - zoomRoundingPosition < 128u,
        "zoom derives square OG and rounded Amp silhouettes from active style");
    passed &= Check(
        application.find(
            "constexpr float UiPanelShadowBlurPixels = 10.f;") !=
                std::string::npos &&
            application.find(
                "constexpr float UiPanelShadowOpacity = 0.34f;") !=
                std::string::npos &&
            application.find(
                "constexpr float UiPanelShadowOffsetYPixels = 3.f;") !=
                std::string::npos &&
            application.find(
                "constants.shadowBlur = UiPanelShadowBlurPixels;") !=
                std::string::npos &&
            application.find(
                "constants.shadowOpacity = UiPanelShadowOpacity;") !=
                std::string::npos &&
            application.find(
                "constants.shadowOffsetY = "
                "UiPanelShadowOffsetYPixels;") !=
                std::string::npos &&
            application.find(
                "static_assert(sizeof(PixelZoomConstants) == 96u);") !=
                std::string::npos &&
            shader.find("g_PixelZoom.shadowOffsetY") !=
                std::string::npos,
        "zoom and OG panels share the accepted shadow presentation constants");

    return passed ? 0 : 1;
}
