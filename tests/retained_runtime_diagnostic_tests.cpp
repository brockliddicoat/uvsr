#include "retained_runtime_diagnostic.h"
#include "ui_settings_command_catalog.h"
#include "../tools/strict_json_contract.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    bool Require(bool condition, std::string_view message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }

    const uvsr::UiSettingsCommandDefinition* FindSetting(
        std::string_view name)
    {
        for (const auto& definition : uvsr::UiSettingsCommandCatalog)
        {
            if (definition.name == name)
                return &definition;
        }
        return nullptr;
    }

    std::string_view Value(
        const uvsr::RetainedRuntimeCase& runtimeCase,
        std::string_view name)
    {
        for (const auto& setting : runtimeCase.settings)
        {
            if (setting.first == name)
                return setting.second;
        }
        return {};
    }

    std::set<std::string> DomainValues(std::string_view name)
    {
        std::set<std::string> values;
        const uvsr::UiSettingsCommandDefinition* definition =
            FindSetting(name);
        if (!definition)
            return values;
        std::size_t offset = 0u;
        while (offset <= definition->domain.size())
        {
            const std::size_t separator =
                definition->domain.find('|', offset);
            values.emplace(definition->domain.substr(
                offset,
                separator == std::string_view::npos
                    ? std::string_view::npos
                    : separator - offset));
            if (separator == std::string_view::npos)
                break;
            offset = separator + 1u;
        }
        return values;
    }

    bool TestLinearReadback()
    {
        // IEEE 754 half: 0, 0.5, 1, and 2. Each neighboring RGB differs.
        constexpr std::array<std::uint16_t, 16> Pixels = {
            0x0000u, 0x0000u, 0x0000u, 0x3c00u,
            0x3c00u, 0x0000u, 0x0000u, 0x3c00u,
            0x0000u, 0x3800u, 0x0000u, 0x3c00u,
            0x0000u, 0x0000u, 0x4000u, 0x3c00u
        };
        const uvsr::RuntimeOutputEvidence valid =
            uvsr::AnalyzeRuntimeLinearRgba16(
                Pixels.data(), 2u, 2u, 16u);
        bool ok = true;
        ok &= Require(valid.linearReadbackValid,
            "Finite, varying RGBA16F input must pass readback analysis.");
        ok &= Require(
            valid.finiteComponentCount == 16u &&
                valid.nonFiniteComponentCount == 0u &&
                valid.varyingPixelCount == 3u &&
                valid.edgePixelCount == 2u &&
                valid.meanLinearLuminance > 0.178 &&
                valid.meanLinearLuminance < 0.179 &&
                valid.rmsLinearLuminance > 0.220 &&
                valid.rmsLinearLuminance < 0.221 &&
                valid.meanLinearHorizontalGradient == 1.75,
            "Readback analysis must count finite, varying, and edge data.");

        auto nonFinite = Pixels;
        nonFinite[0] = 0x7c00u;
        const uvsr::RuntimeOutputEvidence invalid =
            uvsr::AnalyzeRuntimeLinearRgba16(
                nonFinite.data(), 2u, 2u, 16u);
        ok &= Require(
            !invalid.linearReadbackValid &&
                invalid.nonFiniteComponentCount == 1u,
            "A non-finite GPU component must fail readback evidence.");
        ok &= Require(
            !uvsr::AnalyzeRuntimeLinearRgba16(
                Pixels.data(), 2u, 2u, 8u).linearReadbackValid,
            "An undersized readback row pitch must fail closed.");

        constexpr std::array<float, 16> FloatPixels = {
            0.f, 0.f, 0.f, 1.f,
            1.f, 0.f, 0.f, 1.f,
            0.f, 0.5f, 0.f, 1.f,
            0.f, 0.f, 2.f, 1.f
        };
        const uvsr::RuntimeOutputEvidence validFloat =
            uvsr::AnalyzeRuntimeLinearRgba32(
                FloatPixels.data(), 2u, 2u, 32u);
        ok &= Require(
            validFloat.linearReadbackValid &&
                validFloat.finiteComponentCount == 16u &&
                validFloat.nonFiniteComponentCount == 0u &&
                validFloat.varyingPixelCount == 3u &&
                validFloat.edgePixelCount == 2u &&
                validFloat.meanLinearLuminance > 0.178 &&
                validFloat.meanLinearLuminance < 0.179 &&
                validFloat.rmsLinearLuminance > 0.220 &&
                validFloat.rmsLinearLuminance < 0.221 &&
                validFloat.meanLinearHorizontalGradient == 1.75,
            "Finite, varying RGBA32F input must pass readback analysis.");
        auto nonFiniteFloat = FloatPixels;
        nonFiniteFloat[0] = std::numeric_limits<float>::infinity();
        ok &= Require(
            !uvsr::AnalyzeRuntimeLinearRgba32(
                nonFiniteFloat.data(), 2u, 2u, 32u)
                .linearReadbackValid,
            "A non-finite RGBA32F component must fail readback evidence.");
        ok &= Require(
            !uvsr::AnalyzeRuntimeLinearRgba32(
                FloatPixels.data(), 2u, 2u, 16u).linearReadbackValid,
            "An undersized RGBA32F row pitch must fail closed.");

        constexpr uvsr::RuntimeLinearReadbackLayout Layout{
            640u, 360u, 38u, 1u
        };
        ok &= Require(
            uvsr::RuntimeLinearReadbackLayoutsMatch(Layout, Layout) &&
                !uvsr::RuntimeLinearReadbackLayoutsMatch(
                    Layout, { 641u, 360u, 38u, 1u }) &&
                !uvsr::RuntimeLinearReadbackLayoutsMatch(
                    Layout, { 640u, 361u, 38u, 1u }) &&
                !uvsr::RuntimeLinearReadbackLayoutsMatch(
                    Layout, { 640u, 360u, 49u, 1u }) &&
                !uvsr::RuntimeLinearReadbackLayoutsMatch(
                    Layout, { 640u, 360u, 38u, 2u }),
            "Readback staging reuse must bind extent, format, and samples.");
        return ok;
    }

    bool TestCrossCaseSemanticSignatures()
    {
        uvsr::RuntimeOutputEvidence output;
        output.width = 640u;
        output.height = 360u;
        output.meanLinearLuminance = 0.25;
        output.rmsLinearLuminance = 0.5;
        output.meanLinearHorizontalGradient = 0.125;
        const uvsr::RuntimeSemanticSignature baseline =
            uvsr::BuildRuntimeSemanticSignature(
                output, 4u, 10.0, 8.0);
        uvsr::RuntimeSemanticSignature withinTolerance = baseline;
        withinTolerance.meanLinearLuminance *= 1.0005;
        withinTolerance.gpuFrameMilliseconds += 0.1;
        uvsr::RuntimeSemanticSignature visuallyDistinct = baseline;
        visuallyDistinct.meanLinearLuminance *= 1.02;
        uvsr::RuntimeSemanticSignature timingDistinct = baseline;
        timingDistinct.gpuFrameMilliseconds = 12.0;

        bool ok = true;
        ok &= Require(
            !uvsr::RuntimeSemanticSignaturesAreDistinct(
                baseline, withinTolerance),
            "Cross-case normalization must tolerate small rendered/timing jitter.");
        ok &= Require(
            uvsr::RuntimeSemanticSignaturesAreDistinct(
                baseline, visuallyDistinct) &&
                !uvsr::RuntimeSemanticSignaturesAreDistinct(
                    baseline, timingDistinct) &&
                uvsr::RuntimeSemanticTimingsAreDistinct(
                    baseline, timingDistinct),
            "Visible controls must require output differences; timing may "
            "distinguish only an explicitly performance-only contract.");

        const std::array<std::uint32_t, 5> Samples = {
            1u, 2u, 4u, 8u, 16u
        };
        std::vector<uvsr::RetainedRuntimeCase> cases;
        std::vector<uvsr::RetainedRuntimeSemanticCapture> captures;
        const auto appendFamily = [&]
        (
            std::string family,
            double familyOffset)
        {
            for (std::size_t index = 0u; index < Samples.size(); ++index)
            {
                const std::uint32_t samples = Samples[index];
                uvsr::RetainedRuntimeCase runtimeCase;
                runtimeCase.name = family + "-" +
                    std::to_string(samples) + "x";
                runtimeCase.semanticFamily = family;
                runtimeCase.semanticDomain = "test-domain";
                runtimeCase.requireCrossCaseDistinctness = true;
                runtimeCase.expectedSampleCount = samples;
                cases.push_back(runtimeCase);

                for (const std::string_view scene : {
                    "bistro_interior_retextured",
                    "san_miguel_retextured" })
                {
                    uvsr::RetainedRuntimeSemanticCapture capture;
                    capture.caseName = runtimeCase.name;
                    capture.family = family;
                    capture.domain = runtimeCase.semanticDomain;
                    capture.sceneToken = scene;
                    capture.signature = baseline;
                    capture.signature.receiverSampleCount = samples;
                    capture.signature.meanLinearLuminance +=
                        familyOffset + double(index) * 0.02;
                    captures.push_back(std::move(capture));
                }
            }
        };
        appendFamily("family-a", 0.0);
        appendFamily("family-b", 0.2);

        std::string reason;
        ok &= Require(
            uvsr::ValidateRetainedRuntimeSemanticCaptures(
                cases, captures, reason),
            "Complete distinct family/domain signatures must pass: " + reason);

        auto missing = captures;
        missing.pop_back();
        ok &= Require(
            !uvsr::ValidateRetainedRuntimeSemanticCaptures(
                cases, missing, reason),
            "Missing per-case semantic evidence must fail closed.");

        auto identicalMsaa = captures;
        for (auto& capture : identicalMsaa)
        {
            if (capture.family == "family-a")
            {
                capture.signature.meanLinearLuminance = 0.25;
                capture.signature.rmsLinearLuminance = 0.5;
                capture.signature.meanLinearHorizontalGradient = 0.125;
                capture.signature.cpuFrameMilliseconds = 10.0;
                capture.signature.gpuFrameMilliseconds = 8.0;
            }
        }
        ok &= Require(
            !uvsr::ValidateRetainedRuntimeSemanticCaptures(
                cases, identicalMsaa, reason),
            "A family whose MSAA mappings are semantically identical must fail.");

        auto sampleInvariantCases = cases;
        for (auto& runtimeCase : sampleInvariantCases)
        {
            if (runtimeCase.semanticFamily == "family-a")
                runtimeCase.requireCrossSampleDistinctness = false;
        }
        ok &= Require(
            uvsr::ValidateRetainedRuntimeSemanticCaptures(
                sampleInvariantCases, identicalMsaa, reason),
            "An explicitly sample-invariant presentation must retain option "
            "comparisons without demanding different pixels per MSAA count: " +
                reason);

        auto identicalOptions = captures;
        for (auto& right : identicalOptions)
        {
            if (right.family != "family-b")
                continue;
            const auto left = std::find_if(
                identicalOptions.begin(), identicalOptions.end(),
                [&right](const auto& candidate)
                {
                    return candidate.family == "family-a" &&
                        candidate.sceneToken == right.sceneToken &&
                        candidate.signature.receiverSampleCount ==
                            right.signature.receiverSampleCount;
                });
            if (left != identicalOptions.end())
                right.signature = left->signature;
        }
        ok &= Require(
            !uvsr::ValidateRetainedRuntimeSemanticCaptures(
                cases, identicalOptions, reason),
            "Two option families mapping to identical output/timing must fail.");
        return ok;
    }

    bool TestMatrix()
    {
        const std::vector<uvsr::RetainedRuntimeCase> cases =
            uvsr::BuildRetainedRuntimeCases(
                "bistro_interior_retextured.scene.json",
                "san_miguel_retextured.scene.json");
        bool ok = true;
        ok &= Require(cases.size() == 1016u,
            "The retained runtime matrix must contain 184 complete "
            "cross-case AO/GI families plus 96 retained non-AO/GI cases.");

        std::set<std::string> names;
        std::array<std::size_t, 5> screenBySample{};
        std::array<std::size_t, 5> directionalBySample{};
        std::array<std::size_t, 5> skyBySample{};
        std::array<std::size_t, 5> flashlightBySample{};
        std::array<std::size_t, 5> flashlightLightingBySample{};
        std::array<std::size_t, 5> unshadowedFlashlightPairs{};
        std::array<std::size_t, 5> shadowedFlashlightPairs{};
        std::array<std::size_t, 5> shadowDenoisingBySample{};
        std::array<std::size_t, 5> skyDenoisingBySample{};
        std::array<std::size_t, 5> bistroBySample{};
        std::array<std::size_t, 5> sanBySample{};
        std::set<std::string> visibilityQualities;
        std::set<std::string> visibilityEstimators;
        std::set<std::string> visibilityResolutions;
        std::set<std::string> visibilitySamples;
        std::set<std::string> aoMethods;
        std::set<std::string> giMethods;
        std::set<std::string> shadowMethods;
        std::set<std::string> skyMethods;
        std::set<std::string> globalNoiseCombinations;
        std::set<std::string> autoExposureStates;
        std::set<std::string> autoExposureCompensation;
        std::set<std::string> autoExposureBrightening;
        std::set<std::string> autoExposureDarkening;
        std::set<std::string> autoExposurePeriods;
        std::map<std::string, std::set<std::string>>
            sceneSampleCoverageByFamily;
        std::map<std::string, std::set<std::uint32_t>>
            semanticSamplesByFamily;
        std::set<std::string> semanticDomains;
        std::set<std::string> continuousAssignments;
        std::size_t zeroAoStrengthVariants = 0u;
        std::size_t zeroGiIntensityVariants = 0u;
        std::set<std::string> visibilityDebugViews;
        std::set<std::uint32_t> mixedSceneSamples;
        bool sawSnapshot = false;
        bool sawCamera = false;
        bool sawResize = false;
        bool sawReset = false;
        bool sawPathBistro = false;
        bool sawPathSan = false;
        std::set<std::string> pathRestartCases;

        const auto sampleSlot = [](std::uint32_t samples) -> std::size_t
        {
            switch (samples)
            {
            case 1u: return 0u;
            case 2u: return 1u;
            case 4u: return 2u;
            case 8u: return 3u;
            case 16u: return 4u;
            default: return 5u;
            }
        };

        for (const uvsr::RetainedRuntimeCase& runtimeCase : cases)
        {
            ok &= Require(names.insert(runtimeCase.name).second,
                "Runtime case names must be unique.");
            const std::size_t slot = sampleSlot(
                runtimeCase.expectedSampleCount);
            ok &= Require(slot < screenBySample.size(),
                "Every runtime case must declare a retained sample count.");
            if (slot >= screenBySample.size())
                continue;

            ok &= Require(
                !Value(runtimeCase, "scene.current").empty() &&
                    !runtimeCase.expectedSceneToken.empty(),
                "Every runtime case must restore and verify an explicit scene baseline.");

            std::set<std::string> settingNames;
            for (const auto& setting : runtimeCase.settings)
            {
                const uvsr::UiSettingsCommandDefinition* definition =
                    FindSetting(setting.first);
                ok &= Require(definition != nullptr,
                    "Every runtime setting must exist in the live catalog.");
                ok &= Require(
                    definition &&
                        definition->kind !=
                            uvsr::UiSettingsCommandKind::Action &&
                        definition->Supports(
                            uvsr::UiSettingsCommandVerb::Set),
                    "Every runtime setting must be a settable Value.");
                ok &= Require(settingNames.insert(setting.first).second,
                    "A runtime case must not assign one setting twice.");
            }

            if (runtimeCase.expectScreenVisibility)
            {
                const std::string_view aoMethod =
                    Value(runtimeCase, "denoising.ao.method");
                const std::string_view giMethod =
                    Value(runtimeCase, "denoising.gi.method");
                ok &= Require(
                    aoMethod != "reblur" ||
                        Value(runtimeCase,
                            "visibility.ao.output-hit-distance") == "on",
                    "Every positive ReBLUR AO row must produce hit distance.");
                ok &= Require(
                    (giMethod != "reblur" && giMethod != "relax") ||
                        Value(runtimeCase,
                            "visibility.gi.output-hit-distance") == "on",
                    "Every positive NRD GI row must produce hit distance.");
                ++screenBySample[slot];
                if (runtimeCase.requireCrossCaseDistinctness)
                {
                    ok &= Require(
                        runtimeCase.snapshotRoundTrip &&
                            runtimeCase.exerciseRetainedStateChanges &&
                            !runtimeCase.semanticFamily.empty() &&
                            !runtimeCase.semanticTimingOnly,
                        "Every named AO/GI configuration must reset/restore, "
                        "exercise all retained actions, and require rendered "
                        "output rather than timing-only evidence.");
                    semanticSamplesByFamily[runtimeCase.semanticFamily]
                        .insert(runtimeCase.expectedSampleCount);
                    if (!runtimeCase.semanticDomain.empty())
                        semanticDomains.insert(runtimeCase.semanticDomain);
                    const std::string_view scene =
                        Value(runtimeCase, "scene.current");
                    const bool startsInBistro =
                        runtimeCase.actionBaselineSceneToken ==
                            "bistro_interior_retextured" &&
                        scene.find("bistro") != std::string_view::npos;
                    const bool startsInSanMiguel =
                        runtimeCase.actionBaselineSceneToken ==
                            "san_miguel_retextured" &&
                        scene.find("san_miguel") != std::string_view::npos;
                    const bool endsInSanMiguel = startsInBistro &&
                        runtimeCase.expectedSceneToken ==
                            "san_miguel_retextured" &&
                        runtimeCase.actionValue.find("san_miguel") !=
                            std::string::npos;
                    const bool endsInBistro = startsInSanMiguel &&
                        runtimeCase.expectedSceneToken ==
                            "bistro_interior_retextured" &&
                        runtimeCase.actionValue.find("bistro") !=
                            std::string::npos;
                    ok &= Require(endsInBistro || endsInSanMiguel,
                        "Every AO/GI case must bind opposite initial/final scenes.");
                    const std::string sampleSuffix = "-" +
                        std::to_string(runtimeCase.expectedSampleCount) + "x";
                    const std::string sceneSuffix = startsInBistro
                        ? "-bistro-to-san-miguel" + sampleSuffix
                        : "-san-miguel-to-bistro" + sampleSuffix;
                    ok &= Require(
                        runtimeCase.name.size() > sceneSuffix.size() &&
                            runtimeCase.name.compare(
                                runtimeCase.name.size() - sceneSuffix.size(),
                                sceneSuffix.size(), sceneSuffix) == 0,
                        "Every AO/GI case name must encode scene and sample count.");
                    sceneSampleCoverageByFamily[runtimeCase.semanticFamily]
                        .insert("bistro-" +
                            std::to_string(runtimeCase.expectedSampleCount));
                    sceneSampleCoverageByFamily[runtimeCase.semanticFamily]
                        .insert("san-miguel-" +
                            std::to_string(runtimeCase.expectedSampleCount));
                    if (runtimeCase.semanticDomain ==
                        "ao-gi-continuous")
                    {
                        constexpr std::array<std::string_view, 13>
                            ContinuousControls = {
                                "visibility.radius",
                                "visibility.thickness",
                                "visibility.distribution",
                                "visibility.ao.strength",
                                "visibility.gi.intensity",
                                "denoising.ao.radius",
                                "denoising.gi.radius",
                                "denoising.ao.history",
                                "denoising.gi.history",
                                "denoising.ao.disocclusion",
                                "denoising.gi.disocclusion",
                                "denoising.ao.anti-lag",
                                "denoising.gi.anti-lag"
                            };
                        std::size_t assignedControlCount = 0u;
                        for (const std::string_view control :
                            ContinuousControls)
                        {
                            const std::string_view value =
                                Value(runtimeCase, control);
                            if (value.empty())
                                continue;
                            ++assignedControlCount;
                            continuousAssignments.emplace(
                                std::string(control) + "=" +
                                std::string(value));
                        }
                        ok &= Require(
                            assignedControlCount == 1u,
                            "Each continuous family must vary exactly one "
                            "control while every other control stays at its "
                            "factory baseline.");
                        if (Value(runtimeCase,
                                "visibility.ao.strength") == "0")
                        {
                            ++zeroAoStrengthVariants;
                            ok &= Require(
                                !runtimeCase
                                    .expectAmbientOcclusionDenoising,
                                "Zero AO strength must make AO and its "
                                "denoiser inactive.");
                        }
                        if (Value(runtimeCase,
                                "visibility.gi.intensity") == "0")
                        {
                            ++zeroGiIntensityVariants;
                            ok &= Require(
                                !runtimeCase
                                    .expectGlobalIlluminationDenoising,
                                "Zero GI intensity must make GI and its "
                                "denoiser inactive.");
                        }
                    }
                    if (runtimeCase.semanticDomain ==
                        "visibility-debug-view")
                    {
                        const std::string_view debugView = Value(
                            runtimeCase, "debug.visibility.view");
                        visibilityDebugViews.emplace(debugView);
                        const bool finalView = debugView == "final";
                        ok &= Require(
                            runtimeCase.expectLightingAccumulation ==
                                    finalView &&
                                !runtimeCase
                                    .expectAmbientOcclusionDenoising &&
                                !runtimeCase
                                    .expectGlobalIlluminationDenoising &&
                                runtimeCase
                                    .requireCrossSampleDistinctness ==
                                    finalView,
                            "Only the final visibility view may commit the "
                            "mean; accumulation mode must keep every debug "
                            "view on raw, undenoised, sample-invariant "
                            "signals.");
                    }
                    ++bistroBySample[slot];
                    ++sanBySample[slot];
                }
                else if (runtimeCase.name.rfind(
                        "scene-mixed-coverage-", 0u) == 0u)
                {
                    mixedSceneSamples.insert(
                        runtimeCase.expectedSampleCount);
                    ok &= Require(
                        runtimeCase.snapshotRoundTrip &&
                            runtimeCase.exerciseRetainedStateChanges &&
                            runtimeCase.expectSkyVisibility &&
                            runtimeCase.expectFlashlightVisibility,
                        "Combined scene coverage must retain its two-scene "
                        "sky/flashlight action matrix.");
                }

                const std::string_view noisePattern =
                    Value(runtimeCase, "noise.pattern");
                const std::string_view noiseResolution =
                    Value(runtimeCase, "noise.resolution");
                const std::string_view noiseAnimate =
                    Value(runtimeCase, "noise.animate-samples");
                const std::string_view noiseAccumulate =
                    Value(runtimeCase, "noise.accumulate-samples");
                if (!noisePattern.empty())
                {
                    const std::string_view debugView = Value(
                        runtimeCase, "debug.visibility.view");
                    const bool accumulationSelected =
                        noiseAccumulate == "on" &&
                        (debugView.empty() || debugView == "final");
                    globalNoiseCombinations.emplace(
                        std::string(noisePattern) + "|" +
                        std::string(noiseResolution) + "|" +
                        std::string(noiseAnimate) + "|" +
                        std::string(noiseAccumulate));
                    ok &= Require(
                        (runtimeCase.semanticDomain == "visibility-noise"
                            ? Value(runtimeCase,
                                "visibility.specify-noise") == "on"
                            : Value(runtimeCase,
                                "visibility.specify-noise") == "off") &&
                            runtimeCase.assertLightingAccumulationState &&
                            runtimeCase.expectLightingAccumulation ==
                                accumulationSelected,
                        "Global-noise rows must inherit the global texture, "
                        "custom visibility-noise rows must keep their override, "
                        "and accumulation must bind to its live switch.");
                }
            }
            if (runtimeCase.expectDirectionalVisibility)
                ++directionalBySample[slot];
            if (runtimeCase.expectSkyVisibility)
                ++skyBySample[slot];
            if (runtimeCase.expectFlashlightVisibility)
                ++flashlightBySample[slot];
            if (runtimeCase.expectFlashlightLightingSubmitted)
                ++flashlightLightingBySample[slot];
            if (runtimeCase.name.rfind(
                    "flashlight-visible-unshadowed-", 0u) == 0u)
            {
                ++unshadowedFlashlightPairs[slot];
                ok &= Require(
                    runtimeCase.assertFlashlightLightingState &&
                        runtimeCase.expectFlashlightLightingSubmitted &&
                        runtimeCase.assertFlashlightVisibilityState &&
                        !runtimeCase.expectFlashlightVisibility &&
                        runtimeCase.action ==
                            uvsr::RetainedRuntimeAction::ChangeSetting &&
                        runtimeCase.actionSettingName ==
                            "light.selected.flashlight.enabled" &&
                        runtimeCase.actionBaselineValue == "on" &&
                        runtimeCase.actionValue == "off" &&
                        runtimeCase.requireActionOutputDifference,
                    "Unshadowed flashlight rows must compare visible light "
                    "against an exact flashlight-off reference.");
            }
            if (runtimeCase.name.rfind(
                    "flashlight-visible-shadowed-", 0u) == 0u)
            {
                ++shadowedFlashlightPairs[slot];
                ok &= Require(
                    runtimeCase.assertFlashlightLightingState &&
                        runtimeCase.expectFlashlightLightingSubmitted &&
                        runtimeCase.assertFlashlightVisibilityState &&
                        runtimeCase.expectFlashlightVisibility &&
                        runtimeCase.action ==
                            uvsr::RetainedRuntimeAction::ChangeSetting &&
                        runtimeCase.actionSettingName ==
                            "light.selected.flashlight.cast-shadows" &&
                        runtimeCase.actionBaselineValue == "on" &&
                        runtimeCase.actionValue == "off" &&
                        runtimeCase.requireActionOutputDifference,
                    "Shadowed flashlight rows must compare shadow dispatch "
                    "against the same visible unshadowed light.");
            }
            if (runtimeCase.expectShadowDenoising)
                ++shadowDenoisingBySample[slot];
            if (runtimeCase.expectSkyDenoising)
                ++skyDenoisingBySample[slot];
            ok &= Require(
                !runtimeCase.expectLightingAccumulation ||
                    (!runtimeCase.expectAmbientOcclusionDenoising &&
                        !runtimeCase.expectGlobalIlluminationDenoising),
                "Cumulative lighting must be the sole AO/GI temporal owner.");

            if (Value(runtimeCase, "scene.current").find("bistro") !=
                std::string_view::npos)
            {
                ++bistroBySample[slot];
            }
            if (Value(runtimeCase, "scene.current").find("san_miguel") !=
                std::string_view::npos)
            {
                ++sanBySample[slot];
            }

            const auto remember = [&](std::string_view setting,
                                      std::set<std::string>& values)
            {
                const std::string_view value = Value(runtimeCase, setting);
                if (!value.empty())
                    values.emplace(value);
            };
            remember("visibility.quality", visibilityQualities);
            remember("visibility.estimator", visibilityEstimators);
            remember("visibility.resolution", visibilityResolutions);
            remember("visibility.samples", visibilitySamples);
            remember("denoising.ao.method", aoMethods);
            remember("denoising.gi.method", giMethods);
            remember("denoising.shadows.method", shadowMethods);
            remember("denoising.sky.method", skyMethods);
            remember("sky.auto-exposure.enabled", autoExposureStates);
            remember(
                "sky.auto-exposure.exposure-compensation",
                autoExposureCompensation);
            remember(
                "sky.auto-exposure.maximum-brightening",
                autoExposureBrightening);
            remember(
                "sky.auto-exposure.maximum-darkening",
                autoExposureDarkening);
            remember(
                "sky.auto-exposure.adjustment-period",
                autoExposurePeriods);
            if (!Value(runtimeCase, "sky.auto-exposure.enabled").empty())
            {
                ok &= Require(
                    runtimeCase.assertAutoExposureState &&
                        runtimeCase.expectAutoExposure ==
                            (Value(runtimeCase,
                                "sky.auto-exposure.enabled") == "on"),
                    "Auto-exposure rows must bind their setting to live dispatch evidence.");
            }

            sawSnapshot = sawSnapshot || runtimeCase.snapshotRoundTrip;
            sawCamera = sawCamera ||
                runtimeCase.exerciseRetainedStateChanges ||
                runtimeCase.action ==
                    uvsr::RetainedRuntimeAction::NudgeCamera;
            sawResize = sawResize ||
                runtimeCase.exerciseRetainedStateChanges ||
                runtimeCase.action ==
                    uvsr::RetainedRuntimeAction::ResizeViewport;
            sawReset = sawReset || runtimeCase.snapshotRoundTrip;
            if (runtimeCase.requirePathHistoryRestart)
            {
                ok &= Require(
                    runtimeCase.action !=
                        uvsr::RetainedRuntimeAction::None,
                    "Every PT restart row must name exactly one causal action.");
                pathRestartCases.insert(runtimeCase.name);
            }
            const bool pathTracing =
                Value(runtimeCase, "lighting.solution") == "path-tracing";
            sawPathBistro = sawPathBistro ||
                (pathTracing && runtimeCase.expectedSceneToken.find("bistro") !=
                    std::string::npos);
            sawPathSan = sawPathSan ||
                (pathTracing && runtimeCase.expectedSceneToken.find("san_miguel") !=
                    std::string::npos);
        }

        const std::set<std::string> completeSceneSampleCoverage = {
            "bistro-1", "bistro-2", "bistro-4", "bistro-8",
            "bistro-16", "san-miguel-1", "san-miguel-2",
            "san-miguel-4", "san-miguel-8", "san-miguel-16"
        };
        for (const auto& [family, coverage] :
            sceneSampleCoverageByFamily)
        {
            ok &= Require(
                coverage == completeSceneSampleCoverage,
                "Every AO/GI option family must cover both scenes at "
                "1x/2x/4x/8x/16x.");
        }
        const std::set<std::uint32_t> completeSemanticSamples = {
            1u, 2u, 4u, 8u, 16u
        };
        const auto countSemanticFamilies = [
            &semanticSamplesByFamily](std::string_view prefix)
        {
            return std::count_if(
                semanticSamplesByFamily.begin(),
                semanticSamplesByFamily.end(),
                [prefix](const auto& family)
                {
                    return family.first.rfind(prefix, 0u) == 0u;
                });
        };
        ok &= Require(
            sceneSampleCoverageByFamily.size() == 184u &&
                semanticSamplesByFamily.size() == 184u &&
                countSemanticFamilies("ao-gi-continuous-") == 26 &&
                countSemanticFamilies("debug-visibility-view-") == 4 &&
                countSemanticFamilies("global-noise-domain-") == 48 &&
                mixedSceneSamples == completeSemanticSamples,
            "The matrix must contain 184 full-checklist AO/GI families: "
            "26 held-constant continuous bounds, four debug views, and all "
            "48 global-noise combinations among the retained domains.");
        for (const auto& [family, samples] : semanticSamplesByFamily)
        {
            ok &= Require(
                samples == completeSemanticSamples,
                "Every semantic AO/GI family must record normalized output "
                "and timing signatures at 1x/2x/4x/8x/16x.");
        }
        ok &= Require(
            semanticDomains == std::set<std::string>{
                "ao-gi-continuous", "ao-gi-denoising-resolution", "ao-gi-filter",
                "ao-gi-hit-precision", "ao-gi-mode", "ao-gi-quality",
                "global-noise",
                "visibility-estimator", "visibility-noise",
                "visibility-preset", "visibility-resolution",
                "visibility-sample-quality", "visibility-debug-view" },
            "Cross-case comparisons must name every retained AO/GI option "
            "domain that claims a visible or performance tradeoff.");
        const std::set<std::string> expectedContinuousAssignments = {
            "visibility.radius=0.1", "visibility.radius=10",
            "visibility.thickness=0.01", "visibility.thickness=2",
            "visibility.distribution=0.25", "visibility.distribution=8",
            "visibility.ao.strength=0", "visibility.ao.strength=8",
            "visibility.gi.intensity=0", "visibility.gi.intensity=16",
            "denoising.ao.radius=1", "denoising.ao.radius=8",
            "denoising.gi.radius=1", "denoising.gi.radius=8",
            "denoising.ao.history=1", "denoising.ao.history=32",
            "denoising.gi.history=1", "denoising.gi.history=32",
            "denoising.ao.disocclusion=0.001",
            "denoising.ao.disocclusion=0.1",
            "denoising.gi.disocclusion=0.001",
            "denoising.gi.disocclusion=0.1",
            "denoising.ao.anti-lag=0", "denoising.ao.anti-lag=1",
            "denoising.gi.anti-lag=0", "denoising.gi.anti-lag=1"
        };
        ok &= Require(
            continuousAssignments == expectedContinuousAssignments,
            "Every continuous AO/GI control must have its own isolated "
            "minimum/maximum family.");
        ok &= Require(
            zeroAoStrengthVariants == 5u &&
                zeroGiIntensityVariants == 5u,
            "Zero-strength AO and zero-intensity GI must each cover every "
            "retained sample count without demanding an inactive denoiser.");
        ok &= Require(
            visibilityDebugViews == DomainValues("debug.visibility.view"),
            "Every live visibility debug view must have a full-checklist "
            "runtime family.");

        for (std::size_t slot = 0u; slot < 5u; ++slot)
        {
            ok &= Require(
                screenBySample[slot] > 0u &&
                    directionalBySample[slot] > 0u &&
                    skyBySample[slot] > 0u &&
                    flashlightLightingBySample[slot] > 0u &&
                    flashlightBySample[slot] > 0u &&
                    bistroBySample[slot] > 0u &&
                    sanBySample[slot] > 0u,
                "AO/GI, directional, sky, flashlight, Bistro, and San Miguel "
                "must cover 1x/2x/4x/8x/16x.");
            ok &= Require(
                unshadowedFlashlightPairs[slot] == 1u &&
                    shadowedFlashlightPairs[slot] == 1u,
                "Each retained sample count must have one flashlight-on/"
                "off lighting comparison and one shadow-on/off comparison.");
            if (slot > 0u)
            {
                ok &= Require(
                    shadowDenoisingBySample[slot] > 0u &&
                        skyDenoisingBySample[slot] > 0u,
                    "Sky and flashlight denoising must cover each MSAA count.");
            }
        }

        ok &= Require(
            visibilityQualities == std::set<std::string>{
                "custom", "high", "low", "medium", "ultra" } &&
                visibilityEstimators == std::set<std::string>{
                    "cosine-weighted", "projected-angle", "solid-angle" } &&
                visibilityResolutions == std::set<std::string>{
                    "full", "half", "quarter" } &&
                visibilitySamples == std::set<std::string>{
                    "1", "16", "2", "32", "4", "64", "8" },
            "Every retained visibility preset, estimator, resolution, and "
            "sample quality must appear in the matrix.");
        ok &= Require(
            aoMethods == std::set<std::string>{
                "gaussian-bilateral", "joint-bilateral", "raw", "reblur" } &&
                giMethods == std::set<std::string>{
                    "gaussian-bilateral", "joint-bilateral", "raw", "reblur",
                    "relax" } &&
                shadowMethods == std::set<std::string>{
                    "gaussian-bilateral", "joint-bilateral", "raw", "sigma" } &&
                skyMethods == giMethods,
            "Every retained AO/GI/shadow/sky denoising method must appear.");
        std::set<std::string> expectedGlobalNoiseCombinations;
        for (const std::string& pattern : DomainValues("noise.pattern"))
        for (const std::string& resolution :
            DomainValues("noise.resolution"))
        for (const std::string& animate :
            DomainValues("noise.animate-samples"))
        for (const std::string& accumulate :
            DomainValues("noise.accumulate-samples"))
        {
            expectedGlobalNoiseCombinations.emplace(
                pattern + "|" + resolution + "|" + animate + "|" +
                accumulate);
        }
        ok &= Require(
            expectedGlobalNoiseCombinations.size() == 48u &&
                globalNoiseCombinations == expectedGlobalNoiseCombinations,
            "The runtime matrix must exhaust the catalog-derived global "
            "noise/STBN pattern, resolution, animation, and accumulation domain.");
        ok &= Require(
            autoExposureStates == std::set<std::string>{ "off", "on" } &&
                autoExposureCompensation ==
                    std::set<std::string>{ "-18", "0", "8" } &&
                autoExposureBrightening ==
                    std::set<std::string>{ "0", "16", "8" } &&
                autoExposureDarkening == autoExposureBrightening &&
                autoExposurePeriods ==
                    std::set<std::string>{ "0.05", "0.2", "5" },
            "Runtime HDR rows must exercise disabled/enabled auto exposure "
            "and minimum/default/maximum controls.");
        ok &= Require(
            sawSnapshot && sawCamera && sawResize && sawReset &&
                sawPathBistro && sawPathSan,
            "The matrix must include save/apply/reset, camera, resize, scene, "
            "and fixed path-tracing obligations.");
        ok &= Require(
            pathRestartCases == std::set<std::string>{
                "path-history-camera-reset",
                "path-history-resize-reset",
                "path-tracing-san-miguel-scene-reset",
                "path-history-environment-reset",
                "path-history-exposure-reset",
                "path-history-global-noise-reset",
                "path-history-material-reset",
                "path-history-light-reset",
                "path-history-flashlight-reset",
                "path-history-lighting-solution-cycle" },
            "PT history must causally restart for camera, resize, scene, "
            "environment, exposure, global noise, material, light, "
            "flashlight, and a rendered lighting-solution cycle.");
        return ok;
    }

    bool TestStateMachine()
    {
        uvsr::RetainedRuntimeCase runtimeCase;
        runtimeCase.name = "state-round-trip";
        runtimeCase.expectedSampleCount = 2u;
        runtimeCase.expectScreenVisibility = true;
        runtimeCase.expectDirectionalVisibility = true;
        runtimeCase.snapshotRoundTrip = true;

        using Clock = uvsr::RetainedRuntimeDiagnosticState::Clock;
        const Clock::time_point start{};
        uvsr::RetainedRuntimeDiagnosticState state(
            { runtimeCase }, start);
        uvsr::RetainedRuntimeTelemetry telemetry;
        telemetry.sceneLoaded = true;
        telemetry.receiverSampleCount = 2u;
        telemetry.screenVisibilityDispatched = true;
        telemetry.directionalVisibilityDispatched = true;
        telemetry.settingsSnapshot = "configured";
        telemetry.cpuFrameMilliseconds = 10.0;
        telemetry.gpuFrameMilliseconds = 8.0;
        telemetry.gpuFrameTimingAvailable = true;

        bool ok = true;
        auto directive = state.Tick(telemetry, start);
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ApplyCase,
            "The state machine must apply its first case.");
        directive = state.Tick(telemetry, start + std::chrono::milliseconds(1));
        ok &= Require(
            directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait,
            "Runtime evidence must settle for two frames.");
        directive = state.Tick(telemetry, start + std::chrono::milliseconds(2));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ResetSettings,
            "A snapshot case must exercise RESET after stable evidence.");

        telemetry.settingsSnapshot = "defaults";
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(3));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(4));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::RestoreSnapshot &&
                directive.payload == "configured",
            "RESET must differ before restoring the exact saved payload.");

        telemetry.settingsSnapshot = "configured";
        telemetry.gpuFrameTimingAvailable = false;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(5));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(6));
        ok &= Require(
            directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait &&
                state.RequiresSettingsSnapshot(),
            "Restored live settings must remain observable while a stable "
            "GPU timer is pending.");
        telemetry.gpuFrameTimingAvailable = true;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(7));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(8));
        ok &= Require(
            directive.kind ==
                    uvsr::RetainedRuntimeDirectiveKind::CaptureOutput &&
                !state.RequiresSettingsSnapshot(),
            "An exact restored snapshot with stable timing must be captured.");

        uvsr::RuntimeOutputEvidence output;
        output.valid = true;
        output.pixelBytes = 16u;
        output.minimumByte = 1u;
        output.maximumByte = 2u;
        output.linearReadbackValid = true;
        telemetry.output = output;
        telemetry.gpuFrameTimingAvailable = false;
        telemetry.cpuFrameMilliseconds = 5000.0;
        telemetry.gpuFrameMilliseconds = 5000.0;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(9));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ReportCasePass &&
                directive.hasStableFrameTiming &&
                directive.stableCpuFrameMilliseconds == 10.0 &&
                directive.stableGpuFrameMilliseconds == 8.0 &&
                state.PassedCaseCount() == 1u,
            "Synchronous capture overhead must not replace the settled "
            "pre-capture CPU/GPU timing.");
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(10));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::FinishPass,
            "The state machine must finish after all cases pass.");
        return ok;
    }

    bool TestStableTimingMeasurement()
    {
        using Clock = uvsr::RetainedRuntimeDiagnosticState::Clock;
        const Clock::time_point start{};
        uvsr::RetainedRuntimeCase runtimeCase;
        runtimeCase.name = "stable-timing";
        runtimeCase.action = uvsr::RetainedRuntimeAction::ChangeSetting;
        runtimeCase.actionSettingName = "visibility.enabled";
        runtimeCase.actionValue = "off";

        uvsr::RetainedRuntimeDiagnosticState state(
            { runtimeCase }, start);
        uvsr::RetainedRuntimeTelemetry telemetry;
        telemetry.sceneLoaded = true;
        telemetry.cpuFrameMilliseconds = 10.0;
        telemetry.gpuFrameMilliseconds = 8.0;
        telemetry.gpuFrameTimingAvailable = true;

        bool ok = true;
        auto directive = state.Tick(telemetry, start);
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(1));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(2));
        ok &= Require(
            directive.kind ==
                    uvsr::RetainedRuntimeDirectiveKind::CaptureOutput &&
                directive.hasStableFrameTiming &&
                directive.stableCpuFrameMilliseconds == 10.0 &&
                directive.stableGpuFrameMilliseconds == 8.0,
            "Capture must bind the settled frame timing that preceded it.");

        uvsr::RuntimeOutputEvidence output;
        output.valid = true;
        output.pixelBytes = 16u;
        output.minimumByte = 1u;
        output.maximumByte = 2u;
        output.linearReadbackValid = true;
        telemetry.output = output;
        telemetry.output->linearHash = 1u;
        telemetry.cpuFrameMilliseconds = 5000.0;
        telemetry.gpuFrameMilliseconds = 5000.0;
        telemetry.gpuFrameTimingAvailable = false;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(3));
        ok &= Require(
            directive.kind ==
                    uvsr::RetainedRuntimeDirectiveKind::ApplyAction &&
                directive.hasStableFrameTiming &&
                directive.stableCpuFrameMilliseconds == 10.0 &&
                directive.stableGpuFrameMilliseconds == 8.0,
            "Capture/readback/file-I/O overhead must not enter timing "
            "validation or records.");

        telemetry.output.reset();
        telemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ChangeSetting;
        telemetry.cpuFrameMilliseconds = 61.0;
        telemetry.gpuFrameMilliseconds = 8.0;
        telemetry.gpuFrameTimingAvailable = true;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(4));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(5));
        ok &= Require(
            directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait,
            "One delayed post-transition timing sample may recover before "
            "the strict gate fails.");
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(6));
        ok &= Require(
            directive.kind ==
                    uvsr::RetainedRuntimeDirectiveKind::FinishFail &&
                directive.payload.find("CPU=61.000000 ms") !=
                    std::string::npos &&
                directive.payload.find("limit 60.000000 ms") !=
                    std::string::npos,
            "A stable post-action regression must still fail the unchanged "
            "4x/50 ms gate with measured values.");

        uvsr::RetainedRuntimeCase transitionCase;
        transitionCase.name = "transition-timing-recovery";
        uvsr::RetainedRuntimeDiagnosticState transitionState(
            { transitionCase }, start);
        uvsr::RetainedRuntimeTelemetry transitionTelemetry;
        transitionTelemetry.sceneLoaded = true;
        transitionTelemetry.cpuFrameMilliseconds = 1500.0;
        transitionTelemetry.gpuFrameMilliseconds = 8.0;
        transitionTelemetry.gpuFrameTimingAvailable = true;
        directive = transitionState.Tick(transitionTelemetry, start);
        directive = transitionState.Tick(
            transitionTelemetry, start + std::chrono::milliseconds(1));
        directive = transitionState.Tick(
            transitionTelemetry, start + std::chrono::milliseconds(2));
        ok &= Require(
            directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait,
            "A first over-limit transition timing must defer exactly once.");
        transitionTelemetry.cpuFrameMilliseconds = 10.0;
        directive = transitionState.Tick(
            transitionTelemetry, start + std::chrono::milliseconds(3));
        ok &= Require(
            directive.kind ==
                    uvsr::RetainedRuntimeDirectiveKind::CaptureOutput &&
                directive.stableCpuFrameMilliseconds == 10.0,
            "A recovered steady frame must supply the captured timing.");
        return ok;
    }

    bool TestTimeoutBounds()
    {
        using Clock = uvsr::RetainedRuntimeDiagnosticState::Clock;
        const Clock::time_point start{};
        uvsr::RetainedRuntimeCase runtimeCase;
        runtimeCase.name = "timeout-bound";
        uvsr::RetainedRuntimeTelemetry telemetry;

        bool ok = true;
        uvsr::RetainedRuntimeDiagnosticState perCaseState(
            { runtimeCase }, start);
        auto directive = perCaseState.Tick(telemetry, start);
        directive = perCaseState.Tick(
            telemetry, start + std::chrono::minutes(91));
        ok &= Require(
            directive.kind ==
                    uvsr::RetainedRuntimeDirectiveKind::FinishFail &&
                directive.payload.rfind("case timeout:", 0u) == 0u,
            "The exhaustive gate must not retain the obsolete 90-minute "
            "global bound; its five-minute per-case bound remains active.");

        uvsr::RetainedRuntimeDiagnosticState boundaryState(
            { runtimeCase }, start);
        directive = boundaryState.Tick(
            telemetry, start + std::chrono::hours(6));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ApplyCase,
            "The exact six-hour global boundary must remain available.");

        uvsr::RetainedRuntimeDiagnosticState expiredState(
            { runtimeCase }, start);
        directive = expiredState.Tick(
            telemetry,
            start + std::chrono::hours(6) +
                std::chrono::milliseconds(1));
        ok &= Require(
            directive.kind ==
                    uvsr::RetainedRuntimeDirectiveKind::FinishFail &&
                directive.payload ==
                    "global six-hour timeout expired",
            "The exhaustive gate must remain globally bounded at six hours.");
        return ok;
    }

    uvsr::RuntimeOutputEvidence ValidOutput(
        std::uint32_t width = 2u,
        std::uint32_t height = 2u)
    {
        uvsr::RuntimeOutputEvidence output;
        output.valid = true;
        output.width = width;
        output.height = height;
        output.pixelBytes = 16u;
        output.minimumByte = 1u;
        output.maximumByte = 2u;
        output.linearReadbackValid = true;
        return output;
    }

    bool TestGlobalNoiseEvidence()
    {
        using Clock = uvsr::RetainedRuntimeDiagnosticState::Clock;
        const Clock::time_point start{};
        const auto run = [&](bool accumulate)
        {
            uvsr::RetainedRuntimeCase runtimeCase;
            runtimeCase.name = accumulate
                ? "global-noise-accumulation-on"
                : "global-noise-accumulation-off";
            runtimeCase.expectedSampleCount = 1u;
            runtimeCase.expectScreenVisibility = true;
            runtimeCase.assertLightingAccumulationState = true;
            runtimeCase.expectLightingAccumulation = accumulate;
            runtimeCase.settings = {
                { "noise.pattern", "spatiotemporal-blue" },
                { "noise.resolution", "512x512" },
                { "noise.animate-samples", "off" },
                { "noise.accumulate-samples", accumulate ? "on" : "off" }
            };

            uvsr::RetainedRuntimeDiagnosticState state(
                { runtimeCase }, start);
            uvsr::RetainedRuntimeTelemetry telemetry;
            telemetry.sceneLoaded = true;
            telemetry.receiverSampleCount = 1u;
            telemetry.screenVisibilityDispatched = true;
            telemetry.globalNoisePattern = "spatiotemporal-blue";
            telemetry.globalNoiseResolution = "512x512";
            telemetry.globalNoiseAnimateSamples = false;
            telemetry.globalNoiseAccumulateSamples = !accumulate;
            telemetry.lightingAccumulationCommitted = !accumulate;
            telemetry.cpuFrameMilliseconds = 10.0;
            telemetry.gpuFrameMilliseconds = 8.0;
            telemetry.gpuFrameTimingAvailable = true;

            bool ok = true;
            auto directive = state.Tick(telemetry, start);
            directive = state.Tick(
                telemetry, start + std::chrono::milliseconds(1));
            directive = state.Tick(
                telemetry, start + std::chrono::milliseconds(2));
            ok &= Require(
                directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait,
                "The wrong live accumulation state must not satisfy global noise evidence.");

            telemetry.globalNoiseAccumulateSamples = accumulate;
            telemetry.lightingAccumulationCommitted = accumulate;
            directive = state.Tick(
                telemetry, start + std::chrono::milliseconds(3));
            directive = state.Tick(
                telemetry, start + std::chrono::milliseconds(4));
            ok &= Require(
                directive.kind ==
                    uvsr::RetainedRuntimeDirectiveKind::CaptureOutput,
                "The exact global noise and accumulation state must request rendered evidence.");
            return ok;
        };
        return run(false) && run(true);
    }

    bool TestFlashlightLightingAndShadowEvidence()
    {
        using Clock = uvsr::RetainedRuntimeDiagnosticState::Clock;
        const Clock::time_point start{};
        uvsr::RetainedRuntimeCase runtimeCase;
        runtimeCase.name = "flashlight-shadow-separation";
        runtimeCase.expectedSampleCount = 4u;
        runtimeCase.expectFlashlightLightingSubmitted = true;
        runtimeCase.assertFlashlightLightingState = true;
        runtimeCase.expectFlashlightVisibility = true;
        runtimeCase.assertFlashlightVisibilityState = true;
        runtimeCase.action = uvsr::RetainedRuntimeAction::ChangeSetting;
        runtimeCase.actionSettingName =
            "light.selected.flashlight.cast-shadows";
        runtimeCase.actionBaselineValue = "on";
        runtimeCase.actionValue = "off";
        runtimeCase.requireActionOutputDifference = true;

        uvsr::RetainedRuntimeDiagnosticState state(
            { runtimeCase }, start);
        uvsr::RetainedRuntimeTelemetry telemetry;
        telemetry.sceneLoaded = true;
        telemetry.receiverSampleCount = 4u;
        telemetry.flashlightLightingSubmitted = true;
        telemetry.flashlightVisibilityDispatched = true;
        telemetry.cpuFrameMilliseconds = 10.0;
        telemetry.gpuFrameMilliseconds = 8.0;
        telemetry.gpuFrameTimingAvailable = true;

        bool ok = true;
        auto directive = state.Tick(telemetry, start);
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(1));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(2));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::CaptureOutput,
            "Visible shadowed flashlight must request baseline output.");
        telemetry.output = ValidOutput();
        telemetry.output->linearHash = 100u;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(3));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ApplyAction &&
                directive.actionSettingName ==
                    "light.selected.flashlight.cast-shadows" &&
                directive.actionValue == "off",
            "Baseline capture must request only the shadow-off action.");

        telemetry.output.reset();
        telemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ChangeSetting;
        telemetry.flashlightLightingSubmitted = false;
        telemetry.flashlightVisibilityDispatched = false;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(4));
        ok &= Require(
            directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait,
            "Shadow-off evidence must not pass when visible light vanished.");

        telemetry.flashlightLightingSubmitted = true;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(5));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(6));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::CaptureOutput,
            "Visible unshadowed flashlight must request comparison output.");
        telemetry.output = ValidOutput();
        telemetry.output->linearHash = 101u;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(7));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ReportCasePass,
            "Distinct shadowed/unshadowed output with stable lighting must pass.");

        runtimeCase.actionSettingName =
            "light.selected.flashlight.enabled";
        runtimeCase.expectFlashlightVisibility = false;
        runtimeCase.actionValue = "off";
        uvsr::RetainedRuntimeDiagnosticState disabledState(
            { runtimeCase }, start);
        telemetry = {};
        telemetry.sceneLoaded = true;
        telemetry.receiverSampleCount = 4u;
        telemetry.flashlightLightingSubmitted = true;
        telemetry.cpuFrameMilliseconds = 10.0;
        telemetry.gpuFrameMilliseconds = 8.0;
        telemetry.gpuFrameTimingAvailable = true;
        int time = 0;
        const auto tick = [&]()
        {
            return disabledState.Tick(
                telemetry,
                start + std::chrono::milliseconds(time++));
        };
        directive = tick();
        directive = tick();
        directive = tick();
        telemetry.output = ValidOutput();
        telemetry.output->linearHash = 200u;
        directive = tick();
        telemetry.output.reset();
        telemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ChangeSetting;
        directive = tick();
        ok &= Require(
            directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait,
            "Flashlight-off reference must reject lingering light submission.");
        telemetry.flashlightLightingSubmitted = false;
        directive = tick();
        directive = tick();
        telemetry.output = ValidOutput();
        telemetry.output->linearHash = 201u;
        directive = tick();
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ReportCasePass,
            "Flashlight-on output distinct from a no-light reference must pass.");
        return ok;
    }

    bool TestSceneAndActionCausality()
    {
        using Clock = uvsr::RetainedRuntimeDiagnosticState::Clock;
        const Clock::time_point start{};
        bool ok = true;

        uvsr::RetainedRuntimeCase sceneCase;
        sceneCase.name = "causal-scene-switch";
        sceneCase.expectedSampleCount = 1u;
        sceneCase.expectedPathHistoryCount = 3u;
        sceneCase.requirePathHistoryRestart = true;
        sceneCase.action = uvsr::RetainedRuntimeAction::ChangeScene;
        sceneCase.actionBaselineSceneToken =
            "bistro_interior_retextured";
        sceneCase.expectedSceneToken = "san_miguel_retextured";
        uvsr::RetainedRuntimeDiagnosticState sceneState(
            { sceneCase }, start);
        uvsr::RetainedRuntimeTelemetry sceneTelemetry;
        sceneTelemetry.sceneLoaded = true;
        sceneTelemetry.receiverSampleCount = 1u;
        sceneTelemetry.pathHistoryCount = 5u;
        sceneTelemetry.cpuFrameMilliseconds = 10.0;
        sceneTelemetry.gpuFrameMilliseconds = 8.0;
        sceneTelemetry.gpuFrameTimingAvailable = true;
        sceneTelemetry.currentScene = "san_miguel_retextured.scene.json";
        auto directive = sceneState.Tick(sceneTelemetry, start);
        directive = sceneState.Tick(
            sceneTelemetry, start + std::chrono::milliseconds(1));
        directive = sceneState.Tick(
            sceneTelemetry, start + std::chrono::milliseconds(2));
        ok &= Require(
            directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait,
            "A wrong pre-action scene must not emit the scene switch.");
        sceneTelemetry.currentScene =
            "bistro_interior_retextured.scene.json";
        directive = sceneState.Tick(
            sceneTelemetry, start + std::chrono::milliseconds(3));
        directive = sceneState.Tick(
            sceneTelemetry, start + std::chrono::milliseconds(4));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::CaptureOutput,
            "Stable Bistro PT history must be captured before the San Miguel switch.");
        sceneTelemetry.output = ValidOutput();
        directive = sceneState.Tick(
            sceneTelemetry, start + std::chrono::milliseconds(5));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ApplyAction &&
            directive.action == uvsr::RetainedRuntimeAction::ChangeScene,
            "The scene switch must follow captured stable Bistro PT history.");
        sceneTelemetry.output.reset();
        sceneTelemetry.currentScene =
            "san_miguel_retextured.scene.json";
        sceneTelemetry.pathHistoryCount = 1u;
        sceneTelemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ChangeScene;
        directive = sceneState.Tick(
            sceneTelemetry, start + std::chrono::milliseconds(6));
        sceneTelemetry.pathHistoryCount = 3u;
        directive = sceneState.Tick(
            sceneTelemetry, start + std::chrono::milliseconds(7));
        directive = sceneState.Tick(
            sceneTelemetry, start + std::chrono::milliseconds(8));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::CaptureOutput,
            "The named scene action must drop and rebuild PT history.");

        uvsr::RetainedRuntimeCase cameraCase;
        cameraCase.name = "causal-camera";
        cameraCase.expectedSampleCount = 1u;
        cameraCase.expectedPathHistoryCount = 3u;
        cameraCase.requirePathHistoryRestart = true;
        cameraCase.action = uvsr::RetainedRuntimeAction::NudgeCamera;
        uvsr::RetainedRuntimeDiagnosticState cameraState(
            { cameraCase }, start);
        uvsr::RetainedRuntimeTelemetry cameraTelemetry;
        cameraTelemetry.sceneLoaded = true;
        cameraTelemetry.receiverSampleCount = 1u;
        cameraTelemetry.pathHistoryCount = 5u;
        cameraTelemetry.cpuFrameMilliseconds = 10.0;
        cameraTelemetry.gpuFrameMilliseconds = 8.0;
        cameraTelemetry.gpuFrameTimingAvailable = true;
        directive = cameraState.Tick(cameraTelemetry, start);
        directive = cameraState.Tick(
            cameraTelemetry, start + std::chrono::milliseconds(1));
        directive = cameraState.Tick(
            cameraTelemetry, start + std::chrono::milliseconds(2));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::CaptureOutput,
            "A camera row must first capture stable PT history.");
        cameraTelemetry.output = ValidOutput();
        directive = cameraState.Tick(
            cameraTelemetry, start + std::chrono::milliseconds(3));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ApplyAction &&
            directive.action == uvsr::RetainedRuntimeAction::NudgeCamera,
            "A camera row must apply only after stable PT capture.");
        cameraTelemetry.output.reset();
        cameraTelemetry.pathHistoryCount = 1u;
        cameraTelemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ChangeSetting;
        directive = cameraState.Tick(
            cameraTelemetry, start + std::chrono::milliseconds(4));
        cameraTelemetry.pathHistoryCount = 3u;
        directive = cameraState.Tick(
            cameraTelemetry, start + std::chrono::milliseconds(5));
        ok &= Require(
            directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait,
            "A reset or wrong action acknowledgment cannot satisfy camera causality.");
        cameraTelemetry.pathHistoryCount = 1u;
        cameraTelemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::NudgeCamera;
        directive = cameraState.Tick(
            cameraTelemetry, start + std::chrono::milliseconds(6));
        cameraTelemetry.pathHistoryCount = 3u;
        directive = cameraState.Tick(
            cameraTelemetry, start + std::chrono::milliseconds(7));
        directive = cameraState.Tick(
            cameraTelemetry, start + std::chrono::milliseconds(8));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::CaptureOutput,
            "Only the acknowledged camera action may satisfy its PT restart.");

        uvsr::RetainedRuntimeCase resizeCase;
        resizeCase.name = "causal-resize";
        resizeCase.expectedSampleCount = 1u;
        resizeCase.action = uvsr::RetainedRuntimeAction::ResizeViewport;
        resizeCase.resizeWidth = 704;
        resizeCase.resizeHeight = 400;
        uvsr::RetainedRuntimeDiagnosticState resizeState(
            { resizeCase }, start);
        uvsr::RetainedRuntimeTelemetry resizeTelemetry;
        resizeTelemetry.sceneLoaded = true;
        resizeTelemetry.receiverSampleCount = 1u;
        resizeTelemetry.cpuFrameMilliseconds = 10.0;
        resizeTelemetry.gpuFrameMilliseconds = 8.0;
        resizeTelemetry.gpuFrameTimingAvailable = true;
        directive = resizeState.Tick(resizeTelemetry, start);
        directive = resizeState.Tick(
            resizeTelemetry, start + std::chrono::milliseconds(1));
        directive = resizeState.Tick(
            resizeTelemetry, start + std::chrono::milliseconds(2));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::CaptureOutput,
            "A resize row must capture baseline evidence before its action.");
        resizeTelemetry.output = ValidOutput();
        directive = resizeState.Tick(
            resizeTelemetry, start + std::chrono::milliseconds(3));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ApplyAction &&
            directive.action == uvsr::RetainedRuntimeAction::ResizeViewport,
            "The resize action must follow baseline capture.");
        resizeTelemetry.output.reset();
        resizeTelemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ResizeViewport;
        directive = resizeState.Tick(
            resizeTelemetry, start + std::chrono::milliseconds(4));
        directive = resizeState.Tick(
            resizeTelemetry, start + std::chrono::milliseconds(5));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::CaptureOutput,
            "An acknowledged resize must request rendered evidence.");
        resizeTelemetry.output = ValidOutput(700u, 400u);
        directive = resizeState.Tick(
            resizeTelemetry, start + std::chrono::milliseconds(6));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::FinishFail,
            "Wrong rendered dimensions must fail the resize row.");

        for (const uvsr::RetainedRuntimeAction action : {
            uvsr::RetainedRuntimeAction::ChangeMaterial,
            uvsr::RetainedRuntimeAction::ChangeLight,
            uvsr::RetainedRuntimeAction::ToggleFlashlight,
            uvsr::RetainedRuntimeAction::CycleLightingSolution })
        {
            uvsr::RetainedRuntimeCase causalCase;
            causalCase.name = "causal-render-domain";
            causalCase.expectedSampleCount = 1u;
            causalCase.expectedPathHistoryCount = 3u;
            causalCase.requirePathHistoryRestart = true;
            causalCase.action = action;
            uvsr::RetainedRuntimeDiagnosticState causalState(
                { causalCase }, start);
            uvsr::RetainedRuntimeTelemetry causalTelemetry;
            causalTelemetry.sceneLoaded = true;
            causalTelemetry.receiverSampleCount = 1u;
            causalTelemetry.pathHistoryCount = 5u;
            causalTelemetry.cpuFrameMilliseconds = 10.0;
            causalTelemetry.gpuFrameMilliseconds = 8.0;
            causalTelemetry.gpuFrameTimingAvailable = true;

            directive = causalState.Tick(causalTelemetry, start);
            directive = causalState.Tick(
                causalTelemetry, start + std::chrono::milliseconds(1));
            directive = causalState.Tick(
                causalTelemetry, start + std::chrono::milliseconds(2));
            causalTelemetry.output = ValidOutput();
            directive = causalState.Tick(
                causalTelemetry, start + std::chrono::milliseconds(3));
            ok &= Require(
                directive.kind ==
                    uvsr::RetainedRuntimeDirectiveKind::ApplyAction &&
                    directive.action == action,
                "Each retained PT domain must emit its exact causal action.");

            causalTelemetry.output.reset();
            causalTelemetry.pathHistoryCount = 1u;
            causalTelemetry.lastAppliedAction = action;
            directive = causalState.Tick(
                causalTelemetry, start + std::chrono::milliseconds(4));
            causalTelemetry.pathHistoryCount = 3u;
            directive = causalState.Tick(
                causalTelemetry, start + std::chrono::milliseconds(5));
            directive = causalState.Tick(
                causalTelemetry, start + std::chrono::milliseconds(6));
            ok &= Require(
                directive.kind ==
                    uvsr::RetainedRuntimeDirectiveKind::CaptureOutput,
                "Each exact render-domain action must drop and rebuild the "
                "accepted PT history before it can pass.");
        }
        return ok;
    }

    bool TestMultiPhaseAoGiCase()
    {
        using Clock = uvsr::RetainedRuntimeDiagnosticState::Clock;
        const Clock::time_point start{};
        uvsr::RetainedRuntimeCase runtimeCase;
        runtimeCase.name = "ao-gi-multi-phase";
        runtimeCase.expectedSampleCount = 4u;
        runtimeCase.expectScreenVisibility = true;
        runtimeCase.expectDirectionalVisibility = true;
        runtimeCase.expectAmbientOcclusionDenoising = true;
        runtimeCase.expectGlobalIlluminationDenoising = true;
        runtimeCase.snapshotRoundTrip = true;
        runtimeCase.exerciseRetainedStateChanges = true;
        runtimeCase.actionBaselineSceneToken =
            "bistro_interior_retextured";
        runtimeCase.expectedSceneToken = "san_miguel_retextured";
        runtimeCase.actionValue = "san_miguel_retextured.scene.json";
        runtimeCase.resizeWidth = 704;
        runtimeCase.resizeHeight = 400;

        uvsr::RetainedRuntimeDiagnosticState state({ runtimeCase }, start);
        uvsr::RetainedRuntimeTelemetry telemetry;
        telemetry.sceneLoaded = true;
        telemetry.currentScene =
            "bistro_interior_retextured.scene.json";
        telemetry.receiverSampleCount = 4u;
        telemetry.screenVisibilityDispatched = true;
        telemetry.directionalVisibilityDispatched = true;
        telemetry.ambientOcclusionDenoisingDispatched = true;
        telemetry.globalIlluminationDenoisingDispatched = true;
        telemetry.settingsSnapshot = "configured";
        telemetry.cpuFrameMilliseconds = 10.0;
        telemetry.gpuFrameMilliseconds = 8.0;
        telemetry.gpuFrameTimingAvailable = true;

        bool ok = true;
        auto directive = state.Tick(telemetry, start);
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(1));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(2));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ResetSettings,
            "Every multi-phase AO/GI row must RESET its configured snapshot.");
        telemetry.settingsSnapshot = "defaults";
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(3));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(4));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::RestoreSnapshot,
            "A multi-phase AO/GI row must restore the exact saved payload.");
        telemetry.settingsSnapshot = "configured";
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(5));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(6));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::CaptureOutput &&
            directive.payload == "baseline",
            "Restored AO/GI state must capture a baseline.");

        telemetry.output = ValidOutput();
        telemetry.output->linearHash = 10u;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(7));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ApplyAction &&
            directive.action == uvsr::RetainedRuntimeAction::NudgeCamera,
            "The first AO/GI state action must be the camera cut.");
        telemetry.output.reset();
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(8));
        ok &= Require(
            directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait,
            "A missing camera acknowledgment must not advance the case.");
        telemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::NudgeCamera;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(9));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(10));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::CaptureOutput &&
            directive.payload == "camera",
            "The acknowledged camera cut must capture changed output.");
        telemetry.output = ValidOutput();
        telemetry.output->linearHash = 11u;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(11));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ApplyAction &&
            directive.action ==
                uvsr::RetainedRuntimeAction::ResizeViewport,
            "Camera evidence must advance to the exact resize action.");
        telemetry.output.reset();
        telemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ResizeViewport;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(12));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(13));
        telemetry.output = ValidOutput(704u, 400u);
        telemetry.output->linearHash = 12u;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(14));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ApplyAction &&
            directive.action == uvsr::RetainedRuntimeAction::ChangeScene,
            "Exact resize evidence must advance to the scene switch.");

        telemetry.output.reset();
        telemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ChangeScene;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(15));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(16));
        ok &= Require(
            directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait,
            "A scene acknowledgment with the old scene must not advance.");
        telemetry.currentScene = "san_miguel_retextured.scene.json";
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(17));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(18));
        telemetry.output = ValidOutput(704u, 400u);
        telemetry.output->linearHash = 13u;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(19));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ApplyAction &&
            directive.action == uvsr::RetainedRuntimeAction::ChangeSetting &&
            directive.actionSettingName == "visibility.enabled" &&
            directive.actionValue == "off",
            "Scene evidence must advance to a visibility-off reference.");

        telemetry.output.reset();
        telemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ChangeSetting;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(20));
        ok &= Require(
            directive.kind == uvsr::RetainedRuntimeDirectiveKind::Wait,
            "A reference frame that still dispatches AO/GI must not advance.");
        telemetry.screenVisibilityDispatched = false;
        telemetry.ambientOcclusionDenoisingDispatched = false;
        telemetry.globalIlluminationDenoisingDispatched = false;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(21));
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(22));
        telemetry.output = ValidOutput(704u, 400u);
        telemetry.output->linearHash = 14u;
        directive = state.Tick(
            telemetry, start + std::chrono::milliseconds(23));
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::ReportCasePass,
            "Distinct finite reference output must complete all AO/GI phases.");

        runtimeCase.snapshotRoundTrip = false;
        uvsr::RetainedRuntimeDiagnosticState referenceState(
            { runtimeCase }, start);
        uvsr::RetainedRuntimeTelemetry referenceTelemetry;
        referenceTelemetry.sceneLoaded = true;
        referenceTelemetry.currentScene =
            "bistro_interior_retextured.scene.json";
        referenceTelemetry.receiverSampleCount = 4u;
        referenceTelemetry.screenVisibilityDispatched = true;
        referenceTelemetry.directionalVisibilityDispatched = true;
        referenceTelemetry.ambientOcclusionDenoisingDispatched = true;
        referenceTelemetry.globalIlluminationDenoisingDispatched = true;
        referenceTelemetry.cpuFrameMilliseconds = 10.0;
        referenceTelemetry.gpuFrameMilliseconds = 8.0;
        referenceTelemetry.gpuFrameTimingAvailable = true;
        int millisecond = 0;
        const auto tick = [&]()
        {
            return referenceState.Tick(
                referenceTelemetry,
                start + std::chrono::milliseconds(++millisecond));
        };
        directive = referenceState.Tick(referenceTelemetry, start);
        directive = tick();
        directive = tick();
        referenceTelemetry.output = ValidOutput();
        referenceTelemetry.output->linearHash = 20u;
        directive = tick();
        referenceTelemetry.output.reset();
        referenceTelemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::NudgeCamera;
        directive = tick();
        directive = tick();
        referenceTelemetry.output = ValidOutput();
        referenceTelemetry.output->linearHash = 21u;
        directive = tick();
        referenceTelemetry.output.reset();
        referenceTelemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ResizeViewport;
        directive = tick();
        directive = tick();
        referenceTelemetry.output = ValidOutput(704u, 400u);
        referenceTelemetry.output->linearHash = 22u;
        directive = tick();
        referenceTelemetry.output.reset();
        referenceTelemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ChangeScene;
        referenceTelemetry.currentScene =
            "san_miguel_retextured.scene.json";
        directive = tick();
        directive = tick();
        referenceTelemetry.output = ValidOutput(704u, 400u);
        referenceTelemetry.output->linearHash = 23u;
        directive = tick();
        referenceTelemetry.output.reset();
        referenceTelemetry.lastAppliedAction =
            uvsr::RetainedRuntimeAction::ChangeSetting;
        referenceTelemetry.screenVisibilityDispatched = false;
        referenceTelemetry.ambientOcclusionDenoisingDispatched = false;
        referenceTelemetry.globalIlluminationDenoisingDispatched = false;
        directive = tick();
        directive = tick();
        referenceTelemetry.output = ValidOutput(704u, 400u);
        referenceTelemetry.output->linearHash = 23u;
        directive = tick();
        ok &= Require(
            directive.kind ==
                uvsr::RetainedRuntimeDirectiveKind::FinishFail,
            "AO/GI output equal to its visibility-off reference must fail.");
        return ok;
    }

    bool TestJsonRecords()
    {
        uvsr::RetainedRuntimeProvenance provenance;
        provenance.settingsHash = "9c50b0f1515e89d856c8ebb627b86984";
        provenance.engineVersion = "40016.45297.20830.35288";
        provenance.sourceCommit =
            "e29a41245dbd0e6fd7a819d2341646419ab76e72";
        provenance.sourceIdentity = provenance.sourceCommit;
        provenance.sourceClean = true;
        provenance.production = false;
        provenance.configuration = "developer";
        provenance.packagePath = "C:/package";
        provenance.executablePath = "C:/package/bin/uvsr-engine.exe";
        provenance.executableSha256 = std::string(64u, 'a');
        provenance.debugLayerRequested = true;
        provenance.nvrhiValidationRequested = true;

        uvsr::RetainedRuntimeCase runtimeCase;
        runtimeCase.name = "json-\"case";
        uvsr::RetainedRuntimeTelemetry telemetry;
        telemetry.receiverSampleCount = 16u;
        telemetry.directionalVisibilityDispatched = true;
        uvsr::RuntimeOutputEvidence output;
        output.valid = true;
        output.width = 2u;
        output.height = 2u;
        output.pixelBytes = 16u;
        output.minimumByte = 1u;
        output.maximumByte = 2u;
        output.artifactPath = "C:/capture/quoted-\"case.bmp";
        output.linearReadbackValid = true;
        output.varyingPixelCount = 3u;
        output.edgePixelCount = 2u;
        output.meanLinearLuminance = 0.25;
        output.rmsLinearLuminance = 0.5;
        output.meanLinearHorizontalGradient = 0.125;
        telemetry.output = output;

        const std::array<std::string, 5> records = {
            uvsr::BuildRetainedRuntimeStartJson(provenance, 1016u),
            uvsr::BuildRetainedRuntimeFailureJson(
                runtimeCase.name, "failure\nmessage"),
            uvsr::BuildRetainedRuntimeCaptureJson(
                0u, runtimeCase, "camera", telemetry),
            uvsr::BuildRetainedRuntimeCaseJson(
                0u, runtimeCase, telemetry),
            uvsr::BuildRetainedRuntimeSummaryJson(
                provenance, true, 1016u, 1016u, 1234)
        };
        bool ok = true;
        for (const std::string& record : records)
        {
            try
            {
                const uvsr::contract::JsonValue parsed =
                    uvsr::contract::JsonParser(record).Parse();
                ok &= Require(
                    parsed.kind ==
                        uvsr::contract::JsonValue::Kind::Object,
                    "Each runtime JSONL record must be one strict object.");
            }
            catch (const std::exception& exception)
            {
                std::cerr << exception.what() << '\n';
                ok = false;
            }
        }
        return ok;
    }
}

int main()
{
    bool ok = true;
    ok &= TestLinearReadback();
    ok &= TestCrossCaseSemanticSignatures();
    ok &= TestMatrix();
    ok &= TestStateMachine();
    ok &= TestStableTimingMeasurement();
    ok &= TestTimeoutBounds();
    ok &= TestGlobalNoiseEvidence();
    ok &= TestFlashlightLightingAndShadowEvidence();
    ok &= TestSceneAndActionCausality();
    ok &= TestMultiPhaseAoGiCase();
    ok &= TestJsonRecords();
    return ok ? 0 : 1;
}
