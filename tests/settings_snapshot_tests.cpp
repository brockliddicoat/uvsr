#include "settings_snapshot.h"

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

    constexpr auto BaselineCatalog = std::array{
        Value(
            "example.enabled",
            Kind::Boolean,
            Section::General,
            "on|off",
            true,
            false,
            "on")
    };
    constexpr auto ChangedDomainCatalog = std::array{
        Value(
            "example.enabled",
            Kind::Boolean,
            Section::General,
            "false|true",
            true,
            false,
            "on")
    };
    constexpr auto ChangedDefaultCatalog = std::array{
        Value(
            "example.enabled",
            Kind::Boolean,
            Section::General,
            "on|off",
            true,
            false,
            "off")
    };
    constexpr auto ChangedPersistenceCatalog = [BaselineCatalog] {
        auto catalog = BaselineCatalog;
        catalog[0].persistence = UiSettingsPersistence::SessionOnly;
        return catalog;
    }();
    constexpr auto SessionDefaultCatalog = [] {
        auto definition = Value(
            "example.session",
            Kind::Boolean,
            Section::Ui,
            "on|off",
            true,
            false,
            "off");
        definition.persistence = UiSettingsPersistence::SessionOnly;
        return std::array{ definition };
    }();
    constexpr auto ChangedSessionDefaultCatalog = [SessionDefaultCatalog] {
        auto catalog = SessionDefaultCatalog;
        catalog[0].defaultValue = "on";
        return catalog;
    }();
    constexpr auto ActionCatalog = std::array{
        Action("example.reset", Section::General, "")
    };
    constexpr auto ChangedActionCatalog = [ActionCatalog] {
        auto catalog = ActionCatalog;
        catalog[0].domain = "changed-action-domain";
        return catalog;
    }();
    constexpr auto BaselineWithUnrelatedAction = std::array{
        BaselineCatalog[0],
        ActionCatalog[0]
    };
    constexpr auto ReorderedCatalog = std::array{
        Value(
            "example.second",
            Kind::Integer,
            Section::General,
            "0..9",
            true,
            false,
            "2"),
        BaselineCatalog[0]
    };
    constexpr auto CanonicallyOrderedCatalog = std::array{
        BaselineCatalog[0],
        ReorderedCatalog[0]
    };
    constexpr SettingsSnapshotSchemaFingerprint BaselineFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(BaselineCatalog);
    constexpr SettingsSnapshotSchemaFingerprint ChangedFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(ChangedDomainCatalog);
    constexpr SettingsSnapshotSchemaFingerprint ChangedDefaultFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(ChangedDefaultCatalog);
    constexpr SettingsSnapshotSchemaFingerprint
        ChangedPersistenceFingerprint =
            BuildSettingsSnapshotSchemaFingerprint(
                ChangedPersistenceCatalog);
    constexpr SettingsSnapshotSchemaFingerprint ChangedPolicyFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(
            BaselineCatalog,
            "synthetic-policy-change");
    constexpr SettingsSnapshotSchemaFingerprint SessionDefaultFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(SessionDefaultCatalog);
    constexpr SettingsSnapshotSchemaFingerprint
        ChangedSessionDefaultFingerprint =
            BuildSettingsSnapshotSchemaFingerprint(
                ChangedSessionDefaultCatalog);
    constexpr SettingsSnapshotSchemaFingerprint ActionFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(ActionCatalog);
    constexpr SettingsSnapshotSchemaFingerprint ChangedActionFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(ChangedActionCatalog);
    constexpr SettingsSnapshotSchemaFingerprint
        BaselineWithUnrelatedActionFingerprint =
            BuildSettingsSnapshotSchemaFingerprint(
                BaselineWithUnrelatedAction);
    constexpr SettingsSnapshotSchemaFingerprint ReorderedFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(ReorderedCatalog);
    constexpr SettingsSnapshotSchemaFingerprint CanonicallyOrderedFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(CanonicallyOrderedCatalog);
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

    static_assert(SettingsSnapshotVersion != 0u);
    static_assert(SettingsSnapshotCodeLength == 32u);
    static_assert(
        ResolveSettingsSnapshotSchemaVersion(
            CurrentSettingsSnapshotSchemaFingerprint) ==
        SettingsSnapshotVersion);
    static_assert(!(BaselineFingerprint == ChangedFingerprint));
    static_assert(!(BaselineFingerprint == ChangedDefaultFingerprint));
    static_assert(!(BaselineFingerprint == ChangedPersistenceFingerprint));
    static_assert(!(BaselineFingerprint == ChangedPolicyFingerprint));
    static_assert(!(SessionDefaultFingerprint ==
        ChangedSessionDefaultFingerprint));
    static_assert(ActionFingerprint == ChangedActionFingerprint);
    static_assert(BaselineFingerprint ==
        BaselineWithUnrelatedActionFingerprint);
    static_assert(ReorderedFingerprint == CanonicallyOrderedFingerprint);
    static_assert(CurrentSettingsSnapshotSchemaFingerprint ==
        SettingsSnapshotSchemaFingerprint{
            0x9c50b0f1515e89d8ull,
            0x56c8ebb627b86984ull });
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
    static_assert(IsSettingsSnapshotValue(Value(
        "example.enabled",
        Kind::Boolean,
        Section::General,
        "on|off",
        true,
        false,
        "on")));
    static_assert(!IsSettingsSnapshotValue(Value(
        "ui.settings-collapsed",
        Kind::Boolean,
        Section::Ui,
        "on|off")));
    static_assert(!IsSettingsSnapshotValue(Value(
        "material-editor.visible",
        Kind::Boolean,
        Section::Ui,
        "on|off")));

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
