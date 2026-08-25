#ifndef UVSR_PATH_TRACING_ACCUMULATION_CONTRACT_H
#define UVSR_PATH_TRACING_ACCUMULATION_CONTRACT_H

#define UVSR_PATH_TRACING_SATURATED_SAMPLE_COUNT 0xffffffffu

#ifdef __cplusplus

#include <cmath>
#include <cstdint>

using PathTracingAccumulationUint = std::uint32_t;

struct PathTracingAccumulationFloat3
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

inline bool PathTracingAccumulationIsFinite3(
    PathTracingAccumulationFloat3 value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

inline bool PathTracingAccumulationIsNonnegative3(
    PathTracingAccumulationFloat3 value) noexcept
{
    return value.x >= 0.f && value.y >= 0.f && value.z >= 0.f;
}

inline PathTracingAccumulationFloat3 PathTracingAccumulationMakeFloat3(
    float x,
    float y,
    float z) noexcept
{
    return { x, y, z };
}

inline PathTracingAccumulationFloat3 PathTracingAccumulationLerp(
    PathTracingAccumulationFloat3 left,
    PathTracingAccumulationFloat3 right,
    float weight) noexcept
{
    return {
        left.x + (right.x - left.x) * weight,
        left.y + (right.y - left.y) * weight,
        left.z + (right.z - left.z) * weight
    };
}

inline PathTracingAccumulationFloat3 PathTracingAccumulationMaxZero(
    PathTracingAccumulationFloat3 value) noexcept
{
    return {
        value.x > 0.f ? value.x : 0.f,
        value.y > 0.f ? value.y : 0.f,
        value.z > 0.f ? value.z : 0.f
    };
}

#define UVSR_PATH_ACCUMULATION_INLINE inline

#else

#define PathTracingAccumulationUint uint
#define PathTracingAccumulationFloat3 float3

bool PathTracingAccumulationIsFinite3(float3 value)
{
    return all(isfinite(value));
}

bool PathTracingAccumulationIsNonnegative3(float3 value)
{
    return all(value >= 0.0f);
}

float3 PathTracingAccumulationMakeFloat3(float x, float y, float z)
{
    return float3(x, y, z);
}

float3 PathTracingAccumulationLerp(
    float3 left,
    float3 right,
    float weight)
{
    return lerp(left, right, weight);
}

float3 PathTracingAccumulationMaxZero(float3 value)
{
    return max(value, 0.0f);
}

#define UVSR_PATH_ACCUMULATION_INLINE

#endif

struct PathTracingAccumulationState
{
    PathTracingAccumulationFloat3 mean;
    PathTracingAccumulationUint count;
    PathTracingAccumulationUint accepted;
    PathTracingAccumulationUint publish;
};

UVSR_PATH_ACCUMULATION_INLINE PathTracingAccumulationState
    RepairPathTracingAccumulation(
        PathTracingAccumulationFloat3 previousMean,
        PathTracingAccumulationUint previousCount)
{
    PathTracingAccumulationState result;
    result.mean = previousMean;
    result.count = previousCount;
    result.accepted = 0u;
    result.publish = 0u;
    if (!PathTracingAccumulationIsFinite3(previousMean))
    {
        result.mean = PathTracingAccumulationMakeFloat3(
            0.0f, 0.0f, 0.0f);
        result.count = 0u;
        result.publish = 1u;
    }
    return result;
}

UVSR_PATH_ACCUMULATION_INLINE PathTracingAccumulationState
    ResolvePathTracingAccumulation(
        PathTracingAccumulationState previous,
        PathTracingAccumulationFloat3 sample,
        bool attemptValid)
{
    previous.accepted = 0u;
    if (previous.count == UVSR_PATH_TRACING_SATURATED_SAMPLE_COUNT)
        return previous;
    if (!attemptValid ||
        !PathTracingAccumulationIsFinite3(sample) ||
        !PathTracingAccumulationIsNonnegative3(sample))
    {
        return previous;
    }

    const PathTracingAccumulationUint newCount = previous.count + 1u;
    previous.mean = previous.count == 0u
        ? sample
        : PathTracingAccumulationLerp(
            previous.mean,
            sample,
            1.0f / float(newCount));
    previous.mean = PathTracingAccumulationMaxZero(previous.mean);
    previous.count = newCount;
    previous.accepted = 1u;
    previous.publish = 1u;
    return previous;
}

#ifndef __cplusplus
#undef PathTracingAccumulationUint
#undef PathTracingAccumulationFloat3
#endif
#undef UVSR_PATH_ACCUMULATION_INLINE

#endif // UVSR_PATH_TRACING_ACCUMULATION_CONTRACT_H
