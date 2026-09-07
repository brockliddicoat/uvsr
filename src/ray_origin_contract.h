#ifndef UVSR_RAY_ORIGIN_CONTRACT_H
#define UVSR_RAY_ORIGIN_CONTRACT_H

#include "pbr_surface_light_contract.h"

#include "shader_math.h"

UVSR_SHADER_INLINE ShaderFloat3 RayOriginOrientGeometricNormal(
    ShaderFloat3 geometricNormal,
    ShaderFloat3 viewDirection)
{
    ShaderFloat3 safeNormal = PbrSafeNormalize(
        geometricNormal,
        viewDirection);
    if (ShaderDot(safeNormal, viewDirection) < 0.0f)
        safeNormal = ShaderScale3(safeNormal, -1.0f);
    return safeNormal;
}

UVSR_SHADER_INLINE float RayOriginStepDepthTowardCamera(
    float depth,
    bool floatDepth,
    bool reverseDepth,
    float depthQuantizationStep)
{
    if (floatDepth)
    {
        ShaderUint bits = ShaderAsUint(ShaderSaturate(depth));
        const ShaderUint oneBits = ShaderAsUint(1.0f);
        if (reverseDepth)
            bits = bits < oneBits ? bits + 1u : oneBits;
        else
            bits = bits > 0u ? bits - 1u : 0u;
        const float stepped = ShaderAsFloat(bits);
        return ShaderIsFinite(stepped)
            ? ShaderSaturate(stepped)
            : depth;
    }

    const float direction = reverseDepth ? 1.0f : -1.0f;
    return ShaderSaturate(
        depth + direction * depthQuantizationStep);
}

UVSR_SHADER_INLINE float RayOriginOffsetFloatComponent(
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
    const ShaderUint shiftedBits = ShaderAsUint(position) +
        ShaderUint(signedOffset);
    const float shifted = ShaderAsFloat(shiftedBits);
    return ShaderAbs(position) < origin
        ? position + floatScale * direction
        : shifted;
}

UVSR_SHADER_INLINE ShaderFloat3 RayOriginOffsetFloatPosition(
    ShaderFloat3 position,
    ShaderFloat3 direction)
{
    const ShaderFloat3 safeDirection = PbrSafeNormalize(
        direction,
        ShaderMakeFloat3(0.0f, 0.0f, 1.0f));
    return ShaderMakeFloat3(
        RayOriginOffsetFloatComponent(position.x, safeDirection.x),
        RayOriginOffsetFloatComponent(position.y, safeDirection.y),
        RayOriginOffsetFloatComponent(position.z, safeDirection.z));
}

UVSR_SHADER_INLINE float ResolveRayOriginClearance(
    float userBias,
    float depthStepDistance)
{
    return ShaderMax(
        ShaderMax(userBias, 0.0f),
        ShaderIsFinite(depthStepDistance)
            ? ShaderMax(depthStepDistance, 0.0f)
            : 0.0f);
}

UVSR_SHADER_INLINE ShaderFloat3 ResolveRayOriginPosition(
    ShaderFloat3 surfacePosition,
    ShaderFloat3 orientedGeometricNormal,
    float clearance)
{
    return RayOriginOffsetFloatPosition(
        ShaderAdd3(
            surfacePosition,
            ShaderScale3(orientedGeometricNormal, clearance)),
        orientedGeometricNormal);
}


#endif // UVSR_RAY_ORIGIN_CONTRACT_H
