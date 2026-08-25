#include "uvsr_internal.h"

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
        const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
        if (!localAppData || localAppData[0] == L'\0')
            return {};
        return std::filesystem::path(localAppData) /
            L"UVSR" / L"scene-load-history-v1.txt";
    }

auto UIRenderer::LoadSceneLoadTimingDatabase() -> void {
        const std::filesystem::path path =
            GetSceneLoadTimingDatabasePath();
        if (path.empty())
            return;

        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
            return;

        SceneLoadTimingDatabase database;
        if (!ReadSceneLoadTimingDatabase(input, database))
        {
    uvsr::log::warning(
                "Ignoring invalid scene loading history at %s",
                path.generic_string().c_str());
            return;
        }
        m_AllSceneLoadTiming = database.allScenes;
        m_SceneLoadTimingByScene = std::move(database.byScene);
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
        const SceneLoadTimingDatabase database = {
            m_AllSceneLoadTiming,
            m_SceneLoadTimingByScene
        };
        const bool serialized = output.is_open() &&
            WriteSceneLoadTimingDatabase(output, database);
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

auto UIRenderer::ApplyWordSpacing(
        ImGuiID& adjustedFontBakedId,
        float& baseSpaceAdvance,
        bool expanded) -> void {
        ImFontBaked* baked = ImGui::GetFontBaked();
        if (!baked)
            return;

        ImFontGlyph* spaceGlyph =
            baked->FindGlyphNoFallback(ImWchar(' '));
        if (!spaceGlyph)
            return;

        if (adjustedFontBakedId != baked->BakedId)
        {
            adjustedFontBakedId = baked->BakedId;
            baseSpaceAdvance = spaceGlyph->AdvanceX;
        }

        constexpr float WordSpaceScale = 1.65f;
        const float spaceAdvance = expanded
            ? baseSpaceAdvance * WordSpaceScale
            : baseSpaceAdvance;
        spaceGlyph->AdvanceX = spaceAdvance;
        if (baked->IndexAdvanceX.Size > int(ImWchar(' ')))
        {
            baked->IndexAdvanceX[int(ImWchar(' '))] =
                spaceAdvance;
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

auto UIRenderer::GetUiFontFaces(UiFontFamily family) -> UiFontFaces& {
        return m_UiFontFaces[static_cast<std::size_t>(
            ResolveUiFontFamily(family))];
    }

auto UIRenderer::GetUiFontFaces(UiFontFamily family) const -> const UiFontFaces& {
        return m_UiFontFaces[static_cast<std::size_t>(
            ResolveUiFontFamily(family))];
    }

auto UIRenderer::IsUiFontFamilyAvailable(UiFontFamily family) const -> bool {
        const UiFontFaces& faces = GetUiFontFaces(family);
        return faces.regular && faces.regular->GetScaledFont() &&
            faces.body && faces.body->GetScaledFont() &&
            faces.header && faces.header->GetScaledFont();
    }

auto UIRenderer::GetUiFontFamilyUnavailableReason(
        UiFontFamily family) -> std::string {
        switch (ResolveUiFontFamily(family))
        {
        case UiFontFamily::Codex:
            return
                "Codex (Segoe UI) requires the installed Windows fonts "
                "segoeui.ttf, seguisb.ttf, and segoeuib.ttf. Restore the "
                "standard Segoe UI fonts in Windows, then restart UVSR; "
                "Noto Sans remains available.";
        case UiFontFamily::NotoSans:
            return
                "UVSR could not initialize its required Noto Sans UI fonts. "
                "Reinstall UVSR with UVSR Launcher, then restart UVSR.";
        case UiFontFamily::ProggyClean:
            return
                "UVSR could not initialize the embedded ProggyClean UI "
                "fonts. Restart UVSR; if the problem continues, reinstall "
                "UVSR with UVSR Launcher.";
        case UiFontFamily::Count:
            break;
        }
        return
            "UVSR could not initialize the selected UI font family. "
            "Select Noto Sans or reinstall UVSR with UVSR Launcher.";
    }

auto UIRenderer::RequireUiFontFamily(UiFontFamily family) const -> void {
        if (!IsUiFontFamilyAvailable(family))
        {
            throw RequiredUiFontStartupError(
                GetUiFontFamilyUnavailableReason(family));
        }
    }

auto UIRenderer::GetActiveUiFont() -> ImFont* {
        RequireUiFontFamily(m_ui.FontFamily);
        UiFontFaces& faces = GetUiFontFaces(m_ui.FontFamily);
        if (ImGui::IsUvsrStockWidgetRenderingEnabled())
            return faces.regular->GetScaledFont();
        return faces.body->GetScaledFont();
    }

auto UIRenderer::GetActiveUiHeaderFont() -> ImFont* {
        RequireUiFontFamily(m_ui.FontFamily);
        return GetUiFontFaces(m_ui.FontFamily).header->GetScaledFont();
    }

auto UIRenderer::ApplyActiveUiWordSpacing() -> void {
        ApplyWordSpacing(
            m_AdjustedSpaceFontBakedId,
            m_BaseSpaceAdvance,
            GetUiSkinBehavior(m_ComposedUiSkin).expandedWordSpacing);
    }

auto UIRenderer::RestoreActiveUiWordSpacing() -> void {
        ApplyWordSpacing(
            m_AdjustedSpaceFontBakedId,
            m_BaseSpaceAdvance,
            false);
    }

auto UIRenderer::ApplyActiveUiHeaderWordSpacing() -> void {
        ApplyWordSpacing(
            m_AdjustedHeaderSpaceFontBakedId,
            m_BaseHeaderSpaceAdvance,
            GetUiSkinBehavior(m_ComposedUiSkin).expandedWordSpacing);
    }

auto UIRenderer::RestoreActiveUiHeaderWordSpacing() -> void {
        ApplyWordSpacing(
            m_AdjustedHeaderSpaceFontBakedId,
            m_BaseHeaderSpaceAdvance,
            false);
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
        const auto registerRequiredFont =
            [&](const char* path, float size, const char* weight)
            {
                try
                {
                    std::shared_ptr<app::RegisteredFont> font =
                        CreateFontFromFile(*(app->GetRootFs()), path, size);
                    if (!font || !font->HasFontData())
                    {
                        throw RequiredUiFontStartupError(
                            std::string("UVSR could not read the required Noto Sans ") +
                            weight + " UI font at '" + path +
                            "'. Reinstall UVSR.");
                    }
                    return font;
                }
                catch (const RequiredUiFontStartupError&)
                {
                    throw;
                }
                catch (const std::exception& error)
                {
                    throw RequiredUiFontStartupError(
                        std::string("UVSR could not read the required Noto Sans ") +
                        weight + " UI font at '" + path + "': " + error.what() +
                        ". Reinstall UVSR.");
                }
            };
        UiFontFaces& noto = GetUiFontFaces(UiFontFamily::NotoSans);
        noto.regular = registerRequiredFont(
            "/media/fonts/NotoSans/NotoSans-Regular.ttf", 13.f, "Regular");
        noto.body = registerRequiredFont(
            "/media/fonts/NotoSans/NotoSans-SemiBold.ttf", 16.f, "SemiBold");
        noto.header = registerRequiredFont(
            "/media/fonts/NotoSans/NotoSans-Bold.ttf", 16.f, "Bold");

        UiFontFaces& proggy = GetUiFontFaces(UiFontFamily::ProggyClean);
        proggy.regular = GetDefaultFont();
        proggy.body = std::make_shared<app::RegisteredFont>(16.f);
        m_fonts.push_back(proggy.body);
        // ProggyClean has one regular embedded face. Keep authored Amp
        // headings visibly emphasized with the required Noto Sans Bold face.
        proggy.header = noto.header;

        UiFontFaces& codex = GetUiFontFaces(UiFontFamily::Codex);
        const std::filesystem::path windowsFontsDirectory =
            GetWindowsFontsDirectory();
        if (!windowsFontsDirectory.empty())
        {
            NativeFileSystem windowsFileSystem;
            const auto registerOptionalWindowsFont =
                [&](const std::filesystem::path& path, float size)
                    -> std::shared_ptr<app::RegisteredFont>
                {
                    try
                    {
                        std::shared_ptr<app::RegisteredFont> font =
                            CreateFontFromFile(windowsFileSystem, path, size);
                        return font && font->HasFontData() ? font : nullptr;
                    }
                    catch (const std::exception& error)
                    {
                        uvsr::log::warning(
                            "Codex (Segoe UI) font registration failed: %s",
                            error.what());
                        return nullptr;
                    }
                    catch (...)
                    {
                        uvsr::log::warning(
                            "Codex (Segoe UI) font registration failed with "
                            "an unknown error.");
                        return nullptr;
                    }
                };
            codex.regular = registerOptionalWindowsFont(
                windowsFontsDirectory / L"segoeui.ttf", 13.f);
            codex.body = registerOptionalWindowsFont(
                windowsFontsDirectory / L"seguisb.ttf", 16.f);
            codex.header = registerOptionalWindowsFont(
                windowsFontsDirectory / L"segoeuib.ttf", 16.f);
        }
        if (!codex.regular || !codex.body || !codex.header)
        {
            uvsr::log::warning(
                "Codex (Segoe UI) is unavailable because one or more "
                "required Windows Segoe UI font files could not be read. "
                "Noto Sans remains available.");
        }

        ImGui::GetIO().IniFilename = nullptr;
        LoadSceneLoadTimingDatabase();
    }

auto UIRenderer::Animate(float elapsedTimeSeconds) -> void {
        if (m_RequiredFontsReady)
        {
            ImGui_Renderer::Animate(elapsedTimeSeconds);
            return;
        }

        try
        {
            ImGui_Renderer::Animate(elapsedTimeSeconds);
            RequireUiFontFamily(UiFontFamily::NotoSans);
            RequireUiFontFamily(m_ui.FontFamily);
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

auto UIRenderer::ShouldSuppressFullscreenShortcut() const -> bool {
        return m_CommandOpen;
    }

auto UIRenderer::Init(std::shared_ptr<ShaderFactory> shaderFactory) -> bool {
        if (!ImGui_Renderer::Init(shaderFactory))
            return false;

        m_BackdropBlurPass = std::make_unique<BackdropBlurPass>(
            GetDevice(),
            m_app->GetRendererShaderFactory(),
            m_app->GetRendererCommonPasses());
        m_PixelZoomPass = std::make_unique<PixelZoomPass>(
            GetDevice(),
            m_app->GetRendererShaderFactory(),
            m_app->GetRendererCommonPasses());
        return true;
    }

#if defined(UVSR_BUILD_TESTING)
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
                    std::isfinite(material->normalTextureScale);
            });
        if (selected == materials.end())
        {
            error = "the loaded scene has no finite editable material";
            return false;
        }

        m_ui.SelectedMaterial = *selected;
        const float replacement =
            (*selected)->normalTextureScale >= 0.f ? -1.f : 1.f;
        return ApplyRuntimeSetting(
            "material.selected.normal-scale",
            FormatCommandFloat(replacement),
            error);
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

        m_SelectedLight = light;
        const float replacement = light->angularSize < 10.f ? 20.f : 0.f;
        return ApplyRuntimeSetting(
            "light.selected.angular-size",
            FormatCommandFloat(replacement),
            error);
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UIRenderer::ToggleRuntimeDiagnosticFlashlight(
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

        m_SelectedLight = *flashlight;
        return ApplyRuntimeSetting(
            "light.selected.flashlight.enabled",
            m_ui.FlashlightEnabled ? "off" : "on",
            error);
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

            for (const std::shared_ptr<Light>& light :
                m_app->GetEditableLights())
            {
                if (m_app->IsFlashlight(light))
                {
                    m_SelectedLight = light;
                    break;
                }
            }
            if (!m_SelectedLight || !m_app->IsFlashlight(m_SelectedLight))
            {
                RetainedRuntimeDirective failure;
                failure.kind = RetainedRuntimeDirectiveKind::FinishFail;
                failure.payload = "retained flashlight is unavailable";
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
        telemetry.receiverSampleCount =
            m_app->GetActiveRasterSampleCount();
        telemetry.pathHistoryCount =
            m_app->GetPathTracingCenterPixelAcceptedSampleCount();
        telemetry.screenVisibilityDispatched =
            m_app->DidDispatchScreenSpaceVisibilityThisFrame();
        telemetry.directionalVisibilityDispatched =
            m_app->DidDispatchDirectionalRayVisibilityThisFrame();
        telemetry.skyVisibilityDispatched =
            m_app->DidDispatchRayTracedSkyVisibilityThisFrame();
        telemetry.flashlightLightingSubmitted =
            m_app->DidSubmitFlashlightLightingThisFrame();
        telemetry.flashlightVisibilityDispatched =
            m_app->DidDispatchRayTracedFlashlightShadowThisFrame();
        telemetry.shadowDenoisingDispatched =
            m_app->DidDispatchShadowDenoisingThisFrame();
        telemetry.skyDenoisingDispatched =
            m_app->DidDispatchSkyDenoisingThisFrame();
        telemetry.ambientOcclusionDenoisingDispatched =
            m_app->IsRendererStageActiveThisFrame(
                RendererTimingStage::AmbientOcclusionDenoise);
        telemetry.globalIlluminationDenoisingDispatched =
            m_app->IsRendererStageActiveThisFrame(
                RendererTimingStage::DiffuseIlluminationDenoise);
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

        if (m_RetainedRuntimePathReselectionPending)
        {
            if (m_ui.Lighting != LightingSolution::RayMarching)
            {
                finish(m_RetainedRuntimeDiagnostic->Abort(
                    "lighting-solution cycle did not render its Ray Marching leg",
                    now));
                return;
            }
            // This function runs after the frame was rendered. Returning to
            // Path Tracing here therefore guarantees a real Ray Marching
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
            ResetAllSettingsToFactoryDefaults();
            if (m_RetainedRuntimeBaselineWidth > 0 &&
                m_RetainedRuntimeBaselineHeight > 0)
            {
                glfwSetWindowSize(
                    GetDeviceManager()->GetWindow(),
                    m_RetainedRuntimeBaselineWidth,
                    m_RetainedRuntimeBaselineHeight);
            }
            if (m_RetainedRuntimeBaselineCamera)
            {
                m_app->RestoreRetainedRuntimeCameraPose(
                    *m_RetainedRuntimeBaselineCamera);
            }
            if (!directive.runtimeCase->actionSettingName.empty())
            {
                std::string error;
                if (!ApplyRuntimeSetting(
                        directive.runtimeCase->actionSettingName,
                        directive.runtimeCase->actionBaselineValue,
                        error))
                {
                    abort("baseline SET " +
                        directive.runtimeCase->actionSettingName + "=" +
                        directive.runtimeCase->actionBaselineValue +
                        " failed: " + error);
                    return;
                }
            }
            for (const auto& [name, value] :
                directive.runtimeCase->settings)
            {
                if (name == directive.runtimeCase->actionSettingName)
                    continue;
                std::string error;
                if (!ApplyRuntimeSetting(name, value, error))
                {
                    abort(
                        "SET " + name + "=" + value + " failed: " + error);
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
                    directive.runtimeCase->requireCrossCaseDistinctness)
                {
                    if (m_RetainedRuntimeBaselineWidth > 0 &&
                        m_RetainedRuntimeBaselineHeight > 0)
                    {
                        glfwSetWindowSize(
                            GetDeviceManager()->GetWindow(),
                            m_RetainedRuntimeBaselineWidth,
                            m_RetainedRuntimeBaselineHeight);
                    }
                    if (m_RetainedRuntimeBaselineCamera)
                    {
                        m_app->RestoreRetainedRuntimeCameraPose(
                            *m_RetainedRuntimeBaselineCamera);
                    }
                }
                std::string error;
                if (directive.actionSettingName.empty() ||
                    !ApplyRuntimeSetting(
                        directive.actionSettingName,
                        directive.actionValue,
                        error))
                {
                    abort("action SET " + directive.actionSettingName +
                        "=" + directive.actionValue + " failed: " +
                        error);
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

            case RetainedRuntimeAction::None:
                abort("state requested the none runtime action");
                return;
            }
            m_LastRetainedRuntimeAction = directive.action;
            return;
        }

        case RetainedRuntimeDirectiveKind::ResetSettings:
            ResetAllSettingsToFactoryDefaults();
            return;

        case RetainedRuntimeDirectiveKind::RestoreSnapshot:
        {
            std::string error;
            std::size_t changedValueCount = 0u;
            if (!ApplyCanonicalSettingsSnapshot(
                    directive.payload,
                    changedValueCount,
                    error))
                abort(error);
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

        const float deltaTime = ImGui::GetIO().DeltaTime;
        const bool uiMotionEnabled =
            ResolveUiMotionEnabled(
                m_ui.Skin,
                m_ui.AnimationsEnabled);
        const bool pixelZoomRequestedByUi =
            IsPixelZoomEnabled(m_ui.PixelZoom);
        const bool pixelZoomRequested = pixelZoomRequestedByUi;
        if (!uiMotionEnabled)
        {
            m_PixelZoomVisibility = pixelZoomRequested ? 1.f : 0.f;
            m_RenderedPixelZoom = pixelZoomRequested
                ? m_ui.PixelZoom
                : PixelZoomMode::Off;
            m_PendingPixelZoom = m_RenderedPixelZoom;
            m_PixelZoomLevelTransition = 1.f;
        }
        else
        {
            m_PixelZoomVisibility = AdvancePixelZoomVisibility(
                m_PixelZoomVisibility,
                pixelZoomRequested,
                deltaTime);
            if (pixelZoomRequested)
            {
                if (!IsPixelZoomEnabled(m_RenderedPixelZoom))
                {
                    m_RenderedPixelZoom = m_ui.PixelZoom;
                    m_PendingPixelZoom = m_ui.PixelZoom;
                    m_PixelZoomLevelTransition = 1.f;
                }
                else if (m_PixelZoomVisibility < 1.f)
                {
                    // Opening remains responsive to rapid level changes. The
                    // dedicated level pulse begins only from the stable,
                    // fully-visible endpoint.
                    m_RenderedPixelZoom = m_ui.PixelZoom;
                    m_PendingPixelZoom = m_ui.PixelZoom;
                }
                else
                {
                    if (m_PixelZoomLevelTransition >= 1.f &&
                        m_ui.PixelZoom != m_RenderedPixelZoom)
                    {
                        m_PendingPixelZoom = m_ui.PixelZoom;
                        m_PixelZoomLevelTransition = 0.f;
                    }
                    else if (m_PixelZoomLevelTransition < 1.f)
                    {
                        m_PendingPixelZoom = m_ui.PixelZoom;
                        m_PixelZoomLevelTransition =
                            AdvancePixelZoomLevelTransition(
                                m_PixelZoomLevelTransition,
                                deltaTime);
                        if (ShouldSwitchPixelZoomLevel(
                            m_PixelZoomLevelTransition))
                        {
                            m_RenderedPixelZoom = m_PendingPixelZoom;
                        }
                    }
                }
            }
            else
            {
                m_PendingPixelZoom = m_RenderedPixelZoom;
                m_PixelZoomLevelTransition = 1.f;
            }
            if (!pixelZoomRequested && m_PixelZoomVisibility <= 0.f)
            {
                m_RenderedPixelZoom = PixelZoomMode::Off;
                m_PendingPixelZoom = PixelZoomMode::Off;
            }
        }
        m_CommandAppearance = uiMotionEnabled
            ? AdvancePixelZoomVisibility(
                m_CommandAppearance,
                m_CommandOpen,
                deltaTime)
            : m_CommandOpen ? 1.f : 0.f;
        buildUI();
        DrawCommandInterface();
        const float pixelZoomOpacity =
            uiMotionEnabled
                ? SmoothPixelZoomVisibility(m_PixelZoomVisibility)
                : m_PixelZoomVisibility;
        const float pixelZoomLevelTransitionScale =
            uiMotionEnabled
                ? ResolvePixelZoomLevelTransitionScale(
                    m_PixelZoomLevelTransition)
                : 1.f;
        const bool pixelZoomPassActive = IsPixelZoomPassActive(
            m_RenderedPixelZoom,
            pixelZoomOpacity);
        const float materialDrawerOpacity =
            SmoothPixelZoomVisibility(m_MaterialDrawerAppearance) *
            SmoothPixelZoomVisibility(m_SettingsAppearance);
        const float crosshairOpacity = std::max(
            pixelZoomRequested ? pixelZoomOpacity : 0.f,
            materialDrawerOpacity);
        if (crosshairOpacity > 0.f)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImVec2 crosshairCenter(
                viewport->Pos.x + std::floor(viewport->Size.x * 0.5f) + 0.5f,
                viewport->Pos.y + std::floor(viewport->Size.y * 0.5f) + 0.5f);
            ImGui::GetForegroundDrawList()->AddCircleFilled(
                crosshairCenter,
                2.f,
                IM_COL32(
                    255,
                    255,
                    255,
                    int(std::round(128.f * crosshairOpacity))),
                12);
        }
        if (pixelZoomPassActive)
        {
            const nvrhi::FramebufferInfoEx& framebufferInfo =
                framebuffer->getFramebufferInfo();
            const PixelZoomLayout zoomLabelLayout =
                ResolveAnimatedPixelZoomLayout(
                    ResolvePixelZoomLayout(
                        framebufferInfo.width,
                        framebufferInfo.height,
                        m_SettingsPanelMarginPixels,
                        m_RenderedPixelZoom),
                    pixelZoomOpacity,
                    pixelZoomLevelTransitionScale);
            const char* zoomAreaLabel =
                GetPixelZoomAreaLabel(m_RenderedPixelZoom);
            ImFont* zoomLabelFont = GetActiveUiFont();
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
            const int zoomLabelAlpha = int(std::round(
                230.f * pixelZoomOpacity));
            const int zoomLabelShadowAlpha = int(std::round(
                150.f * pixelZoomOpacity));
            ImDrawList* foregroundDrawList =
                ImGui::GetForegroundDrawList();
            const ImVec2 zoomAreaLabelShadowPosition(
                zoomAreaLabelPosition.x + 1.f,
                zoomAreaLabelPosition.y + 1.f);
            foregroundDrawList->AddText(
                zoomLabelFont,
                zoomLabelFont->LegacySize,
                zoomAreaLabelShadowPosition,
                IM_COL32(0, 0, 0, zoomLabelShadowAlpha),
                zoomAreaLabel);
            foregroundDrawList->AddText(
                zoomLabelFont,
                zoomLabelFont->LegacySize,
                zoomAreaLabelPosition,
                IM_COL32(
                    255,
                    255,
                    255,
                    zoomLabelAlpha),
                zoomAreaLabel);
        }
        ImGui::Render();
        if (m_PendingCommand &&
            !HasDeferredDropdownUiActions())
        {
            // A slash command is the newest input. Let any older dropdown
            // choice finish its roll-up, settle, and full idle presentation
            // before applying the command so renderer mutation never interrupts
            // either transaction. The command still wins by executing next.
            UiCommand command = std::move(*m_PendingCommand);
            m_PendingCommand.reset();
            ExecuteUiCommand(command);
        }
        if (pixelZoomPassActive && m_PixelZoomPass)
            m_PixelZoomPass->Capture(framebuffer);
        if (m_BackdropBlurPass)
        {
            const bool backdropEnabled =
                GetUiSkinBehavior(m_ComposedUiSkin).backdropEnabled;
            if (!m_BackdropBlurPass->Render(
                    framebuffer,
                    backdropEnabled ? UiBackgroundBlurPixels : 0.f,
                    m_ui.BackdropRects))
            {
                uvsr::log::error("Required UI backdrop blur pass failed");
                GetDeviceManager()->ReportRenderDisposition(
                    uvsr::RendererRenderDisposition::Failed);
                m_imguiFrameOpened = false;
                return;
            }
        }
        if (pixelZoomPassActive && m_PixelZoomPass)
        {
            m_PixelZoomPass->Composite(
                framebuffer,
                m_RenderedPixelZoom,
                m_SettingsPanelMarginPixels,
                ImGui::GetStyle().WindowRounding,
                pixelZoomOpacity,
                pixelZoomLevelTransitionScale);
        }
        imgui_nvrhi->render(framebuffer);
        m_imguiFrameOpened = false;
    }

auto UIRenderer::BackBufferResizing() -> void {
        if (m_BackdropBlurPass)
            m_BackdropBlurPass->BackBufferResizing();
        if (m_PixelZoomPass)
            m_PixelZoomPass->BackBufferResizing();
        ImGui_Renderer::BackBufferResizing();
    }

auto UIRenderer::DisplayScaleChanged(
        float scaleX,
        float scaleY) -> void {
        ImGui_Renderer::DisplayScaleChanged(scaleX, scaleY);
        m_UiDisplayScale = std::clamp(scaleX, 0.5f, 4.f);
    }

auto UIRenderer::KeyboardUpdate(
        int key,
        int scancode,
        int action,
        int mods) -> bool {
        const bool captured = ImGui_Renderer::KeyboardUpdate(
            key, scancode, action, mods);
        const bool settingsShortcutOwnedByUi =
            ImGui::GetIO().WantTextInput ||
            ImGui::IsAnyItemActive() ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
        const bool plainCommandShortcut =
            (mods & (GLFW_MOD_CONTROL |
                GLFW_MOD_ALT |
                GLFW_MOD_SUPER |
                GLFW_MOD_SHIFT)) == 0;
        if (m_CommandOpen)
        {
            if (key == GLFW_KEY_SLASH &&
                action == GLFW_PRESS &&
                plainCommandShortcut)
            {
                m_CommandOpen = false;
                m_CommandFocusRequested = false;
                m_SuppressCommandShortcutSlashCharacter = true;
                ImGui::ClearActiveID();
            }
            return true;
        }

        if (key == GLFW_KEY_SLASH &&
            action == GLFW_PRESS &&
            plainCommandShortcut &&
            !ImGui::GetIO().WantTextInput)
        {
            m_CommandOpen = true;
            m_CommandFocusRequested = true;
            m_SuppressCommandShortcutSlashCharacter = true;
            return true;
        }

        if ((key == GLFW_KEY_ESCAPE ||
                key == GLFW_KEY_GRAVE_ACCENT) &&
            action == GLFW_PRESS &&
            !settingsShortcutOwnedByUi)
        {
            m_ui.ShowUI = !m_ui.ShowUI;
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
            m_ui.PixelZoom =
                AdvancePixelZoomMode(m_ui.PixelZoom);
            return true;
        }
        const bool plainMaterialEditorShortcut =
            (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) == 0;
        if (key == GLFW_KEY_M &&
            action == GLFW_PRESS &&
            plainMaterialEditorShortcut &&
            !ImGui::GetIO().WantTextInput)
        {
            RequestMaterialDrawerVisible(true);
            return true;
        }

        return captured;
    }

auto UIRenderer::KeyboardCharInput(
        unsigned int unicode,
        int mods) -> bool {
        if (m_SuppressCommandShortcutSlashCharacter)
        {
            m_SuppressCommandShortcutSlashCharacter = false;
            if (unicode == static_cast<unsigned int>('/'))
                return true;
        }
        if (m_CommandOpen)
        {
            ImGui_Renderer::KeyboardCharInput(unicode, mods);
            return true;
        }
        return ImGui_Renderer::KeyboardCharInput(unicode, mods);
    }
