#include "engine_startup.h"
#include "renderer_log.h"

#include <Windows.h>
#include <ShlObj.h>
#include <bcrypt.h>
#include <directx/d3d12.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace uvsr
{
    namespace
    {
        std::mutex g_LogMutex;
        std::ofstream g_Log;
        log::Callback g_Downstream;
        std::chrono::steady_clock::time_point g_LastFlush;
        std::chrono::steady_clock::time_point g_LastMessageWrite;
        log::Severity g_LastSeverity = log::Severity::None;
        std::string g_LastMessage;
        uint64_t g_SuppressedRepeatCount = 0u;
        EngineDiagnosticLogClock g_LogClock;
        bool g_LogCallbackInstalled = false;
        bool g_ShutdownRegistered = false;
        constexpr uintmax_t RequiredD3D12CoreSize = 5'027'640u;
        constexpr std::string_view RequiredD3D12CoreSha256 =
            "eddf4cff4eda8162624b88694ad2adf4b09bc5aee6339191f39adf8ae48b41e7";

        [[nodiscard]] std::string Sha256File(
            const std::filesystem::path& path)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            DWORD objectSize = 0u;
            DWORD resultSize = 0u;
            std::vector<unsigned char> object;
            std::array<unsigned char, 32u> digest{};
            const auto cleanup = [&]()
            {
                if (hash)
                    BCryptDestroyHash(hash);
                if (algorithm)
                    BCryptCloseAlgorithmProvider(algorithm, 0u);
            };

            NTSTATUS status = BCryptOpenAlgorithmProvider(
                &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0u);
            if (status >= 0)
            {
                status = BCryptGetProperty(
                    algorithm,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&objectSize),
                    sizeof(objectSize),
                    &resultSize,
                    0u);
            }
            if (status >= 0)
            {
                object.resize(objectSize);
                status = BCryptCreateHash(
                    algorithm,
                    &hash,
                    object.data(),
                    static_cast<ULONG>(object.size()),
                    nullptr,
                    0u,
                    0u);
            }

            std::ifstream input(path, std::ios::binary);
            std::vector<char> buffer(1024u * 1024u);
            while (status >= 0 && input)
            {
                input.read(buffer.data(),
                    static_cast<std::streamsize>(buffer.size()));
                const std::streamsize count = input.gcount();
                if (count > 0)
                {
                    status = BCryptHashData(
                        hash,
                        reinterpret_cast<PUCHAR>(buffer.data()),
                        static_cast<ULONG>(count),
                        0u);
                }
            }
            if (!input.eof() || status < 0)
            {
                cleanup();
                return {};
            }
            status = BCryptFinishHash(
                hash, digest.data(), static_cast<ULONG>(digest.size()), 0u);
            cleanup();
            if (status < 0)
                return {};

            constexpr char Hex[] = "0123456789abcdef";
            std::string text;
            text.reserve(64u);
            for (const unsigned char byte : digest)
            {
                text.push_back(Hex[byte >> 4u]);
                text.push_back(Hex[byte & 0x0fu]);
            }
            return text;
        }

        [[nodiscard]] const char* GetSeverityName(
            log::Severity severity) noexcept
        {
            switch (severity)
            {
            case log::Severity::Debug: return "debug";
            case log::Severity::Info: return "info";
            case log::Severity::Warning: return "warning";
            case log::Severity::Error: return "error";
            case log::Severity::Fatal: return "fatal";
            default: return "none";
            }
        }

        [[nodiscard]] std::chrono::steady_clock::time_point
            GetMonotonicTime()
        {
            return g_LogClock
                ? g_LogClock()
                : std::chrono::steady_clock::now();
        }

        void WriteDiagnosticLine(
            log::Severity severity,
            const std::string& message)
        {
            const std::time_t now = std::time(nullptr);
            std::tm localTime{};
            localtime_s(&localTime, &now);
            g_Log << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
                  << " [" << GetSeverityName(severity) << "] "
                  << message << '\n';
        }

        void WriteSuppressedWarningSummary()
        {
            if (g_SuppressedRepeatCount == 0u)
                return;
            WriteDiagnosticLine(
                log::Severity::Warning,
                "Previous warning repeated " +
                    std::to_string(g_SuppressedRepeatCount) +
                    " additional times");
            g_SuppressedRepeatCount = 0u;
        }

        void EngineDiagnosticLogCallback(
            log::Severity severity,
            const char* message)
        {
            log::Callback downstream;
            {
                std::lock_guard<std::mutex> lock(g_LogMutex);
                const auto monotonicNow = GetMonotonicTime();
                const char* safeMessage = message ? message : "";
                const bool repeatedWarning =
                    severity == log::Severity::Warning &&
                    g_LastSeverity == severity &&
                    g_LastMessage == safeMessage;
                const bool suppressRepeatedWarning = repeatedWarning &&
                    monotonicNow - g_LastMessageWrite <
                        std::chrono::seconds(5);
                bool wroteLine = false;
                if (suppressRepeatedWarning)
                {
                    ++g_SuppressedRepeatCount;
                }
                else
                {
                    WriteSuppressedWarningSummary();
                    WriteDiagnosticLine(severity, safeMessage);
                    g_LastSeverity = severity;
                    g_LastMessage = safeMessage;
                    g_LastMessageWrite = monotonicNow;
                    wroteLine = true;
                }

                const bool urgent =
                    severity == log::Severity::Error ||
                    severity == log::Severity::Fatal;
                if (urgent || (wroteLine &&
                    monotonicNow - g_LastFlush >=
                        std::chrono::seconds(1)))
                {
                    g_Log.flush();
                    g_LastFlush = monotonicNow;
                }
                downstream = g_Downstream;
            }
            if (downstream)
                downstream(severity, message);
        }
    }

    void ApplyProcessPriority()
    {
        if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
        {
            log::warning(
                "UVSR could not request High process priority (Win32 error %lu)",
                GetLastError());
        }
    }

    void InitializeEngineDiagnosticLog()
    {
        PWSTR localAppData = nullptr;
        const HRESULT folderResult = SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_CREATE,
            nullptr,
            &localAppData);
        if (FAILED(folderResult) || !localAppData)
        {
            log::warning(
                "UVSR could not locate LocalAppData for its engine diagnostic "
                "log (HRESULT 0x%08lX)",
                static_cast<unsigned long>(folderResult));
            return;
        }

        const std::filesystem::path logPath =
            std::filesystem::path(localAppData) / "UVSR" / "logs" /
            "uvsr-engine.log";
        CoTaskMemFree(localAppData);

        (void)InitializeEngineDiagnosticLog(logPath);
    }

    bool InitializeEngineDiagnosticLog(
        const std::filesystem::path& logPath,
        EngineDiagnosticLogClock clock)
    {
        ShutdownEngineDiagnosticLog();
        if (logPath.empty())
            return false;

        std::error_code directoryError;
        const std::filesystem::path directory = logPath.parent_path();
        if (!directory.empty())
            std::filesystem::create_directories(directory, directoryError);
        if (directoryError)
        {
            log::warning(
                "UVSR could not create its engine diagnostic log directory: %s",
                directoryError.message().c_str());
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(g_LogMutex);
            g_Log.open(logPath, std::ios::out | std::ios::trunc);
            if (!g_Log)
            {
                log::warning(
                    "UVSR could not open its engine diagnostic log: %s",
                    logPath.u8string().c_str());
                return false;
            }
            g_LogClock = std::move(clock);
            g_LastFlush = GetMonotonicTime();
            g_LastMessageWrite = g_LastFlush;
            g_LastSeverity = log::Severity::None;
            g_LastMessage.clear();
            g_SuppressedRepeatCount = 0u;
            g_Downstream = log::GetCallback();
            g_LogCallbackInstalled = true;
        }
        log::SetCallback(EngineDiagnosticLogCallback);
        if (!g_ShutdownRegistered)
        {
            std::atexit(ShutdownEngineDiagnosticLog);
            g_ShutdownRegistered = true;
        }
        log::info(
            "Engine diagnostic log: %s",
            logPath.u8string().c_str());
        return true;
    }

    void ShutdownEngineDiagnosticLog()
    {
        log::Callback downstream;
        bool restoreCallback = false;
        {
            std::lock_guard<std::mutex> lock(g_LogMutex);
            if (!g_LogCallbackInstalled)
                return;
            WriteSuppressedWarningSummary();
            g_Log.flush();
            g_Log.close();
            downstream = std::move(g_Downstream);
            g_LogClock = {};
            g_LastSeverity = log::Severity::None;
            g_LastMessage.clear();
            g_LastMessageWrite = {};
            g_LastFlush = {};
            g_LogCallbackInstalled = false;
            restoreCallback = true;
        }
        if (restoreCallback)
            log::SetCallback(std::move(downstream));
    }

    bool VerifyD3D12CoreFile(const std::filesystem::path& path)
    {
        std::error_code error;
        const uintmax_t size = std::filesystem::file_size(path, error);
        if (error || size != RequiredD3D12CoreSize)
            return false;
        return Sha256File(path) == RequiredD3D12CoreSha256;
    }

    bool VerifyAppLocalD3D12Core()
    {
        std::vector<wchar_t> executablePath(512u);
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(
                nullptr,
                executablePath.data(),
                static_cast<DWORD>(executablePath.size()));
            if (length == 0u)
                return false;
            if (length < executablePath.size() - 1u)
            {
                executablePath.resize(length);
                break;
            }
            executablePath.resize(executablePath.size() * 2u);
        }

        const std::filesystem::path core =
            std::filesystem::path(executablePath.data()).parent_path() /
            "D3D12" / "D3D12Core.dll";
        if (!VerifyD3D12CoreFile(core))
        {
            log::error(
                "App-local D3D12Core.dll is missing or differs from the "
                "pinned Direct3D Agility SDK 1.619.5 runtime");
            return false;
        }
        return true;
    }

    void ConfigureD3D12DeviceRemovedDiagnostics(bool enableDiagnostics)
    {
#if defined(UVSR_BUILD_TESTING)
        if (enableDiagnostics)
        {
            ID3D12Debug* debugController = nullptr;
            const HRESULT debugResult = D3D12GetDebugInterface(
                IID_PPV_ARGS(&debugController));
            if (SUCCEEDED(debugResult) && debugController)
            {
                debugController->EnableDebugLayer();
                debugController->Release();
            }
            else
            {
                log::warning(
                    "The D3D12 debug layer is unavailable before adapter probing "
                    "(HRESULT 0x%08lX)",
                    static_cast<unsigned long>(debugResult));
            }
        }
#else
        (void)enableDiagnostics;
#endif

        ID3D12DeviceRemovedExtendedDataSettings* settings = nullptr;
        const HRESULT settingsResult = D3D12GetDebugInterface(
            IID_PPV_ARGS(&settings));
        if (FAILED(settingsResult) || !settings)
        {
            log::warning(
                "D3D12 device-removed diagnostics are unavailable "
                "(HRESULT 0x%08lX)",
                static_cast<unsigned long>(settingsResult));
            return;
        }

        settings->SetAutoBreadcrumbsEnablement(
            D3D12_DRED_ENABLEMENT_FORCED_ON);
        settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        ID3D12DeviceRemovedExtendedDataSettings1* settings1 = nullptr;
        if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&settings1))) &&
            settings1)
        {
            settings1->SetBreadcrumbContextEnablement(
                D3D12_DRED_ENABLEMENT_FORCED_ON);
            settings1->Release();
        }
        settings->Release();
        log::info(
            "Enabled D3D12 automatic breadcrumbs, page-fault tracking, and "
            "available breadcrumb contexts before device creation");
    }
}
