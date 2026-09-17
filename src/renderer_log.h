#pragma once

namespace uvsr::log
{
    inline constexpr unsigned MessageCapacity = 4096u;

    enum class Severity
    {
        None = 0,
        Debug,
        Info,
        Warning,
        Error,
        Fatal
    };

    // context is borrowed through every in-flight call. replacement does not wait
    // for old calls; the caller quiesces producers before destroying its context.
    struct Callback
    {
        void (*function)(void* context, Severity severity, const char* message) = nullptr;
        void* context = nullptr;
    };

    void SetMinimumSeverity(Severity severity) noexcept;
    void SetCallback(Callback callback) noexcept;
    [[nodiscard]] Callback GetCallback() noexcept;

    void debug(const char* format, ...);
    void info(const char* format, ...);
    void warning(const char* format, ...);
    void error(const char* format, ...);
    [[noreturn]] void fatal(const char* format, ...);
}
