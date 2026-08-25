#include "uvsr_settings_commands.h"

#include "settings_snapshot.h"
#include "settings_snapshot_decoder.h"

#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "UVSR settings command owner validation failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    const uvsr::UiSettingsCommandDefinition& Definition(
        std::string_view name)
    {
        const auto* definition =
            uvsr::FindSettingsCommandDefinition(name);
        if (!definition)
            Fail("catalog fixture is missing " + std::string(name));
        return *definition;
    }

    std::string FirstEnumValue(std::string_view domain)
    {
        domain = domain.substr(0u, domain.find('|'));
        domain = domain.substr(0u, domain.find(';'));
        while (!domain.empty() &&
            (domain.front() == ' ' || domain.front() == '\t'))
        {
            domain.remove_prefix(1u);
        }
        while (!domain.empty() &&
            (domain.back() == ' ' || domain.back() == '\t'))
        {
            domain.remove_suffix(1u);
        }
        return std::string(domain);
    }

    std::string FixtureValue(
        const uvsr::UiSettingsCommandDefinition& definition)
    {
        using Kind = uvsr::UiSettingsCommandKind;
        if (definition.kind == Kind::DynamicSelection)
        {
            if (definition.name == "gpu.adapter")
                return "0";
            if (definition.name == "scene.current")
                return "fixture/main.scene.json";
            if (definition.name == "light.selected")
                return "0:fixture-light";
            if (definition.name == "material.selected")
                return "none";
            Fail("unowned dynamic selector fixture");
        }
        if (definition.dynamic)
            return "<unavailable>";

        std::string value(definition.defaultValue);
        std::string error;
        if (uvsr::ValidateSettingsSnapshotCatalogValue(
                definition, value, error))
        {
            return value;
        }
        if (definition.kind == Kind::Enum)
        {
            value = FirstEnumValue(definition.domain);
            if (uvsr::ValidateSettingsSnapshotCatalogValue(
                    definition, value, error))
            {
                return value;
            }
        }
        Fail("could not form a valid live fixture for " +
            std::string(definition.name) + ": " + error);
    }
}

int main()
{
    using namespace uvsr;

    bool boolean = false;
    Require(TryParseCommandBool("Enabled", boolean) && boolean,
        "boolean parsing must remain case/separator insensitive");
    std::int64_t integer = 0;
    Require(TryParseCommandInteger("-17", integer) && integer == -17,
        "integer parsing must consume the complete token");
    Require(!TryParseCommandInteger("17px", integer),
        "integer parsing must reject suffixes");
    Require(FormatCommandFloat(0.123456789f) == "0.123",
        "ordinary command output must retain concise float formatting");
    {
        SettingsCommandFloatPrecisionScope precise;
        Require(FormatCommandFloat(0.123456789f) != "0.123",
            "snapshot capture must use round-trip float precision");
    }
    Require(FormatCommandFloat(0.123456789f) == "0.123",
        "snapshot precision scope must restore ordinary formatting");
    Require(
        BuildSettingsSnapshotCommandArguments(
            Definition("ui.accent.primary"), "0.1 0.2 0.3 1") ==
            std::vector<std::string>({ "0.1", "0.2", "0.3", "1" }),
        "vector snapshot values must use ordinary command arguments");

    const std::vector<SettingsSnapshotAdapterOption> adapters = {
        { 0, "Duplicate GPU" }, { 1, "Duplicate GPU" }
    };
    std::int64_t adapterIndex = -1;
    std::string canonicalToken;
    std::string selectorError;
    std::size_t selectorWrites = 0u;
    Require(!ResolveSettingsSnapshotAdapterToken(
            "Duplicate GPU",
            adapters,
            adapterIndex,
            canonicalToken,
            selectorError) &&
            selectorWrites == 0u &&
            ResolveSettingsSnapshotAdapterToken(
                "1",
                adapters,
                adapterIndex,
                canonicalToken,
                selectorError) &&
            adapterIndex == 1 && canonicalToken == "1",
        "duplicate adapter display names must fail without mutation while "
        "the canonical numeric token remains exact");

    const std::vector<SettingsSnapshotSceneOption> scenes = {
        { "a/main.scene.json", "Duplicate Scene" },
        { "b/main.scene.json", "Duplicate Scene" },
        { "collision.scene.json", "Primary" },
        { "other.scene.json", "collision.scene.json" },
        { "numeric.scene.json", "7" }
    };
    std::string sceneFile;
    Require(!ResolveSettingsSnapshotSceneToken(
            "Duplicate Scene",
            scenes,
            sceneFile,
            canonicalToken,
            selectorError) &&
            selectorWrites == 0u &&
            ResolveSettingsSnapshotSceneToken(
                "b/main.scene.json",
                scenes,
                sceneFile,
                canonicalToken,
                selectorError) &&
            sceneFile == "b/main.scene.json" &&
            canonicalToken == sceneFile,
        "duplicate scene display names must fail while exact filenames "
        "round-trip canonically");
    Require(ResolveSettingsSnapshotSceneToken(
            "collision.scene.json",
            scenes,
            sceneFile,
            canonicalToken,
            selectorError) &&
            sceneFile == "collision.scene.json" &&
            ResolveSettingsSnapshotSceneToken(
                "7",
                scenes,
                sceneFile,
                canonicalToken,
                selectorError) &&
            sceneFile == "numeric.scene.json",
        "an exact canonical scene filename must outrank a display-name "
        "collision while numeric-looking display names remain friendly input");
    const std::vector<SettingsSnapshotSceneOption> runtimeScenes = {
        {
            "bistro/main.scene.json",
            "Bistro",
            "C:/package/media/bistro/main.scene.json"
        }
    };
    Require(ResolveSettingsSnapshotSceneToken(
            "bistro/main.scene.json",
            runtimeScenes,
            sceneFile,
            canonicalToken,
            selectorError) &&
            sceneFile == "C:/package/media/bistro/main.scene.json" &&
            canonicalToken == "bistro/main.scene.json",
        "scene snapshots must preserve a relative canonical token while "
        "selecting the exact runtime catalog path");

    const std::vector<SettingsSnapshotLightOption> lights = {
        { 0u, "duplicate-light" }, { 1u, "duplicate-light" }
    };
    std::size_t lightIndex = 0u;
    Require(!ResolveSettingsSnapshotLightToken(
            "duplicate-light",
            lights,
            lightIndex,
            canonicalToken,
            selectorError) &&
            selectorWrites == 0u &&
            ResolveSettingsSnapshotLightToken(
                "1:duplicate-light",
                lights,
                lightIndex,
                canonicalToken,
                selectorError) &&
            lightIndex == 1u && canonicalToken == "1:duplicate-light",
        "duplicate light identities must require the stable index:identity "
        "snapshot token");

    const std::vector<SettingsSnapshotMaterialOption> materials = {
        { 10u, "duplicate-material" },
        { 20u, "duplicate-material" }
    };
    bool noMaterial = false;
    std::uint32_t materialId = 0u;
    Require(!ResolveSettingsSnapshotMaterialToken(
            "duplicate-material",
            materials,
            noMaterial,
            materialId,
            canonicalToken,
            selectorError) &&
            selectorWrites == 0u &&
            ResolveSettingsSnapshotMaterialToken(
                "20",
                materials,
                noMaterial,
                materialId,
                canonicalToken,
                selectorError) &&
            !noMaterial && materialId == 20u && canonicalToken == "20" &&
            ResolveSettingsSnapshotMaterialToken(
                "none",
                materials,
                noMaterial,
                materialId,
                canonicalToken,
                selectorError) &&
            noMaterial && canonicalToken == "none",
        "material snapshots must use an exact runtime id or explicit none");

    std::string adapterGet = FormatSettingsSnapshotAdapterToken(0);
    adapterIndex = 1;
    Require(ResolveSettingsSnapshotAdapterToken(
            adapterGet,
            adapters,
            adapterIndex,
            canonicalToken,
            selectorError) &&
            FormatSettingsSnapshotAdapterToken(adapterIndex) == adapterGet,
        "adapter GET -> select away -> SET token -> GET must be exact");
    std::string sceneGet = FormatSettingsSnapshotSceneToken(
        "a/main.scene.json");
    sceneFile = "b/main.scene.json";
    Require(ResolveSettingsSnapshotSceneToken(
            sceneGet,
            scenes,
            sceneFile,
            canonicalToken,
            selectorError) && canonicalToken == sceneGet,
        "scene GET -> select away -> SET token -> GET must be exact");
    std::string lightGet = FormatSettingsSnapshotLightToken(
        0u, "duplicate-light");
    lightIndex = 1u;
    Require(ResolveSettingsSnapshotLightToken(
            lightGet,
            lights,
            lightIndex,
            canonicalToken,
            selectorError) && canonicalToken == lightGet,
        "light GET -> select away -> SET token -> GET must be exact");
    std::string materialGet = FormatSettingsSnapshotMaterialToken(
        false, 10u);
    materialId = 20u;
    Require(ResolveSettingsSnapshotMaterialToken(
            materialGet,
            materials,
            noMaterial,
            materialId,
            canonicalToken,
            selectorError) && canonicalToken == materialGet,
        "material GET -> select away -> SET token -> GET must be exact");

    const std::int64_t maximumAdapter =
        static_cast<std::int64_t>((std::numeric_limits<int>::max)());
    const std::vector<SettingsSnapshotAdapterOption> boundaryAdapters = {
        { maximumAdapter, "Maximum Adapter" }
    };
    Require(ResolveSettingsSnapshotAdapterToken(
            std::to_string(maximumAdapter),
            boundaryAdapters,
            adapterIndex,
            canonicalToken,
            selectorError) &&
            adapterIndex == maximumAdapter &&
            !ResolveSettingsSnapshotAdapterToken(
                std::to_string(maximumAdapter + 1),
                boundaryAdapters,
                adapterIndex,
                canonicalToken,
                selectorError),
        "adapter tokens must accept INT_MAX and reject the next value");

    const std::size_t maximumLight =
        (std::numeric_limits<std::size_t>::max)();
    const std::vector<SettingsSnapshotLightOption> boundaryLights = {
        { maximumLight, "maximum-light" }
    };
    const std::string maximumLightToken =
        std::to_string(maximumLight) + ":maximum-light";
    Require(ResolveSettingsSnapshotLightToken(
            maximumLightToken,
            boundaryLights,
            lightIndex,
            canonicalToken,
            selectorError) &&
            lightIndex == maximumLight &&
            !ResolveSettingsSnapshotLightToken(
                "18446744073709551616:maximum-light",
                boundaryLights,
                lightIndex,
                canonicalToken,
                selectorError),
        "light tokens must accept SIZE_MAX and reject unsigned overflow");

    const std::uint32_t maximumMaterial =
        (std::numeric_limits<std::uint32_t>::max)();
    const std::vector<SettingsSnapshotMaterialOption> boundaryMaterials = {
        { maximumMaterial, "Maximum Material" }
    };
    Require(ResolveSettingsSnapshotMaterialToken(
            std::to_string(maximumMaterial),
            boundaryMaterials,
            noMaterial,
            materialId,
            canonicalToken,
            selectorError) &&
            materialId == maximumMaterial &&
            !ResolveSettingsSnapshotMaterialToken(
                "4294967296",
                boundaryMaterials,
                noMaterial,
                materialId,
                canonicalToken,
                selectorError),
        "material tokens must accept UINT32_MAX and reject the next value");

    std::map<std::string, std::string, std::less<>> live;
    for (const UiSettingsCommandDefinition& definition :
        UiSettingsCommandCatalog)
    {
        if (IsSettingsSnapshotValue(definition))
            live.emplace(std::string(definition.name), FixtureValue(definition));
    }

    std::size_t writes = 0u;
    const auto read = [&live](
        std::string_view name,
        std::string& value,
        std::string& error)
    {
        const auto found = live.find(name);
        if (found == live.end())
        {
            error = "missing fake live value";
            return false;
        }
        value = found->second;
        return true;
    };
    const auto validate = [](
        std::string_view name,
        std::string_view value,
        std::string& error)
    {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(name);
        return definition && IsSettingsSnapshotValue(*definition) &&
            ValidateSettingsSnapshotCatalogValue(*definition, value, error);
    };
    const auto write = [&live, &writes](
        std::string_view name,
        std::string_view value,
        std::string& error)
    {
        const auto found = live.find(name);
        if (found == live.end())
        {
            error = "missing fake live value";
            return false;
        }
        ++writes;
        found->second = value;
        return true;
    };

    SettingsSnapshotController controller;
    controller.Refresh(read);
    const DecodedSettings captured =
        ParseSettingsSnapshot(controller.Canonical());
    Require(captured.size() == live.size() &&
            IsSettingsSnapshotCode(controller.Code()),
        "controller capture must include the complete authoritative catalog");

    SettingsSnapshotRuntimeAccess access{
        true,
        validate,
        read,
        write
    };
    std::size_t changed = 0u;
    std::string error;
    Require(controller.ApplyCanonical(
            controller.Canonical(), access, changed, error) &&
            changed == 0u && writes == 0u,
        "a complete idempotent snapshot must verify without setters: " +
            error);

    DecodedSettings changedPayload = captured;
    const std::string previousAo = changedPayload.at(
        "visibility.ao.enabled");
    changedPayload["visibility.ao.enabled"] =
        previousAo == "on" ? "off" : "on";
    const std::string changedCanonical =
        FormatCanonicalSettingsSnapshot(changedPayload);
    Require(controller.ApplyCanonical(
            changedCanonical, access, changed, error) &&
            changed == 1u && writes == 1u &&
            live.at("visibility.ao.enabled") != previousAo,
        "one valid nondefault setting must apply through the transaction: " +
            error);

    DecodedSettings missing = changedPayload;
    missing.erase("visibility.ao.enabled");
    const std::size_t writesBeforeReject = writes;
    Require(!controller.ApplyCanonical(
            FormatCanonicalSettingsSnapshot(missing),
            access,
            changed,
            error) &&
            writes == writesBeforeReject &&
            error.find("missing") != std::string::npos,
        "missing membership must reject before mutation");

    DecodedSettings unknown = changedPayload;
    unknown.emplace("unknown.fixture.setting", "invalid-fixture-value");
    Require(!controller.ApplyCanonical(
            FormatCanonicalSettingsSnapshot(unknown),
            access,
            changed,
            error) &&
            writes == writesBeforeReject &&
            error.find("unknown") != std::string::npos,
        "a concrete retired setting must reject before mutation");

    DecodedSettings selectorPayload = changedPayload;
    selectorPayload["scene.current"] = "target/main.scene.json";
    access.driveSelector = [&live](
        std::string_view name,
        std::string_view value,
        bool begin,
        bool,
        std::string& selectorFailure)
    {
        if (name != "scene.current" || !begin)
        {
            selectorFailure = "unexpected selector transition";
            return SettingsSnapshotSelectorTransition::Failed;
        }
        live[std::string(name)] = std::string(value);
        return SettingsSnapshotSelectorTransition::Ready;
    };
    SettingsSnapshotTransactionStep staged =
        controller.BeginApplyCanonicalStaged(
            FormatCanonicalSettingsSnapshot(selectorPayload), access);
    Require(
        staged.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            live.at("scene.current") == "target/main.scene.json" &&
            staged.result.changedValueCount == 1u,
        "the production controller must drive a changed canonical selector "
        "through the staged transaction path");
    staged = controller.BeginApplyCanonicalStaged(
        FormatCanonicalSettingsSnapshot(selectorPayload), access);
    Require(
        staged.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            staged.result.changedValueCount == 0u,
        "a resolved selector transaction must be idempotent");

    DecodedSettings pendingPayload = selectorPayload;
    pendingPayload["scene.current"] = "pending/main.scene.json";
    bool selectorReady = false;
    access.driveSelector = [&live, &selectorReady](
        std::string_view name,
        std::string_view value,
        bool begin,
        bool,
        std::string& selectorFailure)
    {
        if (name != "scene.current")
        {
            selectorFailure = "unexpected selector";
            return SettingsSnapshotSelectorTransition::Failed;
        }
        if (begin)
            return SettingsSnapshotSelectorTransition::Pending;
        if (!selectorReady)
            return SettingsSnapshotSelectorTransition::Pending;
        live[std::string(name)] = std::string(value);
        return SettingsSnapshotSelectorTransition::Ready;
    };
    staged = controller.BeginApplyCanonicalStaged(
        FormatCanonicalSettingsSnapshot(pendingPayload), access);
    Require(
        staged.progress == SettingsSnapshotTransactionProgress::Pending &&
            controller.HasStagedApply(),
        "controller concurrency fixture must hold one pending transaction");
    const SettingsSnapshotTransactionStep rejectedSecond =
        controller.BeginApplyCanonicalStaged(
            FormatCanonicalSettingsSnapshot(selectorPayload), access);
    const SettingsSnapshotTransactionStep rejectedLoad =
        controller.BeginLoadCodeStaged("not-a-code", access);
    const SettingsSnapshotTransactionStep rejectedResume =
        controller.ResumeStagedApply({}, access);
    Require(
        rejectedSecond.progress == SettingsSnapshotTransactionProgress::Failed &&
            rejectedLoad.progress == SettingsSnapshotTransactionProgress::Failed &&
            rejectedResume.progress == SettingsSnapshotTransactionProgress::Failed &&
            rejectedSecond.result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Configuration &&
            controller.HasStagedApply(),
        "second begin, load, and resume requests must reject without resetting "
        "the active coordinator");
    std::size_t legacyChanged = 0u;
    Require(!controller.ApplyCanonical(
            FormatCanonicalSettingsSnapshot(selectorPayload),
            access,
            legacyChanged,
            error) &&
            error.find("active") != std::string::npos &&
            controller.HasStagedApply(),
        "legacy immediate apply must never abandon active asynchronous work");
    selectorReady = true;
    staged = controller.ContinueStagedApply(access);
    Require(
        staged.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            live.at("scene.current") == "pending/main.scene.json" &&
            !controller.HasStagedApply(),
        "the original pending transaction must survive rejected concurrent "
        "requests and complete unchanged");
    const std::size_t writesBeforeLegacySelector = writes;
    Require(
        !controller.ApplyCanonical(
            FormatCanonicalSettingsSnapshot(selectorPayload),
            access,
            legacyChanged,
            error) &&
            !controller.HasStagedApply() &&
            live.at("scene.current") == "pending/main.scene.json" &&
            writes == writesBeforeLegacySelector &&
            error.find("staged") != std::string::npos,
        "legacy bool apply must reject unresolved selectors immediately "
        "without leaving asynchronous work active");

    SettingsSnapshotController journalController;
    journalController.Refresh(read);
    DecodedSettings restartPayload =
        ParseSettingsSnapshot(journalController.Canonical());
    restartPayload["gpu.adapter"] = "1";
    std::optional<SettingsSnapshotRestartHandoff> restartHandoff;
    access.driveSelector = [](
        std::string_view name,
        std::string_view,
        bool begin,
        bool,
        std::string& selectorFailure)
    {
        if (name == "gpu.adapter" && begin)
            return SettingsSnapshotSelectorTransition::RestartRequired;
        selectorFailure = "unexpected journal selector transition";
        return SettingsSnapshotSelectorTransition::Failed;
    };
    access.persistRestartHandoff = [&restartHandoff](
        const SettingsSnapshotRestartHandoff& handoff,
        std::string&)
    {
        restartHandoff = handoff;
        return true;
    };
    staged = journalController.BeginApplyCanonicalStaged(
        FormatCanonicalSettingsSnapshot(restartPayload), access);
    Require(
        staged.progress ==
                SettingsSnapshotTransactionProgress::RestartRequired &&
            restartHandoff.has_value(),
        "adapter changes must expose a complete persisted controller handoff");

    live["gpu.adapter"] = "1";
    SettingsSnapshotController resumedController;
    staged = resumedController.ResumeStagedApply(*restartHandoff, access);
    Require(
        staged.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            staged.result.succeeded &&
            staged.result.changedValueCount == 1u,
        "SettingsSnapshotController::ResumeStagedApply must accept the exact "
        "authoritative journal after the adapter restart");
    live["gpu.adapter"] = "0";

    const auto requireControllerJournalRejected = [&access, &writes](
        SettingsSnapshotRestartHandoff corrupt,
        std::string_view reason)
    {
        const std::size_t writesBefore = writes;
        SettingsSnapshotController rejectedController;
        const SettingsSnapshotTransactionStep step =
            rejectedController.ResumeStagedApply(corrupt, access);
        Require(
            step.progress == SettingsSnapshotTransactionProgress::Failed &&
                step.result.failureStage ==
                    SettingsSnapshotTransactionFailureStage::Configuration &&
                writes == writesBefore,
            "controller must reject a corrupt restart journal before "
            "mutation: " + std::string(reason));
    };
    SettingsSnapshotRestartHandoff corruptJournal = *restartHandoff;
    corruptJournal.transaction.pop_back();
    corruptJournal.sourceValues.pop_back();
    requireControllerJournalRejected(
        std::move(corruptJournal), "missing catalog entry");
    corruptJournal = *restartHandoff;
    std::swap(
        corruptJournal.transaction[0],
        corruptJournal.transaction[1]);
    std::swap(
        corruptJournal.sourceValues[0],
        corruptJournal.sourceValues[1]);
    requireControllerJournalRejected(
        std::move(corruptJournal), "catalog reordering");
    corruptJournal = *restartHandoff;
    for (SettingsSnapshotTransactionEntry& entry :
         corruptJournal.transaction)
    {
        if (entry.name == "scene.current")
        {
            entry.mode = SettingsSnapshotApplicationMode::Mutable;
            break;
        }
    }
    requireControllerJournalRejected(
        std::move(corruptJournal), "forged selector mode");

    const std::filesystem::path journalPath =
        std::filesystem::temp_directory_path() /
        ("uvsr-settings-restart-handoff-" +
            std::to_string(GetCurrentProcessId()) + ".bin");
    std::error_code fileError;
    std::filesystem::remove(journalPath, fileError);
    std::filesystem::path journalTemporary = journalPath;
    journalTemporary += L".tmp";
    std::filesystem::remove(journalTemporary, fileError);
    Require(PersistSettingsSnapshotRestartHandoff(
            journalPath, *restartHandoff, error),
        "restart handoff must be written atomically: " + error);
    {
        std::ofstream staleTemporary(
            journalTemporary,
            std::ios::binary | std::ios::trunc);
        staleTemporary << "unpublished";
    }
    SettingsSnapshotRestartHandoff loadedHandoff;
    bool handoffFound = false;
    Require(LoadSettingsSnapshotRestartHandoff(
            journalPath, loadedHandoff, handoffFound, error) &&
            handoffFound &&
            !std::filesystem::exists(journalTemporary) &&
            loadedHandoff.transaction.size() ==
                restartHandoff->transaction.size() &&
            loadedHandoff.sourceValues == restartHandoff->sourceValues &&
            loadedHandoff.changedValueCount ==
                restartHandoff->changedValueCount &&
            loadedHandoff.rollingBack == restartHandoff->rollingBack &&
            loadedHandoff.failureStage == restartHandoff->failureStage &&
            loadedHandoff.failure == restartHandoff->failure,
        "restart handoff must survive an exact durable round trip: " +
            error);
    for (std::size_t index = 0u;
         index < loadedHandoff.transaction.size();
         ++index)
    {
        Require(
            loadedHandoff.transaction[index].name ==
                    restartHandoff->transaction[index].name &&
                loadedHandoff.transaction[index].requestedValue ==
                    restartHandoff->transaction[index].requestedValue &&
                loadedHandoff.transaction[index].mode ==
                    restartHandoff->transaction[index].mode,
            "restart handoff entry order and modes must round trip exactly");
    }
    corruptJournal = *restartHandoff;
    corruptJournal.transaction.front().mode =
        static_cast<SettingsSnapshotApplicationMode>(999u);
    Require(!PersistSettingsSnapshotRestartHandoff(
            journalPath, corruptJournal, error) &&
            error.find("invalid entry") != std::string::npos,
        "restart handoff persistence must reject corrupt enum values");
    {
        std::fstream tamper(
            journalPath,
            std::ios::binary | std::ios::in | std::ios::out);
        tamper.seekp(0, std::ios::beg);
        tamper.put('X');
        tamper.flush();
    }
    Require(!LoadSettingsSnapshotRestartHandoff(
            journalPath, loadedHandoff, handoffFound, error) &&
            !handoffFound &&
            error.find("integrity") != std::string::npos,
        "restart handoff loading must reject tampering before resume");
    Require(RemoveSettingsSnapshotRestartHandoff(journalPath, error) &&
            !std::filesystem::exists(journalPath) &&
            !std::filesystem::exists(journalTemporary),
        "restart handoff cleanup must remove published and temporary files");

    access.sceneReady = false;
    Require(!controller.ApplyCanonical(
            changedCanonical, access, changed, error) &&
            writes == writesBeforeReject &&
            error.find("fully loaded scene") != std::string::npos,
        "snapshot application must wait for ordinary scene readiness");
    access.sceneReady = true;

    const std::filesystem::path catalogPath =
        std::filesystem::temp_directory_path() /
        ("uvsr-settings-command-owner-" +
            std::to_string(GetCurrentProcessId()) + ".txt");
    std::filesystem::remove(catalogPath, fileError);
    Require(controller.Persist(catalogPath) &&
            controller.Persist(catalogPath) &&
            !std::filesystem::exists(
                std::filesystem::path(catalogPath).concat(L".tmp")),
        "catalog persistence must be idempotent");
    std::ifstream input(catalogPath, std::ios::binary);
    const std::string persisted{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    Require(persisted.find(controller.BuildCatalogSection()) !=
            std::string::npos &&
            persisted.find(controller.BuildCatalogSection(),
                persisted.find(controller.BuildCatalogSection()) + 1u) ==
                std::string::npos,
        "catalog must contain one exact framed entry");

    const std::filesystem::path loadRoot =
        std::filesystem::temp_directory_path() /
        ("uvsr-settings-load-code-" +
            std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(loadRoot, fileError);
    std::filesystem::create_directories(loadRoot / "UVSR", fileError);
    Require(!fileError, "load-code fixture directory must be writable");
    wchar_t previousLocalAppData[32768]{};
    const DWORD previousLength = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        previousLocalAppData,
        static_cast<DWORD>(std::size(previousLocalAppData)));
    Require(SetEnvironmentVariableW(
            L"LOCALAPPDATA",
            loadRoot.c_str()) != FALSE,
        "load-code fixture must set its isolated catalog root");
    const std::string version(SettingsSnapshotVersionText.data(), 4u);
    const std::filesystem::path loadCatalog =
        loadRoot / "UVSR" /
        ("settings-snapshots-v" + version + ".txt");
    Require(controller.Persist(loadCatalog),
        "load-code fixture must persist the exact controller payload");
    const std::string loadCode = controller.Code();
    Require(controller.LoadCode(loadCode, access, changed, error) &&
            changed == 0u,
        "SettingsSnapshotController::LoadCode must decode and apply the "
        "real isolated catalog entry: " + error);
    Require(SetEnvironmentVariableW(
            L"LOCALAPPDATA",
            previousLength > 0u ? previousLocalAppData : nullptr) != FALSE,
        "load-code fixture must restore LOCALAPPDATA");
    std::filesystem::remove_all(loadRoot, fileError);

    {
        std::ofstream conflict(
            catalogPath,
            std::ios::binary | std::ios::trunc);
        conflict << '[' << controller.Code() << "]\ncorrupt\n[/"
                 << controller.Code() << "]\n";
    }
    Require(!controller.Persist(catalogPath),
        "an existing code with a conflicting payload must fail closed");
    std::filesystem::remove(catalogPath, fileError);

    std::cout << "UVSR settings command owner validation passed\n";
    return EXIT_SUCCESS;
}
