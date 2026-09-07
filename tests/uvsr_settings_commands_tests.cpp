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

    std::string FixtureValue(
        const uvsr::UiSettingsCommandDefinition& definition)
    {
        using Kind = uvsr::UiSettingsCommandKind;
        if (definition.kind == Kind::DynamicSelection)
        {
            switch (definition.typedDomain.selector)
            {
            case uvsr::UiSettingsSelectorKind::Adapter:
                return "0";
            case uvsr::UiSettingsSelectorKind::Scene:
                return "fixture/main.scene.json";
            case uvsr::UiSettingsSelectorKind::Light:
                return "0:fixture-light";
            case uvsr::UiSettingsSelectorKind::Material:
                return "none";
            case uvsr::UiSettingsSelectorKind::None:
                break;
            }
            Fail("unowned dynamic selector fixture");
        }
        if (definition.dynamic)
            return "<unavailable>";

        std::string error;
        if (definition.typedDefault.HasValue())
        {
            const std::string value =
                uvsr::FormatUiSettingsDefaultAnchor(definition);
            if (uvsr::ValidateSettingsSnapshotCatalogValue(
                    definition, value, error))
            {
                return value;
            }
        }
        if (definition.typedDomain.kind ==
                uvsr::UiSettingsDomainKind::Enumeration &&
            definition.typedDomain.tokenCount != 0u)
        {
            const std::string value(
                definition.typedDomain.tokens[0]);
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
    for (const auto& definition : UiSettingsCommandCatalog)
    {
        UiSettingsValue declared;
        if (!GetDeclaredUiSettingsDefaultValue(definition, declared) ||
            definition.kind == UiSettingsCommandKind::DynamicSelection)
            continue;
        UiSettingsValue parsed;
        std::string error;
        Require(ParseCanonicalUiSettingsValue(definition,
                FormatUiSettingsDefaultAnchor(definition), parsed, error) && parsed == declared,
            "declared typed default must equal its canonical round trip: " + std::string(definition.name));
    }
    Require(!(UiSettingsValue::Boolean(true) == UiSettingsValue::Boolean(false)) &&
            !(UiSettingsValue::Integer(1) == UiSettingsValue::Integer(2)) &&
            !(UiSettingsValue::Float(1.f) == UiSettingsValue::Float(2.f)) &&
            !(UiSettingsValue::Token("amp") == UiSettingsValue::Token("ogg")) &&
            !(UiSettingsValue::Token("none") == UiSettingsValue::Selector("none")),
        "changed values and different kinds must request a mutation");
    const auto vector = UiSettingsValue::Vector({ 1.f, 2.f, 3.f, 0.f }, 3u);
    Require(vector == UiSettingsValue::Vector({ 1.f, 2.f, 3.f, 9.f }, 3u) &&
            !(vector == UiSettingsValue::Vector({ 1.f, 2.f, 4.f, 0.f }, 3u)) &&
            !(vector == UiSettingsValue::Vector({ 1.f, 2.f, 3.f, 0.f }, 4u)),
        "vector equality must use its active components and shape");

    std::int64_t integer = 0;
    Require(TryParseCommandInteger("-17", integer) && integer == -17,
        "integer parsing must consume the complete token");
    Require(!TryParseCommandInteger("17px", integer),
        "integer parsing must reject suffixes");
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
        SettingId id,
        std::string& value,
        std::string& error)
    {
        const std::string_view name = SettingName(id);
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
        SettingId id,
        std::string_view value,
        std::string_view,
        std::string& error)
    {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        return definition && IsSettingsSnapshotValue(*definition) &&
            ValidateSettingsSnapshotCatalogValue(*definition, value, error);
    };
    const auto write = [&live, &writes](
        SettingId id,
        std::string_view value,
        std::string& error)
    {
        const std::string_view name = SettingName(id);
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

    const auto driveImmediate = [&live](
        SettingId id,
        std::string_view value,
        bool begin,
        bool,
        std::string& selectorError)
    {
        const std::string_view name = SettingName(id);
        if (name == "gpu.adapter" || !begin)
        {
            selectorError = "unexpected selector transition";
            return SettingsSnapshotSelectorTransition::Failed;
        }
        live[std::string(name)] = std::string(value);
        return SettingsSnapshotSelectorTransition::Ready;
    };
    SettingsSnapshotRuntimeAccess access{
        true,
        validate,
        read,
        read,
        write,
        driveImmediate
    };
    SettingsSnapshotController controller;
    controller.Refresh(read);
    const DecodedSettings captured =
        ParseSettingsSnapshot(controller.Canonical());
    Require(
        captured.size() == live.size() &&
            IsSettingsSnapshotCode(controller.Code()),
        "controller capture must include the authoritative snapshot catalog");

    auto step = controller.BeginApplyCanonicalStaged(
        controller.Canonical(), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            step.result.changedValueCount == 0u && writes == 0u,
        "an idempotent snapshot must verify without setters");

    DecodedSettings changedPayload = captured;
    const std::string previousFill =
        changedPayload.at("sky.ambient-fill.enabled");
    changedPayload["sky.ambient-fill.enabled"] =
        previousFill == "on" ? "off" : "on";
    const std::string changedCanonical =
        FormatCanonicalSettingsSnapshot(changedPayload);
    step = controller.BeginApplyCanonicalStaged(changedCanonical, access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            step.result.changedValueCount == 1u && writes == 1u &&
            live.at("sky.ambient-fill.enabled") != previousFill,
        "one mutable value must apply through the staged transaction");

    DecodedSettings missing = changedPayload;
    missing.erase("sky.ambient-fill.enabled");
    const std::size_t writesBeforeReject = writes;
    step = controller.BeginApplyCanonicalStaged(
        FormatCanonicalSettingsSnapshot(missing), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Failed &&
            writes == writesBeforeReject &&
            step.result.error.find("missing") != std::string::npos,
        "missing membership must reject before mutation");

    DecodedSettings unknown = changedPayload;
    unknown.emplace("unknown.fixture.setting", "invalid-fixture-value");
    step = controller.BeginApplyCanonicalStaged(
        FormatCanonicalSettingsSnapshot(unknown), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Failed &&
            writes == writesBeforeReject &&
            step.result.error.find("unknown") != std::string::npos,
        "a retired setting must reject before mutation");

    DecodedSettings selectorPayload = changedPayload;
    selectorPayload["scene.current"] = "target/main.scene.json";
    step = controller.BeginApplyCanonicalStaged(
        FormatCanonicalSettingsSnapshot(selectorPayload), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            step.result.changedValueCount == 1u &&
            live.at("scene.current") == "target/main.scene.json",
        "the controller must drive selectors through its staged path");

    DecodedSettings pendingPayload = selectorPayload;
    pendingPayload["scene.current"] = "pending/main.scene.json";
    bool selectorReady = false;
    access.driveSelector = [&live, &selectorReady](
        SettingId id,
        std::string_view value,
        bool begin,
        bool,
        std::string& selectorError)
    {
        const std::string_view name = SettingName(id);
        if (name != "scene.current")
        {
            selectorError = "unexpected pending selector";
            return SettingsSnapshotSelectorTransition::Failed;
        }
        if (begin || !selectorReady)
            return SettingsSnapshotSelectorTransition::Pending;
        live[std::string(name)] = std::string(value);
        return SettingsSnapshotSelectorTransition::Ready;
    };
    step = controller.BeginApplyCanonicalStaged(
        FormatCanonicalSettingsSnapshot(pendingPayload), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Pending &&
            controller.HasStagedApply(),
        "one asynchronous selector transaction must remain active");
    const SettingsSnapshotTransactionStep rejectedSecond =
        controller.BeginApplyCanonicalStaged(changedCanonical, access);
    const SettingsSnapshotTransactionStep rejectedLoad =
        controller.BeginLoadCodeStaged("not-a-code", access);
    Require(
        rejectedSecond.progress ==
                SettingsSnapshotTransactionProgress::Failed &&
            rejectedLoad.progress ==
                SettingsSnapshotTransactionProgress::Failed &&
            rejectedSecond.result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Configuration &&
            controller.HasStagedApply(),
        "a second transaction must not replace pending selector work");
    selectorReady = true;
    step = controller.ContinueStagedApply(access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            live.at("scene.current") == "pending/main.scene.json" &&
            !controller.HasStagedApply(),
        "the original selector transaction must complete after polling");
    access.driveSelector = driveImmediate;

    DecodedSettings adapterMismatch =
        ParseSettingsSnapshot(controller.Canonical());
    adapterMismatch["gpu.adapter"] = "1";
    const auto stateBeforeAdapterMismatch = live;
    const std::size_t writesBeforeAdapterMismatch = writes;
    step = controller.BeginApplyCanonicalStaged(
        FormatCanonicalSettingsSnapshot(adapterMismatch), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Failed &&
            step.result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Preflight &&
            live == stateBeforeAdapterMismatch &&
            writes == writesBeforeAdapterMismatch &&
            step.result.error.find("-adapter") != std::string::npos,
        "adapter identity must be a zero mutation startup precondition");

    access.sceneReady = false;
    step = controller.BeginApplyCanonicalStaged(
        controller.Canonical(), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Failed &&
            writes == writesBeforeAdapterMismatch &&
            step.result.error.find("fully loaded scene") != std::string::npos,
        "snapshot application must wait for scene readiness");
    access.sceneReady = true;

    std::error_code fileError;
    const std::filesystem::path catalogPath =
        std::filesystem::temp_directory_path() /
        ("uvsr-settings-command-owner-" +
            std::to_string(GetCurrentProcessId()) + ".txt");
    std::filesystem::remove(catalogPath, fileError);
    Require(
        controller.Persist(catalogPath) &&
            controller.Persist(catalogPath),
        "catalog persistence must be idempotent");
    std::ifstream input(catalogPath, std::ios::binary);
    const std::string persisted{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    Require(
        persisted.find(controller.BuildCatalogSection()) !=
                std::string::npos &&
            persisted.find(
                controller.BuildCatalogSection(),
                persisted.find(controller.BuildCatalogSection()) + 1u) ==
                std::string::npos,
        "catalog must contain one exact framed entry");

    const std::filesystem::path loadRoot =
        std::filesystem::temp_directory_path() /
        ("uvsr-settings-load-code-" +
            std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(loadRoot, fileError);
    std::filesystem::create_directories(loadRoot / "UVSR", fileError);
    Require(!fileError, "isolated snapshot catalog root must be writable");
    wchar_t previousLocalAppData[32768]{};
    const DWORD previousLength = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        previousLocalAppData,
        static_cast<DWORD>(std::size(previousLocalAppData)));
    Require(
        SetEnvironmentVariableW(L"LOCALAPPDATA", loadRoot.c_str()) != FALSE,
        "test must set its process local catalog root");

    controller.Refresh(read);
    const std::string currentVersion(SettingsSnapshotVersionText.data(), 4u);
    const std::filesystem::path currentCatalog =
        loadRoot / "UVSR" /
        ("settings-snapshots-v" + currentVersion + ".txt");
    Require(controller.Persist(currentCatalog),
        "current schema fixture must persist");
    step = controller.BeginLoadCodeStaged(controller.Code(), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            step.result.changedValueCount == 0u,
        "current schema load must use the staged controller path: " +
            step.result.error);

    const auto appendFixture = [&](std::string_view version,
                                   const DecodedSettings& fixture)
    {
        const std::string canonical = FormatCanonicalSettingsSnapshot(fixture);
        const std::string code = BuildSettingsSnapshotCode(canonical, version);
        const std::filesystem::path path = loadRoot / "UVSR" /
            ("settings-snapshots-v" + std::string(version) + ".txt");
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output << '[' << code << "]\n" << canonical << "[/" << code << "]\n";
        Require(bool(output), "migration fixture must be written completely");
        return code;
    };
    const auto withRetiredInterface = [](DecodedSettings values, std::string_view skin = "amp")
    {
        values["ui.skin"] = skin;
        values["ui.font-family"] = "codex";
        for (const char* name : { "ui.accent.primary", "ui.accent.font", "ui.accent.primary-background" })
            values[name] = skin == "amp" ? "0.25 0.5 0.75 1" : "<unavailable>";
        values["ui.accent.secondary"] = "0 0.5 1 1";
        values["ui.accent.tertiary"] = "0.5 1 0 1";
        return values;
    };
    const auto buildLegacyFixture = [&](std::string_view version)
    {
        DecodedSettings fixture = withRetiredInterface(ParseSettingsSnapshot(controller.Canonical()));
        for (const auto& definition : UiSettingsCommandCatalog)
            if (definition.section == UiSettingsCommandSection::Pathing)
                fixture.erase(std::string(definition.name));
        if (version <= "0014")
        {
            fixture["visibility.enabled"] = "on";
            fixture["visibility.quality"] = "high";
            fixture["visibility.estimator"] = "solid-angle";
            fixture["visibility.resolution"] = "full";
            fixture["visibility.samples"] = "16";
            fixture["visibility.radius"] = "3";
            fixture["visibility.thickness"] = "0.5";
            fixture["visibility.distribution"] = "2";
            fixture["visibility.specify-noise"] = "off";
            fixture["visibility.noise-pattern"] = "spatiotemporal-blue";
            fixture["visibility.noise-resolution"] = "128x128";
            fixture["visibility.animate-samples"] = "on";
            fixture["visibility.ao.enabled"] = "on";
            fixture["visibility.ao.strength"] = "1";
            fixture["visibility.ao.precision"] = "16-bit";
            fixture["visibility.gi.enabled"] = "on";
            fixture["visibility.gi.intensity"] = "1";
            fixture["visibility.gi.precision"] = "16-bit";
            fixture["debug.visibility.view"] = "final";
        }
        if (version <= "0013")
        {
            fixture.erase("shadows.ray-traced.hard");
            fixture.erase("shadows.ray-traced.samples-per-pixel");
        }
        if (version <= "0012")
            fixture.erase("tonemapper.enabled");
        if (version <= "0011")
            for (const auto& definition : UiSettingsCommandCatalog)
                if (definition.name.substr(0u, 11u) == "tonemapper.")
                    fixture.erase(std::string(definition.name));
        if (version <= "000f")
        {
            fixture["visibility.ao.output-hit-distance"] = "off";
            fixture["visibility.gi.output-hit-distance"] = "off";
            fixture["denoising.ao.method"] = "raw";
            fixture["denoising.ao.radius"] = "4";
            fixture["denoising.ao.quality"] = "balanced";
            fixture["denoising.ao.resolution"] = "half";
            fixture["denoising.ao.history"] = "16";
            fixture["denoising.ao.disocclusion"] = "0.015625";
            fixture["denoising.ao.anti-lag"] = "0.5";
            fixture["denoising.gi.method"] = "raw";
            fixture["denoising.gi.radius"] = "4";
            fixture["denoising.gi.quality"] = "balanced";
            fixture["denoising.gi.resolution"] = "half";
            fixture["denoising.gi.history"] = "16";
            fixture["denoising.gi.disocclusion"] = "0.015625";
            fixture["denoising.gi.anti-lag"] = "0.5";
            fixture["denoising.shadows.method"] = "raw";
            fixture["denoising.shadows.radius"] = "4";
            fixture["denoising.shadows.quality"] = "balanced";
            fixture["denoising.shadows.resolution"] = "half";
            fixture["denoising.shadows.disocclusion"] = "0.015625";
            fixture["denoising.sky.method"] = "raw";
            fixture["denoising.sky.radius"] = "4";
            fixture["denoising.sky.quality"] = "balanced";
            fixture["denoising.sky.resolution"] = "half";
            fixture["denoising.sky.history"] = "16";
            fixture["denoising.sky.disocclusion"] = "0.015625";
            fixture["denoising.sky.anti-lag"] = "0.5";
            fixture["sky.visibility.output-hit-distance"] = "off";
            fixture["light.selected.flashlight.output-hit-distance"] = "<unavailable>";
        }
        if (version <= "000d")
            fixture["gpu.adaptive-sync"] = "nvidia-exclusive";
        if (version >= "000c")
            return fixture;
        fixture["anti-aliasing.taa.enabled"] = "on";
        fixture["anti-aliasing.taa.quality"] = "low";
        fixture["anti-aliasing.taa.jitter-sequence"] = "rotated-grid-4";
        fixture["anti-aliasing.taa.previous-depth"] = "nearest-texel";
        fixture["anti-aliasing.taa.temporal-cost"] = "full-quality";
        fixture["anti-aliasing.taa.history.frames"] = "-1";
        fixture["anti-aliasing.taa.history.strength"] = "-1";
        fixture["anti-aliasing.taa.history.storage"] = "temporal-cost";
        fixture["anti-aliasing.taa.history.weight"] = "temporal-cost";
        fixture["anti-aliasing.taa.motion-trust"] = "temporal-cost";
        fixture["anti-aliasing.taa.rectification-clip"] = "temporal-cost";
        fixture["anti-aliasing.taa.blend-domain"] = "temporal-cost";
        fixture["anti-aliasing.taa.preset-sharpening"] = "auto";
        fixture["anti-aliasing.sharpen.enabled"] = "on";
        fixture["anti-aliasing.sharpen.strength"] = "0.5";
        fixture["anti-aliasing.msaa.enabled"] = "on";
        fixture["anti-aliasing.msaa.samples"] = "2x";
        if (version <= "000a")
            fixture["ui.animations"] = "on";
        if (version <= "0009")
        {
            fixture["ui.accent.main"] = "0 0.5 1";
            fixture["ui.accent.negative"] = "1 0.5 0";
            fixture["ui.accent.positive"] = "0.5 1 0";
        }
        if (version <= "0008")
        {
            fixture["representation.bvh.build-preference"] = "balanced";
            fixture["representation.blas.update-mode"] = "rebuild";
            fixture["representation.tlas.update-mode"] = "refit";
        }
        if (version == "0007")
            fixture["anti-aliasing.msaa.quality"] = "ultra";
        return fixture;
    };
    const auto requireRejected = [&](std::string_view version,
                                     const DecodedSettings& fixture,
                                     std::string_view field)
    {
        const auto before = live;
        const std::size_t writesBefore = writes;
        const auto rejected = controller.BeginLoadCodeStaged(
            appendFixture(version, fixture), access);
        Require(rejected.progress == SettingsSnapshotTransactionProgress::Failed &&
                rejected.result.failureStage ==
                    SettingsSnapshotTransactionFailureStage::Preflight &&
                rejected.result.error.find(field) != std::string::npos &&
                live == before && writes == writesBefore,
            "schema " + std::string(version) + " must reject " +
                std::string(field) + " before mutation: " + rejected.result.error);
    };
    for (const std::string_view version : SupportedLegacySettingsSnapshotVersions)
    {
        if (version >= "0016")
        {
            const auto before = live;
            for (const char* skin : { "amp", "ogg", "cap" })
            {
                auto retained = before;
                for (const char* threshold : { "5000", "50" })
                {
                    retained["pathing.firefly-threshold"] = threshold;
                    const auto fixture = withRetiredInterface(retained, skin);
                    step = controller.BeginLoadCodeStaged(appendFixture(version, fixture), access);
                    Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded && live == retained,
                        "retired interface must preserve retained values: " + step.result.error);
                    for (const auto& [name, value] : fixture)
                    {
                        if (retained.count(name)) continue;
                        auto invalid = fixture;
                        invalid[name] = "invalid";
                        requireRejected(version, invalid, name);
                        invalid.erase(name);
                        requireRejected(version, invalid, name);
                    }
                    auto mismatched = fixture;
                    mismatched["ui.accent.primary"] = std::string_view(skin) == "amp" ? "<unavailable>" : "0 0 0 1";
                    requireRejected(version, mismatched, "ui.accent.primary");
                }
            }
            step = controller.BeginLoadCodeStaged(appendFixture(version, withRetiredInterface(before)), access);
            Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded && live == before,
                "retired interface migration must round-trip");
            continue;
        }
        DecodedSettings valid = buildLegacyFixture(version);
        for (const std::string_view adaptiveSync : { "off", "vendor-agnostic", "nvidia-exclusive" })
        {
            if (version <= "000d")
                valid["gpu.adaptive-sync"] = adaptiveSync;
            if (version <= "000a")
                valid["ui.animations"] = adaptiveSync == "off" ? "off" : "on";
            auto expected = live;
            expected["pathing.maximum-bounces"] = "3";
            expected["pathing.minimum-bounces"] = "1";
            expected["pathing.firefly-filter"] = "off";
            expected["pathing.firefly-threshold"] = "5000";
            step = controller.BeginLoadCodeStaged(appendFixture(version, valid), access);
            Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded && live == expected,
                "schema " + std::string(version) +
                    " must preserve legacy transport and discard its retired fields: " + step.result.error);
        }
        for (const auto& [name, value] : valid)
        {
            if (live.count(name))
                continue;
            DecodedSettings missingRetired = valid;
            missingRetired.erase(name);
            requireRejected(version, missingRetired, name);
            DecodedSettings invalid = valid;
            invalid[name] = name.find("ui.accent.") == 0u ? "0 1.5 0" : "invalid";
            requireRejected(version, invalid, name);
        }
        DecodedSettings retainedChange = valid;
        const auto beforeRetainedChange = live;
        const auto retainedExposure = live.at("sky.exposure");
        retainedChange["sky.exposure"] = retainedExposure == "1" ? "2" : "1";
        auto expectedRetainedChange = live;
        expectedRetainedChange["sky.exposure"] = retainedChange.at("sky.exposure");
        step = controller.BeginLoadCodeStaged(appendFixture(version, retainedChange), access);
        Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
                live == expectedRetainedChange,
            "legacy migration must apply the retained value and discard only retired fields");
        step = controller.BeginLoadCodeStaged(appendFixture(version, valid), access);
        Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
                live == beforeRetainedChange, "legacy retained value must round-trip");
        DecodedSettings unexpectedPathing = valid;
        unexpectedPathing["pathing.maximum-bounces"] = "3";
        requireRejected(version, unexpectedPathing, "pathing.maximum-bounces");
        DecodedSettings unexpectedSkin = valid;
        unexpectedSkin["ui.skin"] = "cap";
        for (const char* name : { "ui.accent.primary", "ui.accent.font", "ui.accent.primary-background" })
            unexpectedSkin[name] = "<unavailable>";
        requireRejected(version, unexpectedSkin, "ui.skin");
        DecodedSettings unknownLegacy = valid;
        unknownLegacy["ui.unknown"] = "on";
        requireRejected(version, unknownLegacy, "ui.unknown");
    }
    for (const auto& [quality, samples] :
         std::array<std::pair<std::string_view, std::string_view>, 4>{{
            { "low", "16x" }, { "medium", "8x" },
            { "high", "4x" }, { "ultra", "2x" } }})
    {
        DecodedSettings fixture = buildLegacyFixture("0007");
        fixture["anti-aliasing.msaa.quality"] = quality;
        fixture["anti-aliasing.msaa.samples"] = samples;
        step = controller.BeginLoadCodeStaged(appendFixture("0007", fixture), access);
        Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
                live.count("anti-aliasing.msaa.samples") == 0u,
            "schema 0007 must discard both retired MSAA fields: " + step.result.error);
    }
    for (const std::string_view invalid : { "true", "false", "ON", "0", "<unavailable>" })
    {
        DecodedSettings fixture = buildLegacyFixture("000a");
        fixture["ui.animations"] = invalid;
        requireRejected("000a", fixture, "ui.animations");
    }
    DecodedSettings retiredAccent = buildLegacyFixture("000a");
    retiredAccent["ui.accent.main"] = "0 0.5 1";
    requireRejected("000a", retiredAccent, "ui.accent.main");
    const DecodedSettings current = ParseSettingsSnapshot(controller.Canonical());
    for (const auto& [name, value] : buildLegacyFixture("000b"))
    {
        if (current.count(name)) continue;
        auto obsolete = current;
        obsolete[name] = value;
        requireRejected(currentVersion, obsolete, name);
    }
    DecodedSettings retiredAnimation = current;
    retiredAnimation["ui.animations"] = "off";
    requireRejected(currentVersion, retiredAnimation, "ui.animations");
    DecodedSettings unknownCurrent = current;
    unknownCurrent["ui.unknown"] = "on";
    requireRejected(currentVersion, unknownCurrent, "ui.unknown");
    requireRejected("ffff", current, "registered");

    Require(
        SetEnvironmentVariableW(
            L"LOCALAPPDATA",
            previousLength > 0u ? previousLocalAppData : nullptr) != FALSE,
        "test must restore LOCALAPPDATA");
    std::filesystem::remove_all(loadRoot, fileError);

    {
        std::ofstream conflict(
            catalogPath,
            std::ios::binary | std::ios::trunc);
        conflict << '[' << controller.Code() << "]\ncorrupt\n[/"
                 << controller.Code() << "]\n";
    }
    Require(
        !controller.Persist(catalogPath),
        "an existing code with a conflicting payload must fail closed");
    std::filesystem::remove(catalogPath, fileError);

    std::cout << "UVSR settings command owner validation passed\n";
    return EXIT_SUCCESS;
}
