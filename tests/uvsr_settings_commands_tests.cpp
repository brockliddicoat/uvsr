#include "uvsr_settings_commands.h"

#include "settings_snapshot.h"
#include "settings_snapshot_decoder.h"
#include "settings_snapshot_persistence_win32.h"

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

    template<class Option, size_t Count>
    uvsr::SettingsSnapshotOptionSource<Option> OptionSource(const Option (&rows)[Count]) noexcept
    {
        return {rows, Count, [](const void* context, size_t index, Option& output, uvsr::SettingsSnapshotError&) noexcept {
            output = static_cast<const Option*>(context)[index];
            return true;
        }};
    }

    uvsr::DecodedSettings ParseSnapshot(std::string_view text)
    {
        uvsr::DecodedSettings result;
        uvsr::SettingsSnapshotError error;
        if (!uvsr::ParseSettingsSnapshot(text, result, error)) Fail(error.Message());
        return result;
    }
    uvsr::DecodedSettings CloneSnapshot(const uvsr::DecodedSettings& source)
    {
        uvsr::DecodedSettings result;
        uvsr::SettingsSnapshotError error;
        if (!source.CloneTo(result, error)) Fail(error.Message());
        return result;
    }
    uvsr::DecodedSettings SnapshotFromValues(const std::map<std::string, std::string, std::less<>>& values)
    {
        uvsr::DecodedSettings result;
        uvsr::SettingsSnapshotError error;
        for (const auto& [name, value] : values)
            if (!result.Insert(name, value, error)) Fail(error.Message());
        return result;
    }
    void SetSnapshot(uvsr::DecodedSettings& settings, std::string_view name, std::string_view value)
    {
        uvsr::SettingsSnapshotError error;
        if (!settings.Set(name, value, error)) Fail(error.Message());
    }
    std::string SnapshotValue(const uvsr::DecodedSettings& settings, std::string_view name)
    {
        const auto* found = settings.Find(name);
        Require(found != nullptr, "snapshot fixture field is missing");
        return std::string(found->value);
    }
    std::string CanonicalSnapshot(const uvsr::DecodedSettings& settings)
    {
        uvsr::SettingsSnapshotError error;
        uvsr::json::EncodedText output;
        if (!uvsr::FormatCanonicalSettingsSnapshot(settings, output, error)) Fail(error.Message());
        return {output.Data(), output.Size()};
    }
    std::string CatalogSection(const uvsr::SettingsSnapshotController& controller)
    {
        uvsr::SettingsSnapshotError error;
        uvsr::json::EncodedText output;
        if (!controller.BuildCatalogSection(output, error)) Fail(error.Message());
        return {output.Data(), output.Size()};
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

        uvsr::SettingsSnapshotError error;
        if (definition.typedDefault.HasValue())
        {
            const std::string value(uvsr::FormatUiSettingsDefaultAnchor(definition).View());
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
            std::string(definition.name) + ": " + std::string(error.MessageView()));
    }
}

int main()
{
    using namespace uvsr;
    for (const auto& definition : UiSettingsCommandCatalog)
    {
        UiSettingsValue declared;
        SettingsSnapshotError error;
        if (!GetDeclaredUiSettingsDefaultValue(definition, declared, error) ||
            definition.kind == UiSettingsCommandKind::DynamicSelection)
            continue;
        UiSettingsValue parsed;
        Require(ParseCanonicalUiSettingsValue(definition,
                FormatUiSettingsDefaultAnchor(definition).View(), parsed, error) && parsed == declared,
            "declared typed default must equal its canonical round trip: " + std::string(definition.name));
    }
    UiSettingsValue amp, ogg, none, selectorNone;
    SettingsSnapshotError valueError;
    Require(amp.SetToken("amp", valueError) && ogg.SetToken("ogg", valueError) &&
        none.SetToken("none", valueError) && selectorNone.SetSelector("none", valueError),
        "prepare distinct text kinds");
    Require(!(UiSettingsValue::Boolean(true) == UiSettingsValue::Boolean(false)) &&
            !(UiSettingsValue::Integer(1) == UiSettingsValue::Integer(2)) &&
            !(UiSettingsValue::Float(1.f) == UiSettingsValue::Float(2.f)) &&
            !(amp == ogg) && !(none == selectorNone),
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
    const SettingsSnapshotAdapterOption adapters[] = {
        { 0, "Duplicate GPU" }, { 1, "Duplicate GPU" }
    };
    std::int64_t adapterIndex = -1;
    UiSettingsValue canonicalToken;
    SettingsSnapshotError selectorError;
    Require(!ResolveSettingsSnapshotAdapterToken(
            "Duplicate GPU",
            OptionSource(adapters),
            adapterIndex,
            canonicalToken,
            selectorError) &&
            ResolveSettingsSnapshotAdapterToken(
                "1",
                OptionSource(adapters),
                adapterIndex,
                canonicalToken,
                selectorError) &&
            adapterIndex == 1 && canonicalToken.Text() == "1",
        "duplicate adapter display names must fail without mutation while "
        "the canonical numeric token remains exact");

    const SettingsSnapshotSceneOption scenes[] = {
        { "a/main.scene.json", "Duplicate Scene" },
        { "b/main.scene.json", "Duplicate Scene" },
        { "collision.scene.json", "Primary" },
        { "other.scene.json", "collision.scene.json" },
        { "numeric.scene.json", "7" }
    };
    size_t sceneOrdinal = SIZE_MAX;
    Require(!ResolveSettingsSnapshotSceneToken(
            "Duplicate Scene",
            OptionSource(scenes),
            sceneOrdinal,
            canonicalToken,
            selectorError) &&
            ResolveSettingsSnapshotSceneToken(
                "b/main.scene.json",
                OptionSource(scenes),
                sceneOrdinal,
                canonicalToken,
                selectorError) &&
            scenes[sceneOrdinal].fileName == "b/main.scene.json" &&
            canonicalToken.Text() == scenes[sceneOrdinal].fileName,
        "duplicate scene display names must fail while exact filenames "
        "round-trip canonically");
    Require(ResolveSettingsSnapshotSceneToken(
            "collision.scene.json",
            OptionSource(scenes),
            sceneOrdinal,
            canonicalToken,
            selectorError) &&
            scenes[sceneOrdinal].fileName == "collision.scene.json" &&
            ResolveSettingsSnapshotSceneToken(
                "7",
                OptionSource(scenes),
                sceneOrdinal,
                canonicalToken,
                selectorError) &&
            scenes[sceneOrdinal].fileName == "numeric.scene.json",
        "an exact canonical scene filename must outrank a display-name "
        "collision while numeric-looking display names remain friendly input");
    const SettingsSnapshotSceneOption runtimeScenes[] = {
        {
            "bistro/main.scene.json",
            "Bistro"
        }
    };
    const std::string_view runtimePaths[] = {"C:/package/media/bistro/main.scene.json"};
    Require(ResolveSettingsSnapshotSceneToken(
            "bistro/main.scene.json",
            OptionSource(runtimeScenes),
            sceneOrdinal,
            canonicalToken,
            selectorError) &&
            sceneOrdinal == 0 && runtimePaths[sceneOrdinal] == "C:/package/media/bistro/main.scene.json" &&
            canonicalToken.Text() == "bistro/main.scene.json",
        "scene snapshots must preserve a relative canonical token while "
        "selecting the exact runtime catalog path");

    const SettingsSnapshotLightOption lights[] = {
        { 0u, "duplicate-light" }, { 1u, "duplicate-light" }
    };
    std::size_t lightIndex = 0u;
    Require(!ResolveSettingsSnapshotLightToken(
            "duplicate-light",
            OptionSource(lights),
            lightIndex,
            canonicalToken,
            selectorError) &&
            ResolveSettingsSnapshotLightToken(
                "1:duplicate-light",
                OptionSource(lights),
                lightIndex,
                canonicalToken,
                selectorError) &&
            lightIndex == 1u && canonicalToken.Text() == "1:duplicate-light",
        "duplicate light identities must require the stable index:identity "
        "snapshot token");

    const SettingsSnapshotMaterialOption materials[] = {
        { 10u, "duplicate-material" },
        { 20u, "duplicate-material" }
    };
    bool noMaterial = false;
    std::uint32_t materialId = 0u;
    Require(!ResolveSettingsSnapshotMaterialToken(
            "duplicate-material",
            OptionSource(materials),
            noMaterial,
            materialId,
            canonicalToken,
            selectorError) &&
            ResolveSettingsSnapshotMaterialToken(
                "20",
                OptionSource(materials),
                noMaterial,
                materialId,
                canonicalToken,
                selectorError) &&
            !noMaterial && materialId == 20u && canonicalToken.Text() == "20" &&
            ResolveSettingsSnapshotMaterialToken(
                "none",
                OptionSource(materials),
                noMaterial,
                materialId,
                canonicalToken,
                selectorError) &&
            noMaterial && canonicalToken.Text() == "none",
        "material snapshots must use an exact runtime id or explicit none");

    UiSettingsValue adapterGet, adapterRoundTrip;
    Require(FormatSettingsSnapshotAdapterToken(0, adapterGet, selectorError), "prepare adapter GET");
    adapterIndex = 1;
    Require(ResolveSettingsSnapshotAdapterToken(
            adapterGet.Text(),
            OptionSource(adapters),
            adapterIndex,
            canonicalToken,
            selectorError) &&
            FormatSettingsSnapshotAdapterToken(adapterIndex, adapterRoundTrip, selectorError) && adapterRoundTrip == adapterGet,
        "adapter GET -> select away -> SET token -> GET must be exact");
    UiSettingsValue sceneGet;
    Require(FormatSettingsSnapshotSceneToken("a/main.scene.json", sceneGet, selectorError), "prepare scene GET");
    sceneOrdinal = 1;
    Require(ResolveSettingsSnapshotSceneToken(
            sceneGet.Text(),
            OptionSource(scenes),
            sceneOrdinal,
            canonicalToken,
            selectorError) && canonicalToken == sceneGet,
        "scene GET -> select away -> SET token -> GET must be exact");
    UiSettingsValue lightGet;
    Require(FormatSettingsSnapshotLightToken(0u, "duplicate-light", lightGet, selectorError), "prepare light GET");
    lightIndex = 1u;
    Require(ResolveSettingsSnapshotLightToken(
            lightGet.Text(),
            OptionSource(lights),
            lightIndex,
            canonicalToken,
            selectorError) && canonicalToken == lightGet,
        "light GET -> select away -> SET token -> GET must be exact");
    UiSettingsValue materialGet;
    Require(FormatSettingsSnapshotMaterialToken(false, 10u, materialGet, selectorError), "prepare material GET");
    materialId = 20u;
    Require(ResolveSettingsSnapshotMaterialToken(
            materialGet.Text(),
            OptionSource(materials),
            noMaterial,
            materialId,
            canonicalToken,
            selectorError) && canonicalToken == materialGet,
        "material GET -> select away -> SET token -> GET must be exact");

    const std::int64_t maximumAdapter =
        static_cast<std::int64_t>((std::numeric_limits<int>::max)());
    const SettingsSnapshotAdapterOption boundaryAdapters[] = {
        { maximumAdapter, "Maximum Adapter" }
    };
    Require(ResolveSettingsSnapshotAdapterToken(
            std::to_string(maximumAdapter),
            OptionSource(boundaryAdapters),
            adapterIndex,
            canonicalToken,
            selectorError) &&
            adapterIndex == maximumAdapter &&
            !ResolveSettingsSnapshotAdapterToken(
                std::to_string(maximumAdapter + 1),
                OptionSource(boundaryAdapters),
                adapterIndex,
                canonicalToken,
                selectorError),
        "adapter tokens must accept INT_MAX and reject the next value");

    const std::size_t maximumLight =
        (std::numeric_limits<std::size_t>::max)();
    const SettingsSnapshotLightOption boundaryLights[] = {
        { maximumLight, "maximum-light" }
    };
    const std::string maximumLightToken =
        std::to_string(maximumLight) + ":maximum-light";
    Require(ResolveSettingsSnapshotLightToken(
            maximumLightToken,
            OptionSource(boundaryLights),
            lightIndex,
            canonicalToken,
            selectorError) &&
            lightIndex == maximumLight &&
            !ResolveSettingsSnapshotLightToken(
                "18446744073709551616:maximum-light",
                OptionSource(boundaryLights),
                lightIndex,
                canonicalToken,
                selectorError),
        "light tokens must accept SIZE_MAX and reject unsigned overflow");

    const std::uint32_t maximumMaterial =
        (std::numeric_limits<std::uint32_t>::max)();
    const SettingsSnapshotMaterialOption boundaryMaterials[] = {
        { maximumMaterial, "Maximum Material" }
    };
    Require(ResolveSettingsSnapshotMaterialToken(
            std::to_string(maximumMaterial),
            OptionSource(boundaryMaterials),
            noMaterial,
            materialId,
            canonicalToken,
            selectorError) &&
            materialId == maximumMaterial &&
            !ResolveSettingsSnapshotMaterialToken(
                "4294967296",
                OptionSource(boundaryMaterials),
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
    struct RuntimeContext
    {
        decltype(live)& liveValues;
        std::size_t& writeCount;
        bool selectorReady = false;
        SettingsSnapshotErrorCode readFailure = SettingsSnapshotErrorCode::None;
        SettingsSnapshotValueReader read = nullptr;
        SettingsSnapshotValueWriter write = nullptr;
    } runtime{live, writes};
    const auto read = [](void* owner,
        SettingId id,
        SettingsSnapshotText& value,
        SettingsSnapshotError& error) noexcept
    {
        auto& live = static_cast<RuntimeContext*>(owner)->liveValues;
        const std::string_view name = SettingName(id);
        const auto found = live.find(name);
        if (found == live.end())
        {
            error.code = SettingsSnapshotErrorCode::InvalidInput;
            error.message = "missing fake live value";
            return false;
        }
        return value.Assign(found->second, error);
    };
    const auto validate = [](void*,
        SettingId id,
        std::string_view value,
        std::string_view,
        SettingsSnapshotError& error) noexcept
    {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        const bool valid = definition && IsSettingsSnapshotValue(*definition) &&
            ValidateSettingsSnapshotCatalogValue(*definition, value, error);
        return valid;
    };
    const auto write = [](void* owner,
        SettingId id,
        std::string_view value,
        SettingsSnapshotError& error) noexcept
    {
        auto& runtime = *static_cast<RuntimeContext*>(owner);
        auto& live = runtime.liveValues;
        auto& writes = runtime.writeCount;
        const std::string_view name = SettingName(id);
        const auto found = live.find(name);
        if (found == live.end())
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "missing fake live value", {}};
            return false;
        }
        ++writes;
        found->second = value;
        return true;
    };

    const auto driveImmediate = [](void* owner,
        SettingId id,
        std::string_view value,
        bool begin,
        bool,
        SettingsSnapshotError& selectorError) noexcept
    {
        auto& live = static_cast<RuntimeContext*>(owner)->liveValues;
        const std::string_view name = SettingName(id);
        if (name == "gpu.adapter" || !begin)
        {
            selectorError = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "unexpected selector transition", {}};
            return SettingsSnapshotSelectorTransition::Failed;
        }
        live[std::string(name)] = std::string(value);
        return SettingsSnapshotSelectorTransition::Ready;
    };
    SettingsSnapshotRuntimeAccess access{
        true, &runtime,
        validate,
        read,
        read,
        write,
        driveImmediate
    };
    runtime.read = read;
    runtime.write = write;
    const auto WithReader = [&runtime](SettingsSnapshotValueReader reader) {
        SettingsSnapshotRuntimeAccess result;
        result.context = &runtime;
        result.readValue = reader;
        return result;
    };
    SettingsSnapshotController controller{SettingsSnapshotCatalogLocation::Installed};
    {
        SettingsSnapshotError refreshError;
        Require(controller.Refresh(WithReader(read), refreshError), "controller refresh failed");
    }
    const DecodedSettings captured =
        ParseSnapshot(controller.Canonical());
    Require(
        captured.Count() == live.size() &&
            IsSettingsSnapshotCode(controller.Code()),
        "controller capture must include the authoritative snapshot catalog");

    auto step = controller.BeginApplyCanonicalStaged(
        controller.Canonical(), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            step.result.changedValueCount == 0u && writes == 0u,
        "an idempotent snapshot must verify without setters");
    const auto inactive = controller.ContinueStagedApply();
    Require(inactive.progress == SettingsSnapshotTransactionProgress::Failed &&
        inactive.result.failureStage == SettingsSnapshotTransactionFailureStage::Configuration && writes == 0,
        "inactive continuation must reject without using a completed callback context");

    DecodedSettings changedPayload = CloneSnapshot(captured);
    const std::string previousFill =
        SnapshotValue(changedPayload, "sky.ambient-fill.enabled");
    SetSnapshot(changedPayload, "sky.ambient-fill.enabled", previousFill == "on" ? "off" : "on");
    const std::string changedCanonical =
        CanonicalSnapshot(changedPayload);
    step = controller.BeginApplyCanonicalStaged(changedCanonical, access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            step.result.changedValueCount == 1u && writes == 1u &&
            live.at("sky.ambient-fill.enabled") != previousFill,
        "one mutable value must apply through the staged transaction");

    DecodedSettings missing = CloneSnapshot(changedPayload);
    (void)missing.Erase("sky.ambient-fill.enabled");
    const std::size_t writesBeforeReject = writes;
    step = controller.BeginApplyCanonicalStaged(
        CanonicalSnapshot(missing), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Failed &&
            writes == writesBeforeReject &&
            step.result.error.MessageView().find("missing") != std::string::npos,
        "missing membership must reject before mutation");

    DecodedSettings unknown = CloneSnapshot(changedPayload);
    SetSnapshot(unknown, "unknown.fixture.setting", "invalid-fixture-value");
    step = controller.BeginApplyCanonicalStaged(
        CanonicalSnapshot(unknown), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Failed &&
            writes == writesBeforeReject &&
            step.result.error.MessageView().find("unknown") != std::string::npos,
        "a retired setting must reject before mutation");

    DecodedSettings selectorPayload = CloneSnapshot(changedPayload);
    SetSnapshot(selectorPayload, "scene.current", "target/main.scene.json");
    step = controller.BeginApplyCanonicalStaged(
        CanonicalSnapshot(selectorPayload), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            step.result.changedValueCount == 1u &&
            live.at("scene.current") == "target/main.scene.json",
        "the controller must drive selectors through its staged path");

    DecodedSettings pendingPayload = CloneSnapshot(selectorPayload);
    SetSnapshot(pendingPayload, "scene.current", "pending/main.scene.json");
    runtime.selectorReady = false;
    access.driveSelector = [](void* owner,
        SettingId id,
        std::string_view value,
        bool begin,
        bool,
        SettingsSnapshotError& selectorError) noexcept
    {
        auto& runtime = *static_cast<RuntimeContext*>(owner);
        auto& live = runtime.liveValues;
        const std::string_view name = SettingName(id);
        if (name != "scene.current")
        {
            selectorError = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "unexpected pending selector", {}};
            return SettingsSnapshotSelectorTransition::Failed;
        }
        if (begin || !runtime.selectorReady)
            return SettingsSnapshotSelectorTransition::Pending;
        live[std::string(name)] = std::string(value);
        return SettingsSnapshotSelectorTransition::Ready;
    };
    step = controller.BeginApplyCanonicalStaged(
        CanonicalSnapshot(pendingPayload), access);
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
    runtime.selectorReady = true;
    step = controller.ContinueStagedApply();
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            live.at("scene.current") == "pending/main.scene.json" &&
            !controller.HasStagedApply(),
        "the original selector transaction must complete after polling");
    access.driveSelector = driveImmediate;

    DecodedSettings adapterMismatch =
        ParseSnapshot(controller.Canonical());
    SetSnapshot(adapterMismatch, "gpu.adapter", "1");
    const auto stateBeforeAdapterMismatch = live;
    const std::size_t writesBeforeAdapterMismatch = writes;
    step = controller.BeginApplyCanonicalStaged(
        CanonicalSnapshot(adapterMismatch), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Failed &&
            step.result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Preflight &&
            live == stateBeforeAdapterMismatch &&
            writes == writesBeforeAdapterMismatch &&
            step.result.error.MessageView().find("-adapter") != std::string::npos,
        "adapter identity must be a zero mutation startup precondition");

    access.sceneReady = false;
    step = controller.BeginApplyCanonicalStaged(
        controller.Canonical(), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Failed &&
            writes == writesBeforeAdapterMismatch &&
            step.result.error.MessageView().find("fully loaded scene") != std::string::npos,
        "snapshot application must wait for scene readiness");
    access.sceneReady = true;

    std::error_code fileError;
    const std::filesystem::path catalogPath =
        std::filesystem::temp_directory_path() /
        ("uvsr-settings-command-owner-" +
            std::to_string(GetCurrentProcessId()) + ".txt");
    std::filesystem::remove(catalogPath, fileError);
    SettingsSnapshotError persistError;
    Require(
        controller.Persist(catalogPath.c_str(), persistError) &&
            controller.Persist(catalogPath.c_str(), persistError),
        "catalog persistence must be idempotent");
    std::ifstream input(catalogPath, std::ios::binary);
    const std::string persisted{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    Require(
        persisted.find(CatalogSection(controller)) !=
                std::string::npos &&
            persisted.find(
                CatalogSection(controller),
                persisted.find(CatalogSection(controller)) + 1u) ==
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

    {
        SettingsSnapshotError refreshError;
        Require(controller.Refresh(WithReader(read), refreshError), "controller refresh failed");
    }
    const std::string currentVersion(SettingsSnapshotVersionText.data(), 4u);
    const std::filesystem::path currentCatalog =
        loadRoot / "UVSR" /
        ("settings-snapshots-v" + currentVersion + ".txt");
    Require(controller.Persist(currentCatalog.c_str(), persistError),
        "current schema fixture must persist");
    step = controller.BeginLoadCodeStaged(controller.Code(), access);
    Require(
        step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
            step.result.changedValueCount == 0u,
        "current schema load must use the staged controller path: " +
            std::string(step.result.error.MessageView()));

    {
        const std::string installedCode(controller.Code());
        const std::string installedCanonical(controller.Canonical());
        const char* installedData = controller.Canonical().data();
        const auto writesBeforePolicy = writes;
        SetSettingsSnapshotWriteRootForTests(nullptr);
        Require(!controller.PersistToLocalCatalog(persistError) &&
            persistError.code == SettingsSnapshotErrorCode::Path &&
            persistError.nativeCode == std::uint32_t(E_FAIL) && persistError.cleanupCode == 0 &&
            persistError.MessageView() == "cannot locate snapshot catalog local app data directory" &&
            controller.Code() == installedCode && controller.Canonical() == installedCanonical &&
            controller.Canonical().data() == installedData,
            "installed lookup failure must reach the caller without changing the published snapshot");
        SetSettingsSnapshotWriteRootForTests(loadRoot.c_str());
        Require(controller.PersistToLocalCatalog(persistError) &&
            persistError.code == SettingsSnapshotErrorCode::None && persistError.nativeCode == 0 &&
            persistError.cleanupCode == 0 && persistError.MessageView().empty(),
            "installed persistence must use the supplied known-folder root and clear the previous error");

        SettingsSnapshotController executableController{SettingsSnapshotCatalogLocation::ExecutableState};
        const auto changedReader = [](void* owner, SettingId id, SettingsSnapshotText& value, SettingsSnapshotError& error) noexcept {
            auto& context = *static_cast<RuntimeContext*>(owner);
            if (!context.read(owner, id, value, error)) return false;
            if (id == SettingId::SkyAmbientFillEnabled) return value.Assign(value.View() == "on" ? "off" : "on", error);
            return true;
        };
        SettingsSnapshotError refreshError;
        Require(executableController.Refresh(WithReader(changedReader), refreshError) &&
            executableController.Code() != installedCode,
            "executable catalog fixture must have a distinct code without mutating live settings");
        const std::string executableCode(executableController.Code());
        const std::string executableCanonical(executableController.Canonical());
        const char* executableData = executableController.Canonical().data();
        SetSettingsSnapshotWriteRootForTests(nullptr);
        Require(executableController.PersistToLocalCatalog(persistError) &&
            persistError.code == SettingsSnapshotErrorCode::None,
            "executable catalog persistence must not query the installed known folder");

        auto notReady = access;
        notReady.sceneReady = false;
        const auto executableLoad = executableController.BeginLoadCodeStaged(executableCode, notReady);
        Require(executableLoad.progress == SettingsSnapshotTransactionProgress::Failed &&
            executableLoad.result.failureStage == SettingsSnapshotTransactionFailureStage::Preflight &&
            executableLoad.result.error.code == SettingsSnapshotErrorCode::InvalidInput &&
            executableLoad.result.error.MessageView() == "settings.load requires a fully loaded scene",
            "executable policy must find its catalog before rejecting scene readiness");
        const auto installedLoad = controller.BeginLoadCodeStaged(executableCode, notReady);
        Require(installedLoad.progress == SettingsSnapshotTransactionProgress::Failed &&
            installedLoad.result.failureStage == SettingsSnapshotTransactionFailureStage::Preflight &&
            installedLoad.result.error.code == SettingsSnapshotErrorCode::Catalog &&
            installedLoad.result.error.MessageView() ==
                "snapshot decode failed: settings snapshot is absent from the catalogs",
            "installed policy must not discover an executable-only catalog entry");
        ClearSettingsSnapshotWriteRootForTests();
        Require(writes == writesBeforePolicy &&
            controller.Code() == installedCode && controller.Canonical() == installedCanonical &&
            controller.Canonical().data() == installedData && !controller.HasStagedApply() &&
            executableController.Code() == executableCode && executableController.Canonical() == executableCanonical &&
            executableController.Canonical().data() == executableData && !executableController.HasStagedApply(),
            "catalog policy checks must preserve live settings and both published snapshots");
    }

    const auto appendFixture = [&](std::string_view version,
                                   const DecodedSettings& fixture)
    {
        const std::string canonical = CanonicalSnapshot(fixture);
        const std::string code = std::string(BuildSettingsSnapshotCode(canonical, version).View());
        const std::filesystem::path path = loadRoot / "UVSR" /
            ("settings-snapshots-v" + std::string(version) + ".txt");
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output << '[' << code << "]\n" << canonical << "[/" << code << "]\n";
        Require(bool(output), "migration fixture must be written completely");
        return code;
    };
    const auto withRetiredInterface = [](DecodedSettings values, std::string_view skin = "amp")
    {
        SetSnapshot(values, "ui.skin", skin);
        SetSnapshot(values, "ui.font-family", "codex");
        for (const char* name : { "ui.accent.primary", "ui.accent.font", "ui.accent.primary-background" })
            SetSnapshot(values, name, skin == "amp" ? "0.25 0.5 0.75 1" : "<unavailable>");
        SetSnapshot(values, "ui.accent.secondary", "0 0.5 1 1");
        SetSnapshot(values, "ui.accent.tertiary", "0.5 1 0 1");
        return values;
    };
    const auto buildLegacyFixture = [&](std::string_view version)
    {
        DecodedSettings fixture = withRetiredInterface(ParseSnapshot(controller.Canonical()));
        for (const auto& definition : UiSettingsCommandCatalog)
            if (definition.section == UiSettingsCommandSection::Pathing)
                (void)fixture.Erase(std::string(definition.name));
        if (version <= "0014")
        {
            SetSnapshot(fixture, "visibility.enabled", "on");
            SetSnapshot(fixture, "visibility.quality", "high");
            SetSnapshot(fixture, "visibility.estimator", "solid-angle");
            SetSnapshot(fixture, "visibility.resolution", "full");
            SetSnapshot(fixture, "visibility.samples", "16");
            SetSnapshot(fixture, "visibility.radius", "3");
            SetSnapshot(fixture, "visibility.thickness", "0.5");
            SetSnapshot(fixture, "visibility.distribution", "2");
            SetSnapshot(fixture, "visibility.specify-noise", "off");
            SetSnapshot(fixture, "visibility.noise-pattern", "spatiotemporal-blue");
            SetSnapshot(fixture, "visibility.noise-resolution", "128x128");
            SetSnapshot(fixture, "visibility.animate-samples", "on");
            SetSnapshot(fixture, "visibility.ao.enabled", "on");
            SetSnapshot(fixture, "visibility.ao.strength", "1");
            SetSnapshot(fixture, "visibility.ao.precision", "16-bit");
            SetSnapshot(fixture, "visibility.gi.enabled", "on");
            SetSnapshot(fixture, "visibility.gi.intensity", "1");
            SetSnapshot(fixture, "visibility.gi.precision", "16-bit");
            SetSnapshot(fixture, "debug.visibility.view", "final");
        }
        if (version <= "0013")
        {
            (void)fixture.Erase("shadows.ray-traced.hard");
            (void)fixture.Erase("shadows.ray-traced.samples-per-pixel");
        }
        if (version <= "0012")
            (void)fixture.Erase("tonemapper.enabled");
        if (version <= "0011")
            for (const auto& definition : UiSettingsCommandCatalog)
                if (definition.name.substr(0u, 11u) == "tonemapper.")
                    (void)fixture.Erase(std::string(definition.name));
        if (version <= "000f")
        {
            SetSnapshot(fixture, "visibility.ao.output-hit-distance", "off");
            SetSnapshot(fixture, "visibility.gi.output-hit-distance", "off");
            SetSnapshot(fixture, "denoising.ao.method", "raw");
            SetSnapshot(fixture, "denoising.ao.radius", "4");
            SetSnapshot(fixture, "denoising.ao.quality", "balanced");
            SetSnapshot(fixture, "denoising.ao.resolution", "half");
            SetSnapshot(fixture, "denoising.ao.history", "16");
            SetSnapshot(fixture, "denoising.ao.disocclusion", "0.015625");
            SetSnapshot(fixture, "denoising.ao.anti-lag", "0.5");
            SetSnapshot(fixture, "denoising.gi.method", "raw");
            SetSnapshot(fixture, "denoising.gi.radius", "4");
            SetSnapshot(fixture, "denoising.gi.quality", "balanced");
            SetSnapshot(fixture, "denoising.gi.resolution", "half");
            SetSnapshot(fixture, "denoising.gi.history", "16");
            SetSnapshot(fixture, "denoising.gi.disocclusion", "0.015625");
            SetSnapshot(fixture, "denoising.gi.anti-lag", "0.5");
            SetSnapshot(fixture, "denoising.shadows.method", "raw");
            SetSnapshot(fixture, "denoising.shadows.radius", "4");
            SetSnapshot(fixture, "denoising.shadows.quality", "balanced");
            SetSnapshot(fixture, "denoising.shadows.resolution", "half");
            SetSnapshot(fixture, "denoising.shadows.disocclusion", "0.015625");
            SetSnapshot(fixture, "denoising.sky.method", "raw");
            SetSnapshot(fixture, "denoising.sky.radius", "4");
            SetSnapshot(fixture, "denoising.sky.quality", "balanced");
            SetSnapshot(fixture, "denoising.sky.resolution", "half");
            SetSnapshot(fixture, "denoising.sky.history", "16");
            SetSnapshot(fixture, "denoising.sky.disocclusion", "0.015625");
            SetSnapshot(fixture, "denoising.sky.anti-lag", "0.5");
            SetSnapshot(fixture, "sky.visibility.output-hit-distance", "off");
            SetSnapshot(fixture, "light.selected.flashlight.output-hit-distance", "<unavailable>");
        }
        if (version <= "000d")
            SetSnapshot(fixture, "gpu.adaptive-sync", "nvidia-exclusive");
        if (version >= "000c")
            return fixture;
        SetSnapshot(fixture, "anti-aliasing.taa.enabled", "on");
        SetSnapshot(fixture, "anti-aliasing.taa.quality", "low");
        SetSnapshot(fixture, "anti-aliasing.taa.jitter-sequence", "rotated-grid-4");
        SetSnapshot(fixture, "anti-aliasing.taa.previous-depth", "nearest-texel");
        SetSnapshot(fixture, "anti-aliasing.taa.temporal-cost", "full-quality");
        SetSnapshot(fixture, "anti-aliasing.taa.history.frames", "-1");
        SetSnapshot(fixture, "anti-aliasing.taa.history.strength", "-1");
        SetSnapshot(fixture, "anti-aliasing.taa.history.storage", "temporal-cost");
        SetSnapshot(fixture, "anti-aliasing.taa.history.weight", "temporal-cost");
        SetSnapshot(fixture, "anti-aliasing.taa.motion-trust", "temporal-cost");
        SetSnapshot(fixture, "anti-aliasing.taa.rectification-clip", "temporal-cost");
        SetSnapshot(fixture, "anti-aliasing.taa.blend-domain", "temporal-cost");
        SetSnapshot(fixture, "anti-aliasing.taa.preset-sharpening", "auto");
        SetSnapshot(fixture, "anti-aliasing.sharpen.enabled", "on");
        SetSnapshot(fixture, "anti-aliasing.sharpen.strength", "0.5");
        SetSnapshot(fixture, "anti-aliasing.msaa.enabled", "on");
        SetSnapshot(fixture, "anti-aliasing.msaa.samples", "2x");
        if (version <= "000a")
            SetSnapshot(fixture, "ui.animations", "on");
        if (version <= "0009")
        {
            SetSnapshot(fixture, "ui.accent.main", "0 0.5 1");
            SetSnapshot(fixture, "ui.accent.negative", "1 0.5 0");
            SetSnapshot(fixture, "ui.accent.positive", "0.5 1 0");
        }
        if (version <= "0008")
        {
            SetSnapshot(fixture, "representation.bvh.build-preference", "balanced");
            SetSnapshot(fixture, "representation.blas.update-mode", "rebuild");
            SetSnapshot(fixture, "representation.tlas.update-mode", "refit");
        }
        if (version == "0007")
            SetSnapshot(fixture, "anti-aliasing.msaa.quality", "ultra");
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
                rejected.result.error.MessageView().find(field) != std::string::npos &&
                live == before && writes == writesBefore,
            "schema " + std::string(version) + " must reject " +
                std::string(field) + " before mutation: " + std::string(rejected.result.error.MessageView()));
    };
    {
        auto fixture = withRetiredInterface(ParseSnapshot(controller.Canonical()));
        SetSnapshot(fixture, "ui.accent.primary", "0.0000000000000000000000000000000000 0 0 1");
        const auto code = appendFixture("0017", fixture);
        const auto before = live;
        const std::string publishedCode(controller.Code());
        const std::string publishedCanonical(controller.Canonical());
        unsigned callbacks = 0;
        auto guarded = access;
        guarded.context = &callbacks;
        guarded.validateValue = [](void* context, SettingId, std::string_view,
            std::string_view, SettingsSnapshotError&) noexcept {
            ++*static_cast<unsigned*>(context); return false;
        };
        guarded.readValue = guarded.readRawValue = [](void* context, SettingId,
            SettingsSnapshotText&, SettingsSnapshotError&) noexcept {
            ++*static_cast<unsigned*>(context); return false;
        };
        guarded.writeValue = [](void* context, SettingId, std::string_view,
            SettingsSnapshotError&) noexcept {
            ++*static_cast<unsigned*>(context); return false;
        };
        guarded.driveSelector = [](void* context, SettingId, std::string_view,
            bool, bool, SettingsSnapshotError&) noexcept {
            ++*static_cast<unsigned*>(context); return SettingsSnapshotSelectorTransition::Failed;
        };
        FailUiSettingsValueAllocationAfter(0);
        const auto rejected = controller.BeginLoadCodeStaged(code, guarded);
        ClearUiSettingsValueAllocationFailure();
        Require(rejected.progress == SettingsSnapshotTransactionProgress::Failed &&
            rejected.result.failureStage == SettingsSnapshotTransactionFailureStage::Preflight &&
            rejected.result.error.code == SettingsSnapshotErrorCode::OutOfMemory &&
            rejected.result.error.MessageView() == "cannot allocate setting value text" &&
            callbacks == 0 && live == before && !controller.HasStagedApply() &&
            controller.Code() == publishedCode && controller.Canonical() == publishedCanonical,
            "legacy scratch OOM must preserve its structured error and published controller state before callbacks");
        const auto retried = controller.BeginLoadCodeStaged(code, guarded);
        Require(retried.result.error.code == SettingsSnapshotErrorCode::InvalidInput &&
            retried.result.error.MessageView() == "schema 23 snapshot has invalid retired setting 'ui.accent.primary'" &&
            callbacks == 0 && controller.Code() == publishedCode && controller.Canonical() == publishedCanonical,
            "legacy scratch retry must reach the original semantic rejection without publication");
    }
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
                    const auto fixture = withRetiredInterface(SnapshotFromValues(retained), skin);
                    step = controller.BeginLoadCodeStaged(appendFixture(version, fixture), access);
                    Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded && live == retained,
                        "retired interface must preserve retained values: " + std::string(step.result.error.MessageView()));
                    for (size_t entry = 0; entry < fixture.Count(); ++entry)
                    {
                        const auto& [name, value] = fixture.Entries()[entry];
                        if (retained.count(name)) continue;
                        auto invalid = CloneSnapshot(fixture);
                        SetSnapshot(invalid, name, "invalid");
                        requireRejected(version, invalid, name);
                        (void)invalid.Erase(name);
                        requireRejected(version, invalid, name);
                    }
                    auto mismatched = CloneSnapshot(fixture);
                    SetSnapshot(mismatched, "ui.accent.primary", std::string_view(skin) == "amp" ? "<unavailable>" : "0 0 0 1");
                    requireRejected(version, mismatched, "ui.accent.primary");
                }
            }
            step = controller.BeginLoadCodeStaged(appendFixture(version, withRetiredInterface(SnapshotFromValues(before))), access);
            Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded && live == before,
                "retired interface migration must round-trip");
            continue;
        }
        DecodedSettings valid = buildLegacyFixture(version);
        for (const std::string_view adaptiveSync : { "off", "vendor-agnostic", "nvidia-exclusive" })
        {
            if (version <= "000d")
                SetSnapshot(valid, "gpu.adaptive-sync", adaptiveSync);
            if (version <= "000a")
                SetSnapshot(valid, "ui.animations", adaptiveSync == "off" ? "off" : "on");
            auto expected = live;
            expected["pathing.maximum-bounces"] = "3";
            expected["pathing.minimum-bounces"] = "1";
            expected["pathing.firefly-filter"] = "off";
            expected["pathing.firefly-threshold"] = "5000";
            step = controller.BeginLoadCodeStaged(appendFixture(version, valid), access);
            Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded && live == expected,
                "schema " + std::string(version) +
                    " must preserve legacy transport and discard its retired fields: " + std::string(step.result.error.MessageView()));
        }
        for (size_t entry = 0; entry < valid.Count(); ++entry)
        {
            const auto& [name, value] = valid.Entries()[entry];
            if (live.count(name))
                continue;
            DecodedSettings missingRetired = CloneSnapshot(valid);
            (void)missingRetired.Erase(name);
            requireRejected(version, missingRetired, name);
            DecodedSettings invalid = CloneSnapshot(valid);
            SetSnapshot(invalid, name, name.find("ui.accent.") == 0u ? "0 1.5 0" : "invalid");
            requireRejected(version, invalid, name);
        }
        DecodedSettings retainedChange = CloneSnapshot(valid);
        const auto beforeRetainedChange = live;
        const auto retainedExposure = live.at("sky.exposure");
        SetSnapshot(retainedChange, "sky.exposure", retainedExposure == "1" ? "2" : "1");
        auto expectedRetainedChange = live;
        expectedRetainedChange["sky.exposure"] = SnapshotValue(retainedChange, "sky.exposure");
        step = controller.BeginLoadCodeStaged(appendFixture(version, retainedChange), access);
        Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
                live == expectedRetainedChange,
            "legacy migration must apply the retained value and discard only retired fields");
        step = controller.BeginLoadCodeStaged(appendFixture(version, valid), access);
        Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
                live == beforeRetainedChange, "legacy retained value must round-trip");
        DecodedSettings unexpectedPathing = CloneSnapshot(valid);
        SetSnapshot(unexpectedPathing, "pathing.maximum-bounces", "3");
        requireRejected(version, unexpectedPathing, "pathing.maximum-bounces");
        DecodedSettings unexpectedSkin = CloneSnapshot(valid);
        SetSnapshot(unexpectedSkin, "ui.skin", "cap");
        for (const char* name : { "ui.accent.primary", "ui.accent.font", "ui.accent.primary-background" })
            SetSnapshot(unexpectedSkin, name, "<unavailable>");
        requireRejected(version, unexpectedSkin, "ui.skin");
        DecodedSettings unknownLegacy = CloneSnapshot(valid);
        SetSnapshot(unknownLegacy, "ui.unknown", "on");
        requireRejected(version, unknownLegacy, "ui.unknown");
    }
    for (const auto& [quality, samples] :
         std::array<std::pair<std::string_view, std::string_view>, 4>{{
            { "low", "16x" }, { "medium", "8x" },
            { "high", "4x" }, { "ultra", "2x" } }})
    {
        DecodedSettings fixture = buildLegacyFixture("0007");
        SetSnapshot(fixture, "anti-aliasing.msaa.quality", quality);
        SetSnapshot(fixture, "anti-aliasing.msaa.samples", samples);
        step = controller.BeginLoadCodeStaged(appendFixture("0007", fixture), access);
        Require(step.progress == SettingsSnapshotTransactionProgress::Succeeded &&
                live.count("anti-aliasing.msaa.samples") == 0u,
            "schema 0007 must discard both retired MSAA fields: " + std::string(step.result.error.MessageView()));
    }
    for (const std::string_view invalid : { "true", "false", "ON", "0", "<unavailable>" })
    {
        DecodedSettings fixture = buildLegacyFixture("000a");
        SetSnapshot(fixture, "ui.animations", invalid);
        requireRejected("000a", fixture, "ui.animations");
    }
    DecodedSettings retiredAccent = buildLegacyFixture("000a");
    SetSnapshot(retiredAccent, "ui.accent.main", "0 0.5 1");
    requireRejected("000a", retiredAccent, "ui.accent.main");
    const DecodedSettings current = ParseSnapshot(controller.Canonical());
    const auto legacyFields = buildLegacyFixture("000b");
    for (size_t entry = 0; entry < legacyFields.Count(); ++entry)
    {
        const auto& [name, value] = legacyFields.Entries()[entry];
        if (current.Find(name)) continue;
        auto obsolete = CloneSnapshot(current);
        SetSnapshot(obsolete, name, value);
        requireRejected(currentVersion, obsolete, name);
    }
    DecodedSettings retiredAnimation = CloneSnapshot(current);
    SetSnapshot(retiredAnimation, "ui.animations", "off");
    requireRejected(currentVersion, retiredAnimation, "ui.animations");
    DecodedSettings unknownCurrent = CloneSnapshot(current);
    SetSnapshot(unknownCurrent, "ui.unknown", "on");
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
        !controller.Persist(catalogPath.c_str(), persistError) && persistError.code == SettingsSnapshotErrorCode::Collision,
        "an existing code with a conflicting payload must fail closed");
    std::filesystem::remove(catalogPath, fileError);

    {
        const std::string previousCode(controller.Code());
        const std::string previousCanonical(controller.Canonical());
        const char* previousData = controller.Canonical().data();
        const auto changedReader = [](void* owner, SettingId id, SettingsSnapshotText& value, SettingsSnapshotError& error) noexcept {
            auto& runtime = *static_cast<RuntimeContext*>(owner);
            if (!runtime.read(owner, id, value, error)) return false;
            if (id == SettingId::SkyAmbientFillEnabled) return value.Assign(value.View() == "on" ? "off" : "on", error);
            return true;
        };
        SettingsSnapshotError error;
        const auto reportedReadFailure = [](void* owner, SettingId id, SettingsSnapshotText& value, SettingsSnapshotError& readError) noexcept {
            auto& runtime = *static_cast<RuntimeContext*>(owner);
            if (id == SettingId::SkyAmbientFillEnabled)
            {
                readError.code = SettingsSnapshotErrorCode::InvalidInput;
                readError.message = "synthetic typed read failure";
                return false;
            }
            return runtime.read(owner, id, value, readError);
        };
        Require(!controller.Refresh(WithReader(reportedReadFailure), error) &&
            error.code == SettingsSnapshotErrorCode::InvalidInput &&
            error.MessageView() == "cannot read setting 'sky.ambient-fill.enabled': synthetic typed read failure" &&
            controller.Code() == previousCode && controller.Canonical() == previousCanonical &&
            controller.Canonical().data() == previousData,
            "reported read failure must preserve the snapshot and its reason");
        json::FailAllocationAfter(0);
        const bool reported = controller.Refresh(WithReader(reportedReadFailure), error);
        json::ClearAllocationFailure();
        Require(!reported && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
            controller.Canonical().data() == previousData && controller.Code() == previousCode,
            "read diagnostic allocation failure must preserve the snapshot");

        for (const auto code : {SettingsSnapshotErrorCode::OutOfMemory,
                SettingsSnapshotErrorCode::Capacity, SettingsSnapshotErrorCode::Format})
        {
            runtime.readFailure = code;
            const auto failedReader = [](void* owner, SettingId, SettingsSnapshotText&, SettingsSnapshotError& failure) noexcept {
                const auto code = static_cast<RuntimeContext*>(owner)->readFailure;
                failure.code = code;
                failure.nativeCode = 91;
                failure.cleanupCode = 92;
                failure.message = "checked reader failure";
                return false;
            };
            json::FailAllocationAfter(0);
            const bool refreshed = controller.Refresh(WithReader(failedReader), error);
            json::ClearAllocationFailure();
            Require(!refreshed && error.code == code && error.nativeCode == 91 && error.cleanupCode == 92 &&
                error.MessageView() == "checked reader failure" && controller.Code() == previousCode &&
                controller.Canonical().data() == previousData && controller.Canonical() == previousCanonical,
                "reader storage errors must preserve code, detail and published snapshot without allocating diagnostics");
        }
        FailUiSettingsValueAllocationAfter(0);
        const bool readAllocated = controller.Refresh(WithReader(read), error);
        ClearUiSettingsValueAllocationFailure();
        Require(!readAllocated && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
            controller.Code() == previousCode && controller.Canonical().data() == previousData &&
            controller.Canonical() == previousCanonical, "checked text capture exhaustion must preserve the snapshot");

        const auto unavailableReader = [](void* owner, SettingId id, SettingsSnapshotText& value, SettingsSnapshotError& readError) noexcept {
            auto& runtime = *static_cast<RuntimeContext*>(owner);
            if (id == SettingId::SkyAmbientFillEnabled)
            {
                readError = {};
                return false;
            }
            return runtime.read(owner, id, value, readError);
        };
        SettingsSnapshotController unavailable{SettingsSnapshotCatalogLocation::Installed};
        auto expectedUnavailable = ParseSnapshot(previousCanonical);
        SetSnapshot(expectedUnavailable, "sky.ambient-fill.enabled", "<unavailable>");
        const std::string expectedCanonical = CanonicalSnapshot(expectedUnavailable);
        const auto expectedCode = BuildSettingsSnapshotCode(expectedCanonical);
        Require(unavailable.Refresh(WithReader(unavailableReader), error) && unavailable.Canonical() == expectedCanonical &&
            unavailable.Code() == expectedCode.View(), "legacy unavailability must retain exact canonical bytes and code");
        const auto explicitUnavailable = [](void* owner, SettingId id, SettingsSnapshotText& value, SettingsSnapshotError& readError) noexcept {
            auto& runtime = *static_cast<RuntimeContext*>(owner);
            if (id == SettingId::SkyAmbientFillEnabled)
            {
                return value.Assign("<unavailable>", readError);
            }
            return runtime.read(owner, id, value, readError);
        };
        Require(unavailable.Refresh(WithReader(explicitUnavailable), error) && unavailable.Canonical() == expectedCanonical &&
            unavailable.Code() == expectedCode.View(), "explicit unavailability must match the legacy callback");
        DecodedSettings allUnavailable;
        for (const auto& definition : UiSettingsCommandCatalog)
            if (IsSettingsSnapshotValue(definition))
                Require(allUnavailable.Insert(definition.name, "<unavailable>", error), "prepare absent-reader fixture");
        const std::string absentCanonical = CanonicalSnapshot(allUnavailable);
        Require(unavailable.Refresh({}, error) && unavailable.Canonical() == absentCanonical &&
            unavailable.Code() == BuildSettingsSnapshotCode(absentCanonical).View(),
            "absent reader must retain the complete all-unavailable snapshot");

        for (size_t allocation : {0u, 1u, 8u, 50u})
        {
            FailSettingsSnapshotAllocationAfter(allocation);
            const bool refreshed = controller.Refresh(WithReader(changedReader), error);
            ClearSettingsSnapshotAllocationFailure();
            Require(!refreshed && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
                controller.Code() == previousCode && controller.Canonical() == previousCanonical &&
                controller.Canonical().data() == previousData, "failed refresh changed the published snapshot");
        }
        json::FailAllocationAfter(0);
        const bool formatted = controller.Refresh(WithReader(changedReader), error);
        json::ClearAllocationFailure();
        Require(!formatted && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
            controller.Code() == previousCode && controller.Canonical().data() == previousData,
            "failed canonical formatting changed the published snapshot");
        Require(controller.Refresh(WithReader(changedReader), error) && controller.Code() != previousCode &&
            controller.Refresh(WithReader(read), error) && controller.Code() == previousCode,
            "refresh must recover and retain the exact code after restoration");
        json::EncodedText section;
        Require(controller.BuildCatalogSection(section, error), "cannot prepare catalog section");
        const char* originalSection = section.Data();
        json::FailAllocationAfter(0);
        const bool built = controller.BuildCatalogSection(section, error);
        json::ClearAllocationFailure();
        Require(!built && error.code == SettingsSnapshotErrorCode::OutOfMemory && section.Data() == originalSection,
            "failed catalog serialization changed its destination");

        auto changed = ParseSnapshot(controller.Canonical());
        const std::string previousFill = SnapshotValue(changed, "sky.ambient-fill.enabled");
        SetSnapshot(changed, "sky.ambient-fill.enabled", previousFill == "on" ? "off" : "on");
        auto failingRefreshAccess = access;
        failingRefreshAccess.writeValue = [](void* owner, SettingId id, std::string_view value, SettingsSnapshotError& writeError) noexcept {
            auto& runtime = *static_cast<RuntimeContext*>(owner);
            const bool accepted = runtime.write(owner, id, value, writeError);
            if (accepted) FailSettingsSnapshotAllocationAfter(0);
            return accepted;
        };
        const auto result = controller.BeginApplyCanonicalStaged(CanonicalSnapshot(changed), failingRefreshAccess);
        ClearSettingsSnapshotAllocationFailure();
        Require(result.progress == SettingsSnapshotTransactionProgress::Failed &&
            result.result.failureStage == SettingsSnapshotTransactionFailureStage::Readback &&
            !result.result.succeeded && !result.result.rollbackAttempted && result.result.changedValueCount == 1 &&
            live.at("sky.ambient-fill.enabled") != previousFill && controller.Code() == previousCode &&
            controller.Canonical() == previousCanonical && result.result.error.MessageView().find("settings were applied") != std::string::npos,
            "post-apply refresh failure must preserve the snapshot and report the actual applied state");
        Require(controller.Refresh(WithReader(read), error) && controller.Code() != previousCode,
            "post-apply refresh failure must allow recovery");
    }

    std::cout << "UVSR settings command owner validation passed\n";
    return EXIT_SUCCESS;
}
