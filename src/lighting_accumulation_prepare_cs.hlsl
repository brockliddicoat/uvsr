#include "lighting_accumulation_cb.h"
#include "sample_accumulation.hlsli"

cbuffer c_LightingAccumulation : register(b0)
{
    LightingAccumulationConstants g_Accumulation;
};

Texture2D<float4> t_PreviousMean : register(t0);
Texture2D<uint> t_PreviousCount : register(t1);
Texture2D<float4> t_PreviousColorVariance : register(t2);
RWTexture2D<uint> u_AttemptMask : register(u0);

uint LightingAccumulationScheduleHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

uint LightingAccumulationSchedulePhase(uint2 pixel, uint interval)
{
    const uint seed = pixel.x * 0x9e3779b9u ^
        pixel.y * 0x85ebca6bu ^ 0xc2b2ae35u;
    return interval > 1u
        ? LightingAccumulationScheduleHash(seed) % interval
        : 0u;
}

uint LightingAccumulationScheduleSerialModulo(uint interval)
{
    if (interval <= 1u)
        return 0u;
    const uint twoTo32Modulo =
        ((0xffffffffu % interval) + 1u) % interval;
    const uint highContribution =
        (g_Accumulation.schedulingSerialHigh % interval) *
            twoTo32Modulo;
    return (
        (g_Accumulation.schedulingSerialLow % interval) +
        highContribution) % interval;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_Accumulation.extent))
        return;

    if (g_Accumulation.resetHistory != 0u)
    {
        u_AttemptMask[pixel] = 1u;
        return;
    }

    const uint previousCount = t_PreviousCount[pixel];
    const float4 previousMean = t_PreviousMean[pixel];
    if (previousCount == 0u || !all(isfinite(previousMean)))
    {
        u_AttemptMask[pixel] = 1u;
        return;
    }

    const float3 previousVariance =
        t_PreviousColorVariance[pixel].rgb;
    const uint updateInterval = UvsrSampleUpdateInterval(
        g_Accumulation.scheduling,
        g_Accumulation.averaging,
        g_Accumulation.effectiveHistory,
        g_Accumulation.minimumSamples,
        g_Accumulation.targetRelativeError,
        g_Accumulation.minimumUpdateRate,
        previousCount,
        previousMean.rgb,
        previousVariance);
    const uint schedulePhase =
        LightingAccumulationSchedulePhase(pixel, updateInterval);
    // Advance a pixel's congruence class after every successful sample. The
    // resulting revisit gap is always odd, covering every phase of UVSR's
    // power-of-two projection-jitter sequences without exceeding the selected
    // bounded revisit interval.
    const uint coverageStep = updateInterval > 1u
        ? ((updateInterval & 1u) == 0u ? 1u : 0u)
        : 0u;
    const uint successfulPhaseAdvance = updateInterval > 1u
        ? (previousCount % updateInterval) * coverageStep
        : 0u;
    const uint scheduleSerialModulo =
        LightingAccumulationScheduleSerialModulo(updateInterval);
    const bool update = updateInterval == 1u ||
        (scheduleSerialModulo + schedulePhase + successfulPhaseAdvance) %
            updateInterval == 0u;
    // Zero means skip. Nonzero values encode the accepted sample's zero-based
    // stochastic sequence phase plus one.
    u_AttemptMask[pixel] = update
        ? min(previousCount, 0xfffffffeu) + 1u
        : 0u;
}
