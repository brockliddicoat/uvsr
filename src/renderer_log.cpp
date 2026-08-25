#include "renderer_log.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <utility>

namespace uvsr::log
{
    namespace
    {
        constexpr size_t MessageCapacity = 4096u;
        std::mutex g_CallbackMutex;
        std::mutex g_OutputMutex;
        std::atomic<Severity> g_MinimumSeverity{ Severity::Info };

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

        void DefaultCallback(Severity severity, const char* message)
        {
            std::array<char, MessageCapacity> line{};
            std::snprintf(
                line.data(), line.size(), "%s: %s",
                SeverityName(severity), message ? message : "");
            std::lock_guard<std::mutex> lock(g_OutputMutex);
            OutputDebugStringA(line.data());
            OutputDebugStringA("\n");
            FILE* output = severity >= Severity::Error ? stderr : stdout;
            std::fprintf(output, "%s\n", line.data());
            std::fflush(output);
        }

        Callback g_Callback = DefaultCallback;

        [[nodiscard]] bool IsEnabled(Severity severity) noexcept
        {
            return severity == Severity::Fatal ||
                severity >= g_MinimumSeverity.load(std::memory_order_relaxed);
        }

        void Dispatch(Severity severity, const char* format, va_list arguments)
        {
            if (!IsEnabled(severity))
                return;
            std::array<char, MessageCapacity> messageBuffer{};
            if (format != nullptr)
            {
                std::vsnprintf(
                    messageBuffer.data(), messageBuffer.size(),
                    format, arguments);
            }
            Callback callback;
            {
                std::lock_guard<std::mutex> lock(g_CallbackMutex);
                callback = g_Callback;
            }
            callback(severity, messageBuffer.data());
        }
    }

    void SetMinimumSeverity(Severity severity) noexcept
    {
        g_MinimumSeverity.store(severity, std::memory_order_relaxed);
    }

    void SetCallback(Callback callback)
    {
        std::lock_guard<std::mutex> lock(g_CallbackMutex);
        g_Callback = callback ? std::move(callback) : Callback(DefaultCallback);
    }

    Callback GetCallback()
    {
        std::lock_guard<std::mutex> lock(g_CallbackMutex);
        return g_Callback;
    }

    void ResetCallback()
    {
        SetCallback(DefaultCallback);
    }

    void message(Severity severity, const char* format, ...)
    {
        va_list arguments;
        va_start(arguments, format);
        Dispatch(severity, format, arguments);
        va_end(arguments);
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
        std::abort();
    }
}
