/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include "procedural_sky_shared.h"

#include <donut/core/math/math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace uvsr
{
    struct ProceduralSkySettings
    {
        float brightness = 0.1f;
        float horizonSize = 30.f;
        // Glow Size is the outer angular radius of the halo, not a distance
        // added beyond the celestial disk. That invariant gives the sun and
        // moon identical halo footprints even though their disks differ.
        float glowSize = 5.f;
        float glowIntensity = 0.1f;
        float glowSharpness = 4.f;
        float maxLightRadiance = 100.f;
        float timeOfDay = 12.f;
        bool enableCelestials = true;
        // The active directional light's Irradiance control edits this
        // physical moon value at night while authored daylight remains
        // stored on the scene light.
        float moonIrradiance = 0.008f;
    };

    struct ProceduralSkyPalette
    {
        dm::float3 zenithColor;
        dm::float3 middleColor;
        dm::float3 horizonColor;
        dm::float3 groundColor;
        dm::float3 horizonAccentColor;
        dm::float3 starColor;
        dm::float3 ambientTopColor;
        dm::float3 ambientBottomColor;

        float brightnessScale = 1.f;
        float middleBandElevation = 0.40f;
        float elevationCurve = 0.72f;
        float horizonAccentStrength = 0.f;
        float celestialSoftness = 0.08f;
        float celestialRadianceCap = 100.f;
        float moonSurfaceAmount = 0.f;
        float glowIntensityScale = 1.f;
        float starIntensity = 0.f;
        float starDensityThreshold = 1.f;
        float starCellScale = 320.f;
    };

    struct CelestialLightPreset
    {
        dm::float3 color;
        float irradiance = 0.f;
        float angularSize = 0.f;
    };

    struct ProceduralSkyBandWeights
    {
        float horizon = 0.f;
        float middle = 0.f;
        float zenith = 0.f;
    };

    struct ProceduralSkyTimeState
    {
        float timeOfDay = 12.f;
        float solarElevation = 1.f;
        float sunriseAmount = 0.f;
        float sunsetAmount = 0.f;
        float nightDepth = 0.f;
        float celestialVisibility = 1.f;
        float celestialArcProgress = 0.5f;
        bool moonActive = false;
        dm::float3 directionToLight = dm::float3(0.f, 1.f, 0.f);
    };

    inline float SmoothSkyStep(float edge0, float edge1, float value) noexcept
    {
        if (!(edge1 > edge0))
            return value >= edge1 ? 1.f : 0.f;

        const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    inline float WrapProceduralSkyTime(float timeOfDay) noexcept
    {
        if (!std::isfinite(timeOfDay))
            return 12.f;

        float wrapped = std::fmod(timeOfDay, 24.f);
        if (wrapped < 0.f)
            wrapped += 24.f;
        return wrapped;
    }

    inline dm::float3 GetProceduralSkyCelestialDirectionToLight(
        float celestialArcProgress) noexcept
    {
        constexpr float Pi = 3.14159265358979323846f;
        constexpr float InverseSqrtTwo = 0.70710678118654752440f;
        const float progress = std::isfinite(celestialArcProgress)
            ? std::clamp(celestialArcProgress, 0.f, 1.f)
            : 0.5f;
        const float arcAngle = Pi * progress;
        const float horizontalAmount = -std::cos(arcAngle);

        // UVSR and glTF use +Y as world up. The diagonal horizontal basis is
        // the default scene framing camera's fixed screen-right direction, so
        // the body begins on the left horizon and finishes on the right when
        // the scene first opens. It deliberately never reads the live camera:
        // looking or moving elsewhere only changes how this immutable world
        // orbit is observed.
        return dm::float3(
            InverseSqrtTwo * horizontalAmount,
            std::max(std::sin(arcAngle), 0.f),
            InverseSqrtTwo * horizontalAmount);
    }

    inline dm::float3 GetProceduralSkyCelestialPhotonDirection(
        const ProceduralSkyTimeState& state) noexcept
    {
        // Donut directional lights store the direction photons travel, which
        // is the inverse of the world ray from the scene toward the body.
        return -state.directionToLight;
    }

    inline ProceduralSkyTimeState GetProceduralSkyTimeState(
        float timeOfDay) noexcept
    {
        constexpr float TwoPi = 6.28318530717958647692f;

        ProceduralSkyTimeState state;
        state.timeOfDay = WrapProceduralSkyTime(timeOfDay);
        const float phase = (state.timeOfDay - 6.f) * (TwoPi / 24.f);
        const float phaseSine = std::sin(phase);
        state.solarElevation = phaseSine;
        state.moonActive = state.timeOfDay < 6.f || state.timeOfDay >= 18.f;
        if (state.moonActive)
        {
            state.celestialArcProgress = state.timeOfDay >= 18.f
                ? (state.timeOfDay - 18.f) / 12.f
                : (state.timeOfDay + 6.f) / 12.f;
        }
        else
        {
            state.celestialArcProgress =
                (state.timeOfDay - 6.f) / 12.f;
        }
        state.celestialArcProgress = std::clamp(
            state.celestialArcProgress, 0.f, 1.f);
        state.directionToLight =
            GetProceduralSkyCelestialDirectionToLight(
                state.celestialArcProgress);
        const float arcElevation = state.directionToLight.y;
        state.celestialVisibility = SmoothSkyStep(
            0.f, 0.18f, arcElevation);
        state.nightDepth = state.moonActive
            ? SmoothSkyStep(0.f, 1.f, -state.solarElevation)
            : 0.f;

        if (state.timeOfDay >= 4.f && state.timeOfDay < 6.f)
        {
            state.sunriseAmount = SmoothSkyStep(
                4.f, 6.f, state.timeOfDay);
        }
        else if (state.timeOfDay >= 6.f && state.timeOfDay < 8.f)
        {
            state.sunriseAmount = 1.f - SmoothSkyStep(
                6.f, 8.f, state.timeOfDay);
        }

        if (state.timeOfDay >= 16.f && state.timeOfDay < 18.f)
        {
            state.sunsetAmount = SmoothSkyStep(
                16.f, 18.f, state.timeOfDay);
        }
        else if (state.timeOfDay >= 18.f && state.timeOfDay < 20.f)
        {
            state.sunsetAmount = 1.f - SmoothSkyStep(
                18.f, 20.f, state.timeOfDay);
        }

        return state;
    }

    inline ProceduralSkyTimeState GetProceduralSkyTimeState(
        const ProceduralSkySettings& settings) noexcept
    {
        return GetProceduralSkyTimeState(settings.timeOfDay);
    }

    inline ProceduralSkyPalette GetDayProceduralSkyPalette() noexcept
    {
        ProceduralSkyPalette palette;
        // Three independent upper-hemisphere anchors replace Donut's one
        // upper color. Blue lower anchors keep the bottom band atmospheric
        // instead of allowing the former gray-brown ground blend to show.
        palette.zenithColor = dm::float3(0.075f, 0.22f, 0.54f);
        palette.middleColor = dm::float3(0.25f, 0.55f, 0.91f);
        palette.horizonColor = dm::float3(0.42f, 0.72f, 1.08f);
        palette.groundColor = dm::float3(0.20f, 0.38f, 0.67f);
        palette.horizonAccentColor = dm::float3(1.10f, 0.64f, 0.32f);
        palette.starColor = dm::float3(0.f);
        // Keep scene illumination identical to UVSR's pre-experiment Donut
        // defaults at noon. Time changes the palette around this exact anchor.
        palette.ambientTopColor = dm::float3(0.17f, 0.37f, 0.65f);
        palette.ambientBottomColor = dm::float3(0.62f, 0.59f, 0.55f);
        palette.brightnessScale = 1.f;
        palette.middleBandElevation = 0.40f;
        palette.elevationCurve = 0.72f;
        palette.horizonAccentStrength = 0.14f;
        palette.celestialSoftness = 0.08f;
        palette.celestialRadianceCap = 100.f;
        palette.moonSurfaceAmount = 0.f;
        palette.glowIntensityScale = 1.f;
        palette.starIntensity = 0.f;
        palette.starDensityThreshold = 1.f;
        palette.starCellScale = 360.f;
        return palette;
    }

    inline ProceduralSkyPalette GetDeepNightProceduralSkyPalette() noexcept
    {
        ProceduralSkyPalette palette;
        // Deep night is neutral charcoal with only a trace of cool separation
        // aloft. Ambient stays exactly absent; these values affect only the
        // visible backdrop and cannot cast a tint onto scene materials.
        palette.zenithColor = dm::float3(0.0030f, 0.0034f, 0.0042f);
        palette.middleColor = dm::float3(0.0065f, 0.0069f, 0.0075f);
        palette.horizonColor = dm::float3(0.0136f, 0.0138f, 0.0142f);
        palette.groundColor = dm::float3(0.0032f, 0.00315f, 0.0031f);
        palette.horizonAccentColor = dm::float3(0.0192f, 0.0195f, 0.0200f);
        palette.starColor = dm::float3(0.92f, 0.94f, 1.0f);
        palette.ambientTopColor = dm::float3(0.f);
        palette.ambientBottomColor = dm::float3(0.f);
        palette.brightnessScale = 0.9f;
        palette.middleBandElevation = 0.38f;
        palette.elevationCurve = 0.76f;
        palette.horizonAccentStrength = 0.08f;
        palette.celestialSoftness = 0.04f;
        palette.celestialRadianceCap = 0.45f;
        palette.moonSurfaceAmount = 1.f;
        palette.glowIntensityScale = 0.65f;
        palette.starIntensity = 9.0f;
        palette.starDensityThreshold = 0.990f;
        palette.starCellScale = 360.f;
        return palette;
    }

    inline ProceduralSkyPalette GetEveningNightProceduralSkyPalette() noexcept
    {
        ProceduralSkyPalette palette = GetDeepNightProceduralSkyPalette();
        palette.zenithColor = dm::float3(0.0080f, 0.0090f, 0.0115f);
        palette.middleColor = dm::float3(0.0180f, 0.0195f, 0.0220f);
        palette.horizonColor = dm::float3(0.0300f, 0.0310f, 0.0330f);
        palette.groundColor = dm::float3(0.0062f, 0.0061f, 0.0060f);
        palette.horizonAccentColor = dm::float3(0.040f, 0.041f, 0.044f);
        palette.horizonAccentStrength = 0.10f;
        palette.starIntensity = 5.0f;
        palette.starDensityThreshold = 0.993f;
        return palette;
    }

    inline ProceduralSkyPalette GetSunriseProceduralSkyPalette() noexcept
    {
        ProceduralSkyPalette palette = GetDayProceduralSkyPalette();
        // A cool upper band contains the warmth. Peach in the middle relates
        // the blue zenith to the pale golden horizon instead of forming three
        // disconnected stripes.
        palette.zenithColor = dm::float3(0.1170f, 0.2086f, 0.3916f);
        palette.middleColor = dm::float3(0.6724f, 0.2542f, 0.2384f);
        palette.horizonColor = dm::float3(1.0000f, 0.6308f, 0.3515f);
        palette.groundColor = dm::float3(0.30f, 0.14f, 0.18f);
        palette.horizonAccentColor = dm::float3(2.20f, 1.08f, 0.38f);
        palette.starColor = dm::float3(0.72f, 0.82f, 1.0f);
        palette.ambientTopColor = dm::float3(0.045f, 0.060f, 0.085f);
        palette.ambientBottomColor = dm::float3(0.070f, 0.055f, 0.045f);
        palette.middleBandElevation = 0.44f;
        palette.elevationCurve = 0.66f;
        palette.horizonAccentStrength = 0.42f;
        palette.glowIntensityScale = 1.25f;
        return palette;
    }

    inline ProceduralSkyPalette GetSunsetProceduralSkyPalette() noexcept
    {
        ProceduralSkyPalette palette = GetDayProceduralSkyPalette();
        // Sunset is deeper and redder than sunrise, but retains a blue upper
        // anchor so warmth stays concentrated around the horizon and sun.
        palette.zenithColor = dm::float3(0.0513f, 0.0953f, 0.2086f);
        palette.middleColor = dm::float3(0.5841f, 0.1384f, 0.1683f);
        palette.horizonColor = dm::float3(1.0000f, 0.4020f, 0.0976f);
        palette.groundColor = dm::float3(0.26f, 0.075f, 0.13f);
        palette.horizonAccentColor = dm::float3(2.40f, 0.68f, 0.16f);
        palette.starColor = dm::float3(0.72f, 0.82f, 1.0f);
        palette.ambientTopColor = dm::float3(0.035f, 0.045f, 0.070f);
        palette.ambientBottomColor = dm::float3(0.060f, 0.045f, 0.038f);
        palette.middleBandElevation = 0.43f;
        palette.elevationCurve = 0.64f;
        palette.horizonAccentStrength = 0.50f;
        palette.glowIntensityScale = 1.40f;
        return palette;
    }

    inline float LerpSkyValue(float lower, float upper, float amount) noexcept
    {
        return lower + (upper - lower) * amount;
    }

    inline dm::float3 LerpSkyColor(
        const dm::float3& lower,
        const dm::float3& upper,
        float amount) noexcept
    {
        return lower + (upper - lower) * amount;
    }

    inline ProceduralSkyPalette LerpProceduralSkyPalette(
        const ProceduralSkyPalette& lower,
        const ProceduralSkyPalette& upper,
        float amount) noexcept
    {
        const float t = std::clamp(amount, 0.f, 1.f);
        ProceduralSkyPalette palette;
        palette.zenithColor = LerpSkyColor(lower.zenithColor, upper.zenithColor, t);
        palette.middleColor = LerpSkyColor(lower.middleColor, upper.middleColor, t);
        palette.horizonColor = LerpSkyColor(lower.horizonColor, upper.horizonColor, t);
        palette.groundColor = LerpSkyColor(lower.groundColor, upper.groundColor, t);
        palette.horizonAccentColor = LerpSkyColor(
            lower.horizonAccentColor, upper.horizonAccentColor, t);
        palette.starColor = LerpSkyColor(lower.starColor, upper.starColor, t);
        palette.ambientTopColor = LerpSkyColor(
            lower.ambientTopColor, upper.ambientTopColor, t);
        palette.ambientBottomColor = LerpSkyColor(
            lower.ambientBottomColor, upper.ambientBottomColor, t);
        palette.brightnessScale = LerpSkyValue(
            lower.brightnessScale, upper.brightnessScale, t);
        palette.middleBandElevation = LerpSkyValue(
            lower.middleBandElevation, upper.middleBandElevation, t);
        palette.elevationCurve = LerpSkyValue(
            lower.elevationCurve, upper.elevationCurve, t);
        palette.horizonAccentStrength = LerpSkyValue(
            lower.horizonAccentStrength, upper.horizonAccentStrength, t);
        palette.celestialSoftness = LerpSkyValue(
            lower.celestialSoftness, upper.celestialSoftness, t);
        palette.celestialRadianceCap = LerpSkyValue(
            lower.celestialRadianceCap, upper.celestialRadianceCap, t);
        palette.moonSurfaceAmount = LerpSkyValue(
            lower.moonSurfaceAmount, upper.moonSurfaceAmount, t);
        palette.glowIntensityScale = LerpSkyValue(
            lower.glowIntensityScale, upper.glowIntensityScale, t);
        palette.starIntensity = LerpSkyValue(
            lower.starIntensity, upper.starIntensity, t);
        palette.starDensityThreshold = LerpSkyValue(
            lower.starDensityThreshold, upper.starDensityThreshold, t);
        palette.starCellScale = LerpSkyValue(
            lower.starCellScale, upper.starCellScale, t);
        return palette;
    }

    inline ProceduralSkyPalette GetProceduralSkyPaletteForTime(
        float timeOfDay) noexcept
    {
        const float time = WrapProceduralSkyTime(timeOfDay);
        const ProceduralSkyPalette day = GetDayProceduralSkyPalette();
        const ProceduralSkyPalette sunrise = GetSunriseProceduralSkyPalette();
        const ProceduralSkyPalette sunset = GetSunsetProceduralSkyPalette();
        const ProceduralSkyPalette evening =
            GetEveningNightProceduralSkyPalette();
        const ProceduralSkyPalette deepNight =
            GetDeepNightProceduralSkyPalette();

        if (time < 4.f)
            return LerpProceduralSkyPalette(
                deepNight, evening, SmoothSkyStep(0.f, 4.f, time));
        if (time < 6.f)
            return LerpProceduralSkyPalette(
                evening, sunrise, SmoothSkyStep(4.f, 6.f, time));
        if (time < 8.f)
            return LerpProceduralSkyPalette(
                sunrise, day, SmoothSkyStep(6.f, 8.f, time));
        if (time < 16.f)
            return day;
        if (time < 18.f)
            return LerpProceduralSkyPalette(
                day, sunset, SmoothSkyStep(16.f, 18.f, time));
        if (time < 20.f)
            return LerpProceduralSkyPalette(
                sunset, evening, SmoothSkyStep(18.f, 20.f, time));
        return LerpProceduralSkyPalette(
            evening, deepNight, SmoothSkyStep(20.f, 24.f, time));
    }

    inline ProceduralSkyPalette GetProceduralSkyPalette(
        const ProceduralSkySettings& settings) noexcept
    {
        return GetProceduralSkyPaletteForTime(settings.timeOfDay);
    }

    inline float GetEffectiveProceduralSkyBrightness(
        const ProceduralSkySettings& settings,
        const ProceduralSkyPalette& palette,
        const ProceduralSkyTimeState& timeState) noexcept
    {
        // Midnight receives a deliberate additional depth attenuation. The
        // palette already loses color energy through evening; this multiplier
        // makes the complete rendered sky roughly three times darker at the
        // deepest point without changing moon irradiance or tinting materials.
        constexpr float MidnightBrightnessFraction = 0.33f;
        const float nightScale = LerpSkyValue(
            1.f,
            MidnightBrightnessFraction,
            std::clamp(timeState.nightDepth, 0.f, 1.f));
        return std::clamp(settings.brightness, 0.f, 1.f) *
            palette.brightnessScale * nightScale;
    }

    inline dm::float3 GetProceduralSunLightTint(
        const ProceduralSkyTimeState& timeState) noexcept
    {
        const dm::float3 neutral(1.f);
        const dm::float3 sunriseTint(1.f, 0.58f, 0.32f);
        const dm::float3 sunsetTint(1.f, 0.40f, 0.18f);
        return neutral +
            (sunriseTint - neutral) * timeState.sunriseAmount +
            (sunsetTint - neutral) * timeState.sunsetAmount;
    }

    inline CelestialLightPreset GetNightMoonLightPreset() noexcept
    {
        // Direct illumination stays deliberately dim and neutral. The sky pass
        // renders a separate art-realistic one-degree lunar surface: close to
        // the sun's apparent scale, but still about eighteen pixels across at
        // the default 60-degree/1080p view so its markings remain readable.
        return {
            dm::float3(1.f),
            0.008f,
            1.f
        };
    }

    inline float GetNightMoonIrradiance(
        const ProceduralSkySettings& settings) noexcept
    {
        return std::max(settings.moonIrradiance, 0.f);
    }

    inline float GetNightMoonRadianceScale(
        float effectiveMoonIrradiance) noexcept
    {
        const float referenceIrradiance =
            GetNightMoonLightPreset().irradiance;
        return referenceIrradiance > 0.f
            ? std::max(effectiveMoonIrradiance, 0.f) / referenceIrradiance
            : 0.f;
    }

    inline float GetEffectiveCelestialIrradiance(
        const ProceduralSkySettings& settings,
        const ProceduralSkyTimeState& timeState,
        float authoredSunIrradiance) noexcept
    {
        if (!settings.enableCelestials)
            return 0.f;

        const float baseIrradiance = timeState.moonActive
            ? GetNightMoonIrradiance(settings)
            : std::max(authoredSunIrradiance, 0.f);
        return baseIrradiance * std::clamp(
            timeState.celestialVisibility, 0.f, 1.f);
    }

    inline float GetEffectiveProceduralSkyStarIntensity(
        const ProceduralSkySettings& settings,
        const ProceduralSkyPalette& palette) noexcept
    {
        return settings.enableCelestials
            ? std::max(palette.starIntensity, 0.f)
            : 0.f;
    }

    inline float GetProceduralSkyHaloOuterRadiusDegrees(
        const ProceduralSkySettings& settings) noexcept
    {
        return std::clamp(settings.glowSize, 0.f, 90.f);
    }

    inline ProceduralSkyBandWeights GetProceduralSkyBandWeights(
        float normalizedElevation,
        const ProceduralSkyPalette& palette) noexcept
    {
        const float elevation = std::clamp(normalizedElevation, 0.f, 1.f);
        const float elevationCurve = std::max(palette.elevationCurve, 0.05f);
        const float shapedElevation = std::pow(elevation, elevationCurve);
        const float middleElevation = std::clamp(
            palette.middleBandElevation, 0.01f, 0.99f);
        ProceduralSkyBandWeights weights;
        if (shapedElevation <= middleElevation)
        {
            const float horizonToMiddle = SmoothSkyStep(
                0.f, middleElevation, shapedElevation);
            weights.horizon = 1.f - horizonToMiddle;
            weights.middle = horizonToMiddle;
        }
        else
        {
            const float middleToZenith = SmoothSkyStep(
                middleElevation, 1.f, shapedElevation);
            weights.middle = 1.f - middleToZenith;
            weights.zenith = middleToZenith;
        }
        return weights;
    }

    inline float GetProceduralSkyStarAreaCoverage(
        float radius,
        float pixelFootprint) noexcept
    {
        const float safeRadius = std::max(radius, 0.f);
        const float safeFootprint = std::max(pixelFootprint, 1e-4f);
        return std::clamp(
            (safeRadius * safeRadius) /
                (safeFootprint * safeFootprint),
            0.f, 1.f);
    }

    inline uint32_t HashProceduralSkyCell(
        int32_t cellX,
        int32_t cellY,
        uint32_t seed = UVSR_SKY_STAR_SEED) noexcept
    {
        uint32_t value = static_cast<uint32_t>(cellX) * 0x8da6b343u;
        value ^= static_cast<uint32_t>(cellY) * 0xd8163841u;
        value ^= seed * 0xcb1ab31fu;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    inline float ProceduralSkyHash01(uint32_t value) noexcept
    {
        return static_cast<float>(value & UVSR_SKY_STAR_HASH_MASK) *
            static_cast<float>(UVSR_SKY_STAR_HASH_SCALE);
    }

    inline float GetProceduralSkyStarSelection(
        int32_t cellX,
        int32_t cellY) noexcept
    {
        return ProceduralSkyHash01(HashProceduralSkyCell(cellX, cellY));
    }

    inline float GetProceduralSkyStarValueNoise(
        float cellX,
        float cellY,
        float cellScale,
        uint32_t seed) noexcept
    {
        const float safeScale = std::max(cellScale, 1.f);
        const float sampleX = (cellX + 0.5f) / safeScale;
        const float sampleY = (cellY + 0.5f) / safeScale;
        const int32_t latticeX = static_cast<int32_t>(std::floor(sampleX));
        const int32_t latticeY = static_cast<int32_t>(std::floor(sampleY));
        float blendX = sampleX - static_cast<float>(latticeX);
        float blendY = sampleY - static_cast<float>(latticeY);
        blendX = blendX * blendX * (3.f - 2.f * blendX);
        blendY = blendY * blendY * (3.f - 2.f * blendY);

        const float value00 = ProceduralSkyHash01(
            HashProceduralSkyCell(latticeX, latticeY, seed));
        const float value10 = ProceduralSkyHash01(
            HashProceduralSkyCell(latticeX + 1, latticeY, seed));
        const float value01 = ProceduralSkyHash01(
            HashProceduralSkyCell(latticeX, latticeY + 1, seed));
        const float value11 = ProceduralSkyHash01(
            HashProceduralSkyCell(latticeX + 1, latticeY + 1, seed));
        const float lower = LerpSkyValue(value00, value10, blendX);
        const float upper = LerpSkyValue(value01, value11, blendX);
        return LerpSkyValue(lower, upper, blendY);
    }

    inline float GetProceduralSkyStarClusterDensityMultiplier(
        int32_t cellX,
        int32_t cellY) noexcept
    {
        // A broad octave gathers stars into loose constellational regions;
        // a weaker detail octave breaks their edges up. Both sample integer
        // world-direction cells, keeping the pattern deterministic and stable.
        const float broad = GetProceduralSkyStarValueNoise(
            static_cast<float>(cellX),
            static_cast<float>(cellY),
            UVSR_SKY_STAR_CLUSTER_CELL_SCALE,
            UVSR_SKY_STAR_SEED_CLUSTER);
        const float detail = GetProceduralSkyStarValueNoise(
            static_cast<float>(cellX),
            static_cast<float>(cellY),
            UVSR_SKY_STAR_CLUSTER_DETAIL_SCALE,
            UVSR_SKY_STAR_SEED_CLUSTER_DETAIL);
        const float clusterNoise = broad * 0.72f + detail * 0.28f;
        const float clusterAmount = SmoothSkyStep(
            UVSR_SKY_STAR_CLUSTER_LOW,
            UVSR_SKY_STAR_CLUSTER_HIGH,
            clusterNoise);
        return LerpSkyValue(
            UVSR_SKY_STAR_CLUSTER_DENSITY_MIN,
            UVSR_SKY_STAR_CLUSTER_DENSITY_MAX,
            clusterAmount);
    }

    inline float GetProceduralSkyStarDensityThreshold(
        float baseThreshold,
        int32_t cellX,
        int32_t cellY) noexcept
    {
        const float baseProbability = 1.f -
            std::clamp(baseThreshold, 0.f, 1.f);
        const float clusteredProbability = std::clamp(
            baseProbability * GetProceduralSkyStarClusterDensityMultiplier(
                cellX, cellY),
            0.f,
            1.f);
        return 1.f - clusteredProbability;
    }

    inline float GetProceduralSkyStarRadius(
        int32_t cellX,
        int32_t cellY) noexcept
    {
        const float sizeRandom = ProceduralSkyHash01(
            HashProceduralSkyCell(
                cellX, cellY, UVSR_SKY_STAR_SEED_SIZE));
        const float commonProfile = sizeRandom * sizeRandom;
        const float commonRadius = LerpSkyValue(
            UVSR_SKY_STAR_RADIUS_MIN,
            UVSR_SKY_STAR_RADIUS_COMMON_MAX,
            commonProfile);
        const float largeAmount = SmoothSkyStep(
            UVSR_SKY_STAR_LARGE_START, 1.f, sizeRandom);
        return LerpSkyValue(
            commonRadius, UVSR_SKY_STAR_RADIUS_MAX, largeAmount);
    }
}
