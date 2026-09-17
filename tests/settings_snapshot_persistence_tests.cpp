#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "settings_snapshot_persistence_win32.h"
#include "settings_snapshot.h"
#include "file_bytes.h"
#include "file_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

using namespace uvsr;
namespace
{
    size_t checks = 0;
    void Require(bool accepted, const char* message)
    {
        ++checks;
        if (!accepted) { fprintf(stderr, "snapshot persistence: %s\n", message); exit(1); }
    }
    WindowsPath Join(const wchar_t* root, const wchar_t* leaf)
    {
        WindowsPath path; WindowsPathResult error;
        Require(JoinWindowsRelativePath(root, leaf, path, error), "cannot join fixture path");
        return path;
    }
    WindowsPath AutomaticRoot()
    {
        const DWORD size = GetTempPathW(0, nullptr);
        Require(size > 0 && size < 32768, "cannot measure temporary path");
        auto* text = static_cast<wchar_t*>(malloc((size_t(size) + 1) * sizeof(wchar_t)));
        Require(text != nullptr, "cannot allocate temporary path");
        const DWORD copied = GetTempPathW(size + 1, text);
        Require(copied > 0 && copied <= size, "temporary path changed while reading");
        wchar_t name[80];
        Require(swprintf_s(name, L"uvsr-snapshot-write-%lu-%llu", GetCurrentProcessId(),
            static_cast<unsigned long long>(GetTickCount64())) > 0, "cannot format temporary name");
        auto path = Join(text, name); free(text);
        Require(CreateDirectoryW(path.Data(), nullptr) != 0, "temporary root must be new");
        return path;
    }
    void Seed(const wchar_t* path, std::string_view bytes)
    {
        Require(bytes.size() <= MAXDWORD, "seed is too large");
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(file != INVALID_HANDLE_VALUE, "seed already exists");
        DWORD written = 0;
        Require(WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size(), "cannot seed catalog");
        Require(CloseHandle(file) != 0, "cannot close seed");
    }
    void Equal(const wchar_t* path, std::string_view expected)
    {
        FileBytes bytes; FileReadResult error;
        Require(ReadFileBytes(path, UINT64_MAX, bytes, error) &&
            std::string_view(bytes.Data(), bytes.Size()) == expected, "existing catalog bytes changed");
    }
    void Decodes(const wchar_t* path, std::string_view code, std::string_view canonical)
    {
        SettingsSnapshotMatches matches; SettingsSnapshotError error;
        Require(ReadMatchingSettingsSnapshots(path, code, matches, error) &&
            matches.Count() == 1 && matches.Text(0) == canonical, "persisted payload does not match independent decoder");
    }
}
int wmain(int argc, wchar_t** argv)
{
    Require(argc == 1 || argc == 2, "expected at most one fresh scratch directory");
    WindowsPath automatic;
    if (argc == 1) automatic = AutomaticRoot();
    const wchar_t* root = argc == 2 ? argv[1] : automatic.Data();
    const DWORD attributes = GetFileAttributesW(root);
    Require(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY), "scratch root must exist");
    constexpr std::string_view canonical = "a=one\\ntwo\nb=three\\tfour\n";
    const auto code = BuildSettingsSnapshotCode(canonical);
    constexpr std::string_view prior = "retained prior catalog text\n";
    SettingsSnapshotError error;
    for (unsigned fault = 0; fault < 9; ++fault)
    {
        wchar_t filename[48];
        Require(swprintf_s(filename, L"fault-%u.txt", fault) > 0, "cannot format fixture name");
        auto path = Join(root, filename);
        Seed(path.Data(), prior);
        HANDLE locked = INVALID_HANDLE_VALUE;
        if (fault == 0) FailFileAllocationAfter(0);
        else if (fault == 1) SetFileReadTestLimits(1, 1, false);
        else if (fault == 2) SetFileReadTestLimits(SIZE_MAX, SIZE_MAX, true);
        else if (fault == 3) json::FailAllocationAfter(0);
        else if (fault == 4) FailFileWriteAllocationAfter(0);
        else if (fault == 8)
        {
            locked = CreateFileW(path.Data(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(locked != INVALID_HANDLE_VALUE, "cannot lock owned fixture");
        }
        else SetFileWriteTestLimits(SIZE_MAX, SIZE_MAX, fault == 5, fault == 6, fault == 7);
        const bool accepted = PersistSettingsSnapshotCatalog(path.Data(), code.View(), canonical, error);
        ClearFileAllocationFailure(); ClearFileReadTestLimits(); json::ClearAllocationFailure(); ClearFileWriteTestFailures();
        if (locked != INVALID_HANDLE_VALUE) Require(CloseHandle(locked) != 0, "cannot release fixture lock");
        const bool allocation = fault == 0 || fault == 3 || fault == 4;
        Require(!accepted && error.code == (allocation ? SettingsSnapshotErrorCode::OutOfMemory : SettingsSnapshotErrorCode::Catalog), "failure category changed");
        if (fault == 8) Require(error.nativeCode == ERROR_SHARING_VIOLATION, "failed read-open was treated as absent");
        Require(!error.cleanupCode, "owned temporary cleanup failed");
        Equal(path.Data(), prior);
        Require(PersistSettingsSnapshotCatalog(path.Data(), code.View(), canonical, error), "retry failed");
        Decodes(path.Data(), code.View(), canonical);
    }
    auto idempotent = Join(root, L"idempotent.txt");
    Require(PersistSettingsSnapshotCatalog(idempotent.Data(), code.View(), canonical, error), "cannot prepare idempotent catalog");
    FileBytes before; FileReadResult readError;
    Require(ReadFileBytes(idempotent.Data(), UINT64_MAX, before, readError), "cannot preserve idempotent bytes");
    json::FailAllocationAfter(0);
    const bool formatted = PersistSettingsSnapshotCatalog(idempotent.Data(), code.View(), canonical, error);
    json::ClearAllocationFailure();
    Require(!formatted && error.code == SettingsSnapshotErrorCode::OutOfMemory, "idempotence bypassed section formatting failure");
    Equal(idempotent.Data(), {before.Data(), before.Size()});
    Require(PersistSettingsSnapshotCatalog(idempotent.Data(), code.View(), canonical, error), "idempotent retry failed");
    Equal(idempotent.Data(), {before.Data(), before.Size()});

    json::EncodedText section;
    Require(FormatSettingsSnapshotCatalogSection(code.View(), canonical, section, error), "cannot prepare section");
    const char* previous = section.Data();
    json::FailAllocationAfter(0);
    const bool built = FormatSettingsSnapshotCatalogSection(code.View(), canonical, section, error);
    json::ClearAllocationFailure();
    Require(!built && error.code == SettingsSnapshotErrorCode::OutOfMemory && section.Data() == previous, "failed section replaced its output");
    error.detail = json::EncodedText([](json::OutputWriter& writer, const void* context) noexcept {
        const auto bytes = *static_cast<const std::string_view*>(context);
        return writer.Raw({bytes.data(), bytes.size()});
    }, &canonical);
    Require(error.detail.IsValid(), "cannot seed diagnostic-owned input");
    const std::string_view borrowed{error.detail.Data(), error.detail.Size()};
    auto aliased = Join(root, L"diagnostic-alias.txt");
    Require(PersistSettingsSnapshotCatalog(aliased.Data(), code.View(), borrowed, error), "diagnostic-owned input was released early");
    Decodes(aliased.Data(), code.View(), canonical);
    Require(!PersistSettingsSnapshotCatalog(L"", code.View(), canonical, error) && error.code == SettingsSnapshotErrorCode::Path, "empty destination accepted");
    Require(!PersistSettingsSnapshotCatalog(aliased.Data(), {reinterpret_cast<const char*>(UINTPTR_MAX - 2), 8}, canonical, error) &&
        error.code == SettingsSnapshotErrorCode::InvalidInput, "wrapping code range accepted");

    WindowsPath output = Join(root, L"unchanged-path.txt");
    const wchar_t* previousPath = output.Data();
    const wchar_t* missingRoots[] = {nullptr, L""};
    for (const wchar_t* missing : missingRoots)
    {
        SetSettingsSnapshotWriteRootForTests(missing);
        const bool located = GetSettingsSnapshotCatalogWritePath(SettingsSnapshotCatalogLocation::Installed, output, error);
        ClearSettingsSnapshotWriteRootForTests();
        Require(!located && error.code == SettingsSnapshotErrorCode::Path && output.Data() == previousPath, "failed known-folder lookup changed output");
    }
    auto known = Join(root, L"known-folder-\u6811");
    SetSettingsSnapshotWriteRootForTests(known.Data());
    Require(GetSettingsSnapshotCatalogWritePath(SettingsSnapshotCatalogLocation::Installed, output, error), "stubbed known-folder lookup failed");
    ClearSettingsSnapshotWriteRootForTests();
    auto knownDirectory = Join(known.Data(), L"UVSR");
    wchar_t filename[] = L"settings-snapshots-v0000.txt";
    wchar_t* version = wcsstr(filename, L"0000");
    Require(version != nullptr, "expected filename template is invalid");
    for (size_t index = 0; index < 4; ++index) version[index] = wchar_t(code.text[index]);
    auto expectedPath = Join(knownDirectory.Data(), filename);
    Require(wcscmp(output.Data(), expectedPath.Data()) == 0, "known-folder write path differs");
    previousPath = output.Data();
    SetSettingsSnapshotWriteRootForTests(known.Data());
    FailWindowsPathOnce(WindowsPathError::Allocation);
    const bool allocated = GetSettingsSnapshotCatalogWritePath(SettingsSnapshotCatalogLocation::Installed, output, error);
    ClearSettingsSnapshotWriteRootForTests();
    Require(!allocated && error.code == SettingsSnapshotErrorCode::OutOfMemory && output.Data() == previousPath, "write-path allocation failure changed output");
    const WindowsPathError pathFailures[] = {WindowsPathError::Allocation, WindowsPathError::ModuleQuery};
    for (auto failure : pathFailures)
    {
        FailWindowsPathOnce(failure);
        Require(!GetSettingsSnapshotCatalogWritePath(SettingsSnapshotCatalogLocation::ExecutableState, output, error) && output.Data() == previousPath,
            "failed executable lookup changed write path");
    }
    Require(GetSettingsSnapshotCatalogWritePath(SettingsSnapshotCatalogLocation::ExecutableState, output, error), "executable write path failed");
    WindowsPath executable; WindowsPathResult pathError;
    Require(GetExecutableDirectoryWide(executable, pathError), "cannot read executable directory");
    auto state = Join(executable.Data(), L"state");
    expectedPath = Join(state.Data(), filename);
    Require(wcscmp(output.Data(), expectedPath.Data()) == 0, "executable write path differs");
    printf("snapshot persistence: %zu checks passed\n", checks);
    return 0;
}
