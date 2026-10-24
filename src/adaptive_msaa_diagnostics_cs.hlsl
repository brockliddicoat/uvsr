/*
 * Copyright (c) 2017-2025 The Forge Interactive Inc.
 * Licensed under the Apache License, Version 2.0. You may obtain a copy at
 * http://www.apache.org/licenses/LICENSE-2.0
 */

// UVSR adaptive MSAA diagnostics.
//
// Exact-key loading and producer-authored final representatives follow UVSR's
// adaptive MSAA ownership contract. The architecture is informed by The Forge
// Visibility_Buffer at commit a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b
// (Apache-2.0) and Burns/Hunt's visibility-buffer grouping work, but this
// diagnostics implementation is UVSR-native source. It never treats coverage
// or depth as surface identity and never feeds diagnostic results back into
// rendering.

#ifndef UVSR_DIAGNOSTICS_MSAA_SAMPLES
#error UVSR_DIAGNOSTICS_MSAA_SAMPLES must be a static 2, 4, 8, or 16.
#endif

#if UVSR_DIAGNOSTICS_MSAA_SAMPLES != 2 && \
    UVSR_DIAGNOSTICS_MSAA_SAMPLES != 4 && \
    UVSR_DIAGNOSTICS_MSAA_SAMPLES != 8 && \
    UVSR_DIAGNOSTICS_MSAA_SAMPLES != 16
#error Unsupported adaptive MSAA diagnostics sample count.
#endif

#include "adaptive_msaa_raster_cb.h"

static const uint InvalidWord = 0xffffffffu;
static const uint ClearedVisibilityWord =
    ADAPTIVE_MSAA_CLEARED_VISIBILITY_WORD;
static const uint InvalidFloatBits = 0x7fc00000u;
static const uint StatusUnavailableFallback = 0x1fu;
static const uint StatusUnavailableErrorBudget = 0x7u;
static const uint StatusFallbackMask = 0x1fu;
static const uint StatusErrorBudgetShift = 5u;
static const uint GeometryInstanceSlotBits = 20u;
static const uint GeometryInstanceGenerationMask = 0xfffu;
static const uint MinimumGeometryInstanceGeneration = 1u;
static const uint MaximumGeometryInstanceGeneration = 0xffeu;

static const uint InputReferenceHdr = 1u << 0u;
static const uint InputAdaptiveHdr = 1u << 1u;
static const uint InputReferenceAttributes = 1u << 2u;
static const uint InputReconstructedAttributes = 1u << 3u;
static const uint InputSampleStatus = 1u << 4u;
static const uint InputSelectedSampleData = 1u << 5u;
static const uint InputPixelMetadata = 1u << 6u;
static const uint InputRasterSurfaceHits = 1u << 7u;
static const uint InputReferenceDepth = 1u << 8u;
static const uint InputReconstructedDepth = 1u << 9u;
static const uint InputReferenceDiffuse = 1u << 10u;
static const uint InputReconstructedDiffuse = 1u << 11u;
static const uint InputReferenceMaterial = 1u << 12u;
static const uint InputReconstructedMaterial = 1u << 13u;
static const uint InputReferenceNormals = 1u << 14u;
static const uint InputReconstructedNormals = 1u << 15u;
static const uint InputReferenceEmissive = 1u << 16u;
static const uint InputReconstructedEmissive = 1u << 17u;
static const uint InputReferenceAmbientOcclusion = 1u << 18u;
static const uint InputReconstructedAmbientOcclusion = 1u << 19u;
static const uint InputReferenceMotion = 1u << 20u;
static const uint InputReconstructedMotion = 1u << 21u;
static const uint InputVisibilityClass = 1u << 22u;
static const uint InputCompleteReconstruction =
    InputReferenceDepth | InputReconstructedDepth |
    InputReferenceDiffuse | InputReconstructedDiffuse |
    InputReferenceMaterial | InputReconstructedMaterial |
    InputReferenceNormals | InputReconstructedNormals |
    InputReferenceEmissive | InputReconstructedEmissive |
    InputReferenceAmbientOcclusion |
    InputReconstructedAmbientOcclusion |
    InputReferenceMotion | InputReconstructedMotion;

static const uint ExecuteCounters = 1u << 0u;
static const uint ExecuteSelectedPixel = 1u << 1u;
static const uint ExecuteViewAvailable = 1u << 2u;
static const uint ExecuteVisualization = 1u << 3u;
static const uint ExecuteReferencePerSample = 1u << 4u;
static const uint ErrorExecuteMetrics = 1u << 0u;
static const uint ErrorExecuteHistoryWrite = 1u << 1u;
static const uint ErrorExecuteHistoryRead = 1u << 2u;
static const uint ErrorDomainCurrentFrameSpatial = 0u;
static const uint ErrorDomainFrameToFrameTemporal = 1u;
static const uint ErrorLightingSpatialValid = 1u << 0u;
static const uint ErrorLightingTemporalValid = 1u << 1u;
static const uint ErrorReconstructionValid = 1u << 2u;
static const uint ErrorHistoryReset = 1u << 3u;

static const uint ReconstructionChannelCombined = 0u;
static const uint ReconstructionChannelDepth = 1u;
static const uint ReconstructionChannelDiffuse = 2u;
static const uint ReconstructionChannelMaterial = 3u;
static const uint ReconstructionChannelNormals = 4u;
static const uint ReconstructionChannelEmissive = 5u;
static const uint ReconstructionChannelAmbientOcclusion = 6u;
static const uint ReconstructionChannelMotion = 7u;

static const float ReconstructionDepthThreshold = 1.0f / 1048576.0f;
static const float ReconstructionDiffuseThreshold = 1.0f / 255.0f;
static const float ReconstructionMaterialThreshold = 1.0f / 255.0f;
static const float ReconstructionNormalThreshold = 1.0f / 1024.0f;
static const float ReconstructionEmissiveThreshold = 1.0f / 1024.0f;
static const float ReconstructionAmbientOcclusionThreshold = 1.0f / 255.0f;
static const float ReconstructionMotionThreshold = 1.0f / 1024.0f;
static const uint CounterDispatchComplete = 1u << 1u;
static const uint SelectedHeaderDispatchComplete = 1u << 2u;

static const uint DebugViewSurfaceOwnershipKeys = 1u;
static const uint DebugViewUniqueGroupsAndRepresentatives = 2u;
static const uint DebugViewFallbackReasons = 3u;
static const uint DebugViewCompactReconstructionError = 4u;
static const uint DebugViewAdaptiveLightingError = 5u;

static const uint ComparisonSplitScreen = 1u;
static const uint ComparisonErrorHeatmap = 2u;

static const uint SampleInputSuppliedBarycentrics = 1u << 0u;
static const uint SampleInputReconstructedBarycentrics = 1u << 1u;
static const uint SampleInputRadiance = 1u << 2u;
static const uint SampleInputFallbackReason = 1u << 3u;
static const uint SampleInputErrorBudgetReason = 1u << 4u;
static const uint SampleInputFrontFaceValid = 1u << 5u;
static const uint SampleInputFrontFace = 1u << 6u;
static const uint SampleInputShadingGroup = 1u << 7u;

static const uint PixelMetadataLightingValid = 1u << 0u;
static const uint PixelMetadataWorkListValid = 1u << 1u;
static const uint PixelMetadataWorkListOverflow = 1u << 2u;
static const uint PixelMetadataGroupCountShift = 3u;
static const uint PixelMetadataGroupCountMask = 0x1fu << 3u;
static const uint PixelMetadataRepresentativeMaskShift = 8u;
static const uint PixelMetadataRepresentativeMaskMask = 0xffffu << 8u;
static const uint PixelMetadataGroupDataValid = 1u << 24u;
static const uint PixelMetadataErrorBudgetValid = 1u << 25u;

static const uint CounterGroupHistogramValid = 1u << 0u;
static const uint CounterLightingValid = 1u << 1u;
static const uint CounterFallbackReasonsValid = 1u << 2u;
static const uint CounterInvalidKeysValid = 1u << 3u;
static const uint CounterStaleAndMappingValid = 1u << 4u;
static const uint CounterWorkListValid = 1u << 5u;
static const uint CounterErrorBudgetValid = 1u << 6u;

static const uint SelectedRecordCovered = 1u << 0u;
static const uint SelectedRecordKeySupplied = 1u << 1u;
static const uint SelectedRecordKeyValid = 1u << 2u;
static const uint SelectedRecordDepthValid = 1u << 3u;
static const uint SelectedRecordSuppliedBarycentrics = 1u << 4u;
static const uint SelectedRecordReconstructedBarycentrics = 1u << 5u;
static const uint SelectedRecordGroupValid = 1u << 6u;
static const uint SelectedRecordFallbackReason = 1u << 7u;
static const uint SelectedRecordErrorBudgetReason = 1u << 8u;
static const uint SelectedRecordRadiance = 1u << 9u;
static const uint SelectedRecordFrontFaceValid = 1u << 10u;
static const uint SelectedRecordFrontFace = 1u << 11u;
static const uint SelectedRecordWeightValid = 1u << 12u;

static const uint FallbackReasonNone = 0u;
static const uint FallbackReasonInvalidSentinel = 1u;
static const uint FallbackReasonInvalidHandleEncoding = 2u;
static const uint FallbackReasonSlotOutOfRange = 3u;
static const uint FallbackReasonGenerationMismatch = 4u;
static const uint FallbackReasonTopologyEpochMismatch = 5u;
static const uint FallbackReasonLodRevisionMismatch = 6u;
static const uint FallbackReasonDeformationRevisionMismatch = 7u;
static const uint FallbackReasonMaterialRevisionMismatch = 8u;
static const uint FallbackReasonPrimitiveOverflow = 10u;
static const uint FallbackReasonGenerationExhausted = 11u;
static const uint FallbackReasonRegistryResetRequired = 12u;
static const uint FallbackReasonTransientMappingCollision = 13u;
static const uint FallbackReasonTransientMappingFailure = 14u;
static const uint FallbackReasonReconstructionFailure = 18u;
static const uint FallbackReasonTextureContentRevisionMismatch = 22u;
static const uint FallbackReasonTextureQualificationMismatch = 23u;
static const uint FallbackReasonCount = 24u;
static const uint ErrorBudgetReasonNone = 0u;
static const uint ErrorBudgetReasonNonFinite = 1u;
static const uint ErrorBudgetReasonHdrLimitExceeded = 2u;
static const uint ErrorBudgetReasonDisplayLimitExceeded = 3u;
static const uint ErrorBudgetReasonCount = 4u;

static const uint CounterHistogramOffset = 32u;
static const uint CounterReferenceEvaluationsOffset = 100u;
static const uint CounterPerformedEvaluationsOffset = 104u;
static const uint CounterSavedEvaluationsOffset = 108u;
static const uint CounterFallbackReasonsOffset = 112u;
static const uint CounterInvalidKeysOffset = 208u;
static const uint CounterStaleKeysOffset = 212u;
static const uint CounterMappingFailuresOffset = 216u;
static const uint CounterWorkListObservationsOffset = 220u;
static const uint CounterWorkListOccupancyOffset = 224u;
static const uint CounterWorkListCapacityOffset = 228u;
static const uint CounterWorkListPeakOffset = 232u;
static const uint CounterWorkListOverflowOffset = 236u;
static const uint CounterErrorBudgetReasonsOffset = 240u;
static const uint CounterErrorBudgetRejectedGroupsOffset = 256u;
static const uint CounterErrorBudgetRejectedSamplesOffset = 260u;
static const uint CounterValidityOffset = 8u;
static const uint SelectedSamplesOffset = 328u;
static const uint SelectedSampleStride = 64u;
static const uint CounterFlagsOffset = 12u;
static const uint SelectedHeaderFlagsOffset = 276u;
static const uint DiagnosticsGpuAbiVersion = 6u;
static const uint CounterAbiVersionOffset = 0u;
static const uint CounterByteSizeOffset = 4u;
static const uint CounterByteSize = 264u;
static const uint SelectedHeaderAbiVersionOffset = 264u;
static const uint SelectedHeaderByteSizeOffset = 268u;
static const uint SelectedHeaderAndRecordsByteSize = 1088u;

cbuffer DiagnosticsConstants : register(b0)
{
    uint g_Width;
    uint g_Height;
    uint g_SampleCount;
    uint g_RequestedView;
    uint g_ComparisonMode;
    uint g_VisualizedSampleIndex;
    uint g_InputAvailabilityMask;
    uint g_ExecutionFlags;
    uint g_SelectedPixelX;
    uint g_SelectedPixelY;
    uint g_CaptureIdLow;
    uint g_CaptureIdHigh;
    uint g_FrameIndexLow;
    uint g_FrameIndexHigh;
    uint g_CounterValidityMask;
    uint g_MaximumWorkListCapacity;
    uint g_ErrorDomain;
    uint g_ReconstructionChannel;
    uint g_ErrorExecutionFlags;
    uint g_HistoryValid;
};

struct SelectedSampleInput
{
    uint validFields;
    float suppliedBarycentricX;
    float suppliedBarycentricY;
    float reconstructedBarycentricX;
    float reconstructedBarycentricY;
    float radianceR;
    float radianceG;
    float radianceB;
    uint fallbackReason;
    uint errorBudgetReason;
    uint shadingGroupIndex;
    uint representativeSampleIndex;
};

struct PixelMetadata
{
    uint validFields;
    uint performedLightingEvaluations;
    uint workListOccupancy;
    uint workListCapacity;
    uint errorBudgetNonFiniteGroupCount;
    uint errorBudgetHdrGroupCount;
    uint errorBudgetDisplayGroupCount;
    uint errorBudgetRejectedSampleCount;
};

Texture2DMS<uint2, UVSR_DIAGNOSTICS_MSAA_SAMPLES>
    t_SurfaceKeys : register(t0);
Texture2DMS<float, UVSR_DIAGNOSTICS_MSAA_SAMPLES>
    t_Depth : register(t1);
Texture2D<float4> t_ReferenceHdr : register(t2);
Texture2D<float4> t_AdaptiveHdr : register(t3);
Texture2D<float4> t_ReferenceAttributes : register(t4);
Texture2D<float4> t_ReconstructedAttributes : register(t5);
Texture2DArray<uint> t_SampleStatus : register(t6);
StructuredBuffer<SelectedSampleInput> t_SelectedSampleData : register(t7);
StructuredBuffer<PixelMetadata> t_PixelMetadata : register(t8);
Texture2DMS<float4, UVSR_DIAGNOSTICS_MSAA_SAMPLES>
    t_RasterSurfaceHits : register(t9);
Texture2D<float> t_ReferenceDepth : register(t10);
Texture2D<float> t_ReconstructedDepth : register(t11);
Texture2D<float4> t_ReferenceDiffuse : register(t12);
Texture2D<float4> t_ReconstructedDiffuse : register(t13);
Texture2D<float4> t_ReferenceMaterial : register(t14);
Texture2D<float4> t_ReconstructedMaterial : register(t15);
Texture2D<float4> t_ReferenceNormals : register(t16);
Texture2D<float4> t_ReconstructedNormals : register(t17);
Texture2D<float4> t_ReferenceEmissive : register(t18);
Texture2D<float4> t_ReconstructedEmissive : register(t19);
Texture2D<float> t_ReferenceAmbientOcclusion : register(t20);
Texture2D<float> t_ReconstructedAmbientOcclusion : register(t21);
Texture2D<float4> t_ReferenceMotion : register(t22);
Texture2D<float4> t_ReconstructedMotion : register(t23);
Texture2D<float4> t_PreviousReferenceHdr : register(t24);
Texture2D<float4> t_PreviousAdaptiveHdr : register(t25);
Texture2DMS<uint, UVSR_DIAGNOSTICS_MSAA_SAMPLES>
    t_VisibilityClass : register(t26);

RWTexture2D<float4> u_Visualization : register(u0);
RWByteAddressBuffer u_Readback : register(u1);
RWTexture2D<float4> u_ReconstructionError0 : register(u2);
RWTexture2D<float4> u_ReconstructionError1 : register(u3);
RWTexture2D<float4> u_LightingError : register(u4);
RWTexture2D<uint> u_ErrorValidity : register(u5);
RWTexture2D<float4> u_NextReferenceHdr : register(u6);
RWTexture2D<float4> u_NextAdaptiveHdr : register(u7);

bool IsBackgroundKey(uint2 key)
{
    return all(key == ClearedVisibilityWord);
}

bool IsClearedKeyDepth(uint2 key, float deviceDepth)
{
    // UVSR's reverse-depth G-buffer clear is zero. Depth is coverage evidence
    // only: it distinguishes the exact reserved-generation clear from a
    // covered raster failure that wrote an invalid key; it never creates
    // identity.
    return IsBackgroundKey(key) && isfinite(deviceDepth) &&
        asuint(deviceDepth) == 0u;
}

bool IsClearedBackgroundSample(
    uint2 key,
    float deviceDepth,
    uint visibilityClass,
    bool visibilityClassAvailable)
{
    return IsClearedKeyDepth(key, deviceDepth) &&
        (!visibilityClassAvailable ||
            visibilityClass == ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND);
}

bool IsLocalFallbackVisibilityReason(uint fallbackReason)
{
    return fallbackReason ==
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_UNSUPPORTED_DEFORMATION ||
        fallbackReason ==
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_UNSUPPORTED_MATERIAL ||
        fallbackReason ==
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_RECONSTRUCTION_FAILURE;
}

bool DecodeCoveredVisibilityClass(
    uint visibilityClass,
    out bool reconstruct,
    out bool fallback)
{
    reconstruct =
        visibilityClass == ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT;
    fallback =
        (visibilityClass & ADAPTIVE_MSAA_VISIBILITY_CLASS_FALLBACK_BIT) != 0u;
    const bool localFallback = fallback &&
        (visibilityClass &
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_FALLBACK_BIT) != 0u;
    const uint fallbackReason =
        visibilityClass & ADAPTIVE_MSAA_VISIBILITY_CLASS_REASON_MASK;
    return reconstruct ||
        (visibilityClass <= 0xffu && fallback &&
            fallbackReason > 0u &&
            fallbackReason <= ADAPTIVE_MSAA_VISIBILITY_CLASS_MAX_REASON &&
            (!localFallback ||
                IsLocalFallbackVisibilityReason(fallbackReason)));
}

bool IsValidCoveredDepth(float deviceDepth, bool requirePositive)
{
    // Reconstructable primitives are interval-proved away from reverse-Z
    // clear. A full-G-buffer fallback (or the classless adaptive G-buffer
    // path) has exact key/class identity and may legitimately cover depth 0.
    return isfinite(deviceDepth) && deviceDepth >= 0.0f &&
        deviceDepth <= 1.0f && (!requirePositive || deviceDepth > 0.0f);
}

uint ValidateKey(uint2 key)
{
    if (key.x == InvalidWord || key.y == InvalidWord)
        return FallbackReasonInvalidSentinel;

    const uint generation =
        (key.x >> GeometryInstanceSlotBits) &
        GeometryInstanceGenerationMask;
    if (generation < MinimumGeometryInstanceGeneration ||
        generation > MaximumGeometryInstanceGeneration)
    {
        return FallbackReasonInvalidHandleEncoding;
    }
    return FallbackReasonNone;
}

bool IsStaleReason(uint reason)
{
    return reason == FallbackReasonGenerationMismatch ||
        reason == FallbackReasonTopologyEpochMismatch ||
        reason == FallbackReasonLodRevisionMismatch ||
        reason == FallbackReasonDeformationRevisionMismatch ||
        reason == FallbackReasonMaterialRevisionMismatch ||
        reason == FallbackReasonGenerationExhausted ||
        reason == FallbackReasonRegistryResetRequired ||
        reason == FallbackReasonTextureContentRevisionMismatch ||
        reason == FallbackReasonTextureQualificationMismatch;
}

bool IsMappingReason(uint reason)
{
    return reason == FallbackReasonTransientMappingCollision ||
        reason == FallbackReasonTransientMappingFailure;
}

bool IsInvalidKeyReason(uint reason)
{
    return reason == FallbackReasonInvalidSentinel ||
        reason == FallbackReasonInvalidHandleEncoding ||
        reason == FallbackReasonSlotOutOfRange ||
        reason == FallbackReasonPrimitiveOverflow;
}

uint HashWord(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

float3 HashColor(uint2 key)
{
    const uint hash = HashWord(key.x ^ HashWord(key.y));
    return (float3(
        hash & 0xffu,
        (hash >> 8u) & 0xffu,
        (hash >> 16u) & 0xffu) + 32.0f) / 287.0f;
}

float3 ReasonColor(uint reason)
{
    if (reason == FallbackReasonNone)
        return float3(0.05f, 0.35f, 0.08f);
    if (reason == InvalidWord || reason >= FallbackReasonCount)
        return float3(1.0f, 0.0f, 1.0f);
    return HashColor(uint2(reason, 0xa511e9b3u));
}

float3 ErrorHeat(float errorValue)
{
    const float normalized = saturate(errorValue * 32.0f);
    return saturate(float3(
        normalized * 2.0f,
        2.0f - abs(normalized * 4.0f - 2.0f),
        2.0f - normalized * 2.0f));
}

float RelativeHdrError(float3 adaptive, float3 reference)
{
    const float3 denominator = max(
        float3(1.0f / 1024.0f, 1.0f / 1024.0f, 1.0f / 1024.0f),
        max(abs(adaptive), abs(reference)));
    return max(
        abs(adaptive.x - reference.x) / denominator.x,
        max(
            abs(adaptive.y - reference.y) / denominator.y,
            abs(adaptive.z - reference.z) / denominator.z));
}

float TemporalHdrError(
    float3 adaptiveCurrent,
    float3 referenceCurrent,
    float3 adaptivePrevious,
    float3 referencePrevious)
{
    const float3 denominator = max(
        float3(1.0f / 1024.0f, 1.0f / 1024.0f, 1.0f / 1024.0f),
        max(
            max(abs(adaptiveCurrent), abs(referenceCurrent)),
            max(abs(adaptivePrevious), abs(referencePrevious))));
    const float3 residualChange = abs(
        (adaptiveCurrent - referenceCurrent) -
        (adaptivePrevious - referencePrevious));
    return max(
        residualChange.x / denominator.x,
        max(
            residualChange.y / denominator.y,
            residualChange.z / denominator.z));
}

bool IsFinite4(float4 value)
{
    return all(isfinite(value));
}

float MaximumAbsoluteDifference(float4 first, float4 second)
{
    const float4 difference = abs(first - second);
    return max(
        max(difference.x, difference.y),
        max(difference.z, difference.w));
}

float3 SelectReconstructionReference(
    uint channel,
    float referenceDepth,
    float4 referenceDiffuse,
    float4 referenceMaterial,
    float4 referenceNormals,
    float4 referenceEmissive,
    float referenceAmbientOcclusion,
    float4 referenceMotion)
{
    if (channel == ReconstructionChannelDepth)
        return referenceDepth.xxx;
    if (channel == ReconstructionChannelDiffuse)
        return referenceDiffuse.rgb;
    if (channel == ReconstructionChannelMaterial)
        return referenceMaterial.rgb;
    if (channel == ReconstructionChannelNormals)
        return referenceNormals.rgb * 0.5f + 0.5f;
    if (channel == ReconstructionChannelEmissive)
        return referenceEmissive.rgb;
    if (channel == ReconstructionChannelAmbientOcclusion)
        return referenceAmbientOcclusion.xxx;
    if (channel == ReconstructionChannelMotion)
        return float3(referenceMotion.xy * 0.5f + 0.5f, 0.0f);
    return referenceNormals.rgb * 0.5f + 0.5f;
}

float3 SelectReconstructionCandidate(
    uint channel,
    float reconstructedDepth,
    float4 reconstructedDiffuse,
    float4 reconstructedMaterial,
    float4 reconstructedNormals,
    float4 reconstructedEmissive,
    float reconstructedAmbientOcclusion,
    float4 reconstructedMotion)
{
    if (channel == ReconstructionChannelDepth)
        return reconstructedDepth.xxx;
    if (channel == ReconstructionChannelDiffuse)
        return reconstructedDiffuse.rgb;
    if (channel == ReconstructionChannelMaterial)
        return reconstructedMaterial.rgb;
    if (channel == ReconstructionChannelNormals)
        return reconstructedNormals.rgb * 0.5f + 0.5f;
    if (channel == ReconstructionChannelEmissive)
        return reconstructedEmissive.rgb;
    if (channel == ReconstructionChannelAmbientOcclusion)
        return reconstructedAmbientOcclusion.xxx;
    if (channel == ReconstructionChannelMotion)
        return float3(reconstructedMotion.xy * 0.5f + 0.5f, 0.0f);
    return reconstructedNormals.rgb * 0.5f + 0.5f;
}

void AtomicAdd(uint byteOffset, uint value)
{
    uint ignored;
    u_Readback.InterlockedAdd(byteOffset, value, ignored);
}

void AtomicMaximum(uint byteOffset, uint value)
{
    uint ignored;
    u_Readback.InterlockedMax(byteOffset, value, ignored);
}

void AtomicOr(uint byteOffset, uint value)
{
    uint ignored;
    u_Readback.InterlockedOr(byteOffset, value, ignored);
}

void StoreSelectedWord(uint sampleIndex, uint wordIndex, uint value)
{
    u_Readback.Store(
        SelectedSamplesOffset + sampleIndex * SelectedSampleStride +
            wordIndex * 4u,
        value);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= g_Width || dispatchThreadId.y >= g_Height)
        return;

    const int2 pixel = int2(dispatchThreadId.xy);
    const uint linearPixel = dispatchThreadId.y * g_Width +
        dispatchThreadId.x;

    uint2 keys[UVSR_DIAGNOSTICS_MSAA_SAMPLES];
    float depths[UVSR_DIAGNOSTICS_MSAA_SAMPLES];
    uint keyReasons[UVSR_DIAGNOSTICS_MSAA_SAMPLES];
    uint visibilityClasses[UVSR_DIAGNOSTICS_MSAA_SAMPLES];
    bool backgroundSamples[UVSR_DIAGNOSTICS_MSAA_SAMPLES];
    uint2 statuses[UVSR_DIAGNOSTICS_MSAA_SAMPLES];

    uint referenceSampleCount = 0u;
    uint malformedKeyCount = 0u;
    const bool visibilityClassAvailable =
        (g_InputAvailabilityMask & InputVisibilityClass) != 0u;

    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < UVSR_DIAGNOSTICS_MSAA_SAMPLES;
        ++sampleIndex)
    {
        const uint2 key = t_SurfaceKeys.Load(pixel, sampleIndex);
        keys[sampleIndex] = key;
        depths[sampleIndex] = t_Depth.Load(pixel, sampleIndex);
        keyReasons[sampleIndex] = ValidateKey(key);
        visibilityClasses[sampleIndex] = visibilityClassAvailable
            ? t_VisibilityClass.Load(pixel, sampleIndex)
            : InvalidWord;
        backgroundSamples[sampleIndex] = IsClearedBackgroundSample(
            key,
            depths[sampleIndex],
            visibilityClasses[sampleIndex],
            visibilityClassAvailable);
        bool reconstructClass = false;
        bool fallbackClass = false;
        const bool validCoveredClass = !visibilityClassAvailable ||
            DecodeCoveredVisibilityClass(
                visibilityClasses[sampleIndex],
                reconstructClass,
                fallbackClass);
        if (keyReasons[sampleIndex] == FallbackReasonNone &&
            (!IsValidCoveredDepth(
                    depths[sampleIndex],
                    visibilityClassAvailable && reconstructClass &&
                        !fallbackClass) ||
                !validCoveredClass ||
                (visibilityClassAvailable &&
                    visibilityClasses[sampleIndex] ==
                        ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND)))
        {
            keyReasons[sampleIndex] =
                FallbackReasonReconstructionFailure;
        }
        statuses[sampleIndex] =
            uint2(
                StatusUnavailableFallback,
                StatusUnavailableErrorBudget);
        if ((g_InputAvailabilityMask & InputSampleStatus) != 0u)
        {
            const uint packedStatus = t_SampleStatus.Load(
                int4(pixel, int(sampleIndex), 0));
            statuses[sampleIndex] = uint2(
                packedStatus & StatusFallbackMask,
                (packedStatus >> StatusErrorBudgetShift) &
                    StatusUnavailableErrorBudget);
        }

        if (keyReasons[sampleIndex] != FallbackReasonNone)
        {
            if (!backgroundSamples[sampleIndex])
            {
                ++malformedKeyCount;
                ++referenceSampleCount;
            }
            continue;
        }

        ++referenceSampleCount;
    }

    PixelMetadata producerMetadata = (PixelMetadata)0;
    uint shadingGroupCount = InvalidWord;
    uint shadingRepresentativeMask = 0u;
    if ((g_InputAvailabilityMask & InputPixelMetadata) != 0u)
    {
        producerMetadata = t_PixelMetadata[linearPixel];
        if ((producerMetadata.validFields &
                PixelMetadataGroupDataValid) != 0u)
        {
            const uint candidateGroupCount =
                (producerMetadata.validFields &
                    PixelMetadataGroupCountMask) >>
                    PixelMetadataGroupCountShift;
            const uint candidateRepresentativeMask =
                (producerMetadata.validFields &
                    PixelMetadataRepresentativeMaskMask) >>
                    PixelMetadataRepresentativeMaskShift;
            if (candidateGroupCount <= g_SampleCount &&
                countbits(candidateRepresentativeMask) ==
                    candidateGroupCount &&
                (candidateRepresentativeMask &
                    ~((1u << g_SampleCount) - 1u)) == 0u)
            {
                shadingGroupCount = candidateGroupCount;
                shadingRepresentativeMask =
                    candidateRepresentativeMask;
            }
        }
    }
    if ((g_ExecutionFlags & ExecuteReferencePerSample) != 0u)
    {
        // Reference evaluates every valid covered sample independently. Keep
        // this separate from exact ownership grouping: two samples with one
        // SurfaceKey are still two deterministic Reference shading groups.
        shadingGroupCount = 0u;
        shadingRepresentativeMask = 0u;
        [unroll]
        for (uint sampleIndex = 0u;
            sampleIndex < UVSR_DIAGNOSTICS_MSAA_SAMPLES;
            ++sampleIndex)
        {
            if (keyReasons[sampleIndex] == FallbackReasonNone)
            {
                ++shadingGroupCount;
                shadingRepresentativeMask |= 1u << sampleIndex;
            }
        }
    }
    const bool exactErrorBudgetMetadataAvailable =
        (g_InputAvailabilityMask & InputPixelMetadata) != 0u &&
        (producerMetadata.validFields &
            PixelMetadataErrorBudgetValid) != 0u;

    if ((g_ExecutionFlags & ExecuteCounters) != 0u)
    {
        if ((g_CounterValidityMask & CounterGroupHistogramValid) != 0u)
        {
            if (shadingGroupCount != InvalidWord)
            {
                AtomicAdd(
                    CounterHistogramOffset + shadingGroupCount * 4u,
                    1u);
            }
        }
        if ((g_CounterValidityMask & CounterInvalidKeysValid) != 0u &&
            malformedKeyCount != 0u)
        {
            AtomicAdd(CounterInvalidKeysOffset, malformedKeyCount);
        }

        if ((g_InputAvailabilityMask & InputSampleStatus) != 0u ||
            (g_ExecutionFlags & ExecuteReferencePerSample) != 0u)
        {
            [unroll]
            for (uint sampleIndex = 0u;
                sampleIndex < UVSR_DIAGNOSTICS_MSAA_SAMPLES;
                ++sampleIndex)
            {
                uint fallbackReason = statuses[sampleIndex].x;
                const bool malformedKey =
                    keyReasons[sampleIndex] != FallbackReasonNone &&
                    !backgroundSamples[sampleIndex];
                if (malformedKey &&
                    (fallbackReason == StatusUnavailableFallback ||
                        fallbackReason == FallbackReasonNone))
                {
                    fallbackReason = keyReasons[sampleIndex];
                }
                if ((g_CounterValidityMask &
                        CounterFallbackReasonsValid) != 0u &&
                    fallbackReason > FallbackReasonNone &&
                    fallbackReason < FallbackReasonCount)
                {
                    AtomicAdd(
                        CounterFallbackReasonsOffset +
                            fallbackReason * 4u,
                        1u);
                    if ((g_CounterValidityMask &
                            CounterInvalidKeysValid) != 0u &&
                        IsInvalidKeyReason(fallbackReason) &&
                        !malformedKey)
                    {
                        AtomicAdd(CounterInvalidKeysOffset, 1u);
                    }
                    if ((g_CounterValidityMask &
                            CounterStaleAndMappingValid) != 0u &&
                        IsStaleReason(fallbackReason))
                    {
                        AtomicAdd(CounterStaleKeysOffset, 1u);
                    }
                    if ((g_CounterValidityMask &
                            CounterStaleAndMappingValid) != 0u &&
                        IsMappingReason(fallbackReason))
                    {
                        AtomicAdd(CounterMappingFailuresOffset, 1u);
                    }
                }
            }

        }

        if ((g_InputAvailabilityMask & InputPixelMetadata) != 0u)
        {
            if ((g_CounterValidityMask & CounterLightingValid) != 0u &&
                (producerMetadata.validFields &
                    PixelMetadataLightingValid) != 0u &&
                producerMetadata.performedLightingEvaluations <=
                    referenceSampleCount)
            {
                AtomicAdd(
                    CounterReferenceEvaluationsOffset,
                    referenceSampleCount);
                AtomicAdd(
                    CounterPerformedEvaluationsOffset,
                    producerMetadata.performedLightingEvaluations);
                AtomicAdd(
                    CounterSavedEvaluationsOffset,
                    referenceSampleCount -
                        producerMetadata.performedLightingEvaluations);
            }
            if ((g_CounterValidityMask & CounterWorkListValid) != 0u &&
                (producerMetadata.validFields &
                    PixelMetadataWorkListValid) != 0u)
            {
                const uint safeCapacity = min(
                    producerMetadata.workListCapacity,
                    g_MaximumWorkListCapacity);
                const uint safeOccupancy = min(
                    producerMetadata.workListOccupancy,
                    safeCapacity);
                AtomicAdd(CounterWorkListObservationsOffset, 1u);
                AtomicAdd(CounterWorkListOccupancyOffset, safeOccupancy);
                AtomicAdd(CounterWorkListCapacityOffset, safeCapacity);
                AtomicMaximum(CounterWorkListPeakOffset, safeOccupancy);
                if ((producerMetadata.validFields &
                        PixelMetadataWorkListOverflow) != 0u ||
                    producerMetadata.workListOccupancy >
                        producerMetadata.workListCapacity ||
                    producerMetadata.workListCapacity >
                        g_MaximumWorkListCapacity)
                {
                    AtomicAdd(CounterWorkListOverflowOffset, 1u);
                }
            }
            if (exactErrorBudgetMetadataAvailable)
            {
                const uint rejectedGroupCount =
                    producerMetadata.errorBudgetNonFiniteGroupCount +
                    producerMetadata.errorBudgetHdrGroupCount +
                    producerMetadata.errorBudgetDisplayGroupCount;
                const bool validExactCounts =
                    producerMetadata.errorBudgetNonFiniteGroupCount <=
                        referenceSampleCount &&
                    producerMetadata.errorBudgetHdrGroupCount <=
                        referenceSampleCount &&
                    producerMetadata.errorBudgetDisplayGroupCount <=
                        referenceSampleCount &&
                    rejectedGroupCount <= referenceSampleCount &&
                    producerMetadata.errorBudgetRejectedSampleCount >=
                        rejectedGroupCount &&
                    producerMetadata.errorBudgetRejectedSampleCount <=
                        referenceSampleCount;

                // This family is self-describing producer data rather than a
                // resource-presence inference. The producer-authored validity
                // bit promotes the initialized readback family only after the
                // diagnostic shader has consumed an exact record.
                AtomicOr(
                    CounterValidityOffset,
                    CounterErrorBudgetValid);
                if (!validExactCounts)
                {
                    // None is a reserved non-counter bin. Making it nonzero
                    // forces CPU ABI validation to reject malformed metadata.
                    AtomicAdd(
                        CounterErrorBudgetReasonsOffset +
                            ErrorBudgetReasonNone * 4u,
                        1u);
                }
                else
                {
                    AtomicAdd(
                        CounterErrorBudgetReasonsOffset +
                            ErrorBudgetReasonNonFinite * 4u,
                        producerMetadata
                            .errorBudgetNonFiniteGroupCount);
                    AtomicAdd(
                        CounterErrorBudgetReasonsOffset +
                            ErrorBudgetReasonHdrLimitExceeded * 4u,
                        producerMetadata.errorBudgetHdrGroupCount);
                    AtomicAdd(
                        CounterErrorBudgetReasonsOffset +
                            ErrorBudgetReasonDisplayLimitExceeded * 4u,
                        producerMetadata.errorBudgetDisplayGroupCount);
                    AtomicAdd(
                        CounterErrorBudgetRejectedGroupsOffset,
                        rejectedGroupCount);
                    AtomicAdd(
                        CounterErrorBudgetRejectedSamplesOffset,
                        producerMetadata.errorBudgetRejectedSampleCount);
                }
            }
        }
        else if ((g_ExecutionFlags & ExecuteReferencePerSample) != 0u &&
            (g_CounterValidityMask & CounterLightingValid) != 0u)
        {
            AtomicAdd(
                CounterReferenceEvaluationsOffset,
                referenceSampleCount);
            AtomicAdd(
                CounterPerformedEvaluationsOffset,
                referenceSampleCount);
            // Reference performs every covered sample evaluation, so its
            // saved-evaluation count is exactly zero.
        }
    }

    if ((g_ExecutionFlags & ExecuteSelectedPixel) != 0u &&
        dispatchThreadId.x == g_SelectedPixelX &&
        dispatchThreadId.y == g_SelectedPixelY)
    {
        [unroll]
        for (uint sampleIndex = 0u;
            sampleIndex < UVSR_DIAGNOSTICS_MSAA_SAMPLES;
            ++sampleIndex)
        {
            const uint2 key = keys[sampleIndex];
            uint validFields = SelectedRecordKeySupplied;
            if (keyReasons[sampleIndex] == FallbackReasonNone)
            {
                validFields |= SelectedRecordCovered |
                    SelectedRecordKeyValid;
            }
            else if (!backgroundSamples[sampleIndex])
            {
                validFields |= SelectedRecordCovered;
            }
            if (isfinite(depths[sampleIndex]) &&
                depths[sampleIndex] >= 0.0f &&
                depths[sampleIndex] <= 1.0f)
                validFields |= SelectedRecordDepthValid;

            SelectedSampleInput supplied = (SelectedSampleInput)0;
            if ((g_InputAvailabilityMask & InputSelectedSampleData) != 0u)
            {
                supplied = t_SelectedSampleData[sampleIndex];
                if ((supplied.validFields &
                        SampleInputSuppliedBarycentrics) != 0u)
                {
                    validFields |= SelectedRecordSuppliedBarycentrics;
                }
                if ((supplied.validFields &
                        SampleInputReconstructedBarycentrics) != 0u)
                {
                    validFields |=
                        SelectedRecordReconstructedBarycentrics;
                }
                if ((supplied.validFields & SampleInputRadiance) != 0u)
                    validFields |= SelectedRecordRadiance;
                if ((supplied.validFields &
                        SampleInputFrontFaceValid) != 0u)
                {
                    validFields |= SelectedRecordFrontFaceValid;
                    if ((supplied.validFields & SampleInputFrontFace) != 0u)
                        validFields |= SelectedRecordFrontFace;
                }
                if ((supplied.validFields &
                        SampleInputShadingGroup) != 0u &&
                    supplied.shadingGroupIndex < g_SampleCount &&
                    supplied.representativeSampleIndex < g_SampleCount)
                {
                    validFields |= SelectedRecordGroupValid;
                }
            }
            if ((g_InputAvailabilityMask &
                    InputRasterSurfaceHits) != 0u)
            {
                const float4 rasterHit =
                    t_RasterSurfaceHits.Load(pixel, sampleIndex);
                const bool validRasterHit = rasterHit.w == 1.0f &&
                    all(isfinite(rasterHit)) &&
                    rasterHit.x >= 0.0f && rasterHit.y >= 0.0f &&
                    rasterHit.x + rasterHit.y <= 1.00001f;
                if (validRasterHit &&
                    (supplied.validFields &
                        SampleInputSuppliedBarycentrics) == 0u)
                {
                    supplied.validFields |=
                        SampleInputSuppliedBarycentrics;
                    supplied.suppliedBarycentricX = rasterHit.x;
                    supplied.suppliedBarycentricY = rasterHit.y;
                    validFields |= SelectedRecordSuppliedBarycentrics;
                }
                if (validRasterHit &&
                    (supplied.validFields &
                        SampleInputFrontFaceValid) == 0u)
                {
                    supplied.validFields |= SampleInputFrontFaceValid;
                    if (rasterHit.z > 0.5f)
                        supplied.validFields |= SampleInputFrontFace;
                    validFields |= SelectedRecordFrontFaceValid;
                    if (rasterHit.z > 0.5f)
                        validFields |= SelectedRecordFrontFace;
                }
            }
            if ((g_InputAvailabilityMask &
                    InputSelectedSampleData) == 0u &&
                (g_ExecutionFlags & ExecuteReferencePerSample) != 0u &&
                (validFields & SelectedRecordKeyValid) != 0u)
            {
                // The unchanged per-sample Reference path has no adaptive
                // grouping producer. Represent each covered reference sample
                // as its own deterministic final group so the common selected-
                // pixel ABI remains complete without claiming shared shading.
                uint singletonGroup = 0u;
                [unroll]
                for (uint previous = 0u;
                    previous < UVSR_DIAGNOSTICS_MSAA_SAMPLES;
                    ++previous)
                {
                    if (previous < sampleIndex &&
                        keyReasons[previous] == FallbackReasonNone)
                    {
                        ++singletonGroup;
                    }
                }
                supplied.validFields |= SampleInputShadingGroup;
                supplied.shadingGroupIndex = singletonGroup;
                supplied.representativeSampleIndex = sampleIndex;
                validFields |= SelectedRecordGroupValid;
            }

            uint fallbackReason = InvalidWord;
            uint errorBudgetReason = InvalidWord;
            if ((g_InputAvailabilityMask & InputSampleStatus) != 0u)
            {
                fallbackReason = statuses[sampleIndex].x ==
                        StatusUnavailableFallback
                    ? InvalidWord
                    : statuses[sampleIndex].x;
                errorBudgetReason =
                    statuses[sampleIndex].y ==
                        StatusUnavailableErrorBudget
                    ? InvalidWord
                    : statuses[sampleIndex].y;
            }
            if ((g_InputAvailabilityMask &
                    InputSelectedSampleData) != 0u)
            {
                if (fallbackReason == InvalidWord &&
                    (supplied.validFields &
                        SampleInputFallbackReason) != 0u)
                {
                    fallbackReason = supplied.fallbackReason;
                }
                if (errorBudgetReason == InvalidWord &&
                    (supplied.validFields &
                        SampleInputErrorBudgetReason) != 0u)
                {
                    errorBudgetReason = supplied.errorBudgetReason;
                }
            }
            if ((fallbackReason == InvalidWord ||
                    fallbackReason == FallbackReasonNone) &&
                !backgroundSamples[sampleIndex] &&
                keyReasons[sampleIndex] != FallbackReasonNone)
            {
                fallbackReason = keyReasons[sampleIndex];
            }
            if (fallbackReason < FallbackReasonCount)
                validFields |= SelectedRecordFallbackReason;
            if (errorBudgetReason < ErrorBudgetReasonCount)
                validFields |= SelectedRecordErrorBudgetReason;

            uint groupIndex = InvalidWord;
            uint representative = InvalidWord;
            float weight = 1.0f / float(g_SampleCount);
            validFields |= SelectedRecordWeightValid;
            if ((validFields & SelectedRecordGroupValid) != 0u)
            {
                groupIndex = supplied.shadingGroupIndex;
                representative = supplied.representativeSampleIndex;
                uint finalGroupSize =
                    (g_ExecutionFlags &
                        ExecuteReferencePerSample) != 0u
                    ? 1u
                    : 0u;
                if ((g_InputAvailabilityMask &
                        InputSelectedSampleData) != 0u)
                {
                    [unroll]
                    for (uint member = 0u;
                        member < UVSR_DIAGNOSTICS_MSAA_SAMPLES;
                        ++member)
                    {
                        const SelectedSampleInput memberRecord =
                            t_SelectedSampleData[member];
                        if ((memberRecord.validFields &
                                SampleInputShadingGroup) != 0u &&
                            memberRecord.shadingGroupIndex == groupIndex)
                        {
                            ++finalGroupSize;
                        }
                    }
                }
                if (finalGroupSize == 0u)
                {
                    validFields &= ~SelectedRecordGroupValid;
                    groupIndex = InvalidWord;
                    representative = InvalidWord;
                }
                else
                {
                    weight = float(finalGroupSize) /
                        float(g_SampleCount);
                }
            }

            StoreSelectedWord(sampleIndex, 0u, key.x);
            StoreSelectedWord(sampleIndex, 1u, key.y);
            StoreSelectedWord(
                sampleIndex,
                2u,
                isfinite(depths[sampleIndex])
                    ? asuint(depths[sampleIndex])
                    : InvalidFloatBits);
            StoreSelectedWord(sampleIndex, 3u, validFields);
            StoreSelectedWord(
                sampleIndex,
                4u,
                (validFields & SelectedRecordSuppliedBarycentrics) != 0u
                    ? asuint(supplied.suppliedBarycentricX)
                    : InvalidFloatBits);
            StoreSelectedWord(
                sampleIndex,
                5u,
                (validFields & SelectedRecordSuppliedBarycentrics) != 0u
                    ? asuint(supplied.suppliedBarycentricY)
                    : InvalidFloatBits);
            StoreSelectedWord(
                sampleIndex,
                6u,
                (validFields &
                    SelectedRecordReconstructedBarycentrics) != 0u
                    ? asuint(supplied.reconstructedBarycentricX)
                    : InvalidFloatBits);
            StoreSelectedWord(
                sampleIndex,
                7u,
                (validFields &
                    SelectedRecordReconstructedBarycentrics) != 0u
                    ? asuint(supplied.reconstructedBarycentricY)
                    : InvalidFloatBits);
            StoreSelectedWord(sampleIndex, 8u, groupIndex);
            StoreSelectedWord(sampleIndex, 9u, representative);
            StoreSelectedWord(sampleIndex, 10u, asuint(weight));
            StoreSelectedWord(sampleIndex, 11u, fallbackReason);
            StoreSelectedWord(sampleIndex, 12u, errorBudgetReason);
            StoreSelectedWord(
                sampleIndex,
                13u,
                (validFields & SelectedRecordRadiance) != 0u
                    ? asuint(supplied.radianceR)
                    : InvalidFloatBits);
            StoreSelectedWord(
                sampleIndex,
                14u,
                (validFields & SelectedRecordRadiance) != 0u
                    ? asuint(supplied.radianceG)
                    : InvalidFloatBits);
            StoreSelectedWord(
                sampleIndex,
                15u,
                (validFields & SelectedRecordRadiance) != 0u
                    ? asuint(supplied.radianceB)
                    : InvalidFloatBits);
        }
    }

    if (dispatchThreadId.x == 0u && dispatchThreadId.y == 0u)
    {
        // These four words are the shader-authored completion cookie. CPU
        // initialization leaves an impossible ABI sentinel and zero sizes;
        // only current-layout DXIL can make the copied readback authoritative.
        u_Readback.Store(CounterAbiVersionOffset, DiagnosticsGpuAbiVersion);
        u_Readback.Store(CounterByteSizeOffset, CounterByteSize);
        u_Readback.Store(
            SelectedHeaderAbiVersionOffset,
            DiagnosticsGpuAbiVersion);
        u_Readback.Store(
            SelectedHeaderByteSizeOffset,
            SelectedHeaderAndRecordsByteSize);
        if ((g_ExecutionFlags & ExecuteCounters) != 0u)
            AtomicOr(CounterFlagsOffset, CounterDispatchComplete);
        if ((g_ExecutionFlags & ExecuteSelectedPixel) != 0u)
        {
            AtomicOr(
                SelectedHeaderFlagsOffset,
                SelectedHeaderDispatchComplete);
        }
    }

    float referenceReconstructionDepth = 0.0f;
    float reconstructedDepth = 0.0f;
    float4 referenceDiffuse = 0.0f;
    float4 reconstructedDiffuse = 0.0f;
    float4 referenceMaterial = 0.0f;
    float4 reconstructedMaterial = 0.0f;
    float4 referenceNormals = 0.0f;
    float4 reconstructedNormals = 0.0f;
    float4 referenceEmissive = 0.0f;
    float4 reconstructedEmissive = 0.0f;
    float referenceAmbientOcclusion = 0.0f;
    float reconstructedAmbientOcclusion = 0.0f;
    float4 referenceMotion = 0.0f;
    float4 reconstructedMotion = 0.0f;
    float4 reconstructionError0 = 0.0f;
    float4 reconstructionError1 = 0.0f;
    float reconstructionNormalizedError = 0.0f;
    uint errorValidity = 0u;

    if ((g_InputAvailabilityMask & InputCompleteReconstruction) ==
        InputCompleteReconstruction)
    {
        referenceReconstructionDepth = t_ReferenceDepth[pixel];
        reconstructedDepth = t_ReconstructedDepth[pixel];
        referenceDiffuse = t_ReferenceDiffuse[pixel];
        reconstructedDiffuse = t_ReconstructedDiffuse[pixel];
        referenceMaterial = t_ReferenceMaterial[pixel];
        reconstructedMaterial = t_ReconstructedMaterial[pixel];
        referenceNormals = t_ReferenceNormals[pixel];
        reconstructedNormals = t_ReconstructedNormals[pixel];
        referenceEmissive = t_ReferenceEmissive[pixel];
        reconstructedEmissive = t_ReconstructedEmissive[pixel];
        referenceAmbientOcclusion =
            t_ReferenceAmbientOcclusion[pixel];
        reconstructedAmbientOcclusion =
            t_ReconstructedAmbientOcclusion[pixel];
        referenceMotion = t_ReferenceMotion[pixel];
        reconstructedMotion = t_ReconstructedMotion[pixel];

        const bool reconstructionFinite =
            isfinite(referenceReconstructionDepth) &&
            isfinite(reconstructedDepth) &&
            IsFinite4(referenceDiffuse) &&
            IsFinite4(reconstructedDiffuse) &&
            IsFinite4(referenceMaterial) &&
            IsFinite4(reconstructedMaterial) &&
            IsFinite4(referenceNormals) &&
            IsFinite4(reconstructedNormals) &&
            IsFinite4(referenceEmissive) &&
            IsFinite4(reconstructedEmissive) &&
            isfinite(referenceAmbientOcclusion) &&
            isfinite(reconstructedAmbientOcclusion) &&
            IsFinite4(referenceMotion) &&
            IsFinite4(reconstructedMotion);
        if (reconstructionFinite)
        {
            reconstructionError0 = float4(
                abs(referenceReconstructionDepth - reconstructedDepth),
                MaximumAbsoluteDifference(
                    referenceDiffuse, reconstructedDiffuse),
                MaximumAbsoluteDifference(
                    referenceMaterial, reconstructedMaterial),
                MaximumAbsoluteDifference(
                    referenceNormals, reconstructedNormals));
            reconstructionError1 = float4(
                RelativeHdrError(
                    reconstructedEmissive.rgb,
                    referenceEmissive.rgb),
                abs(referenceAmbientOcclusion -
                    reconstructedAmbientOcclusion),
                MaximumAbsoluteDifference(
                    referenceMotion, reconstructedMotion),
                0.0f);
            reconstructionNormalizedError = max(
                max(
                    reconstructionError0.x /
                        ReconstructionDepthThreshold,
                    reconstructionError0.y /
                        ReconstructionDiffuseThreshold),
                max(
                    max(
                        reconstructionError0.z /
                            ReconstructionMaterialThreshold,
                        reconstructionError0.w /
                            ReconstructionNormalThreshold),
                    max(
                        reconstructionError1.x /
                            ReconstructionEmissiveThreshold,
                        max(
                            reconstructionError1.y /
                                ReconstructionAmbientOcclusionThreshold,
                            reconstructionError1.z /
                                ReconstructionMotionThreshold))));
            reconstructionError1.w = reconstructionNormalizedError;
            errorValidity |= ErrorReconstructionValid;
        }
    }

    float4 referenceHdr = 0.0f;
    float4 adaptiveHdr = 0.0f;
    float lightingSpatialError = 0.0f;
    float lightingTemporalError = 0.0f;
    const bool lightingPairAvailable =
        (g_InputAvailabilityMask &
            (InputReferenceHdr | InputAdaptiveHdr)) ==
        (InputReferenceHdr | InputAdaptiveHdr);
    // Read and write are deliberately independent. A frozen temporal
    // diagnostic binds the last submission-confirmed history and sets only
    // HistoryRead; it must not mutate the opposite ping-pong surface.
    const bool historyReadRequested =
        (g_ErrorExecutionFlags & ErrorExecuteHistoryRead) != 0u &&
        g_HistoryValid != 0u;
    const bool historyWriteRequested =
        (g_ErrorExecutionFlags & ErrorExecuteHistoryWrite) != 0u;
    if (lightingPairAvailable)
    {
        referenceHdr = t_ReferenceHdr[pixel];
        adaptiveHdr = t_AdaptiveHdr[pixel];
        if (IsFinite4(referenceHdr) && IsFinite4(adaptiveHdr))
        {
            lightingSpatialError = RelativeHdrError(
                adaptiveHdr.rgb,
                referenceHdr.rgb);
            errorValidity |= ErrorLightingSpatialValid;

            if (historyReadRequested)
            {
                const float4 previousReference =
                    t_PreviousReferenceHdr[pixel];
                const float4 previousAdaptive =
                    t_PreviousAdaptiveHdr[pixel];
                if (IsFinite4(previousReference) &&
                    IsFinite4(previousAdaptive))
                {
                    lightingTemporalError = TemporalHdrError(
                        adaptiveHdr.rgb,
                        referenceHdr.rgb,
                        previousAdaptive.rgb,
                        previousReference.rgb);
                    errorValidity |= ErrorLightingTemporalValid;
                }
            }
        }
        if (historyWriteRequested)
        {
            u_NextReferenceHdr[pixel] = referenceHdr;
            u_NextAdaptiveHdr[pixel] = adaptiveHdr;
            if (!historyReadRequested)
                errorValidity |= ErrorHistoryReset;
        }
    }

    if ((g_ErrorExecutionFlags & ErrorExecuteMetrics) != 0u)
    {
        u_ReconstructionError0[pixel] = reconstructionError0;
        u_ReconstructionError1[pixel] = reconstructionError1;
        u_LightingError[pixel] = float4(
            lightingSpatialError,
            lightingTemporalError,
            lightingSpatialError / (1.0f / 1024.0f),
            lightingTemporalError / (1.0f / 1024.0f));
        u_ErrorValidity[pixel] = errorValidity;
    }

    if ((g_ExecutionFlags & ExecuteVisualization) == 0u)
        return;

    if ((g_ExecutionFlags & ExecuteViewAvailable) == 0u)
    {
        u_Visualization[pixel] = float4(1.0f, 0.0f, 1.0f, 1.0f);
        return;
    }

    float3 output = 0.0f;
    if (g_RequestedView == DebugViewSurfaceOwnershipKeys)
    {
        const uint2 key = keys[g_VisualizedSampleIndex];
        if (backgroundSamples[g_VisualizedSampleIndex])
            output = 0.0f;
        else if (keyReasons[g_VisualizedSampleIndex] != FallbackReasonNone)
            output = float3(1.0f, 0.0f, 1.0f);
        else
            output = HashColor(key);
    }
    else if (g_RequestedView ==
        DebugViewUniqueGroupsAndRepresentatives)
    {
        if (shadingGroupCount == InvalidWord)
        {
            output = float3(1.0f, 0.0f, 1.0f);
        }
        else
        {
            const bool representative =
                (shadingRepresentativeMask &
                    (1u << g_VisualizedSampleIndex)) != 0u;
            output = float3(
                float(shadingGroupCount) / float(g_SampleCount),
                representative ? 1.0f : 0.15f,
                float(g_VisualizedSampleIndex + 1u) /
                    float(g_SampleCount));
        }
    }
    else if (g_RequestedView == DebugViewFallbackReasons)
    {
        uint reason = statuses[g_VisualizedSampleIndex].x;
        if (!backgroundSamples[g_VisualizedSampleIndex] &&
            keyReasons[g_VisualizedSampleIndex] != FallbackReasonNone &&
            (reason == StatusUnavailableFallback ||
                reason == FallbackReasonNone))
        {
            reason = keyReasons[g_VisualizedSampleIndex];
        }
        output = ReasonColor(reason);
    }
    else if (g_RequestedView == DebugViewCompactReconstructionError)
    {
        if ((errorValidity & ErrorReconstructionValid) == 0u)
        {
            output = float3(1.0f, 0.0f, 1.0f);
        }
        else if (g_ComparisonMode == ComparisonSplitScreen)
        {
            const float3 reference = SelectReconstructionReference(
                g_ReconstructionChannel,
                referenceReconstructionDepth,
                referenceDiffuse,
                referenceMaterial,
                referenceNormals,
                referenceEmissive,
                referenceAmbientOcclusion,
                referenceMotion);
            const float3 reconstructed = SelectReconstructionCandidate(
                g_ReconstructionChannel,
                reconstructedDepth,
                reconstructedDiffuse,
                reconstructedMaterial,
                reconstructedNormals,
                reconstructedEmissive,
                reconstructedAmbientOcclusion,
                reconstructedMotion);
            output = dispatchThreadId.x < g_Width / 2u
                ? reference
                : reconstructed;
        }
        else if (g_ComparisonMode == ComparisonErrorHeatmap)
        {
            // One means the selected reconstruction threshold is reached.
            float normalizedError = reconstructionNormalizedError;
            if (g_ReconstructionChannel == ReconstructionChannelDepth)
                normalizedError = reconstructionError0.x /
                    ReconstructionDepthThreshold;
            else if (g_ReconstructionChannel ==
                ReconstructionChannelDiffuse)
                normalizedError = reconstructionError0.y /
                    ReconstructionDiffuseThreshold;
            else if (g_ReconstructionChannel ==
                ReconstructionChannelMaterial)
                normalizedError = reconstructionError0.z /
                    ReconstructionMaterialThreshold;
            else if (g_ReconstructionChannel ==
                ReconstructionChannelNormals)
                normalizedError = reconstructionError0.w /
                    ReconstructionNormalThreshold;
            else if (g_ReconstructionChannel ==
                ReconstructionChannelEmissive)
                normalizedError = reconstructionError1.x /
                    ReconstructionEmissiveThreshold;
            else if (g_ReconstructionChannel ==
                ReconstructionChannelAmbientOcclusion)
                normalizedError = reconstructionError1.y /
                    ReconstructionAmbientOcclusionThreshold;
            else if (g_ReconstructionChannel ==
                ReconstructionChannelMotion)
                normalizedError = reconstructionError1.z /
                    ReconstructionMotionThreshold;
            output = ErrorHeat(normalizedError / 32.0f);
        }
        else
        {
            output = float3(1.0f, 0.0f, 1.0f);
        }
    }
    else if (g_RequestedView == DebugViewAdaptiveLightingError)
    {
        if (g_ComparisonMode == ComparisonSplitScreen)
        {
            if ((errorValidity & ErrorLightingSpatialValid) == 0u)
            {
                output = float3(1.0f, 0.0f, 1.0f);
            }
            else
            {
                output = dispatchThreadId.x < g_Width / 2u
                    ? referenceHdr.rgb
                    : adaptiveHdr.rgb;
            }
        }
        else if (g_ComparisonMode == ComparisonErrorHeatmap)
        {
            const uint requestedValidity = g_ErrorDomain ==
                    ErrorDomainFrameToFrameTemporal
                ? ErrorLightingTemporalValid
                : ErrorLightingSpatialValid;
            if ((errorValidity & requestedValidity) == 0u)
            {
                output = float3(1.0f, 0.0f, 1.0f);
            }
            else
            {
                output = ErrorHeat(g_ErrorDomain ==
                        ErrorDomainFrameToFrameTemporal
                    ? lightingTemporalError
                    : lightingSpatialError);
            }
        }
        else
        {
            output = float3(1.0f, 0.0f, 1.0f);
        }
    }
    else
    {
        output = float3(1.0f, 0.0f, 1.0f);
    }

    u_Visualization[pixel] = float4(output, 1.0f);
}
