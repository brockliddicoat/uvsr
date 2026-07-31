#include "temporal_aa_reference.h"

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

    constexpr uvsr::TemporalAaJitterSample currentJitter{
        0.25f, -0.125f
    };
    constexpr uvsr::TemporalAaJitterSample previousJitter{
        -0.375f, 0.25f
    };
    constexpr auto currentToPreviousJitter =
        uvsr::GetTemporalAaCurrentToPreviousJitter(
            currentJitter,
            previousJitter);
    passed &= Check(
        NearlyEqual(currentToPreviousJitter.x, -0.625f) &&
            NearlyEqual(currentToPreviousJitter.y, 0.375f),
        "jitter reprojection must remain previous minus current in pixels");

    constexpr auto zeroJitterInput =
        uvsr::GetTemporalAaCurrentInputPosition(
            { 20.f, 30.f },
            { 0.f, 0.f });
    passed &= Check(
        zeroJitterInput.x == 20.f &&
            zeroJitterInput.y == 30.f,
        "zero jitter must remain an exact reconstruction identity");

    using Method = uvsr::AntiAliasingMethod;
    using Quality = uvsr::AntiAliasingQuality;
    using Preset = uvsr::AntiAliasingPreset;
    using Morphology = uvsr::MorphologyApplication;
    using Cost = uvsr::TemporalAaCostMode;
    using Storage = uvsr::TemporalAaHistoryStorage;
    using DepthValidation = uvsr::TemporalAaDepthValidation;
    using HistoryWeight = uvsr::TemporalAaHistoryWeightPolicy;
    using MotionTrust = uvsr::TemporalAaMotionTrust;
    using RectificationClip = uvsr::TemporalAaRectificationClip;
    using BlendDomain = uvsr::TemporalAaBlendDomain;

    passed &= Check(
        static_cast<uint32_t>(Method::Count) == 3u,
        "the normal AA method matrix must contain three methods");
    passed &= Check(
        std::string(uvsr::GetAntiAliasingMethodLabel(
            Method::TemporalSubpixelMorphological)) ==
                "Temporal Reconstructive" &&
            std::string(uvsr::GetAntiAliasingMethodLabel(
                Method::IntelCmaa2)) ==
                "Conservative Morphological" &&
            std::string(uvsr::GetAntiAliasingMethodLabel(
                Method::Msaa)) ==
                "Multisample Reference",
        "the AA method menu must use the accepted product labels");
    passed &= Check(
        std::string(uvsr::GetAntiAliasingQualityMenuLabel(
            Method::IntelCmaa2,
            Quality::Ultra)) == "Ultra" &&
            std::string(uvsr::GetAntiAliasingQualityMenuLabel(
                Method::Msaa,
                Quality::Ultra)) == "Ultra (16x)",
        "only the morphology control prefixes conservative strengths");
    passed &= Check(
        uvsr::GetAntiAliasingImplementation(
            Method::IntelCmaa2,
            Quality::High) == Preset::IntelCmaa2 &&
            uvsr::IsAntiAliasingQualitySupported(
                Method::IntelCmaa2,
                Quality::Ultra),
        "CMAA2 must expose Low, Medium, High, and Ultra");
    passed &= Check(
        static_cast<uint32_t>(Cost::Count) == 3u &&
            std::string(uvsr::GetTemporalAaCostModeLabel(
                Cost::FullQuality)) == "Full Quality" &&
            std::string(uvsr::GetTemporalAaCostModeLabel(
                Cost::Reduced)) == "Reduced" &&
            std::string(uvsr::GetTemporalAaCostModeLabel(
                Cost::Minimum)) == "Minimum",
        "temporal cost profiles must retain stable labels and IDs");

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
                    Morphology::Off,
            "every MSAA quality must resolve to 2x/4x/8x/16x without a hidden CMAA2 pass");
    }

    constexpr std::array<Preset, 4> expectedTemporal = {
        Preset::TemporalPerformance,
        Preset::TemporalBalanced,
        Preset::TemporalQuality,
        Preset::TemporalUltra
    };
    constexpr std::array<uint32_t, 4> expectedHistoryFrames = {
        3u, 6u, 9u, 12u
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

    const uvsr::TemporalAaOptions performanceOptions =
        uvsr::GetPresetTemporalOptions(
            Preset::TemporalPerformance);
    const uvsr::TemporalAaOptions balancedOptions =
        uvsr::GetPresetTemporalOptions(
            Preset::TemporalBalanced);
    const uvsr::TemporalAaOptions qualityOptions =
        uvsr::GetPresetTemporalOptions(
            Preset::TemporalQuality);
    const uvsr::TemporalAaOptions ultraOptions =
        uvsr::GetPresetTemporalOptions(
            Preset::TemporalUltra);
    passed &= Check(
        performanceOptions.motionSource ==
                uvsr::TemporalAaMotionSource::Center &&
            performanceOptions.currentReconstruction ==
                uvsr::TemporalAaCurrentReconstruction::Direct &&
            performanceOptions.historyFilter ==
                uvsr::TemporalAaHistoryFilter::Bilinear &&
            performanceOptions.rectification ==
                uvsr::TemporalAaRectification::PairRgb,
        "Temporal Low must use direct current and bilinear history reconstruction");
    passed &= Check(
        balancedOptions.motionSource ==
                uvsr::TemporalAaMotionSource::
                    CenterFirstEdgeDilation &&
            balancedOptions.currentReconstruction ==
                uvsr::TemporalAaCurrentReconstruction::Direct &&
            balancedOptions.historyFilter ==
                uvsr::TemporalAaHistoryFilter::Bilinear &&
            balancedOptions.rectification ==
                uvsr::TemporalAaRectification::PairRgb,
        "Temporal Medium must leave Dejitter off, use bilinear history "
        "reconstruction, and default to Pair Tristimulus rectification");
    passed &= Check(
        qualityOptions.historyFilter ==
                uvsr::TemporalAaHistoryFilter::OneSampleBicubic &&
            qualityOptions.currentReconstruction ==
                uvsr::TemporalAaCurrentReconstruction::Direct &&
            qualityOptions.rectification ==
                uvsr::TemporalAaRectification::VarianceYCoCg,
        "Temporal High must leave Dejitter off and use one-sample bicubic history reconstruction");
    passed &= Check(
        ultraOptions.historyFilter ==
                uvsr::TemporalAaHistoryFilter::FiveTapCatmullRom &&
            ultraOptions.currentReconstruction ==
                uvsr::TemporalAaCurrentReconstruction::DeJittered &&
            ultraOptions.rectification ==
                uvsr::TemporalAaRectification::VarianceYCoCg,
        "Temporal Ultra alone must enable Dejitter and use five-tap bicubic history reconstruction");

    const uvsr::AntiAliasingSettings defaultTemporal;
    const auto defaultTemporalResolved =
        uvsr::ResolveAntiAliasingSettings(defaultTemporal);
    passed &= Check(
        defaultTemporal.method == Method::TemporalSubpixelMorphological &&
            defaultTemporal.quality == Quality::Medium &&
            defaultTemporal.temporalCostMode == Cost::Reduced &&
            defaultTemporalResolved.temporalCostMode == Cost::Reduced &&
            defaultTemporalResolved.depthValidation ==
                DepthValidation::MovingPoint,
        "the default temporal profile must use Medium Reduced with "
        "Stationary Bypass");

    uvsr::AntiAliasingSettings temporal;
    temporal.method = Method::TemporalSubpixelMorphological;
    temporal.quality = Quality::Medium;
    temporal.temporalCostMode = Cost::FullQuality;
    const auto fullQualityResolved =
        uvsr::ResolveAntiAliasingSettings(temporal);
    passed &= Check(
        fullQualityResolved.temporalCostMode ==
                Cost::FullQuality &&
            fullQualityResolved.temporal == balancedOptions &&
            fullQualityResolved.sampleResurrection ==
                uvsr::TemporalAaSampleResurrection::Off &&
            fullQualityResolved.historyStorage == Storage::Robust &&
            fullQualityResolved.depthValidation ==
                DepthValidation::FourTexelFootprint &&
            fullQualityResolved.historyWeight ==
                HistoryWeight::ConfidenceRecurrence &&
            fullQualityResolved.motionTrust ==
                MotionTrust::LinearSpeed &&
            fullQualityResolved.rectificationClip ==
                RectificationClip::VelocityDilatedLine &&
            fullQualityResolved.blendDomain ==
                BlendDomain::LuminanceCompressed &&
            fullQualityResolved.sharpeningAllowed &&
            fullQualityResolved.ldsLayout ==
                uvsr::TemporalAaLdsLayout::Legacy &&
            !fullQualityResolved.sharedWorkReuse &&
            !fullQualityResolved.earlyHistoryRejection &&
            fullQualityResolved.passFusion ==
                uvsr::TemporalAaPassFusion::Separate,
        "Full Quality must preserve the robust image and execution baseline");

    uvsr::AntiAliasingSettings reducedCost = temporal;
    reducedCost.temporalCostMode = Cost::Reduced;
    const auto reducedCostResolved =
        uvsr::ResolveAntiAliasingSettings(reducedCost);
    passed &= Check(
        reducedCostResolved.temporalCostMode == Cost::Reduced &&
            reducedCostResolved.temporal == balancedOptions &&
            reducedCostResolved.sampleResurrection ==
                uvsr::TemporalAaSampleResurrection::Off &&
            reducedCostResolved.historyStorage == Storage::Robust &&
            reducedCostResolved.depthValidation ==
                DepthValidation::MovingPoint &&
            reducedCostResolved.historyWeight ==
                HistoryWeight::ConfidenceRecurrence &&
            reducedCostResolved.motionTrust ==
                MotionTrust::LinearSpeed &&
            reducedCostResolved.rectificationClip ==
                RectificationClip::VelocityDilatedLine &&
            reducedCostResolved.blendDomain ==
                BlendDomain::LuminanceCompressed &&
            reducedCostResolved.sharpeningAllowed &&
            reducedCostResolved.executionPath ==
                uvsr::TemporalAaExecutionPath::Compute &&
            reducedCostResolved.computeKernel ==
                uvsr::TemporalAaComputeKernel::
                    Threads8x8TwoPixels &&
            reducedCostResolved.ldsLayout ==
                uvsr::TemporalAaLdsLayout::SplitAndPacked &&
            reducedCostResolved.sharedWorkReuse &&
            reducedCostResolved.earlyHistoryRejection &&
            reducedCostResolved.passFusion ==
                uvsr::TemporalAaPassFusion::Fused,
        "Reduced must use Stationary Bypass with robust history and the "
        "packed, shared-work, early-rejection, fused topology");

    uvsr::AntiAliasingSettings reducedFourTexel = reducedCost;
    reducedFourTexel.behaviorOverrides.depthValidation =
        uvsr::TemporalAaDepthValidationOverride::FourTexelFootprint;
    const auto reducedFourTexelResolved =
        uvsr::ResolveAntiAliasingSettings(reducedFourTexel);
    passed &= Check(
        !uvsr::IsAntiAliasingPresetCustom(reducedCost) &&
            uvsr::IsAntiAliasingPresetCustom(reducedFourTexel) &&
            reducedFourTexelResolved.depthValidation ==
                DepthValidation::FourTexelFootprint &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                reducedCost,
                reducedFourTexel) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                reducedFourTexel,
                reducedCost),
        "Reduced must own Stationary Bypass while an explicit Four-Texel "
        "override remains custom and resets temporal history");

    uvsr::AntiAliasingSettings minimumCost = temporal;
    minimumCost.temporalCostMode = Cost::Minimum;
    const auto minimumCostResolved =
        uvsr::ResolveAntiAliasingSettings(minimumCost);
    passed &= Check(
        minimumCostResolved.temporalCostMode == Cost::Minimum &&
            minimumCostResolved.sampleResurrection ==
                uvsr::TemporalAaSampleResurrection::Off &&
            minimumCostResolved.temporal.motionSource ==
                uvsr::TemporalAaMotionSource::Center &&
            minimumCostResolved.temporal.currentReconstruction ==
                uvsr::TemporalAaCurrentReconstruction::Direct &&
            minimumCostResolved.temporal.historyFilter ==
                uvsr::TemporalAaHistoryFilter::Bilinear &&
            minimumCostResolved.temporal.rectification ==
                uvsr::TemporalAaRectification::PairRgb &&
            minimumCostResolved.historyStorage == Storage::Compact &&
            minimumCostResolved.depthValidation ==
                DepthValidation::MovingPoint &&
            minimumCostResolved.historyWeight ==
                HistoryWeight::ImmediateHorizon &&
            minimumCostResolved.motionTrust ==
                MotionTrust::SquaredSpeed &&
            minimumCostResolved.rectificationClip ==
                RectificationClip::TightComponent &&
            minimumCostResolved.blendDomain ==
                BlendDomain::LinearRgb &&
            !minimumCostResolved.sharpeningAllowed &&
            minimumCostResolved.ldsLayout ==
                uvsr::TemporalAaLdsLayout::SplitAndPacked &&
            minimumCostResolved.sharedWorkReuse &&
            minimumCostResolved.earlyHistoryRejection &&
            minimumCostResolved.passFusion ==
                uvsr::TemporalAaPassFusion::Fused &&
            uvsr::IsTemporalAaCompactHistoryCompatible(
                minimumCostResolved),
        "Minimum must default every compact image policy, algorithm, and "
        "topology without requiring a stored override");

    const uint32_t robustBehaviorFlags =
        uvsr::GetTemporalAaBehaviorFlags(
            DepthValidation::FourTexelFootprint,
            HistoryWeight::ConfidenceRecurrence,
            MotionTrust::LinearSpeed,
            RectificationClip::VelocityDilatedLine,
            BlendDomain::LuminanceCompressed);
    const uint32_t minimumBehaviorFlags =
        uvsr::GetTemporalAaBehaviorFlags(
            minimumCostResolved.depthValidation,
            minimumCostResolved.historyWeight,
            minimumCostResolved.motionTrust,
            minimumCostResolved.rectificationClip,
            minimumCostResolved.blendDomain);
    passed &= Check(
        robustBehaviorFlags == 0u &&
            minimumBehaviorFlags ==
                (UVSR_TAA_BEHAVIOR_MOVING_POINT_DEPTH |
                    UVSR_TAA_BEHAVIOR_IMMEDIATE_HISTORY_WEIGHT |
                    UVSR_TAA_BEHAVIOR_SQUARED_MOTION_TRUST |
                    UVSR_TAA_BEHAVIOR_TIGHT_RECTIFICATION |
                    UVSR_TAA_BEHAVIOR_LINEAR_BLEND_DOMAIN) &&
            uvsr::GetTemporalAaBehaviorFlags(
                DepthValidation::MovingPoint,
                HistoryWeight::ConfidenceRecurrence,
                MotionTrust::LinearSpeed,
                RectificationClip::VelocityDilatedLine,
                BlendDomain::LuminanceCompressed) ==
                    UVSR_TAA_BEHAVIOR_MOVING_POINT_DEPTH &&
            uvsr::GetTemporalAaBehaviorFlags(
                DepthValidation::FourTexelFootprint,
                HistoryWeight::ImmediateHorizon,
                MotionTrust::LinearSpeed,
                RectificationClip::VelocityDilatedLine,
                BlendDomain::LuminanceCompressed) ==
                    UVSR_TAA_BEHAVIOR_IMMEDIATE_HISTORY_WEIGHT &&
            uvsr::GetTemporalAaBehaviorFlags(
                DepthValidation::FourTexelFootprint,
                HistoryWeight::ConfidenceRecurrence,
                MotionTrust::SquaredSpeed,
                RectificationClip::VelocityDilatedLine,
                BlendDomain::LuminanceCompressed) ==
                    UVSR_TAA_BEHAVIOR_SQUARED_MOTION_TRUST &&
            uvsr::GetTemporalAaBehaviorFlags(
                DepthValidation::FourTexelFootprint,
                HistoryWeight::ConfidenceRecurrence,
                MotionTrust::LinearSpeed,
                RectificationClip::TightComponent,
                BlendDomain::LuminanceCompressed) ==
                    UVSR_TAA_BEHAVIOR_TIGHT_RECTIFICATION &&
            uvsr::GetTemporalAaBehaviorFlags(
                DepthValidation::FourTexelFootprint,
                HistoryWeight::ConfidenceRecurrence,
                MotionTrust::LinearSpeed,
                RectificationClip::VelocityDilatedLine,
                BlendDomain::LinearRgb) ==
                    UVSR_TAA_BEHAVIOR_LINEAR_BLEND_DOMAIN,
        "runtime behavior flags must map each independent image policy to "
        "one stable constant-buffer bit");

    uvsr::AntiAliasingSettings minimumOverrides = minimumCost;
    minimumOverrides.algorithmOverrides.motionSource =
        uvsr::TemporalAaMotionSourceOverride::
            CenterFirstEdgeDilation;
    minimumOverrides.algorithmOverrides.currentReconstruction =
        uvsr::TemporalAaCurrentReconstructionOverride::DeJittered;
    minimumOverrides.algorithmOverrides.historyFilter =
        uvsr::TemporalAaHistoryFilterOverride::NineTapCatmullRom;
    minimumOverrides.algorithmOverrides.rectification =
        uvsr::TemporalAaRectificationOverride::VarianceYCoCg;
    minimumOverrides.algorithmOverrides.sampleResurrection =
        uvsr::TemporalAaSampleResurrectionOverride::TwoOlderFrames;
    minimumOverrides.behaviorOverrides.historyStorage =
        uvsr::TemporalAaHistoryStorageOverride::Robust;
    minimumOverrides.behaviorOverrides.depthValidation =
        uvsr::TemporalAaDepthValidationOverride::FourTexelFootprint;
    minimumOverrides.behaviorOverrides.historyWeight =
        uvsr::TemporalAaHistoryWeightPolicyOverride::
            ConfidenceRecurrence;
    minimumOverrides.behaviorOverrides.motionTrust =
        uvsr::TemporalAaMotionTrustOverride::LinearSpeed;
    minimumOverrides.behaviorOverrides.rectificationClip =
        uvsr::TemporalAaRectificationClipOverride::
            VelocityDilatedLine;
    minimumOverrides.behaviorOverrides.blendDomain =
        uvsr::TemporalAaBlendDomainOverride::LuminanceCompressed;
    minimumOverrides.behaviorOverrides.sharpening =
        uvsr::TemporalAaAutoToggle::On;
    const auto minimumOverridesResolved =
        uvsr::ResolveAntiAliasingSettings(minimumOverrides);
    passed &= Check(
        minimumOverridesResolved.temporal.motionSource ==
                uvsr::TemporalAaMotionSource::
                    CenterFirstEdgeDilation &&
            minimumOverridesResolved.temporal.currentReconstruction ==
                uvsr::TemporalAaCurrentReconstruction::DeJittered &&
            minimumOverridesResolved.temporal.historyFilter ==
                uvsr::TemporalAaHistoryFilter::NineTapCatmullRom &&
            minimumOverridesResolved.temporal.rectification ==
                uvsr::TemporalAaRectification::VarianceYCoCg &&
            minimumOverridesResolved.sampleResurrection ==
                uvsr::TemporalAaSampleResurrection::Off &&
            minimumOverridesResolved.historyStorage == Storage::Robust &&
            minimumOverridesResolved.depthValidation ==
                DepthValidation::FourTexelFootprint &&
            minimumOverridesResolved.historyWeight ==
                HistoryWeight::ConfidenceRecurrence &&
            minimumOverridesResolved.motionTrust ==
                MotionTrust::LinearSpeed &&
            minimumOverridesResolved.rectificationClip ==
                RectificationClip::VelocityDilatedLine &&
            minimumOverridesResolved.blendDomain ==
                BlendDomain::LuminanceCompressed &&
            minimumOverridesResolved.sharpeningAllowed &&
            !uvsr::IsTemporalAaCompactHistoryCompatible(
                minimumOverridesResolved),
        "Developer Options must override each independently exposed Minimum "
        "default while cost-owned resurrection remains disabled");

    uvsr::AntiAliasingSettings reducedCompactCompatible = reducedCost;
    reducedCompactCompatible.algorithmOverrides.motionSource =
        uvsr::TemporalAaMotionSourceOverride::Center;
    reducedCompactCompatible.behaviorOverrides.historyStorage =
        uvsr::TemporalAaHistoryStorageOverride::Compact;
    reducedCompactCompatible.behaviorOverrides.historyWeight =
        uvsr::TemporalAaHistoryWeightPolicyOverride::ImmediateHorizon;
    const auto reducedCompactCompatibleResolved =
        uvsr::ResolveAntiAliasingSettings(reducedCompactCompatible);
    uvsr::AntiAliasingSettings minimumIncompatible = minimumCost;
    minimumIncompatible.behaviorOverrides.historyWeight =
        uvsr::TemporalAaHistoryWeightPolicyOverride::
            ConfidenceRecurrence;
    const auto minimumIncompatibleResolved =
        uvsr::ResolveAntiAliasingSettings(minimumIncompatible);
    uvsr::AntiAliasingSettings minimumResurrectionRequest =
        minimumCost;
    minimumResurrectionRequest.algorithmOverrides.sampleResurrection =
        uvsr::TemporalAaSampleResurrectionOverride::TwoOlderFrames;
    const auto minimumResurrectionRequestResolved =
        uvsr::ResolveAntiAliasingSettings(
            minimumResurrectionRequest);
    passed &= Check(
        reducedCompactCompatibleResolved.historyStorage ==
                Storage::Compact &&
            uvsr::IsTemporalAaCompactHistoryCompatible(
                reducedCompactCompatibleResolved) &&
            minimumIncompatibleResolved.historyStorage ==
                Storage::Compact &&
            !uvsr::IsTemporalAaCompactHistoryCompatible(
                minimumIncompatibleResolved) &&
            minimumResurrectionRequestResolved.sampleResurrection ==
                uvsr::TemporalAaSampleResurrection::Off &&
            uvsr::IsTemporalAaCompactHistoryCompatible(
                minimumResurrectionRequestResolved),
        "compact history compatibility must be driven by resolved "
        "algorithms and Immediate Horizon while Minimum ignores an "
        "inapplicable resurrection request");

    passed &= Check(
        uvsr::AntiAliasingSettingsRequireTemporalReset(
            temporal,
            reducedCost) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                reducedCost,
                temporal) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                reducedCost,
                minimumCost) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                minimumCost,
                reducedCost) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                temporal,
                minimumCost) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                minimumCost,
                temporal),
        "Full Quality, Reduced, and Minimum image-policy transitions must "
        "reset temporal history");

    uvsr::AntiAliasingSettings lowFullCost = temporal;
    lowFullCost.quality = Quality::Low;
    uvsr::AntiAliasingSettings lowReducedCost = lowFullCost;
    lowReducedCost.temporalCostMode = Cost::Reduced;
    uvsr::AntiAliasingSettings lowMinimumCost = lowFullCost;
    lowMinimumCost.temporalCostMode = Cost::Minimum;
    passed &= Check(
        uvsr::AntiAliasingSettingsRequireTemporalReset(
            lowFullCost,
            lowMinimumCost) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                lowMinimumCost,
                lowFullCost) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                lowReducedCost,
                lowMinimumCost) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                lowMinimumCost,
                lowReducedCost),
        "Minimum hidden image policies must reset history even when the Low "
        "preset already matches its static reconstruction algorithms");

    uvsr::AntiAliasingSettings lowMinimumMorphology =
        lowMinimumCost;
    lowMinimumMorphology.algorithmOverrides.subpixelMorphology =
        uvsr::MorphologyApplicationOverride::
            ConservativeMorphological;
    passed &= Check(
        !uvsr::AntiAliasingSettingsRequireTemporalReset(
            lowMinimumCost,
            lowMinimumMorphology) &&
            !uvsr::AntiAliasingSettingsRequireTemporalReset(
                lowMinimumMorphology,
                lowMinimumCost),
        "presentation morphology must not make a requested Minimum layout "
        "reset before runtime compatibility is known");

    uvsr::AntiAliasingSettings fullResurrection = temporal;
    fullResurrection.algorithmOverrides.sampleResurrection =
        uvsr::TemporalAaSampleResurrectionOverride::TwoOlderFrames;
    const auto fullResurrectionResolved =
        uvsr::ResolveAntiAliasingSettings(fullResurrection);
    uvsr::AntiAliasingSettings reducedResurrection =
        fullResurrection;
    reducedResurrection.temporalCostMode = Cost::Reduced;
    const auto reducedResurrectionResolved =
        uvsr::ResolveAntiAliasingSettings(reducedResurrection);
    passed &= Check(
        fullResurrectionResolved.temporalCostMode ==
                Cost::FullQuality &&
            fullResurrectionResolved.sampleResurrection ==
                uvsr::TemporalAaSampleResurrection::
                    TwoOlderFrames &&
            fullResurrectionResolved.ldsLayout ==
                uvsr::TemporalAaLdsLayout::Legacy &&
            !fullResurrectionResolved.sharedWorkReuse &&
            !fullResurrectionResolved.earlyHistoryRejection,
        "Full Quality must retain older-frame resurrection on its validated "
        "baseline topology");
    passed &= Check(
        reducedResurrectionResolved.temporalCostMode ==
                Cost::Reduced &&
            reducedResurrectionResolved.sampleResurrection ==
                uvsr::TemporalAaSampleResurrection::Off &&
            reducedResurrectionResolved.ldsLayout ==
                uvsr::TemporalAaLdsLayout::SplitAndPacked &&
            reducedResurrectionResolved.sharedWorkReuse &&
            reducedResurrectionResolved.earlyHistoryRejection &&
            reducedResurrectionResolved.passFusion ==
                uvsr::TemporalAaPassFusion::Fused &&
            reducedResurrection.algorithmOverrides
                    .sampleResurrection ==
                uvsr::TemporalAaSampleResurrectionOverride::
                    TwoOlderFrames,
        "Reduced must pin resurrection off without overwriting the stored "
        "Full Quality override or losing its optimized topology");
    passed &= Check(
        uvsr::AntiAliasingSettingsRequireTemporalReset(
            fullResurrection,
            reducedResurrection) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                reducedResurrection,
                fullResurrection),
        "a cost-owned effective resurrection change must reset temporal "
        "history symmetrically");

    passed &= Check(
        uvsr::AntiAliasingSettingsRequireTemporalReset(
            minimumCost,
            minimumOverrides),
        "post-cost algorithm and image-policy overrides must not be ignored "
        "by Minimum or preserve incompatible history");

    uvsr::AntiAliasingSettings performanceChange = temporal;
    performanceChange.performanceOverrides.computeKernel =
        uvsr::TemporalAaComputeKernel::Threads16x8OnePixel;
    performanceChange.performanceOverrides.ldsLayout =
        uvsr::TemporalAaLdsLayout::SplitAndPacked;
    performanceChange.performanceOverrides.sharedWorkReuse =
        uvsr::TemporalAaAutoToggle::On;
    performanceChange.performanceOverrides.earlyHistoryRejection =
        uvsr::TemporalAaAutoToggle::On;
    performanceChange.performanceOverrides.passFusion =
        uvsr::TemporalAaPassFusion::Fused;
    performanceChange.performanceOverrides.cacheBlocking =
        uvsr::TemporalAaCacheBlocking::Bands2;
    passed &= Check(
        !uvsr::AntiAliasingSettingsRequireTemporalReset(
            temporal,
            performanceChange) &&
            !uvsr::AntiAliasingSettingsRequireTemporalReset(
                performanceChange,
                temporal),
        "image-equivalent TAA performance overrides must not reset history");

    uvsr::AntiAliasingSettings storageChange = temporal;
    storageChange.behaviorOverrides.historyStorage =
        uvsr::TemporalAaHistoryStorageOverride::Compact;
    uvsr::AntiAliasingSettings sharpeningPolicyChange = temporal;
    sharpeningPolicyChange.behaviorOverrides.sharpening =
        uvsr::TemporalAaAutoToggle::Off;
    passed &= Check(
        !uvsr::AntiAliasingSettingsRequireTemporalReset(
            temporal,
            storageChange) &&
            !uvsr::AntiAliasingSettingsRequireTemporalReset(
                storageChange,
                temporal) &&
            !uvsr::AntiAliasingSettingsRequireTemporalReset(
                temporal,
                sharpeningPolicyChange) &&
            !uvsr::AntiAliasingSettingsRequireTemporalReset(
                sharpeningPolicyChange,
                temporal),
        "physical history storage and presentation sharpening policies must "
        "remain outside the settings-level temporal image key");

    uvsr::AntiAliasingSettings depthValidationChange = temporal;
    depthValidationChange.behaviorOverrides.depthValidation =
        uvsr::TemporalAaDepthValidationOverride::MovingPoint;
    uvsr::AntiAliasingSettings historyWeightChange = temporal;
    historyWeightChange.behaviorOverrides.historyWeight =
        uvsr::TemporalAaHistoryWeightPolicyOverride::ImmediateHorizon;
    uvsr::AntiAliasingSettings motionTrustChange = temporal;
    motionTrustChange.behaviorOverrides.motionTrust =
        uvsr::TemporalAaMotionTrustOverride::SquaredSpeed;
    uvsr::AntiAliasingSettings rectificationClipChange = temporal;
    rectificationClipChange.behaviorOverrides.rectificationClip =
        uvsr::TemporalAaRectificationClipOverride::TightComponent;
    uvsr::AntiAliasingSettings blendDomainChange = temporal;
    blendDomainChange.behaviorOverrides.blendDomain =
        uvsr::TemporalAaBlendDomainOverride::LinearRgb;
    passed &= Check(
        uvsr::AntiAliasingSettingsRequireTemporalReset(
            temporal,
            depthValidationChange) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                depthValidationChange,
                temporal) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                temporal,
                historyWeightChange) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                historyWeightChange,
                temporal) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                temporal,
                motionTrustChange) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                motionTrustChange,
                temporal) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                temporal,
                rectificationClipChange) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                rectificationClipChange,
                temporal) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                temporal,
                blendDomainChange) &&
            uvsr::AntiAliasingSettingsRequireTemporalReset(
                blendDomainChange,
                temporal),
        "all five runtime-uniform image policies must reset temporal history "
        "independently and symmetrically");

    uvsr::AntiAliasingSettings motionChange = temporal;
    motionChange.algorithmOverrides.motionSource =
        uvsr::TemporalAaMotionSourceOverride::Center;
    passed &= Check(
        uvsr::AntiAliasingSettingsRequireTemporalReset(
            temporal,
            motionChange),
        "effective TAA motion-ownership changes must reset history");

    uvsr::AntiAliasingSettings spatialChange = temporal;
    spatialChange.algorithmOverrides.subpixelMorphology =
        uvsr::MorphologyApplicationOverride::
            ConservativeMorphological;
    passed &= Check(
        !uvsr::AntiAliasingSettingsRequireTemporalReset(
            temporal,
            spatialChange),
        "presentation-only morphology changes must preserve TAA history");

    uvsr::AntiAliasingSettings morphologyQualityChange = temporal;
    morphologyQualityChange.algorithmOverrides.subpixelMorphology =
        uvsr::MorphologyApplicationOverride::ConservativeMorphological;
    morphologyQualityChange.algorithmOverrides.morphologyQuality =
        static_cast<int32_t>(Quality::Ultra);
    const auto morphologyQualityResolved =
        uvsr::ResolveAntiAliasingSettings(morphologyQualityChange);
    passed &= Check(
        morphologyQualityResolved.implementation ==
                Preset::TemporalBalanced &&
            morphologyQualityResolved.quality == Quality::Medium &&
            morphologyQualityResolved.morphologyQuality ==
                Quality::Ultra &&
            morphologyQualityResolved.historyFrames == 6u,
        "changing presentation morphology quality must not change the Temporal preset");
    passed &= Check(
        !uvsr::AntiAliasingSettingsRequireTemporalReset(
            temporal,
            morphologyQualityChange),
        "presentation morphology quality must preserve TAA history");

    uvsr::AntiAliasingSettings multisampleMorphology;
    multisampleMorphology.method = Method::Msaa;
    multisampleMorphology.quality = Quality::Low;
    multisampleMorphology.algorithmOverrides.subpixelMorphology =
        uvsr::MorphologyApplicationOverride::ConservativeMorphological;
    multisampleMorphology.algorithmOverrides.morphologyQuality =
        static_cast<int32_t>(Quality::Ultra);
    const auto multisampleMorphologyResolved =
        uvsr::ResolveAntiAliasingSettings(multisampleMorphology);
    passed &= Check(
        multisampleMorphologyResolved.implementation == Preset::Msaa2x &&
            multisampleMorphologyResolved.rasterSampleCount == 2u &&
            multisampleMorphologyResolved.quality == Quality::Low &&
            multisampleMorphologyResolved.morphologyQuality ==
                Quality::Ultra,
        "changing presentation morphology quality must not change the Multisample preset");

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
        clampedResolved.historyFrames == 32u &&
            clampedResolved.historyStrength == 2.f,
        "history sliders must clamp to their documented ranges");
    passed &= Check(
        uvsr::ClampTemporalAaHistoryStrength(-1.f) == 0.f &&
            uvsr::ClampTemporalAaHistoryStrength(0.f) == 0.f &&
            uvsr::ClampTemporalAaHistoryStrength(1.f) == 1.f &&
            uvsr::ClampTemporalAaHistoryStrength(2.f) == 2.f &&
            uvsr::ClampTemporalAaHistoryStrength(3.f) == 2.f &&
        uvsr::ApplyTemporalAaHistoryStrength(
            0.f, 2.f, 0.9f) == 0.f &&
            uvsr::ApplyTemporalAaHistoryStrength(
                0.25f, 2.f, 0.9f) == 0.5f &&
            uvsr::ApplyTemporalAaHistoryStrength(
                0.75f, 2.f, 0.9f) == 0.9f,
        "200-percent history strength must preserve rejection and respect the horizon cap");

    uvsr::AntiAliasingSettings redundantOverrides;
    redundantOverrides.algorithmOverrides.motionSource =
        uvsr::TemporalAaMotionSourceOverride::
            CenterFirstEdgeDilation;
    redundantOverrides.algorithmOverrides.currentReconstruction =
        uvsr::TemporalAaCurrentReconstructionOverride::Direct;
    redundantOverrides.algorithmOverrides.historyFilter =
        uvsr::TemporalAaHistoryFilterOverride::Bilinear;
    redundantOverrides.algorithmOverrides.rectification =
        uvsr::TemporalAaRectificationOverride::PairRgb;
    redundantOverrides.algorithmOverrides.sampleResurrection =
        uvsr::TemporalAaSampleResurrectionOverride::Off;
    redundantOverrides.algorithmOverrides.subpixelMorphology =
        uvsr::MorphologyApplicationOverride::Off;
    uvsr::NormalizeRedundantAntiAliasingOverrides(
        redundantOverrides);
    passed &= Check(
        redundantOverrides.algorithmOverrides.motionSource ==
                uvsr::TemporalAaMotionSourceOverride::FromPreset &&
            redundantOverrides.algorithmOverrides.currentReconstruction ==
                uvsr::TemporalAaCurrentReconstructionOverride::
                    FromPreset &&
            redundantOverrides.algorithmOverrides.historyFilter ==
                uvsr::TemporalAaHistoryFilterOverride::FromPreset &&
            redundantOverrides.algorithmOverrides.rectification ==
                uvsr::TemporalAaRectificationOverride::FromPreset &&
            redundantOverrides.algorithmOverrides.sampleResurrection ==
                uvsr::TemporalAaSampleResurrectionOverride::FromPreset &&
            redundantOverrides.algorithmOverrides.subpixelMorphology ==
                uvsr::MorphologyApplicationOverride::FromPreset,
        "redundant Aliasing overrides must normalize only at an explicit "
        "settings transition");

    uvsr::AntiAliasingSettings distinctOverrides;
    distinctOverrides.algorithmOverrides.motionSource =
        uvsr::TemporalAaMotionSourceOverride::Center;
    distinctOverrides.algorithmOverrides.subpixelMorphology =
        uvsr::MorphologyApplicationOverride::
            ConservativeMorphological;
    uvsr::NormalizeRedundantAntiAliasingOverrides(distinctOverrides);
    passed &= Check(
        distinctOverrides.algorithmOverrides.motionSource ==
                uvsr::TemporalAaMotionSourceOverride::Center &&
            distinctOverrides.algorithmOverrides.subpixelMorphology ==
                uvsr::MorphologyApplicationOverride::
                    ConservativeMorphological,
        "normalization must retain image-changing Aliasing overrides");

#if !UVSR_AA_DEVELOPER_OVERRIDES
    uvsr::AntiAliasingSettings productionControls;
    productionControls.temporalCostMode = Cost::FullQuality;
    productionControls.algorithmOverrides.motionSource =
        uvsr::TemporalAaMotionSourceOverride::ClosestCross;
    productionControls.algorithmOverrides.currentReconstruction =
        uvsr::TemporalAaCurrentReconstructionOverride::Direct;
    productionControls.algorithmOverrides.historyFilter =
        uvsr::TemporalAaHistoryFilterOverride::NineTapCatmullRom;
    productionControls.algorithmOverrides.rectification =
        uvsr::TemporalAaRectificationOverride::VarianceYCoCg;
    productionControls.algorithmOverrides.subpixelMorphology =
        uvsr::MorphologyApplicationOverride::
            ConservativeMorphological;
    productionControls.algorithmOverrides.sampleResurrection =
        uvsr::TemporalAaSampleResurrectionOverride::TwoOlderFrames;
    productionControls.behaviorOverrides.historyStorage =
        uvsr::TemporalAaHistoryStorageOverride::Compact;
    productionControls.behaviorOverrides.depthValidation =
        uvsr::TemporalAaDepthValidationOverride::MovingPoint;
    productionControls.behaviorOverrides.historyWeight =
        uvsr::TemporalAaHistoryWeightPolicyOverride::ImmediateHorizon;
    productionControls.behaviorOverrides.motionTrust =
        uvsr::TemporalAaMotionTrustOverride::SquaredSpeed;
    productionControls.behaviorOverrides.rectificationClip =
        uvsr::TemporalAaRectificationClipOverride::TightComponent;
    productionControls.behaviorOverrides.blendDomain =
        uvsr::TemporalAaBlendDomainOverride::LinearRgb;
    productionControls.behaviorOverrides.sharpening =
        uvsr::TemporalAaAutoToggle::Off;
    productionControls.performanceOverrides.sharedWorkReuse =
        uvsr::TemporalAaAutoToggle::On;
    const auto compiledControls =
        uvsr::GetCompiledAntiAliasingSettings(productionControls);
    const auto compiledControlsResolved =
        uvsr::ResolveCompiledAntiAliasingSettings(productionControls);
#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
    passed &= Check(
        compiledControls.algorithmOverrides.motionSource ==
                productionControls.algorithmOverrides.motionSource &&
            compiledControls.algorithmOverrides.currentReconstruction ==
                productionControls.algorithmOverrides.currentReconstruction &&
            compiledControls.algorithmOverrides.historyFilter ==
                productionControls.algorithmOverrides.historyFilter &&
            compiledControls.algorithmOverrides.rectification ==
                productionControls.algorithmOverrides.rectification &&
            compiledControls.algorithmOverrides.subpixelMorphology ==
                productionControls.algorithmOverrides.subpixelMorphology &&
            compiledControls.algorithmOverrides.sampleResurrection ==
                productionControls.algorithmOverrides.sampleResurrection &&
            compiledControls.behaviorOverrides ==
                productionControls.behaviorOverrides &&
            !compiledControls.performanceOverrides.IsCustom() &&
            compiledControlsResolved.sampleResurrection ==
                uvsr::TemporalAaSampleResurrection::TwoOlderFrames &&
            compiledControlsResolved.depthValidation ==
                DepthValidation::MovingPoint &&
            compiledControlsResolved.historyWeight ==
                HistoryWeight::ImmediateHorizon &&
            compiledControlsResolved.motionTrust ==
                MotionTrust::SquaredSpeed &&
            compiledControlsResolved.rectificationClip ==
                RectificationClip::TightComponent &&
            compiledControlsResolved.blendDomain ==
                BlendDomain::LinearRgb &&
            uvsr::CompiledAntiAliasingSettingsRequireTemporalReset(
                temporal,
                depthValidationChange) &&
            uvsr::CompiledAntiAliasingSettingsRequireTemporalReset(
                temporal,
                fullResurrection) &&
            !uvsr::CompiledAntiAliasingSettingsRequireTemporalReset(
                temporal,
                performanceChange),
        "production must retain runtime-uniform behavior and Full Quality "
        "resurrection while stripping execution experiments");
#else
    passed &= Check(
        compiledControls.algorithmOverrides.sampleResurrection ==
                uvsr::TemporalAaSampleResurrectionOverride::FromPreset &&
            compiledControlsResolved.sampleResurrection ==
                uvsr::TemporalAaSampleResurrection::Off &&
            !uvsr::CompiledAntiAliasingSettingsRequireTemporalReset(
                temporal,
                fullResurrection) &&
            !compiledControls.performanceOverrides.IsCustom(),
        "the factory-startup shader experiment must strip resurrection and "
        "execution overrides that its reduced bundle does not package");
#endif
#endif

    std::array<bool, uvsr::TemporalAaBlendPermutationCount>
        observedTaaPermutations{};
    uint32_t observedTaaPermutationCount = 0u;
    for (uint32_t motion = 0u;
        motion < uvsr::TemporalAaMotionSourceCount;
        ++motion)
    for (uint32_t current = 0u;
        current < uvsr::TemporalAaCurrentReconstructionCount;
        ++current)
    for (uint32_t history = 0u;
        history < uvsr::TemporalAaHistoryFilterCount;
        ++history)
    for (uint32_t rectification = 0u;
        rectification < uvsr::TemporalAaRectificationCount;
        ++rectification)
    {
        uvsr::TemporalAaOptions options;
        options.motionSource =
            static_cast<uvsr::TemporalAaMotionSource>(motion);
        options.currentReconstruction =
            static_cast<uvsr::TemporalAaCurrentReconstruction>(current);
        options.historyFilter =
            static_cast<uvsr::TemporalAaHistoryFilter>(history);
        options.rectification =
            static_cast<uvsr::TemporalAaRectification>(rectification);
        const uint32_t index =
            uvsr::GetTemporalAaBlendPermutationIndex(options);
        if (index < observedTaaPermutations.size() &&
            !observedTaaPermutations[index])
        {
            observedTaaPermutations[index] = true;
            ++observedTaaPermutationCount;
        }
    }
    passed &= Check(
        observedTaaPermutationCount ==
            uvsr::TemporalAaBlendPermutationCount,
        "all 48 retained TAA algorithms must have unique PSO indices");
    passed &= Check(
        static_cast<uint32_t>(
            uvsr::TemporalAaRectification::PairRgb) == 0u &&
            static_cast<uint32_t>(
                uvsr::TemporalAaRectification::VarianceYCoCg) == 1u &&
            uvsr::TemporalAaRectificationCount == 2u,
        "retained rectification modes must use the compact Pair/Variance ABI");
    passed &= Check(
        static_cast<uint32_t>(uvsr::TemporalAaDebugView::Off) == 0u &&
            static_cast<uint32_t>(
                uvsr::TemporalAaDebugView::FinalHistoryWeight) == 1u &&
            static_cast<uint32_t>(
                uvsr::TemporalAaDebugView::SampleResurrection) == 2u &&
            uvsr::TemporalAaResolveDebugViewCount == 3u,
        "retained TAA debug views must use compact contiguous IDs");
    passed &= Check(
        uvsr::GetTemporalAaHistoryBytes(1920u, 1080u) ==
                uint64_t(1920u) * 1080u * 24u &&
            uvsr::GetTemporalAaMinimumHistoryBytes(
                1920u, 1080u, 4u, 2u) ==
                uint64_t(1920u) * 1080u * 12u &&
            uvsr::GetTemporalAaMinimumHistoryBytes(
                1920u, 1080u, 8u, 4u) ==
                uint64_t(1920u) * 1080u * 24u &&
            uvsr::GetTemporalAaResidentHistoryBytes(
                1920u, 1080u, 4u, 2u, false) ==
                uint64_t(1920u) * 1080u * 36u &&
            uvsr::GetTemporalAaResidentHistoryBytes(
                1920u, 1080u, 8u, 4u, true) ==
                uint64_t(1920u) * 1080u * 72u &&
            uvsr::GetTemporalAaDebugBytes(1920u, 1080u) ==
                uint64_t(1920u) * 1080u * 2u,
        "active and resident temporal-history arithmetic changed");

    passed &= Check(
        uvsr::GetTemporalAaHistoryColorSampleCount(
            uvsr::TemporalAaHistoryFilter::Bilinear) == 1u &&
            uvsr::GetTemporalAaHistoryColorSampleCount(
                uvsr::TemporalAaHistoryFilter::
                    OneSampleBicubic) == 1u &&
            uvsr::GetTemporalAaHistoryColorSampleCount(
                uvsr::TemporalAaHistoryFilter::
                    FiveTapCatmullRom) == 5u &&
            uvsr::GetTemporalAaHistoryColorSampleCount(
                uvsr::TemporalAaHistoryFilter::
                    NineTapCatmullRom) == 9u &&
            uvsr::GetTemporalAaHistoryDepthSampleCount(
                uvsr::TemporalAaHistoryFilter::
                    NineTapCatmullRom) == 8u,
        "history-filter labels and real history fetch counts diverged");
    passed &= Check(
        std::string(uvsr::GetTemporalAaHistoryFilterLabel(
            uvsr::TemporalAaHistoryFilter::NineTapCatmullRom)) ==
                "9x Bicubic" &&
            std::string(uvsr::GetTemporalAaHistoryFilterOverrideLabel(
                uvsr::TemporalAaHistoryFilterOverride::
                    NineTapCatmullRom)) == "9x Bicubic" &&
            std::string(uvsr::GetTemporalAaCurrentReconstructionLabel(
                uvsr::TemporalAaCurrentReconstruction::DeJittered)) ==
                "De-Jittered" &&
            std::string(uvsr::GetTemporalAaMotionSourceLabel(
                uvsr::TemporalAaMotionSource::ClosestCross)) ==
                "Closest Cross" &&
            std::string(uvsr::GetTemporalAaMotionSourceLabel(
                uvsr::TemporalAaMotionSource::
                    CenterFirstEdgeDilation)) == "Edge Dilation",
        "normal TAA menu labels must expose the requested reconstruction, "
        "de-jittering, and motion-source choices");
    passed &= Check(
        std::string(uvsr::GetTemporalAaHistoryStorageLabel(
            Storage::Compact)) == "Compact R11/R16 Preferred" &&
            std::string(uvsr::GetTemporalAaDepthValidationLabel(
                DepthValidation::MovingPoint)) ==
                    "Stationary Bypass" &&
            std::string(
                uvsr::GetTemporalAaDepthValidationOverrideLabel(
                    uvsr::TemporalAaDepthValidationOverride::
                        MovingPoint)) == "Stationary Bypass" &&
            std::string(uvsr::GetTemporalAaHistoryWeightPolicyLabel(
                HistoryWeight::ImmediateHorizon)) ==
                    "Immediate Horizon" &&
            std::string(uvsr::GetTemporalAaMotionTrustLabel(
                MotionTrust::SquaredSpeed)) == "Squared Speed" &&
            std::string(uvsr::GetTemporalAaRectificationClipLabel(
                RectificationClip::TightComponent)) ==
                    "Tight Component" &&
            std::string(uvsr::GetTemporalAaBlendDomainLabel(
                BlendDomain::LinearRgb)) == "Linear RGB",
        "Developer Options must expose concrete labels for every Minimum "
        "image-behavior policy");

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
        const std::string applicationSource =
            ReadTextFile(sourceDirectory / "uvsr.cpp");
        const std::string minimumShader =
            ReadTextFile(
                sourceDirectory /
                "temporal_aa_minimum_cs.hlsl");
        const std::string qualityShader =
            ReadTextFile(
                sourceDirectory /
                "temporal_aa_blend_cs.hlsl");
        const std::string temporalCore =
            ReadTextFile(
                sourceDirectory /
                "temporal_aa_core.cpp");
        const std::string temporalPass =
            ReadTextFile(
                sourceDirectory /
                "temporal_aa.cpp");
        const std::string temporalOptions =
            ReadTextFile(
                sourceDirectory /
                "temporal_aa_options.h");
        const std::string cmaaSource =
            ReadTextFile(sourceDirectory / "cmaa2.cpp");

        passed &= Check(
            shaderManifest.find(
                "MSAA_VISIBILITY_SAMPLES={2,4,8,16}") !=
                    std::string::npos &&
                productionManifest.find(
                    "PBR_DEFERRED_MSAA_SAMPLES={2,4,8,16}") !=
                    std::string::npos,
            "developer and production shader bundles must compile MSAA 16x");
        passed &= Check(
            shaderManifest.find("smaa") == std::string::npos &&
                shaderManifest.find("SMAA") == std::string::npos &&
                productionManifest.find("smaa") == std::string::npos &&
                productionManifest.find("SMAA") == std::string::npos,
            "removed SMAA shaders returned to a shader manifest");
        passed &= Check(
            !std::filesystem::exists(sourceDirectory / "smaa.cpp") &&
                !std::filesystem::exists(sourceDirectory / "smaa.h") &&
                !std::filesystem::exists(
                    sourceDirectory / "third_party" / "smaa"),
            "SMAA sources and third-party assets must remain removed");
        passed &= Check(
            applicationSource.find(
                "##ComboPopupInteractionReady") ==
                    std::string::npos,
            "combo interaction state must remain inside the pinned ImGui patch");
        passed &= Check(
            applicationSource.find(
                "const float crosshairOpacity = std::max(") !=
                    std::string::npos &&
                applicationSource.find(
                    "pixelZoomRequested ? pixelZoomOpacity : 0.f") !=
                    std::string::npos &&
                applicationSource.find(
                    "AddCircleFilled(") != std::string::npos &&
                applicationSource.find(
                    "128.f * crosshairOpacity") !=
                    std::string::npos,
            "the fading centered crosshair must cover pixel zoom and material inspection");
        passed &= Check(
            shaderManifest.find("pixel_zoom_ps.hlsl") !=
                    std::string::npos &&
                productionManifest.find("pixel_zoom_ps.hlsl") !=
                    std::string::npos,
            "developer and production bundles must compile pixel zoom");
        passed &= Check(
            !minimumShader.empty() &&
                shaderManifest.find(
                    "temporal_aa_minimum_cs.hlsl -T cs -E main "
                    "-D TAA_RUNTIME_BEHAVIOR={0,1}") !=
                    std::string::npos &&
                productionManifest.find(
                    "temporal_aa_minimum_cs.hlsl -T cs -E main "
                    "-D TAA_RUNTIME_BEHAVIOR={0,1}") !=
                    std::string::npos &&
                minimumShader.find("[numthreads(8, 8, 1)]") !=
                    std::string::npos &&
                minimumShader.find(
                    "groupId.xy * uint2(16u, 8u)") !=
                    std::string::npos &&
                minimumShader.find(
                    "Pair-coherent early rejection") !=
                    std::string::npos &&
                minimumShader.find(
                    "clamp(TemporalBlendFactor, 0.0f, 2.0f)") !=
                    std::string::npos &&
                minimumShader.find(
                    "saturate(TemporalBlendFactor)") ==
                    std::string::npos,
            "the one-dispatch compact temporal path must remain packaged "
            "with its 16x8 pair-coherent topology and full strength range");
        passed &= Check(
            minimumShader.find("TemporalBehaviorEnabled") !=
                    std::string::npos &&
                minimumShader.find("#if TAA_RUNTIME_BEHAVIOR") !=
                    std::string::npos &&
                qualityShader.find("TemporalBehaviorEnabled") !=
                    std::string::npos &&
                qualityShader.find(
                    "UVSR_TAA_BEHAVIOR_IMMEDIATE_HISTORY_WEIGHT") !=
                    std::string::npos &&
                minimumShader.find(
                    "UVSR_TAA_BEHAVIOR_MOVING_POINT_DEPTH") !=
                    std::string::npos,
            "both temporal paths must consume the runtime-uniform behavior "
            "contract used by Developer Options");
        passed &= Check(
            qualityShader.find(
                "WriteDepthOutput(ST, CurDepth[ST])") ==
                    std::string::npos &&
                qualityShader.find(
                    "WriteDepthOutput(ST, LoadDepth(ldsIdx))") !=
                    std::string::npos &&
                qualityShader.find("DelayedPairBounds") !=
                    std::string::npos &&
                qualityShader.find("shareDelayedPairBounds") !=
                    std::string::npos,
            "the quality path must reuse cached depth and preserve delayed "
            "pair-coherent bounds");
        passed &= Check(
            temporalCore.find("clearTextureFloat") ==
                    std::string::npos &&
                temporalCore.find("PrepareForFirstWrite") !=
                    std::string::npos,
            "invalid temporal history must skip reset clears and rely on "
            "first-write validity");
        passed &= Check(
            temporalPass.find(
                "useMinimum != m_LastRenderUsedMinimum") !=
                    std::string::npos &&
                temporalPass.find(
                    "const bool compactHistoryRequested") !=
                    std::string::npos &&
                temporalPass.find(
                    "IsTemporalAaCompactHistoryCompatible(settings)") !=
                    std::string::npos &&
                temporalPass.find(
                    "TemporalAaCostMode::Reduced") !=
                    std::string::npos &&
                temporalPass.find(
                    "compact history fell back to the robust path") !=
                    std::string::npos &&
                temporalPass.find(
                    "behaviorFlags = GetTemporalAaBehaviorFlags(") !=
                    std::string::npos &&
                temporalPass.find("m_MinimumPipelines[0]") !=
                    std::string::npos &&
                temporalPass.find("m_MinimumPipelines[1]") !=
                    std::string::npos &&
                temporalPass.find(
                    "const uint32_t minimumBehaviorIndex") !=
                    std::string::npos &&
                temporalPass.find(
                    "behaviorFlags == minimumDefaultBehaviorFlags ? 0u : 1u") !=
                    std::string::npos &&
                temporalPass.find(
                    "m_MinimumPipelines[minimumBehaviorIndex]") !=
                    std::string::npos &&
                temporalPass.find(
                    "Resurrection may repair a locally rejected sample") !=
                    std::string::npos &&
                temporalPass.find(
                    "if (!m_LastHistoryInputValid)") !=
                    std::string::npos &&
                temporalOptions.find("compactHistory") ==
                    std::string::npos,
            "the render pass alone must own compact-history compatibility, "
            "fallback telemetry, layout transitions, snapshot invalidation, "
            "and behavior upload");
        passed &= Check(
            cmaaSource.find(
                "sourceDesc.format != nvrhi::Format::RGBA16_FLOAT") !=
                    std::string::npos &&
                cmaaSource.find(
                    "copyTexture requires format-compatible resources") !=
                    std::string::npos,
            "CMAA2 must reject an incompatible per-frame source before its "
            "format-preserving copy");
        passed &= Check(
            applicationSource.find(
                "\"Developer Options##AliasingDeveloperOptions\"") !=
                    std::string::npos &&
                applicationSource.find(
                    "\"Morphology##Developer\"") !=
                    std::string::npos &&
                applicationSource.find(
                    "\"Previous-Depth Validation\"") !=
                    std::string::npos &&
                applicationSource.find(
                    "\"History Weight\"") !=
                    std::string::npos &&
                applicationSource.find(
                    "\"Active History Memory\"") !=
                    std::string::npos &&
                applicationSource.find(
                    "\"Resident History Memory\"") !=
                    std::string::npos &&
                applicationSource.find(
                    "requested_temporal_cost_mode") !=
                    std::string::npos &&
                applicationSource.find(
                    "effective_temporal_cost_mode") !=
                    std::string::npos &&
                applicationSource.find("history_storage") !=
                    std::string::npos &&
                applicationSource.find(
                    "previous_depth_validation") !=
                    std::string::npos &&
                applicationSource.find(
                    "history_weight_policy") !=
                    std::string::npos &&
                applicationSource.find("motion_trust") !=
                    std::string::npos &&
                applicationSource.find("rectification_clip") !=
                    std::string::npos &&
                applicationSource.find("blend_domain") !=
                    std::string::npos,
            "Developer Options and requested/effective behavior telemetry "
            "must remain explicit");
    }

    return passed ? 0 : 1;
}
