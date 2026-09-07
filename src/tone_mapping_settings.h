#pragma once

#include <array>
#include <string_view>

namespace uvsr
{
    enum class ToneMappingLut { None, Print2383, Portra400, Ektar100 };

    struct ToneMappingSettings
    {
        float exposure = 0.f;
        float contrast = 1.f;
        float saturation = 1.f;
        float warmth = 0.f;
        float tint = 0.f;
        float slope = 1.f;
        float power = 1.f;
        ToneMappingLut lut = ToneMappingLut::None;
        bool enabled = false;
    };

    inline constexpr ToneMappingSettings DefaultToneMappingSettings{};
    struct ToneMappingPreset
    {
        const char* label;
        ToneMappingSettings settings;
    };

    // retained grades from 5f43205ecfe00e31fd64af34cad0f031472a224c.
    inline constexpr std::array ToneMappingPresets = {
        ToneMappingPreset{ "Base", {} },
        ToneMappingPreset{ "Punchy", { .45f, 1.04f, 1.20f, 0.f, 0.f, 1.f, .98f } },
        ToneMappingPreset{ "Golden", { .40f, .98f, 1.14f, .18f, .03f, 1.01f, .97f } },
        ToneMappingPreset{ "Mix", { .40f, 1.f, 1.16f, .08f, .01f, 1.f, .98f } }
    };

    [[nodiscard]] constexpr std::array<float, 7> ToneMappingGradeValues(
        const ToneMappingSettings& settings)
    {
        return { settings.exposure, settings.contrast, settings.saturation,
            settings.warmth, settings.tint, settings.slope, settings.power };
    }

    [[nodiscard]] inline int FindToneMappingPreset(const ToneMappingSettings& settings)
    {
        for (int index = 0; index < int(ToneMappingPresets.size()); ++index)
            if (ToneMappingGradeValues(settings) == ToneMappingGradeValues(ToneMappingPresets[index].settings))
                return index;
        return -1;
    }

    [[nodiscard]] constexpr const char* ToneMappingLutFilename(ToneMappingLut lut)
    {
        switch (lut)
        {
        case ToneMappingLut::Print2383: return "UVSR_Kodak_2383_Print.cube";
        case ToneMappingLut::Portra400: return "UVSR_Kodak_Portra_400.cube";
        case ToneMappingLut::Ektar100: return "UVSR_Kodak_Ektar_100.cube";
        default: return "";
        }
    }
}
