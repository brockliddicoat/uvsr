#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uvsr
{
    enum class VisibilityBenchmarkStage : uint32_t
    {
        DepthPreparation,
        FirstTrace,
        LaterTrace,
        LaterTraceBounce2,
        LaterTraceBounce3,
        LaterTraceBounce4,
        Temporal,
        SpatialResolve,
        FullResolutionApply,
        Composition,
        EffectEnvelope,
        Count
    };

    using VisibilityBenchmarkStageMask = uint32_t;

    constexpr size_t VisibilityBenchmarkStageCount =
        static_cast<size_t>(VisibilityBenchmarkStage::Count);
    inline constexpr uint32_t
        VisibilityBenchmarkMaximumMeasuredFrameCount = 100000u;

    [[nodiscard]] constexpr VisibilityBenchmarkStageMask
    VisibilityBenchmarkStageBit(VisibilityBenchmarkStage stage)
    {
        const uint32_t index = static_cast<uint32_t>(stage);
        return index < static_cast<uint32_t>(VisibilityBenchmarkStage::Count)
            ? VisibilityBenchmarkStageMask(1u) << index
            : 0u;
    }

    constexpr VisibilityBenchmarkStageMask VisibilityBenchmarkAllStageMask =
        (VisibilityBenchmarkStageMask(1u) << VisibilityBenchmarkStageCount) - 1u;

    // The per-bounce stages are nested inside LaterTrace. They are useful
    // breakdowns, but including them in a stage subtotal would count the same
    // GPU work twice. EffectEnvelope is the independent outer timestamp.
    constexpr VisibilityBenchmarkStageMask
        VisibilityBenchmarkBreakdownOnlyStageMask =
            VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::LaterTraceBounce2) |
            VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::LaterTraceBounce3) |
            VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::LaterTraceBounce4);

    constexpr VisibilityBenchmarkStageMask
        VisibilityBenchmarkDefaultSummedStageMask =
            VisibilityBenchmarkAllStageMask &
            ~VisibilityBenchmarkBreakdownOnlyStageMask &
            ~VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::EffectEnvelope);

    // The producer subtotal deliberately excludes the separately reported
    // full-resolution application and lighting-composition stages. A run can
    // override this mask when its pipeline has a different ownership boundary.
    constexpr VisibilityBenchmarkStageMask
        VisibilityBenchmarkDefaultProducerStageMask =
            VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::DepthPreparation) |
            VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::FirstTrace) |
            VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::LaterTrace) |
            VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::Temporal) |
            VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::SpatialResolve);

    [[nodiscard]] const char* VisibilityBenchmarkStageKey(
        VisibilityBenchmarkStage stage);

    struct VisibilityBenchmarkRunMetadata
    {
        std::string profileName;
        std::string permutationKey;
        std::string adapterName;
        std::string clockState;
    };

    struct VisibilityBenchmarkRunConfiguration
    {
        VisibilityBenchmarkRunMetadata metadata;
        uint64_t firstFrameId = 0u;
        uint32_t warmupFrameCount = 120u;
        uint32_t measuredFrameCount = 240u;
        VisibilityBenchmarkStageMask requiredStageMask =
            VisibilityBenchmarkAllStageMask;
        VisibilityBenchmarkStageMask summedStageMask =
            VisibilityBenchmarkDefaultSummedStageMask;
        VisibilityBenchmarkStageMask producerStageMask =
            VisibilityBenchmarkDefaultProducerStageMask;
    };

    enum class VisibilityBenchmarkSampleResult : uint32_t
    {
        Accepted,
        FrameCompleted,
        WarmupFrame,
        OutsideRunWindow,
        StageNotRequired,
        DuplicateStage,
        InvalidStage,
        InvalidDuration
    };

    struct VisibilityBenchmarkIngestionCounts
    {
        uint64_t acceptedSampleCount = 0u;
        uint64_t warmupSampleCount = 0u;
        uint64_t outsideRunWindowSampleCount = 0u;
        uint64_t extraneousStageSampleCount = 0u;
        uint64_t duplicateStageSampleCount = 0u;
        uint64_t invalidStageSampleCount = 0u;
        uint64_t invalidDurationSampleCount = 0u;
    };

    struct VisibilityBenchmarkDistributionSummary
    {
        uint32_t sampleCount = 0u;
        double medianMilliseconds = 0.0;
        double p95Milliseconds = 0.0;
        bool valid = false;
    };

    struct VisibilityBenchmarkStageSummary
    {
        VisibilityBenchmarkStage stage =
            VisibilityBenchmarkStage::DepthPreparation;
        std::string key;
        bool required = false;
        VisibilityBenchmarkDistributionSummary distribution;
    };

    struct VisibilityBenchmarkCompleteFrameSummary
    {
        uint64_t frameId = 0u;
        std::array<double, VisibilityBenchmarkStageCount>
            stageMilliseconds{};
        double producerSubtotalMilliseconds = 0.0;
        double summedStageMilliseconds = 0.0;
        double unattributedResidualMilliseconds = 0.0;
        double completeEffectMilliseconds = 0.0;
    };

    struct VisibilityBenchmarkSummary
    {
        VisibilityBenchmarkRunConfiguration configuration;
        uint64_t firstMeasuredFrameId = 0u;
        uint32_t completeFrameCount = 0u;
        uint32_t incompleteFrameCount = 0u;
        uint32_t observedIncompleteFrameCount = 0u;
        uint32_t unobservedFrameCount = 0u;
        VisibilityBenchmarkIngestionCounts ingestion;
        std::array<VisibilityBenchmarkStageSummary,
            VisibilityBenchmarkStageCount> stages{};
        VisibilityBenchmarkDistributionSummary producerSubtotal;
        VisibilityBenchmarkDistributionSummary summedStages;
        VisibilityBenchmarkDistributionSummary unattributedResidual;
        VisibilityBenchmarkDistributionSummary completeEffect;
        std::vector<VisibilityBenchmarkCompleteFrameSummary> completeFrames;
    };

    // Collects delayed timer-query results by their originating frame. The
    // class is intentionally independent of the renderer and NVRHI; the caller
    // owns timer polling and supplies a stable frame identifier with each
    // result. It is not synchronized for concurrent writers.
    class VisibilityBenchmarkStatistics
    {
    public:
        VisibilityBenchmarkStatistics();

        [[nodiscard]] static bool IsValidConfiguration(
            const VisibilityBenchmarkRunConfiguration& configuration);

        // Replaces the run only when the supplied configuration is valid.
        // producerStageMask is normalized to the required-stage intersection.
        [[nodiscard]] bool Reset(
            const VisibilityBenchmarkRunConfiguration& configuration);

        // Clears all samples and diagnostics while retaining run metadata and
        // frame-window configuration.
        void Reset();

        [[nodiscard]] VisibilityBenchmarkSampleResult AddSample(
            uint64_t frameId,
            VisibilityBenchmarkStage stage,
            double milliseconds);

        [[nodiscard]] const VisibilityBenchmarkRunConfiguration&
        GetConfiguration() const
        {
            return m_Configuration;
        }

        [[nodiscard]] uint64_t GetFirstMeasuredFrameId() const
        {
            return m_FirstMeasuredFrameId;
        }

        [[nodiscard]] uint32_t GetCompleteFrameCount() const
        {
            return m_CompleteFrameCount;
        }

        [[nodiscard]] bool IsComplete() const
        {
            return m_CompleteFrameCount ==
                m_Configuration.measuredFrameCount;
        }

        // Median and p95 use the inclusive linear-interpolation convention:
        // sort N samples, evaluate rank p * (N - 1), and interpolate between
        // adjacent ranks. Unrequired stages and empty distributions are marked
        // invalid and carry zero-valued statistics for JSON compatibility.
        [[nodiscard]] VisibilityBenchmarkSummary BuildSummary() const;

    private:
        struct FrameSamples
        {
            std::array<double, VisibilityBenchmarkStageCount>
                stageMilliseconds{};
            VisibilityBenchmarkStageMask receivedStageMask = 0u;
            double producerSubtotalMilliseconds = 0.0;
            double summedStageMilliseconds = 0.0;
            double completeEffectMilliseconds = 0.0;
        };

        VisibilityBenchmarkRunConfiguration m_Configuration;
        uint64_t m_FirstMeasuredFrameId = 120u;
        std::vector<FrameSamples> m_Frames;
        VisibilityBenchmarkIngestionCounts m_Ingestion;
        uint32_t m_CompleteFrameCount = 0u;
    };
}
