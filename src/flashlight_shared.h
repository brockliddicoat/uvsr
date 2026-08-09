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

#ifdef __cplusplus

#include <algorithm>
#include <cmath>
#include <cstddef>

struct FlashlightSharedFloat3
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

using FlashlightSharedUint = unsigned int;

inline FlashlightSharedFloat3 FlashlightSharedMakeFloat3(
    float x,
    float y,
    float z)
{
    return { x, y, z };
}

inline FlashlightSharedFloat3 FlashlightSharedAdd(
    FlashlightSharedFloat3 left,
    FlashlightSharedFloat3 right)
{
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z
    };
}

inline FlashlightSharedFloat3 FlashlightSharedSubtract(
    FlashlightSharedFloat3 left,
    FlashlightSharedFloat3 right)
{
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z
    };
}

inline FlashlightSharedFloat3 FlashlightSharedScale(
    FlashlightSharedFloat3 value,
    float scale)
{
    return { value.x * scale, value.y * scale, value.z * scale };
}

inline float FlashlightSharedDot(
    FlashlightSharedFloat3 left,
    FlashlightSharedFloat3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline FlashlightSharedFloat3 FlashlightSharedCross(
    FlashlightSharedFloat3 left,
    FlashlightSharedFloat3 right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x
    };
}

inline bool FlashlightSharedIsFinite(float value)
{
    return std::isfinite(value);
}

inline bool FlashlightSharedIsFinite3(FlashlightSharedFloat3 value)
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

inline float FlashlightSharedSaturate(float value)
{
    return std::clamp(value, 0.f, 1.f);
}

inline float FlashlightSharedMax(float left, float right)
{
    return std::max(left, right);
}

inline float FlashlightSharedMin(float left, float right)
{
    return std::min(left, right);
}

inline float FlashlightSharedAbs(float value)
{
    return std::abs(value);
}

inline float FlashlightSharedPow(float value, float exponent)
{
    return std::pow(value, exponent);
}

inline float FlashlightSharedRsqrt(float value)
{
    return 1.f / std::sqrt(value);
}

inline float FlashlightSharedSqrt(float value)
{
    return std::sqrt(value);
}

inline float FlashlightSharedSin(float value)
{
    return std::sin(value);
}

inline float FlashlightSharedCos(float value)
{
    return std::cos(value);
}

inline float FlashlightSharedFrac(float value)
{
    return value - std::floor(value);
}

#define UVSR_FLASHLIGHT_SHARED_INLINE inline

#else

#define FlashlightSharedFloat3 float3
#define FlashlightSharedUint uint

float3 FlashlightSharedMakeFloat3(float x, float y, float z)
{
    return float3(x, y, z);
}

float3 FlashlightSharedAdd(float3 left, float3 right)
{
    return left + right;
}

float3 FlashlightSharedSubtract(float3 left, float3 right)
{
    return left - right;
}

float3 FlashlightSharedScale(float3 value, float scale)
{
    return value * scale;
}

float FlashlightSharedDot(float3 left, float3 right)
{
    return dot(left, right);
}

float3 FlashlightSharedCross(float3 left, float3 right)
{
    return cross(left, right);
}

bool FlashlightSharedIsFinite(float value)
{
    return isfinite(value);
}

bool FlashlightSharedIsFinite3(float3 value)
{
    return all(isfinite(value));
}

float FlashlightSharedSaturate(float value)
{
    return saturate(value);
}

float FlashlightSharedMax(float left, float right)
{
    return max(left, right);
}

float FlashlightSharedMin(float left, float right)
{
    return min(left, right);
}

float FlashlightSharedAbs(float value)
{
    return abs(value);
}

float FlashlightSharedPow(float value, float exponent)
{
    return pow(value, exponent);
}

float FlashlightSharedRsqrt(float value)
{
    return rsqrt(value);
}

float FlashlightSharedSqrt(float value)
{
    return sqrt(value);
}

float FlashlightSharedSin(float value)
{
    return sin(value);
}

float FlashlightSharedCos(float value)
{
    return cos(value);
}

float FlashlightSharedFrac(float value)
{
    return frac(value);
}

#define UVSR_FLASHLIGHT_SHARED_INLINE

#endif

UVSR_FLASHLIGHT_SHARED_INLINE bool FlashlightBeamProfileIsValid(
    FlashlightBeamProfile profile)
{
    const bool commonValuesValid =
        profile.active > 0.5f &&
        FlashlightSharedIsFinite(profile.beamRightX) &&
        FlashlightSharedIsFinite(profile.beamRightY) &&
        FlashlightSharedIsFinite(profile.beamRightZ) &&
        FlashlightSharedIsFinite(profile.shapeExponent) &&
        profile.shapeExponent >= UVSR_FLASHLIGHT_MIN_SHAPE_EXPONENT &&
        profile.shapeExponent <= UVSR_FLASHLIGHT_MAX_SHAPE_EXPONENT &&
        FlashlightSharedIsFinite(profile.spillInnerCosine) &&
        FlashlightSharedIsFinite(profile.spillOuterCosine) &&
        profile.spillInnerCosine >= -1.0f &&
        profile.spillInnerCosine <= 1.0f &&
        profile.spillOuterCosine >= -1.0f &&
        profile.spillOuterCosine <= 1.0f &&
        profile.spillInnerCosine >= profile.spillOuterCosine &&
        FlashlightSharedIsFinite(profile.spillWeight) &&
        profile.spillWeight >= 0.0f &&
        profile.spillWeight <= 1.0f &&
        FlashlightSharedIsFinite(profile.hotspotWeight) &&
        profile.hotspotWeight >= 0.0f &&
        profile.hotspotWeight <= 1.0f &&
        profile.spillWeight + profile.hotspotWeight > 0.0f &&
        profile.spillWeight + profile.hotspotWeight <= 1.000001f &&
        FlashlightSharedIsFinite(profile.emitterRadiusMeters) &&
        profile.emitterRadiusMeters >= 0.0f &&
        profile.emitterRadiusMeters <=
            UVSR_FLASHLIGHT_MAX_EMITTER_RADIUS_METERS;
    if (!commonValuesValid)
        return false;

    return profile.hotspotWeight <= 0.0f || (
        FlashlightSharedIsFinite(profile.hotspotInnerCosine) &&
        FlashlightSharedIsFinite(profile.hotspotOuterCosine) &&
        profile.hotspotInnerCosine >= -1.0f &&
        profile.hotspotInnerCosine <= 1.0f &&
        profile.hotspotOuterCosine >= -1.0f &&
        profile.hotspotOuterCosine <= 1.0f &&
        profile.hotspotInnerCosine >= profile.hotspotOuterCosine);
}

UVSR_FLASHLIGHT_SHARED_INLINE float FlashlightSmoothConeWeight(
    float shapedCosine,
    float innerCosine,
    float outerCosine)
{
    float weight = FlashlightSharedSaturate(
        (shapedCosine - outerCosine) /
        FlashlightSharedMax(innerCosine - outerCosine, 1e-6f));
    return weight * weight * (3.0f - 2.0f * weight);
}

UVSR_FLASHLIGHT_SHARED_INLINE float EvaluateFlashlightBeamProfile(
    FlashlightBeamProfile profile,
    FlashlightSharedFloat3 lightDirection,
    FlashlightSharedFloat3 directionFromLight)
{
    if (!FlashlightBeamProfileIsValid(profile))
        return 0.0f;

    float directionLengthSquared = FlashlightSharedDot(
        lightDirection, lightDirection);
    float rayLengthSquared = FlashlightSharedDot(
        directionFromLight, directionFromLight);
    if (!(directionLengthSquared > 1e-12f) ||
        !(rayLengthSquared > 1e-12f) ||
        !FlashlightSharedIsFinite(directionLengthSquared) ||
        !FlashlightSharedIsFinite(rayLengthSquared))
    {
        return 0.0f;
    }
    lightDirection = FlashlightSharedScale(
        lightDirection,
        FlashlightSharedRsqrt(directionLengthSquared));
    directionFromLight = FlashlightSharedScale(
        directionFromLight,
        FlashlightSharedRsqrt(rayLengthSquared));

    FlashlightSharedFloat3 beamRight = FlashlightSharedMakeFloat3(
        profile.beamRightX,
        profile.beamRightY,
        profile.beamRightZ);
    beamRight = FlashlightSharedSubtract(
        beamRight,
        FlashlightSharedScale(
            lightDirection,
            FlashlightSharedDot(beamRight, lightDirection)));
    float rightLengthSquared = FlashlightSharedDot(beamRight, beamRight);
    if (!(rightLengthSquared > 1e-12f) ||
        !FlashlightSharedIsFinite(rightLengthSquared))
    {
        return 0.0f;
    }
    beamRight = FlashlightSharedScale(
        beamRight,
        FlashlightSharedRsqrt(rightLengthSquared));
    const FlashlightSharedFloat3 beamUp = FlashlightSharedCross(
        beamRight,
        lightDirection);

    const float axialDistance = FlashlightSharedDot(
        directionFromLight,
        lightDirection);
    if (!(axialDistance > 1e-6f))
        return 0.0f;
    const float inverseAxialDistance = 1.0f / axialDistance;
    const float horizontalSlope = FlashlightSharedDot(
        directionFromLight,
        beamRight) * inverseAxialDistance;
    const float verticalSlope = FlashlightSharedDot(
        directionFromLight,
        beamUp) * inverseAxialDistance;
    if (!FlashlightSharedIsFinite(horizontalSlope) ||
        !FlashlightSharedIsFinite(verticalSlope))
    {
        return 0.0f;
    }

    const float poweredSlope = FlashlightSharedPow(
        FlashlightSharedAbs(horizontalSlope),
        profile.shapeExponent) + FlashlightSharedPow(
            FlashlightSharedAbs(verticalSlope),
            profile.shapeExponent);
    const float shapedSlope = FlashlightSharedPow(
        poweredSlope,
        1.0f / profile.shapeExponent);
    if (!FlashlightSharedIsFinite(shapedSlope))
        return 0.0f;
    const float shapedCosine = FlashlightSharedRsqrt(
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
    return FlashlightSharedSaturate(
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

#ifndef __cplusplus
#undef FlashlightSharedFloat3
#undef FlashlightSharedUint
#endif
#undef UVSR_FLASHLIGHT_SHARED_INLINE

#endif // UVSR_FLASHLIGHT_SHARED_H
