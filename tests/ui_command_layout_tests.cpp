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

    constexpr UiCommandLayoutRect work{
        0.f, 0.f, 1920.f, 1080.f
    };
    constexpr float margin = 12.f;
    constexpr float minimumWidth = 325.f;
    constexpr float minimumRenderableWidth = 32.f;
    constexpr float reservedHeight = 108.f;
    constexpr float minimumHeight = 72.f;
    constexpr float minimumSettingsHeight = 96.f;

    const CommandInterfaceLayout layout =
        ResolveCommandInterfaceLayout(
            work,
            margin,
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
        Near(layout.left, 12.f) &&
            Near(layout.width, 1896.f) &&
            Near(layout.left + layout.width, 1908.f) &&
            Near(layout.bottom, 1068.f) &&
            Near(layout.top, 960.f) &&
            Near(layout.height, reservedHeight),
        "the command lane must fill the complete margin-to-margin width");
    passed &= Check(
        Near(layout.settingsMaximumBottom, 948.f) &&
            Near(command.minX, margin) &&
            Near(command.maxX, work.maxX - margin) &&
            Near(command.minY - layout.settingsMaximumBottom, margin),
        "Settings must stop one consistent margin above the command lane");

    // Settings visibility, collapse, and appearance no longer enter the
    // command formula. Amp and OG therefore share the same raw rectangle.
    for (int presentationState = 0;
        presentationState < 6;
        ++presentationState)
    {
        const CommandInterfaceLayout repeated =
            ResolveCommandInterfaceLayout(
                work,
                margin,
                minimumWidth,
                minimumRenderableWidth,
                reservedHeight,
                minimumHeight,
                minimumSettingsHeight);
        passed &= Check(
            Near(repeated.left, layout.left) &&
                Near(repeated.top, layout.top) &&
                Near(repeated.width, layout.width) &&
                Near(
                    repeated.settingsMaximumBottom,
                    layout.settingsMaximumBottom),
            "every Settings and skin presentation state must reuse one lane");
    }

    // Amp preserves the full horizontal lane while scaling only Y about the
    // bottom edge. The presented top moves downward, so the reserved raw gap
    // can only grow during entry or exit.
    for (const float scale : { 0.86f, 0.93f, 1.f })
    {
        const float presentedTop =
            layout.bottom +
            (layout.top - layout.bottom) * scale;
        passed &= Check(
            Near(command.minX, margin) &&
                Near(command.maxX, work.maxX - margin) &&
                presentedTop - layout.settingsMaximumBottom >=
                    margin - 1e-4f,
            "Amp vertical appearance must preserve width and non-overlap");
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
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    const CommandInterfaceLayout compact =
        ResolveCommandInterfaceLayout(
            { 0.f, 0.f, minimumWidth + margin * 2.f - 1.f, 720.f },
            margin,
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
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    passed &= Check(
        !belowMinimumHeight.fits &&
            Near(belowMinimumHeight.height, 56.f),
        "insufficient command height must report impossible");

    const CommandInterfaceLayout raisedToMinimumHeight =
        ResolveCommandInterfaceLayout(
            { 0.f, 0.f, 1920.f, 720.f },
            margin,
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
        margin * 3.f +
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
            minimumWidth,
            minimumRenderableWidth,
            reservedHeight,
            minimumHeight,
            minimumSettingsHeight);
    const CommandInterfaceLayout exactSettingsEnvelope =
        ResolveCommandInterfaceLayout(
            { 0.f, 0.f, 1920.f, exactMinimumWorkHeight },
            margin,
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
