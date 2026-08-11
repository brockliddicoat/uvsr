#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace uvsr
{
    enum class UiSkin
    {
        Amp,
        Og,
        Count
    };

    inline constexpr UiSkin DefaultUiSkin = UiSkin::Amp;
    inline constexpr std::size_t UiSkinCount =
        static_cast<std::size_t>(UiSkin::Count);

    inline constexpr std::array<UiSkin, UiSkinCount> UiSkinValues = {
        UiSkin::Amp,
        UiSkin::Og
    };

    struct UiRgbaColor
    {
        float red = 0.f;
        float green = 0.f;
        float blue = 0.f;
        float alpha = 1.f;

        [[nodiscard]] constexpr bool operator==(
            const UiRgbaColor& other) const
        {
            return red == other.red &&
                green == other.green &&
                blue == other.blue &&
                alpha == other.alpha;
        }

        [[nodiscard]] constexpr bool operator!=(
            const UiRgbaColor& other) const
        {
            return !(*this == other);
        }
    };

    struct UiSkinPalette
    {
        UiRgbaColor primaryAccent;
        UiRgbaColor fontColor;
        UiRgbaColor primaryBackground;

        [[nodiscard]] constexpr bool operator==(
            const UiSkinPalette& other) const
        {
            return primaryAccent == other.primaryAccent &&
                fontColor == other.fontColor &&
                primaryBackground == other.primaryBackground;
        }

        [[nodiscard]] constexpr bool operator!=(
            const UiSkinPalette& other) const
        {
            return !(*this == other);
        }
    };

    inline constexpr UiRgbaColor DefaultUiSecondaryAccent = {
        0.26f,
        0.59f,
        0.98f,
        0.31f
    };
    inline constexpr UiRgbaColor DefaultUiTertiaryAccent = {
        0.117f,
        0.217f,
        0.342f,
        1.f
    };

    inline constexpr UiSkinPalette DefaultUiAmpPalette = {
        { 66.f / 255.f, 150.f / 255.f, 250.f / 255.f, 0.31f },
        { 0.94f, 0.95f, 0.98f, 1.f },
        { 0.018f, 0.018f, 0.018f, 0.72f }
    };
    struct UiAccentSettings
    {
        UiSkinPalette amp = DefaultUiAmpPalette;
        UiRgbaColor secondaryAccent = DefaultUiSecondaryAccent;
        UiRgbaColor tertiaryAccent = DefaultUiTertiaryAccent;
    };

    [[nodiscard]] constexpr const UiSkinPalette*
        FindDefaultUiSkinPalette(UiSkin skin)
    {
        switch (skin)
        {
        case UiSkin::Amp:
            return &DefaultUiAmpPalette;
        case UiSkin::Og:
        case UiSkin::Count:
            return nullptr;
        }
        return nullptr;
    }

    [[nodiscard]] inline UiSkinPalette* FindUiSkinPalette(
        UiAccentSettings& settings,
        UiSkin skin)
    {
        switch (skin)
        {
        case UiSkin::Amp:
            return &settings.amp;
        case UiSkin::Og:
        case UiSkin::Count:
            return nullptr;
        }
        return nullptr;
    }

    [[nodiscard]] inline const UiSkinPalette* FindUiSkinPalette(
        const UiAccentSettings& settings,
        UiSkin skin)
    {
        switch (skin)
        {
        case UiSkin::Amp:
            return &settings.amp;
        case UiSkin::Og:
        case UiSkin::Count:
            return nullptr;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr std::string_view UiSkinLabel(UiSkin skin)
    {
        switch (skin)
        {
        case UiSkin::Amp:
            return "Amp";
        case UiSkin::Og:
            return "Ogg";
        case UiSkin::Count:
            break;
        }
        return {};
    }

    [[nodiscard]] constexpr std::string_view UiSkinCommandValue(UiSkin skin)
    {
        switch (skin)
        {
        case UiSkin::Amp:
            return "amp";
        case UiSkin::Og:
            return "og";
        case UiSkin::Count:
            break;
        }
        return {};
    }

    struct UiSkinBehavior
    {
        bool motionEnabled = true;
        bool stockImGuiWidgets = false;
        bool backdropEnabled = true;
        bool expandedWordSpacing = true;
    };

    // Behavior is deliberately separate from colors and dimensions. The
    // integration layer can change visual tokens without accidentally weakening
    // the defining contract of Ogg: it alone is immediate and uses the stock
    // widget branches.
    [[nodiscard]] constexpr UiSkinBehavior GetUiSkinBehavior(UiSkin skin)
    {
        switch (skin)
        {
        case UiSkin::Amp:
            return { true, false, true, true };
        case UiSkin::Og:
            return { false, true, false, false };
        case UiSkin::Count:
            break;
        }

        // Count is a sentinel rather than a selectable skin. Returning the
        // launch-default behavior keeps an accidentally stale value visually
        // safe while callers retain responsibility for validating stored enum
        // values before exposing them.
        return GetUiSkinBehavior(DefaultUiSkin);
    }

    [[nodiscard]] constexpr bool ResolveUiMotionEnabled(
        UiSkin skin,
        bool animationsEnabled)
    {
        return animationsEnabled && GetUiSkinBehavior(skin).motionEnabled;
    }

    namespace detail
    {
        [[nodiscard]] inline bool NormalizeUiSkinName(
            std::string_view text,
            std::string& normalized)
        {
            normalized.clear();
            std::size_t begin = 0u;
            std::size_t end = text.size();
            const auto isWhitespace = [](unsigned char character)
            {
                return character == static_cast<unsigned char>(' ') ||
                    character == static_cast<unsigned char>('\t') ||
                    character == static_cast<unsigned char>('\r') ||
                    character == static_cast<unsigned char>('\n');
            };
            while (begin < end &&
                isWhitespace(static_cast<unsigned char>(text[begin])))
            {
                ++begin;
            }
            while (end > begin &&
                isWhitespace(static_cast<unsigned char>(text[end - 1u])))
            {
                --end;
            }
            normalized.reserve(end - begin);

            for (std::size_t index = begin; index < end; ++index)
            {
                const unsigned char character =
                    static_cast<unsigned char>(text[index]);
                if (character >= static_cast<unsigned char>('A') &&
                    character <= static_cast<unsigned char>('Z'))
                {
                    normalized.push_back(static_cast<char>(
                        character - static_cast<unsigned char>('A') +
                        static_cast<unsigned char>('a')));
                }
                else if ((character >= static_cast<unsigned char>('a') &&
                          character <= static_cast<unsigned char>('z')) ||
                         (character >= static_cast<unsigned char>('0') &&
                          character <= static_cast<unsigned char>('9')))
                {
                    normalized.push_back(static_cast<char>(character));
                }
                else
                {
                    normalized.clear();
                    return false;
                }
            }

            return !normalized.empty();
        }
    }

    [[nodiscard]] inline std::optional<UiSkin> ParseUiSkin(
        std::string_view text)
    {
        std::string normalized;
        if (!detail::NormalizeUiSkinName(text, normalized))
            return std::nullopt;

        if (normalized == "amp")
            return UiSkin::Amp;
        if (normalized == "ogg" || normalized == "og")
            return UiSkin::Og;

        return std::nullopt;
    }

    [[nodiscard]] inline bool TryParseUiSkin(
        std::string_view text,
        UiSkin& skin)
    {
        const std::optional<UiSkin> parsed = ParseUiSkin(text);
        if (!parsed)
            return false;
        skin = *parsed;
        return true;
    }
}
