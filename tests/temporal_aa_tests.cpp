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

    constexpr std::array<uvsr::TemporalAaJitterSample, 8>
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
        const uvsr::TemporalAaJitterSample actual =
            uvsr::GetTemporalAaJitter(frame);
        const auto& expected =
            expectedJitter[frame % expectedJitter.size()];
        passed &= Check(
            NearlyEqual(actual.x, expected.x) &&
                NearlyEqual(actual.y, expected.y),
            "the eight-phase Temporal AA jitter sequence changed");
    }

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
            defaults.temporal.stationaryBypass &&
            !defaults.cmaa2.enabled &&
            defaults.cmaa2.quality == Quality::Ultra &&
            !defaults.msaa.enabled &&
            defaults.msaa.sampleCount == 4u,
        "TAA, CMAA2, and MSAA must be independent default-off techniques");

    const auto defaultResolved =
        uvsr::ResolveAntiAliasingSettings(defaults);
    passed &= Check(
        !defaultResolved.temporalEnabled &&
            !defaultResolved.cmaa2Enabled &&
            defaultResolved.rasterSampleCount == 1u &&
            defaultResolved.depthValidation ==
                uvsr::TemporalAaDepthValidation::MovingPoint,
        "disabled AA defaults must preserve Stationary Bypass without work");

    for (uint32_t mask = 0u; mask < 8u; ++mask)
    {
        uvsr::AntiAliasingSettings settings;
        settings.temporal.enabled = (mask & 1u) != 0u;
        settings.cmaa2.enabled = (mask & 2u) != 0u;
        settings.msaa.enabled = (mask & 4u) != 0u;
        settings.msaa.sampleCount = 8u;
        const auto resolved =
            uvsr::ResolveAntiAliasingSettings(settings);
        passed &= Check(
            resolved.temporalEnabled == settings.temporal.enabled &&
                resolved.cmaa2Enabled == settings.cmaa2.enabled &&
                resolved.rasterSampleCount ==
                    (settings.msaa.enabled ? 8u : 1u),
            "all eight AA enable combinations must resolve independently");
    }

    constexpr std::array<Quality, 4> qualities = {
        Quality::Low,
        Quality::Medium,
        Quality::High,
        Quality::Ultra
    };
    constexpr std::array<uint32_t, 4> historyFrames = {
        3u, 6u, 9u, 12u
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
    uvsr::AntiAliasingSettings msaaOnlyChange = minimum;
    msaaOnlyChange.msaa.enabled = true;
    uvsr::AntiAliasingSettings temporalImageChange = minimum;
    temporalImageChange.temporal.stationaryBypass = false;
    passed &= Check(
        !uvsr::AntiAliasingSettingsRequireTemporalReset(
            minimum, cmaaOnlyChange) &&
            !uvsr::AntiAliasingSettingsRequireTemporalReset(
                minimum, msaaOnlyChange) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                minimum, temporalImageChange),
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
                    std::string::npos,
            "shader manifests must remove retired TAA and HDR CMAA2 dimensions");
    }

    return passed ? 0 : 1;
}
