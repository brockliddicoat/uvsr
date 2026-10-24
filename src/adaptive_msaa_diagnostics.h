#pragma once

#include "adaptive_msaa.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace uvsr::adaptive_msaa
{
    inline constexpr uint32_t DiagnosticsGroupHistogramBinCount =
        MaximumAdaptiveSampleCount + 1u;
    inline constexpr uint32_t InvalidDiagnosticsSampleIndex = 0xffffffffu;
    inline constexpr uint64_t InvalidDiagnosticsFrameIndex =
        0xffffffffffffffffull;
    inline constexpr size_t DiagnosticsFallbackReasonBinCount =
        static_cast<size_t>(FallbackReason::Count);
    inline constexpr float DiagnosticsHdrDenominatorFloor =
        1.f / 1024.f;
    inline constexpr float DiagnosticsHdrPercentile99Limit =
        1.f / 1024.f;
    inline constexpr float DiagnosticsHdrGroupLimit = 1.f / 1024.f;
    inline constexpr float DiagnosticsHdrMaximumLimit =
        1.f / 128.f;
    inline constexpr float DiagnosticsDisplayPercentile99Limit = 0.5f;
    inline constexpr float DiagnosticsDisplayMaximumLimit = 2.f;
    inline constexpr float DiagnosticsDisplayCodeScale = 255.f;

    enum class DiagnosticsComparisonMode : uint32_t
    {
        Disabled = 0u,
        SplitScreen = 1u,
        ErrorHeatmap = 2u,
        Count = 3u
    };

    enum class AdaptiveMsaaDebugView : uint32_t
    {
        Disabled = 0u,
        SurfaceOwnershipKeys = 1u,
        UniqueGroupsAndRepresentatives = 2u,
        FallbackReasons = 3u,
        CompactReconstructionError = 4u,
        AdaptiveLightingError = 5u,
        Count = 6u
    };

    enum class AdaptiveMsaaDiagnosticsPass : uint32_t
    {
        VisibilityRaster = 0u,
        ComplexityClassification = 1u,
        SampleLoading = 2u,
        Grouping = 3u,
        WorkListBuild = 4u,
        CompactReconstruction = 5u,
        InvariantShading = 6u,
        SampleDependentLighting = 7u,
        Resolve = 8u,
        ReferenceComparison = 9u,
        Count = 10u
    };

    enum class DiagnosticsErrorDomain : uint32_t
    {
        Spatial = 0u,
        Temporal = 1u,
        Count = 2u
    };

    enum class DiagnosticsReconstructionChannel : uint32_t
    {
        Combined = 0u,
        Depth = 1u,
        Diffuse = 2u,
        Material = 3u,
        Normals = 4u,
        Emissive = 5u,
        AmbientOcclusion = 6u,
        Motion = 7u,
        Count = 8u
    };

    enum class DiagnosticsErrorValueDomain : uint32_t
    {
        Hdr = 0u,
        Display = 1u,
        Count = 2u
    };

    enum class DiagnosticsErrorBudgetRejectionReason : uint32_t
    {
        None = 0u,
        NonFiniteValue = 1u,
        HdrLimitExceeded = 2u,
        DisplayLimitExceeded = 3u,
        Count = 4u
    };

    inline constexpr size_t DiagnosticsErrorBudgetRejectionReasonBinCount =
        static_cast<size_t>(
            DiagnosticsErrorBudgetRejectionReason::Count);

    enum class DiagnosticsInputStatus : uint32_t
    {
        Success = 0u,
        InvalidComparisonMode = 1u,
        InvalidDebugView = 2u,
        InvalidPass = 3u,
        InvalidErrorDomain = 4u,
        InvalidFallbackReason = 5u,
        UnsupportedSampleCount = 6u,
        NullInput = 7u,
        EmptyInput = 8u,
        InvalidGroupCount = 9u,
        InvalidSampleIndex = 10u,
        InvalidLightingEvaluationCount = 11u,
        InvalidWorkListObservation = 12u,
        NonFiniteValue = 13u,
        NegativeValue = 14u,
        InvalidErrorValueDomain = 15u,
        InvalidActiveMask = 16u,
        ValueOutOfRange = 17u,
        InvalidErrorBudgetRejectionReason = 18u,
        InvalidReconstructionChannel = 19u,
        Count = 20u
    };

    struct AdaptiveMsaaDiagnosticsSettings
    {
        bool enabled = false;
        DiagnosticsComparisonMode comparisonMode =
            DiagnosticsComparisonMode::Disabled;
        AdaptiveMsaaDebugView debugView = AdaptiveMsaaDebugView::Disabled;
        DiagnosticsErrorDomain errorDomain = DiagnosticsErrorDomain::Spatial;
        DiagnosticsReconstructionChannel reconstructionChannel =
            DiagnosticsReconstructionChannel::Combined;
        bool freezeCameraAndFrame = false;

        // A deterministic, fixed phase for the task-owned MotionTaaSweep
        // validation animation. This is intentionally not a general scene
        // player: it exists only to produce matched moving-instance,
        // disocclusion, motion-vector, and temporal-error captures. Any
        // non-default state remains benchmark-unsafe.
        bool validationAnimationEnabled = false;
        float validationAnimationPhase = 0.f;

        // Capture is an explicit one-shot request. Readback, counters, pass
        // timings, and dual rendering are separate execution gates so an
        // accepted benchmark can prove that each expensive path is absent.
        bool selectedPixelCaptureRequested = false;
        uint32_t selectedPixelX = 0u;
        uint32_t selectedPixelY = 0u;
        uint64_t selectedPixelCaptureId = 0u;
        uint32_t visualizedSampleIndex = 0u;
        bool readbackEnabled = false;
        bool countersEnabled = false;
        bool passTimingsEnabled = false;
        bool dualRenderingEnabled = false;
    };

    [[nodiscard]] DiagnosticsInputStatus SanitizeDiagnosticsSettings(
        const AdaptiveMsaaDiagnosticsSettings& input,
        AdaptiveMsaaDiagnosticsSettings& output);

    // A comparison mode alone does not prove which reference products the
    // renderer must retain. These helpers keep final-lighting comparison,
    // compact reconstruction comparison, and unrelated debug views distinct.
    // A Disabled debug view with an active comparison mode is the
    // comparison-only final-lighting presentation.
    [[nodiscard]] bool RequestsFinalLightingComparison(
        const AdaptiveMsaaDiagnosticsSettings& settings);
    [[nodiscard]] bool RequestsCompactReconstructionComparison(
        const AdaptiveMsaaDiagnosticsSettings& settings);
    [[nodiscard]] bool RequestsAnyReferenceComparison(
        const AdaptiveMsaaDiagnosticsSettings& settings);

    // This predicate is intentionally stricter than the top-level enabled
    // switch. Raw or partially transitioned state is benchmark-safe only when
    // every diagnostic producer and consumer is structurally disabled.
    [[nodiscard]] bool IsAcceptedBenchmarkDiagnosticsSafe(
        const AdaptiveMsaaDiagnosticsSettings& settings);

    struct AdaptiveMsaaDiagnosticsCountersData
    {
        std::array<uint64_t, DiagnosticsGroupHistogramBinCount>
            groupCountHistogram{};
        uint64_t pixelsObserved = 0u;

        uint64_t referenceLightingEvaluations = 0u;
        uint64_t lightingEvaluations = 0u;
        uint64_t lightingEvaluationsSaved = 0u;

        std::array<uint64_t, DiagnosticsFallbackReasonBinCount>
            fallbackReasonCounts{};
        uint64_t invalidKeyCount = 0u;
        uint64_t staleKeyCount = 0u;
        uint64_t mappingFailureCount = 0u;

        std::array<
            uint64_t,
            DiagnosticsErrorBudgetRejectionReasonBinCount>
            errorBudgetRejectionReasonCounts{};
        uint64_t errorBudgetRejectedGroupCount = 0u;
        uint64_t errorBudgetRejectedSampleCount = 0u;

        uint64_t workListObservationCount = 0u;
        uint64_t workListOccupancyTotal = 0u;
        uint64_t workListCapacityTotal = 0u;
        uint64_t workListPeakOccupancy = 0u;
        uint64_t workListOverflowCount = 0u;

        std::array<
            uint64_t,
            static_cast<size_t>(AdaptiveMsaaDiagnosticsPass::Count)>
            passGpuNanoseconds{};
        std::array<
            uint64_t,
            static_cast<size_t>(AdaptiveMsaaDiagnosticsPass::Count)>
            passInvocationCounts{};
    };

    class AdaptiveMsaaDiagnosticsCounters
    {
    public:
        void Reset();

        [[nodiscard]] DiagnosticsInputStatus RecordGroupCount(
            uint32_t groupCount,
            uint64_t pixelCount = 1u);

        [[nodiscard]] DiagnosticsInputStatus RecordLightingEvaluations(
            uint64_t referenceEvaluations,
            uint64_t performedEvaluations);

        [[nodiscard]] DiagnosticsInputStatus RecordFallbackReason(
            FallbackReason reason,
            uint64_t occurrenceCount = 1u);

        [[nodiscard]] DiagnosticsInputStatus RecordErrorBudgetRejection(
            DiagnosticsErrorBudgetRejectionReason reason,
            uint32_t groupSampleCount,
            uint64_t occurrenceCount = 1u);

        [[nodiscard]] DiagnosticsInputStatus RecordWorkListObservation(
            uint64_t occupancy,
            uint64_t capacity,
            bool overflowed,
            uint64_t observationCount = 1u);

        [[nodiscard]] DiagnosticsInputStatus RecordPassTiming(
            AdaptiveMsaaDiagnosticsPass pass,
            uint64_t gpuNanoseconds,
            uint64_t invocationCount = 1u);

        [[nodiscard]] const AdaptiveMsaaDiagnosticsCountersData& Data() const;

    private:
        AdaptiveMsaaDiagnosticsCountersData m_Data;
    };

    // Error values are nonnegative magnitudes in the caller's chosen metric.
    // Percentiles use the deterministic nearest-rank definition after sorting
    // a private copy; the caller-owned input is never modified.
    struct DiagnosticsErrorSummary
    {
        bool valid = false;
        uint64_t sampleCount = 0u;
        float percentile50 = 0.f;
        float percentile90 = 0.f;
        float percentile95 = 0.f;
        float percentile99 = 0.f;
        float maximum = 0.f;
    };

    struct DiagnosticsErrorDomainSummaries
    {
        DiagnosticsErrorSummary spatial;
        DiagnosticsErrorSummary temporal;
    };

    struct AdaptiveMsaaErrorSummaries
    {
        DiagnosticsErrorDomainSummaries hdr;
        DiagnosticsErrorDomainSummaries display;
    };

    struct DiagnosticsErrorLimits
    {
        float percentile99 = 0.f;
        float maximum = 0.f;
    };

    struct DiagnosticsErrorFrameView
    {
        const float* errors = nullptr;
        const uint8_t* activeMask = nullptr;
        size_t valueCount = 0u;
    };

    struct DiagnosticsErrorGateResult
    {
        bool valid = false;
        bool passed = false;
        bool perFramePassed = false;
        bool pooledPassed = false;
        DiagnosticsErrorValueDomain valueDomain =
            DiagnosticsErrorValueDomain::Hdr;
        DiagnosticsErrorDomain errorDomain =
            DiagnosticsErrorDomain::Spatial;
        uint64_t frameCount = 0u;
        uint64_t activeSampleCount = 0u;
        uint64_t failedFrameCount = 0u;
        uint64_t firstFailedFrameIndex = InvalidDiagnosticsFrameIndex;
        float worstFramePercentile99 = 0.f;
        float worstFrameMaximum = 0.f;
        DiagnosticsErrorSummary pooled;
    };

    struct DiagnosticsGroupRejectionDecision
    {
        bool valid = false;
        bool rejectWholeGroup = false;
        DiagnosticsErrorBudgetRejectionReason reason =
            DiagnosticsErrorBudgetRejectionReason::None;
        uint32_t memberCount = 0u;
        uint32_t firstFailingMember = InvalidDiagnosticsSampleIndex;
    };

    // HDR comparisons operate directly on scene-linear values. Spatial error is
    // max_c(|A-R| / max(2^-10, |A|, |R|)). Temporal error applies the same
    // normalization to the signed residual change
    // (A_current-R_current) - (A_previous-R_previous), using all four values in
    // the denominator. Display inputs are normalized encoded RGB after UVSR's
    // exact AgX and linear-to-sRGB transforms, before dithering. Display errors
    // are returned in continuous 8-bit code values rather than quantized
    // integers.
    [[nodiscard]] DiagnosticsInputStatus ComputeDiagnosticsHdrSpatialError(
        const Float3& adaptive,
        const Float3& reference,
        float& output);

    [[nodiscard]] DiagnosticsInputStatus ComputeDiagnosticsHdrTemporalError(
        const Float3& adaptiveCurrent,
        const Float3& referenceCurrent,
        const Float3& adaptivePrevious,
        const Float3& referencePrevious,
        float& output);

    [[nodiscard]] DiagnosticsInputStatus
        ComputeDiagnosticsDisplaySpatialError(
            const Float3& adaptiveDisplay,
            const Float3& referenceDisplay,
            float& output);

    [[nodiscard]] DiagnosticsInputStatus
        ComputeDiagnosticsDisplayTemporalError(
            const Float3& adaptiveDisplayCurrent,
            const Float3& referenceDisplayCurrent,
            const Float3& adaptiveDisplayPrevious,
            const Float3& referenceDisplayPrevious,
            float& output);

    [[nodiscard]] DiagnosticsInputStatus GetDiagnosticsErrorLimits(
        DiagnosticsErrorValueDomain valueDomain,
        DiagnosticsErrorLimits& output);

    [[nodiscard]] DiagnosticsInputStatus SummarizeDiagnosticsErrors(
        const float* errors,
        size_t errorCount,
        DiagnosticsErrorSummary& output);

    [[nodiscard]] DiagnosticsInputStatus SummarizeActiveDiagnosticsErrors(
        const float* errors,
        const uint8_t* activeMask,
        size_t errorCount,
        DiagnosticsErrorSummary& output);

    [[nodiscard]] DiagnosticsInputStatus SetDiagnosticsErrorSummary(
        AdaptiveMsaaErrorSummaries& summaries,
        DiagnosticsErrorValueDomain valueDomain,
        DiagnosticsErrorDomain domain,
        const float* errors,
        size_t errorCount);

    [[nodiscard]] DiagnosticsInputStatus EvaluateDiagnosticsErrorFrames(
        DiagnosticsErrorValueDomain valueDomain,
        DiagnosticsErrorDomain errorDomain,
        const DiagnosticsErrorFrameView* frames,
        size_t frameCount,
        DiagnosticsErrorGateResult& output);

    // This comparison is intentionally pure. The caller may record the
    // returned reason and substitute Reference shading, but the decision never
    // mutates counters or renderer state. Non-finite member errors reject the
    // whole group; finite negative magnitudes remain invalid caller input.
    // Exact limit equality passes, and any strict exceedance rejects. Reason
    // precedence is lowest-index non-finite member, then lowest-index HDR
    // exceedance, then lowest-index display exceedance.
    [[nodiscard]] DiagnosticsInputStatus DecideDiagnosticsGroupRejection(
        const float* hdrMemberErrors,
        const float* displayMemberErrors,
        uint32_t memberCount,
        DiagnosticsGroupRejectionDecision& output);

    // The automatic GPU group gate uses only the immutable scene-linear HDR
    // spatial limit. Keeping this decision separate from the pooled display
    // and temporal gates prevents one domain from silently standing in for
    // another. Non-finite error rejects before a finite threshold exceedance;
    // equality passes and a strict exceedance rejects the complete group.
    [[nodiscard]] DiagnosticsInputStatus DecideDiagnosticsHdrGroupRejection(
        const float* hdrMemberErrors,
        uint32_t memberCount,
        DiagnosticsGroupRejectionDecision& output);

    void ResetDiagnosticsErrorSummaries(
        AdaptiveMsaaErrorSummaries& summaries);

    struct SelectedPixelSampleRecord
    {
        uint32_t sampleIndex = InvalidDiagnosticsSampleIndex;
        bool covered = false;

        // packedKey preserves the exact raster/DXR GPU representation,
        // including invalid encodings useful to diagnostics. A valid
        // surfaceHit is the single logical identity consumed by future cache,
        // picking, or DXR integrations.
        GpuVisibilityKey packedKey;
        bool surfaceHitValid = false;
        SurfaceHit surfaceHit;

        float linearDepth = 0.f;
        uint32_t groupIndex = InvalidDiagnosticsSampleIndex;
        uint32_t representativeSampleIndex =
            InvalidDiagnosticsSampleIndex;
        float weight = 0.f;
        FallbackReason fallbackReason = FallbackReason::None;
        Float3 radiance;
    };

    struct SelectedPixelRecord
    {
        bool valid = false;
        uint64_t captureId = 0u;
        uint64_t frameIndex = 0u;
        uint32_t pixelX = 0u;
        uint32_t pixelY = 0u;
        uint32_t sampleCount = 0u;
        std::array<SelectedPixelSampleRecord, MaximumAdaptiveSampleCount>
            samples{};
    };

    [[nodiscard]] DiagnosticsInputStatus StoreSelectedPixelRecord(
        SelectedPixelRecord& output,
        uint64_t captureId,
        uint64_t frameIndex,
        uint32_t pixelX,
        uint32_t pixelY,
        const SelectedPixelSampleRecord* samples,
        uint32_t sampleCount);

    void ResetSelectedPixelRecord(SelectedPixelRecord& record);
}
