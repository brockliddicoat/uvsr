#include "windows_executable_path.h"
#include "scene_catalog_path.h"

#include <Windows.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <wchar.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_WINDOWS_PATH_TEST_HOOKS)
        thread_local WindowsPathError PendingFailure = WindowsPathError::None;
        bool TakeFailure(WindowsPathError error) noexcept
        {
            if (PendingFailure != error)
                return false;
            PendingFailure = WindowsPathError::None;
            return true;
        }
#endif

        bool Fail(WindowsPathResult& result, WindowsPathError error, DWORD code) noexcept
        {
            result = {error, code};
            return false;
        }

        bool IsSeparator(wchar_t value) noexcept { return value == L'\\' || value == L'/'; }

        size_t RootNameLength(const wchar_t* text, size_t length) noexcept
        {
            if (length >= 2u && text[1] == L':' &&
                ((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')))
                return 2u;
            if (length < 3u || !IsSeparator(text[0]))
                return 0u;
            if (length >= 4u && text[1] == L'?' && text[2] == L'?' &&
                IsSeparator(text[3]) && (length == 4u || !IsSeparator(text[4])))
                return 3u;
            if (!IsSeparator(text[1]) || IsSeparator(text[2]))
                return 0u;
            size_t end = 3u;
            while (end < length && !IsSeparator(text[end]))
                ++end;
            return end;
        }

        size_t ParentLength(const wchar_t* text, size_t length) noexcept
        {
            size_t root = RootNameLength(text, length);
            while (root < length && IsSeparator(text[root]))
                ++root;
            while (length > root && !IsSeparator(text[length - 1u]))
                --length;
            while (length > root && IsSeparator(text[length - 1u]))
                --length;
            return length;
        }

        bool ValidText(const wchar_t* text, size_t length, WindowsPathResult& result) noexcept
        {
            if (!text || !length || length > SIZE_MAX / sizeof(wchar_t) - 1u ||
                length > (UINTPTR_MAX - reinterpret_cast<uintptr_t>(text)) / sizeof(wchar_t) ||
                wmemchr(text, L'\0', length))
                return Fail(result, WindowsPathError::InvalidPath, ERROR_INVALID_NAME);
            return true;
        }

        wchar_t* Allocate(size_t capacity, WindowsPathResult& result) noexcept
        {
#if defined(UVSR_WINDOWS_PATH_TEST_HOOKS)
            if (TakeFailure(WindowsPathError::Allocation))
            {
                (void)Fail(result, WindowsPathError::Allocation, ERROR_NOT_ENOUGH_MEMORY);
                return nullptr;
            }
#endif
            auto* text = static_cast<wchar_t*>(malloc(capacity * sizeof(wchar_t)));
            if (!text)
                (void)Fail(result, WindowsPathError::Allocation, ERROR_NOT_ENOUGH_MEMORY);
            return text;
        }
    }

    struct WindowsPathWriter
    {
        static void Normalize(WindowsPath& path) noexcept
        {
            path.m_Size = catalog_path::Normalize(path.m_Data, path.m_Size);
        }

        static void Publish(WindowsPath& output, wchar_t* text, size_t length) noexcept
        {
            text[length] = L'\0';
            output.Clear();
            output.m_Data = text;
            output.m_Size = length;
        }
    };

    WindowsPath::~WindowsPath() noexcept { Clear(); }
    WindowsPath::WindowsPath(WindowsPath&& other) noexcept
        : m_Data(other.m_Data), m_Size(other.m_Size)
    {
        other.m_Data = nullptr;
        other.m_Size = 0u;
    }
    WindowsPath& WindowsPath::operator=(WindowsPath&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            m_Data = other.m_Data;
            m_Size = other.m_Size;
            other.m_Data = nullptr;
            other.m_Size = 0u;
        }
        return *this;
    }
    void WindowsPath::Clear() noexcept
    {
        free(m_Data);
        m_Data = nullptr;
        m_Size = 0u;
    }

    bool ExecutableDirectoryFromModulePath(const wchar_t* path, size_t length,
        WindowsPath& output, WindowsPathResult& result) noexcept
    {
        result = {};
        if (!ValidText(path, length, result))
            return false;
        const size_t parent = ParentLength(path, length);
        if (!parent)
            return Fail(result, WindowsPathError::InvalidPath, ERROR_INVALID_NAME);
        wchar_t* text = Allocate(parent + 1u, result);
        if (!text)
            return false;
        wmemcpy(text, path, parent);
        WindowsPathWriter::Publish(output, text, parent);
        return true;
    }

    bool GetModulePathWide(void* module, WindowsPath& output, WindowsPathResult& result) noexcept
    {
        result = {};
        constexpr DWORD capacity = 32768u;
        wchar_t* text = Allocate(capacity, result);
        if (!text) return false;
        wmemset(text, L'\0', capacity);
        const DWORD length = GetModuleFileNameW(static_cast<HMODULE>(module), text, capacity);
        if (!length || length >= capacity)
        {
            const DWORD code = GetLastError();
            free(text);
            return Fail(result, length ? WindowsPathError::ModuleTruncated : WindowsPathError::ModuleQuery, code);
        }
        WindowsPathWriter::Publish(output, text, length);
        return true;
    }

    bool GetExecutableDirectoryWide(WindowsPath& output, WindowsPathResult& result) noexcept
    {
        result = {};
        constexpr DWORD capacity = 32768u;
        wchar_t* text = Allocate(capacity, result);
        if (!text)
            return false;
        SetLastError(ERROR_SUCCESS);
        DWORD length = GetModuleFileNameW(nullptr, text, capacity);
#if defined(UVSR_WINDOWS_PATH_TEST_HOOKS)
        if (TakeFailure(WindowsPathError::ModuleQuery))
        {
            length = 0u;
            SetLastError(ERROR_ACCESS_DENIED);
        }
        else if (TakeFailure(WindowsPathError::ModuleTruncated))
        {
            length = capacity;
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        }
#endif
        if (!length || length >= capacity)
        {
            const DWORD code = length >= capacity ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
            free(text);
            return Fail(result, length ? WindowsPathError::ModuleTruncated : WindowsPathError::ModuleQuery, code);
        }
        const size_t parent = ParentLength(text, length);
        if (!parent)
        {
            free(text);
            return Fail(result, WindowsPathError::InvalidPath, ERROR_INVALID_NAME);
        }
        WindowsPathWriter::Publish(output, text, parent);
        return true;
    }

    bool GetTemporaryDirectoryWide(WindowsPath& output, WindowsPathResult& result) noexcept
    {
        result = {};
        constexpr DWORD capacity = MAX_PATH + 1u;
        wchar_t* text = Allocate(capacity, result);
        if (!text) return false;
        auto query = reinterpret_cast<decltype(&GetTempPathW)>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTempPath2W"));
        if (!query) query = &GetTempPathW;
        const DWORD length = query(capacity, text);
        if (!length || length >= capacity)
        {
            const DWORD code = length ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
            free(text);
            return Fail(result, length ? WindowsPathError::TemporaryTruncated : WindowsPathError::TemporaryQuery, code);
        }
        const DWORD attributes = GetFileAttributesW(text);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            free(text);
            return Fail(result, WindowsPathError::NotDirectory, 0);
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            const HANDLE directory = CreateFileW(text, FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (directory == INVALID_HANDLE_VALUE)
            {
                free(text);
                return Fail(result, WindowsPathError::NotDirectory, 0);
            }
            if (!CloseHandle(directory))
            {
                const DWORD code = GetLastError();
                free(text);
                return Fail(result, WindowsPathError::Close, code);
            }
        }
        WindowsPathWriter::Publish(output, text, length);
        return true;
    }

    bool JoinWindowsRelativePath(const wchar_t* directory, const wchar_t* relative,
        WindowsPath& output, WindowsPathResult& result) noexcept
    {
        result = {};
        if (!directory || !relative || !directory[0] || !relative[0])
            return Fail(result, WindowsPathError::InvalidPath, ERROR_INVALID_NAME);
        const size_t baseLength = wcslen(directory);
        const size_t suffixLength = wcslen(relative);
        if (IsSeparator(relative[0]) || RootNameLength(relative, suffixLength))
            return Fail(result, WindowsPathError::InvalidPath, ERROR_INVALID_NAME);
        const size_t separator = !IsSeparator(directory[baseLength - 1u]) &&
            !(baseLength == 2u && RootNameLength(directory, baseLength) == 2u) ? 1u : 0u;
        const size_t maximum = SIZE_MAX / sizeof(wchar_t) - 1u;
        if (baseLength > maximum - separator || suffixLength > maximum - separator - baseLength)
            return Fail(result, WindowsPathError::InvalidPath, ERROR_FILENAME_EXCED_RANGE);
        const size_t length = baseLength + separator + suffixLength;
        wchar_t* text = Allocate(length + 1u, result);
        if (!text)
            return false;
        wmemcpy(text, directory, baseLength);
        if (separator)
            text[baseLength] = L'\\';
        wmemcpy(text + baseLength + separator, relative, suffixLength);
        WindowsPathWriter::Publish(output, text, length);
        return true;
    }

    bool DecodeWindowsUtf8Path(const char* input, size_t size,
        WindowsPath& output, WindowsPathResult& result) noexcept
    {
        result = {};
        catalog_path::ConversionPlan plan;
        catalog_path::Error error;
        if (size > size_t(INT_MAX) || size > size_t(PTRDIFF_MAX))
            return Fail(result, WindowsPathError::InvalidPath, ERROR_FILENAME_EXCED_RANGE);
        if ((!input && size) || size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(input))
            return Fail(result, WindowsPathError::InvalidPath, ERROR_INVALID_PARAMETER);
        const std::string_view text(input ? input : "", size);
        if (!catalog_path::MeasureDecode(text, catalog_path::Encoding::Utf8, plan, error))
            return Fail(result, WindowsPathError::InvalidPath, error.nativeCode ? error.nativeCode : ERROR_INVALID_PARAMETER);
        if ((size && memchr(input, 0, size)) || plan.size > size_t(PTRDIFF_MAX) / sizeof(wchar_t) - 1u)
            return Fail(result, WindowsPathError::InvalidPath, ERROR_INVALID_NAME);
        if (!size)
        {
            output.Clear();
            return true;
        }
        wchar_t* candidate = Allocate(plan.size + 1u, result);
        if (!candidate) return false;
        size_t written = 0;
        if (!catalog_path::Decode(text, plan, candidate, plan.size + 1u, written, error) || written != plan.size)
        {
            free(candidate);
            return Fail(result, WindowsPathError::InvalidPath, error.nativeCode ? error.nativeCode : ERROR_INVALID_DATA);
        }
        WindowsPathWriter::Publish(output, candidate, written);
        return true;
    }

    namespace
    {
        bool MissingCanonicalPath(DWORD code) noexcept
        {
            return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ||
                code == ERROR_BAD_NETPATH || code == ERROR_INVALID_NAME ||
                code == ERROR_DIRECTORY || code == ERROR_NETNAME_DELETED;
        }

        bool CopyPath(std::wstring_view input, WindowsPath& output, WindowsPathResult& result) noexcept
        {
            if (input.size() > size_t(PTRDIFF_MAX) / sizeof(wchar_t) - 1u)
                return Fail(result, WindowsPathError::InvalidPath, ERROR_FILENAME_EXCED_RANGE);
            if (input.empty())
            {
                output.Clear();
                return true;
            }
            wchar_t* text = Allocate(input.size() + 1u, result);
            if (!text) return false;
            wmemcpy(text, input.data(), input.size());
            WindowsPathWriter::Publish(output, text, input.size());
            return true;
        }

        bool JoinPath(std::wstring_view left, std::wstring_view right,
            WindowsPath& output, WindowsPathResult& result) noexcept
        {
            catalog_path::JoinPlan plan;
            catalog_path::Error error;
            if (!catalog_path::PlanJoin(left, right, plan, error))
                return Fail(result, WindowsPathError::InvalidPath, error.nativeCode);
            wchar_t* text = Allocate(plan.size + 1u, result);
            if (!text) return false;
            size_t size = 0;
            if (!catalog_path::Join(left, right, text, plan.size + 1u, size, error))
            {
                free(text);
                return Fail(result, WindowsPathError::InvalidPath, error.nativeCode);
            }
            WindowsPathWriter::Publish(output, text, size);
            return true;
        }

        bool CanonicalPath(const wchar_t* input, WindowsPath& output, WindowsPathResult& result) noexcept
        {
            result = {};
            if (!input[0])
            {
                output.Clear();
                return true;
            }
            const HANDLE handle = CreateFileW(input, FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                return Fail(result, WindowsPathError::CanonicalQuery, GetLastError());

            DWORD capacity = MAX_PATH;
            DWORD kind = VOLUME_NAME_DOS;
            wchar_t* text = Allocate(capacity, result);
            bool valid = text != nullptr;
            bool emptyQuery = false;
            size_t size = 0;
            while (valid)
            {
                const DWORD length = GetFinalPathNameByHandleW(handle, text, capacity, kind);
                if (!length)
                {
                    const DWORD code = GetLastError();
                    if (code == ERROR_PATH_NOT_FOUND && kind == VOLUME_NAME_DOS)
                    {
                        kind = VOLUME_NAME_NT;
                        continue;
                    }
                    // the pinned CRT returns its error verbatim, including zero.
                    emptyQuery = true;
                    if (code) valid = Fail(result, WindowsPathError::CanonicalQuery, code);
                    break;
                }
                if (length < capacity)
                {
                    size = length;
                    break;
                }
                if (length == capacity) continue;
                if (size_t(length) > size_t(PTRDIFF_MAX) / sizeof(wchar_t))
                {
                    valid = Fail(result, WindowsPathError::InvalidPath, ERROR_FILENAME_EXCED_RANGE);
                    break;
                }
                wchar_t* grown = Allocate(length, result);
                if (!grown)
                {
                    valid = false;
                    break;
                }
                free(text);
                text = grown;
                capacity = length;
            }
            if (!CloseHandle(handle) && valid)
                valid = Fail(result, WindowsPathError::Close, GetLastError());
            if (!valid)
            {
                free(text);
                return false;
            }
            if (!emptyQuery && kind == VOLUME_NAME_NT)
            {
                constexpr wchar_t Prefix[] = LR"(\\?\GLOBALROOT)";
                constexpr size_t prefixSize = sizeof(Prefix) / sizeof(wchar_t) - 1u;
                if (size > size_t(PTRDIFF_MAX) / sizeof(wchar_t) - prefixSize - 1u)
                {
                    free(text);
                    return Fail(result, WindowsPathError::InvalidPath, ERROR_FILENAME_EXCED_RANGE);
                }
                wchar_t* prefixed = Allocate(prefixSize + size + 1u, result);
                if (!prefixed)
                {
                    free(text);
                    return false;
                }
                wmemcpy(prefixed, Prefix, prefixSize);
                wmemcpy(prefixed + prefixSize, text, size);
                free(text);
                text = prefixed;
                size += prefixSize;
            }
            else if (size >= 6u && wmemcmp(text, LR"(\\?\)", 4u) == 0 && text[5] == L':' &&
                ((text[4] >= L'A' && text[4] <= L'Z') || (text[4] >= L'a' && text[4] <= L'z')))
            {
                size -= 4u;
                wmemmove(text, text + 4u, size);
            }
            else if (size >= 8u && wmemcmp(text, LR"(\\?\UNC\)", 8u) == 0)
            {
                wmemmove(text + 2u, text + 8u, size - 8u);
                size -= 6u;
            }
            WindowsPathWriter::Publish(output, text, size);
            return true;
        }
    }

    bool WeaklyCanonicalWindowsPath(const wchar_t* input, WindowsPath& output, WindowsPathResult& result) noexcept
    {
        result = {};
        if (!input) return Fail(result, WindowsPathError::InvalidPath, ERROR_INVALID_PARAMETER);
        WindowsPath complete;
        if (CanonicalPath(input, complete, result))
        {
            output = static_cast<WindowsPath&&>(complete);
            return true;
        }
        if (result.error != WindowsPathError::CanonicalQuery || !MissingCanonicalPath(result.nativeCode))
            return false;
        result = {};
        WindowsPath normalized;
        if (!CopyPath({input, wcslen(input)}, normalized, result)) return false;
        WindowsPathWriter::Normalize(normalized);
        const std::wstring_view path(normalized.Data(), normalized.Size());
        const size_t relativeStart = catalog_path::RelativeStart(path);
        if (!CopyPath(path.substr(0, relativeStart), complete, result)) return false;
        const std::wstring_view relative = path.substr(relativeStart);
        bool canonical = true;
        size_t cursor = 0;
        std::wstring_view component;
        while (catalog_path::Next(relative, cursor, component))
        {
            if (!JoinPath({complete.Data(), complete.Size()}, component, complete, result)) return false;
            if (!canonical) continue;
            WindowsPath prefix;
            if (CanonicalPath(complete.Data(), prefix, result))
                complete = static_cast<WindowsPath&&>(prefix);
            else if (result.error == WindowsPathError::CanonicalQuery && MissingCanonicalPath(result.nativeCode))
            {
                canonical = false;
                result = {};
            }
            else return false;
        }
        output = static_cast<WindowsPath&&>(complete);
        return true;
    }

    bool WindowsPathIsAbsolute(const WindowsPath& path) noexcept
    {
        const auto* text = path.Data();
        const size_t size = path.Size();
        const size_t root = RootNameLength(text, size);
        if (root == 2u && text[1] == L':') return size >= 3u && IsSeparator(text[2]);
        return root != 0u;
    }

#if defined(UVSR_WINDOWS_PATH_TEST_HOOKS)
    void FailWindowsPathOnce(WindowsPathError error) noexcept { PendingFailure = error; }
#endif
}
