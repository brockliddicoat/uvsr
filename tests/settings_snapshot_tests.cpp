#include "settings_snapshot.h"
#include "engine_identity.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Settings snapshot validation failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }
}

int main()
{
    using namespace uvsr;
    using Definition = UiSettingsCommandDefinition;
    const auto find = [](SettingId id) -> const Definition&
    {
        for (const auto& definition : UiSettingsCommandCatalog)
        {
            if (definition.id == id)
                return definition;
        }
        Fail("canonical setting fixture is absent");
    };
    Require(ValidateCanonicalSettingsSchema(), "the immutable catalog must satisfy its complete contract");
    for (std::size_t index = 0u; index < static_cast<std::size_t>(UiSettingsCommandSection::Count); ++index)
    {
        const auto section = static_cast<UiSettingsCommandSection>(index);
        Require(!IsUiSettingsRuntimeMutationLocked(section, false) &&
                IsUiSettingsRuntimeMutationLocked(section, true) == (section != UiSettingsCommandSection::Ui),
            "scene loading must lock renderer settings while leaving interface controls available");
    }
    for (const auto& definition : UiSettingsCommandCatalog)
        Require(!FormatUiSettingsDomain(definition).empty() && !FormatUiSettingsDefault(definition).empty(),
            "diagnostic text must format from the canonical typed metadata");
    auto invalidCatalog = UiSettingsCommandCatalog;
    std::swap(invalidCatalog[0].bindingIndex, invalidCatalog[1].bindingIndex);
    Require(!ValidateCanonicalSettingsSchema(invalidCatalog), "a setting ID and binding mismatch must reject");
    invalidCatalog = UiSettingsCommandCatalog;
    invalidCatalog[find(SettingId::SkyEnvironment).bindingIndex].valueDependsOn = SettingId::SkyExposure;
    Require(!ValidateCanonicalSettingsSchema(invalidCatalog), "a value dependency cycle must reject");
    invalidCatalog = UiSettingsCommandCatalog;
    invalidCatalog[1].name = invalidCatalog[0].name;
    Require(!ValidateCanonicalSettingsSchema(invalidCatalog), "duplicate command identity must reject");
    Require(FormatUiSettingsDomain(find(SettingId::LightingSolution)) == "ray-marching|path-tracing" &&
            FormatUiSettingsTokenLabel(SettingId::LightingSolution, 0u) == "Ray Tracing",
        "renderer labels must preserve their persisted tokens");
    Require(FormatUiSettingsTokenLabel(SettingId::NoiseResolution, 0u) == "64x64" &&
            FormatUiSettingsTokenLabel(SettingId::NoisePattern, 2u) == "Spatiotemporal Blue" &&
            FormatUiSettingsTokenLabel(SettingId::NoisePattern, 99u).empty(),
        "authored enum labels and fallback formatting must remain distinct");
    constexpr SettingsNumberHash knownHash{ 0x0123456789abcdefull, 0xfedcba9876543210ull };
    static_assert(DeriveEngineVersion(knownHash) == EngineVersion{ 0x0123u, 0x4567u, 0x89abu, 0xcdefu });
    constexpr auto knownText = BuildSettingsNumberHashText(knownHash);
    static_assert(std::string_view(knownText.data(), 32u) == "0123456789abcdeffedcba9876543210");
    Require(CanonicalSettingsNumberHash == CurrentSettingsSnapshotSchemaFingerprint &&
            CurrentEngineVersion == DeriveEngineVersion(CanonicalSettingsNumberHash) &&
            GetSettingsNumberHashText() == std::string(CanonicalSettingsNumberHashText.data(), 32u),
        "the exported engine identity must derive from the one canonical catalog");

    const auto baseline = std::array{ find(SettingId::SkyExposure) };
    const auto fingerprint = BuildSettingsSnapshotSchemaFingerprint(baseline);
    struct Change
    {
        const char* name;
        void (*apply)(Definition&);
    };
    const Change changes[] = {
        { "minimum", [](Definition& d) { d.typedDomain.minimum = 0.2; } },
        { "maximum", [](Definition& d) { d.typedDomain.maximum = 9.0; } },
        { "default value", [](Definition& d) { d.typedDefault.value.scalar = 4.f; } },
        { "persistence", [](Definition& d) { d.persistence = UiSettingsPersistence::SessionOnly; } },
        { "stable identity", [](Definition& d) { d.id = SettingId::UiVisible; } },
        { "default policy", [](Definition& d) { d.typedDefault.policy = UiSettingsDefaultPolicy::SceneAuthored; } },
        { "application role", [](Definition& d) { d.applicationRole = UiSettingsApplicationRole::LiveDependent; } },
        { "selector dependency", [](Definition& d) { d.dependsOn = SettingId::MaterialSelected; } },
        { "value dependency", [](Definition& d) { d.valueDependsOn = SettingId::NoisePattern; } },
        { "availability", [](Definition& d) { d.availability = UiSettingsAvailability::SelectedLight; } },
        { "latent storage", [](Definition& d) { d.storage = UiSettingsStoragePolicy::Latent; } },
        { "snapshot read", [](Definition& d) { d.snapshotRead = UiSettingsSnapshotReadPolicy::LatentValue; } },
        { "factory reset", [](Definition& d) { d.factoryReset = UiSettingsFactoryResetPolicy::Preserve; } },
        { "effects", [](Definition& d) { d.effects = UiSettingsMutationEffect::Material; } },
        { "track range", [](Definition& d) { d.presentation.trackMaximum = 9.0; } },
        { "display scale", [](Definition& d) { d.presentation.displayScale = 100.f; } },
        { "display unit", [](Definition& d) { d.presentation.displayUnit = UiSettingsDisplayUnit::Percent; } }
    };
    for (const Change& change : changes)
    {
        auto changed = baseline;
        change.apply(changed[0]);
        Require(!(fingerprint == BuildSettingsSnapshotSchemaFingerprint(changed)),
            std::string(change.name) + " must participate in schema identity");
    }
    auto label = std::array{ find(SettingId::NoiseResolution) };
    const auto labelFingerprint = BuildSettingsSnapshotSchemaFingerprint(label);
    label[0].presentation.tokenLabels[1] = "Changed Label";
    Require(!(labelFingerprint == BuildSettingsSnapshotSchemaFingerprint(label)),
        "authored token labels must participate in schema identity");
    auto session = std::array{ find(SettingId::UiSettingsCollapsed) };
    const auto sessionFingerprint = BuildSettingsSnapshotSchemaFingerprint(session);
    session[0].typedDefault.value.boolean = true;
    Require(!(sessionFingerprint == BuildSettingsSnapshotSchemaFingerprint(session)),
        "session defaults must participate in schema identity");
    auto binding = baseline;
    binding[0].bindingIndex = 42u;
    Require(fingerprint == BuildSettingsSnapshotSchemaFingerprint(binding),
        "internal binding order must not change schema identity");
    Require(!(fingerprint == BuildSettingsSnapshotSchemaFingerprint(baseline, "synthetic-policy-change")),
        "serialization policy must participate in schema identity");
    auto action = UiSettingsCommandCatalog.back();
    const auto withAction = std::array{ baseline[0], action };
    action.typedDomain.presentation = "changed action domain";
    Require(fingerprint == BuildSettingsSnapshotSchemaFingerprint(withAction) &&
            fingerprint == BuildSettingsSnapshotSchemaFingerprint(std::array{ action, baseline[0] }),
        "actions and their presentation must not affect snapshot identity");
    const auto secondDefinition = find(SettingId::ShadowsRayTracedSamplesPerPixel);
    Require(BuildSettingsSnapshotSchemaFingerprint(std::array{ baseline[0], secondDefinition }) ==
            BuildSettingsSnapshotSchemaFingerprint(std::array{ secondDefinition, baseline[0] }),
        "schema identity must be independent of catalog order");
    Require(SettingsSnapshotVersion != 0u &&
            ResolveSettingsSnapshotSchemaVersion(CurrentSettingsSnapshotSchemaFingerprint) == SettingsSnapshotVersion,
        "the compiled catalog must resolve to its registered version");
    Require(IsSettingsSnapshotValue(find(SettingId::UiOverrideVisualMaxes)) &&
            !IsSettingsSnapshotValue(find(SettingId::UiSettingsCollapsed)) &&
            !IsSettingsSnapshotValue(find(SettingId::MaterialEditorVisible)) &&
            !IsSettingsSnapshotValue(find(SettingId::PresentationVerticalSynchronization)) &&
            !IsSettingsSnapshotValue(find(SettingId::PresentationFrameRateLimitEnabled)) &&
            !IsSettingsSnapshotValue(find(SettingId::PresentationFrameRateLimit)),
        "snapshot membership must exclude only session presentation and actions");

    constexpr std::array<SettingsSnapshotSchemaVersionEntry, 1>
        ReservedVersionRegistry = {{
            { 1u, { 1u, 2u } }
        }};
    constexpr std::array<SettingsSnapshotSchemaVersionEntry, 2>
        DuplicateVersionRegistry = {{
            { 2u, { 1u, 2u } },
            { 2u, { 3u, 4u } }
        }};
    constexpr std::array<SettingsSnapshotSchemaVersionEntry, 2>
        DuplicateFingerprintRegistry = {{
            { 2u, { 1u, 2u } },
            { 3u, { 1u, 2u } }
        }};
    constexpr std::array<SettingsSnapshotSchemaVersionEntry, 1>
        EmptyFingerprintRegistry = {{
            { 2u, { 0u, 0u } }
        }};
    constexpr std::array<SettingsSnapshotSchemaVersionEntry, 2>
        RegistryWithGap = {{
            { 2u, { 1u, 2u } },
            { 4u, { 3u, 4u } }
        }};
    constexpr std::array<SettingsSnapshotSchemaVersionEntry, 1>
        ExhaustedRegistry = {{
            { 0xffffu, { 1u, 2u } }
        }};

    static_assert(!ValidateSettingsSnapshotSchemaRegistry(
        ReservedVersionRegistry));
    static_assert(!ValidateSettingsSnapshotSchemaRegistry(
        DuplicateVersionRegistry));
    static_assert(!ValidateSettingsSnapshotSchemaRegistry(
        DuplicateFingerprintRegistry));
    static_assert(!ValidateSettingsSnapshotSchemaRegistry(
        EmptyFingerprintRegistry));
    static_assert(
        GetNextAvailableSettingsSnapshotSchemaVersion(RegistryWithGap) == 5u);
    static_assert(
        GetNextAvailableSettingsSnapshotSchemaVersion(ExhaustedRegistry) == 0u);

    const std::string version(
        SettingsSnapshotVersionText.data(),
        4u);
    const auto code = [&version](std::string_view payload) {
        return version + std::string(payload);
    };

    Require(
        BuildSettingsSnapshotCode({}) ==
            code("cbf29ce4842223256c62272e07bb"),
        "the empty canonical snapshot vector changed");
    Require(
        BuildSettingsSnapshotCode("a=b\n") ==
            code("ec8b8c82c37596fba90fe6756c5c"),
        "the single-setting canonical snapshot vector changed");
    Require(
        BuildSettingsSnapshotCode("ui.skin=amp\n") ==
            code("582ac8a06042865d4c6f64bb61a4"),
        "the UI-skin canonical snapshot vector changed");
    Require(
        IsSettingsSnapshotCode(code(std::string(28u, '0'))) &&
            !IsSettingsSnapshotCode("00000000000000000000000000000000") &&
            !IsSettingsSnapshotCode(
                code("AAAAAAAAAAAAAAAAAAAAAAAAAAAA")),
        "code validation must require the registered current lowercase version");

    const std::string first = BuildSettingsSnapshotCode(
        "noise.pattern=spatial-blue\n");
    const std::string second = BuildSettingsSnapshotCode(
        "noise.pattern=spatial-white\n");
    Require(first != second,
        "distinct canonical settings must not share this regression vector");
    Require(first.size() == SettingsSnapshotCodeLength &&
            IsSettingsSnapshotCode(first),
        "generated settings codes must use the registered lowercase version");
    return EXIT_SUCCESS;
}
