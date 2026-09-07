#ifndef UVSR_FLASHLIGHT_SHARED_H
#define UVSR_FLASHLIGHT_SHARED_H

// First party transport for the camera flashlight's analytic two lobe beam.
// The profile is independent of Donut's LightConstants. Its companion binding
// selects the exact ordinary light that receives the custom beam response.
struct FlashlightBeamProfile
{
    float beamRightX;
    float beamRightY;
    float beamRightZ;
    float shapeExponent;

    float spillInnerCosine;
    float spillOuterCosine;
    float spillWeight;
    float hotspotWeight;

    float hotspotInnerCosine;
    float hotspotOuterCosine;
    float emitterRadiusMeters;
    float active;
};

struct FlashlightBeamProfileBinding
{
    FlashlightBeamProfile profile;
    int lightIndex;
    int padding0;
    int padding1;
    int padding2;
};

#define UVSR_FLASHLIGHT_MIN_SHAPE_EXPONENT 2.0f
#define UVSR_FLASHLIGHT_MAX_SHAPE_EXPONENT 16.0f
#define UVSR_FLASHLIGHT_MAX_EMITTER_RADIUS_METERS 0.176327f

#include "shader_math.h"

UVSR_SHADER_INLINE bool FlashlightBeamProfileIsValid(
    FlashlightBeamProfile profile)
{
    const bool commonValuesValid =
        profile.active > 0.5f &&
        ShaderIsFinite(profile.beamRightX) &&
        ShaderIsFinite(profile.beamRightY) &&
        ShaderIsFinite(profile.beamRightZ) &&
        ShaderIsFinite(profile.shapeExponent) &&
        profile.shapeExponent >= UVSR_FLASHLIGHT_MIN_SHAPE_EXPONENT &&
        profile.shapeExponent <= UVSR_FLASHLIGHT_MAX_SHAPE_EXPONENT &&
        ShaderIsFinite(profile.spillInnerCosine) &&
        ShaderIsFinite(profile.spillOuterCosine) &&
        profile.spillInnerCosine >= -1.0f &&
        profile.spillInnerCosine <= 1.0f &&
        profile.spillOuterCosine >= -1.0f &&
        profile.spillOuterCosine <= 1.0f &&
        profile.spillInnerCosine >= profile.spillOuterCosine &&
        ShaderIsFinite(profile.spillWeight) &&
        profile.spillWeight >= 0.0f &&
        profile.spillWeight <= 1.0f &&
        ShaderIsFinite(profile.hotspotWeight) &&
        profile.hotspotWeight >= 0.0f &&
        profile.hotspotWeight <= 1.0f &&
        profile.spillWeight + profile.hotspotWeight > 0.0f &&
        profile.spillWeight + profile.hotspotWeight <= 1.000001f &&
        ShaderIsFinite(profile.emitterRadiusMeters) &&
        profile.emitterRadiusMeters >= 0.0f &&
        profile.emitterRadiusMeters <=
            UVSR_FLASHLIGHT_MAX_EMITTER_RADIUS_METERS;
    if (!commonValuesValid)
        return false;

    return profile.hotspotWeight <= 0.0f || (
        ShaderIsFinite(profile.hotspotInnerCosine) &&
        ShaderIsFinite(profile.hotspotOuterCosine) &&
        profile.hotspotInnerCosine >= -1.0f &&
        profile.hotspotInnerCosine <= 1.0f &&
        profile.hotspotOuterCosine >= -1.0f &&
        profile.hotspotOuterCosine <= 1.0f &&
        profile.hotspotInnerCosine >= profile.hotspotOuterCosine);
}

UVSR_SHADER_INLINE float FlashlightSmoothConeWeight(
    float shapedCosine,
    float innerCosine,
    float outerCosine)
{
    float weight = ShaderSaturate(
        (shapedCosine - outerCosine) /
        ShaderMax(innerCosine - outerCosine, 1e-6f));
    return weight * weight * (3.0f - 2.0f * weight);
}

UVSR_SHADER_INLINE float EvaluateFlashlightBeamProfile(
    FlashlightBeamProfile profile,
    ShaderFloat3 lightDirection,
    ShaderFloat3 directionFromLight)
{
    if (!FlashlightBeamProfileIsValid(profile))
        return 0.0f;

    float directionLengthSquared = ShaderDot(
        lightDirection, lightDirection);
    float rayLengthSquared = ShaderDot(
        directionFromLight, directionFromLight);
    if (!(directionLengthSquared > 1e-12f) ||
        !(rayLengthSquared > 1e-12f) ||
        !ShaderIsFinite(directionLengthSquared) ||
        !ShaderIsFinite(rayLengthSquared))
    {
        return 0.0f;
    }
    lightDirection = ShaderScale3(
        lightDirection,
        ShaderRsqrt(directionLengthSquared));
    directionFromLight = ShaderScale3(
        directionFromLight,
        ShaderRsqrt(rayLengthSquared));

    ShaderFloat3 beamRight = ShaderMakeFloat3(
        profile.beamRightX,
        profile.beamRightY,
        profile.beamRightZ);
    beamRight = ShaderSubtract3(
        beamRight,
        ShaderScale3(
            lightDirection,
            ShaderDot(beamRight, lightDirection)));
    float rightLengthSquared = ShaderDot(beamRight, beamRight);
    if (!(rightLengthSquared > 1e-12f) ||
        !ShaderIsFinite(rightLengthSquared))
    {
        return 0.0f;
    }
    beamRight = ShaderScale3(
        beamRight,
        ShaderRsqrt(rightLengthSquared));
    const ShaderFloat3 beamUp = ShaderCross(
        beamRight,
        lightDirection);

    const float axialDistance = ShaderDot(
        directionFromLight,
        lightDirection);
    if (!(axialDistance > 1e-6f))
        return 0.0f;
    const float inverseAxialDistance = 1.0f / axialDistance;
    const float horizontalSlope = ShaderDot(
        directionFromLight,
        beamRight) * inverseAxialDistance;
    const float verticalSlope = ShaderDot(
        directionFromLight,
        beamUp) * inverseAxialDistance;
    if (!ShaderIsFinite(horizontalSlope) ||
        !ShaderIsFinite(verticalSlope))
    {
        return 0.0f;
    }

    const float poweredSlope = ShaderPow(
        ShaderAbs(horizontalSlope),
        profile.shapeExponent) + ShaderPow(
            ShaderAbs(verticalSlope),
            profile.shapeExponent);
    const float shapedSlope = ShaderPow(
        poweredSlope,
        1.0f / profile.shapeExponent);
    if (!ShaderIsFinite(shapedSlope))
        return 0.0f;
    const float shapedCosine = ShaderRsqrt(
        1.0f + shapedSlope * shapedSlope);

    const float spill = FlashlightSmoothConeWeight(
        shapedCosine,
        profile.spillInnerCosine,
        profile.spillOuterCosine);
    const float hotspot = profile.hotspotWeight > 0.0f
        ? FlashlightSmoothConeWeight(
            shapedCosine,
            profile.hotspotInnerCosine,
            profile.hotspotOuterCosine)
        : 0.0f;
    return ShaderSaturate(
        profile.spillWeight * spill +
        profile.hotspotWeight * hotspot);
}

#ifdef __cplusplus

static_assert(sizeof(FlashlightBeamProfile) == 48u,
    "Flashlight beam profile must occupy three constant registers.");
static_assert(offsetof(FlashlightBeamProfile, spillInnerCosine) == 16u);
static_assert(offsetof(FlashlightBeamProfile, hotspotInnerCosine) == 32u);
static_assert(sizeof(FlashlightBeamProfileBinding) == 64u,
    "Flashlight profile binding must preserve constant register alignment.");
static_assert(offsetof(FlashlightBeamProfileBinding, lightIndex) == 48u);

#endif


#endif // UVSR_FLASHLIGHT_SHARED_H
