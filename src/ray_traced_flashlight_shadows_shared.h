#ifndef UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_SHARED_H
#define UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_SHARED_H

#include "flashlight_shared.h"

#include "shader_math.h"

static const float RayTracedFlashlightMinimumCosine = 1e-5f;
static const float RayTracedFlashlightMaximumRayBias = 0.1f;
static const ShaderUint
    RayTracedFlashlightMaximumSampleCount = 64u;
static const float RayTracedFlashlightGoldenRatioConjugate =
    0.6180339887498948482f;
static const float RayTracedFlashlightTwoPi =
    6.2831853071795864769f;

struct RayTracedFlashlightShadowSurface
{
    ShaderFloat3 rayOrigin;
    ShaderFloat3 receiverPosition;
    ShaderFloat3 geometricNormal;
    ShaderFloat3 shadingNormal;
    ShaderFloat3 viewDirection;
};

struct RayTracedFlashlightShadowLight
{
    ShaderFloat3 position;
    float rangeMeters;
    ShaderFloat3 direction;
    float emitterRadiusMeters;
    FlashlightBeamProfile beamProfile;
};

struct RayTracedFlashlightShadowRay
{
    ShaderFloat3 directionToLight;
    float tMax;
    float beamWeight;
    ShaderUint eligible;
};

UVSR_SHADER_INLINE RayTracedFlashlightShadowRay
    ResolveRayTracedFlashlightShadowRay(
        RayTracedFlashlightShadowSurface surface,
        RayTracedFlashlightShadowLight light,
        float emitterSampleU,
        float emitterSampleV)
{
    RayTracedFlashlightShadowRay result;
    result.directionToLight = ShaderMakeFloat3(0.0f, 0.0f, 1.0f);
    result.tMax = 0.0f;
    result.beamWeight = 0.0f;
    result.eligible = 0u;

    if (!ShaderIsFinite3(surface.rayOrigin) ||
        !ShaderIsFinite3(surface.receiverPosition) ||
        !ShaderIsFinite3(surface.geometricNormal) ||
        !ShaderIsFinite3(surface.shadingNormal) ||
        !ShaderIsFinite3(surface.viewDirection) ||
        !ShaderIsFinite3(light.position) ||
        !ShaderIsFinite3(light.direction) ||
        !ShaderIsFinite(light.rangeMeters) ||
        !ShaderIsFinite(light.emitterRadiusMeters) ||
        !ShaderIsFinite(emitterSampleU) ||
        !ShaderIsFinite(emitterSampleV) ||
        !(light.rangeMeters > light.emitterRadiusMeters) ||
        light.emitterRadiusMeters < 0.0f ||
        !FlashlightBeamProfileIsValid(light.beamProfile))
    {
        return result;
    }

    const ShaderFloat3 lightToReceiver =
        ShaderSubtract3(
            surface.receiverPosition,
            light.position);
    const float receiverDistanceSquared = ShaderDot(
        lightToReceiver,
        lightToReceiver);
    if (!(receiverDistanceSquared > 1e-12f) ||
        !ShaderIsFinite(receiverDistanceSquared))
    {
        return result;
    }
    const float inverseReceiverDistance = ShaderRsqrt(
        receiverDistanceSquared);
    const float receiverDistance = receiverDistanceSquared *
        inverseReceiverDistance;
    if (!(receiverDistance < light.rangeMeters))
        return result;

    const ShaderFloat3 directionFromLight =
        ShaderScale3(lightToReceiver, inverseReceiverDistance);
    result.beamWeight = EvaluateFlashlightBeamProfile(
        light.beamProfile,
        light.direction,
        directionFromLight);
    if (!(result.beamWeight > 0.0f))
        return result;

    const ShaderFloat3 originToLight =
        ShaderSubtract3(light.position, surface.rayOrigin);
    const float originDistanceSquared = ShaderDot(
        originToLight,
        originToLight);
    if (!(originDistanceSquared > 1e-12f) ||
        !ShaderIsFinite(originDistanceSquared))
    {
        return result;
    }
    const float inverseOriginDistance = ShaderRsqrt(
        originDistanceSquared);
    const float originDistance = originDistanceSquared *
        inverseOriginDistance;
    const ShaderFloat3 centerDirection =
        ShaderScale3(originToLight, inverseOriginDistance);
    result.directionToLight = centerDirection;
    result.tMax = originDistance;

    if (light.emitterRadiusMeters > 0.0f)
    {
        if (!(originDistance > light.emitterRadiusMeters))
        {
            result.tMax = 0.0f;
            return result;
        }

        const float radiusRatio =
            light.emitterRadiusMeters * inverseOriginDistance;
        const float minimumCosine = ShaderSqrt(
            ShaderMax(
                1.0f - radiusRatio * radiusRatio,
                0.0f));
        const float sampleU = ShaderMin(
            ShaderSaturate(emitterSampleU),
            0.999999f);
        const float sampleV = ShaderFrac(emitterSampleV);
        const float cosTheta = 1.0f -
            sampleU * (1.0f - minimumCosine);
        const float sinThetaSquared = ShaderMax(
            1.0f - cosTheta * cosTheta,
            0.0f);
        const float sinTheta = ShaderSqrt(
            sinThetaSquared);
        const float phi = RayTracedFlashlightTwoPi * sampleV;

        const ShaderFloat3 referenceAxis =
            ShaderAbs(centerDirection.z) < 0.999f
            ? ShaderMakeFloat3(0.0f, 0.0f, 1.0f)
            : ShaderMakeFloat3(0.0f, 1.0f, 0.0f);
        ShaderFloat3 tangent = ShaderCross(
            referenceAxis,
            centerDirection);
        const float tangentLengthSquared = ShaderDot(
            tangent,
            tangent);
        if (!(tangentLengthSquared > 1e-12f) ||
            !ShaderIsFinite(tangentLengthSquared))
        {
            result.tMax = 0.0f;
            return result;
        }
        tangent = ShaderScale3(
            tangent,
            ShaderRsqrt(tangentLengthSquared));
        const ShaderFloat3 bitangent =
            ShaderCross(centerDirection, tangent);
        const ShaderFloat3 capDirection = ShaderAdd3(
            ShaderScale3(
                tangent,
                ShaderCos(phi) * sinTheta),
            ShaderScale3(
                bitangent,
                ShaderSin(phi) * sinTheta));
        result.directionToLight = ShaderAdd3(
            ShaderScale3(centerDirection, cosTheta),
            capDirection);

        const float centerProjection = originDistance * cosTheta;
        const float intersectionDiscriminant = ShaderMax(
            light.emitterRadiusMeters * light.emitterRadiusMeters -
                originDistanceSquared * sinThetaSquared,
            0.0f);
        result.tMax = centerProjection -
            ShaderSqrt(intersectionDiscriminant);
    }
    if (!(result.tMax > 0.0f) ||
        !ShaderIsFinite(result.tMax))
    {
        result.tMax = 0.0f;
        return result;
    }

    const float geometricNormalLengthSquared = ShaderDot(
        surface.geometricNormal,
        surface.geometricNormal);
    const float shadingNormalLengthSquared = ShaderDot(
        surface.shadingNormal,
        surface.shadingNormal);
    const float viewDirectionLengthSquared = ShaderDot(
        surface.viewDirection,
        surface.viewDirection);
    if (!(geometricNormalLengthSquared > 1e-12f) ||
        !(shadingNormalLengthSquared > 1e-12f) ||
        !(viewDirectionLengthSquared > 1e-12f) ||
        !ShaderIsFinite(geometricNormalLengthSquared) ||
        !ShaderIsFinite(shadingNormalLengthSquared) ||
        !ShaderIsFinite(viewDirectionLengthSquared))
    {
        result.tMax = 0.0f;
        return result;
    }

    const ShaderFloat3 unitGeometricNormal =
        ShaderScale3(
            surface.geometricNormal,
            ShaderRsqrt(geometricNormalLengthSquared));
    const ShaderFloat3 unitShadingNormal =
        ShaderScale3(
            surface.shadingNormal,
            ShaderRsqrt(shadingNormalLengthSquared));
    const ShaderFloat3 unitViewDirection =
        ShaderScale3(
            surface.viewDirection,
            ShaderRsqrt(viewDirectionLengthSquared));
    const float geometricNoV = ShaderDot(
        unitGeometricNormal,
        unitViewDirection);
    const float geometricNoL = ShaderDot(
        unitGeometricNormal,
        result.directionToLight);
    const float shadingNoV = ShaderDot(
        unitShadingNormal,
        unitViewDirection);
    const float shadingNoL = ShaderDot(
        unitShadingNormal,
        result.directionToLight);
    result.eligible = geometricNoV > RayTracedFlashlightMinimumCosine &&
        geometricNoL > RayTracedFlashlightMinimumCosine &&
        shadingNoV > RayTracedFlashlightMinimumCosine &&
        shadingNoL > RayTracedFlashlightMinimumCosine
        ? 1u
        : 0u;
    if (result.eligible == 0u)
        result.tMax = 0.0f;
    return result;
}

UVSR_SHADER_INLINE float ResolveRayTracedFlashlightShadowAggregate(
    ShaderUint eligibleSampleCount, ShaderUint occludedSampleCount)
{
    if (eligibleSampleCount == 0u)
        return 1.0f;
    occludedSampleCount = occludedSampleCount < eligibleSampleCount
        ? occludedSampleCount : eligibleSampleCount;
    return 1.0f - float(occludedSampleCount) / float(eligibleSampleCount);
}

#ifndef __cplusplus

#undef ShaderFloat3
#undef ShaderUint

#endif


#endif // UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_SHARED_H
