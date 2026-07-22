#include "bend_screen_space_shadows.h"
#include "bend_sss_cpu.h"

#include <cassert>
#include <cmath>
#include <cstdint>

using namespace uvsr;

namespace
{
    void AssertBendExact(
        const BendScreenSpaceShadowSettings& settings,
        bool expectedEnabled)
    {
        assert(settings.enabled == expectedEnabled);
        assert(settings.preset == BendShadowPreset::BendExact);
        assert(settings.length == BendShadowLength::Pixels60);
        assert(settings.hardShadowSamples == 4u);
        assert(settings.fadeOutSamples == 8u);
        assert(std::abs(settings.surfaceThickness - 0.005f) < 1e-8f);
        assert(std::abs(settings.bilinearThreshold - 0.02f) < 1e-8f);
        assert(settings.shadowContrast == 4.f);
        assert(!settings.ignoreEdgePixels);
        assert(!settings.usePrecisionOffset);
        assert(!settings.bilinearSamplingOffsetMode);
        assert(!settings.useEarlyOut);
        assert(settings.debugView == BendShadowDebugView::None);
        assert(IsBendShadowVariantCompiled(settings));
    }

    void TestBendExactDefaults()
    {
        AssertBendExact(BendScreenSpaceShadowSettings{}, false);
    }

    void TestPresetResetAndEnableIndependence()
    {
        BendScreenSpaceShadowSettings settings;
        settings.enabled = true;
        settings.length = BendShadowLength::Pixels480;
        settings.surfaceThickness = 0.04f;
        settings.bilinearThreshold = 0.09f;
        settings.shadowContrast = 12.f;
        settings.hardShadowSamples = 8u;
        settings.fadeOutSamples = 16u;
        settings.ignoreEdgePixels = true;
        settings.usePrecisionOffset = true;
        settings.bilinearSamplingOffsetMode = true;
        settings.useEarlyOut = true;
        settings.debugView = BendShadowDebugView::Wave;

        ApplyBendShadowPreset(settings, BendShadowPreset::BendExact);
        AssertBendExact(settings, true);
    }

    void TestLengthPresetsAndCompiledVariants()
    {
        BendScreenSpaceShadowSettings settings;
        ApplyBendShadowPreset(settings, BendShadowPreset::Long);
        assert(settings.length == BendShadowLength::Pixels240);
        assert(settings.preset == BendShadowPreset::Long);

        ApplyBendShadowPreset(
            settings,
            BendShadowPreset::MaximumValidation);
        assert(settings.length == BendShadowLength::Pixels960);
        assert(settings.preset ==
            BendShadowPreset::MaximumValidation);

        for (uint32_t sampleCount : BendShadowSampleCounts)
        {
            settings.length = BendShadowLength(sampleCount);
            for (uint32_t hard : BendShadowHardSampleCounts)
            {
                settings.hardShadowSamples = hard;
                for (uint32_t fade : BendShadowFadeSampleCounts)
                {
                    settings.fadeOutSamples = fade;
                    assert(IsBendShadowVariantCompiled(settings));
                }
            }
        }

        settings.hardShadowSamples = 3u;
        assert(!IsBendShadowVariantCompiled(settings));
    }

    void TestReleasedCpuDispatchContract()
    {
        int viewport[2] = { 1920, 1080 };
        int minimum[2] = { 0, 0 };
        int maximum[2] = { 1920, 1080 };

        for (float w : { 1.f, -1.f })
        {
            float projectedLight[4] = {
                0.25f, -0.4f, 0.5f, w
            };
            const Bend::DispatchList list = Bend::BuildDispatchList(
                projectedLight,
                viewport,
                minimum,
                maximum,
                false,
                64);
            assert(list.DispatchCount >= 1);
            assert(list.DispatchCount <= 8);
            assert(list.LightCoordinate_Shader[3] ==
                (w > 0.f ? 1.f : -1.f));
            for (int index = 0; index < list.DispatchCount; ++index)
            {
                assert(list.Dispatch[index].WaveCount[0] > 0);
                assert(list.Dispatch[index].WaveCount[1] > 0);
                assert(list.Dispatch[index].WaveCount[2] > 0);
            }
        }
    }
}

int main()
{
    TestBendExactDefaults();
    TestPresetResetAndEnableIndependence();
    TestLengthPresetsAndCompiledVariants();
    TestReleasedCpuDispatchContract();
    return 0;
}
