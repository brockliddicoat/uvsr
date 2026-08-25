#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"
#include "noise_sampling.hlsli"
#include "pbr_gbuffer.hlsli"
#include "ray_origin_contract.h"
#include "ray_traced_material_visibility.hlsli"
#include "ray_visibility_trace_contract.h"
#include "ray_traced_sky_visibility_bindings.h"
#include "ray_traced_sky_visibility_cb.h"
#include "sample_accumulation.hlsli"

#ifndef OUTPUT_HIT_DISTANCE
#define OUTPUT_HIT_DISTANCE 0
#endif
#ifndef SKY_VISIBILITY_SAMPLES
#error SKY_VISIBILITY_SAMPLES must be 1, 2, 4, 8, or 16.
#endif

cbuffer c_RayTracedSkyVisibility :
    register(UVSR_SKY_VISIBILITY_CONSTANT_BUFFER_REGISTER)
{
    RayTracedSkyVisibilityConstants g_SkyVisibility;
};

RaytracingAccelerationStructure t_WorldBvh :
    register(UVSR_SKY_VISIBILITY_WORLD_TLAS_REGISTER);
#if SKY_VISIBILITY_SAMPLES > 1
Texture2DMS<float, SKY_VISIBILITY_SAMPLES> t_Depth :
    register(UVSR_SKY_VISIBILITY_DEPTH_REGISTER);
Texture2DMS<float4, SKY_VISIBILITY_SAMPLES>
    t_GBufferMaterial : register(UVSR_SKY_VISIBILITY_MATERIAL_REGISTER);
Texture2DMS<float4, SKY_VISIBILITY_SAMPLES>
    t_GBufferNormals : register(UVSR_SKY_VISIBILITY_NORMALS_REGISTER);
#else
Texture2D<float> t_Depth : register(UVSR_SKY_VISIBILITY_DEPTH_REGISTER);
Texture2D<float4> t_GBufferMaterial :
    register(UVSR_SKY_VISIBILITY_MATERIAL_REGISTER);
Texture2D<float4> t_GBufferNormals :
    register(UVSR_SKY_VISIBILITY_NORMALS_REGISTER);
#endif
Texture2DArray<float> t_Noise :
    register(UVSR_SKY_VISIBILITY_NOISE_REGISTER);
Texture2D<uint> t_AttemptMask :
    register(UVSR_SKY_VISIBILITY_ATTEMPT_MASK_REGISTER);

#if SKY_VISIBILITY_SAMPLES > 1
RWTexture2DArray<float> u_Visibility :
    register(UVSR_SKY_VISIBILITY_OUTPUT_REGISTER);
#else
RWTexture2D<float> u_Visibility :
    register(UVSR_SKY_VISIBILITY_OUTPUT_REGISTER);
#endif
RWTexture2D<float> u_ClosestVisibility :
    register(UVSR_SKY_VISIBILITY_CLOSEST_OUTPUT_REGISTER);
#if OUTPUT_HIT_DISTANCE
RWTexture2D<float> u_HitDistance :
    register(UVSR_SKY_VISIBILITY_HIT_DISTANCE_OUTPUT_REGISTER);
#endif

static const float SkyVisibilityTwoPi = 6.28318530717958647692f;
static const float SkyVisibilityHitDistanceMaximum = 65472.0f;
static const float SkyVisibilityHitDistanceMiss = 65504.0f;

float SkyVisibilityLoadDepth(int2 pixelPosition, uint sampleIndex)
{
#if SKY_VISIBILITY_SAMPLES > 1
    return t_Depth.Load(pixelPosition, sampleIndex);
#else
    return t_Depth[pixelPosition];
#endif
}

float4 SkyVisibilityLoadMaterial(int2 pixelPosition, uint sampleIndex)
{
#if SKY_VISIBILITY_SAMPLES > 1
    return t_GBufferMaterial.Load(pixelPosition, sampleIndex);
#else
    return t_GBufferMaterial[pixelPosition];
#endif
}

float4 SkyVisibilityLoadNormals(int2 pixelPosition, uint sampleIndex)
{
#if SKY_VISIBILITY_SAMPLES > 1
    return t_GBufferNormals.Load(pixelPosition, sampleIndex);
#else
    return t_GBufferNormals[pixelPosition];
#endif
}

void SkyVisibilityStore(
    int2 pixelPosition,
    uint sampleIndex,
    float visibility)
{
#if SKY_VISIBILITY_SAMPLES > 1
    u_Visibility[uint3(uint2(pixelPosition), sampleIndex)] = visibility;
#else
    u_Visibility[pixelPosition] = visibility;
#endif
}

bool SkyVisibilityInViewport(uint2 dispatchPosition)
{
    return all(dispatchPosition <
        uint2(g_SkyVisibility.view.viewportSize));
}

int2 SkyVisibilityPixelPosition(uint2 dispatchPosition)
{
    return int2(dispatchPosition) +
        int2(g_SkyVisibility.view.viewportOrigin);
}

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
    uint receiverSampleIndex,
    uint sampleIndex,
    uint phase)
{
    const uint firstDimension =
        receiverSampleIndex * 128u + sampleIndex * 2u;
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

float3 SkyVisibilityPrepareRayOrigin(
    float3 surfacePosition,
    float3 geometricNormal,
    float3 viewDirection,
    float2 pixelCenter,
    float depth)
{
    const float3 safeNormal = RayOriginOrientGeometricNormal(
        geometricNormal,
        viewDirection);
    const float safeDepth = RayOriginStepDepthTowardCamera(
        depth,
        g_SkyVisibility.floatDepth != 0u,
        g_SkyVisibility.reverseDepth != 0u,
        g_SkyVisibility.depthQuantizationStep);
    float3 depthStepPosition = ReconstructWorldPosition(
        g_SkyVisibility.view,
        pixelCenter,
        safeDepth);
    const float depthStepDistance = all(isfinite(depthStepPosition))
        ? length(depthStepPosition - surfacePosition)
        : 0.0f;
    const float clearance = ResolveRayOriginClearance(
        g_SkyVisibility.rayBias,
        depthStepDistance);
    return ResolveRayOriginPosition(
        surfacePosition,
        safeNormal,
        clearance);
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

#if OUTPUT_HIT_DISTANCE
    RayQuery<
        RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
#else
    RayQuery<
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
#endif
    query.TraceRayInline(t_WorldBvh, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed())
    {
        UVSR_COMMIT_COVERED_RAY_QUERY_CANDIDATE(query)
    }
    const bool hit = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
#if OUTPUT_HIT_DISTANCE
    const float committedRayT = hit ? query.CommittedRayT() : 0.0f;
#else
    const float committedRayT = 0.0f;
#endif
    return ResolveRayVisibilityTraceSample(
        hit,
        committedRayT,
        OUTPUT_HIT_DISTANCE != 0,
        SkyVisibilityHitDistanceMaximum,
        SkyVisibilityHitDistanceMiss);
}

float2 SkyVisibilityEvaluate(
    int2 pixelPosition,
    uint2 dispatchPosition,
    uint receiverSampleIndex,
    uint sampleSequencePhase)
{
    const float4 normalChannels = SkyVisibilityLoadNormals(
        pixelPosition,
        receiverSampleIndex);
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
        return float2(1.0f, 0.0f);

    const float4 packedMaterial = SkyVisibilityLoadMaterial(
        pixelPosition,
        receiverSampleIndex);
    const PbrGBufferSurfaceNormals surfaceNormals =
        DecodePbrGBufferSurfaceNormals(
            normalChannels,
            packedMaterial);
    const float depth = SkyVisibilityLoadDepth(
        pixelPosition,
        receiverSampleIndex);
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    const float3 surfacePosition = ReconstructWorldPosition(
        g_SkyVisibility.view,
        pixelCenter,
        depth);
    if (!all(isfinite(surfacePosition)))
        return float2(1.0f, 0.0f);
    const float3 viewIncident = GetIncidentVector(
        g_SkyVisibility.view.cameraDirectionOrPosition,
        surfacePosition);
    const float3 viewDirection = -viewIncident;
    const float3 geometricNormal = RayOriginOrientGeometricNormal(
        surfaceNormals.geometricNormal,
        viewDirection);
    const float3 rayOrigin = SkyVisibilityPrepareRayOrigin(
        surfacePosition,
        surfaceNormals.geometricNormal,
        viewDirection,
        pixelCenter,
        depth);

    const uint sampleCount = max(g_SkyVisibility.sampleCount, 1u);
    RayVisibilityTraceAggregate aggregate =
        BeginRayVisibilityTraceAggregate(
            SkyVisibilityHitDistanceMiss);
    [loop]
    for (uint sampleIndex = 0u;
        sampleIndex < sampleCount;
        ++sampleIndex)
    {
        const float3 direction = SkyVisibilitySampleCosineHemisphere(
            geometricNormal,
            SkyVisibilitySample2D(
                dispatchPosition,
                receiverSampleIndex,
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
        return float2(0.0f, SkyVisibilityHitDistanceMaximum);
    }
    return float2(
        ResolveRayVisibilityTraceAverage(aggregate),
        aggregate.closestHitDistance);
}

[numthreads(8, 8, 1)]
void Generate(uint2 dispatchPosition : SV_DispatchThreadID)
{
    if (!SkyVisibilityInViewport(dispatchPosition))
        return;
    const int2 pixelPosition =
        SkyVisibilityPixelPosition(dispatchPosition);
    const bool sampleScheduleEnabled = UvsrSampleScheduleEnabled(
        g_SkyVisibility.sampleSequenceMode);
    const uint attemptToken = sampleScheduleEnabled
        ? t_AttemptMask[pixelPosition]
        : 0u;
    if (sampleScheduleEnabled && attemptToken == 0u)
        return;
    const uint sampleSequencePhase = UvsrResolveSampleSequencePhase(
        g_SkyVisibility.sampleSequenceMode,
        attemptToken,
        g_SkyVisibility.sampleSequencePhase);

    bool foundClosest = false;
    float closestDepth = 0.0f;
    float2 closestResult = float2(1.0f, 0.0f);
    [unroll]
    for (uint receiverSampleIndex = 0u;
        receiverSampleIndex < SKY_VISIBILITY_SAMPLES;
        ++receiverSampleIndex)
    {
        const float depth = SkyVisibilityLoadDepth(
            pixelPosition,
            receiverSampleIndex);
        const float4 normals = SkyVisibilityLoadNormals(
            pixelPosition,
            receiverSampleIndex);
        const bool covered = isfinite(depth) && depth > 0.0f &&
            dot(normals.xyz, normals.xyz) > 1e-12f;
        const float2 result = covered
            ? SkyVisibilityEvaluate(
                pixelPosition,
                dispatchPosition,
                receiverSampleIndex,
                sampleSequencePhase)
            : float2(1.0f, 0.0f);
        SkyVisibilityStore(
            pixelPosition,
            receiverSampleIndex,
            result.x);
        const bool depthIsCloser = covered &&
            (!foundClosest ||
                (g_SkyVisibility.reverseDepth != 0u
                    ? depth > closestDepth
                    : depth < closestDepth));
        if (depthIsCloser)
        {
            foundClosest = true;
            closestDepth = depth;
            closestResult = result;
        }
    }
    u_ClosestVisibility[pixelPosition] = closestResult.x;
#if OUTPUT_HIT_DISTANCE
    u_HitDistance[pixelPosition] = closestResult.y;
#endif
}
