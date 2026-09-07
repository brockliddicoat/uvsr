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


cbuffer c_FlashlightShadows : register(b0)
{
    RayTracedFlashlightShadowConstants g_FlashlightShadows;
};

#define RAY_VISIBILITY_CONSTANTS g_FlashlightShadows
#define RAY_VISIBILITY_STOCHASTIC 1
#include "ray_visibility_receiver.hlsli"

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
        g_FlashlightShadows.view,
        pixelCenter,
        depth);
    const float3 viewIncident = GetIncidentVector(
        g_FlashlightShadows.view.cameraDirectionOrPosition,
        surfacePosition);
    const float3 viewDirection = -viewIncident;
    const float3 rayOrigin = RayVisibilityPrepareRayOrigin(
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
            RayTracedFlashlightMaximumSampleCount)
        : 1u;
    float2 noiseShift = 0.5f;
    if (light.emitterRadiusMeters > 0.0f)
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
                0x400u),
            UVSRSamplePrecomputedNoise(
                t_Noise,
                g_FlashlightShadows.noisePattern,
                dispatchPosition,
                dispatchExtent,
                sampleSequencePhase,
                0x401u));
    }

    RayVisibilityTraceAggregate traceAggregate =
        BeginRayVisibilityTraceAggregate();
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

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
            RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(t_WorldBvh, RAY_FLAG_NONE, 0xff, ray);
        while (query.Proceed())
        {
            UVSR_COMMIT_COVERED_RAY_QUERY_CANDIDATE(query)
        }

        const bool hit =
            query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
        const RayVisibilityTraceSample traceSample =
            ResolveRayVisibilityTraceSample(hit);
        traceAggregate = AccumulateRayVisibilityTraceSample(
            traceAggregate,
            traceSample);
    }
    if (!RayVisibilityTraceAggregateIsComplete(
        traceAggregate,
        traceAggregate.sampleCount))
    {
        return 0.0f;
    }
    return ResolveRayTracedFlashlightShadowAggregate(
        traceAggregate.sampleCount,
        traceAggregate.sampleCount - traceAggregate.visibleSampleCount);
}

[numthreads(8, 8, 1)]
void GenerateVisibility(uint2 dispatchPosition : SV_DispatchThreadID)
{
    RayVisibilityGenerate(dispatchPosition);
}
