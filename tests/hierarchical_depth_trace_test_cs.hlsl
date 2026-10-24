#include "../src/hierarchical_depth_trace.hlsli"

// exact 32-byte layout mirrored by GpuTraceRay in the host test.
struct TraceRayRecord
{
    float2 beginPixel;
    float2 endPixel;
    float beginDepth;
    float endDepth;
    uint orthographic;
    uint padding;
};

cbuffer Constants : register(b0)
{
    uint2 g_FullSize;
    uint g_MipCount;
    float g_Thickness;
    uint g_RayCount;
    uint3 g_Padding;
};

Texture2D<float2> t_Hierarchy : register(t0);
StructuredBuffer<TraceRayRecord> t_Rays : register(t1);
RWStructuredBuffer<uint> u_Hits : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint rayIndex = dispatchThreadId.x;
    if (rayIndex >= g_RayCount)
        return;

    TraceRayRecord input = t_Rays[rayIndex];
    HzbScreenRay ray;
    ray.beginPixel = input.beginPixel;
    ray.endPixel = input.endPixel;
    ray.beginDepth = input.beginDepth;
    ray.endDepth = input.endDepth;
    ray.orthographic = input.orthographic;
    float2 hitPixel;
    u_Hits[rayIndex] = TraceHierarchicalDepth(t_Hierarchy, ray,
        g_FullSize, g_MipCount, g_Thickness, hitPixel) ? 1u : 0u;
}
