#ifndef UVSR_PATH_TRACING_FIREFLY_CONTRACT_H
#define UVSR_PATH_TRACING_FIREFLY_CONTRACT_H

// algorithm reference: RTXPT f08d1c739071e0faad0c7c274d861124c511abab.
// independent implementation of its probability-scaled radiance cap.
// see legal/documentation/nvidia-rtxpt.md for provenance.

#include "shader_math.h"

UVSR_SHADER_INLINE float PathTracingAdvanceFireflyFilter(
    float previous, float samplingPdf, float lobeProbability)
{
    float coneProbability = 1.f;
    if (samplingPdf > 0.f)
    {
        const float solidAngle = 1.f / samplingPdf;
        const float cosine = ShaderMax(-1.f,
            1.f - solidAngle * 0.1591549430918953f);
        const float halfAngle = ShaderAcos(cosine);
        coneProbability = 1.f / (1.f + halfAngle * halfAngle * 0.125f);
    }
    const float selectedLobeWeight = ShaderSqrt(ShaderSaturate(lobeProbability));
    return ShaderMax(1.e-5f, previous * selectedLobeWeight * coneProbability);
}

UVSR_SHADER_INLINE ShaderFloat3 PathTracingFilterFirefly(
    ShaderFloat3 radiance, float threshold, float scatterFactor)
{
    // clamp each incoming event before throughput, never the accumulated mean.
    // the RGB average preserves hue. zero threshold is the exact bypass.
    if (!(threshold > 0.f))
        return radiance;
    const float average = radiance.x / 3.f + radiance.y / 3.f + radiance.z / 3.f;
    const float limit = threshold * scatterFactor;
    return average > limit
        ? ShaderScale3(radiance, limit / average)
        : radiance;
}

#endif
