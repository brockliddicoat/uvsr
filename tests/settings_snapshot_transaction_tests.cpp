#include "settings_snapshot_transaction.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iomanip>
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

    uvsr::SettingsSnapshotTransactionEntry Entry(uvsr::SettingId id, std::string_view value)
    {
        return { id, value };
    }
    void OwnReason(std::string_view reason, uvsr::SettingsSnapshotError& error) noexcept
    {
        error = {};
        error.code = uvsr::SettingsSnapshotErrorCode::InvalidInput;
        error.detail = uvsr::json::EncodedText([](uvsr::json::OutputWriter& output, const void* context) noexcept {
            const auto text = *static_cast<const std::string_view*>(context);
            return output.Raw({text.data(), text.size()});
        }, &reason);
        if (!error.detail.IsValid())
        {
            error.code = uvsr::SettingsSnapshotErrorCode::OutOfMemory;
            error.message = "cannot prepare injected error";
        }
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
        std::optional<uvsr::SettingId> mismatchOnceOnWrite;
        bool failSelectorRollback = false;
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
            uvsr::SettingsSnapshotText& value,
            uvsr::SettingsSnapshotError& error)
        {
            std::string_view text;
            if (id == uvsr::SettingId::SceneCurrent)
                text = scene;
            else if (id == uvsr::SettingId::LightSelected)
                text = light;
            else if (id == uvsr::SettingId::MaterialSelected)
                text = material;
            else if (IsMaterialValue(id))
            {
                if (material == "none")
                    text = "<unavailable>";
                else
                    text = materials[material][id];
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
                    text = "<unavailable>";
                else
                    text = lights[light][id];
            }
            else
            {
                const auto found = globals.find(id);
                if (found == globals.end())
                {
                    error.code = uvsr::SettingsSnapshotErrorCode::InvalidInput;
                    error.message = "missing graph runtime value";
                    return false;
                }
                text = found->second;
            }
            return value.Assign(text, error);
        }

        bool ReadRaw(
            uvsr::SettingId id,
            uvsr::SettingsSnapshotText& value,
            uvsr::SettingsSnapshotError& error)
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
            if (mismatchOnceOnWrite == id)
            {
                mismatchOnceOnWrite.reset();
                error.clear();
                return true;
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
            if (rollback && failSelectorRollback)
            {
                error = "injected selector rollback failure";
                return uvsr::SettingsSnapshotSelectorTransition::Failed;
            }
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
                this,
                [](void* owner, uvsr::SettingId id, std::string_view value,
                    std::string_view dependencySelector, uvsr::SettingsSnapshotError& error) noexcept {
                    auto& live = *static_cast<GraphRuntime*>(owner);
                    uvsr::SettingsSnapshotValidationContext context;
                    if (id == uvsr::SettingId::MaterialSelectedOpacity)
                    {
                        const std::string target = dependencySelector.empty()
                            ? live.material : std::string(dependencySelector);
                        context.hasMaterialBaseTexture = true;
                        context.materialHasBaseTexture = live.baseTexturedMaterials.find(target) != live.baseTexturedMaterials.end();
                    }
                    return uvsr::ValidateSettingsSnapshotCatalogValue(FindDefinition(id), value, error, context);
                },
                [](void* owner, uvsr::SettingId id, uvsr::SettingsSnapshotText& value,
                    uvsr::SettingsSnapshotError& error) noexcept {
                    return static_cast<GraphRuntime*>(owner)->ReadVisible(id, value, error);
                },
                [](void* owner, uvsr::SettingId id, uvsr::SettingsSnapshotText& value,
                    uvsr::SettingsSnapshotError& error) noexcept {
                    return static_cast<GraphRuntime*>(owner)->ReadRaw(id, value, error);
                },
                [](void* owner, uvsr::SettingId id, std::string_view value,
                    uvsr::SettingsSnapshotError& error) noexcept {
                    std::string reason;
                    const bool result = static_cast<GraphRuntime*>(owner)->Write(id, value, reason);
                    if (!result) OwnReason(reason, error);
                    return result;
                },
                [](void* owner, uvsr::SettingId id, std::string_view value, bool begin, bool rollback,
                    uvsr::SettingsSnapshotError& error) noexcept {
                    std::string reason;
                    const auto result = static_cast<GraphRuntime*>(owner)->DriveSelector(id, value, begin, rollback, reason);
                    if (result == uvsr::SettingsSnapshotSelectorTransition::Failed) OwnReason(reason, error);
                    return result;
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

    struct OwnershipRuntime
    {
        GraphRuntime graph = MakeGraphRuntime();
        uvsr::SettingsSnapshotTransactionCoordinator* coordinator = nullptr;
        std::size_t callbacks = 0;
        int reenterOn = 0;
        bool reentered = false;
        bool sourceReadOom = false;
        bool targetReadOom = false;
        bool rollbackReadOom = false;
        bool readFaultIssued = false;
        std::size_t diagnosticBudget = SIZE_MAX;

        OwnershipRuntime() { graph.globals[uvsr::SettingId::UiVisible] = "on"; }

        void Enter(int phase)
        {
            ++callbacks;
            if (phase != reenterOn || reentered) return;
            reentered = true;
            Require(coordinator && coordinator->IsActive(), "callback must observe active coordinator ownership");
            coordinator->Reset();
            auto nested = coordinator->Begin({}, Access());
            auto advanced = coordinator->Advance();
            Require(coordinator->IsActive() &&
                nested.result.failureStage == uvsr::SettingsSnapshotTransactionFailureStage::Configuration &&
                advanced.result.failureStage == uvsr::SettingsSnapshotTransactionFailureStage::Configuration,
                "callback reentry must reject without resetting its outer transaction");
        }

        bool Read(uvsr::SettingId id, bool raw, uvsr::SettingsSnapshotText& value,
            uvsr::SettingsSnapshotError& error)
        {
            Enter(2);
            const bool exhaust = !readFaultIssued &&
                (sourceReadOom || (targetReadOom && raw && graph.scene == "b/main.scene.json") ||
                    (rollbackReadOom && raw && graph.writeCount != 0));
            if (exhaust)
            {
                readFaultIssued = true;
                uvsr::FailUiSettingsValueAllocationAfter(0);
                const bool accepted = value.Assign("a checked read that needs owned text storage", error);
                uvsr::ClearUiSettingsValueAllocationFailure();
                return accepted;
            }
            return raw ? graph.ReadRaw(id, value, error) : graph.ReadVisible(id, value, error);
        }

        uvsr::SettingsSnapshotStagedRuntimeAccess Access()
        {
            return {
                this,
                [](void* owner, uvsr::SettingId id, std::string_view value, std::string_view dependency,
                    uvsr::SettingsSnapshotError& error) noexcept {
                    auto& live = *static_cast<OwnershipRuntime*>(owner);
                    live.Enter(1);
                    const auto access = live.graph.Access();
                    return access.validateValue(access.context, id, value, dependency, error);
                },
                [](void* owner, uvsr::SettingId id, uvsr::SettingsSnapshotText& value,
                    uvsr::SettingsSnapshotError& error) noexcept {
                    return static_cast<OwnershipRuntime*>(owner)->Read(id, false, value, error);
                },
                [](void* owner, uvsr::SettingId id, uvsr::SettingsSnapshotText& value,
                    uvsr::SettingsSnapshotError& error) noexcept {
                    return static_cast<OwnershipRuntime*>(owner)->Read(id, true, value, error);
                },
                [](void* owner, uvsr::SettingId id, std::string_view value,
                    uvsr::SettingsSnapshotError& error) noexcept {
                    auto& live = *static_cast<OwnershipRuntime*>(owner);
                    live.Enter(3);
                    const auto access = live.graph.Access();
                    const bool accepted = access.writeValue(access.context, id, value, error);
                    if (!accepted)
                    {
                        error.nativeCode = 91;
                        error.cleanupCode = 92;
                        if (live.diagnosticBudget != SIZE_MAX)
                            uvsr::json::FailAllocationAfter(live.diagnosticBudget);
                    }
                    return accepted;
                },
                [](void* owner, uvsr::SettingId id, std::string_view value, bool begin, bool rollback,
                    uvsr::SettingsSnapshotError& error) noexcept {
                    auto& live = *static_cast<OwnershipRuntime*>(owner);
                    live.Enter(4);
                    const auto access = live.graph.Access();
                    return access.driveSelector(access.context, id, value, begin, rollback, error);
                }
            };
        }
    };

    void CheckOwnershipFailures()
    {
        using namespace uvsr;
        const SettingsSnapshotTransactionEntry change[] = {{SettingId::UiVisible, "off"}};
        for (int phase : {1, 2, 3})
        {
            OwnershipRuntime live;
            SettingsSnapshotTransactionCoordinator coordinator;
            live.coordinator = &coordinator;
            live.reenterOn = phase;
            const auto result = coordinator.Begin(change, live.Access());
            Require(result.progress == SettingsSnapshotTransactionProgress::Succeeded && live.reentered &&
                live.graph.globals.at(SettingId::UiVisible) == "off" && live.graph.writeCount == 1 &&
                !coordinator.IsActive(), "callback reentry changed or interrupted its outer transaction");
        }
        {
            OwnershipRuntime live;
            SettingsSnapshotTransactionCoordinator coordinator;
            FailSettingsSnapshotTransactionAllocationAfter(0);
            const auto rejected = coordinator.Begin(change, live.Access());
            ClearSettingsSnapshotTransactionAllocationFailure();
            Require(rejected.progress == SettingsSnapshotTransactionProgress::Failed &&
                rejected.result.error.code == SettingsSnapshotErrorCode::OutOfMemory &&
                rejected.result.failureStage == SettingsSnapshotTransactionFailureStage::Configuration &&
                live.callbacks == 0 && !coordinator.IsActive(), "state exhaustion reached a runtime callback");
            Require(coordinator.Begin(change, live.Access()).result.succeeded, "state allocation retry failed");
        }
        {
            OwnershipRuntime live;
            SettingsSnapshotTransactionCoordinator coordinator;
            const std::string longScene = std::string(4096, 'a') + ".scene.json";
            const SettingsSnapshotTransactionEntry request[] = {{SettingId::SceneCurrent, longScene}};
            FailUiSettingsValueAllocationAfter(0);
            const auto rejected = coordinator.Begin(request, live.Access());
            ClearUiSettingsValueAllocationFailure();
            Require(rejected.progress == SettingsSnapshotTransactionProgress::Failed &&
                rejected.result.error.code == SettingsSnapshotErrorCode::OutOfMemory && live.callbacks == 0 &&
                !coordinator.IsActive(), "request clone exhaustion reached a runtime callback");
        }
        for (bool target : {false, true})
        {
            OwnershipRuntime live;
            live.sourceReadOom = !target;
            live.targetReadOom = target;
            SettingsSnapshotTransactionCoordinator coordinator;
            const SettingsSnapshotTransactionEntry request[] = {{SettingId::SceneCurrent, "b/main.scene.json"}};
            const auto result = coordinator.Begin(request, live.Access());
            Require(result.progress == SettingsSnapshotTransactionProgress::Failed && live.readFaultIssued &&
                result.result.error.code == SettingsSnapshotErrorCode::OutOfMemory &&
                result.result.failureStage == SettingsSnapshotTransactionFailureStage::Capture &&
                result.result.rollbackAttempted == target && result.result.rollbackSucceeded == target &&
                live.graph.scene == "a/main.scene.json" && live.graph.writeCount == 0,
                "capture exhaustion failed to preserve or restore the original scene");
        }
        {
            OwnershipRuntime live;
            live.graph.failOnceOnWrite = SettingId::UiVisible;
            live.graph.mutateBeforeFailure = true;
            live.rollbackReadOom = true;
            SettingsSnapshotTransactionCoordinator coordinator;
            const auto result = coordinator.Begin(change, live.Access());
            const auto callbacks = live.callbacks;
            const auto repeated = coordinator.Advance();
            Require(result.progress == SettingsSnapshotTransactionProgress::Failed && live.readFaultIssued &&
                result.result.rollbackAttempted && !result.result.rollbackSucceeded &&
                result.result.failureStage == SettingsSnapshotTransactionFailureStage::Apply &&
                live.graph.writeCount == 1 && live.graph.globals.at(SettingId::UiVisible) == "off" &&
                repeated.result.error.MessageView() == result.result.error.MessageView() && live.callbacks == callbacks,
                "rollback reader failure lost its failure state or continued mutation");
        }
        for (std::size_t budget : {0u, 1u})
        {
            OwnershipRuntime live;
            live.graph.failOnceOnWrite = SettingId::UiVisible;
            live.graph.mutateBeforeFailure = true;
            live.diagnosticBudget = budget;
            SettingsSnapshotTransactionCoordinator coordinator;
            const auto result = coordinator.Begin(change, live.Access());
            json::ClearAllocationFailure();
            const bool preserved = result.progress == SettingsSnapshotTransactionProgress::Failed &&
                result.result.error.code == SettingsSnapshotErrorCode::OutOfMemory &&
                result.result.error.nativeCode == 91 && result.result.error.cleanupCode == 92 &&
                result.result.failureStage == SettingsSnapshotTransactionFailureStage::Apply &&
                result.result.rollbackAttempted && result.result.rollbackSucceeded &&
                live.graph.globals.at(SettingId::UiVisible) == "on" && live.graph.writeCount == 2;
            if (!preserved)
                std::cerr << "diagnostic budget " << budget << ", progress " << static_cast<int>(result.progress)
                    << ", error " << static_cast<int>(result.result.error.code) << ", native "
                    << result.result.error.nativeCode << ", cleanup " << result.result.error.cleanupCode
                    << ", stage " << static_cast<int>(result.result.failureStage) << ", rollback "
                    << result.result.rollbackAttempted << '/' << result.result.rollbackSucceeded << ", live "
                    << live.graph.globals.at(SettingId::UiVisible) << ", writes " << live.graph.writeCount
                    << ", reason " << result.result.error.MessageView() << '\n';
            Require(preserved,
                "diagnostic exhaustion changed rollback or lost its numeric failure fields");
            if (budget == 1)
            {
                const auto callbacks = live.callbacks;
                const auto repeated = coordinator.Advance();
                Require(repeated.result.error.MessageView().find("injected graph write failure") != std::string_view::npos &&
                    repeated.result.error.code == SettingsSnapshotErrorCode::InvalidInput && live.callbacks == callbacks,
                    "result clone exhaustion changed the retained terminal error");
            }
        }
    }

    void CaptureGraph(const uvsr::SettingsSnapshotTransactionResult& result,
        const GraphRuntime& runtime)
    {
        std::cout << "result " << result.succeeded << ' ' << result.rollbackAttempted
            << ' ' << result.rollbackSucceeded << ' ' << result.changedValueCount
            << ' ' << static_cast<unsigned>(result.failureStage) << ' '
            << std::quoted(result.error.MessageView()) << '\n';
        std::cout << "selectors " << std::quoted(runtime.scene) << ' '
            << std::quoted(runtime.light) << ' ' << std::quoted(runtime.material)
            << ' ' << runtime.writeCount << ' ' << runtime.selectorBeginCount
            << ' ' << runtime.selectorPollCount << '\n';
        for (const auto& [id, value] : runtime.globals)
            std::cout << "global " << static_cast<std::uint64_t>(id) << ' '
                << std::quoted(value) << '\n';
        for (const auto& [name, values] : runtime.materials)
            for (const auto& [id, value] : values)
                std::cout << "material " << std::quoted(name) << ' '
                    << static_cast<std::uint64_t>(id) << ' ' << std::quoted(value) << '\n';
        for (const auto& [name, values] : runtime.lights)
            for (const auto& [id, value] : values)
                std::cout << "light " << std::quoted(name) << ' '
                    << static_cast<std::uint64_t>(id) << ' ' << std::quoted(value) << '\n';
        for (const auto& event : runtime.events)
            std::cout << "event " << std::quoted(event) << '\n';
        std::cout << "end\n";
    }

    uvsr::SettingsSnapshotTransactionResult RunGraph(
        const std::vector<uvsr::SettingsSnapshotTransactionEntry>& transaction,
        GraphRuntime& runtime)
    {
        uvsr::SettingsSnapshotStagedRuntimeAccess access = runtime.Access();
        uvsr::SettingsSnapshotTransactionCoordinator coordinator;
        uvsr::SettingsSnapshotTransactionStep step =
            coordinator.Begin({transaction.data(), transaction.size()}, access);
        unsigned advances = 0u;
        while (step.progress ==
            uvsr::SettingsSnapshotTransactionProgress::Pending)
        {
            Require(++advances < 64u, "transaction must finish bounded selector transitions");
            step = coordinator.Advance();
        }
        CaptureGraph(step.result, runtime);
        return std::move(step.result);
    }
}

int main()
{
    using namespace uvsr;
    CheckOwnershipFailures();
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

    {
        GraphRuntime graph = MakeGraphRuntime();
        graph.globals[SettingId::UiVisible] = "off";
        graph.mismatchOnceOnWrite = SettingId::UiVisible;
        result = RunGraph({Entry(SettingId::UiVisible, "on")}, graph);
        Require(!result.succeeded && result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Readback &&
                result.rollbackAttempted && result.rollbackSucceeded &&
                result.changedValueCount == 0u &&
                graph.globals.at(SettingId::UiVisible) == "off",
            "an accepted write with mismatched readback must restore its source");
    }
    {
        GraphRuntime graph = MakeGraphRuntime();
        graph.globals[SettingId::UiVisible] = "off";
        graph.failOnceOnWrite = SettingId::UiVisible;
        graph.failSelectorRollback = true;
        SettingsSnapshotTransactionCoordinator coordinator;
        auto access = graph.Access();
        const SettingsSnapshotTransactionEntry requests[] = {Entry(SettingId::SceneCurrent, "b/main.scene.json"),
            Entry(SettingId::UiVisible, "on")};
        auto step = coordinator.Begin(requests, access);
        Require(step.progress == SettingsSnapshotTransactionProgress::Failed &&
                step.result.failureStage == SettingsSnapshotTransactionFailureStage::Apply &&
                step.result.rollbackAttempted && !step.result.rollbackSucceeded &&
                step.result.error.MessageView().find("injected graph write failure") != std::string::npos &&
                step.result.error.MessageView().find("injected selector rollback failure") != std::string::npos &&
                !coordinator.IsActive(),
            "rollback failure must retain the first failure and its own reason");
        const auto events = graph.events;
        step = coordinator.Advance();
        Require(step.progress == SettingsSnapshotTransactionProgress::Failed &&
                graph.events == events,
            "advancing a failed rollback must issue no further callbacks");
        CaptureGraph(step.result, graph);
    }
    {
        GraphRuntime graph = MakeGraphRuntime();
        graph.asyncSelectors = true;
        const std::string longScene = std::string(4096u, 'a') + ".scene.json";
        SettingsSnapshotTransactionCoordinator coordinator;
        {
            auto access = graph.Access();
            std::string callerScene = longScene;
            auto requests = std::vector{Entry(SettingId::SceneCurrent, callerScene)};
            const auto step = coordinator.Begin({requests.data(), requests.size()}, access);
            Require(step.progress == SettingsSnapshotTransactionProgress::Pending &&
                    step.waitingFor == "scene.current" && coordinator.IsActive() &&
                    graph.selectorBeginCount == 1u,
                "a long scene selector must remain pending with its stable name");
            GraphRuntime other = MakeGraphRuntime();
            const SettingsSnapshotTransactionEntry replacement[] = {{SettingId::UiVisible, "off"}};
            const auto rejected = coordinator.Begin(replacement, other.Access());
            coordinator.Reset();
            Require(rejected.result.failureStage == SettingsSnapshotTransactionFailureStage::Configuration &&
                coordinator.IsActive() && other.events.empty() && other.writeCount == 0,
                "a second Begin or Reset must preserve pending text and its original callback context");
            access = {};
            callerScene.assign(8192u, 'x');
        }
        const auto step = coordinator.Advance();
        Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
                graph.scene == longScene && graph.selectorPollCount == 1u &&
                !coordinator.IsActive(),
            "pending work must own complete text beyond caller request lifetime");
        CaptureGraph(step.result, graph);
    }
    std::cout << "UVSR settings snapshot transaction validation passed\n";
    return EXIT_SUCCESS;
}
