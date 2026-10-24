#ifndef UVSR_DENOISING_REPLAY_SAMPLING_HLSLI
#define UVSR_DENOISING_REPLAY_SAMPLING_HLSLI

// each sparse texel identifies an exact previous sampling pixel. the caller
// uses its previous camera, surface data and random phase with current lighting.
Texture2D<uint> t_ReplaySelection : register(t20);
Texture2D<float> t_ReplayDepth : register(t21);
Texture2D<float4> t_ReplayNormal : register(t22);
Texture2D<float4> t_ReplayPreviousSignal : register(t23);
Texture2D<float4> t_ReplayPreviousGi : register(t24);
#include "denoising_replay_sampling_cb.h"
cbuffer c_ReplaySampling : register(b1) { DenoisingReplaySamplingConstants g_ReplaySampling; };
RWTexture2D<float4> u_ReplayGradient : register(u6);
#if REPLAY_GI
RWTexture2D<float4> u_ReplayGiGradient : register(u7);
#endif

bool ReplaySelect(uint2 tile, out uint2 source)
{
    source = 0u;
    if (any(tile >= g_ReplaySampling.gradientSize)) return false;
    u_ReplayGradient[tile] = 0.0f;
#if REPLAY_GI
    u_ReplayGiGradient[tile] = 0.0f;
#endif
    uint rank = t_ReplaySelection[tile];
    if (g_ReplaySampling.valid == 0u || rank == 0xffffffffu) return false;
    uint index = (rank - g_ReplaySampling.frame) * 0x0e8b2f51u;
    source = uint2(index % g_ReplaySampling.sourceSize.x, index / g_ReplaySampling.sourceSize.x);
    return all(source < g_ReplaySampling.sourceSize);
}
float4 ReplayDifference(float3 previous, float3 current)
{
    if (!all(isfinite(previous)) || !all(isfinite(current))) return 0.0f;
    float a = dot(max(previous, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
    float b = dot(max(current, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
    return float4(abs(a - b), max(a, b), 1.0f, 0.0f);
}
#endif
