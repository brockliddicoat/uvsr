#pragma pack_matrix(row_major)
#include "screen_space_directional_shadows_cb.h"

cbuffer Constants : register(b0) { ScreenSpaceShadowTraceConstants g_Shadow; };
Texture2D<float> t_Sample : register(t0);
Texture2D<float4> t_Normal : register(t1);
RWTexture2D<float> u_Visibility : register(u0);
RWTexture2D<float> u_Accumulation : register(u1);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_Shadow.resolution)) return;
    float previous = g_Shadow.rayIndex == 0u ? 0.0f : u_Accumulation[pixel];
    // each emitter direction contributes binary visibility. sample-distance
    // fading and four-bucket smoothing are excluded from the Bend permutation.
    float visible = t_Sample[pixel] >= 0.5f ? 1.0f : 0.0f;
    float3 normal = t_Normal[pixel].xyz;
    normal *= rsqrt(max(dot(normal, normal), 1e-8f));
    float totalWeight = 0.0f;
    for (uint i = 0u; i < g_Shadow.rayCount; ++i)
        totalWeight += max(dot(normal, g_Shadow.lightDirections[i].xyz), 0.0f);
    float weight = max(dot(normal, g_Shadow.lightDirections[g_Shadow.rayIndex].xyz), 0.0f);
    float accumulated = totalWeight > 0.0f
        ? saturate(previous + visible * weight / totalWeight) : 1.0f;
    u_Accumulation[pixel] = accumulated;
    if (g_Shadow.rayIndex + 1u == g_Shadow.rayCount)
        u_Visibility[pixel] = accumulated;
}
