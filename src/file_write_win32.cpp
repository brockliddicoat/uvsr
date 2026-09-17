#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "file_write.h"
#include "windows_executable_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_FILE_WRITE_TEST_HOOKS)
        thread_local size_t allocationsLeft = SIZE_MAX;
        thread_local size_t maximumTransfer = SIZE_MAX;
        thread_local size_t transfersLeft = SIZE_MAX;
        thread_local bool failFlush = false;
        thread_local bool failClose = false;
        thread_local bool failPublish = false;
#endif
        bool Fail(FileWriteResult& result, FileWriteError error, DWORD code = 0) noexcept
        {
            result = {error, code, 0};
            return false;
        }
        wchar_t* Allocate(size_t characters, FileWriteResult& result) noexcept
        {
            if (characters > size_t(PTRDIFF_MAX) / sizeof(wchar_t))
            {
                (void)Fail(result, FileWriteError::TooLarge);
                return nullptr;
            }
#if defined(UVSR_FILE_WRITE_TEST_HOOKS)
            if (!allocationsLeft)
            {
                (void)Fail(result, FileWriteError::OutOfMemory);
                return nullptr;
            }
            if (allocationsLeft != SIZE_MAX) --allocationsLeft;
#endif
            auto* text = static_cast<wchar_t*>(malloc(characters * sizeof(wchar_t)));
            if (!text) (void)Fail(result, FileWriteError::OutOfMemory);
            return text;
        }
        struct PathText
        {
            wchar_t* data = nullptr;
            ~PathText() noexcept { free(data); }
        };
        bool Separator(wchar_t c) noexcept { return c == L'\\' || c == L'/'; }
        bool ExistingDirectory(const wchar_t* path, DWORD& error) noexcept
        {
            DWORD attributes = GetFileAttributesW(path);
            const DWORD attributeError = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : 0;
            if (attributeError == ERROR_SHARING_VIOLATION)
            {
                // retain filesystem status queries that can succeed through directory enumeration.
                WIN32_FIND_DATAW found;
                const HANDLE search = FindFirstFileW(path, &found);
                if (search == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
                if (!FindClose(search)) { error = GetLastError(); return false; }
                attributes = found.dwFileAttributes;
            }
            if (attributes != INVALID_FILE_ATTRIBUTES)
            {
                if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
                {
                    HANDLE handle = CreateFileW(path, FILE_READ_ATTRIBUTES,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                    if (handle == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
                    BY_HANDLE_FILE_INFORMATION info{};
                    const BOOL measured = GetFileInformationByHandle(handle, &info);
                    const DWORD queryError = measured ? 0 : GetLastError();
                    const BOOL closed = CloseHandle(handle);
                    const DWORD closeError = closed ? 0 : GetLastError();
                    if (!measured || !closed) { error = measured ? closeError : queryError; return false; }
                    attributes = info.dwFileAttributes;
                }
                error = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : ERROR_DIRECTORY;
                return error == 0;
            }
            error = attributeError;
            return false;
        }
        bool DirectoryReady(const wchar_t* path, DWORD& error) noexcept
        {
            if (ExistingDirectory(path, error)) return true;
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return false;
            if (CreateDirectoryW(path, nullptr)) { error = 0; return true; }
            error = GetLastError();
            if (error == ERROR_ALREADY_EXISTS)
                return ExistingDirectory(path, error);
            return false;
        }
        bool CreateParents(const wchar_t* path, size_t length, FileWriteResult& result) noexcept
        {
            WindowsPath parent;
            WindowsPathResult pathError;
            if (!ExecutableDirectoryFromModulePath(path, length, parent, pathError))
                return Fail(result, pathError.error == WindowsPathError::Allocation
                    ? FileWriteError::OutOfMemory : FileWriteError::Directory, pathError.nativeCode);
            DWORD error = 0;
            if (DirectoryReady(parent.Data(), error)) return true;
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                return Fail(result, FileWriteError::Directory, error);
            PathText buffer{Allocate(parent.Size() + 1, result)};
            if (!buffer.data) return false;
            wmemcpy(buffer.data, parent.Data(), parent.Size() + 1);
            size_t end = parent.Size();
            for (;;)
            {
                size_t previous = end;
                while (previous && !Separator(buffer.data[previous - 1])) --previous;
                while (previous && Separator(buffer.data[previous - 1])) --previous;
                if (!previous || previous >= end) return Fail(result, FileWriteError::Directory, error);
                end = previous;
                buffer.data[end] = L'\0';
                if (DirectoryReady(buffer.data, error)) break;
                if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                    return Fail(result, FileWriteError::Directory, error);
            }
            // restore one prefix at a time after reaching an existing ancestor.
            for (size_t index = end; index < parent.Size(); ++index)
            {
                buffer.data[index] = parent.Data()[index];
                if (index + 1 == parent.Size() ||
                    (!Separator(parent.Data()[index]) && Separator(parent.Data()[index + 1])))
                {
                    buffer.data[index + 1] = L'\0';
                    if (!DirectoryReady(buffer.data, error)) return Fail(result, FileWriteError::Directory, error);
                }
            }
            return true;
        }
        bool ValidSpans(const FileWriteSpan* spans, size_t count, FileWriteResult& result) noexcept
        {
            if ((count && !spans) || count > size_t(PTRDIFF_MAX) / sizeof(FileWriteSpan) ||
                count > (UINTPTR_MAX - reinterpret_cast<uintptr_t>(spans)) / sizeof(FileWriteSpan))
                return Fail(result, FileWriteError::InvalidInput);
            uint64_t total = 0;
            for (size_t index = 0; index < count; ++index)
            {
                const auto span = spans[index];
                if ((span.size && !span.data) || span.size > size_t(PTRDIFF_MAX) ||
                    span.size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(span.data))
                    return Fail(result, FileWriteError::InvalidInput);
                if (uint64_t(span.size) > uint64_t(INT64_MAX) - total)
                    return Fail(result, FileWriteError::TooLarge);
                total += span.size;
            }
            return true;
        }
        bool WriteSpans(HANDLE file, const FileWriteSpan* spans, size_t count,
            FileWriteResult& result) noexcept
        {
            for (size_t index = 0; index < count; ++index)
            {
                const auto span = spans[index];
                size_t offset = 0;
                while (offset < span.size)
                {
                    const size_t remaining = span.size - offset;
                    DWORD wanted = remaining > 1024 * 1024 ? 1024 * 1024 : DWORD(remaining);
#if defined(UVSR_FILE_WRITE_TEST_HOOKS)
                    if (!transfersLeft) return Fail(result, FileWriteError::Write, ERROR_WRITE_FAULT);
                    if (transfersLeft != SIZE_MAX) --transfersLeft;
                    if (maximumTransfer < wanted) wanted = DWORD(maximumTransfer);
#endif
                    if (!wanted) return Fail(result, FileWriteError::Write, ERROR_WRITE_FAULT);
                    DWORD written = 0;
                    if (!WriteFile(file, static_cast<const char*>(span.data) + offset, wanted, &written, nullptr))
                        return Fail(result, FileWriteError::Write, GetLastError());
                    if (written != wanted) return Fail(result, FileWriteError::Write, ERROR_WRITE_FAULT);
                    offset += written;
                }
            }
            if (!FlushFileBuffers(file)) return Fail(result, FileWriteError::Flush, GetLastError());
#if defined(UVSR_FILE_WRITE_TEST_HOOKS)
            if (failFlush) return Fail(result, FileWriteError::Flush, ERROR_WRITE_FAULT);
#endif
            return true;
        }
    }

    bool PrepareFileParentDirectories(const wchar_t* path, FileWriteResult& result) noexcept
    {
        result = {};
        if (!path || !*path) return Fail(result, FileWriteError::InvalidPath);
        const size_t length = wcslen(path);
        if (length >= size_t(PTRDIFF_MAX) / sizeof(wchar_t))
            return Fail(result, FileWriteError::TooLarge);
        return CreateParents(path, length, result);
    }

    bool WriteFileBytesAtomically(const wchar_t* path, const FileWriteSpan* spans,
        size_t count, FileWriteResult& result) noexcept
    {
        if (!path || !*path) return Fail(result, FileWriteError::InvalidPath);
        if (!ValidSpans(spans, count, result)) return false;
        const size_t length = wcslen(path);
        constexpr size_t suffixCapacity = 27; // .tmp, decimal suffix and terminator.
        if (length > size_t(PTRDIFF_MAX) / sizeof(wchar_t) - suffixCapacity)
            return Fail(result, FileWriteError::TooLarge);
        PathText temporary{Allocate(length + suffixCapacity, result)};
        if (!temporary.data) return false;
        wmemcpy(temporary.data, path, length);
        wmemcpy(temporary.data + length, L".tmp", 5);
        if (!CreateParents(path, length, result)) return false;
        HANDLE file = INVALID_HANDLE_VALUE;
        uint64_t suffix = 0;
        for (;;)
        {
            file = CreateFileW(temporary.data, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
            if (file != INVALID_HANDLE_VALUE) break;
            const DWORD error = GetLastError();
            const bool occupied = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ||
                (error == ERROR_ACCESS_DENIED && GetFileAttributesW(temporary.data) != INVALID_FILE_ATTRIBUTES);
            if (!occupied) return Fail(result, FileWriteError::Create, error);
            if (suffix == UINT64_MAX) return Fail(result, FileWriteError::Create, ERROR_FILE_EXISTS);
            ++suffix;
            if (swprintf_s(temporary.data + length + 4, suffixCapacity - 4, L".%llu",
                static_cast<unsigned long long>(suffix)) < 0)
                return Fail(result, FileWriteError::TooLarge);
        }
        bool complete = WriteSpans(file, spans, count, result);
        const BOOL closed = CloseHandle(file);
        const DWORD closeCode = closed ? 0 : GetLastError();
        if (complete && !closed) complete = Fail(result, FileWriteError::Close, closeCode);
#if defined(UVSR_FILE_WRITE_TEST_HOOKS)
        if (complete && failClose) complete = Fail(result, FileWriteError::Close, ERROR_WRITE_FAULT);
        if (complete && failPublish) complete = Fail(result, FileWriteError::Publish, ERROR_WRITE_FAULT);
#endif
        if (complete && !MoveFileExW(temporary.data, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            complete = Fail(result, FileWriteError::Publish, GetLastError());
        if (!complete && !DeleteFileW(temporary.data)) result.cleanupCode = GetLastError();
        if (complete) result = {};
        return complete;
    }

#if defined(UVSR_FILE_WRITE_TEST_HOOKS)
    void FailFileWriteAllocationAfter(size_t count) noexcept { allocationsLeft = count; }
    void SetFileWriteTestLimits(size_t limit, size_t transfers, bool flush, bool close, bool publish) noexcept
    {
        maximumTransfer = limit; transfersLeft = transfers;
        failFlush = flush; failClose = close; failPublish = publish;
    }
    void ClearFileWriteTestFailures() noexcept
    {
        allocationsLeft = SIZE_MAX; maximumTransfer = SIZE_MAX; transfersLeft = SIZE_MAX;
        failFlush = false; failClose = false; failPublish = false;
    }
#endif
}
