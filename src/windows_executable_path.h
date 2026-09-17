#pragma once

#include <stddef.h>
#include <stdint.h>

namespace uvsr
{
    enum class WindowsPathError : uint8_t
    {
        None, InvalidPath, Allocation, ModuleQuery, ModuleTruncated,
        TemporaryQuery, TemporaryTruncated, NotDirectory, Close, CanonicalQuery
    };
    struct WindowsPathResult
    {
        WindowsPathError error = WindowsPathError::None;
        uint32_t nativeCode = 0;
    };

    // UTF-16 storage remains borrowed until this owner moves, clears or dies.
    // queries publish a terminated path only on success; failures preserve it.
    class WindowsPath
    {
    public:
        WindowsPath() noexcept = default;
        ~WindowsPath() noexcept;
        WindowsPath(const WindowsPath&) = delete;
        WindowsPath& operator=(const WindowsPath&) = delete;
        WindowsPath(WindowsPath&& other) noexcept;
        WindowsPath& operator=(WindowsPath&& other) noexcept;
        [[nodiscard]] const wchar_t* Data() const noexcept { return m_Data ? m_Data : L""; }
        [[nodiscard]] size_t Size() const noexcept { return m_Size; }
        void Clear() noexcept;
    private:
        wchar_t* m_Data = nullptr;
        size_t m_Size = 0;
        friend struct WindowsPathWriter;
    };

    [[nodiscard]] bool ExecutableDirectoryFromModulePath(const wchar_t* path,
        size_t length, WindowsPath& output, WindowsPathResult& result) noexcept;
    // module is an HMODULE borrowed from the loader, or null for the executable.
    // retain the native query's error code, including a truncated result.
    [[nodiscard]] bool GetModulePathWide(void* module,
        WindowsPath& output, WindowsPathResult& result) noexcept;
    // follows existing prefixes and retains a normalized missing suffix. input
    // is terminated and borrowed throughout the call; failure preserves output.
    [[nodiscard]] bool WeaklyCanonicalWindowsPath(const wchar_t* input,
        WindowsPath& output, WindowsPathResult& result) noexcept;
    [[nodiscard]] bool GetExecutableDirectoryWide(
        WindowsPath& output, WindowsPathResult& result) noexcept;
    [[nodiscard]] bool GetTemporaryDirectoryWide(
        WindowsPath& output, WindowsPathResult& result) noexcept;
    // both inputs are terminated. the suffix must have no root name or directory.
    [[nodiscard]] bool JoinWindowsRelativePath(const wchar_t* directory,
        const wchar_t* relative, WindowsPath& output, WindowsPathResult& result) noexcept;

    // strict UTF-8 to a terminated OS path. interior NULs are invalid; empty is allowed.
    // input stays unchanged through the call. failure preserves the previous owner.
    [[nodiscard]] bool DecodeWindowsUtf8Path(const char* input, size_t size,
        WindowsPath& output, WindowsPathResult& result) noexcept;
    [[nodiscard]] bool WindowsPathIsAbsolute(const WindowsPath& path) noexcept;

#if defined(UVSR_WINDOWS_PATH_TEST_HOOKS)
    void FailWindowsPathOnce(WindowsPathError error) noexcept;
#endif
}
