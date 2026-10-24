#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace uvsr
{
    inline constexpr std::size_t MaxUiThemeSlots = 8u;

    struct UiThemeColor
    {
        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
        float a = 1.f;

        [[nodiscard]] float* data() { return &r; }
        [[nodiscard]] const float* data() const { return &r; }
    };

    struct UiThemeGradient
    {
        UiThemeColor top;
        UiThemeColor bottom;
    };

    struct UiThemeSlot
    {
        UiThemeGradient header;
        UiThemeGradient background;
        UiThemeColor text;
        UiThemeColor control;
        UiThemeColor knob;
        UiThemeColor border;
    };

    enum class UiThemeOrder : std::size_t
    {
        Settings = 0u,
        General,
        Visibility,
        Buffers,
        Statistics,
        Aliasing,
        Debug,
        Sky,
        Lights,
        Shadows,
        Count
    };

    [[nodiscard]] constexpr UiThemeColor UiThemeRgba(
        float r,
        float g,
        float b,
        float a = 1.f)
    {
        return { r, g, b, a };
    }

    [[nodiscard]] constexpr UiThemeSlot MakeUiThemeSlot(
        UiThemeColor headerTop,
        UiThemeColor headerBottom,
        UiThemeColor bodyTint)
    {
        return {
            { headerTop, headerBottom },
            {
                UiThemeRgba(
                    bodyTint.r * 0.16f,
                    bodyTint.g * 0.16f,
                    bodyTint.b * 0.16f,
                    0.90f),
                UiThemeRgba(
                    bodyTint.r * 0.075f,
                    bodyTint.g * 0.075f,
                    bodyTint.b * 0.075f,
                    0.94f)
            },
            UiThemeRgba(0.94f, 0.96f, 1.f, 1.f),
            UiThemeRgba(
                bodyTint.r * 0.30f,
                bodyTint.g * 0.30f,
                bodyTint.b * 0.30f,
                0.92f),
            UiThemeRgba(
                headerBottom.r,
                headerBottom.g,
                headerBottom.b,
                1.f),
            UiThemeRgba(
                headerBottom.r,
                headerBottom.g,
                headerBottom.b,
                0.42f)
        };
    }

    [[nodiscard]] constexpr std::array<UiThemeSlot, MaxUiThemeSlots>
        MakeDefaultUiThemeSlots()
    {
        return {
            MakeUiThemeSlot(
                UiThemeRgba(0.075f, 0.20f, 0.36f, 1.f),
                UiThemeRgba(0.12f, 0.34f, 0.58f, 1.f),
                UiThemeRgba(0.10f, 0.27f, 0.46f)),
            MakeUiThemeSlot(
                UiThemeRgba(0.02f, 0.35f, 0.43f, 1.f),
                UiThemeRgba(0.02f, 0.63f, 0.76f, 1.f),
                UiThemeRgba(0.02f, 0.42f, 0.50f)),
            MakeUiThemeSlot(
                UiThemeRgba(0.05f, 0.34f, 0.20f, 1.f),
                UiThemeRgba(0.08f, 0.62f, 0.34f, 1.f),
                UiThemeRgba(0.05f, 0.40f, 0.23f)),
            MakeUiThemeSlot(
                UiThemeRgba(0.43f, 0.34f, 0.03f, 1.f),
                UiThemeRgba(0.82f, 0.65f, 0.05f, 1.f),
                UiThemeRgba(0.48f, 0.39f, 0.04f)),
            MakeUiThemeSlot(
                UiThemeRgba(0.47f, 0.20f, 0.03f, 1.f),
                UiThemeRgba(0.92f, 0.39f, 0.05f, 1.f),
                UiThemeRgba(0.51f, 0.23f, 0.04f)),
            MakeUiThemeSlot(
                UiThemeRgba(0.43f, 0.07f, 0.09f, 1.f),
                UiThemeRgba(0.84f, 0.11f, 0.16f, 1.f),
                UiThemeRgba(0.47f, 0.08f, 0.11f)),
            MakeUiThemeSlot(
                UiThemeRgba(0.39f, 0.05f, 0.38f, 1.f),
                UiThemeRgba(0.75f, 0.10f, 0.74f, 1.f),
                UiThemeRgba(0.43f, 0.06f, 0.42f)),
            MakeUiThemeSlot(
                UiThemeRgba(0.25f, 0.09f, 0.46f, 1.f),
                UiThemeRgba(0.49f, 0.18f, 0.88f, 1.f),
                UiThemeRgba(0.29f, 0.11f, 0.51f))
        };
    }

    struct UiOrderedTheme
    {
        bool enabled = false;
        int activeSlotCount = static_cast<int>(MaxUiThemeSlots);
        int cycleStart = 0;
        int selectedSlot = 0;
        std::array<UiThemeSlot, MaxUiThemeSlots> slots =
            MakeDefaultUiThemeSlots();
    };

    [[nodiscard]] constexpr UiOrderedTheme MakeDefaultUiOrderedTheme()
    {
        return {};
    }

    [[nodiscard]] constexpr int ClampUiThemeSlotCount(int count)
    {
        return std::clamp(
            count,
            1,
            static_cast<int>(MaxUiThemeSlots));
    }

    [[nodiscard]] constexpr std::size_t ResolveUiThemeSlotIndex(
        UiThemeOrder order,
        const UiOrderedTheme& theme)
    {
        const int count = ClampUiThemeSlotCount(theme.activeSlotCount);
        const int start = ((theme.cycleStart % count) + count) % count;
        return (static_cast<std::size_t>(order) +
            static_cast<std::size_t>(start)) %
            static_cast<std::size_t>(count);
    }
}
