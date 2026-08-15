#include "sample_accumulation_settings.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace
{
    using namespace uvsr;

    [[nodiscard]] bool Near(
        float actual,
        float expected,
        float tolerance = 1e-6f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    void TestPresets()
    {
        const SampleAccumulationSettings defaults;
        assert(defaults.preset == SampleAccumulationPreset::VarianceGuided);
        assert(defaults.averaging ==
            SampleAccumulationAveraging::Cumulative);
        assert(defaults.scheduling ==
            SampleAccumulationScheduling::VarianceGuided);
        assert(!IsSampleAccumulationPresetCustomized(defaults));

        const SampleAccumulationSettings responsive =
            ApplySampleAccumulationPreset(
                defaults,
                SampleAccumulationPreset::Responsive);
        assert(responsive.averaging ==
            SampleAccumulationAveraging::Exponential);
        assert(responsive.scheduling ==
            SampleAccumulationScheduling::EveryPixel);
        assert(responsive.effectiveHistory == 32u);

        const SampleAccumulationSettings adaptive =
            ApplySampleAccumulationPreset(
                defaults,
                SampleAccumulationPreset::VarianceGuided);
        assert(adaptive.averaging ==
            SampleAccumulationAveraging::Cumulative);
        assert(adaptive.scheduling ==
            SampleAccumulationScheduling::VarianceGuided);
        assert(adaptive.minimumSamples == 16u);
        assert(Near(adaptive.targetRelativeError, 0.02f));
        assert(Near(adaptive.minimumUpdateRate, 1.f / 16.f));

        assert(GetSampleAccumulationPresetLabel(defaults.preset) ==
            std::string_view("Variance Guided"));
        assert(GetSampleAccumulationAveragingLabel(responsive.averaging) ==
            std::string_view("Exponential Mean"));
        assert(GetSampleAccumulationSchedulingLabel(adaptive.scheduling) ==
            std::string_view("Variance Guided"));
    }

    void TestEffectiveHistoryPresets()
    {
        struct Expectation
        {
            SampleAccumulationHistoryPreset preset;
            uint32_t effectiveHistory;
            std::string_view label;
        };
        constexpr std::array<Expectation, 5> Expectations = {{
            { SampleAccumulationHistoryPreset::QuickPreview,
                8u, "Quick Preview" },
            { SampleAccumulationHistoryPreset::Responsive,
                32u, "Responsive" },
            { SampleAccumulationHistoryPreset::Balanced,
                64u, "Balanced" },
            { SampleAccumulationHistoryPreset::Stable,
                256u, "Stable" },
            { SampleAccumulationHistoryPreset::VeryStable,
                1024u, "Very Stable" }
        }};

        SampleAccumulationSettings source = ApplySampleAccumulationPreset(
            {}, SampleAccumulationPreset::Progressive);
        source.averaging = SampleAccumulationAveraging::Exponential;
        source.scheduling = SampleAccumulationScheduling::VarianceGuided;
        source.effectiveHistory = 127u;
        source.minimumSamples = 23u;
        source.targetRelativeError = 0.031f;
        source.minimumUpdateRate = 0.125f;

        for (const Expectation& expectation : Expectations)
        {
            assert(IsValidSampleAccumulationHistoryPreset(
                expectation.preset));
            assert(GetSampleAccumulationHistoryPresetLabel(
                expectation.preset) == expectation.label);
            assert(GetSampleAccumulationHistoryPresetValue(
                expectation.preset) == expectation.effectiveHistory);

            SampleAccumulationSettings expected = source;
            expected.effectiveHistory = expectation.effectiveHistory;
            const SampleAccumulationSettings applied =
                ApplySampleAccumulationHistoryPreset(
                    source, expectation.preset);
            assert(applied == expected);
            assert(applied.preset == source.preset);
            assert(GetMatchingSampleAccumulationHistoryPreset(applied) ==
                expectation.preset);
        }

        const SampleAccumulationSettings progressive =
            ApplySampleAccumulationPreset(
                {}, SampleAccumulationPreset::Progressive);
        assert(!IsSampleAccumulationPresetCustomized(progressive));
        assert(IsSampleAccumulationPresetCustomized(
            ApplySampleAccumulationHistoryPreset(
                progressive,
                SampleAccumulationHistoryPreset::QuickPreview)));
        assert(!IsSampleAccumulationPresetCustomized(
            ApplySampleAccumulationHistoryPreset(
                progressive,
                SampleAccumulationHistoryPreset::Balanced)));

        const SampleAccumulationSettings responsive =
            ApplySampleAccumulationPreset(
                {}, SampleAccumulationPreset::Responsive);
        assert(!IsSampleAccumulationPresetCustomized(
            ApplySampleAccumulationHistoryPreset(
                responsive,
                SampleAccumulationHistoryPreset::Responsive)));
        assert(IsSampleAccumulationPresetCustomized(
            ApplySampleAccumulationHistoryPreset(
                responsive,
                SampleAccumulationHistoryPreset::Stable)));

        constexpr auto Invalid =
            static_cast<SampleAccumulationHistoryPreset>(255u);
        static_assert(!IsValidSampleAccumulationHistoryPreset(Invalid));
        static_assert(GetSampleAccumulationHistoryPresetLabel(Invalid).empty());
        static_assert(GetSampleAccumulationHistoryPresetValue(Invalid) == 64u);
        SampleAccumulationSettings expectedFallback = source;
        expectedFallback.effectiveHistory = 64u;
        assert(ApplySampleAccumulationHistoryPreset(source, Invalid) ==
            expectedFallback);
        source.effectiveHistory = 127u;
        assert(GetMatchingSampleAccumulationHistoryPreset(source) ==
            SampleAccumulationHistoryPreset::Count);
    }

    void TestAdaptiveWorkloadPresets()
    {
        struct Expectation
        {
            SampleAccumulationWorkloadPreset preset;
            uint32_t minimumSamples;
            float targetRelativeError;
            float minimumUpdateRate;
            std::string_view label;
        };
        constexpr std::array<Expectation, 4> Expectations = {{
            { SampleAccumulationWorkloadPreset::FullQuality,
                32u, 0.01f, 1.f / 4.f, "Full Quality" },
            { SampleAccumulationWorkloadPreset::Balanced,
                16u, 0.02f, 1.f / 16.f, "Balanced" },
            { SampleAccumulationWorkloadPreset::Performance,
                8u, 0.04f, 1.f / 32.f, "Performance" },
            { SampleAccumulationWorkloadPreset::MaximumSavings,
                4u, 0.08f, 1.f / 64.f, "Maximum Savings" }
        }};

        SampleAccumulationSettings source = ApplySampleAccumulationPreset(
            {}, SampleAccumulationPreset::Responsive);
        source.effectiveHistory = 127u;
        for (const Expectation& expectation : Expectations)
        {
            assert(IsValidSampleAccumulationWorkloadPreset(
                expectation.preset));
            assert(GetSampleAccumulationWorkloadPresetLabel(
                expectation.preset) == expectation.label);
            const SampleAccumulationWorkloadValues values =
                GetSampleAccumulationWorkloadPresetValues(
                    expectation.preset);
            assert(values.minimumSamples == expectation.minimumSamples);
            assert(Near(
                values.targetRelativeError,
                expectation.targetRelativeError));
            assert(Near(
                values.minimumUpdateRate,
                expectation.minimumUpdateRate));

            const SampleAccumulationSettings applied =
                ApplySampleAccumulationWorkloadPreset(
                    source,
                    expectation.preset);
            assert(applied.preset == source.preset);
            assert(applied.averaging == source.averaging);
            assert(applied.scheduling == source.scheduling);
            assert(applied.effectiveHistory == source.effectiveHistory);
            assert(applied.minimumSamples == expectation.minimumSamples);
            assert(Near(
                applied.targetRelativeError,
                expectation.targetRelativeError));
            assert(Near(
                applied.minimumUpdateRate,
                expectation.minimumUpdateRate));
            assert(GetMatchingSampleAccumulationWorkloadPreset(applied) ==
                expectation.preset);
        }

        constexpr auto Invalid =
            static_cast<SampleAccumulationWorkloadPreset>(255u);
        static_assert(!IsValidSampleAccumulationWorkloadPreset(Invalid));
        static_assert(GetSampleAccumulationWorkloadPresetLabel(Invalid).empty());
        const SampleAccumulationSettings fallback =
            ApplySampleAccumulationWorkloadPreset(source, Invalid);
        assert(fallback.minimumSamples == 16u);
        assert(Near(fallback.targetRelativeError, 0.02f));
        assert(Near(fallback.minimumUpdateRate, 1.f / 16.f));
        source.minimumSamples = 17u;
        assert(GetMatchingSampleAccumulationWorkloadPreset(source) ==
            SampleAccumulationWorkloadPreset::Count);
    }

    void TestPresetCustomizationAndSanitization()
    {
        SampleAccumulationSettings custom;
        custom.effectiveHistory = 127u;
        assert(custom.preset == SampleAccumulationPreset::VarianceGuided);
        assert(IsSampleAccumulationPresetCustomized(custom));

        custom.effectiveHistory = 64u;
        assert(!IsSampleAccumulationPresetCustomized(custom));

        SampleAccumulationSettings responsive =
            ApplySampleAccumulationPreset(
                custom,
                SampleAccumulationPreset::Responsive);
        responsive.effectiveHistory = 4u;
        assert(responsive.preset == SampleAccumulationPreset::Responsive);
        assert(IsSampleAccumulationPresetCustomized(responsive));

        const SampleAccumulationSettings origin =
            ApplySampleAccumulationPreset(
                {}, SampleAccumulationPreset::VarianceGuided);
        SampleAccumulationSettings edited = origin;
        edited.averaging = SampleAccumulationAveraging::Exponential;
        assert(IsSampleAccumulationPresetCustomized(edited));
        edited = origin;
        edited.scheduling = SampleAccumulationScheduling::EveryPixel;
        assert(IsSampleAccumulationPresetCustomized(edited));
        edited = origin;
        ++edited.effectiveHistory;
        assert(IsSampleAccumulationPresetCustomized(edited));
        edited = origin;
        ++edited.minimumSamples;
        assert(IsSampleAccumulationPresetCustomized(edited));
        edited = origin;
        edited.targetRelativeError += 0.001f;
        assert(IsSampleAccumulationPresetCustomized(edited));
        edited = origin;
        edited.minimumUpdateRate += 0.001f;
        assert(IsSampleAccumulationPresetCustomized(edited));

        SampleAccumulationSettings provenance =
            ApplySampleAccumulationPreset(
                {}, SampleAccumulationPreset::Progressive);
        const SampleAccumulationSettings responsiveRecipe =
            ApplySampleAccumulationPreset(
                {}, SampleAccumulationPreset::Responsive);
        provenance.averaging = responsiveRecipe.averaging;
        provenance.scheduling = responsiveRecipe.scheduling;
        provenance.effectiveHistory = responsiveRecipe.effectiveHistory;
        provenance.minimumSamples = responsiveRecipe.minimumSamples;
        provenance.targetRelativeError =
            responsiveRecipe.targetRelativeError;
        provenance.minimumUpdateRate = responsiveRecipe.minimumUpdateRate;
        assert(provenance.preset ==
            SampleAccumulationPreset::Progressive);
        assert(IsSampleAccumulationPresetCustomized(provenance));
        provenance = ApplySampleAccumulationPreset(
            provenance, SampleAccumulationPreset::Progressive);
        assert(!IsSampleAccumulationPresetCustomized(provenance));

        SampleAccumulationSettings invalid;
        invalid.preset = static_cast<SampleAccumulationPreset>(255u);
        invalid.averaging =
            static_cast<SampleAccumulationAveraging>(255u);
        invalid.scheduling =
            static_cast<SampleAccumulationScheduling>(255u);
        invalid.effectiveHistory = 0u;
        invalid.minimumSamples = std::numeric_limits<uint32_t>::max();
        invalid.targetRelativeError =
            std::numeric_limits<float>::quiet_NaN();
        invalid.minimumUpdateRate =
            std::numeric_limits<float>::infinity();
        const SampleAccumulationSettings sanitized =
            SanitizeSampleAccumulationSettings(invalid);
        assert(sanitized.preset ==
            SampleAccumulationPreset::VarianceGuided);
        assert(IsValidSampleAccumulationAveraging(sanitized.averaging));
        assert(IsValidSampleAccumulationScheduling(sanitized.scheduling));
        assert(sanitized.effectiveHistory ==
            SampleAccumulationMinimumEffectiveHistory);
        assert(sanitized.minimumSamples ==
            SampleAccumulationMaximumWarmupSamples);
        assert(Near(sanitized.targetRelativeError, 0.02f));
        assert(Near(sanitized.minimumUpdateRate, 1.f / 16.f));
    }

    void TestLinearProgressiveMean()
    {
        const SampleAccumulationSettings settings =
            ApplySampleAccumulationPreset(
                {},
                SampleAccumulationPreset::Progressive);
        SampleAccumulationScalarState state;
        state = AccumulateScalarSample(settings, state, 0.f);
        state = AccumulateScalarSample(settings, state, 1.f);
        assert(state.count == 2u);
        assert(Near(state.mean, 0.5f));
        assert(Near(state.variance, 0.25f));

        state = AccumulateScalarSample(
            settings,
            state,
            std::numeric_limits<float>::quiet_NaN());
        assert(state.count == 2u && Near(state.mean, 0.5f));
    }

    void TestBoundedExponentialMean()
    {
        SampleAccumulationSettings settings =
            ApplySampleAccumulationPreset(
                {},
                SampleAccumulationPreset::Responsive);
        settings.effectiveHistory = 4u;
        assert(settings.preset == SampleAccumulationPreset::Responsive);
        assert(IsSampleAccumulationPresetCustomized(settings));

        SampleAccumulationScalarState state;
        for (uint32_t index = 0u; index < 4u; ++index)
            state = AccumulateScalarSample(settings, state, 0.f);
        state = AccumulateScalarSample(settings, state, 1.f);
        assert(state.count == 5u);
        assert(Near(state.mean, 0.25f));
    }

    void TestVarianceGuidedScheduling()
    {
        const SampleAccumulationSettings settings =
            ApplySampleAccumulationPreset(
                {},
                SampleAccumulationPreset::VarianceGuided);
        assert(Near(
            GetSampleAccumulationUpdateRate(
                settings,
                settings.minimumSamples - 1u,
                1.f,
                0.f),
            1.f));
        assert(Near(
            GetSampleAccumulationUpdateRate(
                settings,
                64u,
                1.f,
                0.f),
            settings.minimumUpdateRate));
        assert(GetSampleAccumulationUpdateInterval(
            settings,
            64u,
            1.f,
            0.f) == 16u);
        assert(Near(
            GetSampleAccumulationUpdateRate(
                settings,
                64u,
                1.f,
                1.f),
            1.f));
        assert(GetSampleAccumulationUpdateInterval(
            settings,
            64u,
            1.f,
            1.f) == 1u);
        assert(Near(
            GetSampleAccumulationUpdateRate(
                settings,
                64u,
                std::numeric_limits<float>::quiet_NaN(),
                0.f),
            1.f));
        SampleAccumulationSettings custom = settings;
        custom.minimumUpdateRate = 0.07f;
        assert(GetSampleAccumulationUpdateInterval(
            custom,
            64u,
            1.f,
            0.f) == 14u);
        assert(1.f / 14.f >= custom.minimumUpdateRate);

        // A fixed interval that is a multiple of the default Halton period
        // must not lock a
        // pixel to one projection-jitter location. Successful-count phase
        // advancement produces an odd revisit stride and covers all 16 phases
        // while never leaving more than 16 committed cycles between samples.
        std::array<bool, 16> visitedJitterPhases{};
        uint32_t successfulCount = 16u;
        uint64_t previousUpdate = 0u;
        bool hasPreviousUpdate = false;
        uint32_t updateCount = 0u;
        for (uint64_t serial = 0u;
            serial < 512u && updateCount < 16u;
            ++serial)
        {
            if (!IsSampleAccumulationUpdateScheduled(
                    serial,
                    0u,
                    16u,
                    successfulCount))
            {
                continue;
            }
            if (hasPreviousUpdate)
                assert(serial - previousUpdate <= 16u);
            visitedJitterPhases[serial % 16u] = true;
            previousUpdate = serial;
            hasPreviousUpdate = true;
            ++successfulCount;
            ++updateCount;
        }
        assert(updateCount == 16u);
        for (const bool visited : visitedJitterPhases)
            assert(visited);

        assert(GetSampleAccumulationSuccessfulUpdatePhase(0u, 2u) == 0u);
        assert(GetSampleAccumulationSuccessfulUpdatePhase(1u, 2u) == 1u);
        assert(GetSampleAccumulationSuccessfulUpdatePhase(2u, 2u) == 1u);
        assert(GetSampleAccumulationSuccessfulUpdatePhase(3u, 2u) == 2u);
        assert(GetSampleAccumulationSuccessfulUpdatePhase(
            std::numeric_limits<uint32_t>::max(), 8u) == 536870912u);
        for (uint32_t samplesPerUpdate = 1u;
            samplesPerUpdate <= 8u;
            ++samplesPerUpdate)
        {
            std::array<bool, 16> pathJitterPhases{};
            uint32_t pathSuccessfulCount = samplesPerUpdate * 16u;
            uint32_t pathUpdateCount = 0u;
            for (uint64_t serial = 0u;
                serial < 512u && pathUpdateCount < 16u;
                ++serial)
            {
                if (!IsSampleAccumulationUpdateScheduled(
                        serial,
                        0u,
                        16u,
                        pathSuccessfulCount,
                        true,
                        samplesPerUpdate))
                {
                    continue;
                }
                pathJitterPhases[serial % 16u] = true;
                pathSuccessfulCount += samplesPerUpdate;
                ++pathUpdateCount;
            }
            assert(pathUpdateCount == 16u);
            for (const bool visited : pathJitterPhases)
                assert(visited);
        }

        // Even one accepted ray from each configured eight-ray update must
        // eventually advance through every camera-coverage phase.
        std::array<bool, 16> partialBatchJitterPhases{};
        uint32_t partialSuccessfulCount = 8u * 16u;
        uint32_t partialUpdateCount = 0u;
        for (uint64_t serial = 0u;
            serial < 4096u && partialUpdateCount < 16u * 8u;
            ++serial)
        {
            if (!IsSampleAccumulationUpdateScheduled(
                    serial,
                    0u,
                    16u,
                    partialSuccessfulCount,
                    true,
                    8u))
            {
                continue;
            }
            partialBatchJitterPhases[serial % 16u] = true;
            ++partialSuccessfulCount;
            ++partialUpdateCount;
        }
        assert(partialUpdateCount == 16u * 8u);
        for (const bool visited : partialBatchJitterPhases)
            assert(visited);

        // The same schedule must retain its original fixed congruence when
        // no camera-coverage jitter is active. This gates the phase advance
        // to the one transport mode that consumes it.
        std::array<bool, 16> disabledJitterPhases{};
        successfulCount = 16u;
        updateCount = 0u;
        for (uint64_t serial = 0u;
            serial < 512u && updateCount < 16u;
            ++serial)
        {
            if (!IsSampleAccumulationUpdateScheduled(
                    serial,
                    0u,
                    16u,
                    successfulCount,
                    false,
                    2u))
            {
                continue;
            }
            disabledJitterPhases[serial % 16u] = true;
            ++successfulCount;
            ++updateCount;
        }
        assert(updateCount == 16u);
        assert(disabledJitterPhases[0]);
        for (size_t phase = 1u; phase < disabledJitterPhases.size(); ++phase)
            assert(!disabledJitterPhases[phase]);

        for (uint32_t interval = 2u; interval <= 256u; ++interval)
        {
            const uint64_t expectedGap = interval -
                GetSampleAccumulationCoverageStep(interval);
            assert((expectedGap & 1u) != 0u);
            uint32_t count = 16u;
            uint64_t previous = 0u;
            bool hasPrevious = false;
            uint32_t accepted = 0u;
            for (uint64_t serial = 0u;
                serial < uint64_t(interval) * 40u && accepted < 33u;
                ++serial)
            {
                if (!IsSampleAccumulationUpdateScheduled(
                        serial, 0u, interval, count))
                {
                    continue;
                }
                if (hasPrevious)
                {
                    const uint64_t gap = serial - previous;
                    assert(gap == expectedGap);
                    assert(gap <= interval);
                }
                previous = serial;
                hasPrevious = true;
                ++count;
                ++accepted;
            }
            assert(accepted == 33u);
        }
        assert(GetSampleAccumulationCoverageStep(3u) == 0u);
        assert(GetSampleAccumulationCoverageStep(5u) == 0u);
    }
}

int main()
{
    TestPresets();
    TestEffectiveHistoryPresets();
    TestAdaptiveWorkloadPresets();
    TestPresetCustomizationAndSanitization();
    TestLinearProgressiveMean();
    TestBoundedExponentialMean();
    TestVarianceGuidedScheduling();
    return 0;
}
