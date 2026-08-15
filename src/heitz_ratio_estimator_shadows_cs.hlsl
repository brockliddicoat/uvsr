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

#ifndef HEITZ_RASTER_SAMPLES
#define HEITZ_RASTER_SAMPLES 1
#endif

#ifndef OUTPUT_SOURCE_MODULATION
#define OUTPUT_SOURCE_MODULATION 0
#endif

#if OUTPUT_SOURCE_MODULATION && OUTPUT_HIT_DISTANCE
#error Source modulation and hit distance are mutually exclusive outputs.
#endif

cbuffer c_HeitzShadows : register(b0)
{
    HeitzRatioEstimatorShadowConstants g_Heitz;
};

RaytracingAccelerationStructure t_WorldBvh : register(t0);
#if HEITZ_RASTER_SAMPLES > 1
Texture2DMS<float, HEITZ_RASTER_SAMPLES> t_Depth : register(t1);
Texture2DMS<float4, HEITZ_RASTER_SAMPLES>
    t_GBufferDiffuse : register(t2);
Texture2DMS<float4, HEITZ_RASTER_SAMPLES>
    t_GBufferMaterial : register(t3);
Texture2DMS<float4, HEITZ_RASTER_SAMPLES>
    t_GBufferNormals : register(t4);
Texture2DMS<float, HEITZ_RASTER_SAMPLES>
    t_MaterialAmbientOcclusion : register(t5);
#else
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_GBufferDiffuse : register(t2);
Texture2D<float4> t_GBufferMaterial : register(t3);
Texture2D<float4> t_GBufferNormals : register(t4);
Texture2D<float> t_MaterialAmbientOcclusion : register(t5);
#endif
Texture2DArray<float> t_Noise : register(t6);
#if HEITZ_RASTER_SAMPLES > 1
Texture2DMS<float4, HEITZ_RASTER_SAMPLES>
    t_GBufferEmissive : register(t7);
#else
Texture2D<float4> t_GBufferEmissive : register(t7);
#endif
Texture2D<uint> t_AttemptMask : register(t8);

VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_Output : register(u0);
#if OUTPUT_SOURCE_MODULATION
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_ClosestSourceOutput : register(u1);
#elif OUTPUT_HIT_DISTANCE
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_HitDistance : register(u1);
#endif

static const float HeitzTwoPi = 6.28318530717958647692f;
static const float HeitzHitDistanceMaximum = 65472.0f;
static const float HeitzHitDistanceMiss = 65504.0f;

float HeitzLoadDepth(int2 pixelPosition, uint receiverSampleIndex)
{
#if HEITZ_RASTER_SAMPLES > 1
    return t_Depth.Load(pixelPosition, receiverSampleIndex);
#else
    return t_Depth[pixelPosition];
#endif
}

float4 HeitzLoadDiffuse(int2 pixelPosition, uint receiverSampleIndex)
{
#if HEITZ_RASTER_SAMPLES > 1
    return t_GBufferDiffuse.Load(pixelPosition, receiverSampleIndex);
#else
    return t_GBufferDiffuse[pixelPosition];
#endif
}

float4 HeitzLoadMaterial(int2 pixelPosition, uint receiverSampleIndex)
{
#if HEITZ_RASTER_SAMPLES > 1
    return t_GBufferMaterial.Load(pixelPosition, receiverSampleIndex);
#else
    return t_GBufferMaterial[pixelPosition];
#endif
}

float4 HeitzLoadNormals(int2 pixelPosition, uint receiverSampleIndex)
{
#if HEITZ_RASTER_SAMPLES > 1
    return t_GBufferNormals.Load(pixelPosition, receiverSampleIndex);
#else
    return t_GBufferNormals[pixelPosition];
#endif
}

float HeitzLoadMaterialAmbientOcclusion(
    int2 pixelPosition,
    uint receiverSampleIndex)
{
#if HEITZ_RASTER_SAMPLES > 1
    return t_MaterialAmbientOcclusion.Load(
        pixelPosition, receiverSampleIndex);
#else
    return t_MaterialAmbientOcclusion[pixelPosition];
#endif
}

float4 HeitzLoadEmissive(int2 pixelPosition, uint receiverSampleIndex)
{
#if HEITZ_RASTER_SAMPLES > 1
    return t_GBufferEmissive.Load(pixelPosition, receiverSampleIndex);
#else
    return t_GBufferEmissive[pixelPosition];
#endif
}

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

struct HeitzNormalizedResponse
{
    float3 diffuse;
    float3 total;
};

float3 HeitzSanitizeNonnegativeResponse(float3 response)
{
    return float3(
        isfinite(response.x) ? max(response.x, 0.0f) : 0.0f,
        isfinite(response.y) ? max(response.y, 0.0f) : 0.0f,
        isfinite(response.z) ? max(response.z, 0.0f) : 0.0f);
}

float HeitzResolveDeterministicRatioChannel(
    float numerator,
    float denominator)
{
    if (!isfinite(numerator) || !isfinite(denominator) ||
        !(denominator > 0.0f))
    {
        return 1.0f;
    }
    return saturate(numerator / denominator);
}

float3 HeitzResolveDeterministicRatio(
    float3 numerator,
    float3 denominator)
{
    return float3(
        HeitzResolveDeterministicRatioChannel(
            numerator.x, denominator.x),
        HeitzResolveDeterministicRatioChannel(
            numerator.y, denominator.y),
        HeitzResolveDeterministicRatioChannel(
            numerator.z, denominator.z));
}

HeitzNormalizedResponse HeitzEvaluateNormalizedResponse(
    PbrPreparedMaterial material,
    PbrPreparedSurface surface,
    float3 directionToLight)
{
    HeitzNormalizedResponse response = (HeitzNormalizedResponse)0;
    if (!CanEvaluatePbrDirectSurfacePrepared(surface, directionToLight))
        return response;

    PbrBsdfEvaluation bsdf = EvaluateBsdfPrepared(
        material,
        surface,
        directionToLight);
    float cosineTerm = saturate(dot(
        surface.shadingNormal,
        directionToLight));
    // Uniform-cone sampling has one constant PDF. Its reciprocal cancels in
    // S_N / U_N, so the normalized response retains a stable epsilon scale.
    response.diffuse = HeitzSanitizeNonnegativeResponse(
        bsdf.diffuse * cosineTerm);
    response.total = HeitzSanitizeNonnegativeResponse(
        bsdf.total * cosineTerm);
    return response;
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
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    const bool useRatioEstimator =
        g_Heitz.hardShadows == 0u &&
        g_Heitz.useRatioEstimator != 0u;
    const uint emitterSampleCount = useRatioEstimator
        ? max(g_Heitz.sampleCount, 1u)
        : 1u;
    const bool traceAllMsaaReceivers =
        HEITZ_RASTER_SAMPLES == 1 ||
        g_Heitz.traceAllMsaaReceivers != 0u;

#if HEITZ_RASTER_SAMPLES == 1
    // Preserve the optimized 1x scalar route. MSAA needs material response
    // weighting across receivers, but one receiver's scalar visibility
    // factors exactly without those additional G-buffer loads.
    [branch]
    if (!useRatioEstimator)
    {
        const float4 normalChannels = HeitzLoadNormals(pixelPosition, 0u);
        const float depth = HeitzLoadDepth(pixelPosition, 0u);
        if (!isfinite(depth) || depth <= 0.0f ||
            !(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
        {
            u_Output[pixelPosition] = 1.0f;
#if OUTPUT_SOURCE_MODULATION
            u_ClosestSourceOutput[pixelPosition] = 1.0f;
#endif
#if OUTPUT_HIT_DISTANCE
            u_HitDistance[pixelPosition] = 0.0f;
#endif
            return;
        }
        const float4 packedMaterial = HeitzLoadMaterial(pixelPosition, 0u);
        const PbrGBufferSurfaceNormals surfaceNormals =
            DecodePbrGBufferSurfaceNormals(normalChannels, packedMaterial);
        const float3 surfacePosition = ReconstructWorldPosition(
            g_Heitz.view,
            pixelCenter,
            depth);
        if (!all(isfinite(surfacePosition)))
        {
            u_Output[pixelPosition] = 1.0f;
#if OUTPUT_SOURCE_MODULATION
            u_ClosestSourceOutput[pixelPosition] = 1.0f;
#endif
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
        const PbrPreparedSurface preparedSurface =
            PreparePbrSurface(surface);
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
            u_Output[pixelPosition] = 1.0f;
#if OUTPUT_SOURCE_MODULATION
            u_ClosestSourceOutput[pixelPosition] = 1.0f;
#endif
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
#if OUTPUT_SOURCE_MODULATION
        u_ClosestSourceOutput[pixelPosition] =
            float4(visible.xxx, 1.0f);
#endif
#if OUTPUT_HIT_DISTANCE
        u_HitDistance[pixelPosition] = hitDistance;
#endif
        return;
    }
#endif

    float3 resolvedNumerator = 0.0f;
    float3 resolvedDenominator = 0.0f;
    float3 closestTotalModulation = 1.0f;
    float3 closestSourceModulation = 1.0f;
    float nearestHitDistance = HeitzHitDistanceMiss;
    uint tracedRayCount = 0u;
    uint validReceiverCount = 0u;
    bool foundClosestReceiver = false;
    float closestReceiverDepth = 0.0f;
#if HEITZ_RASTER_SAMPLES > 1
    bool selectedClosestReceiver = false;
    uint selectedClosestReceiverIndex = 0u;
    if (!traceAllMsaaReceivers)
    {
        // Determine the final coherent owner before issuing any ray query.
        // Tracing successive closest-so-far receivers would make the budget
        // data-dependent and could publish a different GI owner.
        [unroll]
        for (uint receiverSampleIndex = 0u;
            receiverSampleIndex < HEITZ_RASTER_SAMPLES;
            ++receiverSampleIndex)
        {
            const float4 normalChannels = HeitzLoadNormals(
                pixelPosition, receiverSampleIndex);
            const float depth = HeitzLoadDepth(
                pixelPosition, receiverSampleIndex);
            if (!isfinite(depth) || depth <= 0.0f ||
                !(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
            {
                continue;
            }
            if (!selectedClosestReceiver ||
                depth > closestReceiverDepth)
            {
                selectedClosestReceiver = true;
                selectedClosestReceiverIndex = receiverSampleIndex;
                closestReceiverDepth = depth;
            }
        }
    }
#endif
    [loop]
    for (uint receiverSampleIndex = 0u;
        receiverSampleIndex < HEITZ_RASTER_SAMPLES;
        ++receiverSampleIndex)
    {
#if HEITZ_RASTER_SAMPLES > 1
        if (!traceAllMsaaReceivers &&
            (!selectedClosestReceiver ||
                receiverSampleIndex != selectedClosestReceiverIndex))
        {
            continue;
        }
#endif
        const float4 normalChannels = HeitzLoadNormals(
            pixelPosition, receiverSampleIndex);
        const float depth = HeitzLoadDepth(
            pixelPosition, receiverSampleIndex);
        if (!isfinite(depth) || depth <= 0.0f ||
            !(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
            continue;

        const bool ownsClosestSource = !foundClosestReceiver ||
            depth > closestReceiverDepth;
        if (ownsClosestSource)
        {
            foundClosestReceiver = true;
            closestReceiverDepth = depth;
            // The coherent closest-surface resolve owns this raw depth and
            // normal even if later reconstruction fails. Never retain an
            // older receiver's shadow factor for that newer owner.
            closestSourceModulation = 1.0f;
        }

        const float4 packedMaterial = HeitzLoadMaterial(
            pixelPosition, receiverSampleIndex);
        const PbrGBufferSurfaceNormals surfaceNormals =
            DecodePbrGBufferSurfaceNormals(
                normalChannels, packedMaterial);
        const float3 surfacePosition = ReconstructWorldPosition(
            g_Heitz.view,
            pixelCenter,
            depth);
        if (!all(isfinite(surfacePosition)))
            continue;

        const float3 viewIncident = GetIncidentVector(
            g_Heitz.view.cameraDirectionOrPosition,
            surfacePosition);
        const float3 viewDirection = -viewIncident;
        PbrSurfaceInteraction surface;
        surface.position = surfacePosition;
        surface.shadingNormal = surfaceNormals.shadingNormal;
        surface.geometricNormal = surfaceNormals.geometricNormal;
        surface.viewDirection = viewDirection;
        const PbrPreparedSurface preparedSurface =
            PreparePbrSurface(surface);

        float4 channels[4];
        channels[0] = HeitzLoadDiffuse(
            pixelPosition, receiverSampleIndex);
        channels[1] = packedMaterial;
        channels[2] = normalChannels;
        channels[3] = HeitzLoadEmissive(
            pixelPosition, receiverSampleIndex);
        const PbrGBufferData gbuffer = DecodePbrGBuffer(
            channels,
            HeitzLoadMaterialAmbientOcclusion(
                pixelPosition, receiverSampleIndex));
        const PbrPreparedMaterial preparedMaterial =
            PreparePbrMaterial(gbuffer.material);
        const float3 rayOrigin = HeitzPrepareRayOrigin(
            surfacePosition,
            preparedSurface.geometricNormal,
            viewDirection,
            pixelCenter,
            depth);
        ++validReceiverCount;

        const HeitzNormalizedResponse centerResponse =
            HeitzEvaluateNormalizedResponse(
                preparedMaterial,
                preparedSurface,
                HeitzLightCenterDirection());
        float3 receiverTotalNumerator = 0.0f;
        float3 receiverTotalDenominator = 0.0f;
        float3 receiverDiffuseNumerator = 0.0f;
        float3 receiverDiffuseDenominator = 0.0f;
        float3 receiverTotalModulation = 1.0f;
        float3 receiverDiffuseModulation = 1.0f;

        if (!useRatioEstimator)
        {
            if (any(centerResponse.total > 0.0f))
            {
                const float3 directionToLight =
                    g_Heitz.hardShadows != 0u
                        ? HeitzLightCenterDirection()
                        : HeitzSampleDirectionalEmitter(
                            HeitzSample2D(
                                dispatchPosition,
                                receiverSampleIndex,
                                sampleSequencePhase));
                bool visible = true;
                if (CanEvaluatePbrDirectSurfacePrepared(
                        preparedSurface,
                        directionToLight))
                {
                    float hitDistance;
                    visible = HeitzTraceVisibility(
                        rayOrigin, directionToLight, hitDistance);
                    nearestHitDistance = min(
                        nearestHitDistance, hitDistance);
                    ++tracedRayCount;
                }
                const float visibility = visible ? 1.0f : 0.0f;
                receiverTotalModulation = visibility;
                receiverDiffuseModulation = receiverTotalModulation;
            }
        }
        else
        {
            [loop]
            for (uint emitterSampleIndex = 0u;
                emitterSampleIndex < emitterSampleCount;
                ++emitterSampleIndex)
            {
                const uint sequenceIndex =
                    receiverSampleIndex * emitterSampleCount +
                    emitterSampleIndex;
                const float3 directionToLight =
                    HeitzSampleDirectionalEmitter(
                        HeitzSample2D(
                            dispatchPosition,
                            sequenceIndex,
                            sampleSequencePhase));
                const HeitzNormalizedResponse contribution =
                    HeitzEvaluateNormalizedResponse(
                        preparedMaterial,
                        preparedSurface,
                        directionToLight);
                if (!any(contribution.total > 0.0f))
                    continue;

                // Each receiver owns a matched S/U estimator. Visibility is
                // the only difference between its numerator and denominator.
                receiverTotalDenominator += contribution.total;
                receiverDiffuseDenominator += contribution.diffuse;
                float hitDistance;
                if (HeitzTraceVisibility(
                        rayOrigin, directionToLight, hitDistance))
                {
                    receiverTotalNumerator += contribution.total;
                    receiverDiffuseNumerator += contribution.diffuse;
                }
                nearestHitDistance = min(
                    nearestHitDistance, hitDistance);
                ++tracedRayCount;
            }

            const float inverseEmitterSampleCount =
                rcp(float(emitterSampleCount));
            receiverTotalModulation = saturate(
                ResolveCorrelatedRatio(
                    receiverTotalNumerator * inverseEmitterSampleCount,
                    receiverTotalDenominator * inverseEmitterSampleCount,
                    g_Heitz.denominatorEpsilon));
            receiverDiffuseModulation = saturate(
                ResolveCorrelatedRatio(
                    receiverDiffuseNumerator * inverseEmitterSampleCount,
                    receiverDiffuseDenominator * inverseEmitterSampleCount,
                    g_Heitz.denominatorEpsilon));
        }

        // Deferred PBR evaluates the analytic sun at its center direction.
        // Weight independent receiver ratios by that same response so one
        // pixel modulation factors exactly through the linear MSAA resolve.
        resolvedNumerator +=
            centerResponse.total * receiverTotalModulation;
        resolvedDenominator += centerResponse.total;
        if (ownsClosestSource)
        {
            closestTotalModulation = receiverTotalModulation;
            closestSourceModulation = receiverDiffuseModulation;
        }
    }

    if (validReceiverCount == 0u)
    {
        u_Output[pixelPosition] = 1.0f;
#if OUTPUT_SOURCE_MODULATION
        u_ClosestSourceOutput[pixelPosition] = 1.0f;
#endif
#if OUTPUT_HIT_DISTANCE
        u_HitDistance[pixelPosition] = 0.0f;
#endif
        return;
    }

#if HEITZ_RASTER_SAMPLES > 1
    if (!traceAllMsaaReceivers)
    {
        // This is the deliberate lower-cost approximation: broadcast the
        // selected owner's total modulation to final MSAA lighting while the
        // auxiliary GI source receives that owner's diffuse-only modulation.
        // Do not response-weight or coverage-scale either factor again.
        u_Output[pixelPosition] = float4(
            closestTotalModulation, 1.0f);
        u_ClosestSourceOutput[pixelPosition] = float4(
            closestSourceModulation, 1.0f);
        return;
    }
#endif

    // This is a deterministic factorization of deferred PBR's analytic
    // response, not another Monte Carlo estimate. A second stochastic epsilon
    // would turn low-energy shadowed channels white as valid receiver count
    // grows, so only zero or non-finite analytic denominators fail open.
    const float3 modulation = HeitzResolveDeterministicRatio(
        resolvedNumerator,
        resolvedDenominator);
    u_Output[pixelPosition] = float4(modulation, 1.0f);
#if OUTPUT_SOURCE_MODULATION
    u_ClosestSourceOutput[pixelPosition] = float4(
        closestSourceModulation, 1.0f);
#endif
#if OUTPUT_HIT_DISTANCE
    u_HitDistance[pixelPosition] = tracedRayCount != 0u
        ? nearestHitDistance
        : 0.0f;
#endif
}
