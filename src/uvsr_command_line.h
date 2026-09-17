#pragma once

#include "settings_snapshot_code.h"
#include <stdint.h>
#include <stddef.h>

namespace uvsr
{
    enum class RestartCommandLineError : uint8_t
    {
        None, InvalidInput, Capacity, OutOfMemory
    };

    class RestartCommandLine
    {
    public:
        RestartCommandLine() noexcept = default;
        ~RestartCommandLine() noexcept;
        RestartCommandLine(const RestartCommandLine&) = delete;
        RestartCommandLine& operator=(const RestartCommandLine&) = delete;
        RestartCommandLine(RestartCommandLine&& other) noexcept;
        RestartCommandLine& operator=(RestartCommandLine&& other) noexcept;

        // input is terminated and immutable through this call. failure preserves
        // the prior buffer and its views. success copies text without requoting.
        [[nodiscard]] bool Prepare(const wchar_t* current, int adapter,
            RestartCommandLineError& error) noexcept;
        // native process creation may edit this exclusive buffer synchronously.
        // the pointer is null before preparation; Size excludes the terminator.
        [[nodiscard]] wchar_t* Data() noexcept { return m_Data; }
        [[nodiscard]] const wchar_t* Data() const noexcept { return m_Data; }
        [[nodiscard]] size_t Size() const noexcept { return m_Size; }
        void Clear() noexcept;
    private:
        wchar_t* m_Data = nullptr;
        size_t m_Size = 0;
    };

#if defined(UVSR_RESTART_COMMAND_LINE_TEST_HOOKS)
    void FailRestartCommandLineAllocationAfter(size_t successfulAllocations) noexcept;
    void ClearRestartCommandLineAllocationFailure() noexcept;
#endif

    struct UvsrStartupOptions
    {
        // dimensions are positive when specified; adapter zero is a valid choice.
        int32_t width = 0;
        int32_t height = 0;
        int32_t adapterIndex = -1;
        bool fullscreen = false;
        bool debugValidation = false;
        const char* sceneName = "";
        const char* settingsSnapshotCode = "";
    };

    // options and diagnostic fragments borrow argv strings. those strings must
    // outlive their use; a failed parse preserves the previous options.
    struct UvsrCommandLineError
    {
        const char* prefix = "";
        const char* argument = "";
        const char* suffix = "";
        SettingsSnapshotCodeError snapshot;
    };

    [[nodiscard]] bool ParseUvsrCommandLine(
        int argc,
        const char* const* argv,
        UvsrStartupOptions& options,
        UvsrCommandLineError& error) noexcept;
}
