#include "diagnostic_cascaded_shadow_map.h"
#include "diagnostic_csm_benchmark.h"
#include "gpu_performance_monitor.h"

#include <donut/core/math/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace uvsr;
using namespace donut::math;

namespace
{
    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(float actual, float expected, float tolerance = 1e-5f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    bool NearDouble(
        double actual,
        double expected,
        double tolerance = 1e-9)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    void TestGpuTimingNormalization()
    {
        constexpr std::string_view adapterName =
            Rtx4090LaptopGpuTimingNormalizationCalibration
                .exactAdapterName;
        GpuPerformanceMetrics metrics;
        metrics.memoryBandwidthGBps =
            GpuTimingNormalizationReferenceBandwidthGBps;
        metrics.gpuGFlops = 34700.0;
        metrics.gpuUtilization = 1.0;
        metrics.gpuUtilizationValid = true;
        metrics.telemetryAgeMilliseconds = 0.0;
        metrics.telemetryGeneration = 1u;
        metrics.valid = true;

        const GpuTimingNormalizationEstimate first =
            NormalizeGpuTimingMilliseconds(
                1.510,
                metrics,
                adapterName);
        Require(first.valid,
            "finite calibrated same-adapter GPU timing must normalize");
        Require(NearDouble(first.rawMilliseconds, 1.510),
            "normalization must preserve the exact raw GPU timing");
        Require(NearDouble(
                first.estimatedMilliseconds,
                1.510 * 34.7 / 42.5),
            "normalization must scale raw time by current-clock capacity");
        Require(NearDouble(
                first.workIndexMillisecondsTFlops,
                1.510 * 34.7),
            "normalization must retain the same-GPU work index");
        Require(first.grade == GpuTimingNormalizationGrade::B,
            "a saturated 34.7-TFLOPS sample must receive grade B");

        metrics.gpuGFlops = 32700.0;
        const GpuTimingNormalizationEstimate repeated =
            NormalizeGpuTimingMilliseconds(
                1.602,
                metrics,
                adapterName);
        Require(std::abs(
                first.estimatedMilliseconds -
                repeated.estimatedMilliseconds) < 0.001,
            "historical equal-work hot samples must normalize together");

        metrics.gpuGFlops = 42500.0;
        const GpuTimingNormalizationEstimate identity =
            NormalizeGpuTimingMilliseconds(
                1.266,
                metrics,
                adapterName);
        Require(NearDouble(identity.estimatedMilliseconds, 1.266),
            "reference throughput must preserve raw GPU time");
        Require(identity.grade == GpuTimingNormalizationGrade::A,
            "a saturated reference-clock sample must receive grade A");

        metrics.gpuGFlops = 45820.0;
        const GpuTimingNormalizationEstimate healthyBoost =
            NormalizeGpuTimingMilliseconds(
                1.125,
                metrics,
                adapterName);
        Require(healthyBoost.grade ==
                GpuTimingNormalizationGrade::A,
            "a saturated healthy boost clock near the reference must receive grade A");

        metrics.gpuGFlops = 48000.0;
        Require(NormalizeGpuTimingMilliseconds(
                    1.0, metrics, adapterName).grade ==
                GpuTimingNormalizationGrade::B,
            "a clock outside the calibrated grade-A range must lower confidence");

        metrics.gpuGFlops = 42500.0;
        metrics.gpuUtilization = 0.5;
        const GpuTimingNormalizationEstimate underutilized =
            NormalizeGpuTimingMilliseconds(
                1.266,
                metrics,
                adapterName);
        Require(NearDouble(
                underutilized.estimatedMilliseconds,
                identity.estimatedMilliseconds),
            "utilization must never change the normalized estimate");
        Require(NearDouble(underutilized.utilizedTFlops, 21.25),
            "utilized TFLOPS must remain separate diagnostic context");
        Require(underutilized.grade ==
                GpuTimingNormalizationGrade::C,
            "underutilized clock data must lower confidence without rejection");

        metrics.gpuGFlops = 24000.0;
        metrics.gpuUtilization = 1.0;
        Require(NormalizeGpuTimingMilliseconds(
                    1.0, metrics, adapterName).grade ==
                GpuTimingNormalizationGrade::Directional,
            "sub-25-TFLOPS results must remain directional");

        metrics.gpuGFlops = 42500.0;
        Require(!NormalizeGpuTimingMilliseconds(
                    0.0, metrics, adapterName).valid,
            "zero GPU time must be rejected as an invalid benchmark sample");
        Require(!NormalizeGpuTimingMilliseconds(
                -1.0, metrics, adapterName).valid,
            "negative GPU time must be rejected");
        Require(!NormalizeGpuTimingMilliseconds(
                std::numeric_limits<double>::quiet_NaN(),
                metrics,
                adapterName).valid,
            "non-finite GPU time must be rejected");
        Require(!NormalizeGpuTimingMilliseconds(
                std::numeric_limits<double>::infinity(),
                metrics,
                adapterName).valid,
            "infinite GPU time must be rejected");
        Require(!NormalizeGpuTimingMilliseconds(
                1.0,
                metrics,
                "NVIDIA GeForce RTX 4090 Laptop GPU ").valid,
            "adapter names must match their calibration exactly");
        Require(!NormalizeGpuTimingMilliseconds(
                1.0,
                metrics,
                "Intel(R) Arc(TM) Graphics").valid,
            "uncalibrated adapters must not normalize");
        Require(FindGpuTimingNormalizationCalibration(adapterName) !=
                nullptr &&
                FindGpuTimingNormalizationCalibration("RTX 4090") ==
                nullptr,
            "calibration lookup must accept only its exact adapter name");

        metrics.memoryBandwidthGBps =
            GpuTimingNormalizationReferenceBandwidthGBps * 1.051;
        Require(NormalizeGpuTimingMilliseconds(
                    1.0, metrics, adapterName).grade ==
                GpuTimingNormalizationGrade::C,
            "more than five percent bandwidth drift must cap confidence at C");
        metrics.memoryBandwidthGBps =
            GpuTimingNormalizationReferenceBandwidthGBps * 1.151;
        Require(NormalizeGpuTimingMilliseconds(
                    1.0, metrics, adapterName).grade ==
                GpuTimingNormalizationGrade::Directional,
            "more than fifteen percent bandwidth drift must be directional");
        metrics.memoryBandwidthGBps =
            GpuTimingNormalizationReferenceBandwidthGBps;
        metrics.telemetryAgeMilliseconds =
            GpuTimingNormalizationMaximumFreshAgeMilliseconds + 0.1;
        Require(NormalizeGpuTimingMilliseconds(
                    1.0, metrics, adapterName).grade ==
                GpuTimingNormalizationGrade::C,
            "stale telemetry must cap confidence at C");
        metrics.telemetryAgeMilliseconds =
            GpuTimingNormalizationMaximumUsableAgeMilliseconds + 0.1;
        Require(NormalizeGpuTimingMilliseconds(
                    1.0, metrics, adapterName).grade ==
                GpuTimingNormalizationGrade::Directional,
            "telemetry beyond the usable age must be directional");
        metrics.telemetryAgeMilliseconds = 0.0;
        metrics.gpuUtilizationValid = false;
        Require(NormalizeGpuTimingMilliseconds(
                    1.0, metrics, adapterName).grade ==
                GpuTimingNormalizationGrade::C,
            "missing utilization must never be replaced by implicit full utilization");
        metrics.gpuUtilizationValid = true;

        metrics.gpuGFlops = 0.0;
        Require(!NormalizeGpuTimingMilliseconds(
                1.0, metrics, adapterName).valid,
            "zero clock capacity must be rejected");
        metrics.gpuGFlops =
            std::numeric_limits<double>::infinity();
        Require(!NormalizeGpuTimingMilliseconds(
                1.0, metrics, adapterName).valid,
            "infinite clock capacity must be rejected");
        metrics.gpuGFlops =
            std::numeric_limits<double>::quiet_NaN();
        Require(!NormalizeGpuTimingMilliseconds(
                1.0, metrics, adapterName).valid,
            "non-finite clock capacity must be rejected");
        metrics.gpuGFlops = 42500.0;
        metrics.telemetryGeneration = 0u;
        Require(!NormalizeGpuTimingMilliseconds(
                1.0, metrics, adapterName).valid,
            "telemetry without a generation must be rejected");
        metrics.telemetryGeneration = 1u;
        metrics.telemetryAgeMilliseconds =
            std::numeric_limits<double>::quiet_NaN();
        Require(!NormalizeGpuTimingMilliseconds(
                1.0, metrics, adapterName).valid,
            "non-finite telemetry age must be rejected");
        metrics.telemetryAgeMilliseconds = 0.0;
        metrics.memoryBandwidthGBps = 0.0;
        Require(!NormalizeGpuTimingMilliseconds(
                1.0, metrics, adapterName).valid,
            "zero memory bandwidth must be rejected");
        metrics.memoryBandwidthGBps =
            GpuTimingNormalizationReferenceBandwidthGBps;
        metrics.valid = false;
        Require(!NormalizeGpuTimingMilliseconds(
                1.0, metrics, adapterName).valid,
            "invalid adapter telemetry must be rejected");

        std::array<uint32_t, 5u> grades{};
        grades[size_t(GpuTimingNormalizationGrade::A)] = 19u;
        grades[size_t(GpuTimingNormalizationGrade::B)] = 1u;
        Require(SummarizeGpuTimingNormalizationRunGrade(
                    20u, grades).grade ==
                GpuTimingNormalizationGrade::A,
            "ninety-five percent grade-A coverage must produce run grade A");
        grades = {};
        grades[size_t(GpuTimingNormalizationGrade::A)] = 18u;
        grades[size_t(GpuTimingNormalizationGrade::B)] = 1u;
        grades[size_t(GpuTimingNormalizationGrade::C)] = 1u;
        Require(SummarizeGpuTimingNormalizationRunGrade(
                    20u, grades).grade ==
                GpuTimingNormalizationGrade::B,
            "ninety-five percent grade-A-or-B coverage must produce run grade B");
        grades = {};
        grades[size_t(GpuTimingNormalizationGrade::A)] = 18u;
        grades[size_t(GpuTimingNormalizationGrade::C)] = 1u;
        grades[size_t(GpuTimingNormalizationGrade::Directional)] = 1u;
        Require(SummarizeGpuTimingNormalizationRunGrade(
                    20u, grades).grade ==
                GpuTimingNormalizationGrade::C,
            "ninety-five percent grade-A-through-C coverage must produce run grade C");
        grades = {};
        grades[size_t(GpuTimingNormalizationGrade::A)] = 18u;
        grades[size_t(GpuTimingNormalizationGrade::Directional)] = 2u;
        const GpuTimingNormalizationRunGrade directional =
            SummarizeGpuTimingNormalizationRunGrade(20u, grades);
        Require(directional.grade ==
                GpuTimingNormalizationGrade::Directional &&
                NearDouble(directional.validFraction, 1.0),
            "more than five percent directional samples must make the run directional");
        grades = {};
        grades[size_t(GpuTimingNormalizationGrade::A)] = 18u;
        const GpuTimingNormalizationRunGrade incomplete =
            SummarizeGpuTimingNormalizationRunGrade(20u, grades);
        Require(NearDouble(incomplete.validFraction, 0.9) &&
                incomplete.grade ==
                    GpuTimingNormalizationGrade::Directional,
            "run summaries must report incomplete normalization coverage");
    }

    void TestDiagnosticCsmBenchmarkTimingValidation()
    {
        DiagnosticCsmTimings valid;
        valid.supported = true;
        valid.active = true;
        valid.gpuTimingSource =
            DiagnosticCsmGpuTimingSource::TimerQuery;
        valid.totalMilliseconds = 1.f;
        valid.totalCpuMilliseconds = 1.f;
        Require(IsValidDiagnosticCsmBenchmarkTiming(valid),
            "finite positive timer-query results must be benchmarkable");

        const auto requireInvalid = [&valid](
            auto mutate,
            const std::string& message)
        {
            DiagnosticCsmTimings timings = valid;
            mutate(timings);
            Require(!IsValidDiagnosticCsmBenchmarkTiming(timings),
                message);
        };
        requireInvalid([](auto& value) {
            value.totalMilliseconds = 0.f;
        }, "zero total GPU time must be rejected by the recorder");
        requireInvalid([](auto& value) {
            value.totalMilliseconds = -0.1f;
        }, "negative total GPU time must be rejected by the recorder");
        requireInvalid([](auto& value) {
            value.totalMilliseconds =
                std::numeric_limits<float>::quiet_NaN();
        }, "NaN total GPU time must be rejected by the recorder");
        requireInvalid([](auto& value) {
            value.totalMilliseconds =
                std::numeric_limits<float>::infinity();
        }, "infinite total GPU time must be rejected by the recorder");
        requireInvalid([](auto& value) {
            value.totalMilliseconds =
                -std::numeric_limits<float>::infinity();
        }, "negative-infinite total GPU time must be rejected");

        const auto requireInvalidFloatField = [&valid](
            auto select,
            const std::string& field)
        {
            for (const float invalid : {
                    -0.1f,
                    std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity() })
            {
                DiagnosticCsmTimings timings = valid;
                select(timings) = invalid;
                Require(!IsValidDiagnosticCsmBenchmarkTiming(timings),
                    field + " must reject invalid values");
            }
        };
        requireInvalidFloatField([](auto& value) -> float& {
            return value.setupCpuMilliseconds;
        }, "setup CPU timing");
        requireInvalidFloatField([](auto& value) -> float& {
            return value.cullingCpuMilliseconds;
        }, "culling CPU timing");
        requireInvalidFloatField([](auto& value) -> float& {
            return value.recordingCpuMilliseconds;
        }, "recording CPU timing");
        requireInvalidFloatField([](auto& value) -> float& {
            return value.totalCpuMilliseconds;
        }, "total CPU timing");
        requireInvalidFloatField([](auto& value) -> float& {
            return value.cullingGpuMilliseconds;
        }, "culling GPU timing");
        requireInvalidFloatField([](auto& value) -> float& {
            return value.clearUpdateMilliseconds;
        }, "clear/update GPU timing");
        requireInvalidFloatField([](auto& value) -> float& {
            return value.rasterMilliseconds;
        }, "raster GPU timing");
        requireInvalidFloatField([](auto& value) -> float& {
            return value.samplingMilliseconds;
        }, "sampling GPU timing");

        DiagnosticCsmTimings cpuTimings = valid;
        cpuTimings.setupCpuMilliseconds = 1.1f;
        Require(!IsValidDiagnosticCsmBenchmarkTiming(cpuTimings),
            "a nested CSM CPU stage cannot exceed its total interval");
        cpuTimings.setupCpuMilliseconds = 0.4f;
        cpuTimings.cullingCpuMilliseconds = 0.4f;
        cpuTimings.recordingCpuMilliseconds = 0.4f;
        Require(!IsValidDiagnosticCsmBenchmarkTiming(cpuTimings),
            "nested CSM CPU stages cannot sum beyond their total");
        cpuTimings.setupCpuMilliseconds = 0.5f;
        cpuTimings.cullingCpuMilliseconds = 0.5f;
        cpuTimings.recordingCpuMilliseconds = 0.00005f;
        Require(IsValidDiagnosticCsmBenchmarkTiming(cpuTimings),
            "CPU stage rounding within the documented tolerance must pass");

        DiagnosticCsmTimings timings = valid;
        timings.cullingGpuMilliseconds = 0.1f;
        Require(!IsValidDiagnosticCsmBenchmarkTiming(timings),
            "total-only timing must reject stale positive stage data");
        timings.detailedGpuTimingEnabled = true;
        Require(IsValidDiagnosticCsmBenchmarkTiming(timings),
            "detailed timing may contain finite nonnegative stage data");
        timings.cullingGpuMilliseconds = 1.1f;
        Require(!IsValidDiagnosticCsmBenchmarkTiming(timings),
            "a nested CSM stage cannot exceed its total GPU interval");
        timings.cullingGpuMilliseconds = 0.3f;
        timings.clearUpdateMilliseconds = 0.3f;
        timings.rasterMilliseconds = 0.3f;
        timings.samplingMilliseconds = 0.2f;
        Require(!IsValidDiagnosticCsmBenchmarkTiming(timings),
            "nested CSM stage intervals cannot sum beyond their total");
        timings.samplingMilliseconds = 0.1f;
        Require(IsValidDiagnosticCsmBenchmarkTiming(timings),
            "nested CSM stage intervals may exactly fill their total");
        timings.gpuTimingSource =
            DiagnosticCsmGpuTimingSource::KnownZero;
        Require(!IsValidDiagnosticCsmBenchmarkTiming(timings),
            "CPU fallback timing must never enter GPU benchmark results");
        timings.gpuTimingSource =
            DiagnosticCsmGpuTimingSource::TimerQuery;
        timings.supported = false;
        Require(!IsValidDiagnosticCsmBenchmarkTiming(timings),
            "unsupported timing must be rejected");
    }

    void TestDiagnosticCsmBenchmarkWorkIdentity()
    {
        const DiagnosticCsmStats baseline;
        Require(HasSameDiagnosticCsmBenchmarkWorkIdentity(
                    baseline, baseline),
            "identical CSM work records must compare equal");

        uint32_t DiagnosticCsmStats::* uint32Fields[] = {
            &DiagnosticCsmStats::outputWidth,
            &DiagnosticCsmStats::outputHeight,
            &DiagnosticCsmStats::cascadeCount,
            &DiagnosticCsmStats::shadowMapResolution,
            &DiagnosticCsmStats::depthBitsPerTexel,
            &DiagnosticCsmStats::filterSampleCount,
            &DiagnosticCsmStats::filterComparisonCount,
            &DiagnosticCsmStats::candidateCasterProjectionPairs,
            &DiagnosticCsmStats::coarseCasterProjectionPairs,
            &DiagnosticCsmStats::accuratelyCulledCasterProjectionPairs,
            &DiagnosticCsmStats::radiusCulledCasterProjectionPairs,
            &DiagnosticCsmStats::renderedCasterProjectionPairs,
            &DiagnosticCsmStats::alphaTestedCasterProjectionPairs,
            &DiagnosticCsmStats::submittedDrawCalls,
            &DiagnosticCsmStats::submittedAlphaTestedDrawCalls,
            &DiagnosticCsmStats::submittedTranslationOnlyDrawCalls,
            &DiagnosticCsmStats::manualCasterProjectionPairs,
            &DiagnosticCsmStats::inputAssemblerCasterProjectionPairs,
            &DiagnosticCsmStats::casterSceneTraversals,
            &DiagnosticCsmStats::casterSorts,
            &DiagnosticCsmStats::cachedShadowDrawListHits,
            &DiagnosticCsmStats::cachedShadowDrawListMisses,
            &DiagnosticCsmStats::cachedShadowDrawListEntries,
            &DiagnosticCsmStats::cachedShadowDrawListCasterProjectionPairs,
            &DiagnosticCsmStats::reusedCascades,
            &DiagnosticCsmStats::scrolledCascades,
            &DiagnosticCsmStats::dirtyCascades,
            &DiagnosticCsmStats::redrawnCascades,
            &DiagnosticCsmStats::dirtyRectangleCount,
            &DiagnosticCsmStats::receiverRasterScissoredCascades,
            &DiagnosticCsmStats::invalidationMask
        };
        for (const auto field : uint32Fields)
        {
            DiagnosticCsmStats changed = baseline;
            changed.*field = 1u;
            Require(!HasSameDiagnosticCsmBenchmarkWorkIdentity(
                        baseline, changed),
                "every uint32 CSM workload fact must affect identity");
        }

        uint64_t DiagnosticCsmStats::* uint64Fields[] = {
            &DiagnosticCsmStats::submittedInstances,
            &DiagnosticCsmStats::submittedTriangles,
            &DiagnosticCsmStats::submittedTranslationOnlyTriangles,
            &DiagnosticCsmStats::logicalTexels,
            &DiagnosticCsmStats::updatedTexels,
            &DiagnosticCsmStats::copiedTexels,
            &DiagnosticCsmStats::clearedTexels,
            &DiagnosticCsmStats::fullRedrawRasterBoundTexels,
            &DiagnosticCsmStats::fullRedrawRasterExcludedTexels,
            &DiagnosticCsmStats::depthBytes,
            &DiagnosticCsmStats::visibilityBytes,
            &DiagnosticCsmStats::debugVisualizationBytes,
            &DiagnosticCsmStats::scrollingScratchBytes
        };
        for (const auto field : uint64Fields)
        {
            DiagnosticCsmStats changed = baseline;
            changed.*field = 1u;
            Require(!HasSameDiagnosticCsmBenchmarkWorkIdentity(
                        baseline, changed),
                "every uint64 CSM workload fact must affect identity");
        }

        float DiagnosticCsmStats::* floatFields[] = {
            &DiagnosticCsmStats::maximumShadowDistance,
            &DiagnosticCsmStats::maximumLightDepth,
            &DiagnosticCsmStats::maximumActualLightDepthSpan,
            &DiagnosticCsmStats::filterRadiusTexels,
            &DiagnosticCsmStats::finestCoverageExtent,
            &DiagnosticCsmStats::coarsestCoverageExtent,
            &DiagnosticCsmStats::finestWorldTexelSize,
            &DiagnosticCsmStats::coarsestWorldTexelSize,
            &DiagnosticCsmStats::casterRadiusThreshold
        };
        for (const auto field : floatFields)
        {
            DiagnosticCsmStats changed = baseline;
            changed.*field = 1.f;
            Require(!HasSameDiagnosticCsmBenchmarkWorkIdentity(
                        baseline, changed),
                "every floating-point CSM workload fact must affect identity");
        }

        bool DiagnosticCsmStats::* boolFields[] = {
            &DiagnosticCsmStats::submissionStatsAvailable,
            &DiagnosticCsmStats::opaqueDepthStateMergingEnabled,
            &DiagnosticCsmStats::positionOnlyOpaqueEnabled,
            &DiagnosticCsmStats::translationOnlyCasterTransformRequested,
            &DiagnosticCsmStats::translationOnlyCasterTransformEnabled,
            &DiagnosticCsmStats::inputAssemblerCasterFetchRequested,
            &DiagnosticCsmStats::inputAssemblerCasterFetchEnabled,
            &DiagnosticCsmStats::precomputedDepthAxisInverseLengthRequested,
            &DiagnosticCsmStats::precomputedDepthAxisInverseLengthEnabled,
            &DiagnosticCsmStats::conservativeSaturatedSlopeRequested,
            &DiagnosticCsmStats::conservativeSaturatedSlopeActive,
            &DiagnosticCsmStats::algebraicSlowSlopeRequested,
            &DiagnosticCsmStats::algebraicSlowSlopeActive,
            &DiagnosticCsmStats::
                preNormalizedReceiverLightDirectionRequested,
            &DiagnosticCsmStats::
                preNormalizedReceiverLightDirectionEnabled,
            &DiagnosticCsmStats::precomposedClipToShadowRequested,
            &DiagnosticCsmStats::precomposedClipToShadowEnabled,
            &DiagnosticCsmStats::accurateCasterCullingRequested,
            &DiagnosticCsmStats::accurateCasterCullingEnabled,
            &DiagnosticCsmStats::ueCasterRadiusThresholdRequested,
            &DiagnosticCsmStats::ueCasterRadiusThresholdEnabled,
            &DiagnosticCsmStats::
                singleTraversalCasterClassificationRequested,
            &DiagnosticCsmStats::
                singleTraversalCasterClassificationEnabled,
            &DiagnosticCsmStats::precomputedReceiverHullAxesRequested,
            &DiagnosticCsmStats::precomputedReceiverHullAxesEnabled,
            &DiagnosticCsmStats::sharedCasterLightProjectionRequested,
            &DiagnosticCsmStats::sharedCasterLightProjectionEnabled,
            &DiagnosticCsmStats::directCasterSubmissionRequested,
            &DiagnosticCsmStats::directCasterSubmissionEnabled,
            &DiagnosticCsmStats::cachedShadowDrawListsRequested,
            &DiagnosticCsmStats::cachedShadowDrawListsActive,
            &DiagnosticCsmStats::batchedFullRedrawClearRequested,
            &DiagnosticCsmStats::batchedFullRedrawClearActive,
            &DiagnosticCsmStats::receiverRasterScissorRequested,
            &DiagnosticCsmStats::receiverRasterScissorEnabled
        };
        for (const auto field : boolFields)
        {
            DiagnosticCsmStats changed = baseline;
            changed.*field = true;
            Require(!HasSameDiagnosticCsmBenchmarkWorkIdentity(
                        baseline, changed),
                "every Boolean CSM workload fact must affect identity");
        }

        DiagnosticCsmStats changed = baseline;
        changed.light = reinterpret_cast<
            const donut::engine::DirectionalLight*>(uintptr_t{ 1u });
        Require(!HasSameDiagnosticCsmBenchmarkWorkIdentity(
                    baseline, changed),
            "the exact CSM light pointer must affect workload identity");
        changed = baseline;
        changed.filter = DiagnosticCsmFilter::Poisson;
        Require(!HasSameDiagnosticCsmBenchmarkWorkIdentity(
                    baseline, changed),
            "the CSM receiver filter must affect workload identity");
        changed = baseline;
        changed.cascadeActions[0] =
            DiagnosticCsmUpdateAction::FullRedraw;
        Require(!HasSameDiagnosticCsmBenchmarkWorkIdentity(
                    baseline, changed),
            "per-cascade update actions must affect workload identity");
    }

    void TestDiagnosticCsmBenchmarkEvidenceValidation()
    {
        Require(AreDiagnosticCsmBenchmarkSourceFramesConsecutive(
                    7u, 8u),
            "adjacent source frames must be consecutive");
        Require(!AreDiagnosticCsmBenchmarkSourceFramesConsecutive(
                    7u, 7u) &&
                !AreDiagnosticCsmBenchmarkSourceFramesConsecutive(
                    8u, 7u) &&
                !AreDiagnosticCsmBenchmarkSourceFramesConsecutive(
                    std::numeric_limits<uint64_t>::max(), 0u),
            "duplicate, backward, and wrapping source frames must fail");
        Require(IsValidDiagnosticCsmBenchmarkIssuedFrameContext(
                    true, 0.f) &&
                IsValidDiagnosticCsmBenchmarkIssuedFrameContext(
                    true, 16.6f),
            "available finite nonnegative issue contexts must be valid");
        Require(!IsValidDiagnosticCsmBenchmarkIssuedFrameContext(
                    false, 16.6f) &&
                !IsValidDiagnosticCsmBenchmarkIssuedFrameContext(
                    true, -0.1f) &&
                !IsValidDiagnosticCsmBenchmarkIssuedFrameContext(
                    true,
                    std::numeric_limits<float>::quiet_NaN()) &&
                !IsValidDiagnosticCsmBenchmarkIssuedFrameContext(
                    true,
                    std::numeric_limits<float>::infinity()),
            "missing or invalid issue contexts must fail closed");

        DiagnosticCsmBenchmarkEvidence evidence;
        evidence.expectedSampleCount = 1024u;
        evidence.sampleCount = 1024u;
        evidence.maximumSourceFrameGap = 1u;
        evidence.executableIdentityValid = true;
        evidence.timingConfigurationIdentityValid = true;
        evidence.timingsValid = true;
        evidence.detailedGpuTimingModeUniform = true;
        evidence.detailedGpuTimingModeMatchesConfiguration = true;
        evidence.workIdentityStable = true;
        evidence.sourceFrameArithmeticValid = true;
        evidence.sourceFramesStrictlyConsecutive = true;
        evidence.issuedFrameContextsValid = true;
        Require(IsDiagnosticCsmBenchmarkEvidenceAuthoritative(evidence),
            "complete frame/query evidence must be authoritative");
        Require(IsDiagnosticCsmBenchmarkRawAuthoritative(
                    "complete", evidence) &&
                !IsDiagnosticCsmBenchmarkRawAuthoritative(
                    "running", evidence) &&
                !IsDiagnosticCsmBenchmarkRawAuthoritative(
                    "aborted", evidence),
            "only a complete valid artifact may claim raw authority");

        const auto requireNonAuthoritative = [&evidence](
            auto mutate,
            const std::string& message)
        {
            DiagnosticCsmBenchmarkEvidence changed = evidence;
            mutate(changed);
            Require(!IsDiagnosticCsmBenchmarkEvidenceAuthoritative(
                    changed),
                message);
            Require(!IsDiagnosticCsmBenchmarkRawAuthoritative(
                    "complete", changed),
                message + " must clear raw authority");
        };
        requireNonAuthoritative([](auto& value) {
            value.expectedSampleCount = 0u;
        }, "an empty expected sample set");
        requireNonAuthoritative([](auto& value) {
            value.sampleCount = 1023u;
        }, "an incomplete sample set");
        requireNonAuthoritative([](auto& value) {
            value.missingIssuedFrameContexts = 1u;
        }, "a missing issued-frame context");
        requireNonAuthoritative([](auto& value) {
            value.sourceFrameGapEvents = 1u;
        }, "a source-frame gap event");
        requireNonAuthoritative([](auto& value) {
            value.missingSourceFrames = 1u;
        }, "a missing source frame");
        requireNonAuthoritative([](auto& value) {
            value.maximumSourceFrameGap = 2u;
        }, "a source-frame gap larger than one");
        requireNonAuthoritative([](auto& value) {
            value.maximumSourceFrameGap = 0u;
        }, "an impossible zero maximum gap for multiple samples");
        requireNonAuthoritative([](auto& value) {
            value.executableIdentityValid = false;
        }, "missing executable identity");
        requireNonAuthoritative([](auto& value) {
            value.timingConfigurationIdentityValid = false;
        }, "missing timing-configuration identity");
        requireNonAuthoritative([](auto& value) {
            value.timingsValid = false;
        }, "invalid timing data");
        requireNonAuthoritative([](auto& value) {
            value.detailedGpuTimingModeUniform = false;
        }, "mixed detailed timing modes");
        requireNonAuthoritative([](auto& value) {
            value.detailedGpuTimingModeMatchesConfiguration = false;
        }, "a timing mode that differs from the frozen configuration");
        requireNonAuthoritative([](auto& value) {
            value.workIdentityStable = false;
        }, "changing CSM work identity");
        requireNonAuthoritative([](auto& value) {
            value.sourceFrameArithmeticValid = false;
        }, "overflowing source-frame arithmetic");
        requireNonAuthoritative([](auto& value) {
            value.sourceFramesStrictlyConsecutive = false;
        }, "nonconsecutive source frames");
        requireNonAuthoritative([](auto& value) {
            value.issuedFrameContextsValid = false;
        }, "invalid issued-frame context values");
    }

    void TestGpuTimingSourceFrameClassification()
    {
        DiagnosticCsmTimings timings;
        Require(ClassifyDiagnosticCsmTimingSourceFrame(
                    0u, false, timings) ==
                DiagnosticCsmTimingFrameOrder::Unavailable,
            "unavailable GPU timing must not form a benchmark sample");

        timings.gpuTimingSource =
            DiagnosticCsmGpuTimingSource::TimerQuery;
        Require(ClassifyDiagnosticCsmTimingSourceFrame(
                    0u, false, timings) ==
                DiagnosticCsmTimingFrameOrder::New,
            "timer-query source frame zero must be accepted when it is the first published frame");
        Require(ClassifyDiagnosticCsmTimingSourceFrame(
                    0u, true, timings) ==
                DiagnosticCsmTimingFrameOrder::Duplicate,
            "a repeated timer-query source frame zero must deduplicate");

        timings.gpuTimingSourceFrame = 7u;
        Require(ClassifyDiagnosticCsmTimingSourceFrame(
                    0u, false, timings) ==
                DiagnosticCsmTimingFrameOrder::New,
            "the first valid timer source frame must be accepted");
        Require(ClassifyDiagnosticCsmTimingSourceFrame(
                    7u, true, timings) ==
                DiagnosticCsmTimingFrameOrder::Duplicate,
            "a repeated published timer source frame must deduplicate");
        Require(ClassifyDiagnosticCsmTimingSourceFrame(
                    8u, true, timings) ==
                DiagnosticCsmTimingFrameOrder::OutOfOrder,
            "a backward timer source frame must invalidate recording");
        Require(ClassifyDiagnosticCsmTimingSourceFrame(
                    6u, true, timings) ==
                DiagnosticCsmTimingFrameOrder::New,
            "a strictly newer timer source frame must be accepted");
    }

    std::array<float, 12u> MakeTranslationOnlyShaderTransform(
        float translationX,
        float translationY,
        float translationZ)
    {
        return {
            1.f, 0.f, 0.f, translationX,
            0.f, 1.f, 0.f, translationY,
            0.f, 0.f, 1.f, translationZ
        };
    }

    struct DiagnosticCsmNormalTransformTestResult
    {
        std::array<float, 3u> worldNormal{};
        bool valid = false;
    };

    DiagnosticCsmNormalTransformTestResult
        EvaluateDiagnosticCsmUeStyleNormalForTest(
            const std::array<float, 12u>& shaderTransform,
            const std::array<float, 3u>& objectNormal)
    {
        DiagnosticCsmNormalTransformTestResult result;
        const std::array<std::array<float, 3u>, 3u> axes = {{
            {
                shaderTransform[0],
                shaderTransform[4],
                shaderTransform[8]
            },
            {
                shaderTransform[1],
                shaderTransform[5],
                shaderTransform[9]
            },
            {
                shaderTransform[2],
                shaderTransform[6],
                shaderTransform[10]
            }
        }};
        std::array<float, 3u> inverseAxisLength{};
        for (size_t component = 0u; component < 3u; ++component)
        {
            if (!std::isfinite(objectNormal[component]))
                return result;
            const float lengthSquared =
                axes[component][0] * axes[component][0] +
                axes[component][1] * axes[component][1] +
                axes[component][2] * axes[component][2];
            if (!std::isfinite(lengthSquared) ||
                !(lengthSquared > 1e-12f))
            {
                return result;
            }
            inverseAxisLength[component] =
                1.f / std::sqrt(lengthSquared);
        }

        const std::array<float, 3u> scaledNormal = {
            objectNormal[0] * inverseAxisLength[0],
            objectNormal[1] * inverseAxisLength[1],
            objectNormal[2] * inverseAxisLength[2]
        };
        for (size_t row = 0u; row < 3u; ++row)
        {
            result.worldNormal[row] =
                shaderTransform[row * 4u] * scaledNormal[0] +
                shaderTransform[row * 4u + 1u] * scaledNormal[1] +
                shaderTransform[row * 4u + 2u] * scaledNormal[2];
            if (!std::isfinite(result.worldNormal[row]))
                return {};
        }
        const float worldLengthSquared =
            result.worldNormal[0] * result.worldNormal[0] +
            result.worldNormal[1] * result.worldNormal[1] +
            result.worldNormal[2] * result.worldNormal[2];
        result.valid = std::isfinite(worldLengthSquared) &&
            worldLengthSquared > 1e-12f;
        if (!result.valid)
            result.worldNormal = {};
        return result;
    }

    float EvaluateDiagnosticCsmSlopeForTest(
        const std::array<float, 3u>& worldNormal,
        const std::array<float, 3u>& normalizedDepthAxis,
        float maximumSlope)
    {
        const float normalLengthSquared =
            worldNormal[0] * worldNormal[0] +
            worldNormal[1] * worldNormal[1] +
            worldNormal[2] * worldNormal[2];
        if (!std::isfinite(normalLengthSquared) ||
            !(normalLengthSquared > 1e-12f))
        {
            return maximumSlope;
        }
        const float projectedMagnitude = std::abs(
            worldNormal[0] * normalizedDepthAxis[0] +
            worldNormal[1] * normalizedDepthAxis[1] +
            worldNormal[2] * normalizedDepthAxis[2]);
        if (!(projectedMagnitude > 0.f))
            return maximumSlope;
        return std::clamp(
            std::sqrt(std::max(
                normalLengthSquared -
                    projectedMagnitude * projectedMagnitude,
                0.f)) /
                projectedMagnitude,
            0.f,
            maximumSlope);
    }

    void TestUeStyleCasterNormalTransform()
    {
        constexpr float inverseSquareRootTwo =
            0.7071067811865475244f;
        const std::array<float, 3u> diagonalNormal = {
            inverseSquareRootTwo,
            inverseSquareRootTwo,
            0.f
        };
        const std::array<float, 3u> depthAxis = { 1.f, 0.f, 0.f };

        const auto identity = EvaluateDiagnosticCsmUeStyleNormalForTest(
            MakeTranslationOnlyShaderTransform(4.f, -2.f, 7.f),
            diagonalNormal);
        Require(identity.valid &&
                Near(identity.worldNormal[0], diagonalNormal[0]) &&
                Near(identity.worldNormal[1], diagonalNormal[1]) &&
                Near(identity.worldNormal[2], diagonalNormal[2]),
            "identity-linear transforms must preserve the caster normal and ignore translation");

        const std::array<float, 12u> nonUniformScale = {
            2.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f
        };
        const auto corrected = EvaluateDiagnosticCsmUeStyleNormalForTest(
            nonUniformScale,
            diagonalNormal);
        Require(corrected.valid &&
                Near(corrected.worldNormal[0], inverseSquareRootTwo) &&
                Near(corrected.worldNormal[1], inverseSquareRootTwo) &&
                Near(EvaluateDiagnosticCsmSlopeForTest(
                    corrected.worldNormal, depthAxis, 1.f), 1.f),
            "UE-style inverse nonuniform scale must preserve the intended normal direction and full slope");
        const std::array<float, 3u> rawScaledNormal = {
            2.f * inverseSquareRootTwo,
            inverseSquareRootTwo,
            0.f
        };
        Require(Near(EvaluateDiagnosticCsmSlopeForTest(
                    rawScaledNormal, depthAxis, 1.f), 0.5f),
            "the prior raw normal transform must demonstrate the nonuniform-scale half-slope defect");

        const std::array<float, 12u> rotatedNonUniformScale = {
            0.f, -1.f, 0.f, 0.f,
            2.f, 0.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f
        };
        const auto rotated = EvaluateDiagnosticCsmUeStyleNormalForTest(
            rotatedNonUniformScale,
            diagonalNormal);
        Require(rotated.valid &&
                Near(rotated.worldNormal[0], -inverseSquareRootTwo) &&
                Near(rotated.worldNormal[1], inverseSquareRootTwo),
            "rotated nonuniform scale must normalize matrix columns before rotating the normal");

        const std::array<float, 12u> reflectedUniformScale = {
            -2.f, 0.f, 0.f, 0.f,
            0.f, 2.f, 0.f, 0.f,
            0.f, 0.f, 2.f, 0.f
        };
        const auto reflected = EvaluateDiagnosticCsmUeStyleNormalForTest(
            reflectedUniformScale,
            diagonalNormal);
        Require(reflected.valid &&
                Near(reflected.worldNormal[0], -inverseSquareRootTwo) &&
                Near(reflected.worldNormal[1], inverseSquareRootTwo),
            "reflected uniform scale must preserve basis signs while removing scale magnitude");

        const std::array<float, 12u> shear = {
            1.f, 0.5f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f
        };
        const auto sheared = EvaluateDiagnosticCsmUeStyleNormalForTest(
            shear,
            { 0.f, 1.f, 0.f });
        Require(sheared.valid &&
                Near(sheared.worldNormal[0], 0.4472136f) &&
                Near(sheared.worldNormal[1], 0.8944272f),
            "shear must follow UE's normalized local-basis policy consistently");

        std::array<float, 12u> invalid = nonUniformScale;
        invalid[5] = 0.f;
        Require(!EvaluateDiagnosticCsmUeStyleNormalForTest(
                    invalid, diagonalNormal).valid,
            "a singular transform must reject its normal and use maximum slope");
        invalid = nonUniformScale;
        invalid[0] = std::numeric_limits<float>::quiet_NaN();
        Require(!EvaluateDiagnosticCsmUeStyleNormalForTest(
                    invalid, diagonalNormal).valid,
            "a non-finite transform must reject its normal and use maximum slope");
        invalid = nonUniformScale;
        invalid[10] = std::numeric_limits<float>::infinity();
        Require(!EvaluateDiagnosticCsmUeStyleNormalForTest(
                    invalid, diagonalNormal).valid,
            "an infinite transform must reject its normal and use maximum slope");
    }

    bool SameSettings(
        const DiagnosticCascadedShadowMapSettings& left,
        const DiagnosticCascadedShadowMapSettings& right)
    {
        return left.enabled == right.enabled &&
            left.profile == right.profile &&
            left.cascadeCount == right.cascadeCount &&
            left.shadowMapResolution == right.shadowMapResolution &&
            left.maximumShadowDistance == right.maximumShadowDistance &&
            left.maximumLightDepth == right.maximumLightDepth &&
            left.cascadeDistributionExponent ==
                right.cascadeDistributionExponent &&
            left.cascadeTransitionFraction ==
                right.cascadeTransitionFraction &&
            left.shadowDistanceFadeoutFraction ==
                right.shadowDistanceFadeoutFraction &&
            left.projectionSnapTexelMultiple ==
                right.projectionSnapTexelMultiple &&
            left.enforceUeMinimumLightDepth ==
                right.enforceUeMinimumLightDepth &&
            left.depthBias == right.depthBias &&
            left.slopeScaledDepthBias == right.slopeScaledDepthBias &&
            left.directionalLightShadowBias ==
                right.directionalLightShadowBias &&
            left.directionalLightShadowSlopeBias ==
                right.directionalLightShadowSlopeBias &&
            left.receiverDepthBias == right.receiverDepthBias &&
            left.filter == right.filter &&
            left.poissonTapCount == right.poissonTapCount &&
            left.filterRadiusTexels == right.filterRadiusTexels &&
            left.use16BitDepthEnabled == right.use16BitDepthEnabled &&
            left.opaqueDepthStateMergingEnabled ==
                right.opaqueDepthStateMergingEnabled &&
            left.positionOnlyOpaqueEnabled ==
                right.positionOnlyOpaqueEnabled &&
            left.translationOnlyCasterTransformEnabled ==
                right.translationOnlyCasterTransformEnabled &&
            left.inputAssemblerCasterFetchEnabled ==
                right.inputAssemblerCasterFetchEnabled &&
            left.precomputedDepthAxisInverseLengthEnabled ==
                right.precomputedDepthAxisInverseLengthEnabled &&
            left.conservativeSaturatedSlopeEnabled ==
                right.conservativeSaturatedSlopeEnabled &&
            left.algebraicSlowSlopeEnabled ==
                right.algebraicSlowSlopeEnabled &&
            left.preNormalizedReceiverLightDirectionEnabled ==
                right.preNormalizedReceiverLightDirectionEnabled &&
            left.precomposedClipToShadowEnabled ==
                right.precomposedClipToShadowEnabled &&
            left.accurateCasterCullingEnabled ==
                right.accurateCasterCullingEnabled &&
            left.ueCasterRadiusThresholdEnabled ==
                right.ueCasterRadiusThresholdEnabled &&
            left.casterRadiusThreshold == right.casterRadiusThreshold &&
            left.singleTraversalCasterClassificationEnabled ==
                right.singleTraversalCasterClassificationEnabled &&
            left.precomputedReceiverHullAxesEnabled ==
                right.precomputedReceiverHullAxesEnabled &&
            left.sharedCasterLightProjectionEnabled ==
                right.sharedCasterLightProjectionEnabled &&
            left.directCasterSubmissionEnabled ==
                right.directCasterSubmissionEnabled &&
            left.cachedShadowDrawListsEnabled ==
                right.cachedShadowDrawListsEnabled &&
            left.batchedFullRedrawClearEnabled ==
                right.batchedFullRedrawClearEnabled &&
            left.receiverRasterScissorEnabled ==
                right.receiverRasterScissorEnabled &&
            left.wholeMapReuseEnabled == right.wholeMapReuseEnabled &&
            left.wholeCascadeReuseEnabled ==
                right.wholeCascadeReuseEnabled &&
            left.dirtyRectanglesEnabled ==
                right.dirtyRectanglesEnabled &&
            left.scrollingEnabled == right.scrollingEnabled &&
            left.minimumScrollOverlap == right.minimumScrollOverlap &&
            left.detailedGpuTimingEnabled ==
                right.detailedGpuTimingEnabled &&
            left.debugView == right.debugView;
    }

    void RequireRect(
        const DiagnosticCsmRect& actual,
        const DiagnosticCsmRect& expected,
        const std::string& message)
    {
        Require(
            actual.minX == expected.minX &&
                actual.maxX == expected.maxX &&
                actual.minY == expected.minY &&
                actual.maxY == expected.maxY,
            message);
    }

    void TestProfilesAndCustomRetention()
    {
        DiagnosticCascadedShadowMapSettings current;
        current.enabled = true;
        current.profile = DiagnosticCsmProfile::Custom;
        current.cascadeCount = 3u;
        current.shadowMapResolution = 1024u;
        current.maximumShadowDistance = 3456.f;
        current.maximumLightDepth = 7890.f;
        current.cascadeDistributionExponent = 7.f;
        current.cascadeTransitionFraction = 0.4f;
        current.shadowDistanceFadeoutFraction = 0.6f;
        current.projectionSnapTexelMultiple = 7u;
        current.enforceUeMinimumLightDepth = false;
        current.depthBias = 41;
        current.slopeScaledDepthBias = 9.f;
        current.directionalLightShadowBias = 0.25f;
        current.directionalLightShadowSlopeBias = 0.75f;
        current.receiverDepthBias = 0.125f;
        current.filter = DiagnosticCsmFilter::Poisson;
        current.poissonTapCount = 4u;
        current.filterRadiusTexels = 8.f;
        current.use16BitDepthEnabled = false;
        current.opaqueDepthStateMergingEnabled = false;
        current.positionOnlyOpaqueEnabled = false;
        current.translationOnlyCasterTransformEnabled = false;
        current.inputAssemblerCasterFetchEnabled = true;
        current.precomputedDepthAxisInverseLengthEnabled = false;
        current.conservativeSaturatedSlopeEnabled = false;
        current.algebraicSlowSlopeEnabled = false;
        current.preNormalizedReceiverLightDirectionEnabled = false;
        current.precomposedClipToShadowEnabled = false;
        current.accurateCasterCullingEnabled = false;
        current.ueCasterRadiusThresholdEnabled = false;
        current.casterRadiusThreshold = 0.025f;
        current.singleTraversalCasterClassificationEnabled = false;
        current.precomputedReceiverHullAxesEnabled = false;
        current.sharedCasterLightProjectionEnabled = false;
        current.directCasterSubmissionEnabled = false;
        current.cachedShadowDrawListsEnabled = false;
        current.batchedFullRedrawClearEnabled = false;
        current.receiverRasterScissorEnabled = false;
        current.wholeMapReuseEnabled = true;
        current.wholeCascadeReuseEnabled = true;
        current.dirtyRectanglesEnabled = true;
        current.scrollingEnabled = true;
        current.minimumScrollOverlap = 0.2f;
        current.detailedGpuTimingEnabled = false;
        current.debugView = DiagnosticCsmDebugView::CacheAction;

        struct ProfileExpectation
        {
            DiagnosticCsmProfile profile;
            const char* label;
            uint32_t cascadeCount;
            bool wholeMapReuse;
            bool wholeCascadeReuse;
            bool dirtyRectangles;
            bool scrolling;
            bool cachedShadowDrawLists;
        };

        const std::array<ProfileExpectation, 6u> expectations = {{
            {
                DiagnosticCsmProfile::SingleMapReference,
                "Single-Map Reference",
                1u, false, false, false, false, false
            },
            {
                DiagnosticCsmProfile::LowCostCsm,
                "Low-Cost CSM",
                2u, false, false, false, false, true
            },
            {
                DiagnosticCsmProfile::Ue5CsmReference,
                "UE5 CSM Reference",
                4u, false, false, false, false, true
            },
            {
                DiagnosticCsmProfile::CachedSingleShadow,
                "Cached Single Shadow",
                1u, true, false, false, false, true
            },
            {
                DiagnosticCsmProfile::OptimizedCachedSingleShadow,
                "Optimized Cached Single Shadow",
                1u, false, true, true, false, true
            },
            {
                DiagnosticCsmProfile::OptimizedCachedCsm,
                "Optimized Cached CSM",
                4u, false, true, true, true, true
            }
        }};

        for (const ProfileExpectation& expected : expectations)
        {
            const DiagnosticCascadedShadowMapSettings settings =
                ApplyDiagnosticCsmProfile(current, expected.profile);
            const std::string context = expected.label;

            Require(settings.profile == expected.profile,
                context + " must select its named profile");
            Require(std::string(GetDiagnosticCsmProfileLabel(
                    settings.profile)) == expected.label,
                context + " must retain its exact UI label");
            Require(settings.cascadeCount == expected.cascadeCount,
                context + " must select its exact cascade count");
            Require(settings.wholeMapReuseEnabled ==
                    expected.wholeMapReuse,
                context + " must select its exact whole-map policy");
            Require(settings.wholeCascadeReuseEnabled ==
                    expected.wholeCascadeReuse,
                context + " must select its exact whole-cascade policy");
            Require(settings.dirtyRectanglesEnabled ==
                    expected.dirtyRectangles,
                context + " must select its exact dirty-rectangle policy");
            Require(settings.scrollingEnabled == expected.scrolling,
                context + " must select its exact scrolling policy");
            Require(settings.cachedShadowDrawListsEnabled ==
                    expected.cachedShadowDrawLists,
                context +
                    " must select its exact cached shadow draw-list policy");

            Require(settings.shadowMapResolution ==
                    DiagnosticCsmDefaultResolution &&
                    settings.cascadeDistributionExponent == 4.f &&
                    settings.cascadeTransitionFraction == 0.1f &&
                    settings.shadowDistanceFadeoutFraction == 0.1f &&
                    settings.projectionSnapTexelMultiple == 4u &&
                    settings.enforceUeMinimumLightDepth,
                context + " must restore the common UE projection tuple");
            Require(settings.depthBias == 10 &&
                    settings.slopeScaledDepthBias == 3.f &&
                    settings.directionalLightShadowBias == 0.5f &&
                    settings.directionalLightShadowSlopeBias == 0.5f &&
                    settings.receiverDepthBias == 0.9f,
                context + " must restore the common depth-bias tuple");
            Require(settings.filter == DiagnosticCsmFilter::Ue5Pcf5x5 &&
                    settings.poissonTapCount == 16u &&
                    settings.filterRadiusTexels == 3.f,
                context + " must restore the common filtering tuple");
            Require(settings.use16BitDepthEnabled &&
                    settings.opaqueDepthStateMergingEnabled &&
                    settings.positionOnlyOpaqueEnabled &&
                     settings.translationOnlyCasterTransformEnabled &&
                     !settings.inputAssemblerCasterFetchEnabled &&
                     settings.precomputedDepthAxisInverseLengthEnabled &&
                     settings.conservativeSaturatedSlopeEnabled &&
                     settings.algebraicSlowSlopeEnabled &&
                     settings.preNormalizedReceiverLightDirectionEnabled &&
                     settings.precomposedClipToShadowEnabled &&
                     settings.accurateCasterCullingEnabled &&
                     settings.ueCasterRadiusThresholdEnabled &&
                     settings.singleTraversalCasterClassificationEnabled &&
                     settings.precomputedReceiverHullAxesEnabled &&
                     settings.sharedCasterLightProjectionEnabled &&
                     settings.directCasterSubmissionEnabled &&
                     settings.batchedFullRedrawClearEnabled &&
                     settings.receiverRasterScissorEnabled &&
                    settings.casterRadiusThreshold == 0.01f,
                context +
                    " must restore UE's D3D12 depth and caster path");
            Require(settings.minimumScrollOverlap == 0.75f,
                context + " must restore UE's scrolling overlap threshold");

            Require(settings.enabled == current.enabled &&
                    settings.maximumShadowDistance ==
                        current.maximumShadowDistance &&
                    settings.maximumLightDepth == current.maximumLightDepth &&
                    settings.detailedGpuTimingEnabled ==
                        current.detailedGpuTimingEnabled &&
                    settings.debugView == current.debugView,
                context +
                    " must preserve scene-dependent and diagnostic controls");
        }

        const DiagnosticCascadedShadowMapSettings custom =
            ApplyDiagnosticCsmProfile(
                current, DiagnosticCsmProfile::Custom);
        Require(SameSettings(custom, current),
            "the Custom profile must retain every current setting");
        Require(std::string(GetDiagnosticCsmProfileLabel(
                DiagnosticCsmProfile::Custom)) == "(Custom)",
            "the Custom profile must retain its parenthesized UI label");

        const DiagnosticCascadedShadowMapSettings invalid =
            ApplyDiagnosticCsmProfile(
                current, DiagnosticCsmProfile::Count);
        Require(SameSettings(invalid, current),
            "an invalid profile must fail safely without changing settings");
    }

    void TestTranslationOnlyCasterTransformContract()
    {
        const std::array<float, 12u> baseline =
            MakeTranslationOnlyShaderTransform(2.5f, -3.25f, 7.75f);
        std::array<float, 3u> result{};
        Require(TryGetDiagnosticCsmTranslationOnlyTransform(
                    baseline, result),
            "an exact identity-linear transform with finite translation must be eligible");
        Require(result ==
                std::array<float, 3u>{ 2.5f, -3.25f, 7.75f },
            "eligible translation must preserve the shader-layout values exactly");

        auto RequireRejected = [](
            const std::array<float, 12u>& transform,
            const std::string& message)
        {
            std::array<float, 3u> rejected = { 11.f, 12.f, 13.f };
            Require(!TryGetDiagnosticCsmTranslationOnlyTransform(
                        transform, rejected),
                message);
            Require(rejected ==
                    std::array<float, 3u>{ 0.f, 0.f, 0.f },
                message + " and must clear stale output");
        };

        constexpr std::array<size_t, 3u> DiagonalIndices = {
            0u, 5u, 10u
        };
        for (size_t index : DiagonalIndices)
        {
            std::array<float, 12u> changed = baseline;
            changed[index] = std::nextafter(1.f, 2.f);
            RequireRejected(
                changed,
                "any non-identity diagonal must reject translation-only mode");

            changed = baseline;
            changed[index] = std::numeric_limits<float>::quiet_NaN();
            RequireRejected(
                changed,
                "a NaN linear diagonal must reject translation-only mode");

            changed = baseline;
            changed[index] = std::numeric_limits<float>::infinity();
            RequireRejected(
                changed,
                "an infinite linear diagonal must reject translation-only mode");
        }

        constexpr std::array<size_t, 6u> OffDiagonalIndices = {
            1u, 2u, 4u, 6u, 8u, 9u
        };
        for (size_t index : OffDiagonalIndices)
        {
            std::array<float, 12u> changed = baseline;
            changed[index] = -0.f;
            RequireRejected(
                changed,
                "negative-zero off-diagonals must reject translation-only mode");

            changed = baseline;
            changed[index] = 0.25f;
            RequireRejected(
                changed,
                "rotation or shear terms must reject translation-only mode");

            changed = baseline;
            changed[index] = std::numeric_limits<float>::quiet_NaN();
            RequireRejected(
                changed,
                "a NaN off-diagonal must reject translation-only mode");

            changed = baseline;
            changed[index] = std::numeric_limits<float>::infinity();
            RequireRejected(
                changed,
                "an infinite off-diagonal must reject translation-only mode");
        }

        {
            std::array<float, 12u> scaled = baseline;
            scaled[0] = 2.f;
            RequireRejected(
                scaled,
                "scale must reject translation-only mode");

            std::array<float, 12u> reflected = baseline;
            reflected[5] = -1.f;
            RequireRejected(
                reflected,
                "reflection must reject translation-only mode");

            std::array<float, 12u> sheared = baseline;
            sheared[4] = 0.5f;
            RequireRejected(
                sheared,
                "shear must reject translation-only mode");

            std::array<float, 12u> rotated = baseline;
            rotated[0] = 0.f;
            rotated[1] = -1.f;
            rotated[4] = 1.f;
            rotated[5] = 0.f;
            RequireRejected(
                rotated,
                "rotation must reject translation-only mode");
        }

        constexpr std::array<size_t, 3u> TranslationIndices = {
            3u, 7u, 11u
        };
        const std::array<float, 3u> nonFiniteValues = {
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()
        };
        for (size_t index : TranslationIndices)
        {
            for (float value : nonFiniteValues)
            {
                std::array<float, 12u> changed = baseline;
                changed[index] = value;
                RequireRejected(
                    changed,
                    "non-finite translation must reject translation-only mode");
            }
        }

        {
            const std::array<float, 12u> signedZeroTranslation =
                MakeTranslationOnlyShaderTransform(-0.f, 0.f, -0.f);
            std::array<float, 3u> signedZeroResult{};
            Require(TryGetDiagnosticCsmTranslationOnlyTransform(
                        signedZeroTranslation,
                        signedZeroResult),
                "finite signed-zero translation components must remain eligible");
            Require(std::signbit(signedZeroResult[0]) &&
                    !std::signbit(signedZeroResult[1]) &&
                    std::signbit(signedZeroResult[2]),
                "translation extraction must preserve signed-zero bits");
        }

        const std::array<std::array<float, 3u>, 4u> objectValues = {{
            {{ 1.f, 2.f, 3.f }},
            {{ -4.f, 8.f, -16.f }},
            {{ 0.5f, -0.25f, 0.125f }},
            {{ 1024.f, -2048.f, 4096.f }}
        }};
        for (const std::array<float, 3u>& objectValue : objectValues)
        {
            std::array<float, 3u> legacyPosition{};
            std::array<float, 3u> legacyNormal{};
            for (size_t row = 0u; row < 3u; ++row)
            {
                const size_t rowOffset = row * 4u;
                legacyPosition[row] =
                    objectValue[0] * baseline[rowOffset] +
                    objectValue[1] * baseline[rowOffset + 1u] +
                    objectValue[2] * baseline[rowOffset + 2u] +
                    baseline[rowOffset + 3u];
                legacyNormal[row] =
                    objectValue[0] * baseline[rowOffset] +
                    objectValue[1] * baseline[rowOffset + 1u] +
                    objectValue[2] * baseline[rowOffset + 2u];
            }
            const std::array<float, 3u> fastPosition = {
                objectValue[0] + result[0],
                objectValue[1] + result[1],
                objectValue[2] + result[2]
            };
            Require(legacyPosition == fastPosition,
                "translation-only position math must match the exact identity-linear reference");
            Require(legacyNormal == objectValue,
                "translation-only normal math must match the exact identity-linear reference");
        }

        auto RequireDecision = [](
            bool enabled,
            bool lookupCurrent,
            uint32_t instanceCount,
            bool expected,
            const std::string& message)
        {
            Require(ShouldUseDiagnosticCsmTranslationOnlyDraw(
                        enabled,
                        lookupCurrent,
                        instanceCount) == expected,
                message);
        };
        RequireDecision(
            true, true, 1u, true,
            "a current single-instance entry must select translation-only mode");
        RequireDecision(
            false, true, 1u, false,
            "the independent toggle must restore instance-buffer mode");
        RequireDecision(
            true, false, 1u, false,
            "a stale, absent, or ineligible lookup entry must restore instance-buffer mode");
        RequireDecision(
            true, true, 0u, false,
            "a zero-instance draw must not select translation-only mode");
        RequireDecision(
            true, true, 2u, false,
            "a merged draw must retain per-instance transforms");
        RequireDecision(
            true,
            true,
            std::numeric_limits<uint32_t>::max(),
            false,
            "an arbitrarily large instance count must retain per-instance transforms");

        uint32_t registryIndex = 99u;
        Require(!TryGetDiagnosticCsmTranslationRegistryIndex(
                    -1, 64u, registryIndex),
            "Donut's negative unindexed-instance sentinel must be rejected");
        Require(registryIndex == 0u,
            "a rejected signed instance index must clear its output");
        Require(!TryGetDiagnosticCsmTranslationRegistryIndex(
                    0, 0u, registryIndex),
            "an empty current-scene registry must reject every instance");
        Require(!TryGetDiagnosticCsmTranslationRegistryIndex(
                    64, 64u, registryIndex),
            "an instance index at the current-scene registry end must reject");
        Require(TryGetDiagnosticCsmTranslationRegistryIndex(
                    63, 64u, registryIndex),
            "the last indexed instance in the current scene must remain valid");
        Require(registryIndex == 63u,
            "a valid signed instance index must preserve its dense index");

        const std::array<bool, 5u> lookupCurrent = {
            true, false, true, false, true
        };
        const std::array<uint32_t, 5u> instanceCounts = {
            1u, 1u, 2u, 1u, 1u
        };
        const std::array<size_t, 5u> directOrder = {
            0u, 1u, 2u, 3u, 4u
        };
        const std::array<size_t, 5u> copiedOrder = {
            4u, 0u, 2u, 1u, 3u
        };
        const std::array<size_t, 3u> dirtyIndexedOrder = {
            4u, 2u, 0u
        };
        auto EvaluateOrder = [&lookupCurrent, &instanceCounts](
            const auto& order)
        {
            std::array<bool, 5u> decisions{};
            for (size_t sourceIndex : order)
            {
                decisions[sourceIndex] =
                    ShouldUseDiagnosticCsmTranslationOnlyDraw(
                        true,
                        lookupCurrent[sourceIndex],
                        instanceCounts[sourceIndex]);
            }
            return decisions;
        };
        const std::array<bool, 5u> directDecisions =
            EvaluateOrder(directOrder);
        const std::array<bool, 5u> copiedDecisions =
            EvaluateOrder(copiedOrder);
        Require(directDecisions == copiedDecisions,
            "direct and copied submission order must preserve every transform-mode decision");
        const std::array<bool, 5u> expectedDecisions = {
            true, false, false, false, true
        };
        Require(directDecisions == expectedDecisions,
            "lookup currency and instance merging must select the expected modes");
        const std::array<bool, 5u> dirtyDecisions =
            EvaluateOrder(dirtyIndexedOrder);
        for (size_t dirtyIndex : dirtyIndexedOrder)
        {
            Require(dirtyDecisions[dirtyIndex] ==
                    directDecisions[dirtyIndex],
                "dirty-indexed submission must preserve the full-redraw transform-mode decision");
        }
    }

    void TestCachedShadowDrawListContract()
    {
        DiagnosticCascadedShadowMapSettings settings =
            ApplyDiagnosticCsmProfile(
                DiagnosticCascadedShadowMapSettings{},
                DiagnosticCsmProfile::Ue5CsmReference);
        Require(IsDiagnosticCsmCachedShadowDrawListEligible(
                    settings, true),
            "the UE5 full-redraw profile must allow reliable cached shadow draw lists");
        Require(!IsDiagnosticCsmCachedShadowDrawListEligible(
                    settings, false),
            "an unreliable scene revision must fail the draw-list cache closed");

        DiagnosticCascadedShadowMapSettings changed = settings;
        changed.cachedShadowDrawListsEnabled = false;
        Require(!IsDiagnosticCsmCachedShadowDrawListEligible(
                    changed, true),
            "disabling cached shadow draw lists must select the zero-cache path");

        changed = settings;
        changed.wholeCascadeReuseEnabled = true;
        Require(!IsDiagnosticCsmCachedShadowDrawListEligible(
                    changed, true),
            "cached maps must take precedence over the redundant draw-list cache");

        Require(IsSameDiagnosticCsmDrawListConfiguration(
                    settings, settings),
            "an exact draw-list configuration must be reusable");
        changed = settings;
        changed.maximumShadowDistance += 1.f;
        Require(!IsSameDiagnosticCsmDrawListConfiguration(
                    settings, changed),
            "cascade-range changes must invalidate cached caster membership");
        changed = settings;
        changed.filterRadiusTexels += 1.f;
        Require(!IsSameDiagnosticCsmDrawListConfiguration(
                    settings, changed),
            "filter-footprint changes must invalidate cached caster membership");
        changed = settings;
        changed.accurateCasterCullingEnabled =
            !changed.accurateCasterCullingEnabled;
        Require(!IsSameDiagnosticCsmDrawListConfiguration(
                    settings, changed),
            "caster-culling changes must invalidate cached caster membership");

        Require(!IsDiagnosticCsmSceneStateChanged(
                    false, true, 17u, 17u),
            "an exact compatible scene revision must remain unchanged");
        Require(IsDiagnosticCsmSceneStateChanged(
                    true, true, 17u, 17u),
            "an explicit full-scene invalidation must be authoritative even when the revision is unchanged");
        Require(IsDiagnosticCsmSceneStateChanged(
                    false, false, 17u, 17u) &&
                IsDiagnosticCsmSceneStateChanged(
                    false, true, 18u, 17u),
            "an incompatible scene or changed revision must invalidate cached caster membership");
        Require(ShouldInvalidateDiagnosticCsmCachedShadowDrawLists(
                    true, true, true) &&
                ShouldInvalidateDiagnosticCsmCachedShadowDrawLists(
                    true, false, false),
            "full invalidation and unreliable revisions must discard enabled cached shadow draw lists");
        Require(!ShouldInvalidateDiagnosticCsmCachedShadowDrawLists(
                    false, true, false) &&
                !ShouldInvalidateDiagnosticCsmCachedShadowDrawLists(
                    true, false, true),
            "a disabled cache and a reliable unchanged scene must not perform redundant draw-list invalidation");
        Require(ShouldReleaseDiagnosticCsmCachedShadowDrawLists(
                    true, false) &&
                !ShouldReleaseDiagnosticCsmCachedShadowDrawLists(
                    false, false) &&
                !ShouldReleaseDiagnosticCsmCachedShadowDrawLists(
                    true, true),
            "a full-redraw to cached-map transition must release superseded draw-list storage exactly once");

        std::array<float, 3u> restoredTranslation{};
        const std::array<float, 3u> cachedTranslation{
            11.f, -7.f, 3.5f
        };
        Require(TryRestoreDiagnosticCsmCachedTranslationOnlyTransform(
                    true,
                    cachedTranslation,
                    restoredTranslation) &&
                restoredTranslation == cachedTranslation,
            "a cached translation-only caster must restore the exact shader-space translation");
        Require(!TryRestoreDiagnosticCsmCachedTranslationOnlyTransform(
                    false,
                    cachedTranslation,
                    restoredTranslation) &&
                restoredTranslation ==
                    std::array<float, 3u>{ 0.f, 0.f, 0.f },
            "a general transform must never enter the cached translation-only registry");
        const std::array<float, 3u> invalidTranslation{
            0.f,
            std::numeric_limits<float>::infinity(),
            0.f
        };
        Require(!TryRestoreDiagnosticCsmCachedTranslationOnlyTransform(
                    true,
                    invalidTranslation,
                    restoredTranslation) &&
                restoredTranslation ==
                    std::array<float, 3u>{ 0.f, 0.f, 0.f },
            "a non-finite cached translation must fail closed");

        std::array<bool,
            DiagnosticCsmCachedShadowDrawListSlotCount> valid{};
        std::array<uint64_t,
            DiagnosticCsmCachedShadowDrawListSlotCount> lastUse{};
        for (uint32_t slot = 0u;
            slot < DiagnosticCsmCachedShadowDrawListSlotCount;
            ++slot)
        {
            Require(SelectDiagnosticCsmCachedShadowDrawListSlot(
                        valid, lastUse) == slot,
                "the eight repeating TAA phases must warm the first free slot deterministically");
            valid[slot] = true;
            lastUse[slot] = uint64_t(slot + 10u);
        }
        lastUse[3] = 1u;
        Require(SelectDiagnosticCsmCachedShadowDrawListSlot(
                    valid, lastUse) == 3u,
            "a ninth exact key must evict the least-recently-used TAA entry");
    }

    void TestLightFrameAndCoarseBoundsHardening()
    {
        DiagnosticCsmLightFrame frame;
        Require(TryBuildDiagnosticCsmLightFrame(
                    { 0.0, 0.0, -4.0 },
                    { 0.0, 3.0, 0.0 },
                    { 2.0, 0.0, 0.0 },
                    frame),
            "uniformly scaled directional-light bases must be accepted");
        Require(NearDouble(frame.x[0], 1.0) &&
                NearDouble(frame.y[1], 1.0) &&
                NearDouble(frame.z[2], -1.0),
            "scaled directional-light bases must recover the canonical frame");

        Require(TryBuildDiagnosticCsmLightFrame(
                    { 0.0, 0.0, -7.0 },
                    { 0.5, 5.0, 1.25 },
                    { 4.0, 0.25, 0.5 },
                    frame),
            "nonuniformly scaled and sheared directional-light bases must be orthonormalized");
        const auto Dot3 = [](
            const std::array<double, 3u>& left,
            const std::array<double, 3u>& right) {
                return left[0] * right[0] +
                    left[1] * right[1] +
                    left[2] * right[2];
            };
        Require(NearDouble(Dot3(frame.x, frame.x), 1.0) &&
                NearDouble(Dot3(frame.y, frame.y), 1.0) &&
                NearDouble(Dot3(frame.z, frame.z), 1.0) &&
                NearDouble(Dot3(frame.x, frame.y), 0.0) &&
                NearDouble(Dot3(frame.x, frame.z), 0.0) &&
                NearDouble(Dot3(frame.y, frame.z), 0.0),
            "the recovered directional-light frame must be orthonormal");

        Require(TryBuildDiagnosticCsmLightFrame(
                    { 0.0, 1.0, 0.0 },
                    { 0.0, 9.0, 0.0 },
                    { 0.0, 2.0, 0.0 },
                    frame),
            "a near-collinear source basis must use a stable fallback");
        Require(NearDouble(Dot3(frame.x, frame.x), 1.0) &&
                NearDouble(Dot3(frame.y, frame.y), 1.0) &&
                NearDouble(Dot3(frame.z, frame.z), 1.0),
            "the near-collinear fallback must remain normalized");
        Require(!TryBuildDiagnosticCsmLightFrame(
                    { 0.0, 0.0, 0.0 },
                    { 0.0, 1.0, 0.0 },
                    { 1.0, 0.0, 0.0 },
                    frame),
            "a directionless light must fail closed");

        Require(ShouldRetainDiagnosticCsmCoarseBounds(true, true) &&
                !ShouldRetainDiagnosticCsmCoarseBounds(true, false),
            "reliable bounds must retain the normal coarse-frustum cull");
        Require(ShouldRetainDiagnosticCsmCoarseBounds(false, true) &&
                ShouldRetainDiagnosticCsmCoarseBounds(false, false),
            "malformed bounds must fail open instead of pruning casters");
    }

    void TestInputAssemblerCasterFetchContract()
    {
        Require(ShouldUseDiagnosticCsmInputAssemblerCasterFetch(
                true, false, false, true, true, true, true),
            "a complete, non-deforming, non-translation caster must be eligible for the experimental input-assembler path");
        Require(!ShouldUseDiagnosticCsmInputAssemblerCasterFetch(
                    false, false, false, true, true, true, true),
            "the disabled input-assembler toggle must preserve the manual reference path");
        Require(!ShouldUseDiagnosticCsmInputAssemblerCasterFetch(
                    true, true, false, true, true, true, true),
            "translation-only casters must retain the cheaper manual transform path");
        Require(!ShouldUseDiagnosticCsmInputAssemblerCasterFetch(
                    true, false, true, true, true, true, true),
            "deforming casters must fail conservatively to manual fetch");

        for (size_t missing = 0u; missing < 4u; ++missing)
        {
            std::array<bool, 4u> resources = {
                true, true, true, true
            };
            resources[missing] = false;
            Require(!ShouldUseDiagnosticCsmInputAssemblerCasterFetch(
                        true,
                        false,
                        false,
                        resources[0],
                        resources[1],
                        resources[2],
                        resources[3]),
                "every required input-assembler attribute and buffer must have an independent manual fallback");
        }
    }

    void TestResolveLightDirectionPermutationContract()
    {
        Require(DiagnosticCsmResolveLightDirectionPermutationCount == 2u,
            "receiver-light normalization must expose exactly one legacy and one optimized resolve permutation");
        Require(
            DiagnosticCsmResolveReceiverTransformPermutationCount == 2u &&
                DiagnosticCsmResolvePermutationCount == 4u,
            "the two independent receiver optimizations must expose exactly four precreated resolve permutations");
        Require(GetDiagnosticCsmResolveLightDirectionPermutation(false) == 0u,
            "disabling pre-normalized receiver light must select exact legacy permutation zero");
        Require(GetDiagnosticCsmResolveLightDirectionPermutation(true) == 1u,
            "enabling pre-normalized receiver light must select optimized permutation one");
        Require(GetDiagnosticCsmResolvePermutation(false, false) == 0u,
            "disabling both receiver optimizations must select the exact legacy shader");
        Require(GetDiagnosticCsmResolvePermutation(true, false) == 1u,
            "receiver-light normalization must remain independently selectable");
        Require(GetDiagnosticCsmResolvePermutation(false, true) == 2u,
            "the precomposed receiver transform must remain independently selectable");
        Require(GetDiagnosticCsmResolvePermutation(true, true) == 3u,
            "enabling both receiver optimizations must select their unique combined shader");
    }

    void TestPrecomposedClipToShadowTransform()
    {
        const float4x4 cameraClipToWorld = {
            1.25f, 0.10f, -0.20f, 0.05f,
            -0.15f, 0.90f, 0.30f, -0.02f,
            0.20f, -0.10f, 1.10f, 0.04f,
            5.00f, -3.00f, 2.00f, 1.00f
        };
        const float4x4 worldToUvzw = {
            0.020f, 0.002f, 0.001f, 0.0002f,
            -0.001f, 0.030f, 0.003f, -0.0001f,
            0.004f, -0.002f, 0.050f, 0.0003f,
            0.500f, 0.500f, 0.100f, 1.0000f
        };
        const float4x4 worldToView = {
            0.96f, 0.00f, -0.28f, 0.0f,
            0.03f, 0.99f, 0.10f, 0.0f,
            0.28f, -0.11f, 0.95f, 0.0f,
            -2.0f, 1.5f, 4.0f, 1.0f
        };
        const float4 clipPosition(0.25f, -0.5f, 0.7f, 1.f);

        const float4 legacySelected = clipPosition *
            BuildDiagnosticCsmReceiverTransform(
                false, cameraClipToWorld, worldToUvzw);
        const float4 legacyExpected = clipPosition * worldToUvzw;
        Require(legacySelected.x == legacyExpected.x &&
                legacySelected.y == legacyExpected.y &&
                legacySelected.z == legacyExpected.z &&
                legacySelected.w == legacyExpected.w,
            "disabling precomposition must return the exact legacy matrix");

        const float4 worldHomogeneous =
            clipPosition * cameraClipToWorld;
        Require(std::isfinite(worldHomogeneous.w) &&
                std::abs(worldHomogeneous.w) > 1e-5f,
            "the deterministic receiver-transform fixture must have a valid projective world position");
        const float4 worldPosition(
            worldHomogeneous.x / worldHomogeneous.w,
            worldHomogeneous.y / worldHomogeneous.w,
            worldHomogeneous.z / worldHomogeneous.w,
            1.f);
        const float4 legacyShadow = worldPosition * worldToUvzw;
        const float4 precomposedShadow = clipPosition *
            BuildDiagnosticCsmReceiverTransform(
                true, cameraClipToWorld, worldToUvzw);
        Require(std::isfinite(legacyShadow.w) &&
                std::isfinite(precomposedShadow.w) &&
                std::abs(legacyShadow.w) > 1e-5f &&
                std::abs(precomposedShadow.w) > 1e-5f,
            "both deterministic shadow projections must permit projective division");

        const float3 legacyUvz(
            legacyShadow.x / legacyShadow.w,
            legacyShadow.y / legacyShadow.w,
            legacyShadow.z / legacyShadow.w);
        const float3 precomposedUvz(
            precomposedShadow.x / precomposedShadow.w,
            precomposedShadow.y / precomposedShadow.w,
            precomposedShadow.z / precomposedShadow.w);
        Require(Near(precomposedUvz.x, legacyUvz.x) &&
                Near(precomposedUvz.y, legacyUvz.y) &&
                Near(precomposedUvz.z, legacyUvz.z),
            "clip-to-shadow precomposition must preserve projectively divided cascade coordinates");

        const float4 legacyView = worldPosition * worldToView;
        const float4 directView = clipPosition *
            (cameraClipToWorld * worldToView);
        Require(std::isfinite(directView.w) &&
                std::abs(directView.w) > 1e-5f &&
                Near(directView.z / directView.w, legacyView.z),
            "homogeneous clip-to-view reconstruction must preserve the legacy positive view-depth coordinate");
    }

    void TestCasterGatherPlanning()
    {
        std::array<DiagnosticCsmUpdateAction,
            DiagnosticCsmMaximumCascades> actions;
        actions.fill(DiagnosticCsmUpdateAction::Reused);

        DiagnosticCsmCasterGatherPlan plan =
            BuildDiagnosticCsmCasterGatherPlan(
                true, 4u, actions);
        Require(plan.cascadeMask == 0u &&
                plan.cascadeCount == 0u &&
                !plan.singleTraversal,
            "an all-reused frame must perform no caster traversal");

        actions[0] = DiagnosticCsmUpdateAction::FullRedraw;
        plan = BuildDiagnosticCsmCasterGatherPlan(
            true, 4u, actions);
        Require(plan.cascadeMask == 0x1u &&
                plan.cascadeCount == 1u &&
                !plan.singleTraversal,
            "one updating cascade must retain the direct one-view gather");

        actions[1] = DiagnosticCsmUpdateAction::DirtyRectangles;
        actions[2] = DiagnosticCsmUpdateAction::Scrolled;
        plan = BuildDiagnosticCsmCasterGatherPlan(
            true, 4u, actions);
        Require(plan.cascadeMask == 0x7u &&
                plan.cascadeCount == 3u &&
                plan.singleTraversal,
            "two or more updating cascades must share one requested traversal");

        const DiagnosticCsmCasterGatherPlan disabled =
            BuildDiagnosticCsmCasterGatherPlan(
                false, 4u, actions);
        Require(disabled.cascadeMask == plan.cascadeMask &&
                disabled.cascadeCount == plan.cascadeCount &&
                !disabled.singleTraversal,
            "disabling the optimization must preserve the legacy per-cascade plan");

        actions[3] = DiagnosticCsmUpdateAction::FullRedraw;
        const DiagnosticCsmCasterGatherPlan limited =
            BuildDiagnosticCsmCasterGatherPlan(
                true, 2u, actions);
        Require(limited.cascadeMask == 0x3u &&
                limited.cascadeCount == 2u &&
                limited.singleTraversal,
            "gather planning must ignore actions beyond the active cascade count");

        const DiagnosticCsmCasterGatherPlan invalid =
            BuildDiagnosticCsmCasterGatherPlan(
                true, 0u, actions);
        Require(invalid.cascadeMask == 0u &&
                invalid.cascadeCount == 0u &&
                !invalid.singleTraversal,
            "invalid cascade counts must fail to a harmless empty plan");
    }

    void TestProjectedCasterOptimizationParity()
    {
        Require(
            ValidateDiagnosticCsmProjectedCasterOptimizationParity(),
            "precomputed receiver axes and shared caster light projection "
            "must match the legacy projected-caster overlap path in all four "
            "toggle combinations and fail open for invalid input");

        DiagnosticCascadedShadowMapSettings baseline;
        baseline.precomputedReceiverHullAxesEnabled = false;
        baseline.sharedCasterLightProjectionEnabled = false;
        for (uint32_t combination = 0u; combination < 4u; ++combination)
        {
            DiagnosticCascadedShadowMapSettings changed = baseline;
            changed.precomputedReceiverHullAxesEnabled =
                (combination & 1u) != 0u;
            changed.sharedCasterLightProjectionEnabled =
                (combination & 2u) != 0u;
            Require(
                IsSameDiagnosticCsmTimingConfiguration(
                    baseline, changed) == (combination == 0u),
                "each projected-caster optimization combination must have "
                "an independent timing configuration");
        }
    }

    void TestPrecomputedDepthAxisInverseLength()
    {
        const std::array<std::array<float, 3u>, 4u> axes = {{
            {{ 0.f, 0.f, 0.0001f }},
            {{ 0.00003f, -0.00004f, 0.00012f }},
            {{ -2.f, 3.f, 6.f }},
            {{ 0.25f, -0.75f, 0.5f }}
        }};
        const std::array<std::array<float, 3u>, 4u> normals = {{
            {{ 0.f, 0.f, 1.f }},
            {{ 1.f, -2.f, 4.f }},
            {{ -0.25f, 0.5f, 0.75f }},
            {{ 9.f, 1.f, -3.f }}
        }};

        for (const auto& axis : axes)
        {
            const float inverseAxisLength =
                ComputeDiagnosticCsmDepthAxisInverseLength(axis);
            Require(std::isfinite(inverseAxisLength) &&
                    inverseAxisLength > 0.f,
                "a finite nondegenerate depth axis must produce a finite inverse length");

            const float axisLengthSquared =
                axis[0] * axis[0] +
                axis[1] * axis[1] +
                axis[2] * axis[2];
            for (const auto& normal : normals)
            {
                const float normalLengthSquared =
                    normal[0] * normal[0] +
                    normal[1] * normal[1] +
                    normal[2] * normal[2];
                const float inverseNormalLength =
                    1.f / std::sqrt(normalLengthSquared);
                const float legacy =
                    std::abs(
                        normal[0] * inverseNormalLength *
                            axis[0] / std::sqrt(axisLengthSquared) +
                        normal[1] * inverseNormalLength *
                            axis[1] / std::sqrt(axisLengthSquared) +
                        normal[2] * inverseNormalLength *
                            axis[2] / std::sqrt(axisLengthSquared));
                const float optimized =
                    std::abs((
                        normal[0] * axis[0] +
                        normal[1] * axis[1] +
                        normal[2] * axis[2]) *
                        inverseNormalLength * inverseAxisLength);
                Require(Near(optimized, legacy, 2e-6f),
                    "precomputed depth-axis normalization must preserve the reference normal-angle result");
            }
        }

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        Require(ComputeDiagnosticCsmDepthAxisInverseLength(
                    {{ 0.f, 0.f, 0.f }}) == 0.f &&
                ComputeDiagnosticCsmDepthAxisInverseLength(
                    {{ 1e-7f, 0.f, 0.f }}) == 0.f &&
                ComputeDiagnosticCsmDepthAxisInverseLength(
                    {{ nan, 0.f, 1.f }}) == 0.f &&
                ComputeDiagnosticCsmDepthAxisInverseLength(
                    {{ infinity, 0.f, 1.f }}) == 0.f,
            "zero, sub-threshold, NaN, and infinite depth axes must preserve the maximum-slope fallback");
    }

    void TestConservativeSaturatedSlope()
    {
        const float thresholdSquared =
            DiagnosticCsmConservativeSaturatedSlopeNoLSquaredThreshold;
        const float threshold = std::sqrt(thresholdSquared);
        Require(
            ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                threshold, 1.f, 1.f) &&
            ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                -threshold, 1.f, 1.f) &&
            ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                threshold * 2.f, 1.f, 4.f),
            "the conservative saturated-slope guard must include both signs "
            "and scaled normals at the one-step-below-half squared boundary");

        const float aboveThreshold = std::nextafter(
            threshold, std::numeric_limits<float>::infinity());
        Require(
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                aboveThreshold, 1.f, 1.f) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                -aboveThreshold, 1.f, 1.f) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                0.71f, 1.f, 1.f),
            "the shortcut must conservatively fall back immediately above "
            "the one-step-below-half squared NoL boundary");
        Require(
            ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                0.69f, 1.f, 1.f) &&
            ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                0.707f, 1.f, 1.f),
            "the exact-boundary shortcut must include saturated slopes that "
            "the former 0.68 cutoff unnecessarily sent through slow math");

        const auto referenceSlope = [](float projectedNumerator,
                                        float normalLengthSquared)
        {
            if (!(normalLengthSquared > 1e-12f))
                return 1.f;
            const float noL = std::clamp(
                std::abs(projectedNumerator) /
                    std::sqrt(normalLengthSquared),
                0.f,
                1.f);
            const float slope = noL > 1e-6f
                ? std::sqrt(std::max(1.f - noL * noL, 0.f)) / noL
                : 1.f;
            return std::clamp(slope, 0.f, 1.f);
        };
        const std::array<float, 9u> projectedNumerators = {{
            0.f, 0.1f, 0.5f, threshold, 0.69f,
            0.707f, 0.71f, 0.9f, 1.f
        }};
        for (float projectedNumerator : projectedNumerators)
        {
            const float reference =
                referenceSlope(projectedNumerator, 1.f);
            const float optimized =
                ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                    projectedNumerator, 1.f, 1.f)
                ? 1.f
                : referenceSlope(projectedNumerator, 1.f);
            Require(Near(optimized, reference),
                "the conservative saturated-slope shortcut must preserve "
                "the clamped reference result on both sides of its guard");
        }

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        const float maximum = std::numeric_limits<float>::max();
        Require(
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                nan, 1.f, 1.f) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                infinity, 1.f, 1.f) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                maximum, 1.f, 1.f) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                0.f, nan, 1.f) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                0.f, infinity, 1.f) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                0.f, 0.f, 1.f) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                0.f, -1.f, 1.f) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                0.f, 1.f, nan) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                0.f, 1.f, infinity) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                0.f, 1.f, 0.f) &&
            !ShouldUseDiagnosticCsmConservativeSaturatedSlope(
                0.f, 1.f, 1e-13f),
            "non-finite, overflowing, zero, and degenerate inputs must "
            "fall through to the unchanged reference slope math");
    }

    void TestAlgebraicSlowSlope()
    {
        const auto referenceSlope = [](float projectedNumerator,
                                        float normalLengthSquared)
        {
            const float noL = std::clamp(
                std::abs(projectedNumerator) /
                    std::sqrt(normalLengthSquared),
                0.f,
                1.f);
            const float result = noL > 1e-6f
                ? std::sqrt(std::max(1.f - noL * noL, 0.f)) / noL
                : 1.f;
            return std::clamp(result, 0.f, 1.f);
        };
        const std::array<float, 20u> normalizedProjections = {{
            -2.f, -1.00001f, -1.f, -0.99999f, -0.9f, -0.71f,
            -0.69f, -0.1f, -1e-7f, 0.f, 1e-7f, 0.1f, 0.69f,
            0.71f, 0.9f, 0.99999f, 1.f, 1.00001f, 1.5f, 2.f
        }};
        const std::array<float, 3u> normalLengthSquaredValues = {{
            0.25f, 1.f, 4.f
        }};
        const std::array<float, 3u> inverseDepthAxisLengths = {{
            0.25f, 1.f, 4.f
        }};
        for (float normalLengthSquared : normalLengthSquaredValues)
        {
            const float normalLength =
                std::sqrt(normalLengthSquared);
            for (float inverseDepthAxisLength :
                inverseDepthAxisLengths)
            {
                for (float normalizedProjection :
                    normalizedProjections)
                {
                    const float projectedNumerator =
                        normalizedProjection * normalLength;
                    const float depthAxisDot =
                        projectedNumerator /
                        inverseDepthAxisLength;
                    float algebraic = -1.f;
                    Require(
                        TryComputeDiagnosticCsmAlgebraicSlowSlope(
                            depthAxisDot,
                            inverseDepthAxisLength,
                            normalLengthSquared,
                            algebraic),
                        "finite nondegenerate slope inputs must select the "
                        "algebraic path");
                    Require(
                        Near(
                            algebraic,
                            referenceSlope(
                                projectedNumerator,
                                normalLengthSquared),
                            2e-5f),
                        "the perpendicular-to-parallel normal ratio must "
                        "preserve UE's clamped reference slope across "
                        "non-unit depth axes and the parallel boundary");
                }
            }
        }

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        const float maximum = std::numeric_limits<float>::max();
        float slope = -1.f;
        Require(
            !TryComputeDiagnosticCsmAlgebraicSlowSlope(
                nan, 1.f, 1.f, slope) &&
            !TryComputeDiagnosticCsmAlgebraicSlowSlope(
                infinity, 1.f, 1.f, slope) &&
            !TryComputeDiagnosticCsmAlgebraicSlowSlope(
                maximum, maximum, 1.f, slope) &&
            !TryComputeDiagnosticCsmAlgebraicSlowSlope(
                1.f, nan, 1.f, slope) &&
            !TryComputeDiagnosticCsmAlgebraicSlowSlope(
                1.f, infinity, 1.f, slope) &&
            !TryComputeDiagnosticCsmAlgebraicSlowSlope(
                1.f, 0.f, 1.f, slope) &&
            !TryComputeDiagnosticCsmAlgebraicSlowSlope(
                1.f, -1.f, 1.f, slope) &&
            !TryComputeDiagnosticCsmAlgebraicSlowSlope(
                1.f, 1.f, nan, slope) &&
            !TryComputeDiagnosticCsmAlgebraicSlowSlope(
                1.f, 1.f, infinity, slope) &&
            !TryComputeDiagnosticCsmAlgebraicSlowSlope(
                1.f, 1.f, 0.f, slope) &&
            !TryComputeDiagnosticCsmAlgebraicSlowSlope(
                1.f, 1.f, 1e-13f, slope),
            "invalid, overflowing, and degenerate algebraic inputs must "
            "fall back to the unchanged reference slope calculation");
    }

    void TestReceiverRasterScissor()
    {
        const std::array<std::array<float, 2u>,
            DiagnosticCsmReceiverCornerCount> receiverUv = {{
            {{ 0.25f, 0.10f }},
            {{ 0.75f, 0.10f }},
            {{ 0.25f, 0.90f }},
            {{ 0.75f, 0.90f }},
            {{ 0.30f, 0.20f }},
            {{ 0.70f, 0.20f }},
            {{ 0.30f, 0.80f }},
            {{ 0.70f, 0.80f }}
        }};
        DiagnosticCsmRect scissor;
        Require(TryBuildDiagnosticCsmReceiverRasterScissor(
                    receiverUv, 2049u, 7u, scissor),
            "eight finite snapped receiver corners must produce a raster scissor");
        RequireRect(
            scissor,
            { 505, 1544, 197, 1852 },
            "receiver raster scissor must use floor minima, ceil exclusive maxima, an odd-resolution projection guard, and no truncation");

        auto outside = receiverUv;
        outside[0] = {{ -0.25f, -0.25f }};
        outside[3] = {{ 1.25f, 1.25f }};
        Require(TryBuildDiagnosticCsmReceiverRasterScissor(
                    outside, 2049u, 7u, scissor),
            "finite receiver bounds extending outside the map must clip safely");
        RequireRect(
            scissor,
            { 0, 2049, 0, 2049 },
            "out-of-range receiver bounds must clamp to the full map");

        auto invalid = receiverUv;
        invalid[5][0] = std::numeric_limits<float>::quiet_NaN();
        Require(!TryBuildDiagnosticCsmReceiverRasterScissor(
                    invalid, 2049u, 7u, scissor),
            "a non-finite receiver corner must reject the optimized scissor");
        RequireRect(
            scissor,
            { 0, 2049, 0, 2049 },
            "a malformed receiver projection must fail open to the full map");

        auto degenerate = receiverUv;
        for (auto& corner : degenerate)
            corner = {{ 0.5f, 0.5f }};
        Require(!TryBuildDiagnosticCsmReceiverRasterScissor(
                    degenerate, 2049u, 7u, scissor),
            "a degenerate receiver footprint must reject the optimized scissor");
        RequireRect(
            scissor,
            { 0, 2049, 0, 2049 },
            "a degenerate receiver footprint must fail open to the full map");

        Require(!TryBuildDiagnosticCsmReceiverRasterScissor(
                    receiverUv, 0u, 7u, scissor) &&
                !scissor.IsValid(),
            "a zero-resolution receiver map must fail without forming a scissor");
    }

    void TestBatchedFullRedrawClearPlanning()
    {
        DiagnosticCascadedShadowMapSettings settings;
        settings.cascadeCount = 4u;
        settings.batchedFullRedrawClearEnabled = true;
        std::array<DiagnosticCsmUpdateAction,
            DiagnosticCsmMaximumCascades> actions;
        actions.fill(DiagnosticCsmUpdateAction::FullRedraw);

        DiagnosticCsmFullRedrawClearPlan plan =
            BuildDiagnosticCsmFullRedrawClearPlan(settings, actions);
        Require(plan.batched &&
                plan.baseArraySlice == 0u &&
                plan.arraySliceCount == 4u,
            "four contiguous all-full-redraw cascades must form the exact [0, 4) clear batch");

        settings.batchedFullRedrawClearEnabled = false;
        plan = BuildDiagnosticCsmFullRedrawClearPlan(settings, actions);
        Require(!plan.batched &&
                plan.baseArraySlice == 0u &&
                plan.arraySliceCount == 0u,
            "disabling batched clears must select the exact legacy per-cascade path");

        settings.batchedFullRedrawClearEnabled = true;
        settings.cascadeCount = 1u;
        plan = BuildDiagnosticCsmFullRedrawClearPlan(settings, actions);
        Require(!plan.batched && plan.arraySliceCount == 0u,
            "a single cascade must retain its one legacy full-map clear because no batching is possible");

        settings.cascadeCount = 3u;
        actions[3] = DiagnosticCsmUpdateAction::Scrolled;
        plan = BuildDiagnosticCsmFullRedrawClearPlan(settings, actions);
        Require(plan.batched &&
                plan.baseArraySlice == 0u &&
                plan.arraySliceCount == 3u,
            "inactive array slices must not prevent batching the exact active [0, cascadeCount) range");

        const std::array<DiagnosticCsmUpdateAction, 3u> mixedActions = {
            DiagnosticCsmUpdateAction::Reused,
            DiagnosticCsmUpdateAction::DirtyRectangles,
            DiagnosticCsmUpdateAction::Scrolled
        };
        for (uint32_t cascade = 0u;
            cascade < mixedActions.size();
            ++cascade)
        {
            actions[cascade] = mixedActions[cascade];
            plan = BuildDiagnosticCsmFullRedrawClearPlan(
                settings, actions);
            Require(!plan.batched && plan.arraySliceCount == 0u,
                "every mixed, cached, dirty, or scrolling action must retain the legacy clear/update sequence");
            actions[cascade] = DiagnosticCsmUpdateAction::FullRedraw;
        }

        actions[1] = DiagnosticCsmUpdateAction::Count;
        Require(!BuildDiagnosticCsmFullRedrawClearPlan(
                    settings, actions).batched,
            "an invalid active update action must retain the fail-safe legacy path");
        actions[1] = DiagnosticCsmUpdateAction::FullRedraw;

        settings.cascadeCount = 0u;
        Require(!BuildDiagnosticCsmFullRedrawClearPlan(
                    settings, actions).batched,
            "zero cascades must reject batched clear planning");
        settings.cascadeCount = DiagnosticCsmMaximumCascades + 1u;
        Require(!BuildDiagnosticCsmFullRedrawClearPlan(
                    settings, actions).batched,
            "an out-of-range cascade count must reject batched clear planning without indexing the action array");
    }

    void TestCasterSubmissionIndexing()
    {
        size_t readIndex = 0u;
        size_t casterIndex = 99u;
        for (size_t expected = 0u; expected < 3u; ++expected)
        {
            Require(NextDiagnosticCsmCasterSubmissionIndex(
                    3u, nullptr, 0u, false, readIndex, casterIndex) &&
                    casterIndex == expected,
                "contiguous submission must preserve caster order");
        }
        Require(!NextDiagnosticCsmCasterSubmissionIndex(
                3u, nullptr, 0u, false, readIndex, casterIndex),
            "contiguous submission must remain exhausted");

        const std::array<size_t, 6u> indices = {
            2u, 9u, 1u, 2u, 8u, 0u
        };
        const std::array<size_t, 4u> expected = {
            2u, 1u, 2u, 0u
        };
        readIndex = 0u;
        for (size_t expectedIndex : expected)
        {
            Require(NextDiagnosticCsmCasterSubmissionIndex(
                    3u,
                    indices.data(),
                    indices.size(),
                    true,
                    readIndex,
                    casterIndex) &&
                    casterIndex == expectedIndex,
                "indexed submission must preserve order and duplicates "
                "while skipping invalid entries");
        }
        Require(!NextDiagnosticCsmCasterSubmissionIndex(
                3u,
                indices.data(),
                indices.size(),
                true,
                readIndex,
                casterIndex),
            "indexed submission must remain exhausted");

        readIndex = 0u;
        Require(!NextDiagnosticCsmCasterSubmissionIndex(
                3u, nullptr, 0u, true, readIndex, casterIndex),
            "an empty indexed submission must remain empty");
        readIndex = 0u;
        Require(!NextDiagnosticCsmCasterSubmissionIndex(
                3u, nullptr, 2u, true, readIndex, casterIndex),
            "null indexed data must fail safely even with a nonzero count");
        readIndex = 0u;
        Require(!NextDiagnosticCsmCasterSubmissionIndex(
                0u, nullptr, 0u, false, readIndex, casterIndex),
            "a sequential submission with no casters must remain empty");
        const std::array<size_t, 2u> invalid = { 4u, 5u };
        readIndex = 0u;
        Require(!NextDiagnosticCsmCasterSubmissionIndex(
                3u,
                invalid.data(),
                invalid.size(),
                true,
                readIndex,
                casterIndex),
            "an invalid-only indexed submission must issue no draw");

        const std::array<uint64_t, 3u> records = {
            0x1234u, 0x5678u, 0x9abcu
        };
        std::vector<const uint64_t*> direct;
        std::vector<uint64_t> copied;
        readIndex = 0u;
        while (NextDiagnosticCsmCasterSubmissionIndex(
            records.size(),
            indices.data(),
            indices.size(),
            true,
            readIndex,
            casterIndex))
        {
            direct.push_back(&records[casterIndex]);
            copied.push_back(records[casterIndex]);
        }
        Require(direct.size() == copied.size(),
            "direct and copied submission must emit the same item count");
        for (size_t item = 0u; item < direct.size(); ++item)
        {
            Require(*direct[item] == copied[item],
                "direct and copied submission must emit identical ordered "
                "record fields");
        }
    }

    void TestUeGeometricSplitsAndFades()
    {
        Require(IsDiagnosticCsmCascadeCountValid(1u) &&
                IsDiagnosticCsmCascadeCountValid(4u),
            "one through four cascades must be valid");
        Require(!IsDiagnosticCsmCascadeCountValid(0u) &&
                !IsDiagnosticCsmCascadeCountValid(5u),
            "cascade counts outside one through four must be rejected");

        const std::array<float, 5u> exponentThree = {
            0.f, 0.025f, 0.1f, 0.325f, 1.f
        };
        for (uint32_t split = 0u; split < exponentThree.size(); ++split)
        {
            Require(Near(ComputeUeCsmAccumulatedScale(
                    3.f, split, 4u), exponentThree[split]),
                "UE geometric split accumulation must use 1:3:9:27 weights");
        }
        const std::array<float, 5u> dynamicExponentFour = {
            0.f, 1.f / 85.f, 5.f / 85.f, 21.f / 85.f, 1.f
        };
        for (uint32_t split = 0u;
            split < dynamicExponentFour.size();
            ++split)
        {
            Require(Near(ComputeUeCsmAccumulatedScale(
                    4.f, split, 4u), dynamicExponentFour[split]),
                "a fully dynamic UE light must use 1:4:16:64 split weights");
        }
        Require(Near(ComputeUeCsmAccumulatedScale(3.f, 99u, 4u), 1.f),
            "split indices beyond the cascade count must clamp to the far end");

        for (uint32_t split = 0u; split <= 4u; ++split)
        {
            Require(Near(ComputeUeCsmAccumulatedScale(
                    1.f, split, 4u), float(split) / 4.f),
                "an exponent of one must produce linear split intervals");
            Require(Near(ComputeUeCsmAccumulatedScale(
                    0.25f, split, 4u), float(split) / 4.f),
                "an exponent below one must clamp to linear intervals");
        }
        Require(Near(ComputeUeCsmAccumulatedScale(
                std::numeric_limits<float>::infinity(), 2u, 4u), 0.5f),
            "a non-finite exponent must fall back to linear intervals");
        Require(ComputeUeCsmAccumulatedScale(3.f, 1u, 0u) == 0.f &&
                ComputeUeCsmAccumulatedScale(3.f, 1u, 5u) == 0.f,
            "invalid cascade counts must not produce split scales");

        const DiagnosticCsmSplitSet reference =
            ComputeUeCsmSplits(0.f, 40000.f, 3.f, 4u);
        const std::array<float, 5u> referenceDistances = {
            0.f, 1000.f, 4000.f, 13000.f, 40000.f
        };
        Require(reference.valid && reference.cascadeCount == 4u,
            "the four-cascade UE reference split set must be valid");
        for (uint32_t split = 0u; split < referenceDistances.size(); ++split)
        {
            Require(Near(reference.distances[split],
                    referenceDistances[split], 1e-3f),
                "the UE reference split distances must match geometric sums");
        }

        const DiagnosticCsmSplitSet twoCascades =
            ComputeUeCsmSplits(100.f, 500.f, 1.f, 2u);
        Require(twoCascades.valid &&
                Near(twoCascades.distances[0], 100.f) &&
                Near(twoCascades.distances[1], 300.f) &&
                Near(twoCascades.distances[2], 500.f) &&
                Near(twoCascades.distances[3], 500.f) &&
                Near(twoCascades.distances[4], 500.f),
            "unused split entries must be deterministically filled by the far distance");

        Require(!ComputeUeCsmSplits(0.f, 100.f, 3.f, 0u).valid &&
                !ComputeUeCsmSplits(0.f, 100.f, 3.f, 5u).valid &&
                !ComputeUeCsmSplits(-1.f, 100.f, 3.f, 4u).valid &&
                !ComputeUeCsmSplits(10.f, 10.f, 3.f, 4u).valid &&
                !ComputeUeCsmSplits(
                    0.f,
                    std::numeric_limits<float>::infinity(),
                    3.f,
                    4u).valid,
            "invalid split domains must be rejected deterministically");

        const DiagnosticCsmCascadeRange interior =
            ComputeUeCsmCascadeRange(100.f, 300.f, 0.1f, false);
        Require(Near(interior.nominalNear, 100.f) &&
                Near(interior.nominalFar, 300.f) &&
                Near(interior.projectedFar, 320.f) &&
                Near(interior.cascadeFadeOffset, 300.f) &&
                Near(interior.cascadeFadeLength, 20.f),
            "an interior UE cascade must extend its projection through the fade");

        const DiagnosticCsmCascadeRange last =
            ComputeUeCsmCascadeRange(100.f, 300.f, 0.1f, true);
        Require(Near(last.projectedFar, 300.f) &&
                Near(last.cascadeFadeOffset, 280.f) &&
                Near(last.cascadeFadeLength, 20.f),
            "the last UE cascade must move its fade plane inward without extending projection coverage");

        const DiagnosticCsmCascadeRange clamped =
            ComputeUeCsmCascadeRange(100.f, 300.f, 2.f, false);
        Require(Near(clamped.projectedFar, 500.f) &&
                Near(clamped.cascadeFadeLength, 200.f),
            "cascade transition fractions must clamp to one");
        Require(ComputeUeCsmCascadeRange(
                300.f, 100.f, 0.1f, false).cascadeFadeLength == 0.f,
            "an invalid cascade range must not create a fade");

        Require(Near(EvaluateDiagnosticCsmFadeAlpha(290.f, 300.f, 20.f), 1.f) &&
                Near(EvaluateDiagnosticCsmFadeAlpha(310.f, 300.f, 20.f), 0.5f) &&
                Near(EvaluateDiagnosticCsmFadeAlpha(320.f, 300.f, 20.f), 0.f),
            "cascade fade alpha must clamp before, within, and after the transition");
        Require(EvaluateDiagnosticCsmFadeAlpha(300.f, 300.f, 0.f) == 1.f &&
                EvaluateDiagnosticCsmFadeAlpha(301.f, 300.f, 0.f) == 0.f,
            "a disabled cascade fade must behave as a hard boundary");

        Require(Near(EvaluateDiagnosticCsmDistanceFadeAlpha(
                    900.f, 1000.f, 0.1f), 1.f) &&
                Near(EvaluateDiagnosticCsmDistanceFadeAlpha(
                    950.f, 1000.f, 0.1f), 0.75f) &&
                Near(EvaluateDiagnosticCsmDistanceFadeAlpha(
                    1000.f, 1000.f, 0.1f), 0.f),
            "the UE distance fade must use its quadratic camera-distance falloff");
        Require(EvaluateDiagnosticCsmDistanceFadeAlpha(
                    1000.f, 1000.f, 0.f) == 1.f &&
                EvaluateDiagnosticCsmDistanceFadeAlpha(
                    1001.f, 1000.f, 0.f) == 0.f,
            "a disabled distance fade must preserve a hard maximum distance");
        Require(EvaluateDiagnosticCsmFadeAlpha(
                    std::numeric_limits<float>::quiet_NaN(), 0.f, 1.f) == 0.f &&
                EvaluateDiagnosticCsmDistanceFadeAlpha(
                    1.f, 0.f, 0.1f) == 0.f,
            "invalid fade inputs must fail safely to zero contribution");
    }

    void TestTapNormalizationAndCacheGating()
    {
        const std::array<std::pair<uint32_t, uint32_t>, 10u> cases = {{
            { 0u, 1u },
            { 1u, 1u },
            { 2u, 4u },
            { 4u, 4u },
            { 5u, 8u },
            { 8u, 8u },
            { 9u, 16u },
            { 16u, 16u },
            { 17u, 16u },
            { std::numeric_limits<uint32_t>::max(), 16u }
        }};
        for (const auto& [requested, expected] : cases)
        {
            Require(NormalizeDiagnosticCsmTapCount(requested) == expected,
                "tap counts must normalize to the 1, 4, 8, or 16 variants");
        }

        DiagnosticCascadedShadowMapSettings settings;
        settings.wholeMapReuseEnabled = false;
        settings.wholeCascadeReuseEnabled = false;
        settings.dirtyRectanglesEnabled = false;
        settings.scrollingEnabled = false;
        Require(!HasAnyDiagnosticCsmCachePolicy(settings),
            "full redraw must bypass all cache-policy work");
        Require(CanUseDiagnosticCsmViewDependentCasterCulling(settings),
            "full redraw may use camera-dependent receiver and radius culling");
        Require(CanUseDiagnosticCsmReceiverRasterScissor(settings),
            "an enabled full redraw may use the conservative receiver raster scissor");
        settings.receiverRasterScissorEnabled = false;
        Require(!CanUseDiagnosticCsmReceiverRasterScissor(settings),
            "disabling receiver raster scissoring must retain a neutral full-map path");
        settings.receiverRasterScissorEnabled = true;
        settings.ueCasterRadiusThresholdEnabled = true;
        settings.casterRadiusThreshold = 0.01f;
        Require(IsDiagnosticCsmCasterRadiusThresholdActive(settings),
            "a positive finite full-redraw radius threshold must be active");
        settings.casterRadiusThreshold = 0.f;
        Require(!IsDiagnosticCsmCasterRadiusThresholdActive(settings),
            "a zero radius threshold must not retain bounds work");
        settings.casterRadiusThreshold =
            std::numeric_limits<float>::quiet_NaN();
        Require(!IsDiagnosticCsmCasterRadiusThresholdActive(settings),
            "a non-finite radius threshold must fail safely");
        settings.casterRadiusThreshold = 0.01f;

        settings.dirtyRectanglesEnabled = true;
        Require(!HasAnyDiagnosticCsmCachePolicy(settings),
            "dirty rectangles alone must not activate a cache policy");
        settings.dirtyRectanglesEnabled = false;
        settings.scrollingEnabled = true;
        Require(!HasAnyDiagnosticCsmCachePolicy(settings),
            "scrolling alone must not activate a cache policy");

        settings.wholeMapReuseEnabled = true;
        Require(HasAnyDiagnosticCsmCachePolicy(settings),
            "whole-map reuse must independently activate caching");
        Require(!CanUseDiagnosticCsmViewDependentCasterCulling(settings),
            "whole-map reuse must gate camera-dependent caster rejection");
        Require(!CanUseDiagnosticCsmReceiverRasterScissor(settings),
            "whole-map reuse must gate the current-camera receiver scissor");
        settings.wholeMapReuseEnabled = false;
        settings.wholeCascadeReuseEnabled = true;
        Require(HasAnyDiagnosticCsmCachePolicy(settings),
            "whole-cascade reuse must independently activate caching");
        Require(!CanUseDiagnosticCsmViewDependentCasterCulling(settings),
            "whole-cascade reuse must gate camera-dependent caster rejection");
        Require(!CanUseDiagnosticCsmReceiverRasterScissor(settings),
            "whole-cascade reuse must gate the current-camera receiver scissor");
        settings.wholeMapReuseEnabled = true;
        Require(HasAnyDiagnosticCsmCachePolicy(settings),
            "enabling both base reuse policies must remain a valid cache gate");
        Require(!IsDiagnosticCsmCasterRadiusThresholdActive(settings),
            "cached maps must not perform view-dependent radius work");

        Require(CanUseDiagnosticCsmPartialSceneUpdate(
                    false, false, false, true),
            "an unchanged scene must not require dirty-scene cache work");
        Require(CanUseDiagnosticCsmPartialSceneUpdate(
                    true, true, true, false),
            "a reliable localizable scene change may use dirty rectangles");
        Require(!CanUseDiagnosticCsmPartialSceneUpdate(
                    true, true, true, true) &&
                !CanUseDiagnosticCsmPartialSceneUpdate(
                    true, false, true, false) &&
                !CanUseDiagnosticCsmPartialSceneUpdate(
                    true, true, false, false),
            "generic content, unreliable revisions, and disabled dirty rectangles must force full redraws");
    }

    void TestCacheRefinementAndDiagnosticConfiguration()
    {
        DiagnosticCascadedShadowMapSettings timing;
        Require(IsSameDiagnosticCsmTimingConfiguration(timing, timing),
            "identical effective settings must share one timer generation");
        const std::string timingIdentity =
            BuildDiagnosticCsmTimingConfigurationIdentity(timing);
        const std::string timingId =
            BuildDiagnosticCsmTimingConfigurationId(timing);
        Require(timingId.size() == 16u &&
                timingIdentity.find(
                    "inputAssemblerCasterFetchEnabled=") !=
                    std::string::npos &&
                timingIdentity.find("debugView=") !=
                    std::string::npos,
            "benchmark timing identity must be complete and use a stable 64-bit ID");

        DiagnosticCascadedShadowMapSettings labelOnly = timing;
        labelOnly.enabled = !labelOnly.enabled;
        labelOnly.profile = DiagnosticCsmProfile::Custom;
        Require(IsSameDiagnosticCsmTimingConfiguration(
                timing, labelOnly),
            "enable state and profile labels must not redefine an active timing tuple");
        Require(BuildDiagnosticCsmTimingConfigurationIdentity(
                    labelOnly) == timingIdentity &&
                BuildDiagnosticCsmTimingConfigurationId(labelOnly) ==
                    timingId,
            "non-timing labels must not alter benchmark configuration identity");

        auto RequireTimingChange = [&timing, &timingIdentity, &timingId](
            auto mutate,
            const std::string& field)
        {
            DiagnosticCascadedShadowMapSettings changed = timing;
            mutate(changed);
            Require(!IsSameDiagnosticCsmTimingConfiguration(
                    timing, changed),
                field + " must create a new timer configuration generation");
            Require(BuildDiagnosticCsmTimingConfigurationIdentity(
                        changed) != timingIdentity &&
                    BuildDiagnosticCsmTimingConfigurationId(changed) !=
                        timingId,
                field +
                    " must create a distinct benchmark configuration identity");
        };
        RequireTimingChange([](auto& value) { value.cascadeCount = 3u; },
            "cascade count");
        RequireTimingChange([](auto& value) {
            value.shadowMapResolution = 1024u;
        }, "shadow-map resolution");
        RequireTimingChange([](auto& value) {
            value.maximumShadowDistance += 1.f;
        }, "shadow distance");
        RequireTimingChange([](auto& value) {
            value.maximumLightDepth += 1.f;
        }, "light depth");
        RequireTimingChange([](auto& value) {
            value.cascadeDistributionExponent += 1.f;
        }, "split exponent");
        RequireTimingChange([](auto& value) {
            value.cascadeTransitionFraction += 0.01f;
        }, "cascade fade");
        RequireTimingChange([](auto& value) {
            value.shadowDistanceFadeoutFraction += 0.01f;
        }, "distance fade");
        RequireTimingChange([](auto& value) {
            value.projectionSnapTexelMultiple = 1u;
        }, "projection snap multiple");
        RequireTimingChange([](auto& value) {
            value.enforceUeMinimumLightDepth =
                !value.enforceUeMinimumLightDepth;
        }, "UE minimum light depth");
        RequireTimingChange([](auto& value) { ++value.depthBias; },
            "depth bias");
        RequireTimingChange([](auto& value) {
            value.slopeScaledDepthBias += 1.f;
        }, "slope bias");
        RequireTimingChange([](auto& value) {
            value.directionalLightShadowBias += 0.1f;
        }, "directional-light shadow bias");
        RequireTimingChange([](auto& value) {
            value.directionalLightShadowSlopeBias += 0.1f;
        }, "directional-light slope bias");
        RequireTimingChange([](auto& value) {
            value.receiverDepthBias += 0.01f;
        }, "receiver bias");
        RequireTimingChange([](auto& value) {
            value.filter = DiagnosticCsmFilter::Poisson;
        }, "filter mode");
        RequireTimingChange([](auto& value) {
            value.poissonTapCount = 8u;
        }, "Poisson tap count");
        RequireTimingChange([](auto& value) {
            value.filterRadiusTexels += 1.f;
        }, "filter radius");
        RequireTimingChange([](auto& value) {
            value.use16BitDepthEnabled = !value.use16BitDepthEnabled;
        }, "shadow depth format");
        RequireTimingChange([](auto& value) {
            value.opaqueDepthStateMergingEnabled =
                !value.opaqueDepthStateMergingEnabled;
        }, "opaque depth-state merging");
        RequireTimingChange([](auto& value) {
            value.positionOnlyOpaqueEnabled =
                !value.positionOnlyOpaqueEnabled;
        }, "position-only opaque casters");
        RequireTimingChange([](auto& value) {
            value.translationOnlyCasterTransformEnabled =
                !value.translationOnlyCasterTransformEnabled;
        }, "translation-only caster transforms");
        RequireTimingChange([](auto& value) {
            value.inputAssemblerCasterFetchEnabled =
                !value.inputAssemblerCasterFetchEnabled;
        }, "input-assembler caster fetch");
        RequireTimingChange([](auto& value) {
            value.precomputedDepthAxisInverseLengthEnabled =
                !value.precomputedDepthAxisInverseLengthEnabled;
        }, "precomputed depth-axis inverse length");
        RequireTimingChange([](auto& value) {
            value.conservativeSaturatedSlopeEnabled =
                !value.conservativeSaturatedSlopeEnabled;
        }, "conservative saturated-slope shortcut");
        RequireTimingChange([](auto& value) {
            value.algebraicSlowSlopeEnabled =
                !value.algebraicSlowSlopeEnabled;
        }, "algebraic slow-slope reduction");
        RequireTimingChange([](auto& value) {
            value.preNormalizedReceiverLightDirectionEnabled =
                !value.preNormalizedReceiverLightDirectionEnabled;
        }, "pre-normalized receiver light direction");
        RequireTimingChange([](auto& value) {
            value.precomposedClipToShadowEnabled =
                !value.precomposedClipToShadowEnabled;
        }, "precomposed clip-to-shadow receiver transform");
        RequireTimingChange([](auto& value) {
            value.accurateCasterCullingEnabled =
                !value.accurateCasterCullingEnabled;
        }, "accurate caster culling");
        RequireTimingChange([](auto& value) {
            value.ueCasterRadiusThresholdEnabled =
                !value.ueCasterRadiusThresholdEnabled;
        }, "UE caster radius threshold");
        RequireTimingChange([](auto& value) {
            value.casterRadiusThreshold += 0.01f;
        }, "caster radius threshold value");
        RequireTimingChange([](auto& value) {
            value.singleTraversalCasterClassificationEnabled =
                !value.singleTraversalCasterClassificationEnabled;
        }, "single-traversal caster classification");
        RequireTimingChange([](auto& value) {
            value.precomputedReceiverHullAxesEnabled =
                !value.precomputedReceiverHullAxesEnabled;
        }, "precomputed receiver hull axes");
        RequireTimingChange([](auto& value) {
            value.sharedCasterLightProjectionEnabled =
                !value.sharedCasterLightProjectionEnabled;
        }, "shared caster light projection");
        RequireTimingChange([](auto& value) {
            value.directCasterSubmissionEnabled =
                !value.directCasterSubmissionEnabled;
        }, "direct caster submission");
        RequireTimingChange([](auto& value) {
            value.cachedShadowDrawListsEnabled =
                !value.cachedShadowDrawListsEnabled;
        }, "cached shadow draw lists");
        RequireTimingChange([](auto& value) {
            value.batchedFullRedrawClearEnabled =
                !value.batchedFullRedrawClearEnabled;
        }, "batched full-redraw clear");
        RequireTimingChange([](auto& value) {
            value.receiverRasterScissorEnabled =
                !value.receiverRasterScissorEnabled;
        }, "receiver raster scissor");
        RequireTimingChange([](auto& value) {
            value.wholeMapReuseEnabled = true;
        }, "whole-map reuse");
        RequireTimingChange([](auto& value) {
            value.wholeCascadeReuseEnabled = true;
        }, "whole-cascade reuse");
        RequireTimingChange([](auto& value) {
            value.dirtyRectanglesEnabled = true;
        }, "dirty rectangles");
        RequireTimingChange([](auto& value) {
            value.scrollingEnabled = true;
        }, "scrolling");
        RequireTimingChange([](auto& value) {
            value.minimumScrollOverlap -= 0.1f;
        }, "minimum scroll overlap");
        RequireTimingChange([](auto& value) {
            value.detailedGpuTimingEnabled =
                !value.detailedGpuTimingEnabled;
        }, "detailed timing");
        RequireTimingChange([](auto& value) {
            value.debugView = DiagnosticCsmDebugView::Visibility;
        }, "debug view");

        Require(Near(ComputeDiagnosticCsmLightDepthSpan(100.f, 400.f),
                    400.f) &&
                Near(ComputeDiagnosticCsmLightDepthSpan(300.f, 400.f),
                    600.f),
            "actual light depth must retain the request or expand to fit the cascade sphere");
        Require(ComputeDiagnosticCsmLightDepthSpan(0.f, 400.f) == 0.f &&
                ComputeDiagnosticCsmLightDepthSpan(
                    100.f,
                    std::numeric_limits<float>::infinity()) == 0.f,
            "invalid light-depth inputs must fail safely");

        Require(Near(ComputeDiagnosticCsmProjectionGuardTexels(
                    2.f, 4u), 7.f) &&
                Near(ComputeDiagnosticCsmProjectionGuardTexels(
                    0.f, 1u), 2.f) &&
                Near(ComputeDiagnosticCsmProjectionGuardTexels(
                    std::numeric_limits<float>::quiet_NaN(), 0u), 2.f),
            "projection guards must cover filtering plus the full fmod snap quantum");
        Require(Near(SnapUeCsmProjectionCoordinate(2.6f, 1.f), 2.f) &&
                Near(SnapUeCsmProjectionCoordinate(-2.6f, 1.f), -2.f) &&
                Near(SnapUeCsmProjectionCoordinate(3.9999f, 4.f), 0.f) &&
                Near(SnapUeCsmProjectionCoordinate(-3.9999f, 4.f), 0.f) &&
                Near(SnapUeCsmProjectionCoordinate(2.f, 1.f), 2.f) &&
                std::isnan(SnapUeCsmProjectionCoordinate(
                    std::numeric_limits<float>::quiet_NaN(), 1.f)),
            "UE projection snapping must use fmod phase with truncation toward zero");

        Require(Near(
                    ComputeUeCsmSoftTransitionScale(
                        10.f, 2000.f, 100.f, 2000u),
                    4000.f,
                    0.1f) &&
                Near(ComputeUeCsmSoftTransitionScale(
                    0.f, 2000.f, 100.f, 2000u), 100000.f, 10.f),
            "UE soft-transition scale must use normalized bias and its finite minimum");
        Require(ComputeUeCsmSoftTransitionScale(
                    10.f, 0.f, 100.f, 2000u) == 0.f &&
                ComputeUeCsmSoftTransitionScale(
                    std::numeric_limits<float>::quiet_NaN(),
                    2000.f,
                    100.f,
                    2000u) == 0.f,
            "invalid UE soft-transition inputs must fail safely");

        Require(Near(ComputeUeCsmShaderDepthBias(
                    10.f, 2000.f, 100.f, 2000u), 0.00025f) &&
                ComputeUeCsmShaderDepthBias(
                    10.f, 0.f, 100.f, 2000u) == 0.f,
            "UE shader depth bias must be normalized and format independent");
        Require(Near(ComputeUeCsmReceiverTransitionScale(
                    4000.f, 0.9f, 0.f), 400.f, 0.01f) &&
                Near(ComputeUeCsmReceiverTransitionScale(
                    4000.f, 0.9f, 0.5f), 2200.f, 0.01f) &&
                Near(ComputeUeCsmReceiverTransitionScale(
                    4000.f, 0.9f, 1.f), 4000.f, 0.01f) &&
                ComputeUeCsmReceiverTransitionScale(
                    0.f, 0.9f, 0.5f) == 0.f,
            "UE receiver bias must scale the comparison transition by NoL");
        Require(ShouldCullDiagnosticCsmCasterByRadiusSquared(
                    0.99f * 0.99f, 100.f * 100.f, 0.01f) &&
                !ShouldCullDiagnosticCsmCasterByRadiusSquared(
                    1.f, 100.f * 100.f, 0.01f) &&
                !ShouldCullDiagnosticCsmCasterByRadiusSquared(
                    0.99f * 0.99f, 100.f * 100.f, 0.f) &&
                !ShouldCullDiagnosticCsmCasterByRadiusSquared(
                    std::numeric_limits<float>::quiet_NaN(),
                    100.f * 100.f,
                    0.01f) &&
                !ShouldCullDiagnosticCsmCasterByRadiusSquared(
                    1.f,
                    std::numeric_limits<float>::quiet_NaN(),
                    0.01f) &&
                !ShouldCullDiagnosticCsmCasterByRadiusSquared(
                    1.f, -1.f, 0.01f) &&
                !ShouldCullDiagnosticCsmCasterByRadiusSquared(
                    1.f,
                    100.f * 100.f,
                    std::numeric_limits<float>::infinity()) &&
                !ShouldCullDiagnosticCsmCasterByRadiusSquared(
                    1.f,
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max()),
            "UE radius threshold must compare squared values, retain a strict boundary, and fail open");
        Require(ShouldBuildDiagnosticCsmSharedCasterLightShape(
                    false, true, true) &&
                !ShouldBuildDiagnosticCsmSharedCasterLightShape(
                    true, true, true) &&
                !ShouldBuildDiagnosticCsmSharedCasterLightShape(
                    false, false, true) &&
                !ShouldBuildDiagnosticCsmSharedCasterLightShape(
                    false, true, false),
            "shared caster projection must skip radius-rejected and unreliable casters");

        std::array<DiagnosticCsmUpdateAction, 4u> actions = {
            DiagnosticCsmUpdateAction::DirtyRectangles,
            DiagnosticCsmUpdateAction::DirtyRectangles,
            DiagnosticCsmUpdateAction::DirtyRectangles,
            DiagnosticCsmUpdateAction::DirtyRectangles
        };
        const std::array<bool, 4u> localCasterChanged = {
            true, false, false, false
        };
        for (size_t cascade = 0u; cascade < actions.size(); ++cascade)
        {
            actions[cascade] =
                FinalizeDiagnosticCsmLocalizedSceneAction(
                    actions[cascade],
                    true,
                    localCasterChanged[cascade]);
        }
        Require(actions[0] ==
                    DiagnosticCsmUpdateAction::DirtyRectangles &&
                actions[1] == DiagnosticCsmUpdateAction::Reused &&
                actions[2] == DiagnosticCsmUpdateAction::Reused &&
                actions[3] == DiagnosticCsmUpdateAction::Reused,
            "a localized transform must not redraw unaffected cascades");
        Require(FinalizeDiagnosticCsmLocalizedSceneAction(
                    DiagnosticCsmUpdateAction::Scrolled,
                    true,
                    false) == DiagnosticCsmUpdateAction::Scrolled &&
                FinalizeDiagnosticCsmLocalizedSceneAction(
                    DiagnosticCsmUpdateAction::FullRedraw,
                    true,
                    false) == DiagnosticCsmUpdateAction::FullRedraw,
            "scroll-exposed work and fail-open redraws must survive local refinement");

        const DiagnosticCsmRect update{ 0, 2, 0, 8 };
        Require(ShouldRenderDiagnosticCsmCasterForUpdateRect(
                    false, {}, update) &&
                ShouldRenderDiagnosticCsmCasterForUpdateRect(
                    false, { 4, 8, 0, 8 }, update) &&
                !ShouldRenderDiagnosticCsmCasterForUpdateRect(
                    true, { 4, 8, 0, 8 }, update) &&
                ShouldRenderDiagnosticCsmCasterForUpdateRect(
                    true, { 1, 3, 1, 7 }, update) &&
                !ShouldRenderDiagnosticCsmCasterForUpdateRect(
                    false, {}, {}),
            "unreliable caster bounds must fail open for valid update rectangles");

        Require(ShouldResetDiagnosticCsmDepthBindings(true, true) &&
                !ShouldResetDiagnosticCsmDepthBindings(true, false) &&
                !ShouldResetDiagnosticCsmDepthBindings(false, true) &&
                !ShouldResetDiagnosticCsmDepthBindings(false, false),
            "only a changed scene with full content invalidation may reset depth bindings");
    }

    void TestRectangles()
    {
        const DiagnosticCsmRect rectangle{ 1, 5, 2, 7 };
        Require(rectangle.IsValid() && rectangle.Area() == 20u,
            "half-open rectangles must report their exact area");
        Require(!DiagnosticCsmRect{ 1, 1, 2, 7 }.IsValid() &&
                DiagnosticCsmRect{ 1, 1, 2, 7 }.Area() == 0u &&
                !DiagnosticCsmRect{ 5, 1, 2, 7 }.IsValid(),
            "empty and reversed rectangles must be invalid");

        const DiagnosticCsmRect overlapping{ 4, 8, 5, 9 };
        const DiagnosticCsmRect touching{ 5, 8, 2, 7 };
        Require(DiagnosticCsmRectsOverlap(rectangle, overlapping) &&
                DiagnosticCsmRectsOverlap(overlapping, rectangle),
            "overlap tests must be symmetric for positive-area intersections");
        Require(!DiagnosticCsmRectsOverlap(rectangle, touching),
            "half-open rectangles that only share an edge must not overlap");

        RequireRect(
            UnionDiagnosticCsmRects(rectangle, overlapping),
            { 1, 8, 2, 9 },
            "rectangle union must bound both affected regions");
        const DiagnosticCsmRect invalid{ 2, 2, 0, 1 };
        RequireRect(
            UnionDiagnosticCsmRects(invalid, rectangle),
            rectangle,
            "an invalid old rectangle must leave the new rectangle unchanged");
        RequireRect(
            UnionDiagnosticCsmRects(rectangle, invalid),
            rectangle,
            "an invalid new rectangle must leave the old rectangle unchanged");

        const DiagnosticCsmRect oldCaster{ 1, 3, 1, 3 };
        const DiagnosticCsmRect newCaster{ 4, 7, 2, 6 };
        RequireRect(
            UnionDiagnosticCsmRects(oldCaster, newCaster),
            { 1, 7, 1, 6 },
            "dirty updates must retain a bounding region covering old and new caster footprints");

        RequireRect(
            ClipDiagnosticCsmRect({ -3, 12, -4, 10 }, 8u),
            { 0, 8, 0, 8 },
            "clipping must clamp every edge to the shadow-map extent");
        const DiagnosticCsmRect clippedAway =
            ClipDiagnosticCsmRect({ 10, 12, -3, 4 }, 8u);
        RequireRect(clippedAway, { 8, 8, 0, 4 },
            "a rectangle outside the map must clamp to an empty boundary rectangle");
        Require(!clippedAway.IsValid() && clippedAway.Area() == 0u,
            "a fully clipped rectangle must remain invalid with zero area");
        RequireRect(
            ClipDiagnosticCsmRect({ -1, 1, -1, 1 }, 0u),
            { 0, 0, 0, 0 },
            "a zero-resolution map must clip every rectangle to empty");

        RequireRect(
            MakeClippedDiagnosticCsmRectFromUvBounds(
                0.25f, 0.5f, 0.125f, 0.75f, 8u, 1u),
            { 1, 5, 0, 7 },
            "projected UV bounds must expand by the exact dirty halo");
        RequireRect(
            MakeClippedDiagnosticCsmRectFromUvBounds(
                -std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                2049u,
                10u),
            { 0, 2049, 0, 2049 },
            "huge finite projected bounds must clip safely before integer conversion");
        const DiagnosticCsmRect hugeOutside =
            MakeClippedDiagnosticCsmRectFromUvBounds(
                std::numeric_limits<float>::max() * 0.5f,
                std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max() * 0.5f,
                2049u,
                10u);
        RequireRect(hugeOutside, { 2049, 2049, 0, 0 },
            "huge off-map projected bounds must clip to an empty boundary rectangle");
        Require(!hugeOutside.IsValid(),
            "huge off-map projected bounds must not create a bogus dirty update");

        Require(Near(NormalizeDiagnosticCsmFilterRadiusTexels(-1.f), 0.f) &&
                Near(NormalizeDiagnosticCsmFilterRadiusTexels(3.f), 3.f) &&
                Near(NormalizeDiagnosticCsmFilterRadiusTexels(100.f),
                    DiagnosticCsmMaximumFilterRadiusTexels) &&
                std::isnan(NormalizeDiagnosticCsmFilterRadiusTexels(
                    std::numeric_limits<float>::quiet_NaN())),
            "validated custom filter radii must stay inside the cache-safe UI range");
    }

    void TestScrollRegions()
    {
        struct QuadrantExpectation
        {
            int32_t shiftX;
            int32_t shiftY;
            DiagnosticCsmRect source;
            DiagnosticCsmRect destination;
            DiagnosticCsmRect exposedY;
            DiagnosticCsmRect exposedX;
        };

        const std::array<QuadrantExpectation, 4u> quadrants = {{
            {
                2, 3,
                { 0, 6, 0, 5 }, { 2, 8, 3, 8 },
                { 0, 8, 0, 3 }, { 0, 2, 3, 8 }
            },
            {
                2, -3,
                { 0, 6, 3, 8 }, { 2, 8, 0, 5 },
                { 0, 8, 5, 8 }, { 0, 2, 0, 5 }
            },
            {
                -2, 3,
                { 2, 8, 0, 5 }, { 0, 6, 3, 8 },
                { 0, 8, 0, 3 }, { 6, 8, 3, 8 }
            },
            {
                -2, -3,
                { 2, 8, 3, 8 }, { 0, 6, 0, 5 },
                { 0, 8, 5, 8 }, { 6, 8, 0, 5 }
            }
        }};

        for (const QuadrantExpectation& expected : quadrants)
        {
            const DiagnosticCsmScrollRegions regions =
                ComputeDiagnosticCsmScrollRegions(
                    8u, expected.shiftX, expected.shiftY);
            const std::string context = "R=8 scroll delta (" +
                std::to_string(expected.shiftX) + ", " +
                std::to_string(expected.shiftY) + ")";
            Require(regions.valid, context + " must be valid");
            RequireRect(regions.source, expected.source,
                context + " must copy from the exact compatible source");
            RequireRect(regions.destination, expected.destination,
                context + " must copy to the translated destination");
            Require(regions.exposedCount == 2u,
                context + " must produce two non-overlapping exposed strips");
            RequireRect(regions.exposed[0], expected.exposedY,
                context + " must expose the expected full-width strip");
            RequireRect(regions.exposed[1], expected.exposedX,
                context + " must expose the expected overlap-height strip");
            Require(regions.copiedTexels == 30u &&
                    regions.exposedTexels == 34u,
                context + " must exactly partition the 64 texels");
            Require(regions.source.Area() == regions.copiedTexels &&
                    regions.destination.Area() == regions.copiedTexels &&
                    regions.exposed[0].Area() +
                        regions.exposed[1].Area() ==
                        regions.exposedTexels,
                context + " areas must agree with the scroll counters");
            Require(!DiagnosticCsmRectsOverlap(
                    regions.exposed[0], regions.exposed[1]),
                context + " exposed strips must not overlap");
            const std::array<int32_t, 2u> sourceOffset =
                ComputeDiagnosticCsmScrollSourceOffset(regions);
            Require(sourceOffset[0] == -expected.shiftX &&
                    sourceOffset[1] == -expected.shiftY,
                context +
                    " must sample the exact source texel in the depth blit");
        }

        struct AxisExpectation
        {
            int32_t shiftX;
            int32_t shiftY;
            DiagnosticCsmRect exposed;
            uint64_t copiedTexels;
        };
        const std::array<AxisExpectation, 4u> axes = {{
            { 2, 0, { 0, 2, 0, 8 }, 48u },
            { -2, 0, { 6, 8, 0, 8 }, 48u },
            { 0, 3, { 0, 8, 0, 3 }, 40u },
            { 0, -3, { 0, 8, 5, 8 }, 40u }
        }};
        for (const AxisExpectation& expected : axes)
        {
            const DiagnosticCsmScrollRegions regions =
                ComputeDiagnosticCsmScrollRegions(
                    8u, expected.shiftX, expected.shiftY);
            Require(regions.valid && regions.exposedCount == 1u,
                "a one-axis scroll must produce exactly one exposed strip");
            RequireRect(regions.exposed[0], expected.exposed,
                "a one-axis scroll must expose the correct signed edge");
            Require(regions.copiedTexels == expected.copiedTexels &&
                    regions.exposedTexels == 64u - expected.copiedTexels,
                "a one-axis scroll must exactly partition the map");
        }

        const DiagnosticCsmScrollRegions stationary =
            ComputeDiagnosticCsmScrollRegions(8u, 0, 0);
        Require(stationary.valid && stationary.exposedCount == 0u &&
                stationary.copiedTexels == 64u &&
                stationary.exposedTexels == 0u,
            "a zero scroll must reuse the complete map without exposed regions");
        RequireRect(stationary.source, { 0, 8, 0, 8 },
            "a zero scroll source must cover the complete map");
        RequireRect(stationary.destination, { 0, 8, 0, 8 },
            "a zero scroll destination must cover the complete map");

        Require(!ComputeDiagnosticCsmScrollRegions(0u, 0, 0).valid &&
                !ComputeDiagnosticCsmScrollRegions(8u, 8, 0).valid &&
                !ComputeDiagnosticCsmScrollRegions(8u, -8, 0).valid &&
                !ComputeDiagnosticCsmScrollRegions(8u, 0, 8).valid &&
                !ComputeDiagnosticCsmScrollRegions(8u, 0, -8).valid &&
                !ComputeDiagnosticCsmScrollRegions(
                    std::numeric_limits<uint32_t>::max(), 0, 0).valid,
            "empty, non-overlapping, and unrepresentable scrolls must invalidate");
        const DiagnosticCsmScrollRegions maximumRepresentableScroll =
            ComputeDiagnosticCsmScrollRegions(
                uint32_t(std::numeric_limits<int32_t>::max()),
                -1,
                0);
        Require(maximumRepresentableScroll.valid &&
                maximumRepresentableScroll.source.minX == 1 &&
                maximumRepresentableScroll.source.maxX ==
                    std::numeric_limits<int32_t>::max() &&
                maximumRepresentableScroll.destination.minX == 0 &&
                maximumRepresentableScroll.destination.maxX ==
                    std::numeric_limits<int32_t>::max() - 1,
            "maximum representable scroll coordinates must use overflow-safe intermediates");
        Require(ComputeDiagnosticCsmScrollSourceOffset(
                    DiagnosticCsmScrollRegions{}) ==
                std::array<int32_t, 2u>{ 0, 0 },
            "an invalid scroll must provide a harmless zero source offset");

        const auto verifyScrolledContent = [](uint32_t size,
            int32_t shiftX,
            int32_t shiftY)
        {
            constexpr uint32_t Exposed =
                std::numeric_limits<uint32_t>::max();
            const DiagnosticCsmScrollRegions regions =
                ComputeDiagnosticCsmScrollRegions(
                    size, shiftX, shiftY);
            Require(regions.valid,
                "odd-resolution content scroll must classify as valid");
            const uint64_t texelCount = uint64_t(size) * uint64_t(size);
            Require(texelCount < uint64_t(Exposed),
                "content simulation requires unique 32-bit source texels");
            const size_t texelCountAsSizeT = size_t(texelCount);
            std::vector<uint32_t> source(texelCountAsSizeT);
            std::vector<uint32_t> destination(
                texelCountAsSizeT, Exposed - 1u);
            for (uint32_t index = 0u;
                index < uint32_t(texelCount);
                ++index)
            {
                source[index] = index;
            }

            const std::array<int32_t, 2u> sourceOffset =
                ComputeDiagnosticCsmScrollSourceOffset(regions);
            for (int32_t y = regions.destination.minY;
                y < regions.destination.maxY;
                ++y)
            {
                for (int32_t x = regions.destination.minX;
                    x < regions.destination.maxX;
                    ++x)
                {
                    const int32_t sourceX = x + sourceOffset[0];
                    const int32_t sourceY = y + sourceOffset[1];
                    Require(sourceX >= regions.source.minX &&
                            sourceX < regions.source.maxX &&
                            sourceY >= regions.source.minY &&
                            sourceY < regions.source.maxY,
                        "every reused destination texel must map inside the exact source rectangle");
                    destination[size_t(y) * size_t(size) + size_t(x)] =
                        source[size_t(sourceY) * size_t(size) +
                            size_t(sourceX)];
                }
            }
            for (uint32_t exposedIndex = 0u;
                exposedIndex < regions.exposedCount;
                ++exposedIndex)
            {
                const DiagnosticCsmRect& exposed =
                    regions.exposed[exposedIndex];
                for (int32_t y = exposed.minY; y < exposed.maxY; ++y)
                {
                    for (int32_t x = exposed.minX; x < exposed.maxX; ++x)
                    {
                        const size_t index = size_t(y) * size_t(size) +
                            size_t(x);
                        Require(destination[index] == Exposed - 1u,
                            "reused and exposed odd-resolution texels must remain disjoint");
                        destination[index] = Exposed;
                    }
                }
            }

            for (uint32_t y = 0u; y < size; ++y)
            {
                for (uint32_t x = 0u; x < size; ++x)
                {
                    const size_t index = size_t(y) * size_t(size) +
                        size_t(x);
                    const bool reused =
                        int32_t(x) >= regions.destination.minX &&
                        int32_t(x) < regions.destination.maxX &&
                        int32_t(y) >= regions.destination.minY &&
                        int32_t(y) < regions.destination.maxY;
                    if (reused)
                    {
                        const uint32_t expected = uint32_t(
                            (int32_t(y) + sourceOffset[1]) *
                                int32_t(size) +
                            int32_t(x) + sourceOffset[0]);
                        Require(destination[index] == expected,
                            "odd-resolution scroll must preserve the exact compatible source texel");
                    }
                    else
                    {
                        Require(destination[index] == Exposed,
                            "every non-reused odd-resolution texel must be exposed for clearing and redraw");
                    }
                }
            }
        };
        verifyScrolledContent(7u, 2, -3);
        verifyScrolledContent(2049u, -17, 31);
    }

    DiagnosticCsmProjectionCompatibility MakeProjection(
        const void* lightIdentity)
    {
        DiagnosticCsmProjectionCompatibility projection;
        projection.lightIdentity = lightIdentity;
        projection.lightBasis = {
            1.f, 0.f, 0.f,
            0.f, 1.f, 0.f,
            0.f, 0.f, 1.f
        };
        projection.radius = 20.f;
        projection.texelWorldSize = 0.5f;
        projection.snappedCenterX = 4.f;
        projection.snappedCenterY = 8.f;
        projection.snappedCenterZ = 2.f;
        projection.depthNear = -100.f;
        projection.depthFar = 300.f;
        projection.splitNear = 10.f;
        projection.splitFar = 100.f;
        projection.depthBias = 10;
        projection.slopeScaledDepthBias = 3.f;
        projection.resolution = 8u;
        projection.formatKey = 32u;
        projection.normalDepth = true;
        return projection;
    }

    void TestProjectionCompatibilityAndInvalidation()
    {
        int light = 0;
        int otherLight = 0;
        const DiagnosticCsmProjectionCompatibility baseline =
            MakeProjection(&light);

        Require(IsFiniteDiagnosticCsmProjectionCompatibility(baseline),
            "the deterministic projection baseline must be finite and reusable");
        std::array<DiagnosticCsmProjectionCompatibility, 9u>
            invalidProjections{};
        invalidProjections.fill(baseline);
        invalidProjections[0].lightBasis[3] =
            std::numeric_limits<float>::infinity();
        invalidProjections[1].radius =
            std::numeric_limits<float>::infinity();
        invalidProjections[2].snappedCenterZ =
            std::numeric_limits<float>::quiet_NaN();
        invalidProjections[3].depthNear =
            -std::numeric_limits<float>::infinity();
        invalidProjections[4].splitFar =
            std::numeric_limits<float>::infinity();
        invalidProjections[5].depthBias =
            std::numeric_limits<float>::infinity();
        invalidProjections[6].slopeScaledDepthBias =
            std::numeric_limits<float>::quiet_NaN();
        invalidProjections[7].depthFar =
            invalidProjections[7].depthNear;
        invalidProjections[8].radius = 0.f;
        for (const auto& invalidProjection : invalidProjections)
        {
            Require(!IsFiniteDiagnosticCsmProjectionCompatibility(
                        invalidProjection),
                "non-finite or degenerate projection compatibility must be invalid");
            const DiagnosticCsmScrollClassification invalidReuse =
                ClassifyDiagnosticCsmProjectionChange(
                    invalidProjection,
                    invalidProjection,
                    0.f);
            Require(!invalidReuse.exactReuse &&
                    !invalidReuse.scrollCompatible,
                "matching invalid projection fields must never make cache reuse legal");
        }

        Require(GetDiagnosticCsmProjectionInvalidationFlags(
                    baseline, baseline) ==
                DiagnosticCsmInvalidation_None,
            "an exact projection must not report an invalidation reason");

        const DiagnosticCsmScrollClassification exact =
            ClassifyDiagnosticCsmProjectionChange(
                baseline, baseline, 1.f);
        Require(exact.exactReuse && !exact.scrollCompatible &&
                exact.destinationShiftX == 0 &&
                exact.destinationShiftY == 0 &&
                Near(exact.overlap, 1.f),
            "an identical compatible projection must permit exact whole-cascade reuse");

        DiagnosticCsmProjectionCompatibility scrolled = baseline;
        scrolled.snappedCenterX -= 2.f * baseline.texelWorldSize;
        scrolled.snappedCenterY -= 3.f * baseline.texelWorldSize;
        const DiagnosticCsmScrollClassification acceptedScroll =
            ClassifyDiagnosticCsmProjectionChange(
                baseline, scrolled, 0.4f);
        Require(!acceptedScroll.exactReuse &&
                acceptedScroll.scrollCompatible &&
                acceptedScroll.destinationShiftX == 2 &&
                acceptedScroll.destinationShiftY == -3 &&
                Near(acceptedScroll.overlap, 30.f / 64.f),
            "R=8 center motion must include the UV Y flip and classify as (2, -3)");

        DiagnosticCsmProjectionCompatibility positiveY = baseline;
        positiveY.snappedCenterY += 1.f * baseline.texelWorldSize;
        const DiagnosticCsmScrollClassification positiveYScroll =
            ClassifyDiagnosticCsmProjectionChange(
                baseline, positiveY, 0.f);
        Require(positiveYScroll.scrollCompatible &&
                positiveYScroll.destinationShiftX == 0 &&
                positiveYScroll.destinationShiftY == 1,
            "positive light-space Y motion must move unchanged texels toward positive UV Y");

        const DiagnosticCsmScrollClassification rejectedByThreshold =
            ClassifyDiagnosticCsmProjectionChange(
                baseline, scrolled, 0.5f);
        Require(!rejectedByThreshold.exactReuse &&
                !rejectedByThreshold.scrollCompatible &&
                Near(rejectedByThreshold.overlap, 30.f / 64.f),
            "scroll overlap below the configured threshold must force invalidation");
        Require(ClassifyDiagnosticCsmProjectionChange(
                    baseline, scrolled, 30.f / 64.f).scrollCompatible,
            "scroll overlap equal to the threshold must remain reusable");
        Require(ClassifyDiagnosticCsmProjectionChange(
                    baseline, scrolled, -1.f).scrollCompatible,
            "negative overlap thresholds must clamp to zero");
        Require(!ClassifyDiagnosticCsmProjectionChange(
                    baseline, scrolled, 2.f).scrollCompatible,
            "overlap thresholds above one must clamp to one");
        Require(!ClassifyDiagnosticCsmProjectionChange(
                    baseline,
                    scrolled,
                    std::numeric_limits<float>::quiet_NaN())
                    .scrollCompatible,
            "a non-finite overlap threshold must require complete overlap");

        auto RequireInvalidated = [&baseline](
            const DiagnosticCsmProjectionCompatibility& current,
            const std::string& field)
        {
            const DiagnosticCsmScrollClassification classification =
                ClassifyDiagnosticCsmProjectionChange(
                    baseline, current, 0.f);
            Require(!classification.exactReuse &&
                    !classification.scrollCompatible,
                field + " changes must fully invalidate cached projection data");
        };

        DiagnosticCsmProjectionCompatibility changed = baseline;
        changed.lightIdentity = &otherLight;
        Require(GetDiagnosticCsmProjectionInvalidationFlags(
                    baseline, changed) ==
                DiagnosticCsmInvalidation_Light,
            "light identity changes must report light invalidation");
        RequireInvalidated(changed, "directional-light identity");
        changed = baseline;
        changed.lightBasis[4] = 2.f;
        RequireInvalidated(changed, "light basis");
        changed = baseline;
        changed.radius += 1.f;
        RequireInvalidated(changed, "projection radius");
        changed = baseline;
        changed.texelWorldSize *= 2.f;
        RequireInvalidated(changed, "world-space texel size");
        changed = baseline;
        changed.snappedCenterZ += 0.5f;
        Require(GetDiagnosticCsmProjectionInvalidationFlags(
                    baseline, changed) ==
                DiagnosticCsmInvalidation_DepthMapping,
            "depth-center changes must report depth-mapping invalidation");
        RequireInvalidated(changed, "light-space depth center");
        changed = baseline;
        changed.depthNear -= 1.f;
        RequireInvalidated(changed, "depth near mapping");
        changed = baseline;
        changed.depthFar += 1.f;
        RequireInvalidated(changed, "depth far mapping");
        changed = baseline;
        changed.splitNear += 1.f;
        RequireInvalidated(changed, "cascade split near distance");
        changed = baseline;
        changed.splitFar += 1.f;
        RequireInvalidated(changed, "cascade split far distance");
        changed = baseline;
        changed.depthBias += 1;
        Require(GetDiagnosticCsmProjectionInvalidationFlags(
                    baseline, changed) ==
                DiagnosticCsmInvalidation_Bias,
            "bias changes must report bias invalidation");
        RequireInvalidated(changed, "constant depth bias");
        changed = baseline;
        changed.slopeScaledDepthBias += 1.f;
        RequireInvalidated(changed, "slope-scaled depth bias");
        changed = baseline;
        changed.resolution *= 2u;
        Require(GetDiagnosticCsmProjectionInvalidationFlags(
                    baseline, changed) ==
                DiagnosticCsmInvalidation_Resources,
            "resolution changes must report resource invalidation");
        RequireInvalidated(changed, "shadow-map resolution");
        changed = baseline;
        changed.formatKey += 1u;
        RequireInvalidated(changed, "depth format");
        changed = baseline;
        changed.normalDepth = false;
        RequireInvalidated(changed, "depth convention");

        changed = baseline;
        changed.lightIdentity = nullptr;
        RequireInvalidated(changed, "null light identity");
        changed = baseline;
        changed.texelWorldSize = 0.f;
        RequireInvalidated(changed, "zero world-space texel size");
        changed = baseline;
        changed.texelWorldSize =
            std::numeric_limits<float>::quiet_NaN();
        RequireInvalidated(changed, "non-finite world-space texel size");
        changed = baseline;
        changed.snappedCenterX += 0.25f;
        RequireInvalidated(changed, "non-integral horizontal texel motion");
        changed = baseline;
        changed.snappedCenterY =
            std::numeric_limits<float>::infinity();
        RequireInvalidated(changed, "non-finite vertical center motion");

        DiagnosticCsmProjectionCompatibility nullPrevious = baseline;
        nullPrevious.lightIdentity = nullptr;
        const DiagnosticCsmScrollClassification nullIdentity =
            ClassifyDiagnosticCsmProjectionChange(
                nullPrevious, nullPrevious, 0.f);
        Require(!nullIdentity.exactReuse &&
                !nullIdentity.scrollCompatible,
            "cached projections without a light identity must never be reused");

        changed = baseline;
        changed.snappedCenterX -=
            float(baseline.resolution) * baseline.texelWorldSize;
        Require(GetDiagnosticCsmProjectionInvalidationFlags(
                    baseline, changed) ==
                DiagnosticCsmInvalidation_Projection,
            "XY projection movement must report projection invalidation");
        const DiagnosticCsmScrollClassification noOverlap =
            ClassifyDiagnosticCsmProjectionChange(
                baseline, changed, 0.f);
        Require(!noOverlap.exactReuse &&
                !noOverlap.scrollCompatible &&
                noOverlap.destinationShiftX == 0,
            "a whole-map texel shift must invalidate before forming an unusable integer offset");

        DiagnosticCsmProjectionCompatibility hugePrevious = baseline;
        hugePrevious.resolution = std::numeric_limits<uint32_t>::max();
        hugePrevious.texelWorldSize = 1.f;
        hugePrevious.snappedCenterX = 0.f;
        changed = hugePrevious;
        changed.snappedCenterX = 2147483648.f;
        const DiagnosticCsmScrollClassification hugeShift =
            ClassifyDiagnosticCsmProjectionChange(
                hugePrevious, changed, 0.f);
        Require(!hugeShift.exactReuse &&
                !hugeShift.scrollCompatible &&
                hugeShift.destinationShiftX == 0,
            "an INT32_MIN-magnitude projection shift must invalidate before cast or negation");
    }
}

int main()
{
    try
    {
        TestGpuTimingNormalization();
        TestDiagnosticCsmBenchmarkTimingValidation();
        TestDiagnosticCsmBenchmarkWorkIdentity();
        TestDiagnosticCsmBenchmarkEvidenceValidation();
        TestGpuTimingSourceFrameClassification();
        TestProfilesAndCustomRetention();
        TestTranslationOnlyCasterTransformContract();
        TestCachedShadowDrawListContract();
        TestLightFrameAndCoarseBoundsHardening();
        TestInputAssemblerCasterFetchContract();
        TestUeStyleCasterNormalTransform();
        TestResolveLightDirectionPermutationContract();
        TestPrecomposedClipToShadowTransform();
        TestCasterGatherPlanning();
        TestProjectedCasterOptimizationParity();
        TestPrecomputedDepthAxisInverseLength();
        TestConservativeSaturatedSlope();
        TestAlgebraicSlowSlope();
        TestReceiverRasterScissor();
        TestBatchedFullRedrawClearPlanning();
        TestCasterSubmissionIndexing();
        TestUeGeometricSplitsAndFades();
        TestTapNormalizationAndCacheGating();
        TestCacheRefinementAndDiagnosticConfiguration();
        TestRectangles();
        TestScrollRegions();
        TestProjectionCompatibilityAndInvalidation();

        std::cout <<
            "Diagnostic cascaded shadow-map settings tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr <<
            "Diagnostic cascaded shadow-map settings tests failed: " <<
            error.what() << '\n';
        return 1;
    }
}
