#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/gbuffer.hlsli>
#include "noise_sampling.hlsli"
#include "pbr_gbuffer.hlsli"
#include "ray_traced_material_visibility.hlsli"
#include "ray_traced_sky_visibility_cb.h"
#include "sample_accumulation.hlsli"

#ifndef OUTPUT_HIT_DISTANCE
#define OUTPUT_HIT_DISTANCE 0
#endif

cbuffer c_RayTracedSkyVisibility : register(b0)
{
    RayTracedSkyVisibilityConstants g_SkyVisibility;
};

RaytracingAccelerationStructure t_WorldBvh : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_GBufferMaterial : register(t2);
Texture2D<float4> t_GBufferNormals : register(t3);
Texture2DArray<float> t_Noise : register(t4);
Texture2D<uint> t_AttemptMask : register(t5);

RWTexture2D<float> u_Visibility : register(u0);
#if OUTPUT_HIT_DISTANCE
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_HitDistance : register(u1);
#endif

static const float SkyVisibilityTwoPi = 6.28318530717958647692f;
static const float SkyVisibilityHitDistanceMaximum = 65472.0f;
static const float SkyVisibilityHitDistanceMiss = 65504.0f;

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
    uint sampleIndex,
    uint phase)
{
    const uint firstDimension = sampleIndex * 2u;
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

float3 SkyVisibilitySafeGeometricNormal(
    float3 geometricNormal,
    float3 viewDirection)
{
    float3 safeNormal = PbrSafeNormalize(
        geometricNormal,
        viewDirection);
    if (dot(safeNormal, viewDirection) < 0.0f)
        safeNormal = -safeNormal;
    return safeNormal;
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

float SkyVisibilityStepDepthTowardCamera(float depth)
{
    if (g_SkyVisibility.floatDepth != 0u)
    {
        uint bits = asuint(saturate(depth));
        if (g_SkyVisibility.reverseDepth != 0u)
            bits = min(bits + 1u, asuint(1.0f));
        else
            bits = bits > 0u ? bits - 1u : 0u;
        float stepped = asfloat(bits);
        return isfinite(stepped) ? saturate(stepped) : depth;
    }

    float direction = g_SkyVisibility.reverseDepth != 0u
        ? 1.0f
        : -1.0f;
    return saturate(
        depth + direction * g_SkyVisibility.depthQuantizationStep);
}

float SkyVisibilityOffsetFloatComponent(
    float position,
    float direction)
{
    static const float Origin = 1.0f / 32.0f;
    static const float FloatScale = 1.0f / 65536.0f;
    static const float IntegerScale = 256.0f;
    int integerOffset = int(IntegerScale * direction);
    float shifted = asfloat(
        asint(position) + (position < 0.0f
            ? -integerOffset
            : integerOffset));
    return abs(position) < Origin
        ? position + FloatScale * direction
        : shifted;
}

float3 SkyVisibilityOffsetFloatPosition(
    float3 position,
    float3 direction)
{
    float3 safeDirection = PbrSafeNormalize(
        direction,
        float3(0.0f, 0.0f, 1.0f));
    return float3(
        SkyVisibilityOffsetFloatComponent(
            position.x, safeDirection.x),
        SkyVisibilityOffsetFloatComponent(
            position.y, safeDirection.y),
        SkyVisibilityOffsetFloatComponent(
            position.z, safeDirection.z));
}

float3 SkyVisibilityPrepareRayOrigin(
    float3 surfacePosition,
    float3 geometricNormal,
    float3 viewDirection,
    float2 pixelCenter,
    float depth)
{
    const float3 safeNormal = SkyVisibilitySafeGeometricNormal(
        geometricNormal,
        viewDirection);
    const float safeDepth = SkyVisibilityStepDepthTowardCamera(depth);
    float3 depthStepPosition = ReconstructWorldPosition(
        g_SkyVisibility.view,
        pixelCenter,
        safeDepth);
    const float depthStepDistance = all(isfinite(depthStepPosition))
        ? length(depthStepPosition - surfacePosition)
        : 0.0f;
    const float clearance = max(
        max(g_SkyVisibility.rayBias, 0.0f),
        depthStepDistance);
    return SkyVisibilityOffsetFloatPosition(
        surfacePosition + safeNormal * clearance,
        safeNormal);
}

bool SkyVisibilityTrace(
    float3 rayOrigin,
    float3 direction,
    out float hitDistance)
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
    if (hit)
        hitDistance = min(
            query.CommittedRayT(),
            SkyVisibilityHitDistanceMaximum);
    else
        hitDistance = SkyVisibilityHitDistanceMiss;
#else
    hitDistance = hit ? 0.0f : SkyVisibilityHitDistanceMiss;
#endif
    return !hit;
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
    const uint attemptToken =
        sampleScheduleEnabled
            ? t_AttemptMask[pixelPosition]
            : 0u;
    if (sampleScheduleEnabled && attemptToken == 0u)
    {
        return;
    }
    const uint sampleSequencePhase = UvsrResolveSampleSequencePhase(
        g_SkyVisibility.sampleSequenceMode,
        attemptToken,
        g_SkyVisibility.sampleSequencePhase);
    const float4 normalChannels = t_GBufferNormals[pixelPosition];
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
    {
        u_Visibility[pixelPosition] = 1.0f;
#if OUTPUT_HIT_DISTANCE
        u_HitDistance[pixelPosition] = 0.0f;
#endif
        return;
    }

    const float4 packedMaterial = t_GBufferMaterial[pixelPosition];
    const PbrGBufferSurfaceNormals surfaceNormals =
        DecodePbrGBufferSurfaceNormals(
            normalChannels,
            packedMaterial);
    const float depth = t_Depth[pixelPosition];
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    const float3 surfacePosition = ReconstructWorldPosition(
        g_SkyVisibility.view,
        pixelCenter,
        depth);
    if (!all(isfinite(surfacePosition)))
    {
        u_Visibility[pixelPosition] = 1.0f;
#if OUTPUT_HIT_DISTANCE
        u_HitDistance[pixelPosition] = 0.0f;
#endif
        return;
    }
    const float3 viewIncident = GetIncidentVector(
        g_SkyVisibility.view.cameraDirectionOrPosition,
        surfacePosition);
    const float3 viewDirection = -viewIncident;
    const float3 geometricNormal = SkyVisibilitySafeGeometricNormal(
        surfaceNormals.geometricNormal,
        viewDirection);
    const float3 rayOrigin = SkyVisibilityPrepareRayOrigin(
        surfacePosition,
        surfaceNormals.geometricNormal,
        viewDirection,
        pixelCenter,
        depth);

    const uint sampleCount = max(g_SkyVisibility.sampleCount, 1u);
    uint visibleSampleCount = 0u;
    float nearestHitDistance = SkyVisibilityHitDistanceMiss;
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
        float hitDistance;
        if (SkyVisibilityTrace(rayOrigin, direction, hitDistance))
            ++visibleSampleCount;
        else
            nearestHitDistance = min(nearestHitDistance, hitDistance);
    }

    u_Visibility[pixelPosition] =
        float(visibleSampleCount) / float(sampleCount);
#if OUTPUT_HIT_DISTANCE
    u_HitDistance[pixelPosition] = nearestHitDistance;
#endif
}
