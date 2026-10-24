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
        RayTracedFlashlightShadowLight light)
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
        !(light.rangeMeters > light.emitterRadiusMeters) ||
        !(light.emitterRadiusMeters > 0.0f) ||
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
    result.directionToLight = FlashlightSharedScale(
        originToLight,
        inverseOriginDistance);
    result.tMax = originDistance - light.emitterRadiusMeters;
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

#ifdef __cplusplus

namespace uvsr
{
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
            shape.sampleCount == 1u &&
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
            reference.height == candidate.height;
    }
}

#else

#undef FlashlightShadowFloat3
#undef FlashlightShadowUint

#endif

#undef UVSR_FLASHLIGHT_SHADOW_INLINE

#endif // UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_SHARED_H
