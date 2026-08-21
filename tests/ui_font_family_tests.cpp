#include "ui_font_family.h"

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
            std::cerr << "UI font family validation failed: "
                      << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using namespace uvsr;

    static_assert(static_cast<int>(UiFontFamily::Codex) == 0);
    static_assert(static_cast<int>(UiFontFamily::NotoSans) == 1);
    static_assert(static_cast<int>(UiFontFamily::ProggyClean) == 2);
    static_assert(static_cast<std::size_t>(UiFontFamily::Count) == 3u);
    static_assert(UiFontFamilyValues.size() == 3u);
    static_assert(UiFontFamilyValues[0] == UiFontFamily::Codex);
    static_assert(UiFontFamilyValues[1] == UiFontFamily::NotoSans);
    static_assert(UiFontFamilyValues[2] == UiFontFamily::ProggyClean);
    static_assert(DefaultUiFontFamily == UiFontFamily::NotoSans);
    static_assert(
        ResolveUiFontFamily(UiFontFamily::Count) == DefaultUiFontFamily);
    static_assert(
        ResolveUiFontFamily(static_cast<UiFontFamily>(0xffu)) ==
            DefaultUiFontFamily);

    constexpr std::string_view ExpectedLabels[] = {
        "Codex (Segoe UI)",
        "Noto Sans",
        "Ogg (ProggyClean)"
    };
    constexpr std::string_view ExpectedCommandValues[] = {
        "codex",
        "noto-sans",
        "proggy-clean"
    };

    std::set<int> enumValues;
    std::set<std::string> labels;
    std::set<std::string> commandValues;
    for (std::size_t index = 0u;
         index < UiFontFamilyValues.size();
         ++index)
    {
        const UiFontFamily family = UiFontFamilyValues[index];
        Require(
            ResolveUiFontFamily(family) == family,
            "selectable values must resolve without remapping");
        Require(
            enumValues.insert(static_cast<int>(family)).second,
            "selectable enum values must be unique");
        Require(
            UiFontFamilyLabel(family) == ExpectedLabels[index],
            "font labels must retain exact product wording");
        Require(
            labels.insert(std::string(UiFontFamilyLabel(family))).second,
            "font labels must be unique");
        Require(
            UiFontFamilyCommandValue(family) ==
                ExpectedCommandValues[index],
            "font command values must retain canonical spelling");
        Require(
            commandValues.insert(
                std::string(UiFontFamilyCommandValue(family))).second,
            "font command values must be unique");
    }

    Require(
        UiFontFamilyLabel(UiFontFamily::Count).empty() &&
            UiFontFamilyCommandValue(UiFontFamily::Count).empty(),
        "the Count sentinel must not expose selectable text");

    std::cout << "UI font family validation passed\n";
    return EXIT_SUCCESS;
}
