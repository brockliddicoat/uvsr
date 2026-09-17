#include "uvsr_command_line.h"

#include "command_line_options.h"
#include <charconv>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

namespace uvsr
{
#if defined(UVSR_RESTART_COMMAND_LINE_TEST_HOOKS)
    namespace { thread_local size_t RestartAllocationsBeforeFailure = SIZE_MAX; }
    void FailRestartCommandLineAllocationAfter(size_t count) noexcept { RestartAllocationsBeforeFailure = count; }
    void ClearRestartCommandLineAllocationFailure() noexcept { RestartAllocationsBeforeFailure = SIZE_MAX; }
#endif
    RestartCommandLine::~RestartCommandLine() noexcept { Clear(); }
    RestartCommandLine::RestartCommandLine(RestartCommandLine&& other) noexcept
        : m_Data(other.m_Data), m_Size(other.m_Size)
    {
        other.m_Data = nullptr; other.m_Size = 0;
    }
    RestartCommandLine& RestartCommandLine::operator=(RestartCommandLine&& other) noexcept
    {
        if (this != &other)
        {
            Clear(); m_Data = other.m_Data; m_Size = other.m_Size;
            other.m_Data = nullptr; other.m_Size = 0;
        }
        return *this;
    }
    void RestartCommandLine::Clear() noexcept
    {
        free(m_Data); m_Data = nullptr; m_Size = 0;
    }
    bool RestartCommandLine::Prepare(const wchar_t* current, int adapter,
        RestartCommandLineError& error) noexcept
    {
        error = RestartCommandLineError::None;
        if (!current) { error = RestartCommandLineError::InvalidInput; return false; }
        const size_t length = wcslen(current);
        constexpr wchar_t suffix[] = L" -adapter ";
        wchar_t digits[sizeof(int) * 3];
        size_t digitCount = 0;
        if (adapter >= 0)
        {
            unsigned int value = static_cast<unsigned int>(adapter);
            do { digits[digitCount++] = wchar_t(L'0' + value % 10); value /= 10; } while (value);
        }
        constexpr size_t suffixLength = sizeof(suffix) / sizeof(wchar_t) - 1;
        const size_t appended = adapter >= 0 ? suffixLength + digitCount : 0;
        constexpr size_t maximumUnits = size_t(PTRDIFF_MAX) / sizeof(wchar_t) - 1;
        if (length > maximumUnits || appended > maximumUnits - length)
        {
            error = RestartCommandLineError::Capacity;
            return false;
        }
#if defined(UVSR_RESTART_COMMAND_LINE_TEST_HOOKS)
        if (RestartAllocationsBeforeFailure != SIZE_MAX)
        {
            if (!RestartAllocationsBeforeFailure) { error = RestartCommandLineError::OutOfMemory; return false; }
            --RestartAllocationsBeforeFailure;
        }
#endif
        const size_t size = length + appended;
        auto* text = static_cast<wchar_t*>(malloc((size + 1) * sizeof(wchar_t)));
        if (!text) { error = RestartCommandLineError::OutOfMemory; return false; }
        memcpy(text, current, length * sizeof(wchar_t));
        if (appended)
        {
            memcpy(text + length, suffix, suffixLength * sizeof(wchar_t));
            for (size_t index = 0; index < digitCount; ++index)
                text[length + suffixLength + index] = digits[digitCount - 1 - index];
        }
        text[size] = L'\0';
        // current may borrow the old buffer. release it only after the copy.
        free(m_Data); m_Data = text; m_Size = size;
        return true;
    }

    bool ParseCommandLineInt(const char* text,
        int32_t minimum, int32_t maximum, int32_t& value) noexcept
    {
        if (!text || !text[0] || minimum > maximum)
            return false;
        int32_t parsed = 0;
        const char* const end = text + strlen(text);
        const auto result = std::from_chars(text, end, parsed);
        if (result.ec != std::errc{} || result.ptr != end ||
            parsed < minimum || parsed > maximum)
            return false;
        value = parsed;
        return true;
    }

    bool ParseUvsrCommandLine(
        int argc,
        const char* const* argv,
        UvsrStartupOptions& options,
        UvsrCommandLineError& error) noexcept
    {
        UvsrStartupOptions candidate;
        error = {};
        if (argc < 0 || (argc > 0 && (!argv ||
            size_t(argc) > (UINTPTR_MAX - reinterpret_cast<uintptr_t>(argv)) / sizeof(*argv))))
        {
            error.prefix = "command-line arguments are unavailable";
            return false;
        }

        for (int index = 1; index < argc; ++index)
        {
            const char* argument = argv[index];
            if (!argument)
            {
                error.prefix = "command-line argument is null";
                return false;
            }

            const auto readInteger = [&]
            (
                const char* option,
                bool zeroAllowed,
                int32_t& value)
            {
                int32_t parsedValue = 0;
                if (index + 1 >= argc || !argv[index + 1] ||
                    !ParseCommandLineInt(
                        argv[index + 1],
                        zeroAllowed ? 0 : 1,
                        INT32_MAX,
                        parsedValue))
                {
                    error.prefix = option;
                    error.argument = " requires an exact integer of at least ";
                    error.suffix = zeroAllowed ? "0" : "1";
                    return false;
                }
                value = parsedValue;
                ++index;
                return true;
            };

            if (strcmp(argument, "-width") == 0)
            {
                if (!readInteger(argument, false, candidate.width))
                    return false;
            }
            else if (strcmp(argument, "-height") == 0)
            {
                if (!readInteger(argument, false, candidate.height))
                    return false;
            }
            else if (strcmp(argument, "-fullscreen") == 0)
            {
                candidate.fullscreen = true;
            }
#if defined(UVSR_BUILD_TESTING)
            else if (strcmp(argument, "-debug") == 0)
            {
                candidate.debugValidation = true;
            }
#endif
            else if (strcmp(argument, "-adapter") == 0)
            {
                if (!readInteger(argument, true, candidate.adapterIndex))
                    return false;
            }
            else if (strcmp(argument, "--settings-snapshot") == 0)
            {
                if (index + 1 >= argc || !argv[index + 1])
                {
                    error.prefix =
                        "--settings-snapshot requires one 32-character code";
                    return false;
                }
                if (candidate.settingsSnapshotCode[0])
                {
                    error.prefix =
                        "--settings-snapshot may be specified only once";
                    return false;
                }
                if (!ValidateSettingsSnapshotLoadCode(
                        argv[index + 1], strlen(argv[index + 1]), error.snapshot))
                {
                    error.prefix = "invalid --settings-snapshot value: ";
                    return false;
                }
                candidate.settingsSnapshotCode = argv[++index];
            }
#if defined(UVSR_BUILD_TESTING)
            else if (strcmp(
                    argument,
                    "--verify-settings-contract") == 0 ||
                strcmp(
                    argument,
                    "--verify-retained-runtime") == 0)
            {
                // the developer engine consumes these after ordinary startup.
            }
#endif
            else if (argument[0] != '-')
            {
                candidate.sceneName = argument;
            }
            else
            {
                error.prefix = "unknown command-line option '";
                error.argument = argument;
                error.suffix = "'";
                return false;
            }
        }
        options = candidate;
        return true;
    }
}
