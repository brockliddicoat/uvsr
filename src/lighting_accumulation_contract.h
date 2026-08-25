#ifndef UVSR_LIGHTING_ACCUMULATION_CONTRACT_H
#define UVSR_LIGHTING_ACCUMULATION_CONTRACT_H

#define UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT 0xffffffffu

#ifdef __cplusplus

#include <algorithm>
#include <cmath>
#include <cstdint>

using LightingAccumulationUint = std::uint32_t;

struct LightingAccumulationFloat4
{
    float x;
    float y;
    float z;
    float w;
};

inline LightingAccumulationFloat4 LightingAccumulationMakeFloat4(
    float x,
    float y,
    float z,
    float w) noexcept
{
    return { x, y, z, w };
}

inline bool LightingAccumulationIsFinite4(
    LightingAccumulationFloat4 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::isfinite(value.w);
}

inline LightingAccumulationFloat4 LightingAccumulationSanitizeSample(
    LightingAccumulationFloat4 value) noexcept
{
    return {
        std::max(value.x, 0.f),
        std::max(value.y, 0.f),
        std::max(value.z, 0.f),
        1.f
    };
}

inline LightingAccumulationFloat4 LightingAccumulationLerp(
    LightingAccumulationFloat4 left,
    LightingAccumulationFloat4 right,
    float weight) noexcept
{
    return {
        left.x + (right.x - left.x) * weight,
        left.y + (right.y - left.y) * weight,
        left.z + (right.z - left.z) * weight,
        left.w + (right.w - left.w) * weight
    };
}

#define UVSR_LIGHTING_ACCUMULATION_INLINE inline

#else

#define LightingAccumulationUint uint
#define LightingAccumulationFloat4 float4

float4 LightingAccumulationMakeFloat4(
    float x,
    float y,
    float z,
    float w)
{
    return float4(x, y, z, w);
}

bool LightingAccumulationIsFinite4(float4 value)
{
    return all(isfinite(value));
}

float4 LightingAccumulationSanitizeSample(float4 value)
{
    value.rgb = max(value.rgb, 0.0f);
    value.a = 1.0f;
    return value;
}

float4 LightingAccumulationLerp(
    float4 left,
    float4 right,
    float weight)
{
    return lerp(left, right, weight);
}

#define UVSR_LIGHTING_ACCUMULATION_INLINE

#endif

struct LightingAccumulationState
{
    LightingAccumulationFloat4 mean;
    LightingAccumulationUint count;
    LightingAccumulationUint attempted;
    LightingAccumulationUint accepted;
    LightingAccumulationUint publish;
};

UVSR_LIGHTING_ACCUMULATION_INLINE LightingAccumulationUint
    ResolveLightingAccumulationAttemptToken(
        LightingAccumulationUint previousCount,
        bool resetHistory)
{
    const LightingAccumulationUint count = resetHistory
        ? 0u
        : previousCount;
    return (count < UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT
            ? count
            : UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT - 1u) + 1u;
}

UVSR_LIGHTING_ACCUMULATION_INLINE LightingAccumulationState
    RepairLightingAccumulation(
        LightingAccumulationFloat4 previousMean,
        LightingAccumulationUint previousCount,
        bool resetHistory)
{
    LightingAccumulationState result;
    result.mean = previousMean;
    result.count = previousCount;
    result.attempted = 0u;
    result.accepted = 0u;
    result.publish = 1u;
    if (resetHistory || previousCount == 0u ||
        !LightingAccumulationIsFinite4(previousMean))
    {
        result.mean = LightingAccumulationMakeFloat4(
            0.0f, 0.0f, 0.0f, 0.0f);
        result.count = 0u;
    }
    return result;
}

UVSR_LIGHTING_ACCUMULATION_INLINE LightingAccumulationState
    ResolveLightingAccumulationCandidate(
        LightingAccumulationState previous,
        LightingAccumulationUint attemptToken,
        LightingAccumulationFloat4 candidate)
{
    previous.attempted = attemptToken != 0u ? 1u : 0u;
    previous.accepted = 0u;
    previous.publish = 1u;
    if (attemptToken == 0u ||
        !LightingAccumulationIsFinite4(candidate) ||
        previous.count == UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT)
    {
        return previous;
    }

    candidate = LightingAccumulationSanitizeSample(candidate);
    const LightingAccumulationUint newCount = previous.count + 1u;
    previous.mean = previous.count == 0u
        ? candidate
        : LightingAccumulationLerp(
            previous.mean,
            candidate,
            1.0f / float(newCount));
    previous.count = newCount;
    previous.accepted = 1u;
    return previous;
}

#ifndef __cplusplus
#undef LightingAccumulationUint
#undef LightingAccumulationFloat4
#endif
#undef UVSR_LIGHTING_ACCUMULATION_INLINE

#endif // UVSR_LIGHTING_ACCUMULATION_CONTRACT_H
