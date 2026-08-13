#include "lighting_accumulation_cb.h"
#include "sample_accumulation.hlsli"

cbuffer c_LightingAccumulation : register(b0)
{
    LightingAccumulationConstants g_Accumulation;
};

Texture2D<float4> t_CurrentSample : register(t0);
Texture2D<float4> t_PreviousMean : register(t1);
Texture2D<uint> t_PreviousCount : register(t2);
Texture2D<float4> t_PreviousColorVariance : register(t3);
Texture2D<uint> t_AttemptMask : register(t4);
RWTexture2D<float4> u_Mean : register(u0);
RWTexture2D<uint> u_Count : register(u1);
RWTexture2D<float4> u_ColorVariance : register(u2);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_Accumulation.extent))
        return;

    uint previousCount = g_Accumulation.resetHistory != 0u
        ? 0u
        : t_PreviousCount[pixel];
    float4 previousMean = previousCount == 0u
        ? 0.0f
        : t_PreviousMean[pixel];
    float3 previousVariance = previousCount == 0u
        ? 0.0f
        : t_PreviousColorVariance[pixel].rgb;
    if (previousCount > 0u &&
        (!all(isfinite(previousMean)) ||
            !all(isfinite(previousVariance))))
    {
        previousMean = 0.0f;
        previousVariance = 0.0f;
        previousCount = 0u;
    }

    const bool accumulating = g_Accumulation.accumulateSamples != 0u;
    if (accumulating && t_AttemptMask[pixel] == 0u)
    {
        u_Mean[pixel] = previousMean;
        u_Count[pixel] = previousCount;
        u_ColorVariance[pixel] = float4(previousVariance, 0.0f);
        return;
    }

    float4 candidate = t_CurrentSample[pixel];
    const bool candidateValid = all(isfinite(candidate));
    if (!candidateValid)
    {
        // A numerical failure is not a successful sample. Preserve the last
        // finite history exactly, including a zero-count black history.
        u_Mean[pixel] = previousMean;
        u_Count[pixel] = previousCount;
        u_ColorVariance[pixel] = float4(previousVariance, 0.0f);
        return;
    }
    candidate.rgb = max(candidate.rgb, 0.0f);
    candidate.a = 1.0f;

    const uint newCount = accumulating
        ? (previousCount == 0xffffffffu ? previousCount : previousCount + 1u)
        : 1u;
    const float weight = accumulating
        ? UvsrSampleMeanWeight(
            g_Accumulation.averaging,
            g_Accumulation.effectiveHistory,
            previousCount,
            newCount)
        : 1.0f;
    const float4 newMean = previousCount == 0u || !accumulating
        ? candidate
        : lerp(previousMean, candidate, weight);
    const float3 newVariance = previousCount == 0u || !accumulating
        ? 0.0f
        : UvsrSampleVarianceUpdate(
            g_Accumulation.averaging,
            previousCount,
            newCount,
            previousVariance,
            previousMean.rgb,
            candidate.rgb,
            newMean.rgb,
            weight);
    u_Mean[pixel] = newMean;
    u_Count[pixel] = newCount;
    u_ColorVariance[pixel] = float4(newVariance, 0.0f);
}
