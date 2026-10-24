#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "file_write.h"
#include "file_bytes.h"
#include "windows_executable_path.h"

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
        if (!accepted) { fprintf(stderr, "file write: %s\n", message); exit(1); }
    }
    WindowsPath Join(const wchar_t* base, const wchar_t* suffix)
    {
        WindowsPath path;
        WindowsPathResult error;
        Require(JoinWindowsRelativePath(base, suffix, path, error), "cannot join test path");
        return path;
    }
    void Seed(const wchar_t* path, const void* bytes, DWORD size)
    {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(file != INVALID_HANDLE_VALUE, "seed exists or cannot be created");
        DWORD written = 0;
        Require(WriteFile(file, bytes, size, &written, nullptr) && written == size, "cannot write seed");
        Require(CloseHandle(file) != 0, "cannot close seed");
    }
    void Equal(const wchar_t* path, const void* bytes, size_t size)
    {
        FileBytes actual;
        FileReadResult error;
        Require(ReadFileBytes(path, UINT64_MAX, actual, error), "cannot read saved file");
        Require(actual.Size() == size && (!size || memcmp(actual.Data(), bytes, size) == 0), "file bytes changed");
    }
    void Missing(const wchar_t* path)
    {
        Require(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES &&
            (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND), "unexpected output file");
    }
    WindowsPath DefaultRoot()
    {
        const DWORD needed = GetTempPathW(0, nullptr);
        Require(needed > 0 && needed < 32768, "cannot measure test temporary path");
        auto* text = static_cast<wchar_t*>(malloc((size_t(needed) + 1) * sizeof(wchar_t)));
        Require(text != nullptr, "cannot allocate test path");
        const DWORD copied = GetTempPathW(needed + 1, text);
        Require(copied > 0 && copied <= needed, "temporary path changed while copying");
        wchar_t name[80];
        Require(swprintf_s(name, L"uvsr-file-write-%lu-%llu", GetCurrentProcessId(),
            static_cast<unsigned long long>(GetTickCount64())) > 0, "cannot format test name");
        auto root = Join(text, name);
        free(text);
        Require(CreateDirectoryW(root.Data(), nullptr) != 0, "test root already exists or cannot be created");
        return root;
    }
}

int wmain(int argc, wchar_t** argv)
{
    Require(argc == 1 || argc == 2, "expected at most one fresh scratch root");
    WindowsPath automatic;
    if (argc == 1) automatic = DefaultRoot();
    const wchar_t* root = argc == 2 ? argv[1] : automatic.Data();
    const DWORD rootAttributes = GetFileAttributesW(root);
    Require(rootAttributes != INVALID_FILE_ATTRIBUTES && (rootAttributes & FILE_ATTRIBUTE_DIRECTORY), "scratch root must exist");
    FileWriteResult result;
    const unsigned char first[] = {0, 1, 255, 0, 10, 13};
    const char second[] = "retained tail";
    const FileWriteSpan spans[] = {{first, sizeof(first)}, {nullptr, 0}, {second, sizeof(second) - 1}};
    unsigned char expected[sizeof(first) + sizeof(second) - 1];
    memcpy(expected, first, sizeof(first));
    memcpy(expected + sizeof(first), second, sizeof(second) - 1);
    auto basic = Join(root, L"basic.bin");
    Require(WriteFileBytesAtomically(basic.Data(), spans, 3, result), "initial chunked write failed");
    Require(result.error == FileWriteError::None && !result.systemCode && !result.cleanupCode, "success result not cleared");
    Equal(basic.Data(), expected, sizeof(expected));
    SetFileWriteTestLimits(1, SIZE_MAX, false, false, false);
    Require(WriteFileBytesAtomically(basic.Data(), spans, 3, result), "bounded transfers failed");
    ClearFileWriteTestFailures();
    Equal(basic.Data(), expected, sizeof(expected));
    Require(WriteFileBytesAtomically(basic.Data(), nullptr, 0, result), "empty write failed");
    Equal(basic.Data(), nullptr, 0);

    auto nested = Join(root, L"one/two/three/catalog.bin");
    Require(WriteFileBytesAtomically(nested.Data(), spans, 3, result), "nested parents failed");
    Equal(nested.Data(), expected, sizeof(expected));
    auto dotted = Join(root, L"one/./extra/../dotted.bin");
    Require(WriteFileBytesAtomically(dotted.Data(), spans, 3, result), "dot parents failed");
    Equal(dotted.Data(), expected, sizeof(expected));
    auto unicode = Join(root, L"caf\u00e9-\u6811/file-\u2603.bin");
    Require(WriteFileBytesAtomically(unicode.Data(), spans, 3, result), "Unicode path failed");
    Equal(unicode.Data(), expected, sizeof(expected));

    const char prior[] = "prior destination";
    for (unsigned fault = 0; fault < 5; ++fault)
    {
        wchar_t name[48];
        Require(swprintf_s(name, L"fault-%u.bin", fault) > 0, "cannot format fault path");
        auto path = Join(root, name);
        Seed(path.Data(), prior, sizeof(prior));
        if (fault == 0) FailFileWriteAllocationAfter(0);
        else SetFileWriteTestLimits(2, fault == 1 ? 1 : SIZE_MAX, fault == 2, fault == 3, fault == 4);
        const bool accepted = WriteFileBytesAtomically(path.Data(), spans, 3, result);
        ClearFileWriteTestFailures();
        const FileWriteError failures[] = {FileWriteError::OutOfMemory, FileWriteError::Write,
            FileWriteError::Flush, FileWriteError::Close, FileWriteError::Publish};
        Require(!accepted && result.error == failures[fault] && !result.cleanupCode, "injected failure not reported");
        Equal(path.Data(), prior, sizeof(prior));
        wchar_t temporaryName[64];
        Require(swprintf_s(temporaryName, L"%ls.tmp", name) > 0, "cannot format temporary name");
        auto temporary = Join(root, temporaryName);
        Missing(temporary.Data());
        Require(WriteFileBytesAtomically(path.Data(), spans, 3, result), "retry after failure failed");
        Equal(path.Data(), expected, sizeof(expected));
    }
    auto noParents = Join(root, L"allocation/child/grand/file.bin");
    FailFileWriteAllocationAfter(1);
    Require(!WriteFileBytesAtomically(noParents.Data(), spans, 3, result) &&
        result.error == FileWriteError::OutOfMemory, "parent buffer allocation failure not reported");
    ClearFileWriteTestFailures();
    Missing(noParents.Data());
    Require(WriteFileBytesAtomically(noParents.Data(), spans, 3, result), "parent allocation retry failed");
    auto pathFault = Join(root, L"path-fault.bin");
    FailWindowsPathOnce(WindowsPathError::Allocation);
    Require(!WriteFileBytesAtomically(pathFault.Data(), spans, 3, result) &&
        result.error == FileWriteError::OutOfMemory, "path owner allocation failure not reported");
    Missing(pathFault.Data());
    Require(WriteFileBytesAtomically(pathFault.Data(), spans, 3, result), "path owner retry failed");

    auto invalid = Join(root, L"invalid.bin");
    Require(!WriteFileBytesAtomically(nullptr, spans, 3, result) && result.error == FileWriteError::InvalidPath, "null path accepted");
    Require(!WriteFileBytesAtomically(L"", spans, 3, result) && result.error == FileWriteError::InvalidPath, "empty path accepted");
    Require(!WriteFileBytesAtomically(invalid.Data(), nullptr, 1, result) && result.error == FileWriteError::InvalidInput, "null spans accepted");
    const FileWriteSpan bad[] = {{nullptr, 1}};
    Require(!WriteFileBytesAtomically(invalid.Data(), bad, 1, result) && result.error == FileWriteError::InvalidInput, "null bytes accepted");
    const FileWriteSpan wrap[] = {{reinterpret_cast<const void*>(UINTPTR_MAX - 2), 8}};
    Require(!WriteFileBytesAtomically(invalid.Data(), wrap, 1, result) && result.error == FileWriteError::InvalidInput, "wrapping span accepted");
    Require(!WriteFileBytesAtomically(invalid.Data(), reinterpret_cast<const FileWriteSpan*>(UINTPTR_MAX - 2), 1, result) &&
        result.error == FileWriteError::InvalidInput, "wrapping span array accepted");
    const FileWriteSpan oversized[] = {{reinterpret_cast<const void*>(1), size_t(PTRDIFF_MAX)}, {reinterpret_cast<const void*>(1), 1}};
    Require(!WriteFileBytesAtomically(invalid.Data(), oversized, 2, result) &&
        result.error == FileWriteError::TooLarge, "aggregate file size overflow accepted");
    Missing(invalid.Data());
    auto invalidName = Join(root, L"invalid*.bin");
    Require(!WriteFileBytesAtomically(invalidName.Data(), spans, 3, result) && result.error == FileWriteError::Create, "invalid native filename accepted");
    auto blocking = Join(root, L"blocking-parent");
    Seed(blocking.Data(), prior, sizeof(prior));
    auto blocked = Join(blocking.Data(), L"file.bin");
    Require(!WriteFileBytesAtomically(blocked.Data(), spans, 3, result) && result.error == FileWriteError::Directory, "file parent accepted");
    Equal(blocking.Data(), prior, sizeof(prior));

    auto occupied = Join(root, L"occupied.bin");
    auto occupiedTemp = Join(root, L"occupied.bin.tmp");
    auto occupiedDirectory = Join(root, L"occupied.bin.tmp.1");
    Seed(occupiedTemp.Data(), prior, sizeof(prior));
    Require(CreateDirectoryW(occupiedDirectory.Data(), nullptr) != 0, "cannot create occupied temporary directory");
    Require(WriteFileBytesAtomically(occupied.Data(), spans, 3, result), "occupied temporary siblings blocked a write");
    Equal(occupiedTemp.Data(), prior, sizeof(prior));
    Require(GetFileAttributesW(occupiedDirectory.Data()) & FILE_ATTRIBUTE_DIRECTORY, "preexisting temporary directory changed");
    Equal(occupied.Data(), expected, sizeof(expected));
    auto retiredTemp = Join(root, L"occupied.bin.tmp.2");
    Missing(retiredTemp.Data());

    auto alias = Join(root, L"result-alias.bin");
    result = {FileWriteError::Publish, 4321, 8765};
    unsigned char previousResult[sizeof(result)];
    memcpy(previousResult, &result, sizeof(result));
    const FileWriteSpan borrowedResult{&result, sizeof(result)};
    Require(WriteFileBytesAtomically(alias.Data(), &borrowedResult, 1, result), "result-backed byte input failed");
    Equal(alias.Data(), previousResult, sizeof(previousResult));
    Require(result.error == FileWriteError::None, "result-backed success not cleared");
    printf("file write: %zu checks passed\n", checks);
    return 0;
}
