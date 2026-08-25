#ifndef UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_SHARED_H
#define UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_SHARED_H

#include "flashlight_shared.h"

#ifdef __cplusplus

#include <cstdint>

using FlashlightShadowFloat3 = FlashlightSharedFloat3;
using FlashlightShadowUint = std::uint32_t;
#define UVSR_FLASHLIGHT_SHADOW_INLINE inline

#else

#define FlashlightShadowFloat3 float3
#define FlashlightShadowUint uint
#define UVSR_FLASHLIGHT_SHADOW_INLINE

#endif

static const float RayTracedFlashlightMissHitDistance = 65504.0f;
static const float RayTracedFlashlightMinimumCosine = 1e-5f;
static const float RayTracedFlashlightMaximumRayBias = 0.1f;
static const FlashlightShadowUint
    RayTracedFlashlightFiniteEmitterSampleCount = 4u;
static const float RayTracedFlashlightGoldenRatioConjugate =
    0.6180339887498948482f;
static const float RayTracedFlashlightTwoPi =
    6.2831853071795864769f;

struct RayTracedFlashlightShadowSurface
{
    FlashlightShadowFloat3 rayOrigin;
    FlashlightShadowFloat3 receiverPosition;
    FlashlightShadowFloat3 geometricNormal;
    FlashlightShadowFloat3 shadingNormal;
    FlashlightShadowFloat3 viewDirection;
};

struct RayTracedFlashlightShadowLight
{
    FlashlightShadowFloat3 position;
    float rangeMeters;
    FlashlightShadowFloat3 direction;
    float emitterRadiusMeters;
    FlashlightBeamProfile beamProfile;
};

struct RayTracedFlashlightShadowRay
{
    FlashlightShadowFloat3 directionToLight;
    float tMax;
    float beamWeight;
    FlashlightShadowUint eligible;
};

struct RayTracedFlashlightShadowEncoding
{
    float visibility;
    float hitDistance;
};

UVSR_FLASHLIGHT_SHADOW_INLINE RayTracedFlashlightShadowRay
    ResolveRayTracedFlashlightShadowRay(
        RayTracedFlashlightShadowSurface surface,
        RayTracedFlashlightShadowLight light,
        float emitterSampleU,
        float emitterSampleV)
{
    RayTracedFlashlightShadowRay result;
    result.directionToLight = FlashlightSharedMakeFloat3(0.0f, 0.0f, 1.0f);
    result.tMax = 0.0f;
    result.beamWeight = 0.0f;
    result.eligible = 0u;

    if (!FlashlightSharedIsFinite3(surface.rayOrigin) ||
        !FlashlightSharedIsFinite3(surface.receiverPosition) ||
        !FlashlightSharedIsFinite3(surface.geometricNormal) ||
        !FlashlightSharedIsFinite3(surface.shadingNormal) ||
        !FlashlightSharedIsFinite3(surface.viewDirection) ||
        !FlashlightSharedIsFinite3(light.position) ||
        !FlashlightSharedIsFinite3(light.direction) ||
        !FlashlightSharedIsFinite(light.rangeMeters) ||
        !FlashlightSharedIsFinite(light.emitterRadiusMeters) ||
        !FlashlightSharedIsFinite(emitterSampleU) ||
        !FlashlightSharedIsFinite(emitterSampleV) ||
        !(light.rangeMeters > light.emitterRadiusMeters) ||
        light.emitterRadiusMeters < 0.0f ||
        !FlashlightBeamProfileIsValid(light.beamProfile))
    {
        return result;
    }

    const FlashlightShadowFloat3 lightToReceiver =
        FlashlightSharedSubtract(
            surface.receiverPosition,
            light.position);
    const float receiverDistanceSquared = FlashlightSharedDot(
        lightToReceiver,
        lightToReceiver);
    if (!(receiverDistanceSquared > 1e-12f) ||
        !FlashlightSharedIsFinite(receiverDistanceSquared))
    {
        return result;
    }
    const float inverseReceiverDistance = FlashlightSharedRsqrt(
        receiverDistanceSquared);
    const float receiverDistance = receiverDistanceSquared *
        inverseReceiverDistance;
    if (!(receiverDistance < light.rangeMeters))
        return result;

    const FlashlightShadowFloat3 directionFromLight =
        FlashlightSharedScale(lightToReceiver, inverseReceiverDistance);
    result.beamWeight = EvaluateFlashlightBeamProfile(
        light.beamProfile,
        light.direction,
        directionFromLight);
    if (!(result.beamWeight > 0.0f))
        return result;

    const FlashlightShadowFloat3 originToLight =
        FlashlightSharedSubtract(light.position, surface.rayOrigin);
    const float originDistanceSquared = FlashlightSharedDot(
        originToLight,
        originToLight);
    if (!(originDistanceSquared > 1e-12f) ||
        !FlashlightSharedIsFinite(originDistanceSquared))
    {
        return result;
    }
    const float inverseOriginDistance = FlashlightSharedRsqrt(
        originDistanceSquared);
    const float originDistance = originDistanceSquared *
        inverseOriginDistance;
    const FlashlightShadowFloat3 centerDirection =
        FlashlightSharedScale(originToLight, inverseOriginDistance);
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
        const float minimumCosine = FlashlightSharedSqrt(
            FlashlightSharedMax(
                1.0f - radiusRatio * radiusRatio,
                0.0f));
        const float sampleU = FlashlightSharedMin(
            FlashlightSharedSaturate(emitterSampleU),
            0.999999f);
        const float sampleV = FlashlightSharedFrac(emitterSampleV);
        const float cosTheta = 1.0f -
            sampleU * (1.0f - minimumCosine);
        const float sinThetaSquared = FlashlightSharedMax(
            1.0f - cosTheta * cosTheta,
            0.0f);
        const float sinTheta = FlashlightSharedSqrt(
            sinThetaSquared);
        const float phi = RayTracedFlashlightTwoPi * sampleV;

        const FlashlightShadowFloat3 referenceAxis =
            FlashlightSharedAbs(centerDirection.z) < 0.999f
            ? FlashlightSharedMakeFloat3(0.0f, 0.0f, 1.0f)
            : FlashlightSharedMakeFloat3(0.0f, 1.0f, 0.0f);
        FlashlightShadowFloat3 tangent = FlashlightSharedCross(
            referenceAxis,
            centerDirection);
        const float tangentLengthSquared = FlashlightSharedDot(
            tangent,
            tangent);
        if (!(tangentLengthSquared > 1e-12f) ||
            !FlashlightSharedIsFinite(tangentLengthSquared))
        {
            result.tMax = 0.0f;
            return result;
        }
        tangent = FlashlightSharedScale(
            tangent,
            FlashlightSharedRsqrt(tangentLengthSquared));
        const FlashlightShadowFloat3 bitangent =
            FlashlightSharedCross(centerDirection, tangent);
        const FlashlightShadowFloat3 capDirection = FlashlightSharedAdd(
            FlashlightSharedScale(
                tangent,
                FlashlightSharedCos(phi) * sinTheta),
            FlashlightSharedScale(
                bitangent,
                FlashlightSharedSin(phi) * sinTheta));
        result.directionToLight = FlashlightSharedAdd(
            FlashlightSharedScale(centerDirection, cosTheta),
            capDirection);

        const float centerProjection = originDistance * cosTheta;
        const float intersectionDiscriminant = FlashlightSharedMax(
            light.emitterRadiusMeters * light.emitterRadiusMeters -
                originDistanceSquared * sinThetaSquared,
            0.0f);
        result.tMax = centerProjection -
            FlashlightSharedSqrt(intersectionDiscriminant);
    }
    if (!(result.tMax > 0.0f) ||
        !FlashlightSharedIsFinite(result.tMax))
    {
        result.tMax = 0.0f;
        return result;
    }

    const float geometricNormalLengthSquared = FlashlightSharedDot(
        surface.geometricNormal,
        surface.geometricNormal);
    const float shadingNormalLengthSquared = FlashlightSharedDot(
        surface.shadingNormal,
        surface.shadingNormal);
    const float viewDirectionLengthSquared = FlashlightSharedDot(
        surface.viewDirection,
        surface.viewDirection);
    if (!(geometricNormalLengthSquared > 1e-12f) ||
        !(shadingNormalLengthSquared > 1e-12f) ||
        !(viewDirectionLengthSquared > 1e-12f) ||
        !FlashlightSharedIsFinite(geometricNormalLengthSquared) ||
        !FlashlightSharedIsFinite(shadingNormalLengthSquared) ||
        !FlashlightSharedIsFinite(viewDirectionLengthSquared))
    {
        result.tMax = 0.0f;
        return result;
    }

    const FlashlightShadowFloat3 unitGeometricNormal =
        FlashlightSharedScale(
            surface.geometricNormal,
            FlashlightSharedRsqrt(geometricNormalLengthSquared));
    const FlashlightShadowFloat3 unitShadingNormal =
        FlashlightSharedScale(
            surface.shadingNormal,
            FlashlightSharedRsqrt(shadingNormalLengthSquared));
    const FlashlightShadowFloat3 unitViewDirection =
        FlashlightSharedScale(
            surface.viewDirection,
            FlashlightSharedRsqrt(viewDirectionLengthSquared));
    const float geometricNoV = FlashlightSharedDot(
        unitGeometricNormal,
        unitViewDirection);
    const float geometricNoL = FlashlightSharedDot(
        unitGeometricNormal,
        result.directionToLight);
    const float shadingNoV = FlashlightSharedDot(
        unitShadingNormal,
        unitViewDirection);
    const float shadingNoL = FlashlightSharedDot(
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

UVSR_FLASHLIGHT_SHADOW_INLINE RayTracedFlashlightShadowEncoding
    ResolveRayTracedFlashlightShadowEncoding(
        FlashlightShadowUint eligible,
        FlashlightShadowUint committedTriangleHit,
        float committedRayT)
{
    RayTracedFlashlightShadowEncoding result;
    result.visibility = 1.0f;
    result.hitDistance = 0.0f;
    if (eligible == 0u)
        return result;

    if (committedTriangleHit == 0u)
    {
        result.hitDistance = RayTracedFlashlightMissHitDistance;
        return result;
    }

    if (!FlashlightSharedIsFinite(committedRayT) ||
        committedRayT < 0.0f)
    {
        // A malformed query result cannot safely darken the frame.
        result.hitDistance = RayTracedFlashlightMissHitDistance;
        return result;
    }

    result.visibility = 0.0f;
    result.hitDistance = FlashlightSharedMin(
        committedRayT,
        RayTracedFlashlightMissHitDistance);
    return result;
}

UVSR_FLASHLIGHT_SHADOW_INLINE RayTracedFlashlightShadowEncoding
    ResolveRayTracedFlashlightShadowAggregate(
        FlashlightShadowUint eligibleSampleCount,
        FlashlightShadowUint occludedSampleCount,
        float closestHitDistance)
{
    RayTracedFlashlightShadowEncoding result;
    result.visibility = 1.0f;
    result.hitDistance = 0.0f;
    if (eligibleSampleCount == 0u)
        return result;

    occludedSampleCount = occludedSampleCount < eligibleSampleCount
        ? occludedSampleCount
        : eligibleSampleCount;
    if (occludedSampleCount == 0u)
    {
        result.hitDistance = RayTracedFlashlightMissHitDistance;
        return result;
    }
    if (!FlashlightSharedIsFinite(closestHitDistance) ||
        closestHitDistance < 0.0f)
    {
        result.hitDistance = RayTracedFlashlightMissHitDistance;
        return result;
    }

    result.visibility = 1.0f -
        float(occludedSampleCount) / float(eligibleSampleCount);
    result.hitDistance = FlashlightSharedMin(
        closestHitDistance,
        RayTracedFlashlightMissHitDistance);
    return result;
}

#ifdef __cplusplus

namespace uvsr
{
    [[nodiscard]] constexpr bool
        IsRayTracedFlashlightReceiverSampleCountSupported(
            std::uint32_t sampleCount)
    {
        return sampleCount == 1u || sampleCount == 2u ||
            sampleCount == 4u || sampleCount == 8u ||
            sampleCount == 16u;
    }

    struct RayTracedFlashlightTextureShape
    {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::uint32_t depth = 0u;
        std::uint32_t arraySize = 0u;
        std::uint32_t mipLevels = 0u;
        std::uint32_t sampleCount = 0u;
        bool texture2D = false;
        bool shaderResource = false;
    };

    [[nodiscard]] constexpr bool
        IsRayTracedFlashlightTextureShapeValid(
            const RayTracedFlashlightTextureShape& shape)
    {
        return shape.texture2D &&
            shape.width > 0u &&
            shape.height > 0u &&
            shape.depth == 1u &&
            shape.arraySize == 1u &&
            shape.mipLevels > 0u &&
            IsRayTracedFlashlightReceiverSampleCountSupported(
                shape.sampleCount) &&
            shape.shaderResource;
    }

    [[nodiscard]] constexpr bool
        IsRayTracedFlashlightTextureShapeCompatible(
            const RayTracedFlashlightTextureShape& reference,
            const RayTracedFlashlightTextureShape& candidate)
    {
        return IsRayTracedFlashlightTextureShapeValid(reference) &&
            IsRayTracedFlashlightTextureShapeValid(candidate) &&
            reference.width == candidate.width &&
            reference.height == candidate.height &&
            reference.sampleCount == candidate.sampleCount;
    }
}

#else

#undef FlashlightShadowFloat3
#undef FlashlightShadowUint

#endif

#undef UVSR_FLASHLIGHT_SHADOW_INLINE

#endif // UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_SHARED_H
