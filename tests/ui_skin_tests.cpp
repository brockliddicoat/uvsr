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

    static_assert(static_cast<std::size_t>(UiSkin::Count) == 2u);
    static_assert(UiSkinValues.size() == 2u);
    static_assert(DefaultUiSkin == UiSkin::Amp);

    const std::string_view expectedLabels[] = {
        "Amp",
        "OG"
    };

    std::set<int> enumValues;
    std::set<std::string> labels;
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
            ParseUiSkin(UiSkinLabel(skin)) == skin,
            "every stable label must parse back to its skin");
    }
    Require(
        UiSkinLabel(UiSkin::Count).empty(),
        "the Count sentinel must not have a selectable label");

    struct AliasCase
    {
        std::string_view text;
        UiSkin expected;
    };
    const AliasCase aliases[] = {
        { "amp", UiSkin::Amp },
        { " AMP ", UiSkin::Amp },
        { "og", UiSkin::Og },
        { "OG", UiSkin::Og }
    };
    for (const AliasCase& alias : aliases)
    {
        Require(
            ParseUiSkin(alias.text) == alias.expected,
            "normalized ASCII aliases must resolve deterministically");
    }

    UiSkin retained = UiSkin::Og;
    Require(
        !TryParseUiSkin("", retained) &&
            retained == UiSkin::Og,
        "a failed parse must leave the caller's skin untouched");
    Require(
        !ParseUiSkin("current") &&
            !ParseUiSkin("original") &&
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
        const bool og = skin == UiSkin::Og;
        Require(
            behavior.motionEnabled == !og,
            "only OG may disable motion");
        Require(
            behavior.stockImGuiWidgets == og,
            "only OG may select stock ImGui widgets");
    }

    const UiSkinBehavior amp =
        GetUiSkinBehavior(UiSkin::Amp);
    const UiSkinBehavior og =
        GetUiSkinBehavior(UiSkin::Og);
    Require(
        amp.motionEnabled &&
            !amp.stockImGuiWidgets &&
            amp.backdropEnabled &&
            amp.expandedWordSpacing,
        "Amp must retain the authored animated presentation");
    Require(
        !og.motionEnabled &&
            og.stockImGuiWidgets &&
            !og.backdropEnabled &&
            !og.expandedWordSpacing,
        "OG must retain stock surface and immediate typography behavior");

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
