#include "lighting_accumulation_cb.h"
#include "lighting_accumulation_contract.h"

cbuffer c_LightingAccumulation : register(b0)
{
    LightingAccumulationConstants g_Accumulation;
};

Texture2D<float4> t_CurrentSample : register(t0);
Texture2D<float4> t_PreviousMean : register(t1);
Texture2D<uint> t_PreviousCount : register(t2);
Texture2D<uint> t_AttemptMask : register(t3);
RWTexture2D<float4> u_Mean : register(u0);
RWTexture2D<uint> u_Count : register(u1);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_Accumulation.extent))
        return;

    const bool resetHistory = g_Accumulation.resetHistory != 0u;
    uint previousCount = 0u;
    float4 previousMean = 0.0f;
    if (!resetHistory)
    {
        previousCount = t_PreviousCount[pixel];
        if (previousCount != 0u)
            previousMean = t_PreviousMean[pixel];
    }
    LightingAccumulationState state = RepairLightingAccumulation(
        previousMean,
        previousCount,
        resetHistory);

    const uint attemptToken = t_AttemptMask[pixel];
    if (attemptToken == 0u)
    {
        u_Mean[pixel] = state.mean;
        u_Count[pixel] = state.count;
        return;
    }

    state = ResolveLightingAccumulationCandidate(
        state,
        attemptToken,
        t_CurrentSample[pixel]);
    u_Mean[pixel] = state.mean;
    u_Count[pixel] = state.count;
}
