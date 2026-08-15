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
            "on|off")
    };
    constexpr auto ChangedDomainCatalog = std::array{
        Value(
            "example.enabled",
            Kind::Boolean,
            Section::General,
            "false|true")
    };
    constexpr SettingsSnapshotSchemaFingerprint BaselineFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(BaselineCatalog);
    constexpr SettingsSnapshotSchemaFingerprint ChangedFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(ChangedDomainCatalog);
    constexpr SettingsSnapshotSchemaFingerprint ChangedPolicyFingerprint =
        BuildSettingsSnapshotSchemaFingerprint(
            BaselineCatalog,
            "synthetic-policy-change");
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
    static_assert(!(BaselineFingerprint == ChangedPolicyFingerprint));
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
        "on|off")));
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
