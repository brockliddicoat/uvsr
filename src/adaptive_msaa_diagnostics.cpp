#include "adaptive_msaa_diagnostics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    using namespace uvsr::adaptive_msaa;

    [[nodiscard]] bool IsValidComparisonMode(
        DiagnosticsComparisonMode mode)
    {
        return static_cast<uint32_t>(mode) <
            static_cast<uint32_t>(DiagnosticsComparisonMode::Count);
    }

    [[nodiscard]] bool IsValidDebugView(AdaptiveMsaaDebugView view)
    {
        return static_cast<uint32_t>(view) <
            static_cast<uint32_t>(AdaptiveMsaaDebugView::Count);
    }

    [[nodiscard]] bool IsValidDiagnosticsPass(
        AdaptiveMsaaDiagnosticsPass pass)
    {
        return static_cast<uint32_t>(pass) <
            static_cast<uint32_t>(AdaptiveMsaaDiagnosticsPass::Count);
    }

    [[nodiscard]] bool IsValidErrorDomain(DiagnosticsErrorDomain domain)
    {
        return static_cast<uint32_t>(domain) <
            static_cast<uint32_t>(DiagnosticsErrorDomain::Count);
    }

    [[nodiscard]] bool IsValidReconstructionChannel(
        DiagnosticsReconstructionChannel channel)
    {
        return static_cast<uint32_t>(channel) <
            static_cast<uint32_t>(
                DiagnosticsReconstructionChannel::Count);
    }

    [[nodiscard]] bool IsValidErrorValueDomain(
        DiagnosticsErrorValueDomain domain)
    {
        return static_cast<uint32_t>(domain) <
            static_cast<uint32_t>(DiagnosticsErrorValueDomain::Count);
    }

    [[nodiscard]] bool IsValidErrorBudgetRejectionReason(
        DiagnosticsErrorBudgetRejectionReason reason)
    {
        return static_cast<uint32_t>(reason) <
            static_cast<uint32_t>(
                DiagnosticsErrorBudgetRejectionReason::Count);
    }

    [[nodiscard]] bool IsValidFallbackReason(FallbackReason reason)
    {
        return static_cast<uint32_t>(reason) <
            static_cast<uint32_t>(FallbackReason::Count);
    }

    [[nodiscard]] uint64_t SaturatingAdd(
        uint64_t first,
        uint64_t second)
    {
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();
        return first > maximum - second ? maximum : first + second;
    }

    [[nodiscard]] uint64_t SaturatingMultiply(
        uint64_t first,
        uint64_t second)
    {
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();
        if (first == 0u || second == 0u)
            return 0u;
        return first > maximum / second ? maximum : first * second;
    }

    void Accumulate(uint64_t& destination, uint64_t value)
    {
        destination = SaturatingAdd(destination, value);
    }

    enum class FallbackCategory
    {
        None,
        InvalidKey,
        StaleKey,
        MappingFailure
    };

    [[nodiscard]] FallbackCategory ClassifyFallbackReason(
        FallbackReason reason)
    {
        switch (reason)
        {
        case FallbackReason::InvalidSentinel:
        case FallbackReason::InvalidHandleEncoding:
        case FallbackReason::SlotOutOfRange:
        case FallbackReason::PrimitiveOverflow:
            return FallbackCategory::InvalidKey;

        case FallbackReason::GenerationMismatch:
        case FallbackReason::TopologyEpochMismatch:
        case FallbackReason::LodRevisionMismatch:
        case FallbackReason::DeformationRevisionMismatch:
        case FallbackReason::MaterialRevisionMismatch:
        case FallbackReason::TextureContentRevisionMismatch:
        case FallbackReason::TextureQualificationMismatch:
        case FallbackReason::GenerationExhausted:
        case FallbackReason::RegistryResetRequired:
            return FallbackCategory::StaleKey;

        case FallbackReason::TransientMappingCollision:
        case FallbackReason::TransientMappingFailure:
            return FallbackCategory::MappingFailure;

        default:
            return FallbackCategory::None;
        }
    }

    [[nodiscard]] size_t NearestRankIndex(
        size_t sampleCount,
        size_t percentile)
    {
        // ceil(sampleCount * percentile / 100) - 1, rearranged so the
        // multiplication cannot overflow size_t.
        const size_t quotient = sampleCount / 100u;
        const size_t remainder = sampleCount % 100u;
        const size_t remainderRank =
            (remainder * percentile + 99u) / 100u;
        const size_t rank = quotient * percentile + remainderRank;
        return rank == 0u ? 0u : rank - 1u;
    }

    [[nodiscard]] bool IsFinite(const Float3& value)
    {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    [[nodiscard]] DiagnosticsInputStatus ValidateDisplayValue(
        const Float3& value)
    {
        if (!IsFinite(value))
            return DiagnosticsInputStatus::NonFiniteValue;
        if (value.x < 0.f || value.y < 0.f || value.z < 0.f)
            return DiagnosticsInputStatus::NegativeValue;
        if (value.x > 1.f || value.y > 1.f || value.z > 1.f)
            return DiagnosticsInputStatus::ValueOutOfRange;
        return DiagnosticsInputStatus::Success;
    }

    [[nodiscard]] DiagnosticsInputStatus StoreFiniteError(
        double error,
        float& output)
    {
        output = 0.f;
        if (!std::isfinite(error) || error < 0.0 ||
            error > double(std::numeric_limits<float>::max()))
        {
            return DiagnosticsInputStatus::NonFiniteValue;
        }
        output = static_cast<float>(error);
        return DiagnosticsInputStatus::Success;
    }

    [[nodiscard]] DiagnosticsInputStatus CollectActiveErrors(
        const float* errors,
        const uint8_t* activeMask,
        size_t errorCount,
        std::vector<float>& output)
    {
        output.clear();
        if (errorCount == 0u)
            return DiagnosticsInputStatus::EmptyInput;
        if (!errors || !activeMask)
            return DiagnosticsInputStatus::NullInput;

        output.reserve(errorCount);
        for (size_t index = 0u; index < errorCount; ++index)
        {
            if (activeMask[index] > 1u)
                return DiagnosticsInputStatus::InvalidActiveMask;
            const float error = errors[index];
            if (!std::isfinite(error))
                return DiagnosticsInputStatus::NonFiniteValue;
            if (error < 0.f)
                return DiagnosticsInputStatus::NegativeValue;
            if (activeMask[index] != 0u)
                output.push_back(error);
        }

        return output.empty()
            ? DiagnosticsInputStatus::EmptyInput
            : DiagnosticsInputStatus::Success;
    }

    [[nodiscard]] DiagnosticsInputStatus ValidateSelectedSample(
        const SelectedPixelSampleRecord& sample,
        uint32_t expectedSampleIndex,
        uint32_t sampleCount)
    {
        if (sample.sampleIndex != expectedSampleIndex)
            return DiagnosticsInputStatus::InvalidSampleIndex;
        if (!IsValidFallbackReason(sample.fallbackReason))
            return DiagnosticsInputStatus::InvalidFallbackReason;
        if (!std::isfinite(sample.linearDepth) ||
            !std::isfinite(sample.surfaceHit.barycentrics[0]) ||
            !std::isfinite(sample.surfaceHit.barycentrics[1]) ||
            !std::isfinite(sample.weight) ||
            !IsFinite(sample.radiance))
        {
            return DiagnosticsInputStatus::NonFiniteValue;
        }
        if (sample.weight < 0.f)
            return DiagnosticsInputStatus::NegativeValue;
        if (sample.weight > 1.f)
            return DiagnosticsInputStatus::InvalidGroupCount;

        if (!sample.covered)
        {
            if (sample.groupIndex != InvalidDiagnosticsSampleIndex ||
                sample.representativeSampleIndex !=
                    InvalidDiagnosticsSampleIndex ||
                sample.surfaceHitValid ||
                sample.fallbackReason != FallbackReason::None)
            {
                return DiagnosticsInputStatus::InvalidSampleIndex;
            }
            return DiagnosticsInputStatus::Success;
        }

        if (sample.groupIndex >= sampleCount)
            return DiagnosticsInputStatus::InvalidGroupCount;
        if (sample.representativeSampleIndex >= sampleCount)
            return DiagnosticsInputStatus::InvalidSampleIndex;
        if (sample.surfaceHitValid &&
            ValidateGpuVisibilityKey(sample.packedKey) != FallbackReason::None)
        {
            return DiagnosticsInputStatus::InvalidSampleIndex;
        }
        if (!sample.surfaceHitValid &&
            sample.fallbackReason == FallbackReason::None)
        {
            return DiagnosticsInputStatus::InvalidFallbackReason;
        }
        return DiagnosticsInputStatus::Success;
    }
}

namespace uvsr::adaptive_msaa
{
    DiagnosticsInputStatus SanitizeDiagnosticsSettings(
        const AdaptiveMsaaDiagnosticsSettings& input,
        AdaptiveMsaaDiagnosticsSettings& output)
    {
        output = {};
        if (!IsValidComparisonMode(input.comparisonMode))
            return DiagnosticsInputStatus::InvalidComparisonMode;
        if (!IsValidDebugView(input.debugView))
            return DiagnosticsInputStatus::InvalidDebugView;
        if (!IsValidErrorDomain(input.errorDomain))
            return DiagnosticsInputStatus::InvalidErrorDomain;
        if (!IsValidReconstructionChannel(input.reconstructionChannel))
            return DiagnosticsInputStatus::InvalidReconstructionChannel;
        if (!std::isfinite(input.validationAnimationPhase))
            return DiagnosticsInputStatus::NonFiniteValue;
        if (input.validationAnimationPhase < 0.f ||
            input.validationAnimationPhase > 1.f)
        {
            return DiagnosticsInputStatus::ValueOutOfRange;
        }

        if (!input.enabled)
            return DiagnosticsInputStatus::Success;

        output = input;
        return DiagnosticsInputStatus::Success;
    }

    bool RequestsFinalLightingComparison(
        const AdaptiveMsaaDiagnosticsSettings& settings)
    {
        return settings.enabled &&
            settings.dualRenderingEnabled &&
            settings.comparisonMode !=
                DiagnosticsComparisonMode::Disabled &&
            (settings.debugView == AdaptiveMsaaDebugView::Disabled ||
                settings.debugView ==
                    AdaptiveMsaaDebugView::AdaptiveLightingError);
    }

    bool RequestsCompactReconstructionComparison(
        const AdaptiveMsaaDiagnosticsSettings& settings)
    {
        return settings.enabled &&
            settings.dualRenderingEnabled &&
            settings.comparisonMode !=
                DiagnosticsComparisonMode::Disabled &&
            settings.debugView ==
                AdaptiveMsaaDebugView::CompactReconstructionError;
    }

    bool RequestsAnyReferenceComparison(
        const AdaptiveMsaaDiagnosticsSettings& settings)
    {
        return RequestsFinalLightingComparison(settings) ||
            RequestsCompactReconstructionComparison(settings);
    }

    bool IsAcceptedBenchmarkDiagnosticsSafe(
        const AdaptiveMsaaDiagnosticsSettings& settings)
    {
        return !settings.enabled &&
            settings.comparisonMode == DiagnosticsComparisonMode::Disabled &&
            settings.debugView == AdaptiveMsaaDebugView::Disabled &&
            settings.errorDomain == DiagnosticsErrorDomain::Spatial &&
            settings.reconstructionChannel ==
                DiagnosticsReconstructionChannel::Combined &&
            !settings.freezeCameraAndFrame &&
            !settings.validationAnimationEnabled &&
            settings.validationAnimationPhase == 0.f &&
            !settings.selectedPixelCaptureRequested &&
            !settings.readbackEnabled &&
            !settings.countersEnabled &&
            !settings.passTimingsEnabled &&
            !settings.dualRenderingEnabled;
    }

    void AdaptiveMsaaDiagnosticsCounters::Reset()
    {
        m_Data = {};
    }

    DiagnosticsInputStatus AdaptiveMsaaDiagnosticsCounters::RecordGroupCount(
        uint32_t groupCount,
        uint64_t pixelCount)
    {
        if (groupCount > MaximumAdaptiveSampleCount)
            return DiagnosticsInputStatus::InvalidGroupCount;

        Accumulate(m_Data.groupCountHistogram[groupCount], pixelCount);
        Accumulate(m_Data.pixelsObserved, pixelCount);
        return DiagnosticsInputStatus::Success;
    }

    DiagnosticsInputStatus
        AdaptiveMsaaDiagnosticsCounters::RecordLightingEvaluations(
            uint64_t referenceEvaluations,
            uint64_t performedEvaluations)
    {
        if (performedEvaluations > referenceEvaluations)
        {
            return DiagnosticsInputStatus::
                InvalidLightingEvaluationCount;
        }

        Accumulate(
            m_Data.referenceLightingEvaluations,
            referenceEvaluations);
        Accumulate(m_Data.lightingEvaluations, performedEvaluations);
        Accumulate(
            m_Data.lightingEvaluationsSaved,
            referenceEvaluations - performedEvaluations);
        return DiagnosticsInputStatus::Success;
    }

    DiagnosticsInputStatus
        AdaptiveMsaaDiagnosticsCounters::RecordFallbackReason(
            FallbackReason reason,
            uint64_t occurrenceCount)
    {
        if (!IsValidFallbackReason(reason) ||
            reason == FallbackReason::None)
        {
            return DiagnosticsInputStatus::InvalidFallbackReason;
        }

        Accumulate(
            m_Data.fallbackReasonCounts[static_cast<size_t>(reason)],
            occurrenceCount);
        switch (ClassifyFallbackReason(reason))
        {
        case FallbackCategory::InvalidKey:
            Accumulate(m_Data.invalidKeyCount, occurrenceCount);
            break;
        case FallbackCategory::StaleKey:
            Accumulate(m_Data.staleKeyCount, occurrenceCount);
            break;
        case FallbackCategory::MappingFailure:
            Accumulate(m_Data.mappingFailureCount, occurrenceCount);
            break;
        default:
            break;
        }
        return DiagnosticsInputStatus::Success;
    }

    DiagnosticsInputStatus
        AdaptiveMsaaDiagnosticsCounters::RecordErrorBudgetRejection(
            DiagnosticsErrorBudgetRejectionReason reason,
            uint32_t groupSampleCount,
            uint64_t occurrenceCount)
    {
        if (!IsValidErrorBudgetRejectionReason(reason) ||
            reason == DiagnosticsErrorBudgetRejectionReason::None)
        {
            return DiagnosticsInputStatus::
                InvalidErrorBudgetRejectionReason;
        }
        if (groupSampleCount == 0u ||
            groupSampleCount > MaximumAdaptiveSampleCount)
        {
            return DiagnosticsInputStatus::InvalidGroupCount;
        }

        Accumulate(
            m_Data.errorBudgetRejectionReasonCounts[
                static_cast<size_t>(reason)],
            occurrenceCount);
        Accumulate(
            m_Data.errorBudgetRejectedGroupCount,
            occurrenceCount);
        Accumulate(
            m_Data.errorBudgetRejectedSampleCount,
            SaturatingMultiply(groupSampleCount, occurrenceCount));
        return DiagnosticsInputStatus::Success;
    }

    DiagnosticsInputStatus
        AdaptiveMsaaDiagnosticsCounters::RecordWorkListObservation(
            uint64_t occupancy,
            uint64_t capacity,
            bool overflowed,
            uint64_t observationCount)
    {
        if (!overflowed && occupancy > capacity)
            return DiagnosticsInputStatus::InvalidWorkListObservation;

        Accumulate(m_Data.workListObservationCount, observationCount);
        Accumulate(
            m_Data.workListOccupancyTotal,
            SaturatingMultiply(occupancy, observationCount));
        Accumulate(
            m_Data.workListCapacityTotal,
            SaturatingMultiply(capacity, observationCount));
        m_Data.workListPeakOccupancy = std::max(
            m_Data.workListPeakOccupancy,
            occupancy);
        if (overflowed)
            Accumulate(m_Data.workListOverflowCount, observationCount);
        return DiagnosticsInputStatus::Success;
    }

    DiagnosticsInputStatus
        AdaptiveMsaaDiagnosticsCounters::RecordPassTiming(
            AdaptiveMsaaDiagnosticsPass pass,
            uint64_t gpuNanoseconds,
            uint64_t invocationCount)
    {
        if (!IsValidDiagnosticsPass(pass))
            return DiagnosticsInputStatus::InvalidPass;

        const size_t passIndex = static_cast<size_t>(pass);
        Accumulate(
            m_Data.passGpuNanoseconds[passIndex],
            gpuNanoseconds);
        Accumulate(
            m_Data.passInvocationCounts[passIndex],
            invocationCount);
        return DiagnosticsInputStatus::Success;
    }

    const AdaptiveMsaaDiagnosticsCountersData&
        AdaptiveMsaaDiagnosticsCounters::Data() const
    {
        return m_Data;
    }

    DiagnosticsInputStatus ComputeDiagnosticsHdrSpatialError(
        const Float3& adaptive,
        const Float3& reference,
        float& output)
    {
        output = 0.f;
        if (!IsFinite(adaptive) || !IsFinite(reference))
            return DiagnosticsInputStatus::NonFiniteValue;

        const float adaptiveChannels[3] = {
            adaptive.x, adaptive.y, adaptive.z };
        const float referenceChannels[3] = {
            reference.x, reference.y, reference.z };
        double maximumError = 0.0;
        for (size_t channel = 0u; channel < 3u; ++channel)
        {
            const double adaptiveValue = double(adaptiveChannels[channel]);
            const double referenceValue = double(referenceChannels[channel]);
            const double denominator = std::max(
                double(DiagnosticsHdrDenominatorFloor),
                std::max(
                    std::abs(adaptiveValue),
                    std::abs(referenceValue)));
            maximumError = std::max(
                maximumError,
                std::abs(adaptiveValue - referenceValue) / denominator);
        }
        return StoreFiniteError(maximumError, output);
    }

    DiagnosticsInputStatus ComputeDiagnosticsHdrTemporalError(
        const Float3& adaptiveCurrent,
        const Float3& referenceCurrent,
        const Float3& adaptivePrevious,
        const Float3& referencePrevious,
        float& output)
    {
        output = 0.f;
        if (!IsFinite(adaptiveCurrent) || !IsFinite(referenceCurrent) ||
            !IsFinite(adaptivePrevious) || !IsFinite(referencePrevious))
        {
            return DiagnosticsInputStatus::NonFiniteValue;
        }

        const float adaptiveCurrentChannels[3] = {
            adaptiveCurrent.x, adaptiveCurrent.y, adaptiveCurrent.z };
        const float referenceCurrentChannels[3] = {
            referenceCurrent.x, referenceCurrent.y, referenceCurrent.z };
        const float adaptivePreviousChannels[3] = {
            adaptivePrevious.x, adaptivePrevious.y, adaptivePrevious.z };
        const float referencePreviousChannels[3] = {
            referencePrevious.x, referencePrevious.y, referencePrevious.z };
        double maximumError = 0.0;
        for (size_t channel = 0u; channel < 3u; ++channel)
        {
            const double adaptiveNow =
                double(adaptiveCurrentChannels[channel]);
            const double referenceNow =
                double(referenceCurrentChannels[channel]);
            const double adaptiveBefore =
                double(adaptivePreviousChannels[channel]);
            const double referenceBefore =
                double(referencePreviousChannels[channel]);
            const double denominator = std::max(
                double(DiagnosticsHdrDenominatorFloor),
                std::max(
                    std::max(std::abs(adaptiveNow), std::abs(referenceNow)),
                    std::max(
                        std::abs(adaptiveBefore),
                        std::abs(referenceBefore))));
            const double residualNow = adaptiveNow - referenceNow;
            const double residualBefore = adaptiveBefore - referenceBefore;
            maximumError = std::max(
                maximumError,
                std::abs(residualNow - residualBefore) / denominator);
        }
        return StoreFiniteError(maximumError, output);
    }

    DiagnosticsInputStatus ComputeDiagnosticsDisplaySpatialError(
        const Float3& adaptiveDisplay,
        const Float3& referenceDisplay,
        float& output)
    {
        output = 0.f;
        DiagnosticsInputStatus status =
            ValidateDisplayValue(adaptiveDisplay);
        if (status != DiagnosticsInputStatus::Success)
            return status;
        status = ValidateDisplayValue(referenceDisplay);
        if (status != DiagnosticsInputStatus::Success)
            return status;

        const double maximumError = std::max(
            std::abs(double(adaptiveDisplay.x) - referenceDisplay.x),
            std::max(
                std::abs(double(adaptiveDisplay.y) - referenceDisplay.y),
                std::abs(double(adaptiveDisplay.z) - referenceDisplay.z))) *
            double(DiagnosticsDisplayCodeScale);
        return StoreFiniteError(maximumError, output);
    }

    DiagnosticsInputStatus ComputeDiagnosticsDisplayTemporalError(
        const Float3& adaptiveDisplayCurrent,
        const Float3& referenceDisplayCurrent,
        const Float3& adaptiveDisplayPrevious,
        const Float3& referenceDisplayPrevious,
        float& output)
    {
        output = 0.f;
        const Float3 values[4] = {
            adaptiveDisplayCurrent,
            referenceDisplayCurrent,
            adaptiveDisplayPrevious,
            referenceDisplayPrevious };
        for (const Float3& value : values)
        {
            const DiagnosticsInputStatus status =
                ValidateDisplayValue(value);
            if (status != DiagnosticsInputStatus::Success)
                return status;
        }

        const float adaptiveCurrentChannels[3] = {
            adaptiveDisplayCurrent.x,
            adaptiveDisplayCurrent.y,
            adaptiveDisplayCurrent.z };
        const float referenceCurrentChannels[3] = {
            referenceDisplayCurrent.x,
            referenceDisplayCurrent.y,
            referenceDisplayCurrent.z };
        const float adaptivePreviousChannels[3] = {
            adaptiveDisplayPrevious.x,
            adaptiveDisplayPrevious.y,
            adaptiveDisplayPrevious.z };
        const float referencePreviousChannels[3] = {
            referenceDisplayPrevious.x,
            referenceDisplayPrevious.y,
            referenceDisplayPrevious.z };
        double maximumError = 0.0;
        for (size_t channel = 0u; channel < 3u; ++channel)
        {
            const double residualNow =
                double(adaptiveCurrentChannels[channel]) -
                double(referenceCurrentChannels[channel]);
            const double residualBefore =
                double(adaptivePreviousChannels[channel]) -
                double(referencePreviousChannels[channel]);
            maximumError = std::max(
                maximumError,
                std::abs(residualNow - residualBefore));
        }
        return StoreFiniteError(
            maximumError * double(DiagnosticsDisplayCodeScale),
            output);
    }

    DiagnosticsInputStatus GetDiagnosticsErrorLimits(
        DiagnosticsErrorValueDomain valueDomain,
        DiagnosticsErrorLimits& output)
    {
        output = {};
        if (!IsValidErrorValueDomain(valueDomain))
            return DiagnosticsInputStatus::InvalidErrorValueDomain;

        if (valueDomain == DiagnosticsErrorValueDomain::Hdr)
        {
            output.percentile99 = DiagnosticsHdrPercentile99Limit;
            output.maximum = DiagnosticsHdrMaximumLimit;
        }
        else
        {
            output.percentile99 = DiagnosticsDisplayPercentile99Limit;
            output.maximum = DiagnosticsDisplayMaximumLimit;
        }
        return DiagnosticsInputStatus::Success;
    }

    DiagnosticsInputStatus SummarizeDiagnosticsErrors(
        const float* errors,
        size_t errorCount,
        DiagnosticsErrorSummary& output)
    {
        output = {};
        if (errorCount == 0u)
            return DiagnosticsInputStatus::EmptyInput;
        if (!errors)
            return DiagnosticsInputStatus::NullInput;

        std::vector<float> sortedErrors;
        sortedErrors.reserve(errorCount);
        for (size_t index = 0u; index < errorCount; ++index)
        {
            const float error = errors[index];
            if (!std::isfinite(error))
                return DiagnosticsInputStatus::NonFiniteValue;
            if (error < 0.f)
                return DiagnosticsInputStatus::NegativeValue;
            sortedErrors.push_back(error);
        }
        std::sort(sortedErrors.begin(), sortedErrors.end());

        output.valid = true;
        output.sampleCount = static_cast<uint64_t>(errorCount);
        output.percentile50 = sortedErrors[NearestRankIndex(errorCount, 50u)];
        output.percentile90 = sortedErrors[NearestRankIndex(errorCount, 90u)];
        output.percentile95 = sortedErrors[NearestRankIndex(errorCount, 95u)];
        output.percentile99 = sortedErrors[NearestRankIndex(errorCount, 99u)];
        output.maximum = sortedErrors.back();
        return DiagnosticsInputStatus::Success;
    }

    DiagnosticsInputStatus SummarizeActiveDiagnosticsErrors(
        const float* errors,
        const uint8_t* activeMask,
        size_t errorCount,
        DiagnosticsErrorSummary& output)
    {
        output = {};
        std::vector<float> activeErrors;
        const DiagnosticsInputStatus status = CollectActiveErrors(
            errors,
            activeMask,
            errorCount,
            activeErrors);
        if (status != DiagnosticsInputStatus::Success)
            return status;
        return SummarizeDiagnosticsErrors(
            activeErrors.data(),
            activeErrors.size(),
            output);
    }

    DiagnosticsInputStatus SetDiagnosticsErrorSummary(
        AdaptiveMsaaErrorSummaries& summaries,
        DiagnosticsErrorValueDomain valueDomain,
        DiagnosticsErrorDomain domain,
        const float* errors,
        size_t errorCount)
    {
        if (!IsValidErrorValueDomain(valueDomain))
            return DiagnosticsInputStatus::InvalidErrorValueDomain;
        if (!IsValidErrorDomain(domain))
            return DiagnosticsInputStatus::InvalidErrorDomain;

        DiagnosticsErrorSummary summary;
        const DiagnosticsInputStatus status = SummarizeDiagnosticsErrors(
            errors,
            errorCount,
            summary);

        DiagnosticsErrorDomainSummaries& valueSummaries =
            valueDomain == DiagnosticsErrorValueDomain::Hdr
            ? summaries.hdr
            : summaries.display;
        DiagnosticsErrorSummary& destination =
            domain == DiagnosticsErrorDomain::Spatial
            ? valueSummaries.spatial
            : valueSummaries.temporal;
        destination = status == DiagnosticsInputStatus::Success
            ? summary
            : DiagnosticsErrorSummary{};
        return status;
    }

    DiagnosticsInputStatus EvaluateDiagnosticsErrorFrames(
        DiagnosticsErrorValueDomain valueDomain,
        DiagnosticsErrorDomain errorDomain,
        const DiagnosticsErrorFrameView* frames,
        size_t frameCount,
        DiagnosticsErrorGateResult& output)
    {
        output = {};
        output.firstFailedFrameIndex = InvalidDiagnosticsFrameIndex;
        if (!IsValidErrorValueDomain(valueDomain))
            return DiagnosticsInputStatus::InvalidErrorValueDomain;
        if (!IsValidErrorDomain(errorDomain))
            return DiagnosticsInputStatus::InvalidErrorDomain;
        if (frameCount == 0u)
            return DiagnosticsInputStatus::EmptyInput;
        if (!frames)
            return DiagnosticsInputStatus::NullInput;

        DiagnosticsErrorLimits limits;
        const DiagnosticsInputStatus limitStatus =
            GetDiagnosticsErrorLimits(valueDomain, limits);
        if (limitStatus != DiagnosticsInputStatus::Success)
            return limitStatus;

        std::vector<float> pooledErrors;
        std::vector<float> frameErrors;
        uint64_t failedFrameCount = 0u;
        uint64_t firstFailedFrameIndex = InvalidDiagnosticsFrameIndex;
        float worstFramePercentile99 = 0.f;
        float worstFrameMaximum = 0.f;

        for (size_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
        {
            const DiagnosticsErrorFrameView& frame = frames[frameIndex];
            const DiagnosticsInputStatus collectStatus = CollectActiveErrors(
                frame.errors,
                frame.activeMask,
                frame.valueCount,
                frameErrors);
            if (collectStatus != DiagnosticsInputStatus::Success)
                return collectStatus;

            DiagnosticsErrorSummary frameSummary;
            const DiagnosticsInputStatus summaryStatus =
                SummarizeDiagnosticsErrors(
                    frameErrors.data(),
                    frameErrors.size(),
                    frameSummary);
            if (summaryStatus != DiagnosticsInputStatus::Success)
                return summaryStatus;

            worstFramePercentile99 = std::max(
                worstFramePercentile99,
                frameSummary.percentile99);
            worstFrameMaximum = std::max(
                worstFrameMaximum,
                frameSummary.maximum);
            if (frameSummary.percentile99 > limits.percentile99 ||
                frameSummary.maximum > limits.maximum)
            {
                ++failedFrameCount;
                if (firstFailedFrameIndex == InvalidDiagnosticsFrameIndex)
                {
                    firstFailedFrameIndex =
                        static_cast<uint64_t>(frameIndex);
                }
            }
            pooledErrors.insert(
                pooledErrors.end(),
                frameErrors.begin(),
                frameErrors.end());
        }

        DiagnosticsErrorSummary pooledSummary;
        const DiagnosticsInputStatus pooledStatus = SummarizeDiagnosticsErrors(
            pooledErrors.data(),
            pooledErrors.size(),
            pooledSummary);
        if (pooledStatus != DiagnosticsInputStatus::Success)
            return pooledStatus;

        const bool pooledPassed =
            pooledSummary.percentile99 <= limits.percentile99 &&
            pooledSummary.maximum <= limits.maximum;
        output.valid = true;
        output.perFramePassed = failedFrameCount == 0u;
        output.pooledPassed = pooledPassed;
        output.passed = output.perFramePassed && output.pooledPassed;
        output.valueDomain = valueDomain;
        output.errorDomain = errorDomain;
        output.frameCount = static_cast<uint64_t>(frameCount);
        output.activeSampleCount =
            static_cast<uint64_t>(pooledErrors.size());
        output.failedFrameCount = failedFrameCount;
        output.firstFailedFrameIndex = firstFailedFrameIndex;
        output.worstFramePercentile99 = worstFramePercentile99;
        output.worstFrameMaximum = worstFrameMaximum;
        output.pooled = pooledSummary;
        return DiagnosticsInputStatus::Success;
    }

    DiagnosticsInputStatus DecideDiagnosticsGroupRejection(
        const float* hdrMemberErrors,
        const float* displayMemberErrors,
        uint32_t memberCount,
        DiagnosticsGroupRejectionDecision& output)
    {
        output = {};
        if (memberCount == 0u || memberCount > MaximumAdaptiveSampleCount)
            return DiagnosticsInputStatus::InvalidGroupCount;
        if (!hdrMemberErrors || !displayMemberErrors)
            return DiagnosticsInputStatus::NullInput;

        uint32_t firstNonFiniteMember = InvalidDiagnosticsSampleIndex;
        for (uint32_t memberIndex = 0u;
            memberIndex < memberCount;
            ++memberIndex)
        {
            const float hdrError = hdrMemberErrors[memberIndex];
            const float displayError = displayMemberErrors[memberIndex];
            if ((std::isfinite(hdrError) && hdrError < 0.f) ||
                (std::isfinite(displayError) && displayError < 0.f))
            {
                return DiagnosticsInputStatus::NegativeValue;
            }
            if ((!std::isfinite(hdrError) ||
                    !std::isfinite(displayError)) &&
                firstNonFiniteMember == InvalidDiagnosticsSampleIndex)
            {
                firstNonFiniteMember = memberIndex;
            }
        }

        output.valid = true;
        output.memberCount = memberCount;
        if (firstNonFiniteMember != InvalidDiagnosticsSampleIndex)
        {
            output.rejectWholeGroup = true;
            output.reason =
                DiagnosticsErrorBudgetRejectionReason::NonFiniteValue;
            output.firstFailingMember = firstNonFiniteMember;
            return DiagnosticsInputStatus::Success;
        }

        for (uint32_t memberIndex = 0u;
            memberIndex < memberCount;
            ++memberIndex)
        {
            if (hdrMemberErrors[memberIndex] >
                DiagnosticsHdrPercentile99Limit)
            {
                output.rejectWholeGroup = true;
                output.reason =
                    DiagnosticsErrorBudgetRejectionReason::HdrLimitExceeded;
                output.firstFailingMember = memberIndex;
                return DiagnosticsInputStatus::Success;
            }
        }
        for (uint32_t memberIndex = 0u;
            memberIndex < memberCount;
            ++memberIndex)
        {
            if (displayMemberErrors[memberIndex] >
                DiagnosticsDisplayPercentile99Limit)
            {
                output.rejectWholeGroup = true;
                output.reason =
                    DiagnosticsErrorBudgetRejectionReason::DisplayLimitExceeded;
                output.firstFailingMember = memberIndex;
                return DiagnosticsInputStatus::Success;
            }
        }
        return DiagnosticsInputStatus::Success;
    }

    DiagnosticsInputStatus DecideDiagnosticsHdrGroupRejection(
        const float* hdrMemberErrors,
        uint32_t memberCount,
        DiagnosticsGroupRejectionDecision& output)
    {
        output = {};
        if (memberCount == 0u || memberCount > MaximumAdaptiveSampleCount)
            return DiagnosticsInputStatus::InvalidGroupCount;
        if (!hdrMemberErrors)
            return DiagnosticsInputStatus::NullInput;

        output.valid = true;
        output.memberCount = memberCount;
        uint32_t firstNonFiniteMember = InvalidDiagnosticsSampleIndex;
        for (uint32_t memberIndex = 0u;
            memberIndex < memberCount;
            ++memberIndex)
        {
            const float error = hdrMemberErrors[memberIndex];
            if (std::isfinite(error) && error < 0.f)
            {
                output = {};
                return DiagnosticsInputStatus::NegativeValue;
            }
            if (!std::isfinite(error) &&
                firstNonFiniteMember == InvalidDiagnosticsSampleIndex)
            {
                firstNonFiniteMember = memberIndex;
            }
        }
        if (firstNonFiniteMember != InvalidDiagnosticsSampleIndex)
        {
            output.rejectWholeGroup = true;
            output.reason =
                DiagnosticsErrorBudgetRejectionReason::NonFiniteValue;
            output.firstFailingMember = firstNonFiniteMember;
            return DiagnosticsInputStatus::Success;
        }

        for (uint32_t memberIndex = 0u;
            memberIndex < memberCount;
            ++memberIndex)
        {
            if (hdrMemberErrors[memberIndex] >
                DiagnosticsHdrGroupLimit)
            {
                output.rejectWholeGroup = true;
                output.reason =
                    DiagnosticsErrorBudgetRejectionReason::HdrLimitExceeded;
                output.firstFailingMember = memberIndex;
                return DiagnosticsInputStatus::Success;
            }
        }
        return DiagnosticsInputStatus::Success;
    }

    void ResetDiagnosticsErrorSummaries(
        AdaptiveMsaaErrorSummaries& summaries)
    {
        summaries = {};
    }

    DiagnosticsInputStatus StoreSelectedPixelRecord(
        SelectedPixelRecord& output,
        uint64_t captureId,
        uint64_t frameIndex,
        uint32_t pixelX,
        uint32_t pixelY,
        const SelectedPixelSampleRecord* samples,
        uint32_t sampleCount)
    {
        output = {};
        if (!IsSupportedSampleCount(sampleCount))
            return DiagnosticsInputStatus::UnsupportedSampleCount;
        if (!samples)
            return DiagnosticsInputStatus::NullInput;

        SelectedPixelRecord candidate;
        candidate.captureId = captureId;
        candidate.frameIndex = frameIndex;
        candidate.pixelX = pixelX;
        candidate.pixelY = pixelY;
        candidate.sampleCount = sampleCount;
        for (uint32_t sampleIndex = 0u;
            sampleIndex < sampleCount;
            ++sampleIndex)
        {
            const DiagnosticsInputStatus status = ValidateSelectedSample(
                samples[sampleIndex],
                sampleIndex,
                sampleCount);
            if (status != DiagnosticsInputStatus::Success)
                return status;
            candidate.samples[sampleIndex] = samples[sampleIndex];
        }

        // Validate the cross-record topology only after every bounded record
        // has passed individual checks. A representative must be covered,
        // belong to the same group, and identify itself as representative.
        for (uint32_t sampleIndex = 0u;
            sampleIndex < sampleCount;
            ++sampleIndex)
        {
            const SelectedPixelSampleRecord& sample =
                candidate.samples[sampleIndex];
            if (!sample.covered)
                continue;

            const SelectedPixelSampleRecord& representative =
                candidate.samples[sample.representativeSampleIndex];
            if (!representative.covered ||
                representative.groupIndex != sample.groupIndex ||
                representative.representativeSampleIndex !=
                    representative.sampleIndex)
            {
                return DiagnosticsInputStatus::InvalidSampleIndex;
            }
        }

        candidate.valid = true;
        output = candidate;
        return DiagnosticsInputStatus::Success;
    }

    void ResetSelectedPixelRecord(SelectedPixelRecord& record)
    {
        record = {};
    }
}
