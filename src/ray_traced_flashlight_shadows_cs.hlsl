#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/gbuffer.hlsli>

#include "pbr_gbuffer.hlsli"
#include "noise_sampling.hlsli"
#include "ray_traced_material_visibility.hlsli"
#include "ray_traced_flashlight_shadows_cb.h"
#include "ray_traced_flashlight_shadows_shared.h"

cbuffer c_FlashlightShadows : register(b0)
{
    RayTracedFlashlightShadowConstants g_FlashlightShadows;
};

RaytracingAccelerationStructure t_WorldBvh : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_GBufferMaterial : register(t2);
Texture2D<float4> t_GBufferNormals : register(t3);
Texture2DArray<float> t_Noise : register(t4);

RWTexture2D<float> u_Visibility : register(u0);
RWTexture2D<float> u_HitDistance : register(u1);

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

float FlashlightShadowStepDepthTowardCamera(float depth)
{
    if (g_FlashlightShadows.floatDepth != 0u)
    {
        uint bits = asuint(saturate(depth));
        if (g_FlashlightShadows.reverseDepth != 0u)
            bits = min(bits + 1u, asuint(1.0f));
        else
            bits = bits > 0u ? bits - 1u : 0u;
        const float stepped = asfloat(bits);
        return isfinite(stepped) ? saturate(stepped) : depth;
    }

    const float direction = g_FlashlightShadows.reverseDepth != 0u
        ? 1.0f
        : -1.0f;
    return saturate(
        depth + direction *
            g_FlashlightShadows.depthQuantizationStep);
}

float FlashlightShadowOffsetFloatComponent(
    float position,
    float direction)
{
    static const float Origin = 1.0f / 32.0f;
    static const float FloatScale = 1.0f / 65536.0f;
    static const float IntegerScale = 256.0f;
    const int integerOffset = int(IntegerScale * direction);
    const float shifted = asfloat(
        asint(position) + (position < 0.0f
            ? -integerOffset
            : integerOffset));
    return abs(position) < Origin
        ? position + FloatScale * direction
        : shifted;
}

float3 FlashlightShadowOffsetFloatPosition(
    float3 position,
    float3 direction)
{
    const float3 safeDirection = PbrSafeNormalize(
        direction,
        float3(0.0f, 0.0f, 1.0f));
    return float3(
        FlashlightShadowOffsetFloatComponent(
            position.x, safeDirection.x),
        FlashlightShadowOffsetFloatComponent(
            position.y, safeDirection.y),
        FlashlightShadowOffsetFloatComponent(
            position.z, safeDirection.z));
}

float3 FlashlightShadowPrepareRayOrigin(
    float3 surfacePosition,
    float3 geometricNormal,
    float3 viewDirection,
    float2 pixelCenter,
    float depth)
{
    float3 safeNormal = PbrSafeNormalize(
        geometricNormal,
        viewDirection);
    if (dot(safeNormal, viewDirection) < 0.0f)
        safeNormal = -safeNormal;

    const float safeDepth = FlashlightShadowStepDepthTowardCamera(depth);
    const float3 depthStepPosition = ReconstructWorldPosition(
        g_FlashlightShadows.view,
        pixelCenter,
        safeDepth);
    const float depthStepDistance = all(isfinite(depthStepPosition))
        ? length(depthStepPosition - surfacePosition)
        : 0.0f;
    const float clearance = max(
        max(g_FlashlightShadows.rayBias, 0.0f),
        depthStepDistance);
    return FlashlightShadowOffsetFloatPosition(
        surfacePosition + safeNormal * clearance,
        safeNormal);
}

RayTracedFlashlightShadowEncoding FlashlightShadowEvaluate(
    int2 pixelPosition,
    uint2 dispatchPosition)
{
    const float4 normalChannels = t_GBufferNormals[pixelPosition];
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
    {
        return ResolveRayTracedFlashlightShadowEncoding(
            0u, 0u, 0.0f);
    }

    const float4 packedMaterial = t_GBufferMaterial[pixelPosition];
    const PbrGBufferSurfaceNormals surfaceNormals =
        DecodePbrGBufferSurfaceNormals(
            normalChannels,
            packedMaterial);
    const float depth = t_Depth[pixelPosition];
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
                g_FlashlightShadows.sampleSequencePhase,
                0x400u),
            UVSRSamplePrecomputedNoise(
                t_Noise,
                g_FlashlightShadows.noisePattern,
                dispatchPosition,
                dispatchExtent,
                g_FlashlightShadows.sampleSequencePhase,
                0x401u));
    }

    uint eligibleSampleCount = 0u;
    uint occludedSampleCount = 0u;
    float closestHitDistance = RayTracedFlashlightMissHitDistance;
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

        ++eligibleSampleCount;
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

        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            const float committedRayT = query.CommittedRayT();
            if (isfinite(committedRayT) && committedRayT >= 0.0f)
            {
                ++occludedSampleCount;
                closestHitDistance = min(
                    closestHitDistance,
                    committedRayT);
            }
        }
    }
    return ResolveRayTracedFlashlightShadowAggregate(
        eligibleSampleCount,
        occludedSampleCount,
        closestHitDistance);
}

[numthreads(8, 8, 1)]
void GenerateVisibility(uint2 dispatchPosition : SV_DispatchThreadID)
{
    if (!FlashlightShadowInViewport(dispatchPosition))
        return;
    const int2 pixelPosition =
        FlashlightShadowPixelPosition(dispatchPosition);
    const RayTracedFlashlightShadowEncoding result =
        FlashlightShadowEvaluate(pixelPosition, dispatchPosition);
    u_Visibility[pixelPosition] = result.visibility;
}

[numthreads(8, 8, 1)]
void GenerateVisibilityAndHitDistance(
    uint2 dispatchPosition : SV_DispatchThreadID)
{
    if (!FlashlightShadowInViewport(dispatchPosition))
        return;
    const int2 pixelPosition =
        FlashlightShadowPixelPosition(dispatchPosition);
    const RayTracedFlashlightShadowEncoding result =
        FlashlightShadowEvaluate(pixelPosition, dispatchPosition);
    u_Visibility[pixelPosition] = result.visibility;
    u_HitDistance[pixelPosition] = result.hitDistance;
}
