#include "settings_snapshot.h"
#include "settings_snapshot_decoder.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
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

    template<typename Function>
    void RequireThrows(Function&& function, const std::string& message)
    {
        try
        {
            function();
        }
        catch (const std::exception&)
        {
            return;
        }
        Fail(message);
    }

    struct TemporaryDirectory
    {
        std::filesystem::path path;

        TemporaryDirectory()
        {
            path = std::filesystem::temp_directory_path() /
                ("uvsr-settings-decoder-" + std::to_string(
                    static_cast<unsigned long long>(std::rand())));
            std::filesystem::create_directories(path);
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
        BuildSettingsSnapshotCode({}).substr(4u) ==
            "cbf29ce4842223256c62272e07bb",
        "empty known-answer vector changed");
    Require(
        BuildSettingsSnapshotCode("a=b\n").substr(4u) ==
            "ec8b8c82c37596fba90fe6756c5c",
        "single-setting known-answer vector changed");
    Require(
        BuildSettingsSnapshotCode("a=b\n", "0002") ==
            "0002ec8b8c82c37596fba90fe6756c5c",
        "registered historical-version vector changed");
    Require(
        IsSettingsSnapshotCode(
            "0002ec8b8c82c37596fba90fe6756c5c"),
        "registered historical snapshot code was rejected");

    const std::string canonical =
        "scene.current=Sample\\nScene\nui.skin=amp\n";
    const std::string code = BuildSettingsSnapshotCode(canonical);
    std::string loadCodeError;
    Require(
        ValidateSettingsSnapshotLoadCode(code, loadCodeError) &&
            loadCodeError.empty(),
        "the current snapshot schema must be loadable");
    Require(
        !ValidateSettingsSnapshotLoadCode({}, loadCodeError) &&
            !loadCodeError.empty(),
        "a missing load code must fail clearly");
    Require(
        !ValidateSettingsSnapshotLoadCode(
            "0002ec8b8c82c37596fba90fe6756c5c",
            loadCodeError) &&
            loadCodeError.find("does not match") != std::string::npos,
        "a registered historical schema must not load into the current catalog");
    Require(
        !ValidateSettingsSnapshotLoadCode(
            "0007CBF29CE4842223256C62272E07BB",
            loadCodeError),
        "uppercase or otherwise malformed load codes must fail");
    TemporaryDirectory directory;
    const std::filesystem::path catalog = directory.path / "catalog.txt";
    Write(
        catalog,
        "# UVSR Settings Snapshot Catalog\n[" + code + "]\n" +
            canonical + "[/" + code + "]\n");
    const DecodedSettings decoded = DecodeSettingsSnapshot(code, { catalog });
    Require(
        decoded.at("scene.current") == "Sample\nScene" &&
            decoded.at("ui.skin") == "amp",
        "catalog round-trip or escaping changed");
    Require(
        FormatCanonicalSettingsSnapshot(decoded) == canonical,
        "decoded settings must return to command-name order with exact escapes");
    Require(
        FormatDecodedSettingsJson(decoded) ==
            "{\n  \"scene.current\": \"Sample\\nScene\",\n"
            "  \"ui.skin\": \"amp\"\n}\n",
        "JSON rendering changed");

    const std::string absentCode =
        BuildSettingsSnapshotCode("absent.setting=on\n");
    RequireThrows(
        [&] { (void)DecodeSettingsSnapshot(absentCode, { catalog }); },
        "a valid code missing from an existing catalog must fail");
    RequireThrows(
        [&] { (void)DecodeSettingsSnapshot(
            code, { directory.path / "missing-catalog.txt" }); },
        "a missing snapshot catalog must fail");

    Write(
        catalog,
        "[" + code + "]\na=b\n[/" + code + "]\n");
    RequireThrows(
        [&] { (void)DecodeSettingsSnapshot(code, { catalog }); },
        "a catalog payload that does not match its code must fail hashing");

    Write(
        catalog,
        "[" + code + "]\n" + canonical + "[/" + code + "]\n"
        "[" + code + "]\na=b\n[/" + code + "]\n");
    RequireThrows(
        [&] { (void)DecodeSettingsSnapshot(code, { catalog }); },
        "distinct duplicate blocks must report a collision");

    Write(catalog, "[" + code + "]\na=b\n");
    RequireThrows(
        [&] { (void)ReadMatchingSettingsSnapshots(catalog, code); },
        "unterminated blocks must fail");
    RequireThrows(
        [] { (void)ParseSettingsSnapshot("a=b\na=c\n"); },
        "duplicate settings must fail");
    RequireThrows(
        [] { (void)ParseSettingsSnapshot("missing-separator\n"); },
        "malformed settings must fail");
    RequireThrows(
        [] { (void)UnescapeSettingsSnapshotValue("bad\\q"); },
        "unknown escapes must fail");
    RequireThrows(
        [] { (void)UnescapeSettingsSnapshotValue("bad\\"); },
        "trailing escapes must fail");
    RequireThrows(
        [&] { (void)DecodeSettingsSnapshot(
            std::string(32u, '0'), { catalog }); },
        "unregistered codes must fail");

    std::cout << "UVSR settings snapshot decoder validation passed\n";
    return EXIT_SUCCESS;
}
