#include "settings_snapshot_transaction.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Settings snapshot transaction validation failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    const uvsr::UiSettingsCommandDefinition& FindDefinition(
        uvsr::SettingId id)
    {
        for (const uvsr::UiSettingsCommandDefinition& definition :
            uvsr::UiSettingsCommandCatalog)
        {
            if (definition.id == id)
                return definition;
        }
        Fail("catalog SettingId fixture was not found");
    }

    uvsr::SettingsSnapshotTransactionEntry Entry(uvsr::SettingId id, std::string value)
    {
        return { id, std::move(value) };
    }
    struct GraphRuntime
    {
        std::map<uvsr::SettingId, std::string> globals;
        std::map<std::string,
            std::map<uvsr::SettingId, std::string>, std::less<>> materials;
        std::map<std::string,
            std::map<uvsr::SettingId, std::string>, std::less<>> lights;
        std::map<std::string, std::pair<std::string, std::string>, std::less<>>
            sceneDefaults;
        std::set<std::string, std::less<>> baseTexturedMaterials;
        std::set<std::string, std::less<>> spotLights;
        std::set<std::string, std::less<>> flashlights;
        std::string scene = "a/main.scene.json";
        std::string light = "0:spot-a";
        std::string material = "1";
        std::optional<std::pair<uvsr::SettingId, std::string>> pendingSelector;
        std::optional<uvsr::SettingId> failOnceOnWrite;
        bool mutateBeforeFailure = false;
        bool asyncSelectors = false;
        bool pendingRollback = false;
        std::size_t writeCount = 0u;
        std::size_t selectorBeginCount = 0u;
        std::size_t selectorPollCount = 0u;
        std::vector<std::string> events;

        [[nodiscard]] bool IsMaterialValue(uvsr::SettingId id) const
        {
            const auto& definition = FindDefinition(id);
            return definition.section ==
                    uvsr::UiSettingsCommandSection::Materials &&
                id != uvsr::SettingId::MaterialSelected;
        }

        [[nodiscard]] bool IsLightValue(uvsr::SettingId id) const
        {
            const auto& definition = FindDefinition(id);
            return definition.section ==
                    uvsr::UiSettingsCommandSection::Lights &&
                id != uvsr::SettingId::LightSelected;
        }

        bool ReadVisible(
            uvsr::SettingId id,
            std::string& value,
            std::string& error)
        {
            if (id == uvsr::SettingId::SceneCurrent)
                value = scene;
            else if (id == uvsr::SettingId::LightSelected)
                value = light;
            else if (id == uvsr::SettingId::MaterialSelected)
                value = material;
            else if (IsMaterialValue(id))
            {
                if (material == "none")
                    value = "<unavailable>";
                else
                    value = materials[material][id];
            }
            else if (IsLightValue(id))
            {
                const auto availability = FindDefinition(id).availability;
                const bool available =
                    availability == uvsr::UiSettingsAvailability::SelectedLight ||
                    availability == uvsr::UiSettingsAvailability::Always ||
                    (availability ==
                            uvsr::UiSettingsAvailability::SelectedFlashlight &&
                        flashlights.find(light) != flashlights.end()) ||
                    (availability == uvsr::UiSettingsAvailability::SpotLight &&
                        spotLights.find(light) != spotLights.end());
                if (!available)
                    value = "<unavailable>";
                else
                    value = lights[light][id];
            }
            else
            {
                const auto found = globals.find(id);
                if (found == globals.end())
                {
                    error = "missing graph runtime value";
                    return false;
                }
                value = found->second;
            }
            error.clear();
            return true;
        }

        bool ReadRaw(
            uvsr::SettingId id,
            std::string& value,
            std::string& error)
        {
            return ReadVisible(id, value, error);
        }

        bool Write(
            uvsr::SettingId id,
            std::string_view requested,
            std::string& error)
        {
            ++writeCount;
            events.push_back("set:" + std::string(FindDefinition(id).name));
            const auto mutate = [&]()
            {
                if (IsMaterialValue(id))
                    materials[material][id] = requested;
                else if (IsLightValue(id))
                    lights[light][id] = requested;
                else
                    globals[id] = requested;
            };

            if (id == uvsr::SettingId::LightSelectedInnerAngle)
            {
                const float requestedAngle = std::stof(std::string(requested));
                const float outerAngle = std::stof(
                    lights[light][uvsr::SettingId::LightSelectedOuterAngle]);
                if (requestedAngle > outerAngle)
                {
                    error = "inner exceeds outer";
                    return false;
                }
            }
            if (id == uvsr::SettingId::LightSelectedOuterAngle)
            {
                const float requestedAngle = std::stof(std::string(requested));
                const float innerAngle = std::stof(
                    lights[light][uvsr::SettingId::LightSelectedInnerAngle]);
                if (requestedAngle < innerAngle)
                {
                    error = "outer below inner";
                    return false;
                }
            }

            if (failOnceOnWrite == id)
            {
                failOnceOnWrite.reset();
                if (mutateBeforeFailure)
                    mutate();
                error = "injected graph write failure";
                return false;
            }
            mutate();
            error.clear();
            return true;
        }

        void PublishSelector(uvsr::SettingId id, std::string value)
        {
            if (id == uvsr::SettingId::SceneCurrent)
            {
                scene = std::move(value);
                const auto defaults = sceneDefaults.find(scene);
                if (defaults != sceneDefaults.end())
                {
                    light = defaults->second.first;
                    material = defaults->second.second;
                }
            }
            else if (id == uvsr::SettingId::LightSelected)
                light = std::move(value);
            else if (id == uvsr::SettingId::MaterialSelected)
                material = std::move(value);
        }

        uvsr::SettingsSnapshotSelectorTransition DriveSelector(
            uvsr::SettingId id,
            std::string_view value,
            bool begin,
            bool rollback,
            std::string& error)
        {
            Require(id != uvsr::SettingId::GpuAdapter,
                "read-only adapter must never reach the selector driver");
            if (begin)
            {
                Require(!pendingSelector, "a second selector cannot begin while one is pending");
                ++selectorBeginCount;
                events.push_back(std::string(rollback ? "rollback:" : "select:") +
                    std::string(FindDefinition(id).name));
                if (asyncSelectors)
                {
                    pendingSelector = std::pair{ id, std::string(value) };
                    pendingRollback = rollback;
                    return uvsr::SettingsSnapshotSelectorTransition::Pending;
                }
                PublishSelector(id, std::string(value));
                error.clear();
                return uvsr::SettingsSnapshotSelectorTransition::Ready;
            }
            if (!pendingSelector || pendingSelector->first != id ||
                pendingSelector->second != value || pendingRollback != rollback)
            {
                error = "missing pending selector";
                return uvsr::SettingsSnapshotSelectorTransition::Failed;
            }
            ++selectorPollCount;
            PublishSelector(id, std::move(pendingSelector->second));
            pendingSelector.reset();
            error.clear();
            return uvsr::SettingsSnapshotSelectorTransition::Ready;
        }

        [[nodiscard]] uvsr::SettingsSnapshotStagedRuntimeAccess Access()
        {
            return {
                [this](uvsr::SettingId id,
                    std::string_view value,
                    std::string_view dependencySelector,
                    std::string& validationError)
                {
                    uvsr::SettingsSnapshotValidationContext context;
                    if (id == uvsr::SettingId::MaterialSelectedOpacity)
                    {
                        const std::string target = dependencySelector.empty()
                            ? material
                            : std::string(dependencySelector);
                        context.hasMaterialBaseTexture = true;
                        context.materialHasBaseTexture =
                            baseTexturedMaterials.find(target) !=
                            baseTexturedMaterials.end();
                    }
                    return uvsr::ValidateSettingsSnapshotCatalogValue(
                        FindDefinition(id), value, validationError, context);
                },
                [this](uvsr::SettingId id,
                    std::string& value,
                    std::string& error)
                {
                    return ReadVisible(id, value, error);
                },
                [this](uvsr::SettingId id,
                    std::string& value,
                    std::string& error)
                {
                    return ReadRaw(id, value, error);
                },
                [this](uvsr::SettingId id,
                    std::string_view value,
                    std::string& error)
                {
                    return Write(id, value, error);
                },
                [this](uvsr::SettingId id,
                    std::string_view value,
                    bool begin,
                    bool rollback,
                    std::string& error)
                {
                    return DriveSelector(
                        id, value, begin, rollback, error);
                }
            };
        }
    };

    GraphRuntime MakeGraphRuntime()
    {
        using uvsr::SettingId;
        GraphRuntime runtime;
        runtime.globals = {
            { SettingId::GpuAdapter, "0" },
            { SettingId::UiVisible, "off" },
            { SettingId::RepresentationAllowRayTraversal, "off" },
            { SettingId::SkyVisibilityEnabled, "off" },
            { SettingId::DebugPbrFilter, "final" }
        };
        const std::map<SettingId, std::string> materialOne = {
            { SettingId::MaterialSelectedDomain, "opaque" },
            { SettingId::MaterialSelectedOpacity, "0.25" },
            { SettingId::MaterialSelectedAlphaCutoff, "0.25" },
            { SettingId::MaterialSelectedTransmissionTextureEnabled, "off" },
            { SettingId::MaterialSelectedTransmissionFactor, "0.25" },
            { SettingId::MaterialSelectedOcclusionTextureEnabled, "off" },
            { SettingId::MaterialSelectedOcclusionStrength, "0.5" }
        };
        const std::map<SettingId, std::string> materialTwo = {
            { SettingId::MaterialSelectedDomain, "transmissive-alpha-blended" },
            { SettingId::MaterialSelectedOpacity, "1" },
            { SettingId::MaterialSelectedAlphaCutoff, "0.5" },
            { SettingId::MaterialSelectedTransmissionTextureEnabled, "on" },
            { SettingId::MaterialSelectedTransmissionFactor, "0.75" },
            { SettingId::MaterialSelectedOcclusionTextureEnabled, "on" },
            { SettingId::MaterialSelectedOcclusionStrength, "0.5" }
        };
        runtime.materials.emplace("1", materialOne);
        runtime.materials.emplace("2", materialTwo);
        runtime.baseTexturedMaterials.insert("2");
        runtime.lights["0:spot-a"] = {
            { SettingId::LightSelectedInnerAngle, "10" },
            { SettingId::LightSelectedOuterAngle, "20" }
        };
        runtime.lights["1:spot-b"] = {
            { SettingId::LightSelectedInnerAngle, "30" },
            { SettingId::LightSelectedOuterAngle, "40" }
        };
        runtime.lights["2:ordinary"] = {
            { SettingId::LightSelectedColor, "0.2 0.3 0.4" }
        };
        runtime.lights["3:flashlight_1"] = {
            { SettingId::LightSelectedFlashlightEnabled, "off" },
            { SettingId::LightSelectedFlashlightBrightness, "100" }
        };
        runtime.spotLights = { "0:spot-a", "1:spot-b" };
        runtime.flashlights = { "3:flashlight_1" };
        runtime.sceneDefaults = {
            { "a/main.scene.json", { "0:spot-a", "1" } },
            { "b/main.scene.json", { "1:spot-b", "2" } }
        };
        return runtime;
    }

    uvsr::SettingsSnapshotTransactionResult RunGraph(
        const std::vector<uvsr::SettingsSnapshotTransactionEntry>& transaction,
        GraphRuntime& runtime)
    {
        uvsr::SettingsSnapshotStagedRuntimeAccess access = runtime.Access();
        uvsr::SettingsSnapshotTransactionCoordinator coordinator;
        uvsr::SettingsSnapshotTransactionStep step =
            coordinator.Begin(transaction, access);
        unsigned advances = 0u;
        while (step.progress ==
            uvsr::SettingsSnapshotTransactionProgress::Pending)
        {
            Require(++advances < 64u, "transaction must finish bounded selector transitions");
            step = coordinator.Advance(access);
        }
        return step.result;
    }
}

int main()
{
    using namespace uvsr;
    SettingsSnapshotTransactionResult result;

    {
        GraphRuntime graph = MakeGraphRuntime();
        const auto adapterMismatch = std::vector{
            Entry(SettingId::GpuAdapter, "1"),
            Entry(SettingId::UiVisible, "on")
        };
        result = RunGraph(adapterMismatch, graph);
        Require(!result.succeeded &&
                result.failureStage ==
                    SettingsSnapshotTransactionFailureStage::Preflight &&
                graph.writeCount == 0u && graph.selectorBeginCount == 0u,
            "adapter mismatch must reject without a write or selector begin");

        graph = MakeGraphRuntime();
        const auto adapterMatch = std::vector{
            Entry(SettingId::GpuAdapter, "0"),
            Entry(SettingId::UiVisible, "on")
        };
        result = RunGraph(adapterMatch, graph);
        Require(result.succeeded && graph.writeCount == 1u &&
                graph.globals.at(SettingId::UiVisible) == "on",
            "matching adapter precondition must permit the value plan");
    }

    {
        GraphRuntime graph = MakeGraphRuntime();
        const auto texturedTarget = std::vector{
            Entry(SettingId::MaterialSelected, "2"),
            Entry(SettingId::MaterialSelectedOpacity, "1.5")
        };
        result = RunGraph(texturedTarget, graph);
        Require(result.succeeded && graph.material == "2" &&
                graph.materials.at("2").at(
                    SettingId::MaterialSelectedOpacity) == "1.5",
            "target material texture context must allow opacity above one");

        graph = MakeGraphRuntime();
        graph.material = "2";
        const auto untexturedTarget = std::vector{
            Entry(SettingId::MaterialSelected, "1"),
            Entry(SettingId::MaterialSelectedOpacity, "1.5")
        };
        result = RunGraph(untexturedTarget, graph);
        Require(!result.succeeded &&
                result.failureStage ==
                    SettingsSnapshotTransactionFailureStage::Preflight &&
                graph.writeCount == 0u && graph.selectorBeginCount == 0u &&
                graph.material == "2",
            "untextured target opacity must reject before selector mutation");
    }

    {
        GraphRuntime graph = MakeGraphRuntime();
        graph.material = "2";
        const auto opaqueLatent = std::vector{
            Entry(SettingId::MaterialSelectedOcclusionStrength, "0.75"),
            Entry(SettingId::MaterialSelectedTransmissionFactor, "0.25"),
            Entry(SettingId::MaterialSelectedAlphaCutoff, "0.25"),
            Entry(SettingId::MaterialSelectedDomain, "opaque"),
            Entry(SettingId::MaterialSelectedOpacity, "0.5"),
            Entry(SettingId::MaterialSelectedTransmissionTextureEnabled, "off"),
            Entry(SettingId::MaterialSelectedOcclusionTextureEnabled, "off"),
            Entry(SettingId::MaterialSelected, "2")
        };
        result = RunGraph(opaqueLatent, graph);
        Require(result.succeeded &&
                graph.materials.at("2").at(
                    SettingId::MaterialSelectedDomain) == "opaque" &&
                graph.materials.at("2").at(
                    SettingId::MaterialSelectedTransmissionFactor) == "0.25" &&
                graph.materials.at("2").at(
                    SettingId::MaterialSelectedOcclusionStrength) == "0.75",
            "inactive material fields must remain latent raw snapshot state");
        const auto eventIndex = [&graph](std::string_view event)
        {
            return static_cast<std::size_t>(std::distance(
                graph.events.begin(),
                std::find(graph.events.begin(), graph.events.end(), event)));
        };
        Require(eventIndex("set:material.selected.domain") <
                    eventIndex("set:material.selected.opacity") &&
                eventIndex("set:material.selected.domain") <
                    eventIndex("set:material.selected.transmission-factor") &&
                eventIndex("set:material.selected.occlusion-texture-enabled") <
                    eventIndex("set:material.selected.occlusion-strength"),
            "material value prerequisites must apply before their dependents");

        graph.events.clear();
        const auto transmissiveLatent = std::vector{
            Entry(SettingId::MaterialSelected, "2"),
            Entry(SettingId::MaterialSelectedDomain,
                "transmissive-alpha-blended"),
            Entry(SettingId::MaterialSelectedOpacity, "1.5"),
            Entry(SettingId::MaterialSelectedAlphaCutoff, "0.5"),
            Entry(SettingId::MaterialSelectedTransmissionTextureEnabled, "on"),
            Entry(SettingId::MaterialSelectedTransmissionFactor, "0.75"),
            Entry(SettingId::MaterialSelectedOcclusionTextureEnabled, "on"),
            Entry(SettingId::MaterialSelectedOcclusionStrength, "0.75")
        };
        result = RunGraph(transmissiveLatent, graph);
        Require(result.succeeded &&
                graph.materials.at("2").at(
                    SettingId::MaterialSelectedOpacity) == "1.5" &&
                graph.materials.at("2").at(
                    SettingId::MaterialSelectedTransmissionTextureEnabled) ==
                    "on" &&
                graph.materials.at("2").at(
                    SettingId::MaterialSelectedOcclusionStrength) == "0.75",
            "opaque to transmissive and disabled to enabled latent fields "
            "must round trip");

        graph = MakeGraphRuntime();
        const auto sourceMaterials = graph.materials;
        graph.failOnceOnWrite =
            SettingId::MaterialSelectedOcclusionStrength;
        graph.mutateBeforeFailure = true;
        result = RunGraph(transmissiveLatent, graph);
        Require(!result.succeeded && result.rollbackAttempted &&
                result.rollbackSucceeded && graph.material == "1" &&
                graph.materials == sourceMaterials,
            "late material failure must restore hidden target raws and the "
            "original selected material state");
    }

    {
        GraphRuntime graph = MakeGraphRuntime();
        graph.globals[SettingId::PathingMinimumBounces] = "1";
        graph.globals[SettingId::PathingMaximumBounces] = "2";
        const auto original = graph.globals;
        const auto increasing = std::vector{
            Entry(SettingId::PathingMinimumBounces, "6"),
            Entry(SettingId::PathingMaximumBounces, "8") };
        result = RunGraph(increasing, graph);
        Require(result.succeeded && graph.events[0] == "set:pathing.maximum-bounces",
            "raising a bounce pair must raise its maximum before its minimum");
        graph.events.clear();
        result = RunGraph({ Entry(SettingId::PathingMaximumBounces, "2"),
            Entry(SettingId::PathingMinimumBounces, "1") }, graph);
        Require(result.succeeded && graph.globals == original &&
                graph.events[0] == "set:pathing.minimum-bounces",
            "lowering a bounce pair must lower its minimum before its maximum");
        graph.events.clear();
        graph.failOnceOnWrite = SettingId::PathingMinimumBounces;
        graph.mutateBeforeFailure = true;
        result = RunGraph(increasing, graph);
        Require(!result.succeeded && result.rollbackSucceeded && graph.globals == original,
            "a partial bounce mutation must restore both original bounds");
        const auto writesBefore = graph.writeCount;
        result = RunGraph({ Entry(SettingId::PathingMinimumBounces, "9"),
            Entry(SettingId::PathingMaximumBounces, "8") }, graph);
        Require(!result.succeeded && graph.writeCount == writesBefore && graph.globals == original,
            "an inverted bounce interval must reject before mutation");
    }

    {
        GraphRuntime graph = MakeGraphRuntime();
        const auto increasing = std::vector{
            Entry(SettingId::LightSelected, "0:spot-a"),
            Entry(SettingId::LightSelectedInnerAngle, "30"),
            Entry(SettingId::LightSelectedOuterAngle, "40")
        };
        result = RunGraph(increasing, graph);
        Require(result.succeeded && graph.events.size() >= 2u &&
                graph.events[0] == "set:light.selected.outer-angle" &&
                graph.events[1] == "set:light.selected.inner-angle",
            "raising a spot pair must apply outer before inner");

        graph.events.clear();
        const auto decreasing = std::vector{
            Entry(SettingId::LightSelected, "0:spot-a"),
            Entry(SettingId::LightSelectedInnerAngle, "10"),
            Entry(SettingId::LightSelectedOuterAngle, "20")
        };
        result = RunGraph(decreasing, graph);
        Require(result.succeeded && graph.events.size() >= 2u &&
                graph.events[0] == "set:light.selected.inner-angle" &&
                graph.events[1] == "set:light.selected.outer-angle",
            "lowering a spot pair must apply inner before outer");

        graph = MakeGraphRuntime();
        const auto invalidPair = std::vector{
            Entry(SettingId::LightSelected, "0:spot-a"),
            Entry(SettingId::LightSelectedInnerAngle, "40"),
            Entry(SettingId::LightSelectedOuterAngle, "30")
        };
        result = RunGraph(invalidPair, graph);
        Require(!result.succeeded && graph.writeCount == 0u &&
                graph.selectorBeginCount == 0u,
            "invalid final spot pair must reject before mutation");

        graph = MakeGraphRuntime();
        graph.failOnceOnWrite = SettingId::LightSelectedInnerAngle;
        graph.mutateBeforeFailure = true;
        result = RunGraph(increasing, graph);
        Require(!result.succeeded && result.rollbackSucceeded &&
                graph.lights.at("0:spot-a").at(
                    SettingId::LightSelectedInnerAngle) == "10" &&
                graph.lights.at("0:spot-a").at(
                    SettingId::LightSelectedOuterAngle) == "20",
            "failure after the first spot transition must restore the pair");
    }

    {
        GraphRuntime graph = MakeGraphRuntime();
        graph.asyncSelectors = true;
        graph.sceneDefaults["a/main.scene.json"] = { "2:ordinary", "2" };
        graph.sceneDefaults["b/main.scene.json"] = { "2:ordinary", "1" };
        graph.globals[SettingId::UiVisible] = "off";
        const auto asyncFailure = std::vector{
            Entry(SettingId::GpuAdapter, "0"),
            Entry(SettingId::SceneCurrent, "b/main.scene.json"),
            Entry(SettingId::LightSelected, "1:spot-b"),
            Entry(SettingId::MaterialSelected, "2"),
            Entry(SettingId::MaterialSelectedOpacity, "1.5"),
            Entry(SettingId::UiVisible, "on")
        };
        const auto baseline = graph;
        result = RunGraph(asyncFailure, graph);
        Require(result.succeeded && result.changedValueCount == 5u &&
                graph.selectorBeginCount == 3u && graph.selectorPollCount == 3u &&
                graph.scene == "b/main.scene.json" && graph.light == "1:spot-b" && graph.material == "2" &&
                graph.materials.at("2").at(SettingId::MaterialSelectedOpacity) == "1.5" &&
                graph.globals.at(SettingId::UiVisible) == "on",
            "all three selectors must finish pending transitions before live value application");
        graph = baseline;
        graph.failOnceOnWrite = SettingId::UiVisible;
        result = RunGraph(asyncFailure, graph);
        const auto rollbackScene = std::find(
            graph.events.begin(), graph.events.end(),
            "rollback:scene.current");
        const auto rollbackLight = std::find(
            graph.events.begin(), graph.events.end(),
            "rollback:light.selected");
        const auto rollbackMaterial = std::find(
            graph.events.begin(), graph.events.end(),
            "rollback:material.selected");
        Require(!result.succeeded && result.rollbackSucceeded &&
                graph.selectorBeginCount == 6u && graph.selectorPollCount == 6u &&
                graph.materials == baseline.materials &&
                graph.lights == baseline.lights && graph.globals == baseline.globals &&
                graph.scene == "a/main.scene.json" &&
                graph.light == "0:spot-a" && graph.material == "1" &&
                graph.events.size() >= 6u &&
                graph.events[0] == "select:scene.current" &&
                graph.events[1] == "select:light.selected" &&
                graph.events[2] == "select:material.selected" &&
                rollbackScene != graph.events.end() &&
                rollbackLight != graph.events.end() &&
                rollbackMaterial != graph.events.end() &&
                rollbackScene < rollbackLight &&
                rollbackLight < rollbackMaterial,
            "async scene, light, and material apply and rollback must retain "
            "the selector dependency order");
    }

    {
        GraphRuntime graph = MakeGraphRuntime();
        const auto latentDebug = std::vector{
            Entry(SettingId::RepresentationAllowRayTraversal, "off"),
            Entry(SettingId::SkyVisibilityEnabled, "off"),
            Entry(SettingId::DebugPbrFilter, "sky-visibility")
        };
        result = RunGraph(latentDebug, graph);
        Require(result.succeeded &&
                graph.globals.at(SettingId::DebugPbrFilter) ==
                    "sky-visibility",
            "inactive sky debug filter must remain storable latent state");

        graph = MakeGraphRuntime();
        graph.light = "2:ordinary";
        const auto ordinaryFlashlightSentinels = std::vector{
            Entry(SettingId::LightSelected, "2:ordinary"),
            Entry(SettingId::LightSelectedFlashlightEnabled,
                "<unavailable>"),
            Entry(SettingId::LightSelectedFlashlightBrightness,
                "<unavailable>")
        };
        result = RunGraph(ordinaryFlashlightSentinels, graph);
        Require(result.succeeded && graph.writeCount == 0u,
            "ordinary selected light must preserve released flashlight sentinels");
    }

    {
        const auto shuffled = std::vector{
            Entry(SettingId::MaterialSelectedOcclusionStrength, "0.75"),
            Entry(SettingId::MaterialSelectedDomain, "opaque"),
            Entry(SettingId::MaterialSelected, "2"),
            Entry(SettingId::MaterialSelectedOcclusionTextureEnabled, "off"),
            Entry(SettingId::MaterialSelectedOpacity, "0.5")
        };
        auto canonical = shuffled;
        std::reverse(canonical.begin(), canonical.end());
        GraphRuntime left = MakeGraphRuntime();
        GraphRuntime right = MakeGraphRuntime();
        left.material = "2";
        right.material = "2";
        const auto leftResult = RunGraph(shuffled, left);
        const auto rightResult = RunGraph(canonical, right);
        Require(leftResult.succeeded && rightResult.succeeded &&
                left.materials == right.materials &&
                left.events == right.events,
            "shuffled transaction input must produce one deterministic DAG plan");
    }

    std::cout << "UVSR settings snapshot transaction validation passed\n";
    return EXIT_SUCCESS;
}
