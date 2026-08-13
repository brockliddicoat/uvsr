#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/gbuffer.hlsli>
#include "heitz_ratio_estimator_shadows_cb.h"
#include "noise_sampling.hlsli"
#include "pbr_gbuffer.hlsli"
#include "ray_traced_material_visibility.hlsli"
#include "ratio_estimator_shared.h"
#include "sample_accumulation.hlsli"

#ifndef OUTPUT_HIT_DISTANCE
#define OUTPUT_HIT_DISTANCE 0
#endif

cbuffer c_HeitzShadows : register(b0)
{
    HeitzRatioEstimatorShadowConstants g_Heitz;
};

RaytracingAccelerationStructure t_WorldBvh : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_GBufferDiffuse : register(t2);
Texture2D<float4> t_GBufferMaterial : register(t3);
Texture2D<float4> t_GBufferNormals : register(t4);
Texture2D<float> t_MaterialAmbientOcclusion : register(t5);
Texture2DArray<float> t_Noise : register(t6);
Texture2D<float4> t_GBufferEmissive : register(t7);
Texture2D<uint> t_AttemptMask : register(t8);

VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_Output : register(u0);
#if OUTPUT_HIT_DISTANCE
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_HitDistance : register(u1);
#endif

static const float HeitzTwoPi = 6.28318530717958647692f;
static const float HeitzHitDistanceMaximum = 65472.0f;
static const float HeitzHitDistanceMiss = 65504.0f;

bool HeitzInViewport(uint2 dispatchPosition)
{
    return all(dispatchPosition < uint2(g_Heitz.view.viewportSize));
}

int2 HeitzPixelPosition(uint2 dispatchPosition)
{
    return int2(dispatchPosition) + int2(g_Heitz.view.viewportOrigin);
}

float HeitzRadicalInverse(uint index, uint base)
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

float2 HeitzSample2D(
    uint2 dispatchPosition,
    uint sampleIndex,
    uint phase)
{
    const uint firstDimension = sampleIndex * 2u;
    const uint sequenceIndex = sampleIndex + 1u;
    const uint2 dispatchExtent = uint2(g_Heitz.view.viewportSize);
    const float2 noiseShift = float2(
        UVSRSamplePrecomputedNoise(
            t_Noise,
            g_Heitz.noisePattern,
            dispatchPosition,
            dispatchExtent,
            phase,
            0x200u + firstDimension),
        UVSRSamplePrecomputedNoise(
            t_Noise,
            g_Heitz.noisePattern,
            dispatchPosition,
            dispatchExtent,
            phase,
            0x200u + firstDimension + 1u));
    return frac(float2(
        HeitzRadicalInverse(sequenceIndex, 2u),
        HeitzRadicalInverse(sequenceIndex, 3u)) + noiseShift);
}

float3 HeitzLightCenterDirection()
{
    return PbrSafeNormalize(
        g_Heitz.directionToLightAndAngularRadius.xyz,
        float3(0.0f, 1.0f, 0.0f));
}

float3 HeitzSampleDirectionalEmitter(float2 sample)
{
    float3 center = HeitzLightCenterDirection();
    float angularRadius = max(
        g_Heitz.directionToLightAndAngularRadius.w,
        0.0f);
    if (!(angularRadius > 1e-6f))
        return center;

    // sample.x is the exact radial CDF for a uniform spherical cap. This is
    // deliberately not the blog's approximate projected-plane mapping.
    float cosMaximum = cos(angularRadius);
    float cosTheta = lerp(1.0f, cosMaximum, sample.x);
    float sinTheta = sqrt(max(1.0f - cosTheta * cosTheta, 0.0f));
    float phi = HeitzTwoPi * sample.y;
    float3 helper = abs(center.z) < 0.999f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = PbrSafeNormalize(
        cross(helper, center),
        float3(1.0f, 0.0f, 0.0f));
    float3 bitangent = cross(center, tangent);
    return PbrSafeNormalize(
        center * cosTheta +
            tangent * (cos(phi) * sinTheta) +
            bitangent * (sin(phi) * sinTheta),
        center);
}

float HeitzStepDepthTowardCamera(float depth)
{
    if (g_Heitz.floatDepth != 0u)
    {
        uint bits = asuint(saturate(depth));
        if (g_Heitz.reverseDepth != 0u)
            bits = min(bits + 1u, asuint(1.0f));
        else
            bits = bits > 0u ? bits - 1u : 0u;
        float stepped = asfloat(bits);
        return isfinite(stepped) ? saturate(stepped) : depth;
    }

    float direction = g_Heitz.reverseDepth != 0u ? 1.0f : -1.0f;
    return saturate(depth + direction * g_Heitz.depthQuantizationStep);
}

float HeitzOffsetFloatComponent(float position, float direction)
{
    // Ray Tracing Gems' representable-position offset. Apply it after the
    // world-space clearance so float rounding cannot put the origin back onto
    // the represented triangle plane.
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

float3 HeitzOffsetFloatPosition(float3 position, float3 direction)
{
    float3 safeDirection = PbrSafeNormalize(
        direction,
        float3(0.0f, 0.0f, 1.0f));
    return float3(
        HeitzOffsetFloatComponent(position.x, safeDirection.x),
        HeitzOffsetFloatComponent(position.y, safeDirection.y),
        HeitzOffsetFloatComponent(position.z, safeDirection.z));
}

float3 HeitzPrepareRayOrigin(
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

    const float safeDepth = HeitzStepDepthTowardCamera(depth);
    float3 depthStepPosition = ReconstructWorldPosition(
        g_Heitz.view,
        pixelCenter,
        safeDepth);
    const float depthStepDistance = all(isfinite(depthStepPosition))
        ? length(depthStepPosition - surfacePosition)
        : 0.0f;
    const float clearance = max(
        max(g_Heitz.rayBias, 0.0f),
        depthStepDistance);
    return HeitzOffsetFloatPosition(
        surfacePosition + safeNormal * clearance,
        safeNormal);
}

bool HeitzTraceVisibility(
    float3 rayOrigin,
    float3 directionToLight,
    out float hitDistance)
{
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = directionToLight;
    // Ray Bias has already displaced the origin along the raster triangle
    // normal. Applying it again as TMin would compound contact detachment.
    ray.TMin = 0.0f;
    ray.TMax = g_Heitz.rayDistance;

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
            HeitzHitDistanceMaximum);
    else
        hitDistance = HeitzHitDistanceMiss;
#else
    hitDistance = hit ? 0.0f : HeitzHitDistanceMiss;
#endif
    return !hit;
}

float3 HeitzEvaluateNormalizedResponse(
    PbrPreparedMaterial material,
    PbrPreparedSurface surface,
    float3 directionToLight)
{
    if (!CanEvaluatePbrDirectSurfacePrepared(surface, directionToLight))
        return 0.0f;

    PbrBsdfEvaluation bsdf = EvaluateBsdfPrepared(
        material,
        surface,
        directionToLight);
    float cosineTerm = saturate(dot(
        surface.shadingNormal,
        directionToLight));
    // Uniform-cone sampling has one constant PDF. Its reciprocal cancels in
    // S_N / U_N, so the normalized response retains a stable epsilon scale.
    float3 response = max(bsdf.total, 0.0f) * cosineTerm;
    return any(!isfinite(response)) ? 0.0f : response;
}

[numthreads(8, 8, 1)]
void Generate(uint2 dispatchPosition : SV_DispatchThreadID)
{
    if (!HeitzInViewport(dispatchPosition))
        return;
    const int2 pixelPosition = HeitzPixelPosition(dispatchPosition);
    const bool sampleScheduleEnabled = UvsrSampleScheduleEnabled(
        g_Heitz.sampleSequenceMode);
    const uint attemptToken = sampleScheduleEnabled
        ? t_AttemptMask[pixelPosition]
        : 0u;
    if (sampleScheduleEnabled && attemptToken == 0u)
    {
        return;
    }
    const uint sampleSequencePhase = UvsrResolveSampleSequencePhase(
        g_Heitz.sampleSequenceMode,
        attemptToken,
        g_Heitz.sampleSequencePhase);
    const float4 normalChannels = t_GBufferNormals[pixelPosition];
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
    {
        u_Output[pixelPosition] = 1.0f;
#if OUTPUT_HIT_DISTANCE
        u_HitDistance[pixelPosition] = 0.0f;
#endif
        return;
    }

    // The scalar path deliberately stops after the minimum surface data needed
    // for a robust center ray. Diffuse, emissive, AO, material preparation,
    // emitter sampling, and ratio evaluation stay entirely in the soft path.
    const float4 packedMaterial = t_GBufferMaterial[pixelPosition];
    const PbrGBufferSurfaceNormals surfaceNormals =
        DecodePbrGBufferSurfaceNormals(normalChannels, packedMaterial);
    const float depth = t_Depth[pixelPosition];
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    const float3 surfacePosition = ReconstructWorldPosition(
        g_Heitz.view,
        pixelCenter,
        depth);
    if (!all(isfinite(surfacePosition)))
    {
        u_Output[pixelPosition] = 1.0f;
#if OUTPUT_HIT_DISTANCE
        u_HitDistance[pixelPosition] = 0.0f;
#endif
        return;
    }
    const float3 viewIncident = GetIncidentVector(
        g_Heitz.view.cameraDirectionOrPosition,
        surfacePosition);
    const float3 viewDirection = -viewIncident;
    PbrSurfaceInteraction surface;
    surface.position = surfacePosition;
    surface.shadingNormal = surfaceNormals.shadingNormal;
    surface.geometricNormal = surfaceNormals.geometricNormal;
    surface.viewDirection = viewDirection;
    const PbrPreparedSurface preparedSurface = PreparePbrSurface(surface);

    [branch]
    if (g_Heitz.hardShadows != 0u ||
        g_Heitz.useRatioEstimator == 0u)
    {
        const float3 directionToLight = g_Heitz.hardShadows != 0u
            ? HeitzLightCenterDirection()
            : HeitzSampleDirectionalEmitter(
                HeitzSample2D(
                    dispatchPosition,
                    0u,
                    sampleSequencePhase));
        if (!CanEvaluatePbrDirectSurfacePrepared(
                preparedSurface,
                directionToLight))
        {
            // Deferred direct lighting is exactly zero for this light/surface,
            // so a visibility query cannot affect the final result.
            u_Output[pixelPosition] = 1.0f;
#if OUTPUT_HIT_DISTANCE
            u_HitDistance[pixelPosition] = 0.0f;
#endif
            return;
        }
        const float3 rayOrigin = HeitzPrepareRayOrigin(
            surfacePosition,
            preparedSurface.geometricNormal,
            viewDirection,
            pixelCenter,
            depth);
        float hitDistance;
        const float visible = float(HeitzTraceVisibility(
            rayOrigin,
            directionToLight,
            hitDistance));
        u_Output[pixelPosition] = float4(visible.xxx, 1.0f);
#if OUTPUT_HIT_DISTANCE
        u_HitDistance[pixelPosition] = hitDistance;
#endif
        return;
    }

    float4 channels[4];
    channels[0] = t_GBufferDiffuse[pixelPosition];
    channels[1] = packedMaterial;
    channels[2] = normalChannels;
    channels[3] = t_GBufferEmissive[pixelPosition];
    const PbrGBufferData gbuffer = DecodePbrGBuffer(
        channels,
        t_MaterialAmbientOcclusion[pixelPosition]);

    const PbrPreparedMaterial preparedMaterial =
        PreparePbrMaterial(gbuffer.material);
    const float3 rayOrigin = HeitzPrepareRayOrigin(
        surfacePosition,
        preparedSurface.geometricNormal,
        viewDirection,
        pixelCenter,
        depth);

    const uint sampleCount = max(g_Heitz.sampleCount, 1u);
    float3 currentNumerator = 0.0f;
    float3 currentDenominator = 0.0f;
    float nearestHitDistance = HeitzHitDistanceMiss;
    uint tracedRayCount = 0u;
    [loop]
    for (uint sampleIndex = 0u;
        sampleIndex < sampleCount;
        ++sampleIndex)
    {
        const float3 directionToLight = HeitzSampleDirectionalEmitter(
            HeitzSample2D(
                dispatchPosition,
                sampleIndex,
                sampleSequencePhase));
        const float3 contribution = HeitzEvaluateNormalizedResponse(
            preparedMaterial,
            preparedSurface,
            directionToLight);
        if (!any(contribution > 0.0f))
            continue;

        // The exact same direction, proposal normalization, BRDF, cosine, and
        // validity decision feed both estimates. Binary visibility is their
        // only difference.
        currentDenominator += contribution;
        float hitDistance;
        if (HeitzTraceVisibility(
                rayOrigin, directionToLight, hitDistance))
            currentNumerator += contribution;
        nearestHitDistance = min(nearestHitDistance, hitDistance);
        ++tracedRayCount;
    }

    // Normalize both matched current-frame sums before the guarded division.
    // The ratio is homogeneous, but its fail-open epsilon is deliberately not,
    // so retaining means keeps the epsilon independent of sample count.
    const float inverseSampleCount = rcp(float(sampleCount));
    const float3 numeratorMean = currentNumerator * inverseSampleCount;
    const float3 denominatorMean = currentDenominator * inverseSampleCount;
    const float3 modulation = saturate(ResolveCorrelatedRatio(
        numeratorMean,
        denominatorMean,
        g_Heitz.denominatorEpsilon));
    u_Output[pixelPosition] = float4(modulation, 1.0f);
#if OUTPUT_HIT_DISTANCE
    u_HitDistance[pixelPosition] = tracedRayCount != 0u
        ? nearestHitDistance
        : 0.0f;
#endif
}
