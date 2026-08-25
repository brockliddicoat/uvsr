#ifndef UVSR_RAY_ORIGIN_CONTRACT_H
#define UVSR_RAY_ORIGIN_CONTRACT_H

#include "pbr_surface_light_contract.h"

#ifdef __cplusplus

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

using RayOriginFloat3 = PbrContractFloat3;
using RayOriginUint = std::uint32_t;

inline RayOriginUint RayOriginAsUint(float value) noexcept
{
    RayOriginUint bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float RayOriginAsFloat(RayOriginUint bits) noexcept
{
    float value = 0.f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline float RayOriginSaturate(float value) noexcept
{
    return std::clamp(value, 0.f, 1.f);
}

inline float RayOriginAbs(float value) noexcept
{
    return std::abs(value);
}

inline RayOriginFloat3 RayOriginMakeFloat3(
    float x,
    float y,
    float z) noexcept
{
    return { x, y, z };
}

inline RayOriginFloat3 RayOriginAdd(
    RayOriginFloat3 left,
    RayOriginFloat3 right) noexcept
{
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z
    };
}

#define UVSR_RAY_ORIGIN_INLINE inline

#else

#define RayOriginFloat3 float3
#define RayOriginUint uint

uint RayOriginAsUint(float value)
{
    return asuint(value);
}

float RayOriginAsFloat(uint bits)
{
    return asfloat(bits);
}

float RayOriginSaturate(float value)
{
    return saturate(value);
}

float RayOriginAbs(float value)
{
    return abs(value);
}

float3 RayOriginMakeFloat3(float x, float y, float z)
{
    return float3(x, y, z);
}

float3 RayOriginAdd(float3 left, float3 right)
{
    return left + right;
}

#define UVSR_RAY_ORIGIN_INLINE

#endif

UVSR_RAY_ORIGIN_INLINE RayOriginFloat3 RayOriginOrientGeometricNormal(
    RayOriginFloat3 geometricNormal,
    RayOriginFloat3 viewDirection)
{
    RayOriginFloat3 safeNormal = PbrSafeNormalize(
        geometricNormal,
        viewDirection);
    if (PbrContractDot(safeNormal, viewDirection) < 0.0f)
        safeNormal = PbrContractScale(safeNormal, -1.0f);
    return safeNormal;
}

UVSR_RAY_ORIGIN_INLINE float RayOriginStepDepthTowardCamera(
    float depth,
    bool floatDepth,
    bool reverseDepth,
    float depthQuantizationStep)
{
    if (floatDepth)
    {
        RayOriginUint bits = RayOriginAsUint(RayOriginSaturate(depth));
        const RayOriginUint oneBits = RayOriginAsUint(1.0f);
        if (reverseDepth)
            bits = bits < oneBits ? bits + 1u : oneBits;
        else
            bits = bits > 0u ? bits - 1u : 0u;
        const float stepped = RayOriginAsFloat(bits);
        return PbrContractIsFinite(stepped)
            ? RayOriginSaturate(stepped)
            : depth;
    }

    const float direction = reverseDepth ? 1.0f : -1.0f;
    return RayOriginSaturate(
        depth + direction * depthQuantizationStep);
}

UVSR_RAY_ORIGIN_INLINE float RayOriginOffsetFloatComponent(
    float position,
    float direction)
{
    const float origin = 1.0f / 32.0f;
    const float floatScale = 1.0f / 65536.0f;
    const float integerScale = 256.0f;
    const int integerOffset = int(integerScale * direction);
    const int signedOffset = position < 0.0f
        ? -integerOffset
        : integerOffset;
    const RayOriginUint shiftedBits = RayOriginAsUint(position) +
        RayOriginUint(signedOffset);
    const float shifted = RayOriginAsFloat(shiftedBits);
    return RayOriginAbs(position) < origin
        ? position + floatScale * direction
        : shifted;
}

UVSR_RAY_ORIGIN_INLINE RayOriginFloat3 RayOriginOffsetFloatPosition(
    RayOriginFloat3 position,
    RayOriginFloat3 direction)
{
    const RayOriginFloat3 safeDirection = PbrSafeNormalize(
        direction,
        RayOriginMakeFloat3(0.0f, 0.0f, 1.0f));
    return RayOriginMakeFloat3(
        RayOriginOffsetFloatComponent(position.x, safeDirection.x),
        RayOriginOffsetFloatComponent(position.y, safeDirection.y),
        RayOriginOffsetFloatComponent(position.z, safeDirection.z));
}

UVSR_RAY_ORIGIN_INLINE float ResolveRayOriginClearance(
    float userBias,
    float depthStepDistance)
{
    return PbrContractMax(
        PbrContractMax(userBias, 0.0f),
        PbrContractIsFinite(depthStepDistance)
            ? PbrContractMax(depthStepDistance, 0.0f)
            : 0.0f);
}

UVSR_RAY_ORIGIN_INLINE RayOriginFloat3 ResolveRayOriginPosition(
    RayOriginFloat3 surfacePosition,
    RayOriginFloat3 orientedGeometricNormal,
    float clearance)
{
    return RayOriginOffsetFloatPosition(
        RayOriginAdd(
            surfacePosition,
            PbrContractScale(orientedGeometricNormal, clearance)),
        orientedGeometricNormal);
}

#ifndef __cplusplus
#undef RayOriginFloat3
#undef RayOriginUint
#endif
#undef UVSR_RAY_ORIGIN_INLINE

#endif // UVSR_RAY_ORIGIN_CONTRACT_H
