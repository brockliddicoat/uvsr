#pragma pack_matrix(row_major)
#include "renderer_gpu_helpers.hlsli"
#include "denoising_cb.h"

cbuffer c_Replay : register(b0) { DenoisingConstants g_Replay; };
Texture2D<float> t_Depth : register(t0);
Texture2D<float4> t_Normal : register(t1);
Texture2D<float4> t_Material : register(t2);
Texture2D<float4> t_Motion : register(t3);
Texture2D<float> t_PreviousDepth : register(t4);
Texture2D<float4> t_PreviousNormal : register(t5);

#if REPLAY_STAGE == 0
RWTexture2D<float> u_Depth : register(u0);
RWTexture2D<float4> u_Normal : register(u1);
RWTexture2D<float4> u_Material : register(u2);
[numthreads(16, 16, 1)]
void main(uint2 p : SV_DispatchThreadID)
{
    if (any(p >= g_Replay.fullSize)) return;
    u_Depth[p] = t_Depth[p];
    u_Normal[p] = t_Normal[p];
    u_Material[p] = t_Material[p];
}
#else
RWTexture2D<uint> u_Selection : register(u0);
[numthreads(16, 16, 1)]
void main(uint2 source : SV_DispatchThreadID)
{
    if (any(source >= g_Replay.sourceSize) || g_Replay.resetHistory != 0u) return;
    uint2 p = min(source * g_Replay.dilation + g_Replay.dilation / 2u, g_Replay.fullSize - 1u);
    float depth = t_PreviousDepth[p];
    float3 normal = t_PreviousNormal[p].xyz;
    if (!(dot(normal, normal) > 1e-12f) || !isfinite(depth)) return;
    float3 world = ReconstructWorldPosition(g_Replay.previousView, float2(p) + 0.5f, depth);
    float4 clip = mul(float4(world, 1.0f), g_Replay.view.matWorldToClip);
    if (!all(isfinite(clip)) || clip.w <= 0.0f) return;
    float3 ndc = clip.xyz / clip.w;
    float2 pixel = ndc.xy * g_Replay.view.clipToWindowScale + g_Replay.view.clipToWindowBias;
    if (any(pixel < 0.0f) || any(pixel >= float2(g_Replay.fullSize))) return;
    uint2 current = uint2(pixel);
    float4 motion = t_Motion[current];
    if (motion.w <= 0.0f || any(abs(float2(current) + motion.xy - float2(p)) > 1.5f)) return;
    float3 visible = ReconstructWorldPosition(g_Replay.view, float2(current) + 0.5f, t_Depth[current]);
    float3 viewPosition = mul(float4(world, 1.0f), g_Replay.view.matWorldToView).xyz;
    float tolerance = max(abs(viewPosition.z) * 0.005f, 0.002f);
    if (length(visible - world) > tolerance || dot(normal, t_Normal[current].xyz) < 0.95f) return;
    // multiplication by an odd integer permutes the ids without collisions.
    uint rank = (source.y * g_Replay.sourceSize.x + source.x) * 0x9e3779b1u + g_Replay.frameIndex;
    uint ignored;
    InterlockedMin(u_Selection[current / 3u], rank, ignored);
}
#endif
