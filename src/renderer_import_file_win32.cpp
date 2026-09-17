#include "renderer_import_load.h"
#include "renderer_import_path.h"
#include "import/renderer_import_allocation.h"

#include <Windows.h>
#include <limits.h>
#include <stdlib.h>

namespace uvsr
{
    namespace
    {
        ImportResult SystemFailure(DWORD code) noexcept
        {
            const auto error = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND
                ? ImportError::FileUnavailable : ImportError::Io;
            return {error, ImportObject::Document, SIZE_MAX, 0, code};
        }
        struct WidePath
        {
            wchar_t* value = nullptr;
            ~WidePath() noexcept { free(value); }
            ImportResult Prepare(ArrayView<const char> path) noexcept
            {
                ArrayView<const char> filename;
                const auto valid = ReadImportPathFilename(path, filename);
                if (!valid || !path.count) return {ImportError::InvalidInput};
                if (path.count > INT_MAX) return {ImportError::Capacity};
                const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data, int(path.count), nullptr, 0);
                if (!count) return {ImportError::InvalidInput, ImportObject::Document, SIZE_MAX, 0, GetLastError()};
                if (size_t(count) >= size_t(PTRDIFF_MAX) / sizeof(wchar_t)) return {ImportError::Capacity};
                value = static_cast<wchar_t*>(ImportAllocate((size_t(count) + 1) * sizeof(wchar_t)));
                if (!value) return {ImportError::OutOfMemory};
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data, int(path.count), value, count) != count)
                    return {ImportError::InvalidInput, ImportObject::Document, SIZE_MAX, 0, GetLastError()};
                value[count] = L'\0'; return {};
            }
        };
        struct FileHandle
        {
            HANDLE value = INVALID_HANDLE_VALUE;
            ~FileHandle() noexcept { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
            ImportResult Close() noexcept
            {
                const HANDLE handle = value; value = INVALID_HANDLE_VALUE;
                return CloseHandle(handle) ? ImportResult{} : SystemFailure(GetLastError());
            }
        };

        ImportResult Read(void*, ArrayView<const char> path, size_t byteLimit,
            ImportFileData& output, ImportCancellation cancellation) noexcept
        {
            if (cancellation.IsRequested()) return {ImportError::Canceled};
            WidePath name;
            auto result = name.Prepare(path);
            if (!result) return result;
            FileHandle file;
            file.value = CreateFileW(name.value, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (file.value == INVALID_HANDLE_VALUE) return SystemFailure(GetLastError());
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file.value, &size)) return SystemFailure(GetLastError());
            if (size.QuadPart < 0 || uint64_t(size.QuadPart) > uint64_t(PTRDIFF_MAX) || uint64_t(size.QuadPart) > byteLimit)
                return {ImportError::Capacity};
            if (cancellation.IsRequested()) return {ImportError::Canceled};
            ImportFileData candidate;
            result = candidate.Allocate(size_t(size.QuadPart));
            if (!result) return result;
            const auto bytes = candidate.WritableBytes();
            size_t position = 0;
            constexpr DWORD chunkBytes = 1024 * 1024;
            while (position < bytes.count)
            {
                if (cancellation.IsRequested()) return {ImportError::Canceled};
                const size_t remaining = bytes.count - position;
                const DWORD requested = remaining < chunkBytes ? DWORD(remaining) : chunkBytes;
                DWORD received = 0;
                if (!ReadFile(file.value, bytes.data + position, requested, &received, nullptr)) return SystemFailure(GetLastError());
                if (!received || received > requested) return SystemFailure(ERROR_HANDLE_EOF);
                position += received;
            }
            LARGE_INTEGER finalSize{};
            if (!GetFileSizeEx(file.value, &finalSize)) return SystemFailure(GetLastError());
            if (finalSize.QuadPart != size.QuadPart) return SystemFailure(ERROR_FILE_INVALID);
            result = file.Close();
            if (!result) return result;
            if (cancellation.IsRequested()) return {ImportError::Canceled};
            output = static_cast<ImportFileData&&>(candidate); return {};
        }
        ImportResult Exists(void*, ArrayView<const char> path, bool& present) noexcept
        {
            WidePath name;
            const auto prepared = name.Prepare(path);
            if (!prepared) return prepared;
            const DWORD attributes = GetFileAttributesW(name.value);
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const DWORD code = GetLastError();
                if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) return SystemFailure(code);
                present = false; return {};
            }
            present = !(attributes & FILE_ATTRIBUTE_DIRECTORY); return {};
        }
    }

    ImportFileSource NativeImportFileSource() noexcept { return {Read, Exists, nullptr}; }
}
