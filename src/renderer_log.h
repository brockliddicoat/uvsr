#pragma once

#include <functional>

namespace uvsr::log
{
    enum class Severity
    {
        None = 0,
        Debug,
        Info,
        Warning,
        Error,
        Fatal
    };

    using Callback = std::function<void(Severity, const char*)>;

    void SetMinimumSeverity(Severity severity) noexcept;
    void SetCallback(Callback callback);
    [[nodiscard]] Callback GetCallback();
    void ResetCallback();

    void message(Severity severity, const char* format, ...);
    void debug(const char* format, ...);
    void info(const char* format, ...);
    void warning(const char* format, ...);
    void error(const char* format, ...);
    [[noreturn]] void fatal(const char* format, ...);
}
