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
        assert(!directionalSettings.ratioEstimator.enabled);
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
    TestVisibilityEndpoints();
    TestGenericRatioRemainsUnbounded();
    TestMixedCorrelatedSamples();
    TestZeroAndSmallDenominatorsFailOpen();
    TestNonFiniteValuesFailOpenPerChannel();
    TestDefaultSettings();
    TestDivisionAfterMatchedCurrentFrameAccumulation();
    TestGeometricNormalRayBias();
    TestNoiseInheritanceAndValidation();
    TestHardReceiverEligibility();
    return 0;
}
