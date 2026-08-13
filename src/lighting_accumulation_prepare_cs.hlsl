#include "lighting_accumulation_cb.h"

cbuffer c_LightingAccumulation : register(b0)
{
    LightingAccumulationConstants g_Accumulation;
};

Texture2D<float4> t_PreviousMean : register(t0);
Texture2D<uint> t_PreviousCount : register(t1);
RWTexture2D<uint> u_AttemptMask : register(u0);

uint LightingAccumulationScheduleHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

float LightingAccumulationScheduleRandom(uint2 pixel)
{
    const uint seed = pixel.x * 0x9e3779b9u ^
        pixel.y * 0x85ebca6bu ^
        g_Accumulation.schedulingSerialLow * 0xc2b2ae35u ^
        g_Accumulation.schedulingSerialHigh * 0x27d4eb2du;
    return float(LightingAccumulationScheduleHash(seed) >> 8u) *
        (1.0f / 16777216.0f);
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

    const float retryProbability = rcp(float(previousCount) + 1.0f);
    u_AttemptMask[pixel] =
        LightingAccumulationScheduleRandom(pixel) < retryProbability
            ? 1u
            : 0u;
}
