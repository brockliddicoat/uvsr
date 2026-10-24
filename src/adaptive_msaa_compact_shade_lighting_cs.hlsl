/*
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, subject to inclusion of this notice.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

/*
 * Copyright (c) 2017-2025 The Forge Interactive Inc.
 * Licensed under the Apache License, Version 2.0. You may obtain a copy at
 * http://www.apache.org/licenses/LICENSE-2.0
 */

// Exact provenance:
// - Descriptor-once visibility resource flow, reconstruction, and the
//   conservative linear-depth simple/complex scheduling classifier are
//   adapted from The Forge Visibility_Buffer sample's
//   src/Shaders/FSL/MsaaEdgeDetect.frag.fsl at commit
//   a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b (Apache-2.0).
// - Ascending unique-key representatives, sample-count weighting, and dense
//   invariant-work-record redistribution are informed by Burns and Hunt's
//   official Visibility Buffer supplemental code (BSD-style), SHA-256
//   6C9A2410ED37270B6CB5F8FED34CE5A330A229ECD426C16E573646FBBE0C76E2.
// - The PBR lighting body follows the adaptive sibling, which preserves
//   src/pbr_deferred_lighting_msaa_cs.hlsl from UVSR source commit
//   578496047c4753077e36f39a68df8c2c323cee58 as the unchanged Reference.
//   SameSurface never implies constant radiance: position, visibility,
//   shadows, attenuation, and sharp specular remain per sample unless
//   bit-identical inputs prove otherwise.

#include <donut/shaders/utils.hlsli>
#include <donut/shaders/shadows.hlsli>
#include "pbr_deferred_lighting_cb.h"
#include "pbr_environment.hlsli"
#include "pbr_lighting.hlsli"
#include "adaptive_msaa_compact_shade_common.hlsli"

#ifndef ADAPTIVE_MSAA_COMPACT_VISIBILITY
#define ADAPTIVE_MSAA_COMPACT_VISIBILITY 0
#endif

#ifndef UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
#define UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER 0
#endif

// The renderer must select one measured preparation topology explicitly.
// There is intentionally no default: silently changing this value would make
// a matched full-frame A/B result impossible to reproduce.
#define UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_INLINE 0
#define UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_SEPARATED 1
#ifndef UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY
#error UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY must be explicit
#endif
#if UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY != \
        UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_INLINE && \
    UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY != \
        UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_SEPARATED
#error UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY is invalid
#endif
#if ADAPTIVE_MSAA_COMPACT_SAMPLES != 2 && \
    ADAPTIVE_MSAA_COMPACT_SAMPLES != 4 && \
    ADAPTIVE_MSAA_COMPACT_SAMPLES != 8 && \
    ADAPTIVE_MSAA_COMPACT_SAMPLES != 16
#error Compact invariant preparation supports only 2x, 4x, 8x, or 16x MSAA
#endif

cbuffer c_Deferred : register(b0)
{
    PbrDeferredLightingConstants g_PbrDeferred;
};

#define g_Deferred g_PbrDeferred.deferred

Texture2DArray t_ShadowMapArray : register(t0);
TextureCubeArray t_DiffuseEnvironment : register(t1);
TextureCubeArray t_SpecularEnvironment : register(t2);
Texture2D t_EnvironmentBrdf : register(t3);
Texture2D t_ResolvedBackground : register(t17);
#if ADAPTIVE_MSAA_COMPACT_VISIBILITY
Texture2D t_VisibilityBaseLighting : register(t18);
Texture2D t_VisibilityComposite : register(t19);
#endif
Texture2D<float> t_DirectionalVisibility : register(t20);

SamplerComparisonState s_ShadowSamplerComparison : register(s1);
SamplerState s_DiffuseEnvironmentSampler : register(s2);
SamplerState s_EnvironmentBrdfSampler : register(s3);

VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_Output : register(u0);

#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
#define UVSR_ADAPTIVE_MSAA_DIAGNOSTIC_SAMPLES \
    ADAPTIVE_MSAA_COMPACT_SAMPLES
#include "adaptive_msaa_diagnostics_producer.hlsli"
#endif

struct CompactLightingSample
{
    float3 worldPosition;
    float linearDepth;
    float pixelFootprint;
    float deviceDepth;
};

// One originating pixel publishes at most one exact-key record in each
// statically bounded ordinal. Wave compaction redistributes only invariant
// G-buffer decoding and material/BRDF preparation. The prepared result returns
// to its originating lane before any position-, visibility-, shadow-, light-,
// view-, or specular-dependent evaluation; radiance never crosses pixels.
struct CompactInvariantPreparationRecord
{
    float4 channel0;
    float4 channel1;
    float4 channel2;
    float4 channel3;
    float ambientOcclusion;
};

struct CompactPreparedInvariant
{
    PbrGBufferData gbuffer;
    PbrPreparedMaterial preparedMaterial;
    uint referenceLightingValid;
};

// Shader Model 6 ballots contain 128 bits. This is only the accepted configured
// maximum; every observation and overflow decision uses the smaller live wave
// lane count. Tests can request a smaller positive capacity through the
// prepared constants to force the lossless inline-overflow path.
static const uint CompactPreparationMaximumQueueCapacity = 128u;

uint CompactFindNthBallotLane(
    uint4 ballot,
    uint ordinal)
{
    [unroll]
    for (uint wordIndex = 0u; wordIndex < 4u; ++wordIndex)
    {
        uint word = ballot[wordIndex];
        const uint wordCount = countbits(word);
        if (ordinal < wordCount)
        {
            [loop]
            for (uint bitOrdinal = 0u;
                bitOrdinal < ordinal;
                ++bitOrdinal)
            {
                word &= word - 1u;
            }
            return wordIndex * 32u + firstbitlow(word);
        }
        ordinal -= wordCount;
    }

    // Callers prove ordinal < countbits(ballot). Retain a deterministic lane
    // for compiler control-flow analysis without making it a recovery path.
    return 0u;
}

CompactInvariantPreparationRecord CompactWaveReadPreparationRecord(
    CompactInvariantPreparationRecord record,
    uint sourceLane)
{
    CompactInvariantPreparationRecord result;
    result.channel0 = WaveReadLaneAt(record.channel0, sourceLane);
    result.channel1 = WaveReadLaneAt(record.channel1, sourceLane);
    result.channel2 = WaveReadLaneAt(record.channel2, sourceLane);
    result.channel3 = WaveReadLaneAt(record.channel3, sourceLane);
    result.ambientOcclusion = WaveReadLaneAt(
        record.ambientOcclusion, sourceLane);
    return result;
}

CompactPreparedInvariant CompactPrepareInvariant(
    CompactInvariantPreparationRecord record)
{
    CompactPreparedInvariant result = (CompactPreparedInvariant)0;
    float4 channels[4];
    channels[0] = record.channel0;
    channels[1] = record.channel1;
    channels[2] = record.channel2;
    channels[3] = record.channel3;
    result.gbuffer = DecodePbrGBuffer(
        channels, record.ambientOcclusion);
    result.preparedMaterial = PreparePbrMaterial(
        result.gbuffer.material);
    result.referenceLightingValid =
        dot(record.channel2.xyz, record.channel2.xyz) > 1e-12f
        ? 1u : 0u;
    return result;
}

CompactPreparedInvariant CompactWaveReadPreparedInvariant(
    CompactPreparedInvariant prepared,
    uint sourceLane)
{
    CompactPreparedInvariant result = (CompactPreparedInvariant)0;
    result.gbuffer.material.baseColor = WaveReadLaneAt(
        prepared.gbuffer.material.baseColor, sourceLane);
    result.gbuffer.material.metalness = WaveReadLaneAt(
        prepared.gbuffer.material.metalness, sourceLane);
    result.gbuffer.material.perceptualRoughness = WaveReadLaneAt(
        prepared.gbuffer.material.perceptualRoughness, sourceLane);
    result.gbuffer.material.dielectricF0 = WaveReadLaneAt(
        prepared.gbuffer.material.dielectricF0, sourceLane);
    result.gbuffer.material.emissive = WaveReadLaneAt(
        prepared.gbuffer.material.emissive, sourceLane);
    result.gbuffer.material.opacity = WaveReadLaneAt(
        prepared.gbuffer.material.opacity, sourceLane);
    result.gbuffer.material.featureMask = WaveReadLaneAt(
        prepared.gbuffer.material.featureMask, sourceLane);
    result.gbuffer.shadingNormal = WaveReadLaneAt(
        prepared.gbuffer.shadingNormal, sourceLane);
    result.gbuffer.geometricNormal = WaveReadLaneAt(
        prepared.gbuffer.geometricNormal, sourceLane);
    result.gbuffer.ambientOcclusion = WaveReadLaneAt(
        prepared.gbuffer.ambientOcclusion, sourceLane);
    result.preparedMaterial.diffuseColor = WaveReadLaneAt(
        prepared.preparedMaterial.diffuseColor, sourceLane);
    result.preparedMaterial.specularF0 = WaveReadLaneAt(
        prepared.preparedMaterial.specularF0, sourceLane);
    result.preparedMaterial.alpha = WaveReadLaneAt(
        prepared.preparedMaterial.alpha, sourceLane);
    result.referenceLightingValid = WaveReadLaneAt(
        prepared.referenceLightingValid, sourceLane);
    return result;
}

bool CompactPrepareWaveQueue(
    bool queueActive,
    CompactInvariantPreparationRecord record,
    out CompactPreparedInvariant prepared,
    out uint queueOccupancy,
    out uint queueCapacity,
    out bool queueOverflow)
{
    prepared = (CompactPreparedInvariant)0;
    queueOccupancy = WaveActiveCountBits(queueActive);
    const uint liveLaneCount = WaveActiveCountBits(true);
    queueCapacity = min(
        g_CompactPreparationQueueCapacity,
        liveLaneCount);
    queueOverflow = g_CompactPreparationQueueCapacity == 0u ||
        g_CompactPreparationQueueCapacity >
            CompactPreparationMaximumQueueCapacity ||
        queueOccupancy > queueCapacity;
    if (queueOccupancy == 0u || queueOverflow)
        return false;

    const uint4 executionBallot = WaveActiveBallot(true);
    const uint executionOrdinal = WavePrefixCountBits(true);
    const uint4 recordBallot = WaveActiveBallot(queueActive);

    // Every live lane performs each WaveReadLaneAt while source lanes remain
    // active. Only the dense live-lane prefix executes invariant preparation.
    const uint workerRecordOrdinal = min(
        executionOrdinal, queueOccupancy - 1u);
    const uint sourceLane = CompactFindNthBallotLane(
        recordBallot, workerRecordOrdinal);
    const CompactInvariantPreparationRecord workerRecord =
        CompactWaveReadPreparationRecord(record, sourceLane);

    CompactPreparedInvariant workerResult =
        (CompactPreparedInvariant)0;
    if (executionOrdinal < queueOccupancy)
        workerResult = CompactPrepareInvariant(workerRecord);

    const uint recordOrdinal = WavePrefixCountBits(queueActive);
    const uint workerLane = CompactFindNthBallotLane(
        executionBallot, min(recordOrdinal, queueOccupancy - 1u));
    prepared = CompactWaveReadPreparedInvariant(
        workerResult, workerLane);
    return queueActive;
}

bool CompactDepthClassifiesSimpleRecord(
    uint exactKeyMask,
    CompactLightingSample samples[ADAPTIVE_MSAA_COMPACT_SAMPLES])
{
    const uint fullSampleMask =
        (1u << ADAPTIVE_MSAA_COMPACT_SAMPLES) - 1u;
    if (exactKeyMask != fullSampleMask)
        return false;

    // This is deliberately stricter than The Forge's average-depth edge
    // classifier: a simple record must be one fully covered exact surface and
    // its reconstructed linear-depth range must fit the existing conservative
    // coherence tolerance. Classification changes scheduling only; the exact
    // key remains the sole ownership identity and CanShareShading is evaluated
    // later from independently reconstructed sample positions and depths.
    float minimumDepth = 3.402823466e+38f;
    float maximumDepth = -3.402823466e+38f;
    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
        ++sampleIndex)
    {
        const float linearDepth = samples[sampleIndex].linearDepth;
        if (!CompactFinite(linearDepth))
            return false;
        minimumDepth = min(minimumDepth, linearDepth);
        maximumDepth = max(maximumDepth, linearDepth);
    }
    const float depthScale = max(
        abs(minimumDepth), abs(maximumDepth));
    return depthScale > 0.0f &&
        (maximumDepth - minimumDepth) / depthScale <=
            CompactRelativeDepthTolerance;
}

bool CompactPrepareInlineRecord(
    bool recordActive,
    CompactInvariantPreparationRecord record,
    out CompactPreparedInvariant prepared,
    out uint queueOccupancy,
    out uint queueCapacity,
    out bool queueOverflow)
{
    prepared = (CompactPreparedInvariant)0;
    queueOccupancy = 0u;
    queueCapacity = 0u;
    queueOverflow = false;
    if (!recordActive)
        return false;
    prepared = CompactPrepareInvariant(record);
    return true;
}

bool CompactPrepareSeparatedWaveRecord(
    bool recordActive,
    bool depthClassifiedSimple,
    CompactInvariantPreparationRecord record,
    out CompactPreparedInvariant prepared,
    out uint queueOccupancy,
    out uint queueCapacity,
    out bool queueOverflow)
{
    const bool simpleRecordActive =
        recordActive && depthClassifiedSimple;
    const bool complexRecordActive =
        recordActive && !depthClassifiedSimple;

    CompactPreparedInvariant simplePrepared;
    uint simpleOccupancy;
    uint simpleCapacity;
    bool simpleOverflow;
    const bool simplePreparedActive = CompactPrepareWaveQueue(
        simpleRecordActive,
        record,
        simplePrepared,
        simpleOccupancy,
        simpleCapacity,
        simpleOverflow);

    CompactPreparedInvariant complexPrepared;
    uint complexOccupancy;
    uint complexCapacity;
    bool complexOverflow;
    const bool complexPreparedActive = CompactPrepareWaveQueue(
        complexRecordActive,
        record,
        complexPrepared,
        complexOccupancy,
        complexCapacity,
        complexOverflow);

    const uint recordOccupancy = WaveActiveCountBits(recordActive);
    const uint classifiedOccupancy = simpleOccupancy + complexOccupancy;
    queueOccupancy = max(simpleOccupancy, complexOccupancy);
    queueCapacity = min(simpleCapacity, complexCapacity);
    // The class predicates are mutually exclusive, so equality with the
    // active-record count proves that no record was dropped or duplicated.
    // Keep a fail-closed result. Production's accepted maximum cannot
    // overflow a conforming wave, while a deliberately smaller test capacity
    // exercises the same inline fallback used for any accounting defect.
    queueOverflow = simpleOverflow || complexOverflow ||
        queueOccupancy > queueCapacity ||
        classifiedOccupancy != recordOccupancy;
    if (depthClassifiedSimple)
        prepared = simplePrepared;
    else
        prepared = complexPrepared;
    return !queueOverflow &&
        (simplePreparedActive || complexPreparedActive);
}

bool CompactPrepareInvariantTopology(
    bool recordActive,
    bool depthClassifiedSimple,
    CompactInvariantPreparationRecord record,
    out CompactPreparedInvariant prepared,
    out uint queueOccupancy,
    out uint queueCapacity,
    out bool queueOverflow)
{
#if UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_INLINE
    return CompactPrepareInlineRecord(
        recordActive,
        record,
        prepared,
        queueOccupancy,
        queueCapacity,
        queueOverflow);
#elif UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_SEPARATED
    return CompactPrepareSeparatedWaveRecord(
        recordActive,
        depthClassifiedSimple,
        record,
        prepared,
        queueOccupancy,
        queueCapacity,
        queueOverflow);
#endif
}

CompactLightingSample CompactMakeLightingSample(
    CompactSurface surface)
{
    CompactLightingSample result;
    result.worldPosition = surface.lightingWorldPosition;
    result.linearDepth = surface.linearDepth;
    result.pixelFootprint = surface.pixelFootprint;
    result.deviceDepth = surface.deviceDepth;
    return result;
}

CompactLightingSample CompactMakeFallbackLightingSample(
    int2 pixelPosition,
    float deviceDepth)
{
    CompactLightingSample result;
    result.worldPosition = ReconstructWorldPosition(
        g_CompactGBuffer.view,
        float2(pixelPosition) + 0.5f,
        deviceDepth);
    result.linearDepth = CompactLinearViewDepth(result.worldPosition);
    result.pixelFootprint = CompactPixelFootprint(
        pixelPosition, deviceDepth, result.worldPosition);
    result.deviceDepth = deviceDepth;
    return result;
}

bool CompactCanShareLightingSamples(
    CompactLightingSample first,
    CompactLightingSample second)
{
    const float depthDifference = abs(
        first.linearDepth - second.linearDepth);
    const float depthScale = max(
        abs(first.linearDepth), abs(second.linearDepth));
    if ((depthScale == 0.0f && depthDifference != 0.0f) ||
        (depthScale != 0.0f && depthDifference / depthScale >
            CompactRelativeDepthTolerance))
    {
        return false;
    }
    const float positionSpan = length(
        first.worldPosition - second.worldPosition);
    const float smallerFootprint = min(
        first.pixelFootprint, second.pixelFootprint);
    return positionSpan <=
        CompactPositionFootprintFraction * smallerFootprint;
}

bool CompactCanShareLightingRadiance(
    CompactLightingSample first,
    CompactLightingSample second)
{
    return CompactEqualFloatBits(
            first.deviceDepth, second.deviceDepth) &&
        CompactEqualFloat3Bits(
            first.worldPosition, second.worldPosition);
}

float CompactGetRandom(float2 position)
{
    const int x = int(position.x) & 3;
    const int y = int(position.y) & 3;
    return g_Deferred.noisePattern[y][x];
}

float CompactGetDirectionalLightVisibility(
    uint lightIndex,
    float directionalVisibility)
{
    return int(lightIndex) ==
        g_PbrDeferred.directionalVisibilityLightIndex
        ? saturate(directionalVisibility)
        : 1.0f;
}

float CompactEvaluateLightVisibility(
    LightConstants light,
    float3 surfaceWorldPosition,
    float2 sinCosRotation,
    float initialVisibility)
{
    float visibility = initialVisibility;
    float2 combinedCascadeVisibility = 0.0f;
    [loop]
    for (int cascade = 0; cascade < 4; ++cascade)
    {
        if (light.shadowCascades[cascade] < 0)
            break;
        const float2 cascadeVisibility = EvaluateShadowPoisson(
            t_ShadowMapArray,
            s_ShadowSamplerComparison,
            g_Deferred.shadows[light.shadowCascades[cascade]],
            surfaceWorldPosition,
            sinCosRotation,
            3.0f);
        combinedCascadeVisibility = saturate(
            combinedCascadeVisibility + cascadeVisibility *
            (1.0001f - combinedCascadeVisibility.y));
        if (combinedCascadeVisibility.y == 1.0f)
            break;
    }
    combinedCascadeVisibility.x +=
        (1.0f - combinedCascadeVisibility.y) *
        light.outOfBoundsShadow;
    visibility *= combinedCascadeVisibility.x;
    if (!(visibility > 0.0f))
        return 0.0f;

    [loop]
    for (int object = 0; object < 4; ++object)
    {
        if (light.perObjectShadows[object] < 0)
            continue;
        const float2 objectVisibility = EvaluateShadowPoisson(
            t_ShadowMapArray,
            s_ShadowSamplerComparison,
            g_Deferred.shadows[light.perObjectShadows[object]],
            surfaceWorldPosition,
            sinCosRotation,
            3.0f);
        visibility *= saturate(
            objectVisibility.x + (1.0f - objectVisibility.y));
        if (!(visibility > 0.0f))
            break;
    }
    return saturate(visibility);
}

bool CompactShadeLoadedSample(
    int2 dispatchPosition,
    float3 surfaceWorldPosition,
    CompactPreparedInvariant invariant,
    float directionalVisibility,
    out float3 finalLinearHdr
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
    , out bool diagnosticsRadianceFinite
#endif
    )
{
    finalLinearHdr = 0.0f;
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
    diagnosticsRadianceFinite = true;
#endif
    if (invariant.referenceLightingValid == 0u)
    {
        return false;
    }
    if (!CompactFinite3(surfaceWorldPosition))
    {
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
        diagnosticsRadianceFinite = false;
#endif
        return false;
    }

    const PbrGBufferData gbuffer = invariant.gbuffer;
    const PbrPreparedMaterial preparedMaterial =
        invariant.preparedMaterial;

    const float3 viewIncident = GetIncidentVector(
        g_Deferred.view.cameraDirectionOrPosition,
        surfaceWorldPosition);
    PbrSurfaceInteraction surface;
    surface.position = surfaceWorldPosition;
    surface.shadingNormal = gbuffer.shadingNormal;
    surface.geometricNormal = gbuffer.geometricNormal;
    surface.viewDirection = -viewIncident;
    const PbrPreparedSurface preparedSurface =
        PreparePbrSurface(surface);

    LightProbeConstants environmentProbe = (LightProbeConstants)0;
    if (g_Deferred.numLightProbes > 0u)
        environmentProbe = g_Deferred.lightProbes[0];
    const PbrPreparedEnvironment preparedEnvironment =
        PreparePbrEnvironment(
            gbuffer.material,
            surface,
            environmentProbe.mipLevels);

    float3 environmentDiffuseResponse = 0.0f;
    float3 prefilteredEnvironment = 0.0f;
    float2 environmentBrdf = 0.0f;
    float3 environmentDiffuse = 0.0f;
    float3 environmentSpecular = 0.0f;
    if (g_Deferred.numLightProbes > 0u)
    {
        const bool needDiffuseEnvironment =
            g_PbrDeferred.separateIndirect == 0 ||
            g_PbrDeferred.lightingDebugView == 4u ||
            g_PbrDeferred.lightingDebugView == 9u;
        if (needDiffuseEnvironment &&
            environmentProbe.diffuseScale > 0.0f)
        {
            environmentDiffuseResponse =
                t_DiffuseEnvironment.SampleLevel(
                    s_DiffuseEnvironmentSampler,
                    float4(
                        preparedSurface.shadingNormal,
                        environmentProbe.diffuseArrayIndex),
                    0.0f).rgb * environmentProbe.diffuseScale;
            if (any(!isfinite(environmentDiffuseResponse)))
                environmentDiffuseResponse = 0.0f;
            environmentDiffuse = EvaluatePbrEnvironmentDiffuse(
                preparedEnvironment,
                environmentDiffuseResponse,
                gbuffer.ambientOcclusion);
        }

        const bool needSpecularEnvironment =
            g_PbrDeferred.separateIndirect == 0 ||
            (g_PbrDeferred.lightingDebugView >= 6u &&
                g_PbrDeferred.lightingDebugView <= 9u);
        if (needSpecularEnvironment &&
            environmentProbe.specularScale > 0.0f &&
            preparedEnvironment.valid > 0.0f)
        {
            prefilteredEnvironment =
                t_SpecularEnvironment.SampleLevel(
                    s_DiffuseEnvironmentSampler,
                    float4(
                        preparedEnvironment.reflectionDirection,
                        environmentProbe.specularArrayIndex),
                    preparedEnvironment.specularMip).rgb *
                environmentProbe.specularScale;
            environmentBrdf = t_EnvironmentBrdf.SampleLevel(
                s_EnvironmentBrdfSampler,
                float2(
                    preparedEnvironment.noV,
                    preparedEnvironment.perceptualRoughness),
                0.0f).xy;
            if (any(!isfinite(prefilteredEnvironment)))
                prefilteredEnvironment = 0.0f;
            if (any(!isfinite(environmentBrdf)))
                environmentBrdf = 0.0f;
            environmentSpecular = EvaluatePbrEnvironmentSpecular(
                preparedEnvironment,
                prefilteredEnvironment,
                environmentBrdf,
                gbuffer.ambientOcclusion);
        }
    }

    if (g_PbrDeferred.lightingDebugView != 0u)
    {
        float3 debugColor = 0.0f;
        if (g_PbrDeferred.lightingDebugView == 1u)
            debugColor = gbuffer.shadingNormal * 0.5f + 0.5f;
        else if (g_PbrDeferred.lightingDebugView == 2u)
            debugColor = gbuffer.geometricNormal * 0.5f + 0.5f;
        else if (g_PbrDeferred.lightingDebugView == 3u)
        {
            const float angularDifference = saturate(
                1.0f - dot(
                    gbuffer.shadingNormal,
                    gbuffer.geometricNormal));
            debugColor = float3(
                angularDifference, 0.0f,
                1.0f - angularDifference);
        }
        else if (g_PbrDeferred.lightingDebugView == 4u)
            debugColor = environmentDiffuseResponse;
        else if (g_PbrDeferred.lightingDebugView == 5u)
            debugColor = (1.0f +
                0.5f * gbuffer.shadingNormal.x) * 0.5f;
        else if (g_PbrDeferred.lightingDebugView == 6u)
            debugColor = prefilteredEnvironment;
        else if (g_PbrDeferred.lightingDebugView == 7u)
            debugColor = float3(
                environmentBrdf.x, environmentBrdf.y, 0.0f);
        else if (g_PbrDeferred.lightingDebugView == 8u)
            debugColor = environmentSpecular;
        else if (g_PbrDeferred.lightingDebugView == 9u)
            debugColor = environmentDiffuse + environmentSpecular;
        else if (g_PbrDeferred.lightingDebugView == 10u)
        {
            const float specularOcclusion =
                PbrEnvironmentSpecularOcclusion(
                    preparedEnvironment.noV,
                    gbuffer.ambientOcclusion,
                    preparedEnvironment.perceptualRoughness);
            debugColor = specularOcclusion.xxx;
        }
        else if (g_PbrDeferred.lightingDebugView == 11u)
        {
            const float normalizedMip =
                environmentProbe.mipLevels > 1.0f
                ? preparedEnvironment.specularMip /
                    (environmentProbe.mipLevels - 1.0f)
                : 0.0f;
            debugColor = normalizedMip.xxx;
        }
        finalLinearHdr = min(max(debugColor, 0.0f), 65504.0f);
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
        diagnosticsRadianceFinite = all(isfinite(finalLinearHdr));
#endif
        return true;
    }

    float3 directDiffuse = 0.0f;
    float3 directSpecular = 0.0f;
    if (g_Deferred.numLights > 0u)
    {
        const float angle = CompactGetRandom(
            dispatchPosition +
            float2(g_Deferred.randomOffset.x, 0.0f));
        const float2 sinCosRotation =
            float2(sin(angle), cos(angle));
        [loop]
        for (uint lightIndex = 0u;
            lightIndex < g_Deferred.numLights;
            ++lightIndex)
        {
            const LightConstants light =
                g_Deferred.lights[lightIndex];
            float visibility =
                CompactGetDirectionalLightVisibility(
                    lightIndex, directionalVisibility);
            if (!(visibility > 0.0f))
                continue;

            PbrLightSample lightSample = SamplePbrLight(
                light, surfaceWorldPosition, 1.0f);
            if (!HasPositiveFinitePbrSignal(
                    lightSample.incidentRadiance, 1.0f) ||
                !CanEvaluatePbrDirectSurfacePrepared(
                    preparedSurface,
                    lightSample.directionToLight))
            {
                continue;
            }
            visibility = CompactEvaluateLightVisibility(
                light,
                surfaceWorldPosition,
                sinCosRotation,
                visibility);
            if (!(visibility > 0.0f))
                continue;
            lightSample.visibility = visibility;
            const PbrDirectLighting direct =
                EvaluateDirectLightPrevalidated(
                    preparedMaterial,
                    preparedSurface,
                    lightSample);
            directDiffuse += direct.diffuse;
            directSpecular += direct.specular;
        }
    }

    const float3 diffuse = directDiffuse +
        (g_PbrDeferred.separateIndirect != 0
            ? 0.0f : environmentDiffuse);
    const float3 specular = directSpecular +
        (g_PbrDeferred.separateIndirect != 0
            ? 0.0f : environmentSpecular);
    finalLinearHdr = max(
        diffuse + specular + gbuffer.material.emissive,
        0.0f);
    if (any(!isfinite(finalLinearHdr)))
    {
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
        diagnosticsRadianceFinite = false;
#endif
        finalLinearHdr = 0.0f;
        return false;
    }
    return true;
}

void CompactStoreLighting(
    int2 pixelPosition,
    uint visibilityCoveredSampleCount,
    float inverseSampleCount,
    float3 finalLinearHdr)
{
#if ADAPTIVE_MSAA_COMPACT_VISIBILITY
    if (g_PbrDeferred.visibilityDebugView != 0u)
    {
        finalLinearHdr = t_VisibilityComposite[pixelPosition].rgb;
    }
    else if (g_PbrDeferred.lightingDebugView == 0u &&
        visibilityCoveredSampleCount != 0u)
    {
        const float coverage =
            float(visibilityCoveredSampleCount) * inverseSampleCount;
        const float3 visibilityCorrection =
            t_VisibilityComposite[pixelPosition].rgb -
            t_VisibilityBaseLighting[pixelPosition].rgb;
        if (all(isfinite(visibilityCorrection)))
            finalLinearHdr += visibilityCorrection * coverage;
    }
#endif
    if (any(!isfinite(finalLinearHdr)))
        finalLinearHdr = 0.0f;
    u_Output[pixelPosition] = float4(
        min(max(finalLinearHdr, 0.0f), 65504.0f),
        0.0f);
}

#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER && \
    UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_SEPARATED
void CompactDiagnosticsStorePreparationWorkList(
    int2 pixelPosition,
    uint peakOccupancy,
    uint observedCapacity,
    bool overflowed)
{
    // Pixel metadata is the existing producer ABI's only work-list carrier.
    // Publish one observation per wave instead of duplicating the same wave
    // queue count for every pixel lane. The renderer must advertise
    // DiagnosticsCounterWorkListValid only for this separated permutation and
    // set its accepted maximum to
    // CompactPreparationMaximumQueueCapacity. Capacity is the actual live
    // wave/configured minimum, never the ballot-width upper bound.
    if (!WaveIsFirstLane() ||
        (g_DiagnosticsExecutionFlags &
            DiagnosticsProducerWritePixelMetadata) == 0u ||
        !DiagnosticsProducerPixelInViewport(pixelPosition))
    {
        return;
    }
    const uint metadataIndex =
        DiagnosticsProducerLinearPixel(pixelPosition);
    DiagnosticsGpuPixelMetadataRecord metadata =
        u_DiagnosticsPixelMetadata[metadataIndex];
    metadata.validFields |= DiagnosticsPixelWorkListValid;
    if (overflowed)
        metadata.validFields |= DiagnosticsPixelWorkListOverflow;
    metadata.workListOccupancy = peakOccupancy;
    metadata.workListCapacity = observedCapacity;
    u_DiagnosticsPixelMetadata[metadataIndex] = metadata;
}
#endif

void CompactFailLightingPixelLoaded(
    int2 pixelPosition,
    uint failureReason,
    uint2 loadedKeys[ADAPTIVE_MSAA_COMPACT_SAMPLES],
    uint loadedClasses[ADAPTIVE_MSAA_COMPACT_SAMPLES],
    float loadedDepths[ADAPTIVE_MSAA_COMPACT_SAMPLES])
{
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
    const bool diagnosticsRadianceAvailable =
        g_PbrDeferred.lightingDebugView == 0u &&
        g_PbrDeferred.visibilityDebugView == 0u;
    DiagnosticsProducerInvalidatePixelMetadata(pixelPosition);
    [loop]
    for (uint sampleIndex = 0u;
        sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
        ++sampleIndex)
    {
        const uint2 key = loadedKeys[sampleIndex];
        const uint visibilityClass = loadedClasses[sampleIndex];
        const float depth = loadedDepths[sampleIndex];
        const bool background =
            visibilityClass == CompactVisibilityClassBackground;
        const bool validBackground = background &&
            CompactIsClearedVisibilityKey(key) &&
            CompactFinite(depth) && asuint(depth) == 0u;
        bool reconstruct;
        bool fallback;
        bool localFallback;
        uint markerReason;
        const bool validClass = CompactDecodeVisibilityClass(
            visibilityClass,
            reconstruct,
            fallback,
            localFallback,
            markerReason);
        uint keyReason = CompactReasonNone;
        if (reconstruct)
        {
            uint ignoredSlot;
            uint ignoredGeneration;
            keyReason = CompactValidateKey(
                key, ignoredSlot, ignoredGeneration);
        }
        else if (fallback)
        {
            keyReason = CompactValidateFallbackOwnershipKey(
                key, markerReason, localFallback);
        }
        const uint reason = validBackground
            ? CompactReasonNone
            : keyReason != CompactReasonNone
                ? keyReason
                : validClass && fallback
                    ? markerReason
                    : failureReason;
        DiagnosticsProducerStoreStatus(
            pixelPosition,
            sampleIndex,
            reason,
            DiagnosticsUnavailableErrorReason);
        DiagnosticsGpuSampleInputRecord record =
            DiagnosticsProducerEmptySample();
        const float3 backgroundRadiance = max(
            t_DiagnosticsMsaaBackground.Load(
                pixelPosition, sampleIndex).rgb,
            0.0f);
        record.validFields = DiagnosticsSampleFallbackReason |
            (diagnosticsRadianceAvailable
                ? DiagnosticsSampleRadiance
                : 0u);
        record.radianceR = backgroundRadiance.r;
        record.radianceG = backgroundRadiance.g;
        record.radianceB = backgroundRadiance.b;
        record.fallbackReason = reason;
        DiagnosticsProducerStoreSelected(
            pixelPosition, sampleIndex, record);
    }
#endif
    // CPU preflight is required to make this guard unreachable in accepted
    // compact rendering. If a live GPU value nevertheless violates that
    // proof, retain the independently resolved uncovered-sample background
    // and publish failure data rather than replacing the entire pixel with
    // black. This is diagnostic containment, not a reference-shading result.
    u_Output[pixelPosition] = t_ResolvedBackground[pixelPosition];
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId >=
        uint2(g_Deferred.view.viewportSize)))
    {
        return;
    }
    const int2 pixelPosition = int2(dispatchThreadId) +
        int2(g_Deferred.view.viewportOrigin);
    const float inverseSampleCount =
        1.0f / float(ADAPTIVE_MSAA_COMPACT_SAMPLES);

    uint2 keys[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    uint classes[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    float depths[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    uint coveredMask = 0u;
    uint referenceVisibilityCoveredMask = 0u;
    uint descriptorFailureReason = CompactReasonNone;
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
    CompactSurface diagnosticsSurfaces[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    uint diagnosticsFinalGroups[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    uint diagnosticsFinalRepresentatives[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    uint diagnosticsFallbackReasons[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    uint diagnosticsErrorBudgetReasons[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    float3 diagnosticsRadiance[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    const bool diagnosticsRadianceAvailable =
        g_PbrDeferred.lightingDebugView == 0u &&
        g_PbrDeferred.visibilityDebugView == 0u;
#endif
    [loop]
    for (uint sampleIndex = 0u;
        sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
        ++sampleIndex)
    {
        keys[sampleIndex] = t_CompactSurfaceOwnership.Load(
            pixelPosition, sampleIndex);
        classes[sampleIndex] = t_CompactVisibilityClass.Load(
            pixelPosition, sampleIndex);
        depths[sampleIndex] = t_CompactDepth.Load(
            pixelPosition, sampleIndex);
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
        diagnosticsSurfaces[sampleIndex] = (CompactSurface)0;
        diagnosticsFinalGroups[sampleIndex] =
            DiagnosticsUnavailableWord;
        diagnosticsFinalRepresentatives[sampleIndex] =
            DiagnosticsUnavailableWord;
        diagnosticsFallbackReasons[sampleIndex] = CompactReasonNone;
        diagnosticsErrorBudgetReasons[sampleIndex] =
            DiagnosticsUnavailableErrorReason;
        diagnosticsRadiance[sampleIndex] = max(
            t_DiagnosticsMsaaBackground.Load(
                pixelPosition, sampleIndex).rgb,
            0.0f);
#endif
        const bool background =
            classes[sampleIndex] == CompactVisibilityClassBackground;
        if (background)
        {
            if (!CompactIsClearedVisibilityKey(keys[sampleIndex]) ||
                !CompactFinite(depths[sampleIndex]) ||
                asuint(depths[sampleIndex]) != 0u)
            {
                if (descriptorFailureReason == CompactReasonNone)
                {
                    descriptorFailureReason =
                        CompactReasonReconstructionFailure;
                }
            }
            continue;
        }

        bool reconstruct;
        bool fallback;
        bool localFallback;
        uint fallbackReason;
        if (!CompactDecodeVisibilityClass(
                classes[sampleIndex],
                reconstruct,
                fallback,
                localFallback,
                fallbackReason))
        {
            if (descriptorFailureReason == CompactReasonNone)
            {
                descriptorFailureReason =
                    CompactReasonReconstructionFailure;
            }
            continue;
        }
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
        if (fallback)
            diagnosticsFallbackReasons[sampleIndex] = fallbackReason;
#endif
        uint keyReason = CompactReasonNone;
        if (reconstruct)
        {
            uint ignoredSlot;
            uint ignoredGeneration;
            keyReason = CompactValidateKey(
                keys[sampleIndex], ignoredSlot, ignoredGeneration);
            if (!CompactValidDeviceDepth(depths[sampleIndex]))
                keyReason = CompactReasonReconstructionFailure;
        }
        else
        {
            keyReason = CompactValidateFallbackOwnershipKey(
                keys[sampleIndex], fallbackReason, localFallback);
            if (!CompactValidFallbackDeviceDepth(depths[sampleIndex]))
                keyReason = CompactReasonReconstructionFailure;
        }
        if (keyReason != CompactReasonNone)
        {
            // Partial sentinels, stale/invalid handles, and malformed depth
            // are covered failures. Only the exact cleared uint2 key denotes
            // background; depth and coverage never invent identity.
            if (descriptorFailureReason == CompactReasonNone)
            {
                descriptorFailureReason =
                    keyReason;
            }
            continue;
        }
        coveredMask |= 1u << sampleIndex;
    }

    // Every key/class/depth descriptor is loaded exactly once before any
    // failure is published. Diagnostic containment consumes these
    // authoritative values instead of issuing a second multisample load.
    if (descriptorFailureReason != CompactReasonNone)
    {
        CompactFailLightingPixelLoaded(
            pixelPosition,
            descriptorFailureReason,
            keys,
            classes,
            depths);
        return;
    }

    float3 finalLinearHdr =
        g_PbrDeferred.lightingDebugView == 0u
        ? max(t_ResolvedBackground[pixelPosition].rgb, 0.0f)
        : 0.0f;

#if !UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
    if (coveredMask == 0u)
    {
        CompactStoreLighting(
            pixelPosition,
            0u,
            inverseSampleCount,
            finalLinearHdr);
        return;
    }
#endif

    CompactLightingSample samples[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    uint assignedMask = 0u;
    float directionalVisibility = 1.0f;
    if (coveredMask != 0u &&
        g_PbrDeferred.lightingDebugView == 0u &&
        g_PbrDeferred.directionalVisibilityLightIndex >= 0 &&
        uint(g_PbrDeferred.directionalVisibilityLightIndex) <
            g_Deferred.numLights)
    {
        directionalVisibility =
            t_DirectionalVisibility[pixelPosition];
    }

#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
    uint diagnosticsFinalGroupCount = 0u;
    uint diagnosticsRepresentativeMask = 0u;
    uint diagnosticsPerformedLightingEvaluations = 0u;
    uint diagnosticsNonFiniteRejectedGroupCount = 0u;
    uint diagnosticsHdrRejectedGroupCount = 0u;
    uint diagnosticsDisplayRejectedGroupCount = 0u;
    uint diagnosticsErrorBudgetRejectedSampleCount = 0u;
    float3 diagnosticsVisibilityCorrection = 0.0f;
    float3 diagnosticsVisibilityOverride = 0.0f;
    bool diagnosticsVisibilityOverridesLighting = false;
#if UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_SEPARATED
    uint diagnosticsPreparationWorkListPeakOccupancy = 0u;
    uint diagnosticsPreparationWorkListCapacity = min(
        g_CompactPreparationQueueCapacity,
        WaveActiveCountBits(true));
    bool diagnosticsPreparationWorkListOverflow = false;
#endif
#if ADAPTIVE_MSAA_COMPACT_VISIBILITY
    if (g_PbrDeferred.visibilityDebugView != 0u)
    {
        diagnosticsVisibilityOverridesLighting = true;
        diagnosticsVisibilityOverride =
            t_VisibilityComposite[pixelPosition].rgb;
    }
    else if (g_PbrDeferred.lightingDebugView == 0u)
    {
        const float3 candidateVisibilityCorrection =
            t_VisibilityComposite[pixelPosition].rgb -
            t_VisibilityBaseLighting[pixelPosition].rgb;
        if (all(isfinite(candidateVisibilityCorrection)))
        {
            diagnosticsVisibilityCorrection =
                candidateVisibilityCorrection;
        }
    }
#endif
#endif

    // Each fixed ordinal lets every live lane publish at most one exact-key
    // invariant record. False publishers remain available as dense workers,
    // including resolved-background pixels. Other samples validate their true
    // hit and derive only sample-dependent depth, position, and footprint from
    // the representative; no sample descriptor is reloaded.
    [loop]
    for (uint keyOrdinal = 0u;
        keyOrdinal < ADAPTIVE_MSAA_COMPACT_SAMPLES;
        ++keyOrdinal)
    {
        uint keyRepresentative = ADAPTIVE_MSAA_COMPACT_SAMPLES;
        [loop]
        for (uint candidate = 0u;
            candidate < ADAPTIVE_MSAA_COMPACT_SAMPLES;
            ++candidate)
        {
            const uint candidateBit = 1u << candidate;
            if ((coveredMask & candidateBit) != 0u &&
                (assignedMask & candidateBit) == 0u)
            {
                keyRepresentative = candidate;
                break;
            }
        }
        const bool hasKeyRecord =
            keyRepresentative < ADAPTIVE_MSAA_COMPACT_SAMPLES;
        const bool keyFallback = hasKeyRecord &&
            (classes[keyRepresentative] &
                CompactVisibilityClassFallbackBit) != 0u;

        uint reconstructionFailureReason = CompactReasonNone;
        CompactTriangle sharedTriangle = (CompactTriangle)0;
        CompactSurface sharedSurface = (CompactSurface)0;
        CompactInvariantPreparationRecord preparationRecord =
            (CompactInvariantPreparationRecord)0;
        uint exactKeyMask = 0u;
        if (keyFallback)
        {
            // A full-G-buffer fallback record is always sample-local. Equal
            // ownership keys do not license cross-sample radiance sharing.
            exactKeyMask = 1u << keyRepresentative;
            samples[keyRepresentative] =
                CompactMakeFallbackLightingSample(
                    pixelPosition,
                    depths[keyRepresentative]);
            preparationRecord.channel0 =
                t_CompactFallbackDiffuse.Load(
                    pixelPosition, keyRepresentative);
            preparationRecord.channel1 =
                t_CompactFallbackMaterial.Load(
                    pixelPosition, keyRepresentative);
            preparationRecord.channel2 =
                t_CompactFallbackNormals.Load(
                    pixelPosition, keyRepresentative);
            preparationRecord.channel3 =
                t_CompactFallbackEmissive.Load(
                    pixelPosition, keyRepresentative);
            preparationRecord.ambientOcclusion =
                t_CompactFallbackAmbientOcclusion.Load(
                    pixelPosition, keyRepresentative);
            if (dot(preparationRecord.channel2.xyz,
                    preparationRecord.channel2.xyz) > 1e-12f)
            {
                referenceVisibilityCoveredMask |= exactKeyMask;
            }
        }
        else if (hasKeyRecord)
        {
            [loop]
            for (uint sampleIndex = 0u;
                sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
                ++sampleIndex)
            {
                const uint bit = 1u << sampleIndex;
                if ((coveredMask & bit) != 0u &&
                    classes[sampleIndex] ==
                        CompactVisibilityClassReconstruct &&
                    CompactSameSurface(
                        keys[keyRepresentative], keys[sampleIndex]))
                {
                    exactKeyMask |= bit;
                }
            }
        }

        if (hasKeyRecord && exactKeyMask == 0u)
        {
            reconstructionFailureReason =
                CompactReasonReconstructionFailure;
        }
        else if (hasKeyRecord && !keyFallback)
        {
            sharedSurface = CompactReconstructSurfaceWithTriangle(
                pixelPosition,
                keyRepresentative,
                exactKeyMask,
                keys[keyRepresentative],
                depths[keyRepresentative],
                sharedTriangle);
            if (sharedSurface.valid == 0u)
            {
                reconstructionFailureReason = sharedSurface.reason;
            }
            else
            {
                // Match only the unchanged Reference visibility-correction
                // weight. Exact ownership remains authoritative; a degenerate
                // stored normal contributes no Reference lighting or
                // visibility correction.
                if (dot(sharedSurface.channel2.xyz,
                        sharedSurface.channel2.xyz) > 1e-12f)
                {
                    referenceVisibilityCoveredMask |= exactKeyMask;
                }
                samples[keyRepresentative] =
                    CompactMakeLightingSample(sharedSurface);
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
                diagnosticsSurfaces[keyRepresentative] = sharedSurface;
#endif
                [loop]
                for (uint sampleIndex = 0u;
                    sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
                    ++sampleIndex)
                {
                    const uint bit = 1u << sampleIndex;
                    if ((exactKeyMask & bit) == 0u ||
                        sampleIndex == keyRepresentative)
                    {
                        continue;
                    }
                    CompactSurface derivedSurface =
                        CompactDeriveSampleFromSharedTriangle(
                            pixelPosition,
                            sampleIndex,
                            keys[sampleIndex],
                            depths[sampleIndex],
                            sharedSurface,
                            sharedTriangle);
                    if (derivedSurface.valid == 0u)
                    {
                        reconstructionFailureReason =
                            derivedSurface.reason;
                        break;
                    }
                    samples[sampleIndex] =
                        CompactMakeLightingSample(derivedSurface);
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
                    diagnosticsSurfaces[sampleIndex] = derivedSurface;
#endif
                }

                preparationRecord.channel0 = sharedSurface.channel0;
                preparationRecord.channel1 = sharedSurface.channel1;
                preparationRecord.channel2 = sharedSurface.channel2;
                preparationRecord.channel3 = sharedSurface.channel3;
                preparationRecord.ambientOcclusion =
                    sharedSurface.ambientOcclusion;
            }
        }

        const bool preparationRecordActive = hasKeyRecord &&
            reconstructionFailureReason == CompactReasonNone;
        bool depthClassifiedSimple = false;
#if UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_SEPARATED
        if (preparationRecordActive && !keyFallback)
        {
            depthClassifiedSimple = CompactDepthClassifiesSimpleRecord(
                exactKeyMask,
                samples);
        }
#endif
        CompactPreparedInvariant preparedInvariant;
        uint preparationQueueOccupancy;
        uint preparationQueueCapacity;
        bool preparationQueueOverflow;
        bool preparedRecordActive =
            CompactPrepareInvariantTopology(
                preparationRecordActive,
                depthClassifiedSimple,
                preparationRecord,
                preparedInvariant,
                preparationQueueOccupancy,
                preparationQueueCapacity,
                preparationQueueOverflow);
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER && \
    UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_SEPARATED
        diagnosticsPreparationWorkListPeakOccupancy = max(
            diagnosticsPreparationWorkListPeakOccupancy,
            preparationQueueOccupancy);
        diagnosticsPreparationWorkListCapacity = max(
            diagnosticsPreparationWorkListCapacity,
            preparationQueueCapacity);
        diagnosticsPreparationWorkListOverflow =
            diagnosticsPreparationWorkListOverflow ||
            preparationQueueOverflow;
#endif
        if (preparationQueueOverflow)
        {
            // The source record is still resident in its originating lane.
            // Execute invariant preparation inline and continue the exact
            // grouping/weighting/shading path; overflow may cost work but can
            // never erase covered samples.
            preparedInvariant = CompactPrepareInvariant(
                preparationRecord);
            preparedRecordActive = preparationRecordActive;
        }
        if (reconstructionFailureReason != CompactReasonNone)
        {
            CompactFailLightingPixelLoaded(
                pixelPosition,
                reconstructionFailureReason,
                keys,
                classes,
                depths);
            return;
        }
        if (!hasKeyRecord)
            continue;
        if (!preparedRecordActive)
        {
            CompactFailLightingPixelLoaded(
                pixelPosition,
                CompactReasonReconstructionFailure,
                keys,
                classes,
                depths);
            return;
        }

        // SameSurface proves ownership only. Build conservative shading
        // subgroups pairwise; a near/steep/discontinuous group becomes
        // deterministic singletons and therefore executes reference shading.
        uint keyGroupRepresentatives[ADAPTIVE_MSAA_COMPACT_SAMPLES];
        uint keyGroupMasks[ADAPTIVE_MSAA_COMPACT_SAMPLES];
        uint keyGroupCount = 0u;
        uint keyAssignedMask = 0u;
        [loop]
        for (uint sampleIndex = 0u;
            sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
            ++sampleIndex)
        {
            const uint bit = 1u << sampleIndex;
            if ((exactKeyMask & bit) == 0u ||
                (keyAssignedMask & bit) != 0u)
            {
                continue;
            }
            uint safeMask = bit;
            [loop]
            for (uint candidate = sampleIndex + 1u;
                candidate < ADAPTIVE_MSAA_COMPACT_SAMPLES;
                ++candidate)
            {
                const uint candidateBit = 1u << candidate;
                if ((exactKeyMask & candidateBit) == 0u ||
                    (keyAssignedMask & candidateBit) != 0u)
                {
                    continue;
                }
                bool pairwiseSafe = true;
                [loop]
                for (uint member = 0u;
                    member < ADAPTIVE_MSAA_COMPACT_SAMPLES;
                    ++member)
                {
                    if ((safeMask & (1u << member)) != 0u &&
                        !CompactCanShareLightingSamples(
                            samples[member], samples[candidate]))
                    {
                        pairwiseSafe = false;
                    }
                }
                if (pairwiseSafe)
                    safeMask |= candidateBit;
            }
            keyGroupRepresentatives[keyGroupCount] = sampleIndex;
            keyGroupMasks[keyGroupCount] = safeMask;
            ++keyGroupCount;
            keyAssignedMask |= safeMask;
        }
        if (keyAssignedMask != exactKeyMask ||
            keyGroupCount == 0u ||
            keyGroupCount > ADAPTIVE_MSAA_COMPACT_SAMPLES)
        {
            CompactFailLightingPixelLoaded(
                pixelPosition,
                CompactReasonReconstructionFailure,
                keys,
                classes,
                depths);
            return;
        }

        // The dense worker result is invariant for this exact key and shared
        // only inside its originating pixel, even when conservative coherence
        // splits lighting into per-sample reference-equivalent subgroups.
        [loop]
        for (uint keyGroupIndex = 0u;
            keyGroupIndex < keyGroupCount;
            ++keyGroupIndex)
        {
            const uint representative =
                keyGroupRepresentatives[keyGroupIndex];
            const uint memberMask = keyGroupMasks[keyGroupIndex];
            const uint memberCount = countbits(memberMask);
            const float groupWeight =
                float(memberCount) * inverseSampleCount;
            bool shareCompleteRadiance = memberCount > 1u;
            if (shareCompleteRadiance)
            {
                [loop]
                for (uint sampleIndex = 0u;
                    sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
                    ++sampleIndex)
                {
                    if ((memberMask & (1u << sampleIndex)) != 0u &&
                        sampleIndex != representative &&
                        !CompactCanShareLightingRadiance(
                            samples[representative],
                            samples[sampleIndex]))
                    {
                        shareCompleteRadiance = false;
                    }
                }
            }

            if (shareCompleteRadiance)
            {
                float3 representativeRadiance;
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
                bool representativeRadianceFinite = true;
#endif
                const bool representativeShadeValid =
                    CompactShadeLoadedSample(
                    int2(dispatchThreadId),
                    samples[representative].worldPosition,
                    preparedInvariant,
                    directionalVisibility,
                    representativeRadiance
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
                    , representativeRadianceFinite
#endif
                    );
                // The unchanged Reference shader also converts a degenerate
                // normal or non-finite lighting result to a zero contribution
                // for that sample. Preserve that result locally; it is not a
                // reconstruction failure and must never discard other valid
                // samples in the pixel.
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
                // Validate every candidate shared-radiance group against the
                // complete Reference-equivalent per-sample result. The exact
                // key established ownership only; it never participates in
                // this error comparison. Probe evaluations are diagnostic
                // overhead and do not count as production evaluations saved.
                if ((g_DiagnosticsExecutionFlags &
                        DiagnosticsProducerValidateErrorBudget) != 0u)
                {
                float3 diagnosticsReferenceRadiances[
                    ADAPTIVE_MSAA_COMPACT_SAMPLES];
                uint diagnosticsReferenceShadeValidMask = 0u;
                uint diagnosticsNonFiniteMemberMask = 0u;
                uint diagnosticsHdrExceededMemberMask = 0u;
                uint diagnosticsDisplayExceededMemberMask = 0u;
                [loop]
                for (uint sampleIndex = 0u;
                    sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
                    ++sampleIndex)
                {
                    const uint sampleBit = 1u << sampleIndex;
                    if ((memberMask & sampleBit) == 0u)
                        continue;

                    float3 referenceRadiance =
                        representativeRadiance;
                    bool referenceShadeValid =
                        representativeShadeValid;
                    bool referenceRadianceFinite =
                        representativeRadianceFinite;
                    if (sampleIndex != representative)
                    {
                        referenceShadeValid =
                            CompactShadeLoadedSample(
                                int2(dispatchThreadId),
                                samples[sampleIndex].worldPosition,
                                preparedInvariant,
                                directionalVisibility,
                                referenceRadiance,
                                referenceRadianceFinite);
                    }
                    diagnosticsReferenceRadiances[sampleIndex] =
                        referenceRadiance;
                    if (referenceShadeValid)
                        diagnosticsReferenceShadeValidMask |= sampleBit;

                    float3 adaptiveCompleteRadiance =
                        representativeRadiance;
                    float3 referenceCompleteRadiance =
                        referenceRadiance;
                    bool adaptiveCompleteFinite =
                        representativeRadianceFinite;
                    bool referenceCompleteFinite =
                        referenceRadianceFinite;
                    if (diagnosticsVisibilityOverridesLighting)
                    {
                        adaptiveCompleteRadiance =
                            diagnosticsVisibilityOverride;
                        referenceCompleteRadiance =
                            diagnosticsVisibilityOverride;
                        adaptiveCompleteFinite = all(isfinite(
                            adaptiveCompleteRadiance));
                        referenceCompleteFinite =
                            adaptiveCompleteFinite;
                    }
                    else
                    {
                        if ((referenceVisibilityCoveredMask &
                                (1u << representative)) != 0u)
                        {
                            adaptiveCompleteRadiance +=
                                diagnosticsVisibilityCorrection;
                        }
                        if ((referenceVisibilityCoveredMask & sampleBit) !=
                            0u)
                        {
                            referenceCompleteRadiance +=
                                diagnosticsVisibilityCorrection;
                        }
                    }
                    const bool finiteComparison =
                        adaptiveCompleteFinite &&
                        referenceCompleteFinite &&
                        all(isfinite(adaptiveCompleteRadiance)) &&
                        all(isfinite(referenceCompleteRadiance));
                    if (!finiteComparison)
                    {
                        diagnosticsNonFiniteMemberMask |= sampleBit;
                    }
                    else if (DiagnosticsProducerHdrSpatialError(
                            adaptiveCompleteRadiance,
                            referenceCompleteRadiance) >
                        DiagnosticsHdrGroupLimit)
                    {
                        diagnosticsHdrExceededMemberMask |= sampleBit;
                    }
                    if (finiteComparison &&
                        DiagnosticsProducerDisplaySpatialError(
                            adaptiveCompleteRadiance,
                            referenceCompleteRadiance) >
                            DiagnosticsDisplayGroupLimit)
                    {
                        diagnosticsDisplayExceededMemberMask |= sampleBit;
                    }
                }

                // Non-finite evidence wins, then HDR, then display-space
                // error; equality at either frozen threshold remains
                // accepted. Any failing member rejects the whole group.
                const uint diagnosticsGroupErrorReason =
                    diagnosticsNonFiniteMemberMask != 0u
                        ? DiagnosticsErrorReasonNonFinite
                        : diagnosticsHdrExceededMemberMask != 0u
                            ? DiagnosticsErrorReasonHdrLimitExceeded
                            : diagnosticsDisplayExceededMemberMask != 0u
                                ? DiagnosticsErrorReasonDisplayLimitExceeded
                                : DiagnosticsErrorReasonNone;
                if (diagnosticsGroupErrorReason !=
                    DiagnosticsErrorReasonNone)
                {
                    if (diagnosticsGroupErrorReason ==
                        DiagnosticsErrorReasonNonFinite)
                    {
                        ++diagnosticsNonFiniteRejectedGroupCount;
                    }
                    else if (diagnosticsGroupErrorReason ==
                        DiagnosticsErrorReasonHdrLimitExceeded)
                    {
                        ++diagnosticsHdrRejectedGroupCount;
                    }
                    else
                    {
                        ++diagnosticsDisplayRejectedGroupCount;
                    }
                    diagnosticsErrorBudgetRejectedSampleCount +=
                        memberCount;

                    // Consume each member's exact Reference result and turn
                    // the rejected group into deterministic singleton output.
                    [loop]
                    for (uint sampleIndex = 0u;
                        sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
                        ++sampleIndex)
                    {
                        const uint sampleBit = 1u << sampleIndex;
                        if ((memberMask & sampleBit) == 0u)
                            continue;

                        const float3 referenceRadiance =
                            diagnosticsReferenceRadiances[sampleIndex];
                        finalLinearHdr +=
                            referenceRadiance * inverseSampleCount;
                        const uint finalGroup =
                            diagnosticsFinalGroupCount++;
                        diagnosticsRepresentativeMask |= sampleBit;
                        ++diagnosticsPerformedLightingEvaluations;
                        diagnosticsFinalGroups[sampleIndex] = finalGroup;
                        diagnosticsFinalRepresentatives[sampleIndex] =
                            sampleIndex;
                        diagnosticsRadiance[sampleIndex] +=
                            referenceRadiance;
                        diagnosticsFallbackReasons[sampleIndex] =
                            preparationQueueOverflow
                                ? CompactReasonWorkListOverflow
                                : (diagnosticsReferenceShadeValidMask &
                                        sampleBit) != 0u
                                    ? CompactReasonUnsafeSharing
                                    : CompactReasonReconstructionFailure;
                        diagnosticsErrorBudgetReasons[sampleIndex] =
                            diagnosticsGroupErrorReason;
                    }
                    continue;
                }
                }
#endif
                finalLinearHdr +=
                    representativeRadiance * groupWeight;
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
                const uint finalGroup = diagnosticsFinalGroupCount++;
                diagnosticsRepresentativeMask |= 1u << representative;
                ++diagnosticsPerformedLightingEvaluations;
                [loop]
                for (uint sampleIndex = 0u;
                    sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
                    ++sampleIndex)
                {
                    if ((memberMask & (1u << sampleIndex)) == 0u)
                        continue;
                    diagnosticsFinalGroups[sampleIndex] = finalGroup;
                    diagnosticsFinalRepresentatives[sampleIndex] =
                        representative;
                    diagnosticsRadiance[sampleIndex] +=
                        representativeRadiance;
                    diagnosticsFallbackReasons[sampleIndex] =
                        representativeShadeValid
                            ? preparationQueueOverflow
                                ? CompactReasonWorkListOverflow
                                : CompactReasonNone
                            : CompactReasonReconstructionFailure;
                    diagnosticsErrorBudgetReasons[sampleIndex] =
                        DiagnosticsErrorReasonNone;
                }
#endif
                continue;
            }

            // Local lights, shadows, screen-space visibility, view response,
            // and sharp specular remain per sample whenever complete-radiance
            // equality cannot be proven cheaply.
            [loop]
            for (uint sampleIndex = 0u;
                sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
                ++sampleIndex)
            {
                if ((memberMask & (1u << sampleIndex)) == 0u)
                    continue;
                float3 radiance;
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
                bool sampleRadianceFinite = true;
#endif
                const bool sampleShadeValid = CompactShadeLoadedSample(
                    int2(dispatchThreadId),
                    samples[sampleIndex].worldPosition,
                    preparedInvariant,
                    directionalVisibility,
                    radiance
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
                    , sampleRadianceFinite
#endif
                    );
                finalLinearHdr += radiance * inverseSampleCount;
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
                const uint finalGroup = diagnosticsFinalGroupCount++;
                diagnosticsRepresentativeMask |= 1u << sampleIndex;
                ++diagnosticsPerformedLightingEvaluations;
                diagnosticsFinalGroups[sampleIndex] = finalGroup;
                diagnosticsFinalRepresentatives[sampleIndex] = sampleIndex;
                diagnosticsRadiance[sampleIndex] += radiance;
                diagnosticsFallbackReasons[sampleIndex] =
                    preparationQueueOverflow
                        ? CompactReasonWorkListOverflow
                        : keyFallback
                            ? classes[sampleIndex] &
                                CompactVisibilityClassReasonMask
                            : !sampleShadeValid
                                ? CompactReasonReconstructionFailure
                                : memberCount > 1u ||
                                    countbits(exactKeyMask) > 1u
                                ? CompactReasonUnsafeSharing
                                : CompactReasonNone;
                // A singleton/per-sample group has no cross-sample
                // approximation to reject. Publish the same explicit
                // accepted error-budget state as the shared-group path so
                // the selected-pixel ABI never reports this field as
                // unavailable for a successfully shaded sample.
                diagnosticsErrorBudgetReasons[sampleIndex] =
                    DiagnosticsErrorReasonNone;
#endif
            }
        }
        assignedMask |= exactKeyMask;
    }
    if (assignedMask != coveredMask)
    {
        CompactFailLightingPixelLoaded(
            pixelPosition,
            CompactReasonReconstructionFailure,
            keys,
            classes,
            depths);
        return;
    }

#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
#if ADAPTIVE_MSAA_COMPACT_VISIBILITY
    if (g_PbrDeferred.visibilityDebugView == 0u &&
        g_PbrDeferred.lightingDebugView == 0u &&
        referenceVisibilityCoveredMask != 0u)
    {
        const float3 visibilityCorrection =
            t_VisibilityComposite[pixelPosition].rgb -
            t_VisibilityBaseLighting[pixelPosition].rgb;
        if (all(isfinite(visibilityCorrection)))
        {
            [loop]
            for (uint sampleIndex = 0u;
                sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
                ++sampleIndex)
            {
                if ((referenceVisibilityCoveredMask &
                        (1u << sampleIndex)) != 0u)
                    diagnosticsRadiance[sampleIndex] +=
                        visibilityCorrection;
            }
        }
    }
#endif

    DiagnosticsProducerStorePixelMetadataWithErrorBudget(
        pixelPosition,
        diagnosticsFinalGroupCount,
        diagnosticsRepresentativeMask,
        diagnosticsPerformedLightingEvaluations,
        diagnosticsNonFiniteRejectedGroupCount,
        diagnosticsHdrRejectedGroupCount,
        diagnosticsDisplayRejectedGroupCount,
        diagnosticsErrorBudgetRejectedSampleCount);
#if UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_SEPARATED
    CompactDiagnosticsStorePreparationWorkList(
        pixelPosition,
        diagnosticsPreparationWorkListPeakOccupancy,
        diagnosticsPreparationWorkListCapacity,
        diagnosticsPreparationWorkListOverflow);
#endif
    [loop]
    for (uint sampleIndex = 0u;
        sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
        ++sampleIndex)
    {
        const bool covered =
            (coveredMask & (1u << sampleIndex)) != 0u;
        DiagnosticsProducerStoreStatus(
            pixelPosition,
            sampleIndex,
            diagnosticsFallbackReasons[sampleIndex],
            diagnosticsErrorBudgetReasons[sampleIndex]);
        DiagnosticsGpuSampleInputRecord record =
            DiagnosticsProducerEmptySample();
        record.validFields = DiagnosticsSampleFallbackReason |
            (diagnosticsRadianceAvailable
                ? DiagnosticsSampleRadiance
                : 0u);
        record.radianceR = diagnosticsRadiance[sampleIndex].r;
        record.radianceG = diagnosticsRadiance[sampleIndex].g;
        record.radianceB = diagnosticsRadiance[sampleIndex].b;
        record.fallbackReason =
            diagnosticsFallbackReasons[sampleIndex];
        if (diagnosticsErrorBudgetReasons[sampleIndex] !=
            DiagnosticsUnavailableErrorReason)
        {
            record.validFields |=
                DiagnosticsSampleErrorBudgetReason;
            record.errorBudgetReason =
                diagnosticsErrorBudgetReasons[sampleIndex];
        }
        if (covered && diagnosticsSurfaces[sampleIndex].valid != 0u)
        {
            record.validFields |=
                DiagnosticsSampleReconstructedBarycentrics |
                DiagnosticsSampleFrontFaceValid;
            record.reconstructedBarycentricX =
                diagnosticsSurfaces[sampleIndex].hitBarycentrics.y;
            record.reconstructedBarycentricY =
                diagnosticsSurfaces[sampleIndex].hitBarycentrics.z;
            if (diagnosticsSurfaces[sampleIndex].frontFace != 0u)
                record.validFields |= DiagnosticsSampleFrontFace;
        }
        if (covered && diagnosticsFinalGroups[sampleIndex] !=
                DiagnosticsUnavailableWord)
        {
            record.validFields |= DiagnosticsSampleShadingGroup;
            record.shadingGroupIndex =
                diagnosticsFinalGroups[sampleIndex];
            record.representativeSampleIndex =
                diagnosticsFinalRepresentatives[sampleIndex];
        }
        DiagnosticsProducerStoreSelected(
            pixelPosition, sampleIndex, record);
    }
#endif

    CompactStoreLighting(
        pixelPosition,
        countbits(referenceVisibilityCoveredMask),
        inverseSampleCount,
        finalLinearHdr);
}
