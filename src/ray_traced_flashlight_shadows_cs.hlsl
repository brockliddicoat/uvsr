#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"

#include "pbr_gbuffer.hlsli"
#include "noise_sampling.hlsli"
#include "ray_origin_contract.h"
#include "ray_traced_material_visibility.hlsli"
#include "ray_visibility_trace_contract.h"
#include "ray_traced_flashlight_shadows_cb.h"
#include "ray_traced_flashlight_shadows_shared.h"
#include "sample_accumulation.hlsli"

#ifndef FLASHLIGHT_VISIBILITY_SAMPLES
#error FLASHLIGHT_VISIBILITY_SAMPLES must be 1, 2, 4, 8, or 16.
#endif

cbuffer c_FlashlightShadows : register(b0)
{
    RayTracedFlashlightShadowConstants g_FlashlightShadows;
};

RaytracingAccelerationStructure t_WorldBvh : register(t0);
#if FLASHLIGHT_VISIBILITY_SAMPLES > 1
Texture2DMS<float, FLASHLIGHT_VISIBILITY_SAMPLES> t_Depth : register(t1);
Texture2DMS<float4, FLASHLIGHT_VISIBILITY_SAMPLES>
    t_GBufferMaterial : register(t2);
Texture2DMS<float4, FLASHLIGHT_VISIBILITY_SAMPLES>
    t_GBufferNormals : register(t3);
#else
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_GBufferMaterial : register(t2);
Texture2D<float4> t_GBufferNormals : register(t3);
#endif
Texture2DArray<float> t_Noise : register(t4);
Texture2D<uint> t_AttemptMask : register(t5);

#if FLASHLIGHT_VISIBILITY_SAMPLES > 1
RWTexture2DArray<float> u_Visibility : register(u0);
#else
RWTexture2D<float> u_Visibility : register(u0);
#endif
RWTexture2D<float> u_ClosestVisibility : register(u1);
RWTexture2D<float> u_HitDistance : register(u2);

float FlashlightShadowLoadDepth(int2 pixelPosition, uint sampleIndex)
{
#if FLASHLIGHT_VISIBILITY_SAMPLES > 1
    return t_Depth.Load(pixelPosition, sampleIndex);
#else
    return t_Depth[pixelPosition];
#endif
}

float4 FlashlightShadowLoadMaterial(int2 pixelPosition, uint sampleIndex)
{
#if FLASHLIGHT_VISIBILITY_SAMPLES > 1
    return t_GBufferMaterial.Load(pixelPosition, sampleIndex);
#else
    return t_GBufferMaterial[pixelPosition];
#endif
}

float4 FlashlightShadowLoadNormals(int2 pixelPosition, uint sampleIndex)
{
#if FLASHLIGHT_VISIBILITY_SAMPLES > 1
    return t_GBufferNormals.Load(pixelPosition, sampleIndex);
#else
    return t_GBufferNormals[pixelPosition];
#endif
}

void FlashlightShadowStoreVisibility(
    int2 pixelPosition,
    uint sampleIndex,
    float visibility)
{
#if FLASHLIGHT_VISIBILITY_SAMPLES > 1
    u_Visibility[uint3(uint2(pixelPosition), sampleIndex)] = visibility;
#else
    u_Visibility[pixelPosition] = visibility;
#endif
}

bool FlashlightShadowInViewport(uint2 dispatchPosition)
{
    return all(dispatchPosition <
        uint2(g_FlashlightShadows.view.viewportSize));
}

int2 FlashlightShadowPixelPosition(uint2 dispatchPosition)
{
    return int2(dispatchPosition) +
        int2(g_FlashlightShadows.view.viewportOrigin);
}

float3 FlashlightShadowPrepareRayOrigin(
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
        g_FlashlightShadows.floatDepth != 0u,
        g_FlashlightShadows.reverseDepth != 0u,
        g_FlashlightShadows.depthQuantizationStep);
    const float3 depthStepPosition = ReconstructWorldPosition(
        g_FlashlightShadows.view,
        pixelCenter,
        safeDepth);
    const float depthStepDistance = all(isfinite(depthStepPosition))
        ? length(depthStepPosition - surfacePosition)
        : 0.0f;
    const float clearance = ResolveRayOriginClearance(
        g_FlashlightShadows.rayBias,
        depthStepDistance);
    return ResolveRayOriginPosition(
        surfacePosition,
        safeNormal,
        clearance);
}

RayTracedFlashlightShadowEncoding FlashlightShadowEvaluate(
    int2 pixelPosition,
    uint2 dispatchPosition,
    uint receiverSampleIndex,
    uint sampleSequencePhase)
{
    const float4 normalChannels = FlashlightShadowLoadNormals(
        pixelPosition,
        receiverSampleIndex);
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
    {
        return ResolveRayTracedFlashlightShadowEncoding(
            0u, 0u, 0.0f);
    }

    const float4 packedMaterial = FlashlightShadowLoadMaterial(
        pixelPosition,
        receiverSampleIndex);
    const PbrGBufferSurfaceNormals surfaceNormals =
        DecodePbrGBufferSurfaceNormals(
            normalChannels,
            packedMaterial);
    const float depth = FlashlightShadowLoadDepth(
        pixelPosition,
        receiverSampleIndex);
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    const float3 surfacePosition = ReconstructWorldPosition(
        g_FlashlightShadows.view,
        pixelCenter,
        depth);
    const float3 viewIncident = GetIncidentVector(
        g_FlashlightShadows.view.cameraDirectionOrPosition,
        surfacePosition);
    const float3 viewDirection = -viewIncident;
    const float3 rayOrigin = FlashlightShadowPrepareRayOrigin(
        surfacePosition,
        surfaceNormals.geometricNormal,
        viewDirection,
        pixelCenter,
        depth);

    RayTracedFlashlightShadowSurface surface;
    surface.rayOrigin = rayOrigin;
    surface.receiverPosition = surfacePosition;
    surface.geometricNormal = surfaceNormals.geometricNormal;
    surface.shadingNormal = surfaceNormals.shadingNormal;
    surface.viewDirection = viewDirection;

    RayTracedFlashlightShadowLight light;
    light.position =
        g_FlashlightShadows.lightPositionAndRange.xyz;
    light.rangeMeters =
        g_FlashlightShadows.lightPositionAndRange.w;
    light.direction =
        g_FlashlightShadows.lightDirectionAndEmitterRadius.xyz;
    light.emitterRadiusMeters =
        g_FlashlightShadows.lightDirectionAndEmitterRadius.w;
    light.beamProfile = g_FlashlightShadows.beamProfile;

    const uint sampleCount = light.emitterRadiusMeters > 0.0f
        ? clamp(
            g_FlashlightShadows.sampleCount,
            1u,
            RayTracedFlashlightFiniteEmitterSampleCount)
        : 1u;
    float2 noiseShift = 0.5f;
    if (sampleCount > 1u)
    {
        const uint2 dispatchExtent = uint2(
            g_FlashlightShadows.view.viewportSize);
        noiseShift = float2(
            UVSRSamplePrecomputedNoise(
                t_Noise,
                g_FlashlightShadows.noisePattern,
                dispatchPosition,
                dispatchExtent,
                sampleSequencePhase,
                0x400u + receiverSampleIndex * 8u),
            UVSRSamplePrecomputedNoise(
                t_Noise,
                g_FlashlightShadows.noisePattern,
                dispatchPosition,
                dispatchExtent,
                sampleSequencePhase,
                0x401u + receiverSampleIndex * 8u));
    }

    RayVisibilityTraceAggregate traceAggregate =
        BeginRayVisibilityTraceAggregate(
            RayTracedFlashlightMissHitDistance);
    [loop]
    for (uint sampleIndex = 0u;
        sampleIndex < sampleCount;
        ++sampleIndex)
    {
        const float emitterSampleU = frac(
            noiseShift.x +
            (float(sampleIndex) + 0.5f) / float(sampleCount));
        const float emitterSampleV = frac(
            noiseShift.y +
            float(sampleIndex) *
                RayTracedFlashlightGoldenRatioConjugate);
        const RayTracedFlashlightShadowRay flashlightRay =
            ResolveRayTracedFlashlightShadowRay(
                surface,
                light,
                emitterSampleU,
                emitterSampleV);
        if (flashlightRay.eligible == 0u)
            continue;

        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.Direction = flashlightRay.directionToLight;
        // TMax is the first intersection with the sampled finite emitter.
        ray.TMin = 0.0f;
        ray.TMax = flashlightRay.tMax;

        RayQuery<
            RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(t_WorldBvh, RAY_FLAG_NONE, 0xff, ray);
        while (query.Proceed())
        {
            UVSR_COMMIT_COVERED_RAY_QUERY_CANDIDATE(query)
        }

        const bool hit =
            query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
        const float committedRayT = hit
            ? query.CommittedRayT()
            : 0.0f;
        const bool validHit = hit &&
            isfinite(committedRayT) &&
            committedRayT >= 0.0f;
        const RayVisibilityTraceSample traceSample =
            ResolveRayVisibilityTraceSample(
                validHit,
                committedRayT,
                true,
                RayTracedFlashlightMissHitDistance,
                RayTracedFlashlightMissHitDistance);
        traceAggregate = AccumulateRayVisibilityTraceSample(
            traceAggregate,
            traceSample);
    }
    if (!RayVisibilityTraceAggregateIsComplete(
        traceAggregate,
        traceAggregate.sampleCount))
    {
        return ResolveRayTracedFlashlightShadowEncoding(
            1u,
            1u,
            0.0f);
    }
    return ResolveRayTracedFlashlightShadowAggregate(
        traceAggregate.sampleCount,
        traceAggregate.sampleCount -
            traceAggregate.visibleSampleCount,
        traceAggregate.closestHitDistance);
}

void FlashlightShadowGenerate(
    uint2 dispatchPosition,
    bool outputHitDistance)
{
    if (!FlashlightShadowInViewport(dispatchPosition))
        return;
    const int2 pixelPosition =
        FlashlightShadowPixelPosition(dispatchPosition);
    const bool sampleScheduleEnabled = UvsrSampleScheduleEnabled(
        g_FlashlightShadows.sampleSequenceMode);
    const uint attemptToken =
        sampleScheduleEnabled
            ? t_AttemptMask[pixelPosition]
            : 0u;
    if (sampleScheduleEnabled && attemptToken == 0u)
    {
        return;
    }
    const uint sampleSequencePhase = UvsrResolveSampleSequencePhase(
        g_FlashlightShadows.sampleSequenceMode,
        attemptToken,
        g_FlashlightShadows.sampleSequencePhase);
    bool foundClosest = false;
    float closestDepth = 0.0f;
    RayTracedFlashlightShadowEncoding closestResult =
        ResolveRayTracedFlashlightShadowEncoding(0u, 0u, 0.0f);
    [unroll]
    for (uint receiverSampleIndex = 0u;
        receiverSampleIndex < FLASHLIGHT_VISIBILITY_SAMPLES;
        ++receiverSampleIndex)
    {
        const float depth = FlashlightShadowLoadDepth(
            pixelPosition,
            receiverSampleIndex);
        const float4 normals = FlashlightShadowLoadNormals(
            pixelPosition,
            receiverSampleIndex);
        const bool covered = isfinite(depth) && depth > 0.0f &&
            dot(normals.xyz, normals.xyz) > 1e-12f;
        RayTracedFlashlightShadowEncoding result =
            ResolveRayTracedFlashlightShadowEncoding(
                0u, 0u, 0.0f);
        if (covered)
        {
            result = FlashlightShadowEvaluate(
                pixelPosition,
                dispatchPosition,
                receiverSampleIndex,
                sampleSequencePhase);
        }
        FlashlightShadowStoreVisibility(
            pixelPosition,
            receiverSampleIndex,
            result.visibility);

        const bool depthIsCloser = covered &&
            (!foundClosest ||
                (g_FlashlightShadows.reverseDepth != 0u
                    ? depth > closestDepth
                    : depth < closestDepth));
        if (depthIsCloser)
        {
            foundClosest = true;
            closestDepth = depth;
            closestResult = result;
        }
    }
    u_ClosestVisibility[pixelPosition] = closestResult.visibility;
    if (outputHitDistance)
        u_HitDistance[pixelPosition] = closestResult.hitDistance;
}

[numthreads(8, 8, 1)]
void GenerateVisibility(uint2 dispatchPosition : SV_DispatchThreadID)
{
    FlashlightShadowGenerate(dispatchPosition, false);
}

[numthreads(8, 8, 1)]
void GenerateVisibilityAndHitDistance(
    uint2 dispatchPosition : SV_DispatchThreadID)
{
    FlashlightShadowGenerate(dispatchPosition, true);
}
