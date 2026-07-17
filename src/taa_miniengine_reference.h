#pragma once

#include <array>
#include <cstdint>

namespace uvsr
{
    struct MiniEngineTaaJitterSample
    {
        float x;
        float y;
    };

    inline constexpr float MiniEngineTaaDefaultSharpness = 0.5f;
    inline constexpr float MiniEngineTaaMinimumSharpness = 0.0f;
    inline constexpr float MiniEngineTaaMaximumSharpness = 1.0f;
    inline constexpr float MiniEngineTaaSharpenThreshold = 0.001f;

    struct MiniEngineTaaSharpenWeights
    {
        float center;
        float lateral;
    };

    // MiniEngine's exact eight-position Halton 2/3 table. MiniEngine moves a
    // positive viewport origin and therefore stores samples in [0, 1), with
    // 0.5 documented as neutral. UVSR jitters its projection in signed pixel
    // units, so only the constant 0.5 center is removed; phase order and all
    // previous-minus-current deltas remain identical.
    inline constexpr std::array<MiniEngineTaaJitterSample, 8>
        MiniEngineTaaHalton23 = {{
            { 0.0f / 8.0f, 0.0f / 9.0f },
            { 4.0f / 8.0f, 3.0f / 9.0f },
            { 2.0f / 8.0f, 6.0f / 9.0f },
            { 6.0f / 8.0f, 1.0f / 9.0f },
            { 1.0f / 8.0f, 4.0f / 9.0f },
            { 5.0f / 8.0f, 7.0f / 9.0f },
            { 3.0f / 8.0f, 2.0f / 9.0f },
            { 7.0f / 8.0f, 5.0f / 9.0f }
        }};

    [[nodiscard]] inline constexpr MiniEngineTaaJitterSample
        GetMiniEngineTaaJitter(uint64_t frameIndex)
    {
        const MiniEngineTaaJitterSample sample =
            MiniEngineTaaHalton23[frameIndex % MiniEngineTaaHalton23.size()];
        return { sample.x - 0.5f, sample.y - 0.5f };
    }

    [[nodiscard]] inline constexpr uint64_t
        GetMiniEngineTaaHistoryBytes(uint32_t width, uint32_t height)
    {
        // Two RGBA16F confidence/color histories and two R32F device-depth
        // histories. This is exact logical texel payload before API alignment.
        return uint64_t(width) * uint64_t(height) * 24u;
    }

    [[nodiscard]] inline constexpr float ClampMiniEngineTaaSharpness(
        float sharpness)
    {
        return sharpness < MiniEngineTaaMinimumSharpness
            ? MiniEngineTaaMinimumSharpness
            : sharpness > MiniEngineTaaMaximumSharpness
                ? MiniEngineTaaMaximumSharpness
                : sharpness;
    }

    [[nodiscard]] inline constexpr bool ShouldSharpenMiniEngineTaa(
        bool enabled,
        float sharpness)
    {
        return enabled &&
            ClampMiniEngineTaaSharpness(sharpness) >=
                MiniEngineTaaSharpenThreshold;
    }

    [[nodiscard]] inline constexpr MiniEngineTaaSharpenWeights
        GetMiniEngineTaaSharpenWeights(float sharpness)
    {
        const float clamped = ClampMiniEngineTaaSharpness(sharpness);
        return { 1.f + clamped, 0.25f * clamped };
    }

    [[nodiscard]] inline constexpr bool IsMiniEngineTaaAvailable(
        bool enabled,
        bool pbrEnabled,
        bool deferredShading)
    {
        // UVSR's required XYZ+validity motion contract is produced by the
        // first-party deferred PBR G-buffer. Pretending another renderer mode
        // has the same contract would create a plausible-looking but incorrect
        // temporal path.
        return enabled && pbrEnabled && deferredShading;
    }
}
