#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "file_bytes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

bool TestNoiseAssetOwnership(const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*, const char*) noexcept;

namespace
{
    using namespace uvsr;
    void Check(bool value, const char* message)
    {
        if (!value) { fprintf(stderr, "%s (Windows %lu)\n", message, GetLastError()); exit(1); }
    }
    struct Fixture
    {
        wchar_t root[512]{}, large[600]{}, small[600]{}, empty[600]{}, missing[600]{};
        Fixture()
        {
            wchar_t base[256]{};
            const DWORD count = GetTempPathW(256, base);
            Check(count > 0 && count < 256, "test temporary path failed");
            Check(swprintf_s(root, L"%suvsr-file-bytes-%lu-%llu", base, GetCurrentProcessId(), GetTickCount64()) > 0,
                "test directory path exceeded capacity");
            Check(CreateDirectoryW(root, nullptr), "test directory already exists or cannot be created");
            Check(swprintf_s(large, L"%s/large-\u03b2-\U0001f680.bin", root) > 0 &&
                swprintf_s(small, L"%s/small.bin", root) > 0 && swprintf_s(empty, L"%s/empty.bin", root) > 0 &&
                swprintf_s(missing, L"%s/missing.bin", root) > 0, "test file path exceeded capacity");
        }
        ~Fixture()
        {
            Check(DeleteFileW(large) && DeleteFileW(small) && DeleteFileW(empty) && RemoveDirectoryW(root),
                "owned test fixture could not be removed");
        }
    };
    void Write(const wchar_t* path, const char* data, DWORD size)
    {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "test file creation failed");
        DWORD written = 0;
        Check(WriteFile(file, data, size, &written, nullptr) && written == size && CloseHandle(file),
            "test file write failed");
    }
    void Preserved(const FileBytes& bytes, const char* previous, const char* expected, size_t count)
    {
        Check(bytes.Data() == previous && bytes.Size() == count && memcmp(bytes.Data(), expected, count) == 0 &&
            bytes.Data()[count] == '\0', "failed file read changed the published bytes or view");
    }
}

int main()
{
    Fixture fixture;
    constexpr DWORD count = 2 * 1024 * 1024 + 19;
    char* source = static_cast<char*>(malloc(count));
    Check(source != nullptr, "test data allocation failed");
    for (size_t i = 0; i < count; ++i) source[i] = char(i % 251);
    Write(fixture.large, source, count);
    Write(fixture.small, source, 127);
    Write(fixture.empty, "", 0);

    FileBytes bytes;
    FileReadResult result;
    Check(ReadFileBytes(fixture.small, 127, bytes, result), "exact file bound was rejected");
    const char* previous = bytes.Data();
    Preserved(bytes, previous, source, 127);
    Check(!ReadFileBytes(fixture.large, count - 1, bytes, result) && result.error == FileReadError::TooLarge,
        "file size above the bound was accepted");
    Preserved(bytes, previous, source, 127);
    Check(!ReadFileBytes(fixture.missing, count, bytes, result) && result.error == FileReadError::Missing,
        "missing file category changed");
    Check(!ReadFileBytes(nullptr, count, bytes, result) && result.error == FileReadError::InvalidPath,
        "null path was accepted");
    Check(!ReadFileBytes(fixture.root, count, bytes, result) && result.error == FileReadError::NotRegular,
        "directory was accepted or lost its non-file category");
    Check(!ReadFileBytes(L"NUL", count, bytes, result) && result.error == FileReadError::NotRegular,
        "device was accepted or lost its non-file category");
    Check(!ReadFileBytesUtf8("\xff", count, bytes, result) && result.error == FileReadError::InvalidPath,
        "invalid UTF-8 path was accepted");
    Preserved(bytes, previous, source, 127);

    HANDLE writer = CreateFileW(fixture.large, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(writer != INVALID_HANDLE_VALUE, "test writer could not open the file");
    Check(!ReadFileBytes(fixture.large, count, bytes, result) && result.error == FileReadError::Open &&
        result.systemCode == ERROR_SHARING_VIOLATION, "reader allowed a concurrent writer");
    Check(CloseHandle(writer), "test writer did not close");
    Preserved(bytes, previous, source, 127);

    FailFileAllocationAfter(0);
    Check(!ReadFileBytes(fixture.large, count, bytes, result) && result.error == FileReadError::OutOfMemory,
        "failed byte allocation was not reported");
    ClearFileAllocationFailure();
    Preserved(bytes, previous, source, 127);
    char utf8[2400]{};
    Check(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, fixture.large, -1, utf8, sizeof(utf8), nullptr, nullptr) > 0,
        "test UTF-8 path conversion failed");
    for (size_t i = 0; i != 2; ++i)
    {
        FailFileAllocationAfter(i);
        Check(!ReadFileBytesUtf8(utf8, count, bytes, result) && result.error == FileReadError::OutOfMemory,
            "failed path or byte allocation was not reported");
        ClearFileAllocationFailure();
        Preserved(bytes, previous, source, 127);
    }
    for (size_t i = 0; i != 2; ++i)
    {
        SetFileReadTestLimits(SIZE_MAX, i, false);
        Check(!ReadFileBytes(fixture.large, count, bytes, result) && result.error == FileReadError::Read,
            "failed transfer published file bytes");
        ClearFileReadTestLimits();
        Preserved(bytes, previous, source, 127);
    }
    SetFileReadTestLimits(0, SIZE_MAX, false);
    Check(!ReadFileBytes(fixture.large, count, bytes, result) && result.error == FileReadError::Read,
        "zero-progress transfer was accepted");
    ClearFileReadTestLimits();
    Preserved(bytes, previous, source, 127);
    SetFileReadTestLimits(SIZE_MAX, SIZE_MAX, true);
    Check(!ReadFileBytes(fixture.large, count, bytes, result) && result.error == FileReadError::Close,
        "close failure published file bytes");
    ClearFileReadTestLimits();
    Preserved(bytes, previous, source, 127);

    SetFileReadTestLimits(17, SIZE_MAX, false);
    Check(ReadFileBytes(fixture.small, 127, bytes, result) && bytes.Size() == 127 && memcmp(bytes.Data(), source, 127) == 0,
        "positive partial transfers did not finish the file");
    ClearFileReadTestLimits();
    Check(ReadFileBytesUtf8(utf8, count, bytes, result) && bytes.Size() == count &&
        memcmp(bytes.Data(), source, count) == 0 && bytes.Data()[count] == '\0', "Unicode file retry changed exact bytes");
    previous = bytes.Data();
    FileBytes moved(static_cast<FileBytes&&>(bytes));
    Check(!bytes.Size() && !*bytes.Data() && moved.Data() == previous, "moving file bytes lost ownership");
    bytes = static_cast<FileBytes&&>(moved);
    bytes = static_cast<FileBytes&&>(bytes);
    Check(!moved.Size() && bytes.Data() == previous, "file byte move assignment lost ownership");
    Check(ReadFileBytes(fixture.empty, 0, bytes, result) && !bytes.Size() && !*bytes.Data(), "empty file read failed");
    Check(TestNoiseAssetOwnership(fixture.root, fixture.small, fixture.empty, fixture.missing, source),
        "noise and exact-file ownership checks failed");
    free(source);
    puts("file bytes: exact bounds, Unicode, sharing, partial reads, 3 allocation failures, 4 I/O failures, moves and retries passed");
    return 0;
}
