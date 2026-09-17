#include "renderer_log.h"

#include <Windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

namespace uvsr::log
{
    namespace
    {
        SRWLOCK g_CallbackMutex = SRWLOCK_INIT;
        SRWLOCK g_OutputMutex = SRWLOCK_INIT;
        LONG g_MinimumSeverity = LONG(Severity::Info);

        [[nodiscard]] const char* SeverityName(Severity severity) noexcept
        {
            switch (severity)
            {
            case Severity::Debug: return "DEBUG";
            case Severity::Info: return "INFO";
            case Severity::Warning: return "WARNING";
            case Severity::Error: return "ERROR";
            case Severity::Fatal: return "FATAL ERROR";
            default: return "LOG";
            }
        }

        void DefaultCallback(void*, Severity severity, const char* message)
        {
            char line[MessageCapacity]{};
            snprintf(
                line, sizeof(line), "%s: %s",
                SeverityName(severity), message ? message : "");
            AcquireSRWLockExclusive(&g_OutputMutex);
            OutputDebugStringA(line);
            OutputDebugStringA("\n");
            FILE* output = severity >= Severity::Error ? stderr : stdout;
            fprintf(output, "%s\n", line);
            fflush(output);
            ReleaseSRWLockExclusive(&g_OutputMutex);
        }

        Callback g_Callback{DefaultCallback, nullptr};

        [[nodiscard]] bool IsEnabled(Severity severity) noexcept
        {
            return severity == Severity::Fatal ||
                LONG(severity) >= InterlockedCompareExchange(&g_MinimumSeverity, 0, 0);
        }

        void Dispatch(Severity severity, const char* format, va_list arguments)
        {
            if (!IsEnabled(severity))
                return;
            char messageBuffer[MessageCapacity]{};
            if (format != nullptr)
            {
                vsnprintf(
                    messageBuffer, sizeof(messageBuffer),
                    format, arguments);
            }
            const Callback callback = GetCallback();
            callback.function(callback.context, severity, messageBuffer);
        }
    }

    void SetMinimumSeverity(Severity severity) noexcept
    {
        InterlockedExchange(&g_MinimumSeverity, LONG(severity));
    }

    void SetCallback(Callback callback) noexcept
    {
        AcquireSRWLockExclusive(&g_CallbackMutex);
        g_Callback = callback.function ? callback : Callback{DefaultCallback, nullptr};
        ReleaseSRWLockExclusive(&g_CallbackMutex);
    }

    Callback GetCallback() noexcept
    {
        AcquireSRWLockShared(&g_CallbackMutex);
        const Callback callback = g_Callback;
        ReleaseSRWLockShared(&g_CallbackMutex);
        return callback;
    }

    void debug(const char* format, ...)
    {
        va_list arguments;
        va_start(arguments, format);
        Dispatch(Severity::Debug, format, arguments);
        va_end(arguments);
    }

    void info(const char* format, ...)
    {
        va_list arguments;
        va_start(arguments, format);
        Dispatch(Severity::Info, format, arguments);
        va_end(arguments);
    }

    void warning(const char* format, ...)
    {
        va_list arguments;
        va_start(arguments, format);
        Dispatch(Severity::Warning, format, arguments);
        va_end(arguments);
    }

    void error(const char* format, ...)
    {
        va_list arguments;
        va_start(arguments, format);
        Dispatch(Severity::Error, format, arguments);
        va_end(arguments);
    }

    [[noreturn]] void fatal(const char* format, ...)
    {
        va_list arguments;
        va_start(arguments, format);
        Dispatch(Severity::Fatal, format, arguments);
        va_end(arguments);
        abort();
    }
}
