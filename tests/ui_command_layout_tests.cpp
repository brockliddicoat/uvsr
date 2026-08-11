#include "ui_command_layout.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
    bool Check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

    bool Near(float left, float right)
    {
        return std::abs(left - right) <= 1e-4f;
    }
}

int main()
{
    using namespace uvsr;

    bool passed = true;

    constexpr UiSpacingTokens halfScaleSpacing =
        ResolveUiSpacingTokens(0.5f);
    constexpr UiSpacingTokens unitScaleSpacing =
        ResolveUiSpacingTokens(1.f);
    constexpr UiSpacingTokens fractionalScaleSpacing =
        ResolveUiSpacingTokens(1.25f);
    constexpr UiSpacingTokens doubleScaleSpacing =
        ResolveUiSpacingTokens(2.f);
    passed &= Check(
        Near(halfScaleSpacing.tight, 2.f) &&
            Near(halfScaleSpacing.regular, 4.f) &&
            Near(halfScaleSpacing.section, 8.f) &&
            Near(unitScaleSpacing.tight, 4.f) &&
            Near(unitScaleSpacing.regular, 8.f) &&
            Near(unitScaleSpacing.section, 16.f) &&
            Near(fractionalScaleSpacing.tight, 5.f) &&
            Near(fractionalScaleSpacing.regular, 10.f) &&
            Near(fractionalScaleSpacing.section, 20.f) &&
            Near(doubleScaleSpacing.tight, 8.f) &&
            Near(doubleScaleSpacing.regular, 16.f) &&
            Near(doubleScaleSpacing.section, 32.f),
        "UI spacing must retain exact 1x, 2x, and 4x ratios at every scale");
    passed &= Check(
        Near(ResolveUiSpacingTokens(0.f).tight, 2.f) &&
            Near(ResolveUiSpacingTokens(8.f).section, 64.f),
        "UI spacing scale must clamp to the supported display-scale range");

    constexpr UiCommandLayoutRect work{
        0.f, 0.f, 1920.f, 1080.f
    };
    constexpr float margin = unitScaleSpacing.section;
    constexpr float panelToCommandGap = unitScaleSpacing.tight;
    constexpr float minimumWidth = 325.f;
    constexpr float minimumRenderableWidth = 32.f;
    constexpr float reservedHeight = 108.f;
    constexpr float minimumHeight = 72.f;
    constexpr float minimumSettingsHeight = 96.f;

    const CommandInterfaceLayout layout =
        ResolveCommandInterfaceLayout(
            work,
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    const UiCommandLayoutRect command =
        ResolveCommandInterfaceRect(layout);
    passed &= Check(
        layout.fits && !layout.compact,
        "the default command lane must fit without compact presentation");
    passed &= Check(
        Near(layout.left, 16.f) &&
            Near(layout.width, 1888.f) &&
            Near(layout.left + layout.width, 1904.f) &&
            Near(layout.bottom, 1064.f) &&
            Near(layout.top, 956.f) &&
            Near(layout.height, reservedHeight),
        "the command lane must fill the complete margin-to-margin width");
    passed &= Check(
        Near(layout.settingsMaximumBottom, 952.f) &&
            Near(command.minX, margin) &&
            Near(command.maxX, work.maxX - margin) &&
            Near(
                command.minY - layout.settingsMaximumBottom,
                panelToCommandGap),
        "Settings must stop one Tight gap above the command lane while the "
        "root stack retains its distinct Section outer margin");
    const CommandInterfaceLayout negativeCommandGap =
        ResolveCommandInterfaceLayout(
            work,
            margin,
            -40.f,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    passed &= Check(
        negativeCommandGap.fits &&
            Near(
                negativeCommandGap.settingsMaximumBottom,
                negativeCommandGap.top) &&
            Near(negativeCommandGap.left, layout.left) &&
            Near(negativeCommandGap.width, layout.width),
        "a negative panel-to-command gap clamps independently to zero without "
        "changing the Section outer lane");

    const float fullSettingsBottom =
        ResolveSettingsMaximumBottom(layout, work, margin, 0.f);
    const float halfSettingsBottom =
        ResolveSettingsMaximumBottom(layout, work, margin, 0.5f);
    const float reservedSettingsBottom =
        ResolveSettingsMaximumBottom(layout, work, margin, 1.f);
    passed &= Check(
        Near(fullSettingsBottom, work.maxY - margin) &&
            Near(halfSettingsBottom, 1008.f) &&
            Near(reservedSettingsBottom, layout.settingsMaximumBottom),
        "Settings must smoothly shrink from the full work area to the command lane");
    passed &= Check(
        Near(
            ResolveSettingsMaximumBottom(layout, work, margin, -1.f),
            fullSettingsBottom) &&
            Near(
                ResolveSettingsMaximumBottom(layout, work, margin, 2.f),
            reservedSettingsBottom),
        "Settings command appearance must clamp to its closed and open endpoints");

    constexpr float performanceTop = 140.f;
    constexpr float settingsWindowReserve = 112.f;
    // Detached root panels use the same tight 1x spacing as sibling drawers.
    constexpr float panelGap = unitScaleSpacing.tight;
    passed &= Check(
        Near(panelGap, panelToCommandGap),
        "Performance-to-Settings and full-open Settings-to-command gaps share "
        "the Tight token, independently from the Section outer margin");
    const float fullPerformanceHeight = ResolvePerformanceMaximumWindowHeight(
        fullSettingsBottom,
        performanceTop,
        settingsWindowReserve,
        panelGap);
    const float reservedPerformanceHeight = ResolvePerformanceMaximumWindowHeight(
        reservedSettingsBottom,
        performanceTop,
        settingsWindowReserve,
        panelGap);
    passed &= Check(
        Near(fullPerformanceHeight, 808.f) &&
            Near(reservedPerformanceHeight, 696.f) &&
            reservedPerformanceHeight < fullPerformanceHeight,
        "the detached Performance window must shrink with the CLI-reduced "
        "panel-stack cap");
    passed &= Check(
        Near(
            performanceTop + fullPerformanceHeight + panelGap +
                settingsWindowReserve,
            fullSettingsBottom) &&
            Near(
                performanceTop + reservedPerformanceHeight +
                    panelGap + settingsWindowReserve,
                reservedSettingsBottom),
        "Performance and Settings must reserve one exact styled gap");

    const float exactFitBottom =
        performanceTop + 1.f + panelGap + settingsWindowReserve;
    const float onePixelShortBottom = exactFitBottom - 1.f;
    passed &= Check(
        Near(
            ResolvePerformanceMaximumWindowHeight(
                exactFitBottom,
                performanceTop,
                settingsWindowReserve,
                panelGap),
            1.f) &&
            Near(
                performanceTop + 1.f + panelGap + settingsWindowReserve,
                exactFitBottom),
        "the exact minimum stack must fit Performance, the gap, and Settings");
    passed &= Check(
        Near(
            ResolvePerformanceMaximumWindowHeight(
                onePixelShortBottom,
                performanceTop,
                settingsWindowReserve,
                panelGap),
            1.f) &&
            Near(
                performanceTop + 1.f + panelGap + settingsWindowReserve,
                onePixelShortBottom + 1.f),
        "a one-pixel-short stack must retain the one-pixel Performance floor");

    constexpr float stackTranslationY = 50.f;
    passed &= Check(
        Near(
            ResolvePerformanceMaximumWindowHeight(
                reservedSettingsBottom + stackTranslationY,
                performanceTop + stackTranslationY,
                settingsWindowReserve,
                panelGap),
            reservedPerformanceHeight),
        "translated stack coordinates must preserve the detached panel height");
    passed &= Check(
        Near(
            ResolvePerformanceMaximumWindowHeight(
                100.f, 99.f, 80.f, panelGap),
            1.f),
        "the Performance window cap must remain valid in a very short viewport");
    passed &= Check(
        Near(
            ResolvePerformanceMaximumWindowHeight(
                300.f, 100.f, -40.f, panelGap),
            196.f),
        "a negative Settings reserve must clamp independently to zero");
    passed &= Check(
        Near(
            ResolvePerformanceMaximumWindowHeight(
                300.f, 100.f, 80.f, -40.f),
            120.f),
        "a negative panel gap must clamp independently to zero");

    // The raw command rectangle remains fixed while Settings alone follows the
    // command appearance.
    float previousSettingsBottom = fullSettingsBottom;
    for (int presentationState = 0;
        presentationState <= 5;
        ++presentationState)
    {
        const CommandInterfaceLayout repeated =
            ResolveCommandInterfaceLayout(
                work,
                margin,
                panelToCommandGap,
                minimumWidth,
                minimumRenderableWidth,
                reservedHeight,
                minimumHeight,
                minimumSettingsHeight);
        passed &= Check(
            Near(repeated.left, layout.left) &&
                Near(repeated.top, layout.top) &&
                Near(repeated.width, layout.width) &&
                Near(repeated.settingsMaximumBottom, layout.settingsMaximumBottom),
            "every presentation state must reuse one raw command lane");
        const float appearance = float(presentationState) / 5.f;
        const float settingsBottom = ResolveSettingsMaximumBottom(
            repeated,
            work,
            margin,
            appearance);
        passed &= Check(
            settingsBottom <= previousSettingsBottom + 1e-4f,
            "Settings must shrink monotonically as the command interface appears");
        previousSettingsBottom = settingsBottom;
    }

    // The raw lane stays full-width for layout and reservation, while its
    // authored presentation scales uniformly around the bottom-center pivot.
    // The pivot remains fixed, the visual center cannot drift sideways, and
    // the reserved raw gap can only grow during entry or exit.
    const float commandPivotX =
        (command.minX + command.maxX) * 0.5f;
    const float commandPivotY = command.maxY;
    for (const float scale : { 0.f, 0.5f, 1.f })
    {
        const UiCommandLayoutRect presented{
            commandPivotX + (command.minX - commandPivotX) * scale,
            commandPivotY + (command.minY - commandPivotY) * scale,
            commandPivotX + (command.maxX - commandPivotX) * scale,
            commandPivotY + (command.maxY - commandPivotY) * scale
        };
        passed &= Check(
            Near((presented.minX + presented.maxX) * 0.5f, commandPivotX) &&
                Near(presented.maxY, commandPivotY) &&
                Near(
                    presented.maxX - presented.minX,
                    (command.maxX - command.minX) * scale) &&
                Near(
                    presented.maxY - presented.minY,
                    (command.maxY - command.minY) * scale) &&
                presented.minY - layout.settingsMaximumBottom >=
                    panelToCommandGap - 1e-4f,
            "command appearance must scale uniformly from bottom center "
            "without crossing the reserved panel-stack boundary");
    }

    constexpr float translatedX = 100.f;
    constexpr float translatedY = 50.f;
    constexpr UiCommandLayoutRect translatedWork{
        work.minX + translatedX,
        work.minY + translatedY,
        work.maxX + translatedX,
        work.maxY + translatedY
    };
    const CommandInterfaceLayout translated =
        ResolveCommandInterfaceLayout(
            translatedWork,
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    passed &= Check(
        translated.fits &&
            Near(translated.left, layout.left + translatedX) &&
            Near(translated.top, layout.top + translatedY) &&
            Near(translated.bottom, layout.bottom + translatedY) &&
            Near(
                translated.settingsMaximumBottom,
                layout.settingsMaximumBottom + translatedY) &&
            Near(translated.width, layout.width),
        "nonzero WorkPos must translate every vertical-lane boundary");

    constexpr float dpiScale = 1.5f;
    constexpr UiCommandLayoutRect dpiWork{
        0.f, 0.f, 2880.f, 1620.f
    };
    const CommandInterfaceLayout dpiLayout =
        ResolveCommandInterfaceLayout(
            dpiWork,
            margin * dpiScale,
            panelToCommandGap * dpiScale,
            minimumWidth * dpiScale,
            minimumRenderableWidth * dpiScale,
            reservedHeight * dpiScale,
            minimumHeight * dpiScale,
            minimumSettingsHeight * dpiScale);
    passed &= Check(
        dpiLayout.fits && !dpiLayout.compact &&
            Near(dpiLayout.left, layout.left * dpiScale) &&
            Near(dpiLayout.top, layout.top * dpiScale) &&
            Near(dpiLayout.bottom, layout.bottom * dpiScale) &&
            Near(dpiLayout.width, layout.width * dpiScale) &&
            Near(
                dpiLayout.settingsMaximumBottom,
                layout.settingsMaximumBottom * dpiScale),
        "DPI scaling must preserve the full lane and Settings cap");

    const CommandInterfaceLayout exactMinimumWidth =
        ResolveCommandInterfaceLayout(
            { 0.f, 0.f, minimumWidth + margin * 2.f, 720.f },
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    const CommandInterfaceLayout compact =
        ResolveCommandInterfaceLayout(
            { 0.f, 0.f, minimumWidth + margin * 2.f - 1.f, 720.f },
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    passed &= Check(
        exactMinimumWidth.fits &&
            !exactMinimumWidth.compact &&
            Near(exactMinimumWidth.width, minimumWidth),
        "the exact width threshold must remain noncompact");
    passed &= Check(
        compact.fits &&
            compact.compact &&
            Near(compact.left, margin) &&
            Near(compact.width, minimumWidth - 1.f) &&
            Near(
                compact.left + compact.width,
                minimumWidth + margin - 1.f),
        "a narrow command lane must keep both exact edge margins");

    const CommandInterfaceLayout impossibleWidth =
        ResolveCommandInterfaceLayout(
            { 0.f, 0.f, margin * 2.f, 720.f },
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    passed &= Check(
        !impossibleWidth.fits &&
            impossibleWidth.compact &&
            Near(impossibleWidth.width, 0.f),
        "zero margin-to-margin width must report impossible");

    const CommandInterfaceLayout belowRenderableWidth =
        ResolveCommandInterfaceLayout(
            {
                0.f,
                0.f,
                margin * 2.f + minimumRenderableWidth - 1.f,
                720.f
            },
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    const CommandInterfaceLayout exactRenderableWidth =
        ResolveCommandInterfaceLayout(
            {
                0.f,
                0.f,
                margin * 2.f + minimumRenderableWidth,
                720.f
            },
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    passed &= Check(
        !belowRenderableWidth.fits &&
            belowRenderableWidth.compact &&
            Near(
                belowRenderableWidth.width,
                minimumRenderableWidth - 1.f),
        "a positive width below ImGui's root minimum must be withheld");
    passed &= Check(
        exactRenderableWidth.fits &&
            exactRenderableWidth.compact &&
            Near(exactRenderableWidth.width, minimumRenderableWidth),
        "the exact ImGui root minimum width must remain renderable");

    const CommandInterfaceLayout belowMinimumHeight =
        ResolveCommandInterfaceLayout(
            { 0.f, 0.f, 1920.f, 80.f },
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    passed &= Check(
        !belowMinimumHeight.fits &&
            Near(belowMinimumHeight.height, 48.f),
        "insufficient command height must report impossible");

    const CommandInterfaceLayout raisedToMinimumHeight =
        ResolveCommandInterfaceLayout(
            { 0.f, 0.f, 1920.f, 720.f },
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            20.f,
            minimumHeight,
            minimumSettingsHeight);
    passed &= Check(
        raisedToMinimumHeight.fits &&
            Near(raisedToMinimumHeight.height, minimumHeight),
        "a short reservation must retain the minimum command envelope");

    const float exactMinimumWorkHeight =
        margin * 2.f +
        panelToCommandGap +
        reservedHeight +
        minimumSettingsHeight;
    const CommandInterfaceLayout positiveSettingsSliver =
        ResolveCommandInterfaceLayout(
            {
                0.f,
                0.f,
                1920.f,
                exactMinimumWorkHeight - 1.f
            },
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    const CommandInterfaceLayout exactSettingsEnvelope =
        ResolveCommandInterfaceLayout(
            { 0.f, 0.f, 1920.f, exactMinimumWorkHeight },
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    passed &= Check(
        !positiveSettingsSliver.fits &&
            Near(
                positiveSettingsSliver.settingsMaximumBottom -
                    margin,
                minimumSettingsHeight - 1.f),
        "a positive Settings sliver below its ImGui envelope must be withheld");
    passed &= Check(
        exactSettingsEnvelope.fits &&
            Near(
                exactSettingsEnvelope.settingsMaximumBottom - margin,
                minimumSettingsHeight),
        "the exact Settings minimum envelope must remain renderable");

    const CommandInterfaceLayout noSettingsRoom =
        ResolveCommandInterfaceLayout(
            { 0.f, 0.f, 1920.f, 200.f },
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            1000.f,
            minimumHeight,
            minimumSettingsHeight);
    passed &= Check(
        !noSettingsRoom.fits &&
            Near(noSettingsRoom.top, margin) &&
            Near(noSettingsRoom.settingsMaximumBottom, margin),
        "a lane consuming all vertical space must be withheld");

    const CommandInterfaceLayout invalidWork =
        ResolveCommandInterfaceLayout(
            { 0.f, 0.f, 0.f, 0.f },
            margin,
            panelToCommandGap,
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    passed &= Check(
        !invalidWork.fits,
        "an invalid work rectangle must report impossible");

    if (!passed)
        return EXIT_FAILURE;

    std::cout << "UI command layout validation passed\n";
    return EXIT_SUCCESS;
}
