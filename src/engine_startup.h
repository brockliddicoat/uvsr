#pragma once

#include "engine_diagnostic_log.h"

namespace uvsr
{
    void ApplyProcessPriority() noexcept;
    void InitializeEngineDiagnosticLog() noexcept;
    [[nodiscard]] bool VerifyD3D12CoreFile(
        const wchar_t* path) noexcept;
    [[nodiscard]] bool VerifyAppLocalD3D12Core() noexcept;
    void ConfigureD3D12DeviceRemovedDiagnostics(bool enableDiagnostics) noexcept;
}
