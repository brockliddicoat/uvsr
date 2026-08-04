#include "temporal_aa_reference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

namespace
{
    bool NearlyEqual(float actual, float expected, float epsilon = 1e-6f)
    {
        return std::abs(actual - expected) <= epsilon;
    }

    bool Check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

    bool IsSobolNetPrefix(uint32_t count)
    {
        uint32_t exponent = 0u;
        while ((1u << exponent) < count)
            ++exponent;
        if ((1u << exponent) != count)
            return false;

        for (uint32_t xExponent = 0u;
            xExponent <= exponent;
            ++xExponent)
        {
            const uint32_t xCells = 1u << xExponent;
            const uint32_t yCells = 1u << (exponent - xExponent);
            std::array<uint32_t, 32> occupancy{};
            for (uint32_t index = 0u; index < count; ++index)
            {
                const auto sample = uvsr::GetTemporalAaJitter(
                    uvsr::TemporalAaJitterSequence::Sobol32,
                    index);
                const uint32_t x = std::min(
                    static_cast<uint32_t>((sample.x + 0.5f) * xCells),
                    xCells - 1u);
                const uint32_t y = std::min(
                    static_cast<uint32_t>((sample.y + 0.5f) * yCells),
                    yCells - 1u);
                ++occupancy[y * xCells + x];
            }
            for (uint32_t index = 0u; index < count; ++index)
            {
                if (occupancy[index] != 1u)
                    return false;
            }
        }
        return true;
    }

    double GetToroidalMinimumDistance(
        uvsr::TemporalAaJitterSequence sequence,
        uint32_t count)
    {
        double minimum = std::numeric_limits<double>::max();
        for (uint32_t first = 0u; first < count; ++first)
        {
            const auto firstSample =
                uvsr::GetTemporalAaJitter(sequence, first);
            for (uint32_t second = 0u; second < first; ++second)
            {
                const auto secondSample =
                    uvsr::GetTemporalAaJitter(sequence, second);
                double x = std::abs(
                    double(firstSample.x) - double(secondSample.x));
                double y = std::abs(
                    double(firstSample.y) - double(secondSample.y));
                x = std::min(x, 1.0 - x);
                y = std::min(y, 1.0 - y);
                minimum = std::min(minimum, std::sqrt(x * x + y * y));
            }
        }
        return minimum;
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        std::string contents{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        contents.erase(
            std::remove(contents.begin(), contents.end(), '\r'),
            contents.end());
        return contents;
    }
}

int main(int argc, char** argv)
{
    bool passed = true;
    using Quality = uvsr::AntiAliasingQuality;
    using Cost = uvsr::TemporalAaCostMode;
    using Jitter = uvsr::TemporalAaJitterSequence;

    constexpr std::array<uvsr::TemporalAaJitterSample, 4>
        expectedRotatedGrid = {{
            { 0.125f, -0.375f }, { -0.375f, -0.125f },
            { 0.375f, 0.125f }, { -0.125f, 0.375f }
        }};
    constexpr std::array<uvsr::TemporalAaJitterSample, 4>
        expectedUniformHelix = {{
            { -0.25f, -0.25f }, { 0.25f, 0.25f },
            { -0.25f, 0.25f }, { 0.25f, -0.25f }
        }};
    for (uint64_t frame = 0u; frame < 8u; ++frame)
    {
        const auto rotated = uvsr::GetTemporalAaJitter(
            Jitter::RotatedGrid4, frame);
        const auto helix = uvsr::GetTemporalAaJitter(
            Jitter::UniformHelix4, frame);
        const auto& expectedRotated =
            expectedRotatedGrid[frame % expectedRotatedGrid.size()];
        const auto& expectedHelix =
            expectedUniformHelix[frame % expectedUniformHelix.size()];
        passed &= Check(
            NearlyEqual(rotated.x, expectedRotated.x) &&
                NearlyEqual(rotated.y, expectedRotated.y) &&
                NearlyEqual(helix.x, expectedHelix.x) &&
                NearlyEqual(helix.y, expectedHelix.y),
            "Filament's four-sample jitter tables must remain exact");
    }

    constexpr std::array<uvsr::TemporalAaJitterSample, 32>
        expectedFilamentHalton = {{
            { 51.f / 512.f, -67.f / 1458.f },
            { -77.f / 512.f, 419.f / 1458.f },
            { 179.f / 512.f, -391.f / 1458.f },
            { -141.f / 512.f, 95.f / 1458.f },
            { 115.f / 512.f, 581.f / 1458.f },
            { -13.f / 512.f, -661.f / 1458.f },
            { 243.f / 512.f, -175.f / 1458.f },
            { -245.f / 512.f, 311.f / 1458.f },
            { 11.f / 512.f, -499.f / 1458.f },
            { -117.f / 512.f, -13.f / 1458.f },
            { 139.f / 512.f, 473.f / 1458.f },
            { -181.f / 512.f, -337.f / 1458.f },
            { 75.f / 512.f, 149.f / 1458.f },
            { -53.f / 512.f, 635.f / 1458.f },
            { 203.f / 512.f, -607.f / 1458.f },
            { -213.f / 512.f, -121.f / 1458.f },
            { 43.f / 512.f, 365.f / 1458.f },
            { -85.f / 512.f, -445.f / 1458.f },
            { 171.f / 512.f, 41.f / 1458.f },
            { -149.f / 512.f, 527.f / 1458.f },
            { 107.f / 512.f, -283.f / 1458.f },
            { -21.f / 512.f, 203.f / 1458.f },
            { 235.f / 512.f, 689.f / 1458.f },
            { -229.f / 512.f, -697.f / 1458.f },
            { 27.f / 512.f, -211.f / 1458.f },
            { -101.f / 512.f, 275.f / 1458.f },
            { 155.f / 512.f, -535.f / 1458.f },
            { -165.f / 512.f, -49.f / 1458.f },
            { 91.f / 512.f, 437.f / 1458.f },
            { -37.f / 512.f, -373.f / 1458.f },
            { 219.f / 512.f, 113.f / 1458.f },
            { -197.f / 512.f, 599.f / 1458.f }
        }};
    for (uint32_t frame = 0u; frame < expectedFilamentHalton.size(); ++frame)
    {
        const auto actual = uvsr::GetTemporalAaJitter(
            Jitter::Halton23x32, frame);
        passed &= Check(
            NearlyEqual(actual.x, expectedFilamentHalton[frame].x) &&
                NearlyEqual(actual.y, expectedFilamentHalton[frame].y),
            "Filament's skipped Halton (2,3) sequence must remain exact");
    }

    constexpr std::array<uvsr::TemporalAaJitterSample, 32>
        expectedSobol = {{
            { -0.163727030f, -0.150207385f },
            { 0.336629748f, 0.339536399f },
            { -0.269704461f, 0.230351552f },
            { 0.225300357f, -0.269102067f },
            { -0.00180649897f, 0.460878044f },
            { 0.477911383f, -0.0206133239f },
            { -0.428817034f, -0.392376602f },
            { 0.0973897949f, 0.0859183744f },
            { -0.246421888f, 0.0318495929f },
            { 0.312270671f, -0.456435293f },
            { -0.358226746f, -0.124909990f },
            { 0.161113590f, 0.383117408f },
            { -0.116007984f, -0.359382242f },
            { 0.379556715f, 0.149445280f },
            { -0.467211872f, 0.285125732f },
            { 0.0295942873f, -0.211078987f },
            { -0.152378082f, 0.365998149f },
            { 0.374861598f, -0.168332800f },
            { -0.282235920f, -0.291959941f },
            { 0.217606395f, 0.212779120f },
            { -0.0323606580f, -0.0334762856f },
            { 0.466017842f, 0.473048568f },
            { -0.405805230f, 0.116170175f },
            { 0.0930275545f, -0.407893479f },
            { -0.218505859f, -0.487654001f },
            { 0.265011579f, 0.00385231944f },
            { -0.342283189f, 0.407048643f },
            { 0.125823259f, -0.0796916559f },
            { -0.0762585402f, 0.182998121f },
            { 0.418778718f, -0.328401715f },
            { -0.473106205f, -0.222323194f },
            { 0.0601801984f, 0.260530889f }
        }};
    for (uint32_t frame = 0u; frame < expectedSobol.size(); ++frame)
    {
        const auto actual = uvsr::GetTemporalAaJitter(
            Jitter::Sobol32, frame);
        passed &= Check(
            actual.x == expectedSobol[frame].x &&
                actual.y == expectedSobol[frame].y,
            "the fixed seed-43 Sobol table must remain exact");
    }

    constexpr std::array<std::pair<Jitter, uint32_t>, 6> sequences = {{
        { Jitter::RotatedGrid4, 4u },
        { Jitter::UniformHelix4, 4u },
        { Jitter::Halton23x8, 8u },
        { Jitter::Halton23x16, 16u },
        { Jitter::Halton23x32, 32u },
        { Jitter::Sobol32, 32u }
    }};
    for (const auto& [sequence, length] : sequences)
    {
        passed &= Check(
            uvsr::GetTemporalAaJitterSequenceLength(sequence) == length,
            "every jitter sequence must expose its exact period");
        for (uint32_t frame = 0u; frame < length; ++frame)
        {
            const auto sample = uvsr::GetTemporalAaJitter(sequence, frame);
            const auto repeated =
                uvsr::GetTemporalAaJitter(sequence, frame + length);
            passed &= Check(
                std::isfinite(sample.x) && std::isfinite(sample.y) &&
                    sample.x >= -0.5f && sample.x < 0.5f &&
                    sample.y >= -0.5f && sample.y < 0.5f &&
                    sample.x == repeated.x && sample.y == repeated.y,
                "jitter samples must be bounded, deterministic, and periodic");
        }
    }
    for (uint32_t count : { 2u, 4u, 8u, 16u, 32u })
    {
        passed &= Check(
            IsSobolNetPrefix(count) &&
                GetToroidalMinimumDistance(Jitter::Sobol32, count) >
                    GetToroidalMinimumDistance(Jitter::Halton23x32, count),
            "Sobol must retain its dyadic nets and tested toroidal spacing advantage over Filament Halton");
    }
    const auto invalidJitter = uvsr::GetTemporalAaJitter(
        static_cast<Jitter>(999u), 0u);
    const auto defaultJitter =
        uvsr::GetTemporalAaJitter(Jitter::Halton23x16, 0u);
    uvsr::AntiAliasingSettings invalidJitterSettings;
    invalidJitterSettings.temporal.jitterSequence =
        static_cast<Jitter>(999u);
    const auto invalidJitterResolved =
        uvsr::ResolveAntiAliasingSettings(invalidJitterSettings);
    passed &= Check(
        uvsr::GetTemporalAaJitterSequenceLength(
            static_cast<Jitter>(999u)) == 16u &&
            invalidJitter.x == defaultJitter.x &&
            invalidJitter.y == defaultJitter.y &&
            invalidJitterResolved.temporalJitterSequence ==
                Jitter::Halton23x16,
        "invalid jitter selections must fall back to Filament Halton 16");

    constexpr auto jitterDelta =
        uvsr::GetTemporalAaCurrentToPreviousJitter(
            { 0.25f, -0.125f },
            { -0.375f, 0.25f });
    passed &= Check(
        NearlyEqual(jitterDelta.x, -0.625f) &&
            NearlyEqual(jitterDelta.y, 0.375f),
        "jitter reprojection must remain previous minus current");

    const uvsr::AntiAliasingSettings defaults;
    passed &= Check(
        !defaults.temporal.enabled &&
            defaults.temporal.quality == Quality::Medium &&
            defaults.temporal.costMode == Cost::Reduced &&
            defaults.temporal.jitterSequence == Jitter::Halton23x16 &&
            defaults.temporal.stationaryBypass &&
            !defaults.fastApproximate.enabled &&
            defaults.fastApproximate.quality == Quality::Ultra &&
            NearlyEqual(
                defaults.fastApproximate.edgeSharpness,
                uvsr::FastApproximateAaDefaultEdgeSharpness) &&
            NearlyEqual(
                defaults.fastApproximate.edgeThreshold,
                uvsr::FastApproximateAaDefaultEdgeThreshold) &&
            NearlyEqual(
                defaults.fastApproximate.darkEdgeThreshold,
                uvsr::FastApproximateAaDefaultDarkEdgeThreshold) &&
            !defaults.cmaa2.enabled &&
            defaults.cmaa2.quality == Quality::Ultra &&
            NearlyEqual(
                defaults.cmaa2.edgeThreshold,
                uvsr::Cmaa2DefaultEdgeThreshold) &&
            defaults.cmaa2.detector ==
                uvsr::Cmaa2EdgeDetector::FullColor &&
            !defaults.msaa.enabled &&
            defaults.msaa.quality == Quality::Medium &&
            defaults.msaa.sampleCount == 4u,
        "TAA, FXAA, CMAA2, and MSAA must be independent default-off techniques");

    const auto defaultResolved =
        uvsr::ResolveAntiAliasingSettings(defaults);
    passed &= Check(
        !defaultResolved.temporalEnabled &&
            !defaultResolved.fastApproximateEnabled &&
            !defaultResolved.cmaa2Enabled &&
            defaultResolved.temporalJitterSequence ==
                Jitter::Halton23x16 &&
            defaultResolved.rasterSampleCount == 1u &&
            defaultResolved.depthValidation ==
                uvsr::TemporalAaDepthValidation::MovingPoint,
        "disabled AA defaults must preserve Stationary Bypass without work");

    for (uint32_t mask = 0u; mask < 16u; ++mask)
    {
        uvsr::AntiAliasingSettings settings;
        settings.temporal.enabled = (mask & 1u) != 0u;
        settings.fastApproximate.enabled = (mask & 2u) != 0u;
        settings.cmaa2.enabled = (mask & 4u) != 0u;
        settings.msaa.enabled = (mask & 8u) != 0u;
        settings.msaa.sampleCount = 8u;
        const auto resolved =
            uvsr::ResolveAntiAliasingSettings(settings);
        passed &= Check(
            resolved.temporalEnabled == settings.temporal.enabled &&
                resolved.fastApproximateEnabled ==
                    settings.fastApproximate.enabled &&
                resolved.cmaa2Enabled == settings.cmaa2.enabled &&
                resolved.rasterSampleCount ==
                    (settings.msaa.enabled ? 8u : 1u),
            "all sixteen AA enable combinations must resolve independently");
    }

    uvsr::AntiAliasingSettings invalidFastApproximate = defaults;
    invalidFastApproximate.fastApproximate.edgeSharpness = -1.f;
    invalidFastApproximate.fastApproximate.edgeThreshold = 1.f;
    invalidFastApproximate.fastApproximate.darkEdgeThreshold =
        std::numeric_limits<float>::quiet_NaN();
    const auto sanitizedFastApproximate =
        uvsr::ResolveAntiAliasingSettings(invalidFastApproximate);
    passed &= Check(
        NearlyEqual(
            sanitizedFastApproximate.fastApproximateEdgeSharpness,
            uvsr::FastApproximateAaMinimumEdgeSharpness) &&
            NearlyEqual(
                sanitizedFastApproximate.fastApproximateEdgeThreshold,
                uvsr::FastApproximateAaMaximumEdgeThreshold) &&
            NearlyEqual(
                sanitizedFastApproximate.fastApproximateDarkEdgeThreshold,
                uvsr::FastApproximateAaMinimumDarkEdgeThreshold),
        "Fast Approximate controls must clamp finite and non-finite input");

    uvsr::AntiAliasingSettings invalidCmaa2 = defaults;
    invalidCmaa2.cmaa2.edgeThreshold =
        std::numeric_limits<float>::quiet_NaN();
    invalidCmaa2.cmaa2.detector =
        static_cast<uvsr::Cmaa2EdgeDetector>(99u);
    const auto sanitizedCmaa2 =
        uvsr::ResolveAntiAliasingSettings(invalidCmaa2);
    passed &= Check(
        NearlyEqual(
            sanitizedCmaa2.cmaa2EdgeThreshold,
            uvsr::Cmaa2MinimumEdgeThreshold) &&
            sanitizedCmaa2.cmaa2EdgeDetector ==
                uvsr::Cmaa2EdgeDetector::FullColor,
        "CMAA2 controls must clamp invalid threshold and detector input");

    constexpr std::array<Quality, 4> qualities = {
        Quality::Low,
        Quality::Medium,
        Quality::High,
        Quality::Ultra
    };
    constexpr std::array<uint32_t, 4> historyFrames = {
        3u, 6u, 9u, 12u
    };
    constexpr std::array<float, 4> fxaaSharpness = {
        2.f, 4.f, 8.f, 8.f
    };
    constexpr std::array<float, 4> fxaaThresholds = {
        0.25f, 0.1875f, 0.125f, 0.08f
    };
    constexpr std::array<float, 4> fxaaDarkThresholds = {
        0.06f, 0.055f, 0.05f, 0.04f
    };
    constexpr std::array<float, 4> cmaa2Thresholds = {
        0.15f, 0.10f, 0.07f, 0.05f
    };
    constexpr std::array<uvsr::Cmaa2EdgeDetector, 4> cmaa2Detectors = {
        uvsr::Cmaa2EdgeDetector::Luma,
        uvsr::Cmaa2EdgeDetector::Luma,
        uvsr::Cmaa2EdgeDetector::Luma,
        uvsr::Cmaa2EdgeDetector::FullColor
    };
    constexpr std::array<uint32_t, 4> multisampleCounts = {
        2u, 4u, 8u, 16u
    };
    for (uint32_t index = 0u; index < qualities.size(); ++index)
    {
        uvsr::AntiAliasingSettings settings;
        settings.temporal.enabled = true;
        settings.temporal.quality = qualities[index];
        const auto resolved =
            uvsr::ResolveAntiAliasingSettings(settings);
        passed &= Check(
            resolved.historyFrames == historyFrames[index],
            "TAA quality must retain its established history horizon");

        const auto fxaaPreset =
            uvsr::GetFastApproximateAaQualityPreset(qualities[index]);
        uvsr::FastApproximateAaSettings fxaa;
        uvsr::ApplyFastApproximateAaQualityPreset(
            fxaa, qualities[index]);
        passed &= Check(
            NearlyEqual(fxaaPreset.edgeSharpness, fxaaSharpness[index]) &&
                NearlyEqual(
                    fxaaPreset.edgeThreshold,
                    fxaaThresholds[index]) &&
                NearlyEqual(
                    fxaaPreset.darkEdgeThreshold,
                    fxaaDarkThresholds[index]) &&
                fxaa.quality == qualities[index] &&
                uvsr::MatchesFastApproximateAaQualityPreset(fxaa),
            "FXAA quality must apply its complete three-control recipe");
        fxaa.edgeThreshold = uvsr::FastApproximateAaMaximumEdgeThreshold;
        if (NearlyEqual(
                fxaa.edgeThreshold,
                fxaaPreset.edgeThreshold))
        {
            fxaa.edgeThreshold =
                uvsr::FastApproximateAaMinimumEdgeThreshold;
        }
        passed &= Check(
            !uvsr::MatchesFastApproximateAaQualityPreset(fxaa),
            "an FXAA Advanced override must mark Quality custom");
        uvsr::ApplyFastApproximateAaQualityPreset(
            fxaa, qualities[index]);
        passed &= Check(
            uvsr::MatchesFastApproximateAaQualityPreset(fxaa),
            "reapplying the selected FXAA Quality must clear Advanced overrides");

        const auto cmaa2Preset =
            uvsr::GetCmaa2QualityPreset(qualities[index]);
        uvsr::Cmaa2Settings cmaa2;
        uvsr::ApplyCmaa2QualityPreset(cmaa2, qualities[index]);
        passed &= Check(
            NearlyEqual(
                cmaa2Preset.edgeThreshold,
                cmaa2Thresholds[index]) &&
                cmaa2Preset.detector == cmaa2Detectors[index] &&
                cmaa2.quality == qualities[index] &&
                uvsr::MatchesCmaa2QualityPreset(cmaa2),
            "CMAA2 quality must apply its threshold and detector recipe");
        cmaa2.detector = cmaa2.detector ==
                uvsr::Cmaa2EdgeDetector::Luma
            ? uvsr::Cmaa2EdgeDetector::FullColor
            : uvsr::Cmaa2EdgeDetector::Luma;
        passed &= Check(
            !uvsr::MatchesCmaa2QualityPreset(cmaa2),
            "a CMAA2 Advanced override must mark Quality custom");
        uvsr::ApplyCmaa2QualityPreset(cmaa2, qualities[index]);
        passed &= Check(
            uvsr::MatchesCmaa2QualityPreset(cmaa2),
            "reapplying the selected CMAA2 Quality must clear Advanced overrides");

        uvsr::MsaaSettings multisample;
        uvsr::ApplyMultisampleQualityPreset(
            multisample, qualities[index]);
        passed &= Check(
            multisample.quality == qualities[index] &&
                multisample.sampleCount == multisampleCounts[index] &&
                uvsr::GetMultisampleQualitySampleCount(
                    qualities[index]) == multisampleCounts[index] &&
                uvsr::MatchesMultisampleQualityPreset(multisample),
            "Multisample quality must map Low through Ultra to 2x through 16x");
        multisample.sampleCount = multisampleCounts[
            (index + 1u) % multisampleCounts.size()];
        passed &= Check(
            !uvsr::MatchesMultisampleQualityPreset(multisample),
            "a Multisample Advanced override must mark Quality custom");
        uvsr::ApplyMultisampleQualityPreset(
            multisample, qualities[index]);
        passed &= Check(
            uvsr::MatchesMultisampleQualityPreset(multisample),
            "reapplying the selected Multisample Quality must clear Advanced overrides");
    }
    passed &= Check(
        uvsr::GetPresetTemporalOptions(Quality::Low).motionSource ==
                uvsr::TemporalAaMotionSource::Center &&
            uvsr::GetPresetTemporalOptions(Quality::Medium).motionSource ==
                uvsr::TemporalAaMotionSource::CenterFirstEdgeDilation &&
            uvsr::GetPresetTemporalOptions(Quality::High).historyFilter ==
                uvsr::TemporalAaHistoryFilter::OneSampleBicubic &&
            uvsr::GetPresetTemporalOptions(Quality::Ultra)
                    .currentReconstruction ==
                uvsr::TemporalAaCurrentReconstruction::DeJittered,
        "the four retained TAA quality profiles changed");

    uvsr::AntiAliasingSettings fourTexel = defaults;
    fourTexel.temporal.enabled = true;
    fourTexel.temporal.stationaryBypass = false;
    passed &= Check(
        uvsr::ResolveAntiAliasingSettings(fourTexel).depthValidation ==
            uvsr::TemporalAaDepthValidation::FourTexelFootprint,
        "Stationary Bypass must be a direct normal TAA setting");

    uvsr::AntiAliasingSettings advanced = defaults;
    advanced.temporal.enabled = true;
    advanced.temporal.algorithmOverrides.motionSource =
        uvsr::TemporalAaMotionSourceOverride::ClosestCross;
    advanced.temporal.algorithmOverrides.historyFilter =
        uvsr::TemporalAaHistoryFilterOverride::NineTapCatmullRom;
    advanced.temporal.algorithmOverrides.historyFrames = 32;
    advanced.temporal.algorithmOverrides.historyStrength = 1.5f;
    advanced.temporal.behaviorOverrides.historyWeight =
        uvsr::TemporalAaHistoryWeightPolicyOverride::ImmediateHorizon;
    const auto advancedResolved =
        uvsr::ResolveAntiAliasingSettings(advanced);
    passed &= Check(
        advancedResolved.temporal.motionSource ==
                uvsr::TemporalAaMotionSource::ClosestCross &&
            advancedResolved.temporal.historyFilter ==
                uvsr::TemporalAaHistoryFilter::NineTapCatmullRom &&
            advancedResolved.historyFrames == 32u &&
            NearlyEqual(advancedResolved.historyStrength, 1.5f) &&
            advancedResolved.historyWeight ==
                uvsr::TemporalAaHistoryWeightPolicy::ImmediateHorizon,
        "closed Advanced settings must retain supported image controls");

    uvsr::AntiAliasingSettings minimum = defaults;
    minimum.temporal.enabled = true;
    minimum.temporal.costMode = Cost::Minimum;
    const auto minimumResolved =
        uvsr::ResolveAntiAliasingSettings(minimum);
    passed &= Check(
        minimumResolved.historyStorage ==
                uvsr::TemporalAaHistoryStorage::Compact &&
            minimumResolved.historyWeight ==
                uvsr::TemporalAaHistoryWeightPolicy::ImmediateHorizon &&
            minimumResolved.motionTrust ==
                uvsr::TemporalAaMotionTrust::SquaredSpeed &&
            minimumResolved.blendDomain ==
                uvsr::TemporalAaBlendDomain::LinearRgb &&
            !minimumResolved.sharpeningAllowed &&
            uvsr::IsTemporalAaCompactHistoryCompatible(minimumResolved),
        "Minimum must retain its compact image-equivalent contract");

    const uvsr::TemporalAaStaticPerformanceOptions baseline{ false, false };
    const uvsr::TemporalAaStaticPerformanceOptions optimized{ true, true };
    passed &= Check(
        uvsr::TemporalAaStaticPerformanceCount == 4u &&
            uvsr::GetTemporalAaStaticPerformanceIndex(baseline) == 0u &&
            uvsr::GetTemporalAaStaticPerformanceIndex(optimized) == 3u,
        "TAA execution topology must contain only cost-derived compute paths");

    uvsr::AntiAliasingSettings cmaaOnlyChange = minimum;
    cmaaOnlyChange.cmaa2.enabled = true;
    cmaaOnlyChange.cmaa2.quality = Quality::Low;
    cmaaOnlyChange.cmaa2.edgeThreshold = 0.15f;
    cmaaOnlyChange.cmaa2.detector = uvsr::Cmaa2EdgeDetector::Luma;
    uvsr::AntiAliasingSettings fastApproximateOnlyChange = minimum;
    fastApproximateOnlyChange.fastApproximate.enabled = true;
    fastApproximateOnlyChange.fastApproximate.edgeSharpness = 4.f;
    uvsr::AntiAliasingSettings msaaOnlyChange = minimum;
    msaaOnlyChange.msaa.enabled = true;
    uvsr::AntiAliasingSettings temporalImageChange = minimum;
    temporalImageChange.temporal.stationaryBypass = false;
    uvsr::AntiAliasingSettings jitterSequenceChange = minimum;
    jitterSequenceChange.temporal.jitterSequence = Jitter::Halton23x32;
    uvsr::AntiAliasingSettings disabledJitterChange = defaults;
    disabledJitterChange.temporal.jitterSequence = Jitter::Sobol32;
    passed &= Check(
        !uvsr::AntiAliasingSettingsRequireTemporalReset(
            minimum, cmaaOnlyChange) &&
            !uvsr::AntiAliasingSettingsRequireTemporalReset(
                minimum, fastApproximateOnlyChange) &&
            !uvsr::AntiAliasingSettingsRequireTemporalReset(
                minimum, msaaOnlyChange) &&
            !uvsr::AntiAliasingSettingsRequireTemporalReset(
                defaults, disabledJitterChange) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                minimum, temporalImageChange) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                minimum, jitterSequenceChange),
        "only TAA image changes may reset temporal history");

    passed &= Check(
        uvsr::GetTemporalAaHistoryBytes(1920u, 1080u) ==
                uint64_t(1920u) * 1080u * 24u &&
            uvsr::GetTemporalAaResidentHistoryBytes(
                1920u, 1080u, 4u, 2u) ==
                uvsr::GetTemporalAaHistoryBytes(1920u, 1080u) +
                    uvsr::GetTemporalAaMinimumHistoryBytes(
                        1920u, 1080u, 4u, 2u),
        "resident TAA memory must contain no persistent resurrection history");

    const std::array<float, 4> coherentDepths = {
        0.5f, 0.5001f, 0.4999f, 0.5f
    };
    const std::array<float, 4> silhouetteDepths = {
        0.5f, 0.f, 0.5f, 0.5f
    };
    passed &= Check(
        uvsr::TemporalAaFootprintHasConsistentGeometry(
            uvsr::ReduceTemporalAaReverseZFootprint(coherentDepths)) &&
            !uvsr::TemporalAaFootprintHasConsistentGeometry(
                uvsr::ReduceTemporalAaReverseZFootprint(
                    silhouetteDepths)),
        "reverse-Z history validation must reject mixed silhouettes");
    passed &= Check(
        uvsr::IsTemporalAaMotionValid({ 0.f, 0.f, 0.f, 1.f }) &&
            !uvsr::IsTemporalAaMotionValid({
                std::numeric_limits<float>::infinity(),
                0.f, 0.f, 1.f }),
        "invalid motion must fail closed before history sampling");

    if (argc > 1)
    {
        const std::filesystem::path sourceDirectory = argv[1];
        const std::string temporalPass = ReadTextFile(
            sourceDirectory / "temporal_aa.cpp");
        const std::string temporalHeader = ReadTextFile(
            sourceDirectory / "temporal_aa.h");
        const std::string temporalOptions = ReadTextFile(
            sourceDirectory / "temporal_aa_options.h");
        const std::string qualityShader = ReadTextFile(
            sourceDirectory / "temporal_aa_blend_cs.hlsl");
        const std::string resolveShader = ReadTextFile(
            sourceDirectory / "temporal_aa_resolve_cs.hlsl");
        const std::string cmaaHeader = ReadTextFile(
            sourceDirectory / "cmaa2.h");
        const std::string cmaaSource = ReadTextFile(
            sourceDirectory / "cmaa2.cpp");
        const std::string cmaaShader = ReadTextFile(
            sourceDirectory / "cmaa2.hlsl");
        const std::string fastApproximateHeader = ReadTextFile(
            sourceDirectory / "fast_approximate_aa.h");
        const std::string fastApproximateSource = ReadTextFile(
            sourceDirectory / "fast_approximate_aa.cpp");
        const std::string fastApproximateShader = ReadTextFile(
            sourceDirectory / "fast_approximate_aa_ps.hlsl");
        const std::string fastApproximateAttribution = ReadTextFile(
            sourceDirectory.parent_path() / "third_party" /
                "google_filament_fxaa" / "ATTRIBUTION.md");
        const std::string bsdLicense = ReadTextFile(
            sourceDirectory.parent_path() / "third_party" /
                "licenses" / "BSD-2-Clause.txt");
        const std::string shaderManifest = ReadTextFile(
            sourceDirectory / "shaders.cfg");
        const auto lacksRetiredTaaText = [](const std::string& text)
        {
            return text.find("SampleResurrection") == std::string::npos &&
                text.find("SAMPLE_RESURRECTION") == std::string::npos &&
                text.find("TemporalAaDebugView") == std::string::npos &&
                text.find("TAA_DEBUG_VIEW") == std::string::npos &&
                text.find("FullscreenPixelShader") == std::string::npos &&
                text.find("TAA_PIXEL_SHADER") == std::string::npos &&
                text.find("Threads16x8OnePixel") == std::string::npos &&
                text.find("TAA_COMPUTE_KERNEL") == std::string::npos &&
                text.find("CacheBlocking") == std::string::npos &&
                text.find("TAA_EXPORT_SELECTIVE") == std::string::npos;
        };
        passed &= Check(
            lacksRetiredTaaText(temporalPass) &&
                lacksRetiredTaaText(temporalHeader) &&
                lacksRetiredTaaText(temporalOptions) &&
                lacksRetiredTaaText(qualityShader) &&
                lacksRetiredTaaText(resolveShader),
            "retired TAA resurrection, debug, pixel, kernel, or cache paths returned");
        passed &= Check(
            qualityShader.find("[numthreads(8, 8, 1)]") !=
                    std::string::npos &&
                qualityShader.find("TAA_OPTIMIZED_COMPUTE") !=
                    std::string::npos &&
                qualityShader.find("UVSR_TAA_LDS_PACKED") !=
                    std::string::npos &&
                qualityShader.find("UVSR_TAA_LDS_SPLIT") ==
                    std::string::npos,
            "TAA must retain only 8x8 compute and legacy/packed LDS");
        passed &= Check(
            cmaaHeader.find("Cmaa2ColorRange") == std::string::npos &&
                cmaaSource.find("colorRange") == std::string::npos &&
                cmaaShader.find(
                    "#define CMAA2_SUPPORT_HDR_COLOR_RANGE 0") !=
                    std::string::npos,
            "CMAA2 must be display-linear LDR only");
        passed &= Check(
            temporalPass.find("minimumPresentationCompatible") ==
                    std::string::npos &&
                temporalPass.find("settings.cmaa2Enabled") ==
                    std::string::npos &&
                temporalPass.find("settings.fastApproximateEnabled") ==
                    std::string::npos &&
                temporalPass.find("CMAA2 input is incompatible") ==
                    std::string::npos,
            "Minimum TAA and display-linear CMAA2 must remain composable");
        passed &= Check(
            fastApproximateHeader.find("FastApproximateAAPass") !=
                    std::string::npos &&
                fastApproximateSource.find(
                    "sourceColor == m_OutputColor.Get()") !=
                    std::string::npos &&
                fastApproximateSource.find(
                    "nvrhi::Format::RGBA16_FLOAT") !=
                    std::string::npos &&
                fastApproximateSource.find(
                    "compositeView.GetNumChildViews(ViewType::PLANAR) != 1u") !=
                    std::string::npos &&
                fastApproximateShader.find(
                    "47c86eec22e56d75897e16651eb4d2abd64fc29a") !=
                    std::string::npos &&
                fastApproximateShader.find("sqrt(dot(") !=
                    std::string::npos &&
                fastApproximateShader.find("0.00006103515625") !=
                    std::string::npos &&
                fastApproximateShader.find(
                    "float4(filtered.rgb, colorCenter.a)") !=
                    std::string::npos,
            "Fast Approximate must keep its isolated RGBA16F, perceptual-luma, full-view contract");
        passed &= Check(
            fastApproximateAttribution.find(
                    "47c86eec22e56d75897e16651eb4d2abd64fc29a") !=
                    std::string::npos &&
                fastApproximateAttribution.find(
                    "NVIDIA CORPORATION. ALL RIGHTS RESERVED") !=
                    std::string::npos &&
                fastApproximateAttribution.find(
                    "filament/include/filament/Options.h") !=
                    std::string::npos &&
                fastApproximateAttribution.find(
                    "filament/src/PostProcessManager.cpp") !=
                    std::string::npos &&
                fastApproximateAttribution.find(
                    "filament/src/PostProcessManager.h") !=
                    std::string::npos &&
                bsdLicense.find("BSD 2-Clause License") !=
                    std::string::npos &&
                bsdLicense.find("Morgan McGuire") !=
                    std::string::npos,
            "Filament anti-aliasing source and binary provenance must remain complete");
        passed &= Check(
            shaderManifest.find("TAA_SAMPLE_RESURRECTION") ==
                    std::string::npos &&
                shaderManifest.find("TAA_COMPUTE_KERNEL") ==
                    std::string::npos &&
                shaderManifest.find("TAA_EXPORT_SELECTIVE") ==
                    std::string::npos &&
                shaderManifest.find("TAA_LDS_LAYOUT") ==
                    std::string::npos &&
                shaderManifest.find("TAA_SHARED_WORK_REUSE") ==
                    std::string::npos &&
                shaderManifest.find("TAA_EARLY_HISTORY_REJECTION") ==
                    std::string::npos &&
                shaderManifest.find("TAA_OPTIMIZED_COMPUTE") !=
                    std::string::npos &&
                shaderManifest.find(
                    "CMAA2_SUPPORT_HDR_COLOR_RANGE") ==
                    std::string::npos &&
                shaderManifest.find(
                    "CMAA2_EDGE_DETECTION_LUMA_PATH={0,1}") !=
                    std::string::npos &&
                shaderManifest.find("CMAA2_STATIC_QUALITY_PRESET") ==
                    std::string::npos &&
                shaderManifest.find(
                    "fast_approximate_aa_ps.hlsl -T ps -E main") !=
                    std::string::npos,
            "shader manifests must remove retired TAA and HDR CMAA2 dimensions");
    }

    return passed ? 0 : 1;
}
