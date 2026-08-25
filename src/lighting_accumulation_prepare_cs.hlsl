#include "lighting_accumulation_cb.h"
#include "lighting_accumulation_contract.h"

cbuffer c_LightingAccumulation : register(b0)
{
    LightingAccumulationConstants g_Accumulation;
};

Texture2D<uint> t_PreviousCount : register(t0);
RWTexture2D<uint> u_AttemptMask : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_Accumulation.extent))
        return;

    const bool resetHistory = g_Accumulation.resetHistory != 0u;
    uint previousCount = 0u;
    if (!resetHistory)
        previousCount = t_PreviousCount[pixel];
    // Every pixel is attempted. The nonzero token is the accepted sample's
    // zero-based stochastic sequence phase plus one.
    u_AttemptMask[pixel] = ResolveLightingAccumulationAttemptToken(
        previousCount,
        resetHistory);
}
