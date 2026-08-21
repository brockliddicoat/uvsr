#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    enum class UiFontFamily : std::uint8_t
    {
        Codex,
        NotoSans,
        ProggyClean,
        Count
    };

    inline constexpr UiFontFamily DefaultUiFontFamily =
        UiFontFamily::NotoSans;
    inline constexpr std::size_t UiFontFamilyCount =
        static_cast<std::size_t>(UiFontFamily::Count);
    inline constexpr std::array<UiFontFamily, UiFontFamilyCount>
        UiFontFamilyValues = {
            UiFontFamily::Codex,
            UiFontFamily::NotoSans,
            UiFontFamily::ProggyClean
        };

    [[nodiscard]] constexpr UiFontFamily ResolveUiFontFamily(
        UiFontFamily family) noexcept
    {
        return static_cast<std::size_t>(family) < UiFontFamilyCount
            ? family
            : DefaultUiFontFamily;
    }

    [[nodiscard]] constexpr std::string_view UiFontFamilyLabel(
        UiFontFamily family) noexcept
    {
        switch (family)
        {
        case UiFontFamily::Codex:
            return "Codex (Segoe UI)";
        case UiFontFamily::NotoSans:
            return "Noto Sans";
        case UiFontFamily::ProggyClean:
            return "Ogg (ProggyClean)";
        case UiFontFamily::Count:
            break;
        }
        return {};
    }

    [[nodiscard]] constexpr std::string_view UiFontFamilyCommandValue(
        UiFontFamily family) noexcept
    {
        switch (family)
        {
        case UiFontFamily::Codex:
            return "codex";
        case UiFontFamily::NotoSans:
            return "noto-sans";
        case UiFontFamily::ProggyClean:
            return "proggy-clean";
        case UiFontFamily::Count:
            break;
        }
        return {};
    }
}
