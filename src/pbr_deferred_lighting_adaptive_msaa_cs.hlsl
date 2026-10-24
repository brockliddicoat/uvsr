// UVSR adaptive MSAA deferred lighting.
//
// Exact UVSR provenance: the reference-equivalent lighting body and binding
// topology are adapted from src/pbr_deferred_lighting_msaa_cs.hlsl at UVSR
// source commit 578496047c4753077e36f39a68df8c2c323cee58. That reference
// shader remains unchanged and is the fail-closed fallback/permutation.
//
// Primary algorithm source audit: The Forge Visibility_Buffer at commit
// a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b, tree
// e300153c60c611a05d878d22035c703a81724fb6 (Apache-2.0), especially
// Examples_3/Visibility_Buffer/src/Shaders/FSL/MsaaEdgeDetect.frag.fsl and
// VisibilityBufferShade.frag.fsl. UVSR retains its descriptor-once MSAA flow,
// but replaces its mean-depth classifier and transient 2/30-bit key because
// neither proves final surface ownership.
//
// Unique-grouping source audit: Christopher A. Burns and Warren A. Hunt,
// "The Visibility Buffer: A Cache-Friendly Approach to Deferred Shading,"
// official supplemental code at
// https://jcgt.org/published/0002/02/04/code.zip, SHA-256
// 6C9A2410ED37270B6CB5F8FED34CE5A330A229ECD426C16E573646FBBE0C76E2,
// DeferredPass.hlsl lines 424-508 (embedded BSD-style license). UVSR uses the
// ascending representative, group-count weighting, and dense invariant-work
// redistribution concepts, but rejects the source's
// same-surface-implies-same-radiance assumption. Required third-party notices
// are maintained centrally.

#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/utils.hlsli>
#include <donut/shaders/shadows.hlsli>
#include <donut/shaders/binding_helpers.hlsli>
#include "pbr_deferred_lighting_cb.h"
#include "adaptive_msaa_raster_cb.h"
#include "pbr_environment.hlsli"
#include "pbr_gbuffer.hlsli"
#include "pbr_lighting.hlsli"

#ifndef PBR_DEFERRED_MSAA_SAMPLES
#error PBR_DEFERRED_MSAA_SAMPLES must be a static 2, 4, 8, or 16.
#endif

#ifndef PBR_DEFERRED_MSAA_VISIBILITY
#define PBR_DEFERRED_MSAA_VISIBILITY 0
#endif

#ifndef UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
#define UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER 0
#endif

// Both adaptive implementations use the same CMake-selected A/B topology,
// but package distinct shader macros so either implementation can be audited
// independently in the production bundle. Selection is always explicit.
#define UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_INLINE 0
#define UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_SEPARATED 1
#ifndef UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY
#error UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY must be explicit
#endif
#if UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY != \
        UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_INLINE && \
    UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY != \
        UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_SEPARATED
#error UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY is invalid
#endif

#if PBR_DEFERRED_MSAA_SAMPLES != 2 && \
    PBR_DEFERRED_MSAA_SAMPLES != 4 && \
    PBR_DEFERRED_MSAA_SAMPLES != 8 && \
    PBR_DEFERRED_MSAA_SAMPLES != 16
#error Unsupported adaptive PBR deferred MSAA sample count.
#endif

// These values mirror the released CPU key ABI in adaptive_msaa.h. They are
// intentionally repeated here instead of creating a new CPU/HLSL contract in
// this isolated shader assignment.
static const uint UvsrAdaptiveGeometryInstanceSlotBits = 20u;
static const uint UvsrAdaptiveGeometryInstanceGenerationMask = 0xfffu;
static const uint UvsrAdaptiveMinimumGeneration = 1u;
static const uint UvsrAdaptiveMaximumGeneration = 0xffeu;
static const uint UvsrAdaptiveInvalidWord = 0xffffffffu;
static const uint UvsrAdaptiveClearedVisibilityWord =
    ADAPTIVE_MSAA_CLEARED_VISIBILITY_WORD;
static const uint UvsrAdaptiveUnassignedGroup = 0xffffffffu;
static const uint UvsrAdaptiveReasonWorkListOverflow = 21u;
static const float UvsrAdaptiveRelativeDepthTolerance =
    1.0f / 4096.0f;
static const float UvsrAdaptivePositionFootprintFraction = 1.0f / 8.0f;

cbuffer c_Deferred : register(b0)
{
    PbrDeferredLightingConstants g_PbrDeferred;
};

#define g_Deferred g_PbrDeferred.deferred

Texture2DArray t_ShadowMapArray : register(t0);
TextureCubeArray t_DiffuseEnvironment : register(t1);
TextureCubeArray t_SpecularEnvironment : register(t2);
Texture2D t_EnvironmentBrdf : register(t3);

SamplerComparisonState s_ShadowSamplerComparison : register(s1);
SamplerState s_DiffuseEnvironmentSampler : register(s2);
SamplerState s_EnvironmentBrdfSampler : register(s3);

Texture2DMS<float, PBR_DEFERRED_MSAA_SAMPLES>
    t_GBufferDepth : register(t8);
Texture2DMS<float4, PBR_DEFERRED_MSAA_SAMPLES>
    t_GBuffer0 : register(t9);
Texture2DMS<float4, PBR_DEFERRED_MSAA_SAMPLES>
    t_GBuffer1 : register(t10);
Texture2DMS<float4, PBR_DEFERRED_MSAA_SAMPLES>
    t_GBuffer2 : register(t11);
Texture2DMS<float4, PBR_DEFERRED_MSAA_SAMPLES>
    t_GBuffer3 : register(t12);
Texture2DMS<float, PBR_DEFERRED_MSAA_SAMPLES>
    t_MaterialAmbientOcclusion : register(t14);

// HdrColor contains the environment/IBL background and was resolved before
// this dispatch. Covered samples are still zero because the background depth
// test failed; the resolved value therefore already represents the exact
// uncovered-sample environment contribution divided by the sample count.
Texture2D t_ResolvedBackground : register(t17);
#if PBR_DEFERRED_MSAA_VISIBILITY
Texture2D t_VisibilityBaseLighting : register(t18);
Texture2D t_VisibilityComposite : register(t19);
#endif
Texture2D<float> t_DirectionalVisibility : register(t20);
Texture2DMS<uint2, PBR_DEFERRED_MSAA_SAMPLES>
    t_SurfaceOwnership : register(t21);

VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_Output : register(u0);

#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
#define UVSR_ADAPTIVE_MSAA_DIAGNOSTIC_SAMPLES PBR_DEFERRED_MSAA_SAMPLES
#include "adaptive_msaa_diagnostics_producer.hlsli"
Texture2DMS<float4, PBR_DEFERRED_MSAA_SAMPLES>
    t_DiagnosticsSurfaceHits : register(t26);
#endif

float GetRandom(float2 position)
{
    int x = int(position.x) & 3;
    int y = int(position.y) & 3;
    return g_Deferred.noisePattern[y][x];
}

float GetDirectionalLightVisibility(
    uint lightIndex,
    float directionalVisibility)
{
    if (int(lightIndex) ==
        g_PbrDeferred.directionalVisibilityLightIndex)
    {
        return saturate(directionalVisibility);
    }
    return 1.0f;
}

float EvaluateLightVisibility(
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

        float2 cascadeVisibility = EvaluateShadowPoisson(
            t_ShadowMapArray,
            s_ShadowSamplerComparison,
            g_Deferred.shadows[light.shadowCascades[cascade]],
            surfaceWorldPosition,
            sinCosRotation,
            3.0f);
        combinedCascadeVisibility = saturate(
            combinedCascadeVisibility +
            cascadeVisibility *
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

        float2 objectVisibility = EvaluateShadowPoisson(
            t_ShadowMapArray,
            s_ShadowSamplerComparison,
            g_Deferred.shadows[light.perObjectShadows[object]],
            surfaceWorldPosition,
            sinCosRotation,
            3.0f);
        visibility *= saturate(
            objectVisibility.x +
            (1.0f - objectVisibility.y));
        if (!(visibility > 0.0f))
            break;
    }

    return saturate(visibility);
}

bool IsValidAdaptiveSurfaceKey(uint2 key)
{
    if (key.x == UvsrAdaptiveInvalidWord ||
        key.y == UvsrAdaptiveInvalidWord)
    {
        return false;
    }

    const uint generation =
        (key.x >> UvsrAdaptiveGeometryInstanceSlotBits) &
        UvsrAdaptiveGeometryInstanceGenerationMask;
    return generation >= UvsrAdaptiveMinimumGeneration &&
        generation <= UvsrAdaptiveMaximumGeneration;
}

uint ValidateAdaptiveSurfaceKey(uint2 key)
{
    if (key.x == UvsrAdaptiveInvalidWord ||
        key.y == UvsrAdaptiveInvalidWord)
    {
        return 1u;
    }
    const uint generation =
        (key.x >> UvsrAdaptiveGeometryInstanceSlotBits) &
        UvsrAdaptiveGeometryInstanceGenerationMask;
    return generation >= UvsrAdaptiveMinimumGeneration &&
            generation <= UvsrAdaptiveMaximumGeneration
        ? 0u
        : 2u;
}

bool SameSurface(uint2 first, uint2 second)
{
    return IsValidAdaptiveSurfaceKey(first) &&
        IsValidAdaptiveSurfaceKey(second) &&
        all(first == second);
}

bool EqualFloatBits(float first, float second)
{
    return asuint(first) == asuint(second);
}

bool EqualFloat3Bits(float3 first, float3 second)
{
    return all(asuint(first) == asuint(second));
}

bool EqualFloat4Bits(float4 first, float4 second)
{
    return all(asuint(first) == asuint(second));
}

float ComputeLinearViewDepth(float3 worldPosition)
{
    const float4 viewPosition = mul(
        float4(worldPosition, 1.0f),
        g_Deferred.view.matWorldToView);
    return abs(viewPosition.z / viewPosition.w);
}

float ComputePixelFootprint(
    int2 pixelPosition,
    float deviceDepth,
    float3 worldPosition)
{
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    const float3 worldRight = ReconstructWorldPosition(
        g_Deferred.view,
        pixelCenter + float2(1.0f, 0.0f),
        deviceDepth);
    const float3 worldDown = ReconstructWorldPosition(
        g_Deferred.view,
        pixelCenter + float2(0.0f, 1.0f),
        deviceDepth);
    return min(
        length(worldRight - worldPosition),
        length(worldDown - worldPosition));
}

bool IsFiniteSampleDescriptor(
    uint2 key,
    float deviceDepth,
    float4 channel0,
    float4 channel1,
    float4 channel2,
    float4 channel3,
    float authoredAmbientOcclusion,
    float3 worldPosition,
    float linearDepth,
    float pixelFootprint)
{
    return IsValidAdaptiveSurfaceKey(key) &&
        isfinite(deviceDepth) &&
        all(isfinite(channel0)) &&
        all(isfinite(channel1)) &&
        all(isfinite(channel2)) &&
        all(isfinite(channel3)) &&
        isfinite(authoredAmbientOcclusion) &&
        all(isfinite(worldPosition)) &&
        isfinite(linearDepth) &&
        isfinite(pixelFootprint) &&
        pixelFootprint >= 0.0f;
}

bool CanShareShading(
    uint2 firstKey,
    uint2 secondKey,
    float firstLinearDepth,
    float secondLinearDepth,
    float3 firstWorldPosition,
    float3 secondWorldPosition,
    float firstPixelFootprint,
    float secondPixelFootprint,
    float4 firstChannel0,
    float4 secondChannel0,
    float4 firstChannel1,
    float4 secondChannel1,
    float4 firstChannel2,
    float4 secondChannel2,
    float4 firstChannel3,
    float4 secondChannel3,
    float firstAmbientOcclusion,
    float secondAmbientOcclusion,
    bool firstDescriptorValid,
    bool secondDescriptorValid)
{
    if (!firstDescriptorValid ||
        !secondDescriptorValid ||
        !SameSurface(firstKey, secondKey))
    {
        return false;
    }

    // Exact raw-channel equality is intentionally stronger than a decoded
    // epsilon. It proves identical material data and identical oriented
    // geometric/shading normals without making normals part of SurfaceKey.
    if (!EqualFloat4Bits(firstChannel0, secondChannel0) ||
        !EqualFloat4Bits(firstChannel1, secondChannel1) ||
        !EqualFloat4Bits(firstChannel2, secondChannel2) ||
        !EqualFloat4Bits(firstChannel3, secondChannel3) ||
        !EqualFloatBits(
            firstAmbientOcclusion,
            secondAmbientOcclusion))
    {
        return false;
    }

    const float depthDifference =
        abs(firstLinearDepth - secondLinearDepth);
    const float depthScale = max(
        abs(firstLinearDepth),
        abs(secondLinearDepth));
    if ((depthScale == 0.0f && depthDifference != 0.0f) ||
        (depthScale != 0.0f &&
            depthDifference / depthScale >
                UvsrAdaptiveRelativeDepthTolerance))
    {
        return false;
    }

    const float positionSpan = length(
        firstWorldPosition - secondWorldPosition);
    const float smallerFootprint = min(
        firstPixelFootprint,
        secondPixelFootprint);
    return positionSpan <=
        UvsrAdaptivePositionFootprintFraction * smallerFootprint;
}

// One pixel lane publishes at most one exact-key group at each statically
// bounded group ordinal. The separated topology wave-compacts only G-buffer
// decode and material/BRDF preparation. Prepared state returns to its origin
// lane before position-, view-, visibility-, shadow-, attenuation-, specular-,
// or radiance-dependent work; no final radiance crosses a sample or pixel.
struct AdaptiveInvariantPreparationRecord
{
    float4 channel0;
    float4 channel1;
    float4 channel2;
    float4 channel3;
    float ambientOcclusion;
};

struct AdaptivePreparedInvariant
{
    PbrGBufferData gbuffer;
    PbrPreparedMaterial preparedMaterial;
};

// Shader Model 6 wave ballots expose 128 bits. Every lane contributes at most
// one mutually exclusive simple/complex record per fixed group ordinal, so
// this is the exact mathematical maximum for every supported sample count;
// overflow and telemetry use the smaller active live-wave capacity.
static const uint AdaptivePreparationMaximumQueueCapacity = 128u;

uint AdaptiveFindNthBallotLane(
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

    return 0u;
}

AdaptiveInvariantPreparationRecord AdaptiveWaveReadPreparationRecord(
    AdaptiveInvariantPreparationRecord record,
    uint sourceLane)
{
    AdaptiveInvariantPreparationRecord result;
    result.channel0 = WaveReadLaneAt(record.channel0, sourceLane);
    result.channel1 = WaveReadLaneAt(record.channel1, sourceLane);
    result.channel2 = WaveReadLaneAt(record.channel2, sourceLane);
    result.channel3 = WaveReadLaneAt(record.channel3, sourceLane);
    result.ambientOcclusion = WaveReadLaneAt(
        record.ambientOcclusion, sourceLane);
    return result;
}

AdaptivePreparedInvariant AdaptivePrepareInvariant(
    AdaptiveInvariantPreparationRecord record)
{
    AdaptivePreparedInvariant result = (AdaptivePreparedInvariant)0;
    float4 channels[4];
    channels[0] = record.channel0;
    channels[1] = record.channel1;
    channels[2] = record.channel2;
    channels[3] = record.channel3;
    result.gbuffer = DecodePbrGBuffer(
        channels, record.ambientOcclusion);
    result.preparedMaterial = PreparePbrMaterial(
        result.gbuffer.material);
    return result;
}

AdaptivePreparedInvariant AdaptiveWaveReadPreparedInvariant(
    AdaptivePreparedInvariant prepared,
    uint sourceLane)
{
    AdaptivePreparedInvariant result = (AdaptivePreparedInvariant)0;
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
    return result;
}

bool AdaptivePrepareWaveQueue(
    bool queueActive,
    AdaptiveInvariantPreparationRecord record,
    out AdaptivePreparedInvariant prepared,
    out uint queueOccupancy,
    out uint queueCapacity,
    out bool queueOverflow)
{
    prepared = (AdaptivePreparedInvariant)0;
    queueOccupancy = WaveActiveCountBits(queueActive);
    const uint liveLaneCount = WaveActiveCountBits(true);
    queueCapacity = min(
        AdaptivePreparationMaximumQueueCapacity,
        liveLaneCount);
    queueOverflow =
        liveLaneCount > AdaptivePreparationMaximumQueueCapacity ||
        queueOccupancy > queueCapacity;
    if (queueOccupancy == 0u || queueOverflow)
        return false;

    const uint4 executionBallot = WaveActiveBallot(true);
    const uint executionOrdinal = WavePrefixCountBits(true);
    const uint4 recordBallot = WaveActiveBallot(queueActive);

    // Every live lane keeps the source records active for every lane read.
    // Only the dense live-lane prefix performs invariant preparation.
    const uint workerRecordOrdinal = min(
        executionOrdinal, queueOccupancy - 1u);
    const uint sourceLane = AdaptiveFindNthBallotLane(
        recordBallot, workerRecordOrdinal);
    const AdaptiveInvariantPreparationRecord workerRecord =
        AdaptiveWaveReadPreparationRecord(record, sourceLane);

    AdaptivePreparedInvariant workerResult =
        (AdaptivePreparedInvariant)0;
    if (executionOrdinal < queueOccupancy)
        workerResult = AdaptivePrepareInvariant(workerRecord);

    const uint recordOrdinal = WavePrefixCountBits(queueActive);
    const uint workerLane = AdaptiveFindNthBallotLane(
        executionBallot, min(recordOrdinal, queueOccupancy - 1u));
    prepared = AdaptiveWaveReadPreparedInvariant(
        workerResult, workerLane);
    return queueActive;
}

bool AdaptiveDepthClassifiesSimpleGroup(
    uint exactGroupMask,
    float sampleLinearDepths[PBR_DEFERRED_MSAA_SAMPLES])
{
    const uint fullSampleMask =
        (1u << PBR_DEFERRED_MSAA_SAMPLES) - 1u;
    if (exactGroupMask != fullSampleMask)
        return false;

    // This is deliberately stricter than The Forge's mean-depth edge test:
    // simple scheduling requires one fully covered exact group and a bounded
    // finite linear-depth range. It changes scheduling only, never identity,
    // coherence, shading, or resolve decisions.
    float minimumDepth = 3.402823466e+38f;
    float maximumDepth = -3.402823466e+38f;
    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
        ++sampleIndex)
    {
        const float linearDepth = sampleLinearDepths[sampleIndex];
        if (!isfinite(linearDepth))
            return false;
        minimumDepth = min(minimumDepth, linearDepth);
        maximumDepth = max(maximumDepth, linearDepth);
    }
    const float depthScale = max(
        abs(minimumDepth), abs(maximumDepth));
    return depthScale > 0.0f &&
        (maximumDepth - minimumDepth) / depthScale <=
            UvsrAdaptiveRelativeDepthTolerance;
}

bool AdaptivePrepareInlineRecord(
    bool recordActive,
    AdaptiveInvariantPreparationRecord record,
    out AdaptivePreparedInvariant prepared,
    out uint queueOccupancy,
    out uint queueCapacity,
    out bool queueOverflow)
{
    prepared = (AdaptivePreparedInvariant)0;
    queueOccupancy = 0u;
    queueCapacity = 0u;
    queueOverflow = false;
    if (!recordActive)
        return false;
    prepared = AdaptivePrepareInvariant(record);
    return true;
}

bool AdaptivePrepareSeparatedWaveRecord(
    bool recordActive,
    bool depthClassifiedSimple,
    AdaptiveInvariantPreparationRecord record,
    out AdaptivePreparedInvariant prepared,
    out uint queueOccupancy,
    out uint queueCapacity,
    out bool queueOverflow)
{
    const bool simpleRecordActive =
        recordActive && depthClassifiedSimple;
    const bool complexRecordActive =
        recordActive && !depthClassifiedSimple;

    AdaptivePreparedInvariant simplePrepared;
    uint simpleOccupancy;
    uint simpleCapacity;
    bool simpleOverflow;
    const bool simplePreparedActive = AdaptivePrepareWaveQueue(
        simpleRecordActive,
        record,
        simplePrepared,
        simpleOccupancy,
        simpleCapacity,
        simpleOverflow);

    AdaptivePreparedInvariant complexPrepared;
    uint complexOccupancy;
    uint complexCapacity;
    bool complexOverflow;
    const bool complexPreparedActive = AdaptivePrepareWaveQueue(
        complexRecordActive,
        record,
        complexPrepared,
        complexOccupancy,
        complexCapacity,
        complexOverflow);

    const uint recordOccupancy = WaveActiveCountBits(recordActive);
    const uint classifiedOccupancy =
        simpleOccupancy + complexOccupancy;
    queueOccupancy = max(simpleOccupancy, complexOccupancy);
    queueCapacity = min(simpleCapacity, complexCapacity);
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

bool AdaptivePrepareInvariantTopology(
    bool recordActive,
    bool depthClassifiedSimple,
    AdaptiveInvariantPreparationRecord record,
    out AdaptivePreparedInvariant prepared,
    out uint queueOccupancy,
    out uint queueCapacity,
    out bool queueOverflow,
    out bool inlineFallback)
{
    inlineFallback = false;
#if UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_INLINE
    return AdaptivePrepareInlineRecord(
        recordActive,
        record,
        prepared,
        queueOccupancy,
        queueCapacity,
        queueOverflow);
#elif UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_SEPARATED
    const bool preparedActive = AdaptivePrepareSeparatedWaveRecord(
        recordActive,
        depthClassifiedSimple,
        record,
        prepared,
        queueOccupancy,
        queueCapacity,
        queueOverflow);
    // A capacity or accounting fault is local to this record. Recompute the
    // invariant inline and keep every sample on its Reference-equivalent
    // lighting path; diagnostics retain the work-list failure reason.
    inlineFallback = recordActive &&
        (queueOverflow || !preparedActive);
    if (inlineFallback)
    {
        queueOverflow = true;
        prepared = AdaptivePrepareInvariant(record);
        return true;
    }
    return preparedActive;
#endif
}

bool CanShareCompleteRadiance(
    float firstDeviceDepth,
    float secondDeviceDepth,
    float3 firstWorldPosition,
    float3 secondWorldPosition)
{
    // This proof is intentionally independent from preparation scheduling.
    // Complete radiance may be reused only when every sample-dependent input
    // visible to this shader is bit-identical; otherwise the full lighting
    // body remains per sample even when invariant material work was shared.
    return EqualFloatBits(firstDeviceDepth, secondDeviceDepth) &&
        EqualFloat3Bits(firstWorldPosition, secondWorldPosition);
}

bool ShadeLoadedDeferredSample(
    float4 normalChannels,
    float3 surfaceWorldPosition,
    PbrGBufferData gbuffer,
    PbrPreparedMaterial preparedMaterial,
    float directionalVisibility,
    float2 shadowSinCosRotation,
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
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
        return false;

    float3 viewIncident = GetIncidentVector(
        g_Deferred.view.cameraDirectionOrPosition,
        surfaceWorldPosition);
    PbrSurfaceInteraction surface;
    surface.position = surfaceWorldPosition;
    surface.shadingNormal = gbuffer.shadingNormal;
    surface.geometricNormal = gbuffer.geometricNormal;
    surface.viewDirection = -viewIncident;
    PbrPreparedSurface preparedSurface =
        PreparePbrSurface(surface);

    LightProbeConstants environmentProbe =
        (LightProbeConstants)0;
    if (g_Deferred.numLightProbes > 0u)
        environmentProbe = g_Deferred.lightProbes[0];

    const bool needDiffuseEnvironment =
        g_PbrDeferred.separateIndirect == 0 ||
        g_PbrDeferred.lightingDebugView == 4u ||
        g_PbrDeferred.lightingDebugView == 9u;
    const bool needSpecularEnvironment =
        g_PbrDeferred.separateIndirect == 0 ||
        (g_PbrDeferred.lightingDebugView >= 6u &&
            g_PbrDeferred.lightingDebugView <= 9u);
    const bool needPreparedEnvironment =
        needDiffuseEnvironment ||
        needSpecularEnvironment ||
        g_PbrDeferred.lightingDebugView == 10u ||
        g_PbrDeferred.lightingDebugView == 11u;
    PbrPreparedEnvironment preparedEnvironment =
        (PbrPreparedEnvironment)0;
    if (needPreparedEnvironment)
    {
        preparedEnvironment = PreparePbrEnvironment(
            gbuffer.material,
            surface,
            environmentProbe.mipLevels);
    }

    float3 environmentDiffuseResponse = 0.0f;
    float3 prefilteredEnvironment = 0.0f;
    float2 environmentBrdf = 0.0f;
    float3 environmentDiffuse = 0.0f;
    float3 environmentSpecular = 0.0f;
    if (g_Deferred.numLightProbes > 0u)
    {
        if (needDiffuseEnvironment &&
            environmentProbe.diffuseScale > 0.0f)
        {
            environmentDiffuseResponse =
                t_DiffuseEnvironment.SampleLevel(
                    s_DiffuseEnvironmentSampler,
                    float4(
                        preparedSurface.shadingNormal,
                        environmentProbe.diffuseArrayIndex),
                    0.0f).rgb *
                environmentProbe.diffuseScale;
            if (any(!isfinite(environmentDiffuseResponse)))
                environmentDiffuseResponse = 0.0f;
            environmentDiffuse = EvaluatePbrEnvironmentDiffuse(
                preparedEnvironment,
                environmentDiffuseResponse,
                gbuffer.ambientOcclusion);
        }

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
        {
            debugColor = gbuffer.shadingNormal * 0.5f + 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView == 2u)
        {
            debugColor = gbuffer.geometricNormal * 0.5f + 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView == 3u)
        {
            float angularDifference = saturate(
                1.0f - dot(
                    gbuffer.shadingNormal,
                    gbuffer.geometricNormal));
            debugColor = float3(
                angularDifference,
                0.0f,
                1.0f - angularDifference);
        }
        else if (g_PbrDeferred.lightingDebugView == 4u)
        {
            debugColor = environmentDiffuseResponse;
        }
        else if (g_PbrDeferred.lightingDebugView == 5u)
        {
            float cardinalResponse =
                1.0f + 0.5f * gbuffer.shadingNormal.x;
            debugColor = cardinalResponse * 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView == 6u)
        {
            debugColor = prefilteredEnvironment;
        }
        else if (g_PbrDeferred.lightingDebugView == 7u)
        {
            debugColor = float3(
                environmentBrdf.x,
                environmentBrdf.y,
                0.0f);
        }
        else if (g_PbrDeferred.lightingDebugView == 8u)
        {
            debugColor = environmentSpecular;
        }
        else if (g_PbrDeferred.lightingDebugView == 9u)
        {
            debugColor =
                environmentDiffuse + environmentSpecular;
        }
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
        [loop]
        for (uint lightIndex = 0;
            lightIndex < g_Deferred.numLights;
            ++lightIndex)
        {
            LightConstants light =
                g_Deferred.lights[lightIndex];
            float visibility = GetDirectionalLightVisibility(
                lightIndex,
                directionalVisibility);
            if (!(visibility > 0.0f))
                continue;

            PbrLightSample lightSample = SamplePbrLight(
                light,
                surfaceWorldPosition,
                1.0f);
            if (!HasPositiveFinitePbrSignal(
                    lightSample.incidentRadiance,
                    1.0f))
            {
                continue;
            }
            if (!CanEvaluatePbrDirectSurfacePrepared(
                    preparedSurface,
                    lightSample.directionToLight))
            {
                continue;
            }

            visibility = EvaluateLightVisibility(
                light,
                surfaceWorldPosition,
                shadowSinCosRotation,
                visibility);
            if (!(visibility > 0.0f))
                continue;
            lightSample.visibility = visibility;
            PbrDirectLighting direct =
                EvaluateDirectLightPrevalidated(
                    preparedMaterial,
                    preparedSurface,
                    lightSample);
            directDiffuse += direct.diffuse;
            directSpecular += direct.specular;
        }
    }

    float3 diffuse = directDiffuse +
        (g_PbrDeferred.separateIndirect != 0
            ? 0.0f
            : environmentDiffuse);
    float3 specular = directSpecular +
        (g_PbrDeferred.separateIndirect != 0
            ? 0.0f
            : environmentSpecular);
    finalLinearHdr = max(
        diffuse +
            specular +
            gbuffer.material.emissive,
        0.0f);
    if (any(isnan(finalLinearHdr)) ||
        any(isinf(finalLinearHdr)))
    {
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
        diagnosticsRadianceFinite = false;
#endif
        finalLinearHdr = 0.0f;
        return false;
    }
    return true;
}

void StoreAdaptiveDeferredLighting(
    int2 pixelPosition,
    uint visibilityCoveredSampleCount,
    float inverseSampleCount,
    float3 finalLinearHdr)
{
#if PBR_DEFERRED_MSAA_VISIBILITY
    if (g_PbrDeferred.visibilityDebugView != 0u)
    {
        finalLinearHdr = t_VisibilityComposite[pixelPosition].rgb;
    }
    else if (g_PbrDeferred.lightingDebugView == 0u &&
        visibilityCoveredSampleCount != 0u)
    {
        // Visibility evaluates the closest coherent covered surface once.
        // Apply only its signed lighting correction and scale it by raster
        // coverage; uncovered samples retain their resolved sky contribution.
        const float coverage =
            float(visibilityCoveredSampleCount) * inverseSampleCount;
        const float3 visibilityCorrection =
            t_VisibilityComposite[pixelPosition].rgb -
            t_VisibilityBaseLighting[pixelPosition].rgb;
        if (all(isfinite(visibilityCorrection)))
        {
            finalLinearHdr +=
                visibilityCorrection * coverage;
        }
    }
#endif

    if (any(isnan(finalLinearHdr)) ||
        any(isinf(finalLinearHdr)))
    {
        finalLinearHdr = 0.0f;
    }
    u_Output[pixelPosition] = float4(
        min(max(finalLinearHdr, 0.0f), 65504.0f),
        0.0f);
}

#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER && \
    UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_SEPARATED
void AdaptiveDiagnosticsStorePreparationWorkList(
    int2 pixelPosition,
    uint peakOccupancy,
    uint observedCapacity,
    bool overflowed)
{
    // Pixel metadata is the existing diagnostics producer ABI's work-list
    // carrier. One active lane records each wave observation after ordinary
    // metadata is written. The renderer advertises this field only when the
    // separated G-buffer permutation and matching capacity are selected.
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

[numthreads(16, 16, 1)]
void main(int2 i_globalIdx : SV_DispatchThreadID)
{
    if (any(i_globalIdx.xy >=
        int2(g_Deferred.view.viewportSize)))
    {
        return;
    }

    const int2 pixelPosition =
        i_globalIdx.xy +
        int2(g_Deferred.view.viewportOrigin);
    const float inverseSampleCount =
        1.0f / float(PBR_DEFERRED_MSAA_SAMPLES);

    // The exact final key is the only ownership/coverage authority. A valid
    // depth-tested surface with a zero or degenerate normal still owns its
    // sample and must execute the Reference-equivalent zero-shading path;
    // material channels and SV_Coverage never invent or erase identity.
    // Load key, depth, and the normal channel once per sample so an
    // all-background pixel can finish before touching the remaining material
    // descriptors or directional visibility.
    uint2 sampleKeys[PBR_DEFERRED_MSAA_SAMPLES];
    float sampleDeviceDepths[PBR_DEFERRED_MSAA_SAMPLES];
    float4 sampleChannel2[PBR_DEFERRED_MSAA_SAMPLES];
    uint coveredSampleMask = 0u;
    uint referenceVisibilityCoveredMask = 0u;
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
    uint diagnosticsFinalGroups[PBR_DEFERRED_MSAA_SAMPLES];
    uint diagnosticsFinalRepresentatives[PBR_DEFERRED_MSAA_SAMPLES];
    uint diagnosticsFallbackReasons[PBR_DEFERRED_MSAA_SAMPLES];
    uint diagnosticsErrorBudgetReasons[PBR_DEFERRED_MSAA_SAMPLES];
    float3 diagnosticsRadiance[PBR_DEFERRED_MSAA_SAMPLES];
    float4 diagnosticsSurfaceHits[PBR_DEFERRED_MSAA_SAMPLES];
    const bool diagnosticsRadianceAvailable =
        g_PbrDeferred.lightingDebugView == 0u &&
        g_PbrDeferred.visibilityDebugView == 0u;
#endif
    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
        ++sampleIndex)
    {
        sampleKeys[sampleIndex] =
            t_SurfaceOwnership.Load(pixelPosition, sampleIndex);
        sampleDeviceDepths[sampleIndex] =
            t_GBufferDepth.Load(pixelPosition, sampleIndex);
        sampleChannel2[sampleIndex] =
            t_GBuffer2.Load(pixelPosition, sampleIndex);
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
        diagnosticsFinalGroups[sampleIndex] =
            DiagnosticsUnavailableWord;
        diagnosticsFinalRepresentatives[sampleIndex] =
            DiagnosticsUnavailableWord;
        diagnosticsFallbackReasons[sampleIndex] = 0u;
        diagnosticsErrorBudgetReasons[sampleIndex] =
            DiagnosticsUnavailableErrorReason;
        diagnosticsRadiance[sampleIndex] = max(
            t_DiagnosticsMsaaBackground.Load(
                pixelPosition, sampleIndex).rgb,
            0.0f);
        diagnosticsSurfaceHits[sampleIndex] =
            t_DiagnosticsSurfaceHits.Load(pixelPosition, sampleIndex);
#endif
        const bool clearedKey = all(
            sampleKeys[sampleIndex] ==
                UvsrAdaptiveClearedVisibilityWord);
        const bool background = clearedKey &&
            isfinite(sampleDeviceDepths[sampleIndex]) &&
            asuint(sampleDeviceDepths[sampleIndex]) == 0u;
        if (!background)
        {
            coveredSampleMask |= 1u << sampleIndex;
        }
        // The unchanged Reference shader applies the optional resolved
        // visibility correction only when ShadeDeferredSample accepts the
        // stored normal. Preserve that compatibility weight separately from
        // exact ownership; it never classifies background or identity.
        if (!background && dot(
                sampleChannel2[sampleIndex].xyz,
                sampleChannel2[sampleIndex].xyz) > 1e-12f)
        {
            referenceVisibilityCoveredMask |= 1u << sampleIndex;
        }
    }

    float3 finalLinearHdr =
        g_PbrDeferred.lightingDebugView == 0u
            ? max(t_ResolvedBackground[pixelPosition].rgb, 0.0f)
            : 0.0f;
    if (coveredSampleMask == 0u)
    {
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
        DiagnosticsProducerStorePixelMetadataWithErrorBudget(
            pixelPosition,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u);
        [unroll]
        for (uint sampleIndex = 0u;
            sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
            ++sampleIndex)
        {
            DiagnosticsProducerStoreStatus(
                pixelPosition,
                sampleIndex,
                0u,
                DiagnosticsUnavailableErrorReason);
            DiagnosticsGpuSampleInputRecord record =
                DiagnosticsProducerEmptySample();
            record.validFields = DiagnosticsSampleFallbackReason |
                (diagnosticsRadianceAvailable
                    ? DiagnosticsSampleRadiance
                    : 0u);
            record.radianceR = diagnosticsRadiance[sampleIndex].r;
            record.radianceG = diagnosticsRadiance[sampleIndex].g;
            record.radianceB = diagnosticsRadiance[sampleIndex].b;
            record.fallbackReason = 0u;
            DiagnosticsProducerStoreSelected(
                pixelPosition, sampleIndex, record);
        }
#endif
        StoreAdaptiveDeferredLighting(
            pixelPosition,
            countbits(referenceVisibilityCoveredMask),
            inverseSampleCount,
            finalLinearHdr);
        return;
    }

    const float2 pixelCenter =
        float2(pixelPosition) + 0.5f;

    // Every remaining descriptor is fetched exactly once for each covered
    // sample. All grouping, fallback, and shading reads below use this stable
    // bounded snapshot. Exact sentinel equality established background above;
    // key validity only decides whether a covered sample may share work.
    float4 sampleChannel0[PBR_DEFERRED_MSAA_SAMPLES];
    float4 sampleChannel1[PBR_DEFERRED_MSAA_SAMPLES];
    float4 sampleChannel3[PBR_DEFERRED_MSAA_SAMPLES];
    float sampleAmbientOcclusion[PBR_DEFERRED_MSAA_SAMPLES];
    float3 sampleWorldPositions[PBR_DEFERRED_MSAA_SAMPLES];
    float sampleLinearDepths[PBR_DEFERRED_MSAA_SAMPLES];
    float samplePixelFootprints[PBR_DEFERRED_MSAA_SAMPLES];
    uint descriptorValidSampleMask = 0u;

    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
        ++sampleIndex)
    {
        const uint sampleBit = 1u << sampleIndex;
        if ((coveredSampleMask & sampleBit) == 0u)
            continue;

        sampleChannel0[sampleIndex] =
            t_GBuffer0.Load(pixelPosition, sampleIndex);
        sampleChannel1[sampleIndex] =
            t_GBuffer1.Load(pixelPosition, sampleIndex);
        sampleChannel3[sampleIndex] =
            t_GBuffer3.Load(pixelPosition, sampleIndex);
        sampleAmbientOcclusion[sampleIndex] =
            t_MaterialAmbientOcclusion.Load(
                pixelPosition,
                sampleIndex);
        sampleWorldPositions[sampleIndex] =
            ReconstructWorldPosition(
                g_Deferred.view,
                pixelCenter,
                sampleDeviceDepths[sampleIndex]);
        sampleLinearDepths[sampleIndex] =
            ComputeLinearViewDepth(
                sampleWorldPositions[sampleIndex]);
        samplePixelFootprints[sampleIndex] =
            ComputePixelFootprint(
                pixelPosition,
                sampleDeviceDepths[sampleIndex],
                sampleWorldPositions[sampleIndex]);
        if (IsFiniteSampleDescriptor(
                sampleKeys[sampleIndex],
                sampleDeviceDepths[sampleIndex],
                sampleChannel0[sampleIndex],
                sampleChannel1[sampleIndex],
                sampleChannel2[sampleIndex],
                sampleChannel3[sampleIndex],
                sampleAmbientOcclusion[sampleIndex],
                sampleWorldPositions[sampleIndex],
                sampleLinearDepths[sampleIndex],
                samplePixelFootprints[sampleIndex]))
        {
            descriptorValidSampleMask |= sampleBit;
        }
    }

    uint groupRepresentatives[PBR_DEFERRED_MSAA_SAMPLES];
    uint groupSampleMasks[PBR_DEFERRED_MSAA_SAMPLES];
    uint groupCount = 0u;
    uint referenceFallbackGroupMask = 0u;
    uint assignedSampleMask = 0u;

    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
        ++sampleIndex)
    {
        const uint sampleBit = 1u << sampleIndex;
        if ((coveredSampleMask & sampleBit) == 0u)
            continue;

        uint matchingGroup = UvsrAdaptiveUnassignedGroup;
        if ((descriptorValidSampleMask & sampleBit) != 0u)
        {
            [loop]
            for (uint groupIndex = 0u;
                groupIndex < groupCount;
                ++groupIndex)
            {
                const uint representative =
                    groupRepresentatives[groupIndex];
                if ((referenceFallbackGroupMask &
                        (1u << groupIndex)) == 0u &&
                    SameSurface(
                        sampleKeys[representative],
                        sampleKeys[sampleIndex]))
                {
                    matchingGroup = groupIndex;
                    break;
                }
            }
        }

        bool pairwiseSafe =
            matchingGroup != UvsrAdaptiveUnassignedGroup;
        if (pairwiseSafe)
        {
            const uint memberMask =
                groupSampleMasks[matchingGroup];
            [unroll]
            for (uint memberIndex = 0u;
                memberIndex < PBR_DEFERRED_MSAA_SAMPLES;
                ++memberIndex)
            {
                if (memberIndex >= sampleIndex ||
                    (memberMask & (1u << memberIndex)) == 0u)
                {
                    continue;
                }

                if (!CanShareShading(
                        sampleKeys[memberIndex],
                        sampleKeys[sampleIndex],
                        sampleLinearDepths[memberIndex],
                        sampleLinearDepths[sampleIndex],
                        sampleWorldPositions[memberIndex],
                        sampleWorldPositions[sampleIndex],
                        samplePixelFootprints[memberIndex],
                        samplePixelFootprints[sampleIndex],
                        sampleChannel0[memberIndex],
                        sampleChannel0[sampleIndex],
                        sampleChannel1[memberIndex],
                        sampleChannel1[sampleIndex],
                        sampleChannel2[memberIndex],
                        sampleChannel2[sampleIndex],
                        sampleChannel3[memberIndex],
                        sampleChannel3[sampleIndex],
                        sampleAmbientOcclusion[memberIndex],
                        sampleAmbientOcclusion[sampleIndex],
                        (descriptorValidSampleMask &
                            (1u << memberIndex)) != 0u,
                        (descriptorValidSampleMask & sampleBit) != 0u))
                {
                    pairwiseSafe = false;
                    break;
                }
            }
        }

        if (pairwiseSafe)
        {
            groupSampleMasks[matchingGroup] |= sampleBit;
        }
        else
        {
            // Invalid descriptors and an otherwise valid same-key sample that
            // fails pairwise coherence both become complete-reference
            // singletons. They can never merge with another fallback record.
            const uint newGroup = groupCount;
            ++groupCount;
            groupRepresentatives[newGroup] = sampleIndex;
            groupSampleMasks[newGroup] = sampleBit;
            if ((descriptorValidSampleMask & sampleBit) == 0u ||
                matchingGroup != UvsrAdaptiveUnassignedGroup)
            {
                referenceFallbackGroupMask |= 1u << newGroup;
            }
        }

        assignedSampleMask |= sampleBit;
    }

    // A bounded inline implementation cannot overflow under supported sample
    // counts. Keep an executable fail-closed guard so future edits cannot turn
    // an accounting defect into lost coverage or cross-sample reconstruction.
    if (groupCount > PBR_DEFERRED_MSAA_SAMPLES ||
        assignedSampleMask != coveredSampleMask)
    {
        groupCount = 0u;
        referenceFallbackGroupMask = 0u;
        assignedSampleMask = 0u;
        [unroll]
        for (uint sampleIndex = 0u;
            sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
            ++sampleIndex)
        {
            const uint sampleBit = 1u << sampleIndex;
            if ((coveredSampleMask & sampleBit) == 0u)
                continue;

            const uint newGroup = groupCount;
            ++groupCount;
            groupRepresentatives[newGroup] = sampleIndex;
            groupSampleMasks[newGroup] = sampleBit;
            referenceFallbackGroupMask |= 1u << newGroup;
            assignedSampleMask |= sampleBit;
        }
    }

    float directionalVisibility = 1.0f;
    if (g_PbrDeferred.lightingDebugView == 0u &&
        g_PbrDeferred.directionalVisibilityLightIndex >= 0 &&
        uint(g_PbrDeferred.directionalVisibilityLightIndex) <
            g_Deferred.numLights)
    {
        directionalVisibility =
            t_DirectionalVisibility[pixelPosition];
    }

    float2 shadowSinCosRotation = 0.0f;
    if (g_PbrDeferred.lightingDebugView == 0u &&
        g_Deferred.numLights > 0u &&
        referenceVisibilityCoveredMask != 0u)
    {
        const float shadowRotationAngle = GetRandom(
            i_globalIdx +
            float2(g_Deferred.randomOffset.x, 0.0f));
        shadowSinCosRotation = float2(
            sin(shadowRotationAngle),
            cos(shadowRotationAngle));
    }

#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
    uint diagnosticsFinalGroupCount = 0u;
    uint diagnosticsRepresentativeMask = 0u;
    uint diagnosticsPerformedLightingEvaluations = 0u;
    uint diagnosticsNonFiniteRejectedGroupCount = 0u;
    uint diagnosticsHdrRejectedGroupCount = 0u;
    uint diagnosticsDisplayRejectedGroupCount = 0u;
    uint diagnosticsErrorBudgetRejectedSampleCount = 0u;
#if UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_SEPARATED
    uint diagnosticsPreparationWorkListPeakOccupancy = 0u;
    uint diagnosticsPreparationWorkListCapacity =
        AdaptivePreparationMaximumQueueCapacity;
    bool diagnosticsPreparationWorkListOverflow = false;
#endif
    float3 diagnosticsVisibilityCorrection = 0.0f;
    float3 diagnosticsVisibilityOverride = 0.0f;
    bool diagnosticsVisibilityOverridesLighting = false;
#if PBR_DEFERRED_MSAA_VISIBILITY
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

    // Every live lane executes every fixed group ordinal so separated wave
    // operations remain uniform even when neighboring pixels contain a
    // different number of exact-key groups.
    [loop]
    for (uint groupOrdinal = 0u;
        groupOrdinal < PBR_DEFERRED_MSAA_SAMPLES;
        ++groupOrdinal)
    {
        const bool preparationRecordActive = groupOrdinal < groupCount;
        uint representative = 0u;
        uint memberMask = 0u;
        AdaptiveInvariantPreparationRecord preparationRecord =
            (AdaptiveInvariantPreparationRecord)0;
        if (preparationRecordActive)
        {
            representative = groupRepresentatives[groupOrdinal];
            memberMask = groupSampleMasks[groupOrdinal];
            preparationRecord.channel0 =
                sampleChannel0[representative];
            preparationRecord.channel1 =
                sampleChannel1[representative];
            preparationRecord.channel2 =
                sampleChannel2[representative];
            preparationRecord.channel3 =
                sampleChannel3[representative];
            preparationRecord.ambientOcclusion =
                sampleAmbientOcclusion[representative];
        }

        bool depthClassifiedSimple = false;
#if UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_SEPARATED
        if (preparationRecordActive)
        {
            depthClassifiedSimple = AdaptiveDepthClassifiesSimpleGroup(
                memberMask,
                sampleLinearDepths);
        }
#endif
        AdaptivePreparedInvariant preparedInvariant;
        uint preparationQueueOccupancy;
        uint preparationQueueCapacity;
        bool preparationQueueOverflow;
        bool preparationInlineFallback;
        bool preparedRecordActive =
            AdaptivePrepareInvariantTopology(
                preparationRecordActive,
                depthClassifiedSimple,
                preparationRecord,
                preparedInvariant,
                preparationQueueOccupancy,
                preparationQueueCapacity,
                preparationQueueOverflow,
                preparationInlineFallback);
        if (preparationRecordActive && !preparedRecordActive)
        {
            preparedInvariant =
                AdaptivePrepareInvariant(preparationRecord);
            preparedRecordActive = true;
            preparationQueueOverflow = true;
            preparationInlineFallback = true;
        }
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER && \
    UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_SEPARATED
        diagnosticsPreparationWorkListPeakOccupancy = max(
            diagnosticsPreparationWorkListPeakOccupancy,
            preparationQueueOccupancy);
        diagnosticsPreparationWorkListCapacity = min(
            diagnosticsPreparationWorkListCapacity,
            preparationQueueCapacity);
        diagnosticsPreparationWorkListOverflow =
            diagnosticsPreparationWorkListOverflow ||
            preparationQueueOverflow;
#endif
        if (!preparationRecordActive)
            continue;

        const uint groupIndex = groupOrdinal;
        const uint groupSampleCount = countbits(memberMask);
        const float groupWeight =
            float(groupSampleCount) * inverseSampleCount;
        const PbrGBufferData groupGBuffer =
            preparedInvariant.gbuffer;
        const PbrPreparedMaterial groupPreparedMaterial =
            preparedInvariant.preparedMaterial;

        bool shareCompleteRadiance = groupSampleCount > 1u &&
            (referenceFallbackGroupMask & (1u << groupIndex)) == 0u;
        if (shareCompleteRadiance)
        {
            [unroll]
            for (uint sampleIndex = 0u;
                sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
                ++sampleIndex)
            {
                if ((memberMask & (1u << sampleIndex)) == 0u ||
                    sampleIndex == representative)
                {
                    continue;
                }

                if (!CanShareCompleteRadiance(
                        sampleDeviceDepths[representative],
                        sampleDeviceDepths[sampleIndex],
                        sampleWorldPositions[representative],
                        sampleWorldPositions[sampleIndex]))
                {
                    shareCompleteRadiance = false;
                    break;
                }
            }
        }

        if (shareCompleteRadiance)
        {
            float3 representativeRadiance = 0.0f;
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
            bool representativeRadianceFinite = true;
#endif
            const bool representativeShadeValid =
                ShadeLoadedDeferredSample(
                sampleChannel2[representative],
                sampleWorldPositions[representative],
                groupGBuffer,
                groupPreparedMaterial,
                directionalVisibility,
                shadowSinCosRotation,
                representativeRadiance
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
                , representativeRadianceFinite
#endif
                );
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
            // The diagnostic permutation evaluates the complete
            // Reference-equivalent lighting body for every member of each
            // candidate shared-radiance group. Identity never participates in
            // this comparison: it only established the candidate group. Probe
            // evaluations are validation overhead, not production evaluations
            // saved; final counter accounting follows the accepted or
            // per-sample-fallback result below.
            if ((g_DiagnosticsExecutionFlags &
                    DiagnosticsProducerValidateErrorBudget) != 0u)
            {
            float3 diagnosticsReferenceRadiances[
                PBR_DEFERRED_MSAA_SAMPLES];
            uint diagnosticsReferenceShadeValidMask = 0u;
            uint diagnosticsNonFiniteMemberMask = 0u;
            uint diagnosticsHdrExceededMemberMask = 0u;
            uint diagnosticsDisplayExceededMemberMask = 0u;
            [unroll]
            for (uint sampleIndex = 0u;
                sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
                ++sampleIndex)
            {
                const uint sampleBit = 1u << sampleIndex;
                if ((memberMask & sampleBit) == 0u)
                    continue;

                float3 referenceRadiance = representativeRadiance;
                bool referenceShadeValid = representativeShadeValid;
                bool referenceRadianceFinite =
                    representativeRadianceFinite;
                if (sampleIndex != representative)
                {
                    referenceShadeValid = ShadeLoadedDeferredSample(
                        sampleChannel2[sampleIndex],
                        sampleWorldPositions[sampleIndex],
                        groupGBuffer,
                        groupPreparedMaterial,
                        directionalVisibility,
                        shadowSinCosRotation,
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
                    if ((referenceVisibilityCoveredMask & sampleBit) != 0u)
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
                    groupSampleCount;

                // Reject the whole candidate group. Each member consumes its
                // own complete Reference result and becomes a deterministic
                // singleton final group; no shared radiance reaches output.
                [unroll]
                for (uint sampleIndex = 0u;
                    sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
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
                        preparationInlineFallback
                            ? UvsrAdaptiveReasonWorkListOverflow
                            : (diagnosticsReferenceShadeValidMask &
                                    sampleBit) != 0u
                                ? 17u
                                : 18u;
                    diagnosticsErrorBudgetReasons[sampleIndex] =
                        diagnosticsGroupErrorReason;
                }
                continue;
            }
            }
#endif
            finalLinearHdr += representativeRadiance * groupWeight;
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
            const uint finalGroup = diagnosticsFinalGroupCount++;
            diagnosticsRepresentativeMask |= 1u << representative;
            ++diagnosticsPerformedLightingEvaluations;
            [unroll]
            for (uint sampleIndex = 0u;
                sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
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
                    preparationInlineFallback
                        ? UvsrAdaptiveReasonWorkListOverflow
                        : representativeShadeValid ? 0u : 18u;
                diagnosticsErrorBudgetReasons[sampleIndex] =
                    DiagnosticsErrorReasonNone;
            }
#endif
            continue;
        }

        // The decoded material and prepared material are invariant only after
        // exact raw-channel comparison. Every position/view/light/shadow/
        // visibility/specular evaluation below remains per sample.
        [unroll]
        for (uint sampleIndex = 0u;
            sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
            ++sampleIndex)
        {
            if ((memberMask & (1u << sampleIndex)) == 0u)
                continue;

            float3 sampleRadiance = 0.0f;
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
            bool sampleRadianceFinite = true;
#endif
            const bool sampleShadeValid = ShadeLoadedDeferredSample(
                sampleChannel2[sampleIndex],
                sampleWorldPositions[sampleIndex],
                groupGBuffer,
                groupPreparedMaterial,
                directionalVisibility,
                shadowSinCosRotation,
                sampleRadiance
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
                , sampleRadianceFinite
#endif
                );
            // Match Reference's per-sample weighting before accumulation.
            // Summing unweighted HDR samples first can overflow even when
            // every individually weighted contribution and the reference
            // result are finite.
            finalLinearHdr += sampleRadiance * inverseSampleCount;
#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
            const uint finalGroup = diagnosticsFinalGroupCount++;
            diagnosticsRepresentativeMask |= 1u << sampleIndex;
            ++diagnosticsPerformedLightingEvaluations;
            diagnosticsFinalGroups[sampleIndex] = finalGroup;
            diagnosticsFinalRepresentatives[sampleIndex] = sampleIndex;
            diagnosticsRadiance[sampleIndex] += sampleRadiance;
            const uint keyReason = ValidateAdaptiveSurfaceKey(
                sampleKeys[sampleIndex]);
            diagnosticsFallbackReasons[sampleIndex] =
                preparationInlineFallback
                    ? UvsrAdaptiveReasonWorkListOverflow
                    : !sampleShadeValid
                        ? 18u
                        : keyReason != 0u
                            ? keyReason
                            : ((descriptorValidSampleMask &
                                    (1u << sampleIndex)) == 0u)
                                ? 18u
                                : (groupSampleCount > 1u ||
                                        (referenceFallbackGroupMask &
                                            (1u << groupIndex)) != 0u)
                                    ? 17u
                                    : 0u;
#endif
        }
    }

#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER
#if PBR_DEFERRED_MSAA_VISIBILITY
    if (g_PbrDeferred.visibilityDebugView == 0u &&
        g_PbrDeferred.lightingDebugView == 0u &&
        referenceVisibilityCoveredMask != 0u)
    {
        const float3 visibilityCorrection =
            t_VisibilityComposite[pixelPosition].rgb -
            t_VisibilityBaseLighting[pixelPosition].rgb;
        if (all(isfinite(visibilityCorrection)))
        {
            [unroll]
            for (uint sampleIndex = 0u;
                sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
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
#if UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY == \
        UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_SEPARATED
    AdaptiveDiagnosticsStorePreparationWorkList(
        pixelPosition,
        diagnosticsPreparationWorkListPeakOccupancy,
        diagnosticsPreparationWorkListCapacity,
        diagnosticsPreparationWorkListOverflow);
#endif
    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
        ++sampleIndex)
    {
        const bool covered =
            (coveredSampleMask & (1u << sampleIndex)) != 0u;
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
        const float4 surfaceHit = diagnosticsSurfaceHits[sampleIndex];
        if (covered && all(isfinite(surfaceHit)) &&
            surfaceHit.w == 1.0f)
        {
            record.validFields |=
                DiagnosticsSampleSuppliedBarycentrics |
                DiagnosticsSampleFrontFaceValid;
            record.suppliedBarycentricX = surfaceHit.x;
            record.suppliedBarycentricY = surfaceHit.y;
            if (surfaceHit.z != 0.0f)
                record.validFields |= DiagnosticsSampleFrontFace;
        }
        if (covered &&
            diagnosticsFinalGroups[sampleIndex] !=
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

    StoreAdaptiveDeferredLighting(
        pixelPosition,
        countbits(referenceVisibilityCoveredMask),
        inverseSampleCount,
        finalLinearHdr);
}
