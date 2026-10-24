#ifndef UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER_HLSLI
#define UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER_HLSLI

// Diagnostic-only producer ABI. Accepted benchmark permutations compile with
// UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER=0 and therefore contain none of
// these resources, branches, or writes.

static const uint DiagnosticsProducerWriteSampleStatus = 1u << 0u;
static const uint DiagnosticsProducerWritePixelMetadata = 1u << 1u;
static const uint DiagnosticsProducerWriteSelectedSamples = 1u << 2u;
static const uint DiagnosticsProducerValidateErrorBudget = 1u << 3u;

static const uint DiagnosticsSampleSuppliedBarycentrics = 1u << 0u;
static const uint DiagnosticsSampleReconstructedBarycentrics = 1u << 1u;
static const uint DiagnosticsSampleRadiance = 1u << 2u;
static const uint DiagnosticsSampleFallbackReason = 1u << 3u;
static const uint DiagnosticsSampleErrorBudgetReason = 1u << 4u;
static const uint DiagnosticsSampleFrontFaceValid = 1u << 5u;
static const uint DiagnosticsSampleFrontFace = 1u << 6u;
static const uint DiagnosticsSampleShadingGroup = 1u << 7u;

static const uint DiagnosticsPixelLightingValid = 1u << 0u;
static const uint DiagnosticsPixelWorkListValid = 1u << 1u;
static const uint DiagnosticsPixelWorkListOverflow = 1u << 2u;
static const uint DiagnosticsPixelGroupCountShift = 3u;
static const uint DiagnosticsPixelRepresentativeMaskShift = 8u;
static const uint DiagnosticsPixelGroupDataValid = 1u << 24u;
static const uint DiagnosticsPixelErrorBudgetValid = 1u << 25u;

static const uint DiagnosticsUnavailableWord = 0xffffffffu;
static const uint DiagnosticsUnavailableFallbackReason = 0x1fu;
static const uint DiagnosticsUnavailableErrorReason = 0x7u;
static const uint DiagnosticsFallbackReasonCount = 24u;
static const uint DiagnosticsErrorReasonCount = 4u;
static const uint DiagnosticsErrorReasonNone = 0u;
static const uint DiagnosticsErrorReasonNonFinite = 1u;
static const uint DiagnosticsErrorReasonHdrLimitExceeded = 2u;
static const uint DiagnosticsErrorReasonDisplayLimitExceeded = 3u;
static const float DiagnosticsHdrDenominatorFloor = 1.0f / 1024.0f;
static const float DiagnosticsHdrGroupLimit = 1.0f / 1024.0f;
static const float DiagnosticsDisplayGroupLimit = 0.5f;
static const float DiagnosticsDisplayCodeScale = 255.0f;

cbuffer c_AdaptiveDiagnosticsProducer : register(b2)
{
    uint g_DiagnosticsExecutionFlags;
    uint g_DiagnosticsSelectedPixelX;
    uint g_DiagnosticsSelectedPixelY;
    uint g_DiagnosticsProducerCapabilityMask;
    uint g_DiagnosticsViewportOriginX;
    uint g_DiagnosticsViewportOriginY;
    uint g_DiagnosticsViewportWidth;
    uint g_DiagnosticsViewportHeight;
};

struct DiagnosticsGpuSampleInputRecord
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

struct DiagnosticsGpuPixelMetadataRecord
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

Texture2DMS<float4, UVSR_ADAPTIVE_MSAA_DIAGNOSTIC_SAMPLES>
    t_DiagnosticsMsaaBackground : register(t25);
RWTexture2DArray<uint> u_DiagnosticsSampleStatus : register(u1);
RWStructuredBuffer<DiagnosticsGpuSampleInputRecord>
    u_DiagnosticsSelectedSamples : register(u2);
RWStructuredBuffer<DiagnosticsGpuPixelMetadataRecord>
    u_DiagnosticsPixelMetadata : register(u3);

bool DiagnosticsProducerPixelInViewport(int2 pixelPosition)
{
    const int2 relative = pixelPosition - int2(
        g_DiagnosticsViewportOriginX,
        g_DiagnosticsViewportOriginY);
    return all(relative >= 0) &&
        all(relative < int2(
            g_DiagnosticsViewportWidth,
            g_DiagnosticsViewportHeight));
}

uint DiagnosticsProducerLinearPixel(int2 pixelPosition)
{
    const uint2 relative = uint2(pixelPosition - int2(
        g_DiagnosticsViewportOriginX,
        g_DiagnosticsViewportOriginY));
    return relative.y * g_DiagnosticsViewportWidth + relative.x;
}

bool DiagnosticsProducerIsSelectedPixel(int2 pixelPosition)
{
    return (g_DiagnosticsExecutionFlags &
            DiagnosticsProducerWriteSelectedSamples) != 0u &&
        all(uint2(pixelPosition) == uint2(
            g_DiagnosticsSelectedPixelX,
            g_DiagnosticsSelectedPixelY));
}

uint DiagnosticsProducerPackStatus(
    uint fallbackReason,
    uint errorReason)
{
    const bool fallbackReasonValid =
        fallbackReason < DiagnosticsFallbackReasonCount ||
        fallbackReason == DiagnosticsUnavailableFallbackReason;
    const bool errorReasonValid =
        errorReason < DiagnosticsErrorReasonCount ||
        errorReason == DiagnosticsUnavailableErrorReason;
    // Match PackDiagnosticsGpuSampleStatus exactly: an invalid enum is an
    // ABI failure, so both packed families become unavailable. The explicit
    // unavailable sentinel for one family remains independently representable.
    if (!fallbackReasonValid || !errorReasonValid)
    {
        fallbackReason = DiagnosticsUnavailableFallbackReason;
        errorReason = DiagnosticsUnavailableErrorReason;
    }
    return (fallbackReason & 0x1fu) |
        ((errorReason & 0x7u) << 5u);
}

uint DiagnosticsProducerPackGroupData(
    uint groupCount,
    uint representativeMask)
{
    return groupCount <= UVSR_ADAPTIVE_MSAA_DIAGNOSTIC_SAMPLES
        ? DiagnosticsPixelGroupDataValid |
            (groupCount << DiagnosticsPixelGroupCountShift) |
            ((representativeMask & 0xffffu) <<
                DiagnosticsPixelRepresentativeMaskShift)
        : 0u;
}

float DiagnosticsProducerHdrSpatialError(
    float3 adaptive,
    float3 reference)
{
    const float3 denominator = max(
        DiagnosticsHdrDenominatorFloor.xxx,
        max(abs(adaptive), abs(reference)));
    const float3 relativeError =
        abs(adaptive - reference) / denominator;
    return max(
        relativeError.x,
        max(relativeError.y, relativeError.z));
}

float3 DiagnosticsProducerAgxDisplayEncode(float3 hdr)
{
    // Exact fixed UVSR display path from agx_tonemapping_ps.hlsl and
    // display_output_ps.hlsl, evaluated before presentation dithering.
    static const float minimumEv = -12.47393f;
    static const float maximumEv = 4.026069f;
    static const float3x3 inset = float3x3(
        0.842479062253094f, 0.0784335999999992f, 0.0792237451477643f,
        0.0423282422610123f, 0.878468636469772f, 0.0791661274605434f,
        0.0423756549057051f, 0.0784336f, 0.879142973793104f);
    static const float3x3 outset = float3x3(
        1.19687900512017f, -0.0980208811401368f, -0.0990297440797205f,
        -0.0528968517574562f, 1.15190312990417f, -0.0989611768448433f,
        -0.0529716355144438f, -0.0980434501171241f, 1.15107367264116f);
    float3 encoded = mul(inset, max(hdr, 0.0f));
    encoded = saturate(
        (log2(max(encoded, 1.0e-10f)) - minimumEv) /
        (maximumEv - minimumEv));
    const float3 encoded2 = encoded * encoded;
    const float3 encoded4 = encoded2 * encoded2;
    encoded = 15.5f * encoded4 * encoded2
        - 40.14f * encoded4 * encoded
        + 31.96f * encoded4
        - 6.868f * encoded2 * encoded
        + 0.4298f * encoded2
        + 0.1191f * encoded
        - 0.00232f;
    const float3 displayLinear = saturate(mul(outset, saturate(encoded)));
    const float3 low = displayLinear * 12.92f;
    const float3 high = 1.055f * pow(
        max(displayLinear, 0.0f), 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, displayLinear <= 0.0031308f);
}

float DiagnosticsProducerDisplaySpatialError(
    float3 adaptive,
    float3 reference)
{
    const float3 adaptiveDisplay =
        DiagnosticsProducerAgxDisplayEncode(adaptive);
    const float3 referenceDisplay =
        DiagnosticsProducerAgxDisplayEncode(reference);
    const float3 codeError = abs(
        adaptiveDisplay - referenceDisplay) *
        DiagnosticsDisplayCodeScale;
    return max(codeError.x, max(codeError.y, codeError.z));
}

DiagnosticsGpuSampleInputRecord DiagnosticsProducerEmptySample()
{
    DiagnosticsGpuSampleInputRecord result =
        (DiagnosticsGpuSampleInputRecord)0;
    result.fallbackReason = DiagnosticsUnavailableWord;
    result.errorBudgetReason = DiagnosticsUnavailableWord;
    result.shadingGroupIndex = DiagnosticsUnavailableWord;
    result.representativeSampleIndex = DiagnosticsUnavailableWord;
    return result;
}

void DiagnosticsProducerStoreStatus(
    int2 pixelPosition,
    uint sampleIndex,
    uint fallbackReason,
    uint errorReason)
{
    if ((g_DiagnosticsExecutionFlags &
            DiagnosticsProducerWriteSampleStatus) == 0u)
    {
        return;
    }
    u_DiagnosticsSampleStatus[uint3(
        uint2(pixelPosition), sampleIndex)] =
            DiagnosticsProducerPackStatus(fallbackReason, errorReason);
}

void DiagnosticsProducerStoreSelected(
    int2 pixelPosition,
    uint sampleIndex,
    DiagnosticsGpuSampleInputRecord record)
{
    if (DiagnosticsProducerIsSelectedPixel(pixelPosition))
        u_DiagnosticsSelectedSamples[sampleIndex] = record;
}

void DiagnosticsProducerStorePixelMetadata(
    int2 pixelPosition,
    uint groupCount,
    uint representativeMask,
    uint performedLightingEvaluations)
{
    if ((g_DiagnosticsExecutionFlags &
            DiagnosticsProducerWritePixelMetadata) == 0u ||
        !DiagnosticsProducerPixelInViewport(pixelPosition))
    {
        return;
    }
    DiagnosticsGpuPixelMetadataRecord metadata =
        (DiagnosticsGpuPixelMetadataRecord)0;
    metadata.validFields = DiagnosticsPixelLightingValid |
        DiagnosticsProducerPackGroupData(
            groupCount, representativeMask);
    metadata.performedLightingEvaluations =
        performedLightingEvaluations;
    // Work-list fields are published separately by a producer whose selected
    // scheduling topology actually owns a work list. Zero remains invalid for
    // every other producer and permutation.
    u_DiagnosticsPixelMetadata[
        DiagnosticsProducerLinearPixel(pixelPosition)] = metadata;
}

void DiagnosticsProducerStorePixelMetadataWithErrorBudget(
    int2 pixelPosition,
    uint groupCount,
    uint representativeMask,
    uint performedLightingEvaluations,
    uint nonFiniteRejectedGroupCount,
    uint hdrRejectedGroupCount,
    uint displayRejectedGroupCount,
    uint rejectedSampleCount)
{
    if ((g_DiagnosticsExecutionFlags &
            DiagnosticsProducerWritePixelMetadata) == 0u ||
        !DiagnosticsProducerPixelInViewport(pixelPosition))
    {
        return;
    }
    DiagnosticsGpuPixelMetadataRecord metadata =
        (DiagnosticsGpuPixelMetadataRecord)0;
    metadata.validFields = DiagnosticsPixelLightingValid |
        DiagnosticsPixelErrorBudgetValid |
        DiagnosticsProducerPackGroupData(
            groupCount, representativeMask);
    metadata.performedLightingEvaluations =
        performedLightingEvaluations;
    metadata.errorBudgetNonFiniteGroupCount =
        nonFiniteRejectedGroupCount;
    metadata.errorBudgetHdrGroupCount = hdrRejectedGroupCount;
    metadata.errorBudgetDisplayGroupCount =
        displayRejectedGroupCount;
    metadata.errorBudgetRejectedSampleCount = rejectedSampleCount;
    // Work-list fields begin zero and invalid. A separated producer may append
    // its measured values after this store. Error-budget counters have
    // dedicated words so neither family can masquerade as the other.
    u_DiagnosticsPixelMetadata[
        DiagnosticsProducerLinearPixel(pixelPosition)] = metadata;
}

void DiagnosticsProducerInvalidatePixelMetadata(int2 pixelPosition)
{
    if ((g_DiagnosticsExecutionFlags &
            DiagnosticsProducerWritePixelMetadata) == 0u ||
        !DiagnosticsProducerPixelInViewport(pixelPosition))
    {
        return;
    }
    // A covered pixel that could not complete adaptive shading has no
    // authoritative group, representative, lighting, or work-list result.
    // Zero is intentionally an invalid record, not "zero groups saved all
    // evaluations."
    u_DiagnosticsPixelMetadata[
        DiagnosticsProducerLinearPixel(pixelPosition)] =
            (DiagnosticsGpuPixelMetadataRecord)0;
}

#endif // UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER_HLSLI
