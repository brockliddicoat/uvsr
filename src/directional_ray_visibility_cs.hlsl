#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"
#include "directional_ray_visibility_cb.h"
#include "pbr_gbuffer.hlsli"
#include "ray_traced_material_visibility.hlsli"

#ifndef DIRECTIONAL_VISIBILITY_SAMPLES
#error DIRECTIONAL_VISIBILITY_SAMPLES must be 1, 2, 4, 8, or 16.
#endif

cbuffer c_DirectionalVisibility : register(b0)
{
    DirectionalRayVisibilityConstants g_DirectionalVisibility;
};

RaytracingAccelerationStructure t_WorldBvh : register(t0);
#if DIRECTIONAL_VISIBILITY_SAMPLES > 1
Texture2DMS<float, DIRECTIONAL_VISIBILITY_SAMPLES> t_Depth : register(t1);
Texture2DMS<float4, DIRECTIONAL_VISIBILITY_SAMPLES>
    t_Material : register(t2);
Texture2DMS<float4, DIRECTIONAL_VISIBILITY_SAMPLES>
    t_Normals : register(t3);
#else
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_Material : register(t2);
Texture2D<float4> t_Normals : register(t3);
#endif

#if DIRECTIONAL_VISIBILITY_SAMPLES > 1
RWTexture2DArray<float> u_Visibility : register(u0);
#else
RWTexture2D<float> u_Visibility : register(u0);
#endif
RWTexture2D<float> u_ClosestVisibility : register(u1);
RWTexture2D<float> u_ClosestHitDistance : register(u2);

static const float kMissHitDistance = 65504.0f;

float LoadDepth(int2 pixel, uint sampleIndex)
{
#if DIRECTIONAL_VISIBILITY_SAMPLES > 1
    return t_Depth.Load(pixel, sampleIndex);
#else
    return t_Depth[pixel];
#endif
}

float4 LoadMaterial(int2 pixel, uint sampleIndex)
{
#if DIRECTIONAL_VISIBILITY_SAMPLES > 1
    return t_Material.Load(pixel, sampleIndex);
#else
    return t_Material[pixel];
#endif
}

float4 LoadNormals(int2 pixel, uint sampleIndex)
{
#if DIRECTIONAL_VISIBILITY_SAMPLES > 1
    return t_Normals.Load(pixel, sampleIndex);
#else
    return t_Normals[pixel];
#endif
}

float StepDepthTowardCamera(float depth)
{
    if (g_DirectionalVisibility.floatDepth != 0u)
    {
        uint bits = asuint(saturate(depth));
        if (g_DirectionalVisibility.reverseDepth != 0u)
            bits = min(bits + 1u, asuint(1.0f));
        else
            bits = bits > 0u ? bits - 1u : 0u;
        const float stepped = asfloat(bits);
        return isfinite(stepped) ? saturate(stepped) : depth;
    }
    const float direction = g_DirectionalVisibility.reverseDepth != 0u
        ? 1.0f
        : -1.0f;
    return saturate(depth + direction *
        g_DirectionalVisibility.depthQuantizationStep);
}

float OffsetComponent(float position, float direction)
{
    static const float Origin = 1.0f / 32.0f;
    static const float FloatScale = 1.0f / 65536.0f;
    static const float IntegerScale = 256.0f;
    const int integerOffset = int(IntegerScale * direction);
    const float shifted = asfloat(asint(position) +
        (position < 0.0f ? -integerOffset : integerOffset));
    return abs(position) < Origin
        ? position + FloatScale * direction
        : shifted;
}

float3 PrepareRayOrigin(
    float3 position,
    float3 geometricNormal,
    float3 viewDirection,
    float2 pixelCenter,
    float depth)
{
    float3 normal = PbrSafeNormalize(geometricNormal, viewDirection);
    if (dot(normal, viewDirection) < 0.0f)
        normal = -normal;
    const float steppedDepth = StepDepthTowardCamera(depth);
    const float3 steppedPosition = ReconstructWorldPosition(
        g_DirectionalVisibility.view,
        pixelCenter,
        steppedDepth);
    const float depthClearance = all(isfinite(steppedPosition))
        ? length(steppedPosition - position)
        : 0.0f;
    const float clearance = max(
        g_DirectionalVisibility.rayBias,
        depthClearance);
    const float3 displaced = position + normal * clearance;
    return float3(
        OffsetComponent(displaced.x, normal.x),
        OffsetComponent(displaced.y, normal.y),
        OffsetComponent(displaced.z, normal.z));
}

bool TraceVisibility(
    float3 origin,
    float3 direction,
    out float hitDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = g_DirectionalVisibility.directionToLightAndDistance.w;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(t_WorldBvh, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed())
    {
        UVSR_COMMIT_COVERED_RAY_QUERY_CANDIDATE(query)
    }
    const bool visible =
        query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
    hitDistance = visible
        ? kMissHitDistance
        : min(query.CommittedRayT(), kMissHitDistance - 1.0f);
    return visible;
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchPosition : SV_DispatchThreadID)
{
    if (any(dispatchPosition >=
            uint2(g_DirectionalVisibility.view.viewportSize)))
    {
        return;
    }

    const int2 pixel = int2(dispatchPosition) +
        int2(g_DirectionalVisibility.view.viewportOrigin);
    const float2 pixelCenter = float2(pixel) + 0.5f;
    const float3 directionToLight =
        g_DirectionalVisibility.directionToLightAndDistance.xyz;
    bool foundClosest = false;
    float closestDepth = 0.0f;
    float closestVisibility = 1.0f;
    float closestHitDistance = kMissHitDistance;

    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < DIRECTIONAL_VISIBILITY_SAMPLES;
        ++sampleIndex)
    {
        float visibility = 1.0f;
        float hitDistance = kMissHitDistance;
        const float depth = LoadDepth(pixel, sampleIndex);
        const float4 normalChannels = LoadNormals(pixel, sampleIndex);
        const bool covered = isfinite(depth) && depth > 0.0f &&
            dot(normalChannels.xyz, normalChannels.xyz) > 1.e-12f;
        if (covered)
        {
            const PbrGBufferSurfaceNormals normals =
                DecodePbrGBufferSurfaceNormals(
                    normalChannels,
                    LoadMaterial(pixel, sampleIndex));
            const float3 position = ReconstructWorldPosition(
                g_DirectionalVisibility.view,
                pixelCenter,
                depth);
            if (all(isfinite(position)) &&
                dot(normals.shadingNormal, directionToLight) > 0.0f)
            {
                const float3 viewDirection = -GetIncidentVector(
                    g_DirectionalVisibility.view.cameraDirectionOrPosition,
                    position);
                const float3 origin = PrepareRayOrigin(
                    position,
                    normals.geometricNormal,
                    viewDirection,
                    pixelCenter,
                    depth);
                visibility = TraceVisibility(
                        origin,
                        directionToLight,
                        hitDistance)
                    ? 1.0f
                    : 0.0f;
            }
            const bool depthIsCloser = !foundClosest ||
                (g_DirectionalVisibility.reverseDepth != 0u
                    ? depth > closestDepth
                    : depth < closestDepth);
            if (depthIsCloser)
            {
                foundClosest = true;
                closestDepth = depth;
                closestVisibility = visibility;
                closestHitDistance = hitDistance;
            }
        }
#if DIRECTIONAL_VISIBILITY_SAMPLES > 1
        u_Visibility[uint3(uint2(pixel), sampleIndex)] = visibility;
#else
        u_Visibility[pixel] = visibility;
#endif
    }
    u_ClosestVisibility[pixel] = closestVisibility;
    u_ClosestHitDistance[pixel] = closestHitDistance;
}
