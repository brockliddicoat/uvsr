#pragma once

#include <stddef.h>
#include <stdint.h>

namespace uvsr
{
    enum class WindowsPathTextForm : uint8_t { Native, Generic, NormalizedGeneric };
    enum class WindowsPathTextEncoding : uint8_t { Filesystem, Utf8 };
    enum class WindowsPathTextError : uint8_t { None, InvalidPath, Capacity, Allocation, Conversion };
    struct WindowsPathTextResult
    {
        WindowsPathTextError error = WindowsPathTextError::None;
        uint32_t nativeCode = 0;
    };

    // encoded text is owned through move, clear or destruction. assignment borrows
    // counted UTF-16 only during the call and preserves existing text on failure.
    // counted NULs are retained; OS callers supply terminated WindowsPath values.
    class WindowsPathText
    {
    public:
        WindowsPathText() noexcept = default;
        ~WindowsPathText() noexcept;
        WindowsPathText(const WindowsPathText&) = delete;
        WindowsPathText& operator=(const WindowsPathText&) = delete;
        WindowsPathText(WindowsPathText&& other) noexcept;
        WindowsPathText& operator=(WindowsPathText&& other) noexcept;
        [[nodiscard]] bool Assign(const wchar_t* path, size_t size, WindowsPathTextForm form,
            WindowsPathTextEncoding encoding, WindowsPathTextResult& result) noexcept;
        [[nodiscard]] const char* Data() const noexcept { return m_Data ? m_Data : ""; }
        [[nodiscard]] size_t Size() const noexcept { return m_Size; }
        void Clear() noexcept;
    private:
        char* m_Data = nullptr;
        size_t m_Size = 0;
    };

#if defined(UVSR_WINDOWS_PATH_TEXT_TEST_HOOKS)
    void FailWindowsPathTextAllocationAfter(size_t successfulAllocations) noexcept;
    void ClearWindowsPathTextAllocationFailure() noexcept;
#endif
}
