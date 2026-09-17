#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "file_bytes.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_FILE_BYTES_TEST_HOOKS)
        thread_local size_t allocationsUntilFailure = SIZE_MAX;
        thread_local size_t maximumTransfer = SIZE_MAX;
        thread_local size_t transfersUntilFailure = SIZE_MAX;
        thread_local bool failClose = false;
#endif
        void* Allocate(size_t size) noexcept
        {
#if defined(UVSR_FILE_BYTES_TEST_HOOKS)
            if (!allocationsUntilFailure)
            {
                allocationsUntilFailure = SIZE_MAX;
                return nullptr;
            }
            if (allocationsUntilFailure != SIZE_MAX) --allocationsUntilFailure;
#endif
            return malloc(size);
        }
        bool Fail(FileReadResult& result, FileReadError error, DWORD code = 0) noexcept
        {
            result = {error,code};
            return false;
        }
        struct Handle
        {
            HANDLE value = INVALID_HANDLE_VALUE;
            ~Handle() noexcept { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
        };
    }
    FileBytes::~FileBytes() noexcept { Clear(); }
    void FileBytes::Clear() noexcept { free(m_Data); m_Data = nullptr; m_Size = 0; }
    FileBytes::FileBytes(FileBytes&& other) noexcept : m_Data(other.m_Data), m_Size(other.m_Size)
    {
        other.m_Data = nullptr; other.m_Size = 0;
    }
    FileBytes& FileBytes::operator=(FileBytes&& other) noexcept
    {
        if (this != &other)
        {
            Clear(); m_Data = other.m_Data; m_Size = other.m_Size;
            other.m_Data = nullptr; other.m_Size = 0;
        }
        return *this;
    }
    struct FileReader
    {
        static bool Read(const wchar_t* path, uint64_t maximumBytes,
            FileBytes& output, FileReadResult& result, uint64_t* measuredBytes = nullptr) noexcept
        {
            result = {};
            if (measuredBytes) *measuredBytes = 0;
            if (!path || !*path) return Fail(result,FileReadError::InvalidPath);
            Handle file;
            file.value = CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_BACKUP_SEMANTICS,nullptr);
            if (file.value == INVALID_HANDLE_VALUE)
            {
                const DWORD code = GetLastError();
                return Fail(result,code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ?
                    FileReadError::Missing : FileReadError::Open,code);
            }
            if (GetFileType(file.value) != FILE_TYPE_DISK) return Fail(result,FileReadError::NotRegular);
            BY_HANDLE_FILE_INFORMATION information{};
            if (!GetFileInformationByHandle(file.value,&information)) return Fail(result,FileReadError::Measure,GetLastError());
            if (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                return Fail(result,FileReadError::NotRegular);
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file.value,&size) || size.QuadPart < 0) return Fail(result,FileReadError::Measure,GetLastError());
            if (measuredBytes)
            {
                *measuredBytes = uint64_t(size.QuadPart);
                if (*measuredBytes != maximumBytes) return Fail(result,FileReadError::SizeMismatch);
            }
            if (uint64_t(size.QuadPart) > maximumBytes || uint64_t(size.QuadPart) >= uint64_t(PTRDIFF_MAX))
                return Fail(result,FileReadError::TooLarge);
            FileBytes candidate;
            candidate.m_Size = size_t(size.QuadPart);
            if (candidate.m_Size)
            {
                candidate.m_Data = static_cast<char*>(Allocate(candidate.m_Size + 1));
                if (!candidate.m_Data) return Fail(result,FileReadError::OutOfMemory);
                candidate.m_Data[candidate.m_Size] = '\0';
            }
            size_t offset = 0;
            while (offset < candidate.m_Size)
            {
                const size_t remaining = candidate.m_Size - offset;
                DWORD wanted = remaining > 1024 * 1024 ? 1024 * 1024 : DWORD(remaining);
#if defined(UVSR_FILE_BYTES_TEST_HOOKS)
                if (!transfersUntilFailure) return Fail(result,FileReadError::Read,ERROR_READ_FAULT);
                if (transfersUntilFailure != SIZE_MAX) --transfersUntilFailure;
                if (maximumTransfer < wanted) wanted = DWORD(maximumTransfer);
#endif
                DWORD received = 0;
                if (!ReadFile(file.value,candidate.m_Data + offset,wanted,&received,nullptr))
                    return Fail(result,FileReadError::Read,GetLastError());
                if (!received || received > wanted) return Fail(result,FileReadError::Read,ERROR_HANDLE_EOF);
                offset += received;
            }
            const BOOL closed = CloseHandle(file.value);
            const DWORD closeError = closed ? 0 : GetLastError();
            file.value = INVALID_HANDLE_VALUE;
            if (!closed) return Fail(result,FileReadError::Close,closeError);
#if defined(UVSR_FILE_BYTES_TEST_HOOKS)
            if (failClose) return Fail(result,FileReadError::Close,ERROR_READ_FAULT);
#endif
            output = static_cast<FileBytes&&>(candidate);
            return true;
        }
    };
    bool ReadFileBytes(const wchar_t* path, uint64_t maximumBytes, FileBytes& output, FileReadResult& result) noexcept
    {
        return FileReader::Read(path,maximumBytes,output,result);
    }
    bool ReadFileBytesExact(const wchar_t* path, uint64_t expectedBytes, FileBytes& output, FileReadResult& result, uint64_t& measuredBytes) noexcept
    {
        return FileReader::Read(path,expectedBytes,output,result,&measuredBytes);
    }
    bool ReadFileBytesUtf8(const char* path, uint64_t maximumBytes, FileBytes& output, FileReadResult& result) noexcept
    {
        result = {};
        if (!path || !*path) return Fail(result,FileReadError::InvalidPath);
        const size_t count = strlen(path);
        if (count >= INT_MAX) return Fail(result,FileReadError::InvalidPath);
        const int size = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,int(count + 1),nullptr,0);
        if (!size) return Fail(result,FileReadError::InvalidPath,GetLastError());
        if (size_t(size) > size_t(PTRDIFF_MAX) / sizeof(wchar_t)) return Fail(result,FileReadError::TooLarge);
        wchar_t* native = static_cast<wchar_t*>(Allocate(size_t(size) * sizeof(wchar_t)));
        if (!native) return Fail(result,FileReadError::OutOfMemory);
        const bool converted = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,int(count + 1),native,size) == size;
        if (!converted) Fail(result,FileReadError::InvalidPath,GetLastError());
        const bool success = converted && FileReader::Read(native,maximumBytes,output,result);
        free(native);
        return success;
    }
#if defined(UVSR_FILE_BYTES_TEST_HOOKS)
    void FailFileAllocationAfter(size_t count) noexcept { allocationsUntilFailure = count; }
    void ClearFileAllocationFailure() noexcept { allocationsUntilFailure = SIZE_MAX; }
    void SetFileReadTestLimits(size_t limit, size_t transfers, bool close) noexcept
    { maximumTransfer = limit; transfersUntilFailure = transfers; failClose = close; }
    void ClearFileReadTestLimits() noexcept
    { maximumTransfer = SIZE_MAX; transfersUntilFailure = SIZE_MAX; failClose = false; }
#endif
}
