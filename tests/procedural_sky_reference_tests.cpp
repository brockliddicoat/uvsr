#include "procedural_sky_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Procedural sky validation failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool Near(float actual, float expected, float tolerance = 1e-5f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    bool NearColor(
        const dm::float3& actual,
        const dm::float3& expected,
        float tolerance = 1e-5f)
    {
        return Near(actual.x, expected.x, tolerance) &&
            Near(actual.y, expected.y, tolerance) &&
            Near(actual.z, expected.z, tolerance);
    }

    bool NearPalette(
        const uvsr::ProceduralSkyPalette& actual,
        const uvsr::ProceduralSkyPalette& expected,
        float tolerance = 1e-5f)
    {
        return NearColor(actual.zenithColor, expected.zenithColor, tolerance) &&
            NearColor(actual.middleColor, expected.middleColor, tolerance) &&
            NearColor(actual.horizonColor, expected.horizonColor, tolerance) &&
            NearColor(actual.groundColor, expected.groundColor, tolerance) &&
            NearColor(
                actual.horizonAccentColor,
                expected.horizonAccentColor,
                tolerance) &&
            NearColor(actual.starColor, expected.starColor, tolerance) &&
            NearColor(
                actual.ambientTopColor,
                expected.ambientTopColor,
                tolerance) &&
            NearColor(
                actual.ambientBottomColor,
                expected.ambientBottomColor,
                tolerance) &&
            Near(actual.brightnessScale, expected.brightnessScale, tolerance) &&
            Near(
                actual.middleBandElevation,
                expected.middleBandElevation,
                tolerance) &&
            Near(actual.elevationCurve, expected.elevationCurve, tolerance) &&
            Near(
                actual.horizonAccentStrength,
                expected.horizonAccentStrength,
                tolerance) &&
            Near(
                actual.celestialSoftness,
                expected.celestialSoftness,
                tolerance) &&
            Near(
                actual.celestialRadianceCap,
                expected.celestialRadianceCap,
                tolerance) &&
            Near(
                actual.moonSurfaceAmount,
                expected.moonSurfaceAmount,
                tolerance) &&
            Near(
                actual.glowIntensityScale,
                expected.glowIntensityScale,
                tolerance) &&
            Near(actual.starIntensity, expected.starIntensity, tolerance) &&
            Near(
                actual.starDensityThreshold,
                expected.starDensityThreshold,
                tolerance) &&
            Near(actual.starCellScale, expected.starCellScale, tolerance);
    }

    float Luminance(const dm::float3& color)
    {
        return color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
    }

    float Length(const dm::float3& value)
    {
        return std::sqrt(
            value.x * value.x +
            value.y * value.y +
            value.z * value.z);
    }

    void RequireBandContract(
        const uvsr::ProceduralSkyPalette& palette,
        const char* message)
    {
        const uvsr::ProceduralSkyBandWeights horizon =
            uvsr::GetProceduralSkyBandWeights(0.f, palette);
        const float middleAnchor = std::pow(
            palette.middleBandElevation,
            1.f / palette.elevationCurve);
        const uvsr::ProceduralSkyBandWeights middle =
            uvsr::GetProceduralSkyBandWeights(middleAnchor, palette);
        const uvsr::ProceduralSkyBandWeights zenith =
            uvsr::GetProceduralSkyBandWeights(1.f, palette);

        Require(
            Near(horizon.horizon, 1.f) &&
                Near(horizon.middle, 0.f) &&
                Near(horizon.zenith, 0.f) &&
                Near(middle.horizon, 0.f) &&
                Near(middle.middle, 1.f) &&
                Near(middle.zenith, 0.f) &&
                Near(zenith.horizon, 0.f) &&
                Near(zenith.middle, 0.f) &&
                Near(zenith.zenith, 1.f),
            message);

        for (int sample = 0; sample <= 1000; ++sample)
        {
            const float elevation = static_cast<float>(sample) / 1000.f;
            const uvsr::ProceduralSkyBandWeights weights =
                uvsr::GetProceduralSkyBandWeights(elevation, palette);
            Require(
                weights.horizon >= 0.f && weights.horizon <= 1.f &&
                    weights.middle >= 0.f && weights.middle <= 1.f &&
                    weights.zenith >= 0.f && weights.zenith <= 1.f &&
                    Near(
                        weights.horizon + weights.middle + weights.zenith,
                        1.f,
                        2e-5f),
                message);
        }
    }
}

int main()
{
    const uvsr::ProceduralSkyPalette dayAnchor =
        uvsr::GetDayProceduralSkyPalette();
    const uvsr::ProceduralSkyPalette noon =
        uvsr::GetProceduralSkyPaletteForTime(12.f);
    Require(NearPalette(noon, dayAnchor),
        "noon must preserve the exact accepted daylight palette");
    Require(noon.starIntensity == 0.f &&
        Near(noon.moonSurfaceAmount, 0.f),
        "noon must contain neither stars nor lunar surface detail");
    Require(Luminance(noon.zenithColor) < Luminance(noon.middleColor) &&
        Luminance(noon.middleColor) < Luminance(noon.horizonColor),
        "day bands must brighten from zenith to horizon");
    Require(noon.horizonColor.z / noon.horizonColor.x > 2.f,
        "day horizon must remain clearly blue rather than gray");
    Require(noon.groundColor.z / noon.groundColor.x > 3.f,
        "day lower band must remain clearly blue rather than gray-brown");
    Require(NearColor(noon.ambientTopColor, dm::float3(0.17f, 0.37f, 0.65f)) &&
        NearColor(noon.ambientBottomColor, dm::float3(0.62f, 0.59f, 0.55f)),
        "noon ambient must preserve pre-experiment daylight parity");

    const uvsr::ProceduralSkyPalette deepNight =
        uvsr::GetDeepNightProceduralSkyPalette();
    const uvsr::ProceduralSkyPalette midnight =
        uvsr::GetProceduralSkyPaletteForTime(0.f);
    Require(NearPalette(midnight, deepNight),
        "midnight must land on the authored deep-night palette");
    Require(NearColor(midnight.zenithColor, dm::float3(0.0030f, 0.0034f, 0.0042f)) &&
        NearColor(midnight.middleColor, dm::float3(0.0065f, 0.0069f, 0.0075f)) &&
        NearColor(midnight.horizonColor, dm::float3(0.0136f, 0.0138f, 0.0142f)) &&
        NearColor(midnight.groundColor, dm::float3(0.0032f, 0.00315f, 0.0031f)),
        "midnight bands must remain dark and nearly neutral at every elevation");
    Require(midnight.zenithColor.z / midnight.zenithColor.x < 1.5f &&
        midnight.middleColor.z / midnight.middleColor.x < 1.2f &&
        midnight.horizonColor.z / midnight.horizonColor.x < 1.1f &&
        midnight.groundColor.z <= midnight.groundColor.x,
        "night chroma must not rebuild the rejected blue lower-sky cast");
    Require(NearColor(midnight.ambientTopColor, dm::float3(0.f)) &&
        NearColor(midnight.ambientBottomColor, dm::float3(0.f)),
        "midnight ambient must be absent instead of tinting materials blue");
    Require(midnight.starIntensity > 0.f &&
        Near(midnight.celestialSoftness, 0.04f) &&
        Near(midnight.celestialRadianceCap, 0.45f) &&
        Near(midnight.moonSurfaceAmount, 1.f),
        "midnight must retain stars and the detailed lunar rendering contract");
    Require(Luminance(midnight.zenithColor) < Luminance(midnight.middleColor) &&
        Luminance(midnight.middleColor) < Luminance(midnight.horizonColor),
        "midnight bands must brighten from zenith to horizon");

    const uvsr::ProceduralSkyPalette sunrise =
        uvsr::GetProceduralSkyPaletteForTime(6.f);
    const uvsr::ProceduralSkyPalette sunset =
        uvsr::GetProceduralSkyPaletteForTime(18.f);
    Require(NearPalette(sunrise, uvsr::GetSunriseProceduralSkyPalette()) &&
        NearPalette(sunset, uvsr::GetSunsetProceduralSkyPalette()),
        "sunrise and sunset must land on their exact authored palette anchors");
    Require(sunrise.zenithColor.z > sunrise.zenithColor.y &&
        sunrise.zenithColor.y > sunrise.zenithColor.x &&
        sunset.zenithColor.z > sunset.zenithColor.y &&
        sunset.zenithColor.y > sunset.zenithColor.x,
        "twilight must retain a cool blue upper band around the warm horizon");
    Require(sunrise.middleColor.x >
            std::max(sunrise.middleColor.y, sunrise.middleColor.z) &&
        sunset.middleColor.x >
            std::max(sunset.middleColor.y, sunset.middleColor.z) &&
        sunrise.horizonColor.x > sunrise.horizonColor.y &&
        sunrise.horizonColor.y > sunrise.horizonColor.z &&
        sunset.horizonColor.x > sunset.horizonColor.y &&
        sunset.horizonColor.y > sunset.horizonColor.z,
        "twilight middle and horizon bands must remain distinctly warm");
    Require(Luminance(sunrise.zenithColor) < Luminance(sunrise.middleColor) &&
        Luminance(sunrise.middleColor) < Luminance(sunrise.horizonColor) &&
        Luminance(sunset.zenithColor) < Luminance(sunset.middleColor) &&
        Luminance(sunset.middleColor) < Luminance(sunset.horizonColor),
        "twilight must preserve a readable three-band luminance progression");
    Require(!NearColor(sunrise.middleColor, sunset.middleColor) &&
        sunrise.horizonColor.y > sunset.horizonColor.y &&
        sunrise.horizonColor.z > sunset.horizonColor.z,
        "sunrise must stay peachier and sunset must stay separately redder");

    const uvsr::ProceduralSkyPalette evening =
        uvsr::GetProceduralSkyPaletteForTime(20.f);
    const uvsr::ProceduralSkyPalette lateNight =
        uvsr::GetProceduralSkyPaletteForTime(22.f);
    const uvsr::ProceduralSkyTimeState sunsetState =
        uvsr::GetProceduralSkyTimeState(18.f);
    const uvsr::ProceduralSkyTimeState eveningState =
        uvsr::GetProceduralSkyTimeState(20.f);
    const uvsr::ProceduralSkyTimeState lateNightState =
        uvsr::GetProceduralSkyTimeState(22.f);
    const uvsr::ProceduralSkyTimeState midnightState =
        uvsr::GetProceduralSkyTimeState(0.f);
    Require(sunsetState.nightDepth < eveningState.nightDepth &&
        eveningState.nightDepth < lateNightState.nightDepth &&
        lateNightState.nightDepth < midnightState.nightDepth &&
        Near(midnightState.nightDepth, 1.f),
        "night depth must increase continuously toward midnight");
    Require(Luminance(sunset.zenithColor) > Luminance(evening.zenithColor) &&
        Luminance(evening.zenithColor) > Luminance(lateNight.zenithColor) &&
        Luminance(lateNight.zenithColor) > Luminance(midnight.zenithColor) &&
        Luminance(sunset.horizonColor) > Luminance(evening.horizonColor) &&
        Luminance(evening.horizonColor) > Luminance(lateNight.horizonColor) &&
        Luminance(lateNight.horizonColor) > Luminance(midnight.horizonColor),
        "the complete nighttime palette must darken progressively toward midnight");
    Require(sunset.starIntensity < evening.starIntensity &&
        evening.starIntensity < lateNight.starIntensity &&
        lateNight.starIntensity < midnight.starIntensity,
        "stars must fade in progressively as night deepens");
    uvsr::ProceduralSkySettings brightnessSettings;
    const float sunsetBrightness = uvsr::GetEffectiveProceduralSkyBrightness(
        brightnessSettings, sunset, sunsetState);
    const float eveningBrightness = uvsr::GetEffectiveProceduralSkyBrightness(
        brightnessSettings, evening, eveningState);
    const float lateNightBrightness =
        uvsr::GetEffectiveProceduralSkyBrightness(
            brightnessSettings, lateNight, lateNightState);
    const float midnightBrightness = uvsr::GetEffectiveProceduralSkyBrightness(
        brightnessSettings, midnight, midnightState);
    Require(sunsetBrightness > eveningBrightness &&
        eveningBrightness > lateNightBrightness &&
        lateNightBrightness > midnightBrightness &&
        midnightBrightness < brightnessSettings.brightness * 0.30f,
        "rendered sky energy must deepen strongly and continuously toward midnight");

    Require(Near(uvsr::WrapProceduralSkyTime(24.f), 0.f) &&
        Near(uvsr::WrapProceduralSkyTime(25.f), 1.f) &&
        Near(uvsr::WrapProceduralSkyTime(-1.f), 23.f) &&
        Near(uvsr::WrapProceduralSkyTime(-24.f), 0.f),
        "time wrapping must be periodic in both directions");
    Require(Near(uvsr::WrapProceduralSkyTime(
            std::numeric_limits<float>::infinity()), 12.f) &&
        Near(uvsr::WrapProceduralSkyTime(
            std::numeric_limits<float>::quiet_NaN()), 12.f),
        "nonfinite time must fall back safely to noon");
    Require(NearPalette(
            uvsr::GetProceduralSkyPaletteForTime(24.f), midnight) &&
        NearPalette(
            uvsr::GetProceduralSkyPaletteForTime(-24.f), midnight) &&
        NearPalette(
            uvsr::GetProceduralSkyPaletteForTime(
                std::numeric_limits<float>::infinity()),
            noon),
        "palette lookup must honor wrapping and nonfinite fallback");
    uvsr::ProceduralSkySettings paletteSettings;
    paletteSettings.timeOfDay = 18.f;
    Require(NearPalette(
            uvsr::GetProceduralSkyPalette(paletteSettings), sunset),
        "settings-based palette lookup must use the same time contract");

    const uvsr::ProceduralSkyTimeState dawnMoon =
        uvsr::GetProceduralSkyTimeState(5.999f);
    const uvsr::ProceduralSkyTimeState dawnSun =
        uvsr::GetProceduralSkyTimeState(6.f);
    const uvsr::ProceduralSkyTimeState duskSun =
        uvsr::GetProceduralSkyTimeState(17.999f);
    const uvsr::ProceduralSkyTimeState duskMoon =
        uvsr::GetProceduralSkyTimeState(18.f);
    Require(dawnMoon.moonActive && !dawnSun.moonActive &&
        !duskSun.moonActive && duskMoon.moonActive,
        "the active body must switch only at the two horizon crossings");
    Require(dawnMoon.celestialVisibility < 1e-4f &&
        Near(dawnSun.celestialVisibility, 0.f, 1e-4f) &&
        duskSun.celestialVisibility < 1e-4f &&
        Near(duskMoon.celestialVisibility, 0.f, 1e-4f),
        "sun and moon switches must occur only at effectively zero light energy");
    Require(Near(midnightState.celestialVisibility, 1.f) &&
        Near(uvsr::GetProceduralSkyTimeState(12.f).celestialVisibility, 1.f),
        "sun and moon must reach full visibility near culmination");
    const uvsr::ProceduralSkyTimeState noonOrbitState =
        uvsr::GetProceduralSkyTimeState(12.f);
    constexpr float InverseSqrtTwo = 0.70710678118654752440f;
    const dm::float3 leftHorizon(
        -InverseSqrtTwo, 0.f, -InverseSqrtTwo);
    const dm::float3 rightHorizon(
        InverseSqrtTwo, 0.f, InverseSqrtTwo);
    const dm::float3 worldZenith(0.f, 1.f, 0.f);
    Require(Near(dawnSun.celestialArcProgress, 0.f) &&
        NearColor(dawnSun.directionToLight, leftHorizon) &&
        dawnMoon.celestialArcProgress > 0.999f &&
        dawnMoon.directionToLight.x > 0.707f &&
        dawnMoon.directionToLight.z > 0.707f &&
        duskSun.celestialArcProgress > 0.999f &&
        duskSun.directionToLight.x > 0.707f &&
        duskSun.directionToLight.z > 0.707f &&
        Near(duskMoon.celestialArcProgress, 0.f) &&
        NearColor(duskMoon.directionToLight, leftHorizon),
        "each body must finish at the right world horizon before the replacement starts at the left");
    Require(Near(midnightState.celestialArcProgress, 0.5f) &&
        NearColor(midnightState.directionToLight, worldZenith) &&
        NearColor(noonOrbitState.directionToLight, worldZenith) &&
        midnightState.moonActive &&
        Near(midnightState.celestialVisibility, 1.f),
        "time zero and noon must place the active body at the world zenith");
    Require(NearColor(
            uvsr::GetProceduralSkyCelestialDirectionToLight(0.25f),
            dm::float3(-0.5f, InverseSqrtTwo, -0.5f)) &&
        NearColor(
            uvsr::GetProceduralSkyCelestialDirectionToLight(0.75f),
            dm::float3(0.5f, InverseSqrtTwo, 0.5f)) &&
        NearColor(
            uvsr::GetProceduralSkyCelestialDirectionToLight(1.f),
            rightHorizon),
        "the fixed orbit must pass through the exact upper-semicircle anchors");
    const dm::float3 midnightPhotonDirection =
        uvsr::GetProceduralSkyCelestialPhotonDirection(midnightState);
    Require(NearColor(midnightPhotonDirection, dm::float3(0.f, -1.f, 0.f)) &&
        Near(dm::dot(
            midnightPhotonDirection,
            midnightState.directionToLight), -1.f),
        "Donut photon direction must remain opposite the visible celestial body");

    float previousRightProjection = -std::numeric_limits<float>::infinity();
    for (int sample = 0; sample <= 720; ++sample)
    {
        const float progress = static_cast<float>(sample) / 720.f;
        const dm::float3 direction =
            uvsr::GetProceduralSkyCelestialDirectionToLight(progress);
        const float planeDistance = InverseSqrtTwo *
            (direction.x - direction.z);
        const float rightProjection = InverseSqrtTwo *
            (direction.x + direction.z);
        Require(std::isfinite(direction.x) &&
            std::isfinite(direction.y) &&
            std::isfinite(direction.z) &&
            Near(Length(direction), 1.f, 2e-5f) &&
            direction.y >= -1e-6f &&
            (sample == 0 || sample == 720 || direction.y > 0.f) &&
            std::abs(planeDistance) < 1e-5f &&
            rightProjection >= previousRightProjection - 1e-6f,
            "the celestial orbit must stay unit length in one fixed upper-world plane");
        previousRightProjection = rightProjection;
    }

    for (int sample = 0; sample < 24 * 60; ++sample)
    {
        const float time = static_cast<float>(sample) / 60.f;
        const uvsr::ProceduralSkyTimeState state =
            uvsr::GetProceduralSkyTimeState(time);
        const bool expectedMoon = time < 6.f || time >= 18.f;
        Require(state.moonActive == expectedMoon &&
            std::isfinite(state.directionToLight.x) &&
            std::isfinite(state.directionToLight.y) &&
            std::isfinite(state.directionToLight.z) &&
            state.celestialArcProgress >= 0.f &&
            state.celestialArcProgress <= 1.f &&
            Near(Length(state.directionToLight), 1.f, 2e-5f) &&
            state.directionToLight.y >= -1e-6f &&
            Near(state.directionToLight.x, state.directionToLight.z) &&
            state.celestialVisibility >= 0.f &&
            state.celestialVisibility <= 1.f &&
            state.nightDepth >= 0.f && state.nightDepth <= 1.f,
            "the active celestial world orbit must remain finite and above the horizon");
    }

    uvsr::ProceduralSkySettings skySettings;
    Require(Near(skySettings.timeOfDay, 12.f) && skySettings.enableCelestials,
        "time must default to noon with all celestials enabled");
    const uvsr::ProceduralSkyTimeState noonState =
        uvsr::GetProceduralSkyTimeState(12.f);
    Require(Near(uvsr::GetEffectiveCelestialIrradiance(
            skySettings, noonState, 3.f), 3.f) &&
        Near(uvsr::GetEffectiveCelestialIrradiance(
            skySettings, midnightState, 3.f), skySettings.moonIrradiance) &&
        skySettings.moonIrradiance > 0.f &&
        Near(uvsr::GetEffectiveProceduralSkyStarIntensity(
            skySettings, midnight), midnight.starIntensity),
        "enabled celestials must preserve sun, time-zero moon, and star energy");
    skySettings.enableCelestials = false;
    Require(Near(uvsr::GetEffectiveCelestialIrradiance(
            skySettings, noonState, 3.f), 0.f) &&
        Near(uvsr::GetEffectiveCelestialIrradiance(
            skySettings, midnightState, 3.f), 0.f) &&
        Near(uvsr::GetEffectiveProceduralSkyStarIntensity(
            skySettings, midnight), 0.f),
        "disabling celestials must remove sun, moon, and stars together");
    skySettings.enableCelestials = true;
    Require(Near(uvsr::GetNightMoonIrradiance(skySettings), 0.008f) &&
        Near(uvsr::GetNightMoonRadianceScale(0.008f), 1.f),
        "default lunar irradiance must preserve lighting, disk, and halo energy");
    skySettings.moonIrradiance = 0.004f;
    Require(Near(uvsr::GetNightMoonIrradiance(skySettings), 0.004f) &&
        Near(uvsr::GetNightMoonRadianceScale(0.004f), 0.5f),
        "halving lunar irradiance must halve moon lighting and visibility");
    skySettings.moonIrradiance = -1.f;
    Require(Near(uvsr::GetNightMoonIrradiance(skySettings), 0.f) &&
        Near(uvsr::GetNightMoonRadianceScale(-1.f), 0.f),
        "negative lunar irradiance must clamp safely to zero");
    skySettings.moonIrradiance = 0.016f;
    Require(Near(uvsr::GetNightMoonIrradiance(skySettings), 0.016f) &&
        Near(uvsr::GetNightMoonRadianceScale(0.016f), 2.f),
        "doubling lunar irradiance must double moon lighting and visibility");
    const uvsr::CelestialLightPreset moon =
        uvsr::GetNightMoonLightPreset();
    Require(Near(moon.irradiance, 0.008f) && Near(moon.angularSize, 1.f) &&
        NearColor(moon.color, dm::float3(1.f)),
        "the moon preset must remain dim, one degree wide, and neutral");

    uvsr::ProceduralSkySettings dayHalo;
    uvsr::ProceduralSkySettings nightHalo;
    dayHalo.timeOfDay = 12.f;
    nightHalo.timeOfDay = 0.f;
    Require(Near(
            uvsr::GetProceduralSkyHaloOuterRadiusDegrees(dayHalo),
            uvsr::GetProceduralSkyHaloOuterRadiusDegrees(nightHalo)) &&
        Near(uvsr::GetProceduralSkyHaloOuterRadiusDegrees(dayHalo), 5.f),
        "sun and moon halos must retain the same five-degree outer radius");
    nightHalo.glowSize = -1.f;
    Require(Near(
            uvsr::GetProceduralSkyHaloOuterRadiusDegrees(nightHalo), 0.f),
        "negative halo radius must clamp safely to zero");
    nightHalo.glowSize = 180.f;
    Require(Near(
            uvsr::GetProceduralSkyHaloOuterRadiusDegrees(nightHalo), 90.f),
        "halo radius must clamp safely to a hemisphere");

    RequireBandContract(noon,
        "day band weights must remain normalized with exact anchors");
    RequireBandContract(sunrise,
        "sunrise band weights must remain normalized with exact anchors");
    RequireBandContract(sunset,
        "sunset band weights must remain normalized with exact anchors");
    RequireBandContract(midnight,
        "midnight band weights must remain normalized with exact anchors");
    const uvsr::ProceduralSkyBandWeights lowElevation =
        uvsr::GetProceduralSkyBandWeights(0.05f, noon);
    Require(lowElevation.horizon < 1.f && lowElevation.middle > 0.f,
        "day gradient must begin above the horizon without a flat plateau");

    const float repeatedSelection =
        uvsr::GetProceduralSkyStarSelection(-17, 29);
    Require(Near(
            repeatedSelection,
            uvsr::GetProceduralSkyStarSelection(-17, 29),
            0.f) &&
        repeatedSelection >= 0.f && repeatedSelection < 1.f,
        "star identity and hash range must remain deterministic");
    int selectedStars = 0;
    float minimumClusterDensity =
        std::numeric_limits<float>::max();
    float maximumClusterDensity = 0.f;
    float minimumStarRadius = std::numeric_limits<float>::max();
    float maximumStarRadius = 0.f;
    int smallStarCells = 0;
    int largeStarCells = 0;
    constexpr int GridWidth = 128;
    constexpr int GridHeight = 64;
    for (int y = -GridHeight / 2; y < GridHeight / 2; ++y)
    {
        for (int x = -GridWidth / 2; x < GridWidth / 2; ++x)
        {
            const float clusterDensity =
                uvsr::GetProceduralSkyStarClusterDensityMultiplier(x, y);
            minimumClusterDensity = std::min(
                minimumClusterDensity, clusterDensity);
            maximumClusterDensity = std::max(
                maximumClusterDensity, clusterDensity);
            const float starRadius =
                uvsr::GetProceduralSkyStarRadius(x, y);
            minimumStarRadius = std::min(minimumStarRadius, starRadius);
            maximumStarRadius = std::max(maximumStarRadius, starRadius);
            if (starRadius < 0.05f)
                ++smallStarCells;
            if (starRadius > 0.14f)
                ++largeStarCells;

            const float localThreshold =
                uvsr::GetProceduralSkyStarDensityThreshold(
                    midnight.starDensityThreshold, x, y);
            if (uvsr::GetProceduralSkyStarSelection(x, y) >
                localThreshold)
            {
                ++selectedStars;
            }
        }
    }
    Require(selectedStars > 0 &&
        selectedStars < GridWidth * GridHeight / 10,
        "the deterministic midnight star field must remain visible and sparse");
    Require(minimumClusterDensity < 0.5f &&
        maximumClusterDensity > 2.0f &&
        Near(
            uvsr::GetProceduralSkyStarClusterDensityMultiplier(-17, 29),
            uvsr::GetProceduralSkyStarClusterDensityMultiplier(-17, 29),
            0.f),
        "world-anchored cluster density must span sparse gaps and gathered regions deterministically");
    Require(minimumStarRadius < 0.03f &&
        maximumStarRadius > 0.15f &&
        smallStarCells > largeStarCells * 4 &&
        largeStarCells > 0 &&
        Near(
            uvsr::GetProceduralSkyStarRadius(-17, 29),
            uvsr::GetProceduralSkyStarRadius(-17, 29),
            0.f),
        "star sizes must remain deterministic with many tiny points and a rare large tail");

    const float referenceCoverage = uvsr::GetProceduralSkyStarAreaCoverage(
        UVSR_SKY_STAR_RADIUS_COMMON_MAX, 0.17f);
    Require(referenceCoverage > 0.f && referenceCoverage < 0.65f,
        "common subpixel star coverage must stay bounded");
    const float largeStarCoverage = uvsr::GetProceduralSkyStarAreaCoverage(
        UVSR_SKY_STAR_RADIUS_MAX, 0.17f);
    Require(largeStarCoverage > referenceCoverage &&
        largeStarCoverage <= 1.f,
        "rare large stars must resolve more area without exceeding full coverage");
    const float referencePeak = largeStarCoverage * midnight.starIntensity *
        midnightBrightness * 1.18f * 1.45f;
    Require(referencePeak < 0.75f,
        "default 1080p star peak must remain temporally tractable");
    Require(Near(uvsr::GetProceduralSkyStarAreaCoverage(
            UVSR_SKY_STAR_RADIUS_MAX, 0.02f), 1.f),
        "resolved stars must retain full area coverage");

    std::cout << "UVSR procedural sky reference validation passed\n";
    return EXIT_SUCCESS;
}
