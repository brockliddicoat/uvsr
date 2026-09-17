#include "retained_scene_paths.h"
#include "uvsr_ui_internal.h"
#include "file_bytes.h"
#include "file_write.h"
#include <cstdlib>
#include <new>
#if defined(UVSR_BUILD_TESTING)
#include "retained_runtime_json.h"
#include <cstdio>
#endif

auto UIRenderer::GetSceneLoadTimingDatabasePath(
        WindowsPath& output, WindowsPathResult& result) noexcept -> bool {
        result = {};
#if defined(UVSR_BUILD_TESTING)
        WindowsPath directory;
        if (!GetExecutableDirectoryWide(directory, result))
            return false;
        return JoinWindowsRelativePath(directory.Data(), L"state\\scene-load-history-v1.txt", output, result);
#else
        const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
        if (!localAppData || localAppData[0] == L'\0')
            return false;
        return JoinWindowsRelativePath(localAppData, L"UVSR\\scene-load-history-v1.txt", output, result);
#endif
    }

auto UIRenderer::LoadSceneLoadTimingDatabase() -> void {
        WindowsPath path;
        WindowsPathResult pathResult;
        if (!GetSceneLoadTimingDatabasePath(path, pathResult))
        {
            if (pathResult.error != WindowsPathError::None)
                uvsr::log::warning("Could not resolve scene loading history path (error %u, Win32 %u)",
                    unsigned(pathResult.error), pathResult.nativeCode);
            return;
        }
        FileBytes input;
        FileReadResult readResult;
        // the original stream accepts arbitrarily long inter-token whitespace.
        // storage exhaustion remains a checked read failure, not a new format cap.
        if (!ReadFileBytes(path.Data(), UINT64_MAX, input, readResult))
        {
            if (readResult.error != FileReadError::Missing && readResult.error != FileReadError::Open)
                uvsr::log::warning("Could not read scene loading history at %ls (error %u, Win32 %u)",
                    path.Data(), unsigned(readResult.error), readResult.systemCode);
            return;
        }
        SettingsSnapshotError error;
        if (!ReadSceneLoadTimingDatabase({input.Data(), input.Size()}, m_SceneLoadTiming, error))
            uvsr::log::warning("Ignoring invalid scene loading history at %ls: %s", path.Data(), error.Message());
    }

auto UIRenderer::SaveSceneLoadTimingDatabase() const -> void {
        WindowsPath path;
        WindowsPathResult pathResult;
        if (!GetSceneLoadTimingDatabasePath(path, pathResult))
        {
            if (pathResult.error != WindowsPathError::None)
                uvsr::log::warning("Could not resolve scene loading history path (error %u, Win32 %u)",
                    unsigned(pathResult.error), pathResult.nativeCode);
            return;
        }
        json::EncodedText text;
        SettingsSnapshotError error;
        if (!WriteSceneLoadTimingDatabase(m_SceneLoadTiming, text, error))
        {
            uvsr::log::warning("Could not write scene loading history at %ls: %s", path.Data(), error.Message());
            return;
        }
        const FileWriteSpan span{text.Data(), text.Size()};
        FileWriteResult result;
        if (!WriteFileBytesAtomically(path.Data(), &span, 1, result))
            uvsr::log::warning("Could not save scene loading history at %ls (error %u, Win32 %u, cleanup %u)",
                path.Data(), unsigned(result.error), result.systemCode, result.cleanupCode);
    }

UIRenderer::UIRenderer(
        DeviceManager* deviceManager,
        UvsrSceneViewer* app,
        UIData& ui,
        std::string_view startupSettingsSnapshotCode) noexcept
        : IRenderPass(deviceManager)
        , m_app(app)
        , m_SettingsSnapshots(
#if defined(UVSR_BUILD_TESTING)
            SettingsSnapshotCatalogLocation::ExecutableState
#else
            SettingsSnapshotCatalogLocation::Installed
#endif
        )
        , m_StartupSettingsSnapshotCode(
            startupSettingsSnapshotCode)
        , m_ui(ui) {}

auto UIRenderer::Animate(float elapsedTimeSeconds) -> void {
        AdvanceDisplayPresentation(elapsedTimeSeconds);
        if (!m_UiGpuReady) return;
        m_UiContext.CloseFrame();
        float scaleX, scaleY;
        GetDeviceManager()->GetDPIScaleInfo(scaleX, scaleY);
        const bool explicitScaling = GetDeviceManager()->GetDeviceParams().supportExplicitDisplayScaling;
        if (!m_UiContext.EnsureFonts(explicitScaling ? scaleX : 1.f) || !m_UiGpu.UpdateFontTexture())
        {
            if (!m_RequiredFontsReady)
            {
                m_RequiredFontFailure = "UVSR could not initialize its required UI font atlas. "
                    "Reinstall UVSR with UVSR Launcher.";
                return;
            }
            uvsr::log::error("UVSR could not update its UI font atlas.");
            GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
            return;
        }
        m_RequiredFontsReady = true;
        int width, height;
        GetDeviceManager()->GetWindowDimensions(width, height);
        if (!m_UiContext.BeginFrame(width, height, scaleX, scaleY, elapsedTimeSeconds, explicitScaling))
        {
            uvsr::log::error("UVSR could not start its UI frame.");
            GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
        }
    }

auto UIRenderer::Init(const uvsr::RendererNvrhiMessageCallback& messages, SettingsSnapshotError& error) -> bool {
        error = {};
        if (!m_app || m_UiGpuReady || !GetDeviceManager() || !GetDevice() ||
            !m_app->GetRendererShaderFactory() || !m_app->GetRendererCommonPasses() ||
            !m_app->GetRendererCommonPasses()->IsValid())
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Invalid UI initialization state.", {}};
            return false;
        }
        if (!m_UiContext.LoadWindowsFonts())
        {
            m_RequiredFontFailure = "UVSR requires Windows Segoe UI Semibold and Bold. "
                "Restore seguisb.ttf and segoeuib.ttf in Windows Fonts, then restart UVSR. font data is unavailable";
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, m_RequiredFontFailure, {}};
            return false;
        }
        LoadSceneLoadTimingDatabase();
        m_PresentationWaitTimer = CreateWaitableTimerExW(nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!m_UiGpu.Init(GetDevice(), *m_app->GetRendererShaderFactory(), &messages))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Could not initialize UI graphics resources.", {}};
            return false;
        }
        m_PixelZoomPass.reset(new (std::nothrow) PixelZoomPass(
            GetDevice(), m_app->GetRendererShaderFactory(), m_app->GetRendererCommonPasses()));
        if (!m_PixelZoomPass)
        {
            error = {SettingsSnapshotErrorCode::OutOfMemory, 0, 0, "Could not allocate pixel zoom resources.", {}};
            return false;
        }
        m_UiGpuReady = true;
        return true;
    }

#if defined(UVSR_BUILD_TESTING)
bool UIRenderer::SelectRuntimeDiagnostic(SettingId id, const UiSettingsValue& selector,
    const char* emptyError, const char* failurePrefix, SettingsSnapshotError& error)
{
    SettingsSnapshotError selectionError;
    if (!selector.Text().empty() && (ApplySettingValue(id, selector, selectionError) ||
        selectionError.MessageView().rfind("No change: ", 0u) == 0u))
        return true;
    error = selector.Text().empty()
        ? SettingsSnapshotError{SettingsSnapshotErrorCode::InvalidInput, 0, 0, emptyError, {}}
        : ComposeSettingsSnapshotError({failurePrefix, selectionError.MessageView()},
            selectionError.code == SettingsSnapshotErrorCode::None ? SettingsSnapshotErrorCode::InvalidInput : selectionError.code,
            selectionError.nativeCode, selectionError.cleanupCode);
    return false;
}

auto UIRenderer::ChangeRuntimeDiagnosticMaterial(
        SettingsSnapshotError& error) -> bool {
        const auto scene = m_app->GetSceneView();
        if (!scene.generation)
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "no loaded scene provides a material to change", {}};
            return false;
        }
        // the image fixture edits the same authored surface in every run.
        static constexpr char MaterialName[] =
            "MASTER_Interior_01_Floor_Tile_Hexagonal_BLENDSHADER";
        RendererSceneHandle selected;
        for (uint32_t index = 0; index < scene.materials.count; ++index)
        {
            const auto text = RendererSceneText(scene, scene.materials.data[index].name);
            if (std::string_view(text.count ? text.data : "", text.count) != MaterialName)
                continue;
            if (selected)
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "the diagnostic material name is ambiguous", {}};
                return false;
            }
            selected = {scene.generation, index};
        }
        const auto* material = FindRendererSceneMaterial(scene, selected);
        if (!material || material->selectionId == InvalidSceneIndex ||
            !m_app->IsSceneTextureReady(material->values.textures[uint32_t(RendererSceneMaterialTextureSlot::Normal)]) ||
            !std::isfinite(material->values.normalTextureScale))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "the diagnostic floor material is missing or invalid", {}};
            return false;
        }

        const std::uint32_t materialId =
            material->selectionId;
        UiSettingsValue selector;
        SettingsSnapshotError selectorError;
        if (!FormatSettingsSnapshotMaterialToken(false, materialId, selector, selectorError))
        {
            error = ComposeSettingsSnapshotError({"could not select the diagnostic material: ", selectorError.MessageView()},
                selectorError.code, selectorError.nativeCode, selectorError.cleanupCode);
            return false;
        }
        if (!SelectRuntimeDiagnostic(SettingId::MaterialSelected, selector,
            "the diagnostic material has no canonical selector", "could not select the diagnostic material: ", error))
            return false;
        const float before = material->values.normalTextureScale;
        const float replacement = before >= 0.f ? -1.f : 1.f;
        if (!ApplySettingValue(SettingId::MaterialSelectedNormalScale,
                UiSettingsValue::Float(replacement), error))
            return false;
        material = m_app->GetSceneMaterial(selected);
        if (m_ui.SelectedMaterial != selected || !material || material->selectionId != materialId ||
            material->values.normalTextureScale != replacement)
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "the diagnostic material command did not edit its resolved target", {}};
            return false;
        }
        std::fprintf(stdout, "{\"event\":\"material-action\",\"name\":\"%s\","
            "\"id\":%u,\"before\":%.9g,\"after\":%.9g}\n",
            MaterialName, materialId, before, replacement);
        std::fflush(stdout);
        return true;
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UIRenderer::ChangeRuntimeDiagnosticLight(
        SettingsSnapshotError& error) -> bool {
        const auto light = m_app->GetPrimaryDirectionalLight();
        const auto* record = m_app->GetSceneLight(light);
        if (!record || m_app->IsFlashlight(light) || !std::isfinite(record->values.angularSize))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "the loaded scene has no finite directional light", {}};
            return false;
        }

        const auto lights = m_app->GetEditableLights();
        const auto selected = lights.Ordinal(light);
        if (selected == InvalidSceneIndex)
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "the diagnostic directional light is not editable", {}};
            return false;
        }
        UiSettingsValue selector;
        SettingsSnapshotError selectorError;
        if (!AcceptFormattedSelector(FormatSettingsSnapshotLightToken(
                selected, m_app->GetSceneLightNameView(light), selector, selectorError), selector, selectorError))
        {
            error = ComposeSettingsSnapshotError({"could not select the diagnostic directional light: ", selectorError.MessageView()},
                selectorError.code, selectorError.nativeCode, selectorError.cleanupCode);
            return false;
        }
        if (!SelectRuntimeDiagnostic(SettingId::LightSelected, selector,
            "the diagnostic directional light has no canonical selector", "could not select the diagnostic directional light: ", error))
            return false;
        const float replacement = record->values.angularSize < 10.f ? 20.f : 0.f;
        return ApplySettingValue(
            SettingId::LightSelectedAngularSize,
            UiSettingsValue::Float(replacement), error);
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UIRenderer::SelectRuntimeDiagnosticFlashlight(
        SettingsSnapshotError& error) -> bool {
        const auto lights = m_app->GetEditableLights();
        const auto flashlight = lights.At(0);
        if (!m_app->IsFlashlight(flashlight))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "the loaded scene has no retained flashlight", {}};
            return false;
        }

        UiSettingsValue selector;
        SettingsSnapshotError selectorError;
        if (!AcceptFormattedSelector(FormatSettingsSnapshotLightToken(
                0, m_app->GetSceneLightNameView(flashlight), selector, selectorError), selector, selectorError))
        {
            error = ComposeSettingsSnapshotError({"could not select the retained flashlight: ", selectorError.MessageView()},
                selectorError.code, selectorError.nativeCode, selectorError.cleanupCode);
            return false;
        }
        return SelectRuntimeDiagnostic(SettingId::LightSelected, selector,
            "the retained flashlight has no canonical selector", "could not select the retained flashlight: ", error);
    }

auto UIRenderer::ToggleRuntimeDiagnosticFlashlight(
        SettingsSnapshotError& error) -> bool {
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

        const auto writeRecord = [](FILE* stream,
            const json::EncodedText& record, bool flush) noexcept
        {
            if (!record.IsValid() || std::ferror(stream)) return false;
            // one stdio call keeps the record and newline under the same stream lock.
            const int written = std::fprintf(stream, "%s\n", record.Data());
            if (written < 0 || static_cast<size_t>(written) != record.Size() + 1)
                return false;
            return (!flush || std::fflush(stream) == 0) && std::ferror(stream) == 0;
        };
        const auto outputFailure = [&](const char* message)
        {
            m_RetainedRuntimePathReselectionPending = false;
            // do not allocate another error record after output failure.
            if (m_RetainedRuntimeDiagnostic)
                (void)m_RetainedRuntimeDiagnostic->Abort({}, now);
            std::fprintf(stderr, "%s\n", message);
            std::fflush(stderr);
            g_VerifyRetainedRuntimeResult = 1;
            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), GLFW_TRUE);
        };

        const auto finish = [&](const RetainedRuntimeDirective& directive)
        {
            m_RetainedRuntimePathReselectionPending = false;
            const bool passed =
                directive.kind == RetainedRuntimeDirectiveKind::FinishPass;
            const std::string_view caseName = directive.runtimeCase
                ? std::string_view(directive.runtimeCase->name.View())
                : "startup";
            if (!passed)
            {
                const json::EncodedText failure =
                    directive.semanticFailure.Passed()
                        ? EncodeRetainedRuntimeMessageJson(caseName, directive.failure)
                        : EncodeRetainedRuntimeSemanticFailureJson(caseName, directive.semanticFailure);
                if (!writeRecord(stderr, failure, false))
                {
                    outputFailure("retained runtime failure record output failed");
                    return;
                }
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
            const json::EncodedText summary = EncodeRetainedRuntimeSummaryJson(
                m_RetainedRuntimeProvenance,
                passed,
                passedCases,
                totalCases,
                elapsedMilliseconds);
            if (!writeRecord(passed ? stdout : stderr, summary, true))
            {
                outputFailure("retained runtime summary record output failed");
                return;
            }
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
                failure.failure = "default scene startup exceeded 3 minutes";
                finish(failure);
                return;
            }
            if (m_app->IsSceneBusy())
                return;
            if (!m_app->IsSceneLoaded())
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.failure = "default scene did not finish loading";
                finish(failure);
                return;
            }

            const auto lights = m_app->GetEditableLights();
            const auto flashlight = lights.At(0);
            if (!m_app->IsFlashlight(flashlight))
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.failure = "retained flashlight is unavailable";
                finish(failure);
                return;
            }
            UiSettingsValue selector;
            SettingsSnapshotError selectorError;
            const bool prepared = AcceptFormattedSelector(FormatSettingsSnapshotLightToken(
                0, m_app->GetSceneLightNameView(flashlight), selector, selectorError), selector, selectorError);
            SettingsSnapshotError selectionError;
            if (!prepared) selectionError = ComposeSettingsSnapshotError({"could not select retained flashlight: ", selectorError.MessageView()},
                selectorError.code, selectorError.nativeCode, selectorError.cleanupCode);
            if (!prepared || !SelectRuntimeDiagnostic(SettingId::LightSelected, selector,
                "retained flashlight has no canonical selector", "could not select retained flashlight: ", selectionError))
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.failure = selectionError.MessageView();
                finish(failure);
                return;
            }

            RetainedRuntimeMessage provenanceFailure;
            if (!PrepareRetainedRuntimeProvenance(m_RetainedRuntimeProvenance,
                g_RuntimeDebugValidationRequested, provenanceFailure))
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.failure = provenanceFailure;
                finish(failure);
                return;
            }

            const SceneCatalog& scenes = m_app->GetAvailableScenes();
            const SceneCatalogEntry* bistroEntry = nullptr;
            const SceneCatalogEntry* sanMiguelEntry = nullptr;
            SettingsSnapshotError valueError;
            if (!FindRetainedScene(scenes, m_app->GetSceneDir().data(), 0, bistroEntry, valueError) ||
                !FindRetainedScene(scenes, m_app->GetSceneDir().data(), 1, sanMiguelEntry, valueError))
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.failure = valueError.MessageView();
                finish(failure);
                return;
            }
            UiSettingsValue bistroScene, sanMiguelScene;
            if ((bistroEntry && !AcceptFormattedSelector(FormatSettingsSnapshotSceneToken(
                    bistroEntry->CommandName, bistroScene, valueError), bistroScene, valueError)) ||
                (sanMiguelEntry && !AcceptFormattedSelector(FormatSettingsSnapshotSceneToken(
                    sanMiguelEntry->CommandName, sanMiguelScene, valueError), sanMiguelScene, valueError)))
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.failure = valueError.MessageView();
                finish(failure);
                return;
            }
            if (bistroScene.Text().empty() || sanMiguelScene.Text().empty())
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.failure =
                    "retained Bistro or San Miguel scene is absent";
                finish(failure);
                return;
            }

            RetainedRuntimeCases cases;
            if (!uvsr::BuildRetainedRuntimeCases(bistroScene.Text(), sanMiguelScene.Text(), cases, valueError))
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.failure = valueError.MessageView();
                finish(failure);
                return;
            }

            m_RetainedRuntimeBaselineCamera =
                m_app->CaptureRetainedRuntimeCameraPose();
            glfwGetWindowSize(
                GetDeviceManager()->GetWindow(),
                &m_RetainedRuntimeBaselineWidth,
                &m_RetainedRuntimeBaselineHeight);

            m_RetainedRuntimeDiagnostic.reset(new (std::nothrow) RetainedRuntimeDiagnosticState(
                std::move(cases), m_RetainedRuntimeStartup));
            if (!m_RetainedRuntimeDiagnostic)
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.failure = "UVSR could not allocate retained runtime state.";
                finish(failure);
                return;
            }
            const json::EncodedText startRecord = EncodeRetainedRuntimeStartJson(
                m_RetainedRuntimeProvenance,
                m_RetainedRuntimeDiagnostic->TotalCaseCount());
            if (!writeRecord(stdout, startRecord, true))
            {
                outputFailure("retained runtime start record output failed");
                return;
            }
        }

        // every path that mutates these text owners returns before using telemetry again.
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
        telemetry.pathHistoryGeneration =
            m_app->GetPathTracingHistoryGeneration();
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
        if (telemetry.output)
            telemetry.storage = m_app->CaptureRetainedRuntimeStorage();
        if (m_RetainedRuntimeDiagnostic->RequiresSettingsSnapshot())
        {
            if (RefreshSettingsSnapshot())
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
            SettingsSnapshotError error;
            if (!ApplySettingValue(id, m_RetainedRuntimePrerequisiteRestore->value, error))
            {
                finish(m_RetainedRuntimeDiagnostic->Abort({"prerequisite restore failed: ", error.MessageView()}, now));
                return;
            }
            std::fprintf(stdout, "{\"event\":\"prerequisite-cycle\",\"setting\":\"%s\",\"inactiveFrames\":3}\n",
                SettingName(id).data());
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
        const auto abort = [&](RetainedRuntimeMessage message)
        {
            finish(m_RetainedRuntimeDiagnostic->Abort(
                message, now));
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
            SettingsSnapshotError resetError;
            if (!ResetAllSettingsToFactoryDefaults(resetError))
            {
                abort({"factory reset failed: ", resetError.MessageView()});
                return;
            }
            restoreBaseline();
            if (!SelectRuntimeDiagnosticFlashlight(resetError))
            {
                abort({"baseline flashlight selection failed: ", resetError.MessageView()});
                return;
            }
            for (const RetainedRuntimeCase::Setting& setting :
                directive.runtimeCase->settings)
            {
                if (setting.id == directive.runtimeCase->actionSettingId ||
                    setting.id == SettingId::SceneCurrent)
                    continue;
                SettingsSnapshotError error;
                if (!ApplySettingValue(setting.id, setting.value, error))
                {
                    abort({"SET ", SettingName(setting.id), " failed: ", error.MessageView()});
                    return;
                }
            }
            if (directive.runtimeCase->actionSettingId != SettingId::Invalid)
            {
                SettingsSnapshotError error;
                if (!ApplySettingValue(
                        directive.runtimeCase->actionSettingId,
                        directive.runtimeCase->actionBaselineValue,
                        error))
                {
                    abort({"baseline SET ", SettingName(directive.runtimeCase->actionSettingId), " failed: ", error.MessageView()});
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
                SettingsSnapshotError error;
                if (!ApplySettingValue(setting.id, setting.value, error))
                {
                    abort({"SET scene.current failed: ", error.MessageView()});
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
            const json::EncodedText captureRecord =
                EncodeRetainedRuntimeCaptureJson(
                    directive.caseIndex,
                    *directive.runtimeCase,
                    directive.captureLabel,
                    telemetry);
            if (!writeRecord(stdout, captureRecord, true))
            {
                outputFailure("retained runtime capture record output failed");
                return;
            }
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
                SettingsSnapshotError error;
                const UiSettingsCommandDefinition* definition =
                    FindSettingsCommandDefinition(directive.actionSettingId);
                if (definition && definition->availability ==
                        UiSettingsAvailability::SelectedFlashlight &&
                    !SelectRuntimeDiagnosticFlashlight(error))
                {
                    abort({"action flashlight selection failed: ", error.MessageView()});
                    return;
                }
                if (directive.actionSettingId == SettingId::Invalid ||
                    !ApplySettingValue(
                        directive.actionSettingId,
                        directive.runtimeCase->actionValue,
                        error))
                {
                    abort({"action SET ", SettingName(directive.actionSettingId), " failed: ", error.MessageView()});
                    return;
                }
                break;
            }

            case RetainedRuntimeAction::ChangeMaterial:
            {
                SettingsSnapshotError error;
                if (!ChangeRuntimeDiagnosticMaterial(error))
                {
                    abort({"material action failed: ", error.MessageView()});
                    return;
                }
                break;
            }

            case RetainedRuntimeAction::ChangeLight:
            {
                SettingsSnapshotError error;
                if (!ChangeRuntimeDiagnosticLight(error))
                {
                    abort({"light action failed: ", error.MessageView()});
                    return;
                }
                break;
            }

            case RetainedRuntimeAction::ToggleFlashlight:
            {
                SettingsSnapshotError error;
                if (!ToggleRuntimeDiagnosticFlashlight(error))
                {
                    abort({"flashlight action failed: ", error.MessageView()});
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
                SettingsSnapshotError error;
                RetainedRuntimeCase::Setting restore;
                restore.id = directive.actionSettingId;
                SettingsSnapshotError valueError;
                if (!directive.runtimeCase->actionBaselineValue.CloneTo(restore.value, valueError))
                {
                    abort({"prerequisite restore preparation failed: ", valueError.MessageView()});
                    return;
                }
                if (!ApplySettingValue(directive.actionSettingId, directive.runtimeCase->actionValue, error))
                {
                    abort({"prerequisite disable failed: ", error.MessageView()});
                    return;
                }
                m_RetainedRuntimePrerequisiteRestore = std::move(restore);
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
            SettingsSnapshotError error;
            if (!ResetAllSettingsToFactoryDefaults(error))
                abort({"factory reset failed: ", error.MessageView()});
            return;
        }

        case RetainedRuntimeDirectiveKind::RestoreSnapshot:
        {
            const SettingsSnapshotTransactionStep step =
                m_SettingsSnapshots.BeginApplyCanonicalStaged(
                    directive.snapshot,
                    MakeSettingsSnapshotRuntimeAccess());
            if (step.progress !=
                SettingsSnapshotTransactionProgress::Succeeded)
            {
                abort(step.result.error.MessageView().empty()
                    ? std::string_view("runtime snapshot restore unexpectedly requires staged continuation")
                    : step.result.error.MessageView());
            }
            return;
        }

        case RetainedRuntimeDirectiveKind::CaptureOutput:
            if (!directive.runtimeCase)
            {
                abort("state requested output for an empty case");
                return;
            }
            if (!m_app->RequestRuntimeOutputEvidence(directive.caseIndex,
                    directive.runtimeCase->name.View(), directive.captureLabel))
                abort("runtime capture path preparation failed");
            return;

        case RetainedRuntimeDirectiveKind::ReportCasePass:
        {
            if (!directive.runtimeCase || !telemetry.output)
            {
                abort("state reported a case without output evidence");
                return;
            }
            const json::EncodedText caseRecord = EncodeRetainedRuntimeCaseJson(
                directive.caseIndex,
                *directive.runtimeCase,
                telemetry);
            if (!writeRecord(stdout, caseRecord, true))
            {
                outputFailure("retained runtime case record output failed");
                return;
            }
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
        if (!m_UiGpuReady || !m_UiContext.FrameOpened())
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
            ImFont* zoomLabelFont = m_UiContext.BodyFont();
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
        m_UiContext.Render();
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
        if (!m_UiGpu.Render(framebuffer))
        {
            uvsr::log::error("UVSR could not render its UI.");
            GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
        }
    }

auto UIRenderer::BackBufferResizing() -> void {
        if (m_PixelZoomPass)
            m_PixelZoomPass->BackBufferResizing();
        m_UiGpu.BackBufferResizing();
    }

auto UIRenderer::DisplayScaleChanged(
        float scaleX,
        float scaleY) -> void {
        m_UiContext.DisplayScaleChanged(scaleX, GetDeviceManager()->GetDeviceParams().supportExplicitDisplayScaling);
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
        const bool captured = RendererUiKeyboard(GetDeviceManager()->GetWindow(), key, action);
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
            SettingsSnapshotError error;
            if (!ApplySettingValue(id, value, error) &&
                error.MessageView().rfind("No change: ", 0u) != 0u)
            {
                uvsr::log::warning(
                    "Keyboard setting %s failed: %s",
                    SettingName(id).data(),
                    error.Message());
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
            SettingsSnapshotError error;
            if (!definition ||
                !ReadSettingValue(SettingId::UiZoom, current, error) ||
                current.kind != UiSettingsValueKind::Token)
            {
                uvsr::log::warning(
                    "Keyboard setting %s could not read its typed value: %s",
                    SettingName(SettingId::UiZoom).data(),
                    error.Message());
                return true;
            }
            const auto begin = definition->typedDomain.tokens.begin();
            const auto end = begin + definition->typedDomain.tokenCount;
            const auto found = std::find(begin, end, current.Text());
            const std::size_t nextIndex = found == end
                ? 0u
                : (static_cast<std::size_t>(std::distance(begin, found)) +
                    1u) % definition->typedDomain.tokenCount;
            UiSettingsValue next;
            SettingsSnapshotError valueError;
            if (next.SetToken(definition->typedDomain.tokens[nextIndex], valueError))
                applyShortcutSetting(SettingId::UiZoom, std::move(next));
            else
                uvsr::log::warning("Keyboard setting %s failed: %s",
                    SettingName(SettingId::UiZoom).data(), valueError.Message());
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

bool UIRenderer::KeyboardCharInput(unsigned int unicode, int) { return RendererUiCharacter(unicode); }
bool UIRenderer::MousePosUpdate(double x, double y) { return RendererUiMousePosition(x, y); }
bool UIRenderer::MouseScrollUpdate(double x, double y) { return RendererUiMouseScroll(x, y); }
bool UIRenderer::MouseButtonUpdate(int button, int action, int)
{
    return RendererUiMouseButton(GetDeviceManager()->GetWindow(), button, action);
}
