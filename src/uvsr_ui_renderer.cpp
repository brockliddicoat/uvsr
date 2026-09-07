#include "uvsr_ui_internal.h"
#include "windows_executable_path.h"

auto UIRenderer::FormatFrontEllipsisUtf8(
        std::string_view source,
        size_t maximumCodePoints) -> FrontEllipsisText {
        const char* const begin = source.data();
        const char* cursor = begin;
        const char* const end = begin + source.size();
        size_t codePointCount = 0;
        while (cursor < end && codePointCount < maximumCodePoints)
        {
            unsigned int codePoint = 0;
            const int byteCount = ImTextCharFromUtf8(
                &codePoint,
                cursor,
                end);
            cursor += byteCount > 0 ? byteCount : 1;
            ++codePointCount;
        }

        FrontEllipsisText result;
        result.truncated = cursor < end;
        result.display.assign(begin, cursor);
        if (result.truncated)
            result.display += "...";
        return result;
    }

auto UIRenderer::GetSceneLoadTimingDatabasePath() -> std::filesystem::path {
#if defined(UVSR_BUILD_TESTING)
        return GetExecutableDirectoryWide() / "state" / "scene-load-history-v1.txt";
#else
        const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
        if (!localAppData || localAppData[0] == L'\0')
            return {};
        return std::filesystem::path(localAppData) /
            L"UVSR" / L"scene-load-history-v1.txt";
#endif
    }

auto UIRenderer::LoadSceneLoadTimingDatabase() -> void {
        const std::filesystem::path path =
            GetSceneLoadTimingDatabasePath();
        if (path.empty())
            return;

        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
            return;

        if (!ReadSceneLoadTimingDatabase(input, m_SceneLoadTiming))
        {
    uvsr::log::warning(
                "Ignoring invalid scene loading history at %s",
                path.generic_string().c_str());
            return;
        }
    }

auto UIRenderer::SaveSceneLoadTimingDatabase() const -> void {
        const std::filesystem::path path =
            GetSceneLoadTimingDatabasePath();
        if (path.empty())
            return;

        std::error_code error;
        std::filesystem::create_directories(
            path.parent_path(),
            error);
        if (error)
        {
            uvsr::log::warning(
                "Could not create scene loading history directory: %s",
                error.message().c_str());
            return;
        }

        std::filesystem::path temporaryPath = path;
        temporaryPath += L".tmp";
        std::ofstream output(
            temporaryPath,
            std::ios::binary | std::ios::trunc);
        const bool serialized = output.is_open() &&
            WriteSceneLoadTimingDatabase(output, m_SceneLoadTiming);
        output.flush();
        const bool flushed = output.good();
        output.close();
        if (!serialized || !flushed)
        {
            uvsr::log::warning(
                "Could not write scene loading history at %s",
                temporaryPath.generic_string().c_str());
            return;
        }

        if (!MoveFileExW(
                temporaryPath.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            uvsr::log::warning(
                "Could not publish scene loading history (Win32 error %lu)",
                GetLastError());
        }
    }

auto UIRenderer::GetWindowsFontsDirectory() -> std::filesystem::path {
        std::vector<wchar_t> buffer(MAX_PATH);
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const UINT length = GetWindowsDirectoryW(
                buffer.data(),
                static_cast<UINT>(buffer.size()));
            if (length == 0u)
                return {};
            if (length < buffer.size())
            {
                return std::filesystem::path(
                    std::wstring(buffer.data(), length)) / L"Fonts";
            }
            buffer.resize(static_cast<std::size_t>(length) + 1u);
        }
        return {};
    }

UIRenderer::UIRenderer(
        DeviceManager* deviceManager,
        std::shared_ptr<UvsrSceneViewer> app,
        UIData& ui,
        std::string startupSettingsSnapshotCode)
        : ImGui_Renderer(deviceManager)
        , m_app(app)
        , m_StartupSettingsSnapshotCode(
            std::move(startupSettingsSnapshotCode))
        , m_ui(ui) {
        NativeFileSystem windowsFileSystem;
        const auto directory = GetWindowsFontsDirectory();
        try
        {
            m_UiBodyFont = CreateFontFromFile(windowsFileSystem, directory / L"seguisb.ttf", 16.f);
            m_UiHeaderFont = CreateFontFromFile(windowsFileSystem, directory / L"segoeuib.ttf", 16.f);
            if (!m_UiBodyFont || !m_UiHeaderFont ||
                !m_UiBodyFont->HasFontData() || !m_UiHeaderFont->HasFontData())
                throw std::runtime_error("font data is unavailable");
        }
        catch (const std::exception& error)
        {
            throw RequiredUiFontStartupError(std::string("UVSR requires Windows Segoe UI Semibold and Bold. ") +
                "Restore seguisb.ttf and segoeuib.ttf in Windows Fonts, then restart UVSR. " + error.what());
        }

        ImGui::GetIO().IniFilename = nullptr;
        LoadSceneLoadTimingDatabase();
        m_PresentationWaitTimer = CreateWaitableTimerExW(nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
        GetDeviceManager()->m_callbacks.beforePresent =
            [this](donut::app::DeviceManager&, uint32_t) { PacePresentation(); };
    }

auto UIRenderer::Animate(float elapsedTimeSeconds) -> void {
        AdvanceDisplayPresentation(elapsedTimeSeconds);
        if (m_RequiredFontsReady)
        {
            ImGui_Renderer::Animate(elapsedTimeSeconds);
            return;
        }

        try
        {
            ImGui_Renderer::Animate(elapsedTimeSeconds);
            if (!m_UiBodyFont->GetScaledFont() || !m_UiHeaderFont->GetScaledFont())
                throw RequiredUiFontStartupError("UVSR could not initialize its Segoe UI fonts.");
            m_RequiredFontsReady = true;
        }
        catch (const RequiredUiFontStartupError&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            throw RequiredUiFontStartupError(
                std::string("UVSR could not initialize its required UI font ") +
                "atlas: " + error.what() +
                ". Reinstall UVSR with UVSR Launcher.");
        }
    }

auto UIRenderer::Init(std::shared_ptr<ShaderFactory> shaderFactory) -> bool {
        if (!ImGui_Renderer::Init(shaderFactory))
            return false;

        m_PixelZoomPass = std::make_unique<PixelZoomPass>(
            GetDevice(),
            m_app->GetRendererShaderFactory(),
            m_app->GetRendererCommonPasses());
        return true;
    }

#if defined(UVSR_BUILD_TESTING)
bool UIRenderer::SelectRuntimeDiagnostic(SettingId id, const std::string& selector,
    const char* emptyError, const char* failurePrefix, std::string& error)
{
    std::string selectionError;
    if (!selector.empty() && (ApplySettingValue(id, UiSettingsValue::Selector(selector), selectionError) ||
        selectionError.rfind("No change: ", 0u) == 0u))
        return true;
    error = selector.empty() ? emptyError : std::string(failurePrefix) + selectionError;
    return false;
}

auto UIRenderer::ChangeRuntimeDiagnosticMaterial(
        std::string& error) -> bool {
        const std::shared_ptr<Scene> scene = m_app->GetScene();
        if (!scene || !scene->GetSceneGraph())
        {
            error = "no loaded scene provides a material to change";
            return false;
        }
        const auto& materials = scene->GetSceneGraph()->GetMaterials();
        auto selected = std::find_if(
            materials.begin(), materials.end(),
            [](const std::shared_ptr<Material>& material)
            {
                return bool(material) &&
                    material->materialID >= 0 &&
                    std::isfinite(material->normalTextureScale);
            });
        if (selected == materials.end())
        {
            error = "the loaded scene has no finite editable material";
            return false;
        }

        const std::string selector = FormatSettingsSnapshotMaterialToken(
            false,
            static_cast<std::uint32_t>((*selected)->materialID));
        if (!SelectRuntimeDiagnostic(SettingId::MaterialSelected, selector,
            "the diagnostic material has no canonical selector", "could not select the diagnostic material: ", error))
            return false;
        const float replacement =
            (*selected)->normalTextureScale >= 0.f ? -1.f : 1.f;
        return ApplySettingValue(
            SettingId::MaterialSelectedNormalScale,
            UiSettingsValue::Float(replacement), error);
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UIRenderer::ChangeRuntimeDiagnosticLight(
        std::string& error) -> bool {
        const std::shared_ptr<DirectionalLight> light =
            m_app->GetPrimaryDirectionalLight();
        if (!light || m_app->IsFlashlight(light) ||
            !std::isfinite(light->angularSize))
        {
            error = "the loaded scene has no finite directional light";
            return false;
        }

        const auto& lights = m_app->GetEditableLights();
        const auto selected = std::find(lights.begin(), lights.end(), light);
        if (selected == lights.end())
        {
            error = "the diagnostic directional light is not editable";
            return false;
        }
        const std::string selector = FormatSettingsSnapshotLightToken(
            static_cast<std::size_t>(std::distance(lights.begin(), selected)),
            light->GetName());
        if (!SelectRuntimeDiagnostic(SettingId::LightSelected, selector,
            "the diagnostic directional light has no canonical selector", "could not select the diagnostic directional light: ", error))
            return false;
        const float replacement = light->angularSize < 10.f ? 20.f : 0.f;
        return ApplySettingValue(
            SettingId::LightSelectedAngularSize,
            UiSettingsValue::Float(replacement), error);
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UIRenderer::SelectRuntimeDiagnosticFlashlight(
        std::string& error) -> bool {
        const auto flashlight = std::find_if(
            m_app->GetEditableLights().begin(),
            m_app->GetEditableLights().end(),
            [this](const std::shared_ptr<Light>& light)
            {
                return m_app->IsFlashlight(light);
            });
        if (flashlight == m_app->GetEditableLights().end())
        {
            error = "the loaded scene has no retained flashlight";
            return false;
        }

        const std::string selector = FormatSettingsSnapshotLightToken(
            static_cast<std::size_t>(std::distance(
                m_app->GetEditableLights().begin(), flashlight)),
            (*flashlight)->GetName());
        return SelectRuntimeDiagnostic(SettingId::LightSelected, selector,
            "the retained flashlight has no canonical selector", "could not select the retained flashlight: ", error);
    }

auto UIRenderer::ToggleRuntimeDiagnosticFlashlight(
        std::string& error) -> bool {
        if (!SelectRuntimeDiagnosticFlashlight(error))
            return false;
        return ApplySettingValue(
            SettingId::LightSelectedFlashlightEnabled,
            UiSettingsValue::Boolean(!m_ui.FlashlightEnabled), error);
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UIRenderer::DriveRetainedRuntimeDiagnostic() -> void {
        using DiagnosticClock = RetainedRuntimeDiagnosticState::Clock;
        const DiagnosticClock::time_point now = DiagnosticClock::now();
        if (m_RetainedRuntimeStartup.time_since_epoch().count() == 0)
            m_RetainedRuntimeStartup = now;

        const auto finish = [&](const RetainedRuntimeDirective& directive)
        {
            m_RetainedRuntimePathReselectionPending = false;
            const bool passed =
                directive.kind == RetainedRuntimeDirectiveKind::FinishPass;
            const std::string caseName = directive.runtimeCase
                ? directive.runtimeCase->name
                : "startup";
            if (!passed)
            {
                const std::string failure =
                    BuildRetainedRuntimeFailureJson(
                        caseName, directive.payload);
                std::fprintf(stderr, "%s\n", failure.c_str());
            }
            const size_t passedCases = m_RetainedRuntimeDiagnostic
                ? m_RetainedRuntimeDiagnostic->PassedCaseCount()
                : 0u;
            const size_t totalCases = m_RetainedRuntimeDiagnostic
                ? m_RetainedRuntimeDiagnostic->TotalCaseCount()
                : 0u;
            const long long elapsedMilliseconds =
                m_RetainedRuntimeDiagnostic
                ? static_cast<long long>(
                    m_RetainedRuntimeDiagnostic->ElapsedMilliseconds(now))
                : static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - m_RetainedRuntimeStartup).count());
            const std::string summary = BuildRetainedRuntimeSummaryJson(
                m_RetainedRuntimeProvenance,
                passed,
                passedCases,
                totalCases,
                elapsedMilliseconds);
            std::fprintf(
                passed ? stdout : stderr, "%s\n", summary.c_str());
            std::fflush(passed ? stdout : stderr);
            g_VerifyRetainedRuntimeResult = passed ? 0 : 1;
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
        };

        if (!m_RetainedRuntimeDiagnostic)
        {
            if (now - m_RetainedRuntimeStartup > std::chrono::minutes(3))
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.payload = "default scene startup exceeded 3 minutes";
                finish(failure);
                return;
            }
            if (m_app->IsSceneBusy())
                return;
            if (!m_app->IsSceneLoaded())
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.payload = "default scene did not finish loading";
                finish(failure);
                return;
            }

            const auto& lights = m_app->GetEditableLights();
            const auto flashlight = std::find_if(
                lights.begin(), lights.end(),
                [this](const std::shared_ptr<Light>& light)
                {
                    return m_app->IsFlashlight(light);
                });
            if (flashlight == lights.end())
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.payload = "retained flashlight is unavailable";
                finish(failure);
                return;
            }
            const std::string selector = FormatSettingsSnapshotLightToken(
                static_cast<std::size_t>(
                    std::distance(lights.begin(), flashlight)),
                (*flashlight)->GetName());
            std::string selectionError;
            if (!SelectRuntimeDiagnostic(SettingId::LightSelected, selector,
                "retained flashlight has no canonical selector", "could not select retained flashlight: ", selectionError))
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.payload = std::move(selectionError);
                finish(failure);
                return;
            }

            m_RetainedRuntimeProvenance.settingsHash =
                GetBuiltSettingsNumberHash();
            m_RetainedRuntimeProvenance.engineVersion =
                GetBuiltEngineVersion();
            m_RetainedRuntimeProvenance.sourceCommit =
                GetBuiltSourceCommit();
            m_RetainedRuntimeProvenance.sourceIdentity =
                GetBuiltSourceIdentity();
            m_RetainedRuntimeProvenance.sourceClean =
                IsBuiltSourceTreeClean();
            m_RetainedRuntimeProvenance.production =
                IsBuiltProduction();
            m_RetainedRuntimeProvenance.configuration =
                GetBuiltConfiguration();
            m_RetainedRuntimeProvenance.debugLayerRequested =
                g_RuntimeDebugValidationRequested;
            m_RetainedRuntimeProvenance.nvrhiValidationRequested =
                g_RuntimeDebugValidationRequested;
            m_RetainedRuntimeProvenance.executablePath =
                (GetExecutableDirectoryWide() / "uvsr-engine.exe").u8string();
            const char* packagePath = std::getenv(
                "UVSR_RUNTIME_PACKAGE_PATH");
            const char* executableSha256 = std::getenv(
                "UVSR_RUNTIME_ENGINE_SHA256");
            if (packagePath)
            {
                m_RetainedRuntimeProvenance.packagePath = packagePath;
            }
            if (executableSha256)
            {
                m_RetainedRuntimeProvenance.executableSha256 =
                    executableSha256;
            }
            const bool canonicalSha256 =
                m_RetainedRuntimeProvenance.executableSha256.size() == 64u &&
                std::all_of(
                    m_RetainedRuntimeProvenance.executableSha256.begin(),
                    m_RetainedRuntimeProvenance.executableSha256.end(),
                    [](unsigned char character)
                    {
                        return (character >= '0' && character <= '9') ||
                            (character >= 'a' && character <= 'f');
                    });
            const std::filesystem::path declaredPackage =
                std::filesystem::u8path(
                    m_RetainedRuntimeProvenance.packagePath);
            std::error_code packageError;
            const bool executableMatchesPackage =
                declaredPackage.is_absolute() &&
                std::filesystem::equivalent(
                    declaredPackage / "bin/uvsr-engine.exe",
                    std::filesystem::u8path(
                        m_RetainedRuntimeProvenance.executablePath),
                    packageError) &&
                !packageError;
            if (!g_RuntimeDebugValidationRequested ||
                !canonicalSha256 || !executableMatchesPackage)
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.payload =
                    !g_RuntimeDebugValidationRequested
                        ? "retained runtime verification requires -debug"
                        : !canonicalSha256
                            ? "UVSR_RUNTIME_ENGINE_SHA256 must be 64 lowercase hexadecimal characters"
                            : "UVSR_RUNTIME_PACKAGE_PATH must be the absolute package root containing this bin/uvsr-engine.exe";
                finish(failure);
                return;
            }

            const std::vector<SceneCatalogEntry>& scenes =
                m_app->GetAvailableScenes();
            const SceneCatalogEntry* bistroEntry = FindSceneCatalogEntry(
                scenes,
                (m_app->GetSceneDir() /
                    "bistro_interior_retextured/"
                    "bistro_interior_retextured.scene.json")
                    .lexically_normal().generic_string());
            const SceneCatalogEntry* sanMiguelEntry = FindSceneCatalogEntry(
                scenes,
                (m_app->GetSceneDir() /
                    "san_miguel_retextured/"
                    "san_miguel_retextured.scene.json")
                    .lexically_normal().generic_string());
            const std::string bistroScene = bistroEntry
                ? FormatSettingsSnapshotSceneToken(
                    MakeSceneDisplayName(
                        m_app->GetSceneDir(), bistroEntry->FileName))
                : std::string{};
            const std::string sanMiguelScene = sanMiguelEntry
                ? FormatSettingsSnapshotSceneToken(
                    MakeSceneDisplayName(
                        m_app->GetSceneDir(), sanMiguelEntry->FileName))
                : std::string{};
            if (bistroScene.empty() || sanMiguelScene.empty())
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.payload =
                    "retained Bistro or San Miguel scene is absent";
                finish(failure);
                return;
            }

            m_RetainedRuntimeBaselineCamera =
                m_app->CaptureRetainedRuntimeCameraPose();
            glfwGetWindowSize(
                GetDeviceManager()->GetWindow(),
                &m_RetainedRuntimeBaselineWidth,
                &m_RetainedRuntimeBaselineHeight);

            std::vector<RetainedRuntimeCase> cases =
                uvsr::BuildRetainedRuntimeCases(
                    bistroScene, sanMiguelScene);
            m_RetainedRuntimeDiagnostic =
                std::make_unique<RetainedRuntimeDiagnosticState>(
                    std::move(cases),
                    m_RetainedRuntimeStartup);
            const std::string startRecord = BuildRetainedRuntimeStartJson(
                m_RetainedRuntimeProvenance,
                m_RetainedRuntimeDiagnostic->TotalCaseCount());
            std::fprintf(stdout, "%s\n", startRecord.c_str());
            std::fflush(stdout);
        }

        RetainedRuntimeTelemetry telemetry;
        telemetry.sceneBusy = m_app->IsSceneBusy();
        telemetry.sceneLoaded = m_app->IsSceneLoaded();
        telemetry.currentScene = m_app->GetCurrentSceneName();
        switch (m_ui.Noise.pattern)
        {
        case NoisePattern::SpatialWhite:
            telemetry.globalNoisePattern = "spatial-white";
            break;
        case NoisePattern::SpatialBlue:
            telemetry.globalNoisePattern = "spatial-blue";
            break;
        case NoisePattern::SpatiotemporalBlue:
            telemetry.globalNoisePattern = "spatiotemporal-blue";
            break;
        default:
            telemetry.globalNoisePattern = "invalid";
            break;
        }
        telemetry.globalNoiseResolution =
            GetNoiseResolutionLabel(m_ui.Noise.resolution);
        telemetry.globalNoiseAnimateSamples = m_ui.Noise.animate;
        telemetry.globalNoiseAccumulateSamples = m_ui.AccumulateSamples;
        telemetry.pathHistoryCount =
            m_app->GetPathTracingCenterPixelAcceptedSampleCount();
        telemetry.directionalVisibilityDispatched =
            m_app->DidDispatchDirectionalRayVisibilityThisFrame();
        telemetry.skyVisibilityDispatched =
            m_app->DidDispatchRayTracedSkyVisibilityThisFrame();
        telemetry.flashlightLightingSubmitted =
            m_app->DidSubmitFlashlightLightingThisFrame();
        telemetry.flashlightVisibilityDispatched =
            m_app->DidDispatchRayTracedFlashlightShadowThisFrame();
        telemetry.lightingAccumulationCommitted =
            m_app->DidCommitLightingAccumulationThisFrame();
        telemetry.autoExposureDispatched =
            m_app->IsRendererStageActiveThisFrame(
                RendererTimingStage::AutoExposure);
        telemetry.cpuFrameMilliseconds =
            std::max(0.0, double(ImGui::GetIO().DeltaTime) * 1000.0);
        const RendererTimings& runtimeTimings =
            m_app->GetRendererTimings();
        telemetry.gpuFrameTimingAvailable =
            runtimeTimings.IsAvailable(RendererTimingStage::CompleteFrame);
        if (telemetry.gpuFrameTimingAvailable)
        {
            telemetry.gpuFrameMilliseconds = runtimeTimings.Get(
                RendererTimingStage::CompleteFrame);
        }
        telemetry.lastAppliedAction = m_LastRetainedRuntimeAction;
        telemetry.output = m_app->ConsumeRuntimeOutputEvidence();
        if (m_RetainedRuntimeDiagnostic->RequiresSettingsSnapshot())
        {
            RefreshSettingsSnapshot();
            telemetry.settingsSnapshot = m_SettingsSnapshots.Canonical();
        }

        if (m_RetainedRuntimePrerequisiteRestore)
        {
            const SettingId id = m_RetainedRuntimePrerequisiteRestore->id;
            bool inactive = false;
            switch (id)
            {
            case SettingId::RepresentationAllowRayTraversal:
                inactive = m_ui.Lighting == LightingSolution::PathTracing
                    ? m_app->GetSelectedLightingTransportState() == SelectedLightingTransportState::PathTracingUnavailable
                    : !telemetry.directionalVisibilityDispatched && !telemetry.skyVisibilityDispatched &&
                        !telemetry.flashlightVisibilityDispatched;
                break;
            default: break;
            }
            if (!inactive)
            {
                finish(m_RetainedRuntimeDiagnostic->Abort(
                    "dependent pass remained active without its prerequisite", now));
                return;
            }
            if (--m_RetainedRuntimePrerequisiteFrames != 0)
                return;
            std::string error;
            if (!ApplySettingValue(id, m_RetainedRuntimePrerequisiteRestore->value, error))
            {
                finish(m_RetainedRuntimeDiagnostic->Abort("prerequisite restore failed: " + error, now));
                return;
            }
            std::fprintf(stdout, "{\"event\":\"prerequisite-cycle\",\"setting\":\"%s\",\"inactiveFrames\":3}\n",
                std::string(SettingName(id)).c_str());
            m_RetainedRuntimePrerequisiteRestore.reset();
            m_LastRetainedRuntimeAction = RetainedRuntimeAction::CyclePrerequisite;
            return;
        }

        if (m_RetainedRuntimePathReselectionPending)
        {
            if (m_ui.Lighting != LightingSolution::RayMarching)
            {
                finish(m_RetainedRuntimeDiagnostic->Abort(
                    "lighting-solution cycle did not render its Ray Tracing leg",
                    now));
                return;
            }
            // This function runs after the frame was rendered. Returning to
            // Path Tracing here therefore guarantees a real Ray Tracing
            // frame separated the two selections.
            ApplyLightingSolution(LightingSolution::PathTracing);
            m_LastRetainedRuntimeAction =
                RetainedRuntimeAction::CycleLightingSolution;
            m_RetainedRuntimePathReselectionPending = false;
            return;
        }

        RetainedRuntimeDirective directive =
            m_RetainedRuntimeDiagnostic->Tick(telemetry, now);
        if (directive.hasStableFrameTiming)
        {
            telemetry.cpuFrameMilliseconds =
                directive.stableCpuFrameMilliseconds;
            telemetry.gpuFrameMilliseconds =
                directive.stableGpuFrameMilliseconds;
            telemetry.gpuFrameTimingAvailable = true;
        }
        const auto abort = [&](std::string message)
        {
            finish(m_RetainedRuntimeDiagnostic->Abort(
                std::move(message), now));
        };
        const auto restoreBaseline = [&]()
        {
            if (m_RetainedRuntimeBaselineWidth > 0 && m_RetainedRuntimeBaselineHeight > 0)
                glfwSetWindowSize(GetDeviceManager()->GetWindow(), m_RetainedRuntimeBaselineWidth, m_RetainedRuntimeBaselineHeight);
            if (m_RetainedRuntimeBaselineCamera)
                m_app->RestoreRetainedRuntimeCameraPose(*m_RetainedRuntimeBaselineCamera);
        };
        switch (directive.kind)
        {
        case RetainedRuntimeDirectiveKind::Wait:
            return;

        case RetainedRuntimeDirectiveKind::ApplyCase:
        {
            if (!directive.runtimeCase)
            {
                abort("state returned an empty case");
                return;
            }
            // Every case begins from one authoritative baseline so no result
            // inherits state from the preceding matrix row.
            m_LastRetainedRuntimeAction = RetainedRuntimeAction::None;
            m_RetainedRuntimePathReselectionPending = false;
            std::string resetError;
            if (!ResetAllSettingsToFactoryDefaults(resetError))
            {
                abort("factory reset failed: " + resetError);
                return;
            }
            restoreBaseline();
            if (!SelectRuntimeDiagnosticFlashlight(resetError))
            {
                abort("baseline flashlight selection failed: " + resetError);
                return;
            }
            for (const RetainedRuntimeCase::Setting& setting :
                directive.runtimeCase->settings)
            {
                if (setting.id == directive.runtimeCase->actionSettingId ||
                    setting.id == SettingId::SceneCurrent)
                    continue;
                std::string error;
                if (!ApplySettingValue(setting.id, setting.value, error))
                {
                    abort("SET " + std::string(SettingName(setting.id)) +
                        " failed: " + error);
                    return;
                }
            }
            if (directive.runtimeCase->actionSettingId != SettingId::Invalid)
            {
                std::string error;
                if (!ApplySettingValue(
                        directive.runtimeCase->actionSettingId,
                        directive.runtimeCase->actionBaselineValue,
                        error))
                {
                    abort("baseline SET " + std::string(SettingName(
                        directive.runtimeCase->actionSettingId)) +
                        " failed: " + error);
                    return;
                }
            }
            // scene selection starts asynchronous loading and locks further edits.
            for (const RetainedRuntimeCase::Setting& setting :
                directive.runtimeCase->settings)
            {
                if (setting.id != SettingId::SceneCurrent ||
                    setting.id == directive.runtimeCase->actionSettingId)
                    continue;
                std::string error;
                if (!ApplySettingValue(setting.id, setting.value, error))
                {
                    abort("SET scene.current failed: " + error);
                    return;
                }
            }
            return;
        }

        case RetainedRuntimeDirectiveKind::ApplyAction:
        {
            if (!directive.runtimeCase ||
                directive.action == RetainedRuntimeAction::None)
            {
                abort("state requested an empty runtime action");
                return;
            }
            if (!telemetry.output)
            {
                abort("state advanced without phase-specific output evidence");
                return;
            }
            const std::string captureRecord =
                BuildRetainedRuntimeCaptureJson(
                    directive.caseIndex,
                    *directive.runtimeCase,
                    directive.payload,
                    telemetry);
            std::fprintf(stdout, "%s\n", captureRecord.c_str());
            std::fflush(stdout);
            switch (directive.action)
            {
            case RetainedRuntimeAction::NudgeCamera:
                m_app->NudgeCameraForRuntimeDiagnostic();
                break;

            case RetainedRuntimeAction::ResizeViewport:
                if (directive.resizeWidth <= 0 ||
                    directive.resizeHeight <= 0)
                {
                    abort("resize action lacked positive dimensions");
                    return;
                }
                glfwSetWindowSize(
                    GetDeviceManager()->GetWindow(),
                    directive.resizeWidth,
                    directive.resizeHeight);
                break;

            case RetainedRuntimeAction::ChangeScene:
            case RetainedRuntimeAction::ChangeSetting:
            {
                if (directive.action ==
                        RetainedRuntimeAction::ChangeScene &&
                    directive.runtimeCase->exerciseRetainedStateChanges)
                {
                    restoreBaseline();
                }
                std::string error;
                const UiSettingsCommandDefinition* definition =
                    FindSettingsCommandDefinition(directive.actionSettingId);
                if (definition && definition->availability ==
                        UiSettingsAvailability::SelectedFlashlight &&
                    !SelectRuntimeDiagnosticFlashlight(error))
                {
                    abort("action flashlight selection failed: " + error);
                    return;
                }
                if (directive.actionSettingId == SettingId::Invalid ||
                    !ApplySettingValue(
                        directive.actionSettingId,
                        directive.actionValue,
                        error))
                {
                    abort("action SET " + std::string(SettingName(
                        directive.actionSettingId)) + " failed: " + error);
                    return;
                }
                break;
            }

            case RetainedRuntimeAction::ChangeMaterial:
            {
                std::string error;
                if (!ChangeRuntimeDiagnosticMaterial(error))
                {
                    abort("material action failed: " + error);
                    return;
                }
                break;
            }

            case RetainedRuntimeAction::ChangeLight:
            {
                std::string error;
                if (!ChangeRuntimeDiagnosticLight(error))
                {
                    abort("light action failed: " + error);
                    return;
                }
                break;
            }

            case RetainedRuntimeAction::ToggleFlashlight:
            {
                std::string error;
                if (!ToggleRuntimeDiagnosticFlashlight(error))
                {
                    abort("flashlight action failed: " + error);
                    return;
                }
                break;
            }

            case RetainedRuntimeAction::CycleLightingSolution:
                if (m_ui.Lighting != LightingSolution::PathTracing)
                {
                    abort("lighting-solution cycle did not begin in Path Tracing");
                    return;
                }
                ApplyLightingSolution(LightingSolution::RayMarching);
                m_RetainedRuntimePathReselectionPending = true;
                return;

            case RetainedRuntimeAction::CyclePrerequisite:
            {
                std::string error;
                if (!ApplySettingValue(directive.actionSettingId, directive.actionValue, error))
                {
                    abort("prerequisite disable failed: " + error);
                    return;
                }
                m_RetainedRuntimePrerequisiteRestore = RetainedRuntimeCase::Setting{
                    directive.actionSettingId, directive.runtimeCase->actionBaselineValue };
                m_RetainedRuntimePrerequisiteFrames = 3;
                return;
            }

            case RetainedRuntimeAction::None:
                abort("state requested the none runtime action");
                return;
            }
            m_LastRetainedRuntimeAction = directive.action;
            return;
        }

        case RetainedRuntimeDirectiveKind::ResetSettings:
        {
            std::string error;
            if (!ResetAllSettingsToFactoryDefaults(error))
                abort("factory reset failed: " + error);
            return;
        }

        case RetainedRuntimeDirectiveKind::RestoreSnapshot:
        {
            const SettingsSnapshotTransactionStep step =
                m_SettingsSnapshots.BeginApplyCanonicalStaged(
                    directive.payload,
                    MakeSettingsSnapshotRuntimeAccess());
            if (step.progress !=
                SettingsSnapshotTransactionProgress::Succeeded)
            {
                abort(step.result.error.empty()
                    ? "runtime snapshot restore unexpectedly requires "
                        "staged continuation"
                    : step.result.error);
            }
            return;
        }

        case RetainedRuntimeDirectiveKind::CaptureOutput:
            if (!directive.runtimeCase)
            {
                abort("state requested output for an empty case");
                return;
            }
            m_app->RequestRuntimeOutputEvidence(
                directive.caseIndex,
                directive.runtimeCase->name + "-" + directive.payload);
            return;

        case RetainedRuntimeDirectiveKind::ReportCasePass:
        {
            if (!directive.runtimeCase || !telemetry.output)
            {
                abort("state reported a case without output evidence");
                return;
            }
            const std::string caseRecord = BuildRetainedRuntimeCaseJson(
                directive.caseIndex,
                *directive.runtimeCase,
                telemetry);
            std::fprintf(stdout, "%s\n", caseRecord.c_str());
            std::fflush(stdout);
            return;
        }

        case RetainedRuntimeDirectiveKind::FinishPass:
        case RetainedRuntimeDirectiveKind::FinishFail:
            finish(directive);
            return;
        }
    }
#endif

auto UIRenderer::Render(nvrhi::IFramebuffer* framebuffer) -> void {
        if (!imgui_nvrhi)
            return;
#if defined(UVSR_BUILD_TESTING)
        if (g_VerifyRetainedRuntimeRequested)
        {
            DriveRetainedRuntimeDiagnostic();
            return;
        }
        if (g_VerifySettingsContractRequested &&
            !m_SettingsContractDiagnosticComplete &&
            !m_app->IsSceneBusy())
        {
            g_VerifySettingsContractResult =
                VerifyCanonicalSettingsContract();
            m_SettingsContractDiagnosticComplete = true;
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            return;
        }
#endif

        TryApplyStartupSettingsSnapshot();
        if (g_StartupSettingsSnapshotFailed)
            return;

        buildUI();
        DrawDisplaySyncTest();
        const bool pixelZoomEnabled = !m_ui.DisplaySyncTestActive && IsPixelZoomEnabled(m_ui.PixelZoom);
        const bool materialDrawerVisible =
            !m_ui.DisplaySyncTestActive && m_ui.ShowUI && !m_SettingsCollapsed && m_ui.ShowMaterialDrawer &&
                !m_app->IsSceneBusy();
        if (pixelZoomEnabled || materialDrawerVisible)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImVec2 crosshairCenter(
                viewport->Pos.x + std::floor(viewport->Size.x * 0.5f) + 0.5f,
                viewport->Pos.y + std::floor(viewport->Size.y * 0.5f) + 0.5f);
            ImGui::GetForegroundDrawList()->AddCircleFilled(
                crosshairCenter,
                2.f,
                IM_COL32(255, 255, 255, 128),
                12);
        }
        if (pixelZoomEnabled)
        {
            const nvrhi::FramebufferInfoEx& framebufferInfo =
                framebuffer->getFramebufferInfo();
            const PixelZoomLayout zoomLabelLayout = ResolvePixelZoomLayout(
                framebufferInfo.width, framebufferInfo.height,
                m_SettingsPanelMarginPixels, m_ui.PixelZoom);
            const char* zoomAreaLabel =
                GetPixelZoomAreaLabel(m_ui.PixelZoom);
            ImFont* zoomLabelFont = m_UiBodyFont->GetScaledFont();
            ImGui::PushFont(zoomLabelFont);
            const ImVec2 zoomAreaLabelSize =
                ImGui::CalcTextSize(zoomAreaLabel);
            ImGui::PopFont();

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float labelInset =
                float(m_SettingsPanelMarginPixels);
            const ImVec2 zoomAreaLabelPosition(
                viewport->Pos.x +
                    float(zoomLabelLayout.panelMinX) +
                    std::floor(
                        (float(zoomLabelLayout.panelWidth) -
                            zoomAreaLabelSize.x) *
                        0.5f),
                viewport->Pos.y +
                    float(zoomLabelLayout.panelMinY +
                        zoomLabelLayout.panelHeight) -
                    labelInset -
                    zoomAreaLabelSize.y);
            ImDrawList* foregroundDrawList =
                ImGui::GetForegroundDrawList();
            const ImVec2 zoomAreaLabelShadowPosition(
                zoomAreaLabelPosition.x + 1.f,
                zoomAreaLabelPosition.y + 1.f);
            foregroundDrawList->AddText(
                zoomLabelFont,
                zoomLabelFont->LegacySize,
                zoomAreaLabelShadowPosition,
                IM_COL32(0, 0, 0, 150),
                zoomAreaLabel);
            foregroundDrawList->AddText(
                zoomLabelFont,
                zoomLabelFont->LegacySize,
                zoomAreaLabelPosition,
                IM_COL32(255, 255, 255, 230),
                zoomAreaLabel);
        }
        ImGui::Render();
        if (pixelZoomEnabled && m_PixelZoomPass)
            m_PixelZoomPass->Capture(framebuffer);
        if (pixelZoomEnabled && m_PixelZoomPass)
        {
            m_PixelZoomPass->Composite(
                framebuffer,
                m_ui.PixelZoom,
                m_SettingsPanelMarginPixels,
                ImGui::GetStyle().WindowRounding);
        }
        nvrhi::IFramebuffer* uiFramebuffer = framebuffer;
        if (framebuffer->getFramebufferInfo().colorFormats[0] ==
                nvrhi::Format::SRGBA8_UNORM)
        {
            // stock ImGui colors are display-encoded; use an unorm view.
            auto& uiView = m_UiFramebuffers[framebuffer];
            if (!uiView)
            {
                auto desc = framebuffer->getDesc();
                desc.colorAttachments[0].format = nvrhi::Format::RGBA8_UNORM;
                uiView = GetDevice()->createFramebuffer(desc);
            }
            if (uiView)
                uiFramebuffer = uiView;
        }
        const auto uiFormat = uiFramebuffer->getFramebufferInfo().colorFormats[0];
        if (m_UiFramebufferFormat != uiFormat)
        {
            imgui_nvrhi->backbufferResizing();
            m_UiFramebufferFormat = uiFormat;
        }
        imgui_nvrhi->render(uiFramebuffer);
        m_imguiFrameOpened = false;
    }

auto UIRenderer::BackBufferResizing() -> void {
        m_UiFramebuffers.clear();
        m_UiFramebufferFormat = nvrhi::Format::UNKNOWN;
        if (m_PixelZoomPass)
            m_PixelZoomPass->BackBufferResizing();
        ImGui_Renderer::BackBufferResizing();
    }

auto UIRenderer::DisplayScaleChanged(
        float scaleX,
        float scaleY) -> void {
        ImGui_Renderer::DisplayScaleChanged(scaleX, scaleY);
        m_UiDisplayScale = std::clamp(
            scaleX,
            UiMinimumDisplayScale,
            UiMaximumDisplayScale);
    }

auto UIRenderer::KeyboardUpdate(
        int key,
        int scancode,
        int action,
        int mods) -> bool {
        const bool captured = ImGui_Renderer::KeyboardUpdate(
            key, scancode, action, mods);
        if (key == GLFW_KEY_F8 && action == GLFW_PRESS && !ImGui::GetIO().WantTextInput)
        {
            SetDisplaySyncTestActive(!m_ui.DisplaySyncTestActive);
            return true;
        }
        const bool settingsShortcutOwnedByUi =
            ImGui::GetIO().WantTextInput ||
            ImGui::IsAnyItemActive() ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
        const auto applyShortcutSetting = [this](
            SettingId id, UiSettingsValue value)
        {
            std::string error;
            if (!ApplySettingValue(id, value, error) &&
                error.rfind("No change: ", 0u) != 0u)
            {
                uvsr::log::warning(
                    "Keyboard setting %s failed: %s",
                    std::string(SettingName(id)).c_str(),
                    error.c_str());
            }
        };
        if ((key == GLFW_KEY_ESCAPE ||
                key == GLFW_KEY_GRAVE_ACCENT) &&
            action == GLFW_PRESS &&
            !settingsShortcutOwnedByUi)
        {
            applyShortcutSetting(
                SettingId::UiVisible,
                UiSettingsValue::Boolean(!m_ui.ShowUI));
            return true;
        }
        const bool plainFlashlightShortcut =
            (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) == 0;
        if (key == GLFW_KEY_F &&
            action == GLFW_PRESS &&
            plainFlashlightShortcut &&
            !captured &&
            !ImGui::GetIO().WantTextInput)
        {
            m_app->ToggleFlashlight();
            return true;
        }
        const bool plainZoomShortcut =
            (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) == 0;
        if (key == GLFW_KEY_Z &&
            action == GLFW_PRESS &&
            plainZoomShortcut &&
            !ImGui::GetIO().WantTextInput)
        {
            const UiSettingsCommandDefinition* definition =
                FindSettingsCommandDefinition(SettingId::UiZoom);
            UiSettingsValue current;
            std::string error;
            if (!definition ||
                !ReadSettingValue(SettingId::UiZoom, current, error) ||
                current.kind != UiSettingsValueKind::Token)
            {
                uvsr::log::warning(
                    "Keyboard setting %s could not read its typed value: %s",
                    std::string(SettingName(SettingId::UiZoom)).c_str(),
                    error.c_str());
                return true;
            }
            const auto begin = definition->typedDomain.tokens.begin();
            const auto end = begin + definition->typedDomain.tokenCount;
            const auto found = std::find(begin, end, current.text);
            const std::size_t nextIndex = found == end
                ? 0u
                : (static_cast<std::size_t>(std::distance(begin, found)) +
                    1u) % definition->typedDomain.tokenCount;
            applyShortcutSetting(
                SettingId::UiZoom,
                UiSettingsValue::Token(std::string(
                    definition->typedDomain.tokens[nextIndex])));
            return true;
        }
        const bool plainMaterialEditorShortcut =
            (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) == 0;
        if (key == GLFW_KEY_M &&
            action == GLFW_PRESS &&
            plainMaterialEditorShortcut &&
            !ImGui::GetIO().WantTextInput)
        {
            // selection refresh is an action even when the drawer is already open.
            RequestMaterialDrawerVisible(true);
            return true;
        }

        return captured;
    }
