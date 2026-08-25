#include "uvsr_internal.h"

static void ShowGraphicsStartupError(const wchar_t* message)
{
    MessageBoxW(
        nullptr,
        message,
        L"UVSR Graphics Startup",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

bool SelectGraphicsAdapter(
    DeviceManager* deviceManager,
    DeviceCreationParameters& deviceParams,
    std::vector<GpuAdapterChoice>& adapterChoices)
{
    // Donut's DX12 fallback selects DXGI adapter zero. On hybrid laptops that
    // is commonly the integrated GPU even when a much faster discrete GPU is
    // available. Enumerate once before device creation and prefer the usable
    // adapter with the most dedicated video memory. This is stable across
    // machines and avoids hard-coding a vendor name or a machine-specific
    // adapter index.
    if (!deviceManager->CreateInstance(deviceParams))
    {
        uvsr::log::error("Cannot initialize DXGI while selecting a graphics adapter");
        return false;
    }

    std::vector<AdapterInfo> adapters;
    if (!deviceManager->EnumerateAdapters(adapters) || adapters.empty())
    {
        uvsr::log::error("Cannot enumerate DXGI graphics adapters");
        return false;
    }

    adapterChoices.clear();
    const bool automaticSelection = deviceParams.adapterIndex < 0;
    int bestAdapterIndex = -1;
    uint64_t bestDedicatedVideoMemory = 0;
    for (size_t index = 0; index < adapters.size(); ++index)
    {
        const AdapterInfo& adapter = adapters[index];
        nvrhi::RefCountPtr<IDXGIAdapter1> adapter1;
        DXGI_ADAPTER_DESC1 adapterDescription{};
        if (FAILED(adapter.dxgiAdapter->QueryInterface(IID_PPV_ARGS(&adapter1))) ||
            FAILED(adapter1->GetDesc1(&adapterDescription)) ||
            (adapterDescription.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            continue;
        }

        nvrhi::RefCountPtr<ID3D12Device> testDevice;
        if (FAILED(D3D12CreateDevice(
                adapter.dxgiAdapter,
                deviceParams.featureLevel,
                IID_PPV_ARGS(&testDevice))))
        {
            uvsr::log::warning(
                "Rejected graphics adapter %zu: %s (PCI %04X:%04X) "
                "because it could not create the required Direct3D 12 device",
                index,
                adapter.name.c_str(),
                adapterDescription.VendorId,
                adapterDescription.DeviceId);
            continue;
        }

        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
        shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_5;
        const HRESULT shaderModelResult = testDevice->CheckFeatureSupport(
            D3D12_FEATURE_SHADER_MODEL,
            &shaderModel,
            sizeof(shaderModel));
        const uint32_t highestShaderModel = SUCCEEDED(shaderModelResult)
            ? static_cast<uint32_t>(shaderModel.HighestShaderModel)
            : 0u;
        if (!SupportsRequiredShaderModel(highestShaderModel))
        {
            uvsr::log::warning(
                "Rejected graphics adapter %zu: %s (PCI %04X:%04X) "
                "because UVSR requires Shader Model 6.5; reported %u.%u "
                "(HRESULT 0x%08lX)",
                index,
                adapter.name.c_str(),
                adapterDescription.VendorId,
                adapterDescription.DeviceId,
                (highestShaderModel >> 4u) & 0xFu,
                highestShaderModel & 0xFu,
                static_cast<unsigned long>(shaderModelResult));
            continue;
        }

        constexpr std::array<D3D_FEATURE_LEVEL, 5> FeatureLevels = {
            D3D_FEATURE_LEVEL_12_2,
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0
        };
        D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels{};
        featureLevels.NumFeatureLevels =
            static_cast<UINT>(FeatureLevels.size());
        featureLevels.pFeatureLevelsRequested = FeatureLevels.data();
        const HRESULT featureLevelResult = testDevice->CheckFeatureSupport(
            D3D12_FEATURE_FEATURE_LEVELS,
            &featureLevels,
            sizeof(featureLevels));
        const uint32_t highestFeatureLevel = SUCCEEDED(featureLevelResult)
            ? static_cast<uint32_t>(featureLevels.MaxSupportedFeatureLevel)
            : static_cast<uint32_t>(deviceParams.featureLevel);
        if (!SupportsRequiredFeatureLevel(highestFeatureLevel))
        {
            uvsr::log::warning(
                "Rejected graphics adapter %zu: %s because it does not "
                "support the D3D12 feature-level 11.0 baseline",
                index,
                adapter.name.c_str());
            continue;
        }

        D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignature{};
        rootSignature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if (FAILED(testDevice->CheckFeatureSupport(
                D3D12_FEATURE_ROOT_SIGNATURE,
                &rootSignature,
                sizeof(rootSignature))))
        {
            rootSignature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        const uint32_t resourceBindingTier = SUCCEEDED(
            testDevice->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS,
                &options,
                sizeof(options)))
            ? static_cast<uint32_t>(options.ResourceBindingTier)
            : 0u;
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
        const uint32_t rayTracingTier = SUCCEEDED(
            testDevice->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5,
                &options5,
                sizeof(options5)))
            ? static_cast<uint32_t>(options5.RaytracingTier)
            : 0u;

        LARGE_INTEGER driverVersion{};
        const HRESULT driverVersionResult = adapter1->CheckInterfaceSupport(
            __uuidof(IDXGIDevice),
            &driverVersion);

        bool usesSharedSystemMemory = false;
        D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
        architecture.NodeIndex = 0;
        if (SUCCEEDED(testDevice->CheckFeatureSupport(
                D3D12_FEATURE_ARCHITECTURE1,
                &architecture,
                sizeof(architecture))))
        {
            usesSharedSystemMemory = architecture.UMA != FALSE;
        }
        else
        {
            D3D12_FEATURE_DATA_ARCHITECTURE legacyArchitecture{};
            legacyArchitecture.NodeIndex = 0;
            if (SUCCEEDED(testDevice->CheckFeatureSupport(
                    D3D12_FEATURE_ARCHITECTURE,
                    &legacyArchitecture,
                    sizeof(legacyArchitecture))))
            {
                usesSharedSystemMemory = legacyArchitecture.UMA != FALSE;
            }
        }

        GpuAdapterChoice choice;
        choice.adapterIndex = static_cast<int>(index);
        choice.name = adapter.name;
        choice.dedicatedVideoMemory = adapter.dedicatedVideoMemory;
        choice.vendorId = adapterDescription.VendorId;
        choice.deviceId = adapterDescription.DeviceId;
        choice.usesSharedSystemMemory = usesSharedSystemMemory;
        choice.highestShaderModel = highestShaderModel;
        choice.highestFeatureLevel = highestFeatureLevel;
        choice.rootSignatureVersion =
            static_cast<uint32_t>(rootSignature.HighestVersion);
        choice.resourceBindingTier = resourceBindingTier;
        choice.rayTracingTier = rayTracingTier;
        choice.adapterLuidLowPart = adapterDescription.AdapterLuid.LowPart;
        choice.adapterLuidHighPart = adapterDescription.AdapterLuid.HighPart;
        choice.driverVersion = SUCCEEDED(driverVersionResult)
            ? static_cast<uint64_t>(driverVersion.QuadPart)
            : 0u;
        adapterChoices.push_back(std::move(choice));

        if (automaticSelection &&
            (bestAdapterIndex < 0 || adapter.dedicatedVideoMemory > bestDedicatedVideoMemory))
        {
            bestAdapterIndex = static_cast<int>(index);
            bestDedicatedVideoMemory = adapter.dedicatedVideoMemory;
        }
    }

    if (adapterChoices.empty())
    {
        uvsr::log::error(
            "No hardware graphics adapter supports UVSR's Direct3D 12 "
            "Shader Model 6.5 requirement");
        ShowGraphicsStartupError(
            L"UVSR requires a hardware DirectX 12 graphics adapter with "
            L"Shader Model 6.5 or newer. Update the graphics driver and try "
            L"again. If the adapter cannot support Shader Model 6.5, it is "
            L"not compatible with this UVSR build.");
        return false;
    }

    if (automaticSelection)
        deviceParams.adapterIndex = bestAdapterIndex;

    const auto selectedChoice = std::find_if(
        adapterChoices.begin(),
        adapterChoices.end(),
        [&deviceParams](const GpuAdapterChoice& choice)
        {
            return choice.adapterIndex == deviceParams.adapterIndex;
        });
    if (selectedChoice == adapterChoices.end())
    {
        uvsr::log::error(
            "Requested DXGI adapter %d is unavailable or does not meet "
            "UVSR's Direct3D 12 Shader Model 6.5 requirement",
            deviceParams.adapterIndex);
        ShowGraphicsStartupError(
            L"The requested graphics adapter is unavailable or does not meet "
            L"UVSR's DirectX 12 Shader Model 6.5 requirement. Remove the "
            L"-adapter option or choose a compatible adapter.");
        return false;
    }

    if (selectedChoice->usesSharedSystemMemory)
    {
        uvsr::log::info(
            "Selected graphics adapter %d: %s "
            "(PCI %04X:%04X, shared / UMA, Shader Model %u.%u)",
            selectedChoice->adapterIndex,
            selectedChoice->name.c_str(),
            selectedChoice->vendorId,
            selectedChoice->deviceId,
            (selectedChoice->highestShaderModel >> 4u) & 0xFu,
            selectedChoice->highestShaderModel & 0xFu);
    }
    else
    {
        uvsr::log::info(
            "Selected graphics adapter %d: %s "
            "(PCI %04X:%04X, %llu MiB dedicated VRAM, Shader Model %u.%u)",
            selectedChoice->adapterIndex,
            selectedChoice->name.c_str(),
            selectedChoice->vendorId,
            selectedChoice->deviceId,
            static_cast<unsigned long long>(selectedChoice->dedicatedVideoMemory / (1024ull * 1024ull)),
            (selectedChoice->highestShaderModel >> 4u) & 0xFu,
            selectedChoice->highestShaderModel & 0xFu);
    }
    const uint16_t driverProduct = static_cast<uint16_t>(
        selectedChoice->driverVersion >> 48u);
    const uint16_t driverVersion = static_cast<uint16_t>(
        selectedChoice->driverVersion >> 32u);
    const uint16_t driverSubVersion = static_cast<uint16_t>(
        selectedChoice->driverVersion >> 16u);
    const uint16_t driverBuild = static_cast<uint16_t>(
        selectedChoice->driverVersion);
    uvsr::log::info(
        "Selected adapter capabilities: feature level 0x%04X, root "
        "signature 1.%u, Resource Binding Tier %u, DXR tier %u, "
        "LUID %08X:%08X, driver %u.%u.%u.%u",
        selectedChoice->highestFeatureLevel,
        selectedChoice->rootSignatureVersion >
                static_cast<uint32_t>(D3D_ROOT_SIGNATURE_VERSION_1_0)
            ? 1u
            : 0u,
        selectedChoice->resourceBindingTier,
        selectedChoice->rayTracingTier,
        static_cast<uint32_t>(selectedChoice->adapterLuidHighPart),
        selectedChoice->adapterLuidLowPart,
        driverProduct,
        driverVersion,
        driverSubVersion,
        driverBuild);
    return true;
}

bool ValidateLoadedD3D12Runtime()
{
    HMODULE runtimeModule = GetModuleHandleW(L"D3D12Core.dll");
    if (!runtimeModule)
    {
        uvsr::log::error(
            "UVSR did not load its packaged D3D12Core.dll (Win32 error %lu)",
            GetLastError());
        ShowGraphicsStartupError(
            L"UVSR could not load its packaged DirectX 12 runtime. Reinstall "
            L"UVSR with UVSR Launcher, then try again.");
        return false;
    }

    std::wstring loadedPath(32768, L'\0');
    const DWORD loadedLength = GetModuleFileNameW(
        runtimeModule,
        loadedPath.data(),
        static_cast<DWORD>(loadedPath.size()));
    if (loadedLength == 0 || loadedLength >= loadedPath.size())
    {
        uvsr::log::error(
            "UVSR could not identify the loaded DirectX 12 runtime "
            "(Win32 error %lu)",
            GetLastError());
        ShowGraphicsStartupError(
            L"UVSR could not verify its packaged DirectX 12 runtime. Reinstall "
            L"UVSR with UVSR Launcher, then try again.");
        return false;
    }
    loadedPath.resize(loadedLength);

    std::error_code actualPathError;
    std::error_code expectedPathError;
    std::error_code equivalentPathError;
    const std::filesystem::path actual = std::filesystem::weakly_canonical(
        std::filesystem::path(loadedPath),
        actualPathError);
    const std::filesystem::path expected = std::filesystem::weakly_canonical(
        GetExecutableDirectoryWide() / "D3D12" / "D3D12Core.dll",
        expectedPathError);
    const bool matchesPackage = !actualPathError && !expectedPathError &&
        std::filesystem::equivalent(actual, expected, equivalentPathError) &&
        !equivalentPathError;
    if (!matchesPackage)
    {
        uvsr::log::error(
            "UVSR loaded an unexpected DirectX 12 runtime: %s",
            actual.u8string().c_str());
        ShowGraphicsStartupError(
            L"UVSR did not load the DirectX 12 runtime that belongs to this "
            L"installation. Reinstall UVSR with UVSR Launcher, then try again.");
        return false;
    }

    uvsr::log::info(
        "Loaded packaged DirectX 12 runtime: %s (D3D12SDKVersion %u)",
        actual.u8string().c_str(),
        UVSR_D3D12_AGILITY_SDK_VERSION);
    return true;
}

void CenterWindowInMonitorWorkArea(GLFWwindow* window)
{
    if (!window)
        return;

    HWND nativeWindow = glfwGetWin32Window(window);
    if (!nativeWindow)
        return;

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(
            MonitorFromWindow(
                nativeWindow,
                MONITOR_DEFAULTTONEAREST),
            &monitorInfo))
    {
        return;
    }

    int clientWidth = 0;
    int clientHeight = 0;
    glfwGetWindowSize(window, &clientWidth, &clientHeight);
    RECT nativeRect{};
    RECT visibleRect{};
    if (!GetWindowRect(nativeWindow, &nativeRect))
        return;
    if (FAILED(DwmGetWindowAttribute(
            nativeWindow,
            DWMWA_EXTENDED_FRAME_BOUNDS,
            &visibleRect,
            sizeof(visibleRect))))
    {
        visibleRect = nativeRect;
    }

    const auto alignDownToEight = [](int value)
    {
        return std::max(8, value & ~7);
    };
    const int alignedClientWidth =
        alignDownToEight(clientWidth);
    const int alignedClientHeight =
        alignDownToEight(clientHeight);
    if (clientWidth != alignedClientWidth ||
        clientHeight != alignedClientHeight)
    {
        clientWidth = alignedClientWidth;
        clientHeight = alignedClientHeight;
        glfwSetWindowSize(window, clientWidth, clientHeight);
    }

    // Re-read both rectangles after any client alignment, then move only the
    // native window. Centering the DWM-visible frame in rcWork balances the
    // top gap against the taskbar-side gap without changing 1920 x 1080.
    if (!GetWindowRect(nativeWindow, &nativeRect))
        return;
    if (FAILED(DwmGetWindowAttribute(
            nativeWindow,
            DWMWA_EXTENDED_FRAME_BOUNDS,
            &visibleRect,
            sizeof(visibleRect))))
    {
        visibleRect = nativeRect;
    }
    const int workWidth =
        monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    const int workHeight =
        monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    const int visibleWidth = visibleRect.right - visibleRect.left;
    const int visibleHeight = visibleRect.bottom - visibleRect.top;
    const int targetVisibleLeft =
        monitorInfo.rcWork.left + (workWidth - visibleWidth) / 2;
    const int targetVisibleTop =
        monitorInfo.rcWork.top + (workHeight - visibleHeight) / 2;
    const int nativeLeft =
        targetVisibleLeft - (visibleRect.left - nativeRect.left);
    const int nativeTop =
        targetVisibleTop - (visibleRect.top - nativeRect.top);
    SetWindowPos(
        nativeWindow,
        nullptr,
        nativeLeft,
        nativeTop,
        0,
        0,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER |
            SWP_NOSIZE);
}

int WINAPI WinMain(
    HINSTANCE,
    HINSTANCE,
    LPSTR,
    int)
{
#if defined(UVSR_BUILD_TESTING)
    int verifierArgumentCount = 0;
    for (int index = 1; index < __argc; ++index)
    {
        const bool settingsContract = std::strcmp(
            __argv[index], "--verify-settings-contract") == 0;
        const bool retainedRuntime = std::strcmp(
            __argv[index], "--verify-retained-runtime") == 0;
        if (!settingsContract && !retainedRuntime)
        {
            continue;
        }
        ++verifierArgumentCount;
        g_VerifySettingsContractRequested = settingsContract;
        g_VerifyRetainedRuntimeRequested = retainedRuntime;
    }
    if (verifierArgumentCount > 1)
    {
        std::fprintf(stderr, "only one verifier may run at a time\n");
        return 1;
    }
    if (verifierArgumentCount == 1)
    {
        for (int index = 1; index < __argc; ++index)
        {
            if (std::strcmp(__argv[index], "-debug") != 0 &&
                std::strcmp(
                    __argv[index], "--verify-settings-contract") != 0 &&
                std::strcmp(
                    __argv[index], "--verify-retained-runtime") != 0)
            {
                std::fprintf(stderr,
                    "verifiers accept only the optional -debug argument\n");
                return 1;
            }
        }
    }
#endif
    if (const std::optional<int> diagnostic =
            TryRunEngineDiagnosticCommand(__argc, __argv))
    {
        return *diagnostic;
    }
    InitializeEngineDiagnosticLog();
    ApplyProcessPriority();
    uvsr::log::info(
        "UVSR engine identity: source %s, settings %s, version %s",
        std::string(GetBuiltSourceCommit()).c_str(),
        std::string(GetBuiltSettingsNumberHash()).c_str(),
        std::string(GetBuiltEngineVersion()).c_str());
    constexpr nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::D3D12;

    RendererNvrhiMessageCallback nvrhiMessageCallback;
    DeviceCreationParameters deviceParams;
    deviceParams.messageCallback = &nvrhiMessageCallback;
#if defined(UVSR_BUILD_TESTING)
    deviceParams.backBufferWidth = g_VerifyRetainedRuntimeRequested
        ? 640
        : 1920;
    deviceParams.backBufferHeight = g_VerifyRetainedRuntimeRequested
        ? 360
        : 1080;
#else
    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;
#endif
    deviceParams.swapChainSampleCount = 1;
    deviceParams.swapChainBufferCount = 3;
    deviceParams.featureLevel = D3D_FEATURE_LEVEL_11_0;
    deviceParams.startFullscreen = false;
    deviceParams.enablePerMonitorDPI = true;
    deviceParams.supportExplicitDisplayScaling = true;
    deviceParams.vsyncEnabled = false;

    UvsrStartupOptions startupOptions;
    std::string commandLineError;
    if (!ParseUvsrCommandLine(
            __argc, __argv, startupOptions, commandLineError))
    {
        uvsr::log::error("%s", commandLineError.c_str());
        return 1;
    }
    bool messageLoopSucceeded = true;
    if (startupOptions.width)
        deviceParams.backBufferWidth = *startupOptions.width;
    if (startupOptions.height)
        deviceParams.backBufferHeight = *startupOptions.height;
    if (startupOptions.adapterIndex)
        deviceParams.adapterIndex = *startupOptions.adapterIndex;
    deviceParams.startFullscreen = startupOptions.fullscreen;
#if defined(UVSR_BUILD_TESTING)
    if (startupOptions.debugValidation)
    {
        deviceParams.enableDebugRuntime = true;
        deviceParams.enableNvrhiValidationLayer = true;
        g_RuntimeDebugValidationRequested = true;
    }
#endif
    std::string sceneName = std::move(startupOptions.sceneName);
    std::string startupSettingsSnapshotCode =
        std::move(startupOptions.settingsSnapshotCode);
    if (!VerifyAppLocalD3D12Core())
    {
        uvsr::log::error(
            "The app-local Direct3D 12 runtime failed verification");
        return 1;
    }
    ConfigureD3D12DeviceRemovedDiagnostics(
        deviceParams.enableDebugRuntime);

    DeviceManager* deviceManager = DeviceManager::Create(api);
    std::vector<GpuAdapterChoice> adapterChoices;
    if (!SelectGraphicsAdapter(
            deviceManager,
            deviceParams,
            adapterChoices))
    {
        delete deviceManager;
        return 1;
    }

    const char* apiName = nvrhi::utils::GraphicsAPIToString(
        deviceManager->GetGraphicsAPI());
    const std::string windowTitle =
        "UVSR Engine " + std::string(apiName) +
        " " + std::string(GetBuiltEngineVersion()) +
        " (" + std::string(GetBuiltSourceCommit()).substr(0u, 7u) + ")";
    if (!deviceManager->CreateWindowDeviceAndSwapChain(
            deviceParams,
            windowTitle.c_str()))
    {
        uvsr::log::error(
            "Cannot initialize a %s graphics device",
            apiName);
        delete deviceManager;
        return 1;
    }
    if (!ValidateLoadedD3D12Runtime())
    {
        deviceManager->Shutdown();
        delete deviceManager;
        return 1;
    }

#if defined(UVSR_BUILD_TESTING)
    if (g_VerifySettingsContractRequested ||
        g_VerifyRetainedRuntimeRequested)
        glfwHideWindow(deviceManager->GetWindow());
#endif

    if (!deviceParams.startFullscreen &&
        !deviceParams.startMaximized)
    {
        CenterWindowInMonitorWorkArea(
            deviceManager->GetWindow());
    }

    {
        UIData uiData;
        uiData.GpuAdapterChoices = std::move(adapterChoices);
        uiData.ActiveGpuAdapterIndex = deviceParams.adapterIndex;
        const auto activeAdapter = std::find_if(
            uiData.GpuAdapterChoices.begin(),
            uiData.GpuAdapterChoices.end(),
            [&uiData](const GpuAdapterChoice& adapter)
            {
                return adapter.adapterIndex ==
                    uiData.ActiveGpuAdapterIndex;
            });
        const uint32_t activeVendorId =
            activeAdapter != uiData.GpuAdapterChoices.end()
                ? activeAdapter->vendorId
                : 0u;
        uiData.AdaptiveSync = DefaultAdaptiveSyncMode(
            activeVendorId,
            deviceManager->IsPresentAllowTearingSupported());
        deviceManager->SetPresentAllowTearing(
            AdaptiveSyncRequestsPresentTearing(
                uiData.AdaptiveSync));

        std::shared_ptr<UvsrSceneViewer> demo;
        std::shared_ptr<UIRenderer> gui;
        const auto releaseUiState = [&]()
        {
            if (gui)
                deviceManager->RemoveRenderPass(gui.get());
            if (demo)
                deviceManager->RemoveRenderPass(demo.get());
            gui.reset();
            demo.reset();
        };

        try
        {
            demo = std::make_shared<UvsrSceneViewer>(
                deviceManager,
                uiData,
                sceneName);
            gui = std::make_shared<UIRenderer>(
                deviceManager,
                demo,
                uiData,
                startupSettingsSnapshotCode);
            if (!gui->Init(demo->GetShaderFactory()))
            {
                // The scene worker executes UvsrSceneViewer::LoadScene and may
                // still use the device. Destroy UI ownership and join that
                // worker before shutting the device down on this failure.
                releaseUiState();
                deviceManager->Shutdown();
                delete deviceManager;
                return 1;
            }

            deviceManager->AddRenderPassToBack(demo.get());
            deviceManager->AddRenderPassToBack(gui.get());
            messageLoopSucceeded = deviceManager->RunMessageLoop();
        }
        catch (const RequiredUiFontStartupError& error)
        {
            uvsr::log::error(
                "Cannot initialize required UVSR UI fonts: %s",
                error.what());
            ShowGraphicsStartupError(
                L"UVSR could not initialize the selected UI fonts. If Codex "
                L"was selected, restore the standard Windows Segoe UI fonts. "
                L"Otherwise, reinstall UVSR with UVSR Launcher, then try "
                L"again.");
            releaseUiState();
            deviceManager->Shutdown();
            delete deviceManager;
            return 1;
        }

    }

    deviceManager->Shutdown();
    messageLoopSucceeded &= !deviceManager->HasRuntimeFailure();
    delete deviceManager;

    if (!messageLoopSucceeded)
        return 1;

#if defined(UVSR_BUILD_TESTING)
    if (g_VerifySettingsContractRequested)
        return g_VerifySettingsContractResult;
    if (g_VerifyRetainedRuntimeRequested)
        return g_VerifyRetainedRuntimeResult;
#endif

    if (g_StartupSettingsSnapshotFailed)
        return 1;

    if (g_RestartRequested && !RestartCurrentProcess())
        return 1;

    return 0;
}
