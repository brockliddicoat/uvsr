#pragma once

#include <stdint.h>

namespace uvsr
{
    // initialization and shutdown have one owner. the clock context remains
    // borrowed until producers quiesce and shutdown finishes.
    struct EngineDiagnosticLogClock
    {
        int64_t (*nanoseconds)(void* context) noexcept = nullptr;
        void* context = nullptr;
    };

    [[nodiscard]] bool InitializeEngineDiagnosticLog(
        const wchar_t* path, EngineDiagnosticLogClock clock = {}) noexcept;
    void ShutdownEngineDiagnosticLog() noexcept;

#if defined(UVSR_ENGINE_LOG_TEST_HOOKS)
    enum class EngineDiagnosticLogFailure
    {
        None, Allocation, Open, Buffer, Write, Flush, Close
    };
    void FailEngineDiagnosticLogOnce(EngineDiagnosticLogFailure failure) noexcept;
#endif
}
