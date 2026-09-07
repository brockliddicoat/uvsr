#include "uvsr_command_line.h"
#include "command_line_options.h"
#include "sha256.h"
#include "windows_executable_path.h"
#include "settings_snapshot_decoder.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace uvsr;
    namespace fs = std::filesystem;

    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Parse(std::initializer_list<const char*> arguments, UvsrStartupOptions& options, std::string& error)
    {
        return ParseUvsrCommandLine(static_cast<int>(arguments.size()), arguments.begin(), options, error);
    }

    void CommandLine()
    {
        UvsrStartupOptions options;
        std::string error;
        Require(Parse({"uvsr-engine.exe", "-width", "1280", "-height", "720", "-adapter", "2",
            "-fullscreen", "Bistro Interior", "--settings-snapshot", "0007cbf29ce4842223256c62272e07bb"}, options, error) &&
            options.width == 1280 && options.height == 720 && options.adapterIndex == 2 && options.fullscreen &&
            options.sceneName == "Bistro Interior" && options.settingsSnapshotCode == "0007cbf29ce4842223256c62272e07bb",
            "retained options did not parse their exact values");
        Require(!Parse({"uvsr-engine.exe", "-width", "0"}, options, error) && error.find("at least 1") != std::string::npos,
            "zero width did not report its range");
        Require(!Parse({"uvsr-engine.exe", "-unknown"}, options, error) &&
            error.find("unknown command-line option") != std::string::npos, "unknown option was accepted");
        for (const auto arguments : {
                std::initializer_list<const char*>{"uvsr-engine.exe", "-adapter", "2gpu"},
                {"uvsr-engine.exe", "--settings-snapshot"},
                {"uvsr-engine.exe", "--settings-snapshot", "0007not-a-canonical-code"},
                {"uvsr-engine.exe", "--settings-snapshot", "0007cbf29ce4842223256c62272e07bb",
                    "--settings-snapshot", "0007cbf29ce4842223256c62272e07bb"}})
            Require(!Parse(arguments, options, error), "malformed integer or missing/invalid/duplicate snapshot was accepted");
        Require(Parse({"uvsr-engine.exe", "Bistro", "San Miguel"}, options, error) && options.sceneName == "San Miguel",
            "scene arguments lost left-to-right behavior");
#if defined(UVSR_BUILD_TESTING)
        Require(Parse({"uvsr-engine.exe", "-debug", "--verify-settings-contract"}, options, error) && options.debugValidation,
            "developer diagnostics did not compose with validation");
#else
        for (const char* option : {"-debug", "--verify-settings-contract"})
            Require(!Parse({"uvsr-engine.exe", option}, options, error), "production exposed developer diagnostics");
#endif
        int value = -1;
        Require(ParseCommandLineInt("1", 1, 4096, value) && value == 1 &&
            ParseCommandLineInt("4096", 1, 4096, value) && value == 4096, "integer parser lost its inclusive bounds");
        for (const char* text : {"", "0", "-1", "42px", "999999999999999999999"})
            Require(!ParseCommandLineInt(text, 1, 4096, value), "integer parser accepted a partial, overflowing or out-of-range value");
        Require(!ParseCommandLineInt("-1", 0, 4096, value) &&
            !ParseCommandLineInt("999999999999999999999", 1, (std::numeric_limits<int>::max)(), value),
            "integer parser accepted negative zero-bound or integer overflow");
    }

    void ExecutablePaths()
    {
        const fs::path expected(L"C:\\Users\\Miyuki-\u7F8E\u96EA\\AppData\\Local\\Programs\\UVSR\\bin");
        Require(ExecutableDirectoryFromModulePath(
            L"C:\\Users\\Miyuki-\u7F8E\u96EA\\AppData\\Local\\Programs\\UVSR\\bin\\uvsr-engine.exe") == expected &&
            GetExecutableDirectoryWide().is_absolute(), "wide executable path lost Unicode or its absolute directory");
        bool rejected = false;
        try { (void)ExecutableDirectoryFromModulePath(L""); }
        catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "empty module path was accepted");
    }

    template<typename Operation>
    void ExpectFault(Sha256Stage stage, Operation&& operation)
    {
        bool failed = false;
        try { operation(); }
        catch (const Sha256Error& error)
        {
            failed = true;
            Require(error.Stage() == stage, "SHA-256 failure lost its stage attribution");
        }
        Require(failed, "injected SHA-256 failure was not observed");
    }

    void Hashes()
    {
        constexpr std::string_view emptyHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
        constexpr std::string_view abcHash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        Require(Sha256("") == emptyHash && Sha256("abc") == abcHash, "standard SHA-256 known answers changed");
        for (auto stage : {Sha256Stage::OpenProvider, Sha256Stage::ReadProviderObjectSize, Sha256Stage::CreateHash,
                Sha256Stage::HashData, Sha256Stage::FinishHash})
        {
            ExpectFault(stage, [stage] { (void)Sha256("abc", {stage, 1}); });
            Require(Sha256("abc") == abcHash, "injected crypto failure contaminated the next digest");
        }

        const fs::path path = fs::temp_directory_path() / ("uvsr-sha256-" + std::to_string(GetCurrentProcessId()) + ".bin");
        const fs::path moved = path.string() + ".moved";
        std::error_code ignored;
        fs::remove(moved, ignored);
        std::string contents(1024u * 1024u + 3u, 'x');
        contents.replace(contents.size() - 3, 3, "abc");
        try
        {
            {
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
                output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
                Require(bool(output), "cannot write SHA-256 boundary fixture");
            }
            const std::string expected = Sha256(contents);
            Require(Sha256File(path) == expected, "streaming and in-memory SHA-256 differ");
            const Sha256Fault faults[] = {
                {Sha256Stage::OpenFile, 1}, {Sha256Stage::ReadFile, 2}, {Sha256Stage::OpenProvider, 1},
                {Sha256Stage::ReadProviderObjectSize, 1}, {Sha256Stage::CreateHash, 1},
                {Sha256Stage::HashData, 2}, {Sha256Stage::FinishHash, 1}
            };
            for (const auto fault : faults)
            {
                ExpectFault(fault.stage, [&] { (void)Sha256File(path, fault); });
                fs::rename(path, moved);
                fs::rename(moved, path);
                Require(Sha256File(path) == expected, "file hash did not release its handle and recover after failure");
            }
            ExpectFault(Sha256Stage::OpenFile, [&] { (void)Sha256File(moved); });
        }
        catch (...)
        {
            fs::remove(path, ignored);
            fs::remove(moved, ignored);
            throw;
        }
        fs::remove(path);
    }
}

int main()
{
    try
    {
        CommandLine();
#if defined(UVSR_BUILD_TESTING)
        const auto catalogs = GetDefaultSettingsSnapshotCatalogPaths("0007");
        Require(catalogs.size() == 1 &&
            catalogs.front().parent_path() == GetExecutableDirectoryWide() / "state",
            "developer startup must not search installed or packaged snapshot catalogs");
#endif
        ExecutablePaths();
        Hashes();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
