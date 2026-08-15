#include "directional_shadow_settings.h"
#include "ratio_estimator_shared.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace
{
    bool Near(float actual, float expected, float tolerance = 1e-6f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    void AssertRatio(
        RatioEstimatorFloat3 actual,
        RatioEstimatorFloat3 expected,
        float tolerance = 1e-6f)
    {
        assert(Near(actual.x, expected.x, tolerance));
        assert(Near(actual.y, expected.y, tolerance));
        assert(Near(actual.z, expected.z, tolerance));
    }

    struct ReceiverShadowEvidence
    {
        RatioEstimatorFloat3 totalNumeratorMean;
        RatioEstimatorFloat3 totalDenominatorMean;
        RatioEstimatorFloat3 diffuseNumeratorMean;
        RatioEstimatorFloat3 diffuseDenominatorMean;
        RatioEstimatorFloat3 analyticTotalResponse;
        float reverseDepth = 0.f;
        bool hasSurface = false;
        bool useBinaryVisibility = false;
        bool binaryVisible = true;
        bool canReconstruct = true;
        float normalLengthSquared = 1.f;
    };

    struct ResolvedReceiverShadows
    {
        RatioEstimatorFloat3 resolvedModulation =
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
        RatioEstimatorFloat3 closestSourceModulation =
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
        uint32_t validReceiverCount = 0u;
        uint32_t closestReceiverIndex = uint32_t(-1);
    };

    RatioEstimatorFloat3 SaturateRatio(RatioEstimatorFloat3 value)
    {
        return RatioEstimatorMakeFloat3(
            std::clamp(value.x, 0.f, 1.f),
            std::clamp(value.y, 0.f, 1.f),
            std::clamp(value.z, 0.f, 1.f));
    }

    float ResolveDeterministicRatioChannel(
        float numerator,
        float denominator)
    {
        if (!std::isfinite(numerator) || !std::isfinite(denominator) ||
            !(denominator > 0.f))
        {
            return 1.f;
        }
        return std::clamp(numerator / denominator, 0.f, 1.f);
    }

    RatioEstimatorFloat3 ResolveDeterministicRatio(
        RatioEstimatorFloat3 numerator,
        RatioEstimatorFloat3 denominator)
    {
        return RatioEstimatorMakeFloat3(
            ResolveDeterministicRatioChannel(
                numerator.x, denominator.x),
            ResolveDeterministicRatioChannel(
                numerator.y, denominator.y),
            ResolveDeterministicRatioChannel(
                numerator.z, denominator.z));
    }

    RatioEstimatorFloat3 SanitizeNonnegativeResponse(
        RatioEstimatorFloat3 response)
    {
        const auto channel = [](float value)
        {
            return std::isfinite(value) ? std::max(value, 0.f) : 0.f;
        };
        return RatioEstimatorMakeFloat3(
            channel(response.x),
            channel(response.y),
            channel(response.z));
    }

    bool HasValidRawReceiver(const ReceiverShadowEvidence& receiver)
    {
        return receiver.hasSurface &&
            std::isfinite(receiver.reverseDepth) &&
            receiver.reverseDepth > 0.f &&
            receiver.normalLengthSquared > 1e-12f;
    }

    template<std::size_t ReceiverCount>
    ResolvedReceiverShadows ResolveReceiverShadows(
        const std::array<ReceiverShadowEvidence, ReceiverCount>& receivers,
        bool traceAllMsaaReceivers = true,
        float denominatorEpsilon = RatioEstimatorDefaultDenominatorEpsilon)
    {
        ResolvedReceiverShadows result;
        RatioEstimatorFloat3 resolvedNumerator{};
        RatioEstimatorFloat3 resolvedDenominator{};
        RatioEstimatorFloat3 closestTotalModulation =
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
        float closestDepth = 0.f;
        if (!traceAllMsaaReceivers)
        {
            for (uint32_t receiverIndex = 0u;
                receiverIndex < receivers.size();
                ++receiverIndex)
            {
                const ReceiverShadowEvidence& receiver =
                    receivers[receiverIndex];
                if (!HasValidRawReceiver(receiver))
                {
                    continue;
                }
                if (result.closestReceiverIndex == uint32_t(-1) ||
                    receiver.reverseDepth > closestDepth)
                {
                    closestDepth = receiver.reverseDepth;
                    result.closestReceiverIndex = receiverIndex;
                }
            }
        }
        for (uint32_t receiverIndex = 0u;
            receiverIndex < receivers.size();
            ++receiverIndex)
        {
            const ReceiverShadowEvidence& receiver =
                receivers[receiverIndex];
            if (!HasValidRawReceiver(receiver))
            {
                continue;
            }
            if (!traceAllMsaaReceivers &&
                receiverIndex != result.closestReceiverIndex)
            {
                continue;
            }

            const bool ownsClosestSource = !traceAllMsaaReceivers ||
                result.closestReceiverIndex == uint32_t(-1) ||
                receiver.reverseDepth > closestDepth;
            if (traceAllMsaaReceivers && ownsClosestSource)
            {
                closestDepth = receiver.reverseDepth;
                result.closestReceiverIndex = receiverIndex;
                result.closestSourceModulation =
                    RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
            }
            if (!receiver.canReconstruct)
                continue;

            RatioEstimatorFloat3 totalModulation;
            RatioEstimatorFloat3 diffuseModulation;
            if (receiver.useBinaryVisibility)
            {
                const float visibility = receiver.binaryVisible ? 1.f : 0.f;
                totalModulation = RatioEstimatorMakeFloat3(
                    visibility, visibility, visibility);
                diffuseModulation = totalModulation;
            }
            else
            {
                totalModulation = SaturateRatio(
                    ResolveCorrelatedRatio(
                        receiver.totalNumeratorMean,
                        receiver.totalDenominatorMean,
                        denominatorEpsilon));
                diffuseModulation = SaturateRatio(
                    ResolveCorrelatedRatio(
                        receiver.diffuseNumeratorMean,
                        receiver.diffuseDenominatorMean,
                        denominatorEpsilon));
            }
            resolvedNumerator.x +=
                receiver.analyticTotalResponse.x * totalModulation.x;
            resolvedNumerator.y +=
                receiver.analyticTotalResponse.y * totalModulation.y;
            resolvedNumerator.z +=
                receiver.analyticTotalResponse.z * totalModulation.z;
            resolvedDenominator.x += receiver.analyticTotalResponse.x;
            resolvedDenominator.y += receiver.analyticTotalResponse.y;
            resolvedDenominator.z += receiver.analyticTotalResponse.z;

            if (ownsClosestSource)
            {
                closestTotalModulation = totalModulation;
                result.closestSourceModulation = diffuseModulation;
            }
            ++result.validReceiverCount;
        }

        if (result.validReceiverCount == 0u)
            return result;
        if (!traceAllMsaaReceivers)
        {
            result.resolvedModulation = closestTotalModulation;
            return result;
        }

        result.resolvedModulation = ResolveDeterministicRatio(
            resolvedNumerator,
            resolvedDenominator);
        return result;
    }

    template<std::size_t ReceiverCount>
    void TestClosestOnlyShadowBudgetForCount()
    {
        std::array<ReceiverShadowEvidence, ReceiverCount> receivers{};
        for (std::size_t index = 0u; index < receivers.size(); ++index)
        {
            ReceiverShadowEvidence& receiver = receivers[index];
            receiver.totalNumeratorMean =
                RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
            receiver.totalDenominatorMean = receiver.totalNumeratorMean;
            receiver.diffuseNumeratorMean = receiver.totalNumeratorMean;
            receiver.diffuseDenominatorMean = receiver.totalNumeratorMean;
            receiver.analyticTotalResponse = receiver.totalNumeratorMean;
            receiver.reverseDepth = 0.1f + float(index) * 0.01f;
            receiver.hasSurface = true;
        }

        ReceiverShadowEvidence& closest = receivers.back();
        closest.totalNumeratorMean =
            RatioEstimatorMakeFloat3(0.25f, 0.5f, 0.75f);
        closest.diffuseNumeratorMean =
            RatioEstimatorMakeFloat3(0.5f, 0.25f, 0.f);

        const ResolvedReceiverShadows full =
            ResolveReceiverShadows(receivers, true);
        const ResolvedReceiverShadows closestOnly =
            ResolveReceiverShadows(receivers, false);
        assert(full.validReceiverCount == ReceiverCount);
        assert(closestOnly.validReceiverCount == 1u);
        assert(closestOnly.closestReceiverIndex == ReceiverCount - 1u);
        AssertRatio(
            closestOnly.resolvedModulation,
            RatioEstimatorMakeFloat3(0.25f, 0.5f, 0.75f));
        AssertRatio(
            closestOnly.closestSourceModulation,
            RatioEstimatorMakeFloat3(0.5f, 0.25f, 0.f));
    }

    void TestClosestOnlyMsaaShadowBudget()
    {
        TestClosestOnlyShadowBudgetForCount<2>();
        TestClosestOnlyShadowBudgetForCount<4>();
        TestClosestOnlyShadowBudgetForCount<8>();
        TestClosestOnlyShadowBudgetForCount<16>();

        std::array<ReceiverShadowEvidence, 2> receivers{};
        receivers[0].totalNumeratorMean =
            RatioEstimatorMakeFloat3(0.f, 0.f, 0.f);
        receivers[0].totalDenominatorMean =
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
        receivers[0].diffuseNumeratorMean =
            receivers[0].totalNumeratorMean;
        receivers[0].diffuseDenominatorMean =
            receivers[0].totalDenominatorMean;
        receivers[0].analyticTotalResponse =
            receivers[0].totalDenominatorMean;
        receivers[0].reverseDepth = 0.4f;
        receivers[0].hasSurface = true;

        receivers[1].reverseDepth = 0.8f;
        receivers[1].hasSurface = true;
        receivers[1].canReconstruct = false;

        const ResolvedReceiverShadows failedOwner =
            ResolveReceiverShadows(receivers, false);
        assert(failedOwner.closestReceiverIndex == 1u);
        assert(failedOwner.validReceiverCount == 0u);
        AssertRatio(
            failedOwner.resolvedModulation,
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f));
        AssertRatio(
            failedOwner.closestSourceModulation,
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f));

        std::array<ReceiverShadowEvidence, 1> zeroCenterResponse{};
        zeroCenterResponse[0].totalNumeratorMean =
            RatioEstimatorMakeFloat3(0.25f, 0.5f, 0.75f);
        zeroCenterResponse[0].totalDenominatorMean =
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
        zeroCenterResponse[0].diffuseNumeratorMean =
            zeroCenterResponse[0].totalNumeratorMean;
        zeroCenterResponse[0].diffuseDenominatorMean =
            zeroCenterResponse[0].totalDenominatorMean;
        zeroCenterResponse[0].reverseDepth = 0.5f;
        zeroCenterResponse[0].hasSurface = true;
        const ResolvedReceiverShadows directOwner =
            ResolveReceiverShadows(zeroCenterResponse, false);
        AssertRatio(
            directOwner.resolvedModulation,
            RatioEstimatorMakeFloat3(0.25f, 0.5f, 0.75f));
    }

    void TestRgbRatioComposition()
    {
        const RatioEstimatorFloat3 ratio = ResolveCorrelatedRatio(
            RatioEstimatorMakeFloat3(2.f, 3.f, 4.f),
            RatioEstimatorMakeFloat3(4.f, 12.f, 5.f),
            RatioEstimatorDefaultDenominatorEpsilon);
        AssertRatio(
            ratio,
            RatioEstimatorMakeFloat3(0.5f, 0.25f, 0.8f));
    }

    void TestSingleReceiverKeepsDistinctTotalAndDiffuseRatios()
    {
        std::array<ReceiverShadowEvidence, 1> receivers{};
        ReceiverShadowEvidence& receiver = receivers[0];
        receiver.totalNumeratorMean =
            RatioEstimatorMakeFloat3(0.2f, 0.6f, 0.9f);
        receiver.totalDenominatorMean =
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
        receiver.diffuseNumeratorMean =
            RatioEstimatorMakeFloat3(0.8f, 0.3f, 0.1f);
        receiver.diffuseDenominatorMean =
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
        receiver.analyticTotalResponse =
            RatioEstimatorMakeFloat3(2.f, 4.f, 8.f);
        receiver.reverseDepth = 0.5f;
        receiver.hasSurface = true;

        const ResolvedReceiverShadows result =
            ResolveReceiverShadows(receivers);
        AssertRatio(
            result.resolvedModulation,
            RatioEstimatorMakeFloat3(0.2f, 0.6f, 0.9f));
        AssertRatio(
            result.closestSourceModulation,
            RatioEstimatorMakeFloat3(0.8f, 0.3f, 0.1f));
    }

    void TestVisibilityEndpoints()
    {
        const RatioEstimatorFloat3 unshadowed =
            RatioEstimatorMakeFloat3(0.25f, 2.f, 100.f);

        AssertRatio(
            ResolveCorrelatedRatio(
                unshadowed,
                unshadowed,
                RatioEstimatorDefaultDenominatorEpsilon),
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f));
        AssertRatio(
            ResolveCorrelatedRatio(
                RatioEstimatorMakeFloat3(0.f, 0.f, 0.f),
                unshadowed,
                RatioEstimatorDefaultDenominatorEpsilon),
            RatioEstimatorMakeFloat3(0.f, 0.f, 0.f));
    }

    void TestGenericRatioRemainsUnbounded()
    {
        // Shadow composition clamps at its call site. The shared estimator
        // must retain ratios above one for future non-binary consumers.
        AssertRatio(
            ResolveCorrelatedRatio(
                RatioEstimatorMakeFloat3(2.f, 3.f, 4.f),
                RatioEstimatorMakeFloat3(1.f, 1.f, 2.f),
                RatioEstimatorDefaultDenominatorEpsilon),
            RatioEstimatorMakeFloat3(2.f, 3.f, 2.f));
    }

    void TestMixedCorrelatedSamples()
    {
        struct Sample
        {
            RatioEstimatorFloat3 weight;
            bool visible;
        };

        constexpr std::array<Sample, 3> Samples = {{
            { { 1.f, 2.f, 4.f }, true },
            { { 3.f, 2.f, 1.f }, false },
            { { 2.f, 6.f, 5.f }, true }
        }};

        RatioEstimatorFloat3 shadowed{};
        RatioEstimatorFloat3 unshadowed{};
        for (const Sample& sample : Samples)
        {
            unshadowed.x += sample.weight.x;
            unshadowed.y += sample.weight.y;
            unshadowed.z += sample.weight.z;
            if (sample.visible)
            {
                shadowed.x += sample.weight.x;
                shadowed.y += sample.weight.y;
                shadowed.z += sample.weight.z;
            }
        }

        AssertRatio(
            ResolveCorrelatedRatio(
                shadowed,
                unshadowed,
                RatioEstimatorDefaultDenominatorEpsilon),
            RatioEstimatorMakeFloat3(0.5f, 0.8f, 0.9f));
    }

    void TestZeroAndSmallDenominatorsFailOpen()
    {
        assert(ResolveCorrelatedRatioChannel(
            0.f,
            0.f,
            RatioEstimatorDefaultDenominatorEpsilon) == 1.f);
        assert(ResolveCorrelatedRatioChannel(
            1e-6f,
            RatioEstimatorDefaultDenominatorEpsilon * 0.5f,
            RatioEstimatorDefaultDenominatorEpsilon) == 1.f);

        // A non-positive requested epsilon uses the shared default.
        assert(ResolveCorrelatedRatioChannel(
            1e-6f,
            RatioEstimatorDefaultDenominatorEpsilon * 0.5f,
            0.f) == 1.f);
        assert(ResolveCorrelatedRatioChannel(
            1e-6f,
            RatioEstimatorDefaultDenominatorEpsilon * 0.5f,
            -1.f) == 1.f);

        assert(Near(
            ResolveCorrelatedRatioChannel(2.5e-5f, 5e-5f, 1e-6f),
            0.5f));
        assert(Near(
            ResolveCorrelatedRatioChannel(
                RatioEstimatorDefaultDenominatorEpsilon * 0.25f,
                RatioEstimatorDefaultDenominatorEpsilon,
                RatioEstimatorDefaultDenominatorEpsilon),
            0.25f));
    }

    void TestNonFiniteValuesFailOpenPerChannel()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();

        assert(!RatioEstimatorIsFinite(nan));
        assert(!RatioEstimatorIsFinite(infinity));
        assert(RatioEstimatorIsFinite(0.f));

        for (const float invalid : { nan, infinity, -infinity })
        {
            assert(ResolveCorrelatedRatioChannel(
                invalid,
                1.f,
                RatioEstimatorDefaultDenominatorEpsilon) == 1.f);
            assert(ResolveCorrelatedRatioChannel(
                0.5f,
                invalid,
                RatioEstimatorDefaultDenominatorEpsilon) == 1.f);
        }
        assert(ResolveCorrelatedRatioChannel(
            -0.5f,
            1.f,
            RatioEstimatorDefaultDenominatorEpsilon) == 1.f);

        const RatioEstimatorFloat3 ratio = ResolveCorrelatedRatio(
            RatioEstimatorMakeFloat3(0.5f, nan, 0.75f),
            RatioEstimatorMakeFloat3(1.f, 1.f, infinity),
            RatioEstimatorDefaultDenominatorEpsilon);
        AssertRatio(
            ratio,
            RatioEstimatorMakeFloat3(0.5f, 1.f, 1.f));
        assert(std::isfinite(ratio.x));
        assert(std::isfinite(ratio.y));
        assert(std::isfinite(ratio.z));
    }

    void TestDefaultSettings()
    {
        using namespace uvsr;

        const HeitzRatioEstimatorShadowSettings ratioSettings;
        assert(ratioSettings.enabled);
        assert(!ratioSettings.hardShadows);
        assert(ratioSettings.useRatioEstimator);
        assert(!ratioSettings.outputHitDistance);
        assert(ratioSettings.sampleRateLog2 == 1);
        assert(Near(ratioSettings.rayBias, 0.002f));
        assert(ratioSettings.maxDistance ==
            RayVisibilityMaxDistance::Maximum);
        const NoiseSettings defaultNoise;
        assert(!ratioSettings.noise.specifyNoise);
        assert(ratioSettings.noise.custom == defaultNoise);
        assert(IsHeitzRatioEstimatorConfigurationSupported(
            ratioSettings));

        const DirectionalShadowSettings directionalSettings;
        assert(directionalSettings.ratioEstimator.enabled);
        assert(directionalSettings.ratioEstimator.sampleRateLog2 ==
            ratioSettings.sampleRateLog2);
        assert(directionalSettings.ratioEstimator.rayBias ==
            ratioSettings.rayBias);

        HeitzRatioEstimatorShadowSettings scalarSettings = ratioSettings;
        scalarSettings.sampleRateLog2 = 6;
        scalarSettings.useRatioEstimator = false;
        assert(ResolveHeitzShadowTraceCount(
            scalarSettings, true) == 1u);
        scalarSettings.useRatioEstimator = true;
        assert(ResolveHeitzShadowTraceCount(
            scalarSettings, true) == 64u);
        assert(ResolveHeitzShadowTraceCount(
            scalarSettings, false) == 1u);
        assert(HeitzShadowHitDistanceInvalid == 0.f);
        assert(HeitzShadowHitDistanceMaximum == 65472.f);
        assert(HeitzShadowHitDistanceMiss == 65504.f);

        HeitzRatioEstimatorShadowSettings invalid = ratioSettings;
        invalid.sampleRateLog2 = -1;
        assert(!IsHeitzRatioEstimatorConfigurationSupported(invalid));
        invalid = ratioSettings;
        invalid.sampleRateLog2 = 7;
        assert(!IsHeitzRatioEstimatorConfigurationSupported(invalid));
        invalid = ratioSettings;
        invalid.rayBias = -0.001f;
        assert(!IsHeitzRatioEstimatorConfigurationSupported(invalid));
        invalid.rayBias = HeitzRatioEstimatorMaximumRayBias + 0.001f;
        assert(!IsHeitzRatioEstimatorConfigurationSupported(invalid));
        invalid.rayBias = std::numeric_limits<float>::quiet_NaN();
        assert(!IsHeitzRatioEstimatorConfigurationSupported(invalid));
        invalid = ratioSettings;
        invalid.noise.custom.pattern = NoisePattern::Count;
        assert(!IsHeitzRatioEstimatorConfigurationSupported(invalid));
        invalid = ratioSettings;
        invalid.noise.custom.resolution =
            static_cast<NoiseResolution>(0u);
        assert(!IsHeitzRatioEstimatorConfigurationSupported(invalid));
        invalid = ratioSettings;
        invalid.maxDistance = RayVisibilityMaxDistance::Count;
        assert(!IsHeitzRatioEstimatorConfigurationSupported(invalid));
        invalid.maxDistance =
            static_cast<RayVisibilityMaxDistance>(-1);
        assert(!IsHeitzRatioEstimatorConfigurationSupported(invalid));

        constexpr std::array<std::string_view, 7> ExpectedLabels = {
            "1", "2", "4", "8", "16", "32", "64"
        };
        assert(HeitzRatioEstimatorSampleRateLabels == ExpectedLabels);
        for (int32_t exponent = 0; exponent <= 6; ++exponent)
        {
            assert(IsHeitzRatioEstimatorSampleRateSupported(exponent));
            assert(GetHeitzRatioEstimatorSampleRateLabel(exponent) ==
                ExpectedLabels[size_t(exponent)]);
            assert(ResolveHeitzRatioEstimatorSampleCount(exponent) ==
                (1u << uint32_t(exponent)));
        }
        assert(!IsHeitzRatioEstimatorSampleRateSupported(-1));
        assert(!IsHeitzRatioEstimatorSampleRateSupported(7));
    }

    void TestDivisionAfterMatchedCurrentFrameAccumulation()
    {
        const RatioEstimatorFloat3 first =
            RatioEstimatorMakeFloat3(1.f, 2.f, 4.f);
        const RatioEstimatorFloat3 second =
            RatioEstimatorMakeFloat3(9.f, 8.f, 6.f);
        const RatioEstimatorFloat3 matchedNumeratorMean =
            RatioEstimatorMakeFloat3(
                first.x * 0.5f,
                first.y * 0.5f,
                first.z * 0.5f);
        const RatioEstimatorFloat3 matchedDenominatorMean =
            RatioEstimatorMakeFloat3(
                (first.x + second.x) * 0.5f,
                (first.y + second.y) * 0.5f,
                (first.z + second.z) * 0.5f);
        AssertRatio(
            ResolveCorrelatedRatio(
                matchedNumeratorMean,
                matchedDenominatorMean,
                RatioEstimatorDefaultDenominatorEpsilon),
            RatioEstimatorMakeFloat3(0.1f, 0.2f, 0.4f));

        // Averaging already-divided per-sample visibility would incorrectly
        // yield 0.5 in every channel. Accumulate matched current-frame S/U
        // evidence first, normalize both by N, and divide only once.
        assert(!Near(0.5f, 0.1f));
    }

    void TestAnalyticWeightedMsaaReceiverResolution()
    {
        constexpr std::array<ReceiverShadowEvidence, 2> Receivers = {{
            {
                { 1.f, 0.f, 0.f },
                { 1.f, 1.f, 0.f },
                { 0.25f, 0.f, 0.f },
                { 0.25f, 0.5f, 0.f },
                { 10.f, 2.f, 0.f },
                0.4f,
                true
            },
            {
                { 0.f, 3.f, 0.f },
                { 3.f, 3.f, 0.f },
                { 0.f, 0.5f, 0.f },
                { 1.f, 1.f, 0.f },
                { 1.f, 8.f, 4.f },
                0.8f,
                true
            }
        }};
        const ResolvedReceiverShadows result =
            ResolveReceiverShadows(Receivers);
        AssertRatio(
            result.resolvedModulation,
            RatioEstimatorMakeFloat3(10.f / 11.f, 0.8f, 1.f));
        AssertRatio(
            result.closestSourceModulation,
            RatioEstimatorMakeFloat3(0.f, 0.5f, 1.f));
        assert(result.validReceiverCount == 2u);
        assert(result.closestReceiverIndex == 1u);

        const float resolvedRedEnergy =
            (Receivers[0].analyticTotalResponse.x +
                Receivers[1].analyticTotalResponse.x) *
            result.resolvedModulation.x;
        assert(Near(resolvedRedEnergy, 10.f));

        // Pooling S/U across receivers produces 2.75 for the same red-channel
        // center responses. Independent receiver ratios followed by analytic
        // weighting reproduce the per-receiver reference energy of 10.
        const float pooledRatio = 1.f / 4.f;
        assert(Near(pooledRatio * 11.f, 2.75f));
        assert(!Near(pooledRatio * 11.f, resolvedRedEnergy));
    }

    void TestPerReceiverEmitterNormalizationKeepsTheEpsilonStable()
    {
        constexpr float Response = 1.5e-4f;
        const RatioEstimatorFloat3 coveredRatio = ResolveCorrelatedRatio(
            RatioEstimatorMakeFloat3(
                Response * 0.5f,
                Response * 0.5f,
                Response * 0.5f),
            RatioEstimatorMakeFloat3(
                Response,
                Response,
                Response),
            RatioEstimatorDefaultDenominatorEpsilon);
        AssertRatio(
            coveredRatio,
            RatioEstimatorMakeFloat3(0.5f, 0.5f, 0.5f));

        // Dividing the same evidence by configured 8x MSAA instead of its one
        // valid covered receiver pushes U below the fixed guard and fails open.
        const float wrongConfiguredSampleNormalization = 1.f / 16.f;
        const float wrongRatio = ResolveCorrelatedRatioChannel(
            Response * wrongConfiguredSampleNormalization,
            (Response * 2.f) * wrongConfiguredSampleNormalization,
            RatioEstimatorDefaultDenominatorEpsilon);
        assert(wrongRatio == 1.f);
        assert(!Near(wrongRatio, coveredRatio.x));
    }

    void TestDeterministicAggregationPreservesLowEnergyRatios()
    {
        std::array<ReceiverShadowEvidence, 16> receivers{};
        ReceiverShadowEvidence& illuminated = receivers[0];
        illuminated.totalNumeratorMean =
            RatioEstimatorMakeFloat3(0.f, 5e-4f, 2.5e-4f);
        illuminated.totalDenominatorMean =
            RatioEstimatorMakeFloat3(1e-3f, 1e-3f, 1e-3f);
        illuminated.diffuseNumeratorMean =
            illuminated.totalNumeratorMean;
        illuminated.diffuseDenominatorMean =
            illuminated.totalDenominatorMean;
        illuminated.analyticTotalResponse =
            RatioEstimatorMakeFloat3(1.5e-4f, 3e-5f, 2e-4f);
        illuminated.reverseDepth = 0.75f;
        illuminated.hasSurface = true;

        for (std::size_t index = 1u; index < receivers.size(); ++index)
        {
            receivers[index].reverseDepth = 0.25f;
            receivers[index].hasSurface = true;
        }

        const ResolvedReceiverShadows result =
            ResolveReceiverShadows(receivers);
        AssertRatio(
            result.resolvedModulation,
            RatioEstimatorMakeFloat3(0.f, 0.5f, 0.25f));
        assert(result.validReceiverCount == receivers.size());

        // The stochastic guard remains correct for each local S/U estimate,
        // but applying it again after averaging sixteen analytic responses
        // would fail every low-energy channel open to white.
        assert(ResolveCorrelatedRatioChannel(
            0.f,
            illuminated.analyticTotalResponse.x / 16.f,
            RatioEstimatorDefaultDenominatorEpsilon) == 1.f);
    }

    void TestHardVisibilityBypassesTheStochasticResponseGuard()
    {
        std::array<ReceiverShadowEvidence, 1> receivers{};
        ReceiverShadowEvidence& receiver = receivers[0];
        receiver.analyticTotalResponse =
            RatioEstimatorMakeFloat3(1e-6f, 5e-6f, 2e-5f);
        receiver.reverseDepth = 0.5f;
        receiver.hasSurface = true;
        receiver.useBinaryVisibility = true;
        receiver.binaryVisible = false;

        const ResolvedReceiverShadows result =
            ResolveReceiverShadows(receivers);
        AssertRatio(
            result.resolvedModulation,
            RatioEstimatorMakeFloat3(0.f, 0.f, 0.f));
        AssertRatio(
            result.closestSourceModulation,
            RatioEstimatorMakeFloat3(0.f, 0.f, 0.f));
    }

    void TestClosestOwnerFailureCannotReuseAnOlderShadow()
    {
        std::array<ReceiverShadowEvidence, 2> receivers{};
        ReceiverShadowEvidence& older = receivers[0];
        older.totalDenominatorMean =
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
        older.diffuseDenominatorMean = older.totalDenominatorMean;
        older.analyticTotalResponse = older.totalDenominatorMean;
        older.reverseDepth = 0.4f;
        older.hasSurface = true;

        ReceiverShadowEvidence& newer = receivers[1];
        newer.reverseDepth = 0.8f;
        newer.hasSurface = true;
        newer.canReconstruct = false;

        const ResolvedReceiverShadows result =
            ResolveReceiverShadows(receivers);
        assert(result.validReceiverCount == 1u);
        assert(result.closestReceiverIndex == 1u);
        AssertRatio(
            result.closestSourceModulation,
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f));
    }

    void TestResponseSanitizationIsPerChannel()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const RatioEstimatorFloat3 sanitized =
            SanitizeNonnegativeResponse(
                RatioEstimatorMakeFloat3(0.25f, nan, 0.75f));
        AssertRatio(
            sanitized,
            RatioEstimatorMakeFloat3(0.25f, 0.f, 0.75f));
    }

    template<std::size_t ReceiverCount>
    void TestSparseCoverageForCount()
    {
        std::array<ReceiverShadowEvidence, ReceiverCount> receivers{};
        ReceiverShadowEvidence& covered = receivers[ReceiverCount - 1u];
        covered.totalNumeratorMean =
            RatioEstimatorMakeFloat3(0.25f, 0.5f, 0.75f);
        covered.totalDenominatorMean =
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
        covered.diffuseNumeratorMean = covered.totalNumeratorMean;
        covered.diffuseDenominatorMean = covered.totalDenominatorMean;
        covered.analyticTotalResponse =
            RatioEstimatorMakeFloat3(2.f, 2.f, 2.f);
        covered.reverseDepth = 0.5f;
        covered.hasSurface = true;
        const ResolvedReceiverShadows result =
            ResolveReceiverShadows(receivers);
        const ResolvedReceiverShadows closestOnly =
            ResolveReceiverShadows(receivers, false);
        AssertRatio(
            result.resolvedModulation,
            RatioEstimatorMakeFloat3(0.25f, 0.5f, 0.75f));
        AssertRatio(
            result.closestSourceModulation,
            result.resolvedModulation);
        AssertRatio(
            closestOnly.resolvedModulation,
            result.resolvedModulation);
        AssertRatio(
            closestOnly.closestSourceModulation,
            result.closestSourceModulation);
        assert(result.validReceiverCount == 1u);
        assert(result.closestReceiverIndex == ReceiverCount - 1u);
        assert(closestOnly.validReceiverCount == 1u);
        assert(closestOnly.closestReceiverIndex == ReceiverCount - 1u);
    }

    void TestSparseCoverageAndClosestOwnerRules()
    {
        TestSparseCoverageForCount<2>();
        TestSparseCoverageForCount<4>();
        TestSparseCoverageForCount<8>();
        TestSparseCoverageForCount<16>();

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        std::array<ReceiverShadowEvidence, 8> receivers{};
        for (ReceiverShadowEvidence& receiver : receivers)
        {
            receiver.totalNumeratorMean =
                RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
            receiver.totalDenominatorMean =
                RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
            receiver.diffuseNumeratorMean =
                receiver.totalNumeratorMean;
            receiver.diffuseDenominatorMean =
                receiver.totalDenominatorMean;
            receiver.analyticTotalResponse =
                RatioEstimatorMakeFloat3(1.f, 1.f, 1.f);
            receiver.hasSurface = true;
        }
        receivers[0].reverseDepth = nan;
        receivers[1].reverseDepth = 0.f;
        receivers[2].reverseDepth = infinity;
        receivers[3].reverseDepth = 0.8f;
        receivers[3].normalLengthSquared = 0.f;
        receivers[4].reverseDepth = 0.9f;
        receivers[4].normalLengthSquared = nan;
        receivers[5].reverseDepth = 0.75f;
        receivers[6].reverseDepth = 0.75f;
        receivers[7].hasSurface = false;
        receivers[5].diffuseNumeratorMean =
            RatioEstimatorMakeFloat3(0.f, 0.f, 0.f);
        const ResolvedReceiverShadows result =
            ResolveReceiverShadows(receivers);
        assert(result.validReceiverCount == 2u);
        assert(result.closestReceiverIndex == 5u);
        AssertRatio(
            result.closestSourceModulation,
            RatioEstimatorMakeFloat3(0.f, 0.f, 0.f));
        const ResolvedReceiverShadows closestOnly =
            ResolveReceiverShadows(receivers, false);
        assert(closestOnly.validReceiverCount == 1u);
        assert(closestOnly.closestReceiverIndex == 5u);
        AssertRatio(
            closestOnly.closestSourceModulation,
            RatioEstimatorMakeFloat3(0.f, 0.f, 0.f));

        constexpr std::array<ReceiverShadowEvidence, 2> NoCoverage{};
        const ResolvedReceiverShadows empty =
            ResolveReceiverShadows(NoCoverage);
        const ResolvedReceiverShadows closestOnlyEmpty =
            ResolveReceiverShadows(NoCoverage, false);
        AssertRatio(
            empty.resolvedModulation,
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f));
        AssertRatio(
            empty.closestSourceModulation,
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f));
        AssertRatio(
            closestOnlyEmpty.resolvedModulation,
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f));
        AssertRatio(
            closestOnlyEmpty.closestSourceModulation,
            RatioEstimatorMakeFloat3(1.f, 1.f, 1.f));
    }

    void TestGeometricNormalRayBias()
    {
        constexpr float ReconstructionError = 0.001f;
        constexpr float GrazingCosine = 0.005f;
        constexpr float UserBias = 0.002f;
        constexpr float DepthStepDistance = 0.0005f;

        // An along-ray TMin must grow without bound as N.L approaches zero.
        const float samePlaneHitDistance =
            ReconstructionError / GrazingCosine;
        assert(Near(samePlaneHitDistance, 0.2f));
        assert(samePlaneHitDistance > 0.1f);

        // A true face-normal displacement establishes the requested exterior
        // plane clearance independent of the outgoing ray angle.
        const float clearance = std::max(UserBias, DepthStepDistance);
        assert(clearance >= ReconstructionError);
        assert(Near(clearance, UserBias));

        // An interpolated normal nearly tangent to the triangle would project
        // the same numeric bias to an ineffective plane clearance.
        constexpr float InterpolatedToFaceCosine = 0.01f;
        assert(UserBias * InterpolatedToFaceCosine < ReconstructionError);
    }

    void TestNoiseInheritanceAndValidation()
    {
        using namespace uvsr;

        NoiseSettings global;
        global.pattern = NoisePattern::SpatialWhite;
        global.resolution = NoiseResolution::Size512;
        global.animate = false;

        HeitzRatioEstimatorShadowSettings settings;
        assert(!settings.noise.specifyNoise);
        assert(ResolveNoiseSettings(global, settings.noise) == global);

        settings.noise.custom.pattern = NoisePattern::SpatialBlue;
        settings.noise.custom.resolution = NoiseResolution::Size64;
        settings.noise.custom.animate = true;
        assert(ResolveNoiseSettings(global, settings.noise) == global);

        settings.noise.specifyNoise = true;
        assert(ResolveNoiseSettings(global, settings.noise) ==
            settings.noise.custom);
        assert(ResolveNoiseSettings(global, settings.noise) != global);
        assert(IsHeitzRatioEstimatorConfigurationSupported(settings));

        settings.noise.custom.pattern = NoisePattern::Count;
        assert(!IsHeitzRatioEstimatorConfigurationSupported(settings));
    }

    void TestHardReceiverEligibility()
    {
        constexpr float MinimumCosine = 1e-5f;
        const auto eligible = [=](float geometricNoV,
            float geometricNoL,
            float shadingNoV,
            float shadingNoL)
        {
            return geometricNoV > MinimumCosine &&
                geometricNoL > MinimumCosine &&
                shadingNoV > MinimumCosine &&
                shadingNoL > MinimumCosine;
        };

        assert(eligible(1.f, 1.f, 1.f, 1.f));
        assert(!eligible(0.f, 1.f, 1.f, 1.f));
        assert(!eligible(1.f, 0.f, 1.f, 1.f));
        assert(!eligible(1.f, 1.f, 0.f, 1.f));
        assert(!eligible(1.f, 1.f, 1.f, 0.f));
        assert(!eligible(MinimumCosine, 1.f, 1.f, 1.f));
        assert(!eligible(1.f, MinimumCosine, 1.f, 1.f));
        assert(!eligible(1.f, 1.f, MinimumCosine, 1.f));
        assert(!eligible(1.f, 1.f, 1.f, MinimumCosine));
    }

}

int main()
{
    TestRgbRatioComposition();
    TestSingleReceiverKeepsDistinctTotalAndDiffuseRatios();
    TestVisibilityEndpoints();
    TestGenericRatioRemainsUnbounded();
    TestMixedCorrelatedSamples();
    TestZeroAndSmallDenominatorsFailOpen();
    TestNonFiniteValuesFailOpenPerChannel();
    TestDefaultSettings();
    TestDivisionAfterMatchedCurrentFrameAccumulation();
    TestAnalyticWeightedMsaaReceiverResolution();
    TestClosestOnlyMsaaShadowBudget();
    TestPerReceiverEmitterNormalizationKeepsTheEpsilonStable();
    TestDeterministicAggregationPreservesLowEnergyRatios();
    TestHardVisibilityBypassesTheStochasticResponseGuard();
    TestClosestOwnerFailureCannotReuseAnOlderShadow();
    TestResponseSanitizationIsPerChannel();
    TestSparseCoverageAndClosestOwnerRules();
    TestGeometricNormalRayBias();
    TestNoiseInheritanceAndValidation();
    TestHardReceiverEligibility();
    return 0;
}
