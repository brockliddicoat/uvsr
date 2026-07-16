#include "visibility_benchmark_statistics.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    using namespace uvsr;

    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Visibility benchmark statistics validation failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    void RequireNear(
        double actual,
        double expected,
        const std::string& message,
        double tolerance = 1e-9)
    {
        if (std::abs(actual - expected) > tolerance)
            Fail(message);
    }

    size_t StageIndex(VisibilityBenchmarkStage stage)
    {
        return static_cast<size_t>(stage);
    }

    VisibilityBenchmarkStageMask Stages(
        std::initializer_list<VisibilityBenchmarkStage> stages)
    {
        VisibilityBenchmarkStageMask result = 0u;
        for (VisibilityBenchmarkStage stage : stages)
            result |= VisibilityBenchmarkStageBit(stage);
        return result;
    }

    void TestDelayedOutOfOrderCorrelationAndMetadata()
    {
        VisibilityBenchmarkRunConfiguration configuration;
        configuration.metadata.profileName = "Exact Fixed 8";
        configuration.metadata.permutationKey =
            "AO/Fixed8/UniformSolidAngle/ExactMath";
        configuration.metadata.adapterName = "Intel(R) Arc(TM) Graphics";
        configuration.metadata.clockState = "stable, telemetry available";
        configuration.firstFrameId = 100u;
        configuration.warmupFrameCount = 2u;
        configuration.measuredFrameCount = 3u;
        configuration.requiredStageMask = Stages({
            VisibilityBenchmarkStage::DepthPreparation,
            VisibilityBenchmarkStage::FirstTrace,
            VisibilityBenchmarkStage::Composition });
        configuration.producerStageMask = Stages({
            VisibilityBenchmarkStage::DepthPreparation,
            VisibilityBenchmarkStage::FirstTrace });

        VisibilityBenchmarkStatistics statistics;
        Require(statistics.Reset(configuration),
            "A valid correlated run configuration is accepted");

        Require(statistics.AddSample(103u,
                VisibilityBenchmarkStage::Composition, 30.0) ==
                VisibilityBenchmarkSampleResult::Accepted,
            "A delayed composition result starts frame 103");
        Require(statistics.AddSample(102u,
                VisibilityBenchmarkStage::FirstTrace, 2.0) ==
                VisibilityBenchmarkSampleResult::Accepted,
            "An older frame can arrive after a newer frame");
        Require(statistics.AddSample(104u,
                VisibilityBenchmarkStage::DepthPreparation, 100.0) ==
                VisibilityBenchmarkSampleResult::Accepted,
            "A third frame can interleave with both earlier frames");
        Require(statistics.AddSample(102u,
                VisibilityBenchmarkStage::Composition, 3.0) ==
                VisibilityBenchmarkSampleResult::Accepted,
            "Frame 102 remains incomplete until every required stage arrives");
        Require(statistics.AddSample(103u,
                VisibilityBenchmarkStage::DepthPreparation, 10.0) ==
                VisibilityBenchmarkSampleResult::Accepted,
            "Frame 103 accepts an out-of-order depth result");
        Require(statistics.AddSample(104u,
                VisibilityBenchmarkStage::Composition, 300.0) ==
                VisibilityBenchmarkSampleResult::Accepted,
            "Frame 104 accepts its composition result");
        Require(statistics.AddSample(103u,
                VisibilityBenchmarkStage::FirstTrace, 20.0) ==
                VisibilityBenchmarkSampleResult::FrameCompleted,
            "Frame 103 completes independently of arrival order");
        Require(statistics.AddSample(102u,
                VisibilityBenchmarkStage::DepthPreparation, 1.0) ==
                VisibilityBenchmarkSampleResult::FrameCompleted,
            "Frame 102 completes after its final delayed result");
        Require(statistics.AddSample(104u,
                VisibilityBenchmarkStage::FirstTrace, 200.0) ==
                VisibilityBenchmarkSampleResult::FrameCompleted,
            "Frame 104 completes the measured window");

        Require(statistics.IsComplete(),
            "Every configured measured frame is complete");
        const VisibilityBenchmarkSummary summary = statistics.BuildSummary();
        Require(summary.configuration.metadata.profileName ==
                configuration.metadata.profileName &&
                summary.configuration.metadata.permutationKey ==
                configuration.metadata.permutationKey &&
                summary.configuration.metadata.adapterName ==
                configuration.metadata.adapterName &&
                summary.configuration.metadata.clockState ==
                configuration.metadata.clockState,
            "Profile, permutation, adapter, and clock metadata survive collection");
        Require(summary.firstMeasuredFrameId == 102u,
            "The measured range begins after the explicit warmup range");
        Require(summary.completeFrames.size() == 3u,
            "All complete frames are exported");
        Require(summary.completeFrames[0].frameId == 102u &&
                summary.completeFrames[1].frameId == 103u &&
                summary.completeFrames[2].frameId == 104u,
            "Complete frame rows are deterministic and sorted by frame id");
        RequireNear(summary.completeFrames[0].completeEffectMilliseconds,
            6.0, "Frame 102 retains its correlated complete-effect sum");
        RequireNear(summary.completeFrames[1].producerSubtotalMilliseconds,
            30.0, "Frame 103 retains its correlated producer sum");
        RequireNear(summary.completeFrames[2].completeEffectMilliseconds,
            600.0, "Frame 104 retains its correlated complete-effect sum");
        RequireNear(summary.stages[StageIndex(
                VisibilityBenchmarkStage::FirstTrace)]
                .distribution.medianMilliseconds,
            20.0, "Stage statistics use the same complete frame population");
        RequireNear(summary.producerSubtotal.medianMilliseconds,
            30.0, "Producer subtotal statistics are frame correlated");
        RequireNear(summary.completeEffect.medianMilliseconds,
            60.0, "Complete-effect statistics are frame correlated");
    }

    void TestMissingExtraneousAndDuplicateSamples()
    {
        VisibilityBenchmarkRunConfiguration configuration;
        configuration.firstFrameId = 10u;
        configuration.warmupFrameCount = 0u;
        configuration.measuredFrameCount = 2u;
        configuration.requiredStageMask = Stages({
            VisibilityBenchmarkStage::DepthPreparation,
            VisibilityBenchmarkStage::Composition });
        configuration.producerStageMask = VisibilityBenchmarkStageBit(
            VisibilityBenchmarkStage::DepthPreparation);

        VisibilityBenchmarkStatistics statistics;
        Require(statistics.Reset(configuration),
            "The missing-sample test configuration is valid");
        Require(statistics.AddSample(10u,
                VisibilityBenchmarkStage::DepthPreparation, 1.0) ==
                VisibilityBenchmarkSampleResult::Accepted,
            "The first required stage is accepted");
        Require(statistics.AddSample(10u,
                VisibilityBenchmarkStage::DepthPreparation, 999.0) ==
                VisibilityBenchmarkSampleResult::DuplicateStage,
            "A duplicate stage is rejected and cannot replace its first value");
        Require(statistics.AddSample(10u,
                VisibilityBenchmarkStage::Temporal, 0.25) ==
                VisibilityBenchmarkSampleResult::StageNotRequired,
            "A valid but unrequired stage is reported as extraneous");
        Require(statistics.AddSample(10u,
                VisibilityBenchmarkStage::Composition, 2.0) ==
                VisibilityBenchmarkSampleResult::FrameCompleted,
            "The first frame completes with exactly its required mask");
        Require(statistics.AddSample(11u,
                VisibilityBenchmarkStage::DepthPreparation, 100.0) ==
                VisibilityBenchmarkSampleResult::Accepted,
            "The second frame remains observed but incomplete");
        Require(statistics.AddSample(9u,
                VisibilityBenchmarkStage::DepthPreparation, 4.0) ==
                VisibilityBenchmarkSampleResult::OutsideRunWindow &&
                statistics.AddSample(12u,
                VisibilityBenchmarkStage::DepthPreparation, 4.0) ==
                VisibilityBenchmarkSampleResult::OutsideRunWindow,
            "Frames before and after the run are rejected");

        const VisibilityBenchmarkSummary summary = statistics.BuildSummary();
        Require(summary.completeFrameCount == 1u &&
                summary.incompleteFrameCount == 1u &&
                summary.observedIncompleteFrameCount == 1u &&
                summary.unobservedFrameCount == 0u,
            "Missing stages exclude the frame and remain visible in diagnostics");
        Require(summary.stages[StageIndex(
                VisibilityBenchmarkStage::DepthPreparation)]
                .distribution.sampleCount == 1u,
            "Stage distributions exclude samples from incomplete frames");
        RequireNear(summary.stages[StageIndex(
                VisibilityBenchmarkStage::DepthPreparation)]
                .distribution.medianMilliseconds,
            1.0, "Neither a duplicate nor an incomplete-frame value contaminates evidence");
        RequireNear(summary.completeFrames[0].completeEffectMilliseconds,
            3.0, "The complete frame retains the first accepted stage value");
        Require(summary.ingestion.acceptedSampleCount == 3u &&
                summary.ingestion.duplicateStageSampleCount == 1u &&
                summary.ingestion.extraneousStageSampleCount == 1u &&
                summary.ingestion.outsideRunWindowSampleCount == 2u,
            "Ingestion diagnostics account for accepted and rejected samples");
    }

    void TestPercentileConvention()
    {
        VisibilityBenchmarkRunConfiguration configuration;
        configuration.warmupFrameCount = 0u;
        configuration.measuredFrameCount = 5u;
        configuration.requiredStageMask = VisibilityBenchmarkStageBit(
            VisibilityBenchmarkStage::FirstTrace);
        configuration.producerStageMask = configuration.requiredStageMask;

        VisibilityBenchmarkStatistics statistics;
        Require(statistics.Reset(configuration),
            "The percentile test configuration is valid");
        const std::array<double, 5> values = { 4.0, 0.0, 3.0, 1.0, 2.0 };
        for (size_t index = 0u; index < values.size(); ++index)
        {
            Require(statistics.AddSample(index,
                    VisibilityBenchmarkStage::FirstTrace, values[index]) ==
                    VisibilityBenchmarkSampleResult::FrameCompleted,
                "A one-stage frame completes on its only sample");
        }

        const VisibilityBenchmarkSummary summary = statistics.BuildSummary();
        const VisibilityBenchmarkDistributionSummary& distribution =
            summary.stages[StageIndex(
                VisibilityBenchmarkStage::FirstTrace)].distribution;
        Require(distribution.valid && distribution.sampleCount == 5u,
            "A populated required stage has a valid distribution");
        RequireNear(distribution.medianMilliseconds, 2.0,
            "The median is the inclusive 50th percentile");
        RequireNear(distribution.p95Milliseconds, 3.8,
            "P95 linearly interpolates rank 3.8 of five sorted samples");

        const VisibilityBenchmarkStageSummary& optionalStage =
            summary.stages[StageIndex(
                VisibilityBenchmarkStage::Temporal)];
        Require(!optionalStage.required &&
                !optionalStage.distribution.valid &&
                optionalStage.distribution.sampleCount == 0u &&
                optionalStage.distribution.medianMilliseconds == 0.0 &&
                optionalStage.distribution.p95Milliseconds == 0.0,
            "An unrequired stage emits JSON-safe zero values marked invalid");
    }

    void TestPerFrameTotalsAreNotSumsOfStagePercentiles()
    {
        VisibilityBenchmarkRunConfiguration configuration;
        configuration.warmupFrameCount = 0u;
        configuration.measuredFrameCount = 3u;
        configuration.requiredStageMask = Stages({
            VisibilityBenchmarkStage::FirstTrace,
            VisibilityBenchmarkStage::Composition });
        configuration.producerStageMask = VisibilityBenchmarkStageBit(
            VisibilityBenchmarkStage::FirstTrace);

        VisibilityBenchmarkStatistics statistics;
        Require(statistics.Reset(configuration),
            "The anti-correlated-stage test configuration is valid");
        const std::array<double, 3> trace = { 1.0, 100.0, 1.0 };
        const std::array<double, 3> composition = { 100.0, 1.0, 1.0 };
        for (size_t frame = 0u; frame < trace.size(); ++frame)
        {
            Require(statistics.AddSample(frame,
                    VisibilityBenchmarkStage::Composition,
                    composition[frame]) ==
                    VisibilityBenchmarkSampleResult::Accepted,
                "The first of two anti-correlated stages is accepted");
            Require(statistics.AddSample(frame,
                    VisibilityBenchmarkStage::FirstTrace, trace[frame]) ==
                    VisibilityBenchmarkSampleResult::FrameCompleted,
                "The second anti-correlated stage completes its frame");
        }

        const VisibilityBenchmarkSummary summary = statistics.BuildSummary();
        const auto& traceDistribution = summary.stages[StageIndex(
            VisibilityBenchmarkStage::FirstTrace)].distribution;
        const auto& compositionDistribution = summary.stages[StageIndex(
            VisibilityBenchmarkStage::Composition)].distribution;
        RequireNear(traceDistribution.medianMilliseconds, 1.0,
            "The trace-stage median is independently correct");
        RequireNear(compositionDistribution.medianMilliseconds, 1.0,
            "The composition-stage median is independently correct");
        RequireNear(summary.completeEffect.medianMilliseconds, 101.0,
            "The effect median is computed from per-frame sums");
        RequireNear(summary.completeEffect.p95Milliseconds, 101.0,
            "The effect p95 is computed from per-frame sums");
        Require(std::abs(summary.completeEffect.medianMilliseconds -
                    (traceDistribution.medianMilliseconds +
                        compositionDistribution.medianMilliseconds)) > 1.0,
            "The effect median is never synthesized by summing stage medians");
        Require(std::abs(summary.completeEffect.p95Milliseconds -
                    (traceDistribution.p95Milliseconds +
                        compositionDistribution.p95Milliseconds)) > 1.0,
            "The effect p95 is never synthesized by summing stage p95 values");
        RequireNear(summary.completeFrames[0].completeEffectMilliseconds,
            101.0, "Frame zero preserves its correlated total");
        RequireNear(summary.completeFrames[1].completeEffectMilliseconds,
            101.0, "Frame one preserves its correlated total");
        RequireNear(summary.completeFrames[2].completeEffectMilliseconds,
            2.0, "Frame two preserves its correlated total");
    }

    void TestOuterEnvelopeAndNestedBounceBreakdowns()
    {
        VisibilityBenchmarkRunConfiguration configuration;
        configuration.warmupFrameCount = 0u;
        configuration.measuredFrameCount = 2u;
        configuration.requiredStageMask = Stages({
            VisibilityBenchmarkStage::FirstTrace,
            VisibilityBenchmarkStage::LaterTrace,
            VisibilityBenchmarkStage::LaterTraceBounce2,
            VisibilityBenchmarkStage::LaterTraceBounce3,
            VisibilityBenchmarkStage::Composition,
            VisibilityBenchmarkStage::EffectEnvelope });
        configuration.summedStageMask =
            VisibilityBenchmarkDefaultSummedStageMask;
        configuration.producerStageMask = Stages({
            VisibilityBenchmarkStage::FirstTrace,
            VisibilityBenchmarkStage::LaterTrace });

        VisibilityBenchmarkStatistics statistics;
        Require(statistics.Reset(configuration),
            "An envelope run with nested bounce breakdowns is valid");

        const auto addFrame = [&statistics](
            uint64_t frameId,
            double envelopeMilliseconds)
        {
            Require(statistics.AddSample(frameId,
                    VisibilityBenchmarkStage::LaterTraceBounce3, 0.75) ==
                    VisibilityBenchmarkSampleResult::Accepted,
                "A nested bounce-three sample is accepted");
            Require(statistics.AddSample(frameId,
                    VisibilityBenchmarkStage::FirstTrace, 2.0) ==
                    VisibilityBenchmarkSampleResult::Accepted,
                "The first trace is accepted independently of query order");
            Require(statistics.AddSample(frameId,
                    VisibilityBenchmarkStage::LaterTraceBounce2, 1.25) ==
                    VisibilityBenchmarkSampleResult::Accepted,
                "A nested bounce-two sample is accepted");
            Require(statistics.AddSample(frameId,
                    VisibilityBenchmarkStage::LaterTrace, 2.0) ==
                    VisibilityBenchmarkSampleResult::Accepted,
                "The aggregate later-trace query is accepted once");
            Require(statistics.AddSample(frameId,
                    VisibilityBenchmarkStage::Composition, 1.0) ==
                    VisibilityBenchmarkSampleResult::Accepted,
                "Composition contributes to the additive subtotal");
            Require(statistics.AddSample(frameId,
                    VisibilityBenchmarkStage::EffectEnvelope,
                    envelopeMilliseconds) ==
                    VisibilityBenchmarkSampleResult::FrameCompleted,
                "The independent envelope completes the correlated frame");
        };

        addFrame(0u, 5.5);
        addFrame(1u, 4.75);

        const VisibilityBenchmarkSummary summary = statistics.BuildSummary();
        RequireNear(summary.completeFrames[0].summedStageMilliseconds,
            5.0, "Nested bounce breakdowns are not double-counted");
        RequireNear(summary.completeFrames[0].completeEffectMilliseconds,
            5.5, "The complete effect comes from the outer GPU envelope");
        RequireNear(
            summary.completeFrames[0].unattributedResidualMilliseconds,
            0.5, "The residual exposes work outside named stage queries");
        RequireNear(
            summary.completeFrames[1].unattributedResidualMilliseconds,
            -0.25,
            "A signed residual preserves timer noise instead of clamping it");
        RequireNear(summary.producerSubtotal.medianMilliseconds,
            4.0, "The producer subtotal counts the aggregate later trace once");
        RequireNear(summary.summedStages.medianMilliseconds,
            5.0, "Summed-stage statistics use per-frame additive totals");
        RequireNear(summary.completeEffect.medianMilliseconds,
            5.125, "Envelope statistics remain frame correlated");
        RequireNear(summary.unattributedResidual.medianMilliseconds,
            0.125, "Residual statistics retain their signed values");
        RequireNear(summary.stages[StageIndex(
                VisibilityBenchmarkStage::LaterTraceBounce2)]
                .distribution.medianMilliseconds,
            1.25, "Per-bounce timing remains independently reportable");
    }

    void TestWarmupAndMeasuredWindows()
    {
        VisibilityBenchmarkStatistics defaults;
        Require(defaults.GetConfiguration().warmupFrameCount == 120u &&
                defaults.GetConfiguration().measuredFrameCount == 240u &&
                defaults.GetFirstMeasuredFrameId() == 120u,
            "Default runs use 120 warmup and 240 measured frames");

        VisibilityBenchmarkRunConfiguration configuration;
        configuration.firstFrameId = 50u;
        configuration.warmupFrameCount = 2u;
        configuration.measuredFrameCount = 2u;
        configuration.requiredStageMask = VisibilityBenchmarkStageBit(
            VisibilityBenchmarkStage::Temporal);
        configuration.producerStageMask = configuration.requiredStageMask;

        VisibilityBenchmarkStatistics statistics;
        Require(statistics.Reset(configuration),
            "The explicit warmup-window configuration is valid");
        Require(statistics.AddSample(49u,
                VisibilityBenchmarkStage::Temporal, 1.0) ==
                VisibilityBenchmarkSampleResult::OutsideRunWindow,
            "A pre-run frame is outside the run window");
        Require(statistics.AddSample(50u,
                VisibilityBenchmarkStage::Temporal, 10.0) ==
                VisibilityBenchmarkSampleResult::WarmupFrame &&
                statistics.AddSample(51u,
                VisibilityBenchmarkStage::Temporal, 20.0) ==
                VisibilityBenchmarkSampleResult::WarmupFrame,
            "Warmup frames are acknowledged but never measured");
        Require(statistics.AddSample(53u,
                VisibilityBenchmarkStage::Temporal, 2.0) ==
                VisibilityBenchmarkSampleResult::FrameCompleted &&
                statistics.AddSample(52u,
                VisibilityBenchmarkStage::Temporal, 1.0) ==
                VisibilityBenchmarkSampleResult::FrameCompleted,
            "Measured frames can complete in reverse arrival order");
        Require(statistics.AddSample(54u,
                VisibilityBenchmarkStage::Temporal, 3.0) ==
                VisibilityBenchmarkSampleResult::OutsideRunWindow,
            "A post-measurement frame is outside the run window");

        const VisibilityBenchmarkSummary summary = statistics.BuildSummary();
        Require(summary.completeFrames.size() == 2u &&
                summary.completeFrames[0].frameId == 52u &&
                summary.completeFrames[1].frameId == 53u,
            "Only the measured frame range appears in sorted output");
        RequireNear(summary.completeEffect.medianMilliseconds, 1.5,
            "Warmup values do not affect measured statistics");
        Require(summary.ingestion.warmupSampleCount == 2u &&
                summary.ingestion.outsideRunWindowSampleCount == 2u,
            "Warmup and out-of-window diagnostics remain distinct");
    }

    void TestResetProfileChangeAndOptionalStages()
    {
        VisibilityBenchmarkRunConfiguration firstConfiguration;
        firstConfiguration.metadata.profileName = "Reference";
        firstConfiguration.warmupFrameCount = 0u;
        firstConfiguration.measuredFrameCount = 1u;
        firstConfiguration.requiredStageMask = VisibilityBenchmarkStageBit(
            VisibilityBenchmarkStage::FirstTrace);
        firstConfiguration.producerStageMask =
            firstConfiguration.requiredStageMask;

        VisibilityBenchmarkStatistics statistics;
        Require(statistics.Reset(firstConfiguration),
            "The first profile configuration is valid");
        Require(statistics.AddSample(0u,
                VisibilityBenchmarkStage::FirstTrace, 3.0) ==
                VisibilityBenchmarkSampleResult::FrameCompleted,
            "The first profile records a complete frame");
        statistics.Reset();
        VisibilityBenchmarkSummary summary = statistics.BuildSummary();
        Require(summary.configuration.metadata.profileName == "Reference" &&
                summary.completeFrameCount == 0u &&
                summary.ingestion.acceptedSampleCount == 0u,
            "A same-profile reset retains configuration but clears all evidence");

        VisibilityBenchmarkRunConfiguration secondConfiguration;
        secondConfiguration.metadata.profileName = "Fused Apply";
        secondConfiguration.metadata.permutationKey = "AO/FusedApply";
        secondConfiguration.firstFrameId = 200u;
        secondConfiguration.warmupFrameCount = 1u;
        secondConfiguration.measuredFrameCount = 1u;
        secondConfiguration.requiredStageMask = VisibilityBenchmarkStageBit(
            VisibilityBenchmarkStage::FullResolutionApply);
        secondConfiguration.producerStageMask =
            VisibilityBenchmarkDefaultProducerStageMask;
        Require(statistics.Reset(secondConfiguration),
            "Changing profile installs a fresh run configuration");
        Require(statistics.GetConfiguration().producerStageMask == 0u,
            "The producer mask is normalized to the required-stage intersection");
        Require(statistics.AddSample(201u,
                VisibilityBenchmarkStage::FullResolutionApply, 0.5) ==
                VisibilityBenchmarkSampleResult::FrameCompleted,
            "An application-only optional mask can complete");
        summary = statistics.BuildSummary();
        Require(summary.configuration.metadata.profileName == "Fused Apply" &&
                summary.configuration.metadata.permutationKey == "AO/FusedApply" &&
                !summary.producerSubtotal.valid &&
                summary.completeEffect.valid,
            "Profile changes replace metadata and expose an absent producer subtotal");

        VisibilityBenchmarkRunConfiguration invalid = secondConfiguration;
        invalid.measuredFrameCount = 0u;
        Require(!statistics.Reset(invalid),
            "A zero-sized measured window is rejected");
        invalid = secondConfiguration;
        invalid.measuredFrameCount =
            VisibilityBenchmarkMaximumMeasuredFrameCount + 1u;
        Require(!statistics.Reset(invalid),
            "An impractically large measured window is rejected before allocation");
        summary = statistics.BuildSummary();
        Require(summary.configuration.metadata.profileName == "Fused Apply" &&
                summary.completeFrameCount == 1u,
            "An invalid reset leaves the active run and its evidence unchanged");

        invalid = secondConfiguration;
        invalid.requiredStageMask = 0u;
        Require(!VisibilityBenchmarkStatistics::IsValidConfiguration(invalid),
            "An empty required-stage mask is invalid");
        invalid = secondConfiguration;
        invalid.requiredStageMask = VisibilityBenchmarkAllStageMask | (1u << 20u);
        Require(!VisibilityBenchmarkStatistics::IsValidConfiguration(invalid),
            "Unknown required-stage bits are invalid");
        invalid = secondConfiguration;
        invalid.producerStageMask = 1u << 20u;
        Require(!VisibilityBenchmarkStatistics::IsValidConfiguration(invalid),
            "Unknown producer-stage bits are invalid");
        invalid = secondConfiguration;
        invalid.summedStageMask = 1u << 20u;
        Require(!VisibilityBenchmarkStatistics::IsValidConfiguration(invalid),
            "Unknown summed-stage bits are invalid");
        invalid = secondConfiguration;
        invalid.firstFrameId = std::numeric_limits<uint64_t>::max();
        invalid.warmupFrameCount = 1u;
        Require(!VisibilityBenchmarkStatistics::IsValidConfiguration(invalid),
            "A warmup range that overflows frame ids is invalid");
        invalid.warmupFrameCount = 0u;
        invalid.measuredFrameCount = 2u;
        Require(!VisibilityBenchmarkStatistics::IsValidConfiguration(invalid),
            "A measured range that overflows frame ids is invalid");
    }

    void TestInvalidNumericInputs()
    {
        VisibilityBenchmarkRunConfiguration configuration;
        configuration.warmupFrameCount = 0u;
        configuration.measuredFrameCount = 2u;
        configuration.requiredStageMask = Stages({
            VisibilityBenchmarkStage::DepthPreparation,
            VisibilityBenchmarkStage::Composition });
        configuration.producerStageMask = VisibilityBenchmarkStageBit(
            VisibilityBenchmarkStage::DepthPreparation);

        VisibilityBenchmarkStatistics statistics;
        Require(statistics.Reset(configuration),
            "The invalid-input test configuration is valid");
        Require(statistics.AddSample(0u,
                static_cast<VisibilityBenchmarkStage>(999u), 1.0) ==
                VisibilityBenchmarkSampleResult::InvalidStage,
            "An invalid stage enumerator is rejected");
        Require(statistics.AddSample(0u,
                VisibilityBenchmarkStage::DepthPreparation, -1.0) ==
                VisibilityBenchmarkSampleResult::InvalidDuration &&
                statistics.AddSample(0u,
                VisibilityBenchmarkStage::DepthPreparation,
                std::numeric_limits<double>::quiet_NaN()) ==
                VisibilityBenchmarkSampleResult::InvalidDuration &&
                statistics.AddSample(0u,
                VisibilityBenchmarkStage::DepthPreparation,
                std::numeric_limits<double>::infinity()) ==
                VisibilityBenchmarkSampleResult::InvalidDuration &&
                statistics.AddSample(0u,
                VisibilityBenchmarkStage::DepthPreparation,
                -std::numeric_limits<double>::infinity()) ==
                VisibilityBenchmarkSampleResult::InvalidDuration,
            "Negative and non-finite durations are rejected");

        const double maximum = std::numeric_limits<double>::max();
        Require(statistics.AddSample(0u,
                VisibilityBenchmarkStage::DepthPreparation, maximum) ==
                VisibilityBenchmarkSampleResult::Accepted,
            "A finite duration is accepted even at the representable limit");
        Require(statistics.AddSample(0u,
                VisibilityBenchmarkStage::Composition, maximum) ==
                VisibilityBenchmarkSampleResult::InvalidDuration,
            "A finite sample that would overflow its frame subtotal is rejected");
        Require(statistics.AddSample(0u,
                VisibilityBenchmarkStage::Composition, 0.0) ==
                VisibilityBenchmarkSampleResult::FrameCompleted,
            "The rejected subtotal sample leaves its stage available for repair");
        Require(statistics.AddSample(1u,
                VisibilityBenchmarkStage::DepthPreparation, -0.0) ==
                VisibilityBenchmarkSampleResult::Accepted &&
                statistics.AddSample(1u,
                VisibilityBenchmarkStage::Composition, 0.0) ==
                VisibilityBenchmarkSampleResult::FrameCompleted,
            "Zero and negative zero are valid timer results");

        const VisibilityBenchmarkSummary summary = statistics.BuildSummary();
        Require(summary.completeFrameCount == 2u &&
                std::isfinite(summary.completeEffect.medianMilliseconds) &&
                std::isfinite(summary.completeEffect.p95Milliseconds),
            "Accepted numeric inputs always produce finite export statistics");
        Require(summary.ingestion.invalidStageSampleCount == 1u &&
                summary.ingestion.invalidDurationSampleCount == 5u,
            "Invalid-stage, invalid-duration, and overflow diagnostics are counted");
    }
}

int main()
{
    TestDelayedOutOfOrderCorrelationAndMetadata();
    TestMissingExtraneousAndDuplicateSamples();
    TestPercentileConvention();
    TestPerFrameTotalsAreNotSumsOfStagePercentiles();
    TestOuterEnvelopeAndNestedBounceBreakdowns();
    TestWarmupAndMeasuredWindows();
    TestResetProfileChangeAndOptionalStages();
    TestInvalidNumericInputs();

    std::cout << "Visibility benchmark statistics validation passed\n";
    return EXIT_SUCCESS;
}
