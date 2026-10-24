#ifndef UVSR_RAY_VISIBILITY_SAMPLE_SEQUENCE_SHARED_H
#define UVSR_RAY_VISIBILITY_SAMPLE_SEQUENCE_SHARED_H

// Shared by C++ tests and HLSL ray-visibility producers. One Cranley-Patterson
// shift belongs to the pixel/effect; every sample then advances only the
// low-discrepancy index. Randomizing the shift per sample destroys the sequence
// stratification while preserving only marginal uniformity.
#ifdef __cplusplus

#include <cmath>
#include <cstdint>

using RayVisibilitySequenceUint = std::uint32_t;

struct RayVisibilitySequenceFloat2
{
    float x = 0.f;
    float y = 0.f;
};

inline float RayVisibilitySequenceFraction(float value) noexcept
{
    return value - std::floor(value);
}

inline RayVisibilitySequenceFloat2 RayVisibilitySequenceMakeFloat2(
    float x,
    float y) noexcept
{
    return { x, y };
}

#define UVSR_RAY_VISIBILITY_SEQUENCE_INLINE inline
#define UVSR_RAY_VISIBILITY_SEQUENCE_FLOAT2 RayVisibilitySequenceFloat2

#else

#define RayVisibilitySequenceUint uint

float RayVisibilitySequenceFraction(float value)
{
    return frac(value);
}

float2 RayVisibilitySequenceMakeFloat2(float x, float y)
{
    return float2(x, y);
}

#define UVSR_RAY_VISIBILITY_SEQUENCE_INLINE
#define UVSR_RAY_VISIBILITY_SEQUENCE_FLOAT2 float2

#endif

UVSR_RAY_VISIBILITY_SEQUENCE_INLINE float RayVisibilityRadicalInverse(
    RayVisibilitySequenceUint index,
    RayVisibilitySequenceUint base)
{
    if (base < 2u)
        return 0.0f;

    const float inverseBase = 1.0f / float(base);
    float inversePower = inverseBase;
    float result = 0.0f;
    while (index > 0u)
    {
        const RayVisibilitySequenceUint digit = index % base;
        result += float(digit) * inversePower;
        index /= base;
        inversePower *= inverseBase;
    }
    return result;
}

UVSR_RAY_VISIBILITY_SEQUENCE_INLINE UVSR_RAY_VISIBILITY_SEQUENCE_FLOAT2
RayVisibilityShiftedHalton23(
    RayVisibilitySequenceUint sampleIndex,
    UVSR_RAY_VISIBILITY_SEQUENCE_FLOAT2 pixelShift)
{
    const RayVisibilitySequenceUint sequenceIndex = sampleIndex + 1u;
    return RayVisibilitySequenceMakeFloat2(
        RayVisibilitySequenceFraction(
            RayVisibilityRadicalInverse(sequenceIndex, 2u) + pixelShift.x),
        RayVisibilitySequenceFraction(
            RayVisibilityRadicalInverse(sequenceIndex, 3u) + pixelShift.y));
}

#ifndef __cplusplus
#undef RayVisibilitySequenceUint
#endif
#undef UVSR_RAY_VISIBILITY_SEQUENCE_FLOAT2
#undef UVSR_RAY_VISIBILITY_SEQUENCE_INLINE

#endif // UVSR_RAY_VISIBILITY_SAMPLE_SEQUENCE_SHARED_H
