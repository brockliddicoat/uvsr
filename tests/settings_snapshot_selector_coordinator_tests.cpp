#include "settings_snapshot_transaction.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using namespace uvsr;

    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Settings selector coordinator validation failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    struct KeyedRuntime
    {
        std::string adapter = "0";
        std::string scene = "old.scene.json";
        std::string light = "0:old-light";
        std::string material = "10";
        std::map<std::string, std::string, std::less<>> values;
        std::vector<std::string> events;
        std::vector<std::string> validated;
        std::string pendingName;
        std::string pendingValue;
        std::string readyName;
        std::string readyValue;
        std::string failWriteName;
        std::string ignoreWriteName;
        std::string failRollbackName;
        bool mutationSeen = false;
        bool publishSceneIdentityBeforeReady = false;
        std::optional<SettingsSnapshotRestartHandoff> persisted;

        static std::string Key(
            std::string_view adapter,
            std::string_view scene,
            std::string_view object,
            std::string_view name)
        {
            return std::string(adapter) + "|" + std::string(scene) + "|" +
                std::string(object) + "|" + std::string(name);
        }

        std::string ActiveKey(std::string_view name) const
        {
            if (name.find("light.selected.") == 0u)
                return Key(adapter, scene, light, name);
            if (name.find("material.selected.") == 0u)
                return Key(adapter, scene, material, name);
            return std::string(name);
        }

        bool Read(
            std::string_view name,
            std::string& value,
            std::string& error)
        {
            if (name == "gpu.adapter")
                value = adapter;
            else if (name == "scene.current")
                value = scene;
            else if (name == "light.selected")
                value = light;
            else if (name == "material.selected")
                value = material;
            else
            {
                const auto found = values.find(ActiveKey(name));
                if (found == values.end())
                {
                    error = "missing keyed value " + ActiveKey(name);
                    return false;
                }
                value = found->second;
            }
            return true;
        }

        bool Validate(
            std::string_view name,
            std::string_view value,
            std::string& error)
        {
            validated.emplace_back(name);
            if (name == "invalid.late")
            {
                error = "injected late invalid value";
                return false;
            }
            if (value == "invalid-source")
            {
                error = "injected invalid source value";
                return false;
            }
            if (value.empty())
            {
                error = "empty value";
                return false;
            }
            return true;
        }

        bool SelectorExists(
            std::string_view name,
            std::string_view value) const
        {
            if (name == "gpu.adapter")
                return value == "0" || value == "1";
            if (name == "scene.current")
            {
                return adapter == "0"
                    ? value == "old.scene.json"
                    : value == "adapter-one.scene.json" ||
                        value == "target.scene.json";
            }
            if (name == "light.selected")
            {
                if (adapter == "0" && scene == "old.scene.json")
                    return value == "0:old-light";
                if (adapter == "1" && scene == "target.scene.json")
                {
                    return value == "0:target-default" ||
                        value == "1:target-light";
                }
                return value == "0:adapter-one-light";
            }
            if (name == "material.selected")
            {
                if (adapter == "0" && scene == "old.scene.json")
                    return value == "10";
                if (adapter == "1" && scene == "target.scene.json")
                    return value == "21" || value == "20";
                return value == "30";
            }
            return false;
        }

        SettingsSnapshotSelectorTransition DriveSelector(
            std::string_view name,
            std::string_view value,
            bool begin,
            bool rollback,
            std::string& error)
        {
            if (!SelectorExists(name, value))
            {
                error = "selector is unavailable in the active parent context";
                return SettingsSnapshotSelectorTransition::Failed;
            }
            if (!begin)
            {
                return readyName == name && readyValue == value
                    ? SettingsSnapshotSelectorTransition::Ready
                    : SettingsSnapshotSelectorTransition::Pending;
            }
            events.push_back(
                "select:" + std::string(name) + "=" + std::string(value) +
                (rollback ? ":rollback" : ":apply"));
            pendingName = name;
            pendingValue = value;
            readyName.clear();
            readyValue.clear();
            if (publishSceneIdentityBeforeReady &&
                name == "scene.current")
            {
                scene = value;
            }
            return name == "gpu.adapter"
                ? SettingsSnapshotSelectorTransition::RestartRequired
                : SettingsSnapshotSelectorTransition::Pending;
        }

        void ResolvePending()
        {
            Require(!pendingName.empty(),
                "a pending selector must exist before resolution");
            events.push_back(
                "resolve:" + pendingName + "=" + pendingValue);
            readyName = pendingName;
            readyValue = pendingValue;
            if (pendingName == "gpu.adapter")
            {
                adapter = pendingValue;
                if (adapter == "0")
                {
                    scene = "old.scene.json";
                    light = "0:old-light";
                    material = "10";
                }
                else
                {
                    scene = "adapter-one.scene.json";
                    light = "0:adapter-one-light";
                    material = "30";
                }
            }
            else if (pendingName == "scene.current")
            {
                scene = pendingValue;
                if (scene == "old.scene.json")
                {
                    light = "0:old-light";
                    material = "10";
                }
                else if (scene == "target.scene.json")
                {
                    light = "0:target-default";
                    material = "21";
                }
            }
            else if (pendingName == "light.selected")
                light = pendingValue;
            else if (pendingName == "material.selected")
                material = pendingValue;
            pendingName.clear();
            pendingValue.clear();
        }

        bool Write(
            std::string_view name,
            std::string_view value,
            std::string& error)
        {
            const std::string key = ActiveKey(name);
            events.push_back("write:" + key + "=" + std::string(value));
            mutationSeen = true;
            if (name == failRollbackName && value != "requested")
            {
                error = "injected rollback failure";
                return false;
            }
            if (name == failWriteName && value == "requested")
            {
                values[key] = std::string(value);
                error = "injected target writer failure";
                failWriteName.clear();
                return false;
            }
            if (name == ignoreWriteName && value == "requested")
            {
                ignoreWriteName.clear();
                return true;
            }
            values[key] = std::string(value);
            return true;
        }

        bool Persist(
            const SettingsSnapshotRestartHandoff& handoff,
            std::string&)
        {
            events.push_back(handoff.rollingBack
                ? "persist:rollback"
                : "persist:apply");
            persisted = handoff;
            return true;
        }

        SettingsSnapshotStagedRuntimeAccess Access()
        {
            return {
                [this](std::string_view name,
                       std::string_view value,
                       std::string& error)
                {
                    return Validate(name, value, error);
                },
                [this](std::string_view name,
                       std::string& value,
                       std::string& error)
                {
                    return Read(name, value, error);
                },
                [this](std::string_view name,
                       std::string_view value,
                       std::string& error)
                {
                    return Write(name, value, error);
                },
                [this](std::string_view name,
                       std::string_view value,
                       bool begin,
                       bool rollback,
                       std::string& error)
                {
                    return DriveSelector(
                        name, value, begin, rollback, error);
                },
                [this](const SettingsSnapshotRestartHandoff& handoff,
                       std::string& error)
                {
                    return Persist(handoff, error);
                }
            };
        }
    };

    std::vector<SettingsSnapshotTransactionEntry> BuildTransaction()
    {
        using Mode = SettingsSnapshotApplicationMode;
        return {
            { "global.value", "requested", Mode::Mutable },
            { "material.selected.roughness", "requested",
                Mode::LiveDependent },
            { "gpu.adapter", "1", Mode::Selector },
            { "light.selected", "1:target-light", Mode::Selector },
            { "light.selected.flashlight.brightness", "requested",
                Mode::LiveDependent },
            { "scene.current", "target.scene.json", Mode::Selector },
            { "material.selected", "20", Mode::Selector }
        };
    }

    void PopulateValues(KeyedRuntime& runtime)
    {
        runtime.values["global.value"] = "source";
        runtime.values[KeyedRuntime::Key(
            "0", "old.scene.json", "0:old-light",
            "light.selected.flashlight.brightness")] = "source-light";
        runtime.values[KeyedRuntime::Key(
            "0", "old.scene.json", "10",
            "material.selected.roughness")] = "source-material";
        runtime.values[KeyedRuntime::Key(
            "1", "target.scene.json", "1:target-light",
            "light.selected.flashlight.brightness")] = "target-light-pre";
        runtime.values[KeyedRuntime::Key(
            "1", "target.scene.json", "20",
            "material.selected.roughness")] = "target-material-pre";
    }

    SettingsSnapshotTransactionStep ResolveNonRestartingSelectors(
        SettingsSnapshotTransactionCoordinator& coordinator,
        KeyedRuntime& runtime,
        SettingsSnapshotTransactionStep step)
    {
        while (step.progress == SettingsSnapshotTransactionProgress::Pending)
        {
            runtime.ResolvePending();
            step = coordinator.Advance(runtime.Access());
        }
        return step;
    }

    std::size_t EventIndex(
        const KeyedRuntime& runtime,
        std::string_view prefix)
    {
        for (std::size_t index = 0u; index < runtime.events.size(); ++index)
        {
            if (runtime.events[index].find(prefix) == 0u)
                return index;
        }
        return runtime.events.size();
    }
}

int main()
{
    using namespace uvsr;

    std::string error;
    Require(ValidateSettingsSnapshotSelectorToken(
            "gpu.adapter", "12", error) &&
            !ValidateSettingsSnapshotSelectorToken(
                "gpu.adapter", "012", error) &&
            ValidateSettingsSnapshotSelectorToken(
                "scene.current", "folder/main.scene.json", error) &&
            !ValidateSettingsSnapshotSelectorToken(
                "scene.current", "../main.scene.json", error) &&
            !ValidateSettingsSnapshotSelectorToken(
                "scene.current", "./main.scene.json", error) &&
            ValidateSettingsSnapshotSelectorToken(
                "light.selected", "2:Key Light", error) &&
            !ValidateSettingsSnapshotSelectorToken(
                "light.selected", "2", error) &&
            ValidateSettingsSnapshotSelectorToken(
                "material.selected", "none", error) &&
            ValidateSettingsSnapshotSelectorToken(
                "material.selected", "42", error),
        "selector tokens must have one canonical replay representation");

    {
        KeyedRuntime runtime;
        PopulateValues(runtime);
        SettingsSnapshotTransactionCoordinator coordinator;
        SettingsSnapshotTransactionStep step = coordinator.Begin(
            BuildTransaction(), runtime.Access());
        Require(
            step.progress ==
                SettingsSnapshotTransactionProgress::RestartRequired &&
                runtime.persisted.has_value() &&
                !runtime.persisted->rollingBack &&
                runtime.persisted->changedValueCount == 1u &&
                runtime.persisted->sourceValues.size() ==
                    BuildTransaction().size(),
            "adapter selection must persist complete pre-restart state before "
            "requesting a restart");
        Require(
            EventIndex(runtime, "select:gpu.adapter=1:apply") <
                EventIndex(runtime, "persist:apply"),
            "adapter target staging must precede durable restart handoff "
            "publication");
        Require(std::find(
                runtime.validated.begin(), runtime.validated.end(),
                "scene.current") == runtime.validated.end(),
            "selector tables must not resolve against the old parent during "
            "whole-payload preflight");

        const SettingsSnapshotRestartHandoff handoff = *runtime.persisted;
        const std::size_t eventsBeforeWrongAdapterResume =
            runtime.events.size();
        SettingsSnapshotTransactionCoordinator wrongAdapterResume;
        const SettingsSnapshotTransactionStep wrongAdapterStep =
            wrongAdapterResume.Resume(handoff, runtime.Access());
        Require(
            wrongAdapterStep.progress ==
                    SettingsSnapshotTransactionProgress::Failed &&
                wrongAdapterStep.result.failureStage ==
                    SettingsSnapshotTransactionFailureStage::Preflight &&
                runtime.adapter == "0" &&
                runtime.events.size() == eventsBeforeWrongAdapterResume,
            "resume must reject an adapter restart that did not activate the "
            "journal target without counting or requesting it again");
        runtime.ResolvePending();
        SettingsSnapshotTransactionCoordinator resumed;
        step = resumed.Resume(handoff, runtime.Access());
        step = ResolveNonRestartingSelectors(resumed, runtime, step);
        Require(
                step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
                step.result.succeeded &&
                step.result.changedValueCount == 7u &&
                runtime.adapter == "1" &&
                runtime.scene == "target.scene.json" &&
                runtime.light == "1:target-light" &&
                runtime.material == "20" &&
                runtime.values["global.value"] == "requested" &&
                runtime.values[KeyedRuntime::Key(
                    "1", "target.scene.json", "1:target-light",
                    "light.selected.flashlight.brightness")] == "requested" &&
                runtime.values[KeyedRuntime::Key(
                    "1", "target.scene.json", "20",
                    "material.selected.roughness")] == "requested",
            "a resumed transaction must restore selectors, target dependents, "
            "and remaining values in dependency order");
        Require(
            EventIndex(runtime, "select:scene.current") <
                EventIndex(runtime, "select:light.selected") &&
            EventIndex(runtime, "select:light.selected") <
                EventIndex(runtime, "select:material.selected") &&
            EventIndex(runtime, "select:material.selected") <
                EventIndex(runtime, "write:1|target.scene.json"),
            "scene, light, material, and dependent writes must remain ordered");
        const std::size_t eventsBeforeIdempotent = runtime.events.size();
        SettingsSnapshotTransactionCoordinator idempotent;
        const SettingsSnapshotTransactionStep idempotentStep =
            idempotent.Begin(BuildTransaction(), runtime.Access());
        Require(
            idempotentStep.progress ==
                SettingsSnapshotTransactionProgress::Succeeded &&
                idempotentStep.result.changedValueCount == 0u &&
                runtime.events.size() == eventsBeforeIdempotent,
            "an already-applied selector/object transaction must issue no "
            "selector request or value write");
    }

    {
        KeyedRuntime runtime;
        PopulateValues(runtime);
        runtime.adapter = "1";
        runtime.scene = "adapter-one.scene.json";
        runtime.light = "0:adapter-one-light";
        runtime.material = "30";
        runtime.values[KeyedRuntime::Key(
            "1", "adapter-one.scene.json", "0:adapter-one-light",
            "light.selected.flashlight.brightness")] = "source-light";
        runtime.publishSceneIdentityBeforeReady = true;
        const std::vector<SettingsSnapshotTransactionEntry> transaction = {
            { "scene.current", "target.scene.json",
                SettingsSnapshotApplicationMode::Selector },
            { "light.selected", "1:target-light",
                SettingsSnapshotApplicationMode::Selector },
            { "light.selected.flashlight.brightness", "requested",
                SettingsSnapshotApplicationMode::LiveDependent },
            { "material.selected", "20",
                SettingsSnapshotApplicationMode::Selector }
        };

        SettingsSnapshotTransactionCoordinator coordinator;
        SettingsSnapshotTransactionStep step = coordinator.Begin(
            transaction, runtime.Access());
        Require(
            step.progress == SettingsSnapshotTransactionProgress::Pending &&
                runtime.scene == "target.scene.json" &&
                EventIndex(runtime, "select:light.selected") ==
                    runtime.events.size() &&
                EventIndex(runtime, "select:material.selected") ==
                    runtime.events.size() &&
                EventIndex(runtime, "write:") == runtime.events.size(),
            "an eagerly published scene token must not expose child selectors "
            "or dependent writes before scene readiness");

        step = coordinator.Advance(runtime.Access());
        Require(
            step.progress == SettingsSnapshotTransactionProgress::Pending &&
                EventIndex(runtime, "select:light.selected") ==
                    runtime.events.size() &&
                EventIndex(runtime, "write:") == runtime.events.size(),
            "a matching scene token must keep polling the issued transition");

        runtime.ResolvePending();
        step = coordinator.Advance(runtime.Access());
        Require(
            step.progress == SettingsSnapshotTransactionProgress::Pending &&
                EventIndex(runtime, "select:light.selected") <
                    runtime.events.size(),
            "child selector work may begin only after the scene transition "
            "reports ready");
    }

    {
        KeyedRuntime runtime;
        PopulateValues(runtime);
        runtime.adapter = "1";
        runtime.scene = "adapter-one.scene.json";
        runtime.light = "0:adapter-one-light";
        runtime.material = "30";
        runtime.publishSceneIdentityBeforeReady = true;
        runtime.failWriteName = "global.value";
        const std::vector<SettingsSnapshotTransactionEntry> transaction = {
            { "scene.current", "target.scene.json",
                SettingsSnapshotApplicationMode::Selector },
            { "global.value", "requested",
                SettingsSnapshotApplicationMode::Mutable }
        };
        SettingsSnapshotTransactionCoordinator coordinator;
        SettingsSnapshotTransactionStep step = coordinator.Begin(
            transaction, runtime.Access());
        runtime.ResolvePending();
        step = coordinator.Advance(runtime.Access());
        Require(
            step.progress == SettingsSnapshotTransactionProgress::Pending &&
                runtime.scene == "adapter-one.scene.json",
            "rollback must remain pending when the source scene token is "
            "published before rollback readiness");
        step = coordinator.Advance(runtime.Access());
        Require(
            step.progress == SettingsSnapshotTransactionProgress::Pending,
            "rollback must poll begin=false despite matching selector readback");
        runtime.ResolvePending();
        step = coordinator.Advance(runtime.Access());
        Require(
            step.progress == SettingsSnapshotTransactionProgress::Failed &&
                step.result.rollbackSucceeded,
            "rollback may complete only after its issued selector transition "
            "reports ready");
    }

    {
        KeyedRuntime runtime;
        PopulateValues(runtime);
        runtime.failWriteName = "global.value";
        SettingsSnapshotTransactionCoordinator coordinator;
        SettingsSnapshotTransactionStep step = coordinator.Begin(
            BuildTransaction(), runtime.Access());
        Require(step.progress ==
                SettingsSnapshotTransactionProgress::RestartRequired,
            "rollback fixture must begin with the adapter handoff");
        SettingsSnapshotRestartHandoff handoff = *runtime.persisted;
        runtime.ResolvePending();
        SettingsSnapshotTransactionCoordinator resumed;
        step = resumed.Resume(handoff, runtime.Access());
        step = ResolveNonRestartingSelectors(resumed, runtime, step);
        Require(
            step.progress ==
                SettingsSnapshotTransactionProgress::RestartRequired &&
                runtime.persisted.has_value() &&
                runtime.persisted->rollingBack,
            "a target-context failure must restore target dependents and "
            "persist the reverse adapter handoff");
        const std::size_t targetLightRestore = EventIndex(
            runtime,
            "write:1|target.scene.json|1:target-light|");
        const std::size_t targetMaterialRestore = EventIndex(
            runtime,
            "write:1|target.scene.json|20|");
        const std::size_t adapterRollback = EventIndex(
            runtime, "select:gpu.adapter=0:rollback");
        Require(
            targetLightRestore < adapterRollback &&
                targetMaterialRestore < adapterRollback &&
                runtime.values[KeyedRuntime::Key(
                    "1", "target.scene.json", "1:target-light",
                    "light.selected.flashlight.brightness")] ==
                    "target-light-pre" &&
                runtime.values[KeyedRuntime::Key(
                    "1", "target.scene.json", "20",
                    "material.selected.roughness")] ==
                    "target-material-pre",
            "target-object state must be restored before returning to the "
            "source adapter and selectors");

        handoff = *runtime.persisted;
        runtime.ResolvePending();
        SettingsSnapshotTransactionCoordinator rollbackResumed;
        step = rollbackResumed.Resume(handoff, runtime.Access());
        step = ResolveNonRestartingSelectors(
            rollbackResumed, runtime, step);
        Require(
            step.progress == SettingsSnapshotTransactionProgress::Failed &&
                !step.result.succeeded && step.result.rollbackAttempted &&
                step.result.rollbackSucceeded &&
                runtime.adapter == "0" &&
                runtime.scene == "old.scene.json" &&
                runtime.light == "0:old-light" &&
                runtime.material == "10" &&
                runtime.values["global.value"] == "source" &&
                runtime.values[KeyedRuntime::Key(
                    "0", "old.scene.json", "0:old-light",
                    "light.selected.flashlight.brightness")] ==
                    "source-light" &&
                runtime.values[KeyedRuntime::Key(
                    "0", "old.scene.json", "10",
                    "material.selected.roughness")] == "source-material",
            "selector/object-aware rollback must survive both restart "
            "boundaries without leaking target values");
    }

    {
        KeyedRuntime runtime;
        PopulateValues(runtime);
        runtime.failWriteName = "global.value";
        runtime.failRollbackName = "material.selected.roughness";
        SettingsSnapshotTransactionCoordinator coordinator;
        SettingsSnapshotTransactionStep step = coordinator.Begin(
            BuildTransaction(), runtime.Access());
        const SettingsSnapshotRestartHandoff handoff = *runtime.persisted;
        runtime.ResolvePending();
        SettingsSnapshotTransactionCoordinator resumed;
        step = resumed.Resume(handoff, runtime.Access());
        step = ResolveNonRestartingSelectors(resumed, runtime, step);
        Require(
            step.progress == SettingsSnapshotTransactionProgress::Failed &&
                step.result.rollbackAttempted &&
                !step.result.rollbackSucceeded &&
                step.result.error.find("rollback failed") !=
                    std::string::npos &&
                runtime.adapter == "1",
            "a target-object rollback failure must remain distinct and must "
            "not return to the source selector with leaked target state");
    }

    {
        KeyedRuntime runtime;
        PopulateValues(runtime);
        auto invalid = BuildTransaction();
        invalid.push_back({
            "invalid.late", "bad", SettingsSnapshotApplicationMode::Mutable });
        runtime.values["invalid.late"] = "source";
        SettingsSnapshotTransactionCoordinator coordinator;
        const SettingsSnapshotTransactionStep step = coordinator.Begin(
            invalid, runtime.Access());
        Require(
            step.progress == SettingsSnapshotTransactionProgress::Failed &&
                step.result.failureStage ==
                    SettingsSnapshotTransactionFailureStage::Preflight &&
                runtime.events.empty() && !runtime.mutationSeen,
            "a late invalid value must reject the complete payload before "
            "capture, selector resolution, or mutation");
    }

    {
        SettingsSnapshotRestartHandoff base;
        base.transaction = {{
            "global.value",
            "requested",
            SettingsSnapshotApplicationMode::Mutable
        }};
        base.sourceValues = { "source" };

        const auto requireRejected = [](
            SettingsSnapshotRestartHandoff corrupt,
            std::string_view reason)
        {
            KeyedRuntime runtime;
            PopulateValues(runtime);
            SettingsSnapshotTransactionCoordinator coordinator;
            const SettingsSnapshotTransactionStep step = coordinator.Resume(
                corrupt, runtime.Access());
            Require(
                step.progress == SettingsSnapshotTransactionProgress::Failed &&
                    !runtime.mutationSeen && runtime.events.empty(),
                "corrupt restart handoff must fail before mutation: " +
                    std::string(reason));
        };

        SettingsSnapshotRestartHandoff corrupt = base;
        corrupt.transaction[0].requestedValue = "<unavailable>";
        requireRejected(std::move(corrupt), "mutable requested sentinel");
        corrupt = base;
        corrupt.sourceValues[0] = "<unavailable>";
        requireRejected(std::move(corrupt), "mutable source sentinel");
        corrupt = base;
        corrupt.sourceValues[0] = "invalid-source";
        requireRejected(std::move(corrupt), "invalid captured source value");
        corrupt = base;
        corrupt.transaction[0] = {
            "scene.current",
            "target.scene.json",
            SettingsSnapshotApplicationMode::Mutable
        };
        corrupt.sourceValues[0] = "old.scene.json";
        requireRejected(std::move(corrupt), "selector forged as mutable");
        corrupt = base;
        corrupt.failureStage =
            static_cast<SettingsSnapshotTransactionFailureStage>(255u);
        requireRejected(std::move(corrupt), "invalid failure stage");
        corrupt = base;
        corrupt.changedValueCount = 2u;
        requireRejected(std::move(corrupt), "out-of-range change count");
        corrupt = base;
        corrupt.rollingBack = true;
        requireRejected(std::move(corrupt), "rollback without failure record");
        corrupt = base;
        corrupt.rollingBack = true;
        corrupt.failureStage =
            SettingsSnapshotTransactionFailureStage::Apply;
        corrupt.failure = "recorded failure";
        requireRejected(std::move(corrupt), "rollback without a changed value");
    }

    std::cout << "UVSR settings selector coordinator validation passed\n";
    return EXIT_SUCCESS;
}
