#pragma once

#include <stdint.h>
#include <stddef.h>

namespace uvsr
{
    struct WindowsFileStatus
    {
        bool regular = false;
        uint32_t nativeCode = 0;
        uint32_t cleanupCode = 0;
    };

    // follows reparse targets. no file data is read, and every acquired handle
    // is closed before returning. cleanup failure cannot report a usable file.
    [[nodiscard]] WindowsFileStatus QueryWindowsRegularFile(const wchar_t* path) noexcept;

    class WindowsErrorMessage
    {
    public:
        explicit WindowsErrorMessage(uint32_t code) noexcept;
        ~WindowsErrorMessage() noexcept;
        WindowsErrorMessage(const WindowsErrorMessage&) = delete;
        WindowsErrorMessage& operator=(const WindowsErrorMessage&) = delete;
        [[nodiscard]] const char* Text() const noexcept { return m_Size ? m_Text : "unknown error"; }
        [[nodiscard]] size_t Size() const noexcept { return m_Size ? m_Size : sizeof("unknown error") - 1; }
    private:
        char* m_Text = nullptr;
        size_t m_Size = 0;
    };
}
