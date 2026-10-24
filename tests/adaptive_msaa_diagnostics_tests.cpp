#include "adaptive_msaa_diagnostics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    using namespace uvsr::adaptive_msaa;

    static_assert(DiagnosticsHdrDenominatorFloor == 1.f / 1024.f);
    static_assert(DiagnosticsHdrPercentile99Limit == 1.f / 1024.f);
    static_assert(DiagnosticsHdrGroupLimit == 1.f / 1024.f);
    static_assert(DiagnosticsHdrMaximumLimit == 1.f / 128.f);
    static_assert(DiagnosticsDisplayPercentile99Limit == 0.5f);
    static_assert(DiagnosticsDisplayMaximumLimit == 2.f);
    static_assert(DiagnosticsDisplayCodeScale == 255.f);

    bool g_Passed = true;

    void Check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "Adaptive MSAA diagnostics validation failed: "
                << message << '\n';
            g_Passed = false;
        }
    }

    bool Near(float actual, float expected, float tolerance = 1e-6f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    GpuVisibilityKey MakePackedKey(uint32_t slot, uint32_t primitive)
    {
        const HandlePackResult handle = PackGeometryInstanceHandle(slot, 1u);
        Check(handle.Succeeded(), "diagnostics test handle must pack");
        const GpuVisibilityKeyResult key =
            MakeGpuVisibilityKey(handle.handle, primitive);
        Check(key.Succeeded(), "diagnostics test key must pack");
        return key.key;
    }

    SelectedPixelSampleRecord MakeSelectedSample(
        uint32_t sampleIndex,
        uint32_t sampleCount)
    {
        SelectedPixelSampleRecord sample;
        sample.sampleIndex = sampleIndex;
        sample.covered = true;
        sample.packedKey = MakePackedKey(sampleIndex, 100u + sampleIndex);
        sample.surfaceHitValid = true;
        sample.surfaceHit.key = {
            0x100000000ull + sampleIndex,
            10u + sampleIndex,
            100u + sampleIndex };
        sample.surfaceHit.barycentrics = { 0.2f, 0.3f };
        sample.surfaceHit.frontFace = (sampleIndex & 1u) == 0u;
        sample.linearDepth = 5.f + float(sampleIndex);
        sample.groupIndex = sampleIndex;
        sample.representativeSampleIndex = sampleIndex;
        sample.weight = 1.f / float(sampleCount);
        sample.radiance = {
            float(sampleIndex),
            float(sampleIndex) + 0.25f,
            float(sampleIndex) + 0.5f };
        return sample;
    }

    void TestReferenceComparisonRequests()
    {
        AdaptiveMsaaDiagnosticsSettings settings;
        settings.enabled = true;
        settings.dualRenderingEnabled = true;
        settings.comparisonMode =
            DiagnosticsComparisonMode::SplitScreen;

        Check(RequestsFinalLightingComparison(settings) &&
                !RequestsCompactReconstructionComparison(settings) &&
                RequestsAnyReferenceComparison(settings),
            "comparison-only diagnostics request final-lighting references");

        settings.debugView =
            AdaptiveMsaaDebugView::AdaptiveLightingError;
        settings.comparisonMode =
            DiagnosticsComparisonMode::ErrorHeatmap;
        Check(RequestsFinalLightingComparison(settings) &&
                RequestsAnyReferenceComparison(settings),
            "explicit lighting heatmap requests final-lighting references");

        settings.debugView =
            AdaptiveMsaaDebugView::CompactReconstructionError;
        Check(!RequestsFinalLightingComparison(settings) &&
                RequestsCompactReconstructionComparison(settings) &&
                RequestsAnyReferenceComparison(settings),
            "compact reconstruction comparison remains a distinct reference product");

        settings.debugView =
            AdaptiveMsaaDebugView::SurfaceOwnershipKeys;
        Check(!RequestsFinalLightingComparison(settings) &&
                !RequestsCompactReconstructionComparison(settings) &&
                !RequestsAnyReferenceComparison(settings),
            "non-reference debug views do not retain comparison products");

        settings.debugView = AdaptiveMsaaDebugView::Disabled;
        settings.comparisonMode =
            DiagnosticsComparisonMode::Disabled;
        Check(!RequestsAnyReferenceComparison(settings),
            "disabled comparison requests no reference products");
        settings.comparisonMode =
            DiagnosticsComparisonMode::SplitScreen;
        settings.dualRenderingEnabled = false;
        Check(!RequestsAnyReferenceComparison(settings),
            "comparison presentation requires the explicit dual-render gate");
        settings.dualRenderingEnabled = true;
        settings.enabled = false;
        Check(!RequestsAnyReferenceComparison(settings),
            "top-level disable clears every reference-comparison request");
    }

    void TestDefaultsSanitizationAndBenchmarkGate()
    {
        AdaptiveMsaaDiagnosticsSettings defaults;
        Check(!defaults.enabled &&
                defaults.comparisonMode ==
                    DiagnosticsComparisonMode::Disabled &&
                defaults.debugView == AdaptiveMsaaDebugView::Disabled &&
                defaults.errorDomain == DiagnosticsErrorDomain::Spatial &&
                defaults.reconstructionChannel ==
                    DiagnosticsReconstructionChannel::Combined &&
                !defaults.freezeCameraAndFrame &&
                !defaults.validationAnimationEnabled &&
                defaults.validationAnimationPhase == 0.f &&
                !defaults.selectedPixelCaptureRequested &&
                !defaults.readbackEnabled &&
                !defaults.countersEnabled &&
                !defaults.passTimingsEnabled &&
                !defaults.dualRenderingEnabled &&
                IsAcceptedBenchmarkDiagnosticsSafe(defaults),
            "default diagnostics state is structurally benchmark-safe");

        AdaptiveMsaaDiagnosticsSettings dirtyDisabled;
        dirtyDisabled.comparisonMode = DiagnosticsComparisonMode::SplitScreen;
        dirtyDisabled.debugView =
            AdaptiveMsaaDebugView::AdaptiveLightingError;
        dirtyDisabled.errorDomain = DiagnosticsErrorDomain::Temporal;
        dirtyDisabled.reconstructionChannel =
            DiagnosticsReconstructionChannel::Normals;
        dirtyDisabled.freezeCameraAndFrame = true;
        dirtyDisabled.validationAnimationEnabled = true;
        dirtyDisabled.validationAnimationPhase = 0.75f;
        dirtyDisabled.selectedPixelCaptureRequested = true;
        dirtyDisabled.selectedPixelX = 99u;
        dirtyDisabled.selectedPixelY = 101u;
        dirtyDisabled.selectedPixelCaptureId = 12u;
        dirtyDisabled.readbackEnabled = true;
        dirtyDisabled.countersEnabled = true;
        dirtyDisabled.passTimingsEnabled = true;
        dirtyDisabled.dualRenderingEnabled = true;
        Check(!IsAcceptedBenchmarkDiagnosticsSafe(dirtyDisabled),
            "top-level disabled does not excuse active diagnostic subpaths");

        AdaptiveMsaaDiagnosticsSettings sanitized;
        Check(SanitizeDiagnosticsSettings(dirtyDisabled, sanitized) ==
                DiagnosticsInputStatus::Success &&
                IsAcceptedBenchmarkDiagnosticsSafe(sanitized),
            "sanitization clears every dependent path when diagnostics are off");

        AdaptiveMsaaDiagnosticsSettings enabled = dirtyDisabled;
        enabled.enabled = true;
        Check(SanitizeDiagnosticsSettings(enabled, sanitized) ==
                DiagnosticsInputStatus::Success &&
                sanitized.enabled &&
                sanitized.comparisonMode ==
                    DiagnosticsComparisonMode::SplitScreen &&
                sanitized.errorDomain == DiagnosticsErrorDomain::Temporal &&
                sanitized.reconstructionChannel ==
                    DiagnosticsReconstructionChannel::Normals &&
                sanitized.validationAnimationEnabled &&
                sanitized.validationAnimationPhase == 0.75f &&
                sanitized.selectedPixelX == 99u &&
                sanitized.selectedPixelCaptureId == 12u &&
                !IsAcceptedBenchmarkDiagnosticsSafe(sanitized),
            "valid enabled state survives sanitization without hidden work");

        AdaptiveMsaaDiagnosticsSettings invalid = enabled;
        invalid.comparisonMode =
            static_cast<DiagnosticsComparisonMode>(0xffffffffu);
        Check(SanitizeDiagnosticsSettings(invalid, sanitized) ==
                DiagnosticsInputStatus::InvalidComparisonMode &&
                IsAcceptedBenchmarkDiagnosticsSafe(sanitized),
            "invalid comparison enum fails to safe defaults");
        invalid = enabled;
        invalid.debugView = static_cast<AdaptiveMsaaDebugView>(0xffffffffu);
        Check(SanitizeDiagnosticsSettings(invalid, sanitized) ==
                DiagnosticsInputStatus::InvalidDebugView &&
                IsAcceptedBenchmarkDiagnosticsSafe(sanitized),
            "invalid debug-view enum fails to safe defaults");
        invalid = enabled;
        invalid.errorDomain =
            static_cast<DiagnosticsErrorDomain>(0xffffffffu);
        Check(SanitizeDiagnosticsSettings(invalid, sanitized) ==
                DiagnosticsInputStatus::InvalidErrorDomain &&
                IsAcceptedBenchmarkDiagnosticsSafe(sanitized),
            "invalid error-domain enum fails to safe defaults");
        invalid = enabled;
        invalid.reconstructionChannel =
            static_cast<DiagnosticsReconstructionChannel>(0xffffffffu);
        Check(SanitizeDiagnosticsSettings(invalid, sanitized) ==
                DiagnosticsInputStatus::InvalidReconstructionChannel &&
                IsAcceptedBenchmarkDiagnosticsSafe(sanitized),
            "invalid reconstruction-channel enum fails to safe defaults");

        invalid = enabled;
        invalid.validationAnimationPhase =
            std::numeric_limits<float>::quiet_NaN();
        Check(SanitizeDiagnosticsSettings(invalid, sanitized) ==
                DiagnosticsInputStatus::NonFiniteValue &&
                IsAcceptedBenchmarkDiagnosticsSafe(sanitized),
            "non-finite validation animation phase fails to safe defaults");
        invalid = enabled;
        invalid.validationAnimationPhase = 1.0001f;
        Check(SanitizeDiagnosticsSettings(invalid, sanitized) ==
                DiagnosticsInputStatus::ValueOutOfRange &&
                IsAcceptedBenchmarkDiagnosticsSafe(sanitized),
            "out-of-range validation animation phase fails to safe defaults");

        std::array<AdaptiveMsaaDiagnosticsSettings, 13> unsafe{};
        unsafe[0].enabled = true;
        unsafe[1].comparisonMode = DiagnosticsComparisonMode::ErrorHeatmap;
        unsafe[2].debugView = AdaptiveMsaaDebugView::SurfaceOwnershipKeys;
        unsafe[3].errorDomain = DiagnosticsErrorDomain::Temporal;
        unsafe[4].reconstructionChannel =
            DiagnosticsReconstructionChannel::Depth;
        unsafe[5].freezeCameraAndFrame = true;
        unsafe[6].validationAnimationEnabled = true;
        unsafe[7].validationAnimationPhase = 0.5f;
        unsafe[8].selectedPixelCaptureRequested = true;
        unsafe[9].readbackEnabled = true;
        unsafe[10].countersEnabled = true;
        unsafe[11].passTimingsEnabled = true;
        unsafe[12].dualRenderingEnabled = true;
        for (const auto& state : unsafe)
        {
            Check(!IsAcceptedBenchmarkDiagnosticsSafe(state),
                "every diagnostic execution path independently blocks benchmarks");
        }
    }

    void TestCounterAccountingAndSaturation()
    {
        AdaptiveMsaaDiagnosticsCounters counters;
        for (uint32_t groupCount = 0u;
            groupCount <= MaximumAdaptiveSampleCount;
            ++groupCount)
        {
            Check(counters.RecordGroupCount(groupCount, groupCount + 1u) ==
                    DiagnosticsInputStatus::Success,
                "every 0..16 group histogram bin is valid");
        }
        const auto beforeInvalidGroup = counters.Data();
        Check(counters.RecordGroupCount(MaximumAdaptiveSampleCount + 1u) ==
                DiagnosticsInputStatus::InvalidGroupCount &&
                counters.Data().pixelsObserved ==
                    beforeInvalidGroup.pixelsObserved,
            "group histogram overflow fails without mutation");
        for (uint32_t groupCount = 0u;
            groupCount <= MaximumAdaptiveSampleCount;
            ++groupCount)
        {
            Check(counters.Data().groupCountHistogram[groupCount] ==
                    groupCount + 1u,
                "group histogram retains exact bounded counts");
        }

        counters.Reset();
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();
        Check(counters.RecordGroupCount(0u, maximum - 2u) ==
                DiagnosticsInputStatus::Success &&
                counters.RecordGroupCount(0u, 10u) ==
                    DiagnosticsInputStatus::Success &&
                counters.Data().groupCountHistogram[0] == maximum &&
                counters.Data().pixelsObserved == maximum,
            "group and pixel totals saturate instead of wrapping");

        counters.Reset();
        for (uint32_t value = 1u;
            value < static_cast<uint32_t>(FallbackReason::Count);
            ++value)
        {
            Check(counters.RecordFallbackReason(
                    static_cast<FallbackReason>(value)) ==
                        DiagnosticsInputStatus::Success,
                "every concrete fallback reason is countable");
        }
        Check(counters.RecordFallbackReason(FallbackReason::None) ==
                DiagnosticsInputStatus::InvalidFallbackReason &&
            counters.RecordFallbackReason(FallbackReason::Count) ==
                DiagnosticsInputStatus::InvalidFallbackReason,
            "None and Count are not fallback occurrences");
        Check(counters.Data().invalidKeyCount == 4u &&
                counters.Data().staleKeyCount == 9u &&
                counters.Data().mappingFailureCount == 2u,
            "fallback reasons derive stable invalid, stale, and mapping totals");
        for (uint32_t value = 1u;
            value < static_cast<uint32_t>(FallbackReason::Count);
            ++value)
        {
            Check(counters.Data().fallbackReasonCounts[value] == 1u,
                "fallback histogram retains every exact ABI reason");
        }

        counters.Reset();
        Check(counters.RecordFallbackReason(
                FallbackReason::InvalidSentinel, maximum) ==
                    DiagnosticsInputStatus::Success &&
            counters.RecordFallbackReason(
                FallbackReason::InvalidSentinel, 1u) ==
                    DiagnosticsInputStatus::Success &&
            counters.Data().invalidKeyCount == maximum &&
            counters.Data().fallbackReasonCounts[
                static_cast<size_t>(FallbackReason::InvalidSentinel)] ==
                    maximum,
            "fallback and derived category totals saturate");

        counters.Reset();
        Check(counters.RecordErrorBudgetRejection(
                DiagnosticsErrorBudgetRejectionReason::HdrLimitExceeded,
                4u,
                3u) == DiagnosticsInputStatus::Success &&
            counters.RecordErrorBudgetRejection(
                DiagnosticsErrorBudgetRejectionReason::DisplayLimitExceeded,
                2u,
                5u) == DiagnosticsInputStatus::Success &&
            counters.Data().errorBudgetRejectedGroupCount == 8u &&
            counters.Data().errorBudgetRejectedSampleCount == 22u &&
            counters.Data().errorBudgetRejectionReasonCounts[
                static_cast<size_t>(
                    DiagnosticsErrorBudgetRejectionReason::
                        HdrLimitExceeded)] == 3u &&
            counters.Data().errorBudgetRejectionReasonCounts[
                static_cast<size_t>(
                    DiagnosticsErrorBudgetRejectionReason::
                        DisplayLimitExceeded)] == 5u,
            "error-budget rejection groups, samples, and reasons remain separate");
        const auto beforeInvalidErrorBudget = counters.Data();
        Check(counters.RecordErrorBudgetRejection(
                DiagnosticsErrorBudgetRejectionReason::None,
                1u) == DiagnosticsInputStatus::
                    InvalidErrorBudgetRejectionReason &&
            counters.RecordErrorBudgetRejection(
                DiagnosticsErrorBudgetRejectionReason::Count,
                1u) == DiagnosticsInputStatus::
                    InvalidErrorBudgetRejectionReason &&
            counters.RecordErrorBudgetRejection(
                DiagnosticsErrorBudgetRejectionReason::HdrLimitExceeded,
                0u) == DiagnosticsInputStatus::InvalidGroupCount &&
            counters.RecordErrorBudgetRejection(
                DiagnosticsErrorBudgetRejectionReason::HdrLimitExceeded,
                MaximumAdaptiveSampleCount + 1u) ==
                    DiagnosticsInputStatus::InvalidGroupCount &&
            counters.Data().errorBudgetRejectedGroupCount ==
                beforeInvalidErrorBudget.errorBudgetRejectedGroupCount &&
            counters.Data().errorBudgetRejectedSampleCount ==
                beforeInvalidErrorBudget.errorBudgetRejectedSampleCount &&
            counters.Data().errorBudgetRejectionReasonCounts ==
                beforeInvalidErrorBudget.errorBudgetRejectionReasonCounts,
            "invalid error-budget reasons and group sizes fail without mutation");
        counters.Reset();
        Check(counters.RecordErrorBudgetRejection(
                DiagnosticsErrorBudgetRejectionReason::NonFiniteValue,
                MaximumAdaptiveSampleCount,
                maximum) == DiagnosticsInputStatus::Success &&
            counters.RecordErrorBudgetRejection(
                DiagnosticsErrorBudgetRejectionReason::NonFiniteValue,
                1u) == DiagnosticsInputStatus::Success &&
            counters.Data().errorBudgetRejectedGroupCount == maximum &&
            counters.Data().errorBudgetRejectedSampleCount == maximum &&
            counters.Data().errorBudgetRejectionReasonCounts[
                static_cast<size_t>(
                    DiagnosticsErrorBudgetRejectionReason::
                        NonFiniteValue)] == maximum,
            "error-budget rejection counters saturate instead of wrapping");

        counters.Reset();
        Check(counters.RecordLightingEvaluations(16u, 5u) ==
                DiagnosticsInputStatus::Success &&
                counters.Data().referenceLightingEvaluations == 16u &&
                counters.Data().lightingEvaluations == 5u &&
                counters.Data().lightingEvaluationsSaved == 11u,
            "lighting accounting records performed and saved evaluations");
        const auto beforeInvalidLighting = counters.Data();
        Check(counters.RecordLightingEvaluations(4u, 5u) ==
                DiagnosticsInputStatus::InvalidLightingEvaluationCount &&
                counters.Data().lightingEvaluations ==
                    beforeInvalidLighting.lightingEvaluations,
            "negative evaluation savings fail without mutation");
        Check(counters.RecordLightingEvaluations(maximum, 0u) ==
                DiagnosticsInputStatus::Success &&
                counters.RecordLightingEvaluations(1u, 1u) ==
                    DiagnosticsInputStatus::Success &&
                counters.Data().referenceLightingEvaluations == maximum &&
                counters.Data().lightingEvaluationsSaved == maximum,
            "lighting totals saturate instead of wrapping");

        counters.Reset();
        Check(counters.RecordWorkListObservation(5u, 8u, false, 2u) ==
                DiagnosticsInputStatus::Success &&
            counters.RecordWorkListObservation(9u, 8u, true, 3u) ==
                DiagnosticsInputStatus::Success &&
            counters.Data().workListObservationCount == 5u &&
            counters.Data().workListOccupancyTotal == 37u &&
            counters.Data().workListCapacityTotal == 40u &&
            counters.Data().workListPeakOccupancy == 9u &&
            counters.Data().workListOverflowCount == 3u,
            "work-list occupancy, capacity, peak, and overflow stay separate");
        const auto beforeInvalidWorkList = counters.Data();
        Check(counters.RecordWorkListObservation(9u, 8u, false) ==
                DiagnosticsInputStatus::InvalidWorkListObservation &&
                counters.Data().workListObservationCount ==
                    beforeInvalidWorkList.workListObservationCount,
            "unreported work-list overflow fails without mutation");
        Check(counters.RecordWorkListObservation(
                maximum, maximum, false, maximum) ==
                    DiagnosticsInputStatus::Success &&
                counters.Data().workListObservationCount == maximum &&
                counters.Data().workListOccupancyTotal == maximum &&
                counters.Data().workListCapacityTotal == maximum,
            "work-list multiply and accumulation saturate");

        counters.Reset();
        for (uint32_t value = 0u;
            value < static_cast<uint32_t>(
                AdaptiveMsaaDiagnosticsPass::Count);
            ++value)
        {
            Check(counters.RecordPassTiming(
                    static_cast<AdaptiveMsaaDiagnosticsPass>(value),
                    uint64_t(value + 1u),
                    2u) == DiagnosticsInputStatus::Success,
                "every bounded diagnostics pass accepts timing");
        }
        Check(counters.RecordPassTiming(
                AdaptiveMsaaDiagnosticsPass::Count, 1u) ==
                    DiagnosticsInputStatus::InvalidPass,
            "invalid pass enum fails closed");
        Check(counters.RecordPassTiming(
                AdaptiveMsaaDiagnosticsPass::Resolve, maximum, maximum) ==
                    DiagnosticsInputStatus::Success &&
            counters.RecordPassTiming(
                AdaptiveMsaaDiagnosticsPass::Resolve, 1u, 1u) ==
                    DiagnosticsInputStatus::Success &&
            counters.Data().passGpuNanoseconds[
                static_cast<size_t>(AdaptiveMsaaDiagnosticsPass::Resolve)] ==
                    maximum &&
            counters.Data().passInvocationCounts[
                static_cast<size_t>(AdaptiveMsaaDiagnosticsPass::Resolve)] ==
                    maximum,
            "pass timing and invocation totals saturate");

        counters.Reset();
        const AdaptiveMsaaDiagnosticsCountersData empty;
        Check(counters.Data().pixelsObserved == empty.pixelsObserved &&
                counters.Data().invalidKeyCount == 0u &&
                counters.Data().errorBudgetRejectedGroupCount == 0u &&
                counters.Data().workListPeakOccupancy == 0u,
            "frame counter reset clears all bounded state");
    }

    void TestDeterministicErrorSummaries()
    {
        std::array<float, 100> errors{};
        for (uint32_t index = 0u; index < errors.size(); ++index)
            errors[index] = float(errors.size() - index);
        const auto original = errors;

        DiagnosticsErrorSummary summary;
        Check(SummarizeDiagnosticsErrors(
                errors.data(), errors.size(), summary) ==
                    DiagnosticsInputStatus::Success &&
                summary.valid && summary.sampleCount == 100u &&
                Near(summary.percentile50, 50.f) &&
                Near(summary.percentile90, 90.f) &&
                Near(summary.percentile95, 95.f) &&
                Near(summary.percentile99, 99.f) &&
                Near(summary.maximum, 100.f) &&
                errors == original,
            "nearest-rank percentiles are deterministic and caller-owned input is unchanged");

        const std::array<float, 2> two = { 8.f, 2.f };
        Check(SummarizeDiagnosticsErrors(
                two.data(), two.size(), summary) ==
                    DiagnosticsInputStatus::Success &&
                Near(summary.percentile50, 2.f) &&
                Near(summary.percentile90, 8.f) &&
                Near(summary.maximum, 8.f),
            "nearest-rank boundaries are defined for small samples");

        AdaptiveMsaaErrorSummaries summaries;
        const std::array<float, 3> spatial = { 1.f, 2.f, 3.f };
        const std::array<float, 3> temporal = { 10.f, 20.f, 30.f };
        Check(SetDiagnosticsErrorSummary(
                summaries,
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Spatial,
                spatial.data(),
                spatial.size()) == DiagnosticsInputStatus::Success &&
            SetDiagnosticsErrorSummary(
                summaries,
                DiagnosticsErrorValueDomain::Display,
                DiagnosticsErrorDomain::Temporal,
                temporal.data(),
                temporal.size()) == DiagnosticsInputStatus::Success &&
            Near(summaries.hdr.spatial.maximum, 3.f) &&
            Near(summaries.display.temporal.maximum, 30.f) &&
            !summaries.hdr.temporal.valid &&
            !summaries.display.spatial.valid,
            "HDR/display and spatial/temporal error state remains independent");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(SetDiagnosticsErrorSummary(
                summaries,
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Spatial,
                &nan,
                1u) == DiagnosticsInputStatus::NonFiniteValue &&
                !summaries.hdr.spatial.valid &&
                summaries.display.temporal.valid,
            "non-finite HDR spatial input does not erase another domain");
        const float infinity = std::numeric_limits<float>::infinity();
        Check(SummarizeDiagnosticsErrors(&infinity, 1u, summary) ==
                DiagnosticsInputStatus::NonFiniteValue &&
                !summary.valid,
            "infinite error input fails closed");
        const float negative = -1.f;
        Check(SummarizeDiagnosticsErrors(&negative, 1u, summary) ==
                DiagnosticsInputStatus::NegativeValue &&
                !summary.valid,
            "negative error magnitude fails closed");
        Check(SummarizeDiagnosticsErrors(nullptr, 1u, summary) ==
                DiagnosticsInputStatus::NullInput &&
            SummarizeDiagnosticsErrors(spatial.data(), 0u, summary) ==
                DiagnosticsInputStatus::EmptyInput,
            "null and empty error input are distinguished");

        const DiagnosticsErrorSummary temporalBefore =
            summaries.display.temporal;
        Check(SetDiagnosticsErrorSummary(
                summaries,
                DiagnosticsErrorValueDomain::Display,
                DiagnosticsErrorDomain::Count,
                temporal.data(),
                temporal.size()) ==
                    DiagnosticsInputStatus::InvalidErrorDomain &&
                summaries.display.temporal.valid == temporalBefore.valid &&
                Near(summaries.display.temporal.maximum,
                    temporalBefore.maximum),
            "invalid error-domain enum fails without mutation");
        Check(SetDiagnosticsErrorSummary(
                summaries,
                DiagnosticsErrorValueDomain::Count,
                DiagnosticsErrorDomain::Temporal,
                temporal.data(),
                temporal.size()) ==
                    DiagnosticsInputStatus::InvalidErrorValueDomain &&
                summaries.display.temporal.valid == temporalBefore.valid &&
                Near(summaries.display.temporal.maximum,
                    temporalBefore.maximum),
            "invalid value-domain enum fails without conflating summaries");
        ResetDiagnosticsErrorSummaries(summaries);
        Check(!summaries.hdr.spatial.valid &&
                !summaries.hdr.temporal.valid &&
                !summaries.display.spatial.valid &&
                !summaries.display.temporal.valid,
            "error reset clears all four independent summaries");
    }

    void TestFrozenErrorFormulasAndLimits()
    {
        Check(Near(DiagnosticsHdrDenominatorFloor, 1.f / 1024.f) &&
                Near(DiagnosticsHdrPercentile99Limit, 1.f / 1024.f) &&
                Near(DiagnosticsHdrMaximumLimit, 1.f / 128.f) &&
                Near(DiagnosticsDisplayPercentile99Limit, 0.5f) &&
                Near(DiagnosticsDisplayMaximumLimit, 2.f) &&
                Near(DiagnosticsDisplayCodeScale, 255.f),
            "error formula and gate constants are frozen at compile time");

        DiagnosticsErrorLimits limits;
        Check(GetDiagnosticsErrorLimits(
                DiagnosticsErrorValueDomain::Hdr,
                limits) == DiagnosticsInputStatus::Success &&
                Near(limits.percentile99,
                    DiagnosticsHdrPercentile99Limit) &&
                Near(limits.maximum, DiagnosticsHdrMaximumLimit),
            "HDR limits expose the frozen p99 and maximum");
        Check(GetDiagnosticsErrorLimits(
                DiagnosticsErrorValueDomain::Display,
                limits) == DiagnosticsInputStatus::Success &&
                Near(limits.percentile99,
                    DiagnosticsDisplayPercentile99Limit) &&
                Near(limits.maximum, DiagnosticsDisplayMaximumLimit),
            "display limits expose continuous code-value bounds");
        limits = { 4.f, 5.f };
        Check(GetDiagnosticsErrorLimits(
                DiagnosticsErrorValueDomain::Count,
                limits) == DiagnosticsInputStatus::InvalidErrorValueDomain &&
                Near(limits.percentile99, 0.f) && Near(limits.maximum, 0.f),
            "invalid value domain clears limits and fails closed");

        float error = -1.f;
        Check(ComputeDiagnosticsHdrSpatialError(
                Float3{ 2.f, 0.f, 0.f },
                Float3{ 1.f, 0.f, 0.f },
                error) == DiagnosticsInputStatus::Success &&
                Near(error, 0.5f),
            "HDR spatial error uses symmetric per-channel normalization");
        Check(ComputeDiagnosticsHdrSpatialError(
                Float3{ DiagnosticsHdrDenominatorFloor, 0.f, 0.f },
                Float3{},
                error) == DiagnosticsInputStatus::Success &&
                Near(error, 1.f),
            "HDR spatial error applies the exact denominator floor");
        Check(ComputeDiagnosticsHdrTemporalError(
                Float3{ 2.f, 0.f, 0.f },
                Float3{ 1.f, 0.f, 0.f },
                Float3{ 1.f, 0.f, 0.f },
                Float3{ 2.f, 0.f, 0.f },
                error) == DiagnosticsInputStatus::Success &&
                Near(error, 1.f),
            "HDR temporal error preserves the signed residual before magnitude");
        Check(ComputeDiagnosticsHdrTemporalError(
                Float3{ 2.f, 0.f, 0.f },
                Float3{ 1.f, 0.f, 0.f },
                Float3{ 3.f, 0.f, 0.f },
                Float3{ 2.f, 0.f, 0.f },
                error) == DiagnosticsInputStatus::Success &&
                Near(error, 0.f),
            "a temporally constant signed HDR residual has zero temporal error");

        Check(ComputeDiagnosticsDisplaySpatialError(
                Float3{ 0.5f, 0.f, 0.f },
                Float3{ 0.25f, 0.f, 0.f },
                error) == DiagnosticsInputStatus::Success &&
                Near(error, 63.75f),
            "display spatial error reports continuous 8-bit code values");
        Check(ComputeDiagnosticsDisplayTemporalError(
                Float3{ 0.4f, 0.f, 0.f },
                Float3{ 0.5f, 0.f, 0.f },
                Float3{ 0.6f, 0.f, 0.f },
                Float3{ 0.5f, 0.f, 0.f },
                error) == DiagnosticsInputStatus::Success &&
                Near(error, 51.f, 1e-4f),
            "display temporal error detects equal-magnitude sign flips");
        Check(ComputeDiagnosticsDisplayTemporalError(
                Float3{ 0.6f, 0.f, 0.f },
                Float3{ 0.5f, 0.f, 0.f },
                Float3{ 0.7f, 0.f, 0.f },
                Float3{ 0.6f, 0.f, 0.f },
                error) == DiagnosticsInputStatus::Success &&
                Near(error, 0.f, 1e-4f),
            "a temporally constant signed display residual is stable");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(ComputeDiagnosticsHdrSpatialError(
                Float3{ nan, 0.f, 0.f }, Float3{}, error) ==
                    DiagnosticsInputStatus::NonFiniteValue &&
                Near(error, 0.f),
            "non-finite HDR inputs fail with a cleared output");
        Check(ComputeDiagnosticsDisplaySpatialError(
                Float3{ -0.01f, 0.f, 0.f }, Float3{}, error) ==
                    DiagnosticsInputStatus::NegativeValue &&
                Near(error, 0.f),
            "negative post-transform display input fails closed");
        Check(ComputeDiagnosticsDisplaySpatialError(
                Float3{ 1.01f, 0.f, 0.f }, Float3{}, error) ==
                    DiagnosticsInputStatus::ValueOutOfRange &&
                Near(error, 0.f),
            "display input above the normalized transform range fails closed");
    }

    void TestFourDomainsActiveMasksAndGating()
    {
        AdaptiveMsaaErrorSummaries summaries;
        const std::array<float, 2> hdrSpatial = { 1.f, 2.f };
        const std::array<float, 2> hdrTemporal = { 3.f, 4.f };
        const std::array<float, 2> displaySpatial = { 5.f, 6.f };
        const std::array<float, 2> displayTemporal = { 7.f, 8.f };
        Check(SetDiagnosticsErrorSummary(
                summaries,
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Spatial,
                hdrSpatial.data(), hdrSpatial.size()) ==
                    DiagnosticsInputStatus::Success &&
            SetDiagnosticsErrorSummary(
                summaries,
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Temporal,
                hdrTemporal.data(), hdrTemporal.size()) ==
                    DiagnosticsInputStatus::Success &&
            SetDiagnosticsErrorSummary(
                summaries,
                DiagnosticsErrorValueDomain::Display,
                DiagnosticsErrorDomain::Spatial,
                displaySpatial.data(), displaySpatial.size()) ==
                    DiagnosticsInputStatus::Success &&
            SetDiagnosticsErrorSummary(
                summaries,
                DiagnosticsErrorValueDomain::Display,
                DiagnosticsErrorDomain::Temporal,
                displayTemporal.data(), displayTemporal.size()) ==
                    DiagnosticsInputStatus::Success &&
            Near(summaries.hdr.spatial.maximum, 2.f) &&
            Near(summaries.hdr.temporal.maximum, 4.f) &&
            Near(summaries.display.spatial.maximum, 6.f) &&
            Near(summaries.display.temporal.maximum, 8.f),
            "all four error summaries coexist without domain conflation");

        const std::array<float, 4> activeErrors = { 4.f, 1.f, 3.f, 2.f };
        const std::array<uint8_t, 4> activeMask = { 0u, 1u, 0u, 1u };
        DiagnosticsErrorSummary activeSummary;
        Check(SummarizeActiveDiagnosticsErrors(
                activeErrors.data(),
                activeMask.data(),
                activeErrors.size(),
                activeSummary) == DiagnosticsInputStatus::Success &&
                activeSummary.sampleCount == 2u &&
                Near(activeSummary.percentile99, 2.f) &&
                Near(activeSummary.maximum, 2.f),
            "active-mask summaries exclude finite inactive samples deterministically");
        const std::array<uint8_t, 4> emptyMask{};
        Check(SummarizeActiveDiagnosticsErrors(
                activeErrors.data(),
                emptyMask.data(),
                activeErrors.size(),
                activeSummary) == DiagnosticsInputStatus::EmptyInput &&
                !activeSummary.valid,
            "an empty active mask is an invalid comparison population");
        std::array<uint8_t, 4> invalidMask = activeMask;
        invalidMask[2] = 2u;
        Check(SummarizeActiveDiagnosticsErrors(
                activeErrors.data(),
                invalidMask.data(),
                activeErrors.size(),
                activeSummary) == DiagnosticsInputStatus::InvalidActiveMask,
            "active masks accept only canonical zero and one values");
        std::array<float, 4> nonFiniteInactive = activeErrors;
        nonFiniteInactive[0] = std::numeric_limits<float>::infinity();
        Check(SummarizeActiveDiagnosticsErrors(
                nonFiniteInactive.data(),
                activeMask.data(),
                nonFiniteInactive.size(),
                activeSummary) == DiagnosticsInputStatus::NonFiniteValue,
            "non-finite scalar input fails even when its mask bit is inactive");
        Check(SummarizeActiveDiagnosticsErrors(
                nullptr,
                activeMask.data(),
                activeErrors.size(),
                activeSummary) == DiagnosticsInputStatus::NullInput &&
            SummarizeActiveDiagnosticsErrors(
                activeErrors.data(),
                nullptr,
                activeErrors.size(),
                activeSummary) == DiagnosticsInputStatus::NullInput,
            "active summaries require both scalar and mask input");

        std::array<float, 100> hdrBoundary{};
        hdrBoundary.fill(DiagnosticsHdrPercentile99Limit);
        hdrBoundary.back() = DiagnosticsHdrMaximumLimit;
        std::array<uint8_t, 100> allActive{};
        allActive.fill(1u);
        DiagnosticsErrorFrameView hdrFrame = {
            hdrBoundary.data(), allActive.data(), hdrBoundary.size() };
        DiagnosticsErrorGateResult gate;
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Temporal,
                &hdrFrame,
                1u,
                gate) == DiagnosticsInputStatus::Success &&
                gate.valid && gate.passed && gate.perFramePassed &&
                gate.pooledPassed && gate.frameCount == 1u &&
                gate.activeSampleCount == hdrBoundary.size() &&
                Near(gate.worstFramePercentile99,
                    DiagnosticsHdrPercentile99Limit) &&
                Near(gate.worstFrameMaximum,
                    DiagnosticsHdrMaximumLimit),
            "HDR p99 and maximum boundaries pass inclusively per-frame and pooled");
        hdrBoundary.back() = std::nextafter(
            DiagnosticsHdrMaximumLimit,
            std::numeric_limits<float>::infinity());
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Temporal,
                &hdrFrame,
                1u,
                gate) == DiagnosticsInputStatus::Success &&
                gate.valid && !gate.passed &&
                gate.failedFrameCount == 1u &&
                gate.firstFailedFrameIndex == 0u &&
                gate.pooled.percentile99 <=
                    DiagnosticsHdrPercentile99Limit &&
                gate.pooled.maximum > DiagnosticsHdrMaximumLimit,
            "one HDR sample above the maximum fails without relying on p99");

        const float hdrAboveP99 = std::nextafter(
            DiagnosticsHdrPercentile99Limit,
            std::numeric_limits<float>::infinity());
        hdrBoundary.fill(0.f);
        hdrBoundary.back() = hdrAboveP99;
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Temporal,
                &hdrFrame,
                1u,
                gate) == DiagnosticsInputStatus::Success &&
                gate.passed && gate.pooled.percentile99 == 0.f &&
                gate.pooled.maximum < DiagnosticsHdrMaximumLimit,
            "exactly one percent of HDR samples may exceed p99 while remaining below maximum");
        hdrBoundary[98] = hdrAboveP99;
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Temporal,
                &hdrFrame,
                1u,
                gate) == DiagnosticsInputStatus::Success &&
                !gate.passed && gate.pooled.percentile99 >
                    DiagnosticsHdrPercentile99Limit &&
                gate.pooled.maximum < DiagnosticsHdrMaximumLimit,
            "greater than one percent HDR error fails p99 below the maximum");

        std::array<float, 100> displayP99{};
        displayP99.fill(DiagnosticsDisplayPercentile99Limit);
        displayP99.back() = DiagnosticsDisplayMaximumLimit;
        DiagnosticsErrorFrameView displayFrame = {
            displayP99.data(), allActive.data(), displayP99.size() };
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Display,
                DiagnosticsErrorDomain::Spatial,
                &displayFrame,
                1u,
                gate) == DiagnosticsInputStatus::Success &&
                gate.passed && Near(gate.pooled.percentile99,
                    DiagnosticsDisplayPercentile99Limit) &&
                Near(gate.pooled.maximum,
                    DiagnosticsDisplayMaximumLimit),
            "display p99 and maximum boundaries pass inclusively");
        displayP99.fill(0.f);
        displayP99.back() = 1.f;
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Display,
                DiagnosticsErrorDomain::Spatial,
                &displayFrame,
                1u,
                gate) == DiagnosticsInputStatus::Success &&
                gate.passed && gate.pooled.percentile99 == 0.f &&
                Near(gate.pooled.maximum, 1.f),
            "exactly one percent of display samples may exceed p99 below maximum");
        displayP99[98] = std::nextafter(
            DiagnosticsDisplayPercentile99Limit,
            std::numeric_limits<float>::infinity());
        displayP99[99] = displayP99[98];
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Display,
                DiagnosticsErrorDomain::Spatial,
                &displayFrame,
                1u,
                gate) == DiagnosticsInputStatus::Success &&
                !gate.passed &&
                gate.pooled.percentile99 >
                    DiagnosticsDisplayPercentile99Limit &&
                gate.pooled.maximum < DiagnosticsDisplayMaximumLimit,
            "greater than one percent display error fails the nearest-rank p99 gate");
        displayP99.fill(0.f);
        displayP99.back() = std::nextafter(
            DiagnosticsDisplayMaximumLimit,
            std::numeric_limits<float>::infinity());
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Display,
                DiagnosticsErrorDomain::Spatial,
                &displayFrame,
                1u,
                gate) == DiagnosticsInputStatus::Success &&
                !gate.passed &&
                gate.pooled.percentile99 == 0.f &&
                gate.pooled.maximum > DiagnosticsDisplayMaximumLimit,
            "one display pixel above two codes fails the full-frame maximum");
        displayP99[0] = std::numeric_limits<float>::quiet_NaN();
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Display,
                DiagnosticsErrorDomain::Spatial,
                &displayFrame,
                1u,
                gate) == DiagnosticsInputStatus::NonFiniteValue &&
                !gate.valid,
            "frame gating invalidates active non-finite input");

        const float badSmallFrameError = 1.f;
        const uint8_t oneActive = 1u;
        std::array<float, 199> quietLargeFrame{};
        std::array<uint8_t, 199> quietLargeMask{};
        quietLargeMask.fill(1u);
        const DiagnosticsErrorFrameView mixedFrames[2] = {
            { &badSmallFrameError, &oneActive, 1u },
            { quietLargeFrame.data(), quietLargeMask.data(),
                quietLargeFrame.size() }
        };
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Display,
                DiagnosticsErrorDomain::Temporal,
                mixedFrames,
                2u,
                gate) == DiagnosticsInputStatus::Success &&
                !gate.passed && !gate.perFramePassed && gate.pooledPassed &&
                gate.failedFrameCount == 1u &&
                gate.firstFailedFrameIndex == 0u &&
                gate.activeSampleCount == 200u,
            "per-frame gating catches an error that pooled p99 would dilute");

        const std::array<float, 2> zeroErrors{};
        const std::array<uint8_t, 2> zeroMask{};
        const DiagnosticsErrorFrameView emptyFrame = {
            zeroErrors.data(), zeroMask.data(), zeroErrors.size() };
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Spatial,
                &emptyFrame,
                1u,
                gate) == DiagnosticsInputStatus::EmptyInput && !gate.valid,
            "frame gating rejects an empty active population");
        const std::array<uint8_t, 2> badGateMask = { 1u, 2u };
        const DiagnosticsErrorFrameView badMaskFrame = {
            zeroErrors.data(), badGateMask.data(), zeroErrors.size() };
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Spatial,
                &badMaskFrame,
                1u,
                gate) == DiagnosticsInputStatus::InvalidActiveMask &&
                !gate.valid,
            "frame gating rejects noncanonical active-mask values");
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Count,
                DiagnosticsErrorDomain::Spatial,
                &hdrFrame,
                1u,
                gate) == DiagnosticsInputStatus::InvalidErrorValueDomain &&
            EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Count,
                &hdrFrame,
                1u,
                gate) == DiagnosticsInputStatus::InvalidErrorDomain,
            "frame gating rejects either invalid domain without conflation");
        Check(EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Spatial,
                nullptr,
                1u,
                gate) == DiagnosticsInputStatus::NullInput &&
            EvaluateDiagnosticsErrorFrames(
                DiagnosticsErrorValueDomain::Hdr,
                DiagnosticsErrorDomain::Spatial,
                &hdrFrame,
                0u,
                gate) == DiagnosticsInputStatus::EmptyInput,
            "frame gating distinguishes null and empty frame input");
    }

    void TestWholeGroupErrorBudgetRejection()
    {
        std::array<float, MaximumAdaptiveSampleCount> hdrErrors{};
        std::array<float, MaximumAdaptiveSampleCount> displayErrors{};
        hdrErrors.fill(DiagnosticsHdrPercentile99Limit);
        displayErrors.fill(DiagnosticsDisplayPercentile99Limit);
        const auto originalHdr = hdrErrors;
        const auto originalDisplay = displayErrors;

        DiagnosticsGroupRejectionDecision decision;
        for (uint32_t memberCount = 1u;
            memberCount <= MaximumAdaptiveSampleCount;
            ++memberCount)
        {
            Check(DecideDiagnosticsGroupRejection(
                    hdrErrors.data(),
                    displayErrors.data(),
                    memberCount,
                    decision) == DiagnosticsInputStatus::Success &&
                    decision.valid && !decision.rejectWholeGroup &&
                    decision.memberCount == memberCount &&
                    decision.reason ==
                        DiagnosticsErrorBudgetRejectionReason::None &&
                    decision.firstFailingMember ==
                        InvalidDiagnosticsSampleIndex &&
                    hdrErrors == originalHdr &&
                    displayErrors == originalDisplay,
                "every bounded whole-group size passes exact member limits without mutation");
        }

        hdrErrors[7] = std::nextafter(
            DiagnosticsHdrPercentile99Limit,
            std::numeric_limits<float>::infinity());
        Check(DecideDiagnosticsGroupRejection(
                hdrErrors.data(), displayErrors.data(), 16u, decision) ==
                    DiagnosticsInputStatus::Success &&
                decision.valid && decision.rejectWholeGroup &&
                decision.reason == DiagnosticsErrorBudgetRejectionReason::
                    HdrLimitExceeded &&
                decision.firstFailingMember == 7u,
            "one HDR member above budget rejects the complete group");
        hdrErrors[5] = hdrErrors[7];
        Check(DecideDiagnosticsGroupRejection(
                hdrErrors.data(), displayErrors.data(), 16u, decision) ==
                    DiagnosticsInputStatus::Success &&
                decision.reason == DiagnosticsErrorBudgetRejectionReason::
                    HdrLimitExceeded &&
                decision.firstFailingMember == 5u,
            "multiple HDR failures select the lowest member index deterministically");

        hdrErrors.fill(0.f);
        displayErrors.fill(0.f);
        displayErrors[2] = std::nextafter(
            DiagnosticsDisplayPercentile99Limit,
            std::numeric_limits<float>::infinity());
        Check(DecideDiagnosticsGroupRejection(
                hdrErrors.data(), displayErrors.data(), 16u, decision) ==
                    DiagnosticsInputStatus::Success &&
                decision.rejectWholeGroup &&
                decision.reason == DiagnosticsErrorBudgetRejectionReason::
                    DisplayLimitExceeded &&
                decision.firstFailingMember == 2u,
            "one display member above budget rejects the complete group");

        hdrErrors[9] = 1.f;
        Check(DecideDiagnosticsGroupRejection(
                hdrErrors.data(), displayErrors.data(), 16u, decision) ==
                    DiagnosticsInputStatus::Success &&
                decision.reason == DiagnosticsErrorBudgetRejectionReason::
                    HdrLimitExceeded &&
                decision.firstFailingMember == 9u,
            "HDR rejection has deterministic domain precedence over display");

        displayErrors[3] = std::numeric_limits<float>::quiet_NaN();
        Check(DecideDiagnosticsGroupRejection(
                hdrErrors.data(), displayErrors.data(), 16u, decision) ==
                    DiagnosticsInputStatus::Success &&
                decision.reason == DiagnosticsErrorBudgetRejectionReason::
                    NonFiniteValue &&
                decision.firstFailingMember == 3u,
            "non-finite member error deterministically rejects before finite limits");
        hdrErrors[1] = std::numeric_limits<float>::infinity();
        Check(DecideDiagnosticsGroupRejection(
                hdrErrors.data(), displayErrors.data(), 16u, decision) ==
                    DiagnosticsInputStatus::Success &&
                decision.reason == DiagnosticsErrorBudgetRejectionReason::
                    NonFiniteValue &&
                decision.firstFailingMember == 1u,
            "the lowest non-finite member is stable across HDR and display domains");

        hdrErrors.fill(0.f);
        displayErrors.fill(0.f);
        hdrErrors[0] = -0.1f;
        Check(DecideDiagnosticsGroupRejection(
                hdrErrors.data(), displayErrors.data(), 4u, decision) ==
                    DiagnosticsInputStatus::NegativeValue &&
                !decision.valid,
            "negative group error magnitude is invalid rather than reclassified");
        hdrErrors[0] = 0.f;
        Check(DecideDiagnosticsGroupRejection(
                nullptr, displayErrors.data(), 4u, decision) ==
                    DiagnosticsInputStatus::NullInput &&
            DecideDiagnosticsGroupRejection(
                hdrErrors.data(), nullptr, 4u, decision) ==
                    DiagnosticsInputStatus::NullInput &&
            DecideDiagnosticsGroupRejection(
                hdrErrors.data(), displayErrors.data(), 0u, decision) ==
                    DiagnosticsInputStatus::InvalidGroupCount &&
            DecideDiagnosticsGroupRejection(
                hdrErrors.data(),
                displayErrors.data(),
                MaximumAdaptiveSampleCount + 1u,
                decision) == DiagnosticsInputStatus::InvalidGroupCount,
            "group rejection fails closed on null or unbounded input");
    }

    void TestAutomaticHdrGroupGate()
    {
        std::array<float, MaximumAdaptiveSampleCount> hdrErrors{};
        hdrErrors.fill(DiagnosticsHdrGroupLimit);
        DiagnosticsGroupRejectionDecision decision;
        for (uint32_t memberCount : { 2u, 4u, 8u, 16u })
        {
            Check(DecideDiagnosticsHdrGroupRejection(
                    hdrErrors.data(), memberCount, decision) ==
                        DiagnosticsInputStatus::Success &&
                    decision.valid && !decision.rejectWholeGroup &&
                    decision.memberCount == memberCount &&
                    decision.reason ==
                        DiagnosticsErrorBudgetRejectionReason::None &&
                    decision.firstFailingMember ==
                        InvalidDiagnosticsSampleIndex,
                "the automatic HDR group gate accepts exact immutable-limit equality");
        }

        hdrErrors[6] = std::nextafter(
            DiagnosticsHdrGroupLimit,
            std::numeric_limits<float>::infinity());
        Check(DecideDiagnosticsHdrGroupRejection(
                hdrErrors.data(), 8u, decision) ==
                    DiagnosticsInputStatus::Success &&
                decision.rejectWholeGroup &&
                decision.reason == DiagnosticsErrorBudgetRejectionReason::
                    HdrLimitExceeded &&
                decision.firstFailingMember == 6u &&
                decision.memberCount == 8u,
            "one strict HDR exceedance rejects every member of the candidate group");

        hdrErrors[1] = std::numeric_limits<float>::quiet_NaN();
        Check(DecideDiagnosticsHdrGroupRejection(
                hdrErrors.data(), 8u, decision) ==
                    DiagnosticsInputStatus::Success &&
                decision.rejectWholeGroup &&
                decision.reason == DiagnosticsErrorBudgetRejectionReason::
                    NonFiniteValue &&
                decision.firstFailingMember == 1u,
            "the lowest non-finite member takes precedence over finite HDR exceedance");

        hdrErrors.fill(0.f);
        hdrErrors[0] = -0.01f;
        Check(DecideDiagnosticsHdrGroupRejection(
                hdrErrors.data(), 4u, decision) ==
                    DiagnosticsInputStatus::NegativeValue &&
                !decision.valid &&
            DecideDiagnosticsHdrGroupRejection(
                nullptr, 4u, decision) ==
                    DiagnosticsInputStatus::NullInput &&
            DecideDiagnosticsHdrGroupRejection(
                hdrErrors.data(), 0u, decision) ==
                    DiagnosticsInputStatus::InvalidGroupCount &&
            DecideDiagnosticsHdrGroupRejection(
                hdrErrors.data(),
                MaximumAdaptiveSampleCount + 1u,
                decision) == DiagnosticsInputStatus::InvalidGroupCount,
            "the automatic HDR gate fails closed on negative, null, or unbounded input");
    }

    void TestSelectedPixelBoundsAndIdentity()
    {
        for (uint32_t sampleCount : { 2u, 4u, 8u, 16u })
        {
            std::array<
                SelectedPixelSampleRecord,
                MaximumAdaptiveSampleCount> samples{};
            for (uint32_t sampleIndex = 0u;
                sampleIndex < sampleCount;
                ++sampleIndex)
            {
                samples[sampleIndex] = MakeSelectedSample(
                    sampleIndex,
                    sampleCount);
            }

            SelectedPixelRecord record;
            const uint64_t captureId = 0x100u + sampleCount;
            Check(StoreSelectedPixelRecord(
                    record,
                    captureId,
                    77u,
                    123u,
                    456u,
                    samples.data(),
                    sampleCount) == DiagnosticsInputStatus::Success &&
                    record.valid &&
                    record.captureId == captureId &&
                    record.frameIndex == 77u &&
                    record.pixelX == 123u &&
                    record.pixelY == 456u &&
                    record.sampleCount == sampleCount,
                "selected-pixel record accepts each exact adaptive sample count");
            for (uint32_t sampleIndex = 0u;
                sampleIndex < sampleCount;
                ++sampleIndex)
            {
                Check(SameSurface(
                        record.samples[sampleIndex].surfaceHit,
                        samples[sampleIndex].surfaceHit) &&
                        record.samples[sampleIndex].packedKey
                                .geometryInstanceHandle ==
                            samples[sampleIndex].packedKey
                                .geometryInstanceHandle &&
                        record.samples[sampleIndex].groupIndex ==
                            sampleIndex &&
                        record.samples[sampleIndex]
                                .representativeSampleIndex ==
                            sampleIndex &&
                        Near(record.samples[sampleIndex].weight,
                            1.f / float(sampleCount)) &&
                        Near(record.samples[sampleIndex].radiance.z,
                            float(sampleIndex) + 0.5f),
                    "inspector preserves existing key/hit, group, representative, weight, and radiance contracts");
            }
            ResetSelectedPixelRecord(record);
            Check(!record.valid && record.sampleCount == 0u,
                "selected-pixel reset invalidates the complete record");
        }

        std::array<SelectedPixelSampleRecord, MaximumAdaptiveSampleCount>
            samples{};
        for (uint32_t index = 0u; index < MaximumAdaptiveSampleCount; ++index)
            samples[index] = MakeSelectedSample(index, MaximumAdaptiveSampleCount);

        for (uint32_t unsupported : { 0u, 1u, 3u, 5u, 15u, 17u })
        {
            SelectedPixelRecord record;
            record.valid = true;
            Check(StoreSelectedPixelRecord(
                    record, 1u, 2u, 3u, 4u,
                    samples.data(), unsupported) ==
                        DiagnosticsInputStatus::UnsupportedSampleCount &&
                    !record.valid && record.sampleCount == 0u,
                "unsupported inspector sample counts fail to an empty record");
        }

        SelectedPixelRecord record;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, nullptr, 4u) ==
                    DiagnosticsInputStatus::NullInput &&
                !record.valid,
            "null inspector input fails closed");

        auto invalid = samples;
        invalid[1].sampleIndex = 0u;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::InvalidSampleIndex,
            "inspector sample order is exact and deterministic");
        invalid = samples;
        invalid[0].groupIndex = 4u;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::InvalidGroupCount,
            "inspector group index is bounded by sample count");
        invalid = samples;
        invalid[0].representativeSampleIndex = 4u;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::InvalidSampleIndex,
            "inspector representative index is bounded by sample count");
        invalid = samples;
        invalid[0].representativeSampleIndex = 1u;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::InvalidSampleIndex,
            "inspector representative must belong to the same group");
        invalid = samples;
        invalid[1].groupIndex = 0u;
        invalid[1].representativeSampleIndex = 0u;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::Success,
            "multiple samples may name one deterministic covered representative");

        invalid = samples;
        invalid[0].covered = false;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::InvalidSampleIndex,
            "uncovered samples cannot retain surface/group assignments");
        invalid[0].groupIndex = InvalidDiagnosticsSampleIndex;
        invalid[0].representativeSampleIndex =
            InvalidDiagnosticsSampleIndex;
        invalid[0].surfaceHitValid = false;
        invalid[0].fallbackReason = FallbackReason::None;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::Success &&
                !record.samples[0].covered,
            "explicit uncovered samples preserve background inspector state");

        invalid = samples;
        invalid[0].surfaceHitValid = false;
        invalid[0].packedKey = {};
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::InvalidFallbackReason,
            "covered invalid hits require an explicit fallback reason");
        invalid[0].fallbackReason = FallbackReason::GenerationMismatch;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::Success &&
                record.samples[0].fallbackReason ==
                    FallbackReason::GenerationMismatch,
            "covered stale-key samples remain inspectable through existing reasons");

        invalid = samples;
        invalid[0].packedKey = {};
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::InvalidSampleIndex,
            "a valid logical hit cannot claim an invalid packed key");
        invalid = samples;
        invalid[0].fallbackReason = FallbackReason::Count;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::InvalidFallbackReason,
            "inspector rejects fallback sentinel enum");

        invalid = samples;
        invalid[0].linearDepth =
            std::numeric_limits<float>::quiet_NaN();
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::NonFiniteValue,
            "inspector rejects non-finite depth");
        invalid = samples;
        invalid[0].surfaceHit.barycentrics[0] =
            std::numeric_limits<float>::infinity();
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::NonFiniteValue,
            "inspector rejects non-finite barycentrics");
        invalid = samples;
        invalid[0].radiance.z =
            std::numeric_limits<float>::quiet_NaN();
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::NonFiniteValue,
            "inspector rejects non-finite radiance");
        invalid = samples;
        invalid[0].weight = -0.1f;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::NegativeValue,
            "inspector rejects negative weights");
        invalid[0].weight = 1.1f;
        Check(StoreSelectedPixelRecord(
                record, 1u, 2u, 3u, 4u, invalid.data(), 4u) ==
                    DiagnosticsInputStatus::InvalidGroupCount,
            "inspector rejects weights above one");
    }
}

int main()
{
    TestReferenceComparisonRequests();
    TestDefaultsSanitizationAndBenchmarkGate();
    TestCounterAccountingAndSaturation();
    TestDeterministicErrorSummaries();
    TestFrozenErrorFormulasAndLimits();
    TestFourDomainsActiveMasksAndGating();
    TestWholeGroupErrorBudgetRejection();
    TestAutomaticHdrGroupGate();
    TestSelectedPixelBoundsAndIdentity();

    if (g_Passed)
        std::cout << "UVSR adaptive MSAA diagnostics validation passed\n";
    return g_Passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
