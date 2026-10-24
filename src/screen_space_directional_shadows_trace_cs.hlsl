#pragma pack_matrix(row_major)
#include "screen_space_directional_shadows_cb.h"
#include "hierarchical_depth_trace.hlsli"

cbuffer Constants : register(b0) { ScreenSpaceShadowTraceConstants g_Shadow; };
Texture2D<float> t_Depth : register(t0);
Texture2D<float4> t_Normal : register(t1);
Texture2D<float2> t_Hierarchy : register(t2);
RWTexture2D<float> u_Visibility : register(u0);

float2 ProjectPixel(float3 position)
{
    float4 clip = mul(float4(position, 1.0f), g_Shadow.view.matViewToClip);
    return clip.xy / clip.w * g_Shadow.view.clipToWindowScale +
        g_Shadow.view.clipToWindowBias;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_Shadow.resolution)) return;
    float depth = t_Depth[pixel];
    bool valid = isfinite(depth) && (g_Shadow.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f);
    float3 normal = t_Normal[pixel].xyz;
    if (!valid || dot(normal, normal) < 1e-8f)
    {
        u_Visibility[pixel] = 1.0f;
        return;
    }
    normal = normalize(mul(float4(normal, 0.0f), g_Shadow.view.matWorldToView).xyz);
    float2 ndc = (float2(pixel) + 0.5f - g_Shadow.view.clipToWindowBias) /
        g_Shadow.view.clipToWindowScale;
    float4 view = mul(float4(ndc, depth, 1.0f), g_Shadow.view.matClipToView);
    float3 receiver = view.xyz / view.w;
    if (any(!isfinite(receiver)))
    {
        u_Visibility[pixel] = 1.0f;
        return;
    }
    uint width, height, mipCount;
    t_Hierarchy.GetDimensions(0u, width, height, mipCount);
    float visibility = 0.0f;
    float totalWeight = 0.0f;
    [loop]
    for (uint rayIndex = 0u; rayIndex < g_Shadow.rayCount; ++rayIndex)
    {
        float3 direction = mul(g_Shadow.lightDirections[rayIndex],
            g_Shadow.view.matWorldToView).xyz;
        float weight = max(dot(normal, direction), 0.0f);
        totalWeight += weight;
        if (weight <= 0.0f) continue;
        float bias = max(0.001f, abs(receiver.z) * 1e-5f);
        float3 origin = receiver + normal * bias;
        float4 originClip = mul(float4(origin, 1.0f), g_Shadow.view.matViewToClip);
        float4 directionClip = mul(float4(direction, 0.0f), g_Shadow.view.matViewToClip);
        float2 screenDirection = (directionClip.xy * originClip.w -
            originClip.xy * directionClip.w) * g_Shadow.view.clipToWindowScale;
        float major = max(abs(screenDirection.x), abs(screenDirection.y));
        if (!(major > 1e-8f)) { visibility += weight; continue; }
        float2 pixelDelta = screenDirection / major * float(g_Shadow.traceReach);
        // away-facing rays stop at the vanishing point. solve the projective
        // parameter for the requested screen reach, rather than guessing meters.
        if (directionClip.w > 0.0f)
        {
            float2 limitDelta = (directionClip.xy / directionClip.w -
                originClip.xy / originClip.w) * g_Shadow.view.clipToWindowScale;
            float limitReach = max(abs(limitDelta.x), abs(limitDelta.y));
            pixelDelta *= min(1.0f, limitReach * 0.99999f / float(g_Shadow.traceReach));
        }
        float2 targetNdc = originClip.xy / originClip.w +
            pixelDelta / g_Shadow.view.clipToWindowScale;
        uint axis = abs(screenDirection.x) >= abs(screenDirection.y) ? 0u : 1u;
        float denominator = directionClip[axis] - targetNdc[axis] * directionClip.w;
        float distance = (targetNdc[axis] * originClip.w - originClip[axis]) / denominator;
        if (!isfinite(distance) || distance <= 0.0f) { visibility += weight; continue; }
        float3 end = origin + direction * distance;
        HzbScreenRay ray;
        ray.beginPixel = ProjectPixel(origin);
        ray.endPixel = ProjectPixel(end);
        ray.beginDepth = abs(origin.z);
        ray.endDepth = abs(end.z);
        ray.orthographic = g_Shadow.orthographic;
        float2 delta = ray.endPixel - ray.beginPixel;
        float reach = max(abs(delta.x), abs(delta.y));
        if (reach > float(g_Shadow.traceReach))
        {
            float limit = float(g_Shadow.traceReach) / reach;
            ray.endDepth = HzbRayDepth(ray, limit);
            ray.endPixel = lerp(ray.beginPixel, ray.endPixel, limit);
        }
        float2 hitPixel;
        bool hit = TraceHierarchicalDepth(t_Hierarchy, ray, g_Shadow.resolution,
            mipCount, g_Shadow.thicknessWorld, hitPixel);
        visibility += hit ? 0.0f : weight;
    }
    u_Visibility[pixel] = totalWeight > 0.0f ? visibility / totalWeight : 1.0f;
}
