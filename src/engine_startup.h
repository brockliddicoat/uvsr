#pragma once

#include <chrono>
#include <filesystem>
#include <functional>

namespace uvsr
{
    using EngineDiagnosticLogClock = std::function<
        std::chrono::steady_clock::time_point()>;

    void ApplyProcessPriority();
    void InitializeEngineDiagnosticLog();
    [[nodiscard]] bool InitializeEngineDiagnosticLog(
        const std::filesystem::path& logPath,
        EngineDiagnosticLogClock clock = {});
    void ShutdownEngineDiagnosticLog();
    [[nodiscard]] bool VerifyD3D12CoreFile(
        const std::filesystem::path& path);
    [[nodiscard]] bool VerifyAppLocalD3D12Core();
    void ConfigureD3D12DeviceRemovedDiagnostics(bool enableDiagnostics);
}
