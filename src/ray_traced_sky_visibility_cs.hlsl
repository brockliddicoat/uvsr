#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"
#include "noise_sampling.hlsli"
#include "pbr_gbuffer.hlsli"
#include "ray_origin_contract.h"
#include "ray_traced_material_visibility.hlsli"
#include "ray_visibility_trace_contract.h"
#include "ray_traced_sky_visibility_cb.h"
#include "sample_accumulation.hlsli"


cbuffer c_RayTracedSkyVisibility : register(b0)
{
    RayTracedSkyVisibilityConstants g_SkyVisibility;
};

#define RAY_VISIBILITY_CONSTANTS g_SkyVisibility
#define RAY_VISIBILITY_STOCHASTIC 1
#include "ray_visibility_receiver.hlsli"

static const float SkyVisibilityTwoPi = 6.28318530717958647692f;

float SkyVisibilityRadicalInverse(uint index, uint base)
{
    float inverseBase = rcp(float(base));
    float inversePower = inverseBase;
    float result = 0.0f;
    [loop]
    while (index > 0u)
    {
        uint digit = index % base;
        result += float(digit) * inversePower;
        index /= base;
        inversePower *= inverseBase;
    }
    return result;
}

float2 SkyVisibilitySample2D(
    uint2 dispatchPosition,
    uint sampleIndex,
    uint phase)
{
    const uint firstDimension =
        sampleIndex * 2u;
    const uint sequenceIndex = sampleIndex + 1u;
    const uint2 dispatchExtent =
        uint2(g_SkyVisibility.view.viewportSize);
    const float2 noiseShift = float2(
        UVSRSamplePrecomputedNoise(
            t_Noise,
            g_SkyVisibility.noisePattern,
            dispatchPosition,
            dispatchExtent,
            phase,
            0x300u + firstDimension),
        UVSRSamplePrecomputedNoise(
            t_Noise,
            g_SkyVisibility.noisePattern,
            dispatchPosition,
            dispatchExtent,
            phase,
            0x300u + firstDimension + 1u));
    return frac(float2(
        SkyVisibilityRadicalInverse(sequenceIndex, 2u),
        SkyVisibilityRadicalInverse(sequenceIndex, 3u)) + noiseShift);
}

float3 SkyVisibilitySampleCosineHemisphere(
    float3 geometricNormal,
    float2 sample)
{
    const float radialSquared = saturate(sample.x);
    const float radial = sqrt(radialSquared);
    const float phi = SkyVisibilityTwoPi * sample.y;
    const float normalDistance = sqrt(max(1.0f - radialSquared, 0.0f));
    const float3 helper = abs(geometricNormal.z) < 0.999f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(1.0f, 0.0f, 0.0f);
    const float3 tangent = PbrSafeNormalize(
        cross(helper, geometricNormal),
        float3(1.0f, 0.0f, 0.0f));
    const float3 bitangent = cross(geometricNormal, tangent);
    return PbrSafeNormalize(
        tangent * (cos(phi) * radial) +
            bitangent * (sin(phi) * radial) +
            geometricNormal * normalDistance,
        geometricNormal);
}

RayVisibilityTraceSample SkyVisibilityTrace(
    float3 rayOrigin,
    float3 direction)
{
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = g_SkyVisibility.rayDistance;

    RayQuery<
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(t_WorldBvh, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed())
    {
        UVSR_COMMIT_COVERED_RAY_QUERY_CANDIDATE(query)
    }
    const bool hit = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    return ResolveRayVisibilityTraceSample(hit);
}

float RayVisibilityEvaluate(int2 pixelPosition, uint2 dispatchPosition,
    uint sampleSequencePhase, float depth, float4 normalChannels)
{
    const float4 packedMaterial = t_GBufferMaterial[pixelPosition];
    const PbrGBufferSurfaceNormals surfaceNormals =
        DecodePbrGBufferSurfaceNormals(
            normalChannels,
            packedMaterial);
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    const float3 surfacePosition = ReconstructWorldPosition(
        g_SkyVisibility.view,
        pixelCenter,
        depth);
    if (!all(isfinite(surfacePosition)))
        return 1.0f;
    const float3 viewIncident = GetIncidentVector(
        g_SkyVisibility.view.cameraDirectionOrPosition,
        surfacePosition);
    const float3 viewDirection = -viewIncident;
    const float3 geometricNormal = RayOriginOrientGeometricNormal(
        surfaceNormals.geometricNormal,
        viewDirection);
    const float3 rayOrigin = RayVisibilityPrepareRayOrigin(
        surfacePosition,
        surfaceNormals.geometricNormal,
        viewDirection,
        pixelCenter,
        depth);

    const uint sampleCount = max(g_SkyVisibility.sampleCount, 1u);
    RayVisibilityTraceAggregate aggregate =
        BeginRayVisibilityTraceAggregate();
    [loop]
    for (uint sampleIndex = 0u;
        sampleIndex < sampleCount;
        ++sampleIndex)
    {
        const float3 direction = SkyVisibilitySampleCosineHemisphere(
            geometricNormal,
            SkyVisibilitySample2D(
                dispatchPosition,
                    sampleIndex,
                sampleSequencePhase));
        aggregate = AccumulateRayVisibilityTraceSample(
            aggregate,
            SkyVisibilityTrace(rayOrigin, direction));
    }

    if (!RayVisibilityTraceAggregateIsComplete(
        aggregate,
        sampleCount))
    {
        return 0.0f;
    }
    return ResolveRayVisibilityTraceAverage(aggregate);
}

[numthreads(8, 8, 1)]
void Generate(uint2 dispatchPosition : SV_DispatchThreadID)
{
    RayVisibilityGenerate(dispatchPosition);
}
