#include "uvsr_command_line.h"
#include "command_line_options.h"
#include "sha256.h"
#include "windows_executable_path.h"
#include "settings_snapshot_decoder.h"
#include "settings_snapshot.h"

#include <Windows.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <wchar.h>

bool TestRestartCommandLineOwnership();

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
        UvsrCommandLineError result;
        const bool accepted = ParseUvsrCommandLine(static_cast<int>(arguments.size()), arguments.begin(), options, result);
        error = std::string(result.prefix) + result.argument + result.suffix + result.snapshot.text;
        return accepted;
    }

    void CommandLine()
    {
        UvsrStartupOptions options;
        std::string error;
        Require(Parse({"uvsr-engine.exe", "-width", "1280", "-height", "720", "-adapter", "2",
            "-fullscreen", "Bistro Interior", "--settings-snapshot", "0007cbf29ce4842223256c62272e07bb"}, options, error) &&
            options.width == 1280 && options.height == 720 && options.adapterIndex == 2 && options.fullscreen &&
            std::string_view(options.sceneName) == "Bistro Interior" &&
            std::string_view(options.settingsSnapshotCode) == "0007cbf29ce4842223256c62272e07bb",
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
        Require(Parse({"uvsr-engine.exe", "Bistro", "San Miguel"}, options, error) && std::string_view(options.sceneName) == "San Miguel",
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

    void CommandLineBoundaries()
    {
        char scene[] = "scene with spaces";
        char code[] = "0007cbf29ce4842223256c62272e07bb";
        const char* arguments[] = {"uvsr-engine.exe", "-width", "1280", "-height", "720",
            "-adapter", "0", "-fullscreen", scene, "--settings-snapshot", code};
        UvsrStartupOptions options;
        UvsrCommandLineError error;
        const auto parseValid = [&]()
        {
            Require(ParseUvsrCommandLine(int(std::size(arguments)), arguments, options, error) &&
                options.width == 1280 && options.height == 720 && options.adapterIndex == 0 &&
                options.fullscreen && options.sceneName == scene && options.settingsSnapshotCode == code &&
                error.prefix[0] == '\0' && error.argument[0] == '\0' && error.suffix[0] == '\0' &&
                error.snapshot.text[0] == '\0', "successful parse lost borrowing, values or error clearing");
        };
        parseValid();
        const auto before = options;
        const auto expectFailure = [&](int count, const char* const* input, const std::string& message)
        {
            error.prefix = "stale"; error.argument = "old"; error.suffix = "tail"; error.snapshot.text[0] = 'x';
            Require(!ParseUvsrCommandLine(count, input, options, error), "malformed command line was accepted");
            Require(std::string(error.prefix) + error.argument + error.suffix + error.snapshot.text == message,
                "command-line error text changed or was truncated");
            Require(options.width == before.width && options.height == before.height &&
                options.adapterIndex == before.adapterIndex && options.fullscreen == before.fullscreen &&
                options.debugValidation == before.debugValidation && options.sceneName == before.sceneName &&
                options.settingsSnapshotCode == before.settingsSnapshotCode, "failed parse published partial options");
            parseValid();
        };
        expectFailure(-1, nullptr, "command-line arguments are unavailable");
        expectFailure(1, nullptr, "command-line arguments are unavailable");
        expectFailure(2, reinterpret_cast<const char* const*>(UINTPTR_MAX - sizeof(char*) + 1u),
            "command-line arguments are unavailable");
        const char* nullArgument[] = {"uvsr-engine.exe", nullptr};
        expectFailure(2, nullArgument, "command-line argument is null");
        for (const char* option : {"-width", "-height", "-adapter"})
        {
            const std::string expected = std::string(option) + " requires an exact integer of at least " +
                (std::string_view(option) == "-adapter" ? "0" : "1");
            const char* missing[] = {"uvsr-engine.exe", option};
            expectFailure(2, missing, expected);
            for (const char* value : {static_cast<const char*>(nullptr), "", "+1", " 1", "1 ",
                    "1x", "-1", "2147483648", "999999999999999999999999999999"})
            {
                const char* invalid[] = {"uvsr-engine.exe", "replacement scene", "-fullscreen", option, value};
                expectFailure(5, invalid, expected);
            }
        }
        const char* missingCode[] = {"uvsr-engine.exe", "--settings-snapshot", nullptr};
        expectFailure(3, missingCode, "--settings-snapshot requires one 32-character code");
        const char* duplicate[] = {"uvsr-engine.exe", "--settings-snapshot", code, "--settings-snapshot", code};
        expectFailure(5, duplicate, "--settings-snapshot may be specified only once");
        const char* invalidCode[] = {"uvsr-engine.exe", "--settings-snapshot", "bad"};
        expectFailure(3, invalidCode,
            "invalid --settings-snapshot value: expected a registered 32-character lowercase snapshot code");
        const std::string unknown = "-" + std::string(10000u, 'x');
        const char* unknownArguments[] = {"uvsr-engine.exe", unknown.c_str()};
        expectFailure(2, unknownArguments, "unknown command-line option '" + unknown + "'");
        const char* upper[] = {"uvsr-engine.exe", "-WIDTH"};
        expectFailure(2, upper, "unknown command-line option '-WIDTH'");
        const char* endOptions[] = {"uvsr-engine.exe", "--"};
        expectFailure(2, endOptions, "unknown command-line option '--'");

        std::string text;
        Require(Parse({"uvsr-engine.exe", "-width", "1", "-width", "2147483647",
            "-height", "2147483647", "-adapter", "2147483647", "-adapter", "0", "earlier", ""}, options, text) &&
            options.width == INT32_MAX && options.height == INT32_MAX && options.adapterIndex == 0 &&
            options.sceneName[0] == '\0', "integer bounds or left-to-right replacement changed");
        Require(Parse({"uvsr-engine.exe", "--verify-retained-runtime"}, options, text) ==
#if defined(UVSR_BUILD_TESTING)
            true
#else
            false
#endif
            , "retained-runtime diagnostic option lost its build-mode boundary");
        Require(ParseUvsrCommandLine(0, nullptr, options, error) && options.width == 0 &&
            options.height == 0 && options.adapterIndex == -1 && !options.fullscreen && !options.debugValidation &&
            options.sceneName[0] == '\0' && options.settingsSnapshotCode[0] == '\0' &&
            error.prefix[0] == '\0', "empty argument list did not publish the unspecified defaults");
        int32_t value = 23;
        for (const char* invalid : {static_cast<const char*>(nullptr), "", " 1", "+1", "1 ", "2147483648"})
            Require(!ParseCommandLineInt(invalid, 0, INT32_MAX, value) && value == 23,
                "failed integer parsing changed its output");
        Require(!ParseCommandLineInt("1", 2, 1, value) && value == 23 &&
            ParseCommandLineInt("-2147483648", INT32_MIN, INT32_MAX, value) && value == INT32_MIN,
            "integer parser lost inverted-range rejection or its signed minimum");
    }

    void SnapshotCodeValidation()
    {
        char code[33];
        std::memset(code, '0', 32u); code[32] = '\0';
        const std::string current(SettingsSnapshotVersionText.data(), 4u);
        SettingsSnapshotCodeError error;
        for (uint32_t version = 0u; version <= UINT16_MAX; ++version)
        {
            const auto prefix = BuildSettingsSnapshotVersionText(uint16_t(version));
            std::memcpy(code, prefix.data(), 4u);
            const std::string requested(code, 4u);
            // this retains the previous validation expression and error construction.
            const bool registered = IsSettingsSnapshotCode(code);
            const bool supported = requested == current || IsSupportedLegacySettingsSnapshotVersion(requested);
            const std::string expected = !registered
                ? "expected a registered 32-character lowercase snapshot code"
                : !supported ? "snapshot schema " + requested + " is neither this engine's schema " +
                    current + " nor a supported legacy migration" : "";
            std::memset(error.text, 'x', sizeof(error.text));
            Require(ValidateSettingsSnapshotLoadCode(code, 32u, error) == (registered && supported) &&
                std::string_view(error.text) == expected, "snapshot version acceptance or exact error changed");
        }
        for (const auto length : {0u, 31u, 33u})
            Require(!ValidateSettingsSnapshotLoadCode(code, length, error) && error.text[0] != '\0',
                "snapshot code length was accepted");
        Require(!ValidateSettingsSnapshotLoadCode(nullptr, 32u, error) &&
            !ValidateSettingsSnapshotLoadCode(reinterpret_cast<const char*>(UINTPTR_MAX - 15u), 32u, error),
            "snapshot code accepted an invalid address range");
        std::memcpy(code, "0007", 4u);
        code[8] = '\0';
        Require(!ValidateSettingsSnapshotLoadCode(code, 32u, error), "snapshot code accepted an embedded null");
        code[8] = 'A';
        Require(!ValidateSettingsSnapshotLoadCode(code, 32u, error), "snapshot code accepted uppercase hex");
        code[8] = 'a';
        Require(ValidateSettingsSnapshotLoadCode(code, 32u, error) && error.text[0] == '\0',
            "snapshot validation failed to recover or clear its old error");
    }

    void CheckParentPath(std::wstring_view input)
    {
        const auto expected = fs::path(input).parent_path().native();
        WindowsPath output;
        WindowsPathResult result{WindowsPathError::ModuleQuery, 123u};
        const bool accepted = ExecutableDirectoryFromModulePath(input.data(), input.size(), output, result);
        Require(accepted == (!input.empty() && !expected.empty()), "module parent acceptance changed");
        if (accepted)
        {
            Require(result.error == WindowsPathError::None && result.nativeCode == 0u &&
                std::wstring_view(output.Data(), output.Size()) == expected && output.Data()[output.Size()] == L'\0',
                "module parent changed its exact code units or retained a stale error");
            for (const wchar_t* suffix : {L"state\\logs\\uvsr-engine.log", L"media/environments", L"..\\file"})
            {
                WindowsPath joined;
                result = {WindowsPathError::ModuleQuery, 123u};
                const auto expectedJoin = (fs::path(expected) / suffix).native();
                Require(JoinWindowsRelativePath(output.Data(), suffix, joined, result) &&
                    result.error == WindowsPathError::None && result.nativeCode == 0u &&
                    std::wstring_view(joined.Data(), joined.Size()) == expectedJoin &&
                    joined.Data()[joined.Size()] == L'\0', "relative join changed the previous path grammar");
            }
        }
        else
            Require(result.error == WindowsPathError::InvalidPath && result.nativeCode != 0u &&
                output.Size() == 0u && output.Data()[0] == L'\0', "invalid parent lost its result or empty output");
    }

    template<typename Operation>
    void ExpectPathFailure(WindowsPathError expected, Operation&& operation)
    {
        constexpr std::wstring_view input = L"C:\\stable\\uvsr-engine.exe";
        WindowsPath output;
        WindowsPathResult result;
        Require(ExecutableDirectoryFromModulePath(input.data(), input.size(), output, result), "cannot seed path output");
        const wchar_t* original = output.Data();
        const size_t originalSize = output.Size();
        Require(!operation(output, result) && result.error == expected && result.nativeCode != 0u,
            "path failure lost its checked result");
        Require(output.Data() == original && output.Size() == originalSize &&
            std::wstring_view(output.Data(), output.Size()) == L"C:\\stable" && output.Data()[output.Size()] == L'\0',
            "failed path operation replaced or damaged its output");
    }

    void ExecutablePaths()
    {
        const wchar_t* cases[] = {
            L"", L"uvsr-engine.exe", L"bin\\uvsr-engine.exe", L"bin/uvsr-engine.exe",
            L"C:", L"C:uvsr-engine.exe", L"C:\\", L"C:/", L"C:\\uvsr-engine.exe", L"C:/uvsr-engine.exe",
            L"C:\\bin\\uvsr-engine.exe", L"C:\\bin/uvsr-engine.exe", L"C:/bin\\uvsr-engine.exe",
            L"C:\\Users\\Miyuki-\u7f8e\u96ea\\AppData\\Local\\Programs\\UVSR\\bin\\uvsr-engine.exe",
            L"C:\\bin\\", L"C:\\bin\\\\uvsr-engine.exe", L"C:\\bin\\.\\uvsr-engine.exe",
            L"C:\\bin\\..\\uvsr-engine.exe", L"\\uvsr-engine.exe", L"/uvsr-engine.exe",
            L"\\\\server", L"\\\\server\\share", L"\\\\server\\share\\uvsr-engine.exe",
            L"\\\\server\\share\\bin\\uvsr-engine.exe", L"//server/share/bin/uvsr-engine.exe",
            L"\\\\?\\C:\\uvsr-engine.exe", L"\\\\?\\C:\\bin\\uvsr-engine.exe",
            L"\\\\.\\C:\\uvsr-engine.exe", L"\\\\.\\C:\\bin\\uvsr-engine.exe",
            L"\\\\?\\UNC\\server\\share\\uvsr-engine.exe", L"\\\\?\\UNC\\server\\share\\bin\\uvsr-engine.exe",
            L"\\\\?\\Volume{b134c719-7b0a-4e09-973e-b075cc2e0923}\\uvsr-engine.exe",
            L"C:\\space dir\\uvsr-engine.exe", L"C:\\bin\\.", L"C:\\bin\\..",
            L"\\??\\C:\\uvsr-engine.exe", L"\\??\\", L"\\??\\\\device\\file", L"///server/file",
            L"\\\\?\\", L"\\\\?\\\\", L"c:relative\\file", L"C:\\\\file", L"C:\\\\",
            L"C:\\emoji-\U0001f9ea\\uvsr-engine.exe"
        };
        for (const wchar_t* input : cases)
            CheckParentPath(input);
        // the previous standard-library expression is an independent lexical control.
        // short combinations exercise root-prefix boundaries without filesystem I/O.
        constexpr wchar_t alphabet[] = L"/\\?.:Ca";
        constexpr size_t radix = sizeof(alphabet) / sizeof(alphabet[0]) - 1u;
        wchar_t word[6]{};
        size_t combinations = 1u;
        for (size_t length = 1u; length <= 5u; ++length)
        {
            combinations *= radix;
            for (size_t encoded = 0u; encoded < combinations; ++encoded)
            {
                size_t value = encoded;
                for (size_t position = 0u; position < length; ++position)
                {
                    word[position] = alphabet[value % radix];
                    value /= radix;
                }
                CheckParentPath({word, length});
            }
        }
        const std::wstring longPath = L"C:\\" + std::wstring(33000u, L'x') + L"\\uvsr-engine.exe";
        CheckParentPath(longPath);

        wchar_t native[32768]{};
        const DWORD nativeLength = GetModuleFileNameW(nullptr, native, DWORD(sizeof(native) / sizeof(native[0])));
        Require(nativeLength != 0u && nativeLength < sizeof(native) / sizeof(native[0]), "native module control failed");
        const auto expectedDirectory = fs::path(native).parent_path().native();
        WindowsPath directory;
        WindowsPathResult result{WindowsPathError::ModuleQuery, 123u};
        Require(GetExecutableDirectoryWide(directory, result) && result.error == WindowsPathError::None &&
            result.nativeCode == 0u && fs::path(directory.Data()).is_absolute() &&
            std::wstring_view(directory.Data(), directory.Size()) == expectedDirectory,
            "wide executable path lost its exact absolute directory");
        for (const auto error : {WindowsPathError::Allocation, WindowsPathError::ModuleQuery, WindowsPathError::ModuleTruncated})
        {
            ExpectPathFailure(error, [=](auto& output, auto& failure)
            {
                FailWindowsPathOnce(error);
                return GetExecutableDirectoryWide(output, failure);
            });
            Require(GetExecutableDirectoryWide(directory, result) &&
                std::wstring_view(directory.Data(), directory.Size()) == expectedDirectory,
                "module query did not recover after failure");
        }
        constexpr std::wstring_view input = L"C:\\bin\\uvsr-engine.exe";
        ExpectPathFailure(WindowsPathError::Allocation, [&](auto& output, auto& failure)
        {
            FailWindowsPathOnce(WindowsPathError::Allocation);
            return ExecutableDirectoryFromModulePath(input.data(), input.size(), output, failure);
        });
        CheckParentPath(input);
        ExpectPathFailure(WindowsPathError::Allocation, [](auto& output, auto& failure)
        {
            FailWindowsPathOnce(WindowsPathError::Allocation);
            return JoinWindowsRelativePath(output.Data(), L"child", output, failure);
        });
        Require(JoinWindowsRelativePath(L"C:\\bin", L"child", directory, result) &&
            JoinWindowsRelativePath(directory.Data(), L"next", directory, result) &&
            std::wstring_view(directory.Data(), directory.Size()) == L"C:\\bin\\child\\next",
            "relative join did not recover or preserve aliased input");
        Require(ExecutableDirectoryFromModulePath(directory.Data(), directory.Size(), directory, result) &&
            std::wstring_view(directory.Data(), directory.Size()) == L"C:\\bin\\child", "aliased parent extraction failed");

        const std::wstring_view invalidInputs[] = {L"", L"no-parent", {L"C:\\\0file", 8u}};
        for (const auto invalid : invalidInputs)
            ExpectPathFailure(WindowsPathError::InvalidPath, [&](auto& output, auto& failure)
                { return ExecutableDirectoryFromModulePath(invalid.data(), invalid.size(), output, failure); });
        ExpectPathFailure(WindowsPathError::InvalidPath, [](auto& output, auto& failure)
            { return ExecutableDirectoryFromModulePath(nullptr, 1u, output, failure); });
        ExpectPathFailure(WindowsPathError::InvalidPath, [](auto& output, auto& failure)
            { return ExecutableDirectoryFromModulePath(L"x", SIZE_MAX, output, failure); });
        ExpectPathFailure(WindowsPathError::InvalidPath, [](auto& output, auto& failure)
            { return ExecutableDirectoryFromModulePath(reinterpret_cast<const wchar_t*>(UINTPTR_MAX - 1u), 2u, output, failure); });
        for (const wchar_t* invalid : {static_cast<const wchar_t*>(nullptr), L"", L"\\rooted", L"/rooted", L"C:drive"})
            ExpectPathFailure(WindowsPathError::InvalidPath, [=](auto& output, auto& failure)
                { return JoinWindowsRelativePath(output.Data(), invalid, output, failure); });
        for (const wchar_t* invalid : {static_cast<const wchar_t*>(nullptr), L""})
            ExpectPathFailure(WindowsPathError::InvalidPath, [=](auto& output, auto& failure)
                { return JoinWindowsRelativePath(invalid, L"child", output, failure); });

        const wchar_t* owned = directory.Data();
        WindowsPath moved(std::move(directory));
        Require(moved.Data() == owned && directory.Size() == 0u && directory.Data()[0] == L'\0',
            "path move construction lost ownership");
        Require(GetExecutableDirectoryWide(directory, result), "cannot replace the move destination");
        directory = std::move(moved);
        Require(directory.Data() == owned && moved.Size() == 0u && moved.Data()[0] == L'\0',
            "path move assignment lost ownership");
        directory.Clear();
        directory.Clear();
        Require(directory.Size() == 0u && directory.Data()[0] == L'\0', "cleared path retained storage");
    }

    std::string Hash(std::string_view input)
    {
        Sha256Digest digest;
        Sha256Result result{ Sha256Stage::ReadFile, 123u };
        Require(Sha256(input.data(), input.size(), digest, result), "cannot hash memory");
        Require(result.stage == Sha256Stage::None && !result.nativeCode,
            "successful memory hash retained a stale error");
        return digest.text;
    }

    std::string HashFile(const fs::path& path)
    {
        Sha256Digest digest;
        Sha256Result result{ Sha256Stage::ReadFile, 123u };
        Require(Sha256File(path.c_str(), digest, result), "cannot hash file");
        Require(result.stage == Sha256Stage::None && !result.nativeCode,
            "successful file hash retained a stale error");
        return digest.text;
    }

    template<typename Operation>
    void ExpectFault(Sha256Stage stage, Operation&& operation)
    {
        Sha256Digest digest;
        std::memset(digest.text, 'z', sizeof(digest.text));
        Sha256Result result{ Sha256Stage::None, 0u };
        Require(!operation(digest, result), "injected SHA-256 failure was not observed");
        Require(result.stage == stage && result.nativeCode != 0u, "SHA-256 failure lost its stage attribution");
        for (char byte : digest.text)
            Require(byte == 'z', "failed SHA-256 changed the output digest");
    }

    void Hashes()
    {
        constexpr std::string_view emptyHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
        constexpr std::string_view abcHash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        Require(Hash("") == emptyHash && Hash("abc") == abcHash, "standard SHA-256 known answers changed");
        Sha256Digest digest;
        Sha256Result result;
        Require(Sha256(nullptr, 0u, digest, result) && digest.text == emptyHash,
            "null empty input changed the empty digest");
        ExpectFault(Sha256Stage::InvalidInput, [](auto& output, auto& error)
            { return Sha256(nullptr, 1u, output, error); });
        ExpectFault(Sha256Stage::InvalidInput, [](auto& output, auto& error)
            { return Sha256(reinterpret_cast<const void*>(UINTPTR_MAX), 2u, output, error); });
        for (const wchar_t* invalidPath : {static_cast<const wchar_t*>(nullptr), L""})
            ExpectFault(Sha256Stage::InvalidInput, [=](auto& output, auto& error)
                { return Sha256File(invalidPath, output, error); });
        for (auto stage : {Sha256Stage::OpenProvider, Sha256Stage::CreateHash,
                Sha256Stage::HashData, Sha256Stage::FinishHash})
        {
            ExpectFault(stage, [stage](auto& output, auto& error)
                { return Sha256("abc", 3u, output, error, {stage, 1}); });
            Require(Hash("abc") == abcHash, "injected crypto failure contaminated the next digest");
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
            const std::string expected = Hash(contents);
            Require(expected == "323b6294511407904c0cd621d4e0740aefea916bdbab4b92bb667a5d2bf3515e",
                "SHA-256 boundary input differs from the independent known digest");
            Require(HashFile(path) == expected, "streaming and in-memory SHA-256 differ");
            const Sha256Fault faults[] = {
                {Sha256Stage::OpenFile, 1}, {Sha256Stage::ReadFile, 2}, {Sha256Stage::OpenProvider, 1},
                {Sha256Stage::CreateHash, 1}, {Sha256Stage::HashData, 2}, {Sha256Stage::FinishHash, 1},
                {Sha256Stage::AllocateReadBuffer, 1}, {Sha256Stage::CloseFile, 1}
            };
            for (const auto fault : faults)
            {
                ExpectFault(fault.stage, [&](auto& output, auto& error)
                    { return Sha256File(path.c_str(), output, error, fault); });
                fs::rename(path, moved);
                fs::rename(moved, path);
                Require(HashFile(path) == expected, "file hash did not release its handle and recover after failure");
            }
            ExpectFault(Sha256Stage::OpenFile, [&](auto& output, auto& error)
                { return Sha256File(moved.c_str(), output, error); });
            const HANDLE blocked = CreateFileW(path.c_str(), GENERIC_READ, 0u, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(blocked != INVALID_HANDLE_VALUE, "cannot hold the SHA-256 sharing fixture");
            std::memset(digest.text, 'z', sizeof(digest.text));
            const bool accepted = Sha256File(path.c_str(), digest, result);
            const bool closed = CloseHandle(blocked) != FALSE;
            Require(!accepted && closed && result.stage == Sha256Stage::OpenFile &&
                result.nativeCode == ERROR_SHARING_VIOLATION, "SHA-256 lost the real sharing failure");
            for (char byte : digest.text)
                Require(byte == 'z', "native open failure changed the digest");
            Require(HashFile(path) == expected, "SHA-256 sharing retry failed");
            {
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
                Require(bool(output), "cannot prepare empty SHA-256 file");
            }
            Require(HashFile(path) == emptyHash, "empty file differs from the standard empty digest");
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
        CommandLineBoundaries();
        Require(TestRestartCommandLineOwnership(), "restart ownership checks failed");
        SnapshotCodeValidation();
#if defined(UVSR_BUILD_TESTING)
        SettingsSnapshotCatalogPaths catalogs;
        SettingsSnapshotError catalogError;
        Require(GetDefaultSettingsSnapshotCatalogPaths("0007", SettingsSnapshotCatalogLocation::ExecutableState,
            catalogs, catalogError), "cannot find the developer snapshot catalog");
        WindowsPath directory;
        WindowsPathResult pathResult;
        Require(GetExecutableDirectoryWide(directory, pathResult), "cannot query the developer catalog directory");
        Require(catalogs.Count() == 1 &&
            fs::path(catalogs.Path(0)).parent_path() == fs::path(directory.Data()) / "state",
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
