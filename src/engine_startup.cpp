#include "engine_startup.h"
#include "renderer_log.h"
#include "sha256.h"
#include "windows_executable_path.h"

#include <Windows.h>
#include <ShlObj.h>
#include <directx/d3d12.h>

#include <string.h>

namespace uvsr
{
    namespace
    {
        constexpr LONGLONG RequiredD3D12CoreSize = 5'027'640;
        constexpr char RequiredD3D12CoreSha256[] =
            "eddf4cff4eda8162624b88694ad2adf4b09bc5aee6339191f39adf8ae48b41e7";
    }

    void ApplyProcessPriority() noexcept
    {
        if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
        {
            log::warning(
                "UVSR could not request High process priority (Win32 error %lu)",
                GetLastError());
        }
    }

    void InitializeEngineDiagnosticLog() noexcept
    {
        WindowsPath logPath;
        WindowsPathResult pathResult;
        bool havePath = false;
#if defined(UVSR_BUILD_TESTING)
        // each developer executable owns its state independently of installed builds.
        WindowsPath directory;
        havePath = GetExecutableDirectoryWide(directory, pathResult) &&
            JoinWindowsRelativePath(directory.Data(), L"state\\logs\\uvsr-engine.log", logPath, pathResult);
#else
        PWSTR localAppData = nullptr;
        const HRESULT folderResult = SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_CREATE,
            nullptr,
            &localAppData);
        if (FAILED(folderResult) || !localAppData)
        {
            CoTaskMemFree(localAppData);
            log::warning(
                "UVSR could not locate LocalAppData for its engine diagnostic "
                "log (HRESULT 0x%08lX)",
                static_cast<unsigned long>(folderResult));
            return;
        }

        havePath = JoinWindowsRelativePath(localAppData, L"UVSR\\logs\\uvsr-engine.log", logPath, pathResult);
        CoTaskMemFree(localAppData);
#endif
        if (!havePath)
        {
            log::warning("UVSR could not prepare its engine diagnostic log path (Win32 error %lu)",
                static_cast<unsigned long>(pathResult.nativeCode));
            return;
        }
        (void)InitializeEngineDiagnosticLog(logPath.Data());
    }

    bool VerifyD3D12CoreFile(const wchar_t* path) noexcept
    {
        if (!path || !path[0])
            return false;
        const HANDLE file = CreateFileW(path, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        const bool measured = GetFileSizeEx(file, &size) != FALSE;
        const bool closed = CloseHandle(file) != FALSE;
        if (!measured || !closed || size.QuadPart != RequiredD3D12CoreSize)
            return false;
        Sha256Digest digest;
        Sha256Result result;
        return Sha256File(path, digest, result) && strcmp(digest.text, RequiredD3D12CoreSha256) == 0;
    }

    bool VerifyAppLocalD3D12Core() noexcept
    {
        WindowsPath directory;
        WindowsPath core;
        WindowsPathResult result;
        if (!GetExecutableDirectoryWide(directory, result) ||
            !JoinWindowsRelativePath(directory.Data(), L"D3D12\\D3D12Core.dll", core, result) ||
            !VerifyD3D12CoreFile(core.Data()))
        {
            log::error(
                "App-local D3D12Core.dll is missing or differs from the "
                "pinned Direct3D Agility SDK 1.619.5 runtime");
            return false;
        }
        return true;
    }

    void ConfigureD3D12DeviceRemovedDiagnostics(bool enableDiagnostics) noexcept
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
