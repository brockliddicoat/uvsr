#include "visibility_benchmark_statistics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace uvsr
{
    namespace
    {
        VisibilityBenchmarkDistributionSummary BuildDistribution(
            std::vector<double> values)
        {
            VisibilityBenchmarkDistributionSummary result;
            if (values.empty())
                return result;

            std::sort(values.begin(), values.end());

            const auto percentile = [&values](double probability)
            {
                const double scaledIndex = probability *
                    static_cast<double>(values.size() - 1u);
                const size_t lowerIndex =
                    static_cast<size_t>(std::floor(scaledIndex));
                const size_t upperIndex =
                    std::min(lowerIndex + 1u, values.size() - 1u);
                const double interpolation =
                    scaledIndex - static_cast<double>(lowerIndex);
                return values[lowerIndex] +
                    (values[upperIndex] - values[lowerIndex]) * interpolation;
            };

            result.sampleCount = static_cast<uint32_t>(values.size());
            result.medianMilliseconds = percentile(0.5);
            result.p95Milliseconds = percentile(0.95);
            result.valid = true;
            return result;
        }
    }

    const char* VisibilityBenchmarkStageKey(VisibilityBenchmarkStage stage)
    {
        switch (stage)
        {
        case VisibilityBenchmarkStage::DepthPreparation:
            return "depth_preparation";
        case VisibilityBenchmarkStage::FirstTrace:
            return "first_trace";
        case VisibilityBenchmarkStage::LaterTrace:
            return "later_trace";
        case VisibilityBenchmarkStage::LaterTraceBounce2:
            return "later_trace_bounce_2";
        case VisibilityBenchmarkStage::LaterTraceBounce3:
            return "later_trace_bounce_3";
        case VisibilityBenchmarkStage::LaterTraceBounce4:
            return "later_trace_bounce_4";
        case VisibilityBenchmarkStage::SpatialDenoise:
            return "spatial_denoise";
        case VisibilityBenchmarkStage::Temporal:
            return "temporal";
        case VisibilityBenchmarkStage::FusedSpatialDenoiseUpsample:
            return "fused_spatial_denoise_upsample";
        case VisibilityBenchmarkStage::RequiredUpsample:
            return "required_upsample";
        case VisibilityBenchmarkStage::FullResolutionApply:
            return "full_resolution_apply";
        case VisibilityBenchmarkStage::Composition:
            return "composition";
        case VisibilityBenchmarkStage::EffectEnvelope:
            return "effect_envelope";
        default:
            return "invalid";
        }
    }

    VisibilityBenchmarkStatistics::VisibilityBenchmarkStatistics()
    {
        const VisibilityBenchmarkRunConfiguration configuration;
        (void)Reset(configuration);
    }

    bool VisibilityBenchmarkStatistics::IsValidConfiguration(
        const VisibilityBenchmarkRunConfiguration& configuration)
    {
        if (configuration.measuredFrameCount == 0u ||
            configuration.measuredFrameCount >
                VisibilityBenchmarkMaximumMeasuredFrameCount ||
            configuration.requiredStageMask == 0u ||
            (configuration.requiredStageMask &
                ~VisibilityBenchmarkAllStageMask) != 0u ||
            (configuration.summedStageMask &
                ~VisibilityBenchmarkAllStageMask) != 0u ||
            (configuration.producerStageMask &
                ~VisibilityBenchmarkAllStageMask) != 0u)
        {
            return false;
        }

        const uint64_t maximumFrameId =
            std::numeric_limits<uint64_t>::max();
        if (uint64_t(configuration.warmupFrameCount) >
            maximumFrameId - configuration.firstFrameId)
        {
            return false;
        }

        const uint64_t firstMeasuredFrameId =
            configuration.firstFrameId + configuration.warmupFrameCount;
        return uint64_t(configuration.measuredFrameCount - 1u) <=
            maximumFrameId - firstMeasuredFrameId;
    }

    bool VisibilityBenchmarkStatistics::Reset(
        const VisibilityBenchmarkRunConfiguration& configuration)
    {
        if (!IsValidConfiguration(configuration))
            return false;

        std::vector<FrameSamples> frames;
        try
        {
            frames.resize(configuration.measuredFrameCount);
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        m_Configuration = configuration;
        m_Configuration.summedStageMask &=
            m_Configuration.requiredStageMask;
        m_Configuration.producerStageMask &=
            m_Configuration.requiredStageMask;
        m_FirstMeasuredFrameId = configuration.firstFrameId +
            configuration.warmupFrameCount;
        m_Frames = std::move(frames);
        m_Ingestion = {};
        m_CompleteFrameCount = 0u;
        return true;
    }

    void VisibilityBenchmarkStatistics::Reset()
    {
        std::fill(m_Frames.begin(), m_Frames.end(), FrameSamples{});
        m_Ingestion = {};
        m_CompleteFrameCount = 0u;
    }

    VisibilityBenchmarkSampleResult VisibilityBenchmarkStatistics::AddSample(
        uint64_t frameId,
        VisibilityBenchmarkStage stage,
        double milliseconds)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        if (stageIndex >=
            static_cast<uint32_t>(VisibilityBenchmarkStage::Count))
        {
            ++m_Ingestion.invalidStageSampleCount;
            return VisibilityBenchmarkSampleResult::InvalidStage;
        }

        if (!std::isfinite(milliseconds) || milliseconds < 0.0)
        {
            ++m_Ingestion.invalidDurationSampleCount;
            return VisibilityBenchmarkSampleResult::InvalidDuration;
        }

        if (frameId < m_Configuration.firstFrameId)
        {
            ++m_Ingestion.outsideRunWindowSampleCount;
            return VisibilityBenchmarkSampleResult::OutsideRunWindow;
        }

        if (frameId < m_FirstMeasuredFrameId)
        {
            ++m_Ingestion.warmupSampleCount;
            return VisibilityBenchmarkSampleResult::WarmupFrame;
        }

        const uint64_t frameOffset = frameId - m_FirstMeasuredFrameId;
        if (frameOffset >= m_Configuration.measuredFrameCount)
        {
            ++m_Ingestion.outsideRunWindowSampleCount;
            return VisibilityBenchmarkSampleResult::OutsideRunWindow;
        }

        const VisibilityBenchmarkStageMask stageBit =
            VisibilityBenchmarkStageMask(1u) << stageIndex;
        if ((m_Configuration.requiredStageMask & stageBit) == 0u)
        {
            ++m_Ingestion.extraneousStageSampleCount;
            return VisibilityBenchmarkSampleResult::StageNotRequired;
        }

        FrameSamples& frame = m_Frames[static_cast<size_t>(frameOffset)];
        if ((frame.receivedStageMask & stageBit) != 0u)
        {
            ++m_Ingestion.duplicateStageSampleCount;
            return VisibilityBenchmarkSampleResult::DuplicateStage;
        }

        double summedStageMilliseconds = frame.summedStageMilliseconds;
        if ((m_Configuration.summedStageMask & stageBit) != 0u)
            summedStageMilliseconds += milliseconds;
        double producerSubtotalMilliseconds =
            frame.producerSubtotalMilliseconds;
        if ((m_Configuration.producerStageMask & stageBit) != 0u)
            producerSubtotalMilliseconds += milliseconds;

        double completeEffectMilliseconds = summedStageMilliseconds;
        if (stage == VisibilityBenchmarkStage::EffectEnvelope)
            completeEffectMilliseconds = milliseconds;
        else if ((frame.receivedStageMask & VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::EffectEnvelope)) != 0u)
        {
            completeEffectMilliseconds = frame.stageMilliseconds[
                static_cast<size_t>(
                    VisibilityBenchmarkStage::EffectEnvelope)];
        }

        if (!std::isfinite(summedStageMilliseconds) ||
            !std::isfinite(completeEffectMilliseconds) ||
            !std::isfinite(producerSubtotalMilliseconds))
        {
            ++m_Ingestion.invalidDurationSampleCount;
            return VisibilityBenchmarkSampleResult::InvalidDuration;
        }

        frame.stageMilliseconds[stageIndex] = milliseconds;
        frame.receivedStageMask |= stageBit;
        frame.summedStageMilliseconds = summedStageMilliseconds;
        frame.completeEffectMilliseconds = completeEffectMilliseconds;
        frame.producerSubtotalMilliseconds = producerSubtotalMilliseconds;
        ++m_Ingestion.acceptedSampleCount;

        if (frame.receivedStageMask == m_Configuration.requiredStageMask)
        {
            ++m_CompleteFrameCount;
            return VisibilityBenchmarkSampleResult::FrameCompleted;
        }

        return VisibilityBenchmarkSampleResult::Accepted;
    }

    VisibilityBenchmarkSummary VisibilityBenchmarkStatistics::BuildSummary()
        const
    {
        VisibilityBenchmarkSummary result;
        result.configuration = m_Configuration;
        result.firstMeasuredFrameId = m_FirstMeasuredFrameId;
        result.ingestion = m_Ingestion;

        std::array<std::vector<double>, VisibilityBenchmarkStageCount>
            stageValues;
        for (size_t stageIndex = 0u;
            stageIndex < VisibilityBenchmarkStageCount;
            ++stageIndex)
        {
            const auto stage =
                static_cast<VisibilityBenchmarkStage>(stageIndex);
            VisibilityBenchmarkStageSummary& stageSummary =
                result.stages[stageIndex];
            stageSummary.stage = stage;
            stageSummary.key = VisibilityBenchmarkStageKey(stage);
            stageSummary.required =
                (m_Configuration.requiredStageMask &
                    (VisibilityBenchmarkStageMask(1u) << stageIndex)) != 0u;
            stageValues[stageIndex].reserve(m_CompleteFrameCount);
        }

        std::vector<double> producerSubtotalValues;
        std::vector<double> summedStageValues;
        std::vector<double> unattributedResidualValues;
        std::vector<double> completeEffectValues;
        producerSubtotalValues.reserve(m_CompleteFrameCount);
        summedStageValues.reserve(m_CompleteFrameCount);
        unattributedResidualValues.reserve(m_CompleteFrameCount);
        completeEffectValues.reserve(m_CompleteFrameCount);
        result.completeFrames.reserve(m_CompleteFrameCount);

        for (size_t frameIndex = 0u;
            frameIndex < m_Frames.size();
            ++frameIndex)
        {
            const FrameSamples& frame = m_Frames[frameIndex];
            if (frame.receivedStageMask !=
                m_Configuration.requiredStageMask)
            {
                if (frame.receivedStageMask != 0u)
                    ++result.observedIncompleteFrameCount;
                continue;
            }

            VisibilityBenchmarkCompleteFrameSummary frameSummary;
            frameSummary.frameId = m_FirstMeasuredFrameId + frameIndex;
            frameSummary.stageMilliseconds = frame.stageMilliseconds;
            frameSummary.producerSubtotalMilliseconds =
                frame.producerSubtotalMilliseconds;
            frameSummary.summedStageMilliseconds =
                frame.summedStageMilliseconds;
            const bool hasEffectEnvelope =
                (m_Configuration.requiredStageMask &
                    VisibilityBenchmarkStageBit(
                        VisibilityBenchmarkStage::EffectEnvelope)) != 0u;
            frameSummary.unattributedResidualMilliseconds =
                hasEffectEnvelope
                    ? frame.completeEffectMilliseconds -
                        frame.summedStageMilliseconds
                    : 0.0;
            frameSummary.completeEffectMilliseconds =
                frame.completeEffectMilliseconds;
            result.completeFrames.push_back(frameSummary);

            for (size_t stageIndex = 0u;
                stageIndex < VisibilityBenchmarkStageCount;
                ++stageIndex)
            {
                if (result.stages[stageIndex].required)
                {
                    stageValues[stageIndex].push_back(
                        frame.stageMilliseconds[stageIndex]);
                }
            }

            if (m_Configuration.producerStageMask != 0u)
            {
                producerSubtotalValues.push_back(
                    frame.producerSubtotalMilliseconds);
            }
            if (m_Configuration.summedStageMask != 0u)
            {
                summedStageValues.push_back(
                    frame.summedStageMilliseconds);
            }
            if (hasEffectEnvelope)
            {
                unattributedResidualValues.push_back(
                    frameSummary.unattributedResidualMilliseconds);
            }
            completeEffectValues.push_back(
                frame.completeEffectMilliseconds);
        }

        result.completeFrameCount =
            static_cast<uint32_t>(result.completeFrames.size());
        result.incompleteFrameCount =
            m_Configuration.measuredFrameCount - result.completeFrameCount;
        result.unobservedFrameCount = result.incompleteFrameCount -
            result.observedIncompleteFrameCount;

        for (size_t stageIndex = 0u;
            stageIndex < VisibilityBenchmarkStageCount;
            ++stageIndex)
        {
            if (result.stages[stageIndex].required)
            {
                result.stages[stageIndex].distribution =
                    BuildDistribution(std::move(stageValues[stageIndex]));
            }
        }

        result.producerSubtotal =
            BuildDistribution(std::move(producerSubtotalValues));
        result.summedStages =
            BuildDistribution(std::move(summedStageValues));
        result.unattributedResidual =
            BuildDistribution(std::move(unattributedResidualValues));
        result.completeEffect =
            BuildDistribution(std::move(completeEffectValues));
        return result;
    }
}
