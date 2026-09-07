#ifndef UVSR_LIGHTING_ACCUMULATION_CONTRACT_H
#define UVSR_LIGHTING_ACCUMULATION_CONTRACT_H

#define UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT 0xffffffffu

#include "shader_math.h"

#ifdef __cplusplus
inline ShaderFloat4 LightingAccumulationSanitizeSample(
    ShaderFloat4 value) noexcept
{
    return {
        std::max(value.x, 0.f),
        std::max(value.y, 0.f),
        std::max(value.z, 0.f),
        1.f
    };
}
#else
float4 LightingAccumulationSanitizeSample(float4 value)
{
    value.rgb = max(value.rgb, 0.0f);
    value.a = 1.0f;
    return value;
}
#endif

struct LightingAccumulationState
{
    ShaderFloat4 mean;
    ShaderUint count;
    ShaderUint attempted;
    ShaderUint accepted;
    ShaderUint publish;
};

UVSR_SHADER_INLINE ShaderUint
    ResolveLightingAccumulationAttemptToken(
        ShaderUint previousCount,
        bool resetHistory)
{
    const ShaderUint count = resetHistory
        ? 0u
        : previousCount;
    return (count < UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT
            ? count
            : UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT - 1u) + 1u;
}

UVSR_SHADER_INLINE LightingAccumulationState
    RepairLightingAccumulation(
        ShaderFloat4 previousMean,
        ShaderUint previousCount,
        bool resetHistory)
{
    LightingAccumulationState result;
    result.mean = previousMean;
    result.count = previousCount;
    result.attempted = 0u;
    result.accepted = 0u;
    result.publish = 1u;
    if (resetHistory || previousCount == 0u ||
        !ShaderIsFinite4(previousMean))
    {
        result.mean = ShaderMakeFloat4(
            0.0f, 0.0f, 0.0f, 0.0f);
        result.count = 0u;
    }
    return result;
}

UVSR_SHADER_INLINE LightingAccumulationState
    ResolveLightingAccumulationCandidate(
        LightingAccumulationState previous,
        ShaderUint attemptToken,
        ShaderFloat4 candidate)
{
    previous.attempted = attemptToken != 0u ? 1u : 0u;
    previous.accepted = 0u;
    previous.publish = 1u;
    if (attemptToken == 0u ||
        !ShaderIsFinite4(candidate) ||
        previous.count == UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT)
    {
        return previous;
    }

    candidate = LightingAccumulationSanitizeSample(candidate);
    const ShaderUint newCount = previous.count + 1u;
    previous.mean = previous.count == 0u
        ? candidate
        : ShaderLerp4(
            previous.mean,
            candidate,
            1.0f / float(newCount));
    previous.count = newCount;
    previous.accepted = 1u;
    return previous;
}


#endif // UVSR_LIGHTING_ACCUMULATION_CONTRACT_H
