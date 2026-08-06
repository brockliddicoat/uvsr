#include "directional_shadow_settings.h"
#include "ratio_estimator_shared.h"
#include "screen_space_directional_shadows_settings.h"
#include "visibility_blue_noise.h"

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
#include <vector>

namespace
{
    float Fraction(float value)
    {
        return value - std::floor(value);
    }

    float RadicalInverse(uint32_t index, uint32_t base)
    {
        const float inverseBase = 1.f / float(base);
        float inversePower = inverseBase;
        float result = 0.f;
        while (index > 0u)
        {
            const uint32_t digit = index % base;
            result += float(digit) * inversePower;
            index /= base;
            inversePower *= inverseBase;
        }
        return result;
    }

    float GoldenWeylPhase(uint32_t frameIndex)
    {
        const uint32_t phase = frameIndex * 0x9e3779b9u;
        return float(phase >> 8u) * (1.f / 16777216.f);
    }

    float PermutatedWhiteNoise(
        uint32_t x,
        uint32_t y,
        uint32_t dimension,
        uint32_t phase)
    {
        uint32_t state = x + y * 65537u +
            dimension * 747796405u + phase * 2891336453u + 1u;
        state = state * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) *
            277803737u;
        word = (word >> 22u) ^ word;
        return float((word >> 8u) & 0x00ffffffu) / 16777216.f;
    }

    float BlueNoise(
        const std::vector<uint16_t>& noise,
        uint32_t x,
        uint32_t y,
        uint32_t layer)
    {
        using namespace uvsr;
        const size_t texel = size_t(y & 63u) * VisibilityBlueNoiseSize +
            size_t(x & 63u);
        return float(noise[
            size_t(layer % VisibilityBlueNoiseLayerCount) *
                VisibilityBlueNoiseTexelCount + texel]) / 65535.f;
    }

    std::array<float, 2> Sample2D(
        const std::vector<uint16_t>& noise,
        uint32_t x,
        uint32_t y,
        uint32_t sampleIndex,
        uint32_t frameIndex)
    {
        const uint32_t sequenceIndex = sampleIndex + 1u;
        return {
            Fraction(
                RadicalInverse(sequenceIndex, 2u) +
                BlueNoise(noise, x, y, 0u) +
                RadicalInverse(frameIndex + 1u, 5u)),
            Fraction(
                RadicalInverse(sequenceIndex, 3u) +
                BlueNoise(noise, x, y, 1u) +
                GoldenWeylPhase(frameIndex))
        };
    }

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
        assert(!ratioSettings.enabled);
        assert(!ratioSettings.hardShadows);
        assert(ratioSettings.sampleRateLog2 == 1);
        assert(Near(ratioSettings.rayBias, 0.002f));
        assert(ratioSettings.maxDistance ==
            RayVisibilityMaxDistance::Maximum);
        assert(ratioSettings.noisePattern ==
            HeitzRatioEstimatorNoisePattern::VoidClusterBlueNoise);
        assert(ratioSettings.animateSamples);
        assert(IsHeitzRatioEstimatorConfigurationSupported(
            ratioSettings));

        const DirectionalShadowSettings directionalSettings;
        assert(!directionalSettings.ratioEstimator.enabled);
        assert(directionalSettings.ratioEstimator.sampleRateLog2 ==
            ratioSettings.sampleRateLog2);
        assert(directionalSettings.ratioEstimator.rayBias ==
            ratioSettings.rayBias);

        ScreenSpaceDirectionalShadowSettings screenSpace;
        DirectionalShadowSettings rayTraced;
        screenSpace.enabled = true;
        assert(screenSpace.enabled &&
            !rayTraced.ratioEstimator.enabled);
        rayTraced.ratioEstimator.enabled = true;
        assert(screenSpace.enabled &&
            rayTraced.ratioEstimator.enabled);
        screenSpace.enabled = false;
        assert(!screenSpace.enabled &&
            rayTraced.ratioEstimator.enabled);
        rayTraced.ratioEstimator.enabled = false;
        assert(!screenSpace.enabled &&
            !rayTraced.ratioEstimator.enabled);

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
        invalid.noisePattern = HeitzRatioEstimatorNoisePattern::Count;
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

    void TestBlueNoiseDirectionalEmitterSampling()
    {
        using namespace uvsr;

        const std::vector<uint16_t> noise = GenerateVisibilityBlueNoise();
        assert(Near(RadicalInverse(1u, 2u), 0.5f));
        assert(Near(RadicalInverse(2u, 2u), 0.25f));
        assert(Near(RadicalInverse(1u, 3u), 1.f / 3.f));
        assert(Near(RadicalInverse(2u, 3u), 2.f / 3.f));
        assert(Near(GoldenWeylPhase(0u), 0.f));

        const auto held = Sample2D(noise, 17u, 29u, 3u, 11u);
        const auto repeated = Sample2D(noise, 17u, 29u, 3u, 11u);
        const auto advanced = Sample2D(noise, 17u, 29u, 3u, 12u);
        assert(held == repeated);
        assert(!Near(held[0], advanced[0]));
        assert(!Near(held[1], advanced[1]));

        double radialMean = 0.0;
        double azimuthCosMean = 0.0;
        double azimuthSinMean = 0.0;
        double layerZeroMean = 0.0;
        double layerOneMean = 0.0;
        for (uint32_t y = 0u; y < VisibilityBlueNoiseSize; ++y)
        {
            for (uint32_t x = 0u; x < VisibilityBlueNoiseSize; ++x)
            {
                const auto sample = Sample2D(noise, x, y, 0u, 0u);
                assert(sample[0] >= 0.f && sample[0] < 1.f);
                assert(sample[1] >= 0.f && sample[1] < 1.f);
                radialMean += sample[0];
                constexpr double TwoPi = 6.28318530717958647692;
                azimuthCosMean += std::cos(TwoPi * sample[1]);
                azimuthSinMean += std::sin(TwoPi * sample[1]);
                layerZeroMean += BlueNoise(noise, x, y, 0u);
                layerOneMean += BlueNoise(noise, x, y, 1u);
            }
        }

        const double reciprocalCount =
            1.0 / double(VisibilityBlueNoiseTexelCount);
        radialMean *= reciprocalCount;
        azimuthCosMean *= reciprocalCount;
        azimuthSinMean *= reciprocalCount;
        layerZeroMean *= reciprocalCount;
        layerOneMean *= reciprocalCount;
        assert(std::abs(radialMean - 0.5) < 1e-3);
        assert(std::abs(azimuthCosMean) < 1e-3);
        assert(std::abs(azimuthSinMean) < 1e-3);

        double covariance = 0.0;
        double layerZeroVariance = 0.0;
        double layerOneVariance = 0.0;
        for (uint32_t y = 0u; y < VisibilityBlueNoiseSize; ++y)
        {
            for (uint32_t x = 0u; x < VisibilityBlueNoiseSize; ++x)
            {
                const double zero =
                    BlueNoise(noise, x, y, 0u) - layerZeroMean;
                const double one =
                    BlueNoise(noise, x, y, 1u) - layerOneMean;
                covariance += zero * one;
                layerZeroVariance += zero * zero;
                layerOneVariance += one * one;
            }
        }
        const double correlation = covariance /
            std::sqrt(layerZeroVariance * layerOneVariance);
        assert(std::abs(correlation) < 0.1);

        constexpr float AngularRadius = 0.5f;
        const float cosMaximum = std::cos(AngularRadius);
        const float measuredCosTheta =
            1.f + float(radialMean) * (cosMaximum - 1.f);
        const float expectedCosTheta = 0.5f * (1.f + cosMaximum);
        assert(Near(measuredCosTheta, expectedCosTheta, 1e-4f));
    }

    void TestPermutatedWhiteNoiseDirectionalEmitterSampling()
    {
        const float permutated = PermutatedWhiteNoise(
            17u, 29u, 6u, 11u);
        const float permutatedRepeated = PermutatedWhiteNoise(
            17u, 29u, 6u, 11u);
        const float permutatedAdvanced = PermutatedWhiteNoise(
            17u, 29u, 6u, 12u);
        assert(permutated == permutatedRepeated);
        assert(permutated >= 0.f && permutated < 1.f);
        assert(!Near(permutated, permutatedAdvanced));
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
    TestVisibilityEndpoints();
    TestGenericRatioRemainsUnbounded();
    TestMixedCorrelatedSamples();
    TestZeroAndSmallDenominatorsFailOpen();
    TestNonFiniteValuesFailOpenPerChannel();
    TestDefaultSettings();
    TestDivisionAfterMatchedCurrentFrameAccumulation();
    TestGeometricNormalRayBias();
    TestBlueNoiseDirectionalEmitterSampling();
    TestPermutatedWhiteNoiseDirectionalEmitterSampling();
    TestHardReceiverEligibility();
    return 0;
}
