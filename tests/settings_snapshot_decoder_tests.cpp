#include "settings_snapshot.h"
#include "settings_snapshot_decoder.h"
#include "file_bytes.h"
#include "windows_executable_path.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    bool CheckLoadCode(std::string_view code, std::string& error)
    {
        uvsr::SettingsSnapshotCodeError result;
        const bool accepted = uvsr::ValidateSettingsSnapshotLoadCode(code.data(), code.size(), result);
        error = result.text;
        return accepted;
    }

    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Settings snapshot decoder validation failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    struct TemporaryDirectory
    {
        std::filesystem::path path;

        TemporaryDirectory()
        {
            std::error_code error;
            const auto temporary = std::filesystem::temp_directory_path(error);
            Require(!error, "cannot find temporary directory");
            path = temporary /
                ("uvsr-settings-decoder-" + std::to_string(
                    static_cast<unsigned long long>(std::rand())));
            Require(std::filesystem::create_directories(path, error) && !error,
                "test directory must be new");
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    void Write(const std::filesystem::path& path, const std::string& value)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream)
            Fail("cannot create test catalog");
        stream << value;
    }
}

int main()
{
    using namespace uvsr;

    Require(
        BuildSettingsSnapshotCode({}).View().substr(4u) ==
            "cbf29ce4842223256c62272e07bb",
        "empty known-answer vector changed");
    Require(
        BuildSettingsSnapshotCode("a=b\n").View().substr(4u) ==
            "ec8b8c82c37596fba90fe6756c5c",
        "single-setting known-answer vector changed");
    Require(
        BuildSettingsSnapshotCode("a=b\n", "0002").View() ==
            "0002ec8b8c82c37596fba90fe6756c5c",
        "registered historical-version vector changed");
    Require(
        IsSettingsSnapshotCode(
            "0002ec8b8c82c37596fba90fe6756c5c"),
        "registered historical snapshot code was rejected");

    const std::string canonical =
        "scene.current=Sample\\nScene\nui.skin=amp\n";
    const std::string code = std::string(BuildSettingsSnapshotCode(canonical).View());
    std::string loadCodeError;
    Require(
        CheckLoadCode(code, loadCodeError) &&
            loadCodeError.empty(),
        "the current snapshot schema must be loadable");
    Require(
        !CheckLoadCode({}, loadCodeError) &&
            !loadCodeError.empty(),
        "a missing load code must fail clearly");
    Require(
        !CheckLoadCode(
            "0002ec8b8c82c37596fba90fe6756c5c",
            loadCodeError) &&
            !loadCodeError.empty(),
        "a registered historical schema must not load into the current catalog");
    for (const std::string_view version : SupportedLegacySettingsSnapshotVersions)
    {
        Require(
            CheckLoadCode(
                BuildSettingsSnapshotCode("a=b\n", version).View(), loadCodeError) &&
                loadCodeError.empty(),
            "every supported legacy migration must remain loadable");
    }
    Require(!CheckLoadCode(
            BuildSettingsSnapshotCode("a=b\n", "ffff").View(), loadCodeError),
        "unknown snapshot versions must fail before catalog lookup");
    Require(
        !CheckLoadCode(
            "0007CBF29CE4842223256C62272E07BB",
            loadCodeError),
        "uppercase or otherwise malformed load codes must fail");
    TemporaryDirectory directory;
    const std::filesystem::path catalog = directory.path / "catalog.txt";
    Write(
        catalog,
        "# UVSR Settings Snapshot Catalog\n[" + code + "]\n" +
            canonical + "[/" + code + "]\n");
    SettingsSnapshotError error;
    SettingsSnapshotCatalogPaths catalogs;
    Require(catalogs.Append(catalog.c_str(), error), "cannot own test catalog path");
    DecodedSettings decoded;
    Require(DecodeSettingsSnapshot(code, catalogs, decoded, error), "cannot decode test catalog");
    Require(
        decoded.Find("scene.current") && decoded.Find("scene.current")->value == "Sample\nScene" &&
            decoded.Find("ui.skin") && decoded.Find("ui.skin")->value == "amp",
        "catalog round-trip or escaping changed");
    json::EncodedText formatted;
    Require(
        FormatCanonicalSettingsSnapshot(decoded, formatted, error) &&
            std::string_view(formatted.Data(), formatted.Size()) == canonical,
        "decoded settings must return to command-name order with exact escapes");
    Require(
        FormatDecodedSettingsJson(decoded, formatted, error) &&
            std::string_view(formatted.Data(), formatted.Size()) ==
            "{\n  \"scene.current\": \"Sample\\nScene\",\n"
            "  \"ui.skin\": \"amp\"\n}\n",
        "JSON rendering changed");

    const std::string absentCode =
        std::string(BuildSettingsSnapshotCode("absent.setting=on\n").View());
    Require(
        !DecodeSettingsSnapshot(absentCode, catalogs, decoded, error),
        "a valid code missing from an existing catalog must fail");
    SettingsSnapshotCatalogPaths missingCatalog;
    Require(missingCatalog.Append((directory.path / "missing-catalog.txt").c_str(), error),
        "cannot own missing test path");
    Require(
        !DecodeSettingsSnapshot(code, missingCatalog, decoded, error),
        "a missing snapshot catalog must fail");

    Write(
        catalog,
        "[" + code + "]\na=b\n[/" + code + "]\n");
    Require(
        !DecodeSettingsSnapshot(code, catalogs, decoded, error),
        "a catalog payload that does not match its code must fail hashing");

    Write(
        catalog,
        "[" + code + "]\n" + canonical + "[/" + code + "]\n"
        "[" + code + "]\na=b\n[/" + code + "]\n");
    Require(
        !DecodeSettingsSnapshot(code, catalogs, decoded, error),
        "distinct duplicate blocks must report a collision");

    Write(catalog, "[" + code + "]\na=b\n");
    SettingsSnapshotMatches matches;
    Require(
        !ReadMatchingSettingsSnapshots(catalog.c_str(), code, matches, error),
        "unterminated blocks must fail");
    Require(
        !ParseSettingsSnapshot("a=b\na=c\n", decoded, error),
        "duplicate settings must fail");
    Require(
        !ParseSettingsSnapshot("missing-separator\n", decoded, error),
        "malformed settings must fail");
    Require(
        !UnescapeSettingsSnapshotValue("bad\\q", formatted, error),
        "unknown escapes must fail");
    Require(
        !UnescapeSettingsSnapshotValue("bad\\", formatted, error),
        "trailing escapes must fail");
    Require(
        !DecodeSettingsSnapshot(std::string(32u, '0'), catalogs, decoded, error),
        "unregistered codes must fail");

    Write(catalog, "[" + code + "]\n" + canonical + "[/" + code + "]\n");
    Require(ReadMatchingSettingsSnapshots(catalog.c_str(), code, matches, error) && matches.Count() == 1,
        "cannot prepare catalog failure controls");
    const char* originalMatch = matches.Text(0).data();
    FailFileAllocationAfter(0);
    const bool allocated = ReadMatchingSettingsSnapshots(catalog.c_str(), code, matches, error);
    ClearFileAllocationFailure();
    Require(!allocated && error.code == SettingsSnapshotErrorCode::OutOfMemory && matches.Text(0).data() == originalMatch,
        "failed catalog allocation changed its output");
    for (bool closeFailure : {false, true})
    {
        SetFileReadTestLimits(closeFailure ? SIZE_MAX : 1, closeFailure ? SIZE_MAX : 1, closeFailure);
        const bool read = ReadMatchingSettingsSnapshots(catalog.c_str(), code, matches, error);
        ClearFileReadTestLimits();
        Require(!read && error.code == SettingsSnapshotErrorCode::Catalog && matches.Text(0).data() == originalMatch &&
            matches.Text(0) == canonical && std::string_view(error.Message()).find("cannot read snapshot catalog: ") == 0,
            "failed catalog read or close published partial output");
    }
    const auto* originalDecoded = decoded.Entries();
    for (size_t failure = 0; failure != 4; ++failure)
    {
        FailSettingsSnapshotAllocationAfter(failure);
        const bool accepted = DecodeSettingsSnapshot(code, catalogs, decoded, error);
        ClearSettingsSnapshotAllocationFailure();
        Require(!accepted && error.code == SettingsSnapshotErrorCode::OutOfMemory && decoded.Entries() == originalDecoded,
            "failed catalog decoding changed existing settings");
    }
    for (size_t failure = 0; failure != 4; ++failure)
    {
        json::FailAllocationAfter(failure);
        const bool accepted = DecodeSettingsSnapshot(code, catalogs, decoded, error);
        json::ClearAllocationFailure();
        Require(!accepted && error.code == SettingsSnapshotErrorCode::OutOfMemory && decoded.Entries() == originalDecoded,
            "failed catalog text decoding changed existing settings");
    }
    Require(DecodeSettingsSnapshot(code, catalogs, decoded, error), "catalog decode retry failed");
    Require(!DecodeSettingsSnapshot({reinterpret_cast<const char*>(UINTPTR_MAX - 4), 32}, catalogs, decoded, error) &&
        error.code == SettingsSnapshotErrorCode::InvalidInput, "wrapping code range was accepted");

    const auto local = directory.path / "local";
    const auto blockedPackage = local / "Packages" / "blocked";
    const auto validPackage = local / "Packages" / "valid";
    const auto blockedRoot = blockedPackage / "LocalCache" / "Local" / "UVSR";
    const auto validRoot = validPackage / "LocalCache" / "Local" / "UVSR";
    std::error_code nativeError;
    Require(std::filesystem::create_directories(blockedRoot, nativeError) && !nativeError &&
        std::filesystem::create_directories(validRoot, nativeError) && !nativeError,
        "cannot prepare private package lookup controls");
    const auto blockedCatalog = blockedRoot / "settings-snapshots-v0007.txt";
    const auto validCatalog = validRoot / "settings-snapshots-v0007.txt";
    Write(blockedCatalog, "");
    Write(validCatalog, "");
    wchar_t* oldLocal = nullptr;
    size_t oldLocalLength = 0;
    Require(_wdupenv_s(&oldLocal, &oldLocalLength, L"LOCALAPPDATA") == 0 &&
        _wputenv_s(L"LOCALAPPDATA", local.c_str()) == 0, "cannot set the private catalog environment");
    SettingsSnapshotCatalogPaths defaults;
    Require(GetDefaultSettingsSnapshotCatalogPaths("0007", SettingsSnapshotCatalogLocation::Installed, defaults, error) &&
        defaults.Count() == 3, "private package catalogs were not discovered");
    for (uint32_t status : {uint32_t(ERROR_ACCESS_DENIED), uint32_t(ERROR_FILE_NOT_FOUND)})
    {
        FailSettingsSnapshotCatalogProbeOnce(blockedPackage.c_str(), status);
        const bool located = GetDefaultSettingsSnapshotCatalogPaths("0007", SettingsSnapshotCatalogLocation::Installed,
            defaults, error);
        ClearSettingsSnapshotCatalogProbeFailure();
        Require(located && defaults.Count() == 2 &&
            std::filesystem::path(defaults.Path(0)) == local / "UVSR" / "settings-snapshots-v0007.txt" &&
            std::filesystem::path(defaults.Path(1)) == validCatalog,
            "an unusable package entry must not block other snapshot catalogs");
    }
    const wchar_t* originalDefault = defaults.Path(0);
    FailSettingsSnapshotAllocationAfter(0);
    const bool pathsAllocated = GetDefaultSettingsSnapshotCatalogPaths("0007", SettingsSnapshotCatalogLocation::Installed,
        defaults, error);
    ClearSettingsSnapshotAllocationFailure();
    Require(!pathsAllocated && error.code == SettingsSnapshotErrorCode::OutOfMemory && defaults.Path(0) == originalDefault &&
        defaults.Count() == 2, "failed default lookup changed its path owner");
    for (auto failure : {WindowsPathError::Allocation, WindowsPathError::ModuleQuery})
    {
        FailWindowsPathOnce(failure);
        const bool located = GetDefaultSettingsSnapshotCatalogPaths("0007", SettingsSnapshotCatalogLocation::ExecutableState,
            defaults, error);
        Require(!located && defaults.Path(0) == originalDefault && defaults.Count() == 2,
            "failed executable path query changed default catalogs");
    }
    Require(_wputenv_s(L"LOCALAPPDATA", oldLocal ? oldLocal : L"") == 0, "cannot restore the private catalog environment");
    free(oldLocal);

    std::cout << "UVSR settings snapshot decoder validation passed\n";
    return EXIT_SUCCESS;
}
