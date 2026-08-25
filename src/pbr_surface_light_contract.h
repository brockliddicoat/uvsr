#ifndef UVSR_PBR_SURFACE_LIGHT_CONTRACT_H
#define UVSR_PBR_SURFACE_LIGHT_CONTRACT_H

// Executable CPU/HLSL contract for surface orientation and analytical-light
// profiles. Production shaders and known-answer tests consume these exact
// functions so sign, operand order, and energy equations cannot drift apart.

#ifdef __cplusplus

#include <algorithm>
#include <cmath>

struct PbrContractFloat3
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

inline PbrContractFloat3 PbrContractScale(
    PbrContractFloat3 value,
    float scale) noexcept
{
    return { value.x * scale, value.y * scale, value.z * scale };
}

inline float PbrContractDot(
    PbrContractFloat3 left,
    PbrContractFloat3 right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline PbrContractFloat3 PbrContractCross(
    PbrContractFloat3 left,
    PbrContractFloat3 right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x
    };
}

inline float PbrContractRsqrt(float value) noexcept
{
    return 1.f / std::sqrt(value);
}

inline bool PbrContractIsFinite(float value) noexcept
{
    return std::isfinite(value);
}

inline float PbrContractMin(float left, float right) noexcept
{
    return std::min(left, right);
}

inline float PbrContractMax(float left, float right) noexcept
{
    return std::max(left, right);
}

inline float PbrContractSaturate(float value) noexcept
{
    return std::clamp(value, 0.f, 1.f);
}

inline float PbrContractAtan(float value) noexcept
{
    return std::atan(value);
}

inline float PbrContractCos(float value) noexcept
{
    return std::cos(value);
}

inline float PbrContractSin(float value) noexcept
{
    return std::sin(value);
}

inline float PbrContractSqrt(float value) noexcept
{
    return std::sqrt(value);
}

#define UVSR_PBR_CONTRACT_INLINE inline

#else

#define PbrContractFloat3 float3

float3 PbrContractScale(float3 value, float scale)
{
    return value * scale;
}

float PbrContractDot(float3 left, float3 right)
{
    return dot(left, right);
}

float3 PbrContractCross(float3 left, float3 right)
{
    return cross(left, right);
}

float PbrContractRsqrt(float value)
{
    return rsqrt(value);
}

bool PbrContractIsFinite(float value)
{
    return isfinite(value);
}

float PbrContractMin(float left, float right)
{
    return min(left, right);
}

float PbrContractMax(float left, float right)
{
    return max(left, right);
}

float PbrContractSaturate(float value)
{
    return saturate(value);
}

float PbrContractAtan(float value)
{
    return atan(value);
}

float PbrContractCos(float value)
{
    return cos(value);
}

float PbrContractSin(float value)
{
    return sin(value);
}

float PbrContractSqrt(float value)
{
    return sqrt(value);
}

#define UVSR_PBR_CONTRACT_INLINE

#endif

struct PbrContractSurfaceNormals
{
    PbrContractFloat3 shadingNormal;
    PbrContractFloat3 geometricNormal;
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

struct PbrFiniteSphereEndpointContract
{
    float distance;
    int valid;
};

UVSR_PBR_CONTRACT_INLINE PbrContractFloat3 PbrSafeNormalize(
    PbrContractFloat3 value,
    PbrContractFloat3 fallback)
{
    const float lengthSquared = PbrContractDot(value, value);
    return lengthSquared > 1e-12f
        ? PbrContractScale(value, PbrContractRsqrt(lengthSquared))
        : fallback;
}

UVSR_PBR_CONTRACT_INLINE bool ShouldFlipPbrSurfaceNormals(
    bool isDoubleSided,
    bool isFrontFace,
    PbrContractFloat3 geometricNormal,
    PbrContractFloat3 viewDirection)
{
    // Reflected instances can reverse raster winding independently of their
    // transformed normal. Double-sided surfaces therefore follow the actual
    // view hemisphere; single-sided surfaces retain the raster-facing rule.
    return isDoubleSided
        ? PbrContractDot(geometricNormal, viewDirection) < 0.0f
        : !isFrontFace;
}

UVSR_PBR_CONTRACT_INLINE PbrContractFloat3 ResolvePbrTrianglePlaneNormal(
    PbrContractFloat3 positionDerivativeX,
    PbrContractFloat3 positionDerivativeY,
    PbrContractFloat3 fallbackGeometricNormal)
{
    // Cross order is part of the raster-space contract: x derivative first,
    // y derivative second.
    return PbrSafeNormalize(
        PbrContractCross(positionDerivativeX, positionDerivativeY),
        fallbackGeometricNormal);
}

UVSR_PBR_CONTRACT_INLINE PbrContractSurfaceNormals
    ResolvePbrTriangleSurfaceNormals(
        PbrContractFloat3 positionDerivativeX,
        PbrContractFloat3 positionDerivativeY,
        PbrContractFloat3 fallbackGeometricNormal,
        PbrContractFloat3 shadingNormal,
        PbrContractFloat3 viewDirection)
{
    PbrContractSurfaceNormals result;
    result.geometricNormal = ResolvePbrTrianglePlaneNormal(
        positionDerivativeX,
        positionDerivativeY,
        fallbackGeometricNormal);
    if (PbrContractDot(result.geometricNormal, viewDirection) < 0.0f)
        result.geometricNormal = PbrContractScale(result.geometricNormal, -1.0f);
    result.shadingNormal = shadingNormal;
    if (PbrContractDot(result.shadingNormal, result.geometricNormal) < 0.0f)
        result.shadingNormal = PbrContractScale(result.shadingNormal, -1.0f);
    return result;
}

UVSR_PBR_CONTRACT_INLINE float ResolveAnalyticalPositionalLightIntensity(
    float luminousIntensity,
    float radius,
    float inverseDistance,
    float distanceSquared)
{
    if (!(PbrContractIsFinite(radius) && radius > 0.0f))
        return luminousIntensity / distanceSquared;

    const float halfAngularSize = PbrContractAtan(PbrContractMin(
        radius * inverseDistance,
        1.0f));
    return luminousIntensity / (radius * radius) *
        halfAngularSize * halfAngularSize;
}

UVSR_PBR_CONTRACT_INLINE float ResolvePbrAnalyticalRangeWeight(
    float distanceSquared,
    float inverseRange)
{
    if (!(inverseRange > 0.0f))
        return 1.0f;
    float weight = PbrContractSaturate(
        1.0f - distanceSquared * inverseRange * inverseRange);
    return weight * weight;
}

UVSR_PBR_CONTRACT_INLINE float ResolvePbrOrdinarySpotWeight(
    float cosTheta,
    float innerAngle,
    float outerAngle)
{
    const float cosInner = PbrContractCos(innerAngle * 0.5f);
    const float cosOuter = PbrContractCos(outerAngle * 0.5f);
    float weight = PbrContractSaturate(
        (cosTheta - cosOuter) /
        PbrContractMax(cosInner - cosOuter, 1e-6f));
    return weight * weight * (3.0f - 2.0f * weight);
}

UVSR_PBR_CONTRACT_INLINE float ApplyPbrAnalyticalLightProfile(
    float unweightedIntensity,
    float rangeWeight,
    float spotWeight)
{
    return unweightedIntensity * rangeWeight * spotWeight;
}

UVSR_PBR_CONTRACT_INLINE PbrFiniteDirectionalEmitterContract
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
        !PbrContractIsFinite(directionalIrradiance))
    {
        return result;
    }

    const float sineAlpha = PbrContractSin(alpha);
    const float sineHalfAlpha = PbrContractSin(0.5f * alpha);
    const float sineAlphaSquared = sineAlpha * sineAlpha;
    result.oneMinusCosineMaximum =
        2.0f * sineHalfAlpha * sineHalfAlpha;
    result.solidAngle =
        2.0f * pi * result.oneMinusCosineMaximum;
    if (!(sineAlphaSquared > 0.0f) ||
        !(result.solidAngle > 0.0f) ||
        !PbrContractIsFinite(sineAlphaSquared) ||
        !PbrContractIsFinite(result.solidAngle))
    {
        return result;
    }

    result.directionalPdf = 1.0f / result.solidAngle;
    result.radianceScale = directionalIrradiance /
        (pi * sineAlphaSquared);
    result.valid = PbrContractIsFinite(result.directionalPdf) &&
        result.directionalPdf > 0.0f &&
        PbrContractIsFinite(result.radianceScale)
        ? 1
        : 0;
    return result;
}

UVSR_PBR_CONTRACT_INLINE PbrFiniteSphereEmitterContract
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
        !PbrContractIsFinite(radiusSquared) ||
        !PbrContractIsFinite(centerDistance) ||
        !PbrContractIsFinite(luminousIntensity))
    {
        return result;
    }

    result.radianceScale = luminousIntensity / (pi * radiusSquared);
    result.receiverOutside = centerDistance > radius ? 1 : 0;
    if (result.receiverOutside != 0)
    {
        const float sineAlpha = radius / centerDistance;
        const float sineAlphaSquared = sineAlpha * sineAlpha;
        const float cosineAlpha = PbrContractSqrt(PbrContractSaturate(
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
        !PbrContractIsFinite(result.solidAngle))
    {
        return result;
    }
    result.directionalPdf = 1.0f / result.solidAngle;
    result.valid = PbrContractIsFinite(result.directionalPdf) &&
        result.directionalPdf > 0.0f &&
        PbrContractIsFinite(result.radianceScale)
        ? 1
        : 0;
    return result;
}

UVSR_PBR_CONTRACT_INLINE PbrFiniteSphereEndpointContract
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
    if (!PbrContractIsFinite(discriminant))
        return result;
    const float root = PbrContractSqrt(PbrContractMax(
        discriminant,
        0.0f));
    result.distance = receiverOutside
        ? centerProjection - root
        : centerProjection + root;
    result.valid = result.distance >= 0.0f &&
        PbrContractIsFinite(result.distance)
        ? 1
        : 0;
    return result;
}

#ifndef __cplusplus
#undef PbrContractFloat3
#endif
#undef UVSR_PBR_CONTRACT_INLINE

#endif // UVSR_PBR_SURFACE_LIGHT_CONTRACT_H
