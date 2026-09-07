#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"
#include "directional_ray_visibility_cb.h"
#include "pbr_gbuffer.hlsli"
#include "ray_traced_material_visibility.hlsli"
#include "pbr_surface_light_contract.h"
#include "noise_sampling.hlsli"
#include "ray_origin_contract.h"
#include "sample_accumulation.hlsli"


cbuffer c_DirectionalVisibility : register(b0)
{
    DirectionalRayVisibilityConstants g_DirectionalVisibility;
};

#define RAY_VISIBILITY_CONSTANTS g_DirectionalVisibility
#define RAY_VISIBILITY_STOCHASTIC 1
#include "ray_visibility_receiver.hlsli"


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
    float3 direction)
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
    return visible;
}

float RayVisibilityEvaluate(int2 pixelPosition, uint2 dispatchPosition,
    uint sampleSequencePhase, float depth, float4 normalChannels)
{
    float visibility = 1.0f;
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    const float3 directionToLight = g_DirectionalVisibility.directionToLightAndDistance.xyz;
    const PbrGBufferSurfaceNormals normals =
        DecodePbrGBufferSurfaceNormals(
            normalChannels,
            t_GBufferMaterial[pixelPosition]);
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
        const float angularDiameter = g_DirectionalVisibility.angularDiameter;
        if (!(angularDiameter > 0.0f))
            return TraceVisibility(origin, directionToLight) ? 1.0f : 0.0f;

        const uint sampleCount = clamp(g_DirectionalVisibility.sampleCount, 1u, 64u);
        const uint2 extent = uint2(g_DirectionalVisibility.view.viewportSize);
        const float2 shift = float2(
            UVSRSamplePrecomputedNoise(t_Noise, g_DirectionalVisibility.noisePattern,
                dispatchPosition, extent, sampleSequencePhase, 0x200u),
            UVSRSamplePrecomputedNoise(t_Noise, g_DirectionalVisibility.noisePattern,
                dispatchPosition, extent, sampleSequencePhase, 0x201u));
        visibility = 0.0f;
        [loop]
        for (uint index = 0u; index < sampleCount; ++index)
        {
            const float2 random = frac(shift + float2(
                (float(index) + 0.5f) / float(sampleCount),
                float(index) * 0.6180339887498948482f));
            const float3 direction = SamplePbrDirectionalEmitter(directionToLight, angularDiameter, random);
            visibility += dot(normals.shadingNormal, direction) > 0.0f &&
                TraceVisibility(origin, direction) ? 1.0f : 0.0f;
        }
        visibility /= float(sampleCount);
    }
    return visibility;
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchPosition : SV_DispatchThreadID)
{
    RayVisibilityGenerate(dispatchPosition);
}
