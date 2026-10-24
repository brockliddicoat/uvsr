#ifndef UVSR_BENT_AO_APPLICATION_SHARED_H
#define UVSR_BENT_AO_APPLICATION_SHARED_H

#ifdef __cplusplus
#include "image_based_lighting_shared.h"

namespace uvsr
{
#define UVSR_BENT_AO_INLINE inline
    struct BentAoFloat3
    {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
    };

    struct BentAoFloat4
    {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        float w = 0.f;
    };

    using BentAoUint = std::uint32_t;

    inline BentAoFloat3 BentAoMakeFloat3(float x, float y, float z)
    {
        return { x, y, z };
    }

    inline BentAoFloat3 BentAoExtractFloat3(BentAoFloat4 value)
    {
        return { value.x, value.y, value.z };
    }

    inline bool BentAoIsFinite(float value)
    {
        return std::isfinite(value);
    }

    inline bool BentAoIsFinite3(BentAoFloat3 value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    inline bool BentAoIsFinite4(BentAoFloat4 value)
    {
        return BentAoIsFinite3(BentAoExtractFloat3(value)) &&
            std::isfinite(value.w);
    }

    inline float BentAoSaturate(float value)
    {
        return std::clamp(value, 0.f, 1.f);
    }

    inline float BentAoMinimum(float left, float right)
    {
        return std::min(left, right);
    }

    inline float BentAoMaximum(float left, float right)
    {
        return std::max(left, right);
    }

    inline float BentAoDot(BentAoFloat3 left, BentAoFloat3 right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    inline BentAoFloat3 BentAoScale(BentAoFloat3 value, float scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    inline BentAoFloat3 BentAoSubtract(
        BentAoFloat3 left,
        BentAoFloat3 right)
    {
        return { left.x - right.x, left.y - right.y, left.z - right.z };
    }

    inline BentAoFloat3 BentAoLerp(
        BentAoFloat3 left,
        BentAoFloat3 right,
        float weight)
    {
        return {
            left.x + (right.x - left.x) * weight,
            left.y + (right.y - left.y) * weight,
            left.z + (right.z - left.z) * weight
        };
    }

    inline float BentAoReciprocalSquareRoot(float value)
    {
        return 1.f / std::sqrt(value);
    }

    inline float BentAoSmoothStep(float minimum, float maximum, float value)
    {
        const float normalized = BentAoSaturate(
            (value - minimum) / (maximum - minimum));
        return normalized * normalized * (3.f - 2.f * normalized);
    }

#else

#include "image_based_lighting_shared.h"

#define BentAoFloat3 float3
#define BentAoFloat4 float4
#define BentAoUint uint
#define UVSR_BENT_AO_INLINE

float3 BentAoMakeFloat3(float x, float y, float z)
{
    return float3(x, y, z);
}

float3 BentAoExtractFloat3(float4 value)
{
    return value.xyz;
}

bool BentAoIsFinite(float value)
{
    return isfinite(value);
}

bool BentAoIsFinite3(float3 value)
{
    return all(isfinite(value));
}

bool BentAoIsFinite4(float4 value)
{
    return all(isfinite(value));
}

float BentAoSaturate(float value)
{
    return saturate(value);
}

float BentAoMinimum(float left, float right)
{
    return min(left, right);
}

float BentAoMaximum(float left, float right)
{
    return max(left, right);
}

float BentAoDot(float3 left, float3 right)
{
    return dot(left, right);
}

float3 BentAoScale(float3 value, float scale)
{
    return value * scale;
}

float3 BentAoSubtract(float3 left, float3 right)
{
    return left - right;
}

float3 BentAoLerp(float3 left, float3 right, float weight)
{
    return lerp(left, right, weight);
}

float BentAoReciprocalSquareRoot(float value)
{
    return rsqrt(value);
}

float BentAoSmoothStep(float minimum, float maximum, float value)
{
    return smoothstep(minimum, maximum, value);
}

#endif

struct BentAoResolvedSignal
{
    BentAoFloat3 direction;
    float cosineVisibility;
    float confidence;
    float confidenceBlend;
    BentAoUint valid;
};

UVSR_BENT_AO_INLINE BentAoUint BentAoHasDirectionalSupport(
    BentAoFloat4 momentAndConfidence,
    float cosineVisibility)
{
    return BentAoIsFinite4(momentAndConfidence) &&
        BentAoIsFinite(cosineVisibility) &&
        BentAoSaturate(momentAndConfidence.w) > 0.0f
        ? 1u
        : 0u;
}

UVSR_BENT_AO_INLINE float ResolveBentAoTemporalConfidence(
    float currentConfidence,
    float previousConfidence,
    float similarity,
    float currentWeight,
    BentAoUint validPreviousHistory)
{
    float representedCurrentWeight = BentAoSaturate(currentWeight);
    float acceptedPreviousSupport = validPreviousHistory != 0u
        ? BentAoSaturate(previousConfidence) * BentAoSaturate(similarity) *
            (1.0f - representedCurrentWeight)
        : 0.0f;
    float acceptedCurrentSupport = representedCurrentWeight > 0.0f
        ? BentAoSaturate(currentConfidence)
        : 0.0f;
    return BentAoSaturate(1.0f -
        (1.0f - acceptedPreviousSupport) *
        (1.0f - acceptedCurrentSupport));
}

UVSR_BENT_AO_INLINE float ResolveBentAoRetainedHistoryConfidence(
    float previousConfidence,
    float similarity,
    float currentWeight)
{
    return BentAoSaturate(previousConfidence) *
        BentAoSaturate(similarity) *
        (1.0f - BentAoSaturate(currentWeight));
}

UVSR_BENT_AO_INLINE BentAoResolvedSignal ResolveBentAoSignal(
    BentAoFloat4 momentAndConfidence,
    float cosineVisibility,
    BentAoFloat3 surfaceNormal,
    BentAoFloat3 geometricNormal)
{
    BentAoResolvedSignal result;
    result.direction = BentAoIsFinite3(surfaceNormal)
        ? surfaceNormal
        : BentAoIsFinite3(geometricNormal)
            ? geometricNormal
            : BentAoMakeFloat3(0.0f, 1.0f, 0.0f);
    result.cosineVisibility = 1.0f;
    result.confidence = 0.0f;
    result.confidenceBlend = 0.0f;
    result.valid = 0u;

    if (!BentAoIsFinite4(momentAndConfidence) ||
        !BentAoIsFinite(cosineVisibility) ||
        !BentAoIsFinite3(surfaceNormal) ||
        !BentAoIsFinite3(geometricNormal))
    {
        return result;
    }

    float confidence = BentAoSaturate(momentAndConfidence.w);
    result.cosineVisibility = BentAoSaturate(cosineVisibility);
    BentAoFloat3 moment = BentAoExtractFloat3(momentAndConfidence);
    float momentLengthSquared = BentAoDot(moment, moment);
    if (!(confidence > 1e-4f) ||
        !(momentLengthSquared > 1e-12f) ||
        !BentAoIsFinite(momentLengthSquared))
    {
        return result;
    }

    // Project only after spatial and temporal reconstruction so history keeps
    // a linear world-space first moment. The final IBL direction may not cross
    // below the geometric surface even when the shading normal is mapped.
    float negativeGeometricComponent = BentAoMinimum(
        BentAoDot(moment, geometricNormal), 0.0f);
    moment = BentAoSubtract(
        moment,
        BentAoScale(geometricNormal, negativeGeometricComponent));
    momentLengthSquared = BentAoDot(moment, moment);
    if (!(momentLengthSquared > 1e-12f) ||
        !BentAoIsFinite(momentLengthSquared))
    {
        return result;
    }

    BentAoFloat3 candidate = BentAoScale(
        moment, BentAoReciprocalSquareRoot(momentLengthSquared));
    float blend = BentAoSmoothStep(1.0f / 12.0f, 0.5f, confidence);
    if (blend >= 1.0f)
    {
        if (BentAoDot(candidate, geometricNormal) < 0.0f)
            return result;
        result.direction = candidate;
        result.confidence = confidence;
        result.confidenceBlend = 1.0f;
        result.valid = 1u;
        return result;
    }
    BentAoFloat3 blended = BentAoLerp(surfaceNormal, candidate, blend);
    float blendedLengthSquared = BentAoDot(blended, blended);
    if (!(blendedLengthSquared > 1e-12f) ||
        !BentAoIsFinite(blendedLengthSquared))
    {
        return result;
    }

    BentAoFloat3 resolvedDirection = BentAoScale(
        blended, BentAoReciprocalSquareRoot(blendedLengthSquared));
    if (BentAoDot(resolvedDirection, geometricNormal) < 0.0f)
        return result;
    result.direction = resolvedDirection;
    result.confidence = confidence;
    result.confidenceBlend = blend;
    result.valid = 1u;
    return result;
}

UVSR_BENT_AO_INLINE float ResolveBentAoSpecularOcclusion(
    BentAoResolvedSignal bent,
    BentAoFloat3 reflectionDirection,
    BentAoFloat3 surfaceNormal,
    float perceptualRoughness,
    float legacyOcclusion,
    float materialOcclusion,
    float ambientStrength)
{
    if (bent.valid == 0u ||
        !BentAoIsFinite3(reflectionDirection) ||
        !BentAoIsFinite3(surfaceNormal) ||
        !BentAoIsFinite(perceptualRoughness) ||
        !BentAoIsFinite(legacyOcclusion) ||
        !BentAoIsFinite(materialOcclusion) ||
        !BentAoIsFinite(ambientStrength))
    {
        return BentAoIsFinite(legacyOcclusion)
            ? BentAoSaturate(legacyOcclusion)
            : 1.0f;
    }

    float directionalOcclusion =
        ImageBasedLightingBentSpecularOcclusion(
            bent.cosineVisibility,
            BentAoDot(bent.direction, reflectionDirection),
            BentAoDot(bent.direction, surfaceNormal),
            BentAoDot(reflectionDirection, surfaceNormal),
            perceptualRoughness);
    directionalOcclusion = BentAoSaturate(directionalOcclusion);
    directionalOcclusion = BentAoSaturate(
        1.0f - BentAoMaximum(ambientStrength, 0.0f) *
            (1.0f - directionalOcclusion));
    directionalOcclusion *= materialOcclusion;
    return BentAoSaturate(
        legacyOcclusion +
        (directionalOcclusion - legacyOcclusion) * bent.confidenceBlend);
}

#ifdef __cplusplus
#undef UVSR_BENT_AO_INLINE
}
#else
#undef UVSR_BENT_AO_INLINE
#undef BentAoFloat3
#undef BentAoFloat4
#undef BentAoUint
#endif

#endif // UVSR_BENT_AO_APPLICATION_SHARED_H
