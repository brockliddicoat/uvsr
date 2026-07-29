#pragma once

#include "diagnostic_cascaded_shadow_map.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

namespace uvsr
{
    [[nodiscard]] inline bool IsValidDiagnosticCsmBenchmarkTiming(
        const DiagnosticCsmTimings& timings)
    {
        const auto finiteNonnegative = [](float value) {
            return std::isfinite(value) && value >= 0.f;
        };
        if (!timings.supported ||
            !timings.active ||
            timings.gpuTimingSource !=
                DiagnosticCsmGpuTimingSource::TimerQuery ||
            !std::isfinite(timings.totalMilliseconds) ||
            !(timings.totalMilliseconds > 0.f) ||
            !finiteNonnegative(timings.setupCpuMilliseconds) ||
            !finiteNonnegative(timings.cullingCpuMilliseconds) ||
            !finiteNonnegative(timings.recordingCpuMilliseconds) ||
            !std::isfinite(timings.totalCpuMilliseconds) ||
            !(timings.totalCpuMilliseconds > 0.f))
        {
            return false;
        }

        const double totalCpu = double(timings.totalCpuMilliseconds);
        const double cpuNestingTolerance =
            std::max(0.0001, totalCpu * 0.0001);
        const double cpuStageSum =
            double(timings.setupCpuMilliseconds) +
            double(timings.cullingCpuMilliseconds) +
            double(timings.recordingCpuMilliseconds);
        const bool nestedCpuTimingsValid =
            double(timings.setupCpuMilliseconds) <=
                totalCpu + cpuNestingTolerance &&
            double(timings.cullingCpuMilliseconds) <=
                totalCpu + cpuNestingTolerance &&
            double(timings.recordingCpuMilliseconds) <=
                totalCpu + cpuNestingTolerance &&
            cpuStageSum <= totalCpu + cpuNestingTolerance;
        if (!nestedCpuTimingsValid)
            return false;

        const bool stageTimingsValid =
            finiteNonnegative(timings.cullingGpuMilliseconds) &&
            finiteNonnegative(timings.clearUpdateMilliseconds) &&
            finiteNonnegative(timings.rasterMilliseconds) &&
            finiteNonnegative(timings.samplingMilliseconds);
        if (!stageTimingsValid)
            return false;

        const double total = double(timings.totalMilliseconds);
        const double nestingTolerance =
            std::max(0.0001, total * 0.0001);
        const double stageSum =
            double(timings.cullingGpuMilliseconds) +
            double(timings.clearUpdateMilliseconds) +
            double(timings.rasterMilliseconds) +
            double(timings.samplingMilliseconds);
        const bool nestedStageTimingsValid =
            double(timings.cullingGpuMilliseconds) <=
                total + nestingTolerance &&
            double(timings.clearUpdateMilliseconds) <=
                total + nestingTolerance &&
            double(timings.rasterMilliseconds) <=
                total + nestingTolerance &&
            double(timings.samplingMilliseconds) <=
                total + nestingTolerance &&
            stageSum <= total + nestingTolerance;

        // Total-only queries publish exact zero for unavailable sub-stages.
        // Reject stale positive values as well as non-finite/negative values,
        // otherwise clearing the detailed flag could conceal mixed evidence.
        return timings.detailedGpuTimingEnabled
            ? nestedStageTimingsValid
            : (timings.cullingGpuMilliseconds == 0.f &&
                timings.clearUpdateMilliseconds == 0.f &&
                timings.rasterMilliseconds == 0.f &&
                timings.samplingMilliseconds == 0.f);
    }

    [[nodiscard]] inline bool
        HasSameDiagnosticCsmBenchmarkWorkIdentity(
            const DiagnosticCsmStats& left,
            const DiagnosticCsmStats& right)
    {
        return left.outputWidth == right.outputWidth &&
            left.outputHeight == right.outputHeight &&
            left.cascadeCount == right.cascadeCount &&
            left.shadowMapResolution == right.shadowMapResolution &&
            left.depthBitsPerTexel == right.depthBitsPerTexel &&
            left.filterSampleCount == right.filterSampleCount &&
            left.filterComparisonCount == right.filterComparisonCount &&
            left.maximumShadowDistance == right.maximumShadowDistance &&
            left.maximumLightDepth == right.maximumLightDepth &&
            left.maximumActualLightDepthSpan ==
                right.maximumActualLightDepthSpan &&
            left.filterRadiusTexels == right.filterRadiusTexels &&
            left.finestCoverageExtent == right.finestCoverageExtent &&
            left.coarsestCoverageExtent == right.coarsestCoverageExtent &&
            left.finestWorldTexelSize == right.finestWorldTexelSize &&
            left.coarsestWorldTexelSize == right.coarsestWorldTexelSize &&
            left.candidateCasterProjectionPairs ==
                right.candidateCasterProjectionPairs &&
            left.coarseCasterProjectionPairs ==
                right.coarseCasterProjectionPairs &&
            left.accuratelyCulledCasterProjectionPairs ==
                right.accuratelyCulledCasterProjectionPairs &&
            left.radiusCulledCasterProjectionPairs ==
                right.radiusCulledCasterProjectionPairs &&
            left.renderedCasterProjectionPairs ==
                right.renderedCasterProjectionPairs &&
            left.alphaTestedCasterProjectionPairs ==
                right.alphaTestedCasterProjectionPairs &&
            left.submittedDrawCalls == right.submittedDrawCalls &&
            left.submittedAlphaTestedDrawCalls ==
                right.submittedAlphaTestedDrawCalls &&
            left.submittedTranslationOnlyDrawCalls ==
                right.submittedTranslationOnlyDrawCalls &&
            left.manualCasterProjectionPairs ==
                right.manualCasterProjectionPairs &&
            left.inputAssemblerCasterProjectionPairs ==
                right.inputAssemblerCasterProjectionPairs &&
            left.submittedInstances == right.submittedInstances &&
            left.submittedTriangles == right.submittedTriangles &&
            left.submittedTranslationOnlyTriangles ==
                right.submittedTranslationOnlyTriangles &&
            left.casterSceneTraversals == right.casterSceneTraversals &&
            left.casterSorts == right.casterSorts &&
            left.cachedShadowDrawListHits ==
                right.cachedShadowDrawListHits &&
            left.cachedShadowDrawListMisses ==
                right.cachedShadowDrawListMisses &&
            left.cachedShadowDrawListEntries ==
                right.cachedShadowDrawListEntries &&
            left.cachedShadowDrawListCasterProjectionPairs ==
                right.cachedShadowDrawListCasterProjectionPairs &&
            left.reusedCascades == right.reusedCascades &&
            left.scrolledCascades == right.scrolledCascades &&
            left.dirtyCascades == right.dirtyCascades &&
            left.redrawnCascades == right.redrawnCascades &&
            left.dirtyRectangleCount == right.dirtyRectangleCount &&
            left.receiverRasterScissoredCascades ==
                right.receiverRasterScissoredCascades &&
            left.logicalTexels == right.logicalTexels &&
            left.updatedTexels == right.updatedTexels &&
            left.copiedTexels == right.copiedTexels &&
            left.clearedTexels == right.clearedTexels &&
            left.fullRedrawRasterBoundTexels ==
                right.fullRedrawRasterBoundTexels &&
            left.fullRedrawRasterExcludedTexels ==
                right.fullRedrawRasterExcludedTexels &&
            left.depthBytes == right.depthBytes &&
            left.visibilityBytes == right.visibilityBytes &&
            left.debugVisualizationBytes ==
                right.debugVisualizationBytes &&
            left.scrollingScratchBytes == right.scrollingScratchBytes &&
            left.invalidationMask == right.invalidationMask &&
            left.submissionStatsAvailable ==
                right.submissionStatsAvailable &&
            left.opaqueDepthStateMergingEnabled ==
                right.opaqueDepthStateMergingEnabled &&
            left.positionOnlyOpaqueEnabled ==
                right.positionOnlyOpaqueEnabled &&
            left.translationOnlyCasterTransformRequested ==
                right.translationOnlyCasterTransformRequested &&
            left.translationOnlyCasterTransformEnabled ==
                right.translationOnlyCasterTransformEnabled &&
            left.inputAssemblerCasterFetchRequested ==
                right.inputAssemblerCasterFetchRequested &&
            left.inputAssemblerCasterFetchEnabled ==
                right.inputAssemblerCasterFetchEnabled &&
            left.precomputedDepthAxisInverseLengthRequested ==
                right.precomputedDepthAxisInverseLengthRequested &&
            left.precomputedDepthAxisInverseLengthEnabled ==
                right.precomputedDepthAxisInverseLengthEnabled &&
            left.conservativeSaturatedSlopeRequested ==
                right.conservativeSaturatedSlopeRequested &&
            left.conservativeSaturatedSlopeActive ==
                right.conservativeSaturatedSlopeActive &&
            left.algebraicSlowSlopeRequested ==
                right.algebraicSlowSlopeRequested &&
            left.algebraicSlowSlopeActive ==
                right.algebraicSlowSlopeActive &&
            left.preNormalizedReceiverLightDirectionRequested ==
                right.preNormalizedReceiverLightDirectionRequested &&
            left.preNormalizedReceiverLightDirectionEnabled ==
                right.preNormalizedReceiverLightDirectionEnabled &&
            left.precomposedClipToShadowRequested ==
                right.precomposedClipToShadowRequested &&
            left.precomposedClipToShadowEnabled ==
                right.precomposedClipToShadowEnabled &&
            left.accurateCasterCullingRequested ==
                right.accurateCasterCullingRequested &&
            left.accurateCasterCullingEnabled ==
                right.accurateCasterCullingEnabled &&
            left.ueCasterRadiusThresholdRequested ==
                right.ueCasterRadiusThresholdRequested &&
            left.ueCasterRadiusThresholdEnabled ==
                right.ueCasterRadiusThresholdEnabled &&
            left.singleTraversalCasterClassificationRequested ==
                right.singleTraversalCasterClassificationRequested &&
            left.singleTraversalCasterClassificationEnabled ==
                right.singleTraversalCasterClassificationEnabled &&
            left.precomputedReceiverHullAxesRequested ==
                right.precomputedReceiverHullAxesRequested &&
            left.precomputedReceiverHullAxesEnabled ==
                right.precomputedReceiverHullAxesEnabled &&
            left.sharedCasterLightProjectionRequested ==
                right.sharedCasterLightProjectionRequested &&
            left.sharedCasterLightProjectionEnabled ==
                right.sharedCasterLightProjectionEnabled &&
            left.directCasterSubmissionRequested ==
                right.directCasterSubmissionRequested &&
            left.directCasterSubmissionEnabled ==
                right.directCasterSubmissionEnabled &&
            left.cachedShadowDrawListsRequested ==
                right.cachedShadowDrawListsRequested &&
            left.cachedShadowDrawListsActive ==
                right.cachedShadowDrawListsActive &&
            left.batchedFullRedrawClearRequested ==
                right.batchedFullRedrawClearRequested &&
            left.batchedFullRedrawClearActive ==
                right.batchedFullRedrawClearActive &&
            left.receiverRasterScissorRequested ==
                right.receiverRasterScissorRequested &&
            left.receiverRasterScissorEnabled ==
                right.receiverRasterScissorEnabled &&
            left.casterRadiusThreshold == right.casterRadiusThreshold &&
            left.light == right.light &&
            left.filter == right.filter &&
            left.cascadeActions == right.cascadeActions;
    }

    [[nodiscard]] constexpr bool
        AreDiagnosticCsmBenchmarkSourceFramesConsecutive(
            uint64_t previous,
            uint64_t current)
    {
        return previous != std::numeric_limits<uint64_t>::max() &&
            current == previous + 1u;
    }

    [[nodiscard]] inline bool
        IsValidDiagnosticCsmBenchmarkIssuedFrameContext(
            bool available,
            float frameIntervalMilliseconds)
    {
        return available &&
            std::isfinite(frameIntervalMilliseconds) &&
            frameIntervalMilliseconds >= 0.f;
    }

    struct DiagnosticCsmBenchmarkEvidence
    {
        std::size_t expectedSampleCount = 0u;
        std::size_t sampleCount = 0u;
        std::size_t missingIssuedFrameContexts = 0u;
        uint64_t sourceFrameGapEvents = 0u;
        uint64_t missingSourceFrames = 0u;
        uint64_t maximumSourceFrameGap = 0u;
        bool executableIdentityValid = false;
        bool timingConfigurationIdentityValid = false;
        bool timingsValid = false;
        bool detailedGpuTimingModeUniform = false;
        bool detailedGpuTimingModeMatchesConfiguration = false;
        bool workIdentityStable = false;
        bool sourceFrameArithmeticValid = false;
        bool sourceFramesStrictlyConsecutive = false;
        bool issuedFrameContextsValid = false;
    };

    [[nodiscard]] constexpr bool
        IsDiagnosticCsmBenchmarkEvidenceAuthoritative(
            const DiagnosticCsmBenchmarkEvidence& evidence)
    {
        return evidence.expectedSampleCount > 0u &&
            evidence.sampleCount == evidence.expectedSampleCount &&
            evidence.missingIssuedFrameContexts == 0u &&
            evidence.sourceFrameGapEvents == 0u &&
            evidence.missingSourceFrames == 0u &&
            evidence.maximumSourceFrameGap ==
                (evidence.sampleCount > 1u ? 1u : 0u) &&
            evidence.executableIdentityValid &&
            evidence.timingConfigurationIdentityValid &&
            evidence.timingsValid &&
            evidence.detailedGpuTimingModeUniform &&
            evidence.detailedGpuTimingModeMatchesConfiguration &&
            evidence.workIdentityStable &&
            evidence.sourceFrameArithmeticValid &&
            evidence.sourceFramesStrictlyConsecutive &&
            evidence.issuedFrameContextsValid;
    }

    [[nodiscard]] constexpr bool
        IsDiagnosticCsmBenchmarkRawAuthoritative(
            std::string_view state,
            const DiagnosticCsmBenchmarkEvidence& evidence)
    {
        return state == "complete" &&
            IsDiagnosticCsmBenchmarkEvidenceAuthoritative(evidence);
    }

    [[nodiscard]] inline std::string
        BuildDiagnosticCsmTimingConfigurationIdentity(
            const DiagnosticCascadedShadowMapSettings& settings)
    {
        std::ostringstream identity;
        identity.imbue(std::locale::classic());
        identity << std::setprecision(
            std::numeric_limits<float>::max_digits10)
            << "diagnostic-csm-timing-v2"
            << ";cascadeCount=" << settings.cascadeCount
            << ";shadowMapResolution=" << settings.shadowMapResolution
            << ";maximumShadowDistance="
            << settings.maximumShadowDistance
            << ";maximumLightDepth=" << settings.maximumLightDepth
            << ";cascadeDistributionExponent="
            << settings.cascadeDistributionExponent
            << ";cascadeTransitionFraction="
            << settings.cascadeTransitionFraction
            << ";shadowDistanceFadeoutFraction="
            << settings.shadowDistanceFadeoutFraction
            << ";projectionSnapTexelMultiple="
            << settings.projectionSnapTexelMultiple
            << ";enforceUeMinimumLightDepth="
            << uint32_t(settings.enforceUeMinimumLightDepth)
            << ";depthBias=" << settings.depthBias
            << ";slopeScaledDepthBias=" << settings.slopeScaledDepthBias
            << ";directionalLightShadowBias="
            << settings.directionalLightShadowBias
            << ";directionalLightShadowSlopeBias="
            << settings.directionalLightShadowSlopeBias
            << ";receiverDepthBias=" << settings.receiverDepthBias
            << ";filter=" << uint32_t(settings.filter)
            << ";poissonTapCount=" << settings.poissonTapCount
            << ";filterRadiusTexels=" << settings.filterRadiusTexels
            << ";use16BitDepthEnabled="
            << uint32_t(settings.use16BitDepthEnabled)
            << ";opaqueDepthStateMergingEnabled="
            << uint32_t(settings.opaqueDepthStateMergingEnabled)
            << ";positionOnlyOpaqueEnabled="
            << uint32_t(settings.positionOnlyOpaqueEnabled)
            << ";translationOnlyCasterTransformEnabled="
            << uint32_t(
                settings.translationOnlyCasterTransformEnabled)
            << ";inputAssemblerCasterFetchEnabled="
            << uint32_t(settings.inputAssemblerCasterFetchEnabled)
            << ";precomputedDepthAxisInverseLengthEnabled="
            << uint32_t(
                settings.precomputedDepthAxisInverseLengthEnabled)
            << ";conservativeSaturatedSlopeEnabled="
            << uint32_t(settings.conservativeSaturatedSlopeEnabled)
            << ";algebraicSlowSlopeEnabled="
            << uint32_t(settings.algebraicSlowSlopeEnabled)
            << ";preNormalizedReceiverLightDirectionEnabled="
            << uint32_t(
                settings.preNormalizedReceiverLightDirectionEnabled)
            << ";precomposedClipToShadowEnabled="
            << uint32_t(settings.precomposedClipToShadowEnabled)
            << ";accurateCasterCullingEnabled="
            << uint32_t(settings.accurateCasterCullingEnabled)
            << ";ueCasterRadiusThresholdEnabled="
            << uint32_t(settings.ueCasterRadiusThresholdEnabled)
            << ";casterRadiusThreshold="
            << settings.casterRadiusThreshold
            << ";singleTraversalCasterClassificationEnabled="
            << uint32_t(
                settings.singleTraversalCasterClassificationEnabled)
            << ";precomputedReceiverHullAxesEnabled="
            << uint32_t(settings.precomputedReceiverHullAxesEnabled)
            << ";sharedCasterLightProjectionEnabled="
            << uint32_t(settings.sharedCasterLightProjectionEnabled)
            << ";directCasterSubmissionEnabled="
            << uint32_t(settings.directCasterSubmissionEnabled)
            << ";cachedShadowDrawListsEnabled="
            << uint32_t(settings.cachedShadowDrawListsEnabled)
            << ";batchedFullRedrawClearEnabled="
            << uint32_t(settings.batchedFullRedrawClearEnabled)
            << ";receiverRasterScissorEnabled="
            << uint32_t(settings.receiverRasterScissorEnabled)
            << ";wholeMapReuseEnabled="
            << uint32_t(settings.wholeMapReuseEnabled)
            << ";wholeCascadeReuseEnabled="
            << uint32_t(settings.wholeCascadeReuseEnabled)
            << ";dirtyRectanglesEnabled="
            << uint32_t(settings.dirtyRectanglesEnabled)
            << ";scrollingEnabled="
            << uint32_t(settings.scrollingEnabled)
            << ";minimumScrollOverlap=" << settings.minimumScrollOverlap
            << ";detailedGpuTimingEnabled="
            << uint32_t(settings.detailedGpuTimingEnabled)
            << ";debugView=" << uint32_t(settings.debugView);
        return identity.str();
    }

    [[nodiscard]] inline uint64_t
        HashDiagnosticCsmTimingConfigurationIdentity(
            std::string_view identity)
    {
        uint64_t hash = 14695981039346656037ull;
        for (const unsigned char value : identity)
        {
            hash ^= uint64_t(value);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    [[nodiscard]] inline std::string
        BuildDiagnosticCsmTimingConfigurationId(
            const DiagnosticCascadedShadowMapSettings& settings)
    {
        const std::string identity =
            BuildDiagnosticCsmTimingConfigurationIdentity(settings);
        std::ostringstream result;
        result.imbue(std::locale::classic());
        result << std::hex << std::uppercase << std::setfill('0')
            << std::setw(16)
            << HashDiagnosticCsmTimingConfigurationIdentity(identity);
        return result.str();
    }
}
