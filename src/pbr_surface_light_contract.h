#ifndef UVSR_PBR_SURFACE_LIGHT_CONTRACT_H
#define UVSR_PBR_SURFACE_LIGHT_CONTRACT_H

// Executable CPU/HLSL contract for surface orientation and analytical-light
// profiles. Production shaders and known-answer tests consume these exact
// functions so sign, operand order, and energy equations cannot drift apart.

#include "shader_math.h"

struct PbrContractSurfaceNormals
{
    ShaderFloat3 shadingNormal;
    ShaderFloat3 geometricNormal;
};

struct PbrFiniteDirectionalEmitterContract
{
    float oneMinusCosineMaximum;
    float solidAngle;
    float directionalPdf;
    float radianceScale;
    int valid;
};

struct PbrFiniteSphereEmitterContract
{
    float oneMinusCosineMaximum;
    float solidAngle;
    float directionalPdf;
    float radianceScale;
    int receiverOutside;
    int valid;
};

UVSR_SHADER_INLINE ShaderFloat3 SamplePbrDirectionalEmitter(
    ShaderFloat3 centerDirection, float angularDiameter, ShaderFloat2 random)
{
    if (!(angularDiameter > 0.0f))
        return centerDirection;
    // uniform solid angle over the emitter cone. the sine form stays stable
    // for small solar angles where subtracting cos(radius) loses precision.
    const float sineQuarterAngle = ShaderSin(angularDiameter * 0.25f);
    const float oneMinusCosine = random.x * 2.0f * sineQuarterAngle * sineQuarterAngle;
    const float cosine = 1.0f - oneMinusCosine;
    const float sine = ShaderSqrt(ShaderMax(0.0f, oneMinusCosine * (2.0f - oneMinusCosine)));
    const float phi = random.y * 6.2831853071795864769f;
    const ShaderFloat3 up = ShaderAbs(centerDirection.z) < 0.999f
        ? ShaderFloat3(0.0f, 0.0f, 1.0f) : ShaderFloat3(0.0f, 1.0f, 0.0f);
    ShaderFloat3 tangent = ShaderCross(up, centerDirection);
    tangent = tangent * ShaderRsqrt(ShaderDot(tangent, tangent));
    const ShaderFloat3 bitangent = ShaderCross(centerDirection, tangent);
    return centerDirection * cosine +
        tangent * (sine * ShaderCos(phi)) + bitangent * (sine * ShaderSin(phi));
}

struct PbrFiniteSphereEndpointContract
{
    float distance;
    int valid;
};

UVSR_SHADER_INLINE ShaderFloat3 PbrSafeNormalize(
    ShaderFloat3 value,
    ShaderFloat3 fallback)
{
    const float lengthSquared = ShaderDot(value, value);
    return lengthSquared > 1e-12f
        ? ShaderScale3(value, ShaderRsqrt(lengthSquared))
        : fallback;
}

UVSR_SHADER_INLINE bool ShouldFlipPbrSurfaceNormals(
    bool isDoubleSided,
    bool isFrontFace,
    ShaderFloat3 geometricNormal,
    ShaderFloat3 viewDirection)
{
    // Reflected instances can reverse raster winding independently of their
    // transformed normal. Double-sided surfaces therefore follow the actual
    // view hemisphere; single-sided surfaces retain the raster-facing rule.
    return isDoubleSided
        ? ShaderDot(geometricNormal, viewDirection) < 0.0f
        : !isFrontFace;
}

UVSR_SHADER_INLINE ShaderFloat3 ResolvePbrTrianglePlaneNormal(
    ShaderFloat3 positionDerivativeX,
    ShaderFloat3 positionDerivativeY,
    ShaderFloat3 fallbackGeometricNormal)
{
    // Cross order is part of the raster-space contract: x derivative first,
    // y derivative second.
    return PbrSafeNormalize(
        ShaderCross(positionDerivativeX, positionDerivativeY),
        fallbackGeometricNormal);
}

UVSR_SHADER_INLINE PbrContractSurfaceNormals
    ResolvePbrTriangleSurfaceNormals(
        ShaderFloat3 positionDerivativeX,
        ShaderFloat3 positionDerivativeY,
        ShaderFloat3 fallbackGeometricNormal,
        ShaderFloat3 shadingNormal,
        ShaderFloat3 viewDirection)
{
    PbrContractSurfaceNormals result;
    result.geometricNormal = ResolvePbrTrianglePlaneNormal(
        positionDerivativeX,
        positionDerivativeY,
        fallbackGeometricNormal);
    if (ShaderDot(result.geometricNormal, viewDirection) < 0.0f)
        result.geometricNormal = ShaderScale3(result.geometricNormal, -1.0f);
    result.shadingNormal = shadingNormal;
    if (ShaderDot(result.shadingNormal, result.geometricNormal) < 0.0f)
        result.shadingNormal = ShaderScale3(result.shadingNormal, -1.0f);
    return result;
}

UVSR_SHADER_INLINE float ResolveAnalyticalPositionalLightIntensity(
    float luminousIntensity,
    float radius,
    float inverseDistance,
    float distanceSquared)
{
    if (!(ShaderIsFinite(radius) && radius > 0.0f))
        return luminousIntensity / distanceSquared;

    const float halfAngularSize = ShaderAtan(ShaderMin(
        radius * inverseDistance,
        1.0f));
    return luminousIntensity / (radius * radius) *
        halfAngularSize * halfAngularSize;
}

UVSR_SHADER_INLINE float ResolvePbrAnalyticalRangeWeight(
    float distanceSquared,
    float inverseRange)
{
    if (!(inverseRange > 0.0f))
        return 1.0f;
    float weight = ShaderSaturate(
        1.0f - distanceSquared * inverseRange * inverseRange);
    return weight * weight;
}

UVSR_SHADER_INLINE float ResolvePbrOrdinarySpotWeight(
    float cosTheta,
    float innerAngle,
    float outerAngle)
{
    const float cosInner = ShaderCos(innerAngle * 0.5f);
    const float cosOuter = ShaderCos(outerAngle * 0.5f);
    float weight = ShaderSaturate(
        (cosTheta - cosOuter) /
        ShaderMax(cosInner - cosOuter, 1e-6f));
    return weight * weight * (3.0f - 2.0f * weight);
}

UVSR_SHADER_INLINE float ApplyPbrAnalyticalLightProfile(
    float unweightedIntensity,
    float rangeWeight,
    float spotWeight)
{
    return unweightedIntensity * rangeWeight * spotWeight;
}

UVSR_SHADER_INLINE PbrFiniteDirectionalEmitterContract
    ResolvePbrFiniteDirectionalEmitter(
        float directionalIrradiance,
        float angularDiameter)
{
    PbrFiniteDirectionalEmitterContract result;
    result.oneMinusCosineMaximum = 0.0f;
    result.solidAngle = 0.0f;
    result.directionalPdf = 0.0f;
    result.radianceScale = 0.0f;
    result.valid = 0;

    const float pi = 3.14159265358979323846f;
    const float alpha = 0.5f * angularDiameter;
    if (!(alpha > 0.0f) || !(alpha < pi) ||
        !ShaderIsFinite(directionalIrradiance))
    {
        return result;
    }

    const float sineAlpha = ShaderSin(alpha);
    const float sineHalfAlpha = ShaderSin(0.5f * alpha);
    const float sineAlphaSquared = sineAlpha * sineAlpha;
    result.oneMinusCosineMaximum =
        2.0f * sineHalfAlpha * sineHalfAlpha;
    result.solidAngle =
        2.0f * pi * result.oneMinusCosineMaximum;
    if (!(sineAlphaSquared > 0.0f) ||
        !(result.solidAngle > 0.0f) ||
        !ShaderIsFinite(sineAlphaSquared) ||
        !ShaderIsFinite(result.solidAngle))
    {
        return result;
    }

    result.directionalPdf = 1.0f / result.solidAngle;
    result.radianceScale = directionalIrradiance /
        (pi * sineAlphaSquared);
    result.valid = ShaderIsFinite(result.directionalPdf) &&
        result.directionalPdf > 0.0f &&
        ShaderIsFinite(result.radianceScale)
        ? 1
        : 0;
    return result;
}

UVSR_SHADER_INLINE PbrFiniteSphereEmitterContract
    ResolvePbrFiniteSphereEmitter(
        float luminousIntensity,
        float radius,
        float centerDistance)
{
    PbrFiniteSphereEmitterContract result;
    result.oneMinusCosineMaximum = 0.0f;
    result.solidAngle = 0.0f;
    result.directionalPdf = 0.0f;
    result.radianceScale = 0.0f;
    result.receiverOutside = 0;
    result.valid = 0;

    const float pi = 3.14159265358979323846f;
    const float radiusSquared = radius * radius;
    if (!(radiusSquared > 0.0f) ||
        !(centerDistance >= 0.0f) ||
        !ShaderIsFinite(radiusSquared) ||
        !ShaderIsFinite(centerDistance) ||
        !ShaderIsFinite(luminousIntensity))
    {
        return result;
    }

    result.radianceScale = luminousIntensity / (pi * radiusSquared);
    result.receiverOutside = centerDistance > radius ? 1 : 0;
    if (result.receiverOutside != 0)
    {
        const float sineAlpha = radius / centerDistance;
        const float sineAlphaSquared = sineAlpha * sineAlpha;
        const float cosineAlpha = ShaderSqrt(ShaderSaturate(
            1.0f - sineAlphaSquared));
        // Stable 1-cos(alpha) for small apparent emitters.
        result.oneMinusCosineMaximum = sineAlphaSquared /
            (1.0f + cosineAlpha);
    }
    else
    {
        result.oneMinusCosineMaximum = 2.0f;
    }
    result.solidAngle =
        2.0f * pi * result.oneMinusCosineMaximum;
    if (!(result.solidAngle > 0.0f) ||
        !ShaderIsFinite(result.solidAngle))
    {
        return result;
    }
    result.directionalPdf = 1.0f / result.solidAngle;
    result.valid = ShaderIsFinite(result.directionalPdf) &&
        result.directionalPdf > 0.0f &&
        ShaderIsFinite(result.radianceScale)
        ? 1
        : 0;
    return result;
}

UVSR_SHADER_INLINE PbrFiniteSphereEndpointContract
    ResolvePbrFiniteSphereEndpoint(
        float centerProjection,
        float centerDistanceSquared,
        float radiusSquared,
        bool receiverOutside)
{
    PbrFiniteSphereEndpointContract result;
    result.distance = 0.0f;
    result.valid = 0;
    const float discriminant = radiusSquared -
        (centerDistanceSquared - centerProjection * centerProjection);
    if (!ShaderIsFinite(discriminant))
        return result;
    const float root = ShaderSqrt(ShaderMax(
        discriminant,
        0.0f));
    result.distance = receiverOutside
        ? centerProjection - root
        : centerProjection + root;
    result.valid = result.distance >= 0.0f &&
        ShaderIsFinite(result.distance)
        ? 1
        : 0;
    return result;
}


#endif // UVSR_PBR_SURFACE_LIGHT_CONTRACT_H
