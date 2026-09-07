#ifndef UVSR_PATH_TRACING_ACCUMULATION_CONTRACT_H
#define UVSR_PATH_TRACING_ACCUMULATION_CONTRACT_H

#define UVSR_PATH_TRACING_SATURATED_SAMPLE_COUNT 0xffffffffu

#include "shader_math.h"

#ifdef __cplusplus
inline ShaderFloat3 PathTracingAccumulationMaxZero(
    ShaderFloat3 value) noexcept
{
    return {
        value.x > 0.f ? value.x : 0.f,
        value.y > 0.f ? value.y : 0.f,
        value.z > 0.f ? value.z : 0.f
    };
}
#else
float3 PathTracingAccumulationMaxZero(float3 value)
{
    return max(value, 0.0f);
}
#endif

struct PathTracingAccumulationState
{
    ShaderFloat3 mean;
    ShaderUint count;
    ShaderUint accepted;
    ShaderUint publish;
};

UVSR_SHADER_INLINE PathTracingAccumulationState
    RepairPathTracingAccumulation(
        ShaderFloat3 previousMean,
        ShaderUint previousCount)
{
    PathTracingAccumulationState result;
    result.mean = previousMean;
    result.count = previousCount;
    result.accepted = 0u;
    result.publish = 0u;
    if (!ShaderIsFinite3(previousMean))
    {
        result.mean = ShaderMakeFloat3(
            0.0f, 0.0f, 0.0f);
        result.count = 0u;
        result.publish = 1u;
    }
    return result;
}

UVSR_SHADER_INLINE PathTracingAccumulationState
    ResolvePathTracingAccumulation(
        PathTracingAccumulationState previous,
        ShaderFloat3 sample,
        bool attemptValid)
{
    previous.accepted = 0u;
    if (previous.count == UVSR_PATH_TRACING_SATURATED_SAMPLE_COUNT)
        return previous;
    if (!attemptValid ||
        !ShaderIsFinite3(sample) ||
        !ShaderIsNonnegative3(sample))
    {
        return previous;
    }

    const ShaderUint newCount = previous.count + 1u;
    previous.mean = previous.count == 0u
        ? sample
        : ShaderLerp3(
            previous.mean,
            sample,
            1.0f / float(newCount));
    previous.mean = PathTracingAccumulationMaxZero(previous.mean);
    previous.count = newCount;
    previous.accepted = 1u;
    previous.publish = 1u;
    return previous;
}


#endif // UVSR_PATH_TRACING_ACCUMULATION_CONTRACT_H
