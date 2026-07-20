#include "taa_miniengine_reference.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

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

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    }
}

int main(int argc, char** argv)
{
    bool passed = true;

    constexpr std::array<uvsr::MiniEngineTaaJitterSample, 8>
        expectedJitter = {{
            { -0.5f, -0.5f },
            { 0.0f, -1.0f / 6.0f },
            { -0.25f, 1.0f / 6.0f },
            { 0.25f, -7.0f / 18.0f },
            { -0.375f, -1.0f / 18.0f },
            { 0.125f, 5.0f / 18.0f },
            { -0.125f, -5.0f / 18.0f },
            { 0.375f, 1.0f / 18.0f }
        }};
    for (uint64_t frame = 0u; frame < 16u; ++frame)
    {
        const uvsr::MiniEngineTaaJitterSample actual =
            uvsr::GetMiniEngineTaaJitter(frame);
        const auto& expected =
            expectedJitter[frame % expectedJitter.size()];
        passed &= Check(
            NearlyEqual(actual.x, expected.x) &&
                NearlyEqual(actual.y, expected.y),
            "the eight-phase MiniEngine jitter sequence changed");
    }

    constexpr uvsr::MiniEngineTaaJitterSample currentJitter{
        0.25f, -0.125f
    };
    constexpr uvsr::MiniEngineTaaJitterSample previousJitter{
        -0.375f, 0.25f
    };
    constexpr auto currentToPreviousJitter =
        uvsr::GetMiniEngineTaaCurrentToPreviousJitter(
            currentJitter,
            previousJitter);
    passed &= Check(
        NearlyEqual(currentToPreviousJitter.x, -0.625f) &&
            NearlyEqual(currentToPreviousJitter.y, 0.375f),
        "jitter reprojection must remain previous minus current in pixels");

    constexpr auto zeroJitterInput =
        uvsr::GetMiniEngineTaaCurrentInputPosition(
            { 20.f, 30.f },
            { 0.f, 0.f });
    passed &= Check(
        zeroJitterInput.x == 20.f &&
            zeroJitterInput.y == 30.f,
        "zero jitter must remain an exact reconstruction identity");

    using Method = uvsr::AntiAliasingMethod;
    using Quality = uvsr::AntiAliasingQuality;
    using Preset = uvsr::AntiAliasingPreset;
    using Morphology = uvsr::SmaaApplication;

    passed &= Check(
        static_cast<uint32_t>(Method::Count) == 4u,
        "the normal AA method matrix must contain four methods");
    passed &= Check(
        uvsr::GetAntiAliasingImplementation(
            Method::SubpixelMorphological,
            Quality::Ultra) == Preset::Smaa1x,
        "spatial SMAA must remain a single non-temporal implementation");
    passed &= Check(
        uvsr::GetAntiAliasingImplementation(
            Method::IntelCmaa2,
            Quality::High) == Preset::IntelCmaa2 &&
            !uvsr::IsAntiAliasingQualitySupported(
                Method::IntelCmaa2,
                Quality::Ultra),
        "CMAA2 must expose Low, Medium, and High only");

    constexpr std::array<Preset, 4> expectedMsaa = {
        Preset::Msaa2x,
        Preset::Msaa4x,
        Preset::Msaa8x,
        Preset::Msaa16x
    };
    constexpr std::array<uint32_t, 4> expectedSamples = {
        2u, 4u, 8u, 16u
    };
    for (uint32_t qualityIndex = 0u;
        qualityIndex < expectedMsaa.size();
        ++qualityIndex)
    {
        uvsr::AntiAliasingSettings settings;
        settings.method = Method::Msaa;
        settings.quality =
            static_cast<Quality>(qualityIndex);
        const uvsr::ResolvedAntiAliasingSettings resolved =
            uvsr::ResolveAntiAliasingSettings(settings);
        passed &= Check(
            resolved.implementation == expectedMsaa[qualityIndex] &&
                resolved.rasterSampleCount ==
                    expectedSamples[qualityIndex] &&
                resolved.subpixelMorphology ==
                    Morphology::ConservativeMorphological,
            "every MSAA quality must resolve to 2x/4x/8x/16x plus CMAA2");
    }

    constexpr std::array<uint32_t, 4> expectedSearch = {
        4u, 8u, 16u, 32u
    };
    constexpr std::array<float, 4> expectedThreshold = {
        0.15f, 0.10f, 0.10f, 0.05f
    };
    for (uint32_t qualityIndex = 0u;
        qualityIndex < expectedSearch.size();
        ++qualityIndex)
    {
        const uvsr::SmaaQualityReference reference =
            uvsr::GetSmaaQualityReference(
                static_cast<Quality>(qualityIndex));
        passed &= Check(
            reference.axialSearchSteps ==
                    expectedSearch[qualityIndex] &&
                NearlyEqual(
                    reference.edgeThreshold,
                    expectedThreshold[qualityIndex]),
            "SMAA qualities must preserve the pinned source thresholds and searches");
    }
    passed &= Check(
        !uvsr::GetSmaaQualityReference(
            Quality::Low)
                .diagonalDetection &&
            !uvsr::GetSmaaQualityReference(
                Quality::Medium)
                .cornerDetection &&
            uvsr::GetSmaaQualityReference(
                Quality::High)
                .diagonalDetection &&
            uvsr::GetSmaaQualityReference(
                Quality::Ultra)
                .cornerDetection,
        "SMAA diagonal and corner behavior must follow the pinned quality presets");

    constexpr std::array<Preset, 4> expectedTemporal = {
        Preset::TemporalPerformance,
        Preset::TemporalBalanced,
        Preset::TemporalQuality,
        Preset::TemporalUltra
    };
    constexpr std::array<uint32_t, 4> expectedHistoryFrames = {
        3u, 7u, 15u, 31u
    };
    for (uint32_t qualityIndex = 0u;
        qualityIndex < expectedTemporal.size();
        ++qualityIndex)
    {
        uvsr::AntiAliasingSettings settings;
        settings.method = Method::TemporalSubpixelMorphological;
        settings.quality =
            static_cast<Quality>(qualityIndex);
        const auto resolved =
            uvsr::ResolveAntiAliasingSettings(settings);
        passed &= Check(
            resolved.implementation ==
                    expectedTemporal[qualityIndex] &&
                resolved.historyFrames ==
                    expectedHistoryFrames[qualityIndex] &&
                resolved.rasterSampleCount == 1u,
            "temporal quality must resolve to its expected long-term horizon");
    }

    const uvsr::MiniEngineTaaOptions performanceOptions =
        uvsr::GetPresetTemporalOptions(
            Preset::TemporalPerformance);
    const uvsr::MiniEngineTaaOptions balancedOptions =
        uvsr::GetPresetTemporalOptions(
            Preset::TemporalBalanced);
    const uvsr::MiniEngineTaaOptions qualityOptions =
        uvsr::GetPresetTemporalOptions(
            Preset::TemporalQuality);
    for (uint32_t presetIndex = 0u;
        presetIndex < static_cast<uint32_t>(Preset::Count);
        ++presetIndex)
    {
        passed &= Check(
            uvsr::GetPresetTemporalOptions(
                static_cast<Preset>(presetIndex)).interiorWeighting ==
                uvsr::MiniEngineTaaInteriorWeighting::Off,
            "every anti-aliasing preset must leave Stable Interior off");
    }
    passed &= Check(
        performanceOptions.motionSource ==
                uvsr::MiniEngineTaaMotionSource::Center &&
            performanceOptions.currentReconstruction ==
                uvsr::MiniEngineTaaCurrentReconstruction::Direct &&
            performanceOptions.historyFilter ==
                uvsr::MiniEngineTaaHistoryFilter::OneSampleBicubic &&
            performanceOptions.rectification ==
                uvsr::MiniEngineTaaRectification::PairRgb,
        "Temporal Low must keep the fast validated MiniEngine configuration");
    passed &= Check(
        balancedOptions.motionSource ==
                uvsr::MiniEngineTaaMotionSource::
                    CenterFirstEdgeDilation &&
            balancedOptions.currentReconstruction ==
                uvsr::MiniEngineTaaCurrentReconstruction::DeJittered &&
            balancedOptions.interiorWeighting ==
                uvsr::MiniEngineTaaInteriorWeighting::Off,
        "Temporal Medium must keep stable edge ownership and leave Stable Interior off");
    passed &= Check(
        qualityOptions.historyFilter ==
                uvsr::MiniEngineTaaHistoryFilter::FiveTapCatmullRom &&
            qualityOptions.rectification ==
                uvsr::MiniEngineTaaRectification::VarianceYCoCg &&
            qualityOptions.interiorWeighting ==
                uvsr::MiniEngineTaaInteriorWeighting::Off &&
            uvsr::GetPresetTemporalOptions(
                Preset::TemporalUltra).interiorWeighting ==
                uvsr::MiniEngineTaaInteriorWeighting::Off,
        "Temporal High and Ultra must keep the wider variance-aware configuration with Stable Interior off");

    uvsr::AntiAliasingSettings temporal;
    temporal.method = Method::TemporalSubpixelMorphological;
    temporal.quality = Quality::Medium;
    uvsr::AntiAliasingSettings performanceChange = temporal;
    performanceChange.performanceOverrides.computeKernel =
        uvsr::MiniEngineTaaComputeKernel::Threads16x8OnePixel;
    performanceChange.performanceOverrides.ldsLayout =
        uvsr::MiniEngineTaaLdsLayout::SplitAndPacked;
    performanceChange.performanceOverrides.sharedWorkReuse =
        uvsr::MiniEngineTaaAutoToggle::On;
    performanceChange.performanceOverrides.earlyHistoryRejection =
        uvsr::MiniEngineTaaAutoToggle::On;
    performanceChange.performanceOverrides.passFusion =
        uvsr::MiniEngineTaaPassFusion::Fused;
    performanceChange.performanceOverrides.cacheBlocking =
        uvsr::MiniEngineTaaCacheBlocking::Bands2;
    passed &= Check(
        !uvsr::AntiAliasingSettingsRequireTemporalReset(
            temporal,
            performanceChange),
        "image-equivalent TAA performance overrides must not reset history");

    uvsr::AntiAliasingSettings motionChange = temporal;
    motionChange.algorithmOverrides.motionSource =
        uvsr::MiniEngineTaaMotionSourceOverride::Center;
    passed &= Check(
        uvsr::AntiAliasingSettingsRequireTemporalReset(
            temporal,
            motionChange),
        "effective TAA motion-ownership changes must reset history");

    uvsr::AntiAliasingSettings spatialChange = temporal;
    spatialChange.algorithmOverrides.subpixelMorphology =
        uvsr::SmaaApplicationOverride::FullScreen;
    passed &= Check(
        !uvsr::AntiAliasingSettingsRequireTemporalReset(
            temporal,
            spatialChange),
        "presentation-only morphology changes must preserve TAA history");

    uvsr::AntiAliasingSettings strengthChange = temporal;
    strengthChange.algorithmOverrides.historyStrength = 0.5f;
    passed &= Check(
        uvsr::AntiAliasingSettingsRequireTemporalReset(
            temporal,
            strengthChange),
        "history-strength changes must reset the effective temporal image state");

    uvsr::AntiAliasingSettings clampedHistory = temporal;
    clampedHistory.algorithmOverrides.historyFrames = 100;
    clampedHistory.algorithmOverrides.historyStrength = 2.f;
    const auto clampedResolved =
        uvsr::ResolveAntiAliasingSettings(clampedHistory);
    passed &= Check(
        clampedResolved.historyFrames == 31u &&
            clampedResolved.historyStrength == 1.f,
        "history sliders must clamp to their documented ranges");

    passed &= Check(
        uvsr::GetMiniEngineTaaHistoryColorSampleCount(
            uvsr::MiniEngineTaaHistoryFilter::Bilinear) == 1u &&
            uvsr::GetMiniEngineTaaHistoryColorSampleCount(
                uvsr::MiniEngineTaaHistoryFilter::
                    OneSampleBicubic) == 1u &&
            uvsr::GetMiniEngineTaaHistoryColorSampleCount(
                uvsr::MiniEngineTaaHistoryFilter::
                    FiveTapCatmullRom) == 5u,
        "history-filter labels and real history fetch counts diverged");

    const std::array<float, 4> coherentDepths = {
        0.5f, 0.5001f, 0.4999f, 0.5f
    };
    const std::array<float, 4> silhouetteDepths = {
        0.5f, 0.f, 0.5f, 0.5f
    };
    const auto coherentFootprint =
        uvsr::ReduceTemporalAaReverseZFootprint(coherentDepths);
    const auto silhouetteFootprint =
        uvsr::ReduceTemporalAaReverseZFootprint(silhouetteDepths);
    passed &= Check(
        uvsr::TemporalAaFootprintHasConsistentGeometry(
            coherentFootprint) &&
            !uvsr::TemporalAaFootprintHasConsistentGeometry(
                silhouetteFootprint),
        "reverse-Z history validation must reject mixed background silhouettes");
    passed &= Check(
        uvsr::IsTemporalAaMotionValid(
            { 0.f, 0.f, 0.f, 1.f }) &&
            !uvsr::IsTemporalAaMotionValid(
                { std::numeric_limits<float>::infinity(),
                    0.f, 0.f, 1.f }) &&
            !uvsr::IsTemporalAaMotionValid(
                { 0.f, 0.f, 0.f, 0.f }),
        "invalid motion must fail closed before history sampling");

    if (argc > 1)
    {
        const std::filesystem::path sourceDirectory = argv[1];
        const std::string shaderManifest =
            ReadTextFile(sourceDirectory / "shaders.cfg");
        const std::string productionManifest =
            ReadTextFile(sourceDirectory / "shaders_production.cfg");
        const std::string smaaSource =
            ReadTextFile(sourceDirectory / "smaa.cpp");
        const std::string applicationSource =
            ReadTextFile(sourceDirectory / "uvsr.cpp");

        passed &= Check(
            shaderManifest.find(
                "MSAA_VISIBILITY_SAMPLES={2,4,8,16}") !=
                    std::string::npos &&
                productionManifest.find(
                    "PBR_DEFERRED_MSAA_SAMPLES={2,4,8,16}") !=
                    std::string::npos,
            "developer and production shader bundles must compile MSAA 16x");
        passed &= Check(
            shaderManifest.find("smaa_t2x") ==
                    std::string::npos &&
                productionManifest.find("smaa_t2x") ==
                    std::string::npos &&
                shaderManifest.find("smaa_edge_cs") ==
                    std::string::npos &&
                shaderManifest.find("smaa_classify_tiles") ==
                    std::string::npos,
            "removed temporal and SMAA-only performance shaders returned");
        passed &= Check(
            smaaSource.find("TemporalHistoryState") ==
                    std::string::npos &&
                smaaSource.find("SmaaEdgeKernel") ==
                    std::string::npos &&
                smaaSource.find("SmaaWeightCulling") ==
                    std::string::npos,
            "spatial SMAA must not own temporal history or execution experiments");
        passed &= Check(
            applicationSource.find(
                "##ComboPopupInteractionReady") ==
                    std::string::npos,
            "combo interaction state must remain inside the pinned ImGui patch");
        passed &= Check(
            applicationSource.find(
                "AddCircleFilled(") != std::string::npos &&
                applicationSource.find(
                    "IM_COL32(255, 255, 255, 128)") !=
                    std::string::npos,
            "the centered 50-percent white crosshair dot is missing");
    }

    return passed ? 0 : 1;
}
