#include "ui_skin.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "UI skin validation failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using namespace uvsr;

    static_assert(static_cast<int>(UiSkin::Amp) == 0);
    static_assert(static_cast<int>(UiSkin::Og) == 1);
    static_assert(static_cast<std::size_t>(UiSkin::Count) == 2u);
    static_assert(UiSkinValues.size() == 2u);
    static_assert(UiSkinValues[0] == UiSkin::Amp);
    static_assert(UiSkinValues[1] == UiSkin::Og);
    static_assert(DefaultUiSkin == UiSkin::Amp);
    static_assert(DefaultUiSecondaryAccent == UiRgbaColor{
        0.26f, 0.59f, 0.98f, 0.31f });
    static_assert(DefaultUiTertiaryAccent == UiRgbaColor{
        0.117f, 0.217f, 0.342f, 1.f });
    static_assert(DefaultUiAmpPalette == UiSkinPalette{
        { 66.f / 255.f, 150.f / 255.f, 250.f / 255.f, 0.31f },
        { 0.94f, 0.95f, 0.98f, 1.f },
        { 0.018f, 0.018f, 0.018f, 0.72f }
    });
    static_assert(
        FindDefaultUiSkinPalette(UiSkin::Amp) == &DefaultUiAmpPalette);
    static_assert(FindDefaultUiSkinPalette(UiSkin::Og) == nullptr);
    static_assert(FindDefaultUiSkinPalette(UiSkin::Count) == nullptr);

    UiAccentSettings accents;
    Require(
        accents.amp == DefaultUiAmpPalette &&
            accents.secondaryAccent == DefaultUiSecondaryAccent &&
            accents.tertiaryAccent == DefaultUiTertiaryAccent,
        "interface accents must factory-reset the Amp RGBA palette and its "
        "historical blue semantic endpoints");
    Require(
        FindUiSkinPalette(accents, UiSkin::Amp) == &accents.amp &&
            FindUiSkinPalette(accents, UiSkin::Og) == nullptr &&
            FindUiSkinPalette(accents, UiSkin::Count) == nullptr,
        "Amp alone may expose the authored RGBA palette");
    const UiAccentSettings& constAccents = accents;
    Require(
        FindUiSkinPalette(constAccents, UiSkin::Amp) ==
                &constAccents.amp &&
            FindUiSkinPalette(constAccents, UiSkin::Og) == nullptr,
        "const palette lookup must preserve Amp-only authored ownership");

    UiAccentSettings resetAccents = accents;
    resetAccents.amp.primaryAccent = { 1.f, 0.f, 0.f, 0.25f };
    resetAccents.amp.fontColor = { 0.f, 0.f, 0.f, 0.5f };
    resetAccents.amp.primaryBackground = { 1.f, 1.f, 1.f, 0.75f };
    resetAccents.secondaryAccent = { 0.f, 1.f, 0.f, 0.4f };
    resetAccents.tertiaryAccent = { 0.f, 0.f, 1.f, 0.6f };
    resetAccents = UiAccentSettings{};
    Require(
        resetAccents.amp == DefaultUiAmpPalette &&
            resetAccents.secondaryAccent == DefaultUiSecondaryAccent &&
            resetAccents.tertiaryAccent == DefaultUiTertiaryAccent,
        "value-initializing UiAccentSettings must restore every unique "
        "Interface color target after live edits");

    const std::string_view expectedLabels[] = {
        "Amp",
        "Ogg"
    };
    const std::string_view expectedCommandValues[] = {
        "amp",
        "ogg"
    };

    std::set<int> enumValues;
    std::set<std::string> labels;
    std::set<std::string> commandValues;
    for (std::size_t index = 0u; index < UiSkinValues.size(); ++index)
    {
        const UiSkin skin = UiSkinValues[index];
        Require(
            enumValues.insert(static_cast<int>(skin)).second,
            "the two selectable enum values must be unique");
        Require(
            UiSkinLabel(skin) == expectedLabels[index],
            "skin labels must retain their stable product wording");
        Require(
            labels.insert(std::string(UiSkinLabel(skin))).second,
            "skin labels must be unique");
        Require(
            UiSkinCommandValue(skin) == expectedCommandValues[index],
            "skin command values must retain their stable spelling");
        Require(
            commandValues.insert(
                std::string(UiSkinCommandValue(skin))).second,
            "skin command values must be unique");
        Require(
            ParseUiSkin(UiSkinLabel(skin)) == skin &&
                ParseUiSkin(UiSkinCommandValue(skin)) == skin,
            "every stable label and command value must parse back to its skin");
    }
    Require(
        UiSkinLabel(UiSkin::Count).empty() &&
            UiSkinCommandValue(UiSkin::Count).empty(),
        "the Count sentinel must not have selectable text");

    struct AliasCase
    {
        std::string_view text;
        UiSkin expected;
    };
    const AliasCase aliases[] = {
        { "amp", UiSkin::Amp },
        { " AMP ", UiSkin::Amp },
        { "ogg", UiSkin::Og },
        { "OGG", UiSkin::Og }
    };
    for (const AliasCase& alias : aliases)
    {
        Require(
            ParseUiSkin(alias.text) == alias.expected,
            "normalized ASCII aliases must resolve deterministically");
    }
    Require(
        !ParseUiSkin("og") && !ParseUiSkin("OG"),
        "the retired short spelling must fail rather than bypass the "
        "canonical command domain");

    UiSkin retained = UiSkin::Og;
    Require(
        !TryParseUiSkin("", retained) &&
            retained == UiSkin::Og,
        "a failed parse must leave the caller's skin untouched");
    Require(
        !ParseUiSkin("current") &&
            !ParseUiSkin("original") &&
            !ParseUiSkin("neo") &&
            !ParseUiSkin("white") &&
            !ParseUiSkin("noir") &&
            !ParseUiSkin("chatgpt-codex") &&
            !ParseUiSkin("ue5") &&
            !ParseUiSkin("signal"),
        "legacy and rejected skin aliases must not parse");
    Require(
        !ParseUiSkin("a.mp"),
        "unsupported punctuation must not be discarded");
    Require(
        !ParseUiSkin("a-m-p") &&
            !ParseUiSkin("a m p") &&
            !ParseUiSkin("o_g"),
        "segmented spellings must not create undocumented aliases");
    Require(
        !ParseUiSkin("unknown"),
        "unknown skin names must be rejected");
    Require(
        !ParseUiSkin("\xC3\xA4mp"),
        "non-ASCII skin values must be rejected");

    for (const UiSkin skin : UiSkinValues)
    {
        const UiSkinBehavior behavior = GetUiSkinBehavior(skin);
        const bool ogg = skin == UiSkin::Og;
        Require(
            behavior.motionEnabled == !ogg,
            "only Ogg may disable motion");
        Require(
            behavior.stockImGuiWidgets == ogg,
            "only Ogg may select stock ImGui widgets");
    }

    const UiSkinBehavior amp =
        GetUiSkinBehavior(UiSkin::Amp);
    const UiSkinBehavior ogg =
        GetUiSkinBehavior(UiSkin::Og);
    Require(
        amp.motionEnabled &&
            !amp.stockImGuiWidgets &&
            amp.backdropEnabled &&
            amp.expandedWordSpacing,
        "Amp must retain the authored animated presentation");
    Require(
        !ogg.motionEnabled &&
            ogg.stockImGuiWidgets &&
            !ogg.backdropEnabled &&
            !ogg.expandedWordSpacing,
        "Ogg must retain stock surface and immediate typography behavior");
    Require(
        ResolveUiMotionEnabled(UiSkin::Amp, true) &&
            !ResolveUiMotionEnabled(UiSkin::Amp, false),
        "the Interface animation preference must gate Amp motion");
    Require(
        !ResolveUiMotionEnabled(UiSkin::Og, true) &&
            !ResolveUiMotionEnabled(UiSkin::Og, false),
        "Ogg must remain immediate regardless of the Interface preference");

    const UiSkinBehavior sentinel =
        GetUiSkinBehavior(UiSkin::Count);
    Require(
        sentinel.motionEnabled == amp.motionEnabled &&
            sentinel.stockImGuiWidgets == amp.stockImGuiWidgets &&
            sentinel.backdropEnabled == amp.backdropEnabled &&
            sentinel.expandedWordSpacing == amp.expandedWordSpacing,
        "an invalid sentinel must fall back to launch-default behavior");

    std::cout << "UI skin validation passed\n";
    return EXIT_SUCCESS;
}
