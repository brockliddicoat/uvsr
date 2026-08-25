#include "uvsr_internal.h"

auto UIRenderer::FormatCommandFloat3(const float3& value) -> std::string {
        return FormatCommandFloat(value.x) + " " +
            FormatCommandFloat(value.y) + " " +
            FormatCommandFloat(value.z);
    }

auto UIRenderer::ApplyCommandFloat3(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        float3& current,
        const float3& defaultValue,
        float minimum,
        float maximum,
        std::string& value,
        std::string& error) -> bool {
        float3 candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            if (arguments.size() != 3u ||
                !TryParseCommandFloat(arguments[0], candidate.x) ||
                !TryParseCommandFloat(arguments[1], candidate.y) ||
                !TryParseCommandFloat(arguments[2], candidate.z) ||
                candidate.x < minimum || candidate.x > maximum ||
                candidate.y < minimum || candidate.y > maximum ||
                candidate.z < minimum || candidate.z > maximum)
            {
                error = std::string(path) +
                    " expects three finite numbers from " +
                    FormatCommandFloat(minimum) + " through " +
                    FormatCommandFloat(maximum) + ".";
                return false;
            }
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }
        if (operation != CommandValueOperation::Get &&
            candidate.x == current.x &&
            candidate.y == current.y &&
            candidate.z == current.z)
        {
            return RejectUnchangedCommandMutation(path, error);
        }
        current = candidate;
        value = FormatCommandFloat3(current);
        return true;
    }

auto UIRenderer::SetCommandResult(
        std::string result,
        bool error ) -> void {
        m_CommandResult = error ? "Error: " : "Success: ";
        m_CommandResult += std::move(result);
        m_CommandResultIsError = error;
    }

auto UIRenderer::IsCommandRuntimeMutationLocked(
        const UiSettingsCommandDefinition& definition) const -> bool {
        return uvsr::IsUiSettingsRuntimeMutationLocked(
            definition.section,
            m_app->IsSceneBusy());
    }

auto UIRenderer::CheckCommandMutationAllowed(
        const UiSettingsCommandDefinition& definition,
        std::string& error) const -> bool {
        if (IsCommandRuntimeMutationLocked(definition))
        {
            error =
                "This setting cannot change while a scene is loading.";
            return false;
        }
        return true;
    }

auto UIRenderer::DispatchUiCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        if (path == "ui.skin")
        {
            static constexpr std::array<
                std::pair<std::string_view, UiSkin>, 2> Options = {{
                { "amp", UiSkin::Amp },
                { "ogg", UiSkin::Og }
            }};
            return ApplyCommandEnum(
                operation,
                arguments,
                path,
                m_ui.Skin,
                DefaultUiSkin,
                Options,
                value,
                error);
        }
        if (path == "ui.font-family")
        {
            static constexpr std::array<
                std::pair<std::string_view, UiFontFamily>, 3> Options = {{
                { "codex", UiFontFamily::Codex },
                { "noto-sans", UiFontFamily::NotoSans },
                { "proggy-clean", UiFontFamily::ProggyClean }
            }};
            UiFontFamily candidate = m_ui.FontFamily;
            if (!ApplyCommandEnum(
                    operation,
                    arguments,
                    path,
                    candidate,
                    DefaultUiFontFamily,
                    Options,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get &&
                !IsUiFontFamilyAvailable(candidate))
            {
                error = GetUiFontFamilyUnavailableReason(candidate);
                return false;
            }
            m_ui.FontFamily = candidate;
            return true;
        }
        if (path == "ui.animations")
        {
            return ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.AnimationsEnabled,
                true,
                value,
                error);
        }
        if (path == "ui.override-visual-maxes")
        {
            return ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.OverrideVisualMaxes,
                false,
                value,
                error);
        }
        if (path == "ui.accent.main")
        {
            UiSkinPalette* palette =
                FindUiSkinPalette(m_ui.Accents, m_ui.Skin);
            const UiSkinPalette* defaultPalette =
                FindDefaultUiSkinPalette(m_ui.Skin);
            if (!palette || !defaultPalette)
            {
                error =
                    "ui.accent.main is unavailable while Ogg owns stock "
                    "ImGui colors.";
                return false;
            }
            return ApplyCommandUiColorRgb(
                operation,
                arguments,
                path,
                palette->primaryAccent,
                defaultPalette->primaryAccent,
                value,
                error);
        }
        if (path == "ui.accent.negative")
        {
            return ApplyCommandUiColorRgb(
                operation,
                arguments,
                path,
                m_ui.Accents.secondaryAccent,
                DefaultUiSecondaryAccent,
                value,
                error);
        }
        if (path == "ui.accent.positive")
        {
            return ApplyCommandUiColorRgb(
                operation,
                arguments,
                path,
                m_ui.Accents.tertiaryAccent,
                DefaultUiTertiaryAccent,
                value,
                error);
        }
        if (path == "ui.accent.secondary")
        {
            return ApplyCommandUiColorRgba(
                operation,
                arguments,
                path,
                m_ui.Accents.secondaryAccent,
                DefaultUiSecondaryAccent,
                value,
                error);
        }
        if (path == "ui.accent.tertiary")
        {
            return ApplyCommandUiColorRgba(
                operation,
                arguments,
                path,
                m_ui.Accents.tertiaryAccent,
                DefaultUiTertiaryAccent,
                value,
                error);
        }
        if (path == "ui.accent.primary" ||
            path == "ui.accent.font" ||
            path == "ui.accent.primary-background")
        {
            UiSkinPalette* palette =
                FindUiSkinPalette(m_ui.Accents, m_ui.Skin);
            const UiSkinPalette* defaultPalette =
                FindDefaultUiSkinPalette(m_ui.Skin);
            if (!palette || !defaultPalette)
            {
                error = std::string(path) +
                    " is unavailable while Ogg owns stock ImGui colors.";
                return false;
            }

            UiRgbaColor* current = nullptr;
            const UiRgbaColor* defaultValue = nullptr;
            if (path == "ui.accent.primary")
            {
                current = &palette->primaryAccent;
                defaultValue = &defaultPalette->primaryAccent;
            }
            else if (path == "ui.accent.font")
            {
                current = &palette->fontColor;
                defaultValue = &defaultPalette->fontColor;
            }
            else
            {
                current = &palette->primaryBackground;
                defaultValue = &defaultPalette->primaryBackground;
            }
            return ApplyCommandUiColorRgba(
                operation,
                arguments,
                path,
                *current,
                *defaultValue,
                value,
                error);
        }
        if (path == "ui.visible")
        {
            return ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.ShowUI,
                false,
                value,
                error);
        }
        if (path == "ui.settings-collapsed")
        {
            bool collapsed = m_SettingsCollapsedRequest.value_or(
                m_SettingsCollapsed);
            if (!ApplyCommandBool(
                    operation,
                    arguments,
                    path,
                    collapsed,
                    false,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
            {
                m_SettingsCollapsedRequest = collapsed;
                m_SettingsCollapsed = collapsed;
            }
            return true;
        }
        if (path == "ui.zoom")
        {
            static constexpr std::array<
                std::pair<std::string_view, PixelZoomMode>, 5> Options = {{
                { "off", PixelZoomMode::Off },
                { "2x", PixelZoomMode::Zoom2x },
                { "3x", PixelZoomMode::Zoom3x },
                { "4x", PixelZoomMode::Zoom4x },
                { "5x", PixelZoomMode::Zoom5x }
            }};
            return ApplyCommandEnum(
                operation,
                arguments,
                path,
                m_ui.PixelZoom,
                PixelZoomMode::Off,
                Options,
                value,
                error);
        }
        if (path == "material-editor.visible")
        {
            bool candidate = m_ui.ShowMaterialDrawer;
            if (!ApplyCommandBool(
                    operation,
                    arguments,
                    path,
                    candidate,
                    false,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
                RequestMaterialDrawerVisible(candidate);
            return true;
        }
        error = "Internal UI command binding is missing for '" +
            std::string(path) + "'.";
        return false;
    }

auto UIRenderer::RequestMaterialDrawerVisible(bool visible) -> void {
        m_app->SetMaterialDrawerVisible(visible);
        m_MaterialRevealRequested = visible;
        if (visible)
        {
            m_SettingsCollapsedRequest = false;
            m_SettingsCollapsed = false;
        }
    }

auto UIRenderer::GetActiveGpuAdapterChoice() const -> const GpuAdapterChoice* {
        const auto active = std::find_if(
            m_ui.GpuAdapterChoices.begin(),
            m_ui.GpuAdapterChoices.end(),
            [this](const GpuAdapterChoice& adapter)
            {
                return adapter.adapterIndex ==
                    m_ui.ActiveGpuAdapterIndex;
            });
        return active != m_ui.GpuAdapterChoices.end()
            ? &*active
            : nullptr;
    }

auto UIRenderer::GetDefaultAdaptiveSyncMode() const -> AdaptiveSyncMode {
        const GpuAdapterChoice* adapter = GetActiveGpuAdapterChoice();
        return DefaultAdaptiveSyncMode(
            adapter ? adapter->vendorId : 0u,
            GetDeviceManager()->IsPresentAllowTearingSupported());
    }

auto UIRenderer::IsAdaptiveSyncModeAvailableForActiveAdapter(
        AdaptiveSyncMode mode) const -> bool {
        const GpuAdapterChoice* adapter = GetActiveGpuAdapterChoice();
        return IsAdaptiveSyncModeAvailable(
            mode,
            adapter ? adapter->vendorId : 0u,
            GetDeviceManager()->IsPresentAllowTearingSupported());
    }

auto UIRenderer::ApplyAdaptiveSyncMode(AdaptiveSyncMode mode) -> void {
        m_ui.AdaptiveSync = mode;
        GetDeviceManager()->SetPresentAllowTearing(
            AdaptiveSyncRequestsPresentTearing(mode));
    }

auto UIRenderer::ApplyLightingSolution(LightingSolution solution) -> void {
        const LightingSolutionTransition transition =
            ResolveLightingSolutionTransition(m_ui.Lighting, solution);
        if (!transition.accepted)
            return;
        m_ui.Lighting = transition.selection;
        if (transition.openPathTracingDrawer)
            m_PathingDrawerOpenRequested = true;
        if (transition.resetHistory)
            m_app->ResetImageBasedLightingHistory();
    }

auto UIRenderer::ResetAllSettingsToFactoryDefaults() -> void {
        // Renderer defaults own the rendering passes and Pixel Zoom. Restore
        // the session-owned Interface and Performance settings here rather
        // than replacing UIData, which would also erase the active adapter,
        // scene-independent navigation state, and pending capture state.
        m_app->ResetAllRendererSettings();
        ApplyAdaptiveSyncMode(GetDefaultAdaptiveSyncMode());
        m_ui.Skin = DefaultUiSkin;
        m_ui.FontFamily = DefaultUiFontFamily;
        m_ui.AnimationsEnabled = true;
        m_ui.OverrideVisualMaxes = false;
        m_ui.Accents = UiAccentSettings{};
        m_StatisticsEffect =
            static_cast<int>(StatisticsEffect::CompleteRenderer);
        m_PerformanceCollapsedRequest = true;
        m_PathingDrawerOpenRequested = false;
        ImGui::CloseUvsrColorPickerPopup();
    }

auto UIRenderer::DispatchGeneralCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        if (path == "lighting.solution")
        {
            static constexpr std::array<
                std::pair<std::string_view, LightingSolution>, 2> Options = {{
                { "ray-marching", LightingSolution::RayMarching },
                { "path-tracing", LightingSolution::PathTracing }
            }};
            LightingSolution candidate = m_ui.Lighting;
            if (!ApplyCommandEnum(
                    operation,
                    arguments,
                    path,
                    candidate,
                    LightingSolution::RayMarching,
                    Options,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
                ApplyLightingSolution(candidate);
            return true;
        }
        if (path == "gpu.adapter")
        {
            if (operation == CommandValueOperation::Get)
            {
                value = FormatSettingsSnapshotAdapterToken(
                    m_ui.ActiveGpuAdapterIndex);
                return true;
            }
            if (operation != CommandValueOperation::Set)
            {
                error = "gpu.adapter supports get and set only.";
                return false;
            }

            const std::string requested = JoinCommandArguments(arguments);
            if (requested.empty())
            {
                error =
                    "gpu.adapter expects an adapter index or unique name.";
                return false;
            }
            std::vector<SettingsSnapshotAdapterOption> options;
            options.reserve(m_ui.GpuAdapterChoices.size());
            for (const GpuAdapterChoice& adapter : m_ui.GpuAdapterChoices)
                options.push_back({ adapter.adapterIndex, adapter.name });
            int64_t requestedIndex = -1;
            if (!ResolveSettingsSnapshotAdapterToken(
                    requested, options, requestedIndex, value, error))
                return false;
            if (requestedIndex == m_ui.ActiveGpuAdapterIndex)
                return RejectUnchangedCommandMutation(path, error);
            g_RestartAdapterIndex = static_cast<int>(requestedIndex);
            g_RestartRequested = true;
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            return true;
        }
        if (path == "gpu.adaptive-sync")
        {
            static constexpr std::array<
                std::pair<std::string_view, AdaptiveSyncMode>, 3> Options = {{
                { "off", AdaptiveSyncMode::Off },
                { "vendor-agnostic", AdaptiveSyncMode::VendorAgnostic },
                { "nvidia-exclusive", AdaptiveSyncMode::NvidiaExclusive }
            }};
            AdaptiveSyncMode candidate = m_ui.AdaptiveSync;
            if (!ApplyCommandEnum(
                    operation,
                    arguments,
                    path,
                    candidate,
                    GetDefaultAdaptiveSyncMode(),
                    Options,
                    value,
                    error))
            {
                return false;
            }
            if (operation == CommandValueOperation::Get)
                return true;
            if (!IsAdaptiveSyncModeAvailableForActiveAdapter(candidate))
            {
                error = GetDeviceManager()->IsPresentAllowTearingSupported()
                    ? "Nvidia Exclusive requires an NVIDIA graphics adapter."
                    : "Adaptive Sync requires DXGI tearing-present support.";
                return false;
            }
            ApplyAdaptiveSyncMode(candidate);
            value = std::string(AdaptiveSyncModeToken(candidate));
            return true;
        }
        if (path == "camera.mode")
        {
            static constexpr std::array<
                std::pair<std::string_view, CameraMode>, 2> Options = {{
                { "freelook", CameraMode::ThirdPerson },
                { "locked", CameraMode::Static }
            }};
            CameraMode candidate = m_ui.Camera;
            if (!ApplyCommandEnum(
                    operation,
                    arguments,
                    path,
                    candidate,
                    CameraMode::ThirdPerson,
                    Options,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
                m_app->SetCameraMode(candidate);
            return true;
        }
        if (path == "scene.current")
        {
            if (operation == CommandValueOperation::Get)
            {
                const SceneCatalogEntry* scene = FindSceneCatalogEntry(
                    m_app->GetAvailableScenes(),
                    m_app->GetCurrentSceneName());
                value = scene
                    ? FormatSettingsSnapshotSceneToken(
                        MakeSceneDisplayName(
                            m_app->GetSceneDir(), scene->FileName))
                    : std::string{};
                if (!value.empty())
                    return true;
                error = "scene.current is not a canonical catalog filename.";
                return false;
            }
            if (operation != CommandValueOperation::Set)
            {
                error = "scene.current supports get and set only.";
                return false;
            }
            const std::string requested = JoinCommandArguments(arguments);
            std::vector<SettingsSnapshotSceneOption> options;
            options.reserve(m_app->GetAvailableScenes().size());
            for (const SceneCatalogEntry& scene : m_app->GetAvailableScenes())
            {
                const std::string token = FormatSettingsSnapshotSceneToken(
                    MakeSceneDisplayName(
                        m_app->GetSceneDir(), scene.FileName));
                if (token.empty())
                {
                    error = "scene catalog contains a noncanonical filename";
                    return false;
                }
                options.push_back(
                    { token, scene.DisplayName, scene.FileName });
            }
            std::string requestedFileName;
            if (!ResolveSettingsSnapshotSceneToken(
                    requested,
                    options,
                    requestedFileName,
                    value,
                    error))
                return false;
            if (requestedFileName == m_app->GetCurrentSceneName())
                return RejectUnchangedCommandMutation(path, error);
            m_app->SetCurrentSceneName(requestedFileName);
            return true;
        }
        error = "Internal General command binding is missing for '" +
            std::string(path) + "'.";
        return false;
    }

auto UIRenderer::DispatchRepresentationCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        WorldSpaceRepresentationSettings candidate = m_ui.Representation;
        const WorldSpaceRepresentationSettings factoryDefaults;
        bool handled = true;

        if (path == "representation.bvh.build-preference")
        {
            static constexpr std::array<
                std::pair<std::string_view, BvhBuildPreference>, 3>
                Options = {{
                    { "fast-trace", BvhBuildPreference::FastTrace },
                    { "balanced", BvhBuildPreference::Balanced },
                    { "fast-build", BvhBuildPreference::FastBuild }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.bvhBuildPreference,
                factoryDefaults.bvhBuildPreference,
                Options,
                value,
                error);
        }
        else if (path == "representation.blas.update-mode")
        {
            static constexpr std::array<
                std::pair<std::string_view, BlasUpdateMode>, 2>
                Options = {{
                    { "rebuild", BlasUpdateMode::Rebuild },
                    { "refit", BlasUpdateMode::Refit }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.blasUpdateMode,
                factoryDefaults.blasUpdateMode,
                Options,
                value,
                error);
        }
        else if (path == "representation.tlas.update-mode")
        {
            static constexpr std::array<
                std::pair<std::string_view, TlasUpdateMode>, 2>
                Options = {{
                    { "rebuild", TlasUpdateMode::Rebuild },
                    { "refit", TlasUpdateMode::Refit }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.tlasUpdateMode,
                factoryDefaults.tlasUpdateMode,
                Options,
                value,
                error);
        }
        else if (path == "representation.allow-ray-traversal")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.allowRayTraversal,
                factoryDefaults.allowRayTraversal,
                value,
                error);
        }
        else
        {
            handled = false;
            error =
                "Internal Representation command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (!handled)
            return false;
        if (operation != CommandValueOperation::Get)
        {
            const WorldSpaceRepresentationInvalidation invalidation =
                GetWorldSpaceRepresentationInvalidation(
                    m_ui.Representation,
                    candidate);
            m_ui.Representation = candidate;
            m_app->InvalidateWorldSpaceRepresentation(invalidation);
            m_app->ResetImageBasedLightingHistory();
        }
        return true;
    }

auto UIRenderer::DispatchNoiseCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        if (path == "noise.accumulate-samples")
        {
            bool candidate = m_ui.AccumulateSamples;
            if (!ApplyCommandBool(
                    operation,
                    arguments,
                    path,
                    candidate,
                    false,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
            {
                const bool changed = m_ui.AccumulateSamples != candidate;
                m_ui.AccumulateSamples = candidate;
                if (changed &&
                    m_ui.Lighting == LightingSolution::RayMarching)
                {
                    m_app->ResetImageBasedLightingHistory();
                }
            }
            return true;
        }
        NoiseSettings candidate = m_ui.Noise;
        const NoiseSettings defaults;
        bool handled = true;
        if (path == "noise.pattern")
        {
            static constexpr std::array<
                std::pair<std::string_view, NoisePattern>, 3> Options = {{
                    { "spatial-white", NoisePattern::SpatialWhite },
                    { "spatial-blue", NoisePattern::SpatialBlue },
                    { "spatiotemporal-blue",
                        NoisePattern::SpatiotemporalBlue }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.pattern,
                defaults.pattern,
                Options,
                value,
                error);
        }
        else if (path == "noise.resolution")
        {
            static constexpr std::array<
                std::pair<std::string_view, NoiseResolution>, 4> Options = {{
                    { "64x64", NoiseResolution::Size64 },
                    { "128x128", NoiseResolution::Size128 },
                    { "256x256", NoiseResolution::Size256 },
                    { "512x512", NoiseResolution::Size512 }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.resolution,
                defaults.resolution,
                Options,
                value,
                error);
        }
        else if (path == "noise.animate-samples")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.animate,
                defaults.animate,
                value,
                error);
        }
        else
        {
            handled = false;
            error = "Internal Noise command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (!handled)
            return false;
        if (operation == CommandValueOperation::Get)
            return true;
        if (!IsValidNoiseSettings(candidate))
        {
            error = "The requested global noise configuration is invalid.";
            return false;
        }
        m_ui.Noise = candidate;
        m_app->ResetNoiseSamplingHistory(
            !m_ui.ScreenSpaceVisibility.noise.specifyNoise,
            false,
            !m_ui.RayTracedSkyVisibility.noise.specifyNoise,
            true);
        return true;
    }

auto UIRenderer::DispatchVisibilityCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        ScreenSpaceVisibilitySettings candidate =
            m_ui.ScreenSpaceVisibility;
        const ScreenSpaceVisibilitySettings defaults;
        bool handled = true;

        if (path == "visibility.enabled")
        {
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.enabled, defaults.enabled, value, error);
        }
        else if (path == "visibility.quality")
        {
            static constexpr std::array<
                std::pair<std::string_view, ScreenSpaceVisibilityQuality>, 5>
                Options = {{
                    { "low", ScreenSpaceVisibilityQuality::Low },
                    { "medium", ScreenSpaceVisibilityQuality::Medium },
                    { "high", ScreenSpaceVisibilityQuality::High },
                    { "ultra", ScreenSpaceVisibilityQuality::Ultra },
                    { "custom", ScreenSpaceVisibilityQuality::Custom }
                }};
            ScreenSpaceVisibilityQuality quality = candidate.quality;
            handled = ApplyCommandEnum(operation, arguments, path, quality,
                defaults.quality, Options, value, error);
            if (handled && operation != CommandValueOperation::Get)
            {
                if (quality == ScreenSpaceVisibilityQuality::Custom)
                    MarkScreenSpaceVisibilityQualityCustom(candidate);
                else
                    ApplyScreenSpaceVisibilityQualityPreset(candidate, quality);
            }
        }
        else if (path == "visibility.resolution")
        {
            static constexpr std::array<
                std::pair<std::string_view, VisibilityResolution>, 3>
                Options = {{
                    { "full", VisibilityResolution::Full },
                    { "half", VisibilityResolution::Half },
                    { "quarter", VisibilityResolution::Quarter }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.resolution, defaults.resolution,
                Options, value, error);
        }
        else if (path == "visibility.estimator")
        {
            static constexpr std::array<
                std::pair<std::string_view, VisibilityEstimator>, 3>
                Options = {{
                    { "projected-angle",
                        VisibilityEstimator::UniformProjectedAngle },
                    { "solid-angle",
                        VisibilityEstimator::UniformSolidAngle },
                    { "cosine-weighted",
                        VisibilityEstimator::CosineWeightedSolidAngle }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.estimator, defaults.estimator,
                Options, value, error);
        }
        else if (path == "visibility.specify-noise")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.noise.specifyNoise,
                defaults.noise.specifyNoise,
                value,
                error);
        }
        else if (path == "visibility.noise-pattern")
        {
            static constexpr std::array<
                std::pair<std::string_view, NoisePattern>, 3> Options = {{
                    { "spatial-white", NoisePattern::SpatialWhite },
                    { "spatial-blue", NoisePattern::SpatialBlue },
                    { "spatiotemporal-blue",
                        NoisePattern::SpatiotemporalBlue }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.noise.custom.pattern,
                defaults.noise.custom.pattern,
                Options,
                value,
                error);
        }
        else if (path == "visibility.noise-resolution")
        {
            static constexpr std::array<
                std::pair<std::string_view, NoiseResolution>, 4> Options = {{
                    { "64x64", NoiseResolution::Size64 },
                    { "128x128", NoiseResolution::Size128 },
                    { "256x256", NoiseResolution::Size256 },
                    { "512x512", NoiseResolution::Size512 }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.noise.custom.resolution,
                defaults.noise.custom.resolution,
                Options,
                value,
                error);
        }
        else if (path == "visibility.animate-samples")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.noise.custom.animate,
                defaults.noise.custom.animate,
                value,
                error);
        }
        else if (path == "visibility.samples")
        {
            handled = ApplyCommandUnsigned(operation, arguments, path,
                candidate.sampling.maximumSampleCount,
                defaults.sampling.maximumSampleCount, 1u, 64u, value, error);
        }
        else if (path == "visibility.radius")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.sampling.radius, defaults.sampling.radius,
                0.1f, 10.f, value, error);
        }
        else if (path == "visibility.thickness")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.sampling.thickness, defaults.sampling.thickness,
                0.01f, 2.f, value, error);
        }
        else if (path == "visibility.distribution")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.sampling.stepDistributionExponent,
                defaults.sampling.stepDistributionExponent,
                MinimumVisibilityStepDistributionExponent,
                MaximumVisibilityStepDistributionExponent,
                value,
                error);
        }
        else if (path == "visibility.ao.enabled")
        {
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.ambientOcclusion.enabled,
                defaults.ambientOcclusion.enabled, value, error);
        }
        else if (path == "visibility.ao.strength")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.ambientOcclusion.strength,
                defaults.ambientOcclusion.strength,
                MinimumVisibilityAmbientOcclusionStrength,
                MaximumVisibilityAmbientOcclusionStrength,
                value,
                error);
        }
        else if (path == "visibility.ao.output-hit-distance")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.ambientOcclusion.outputHitDistance,
                defaults.ambientOcclusion.outputHitDistance,
                value,
                error);
        }
        else if (path == "visibility.ao.precision")
        {
            using Precision = VisibilityScalarBufferPrecision;
            static constexpr std::array<
                std::pair<std::string_view, Precision>, 2> Options = {{
                    { "16-bit", Precision::Float16 },
                    { "32-bit", Precision::Float32 }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.bufferPrecision.ambient,
                defaults.bufferPrecision.ambient,
                Options, value, error);
        }
        else if (path == "visibility.gi.enabled")
        {
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.indirectDiffuse.enabled,
                defaults.indirectDiffuse.enabled, value, error);
        }
        else if (path == "visibility.gi.output-hit-distance")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.indirectDiffuse.outputHitDistance,
                defaults.indirectDiffuse.outputHitDistance,
                value,
                error);
        }
        else if (path == "visibility.gi.intensity")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.indirectDiffuse.intensity,
                defaults.indirectDiffuse.intensity,
                0.f, 16.f, value, error);
        }
        else if (path == "visibility.gi.precision")
        {
            using Precision = VisibilityVectorBufferPrecision;
            static constexpr std::array<
                std::pair<std::string_view, Precision>, 2> Options = {{
                    { "16-bit", Precision::Rgba16Float },
                    { "32-bit", Precision::Rgba32Float }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.bufferPrecision.indirect,
                defaults.bufferPrecision.indirect,
                Options, value, error);
        }
        else
        {
            handled = false;
            error = "Internal Visibility command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (!handled)
            return false;
        if (operation != CommandValueOperation::Get)
        {
            const bool noiseCommand =
                path == "visibility.specify-noise" ||
                path == "visibility.noise-pattern" ||
                path == "visibility.noise-resolution" ||
                path == "visibility.animate-samples";
            if (noiseCommand)
            {
                if (!IsValidNoiseSettings(candidate.noise.custom))
                {
                    error = "The requested visibility noise configuration is invalid.";
                    return false;
                }
                const NoiseSettings oldResolved = ResolveNoiseSettings(
                    m_ui.Noise,
                    m_ui.ScreenSpaceVisibility.noise);
                const NoiseSettings newResolved = ResolveNoiseSettings(
                    m_ui.Noise,
                    candidate.noise);
                m_ui.ScreenSpaceVisibility = candidate;
                if (oldResolved != newResolved)
                    m_app->ResetNoiseSamplingHistory(
                        true, false, false, false);
                return true;
            }
            if (path != "visibility.quality" &&
                path != "visibility.ao.output-hit-distance" &&
                path != "visibility.gi.output-hit-distance")
            {
                MarkScreenSpaceVisibilityQualityCustom(candidate);
                ReconcileScreenSpaceVisibilityQualityPreset(candidate);
            }
            m_ui.ScreenSpaceVisibility = candidate;
            m_app->ResetImageBasedLightingHistory();
        }
        return true;
    }

auto UIRenderer::DispatchAliasingCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        AntiAliasingSettings candidate = m_ui.AntiAliasing;
        const AntiAliasingSettings defaults;
        static constexpr std::array<
            std::pair<std::string_view, AntiAliasingQuality>, 4>
            QualityOptions = {{
                { "low", AntiAliasingQuality::Low },
                { "medium", AntiAliasingQuality::Medium },
                { "high", AntiAliasingQuality::High },
                { "ultra", AntiAliasingQuality::Ultra }
            }};
        bool handled = true;

        if (path == "anti-aliasing.taa.enabled")
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.temporal.enabled, defaults.temporal.enabled,
                value, error);
        else if (path == "anti-aliasing.taa.quality")
        {
            const bool temporalQualityCustom =
                candidate.temporal.nearestTexelDepth !=
                    defaults.temporal.nearestTexelDepth ||
                !(candidate.temporal.algorithmOverrides ==
                    defaults.temporal.algorithmOverrides);
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.quality, defaults.temporal.quality,
                QualityOptions, value, error,
                temporalQualityCustom);
            if (handled && operation != CommandValueOperation::Get)
            {
                candidate.temporal.nearestTexelDepth =
                    defaults.temporal.nearestTexelDepth;
                candidate.temporal.algorithmOverrides =
                    defaults.temporal.algorithmOverrides;
            }
        }
        else if (path == "anti-aliasing.taa.jitter-sequence")
        {
            using Sequence = TemporalAaJitterSequence;
            static constexpr std::array<
                std::pair<std::string_view, Sequence>, 6> Options = {{
                    { "rotated-grid-4", Sequence::RotatedGrid4 },
                    { "uniform-helix-4", Sequence::UniformHelix4 },
                    { "halton-8", Sequence::Halton23x8 },
                    { "halton-16", Sequence::Halton23x16 },
                    { "halton-32", Sequence::Halton23x32 },
                    { "sobol-32", Sequence::Sobol32 }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.jitterSequence,
                defaults.temporal.jitterSequence,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.previous-depth")
        {
            static constexpr std::array<std::pair<
                std::string_view, TemporalAaDepthValidation>, 2> Options = {{
                    { "nearest-texel",
                        TemporalAaDepthValidation::NearestTexel },
                    { "four-texel-footprint",
                        TemporalAaDepthValidation::FourTexelFootprint }
                }};
            TemporalAaDepthValidation validation =
                candidate.temporal.nearestTexelDepth
                ? TemporalAaDepthValidation::NearestTexel
                : TemporalAaDepthValidation::FourTexelFootprint;
            const TemporalAaDepthValidation defaultValidation =
                defaults.temporal.nearestTexelDepth
                ? TemporalAaDepthValidation::NearestTexel
                : TemporalAaDepthValidation::FourTexelFootprint;
            handled = ApplyCommandEnum(
                operation, arguments, path, validation,
                defaultValidation, Options, value, error);
            if (handled && operation != CommandValueOperation::Get)
            {
                candidate.temporal.nearestTexelDepth =
                    validation == TemporalAaDepthValidation::NearestTexel;
            }
        }
        else if (path == "anti-aliasing.taa.temporal-cost")
        {
            static constexpr std::array<
                std::pair<std::string_view, TemporalAaCostMode>, 3>
                Options = {{
                    { "full-quality", TemporalAaCostMode::FullQuality },
                    { "reduced", TemporalAaCostMode::Reduced },
                    { "minimum", TemporalAaCostMode::Minimum }
                }};
            const bool temporalCostCustom =
                !(candidate.temporal.behaviorOverrides ==
                    defaults.temporal.behaviorOverrides) ||
                m_ui.TemporalAaSharpenEnabled ||
                m_ui.TemporalAaSharpness !=
                    TemporalAaDefaultSharpness;
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.costMode,
                defaults.temporal.costMode, Options, value, error,
                temporalCostCustom);
            if (handled && operation != CommandValueOperation::Get)
            {
                candidate.temporal.behaviorOverrides =
                    defaults.temporal.behaviorOverrides;
                m_ui.TemporalAaSharpenEnabled = false;
                m_ui.TemporalAaSharpness = TemporalAaDefaultSharpness;
            }
        }
        else if (path == "anti-aliasing.taa.history.frames")
        {
            if (operation == CommandValueOperation::Set)
            {
                int64_t requested = 0;
                if (arguments.size() != 1u ||
                    !TryParseCommandInteger(arguments.front(), requested) ||
                    (requested != -1 && (requested < 1 || requested > 32)))
                {
                    error = std::string(path) +
                        " expects -1 or an integer from 1 through 32.";
                    return false;
                }
            }
            handled = ApplyCommandInteger(operation, arguments, path,
                candidate.temporal.algorithmOverrides.historyFrames,
                defaults.temporal.algorithmOverrides.historyFrames,
                -1, 32, value, error);
        }
        else if (path == "anti-aliasing.taa.history.strength")
        {
            if (operation == CommandValueOperation::Set)
            {
                float requested = 0.f;
                if (arguments.size() != 1u ||
                    !TryParseCommandFloat(arguments.front(), requested) ||
                    (requested != -1.f &&
                        (requested < 0.f || requested > 2.f)))
                {
                    error = std::string(path) +
                        " expects -1 or a number from 0 through 2.";
                    return false;
                }
            }
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.temporal.algorithmOverrides.historyStrength,
                defaults.temporal.algorithmOverrides.historyStrength,
                -1.f, 2.f, value, error);
        }
        else if (path == "anti-aliasing.taa.history.storage")
        {
            using Override = TemporalAaHistoryStorageOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "temporal-cost", Override::FromTemporalCost },
                    { "robust", Override::Robust },
                    { "compact", Override::Compact }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.historyStorage,
                defaults.temporal.behaviorOverrides.historyStorage,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.history.weight")
        {
            using Override = TemporalAaHistoryWeightPolicyOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "temporal-cost", Override::FromTemporalCost },
                    { "confidence-recurrence",
                        Override::ConfidenceRecurrence },
                    { "immediate-horizon", Override::ImmediateHorizon }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.historyWeight,
                defaults.temporal.behaviorOverrides.historyWeight,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.motion-trust")
        {
            using Override = TemporalAaMotionTrustOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "temporal-cost", Override::FromTemporalCost },
                    { "linear-speed", Override::LinearSpeed },
                    { "squared-speed", Override::SquaredSpeed }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.motionTrust,
                defaults.temporal.behaviorOverrides.motionTrust,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.rectification-clip")
        {
            using Override = TemporalAaRectificationClipOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "temporal-cost", Override::FromTemporalCost },
                    { "velocity-dilated", Override::VelocityDilatedLine },
                    { "tight-component", Override::TightComponent }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.rectificationClip,
                defaults.temporal.behaviorOverrides.rectificationClip,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.blend-domain")
        {
            using Override = TemporalAaBlendDomainOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "temporal-cost", Override::FromTemporalCost },
                    { "luminance-compressed",
                        Override::LuminanceCompressed },
                    { "linear-rgb", Override::LinearRgb }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.blendDomain,
                defaults.temporal.behaviorOverrides.blendDomain,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.preset-sharpening")
        {
            static constexpr std::array<
                std::pair<std::string_view, TemporalAaAutoToggle>, 3>
                Options = {{
                    { "auto", TemporalAaAutoToggle::Auto },
                    { "off", TemporalAaAutoToggle::Off },
                    { "on", TemporalAaAutoToggle::On }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.sharpening,
                defaults.temporal.behaviorOverrides.sharpening,
                Options, value, error);
        }
        else if (path == "anti-aliasing.fxaa.enabled")
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.fastApproximate.enabled,
                defaults.fastApproximate.enabled,
                value, error);
        else if (path == "anti-aliasing.fxaa.quality")
        {
            const bool fastApproximateQualityCustom =
                !MatchesFastApproximateAaQualityPreset(
                    candidate.fastApproximate);
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.fastApproximate.quality,
                defaults.fastApproximate.quality,
                QualityOptions, value, error,
                fastApproximateQualityCustom);
            if (handled && operation != CommandValueOperation::Get)
            {
                ApplyFastApproximateAaQualityPreset(
                    candidate.fastApproximate,
                    candidate.fastApproximate.quality);
            }
        }
        else if (path == "anti-aliasing.fxaa.edge-sharpness")
        {
            const FastApproximateAaQualityPreset preset =
                GetFastApproximateAaQualityPreset(
                    candidate.fastApproximate.quality);
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.fastApproximate.edgeSharpness,
                preset.edgeSharpness,
                FastApproximateAaMinimumEdgeSharpness,
                FastApproximateAaMaximumEdgeSharpness,
                value, error);
        }
        else if (path == "anti-aliasing.fxaa.edge-threshold")
        {
            const FastApproximateAaQualityPreset preset =
                GetFastApproximateAaQualityPreset(
                    candidate.fastApproximate.quality);
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.fastApproximate.edgeThreshold,
                preset.edgeThreshold,
                FastApproximateAaMinimumEdgeThreshold,
                FastApproximateAaMaximumEdgeThreshold,
                value, error);
        }
        else if (path == "anti-aliasing.fxaa.minimum-edge-threshold")
        {
            const FastApproximateAaQualityPreset preset =
                GetFastApproximateAaQualityPreset(
                    candidate.fastApproximate.quality);
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.fastApproximate.darkEdgeThreshold,
                preset.darkEdgeThreshold,
                FastApproximateAaMinimumDarkEdgeThreshold,
                FastApproximateAaMaximumDarkEdgeThreshold,
                value, error);
        }
        else if (path == "anti-aliasing.msaa.enabled")
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.msaa.enabled, defaults.msaa.enabled,
                value, error);
        else if (path == "anti-aliasing.msaa.quality")
        {
            const bool multisampleQualityCustom =
                !MatchesMultisampleQualityPreset(candidate.msaa);
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.msaa.quality, defaults.msaa.quality,
                QualityOptions, value, error,
                multisampleQualityCustom);
            if (handled && operation != CommandValueOperation::Get)
            {
                ApplyMultisampleQualityPreset(
                    candidate.msaa, candidate.msaa.quality);
            }
        }
        else if (path == "anti-aliasing.msaa.samples")
        {
            static constexpr std::array<
                std::pair<std::string_view, uint32_t>, 4> Options = {{
                    { "2x", 2u }, { "4x", 4u },
                    { "8x", 8u }, { "16x", 16u }
            }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.msaa.sampleCount,
                GetMultisampleQualitySampleCount(candidate.msaa.quality),
                Options, value, error);
        }
        else if (path == "anti-aliasing.sharpen.enabled")
        {
            handled = ApplyCommandBool(operation, arguments, path,
                m_ui.TemporalAaSharpenEnabled, false, value, error);
            return handled;
        }
        else if (path == "anti-aliasing.sharpen.strength")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                m_ui.TemporalAaSharpness, TemporalAaDefaultSharpness,
                0.f, 1.f, value, error);
            return handled;
        }
        else
        {
            handled = false;
            error = "Internal Anti-Aliasing command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (!handled)
            return false;
        if (operation != CommandValueOperation::Get)
        {
            candidate.fastApproximate.edgeSharpness =
                ClampFastApproximateAaEdgeSharpness(
                    candidate.fastApproximate.edgeSharpness);
            candidate.fastApproximate.edgeThreshold =
                ClampFastApproximateAaEdgeThreshold(
                    candidate.fastApproximate.edgeThreshold);
            candidate.fastApproximate.darkEdgeThreshold =
                ClampFastApproximateAaDarkEdgeThreshold(
                    candidate.fastApproximate.darkEdgeThreshold);
            candidate.msaa.sampleCount =
                SanitizeMsaaSampleCount(candidate.msaa.sampleCount);
            m_ui.AntiAliasing = candidate;
        }
        return true;
    }

auto UIRenderer::DispatchDebugCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        if (path == "debug.world.materials")
        {
            static constexpr std::array<
                std::pair<std::string_view, WhiteWorldMode>, 4> Options = {{
                    { "scene", WhiteWorldMode::Off },
                    { "white", WhiteWorldMode::On },
                    { "white-detail", WhiteWorldMode::PreserveDetail },
                    { "white-lighting", WhiteWorldMode::PreserveLighting }
                }};
            WhiteWorldMode mode = m_ui.WhiteWorld;
            const bool handled = ApplyCommandEnum(
                operation, arguments, path, mode, WhiteWorldMode::Off,
                Options, value, error);
            if (handled && operation != CommandValueOperation::Get)
                m_app->SetWhiteWorldMode(mode);
            return handled;
        }
        if (path == "debug.visibility.view")
        {
            static constexpr std::array<std::pair<
                std::string_view, VisibilityDebugView>, 4> Options = {{
                    { "final", VisibilityDebugView::FinalImage },
                    { "ambient-visibility",
                        VisibilityDebugView::AmbientVisibility },
                    { "traced-indirect",
                        VisibilityDebugView::TracedIndirect },
                    { "applied-indirect",
                        VisibilityDebugView::AppliedIndirect }
                }};
            const VisibilityDebugView previous =
                m_ui.ScreenSpaceVisibility.debugView;
            const bool handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                m_ui.ScreenSpaceVisibility.debugView,
                VisibilityDebugView::FinalImage,
                Options,
                value,
                error);
            if (handled &&
                operation != CommandValueOperation::Get &&
                previous != m_ui.ScreenSpaceVisibility.debugView)
            {
                m_app->ResetImageBasedLightingHistory();
            }
            return handled;
        }
        if (path == "debug.pbr.filter")
        {
            static constexpr std::array<std::pair<
                std::string_view, PbrLightingDebugView>, 13> Options = {{
                    { "final", PbrLightingDebugView::None },
                    { "surface-normals", PbrLightingDebugView::ShadingNormal },
                    { "geometry-normals", PbrLightingDebugView::GeometricNormal },
                    { "normal-difference", PbrLightingDebugView::NormalDifference },
                    { "diffuse-environment", PbrLightingDebugView::DiffuseEnvironment },
                    { "environment-direction", PbrLightingDebugView::EnvironmentDirection },
                    { "reflected-environment", PbrLightingDebugView::PrefilteredSpecularEnvironment },
                    { "brdf-response", PbrLightingDebugView::EnvironmentBrdf },
                    { "specular-environment", PbrLightingDebugView::FinalSpecularEnvironment },
                    { "all-environment-light", PbrLightingDebugView::CombinedEnvironment },
                    { "specular-visibility", PbrLightingDebugView::SpecularOcclusion },
                    { "environment-level", PbrLightingDebugView::EnvironmentMip },
                    { "sky-visibility", PbrLightingDebugView::SkyVisibility }
                }};
            PbrLightingDebugView candidate = m_ui.LightingDebugView;
            const bool handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate,
                PbrLightingDebugView::None,
                Options,
                value,
                error);
            if (handled && operation != CommandValueOperation::Get)
            {
                if (candidate == PbrLightingDebugView::SkyVisibility &&
                    (!m_ui.RayTracedSkyVisibility.enabled ||
                        !m_ui.Representation.allowRayTraversal ||
                        !m_app->SupportsRayTracedSkyVisibility()))
                {
                    error = "debug.pbr.filter=sky-visibility requires enabled, "
                        "supported Ray Traced Sky Visibility and ray traversal.";
                    return false;
                }
                m_ui.LightingDebugView = candidate;
                m_app->ResetImageBasedLightingHistory();
            }
            return handled;
        }
        error = "Internal Debug command binding is missing for '" +
            std::string(path) + "'.";
        return false;
    }

auto UIRenderer::DispatchSkyCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        if (path == "sky.environment")
        {
            const ImageBasedLightingSource current =
                m_ui.EnvironmentSource;
            ImageBasedLightingSource candidate = current;
            if (operation == CommandValueOperation::Set)
            {
                const std::string requested = JoinCommandArguments(arguments);
                bool found = false;
                for (uint32_t index = 0u;
                    index < uint32_t(ImageBasedLightingSource::Count);
                    ++index)
                {
                    const auto source =
                        ImageBasedLightingSource(index);
                    if (requested ==
                        GetImageBasedLightingSourceInfo(source).canonicalToken)
                    {
                        candidate = source;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    error =
                        "Unknown environment. Use '/list sky.environment'.";
                    return false;
                }
            }
            else if (operation == CommandValueOperation::Reset)
            {
                candidate =
                    ImageBasedLightingSource::Kloppenheim03Day;
            }
            else if (operation == CommandValueOperation::Toggle)
            {
                error = std::string(path) + " is not boolean.";
                return false;
            }
            if (operation != CommandValueOperation::Get &&
                candidate == current)
            {
                return RejectUnchangedCommandMutation(path, error);
            }
            value = GetImageBasedLightingSourceInfo(candidate).canonicalToken;
            if (operation != CommandValueOperation::Get)
            {
                m_ui.EnvironmentSource = candidate;
                m_ui.EnvironmentExposureStops =
                    GetImageBasedLightingSourceInfo(candidate)
                        .defaultExposureStops;
                m_app->ResetImageBasedLightingHistory();
            }
            return true;
        }

        if (path == "sky.visibility.enabled" ||
            path == "sky.visibility.diffuse-ibl" ||
            path == "sky.visibility.specular-ibl" ||
            path == "sky.visibility.output-hit-distance" ||
            path == "sky.visibility.samples-per-pixel" ||
            path == "sky.visibility.specify-noise" ||
            path == "sky.visibility.noise-pattern" ||
            path == "sky.visibility.noise-resolution" ||
            path == "sky.visibility.animate-samples" ||
            path == "sky.visibility.max-distance" ||
            path == "sky.visibility.ray-bias")
        {
            RayTracedSkyVisibilitySettings candidate =
                m_ui.RayTracedSkyVisibility;
            const RayTracedSkyVisibilitySettings factoryDefaults;
            bool handled = true;
            if (path == "sky.visibility.enabled")
            {
                handled = ApplyCommandBool(
                    operation,
                    arguments,
                    path,
                    candidate.enabled,
                    factoryDefaults.enabled,
                    value,
                    error);
            }
            else if (path == "sky.visibility.diffuse-ibl")
            {
                handled = ApplyCommandBool(
                    operation,
                    arguments,
                    path,
                    candidate.applyToDiffuseIbl,
                    factoryDefaults.applyToDiffuseIbl,
                    value,
                    error);
            }
            else if (path == "sky.visibility.specular-ibl")
            {
                handled = ApplyCommandBool(
                    operation,
                    arguments,
                    path,
                    candidate.applyToSpecularIbl,
                    factoryDefaults.applyToSpecularIbl,
                    value,
                    error);
            }
            else if (path == "sky.visibility.output-hit-distance")
            {
                handled = ApplyCommandBool(
                    operation,
                    arguments,
                    path,
                    candidate.outputHitDistance,
                    factoryDefaults.outputHitDistance,
                    value,
                    error);
            }
            else if (path == "sky.visibility.samples-per-pixel")
            {
                static constexpr std::array<
                    std::pair<std::string_view, int32_t>, 7> Options = {{
                        { "1", 0 },
                        { "2", 1 },
                        { "4", 2 },
                        { "8", 3 },
                        { "16", 4 },
                        { "32", 5 },
                        { "64", 6 }
                    }};
                handled = ApplyCommandEnum(
                    operation,
                    arguments,
                    path,
                    candidate.sampleRateLog2,
                    factoryDefaults.sampleRateLog2,
                    Options,
                    value,
                    error);
            }
            else if (path == "sky.visibility.specify-noise")
            {
                handled = ApplyCommandBool(
                    operation,
                    arguments,
                    path,
                    candidate.noise.specifyNoise,
                    factoryDefaults.noise.specifyNoise,
                    value,
                    error);
            }
            else if (path == "sky.visibility.noise-pattern")
            {
                static constexpr std::array<
                    std::pair<std::string_view, NoisePattern>, 3> Options = {{
                        { "spatial-white", NoisePattern::SpatialWhite },
                        { "spatial-blue", NoisePattern::SpatialBlue },
                        { "spatiotemporal-blue",
                            NoisePattern::SpatiotemporalBlue }
                    }};
                handled = ApplyCommandEnum(
                    operation,
                    arguments,
                    path,
                    candidate.noise.custom.pattern,
                    factoryDefaults.noise.custom.pattern,
                    Options,
                    value,
                    error);
            }
            else if (path == "sky.visibility.noise-resolution")
            {
                static constexpr std::array<
                    std::pair<std::string_view, NoiseResolution>, 4>
                    Options = {{
                        { "64x64", NoiseResolution::Size64 },
                        { "128x128", NoiseResolution::Size128 },
                        { "256x256", NoiseResolution::Size256 },
                        { "512x512", NoiseResolution::Size512 }
                    }};
                handled = ApplyCommandEnum(
                    operation,
                    arguments,
                    path,
                    candidate.noise.custom.resolution,
                    factoryDefaults.noise.custom.resolution,
                    Options,
                    value,
                    error);
            }
            else if (path == "sky.visibility.animate-samples")
            {
                handled = ApplyCommandBool(
                    operation,
                    arguments,
                    path,
                    candidate.noise.custom.animate,
                    factoryDefaults.noise.custom.animate,
                    value,
                    error);
            }
            else if (path == "sky.visibility.max-distance")
            {
                static constexpr std::array<std::pair<std::string_view,
                    RayVisibilityMaxDistance>, 6> Options = {{
                        { "max", RayVisibilityMaxDistance::Maximum },
                        { "32m", RayVisibilityMaxDistance::Meters32 },
                        { "16m", RayVisibilityMaxDistance::Meters16 },
                        { "8m", RayVisibilityMaxDistance::Meters8 },
                        { "4m", RayVisibilityMaxDistance::Meters4 },
                        { "2m", RayVisibilityMaxDistance::Meters2 }
                    }};
                handled = ApplyCommandEnum(
                    operation,
                    arguments,
                    path,
                    candidate.maxDistance,
                    factoryDefaults.maxDistance,
                    Options,
                    value,
                    error);
            }
            else
            {
                handled = ApplyCommandFloat(
                    operation,
                    arguments,
                    path,
                    candidate.rayBias,
                    factoryDefaults.rayBias,
                    0.f,
                    RayTracedSkyVisibilityMaximumRayBias,
                    value,
                    error);
            }

            if (!handled)
                return false;
            if (operation == CommandValueOperation::Get)
                return true;
            if (!IsRayTracedSkyVisibilityConfigurationSupported(candidate))
            {
                error =
                    "The requested ray traced sky visibility configuration is not supported.";
                return false;
            }
            if (operation != CommandValueOperation::Reset &&
                candidate.enabled &&
                !m_app->SupportsRayTracedSkyVisibility())
            {
                error =
                    "Ray-traced sky visibility requires DXR 1.1 support.";
                return false;
            }

            if (!IsValidNoiseSettings(candidate.noise.custom))
            {
                error =
                    "The requested sky visibility noise configuration is invalid.";
                return false;
            }

            const bool changed =
                candidate.enabled != m_ui.RayTracedSkyVisibility.enabled ||
                candidate.applyToDiffuseIbl !=
                    m_ui.RayTracedSkyVisibility.applyToDiffuseIbl ||
                candidate.applyToSpecularIbl !=
                    m_ui.RayTracedSkyVisibility.applyToSpecularIbl ||
                candidate.outputHitDistance !=
                    m_ui.RayTracedSkyVisibility.outputHitDistance ||
                candidate.sampleRateLog2 !=
                    m_ui.RayTracedSkyVisibility.sampleRateLog2 ||
                candidate.noise !=
                    m_ui.RayTracedSkyVisibility.noise ||
                candidate.maxDistance !=
                    m_ui.RayTracedSkyVisibility.maxDistance ||
                candidate.rayBias != m_ui.RayTracedSkyVisibility.rayBias;
            const NoiseSettings oldResolved = ResolveNoiseSettings(
                m_ui.Noise,
                m_ui.RayTracedSkyVisibility.noise);
            const NoiseSettings newResolved = ResolveNoiseSettings(
                m_ui.Noise,
                candidate.noise);
            const bool noiseCommand =
                path == "sky.visibility.specify-noise" ||
                path == "sky.visibility.noise-pattern" ||
                path == "sky.visibility.noise-resolution" ||
                path == "sky.visibility.animate-samples";
            m_ui.RayTracedSkyVisibility = candidate;
            if (changed)
            {
                if (noiseCommand && oldResolved != newResolved)
                    m_app->ResetNoiseSamplingHistory(
                        false, false, true, false);
                else if (!noiseCommand)
                    m_app->ResetImageBasedLightingHistory();
            }
            return true;
        }

        bool handled = true;
        bool resetVisibilityHistory = false;
        bool resetTemporalHistory = false;
        if (path == "sky.exposure")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                m_ui.EnvironmentExposureStops,
                GetImageBasedLightingSourceInfo(
                    m_ui.EnvironmentSource).defaultExposureStops,
                -8.f,
                8.f,
                value,
                error);
            resetVisibilityHistory = true;
        }
        else if (path == "sky.auto-exposure.enabled")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.AutoExposure.enabled,
                false,
                value,
                error);
        }
        else if (path == "sky.auto-exposure.exposure-compensation")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                m_ui.AutoExposure.exposureCompensationEV,
                AutoExposureDefaultCompensationEV,
                AutoExposureMinimumCompensationEV,
                AutoExposureMaximumCompensationEV,
                value,
                error);
        }
        else if (path == "sky.auto-exposure.maximum-brightening")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                m_ui.AutoExposure.maximumBrighteningEV,
                AutoExposureDefaultMaximumBrighteningEV,
                AutoExposureMinimumMovementEV,
                AutoExposureMaximumMovementEV,
                value,
                error);
        }
        else if (path == "sky.auto-exposure.maximum-darkening")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                m_ui.AutoExposure.maximumDarkeningEV,
                AutoExposureDefaultMaximumDarkeningEV,
                AutoExposureMinimumMovementEV,
                AutoExposureMaximumMovementEV,
                value,
                error);
        }
        else if (path == "sky.auto-exposure.adjustment-period")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                m_ui.AutoExposure.adjustmentPeriodSeconds,
                AutoExposureDefaultAdjustmentPeriodSeconds,
                AutoExposureMinimumAdjustmentPeriodSeconds,
                AutoExposureMaximumAdjustmentPeriodSeconds,
                value,
                error);
        }
        else if (path == "sky.diffuse-ibl")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.EnableDiffuseIbl,
                true,
                value,
                error);
            resetVisibilityHistory = true;
        }
        else if (path == "sky.diffuse-ibl-strength")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                m_ui.DiffuseIblStrength,
                1.f,
                0.f,
                4.f,
                value,
                error);
            resetVisibilityHistory = true;
        }
        else if (path == "sky.specular-ibl")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.EnableSpecularIbl,
                true,
                value,
                error);
            resetTemporalHistory = true;
        }
        else if (path == "sky.specular-ibl-strength")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                m_ui.SpecularIblStrength,
                1.f,
                0.f,
                4.f,
                value,
                error);
            resetTemporalHistory = true;
        }
        else if (path == "sky.environment-background")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.ShowEnvironmentBackground,
                true,
                value,
                error);
            resetTemporalHistory = true;
        }
        else if (path == "sky.ambient-fill.enabled")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.EnableAmbientFill,
                true,
                value,
                error);
            resetVisibilityHistory = true;
        }
        else
        {
            handled = false;
            error = "Internal Sky command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (handled &&
            operation != CommandValueOperation::Get)
        {
            if (resetVisibilityHistory)
                m_app->ResetImageBasedLightingHistory();
            else if (resetTemporalHistory)
                m_app->ResetImageBasedLightingHistory();
        }
        return handled;
    }

auto UIRenderer::DispatchDenoisingCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        DenoisingSignalSettings* current = nullptr;
        DenoisingEffect effect = DenoisingEffect::AmbientOcclusion;
        std::string_view prefix;

        if (path.rfind("denoising.ao.", 0u) == 0u)
        {
            current = &m_ui.Denoising.ambientOcclusion;
            effect = DenoisingEffect::AmbientOcclusion;
            prefix = "denoising.ao.";
        }
        else if (path.rfind("denoising.gi.", 0u) == 0u)
        {
            current = &m_ui.Denoising.diffuseGi;
            effect = DenoisingEffect::DiffuseGi;
            prefix = "denoising.gi.";
        }
        else if (path.rfind("denoising.shadows.", 0u) == 0u)
        {
            current = &m_ui.Denoising.shadows;
            effect = DenoisingEffect::Shadows;
            prefix = "denoising.shadows.";
        }
        else if (path.rfind("denoising.sky.", 0u) == 0u)
        {
            current = &m_ui.Denoising.skyVisibility;
            effect = DenoisingEffect::SkyVisibility;
            prefix = "denoising.sky.";
        }
        else
        {
            error = "Internal Denoising command binding is missing for '" +
                std::string(path) + "'.";
            return false;
        }

        DenoisingSignalSettings candidate = *current;
        const DenoisingSignalSettings defaults;
        const std::string_view property = path.substr(prefix.size());
        bool handled = false;

        if (property == "method")
        {
            if (effect == DenoisingEffect::AmbientOcclusion)
            {
                static constexpr std::array<std::pair<std::string_view,
                    DenoisingMethodChoice>, 4> Options = {{
                    { "raw", DenoisingMethodChoice::None },
                    { "joint-bilateral",
                        DenoisingMethodChoice::JointBilateral },
                    { "gaussian-bilateral",
                        DenoisingMethodChoice::GaussianBilateral },
                    { "reblur", DenoisingMethodChoice::Reblur }
                }};
                handled = ApplyCommandEnum(
                    operation, arguments, path,
                    candidate.method, defaults.method,
                    Options, value, error);
            }
            else if (effect == DenoisingEffect::Shadows)
            {
                static constexpr std::array<std::pair<std::string_view,
                    DenoisingMethodChoice>, 4> Options = {{
                    { "raw", DenoisingMethodChoice::None },
                    { "joint-bilateral",
                        DenoisingMethodChoice::JointBilateral },
                    { "gaussian-bilateral",
                        DenoisingMethodChoice::GaussianBilateral },
                    { "sigma", DenoisingMethodChoice::Sigma }
                }};
                handled = ApplyCommandEnum(
                    operation, arguments, path,
                    candidate.method, defaults.method,
                    Options, value, error);
            }
            else
            {
                static constexpr std::array<std::pair<std::string_view,
                    DenoisingMethodChoice>, 5> Options = {{
                    { "raw", DenoisingMethodChoice::None },
                    { "joint-bilateral",
                        DenoisingMethodChoice::JointBilateral },
                    { "gaussian-bilateral",
                        DenoisingMethodChoice::GaussianBilateral },
                    { "reblur", DenoisingMethodChoice::Reblur },
                    { "relax", DenoisingMethodChoice::Relax }
                }};
                handled = ApplyCommandEnum(
                    operation, arguments, path,
                    candidate.method, defaults.method,
                    Options, value, error);
            }
        }
        else if (property == "quality")
        {
            static constexpr std::array<std::pair<std::string_view,
                DenoisingQuality>, 4> Options = {{
                { "performance", DenoisingQuality::Performance },
                { "balanced", DenoisingQuality::Balanced },
                { "quality", DenoisingQuality::Quality },
                { "ultra", DenoisingQuality::Ultra }
            }};
            handled = ApplyCommandEnum(
                operation, arguments, path,
                candidate.quality, defaults.quality,
                Options, value, error);
        }
        else if (property == "resolution")
        {
            static constexpr std::array<std::pair<std::string_view,
                DenoisingResolution>, 3> Options = {{
                { "quarter", DenoisingResolution::Quarter },
                { "half", DenoisingResolution::Half },
                { "full", DenoisingResolution::Full }
            }};
            handled = ApplyCommandEnum(
                operation, arguments, path,
                candidate.resolution, defaults.resolution,
                Options, value, error);
        }
        else if (property == "history")
        {
            handled = ApplyCommandUnsigned(
                operation, arguments, path,
                candidate.historyLength, defaults.historyLength,
                1u, 32u, value, error);
        }
        else if (property == "disocclusion")
        {
            handled = ApplyCommandFloat(
                operation, arguments, path,
                candidate.disocclusionThreshold,
                defaults.disocclusionThreshold,
                0.001f, 0.1f, value, error);
        }
        else if (property == "anti-lag")
        {
            handled = ApplyCommandFloat(
                operation, arguments, path,
                candidate.antiLagStrength,
                defaults.antiLagStrength,
                0.f, 1.f, value, error);
        }
        else if (property == "radius")
        {
            handled = ApplyCommandFloat(
                operation, arguments, path,
                candidate.spatialRadius,
                defaults.spatialRadius,
                1.f, 8.f, value, error);
        }

        if (!handled)
        {
            if (error.empty())
            {
                error = "Internal Denoising command binding is missing for '" +
                    std::string(path) + "'.";
            }
            return false;
        }
        if (operation == CommandValueOperation::Get)
            return true;

        candidate = SanitizeDenoisingSettings(effect, candidate);
        *current = candidate;
        m_app->ResetImageBasedLightingHistory();
        return true;
    }

auto UIRenderer::GetDefaultCommandLight() const -> std::shared_ptr<Light> {
        const auto& lights = m_app->GetEditableLights();
        std::shared_ptr<Light> selected =
            m_app->GetPrimaryDirectionalLight();
        if (!selected ||
            std::find(lights.begin(), lights.end(), selected) ==
                lights.end())
        {
            selected = lights.empty() ? nullptr : lights.front();
        }
        return selected;
    }

auto UIRenderer::EnsureCommandSelectedLight() -> std::shared_ptr<Light> {
        const auto& lights = m_app->GetEditableLights();
        if (lights.empty())
        {
            m_SelectedLight.reset();
            return nullptr;
        }
        if (std::find(
                lights.begin(), lights.end(), m_SelectedLight) ==
            lights.end())
        {
            m_SelectedLight = GetDefaultCommandLight();
        }
        return m_SelectedLight;
    }

auto UIRenderer::GetCommandLightDefaults(
        const std::shared_ptr<Light>& light) -> const LightDefaultState& {
        const auto& lights = m_app->GetEditableLights();
        const auto selected = std::find(
            lights.begin(), lights.end(), light);
        const size_t index =
            static_cast<size_t>(std::distance(lights.begin(), selected));
        const std::string key =
            m_app->GetCurrentSceneName() + "\n" +
            std::to_string(index) + "\n" +
            light->GetName();
        const auto capture = [](const Light& source)
        {
            LightDefaultState result;
            result.type = source.GetLightType();
            result.direction = source.GetDirection();
            result.color = source.color;
            switch (result.type)
            {
            case UVSR_LIGHT_TYPE_DIRECTIONAL:
            {
                const auto& directional =
                    static_cast<const DirectionalLight&>(source);
                result.irradiance = directional.irradiance;
                result.angularSize = directional.angularSize;
                break;
            }
            case UVSR_LIGHT_TYPE_POINT:
            {
                const auto& point =
                    static_cast<const PointLight&>(source);
                result.radius = point.radius;
                result.intensity = point.intensity;
                break;
            }
            case UVSR_LIGHT_TYPE_SPOT:
            {
                const auto& spot =
                    static_cast<const SpotLight&>(source);
                result.radius = spot.radius;
                result.intensity = spot.intensity;
                result.innerAngle = spot.innerAngle;
                result.outerAngle = spot.outerAngle;
                break;
            }
            default:
                break;
            }
            return result;
        };
        return m_LightDefaults.try_emplace(
            key,
            capture(*light)).first->second;
    }

auto UIRenderer::GetCommandLightAngles(
        const double3& storedDirection,
        bool directional) -> std::pair<float, float> {
        double3 direction = normalize(storedDirection);
        if (directional)
            direction = -direction;
        const float azimuth = degrees(float(
            std::atan2(direction.z, direction.x)));
        const float elevation = degrees(float(std::asin(
            std::clamp(direction.y, -1.0, 1.0))));
        return { azimuth, elevation };
    }

auto UIRenderer::MakeCommandLightDirection(
        float azimuthDegrees,
        float elevationDegrees,
        bool directional) -> double3 {
        const double azimuth = radians(double(azimuthDegrees));
        const double elevation = radians(double(elevationDegrees));
        const double horizontal = std::cos(elevation);
        double3 direction(
            std::cos(azimuth) * horizontal,
            std::sin(elevation),
            std::sin(azimuth) * horizontal);
        if (directional)
            direction = -direction;
        return normalize(direction);
    }

auto UIRenderer::DispatchFlashlightCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        if (path == "light.selected.flashlight.enabled")
        {
            const bool handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.FlashlightEnabled,
                DefaultFlashlightEnabled,
                value,
                error);
            if (handled && operation != CommandValueOperation::Get)
                m_app->ResetImageBasedLightingHistory();
            return handled;
        }

        FlashlightSettings candidate = m_ui.Flashlight;
        const FlashlightSettings defaults = DefaultFlashlightSettings;
        bool handled = true;
        if (path == "light.selected.color")
        {
            float3 color(
                candidate.colorLinearRed,
                candidate.colorLinearGreen,
                candidate.colorLinearBlue);
            const float3 defaultColor(
                defaults.colorLinearRed,
                defaults.colorLinearGreen,
                defaults.colorLinearBlue);
            handled = ApplyCommandFloat3(
                operation,
                arguments,
                path,
                color,
                defaultColor,
                0.f,
                1.f,
                value,
                error);
            candidate.colorLinearRed = color.x;
            candidate.colorLinearGreen = color.y;
            candidate.colorLinearBlue = color.z;
        }
        else if (path == "light.selected.flashlight.cast-shadows")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.castShadows,
                defaults.castShadows,
                value,
                error);
        }
        else if (path ==
            "light.selected.flashlight.output-hit-distance")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.outputHitDistance,
                defaults.outputHitDistance,
                value,
                error);
        }
        else if (path == "light.selected.flashlight.realistic")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.realisticLens,
                defaults.realisticLens,
                value,
                error);
        }
        else if (path ==
            "light.selected.flashlight.stationary-when-idle")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.stationaryWhenIdle,
                defaults.stationaryWhenIdle,
                value,
                error);
        }
        else
        {
            handled = false;
            for (const FlashlightFloatCommandBinding& binding :
                FlashlightFloatCommandBindings)
            {
                if (path != binding.path)
                    continue;
                handled = ApplyCommandFloat(
                    operation,
                    arguments,
                    path,
                    candidate.*(binding.member),
                    defaults.*(binding.member),
                    binding.minimum,
                    binding.maximum,
                    value,
                    error);
                break;
            }
        }

        if (!handled)
        {
            if (error.empty())
            {
                error =
                    "Internal flashlight command binding is missing for '" +
                    std::string(path) + "'.";
            }
            return false;
        }
        if (operation != CommandValueOperation::Get)
        {
            m_ui.Flashlight = SanitizeFlashlightSettings(candidate);
            m_app->ResetImageBasedLightingHistory();
        }
        return true;
    }

auto UIRenderer::DispatchLightCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        const std::shared_ptr<Scene> scene = m_app->GetScene();
        if (!scene || !scene->GetSceneGraph())
        {
            error = "No loaded scene provides light controls.";
            return false;
        }
        const auto& lights = m_app->GetEditableLights();
        if (path == "light.selected")
        {
            std::shared_ptr<Light> currentSelected = m_SelectedLight;
            if (std::find(
                    lights.begin(), lights.end(), currentSelected) ==
                lights.end())
            {
                currentSelected = GetDefaultCommandLight();
            }
            std::shared_ptr<Light> selected = currentSelected;
            const auto selectedIndex = [&lights](
                const std::shared_ptr<Light>& light)
            {
                const auto found = std::find(
                    lights.begin(), lights.end(), light);
                return found == lights.end()
                    ? std::optional<size_t>{}
                    : std::optional<size_t>{
                        static_cast<size_t>(
                            std::distance(lights.begin(), found)) };
            };
            if (operation == CommandValueOperation::Get)
            {
                if (!selected)
                {
                    error = "The current scene has no lights.";
                    return false;
                }
                const std::optional<size_t> index = selectedIndex(selected);
                if (!index)
                {
                    error = "The selected light is not owned by this scene.";
                    return false;
                }
                value = FormatSettingsSnapshotLightToken(
                    *index, selected->GetName());
                return true;
            }
            if (operation == CommandValueOperation::Toggle)
            {
                error = std::string(path) + " is not boolean.";
                return false;
            }
            if (operation == CommandValueOperation::Reset)
            {
                selected = GetDefaultCommandLight();
            }
            else
            {
                const std::string requested =
                    JoinCommandArguments(arguments);
                std::vector<SettingsSnapshotLightOption> options;
                options.reserve(lights.size());
                for (size_t index = 0u; index < lights.size(); ++index)
                {
                    const auto& light = lights[index];
                    if (light)
                        options.push_back({ index, light->GetName() });
                }
                size_t requestedIndex = 0u;
                if (!ResolveSettingsSnapshotLightToken(
                        requested,
                        options,
                        requestedIndex,
                        value,
                        error) ||
                    requestedIndex >= lights.size())
                    return false;
                selected = lights[requestedIndex];
            }
            if (!selected)
            {
                error = "Unknown light. Use '/list light.selected'.";
                return false;
            }
            if (selected == currentSelected)
                return RejectUnchangedCommandMutation(path, error);
            m_SelectedLight = selected;
            GetCommandLightDefaults(selected);
            const std::optional<size_t> index = selectedIndex(selected);
            if (!index)
            {
                error = "The selected light is not owned by this scene.";
                return false;
            }
            value = FormatSettingsSnapshotLightToken(
                *index, selected->GetName());
            return true;
        }

        const std::shared_ptr<Light> selected =
            EnsureCommandSelectedLight();
        if (!selected)
        {
            error = "The current scene has no selected light.";
            return false;
        }
        const bool flashlightSelected = m_app->IsFlashlight(selected);
        const bool flashlightPath =
            path.find("light.selected.flashlight.") == 0u;
        if (flashlightSelected)
        {
            if (path == "light.selected.color" || flashlightPath)
            {
                return DispatchFlashlightCommandValue(
                    definition,
                    operation,
                    arguments,
                    value,
                    error);
            }
            error = std::string(path) +
                " is not an editable generic flashlight property.";
            return false;
        }
        if (flashlightPath)
        {
            error = std::string(path) +
                " requires selecting flashlight_1 first.";
            return false;
        }
        const LightDefaultState& defaults =
            GetCommandLightDefaults(selected);
        const int type = selected->GetLightType();
        const bool directional = type == UVSR_LIGHT_TYPE_DIRECTIONAL;
        const bool spot = type == UVSR_LIGHT_TYPE_SPOT;
        const bool pointOrSpot =
            type == UVSR_LIGHT_TYPE_POINT || spot;

        if (path == "light.selected.azimuth" ||
            path == "light.selected.elevation")
        {
            if (!directional && !spot)
            {
                error =
                    std::string(path) +
                    " requires a directional or spot light.";
                return false;
            }
            auto [azimuth, elevation] = GetCommandLightAngles(
                selected->GetDirection(), directional);
            const auto [defaultAzimuth, defaultElevation] =
                GetCommandLightAngles(
                    defaults.direction, directional);
            float& current = path == "light.selected.azimuth"
                ? azimuth
                : elevation;
            const float defaultValue =
                path == "light.selected.azimuth"
                    ? defaultAzimuth
                    : defaultElevation;
            if (!ApplyCommandFloat(
                    operation,
                    arguments,
                    path,
                    current,
                    defaultValue,
                    path == "light.selected.azimuth" ? -180.f : -90.f,
                    path == "light.selected.azimuth" ? 180.f : 90.f,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
            {
                selected->SetDirection(MakeCommandLightDirection(
                    azimuth,
                    elevation,
                    directional));
                if (directional)
                    m_app->ResetImageBasedLightingHistory();
            }
            return true;
        }
        if (path == "light.selected.color")
        {
            return ApplyCommandFloat3(
                operation,
                arguments,
                path,
                selected->color,
                defaults.color,
                0.f,
                1.f,
                value,
                error);
        }

        if (path == "light.selected.irradiance")
        {
            if (!directional)
            {
                error =
                    "light.selected.irradiance requires a directional light.";
                return false;
            }
            auto& light = static_cast<DirectionalLight&>(*selected);
            return ApplyCommandFloat(
                operation,
                arguments,
                path,
                light.irradiance,
                defaults.irradiance,
                0.f,
                100.f,
                value,
                error);
        }
        if (path == "light.selected.angular-size")
        {
            if (!directional)
            {
                error =
                    "light.selected.angular-size requires a directional "
                    "light.";
                return false;
            }
            auto& light = static_cast<DirectionalLight&>(*selected);
            const bool handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                light.angularSize,
                defaults.angularSize,
                0.f,
                20.f,
                value,
                error);
            if (handled && operation != CommandValueOperation::Get)
                m_app->ResetImageBasedLightingHistory();
            return handled;
        }
        if (path == "light.selected.radius" ||
            path == "light.selected.intensity")
        {
            if (!pointOrSpot)
            {
                error =
                    std::string(path) +
                    " requires a point or spot light.";
                return false;
            }
            float* current = nullptr;
            const float* defaultValue = nullptr;
            if (type == UVSR_LIGHT_TYPE_POINT)
            {
                auto& light = static_cast<PointLight&>(*selected);
                current = path == "light.selected.radius"
                    ? &light.radius
                    : &light.intensity;
            }
            else
            {
                auto& light = static_cast<SpotLight&>(*selected);
                current = path == "light.selected.radius"
                    ? &light.radius
                    : &light.intensity;
            }
            defaultValue = path == "light.selected.radius"
                ? &defaults.radius
                : &defaults.intensity;
            return ApplyCommandFloat(
                operation,
                arguments,
                path,
                *current,
                *defaultValue,
                path == "light.selected.radius" ? 0.01f : 0.f,
                path == "light.selected.radius" ? 1.f : 100.f,
                value,
                error);
        }
        if (path == "light.selected.inner-angle" ||
            path == "light.selected.outer-angle")
        {
            if (!spot)
            {
                error =
                    std::string(path) + " requires a spot light.";
                return false;
            }
            auto& light = static_cast<SpotLight&>(*selected);
            float candidate = path == "light.selected.inner-angle"
                ? light.innerAngle
                : light.outerAngle;
            if (!ApplyCommandFloat(
                    operation,
                    arguments,
                    path,
                    candidate,
                    path == "light.selected.inner-angle"
                        ? defaults.innerAngle
                        : defaults.outerAngle,
                    0.f,
                    180.f,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
            {
                if (path == "light.selected.inner-angle" &&
                    candidate > light.outerAngle)
                {
                    error =
                        "The inner spot angle cannot exceed the outer angle.";
                    return false;
                }
                if (path == "light.selected.outer-angle" &&
                    candidate < light.innerAngle)
                {
                    error =
                        "The outer spot angle cannot be below the inner angle.";
                    return false;
                }
                if (path == "light.selected.inner-angle")
                    light.innerAngle = candidate;
                else
                    light.outerAngle = candidate;
            }
            return true;
        }

        error = "Internal Light command binding is missing for '" +
            std::string(path) + "'.";
        return false;
    }

auto UIRenderer::DispatchDirectionalShadowCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        DirectionalShadowSettings candidate = m_ui.DirectionalShadows;
        const DirectionalShadowSettings defaults;
        bool handled = true;

        if (path == "shadows.ray-traced.enabled")
        {
            handled = ApplyCommandBool(
                operation, arguments, path, candidate.enabled,
                defaults.enabled, value, error);
        }
        else if (path == "shadows.ray-traced.max-distance")
        {
            static constexpr std::array<std::pair<std::string_view,
                RayVisibilityMaxDistance>, 6> Options = {{
                    { "max", RayVisibilityMaxDistance::Maximum },
                    { "32m", RayVisibilityMaxDistance::Meters32 },
                    { "16m", RayVisibilityMaxDistance::Meters16 },
                    { "8m", RayVisibilityMaxDistance::Meters8 },
                    { "4m", RayVisibilityMaxDistance::Meters4 },
                    { "2m", RayVisibilityMaxDistance::Meters2 }
                }};
            handled = ApplyCommandEnum(
                operation, arguments, path, candidate.maxDistance,
                defaults.maxDistance, Options, value, error);
        }
        else if (path == "shadows.ray-traced.ray-bias")
        {
            handled = ApplyCommandFloat(
                operation, arguments, path, candidate.rayBias,
                defaults.rayBias, 0.f,
                DirectionalShadowMaximumRayBias, value, error);
        }
        else
        {
            handled = false;
            error = "Internal Directional Shadows command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (!handled || operation == CommandValueOperation::Get)
            return handled;
        if (!IsDirectionalShadowSettingsValid(candidate))
        {
            error = "The requested direct directional-shadow configuration is invalid.";
            return false;
        }
        if (operation != CommandValueOperation::Reset && candidate.enabled &&
            !m_app->HasPrimaryDirectionalLight())
        {
            error = "Directional ray shadows require a primary directional light.";
            return false;
        }
        if (operation != CommandValueOperation::Reset && candidate.enabled &&
            !m_app->SupportsDirectionalRayVisibility())
        {
            error = "Directional ray shadows require DXR 1.1 support.";
            return false;
        }

        const bool changed = candidate.enabled != m_ui.DirectionalShadows.enabled ||
            candidate.maxDistance != m_ui.DirectionalShadows.maxDistance ||
            candidate.rayBias != m_ui.DirectionalShadows.rayBias;
        m_ui.DirectionalShadows = candidate;
        if (changed)
            m_app->ResetImageBasedLightingHistory();
        return true;
    }

auto UIRenderer::IsCommandMaterialTransmissive(MaterialDomain domain) -> bool {
        return domain == MaterialDomain::Transmissive ||
            domain == MaterialDomain::TransmissiveAlphaTested ||
            domain == MaterialDomain::TransmissiveAlphaBlended;
    }

auto UIRenderer::IsCommandMaterialAlphaTested(MaterialDomain domain) -> bool {
        return domain == MaterialDomain::AlphaTested ||
            domain == MaterialDomain::TransmissiveAlphaTested;
    }

auto UIRenderer::IsCommandMaterialAlphaBlended(MaterialDomain domain) -> bool {
        return domain == MaterialDomain::AlphaBlended ||
            domain == MaterialDomain::TransmissiveAlphaBlended;
    }

auto UIRenderer::DispatchMaterialCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        const std::string_view path = definition.name;
        const std::shared_ptr<Scene> scene = m_app->GetScene();
        if (!scene || !scene->GetSceneGraph())
        {
            error = "No loaded scene provides material controls.";
            return false;
        }
        const auto& materials =
            scene->GetSceneGraph()->GetMaterials();

        if (path == "material.selected")
        {
            if (operation == CommandValueOperation::Get)
            {
                if (!m_ui.SelectedMaterial)
                {
                    value = FormatSettingsSnapshotMaterialToken(true);
                    return true;
                }
                if (m_ui.SelectedMaterial->materialID < 0)
                {
                    error =
                        "The selected material has an invalid negative id.";
                    return false;
                }
                value = FormatSettingsSnapshotMaterialToken(
                    false,
                    static_cast<uint32_t>(
                        m_ui.SelectedMaterial->materialID));
                return true;
            }
            if (operation != CommandValueOperation::Set)
            {
                error =
                    "material.selected supports get and set only.";
                return false;
            }

            const std::string requested =
                JoinCommandArguments(arguments);
            if (requested.empty())
            {
                error =
                    "material.selected expects a material id or unique "
                    "name.";
                return false;
            }
            std::vector<SettingsSnapshotMaterialOption> options;
            options.reserve(materials.size());
            for (const std::shared_ptr<Material>& material : materials)
            {
                if (!material)
                    continue;
                if (material->materialID < 0)
                {
                    error = "The scene contains an invalid negative material id.";
                    return false;
                }
                options.push_back({
                    static_cast<uint32_t>(material->materialID),
                    material->name
                });
            }
            bool none = false;
            uint32_t requestedId = 0u;
            if (!ResolveSettingsSnapshotMaterialToken(
                    requested,
                    options,
                    none,
                    requestedId,
                    value,
                    error))
                return false;
            if (none)
            {
                if (!m_ui.SelectedMaterial)
                    return RejectUnchangedCommandMutation(path, error);
                m_ui.SelectedMaterial.reset();
                m_ui.SelectedNode.reset();
                return true;
            }
            std::shared_ptr<Material> match;
            for (const std::shared_ptr<Material>& material : materials)
            {
                if (material && material->materialID >= 0 &&
                    static_cast<uint32_t>(material->materialID) ==
                        requestedId)
                {
                    match = material;
                    break;
                }
            }
            if (!match)
            {
                error = "Resolved material is not owned by this scene.";
                return false;
            }
            if (match == m_ui.SelectedMaterial)
                return RejectUnchangedCommandMutation(path, error);
            m_ui.SelectedMaterial = match;
            value = FormatSettingsSnapshotMaterialToken(false, requestedId);
            return true;
        }

        const std::shared_ptr<Material> material =
            m_ui.SelectedMaterial;
        bool materialStillOwnedByScene = false;
        if (material)
        {
            for (const std::shared_ptr<Material>& sceneMaterial : materials)
            {
                if (sceneMaterial == material)
                {
                    materialStillOwnedByScene = true;
                    break;
                }
            }
        }
        if (!materialStillOwnedByScene)
        {
            error =
                "No scene material is selected. Use "
                "'/list material.selected' and "
                "'/set material.selected <id-or-name>'.";
            return false;
        }

        Material candidate = *material;
        bool handled = true;
        if (path == "material.selected.domain")
        {
            static constexpr std::array<
                std::pair<std::string_view, MaterialDomain>, 6>
                Options = {{
                    { "opaque", MaterialDomain::Opaque },
                    { "alpha-tested", MaterialDomain::AlphaTested },
                    { "alpha-blended", MaterialDomain::AlphaBlended },
                    { "transmissive", MaterialDomain::Transmissive },
                    {
                        "transmissive-alpha-tested",
                        MaterialDomain::TransmissiveAlphaTested
                    },
                    {
                        "transmissive-alpha-blended",
                        MaterialDomain::TransmissiveAlphaBlended
                    }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.domain,
                candidate.domain,
                Options,
                value,
                error);
        }
        else if (path == "material.selected.double-sided")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.doubleSided,
                candidate.doubleSided,
                value,
                error);
        }
        else if (path ==
            "material.selected.base-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.baseOrDiffuseTexture)
            {
                error =
                    "The selected material has no base or diffuse "
                    "texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableBaseOrDiffuseTexture,
                candidate.enableBaseOrDiffuseTexture,
                value,
                error);
        }
        else if (path == "material.selected.base-color")
        {
            handled = ApplyCommandFloat3(
                operation,
                arguments,
                path,
                candidate.baseOrDiffuseColor,
                candidate.baseOrDiffuseColor,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.metal-specular-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.metalRoughOrSpecularTexture)
            {
                error =
                    "The selected material has no metal-rough or "
                    "specular texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableMetalRoughOrSpecularTexture,
                candidate.enableMetalRoughOrSpecularTexture,
                value,
                error);
        }
        else if (path == "material.selected.specular-color")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.useSpecularGlossModel)
            {
                error =
                    "Specular color requires a specular-gloss material.";
                return false;
            }
            handled = ApplyCommandFloat3(
                operation,
                arguments,
                path,
                candidate.specularColor,
                candidate.specularColor,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path == "material.selected.metalness")
        {
            if (operation != CommandValueOperation::Get &&
                candidate.useSpecularGlossModel)
            {
                error =
                    "Metalness requires a metal-rough material.";
                return false;
            }
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.metalness,
                candidate.metalness,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path == "material.selected.roughness")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.roughness,
                candidate.roughness,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path == "material.selected.opacity")
        {
            if (operation != CommandValueOperation::Get &&
                !IsCommandMaterialAlphaBlended(candidate.domain))
            {
                error =
                    "Opacity is available only for alpha-blended "
                    "materials.";
                return false;
            }
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.opacity,
                candidate.opacity,
                0.f,
                candidate.baseOrDiffuseTexture ? 2.f : 1.f,
                value,
                error);
        }
        else if (path == "material.selected.alpha-cutoff")
        {
            if (operation != CommandValueOperation::Get &&
                (!IsCommandMaterialAlphaTested(candidate.domain) ||
                 !candidate.baseOrDiffuseTexture))
            {
                error =
                    "Alpha cutoff requires an alpha-tested material "
                    "with a base or diffuse texture.";
                return false;
            }
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.alphaCutoff,
                candidate.alphaCutoff,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.normal-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.normalTexture)
            {
                error =
                    "The selected material has no normal texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableNormalTexture,
                candidate.enableNormalTexture,
                value,
                error);
        }
        else if (path == "material.selected.normal-scale")
        {
            const Material* original =
                m_app->GetOriginalMaterial(material);
            const float defaultScale =
                original
                    ? original->normalTextureScale
                    : 1.f;
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.normalTextureScale,
                defaultScale,
                -2.f,
                2.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.occlusion-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.occlusionTexture)
            {
                error =
                    "The selected material has no occlusion texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableOcclusionTexture,
                candidate.enableOcclusionTexture,
                value,
                error);
        }
        else if (path ==
            "material.selected.occlusion-strength")
        {
            if (operation != CommandValueOperation::Get &&
                (!candidate.occlusionTexture ||
                 !candidate.enableOcclusionTexture))
            {
                error =
                    "Occlusion strength requires an enabled occlusion "
                    "texture.";
                return false;
            }
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.occlusionStrength,
                candidate.occlusionStrength,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.emissive-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.emissiveTexture)
            {
                error =
                    "The selected material has no emissive texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableEmissiveTexture,
                candidate.enableEmissiveTexture,
                value,
                error);
        }
        else if (path == "material.selected.emissive-color")
        {
            handled = ApplyCommandFloat3(
                operation,
                arguments,
                path,
                candidate.emissiveColor,
                candidate.emissiveColor,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.emissive-intensity")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.emissiveIntensity,
                candidate.emissiveIntensity,
                0.f,
                1000.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.transmission-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                (!IsCommandMaterialTransmissive(candidate.domain) ||
                 !candidate.transmissionTexture))
            {
                error =
                    "Transmission texture control requires a "
                    "transmissive material with a transmission texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableTransmissionTexture,
                candidate.enableTransmissionTexture,
                value,
                error);
        }
        else if (path ==
            "material.selected.transmission-factor")
        {
            if (operation != CommandValueOperation::Get &&
                !IsCommandMaterialTransmissive(candidate.domain))
            {
                error =
                    "Transmission factor requires a transmissive "
                    "material.";
                return false;
            }
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.transmissionFactor,
                candidate.transmissionFactor,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.alpha-mask-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.opacityTexture)
            {
                error =
                    "The selected material has no opacity texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableOpacityTexture,
                candidate.enableOpacityTexture,
                value,
                error);
        }
        else
        {
            handled = false;
        }

        if (!handled)
        {
            if (error.empty())
            {
                error =
                    "Internal Material command binding is missing for '" +
                    std::string(path) + "'.";
            }
            return false;
        }
        if (operation == CommandValueOperation::Get)
            return true;

        *material = candidate;
        m_app->NotifyMaterialCommandChanged(material);
        return true;
    }

auto UIRenderer::DispatchCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        switch (uvsr::ResolveUiSettingsCommandDispatcher(
            definition.section))
        {
        case UiSettingsCommandDispatcher::Ui:
            return DispatchUiCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::General:
            return DispatchGeneralCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::Representation:
            return DispatchRepresentationCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::Noise:
            return DispatchNoiseCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::Visibility:
            return DispatchVisibilityCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::Aliasing:
            return DispatchAliasingCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::Debug:
            return DispatchDebugCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::Sky:
            return DispatchSkyCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::Lights:
            return DispatchLightCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::DirectionalShadows:
            return DispatchDirectionalShadowCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::Denoising:
            return DispatchDenoisingCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::Materials:
            return DispatchMaterialCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandDispatcher::None:
            break;
        }
        error = "Internal value-command section dispatch is missing for '" +
            std::string(definition.name) + "'.";
        return false;
    }

auto UIRenderer::MakeSettingsSnapshotRuntimeAccess() -> SettingsSnapshotRuntimeAccess {
        SettingsSnapshotRuntimeAccess access;
        access.sceneReady =
            !m_app->IsSceneBusy() && m_app->IsSceneLoaded();
        access.validateValue =
            [](std::string_view name,
               std::string_view requestedValue,
               std::string& validationError)
            {
                const UiSettingsCommandDefinition* definition =
                    FindSettingsCommandDefinition(name);
                if (!definition ||
                    !IsSettingsSnapshotValue(*definition))
                {
                    validationError =
                        "setting is absent from the live SnapshotCatalog";
                    return false;
                }
                return ValidateSettingsSnapshotCatalogValue(
                    *definition,
                    requestedValue,
                    validationError);
            };
        access.readValue =
            [this](std::string_view name,
                   std::string& liveValue,
                   std::string& readError)
            {
                const UiSettingsCommandDefinition* definition =
                    FindSettingsCommandDefinition(name);
                if (!definition ||
                    !IsSettingsSnapshotValue(*definition))
                {
                    readError =
                        "setting is absent from the live SnapshotCatalog";
                    return false;
                }
                if (DispatchCommandValue(
                        *definition,
                        CommandValueOperation::Get,
                        {},
                        liveValue,
                        readError))
                {
                    return true;
                }
                if (definition->dynamic)
                {
                    liveValue = "<unavailable>";
                    readError.clear();
                    return true;
                }
                return false;
            };
        access.writeValue =
            [this](std::string_view name,
                   std::string_view requestedValue,
                   std::string& writeError)
            {
                const UiSettingsCommandDefinition* definition =
                    FindSettingsCommandDefinition(name);
                if (!definition ||
                    !IsSettingsSnapshotValue(*definition) ||
                    definition->kind ==
                        UiSettingsCommandKind::DynamicSelection ||
                    !definition->Supports(UiSettingsCommandVerb::Set))
                {
                    writeError =
                        "setting is not a mutable SnapshotCatalog value";
                    return false;
                }
                if (!CheckCommandMutationAllowed(
                        *definition,
                        writeError))
                {
                    return false;
                }

                std::string appliedValue;
                const std::vector<std::string> arguments =
                    BuildSettingsSnapshotCommandArguments(
                        *definition,
                        requestedValue);
                if (DispatchCommandValue(
                        *definition,
                        CommandValueOperation::Set,
                        arguments,
                        appliedValue,
                        writeError))
                {
                    return true;
                }
                if (writeError.rfind("No change: ", 0u) == 0u)
                {
                    writeError.clear();
                    return true;
                }
                return false;
            };
        access.driveSelector =
            [this](std::string_view name,
                   std::string_view canonicalToken,
                   bool begin,
                   bool rollback,
                   std::string& selectorError)
            {
                if (!begin)
                {
                    if (name == "gpu.adapter")
                    {
                        return SettingsSnapshotSelectorTransition::
                            RestartRequired;
                    }
                    if (name != "scene.current")
                    {
                        selectorError =
                            "selector remained pending without a poll contract";
                        return SettingsSnapshotSelectorTransition::Failed;
                    }
                    if (m_app->IsSceneBusy())
                        return SettingsSnapshotSelectorTransition::Pending;
                    if (m_app->HasSceneLoadFailure() ||
                        !m_app->IsSceneLoaded())
                    {
                        selectorError = "selected scene failed to load";
                        return SettingsSnapshotSelectorTransition::Failed;
                    }
                    const UiSettingsCommandDefinition* definition =
                        FindSettingsCommandDefinition(name);
                    std::string current;
                    if (!definition ||
                        !DispatchCommandValue(
                            *definition,
                            CommandValueOperation::Get,
                            {},
                            current,
                            selectorError) ||
                        current != canonicalToken)
                    {
                        if (selectorError.empty())
                        {
                            selectorError =
                                "loaded scene did not publish the requested "
                                "canonical token";
                        }
                        return SettingsSnapshotSelectorTransition::Failed;
                    }
                    return SettingsSnapshotSelectorTransition::Ready;
                }

                const UiSettingsCommandDefinition* definition =
                    FindSettingsCommandDefinition(name);
                if (!definition ||
                    !definition->Supports(UiSettingsCommandVerb::Set))
                {
                    selectorError = "selector is not mutable";
                    return SettingsSnapshotSelectorTransition::Failed;
                }
                if (!CheckCommandMutationAllowed(
                        *definition, selectorError))
                {
                    return SettingsSnapshotSelectorTransition::Failed;
                }
                if (name == "gpu.adapter")
                {
                    std::vector<SettingsSnapshotAdapterOption> options;
                    options.reserve(m_ui.GpuAdapterChoices.size());
                    for (const GpuAdapterChoice& adapter :
                         m_ui.GpuAdapterChoices)
                    {
                        options.push_back({
                            adapter.adapterIndex,
                            adapter.name
                        });
                    }
                    std::int64_t requestedIndex = -1;
                    std::string resolvedToken;
                    if (!ResolveSettingsSnapshotAdapterToken(
                            canonicalToken,
                            options,
                            requestedIndex,
                            resolvedToken,
                            selectorError) ||
                        resolvedToken != canonicalToken)
                    {
                        if (selectorError.empty())
                        {
                            selectorError =
                                "adapter selector token is not canonical";
                        }
                        return SettingsSnapshotSelectorTransition::Failed;
                    }
                    if (requestedIndex == m_ui.ActiveGpuAdapterIndex)
                    {
                        g_RestartAdapterIndex = -1;
                        return SettingsSnapshotSelectorTransition::Ready;
                    }
                    g_RestartAdapterIndex =
                        static_cast<int>(requestedIndex);
                    return SettingsSnapshotSelectorTransition::
                        RestartRequired;
                }
                try
                {
                    std::string appliedValue;
                    const std::vector<std::string> arguments =
                        BuildSettingsSnapshotCommandArguments(
                            *definition, canonicalToken);
                    if (!DispatchCommandValue(
                            *definition,
                            CommandValueOperation::Set,
                            arguments,
                            appliedValue,
                            selectorError))
                    {
                        return SettingsSnapshotSelectorTransition::Failed;
                    }
                    if (appliedValue != canonicalToken)
                    {
                        selectorError =
                            "selector did not return its canonical token";
                        return SettingsSnapshotSelectorTransition::Failed;
                    }
                }
                catch (const std::exception& exception)
                {
                    selectorError = std::string(
                        rollback ? "selector rollback threw: " :
                            "selector apply threw: ") + exception.what();
                    return SettingsSnapshotSelectorTransition::Failed;
                }
                if (name == "scene.current")
                    return SettingsSnapshotSelectorTransition::Pending;
                return SettingsSnapshotSelectorTransition::Ready;
            };
        access.persistRestartHandoff =
            [this](const SettingsSnapshotRestartHandoff& handoff,
                   std::string& error)
            {
                const std::filesystem::path path =
                    GetSettingsSnapshotRestartHandoffPath();
                if (!PersistSettingsSnapshotRestartHandoff(
                        path, handoff, error))
                {
                    return false;
                }
                m_SettingsSnapshotRestartHandoffOwned = true;
                return true;
            };
        return access;
    }

auto UIRenderer::RefreshSettingsSnapshot() -> void {
        m_SettingsSnapshots.Refresh(
            MakeSettingsSnapshotRuntimeAccess().readValue);
    }

auto UIRenderer::CopySettingsSnapshot() -> void {
        if (!m_SettingsSnapshots.PersistToLocalCatalog())
        {
            uvsr::log::warning(
                "The settings snapshot code was not copied because its "
                "local catalog entry could not be saved.");
            return;
        }
        ImGui::SetClipboardText(m_SettingsSnapshots.Code().c_str());
    }

auto UIRenderer::ApplyCanonicalSettingsSnapshot(
        std::string_view canonical,
        std::size_t& changedValueCount,
        std::string& error) -> bool {
        const SettingsSnapshotTransactionStep step =
            m_SettingsSnapshots.BeginApplyCanonicalStaged(
            canonical,
            MakeSettingsSnapshotRuntimeAccess());
        changedValueCount = step.result.changedValueCount;
        if (step.progress == SettingsSnapshotTransactionProgress::Succeeded)
        {
            error.clear();
            return true;
        }
        error = step.result.error.empty()
            ? "runtime settings round trip unexpectedly requires staged "
                "continuation"
            : step.result.error;
        return false;
    }

auto UIRenderer::BuildSettingsSnapshotRestartHandoffCode(
        const SettingsSnapshotRestartHandoff& handoff,
        std::string& code,
        std::string& error) const -> bool {
        DecodedSettings requested;
        for (const SettingsSnapshotTransactionEntry& entry :
             handoff.transaction)
        {
            if (!requested.emplace(
                    entry.name, entry.requestedValue).second)
            {
                error = "restart handoff contains duplicate setting names";
                return false;
            }
        }
        code = BuildSettingsSnapshotCode(
            FormatCanonicalSettingsSnapshot(requested));
        error.clear();
        return true;
    }

auto UIRenderer::FailStartupSettingsSnapshot(
        std::string_view code,
        std::string_view error) -> void {
        g_RestartRequested = false;
        g_RestartAdapterIndex = -1;
        g_StartupSettingsSnapshotFailed = true;
        uvsr::log::error(
            "Startup settings snapshot %s failed: %s",
            code.empty() ? "<restart-handoff>" :
                std::string(code).c_str(),
            error.empty() ? "settings snapshot transaction failed" :
                std::string(error).c_str());
        glfwSetWindowShouldClose(
            GetDeviceManager()->GetWindow(), GLFW_TRUE);
    }

auto UIRenderer::HandleStagedSettingsSnapshotStep(
        const SettingsSnapshotTransactionStep& step) -> void {
        if (step.progress == SettingsSnapshotTransactionProgress::Pending)
            return;
        if (step.progress ==
            SettingsSnapshotTransactionProgress::RestartRequired)
        {
            if (!m_SettingsSnapshotRestartHandoffOwned ||
                g_RestartAdapterIndex < 0)
            {
                FailStartupSettingsSnapshot(
                    m_PendingSettingsSnapshotCode,
                    "adapter restart was requested without a durable "
                    "handoff and staged target");
                return;
            }
            g_RestartRequested = true;
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(), GLFW_TRUE);
            uvsr::log::info(
                "Settings snapshot durable handoff is ready; requesting "
                "adapter restart");
            return;
        }

        const SettingsSnapshotApplyOrigin origin =
            m_SettingsSnapshotApplyOrigin;
        const std::string code = m_PendingSettingsSnapshotCode;
        bool failed =
            step.progress == SettingsSnapshotTransactionProgress::Failed;
        std::string terminalError = step.result.error.empty()
            ? "settings snapshot transaction failed"
            : step.result.error;
        if (m_SettingsSnapshotRestartHandoffOwned)
        {
            std::string cleanupError;
            if (!RemoveSettingsSnapshotRestartHandoff(
                    GetSettingsSnapshotRestartHandoffPath(),
                    cleanupError))
            {
                failed = true;
                terminalError +=
                    "; could not clean durable restart handoff: " +
                    cleanupError;
            }
            else
            {
                m_SettingsSnapshotRestartHandoffOwned = false;
            }
        }
        m_SettingsSnapshotApplyOrigin = SettingsSnapshotApplyOrigin::None;
        m_PendingSettingsSnapshotCode.clear();
        if (failed)
        {
            if (origin == SettingsSnapshotApplyOrigin::Startup)
            {
                FailStartupSettingsSnapshot(code, terminalError);
            }
            else if (origin == SettingsSnapshotApplyOrigin::Command)
            {
                SetCommandResult(terminalError, true);
            }
            else
            {
                FailStartupSettingsSnapshot(code, terminalError);
            }
            return;
        }

        if (origin == SettingsSnapshotApplyOrigin::Startup)
        {
            uvsr::log::info(
                "Loaded startup settings snapshot %s (%zu values changed)",
                code.c_str(), step.result.changedValueCount);
        }
        else if (origin == SettingsSnapshotApplyOrigin::Command)
        {
            SetCommandResult(
                "settings.load = " + code + " (" +
                std::to_string(step.result.changedValueCount) +
                " values changed)");
        }
    }

auto UIRenderer::TryApplyStartupSettingsSnapshot() -> void {
        if (m_SettingsSnapshots.HasStagedApply())
        {
            HandleStagedSettingsSnapshotStep(
                m_SettingsSnapshots.ContinueStagedApply(
                    MakeSettingsSnapshotRuntimeAccess()));
            return;
        }
        if (m_app->IsSceneBusy() || !m_app->IsSceneLoaded())
        {
            return;
        }

        if (!m_SettingsSnapshotRestartHandoffAttempted)
        {
            m_SettingsSnapshotRestartHandoffAttempted = true;
            const std::filesystem::path path =
                GetSettingsSnapshotRestartHandoffPath();
            SettingsSnapshotRestartHandoff handoff;
            bool found = false;
            std::string journalError;
            if (!LoadSettingsSnapshotRestartHandoff(
                    path, handoff, found, journalError))
            {
                std::string cleanupError;
                if (!RemoveSettingsSnapshotRestartHandoff(
                        path, cleanupError))
                {
                    journalError +=
                        "; corrupt journal cleanup failed: " + cleanupError;
                }
                m_StartupSettingsSnapshotAttempted = true;
                FailStartupSettingsSnapshot(
                    m_StartupSettingsSnapshotCode, journalError);
                return;
            }
            if (found)
            {
                m_SettingsSnapshotRestartHandoffOwned = true;
                m_StartupSettingsSnapshotAttempted = true;
                std::string journalCode;
                if (!BuildSettingsSnapshotRestartHandoffCode(
                        handoff, journalCode, journalError) ||
                    (!m_StartupSettingsSnapshotCode.empty() &&
                        journalCode != m_StartupSettingsSnapshotCode))
                {
                    if (journalError.empty())
                    {
                        journalError =
                            "restart handoff does not match the command-line "
                            "snapshot code";
                    }
                    std::string cleanupError;
                    if (!RemoveSettingsSnapshotRestartHandoff(
                            path, cleanupError))
                    {
                        journalError +=
                            "; mismatched journal cleanup failed: " +
                            cleanupError;
                    }
                    m_SettingsSnapshotRestartHandoffOwned = false;
                    FailStartupSettingsSnapshot(
                        journalCode, journalError);
                    return;
                }

                m_SettingsSnapshotApplyOrigin =
                    SettingsSnapshotApplyOrigin::Startup;
                m_PendingSettingsSnapshotCode = journalCode;
                HandleStagedSettingsSnapshotStep(
                    m_SettingsSnapshots.ResumeStagedApply(
                        handoff,
                        MakeSettingsSnapshotRuntimeAccess()));
                return;
            }
        }

        if (m_StartupSettingsSnapshotCode.empty() ||
            m_StartupSettingsSnapshotAttempted)
        {
            return;
        }

        m_StartupSettingsSnapshotAttempted = true;
        m_SettingsSnapshotApplyOrigin =
            SettingsSnapshotApplyOrigin::Startup;
        m_PendingSettingsSnapshotCode = m_StartupSettingsSnapshotCode;
        HandleStagedSettingsSnapshotStep(
            m_SettingsSnapshots.BeginLoadCodeStaged(
                m_StartupSettingsSnapshotCode,
                MakeSettingsSnapshotRuntimeAccess()));
    }

auto UIRenderer::DispatchCommandAction(
        const UiSettingsCommandDefinition& definition,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error) -> bool {
        if (!arguments.empty())
        {
            error = std::string(definition.name) +
                " does not accept arguments.";
            return false;
        }

        const std::string_view action = definition.name;
        if (action == "open-scene-folder")
        {
            const std::filesystem::path sceneFolder =
                m_app->GetSceneDir();
            const HINSTANCE result = ShellExecuteW(
                nullptr,
                L"open",
                sceneFolder.c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32)
            {
                error = "Windows could not open the scene folder.";
                return false;
            }
            value = "opened";
            return true;
        }
        if (action == "reset-settings")
        {
            ResetAllSettingsToFactoryDefaults();
            value = "restored";
            return true;
        }
        if (action == "capture")
        {
            m_ui.CopyScreenshotToClipboard = true;
            value = "queued";
            return true;
        }
        if (action == "restart")
        {
            g_RestartRequested = true;
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            value = "requested";
            return true;
        }

        error = "Internal action binding is missing for '" +
            std::string(action) + "'.";
        return false;
    }

auto UIRenderer::AppendDynamicCommandValues(
        std::string_view path,
        std::vector<std::string>& values) const -> void {
        if (path == "gpu.adapter")
        {
            for (const GpuAdapterChoice& adapter :
                m_ui.GpuAdapterChoices)
            {
                values.push_back(std::to_string(adapter.adapterIndex));
                values.push_back(adapter.name);
            }
        }
        else if (path == "scene.current")
        {
            for (const SceneCatalogEntry& scene :
                m_app->GetAvailableScenes())
            {
                values.push_back(scene.FileName);
                values.push_back(scene.DisplayName);
            }
        }
        else if (path == "light.selected")
        {
            const auto& lights = m_app->GetEditableLights();
            for (size_t index = 0u;
                index < lights.size();
                ++index)
            {
                values.push_back(std::to_string(index));
                if (lights[index])
                    values.push_back(lights[index]->GetName());
            }
        }
        else if (path == "material.selected")
        {
            const std::shared_ptr<Scene> scene = m_app->GetScene();
            if (scene && scene->GetSceneGraph())
            {
                for (const std::shared_ptr<Material>& material :
                    scene->GetSceneGraph()->GetMaterials())
                {
                    if (!material)
                        continue;
                    values.push_back(
                        std::to_string(material->materialID));
                    values.push_back(material->name);
                }
            }
        }
    }

auto UIRenderer::GetCommandCompletionCandidates(
        const UiCommandCompletionToken& completion) const -> std::vector<std::string> {
        std::vector<std::string> candidates;
        const auto appendCandidate =
            [&candidates, &completion](std::string_view candidate)
        {
            if (candidate.empty() ||
                !UiCommandCompletionMatches(
                    candidate, completion.prefix))
            {
                return;
            }
            candidates.emplace_back(candidate);
        };

        switch (completion.target)
        {
        case UiCommandCompletionTarget::Verb:
            for (const std::string_view candidate :
                UiCommandVerbCompletionCandidates)
            {
                appendCandidate(candidate);
            }
            break;
        case UiCommandCompletionTarget::HelpTopic:
            for (const std::string_view candidate :
                { "commands", "settings", "skins" })
            {
                appendCandidate(candidate);
            }
            break;
        case UiCommandCompletionTarget::ListPrefix:
        case UiCommandCompletionTarget::Path:
            for (const UiSettingsCommandBinding& binding :
                UiSettingsCommandBindings)
            {
                if (completion.target ==
                        UiCommandCompletionTarget::Path &&
                    binding.kind == UiSettingsCommandKind::Action)
                {
                    continue;
                }
                appendCandidate(binding.name);
            }
            break;
        case UiCommandCompletionTarget::Action:
            for (const UiSettingsCommandBinding& binding :
                UiSettingsCommandBindings)
            {
                if (binding.kind ==
                    UiSettingsCommandKind::Action)
                {
                    appendCandidate(binding.name);
                }
            }
            appendCandidate(UiReloadShadersCommandAction);
            appendCandidate(UiSettingsLoadCommandAction);
            break;
        case UiCommandCompletionTarget::Value:
        {
            const UiSettingsCommandDefinition* definition =
                FindSettingsCommandDefinition(completion.valuePath);
            if (!definition ||
                definition->kind == UiSettingsCommandKind::Action)
            {
                break;
            }

            std::string_view remaining = definition->domain;
            while (!remaining.empty())
            {
                const size_t separator = remaining.find('|');
                const std::string_view candidate =
                    remaining.substr(0u, separator);
                if (candidate.find(' ') == std::string_view::npos &&
                    candidate.find(';') == std::string_view::npos)
                {
                    appendCandidate(candidate);
                }
                if (separator == std::string_view::npos)
                    break;
                remaining.remove_prefix(separator + 1u);
            }
            if (definition->dynamic)
            {
                std::vector<std::string> dynamicValues;
                AppendDynamicCommandValues(
                    definition->name, dynamicValues);
                for (const std::string& candidate : dynamicValues)
                    appendCandidate(candidate);
            }
            break;
        }
        case UiCommandCompletionTarget::Argument:
            break;
        }

        std::sort(candidates.begin(), candidates.end());
        candidates.erase(
            std::unique(candidates.begin(), candidates.end()),
            candidates.end());
        return candidates;
    }

auto UIRenderer::ExecuteUiCommand(const UiCommand& command) -> void {
        if (command.verb == UiCommandVerb::Help)
        {
            const std::string topic = command.arguments.empty()
                ? "commands"
                : NormalizeCommandAscii(
                    command.arguments.front(), true);
            if (topic == "skins")
            {
                SetCommandResult(
                    "Skins: Amp is the animated UVSR interface; Ogg is stock "
                    "ImGui with UI animations disabled. Use /skin [amp|ogg].");
            }
            else if (topic == "fonts")
            {
                SetCommandResult(
                    "Fonts: Codex uses installed Windows Segoe UI; Noto Sans "
                    "uses bundled Regular, SemiBold, and Bold faces; Ogg uses "
                    "ProggyClean body text. Amp headings remain Noto Sans Bold "
                    "when Ogg is selected. Use 'set ui.font-family "
                    "[codex|noto-sans|proggy-clean]'.");
            }
            else if (topic == "settings")
            {
                SetCommandResult(
                    "Use 'list [prefix]' to discover settings and "
                    "supported verbs, then get, set, toggle, reset, or "
                    "run the listed path. Tab completes.");
            }
            else
            {
                SetCommandResult(
                    "Commands: help [topic], list [prefix], get <path>, "
                    "set <path> <value>, toggle <path>, reset <path|all>, "
                    "run <action>. Shortcuts: skin, ui, scene, camera, "
                    "reload-shaders, settings.load <snapshot-code>.");
            }
            return;
        }

        if (command.verb == UiCommandVerb::List)
        {
            const std::string prefix = command.arguments.empty()
                ? std::string{}
                : NormalizeCommandAscii(
                    command.arguments.front());
            std::string listing;
            for (const UiSettingsCommandDefinition& definition :
                UiSettingsCommandCatalog)
            {
                if (!StartsWithCommandPrefix(
                        definition.name, prefix))
                {
                    continue;
                }
                if (!listing.empty())
                    listing += "\n";
                listing += definition.name;
                listing += " [";
                listing += GetSettingsCommandVerbList(definition);
                listing += "] / ";
                listing += definition.domain;

                std::vector<std::string> dynamicValues;
                AppendDynamicCommandValues(
                    definition.name, dynamicValues);
                if (!dynamicValues.empty())
                {
                    std::sort(
                        dynamicValues.begin(), dynamicValues.end());
                    dynamicValues.erase(
                        std::unique(
                            dynamicValues.begin(),
                            dynamicValues.end()),
                        dynamicValues.end());
                    listing += "\n  values: ";
                    for (size_t index = 0u;
                        index < dynamicValues.size();
                        ++index)
                    {
                        if (index > 0u)
                            listing += " | ";
                        listing += dynamicValues[index];
                    }
                }
            }
            if (listing.empty())
            {
                SetCommandResult(
                    "No Settings command matches '" + prefix + "'.",
                    true);
            }
            else
            {
                SetCommandResult(std::move(listing));
            }
            return;
        }

        if (command.verb == UiCommandVerb::Run &&
            NormalizeCommandAscii(command.action) ==
                UiSettingsLoadCommandAction)
        {
            if (command.arguments.size() != 1u)
            {
                SetCommandResult(
                    "settings.load requires exactly one snapshot code.",
                    true);
                return;
            }
            if (m_SettingsSnapshots.HasStagedApply())
            {
                SetCommandResult(
                    "Another settings snapshot transaction is active.", true);
                return;
            }
            m_SettingsSnapshotApplyOrigin =
                SettingsSnapshotApplyOrigin::Command;
            m_PendingSettingsSnapshotCode = command.arguments.front();
            SetCommandResult(
                "settings.load = " + command.arguments.front() +
                " (pending)");
            HandleStagedSettingsSnapshotStep(
                m_SettingsSnapshots.BeginLoadCodeStaged(
                    command.arguments.front(),
                    MakeSettingsSnapshotRuntimeAccess()));
            return;
        }

        if (command.verb == UiCommandVerb::Run &&
            NormalizeCommandAscii(command.action) ==
                UiReloadShadersCommandAction)
        {
            if (!command.arguments.empty())
            {
                SetCommandResult(
                    "reload-shaders does not accept arguments.",
                    true);
                return;
            }
            if (m_app->IsSceneBusy())
            {
                SetCommandResult(
                    "Shaders cannot reload while a scene is loading.",
                    true);
                return;
            }
            m_ui.ShaderReloadRequested = true;
            SetCommandResult("reload-shaders = requested");
            return;
        }

        if (command.verb == UiCommandVerb::Run)
        {
            const UiSettingsCommandDefinition* definition =
                FindSettingsCommandDefinition(command.action);
            if (!definition ||
                definition->kind != UiSettingsCommandKind::Action)
            {
                SetCommandResult(
                    "Unknown Settings action '" + command.action +
                    "'. Use '/list'.",
                    true);
                return;
            }
            if (!definition->Supports(UiSettingsCommandVerb::Run))
            {
                SetCommandResult(
                    std::string(definition->name) + " supports [" +
                    GetSettingsCommandVerbList(*definition) + "].",
                    true);
                return;
            }
            std::string error;
            if (!CheckCommandMutationAllowed(*definition, error))
            {
                SetCommandResult(std::move(error), true);
                return;
            }
            std::string value;
            if (!DispatchCommandAction(
                    *definition,
                    command.arguments,
                    value,
                    error))
            {
                SetCommandResult(std::move(error), true);
                return;
            }
            SetCommandResult(
                std::string(definition->name) + " = " + value);
            return;
        }

        if (command.verb == UiCommandVerb::Reset &&
            NormalizeCommandAscii(command.path) == "all")
        {
            const UiSettingsCommandDefinition* definition =
                FindSettingsCommandDefinition("reset-settings");
            std::string error;
            if (!definition ||
                !CheckCommandMutationAllowed(*definition, error))
            {
                SetCommandResult(
                    error.empty()
                        ? "The Settings reset action is unavailable."
                        : std::move(error),
                    true);
                return;
            }
            std::string value;
            if (!DispatchCommandAction(
                    *definition, {}, value, error))
            {
                SetCommandResult(std::move(error), true);
                return;
            }
            SetCommandResult("all = " + value);
            return;
        }

        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(command.path);
        if (!definition ||
            definition->kind == UiSettingsCommandKind::Action)
        {
            SetCommandResult(
                "Unknown Settings path '" + command.path +
                "'. Use '/list'.",
                true);
            return;
        }

        CommandValueOperation operation =
            CommandValueOperation::Get;
        switch (command.verb)
        {
        case UiCommandVerb::Get:
            operation = CommandValueOperation::Get;
            break;
        case UiCommandVerb::Set:
            operation = CommandValueOperation::Set;
            break;
        case UiCommandVerb::Toggle:
            operation = CommandValueOperation::Toggle;
            break;
        case UiCommandVerb::Reset:
            operation = CommandValueOperation::Reset;
            break;
        default:
            SetCommandResult("Expected a Settings value command.", true);
            return;
        }

        const UiSettingsCommandVerb settingsVerb =
            GetSettingsCommandVerb(operation);
        if (!definition->Supports(settingsVerb))
        {
            SetCommandResult(
                std::string(definition->name) + " supports [" +
                GetSettingsCommandVerbList(*definition) + "].",
                true);
            return;
        }
        std::string error;
        if (operation != CommandValueOperation::Get &&
            !CheckCommandMutationAllowed(*definition, error))
        {
            SetCommandResult(std::move(error), true);
            return;
        }

        std::string value;
        if (!DispatchCommandValue(
                *definition,
                operation,
                command.arguments,
                value,
                error))
        {
            SetCommandResult(std::move(error), true);
            return;
        }
        SetCommandResult(
            std::string(definition->name) + " = " + value);
    }

auto UIRenderer::CompleteCommandInput(ImGuiInputTextCallbackData* data) -> void {
        const std::string_view input(
            data->Buf,
            static_cast<size_t>(data->BufTextLen));
        const UiCommandCompletionToken completion =
            GetUiCommandCompletionToken(
                input,
                static_cast<size_t>(data->CursorPos));
        std::vector<std::string> candidates =
            GetCommandCompletionCandidates(completion);
        if (candidates.empty())
        {
            SetCommandResult("No completion matches.", true);
            return;
        }

        std::string replacement = candidates.front();
        if (candidates.size() > 1u)
        {
            size_t commonLength = replacement.size();
            for (size_t candidateIndex = 1u;
                candidateIndex < candidates.size();
                ++candidateIndex)
            {
                const std::string& candidate =
                    candidates[candidateIndex];
                commonLength = std::min(
                    commonLength,
                    candidate.size());
                size_t index = 0u;
                while (index < commonLength &&
                    std::tolower(static_cast<unsigned char>(
                        replacement[index])) ==
                    std::tolower(static_cast<unsigned char>(
                        candidate[index])))
                {
                    ++index;
                }
                commonLength = index;
            }
            replacement.resize(commonLength);

            std::string matches = "Matches: ";
            for (size_t index = 0u;
                index < candidates.size();
                ++index)
            {
                if (index > 0u)
                    matches += ", ";
                matches += candidates[index];
                if (matches.size() > 240u)
                {
                    matches += ", ...";
                    break;
                }
            }
            SetCommandResult(std::move(matches));
        }

        if (replacement.size() < completion.prefix.size())
            return;
        data->DeleteChars(
            static_cast<int>(completion.replaceBegin),
            static_cast<int>(
                completion.replaceEnd -
                completion.replaceBegin));
        data->InsertChars(
            static_cast<int>(completion.replaceBegin),
            replacement.c_str());
        if (candidates.size() == 1u &&
            completion.target != UiCommandCompletionTarget::Argument &&
            completion.target != UiCommandCompletionTarget::Value)
        {
            data->InsertChars(data->CursorPos, " ");
        }
    }

auto UIRenderer::RecallCommandHistory(
        ImGuiInputTextCallbackData* data,
        bool previous) -> void {
        if (m_CommandHistory.empty())
            return;
        if (previous)
        {
            if (m_CommandHistoryIndex < 0)
            {
                m_CommandHistoryIndex =
                    static_cast<int>(m_CommandHistory.size()) - 1;
            }
            else if (m_CommandHistoryIndex > 0)
            {
                --m_CommandHistoryIndex;
            }
        }
        else
        {
            if (m_CommandHistoryIndex >= 0 &&
                m_CommandHistoryIndex <
                    static_cast<int>(m_CommandHistory.size()) - 1)
            {
                ++m_CommandHistoryIndex;
            }
            else
            {
                m_CommandHistoryIndex = -1;
            }
        }

        data->DeleteChars(0, data->BufTextLen);
        if (m_CommandHistoryIndex >= 0)
        {
            data->InsertChars(
                0,
                m_CommandHistory[
                    static_cast<size_t>(m_CommandHistoryIndex)]
                    .c_str());
        }
    }

auto UIRenderer::CommandInputCallback(
        ImGuiInputTextCallbackData* data) -> int {
        UIRenderer* renderer =
            static_cast<UIRenderer*>(data->UserData);
        if (data->EventFlag ==
            ImGuiInputTextFlags_CallbackCompletion)
        {
            renderer->CompleteCommandInput(data);
        }
        else if (data->EventFlag ==
            ImGuiInputTextFlags_CallbackHistory)
        {
            renderer->RecallCommandHistory(
                data,
                data->EventKey == ImGuiKey_UpArrow);
        }
        return 0;
    }

auto UIRenderer::SubmitCommandInput() -> void {
        const std::string input(m_CommandBuffer.data());
        if (!input.empty())
        {
            if (m_CommandHistory.empty() ||
                m_CommandHistory.back() != input)
            {
                m_CommandHistory.push_back(input);
                if (m_CommandHistory.size() > 32u)
                    m_CommandHistory.pop_front();
            }
        }
        m_CommandHistoryIndex = -1;
        m_CommandBuffer.fill('\0');

        const UiCommandParseResult parsed =
            ParseUiCommand(input);
        if (!parsed)
        {
            SetCommandResult(parsed.message, true);
            return;
        }
        m_PendingCommand = parsed.command;
    }

auto UIRenderer::DrawCommandInterface() -> void {
        const bool commandMotionEnabled =
            ImGui::IsUvsrUiMotionEnabled();
        if (!m_CommandOpen && m_CommandAppearance <= 0.f)
            return;
        const float commandAppearanceOpacity =
            commandMotionEnabled
                ? SmoothPixelZoomVisibility(m_CommandAppearance)
                : m_CommandAppearance;
        const float commandAppearanceScale = commandAppearanceOpacity;

        const CommandInterfaceLayout& commandLayout =
            m_CommandLayout;
        if (!commandLayout.fits)
        {
            if (m_CommandOpen)
                m_CommandFocusRequested = true;
            return;
        }

        ImGuiWindowFlags commandWindowFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings;
        if (!m_CommandOpen)
            commandWindowFlags |= ImGuiWindowFlags_NoInputs;
        else if (commandMotionEnabled &&
            m_CommandAppearance < 1.f)
        {
            commandWindowFlags |= ImGuiWindowFlags_NoMouseInputs;
        }

        ImGui::PushFont(GetActiveUiFont());
        ApplyActiveUiWordSpacing();
        // The command shell shares the compact Performance body's opaque
        // surface. Its existing whole-window appearance transform still owns
        // entry and exit opacity, so no other panel is affected.
        PushOpaquePanelBodySurface();

        ImGui::SetNextWindowPos(
            ImVec2(
                commandLayout.left,
                commandLayout.bottom),
            ImGuiCond_Always,
            ImVec2(0.f, 1.f));
        ImGui::SetNextWindowSize(
            ImVec2(
                commandLayout.width,
                commandLayout.height),
            ImGuiCond_Always);
        ImGui::Begin(
            "##UvsrCommandInterface",
            nullptr,
            commandWindowFlags);
        ImDrawList* commandWindowDrawList =
            ImGui::GetWindowDrawList();
        ImGui::TextUnformatted("/");
        ImGui::SameLine();
        const bool showCommandResult =
            m_CommandBuffer.front() == '\0' &&
            !m_CommandResult.empty();
        const float commandRowWidth = ImGui::GetContentRegionAvail().x;
        const float resultDetailsButtonWidth = ImGui::GetFrameHeight();
        const bool commandResultNeedsDetails = showCommandResult &&
            (m_CommandResult.find('\n') != std::string::npos ||
                ImGui::CalcTextSize(m_CommandResult.c_str()).x >
                    commandRowWidth);
        ImGui::SetNextItemWidth(
            commandResultNeedsDetails
                ? std::max(
                    1.f,
                    commandRowWidth - resultDetailsButtonWidth -
                        ImGui::GetStyle().ItemSpacing.x)
                : -FLT_MIN);
        if (m_CommandFocusRequested)
        {
            ImGui::SetKeyboardFocusHere();
            m_CommandFocusRequested = false;
        }
        const ImGuiInputTextFlags inputFlags =
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CallbackCompletion |
            ImGuiInputTextFlags_CallbackHistory;
        const char* commandHint = showCommandResult
            ? m_CommandResult.c_str()
            : "Try help / Enter applies / Tab completes / Up/Down history / Slash closes";
        if (showCommandResult)
        {
            ImGui::PushStyleColor(
                ImGuiCol_TextDisabled,
                m_CommandResultIsError
                    ? g_UiVisualTokens.errorText
                    : g_UiVisualTokens.successText);
        }
        const bool commandSubmitted = ImGui::InputTextWithHint(
                "##UvsrCommand",
                commandHint,
                m_CommandBuffer.data(),
                m_CommandBuffer.size(),
                inputFlags,
                CommandInputCallback,
                this);
        if (showCommandResult)
            ImGui::PopStyleColor();
        const bool commandEdited = ImGui::IsItemEdited();
        if (commandResultNeedsDetails && !commandEdited)
        {
            ImGui::SameLine();
            if (ImGui::Button(
                    "...##CommandResultDetails",
                    ImVec2(resultDetailsButtonWidth, 0.f)))
            {
                ImGui::OpenPopup("##CommandResultDetailsPopup");
            }
            ImGui::SetItemTooltip("Open the complete command result.");
        }
        if (commandEdited)
            m_CommandResult.clear();
        if (commandSubmitted)
        {
            SubmitCommandInput();
            m_CommandFocusRequested = true;
        }
        if (ImGui::IsPopupOpen("##CommandResultDetailsPopup"))
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowSize(
                ImVec2(
                    std::max(
                        1.f,
                        std::min(
                            720.f * m_UiDisplayScale,
                            viewport->WorkSize.x -
                                20.f * m_UiDisplayScale)),
                    std::max(
                        1.f,
                        std::min(
                            300.f * m_UiDisplayScale,
                            viewport->WorkSize.y * 0.5f))),
                ImGuiCond_Appearing);
            if (ImGui::BeginPopup(
                    "##CommandResultDetailsPopup",
                    ImGuiWindowFlags_NoSavedSettings))
            {
                if (m_CommandResult.empty())
                {
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        m_CommandResultIsError
                            ? g_UiVisualTokens.errorText
                            : g_UiVisualTokens.successText);
                    ImGui::InputTextMultiline(
                        "##CommandResultText",
                        m_CommandResult.data(),
                        m_CommandResult.size() + 1u,
                        ImVec2(-FLT_MIN, -FLT_MIN),
                        ImGuiInputTextFlags_ReadOnly);
                    ImGui::PopStyleColor();
                }
                ImGui::EndPopup();
            }
        }
        const ImVec2 commandWindowPosition =
            ImGui::GetWindowPos();
        const ImVec2 commandWindowSize =
            ImGui::GetWindowSize();
        const float commandWindowBottom =
            commandWindowPosition.y + commandWindowSize.y;
        const ImVec2 commandAppearancePivot(
            commandWindowPosition.x + commandWindowSize.x * 0.5f,
            commandWindowBottom);
        UiBackdropRect& commandBackdrop =
            m_ui.BackdropRects[UiCommandBackdropIndex];
        CaptureCurrentWindowBackdrop(
            commandBackdrop,
            ImGui::GetStyle().WindowRounding);
        ApplyBackdropAppearance(
            commandBackdrop,
            commandAppearancePivot,
            commandAppearanceScale,
            commandAppearanceOpacity);
        commandBackdrop.shadowBlur =
            g_UiVisualTokens.backdropShadowBlur;
        commandBackdrop.shadowOpacity =
            g_UiVisualTokens.backdropShadowOpacity;
        commandBackdrop.shadowOffsetY =
            g_UiVisualTokens.backdropShadowOffsetY;
        ImGui::End();
        ImGui::PopStyleColor();
        RestoreActiveUiWordSpacing();
        ImGui::PopFont();
        ApplyWindowAppearance(
            commandWindowDrawList,
            commandAppearancePivot,
            commandAppearanceScale,
            commandAppearanceOpacity);
    }

#if defined(UVSR_BUILD_TESTING)
auto UIRenderer::ApplyRuntimeSetting(
        std::string_view name,
        std::string_view requestedValue,
        std::string& error) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(name);
        if (!definition ||
            definition->kind == UiSettingsCommandKind::Action)
        {
            error = "unknown retained setting '" + std::string(name) + "'";
            return false;
        }

        std::vector<std::string> arguments;
        if (definition->kind == UiSettingsCommandKind::Float3 ||
            definition->kind == UiSettingsCommandKind::Float4)
        {
            std::istringstream values{ std::string(requestedValue) };
            for (std::string token; values >> token; )
                arguments.push_back(std::move(token));
        }
        else
        {
            arguments.emplace_back(requestedValue);
        }

        std::string appliedValue;
        if (DispatchCommandValue(
                *definition,
                CommandValueOperation::Set,
                arguments,
                appliedValue,
                error))
        {
            return true;
        }
        if (error.rfind("No change: ", 0u) == 0u)
        {
            error.clear();
            return true;
        }
        return false;
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UIRenderer::VerifyCanonicalSettingsContract() -> int {
        std::vector<std::string> failures;
        const auto fail = [&](std::string message)
        {
            failures.push_back(std::move(message));
        };
        if (m_app->IsSceneBusy() || !m_app->IsSceneLoaded())
        {
            fail("default scene did not finish loading");
        }

        SettingsCommandFloatPrecisionScope preciseFormatting;
        const auto isExpectedNoChange = [](std::string_view error)
        {
            return error.rfind("No change: ", 0u) == 0u;
        };
        const auto isSceneRule = [](std::string_view value)
        {
            return value == "scene-default-selection" ||
                value == "scene-authored";
        };
        const auto canonicalizeDefault = [&]
        (
            const UiSettingsCommandDefinition& definition,
            bool getSucceeded,
            std::string_view runtimeValue,
            std::string_view getError,
            std::string& canonical)
        {
            const std::string_view rule = definition.defaultValue;
            if (rule == "automatic-highest-dedicated-memory")
            {
                const GpuAdapterChoice* active =
                    GetActiveGpuAdapterChoice();
                uint64_t maximumDedicatedMemory = 0u;
                for (const GpuAdapterChoice& adapter :
                    m_ui.GpuAdapterChoices)
                {
                    maximumDedicatedMemory = std::max(
                        maximumDedicatedMemory,
                        adapter.dedicatedVideoMemory);
                }
                if (!getSucceeded || !active ||
                    active->dedicatedVideoMemory !=
                        maximumDedicatedMemory)
                {
                    fail(std::string(definition.name) +
                        " did not resolve the highest-dedicated-memory adapter: " +
                        std::string(getError));
                    return false;
                }
                canonical = std::string(rule);
                return true;
            }
            if (rule == "automatic-supported-mode")
            {
                if (!getSucceeded ||
                    m_ui.AdaptiveSync != GetDefaultAdaptiveSyncMode())
                {
                    fail(std::string(definition.name) +
                        " did not resolve the supported automatic mode: " +
                        std::string(getError));
                    return false;
                }
                canonical = std::string(rule);
                return true;
            }
            if (rule == "bistro-interior")
            {
                const std::string scene = NormalizeCommandAscii(
                    m_app->GetCurrentSceneName(), true);
                if (!getSucceeded ||
                    scene.find("bistrointeriorretextured") ==
                        std::string::npos)
                {
                    fail(std::string(definition.name) +
                        " did not resolve the retained Bistro default: " +
                        std::string(runtimeValue));
                    return false;
                }
                canonical = std::string(rule);
                return true;
            }
            if (rule == "scene-default-selection")
            {
                const std::shared_ptr<Light> selected =
                    EnsureCommandSelectedLight();
                const auto& lights = m_app->GetEditableLights();
                const auto selectedPosition = std::find(
                    lights.begin(), lights.end(), selected);
                const std::string selectedToken =
                    !selected || selectedPosition == lights.end()
                    ? std::string{}
                    : FormatSettingsSnapshotLightToken(
                        static_cast<size_t>(std::distance(
                            lights.begin(), selectedPosition)),
                        selected->GetName());
                if (!getSucceeded || !selected ||
                    selected != GetDefaultCommandLight() ||
                    runtimeValue != selectedToken)
                {
                    fail(std::string(definition.name) +
                        " did not resolve the scene-default light");
                    return false;
                }
                canonical = std::string(rule);
                return true;
            }
            if (rule == "scene-authored")
            {
                if (EnsureCommandSelectedLight() !=
                    GetDefaultCommandLight())
                {
                    fail(std::string(definition.name) +
                        " was not evaluated against the scene-default light");
                    return false;
                }
                // Properties outside the selected light's domain are
                // intentionally unavailable; the dispatcher was still the
                // source of that result.
                canonical = std::string(rule);
                return true;
            }
            if (rule == "none")
            {
                if (!getSucceeded || m_ui.SelectedMaterial ||
                    runtimeValue != "none")
                {
                    fail(std::string(definition.name) +
                        " did not retain the no-material default");
                    return false;
                }
                canonical = std::string(rule);
                return true;
            }
            if (rule == "material-authored")
            {
                if (m_ui.SelectedMaterial)
                {
                    fail(std::string(definition.name) +
                        " unexpectedly had a selected material");
                    return false;
                }
                canonical = std::string(rule);
                return true;
            }
            if (!getSucceeded)
            {
                fail(std::string(definition.name) +
                    " GET failed: " + std::string(getError));
                return false;
            }
            canonical = std::string(runtimeValue);
            return true;
        };

        for (const UiSettingsCommandDefinition& definition :
            UiSettingsCommandCatalog)
        {
            if (!IsSettingsSnapshotValue(definition))
                continue;

            const bool mayBeUnavailable =
                isSceneRule(definition.defaultValue) ||
                definition.defaultValue == "material-authored";
            if (definition.Supports(UiSettingsCommandVerb::Reset))
            {
                std::string resetValue;
                std::string resetError;
                const bool resetSucceeded = DispatchCommandValue(
                    definition,
                    CommandValueOperation::Reset,
                    {},
                    resetValue,
                    resetError);
                if (!resetSucceeded &&
                    !isExpectedNoChange(resetError) &&
                    !mayBeUnavailable)
                {
                    fail(std::string(definition.name) +
                        " RESET failed: " + resetError);
                }
            }

            const auto getValue = [&]()
            {
                std::string value;
                std::string error;
                const bool succeeded = DispatchCommandValue(
                    definition,
                    CommandValueOperation::Get,
                    {},
                    value,
                    error);
                return std::tuple<bool, std::string, std::string>{
                    succeeded, std::move(value), std::move(error)
                };
            };
            const auto [firstGetSucceeded, firstValue, firstError] =
                getValue();
            std::string firstCanonical;
            if (canonicalizeDefault(
                    definition,
                    firstGetSucceeded,
                    firstValue,
                    firstError,
                    firstCanonical) &&
                firstCanonical != definition.defaultValue)
            {
                fail(std::string(definition.name) +
                    " default mismatch: expected '" +
                    std::string(definition.defaultValue) + "', got '" +
                    firstCanonical + "'");
            }

            if (!definition.Supports(UiSettingsCommandVerb::Reset))
                continue;
            std::string secondResetValue;
            std::string secondResetError;
            const bool secondResetSucceeded = DispatchCommandValue(
                definition,
                CommandValueOperation::Reset,
                {},
                secondResetValue,
                secondResetError);
            if (!secondResetSucceeded &&
                !isExpectedNoChange(secondResetError) &&
                !mayBeUnavailable)
            {
                fail(std::string(definition.name) +
                    " second RESET failed: " + secondResetError);
            }
            const auto [secondGetSucceeded, secondValue, secondError] =
                getValue();
            std::string secondCanonical;
            canonicalizeDefault(
                definition,
                secondGetSucceeded,
                secondValue,
                secondError,
                secondCanonical);
            if (firstGetSucceeded != secondGetSucceeded ||
                firstValue != secondValue ||
                firstError != secondError ||
                firstCanonical != secondCanonical)
            {
                fail(std::string(definition.name) +
                    " was not stable after a second RESET");
            }
        }

        DecodedSettings serialized;
        for (const UiSettingsCommandDefinition& definition :
            UiSettingsCommandCatalog)
        {
            if (!IsSettingsSnapshotValue(definition))
                continue;
            std::string value;
            std::string error;
            if (!DispatchCommandValue(
                    definition,
                    CommandValueOperation::Get,
                    {},
                    value,
                    error))
            {
                value = "<unavailable>";
            }
            serialized.emplace(std::string(definition.name), std::move(value));
        }
        const std::string expectedCanonical =
            FormatCanonicalSettingsSnapshot(serialized);
        RefreshSettingsSnapshot();
        if (m_SettingsSnapshots.Canonical() != expectedCanonical)
            fail("snapshot serialization membership or values drifted");
        if (!IsSettingsSnapshotCode(m_SettingsSnapshots.Code()) ||
            BuildSettingsSnapshotCode(m_SettingsSnapshots.Canonical()) !=
                m_SettingsSnapshots.Code())
        {
            fail("snapshot code did not identify its canonical payload");
        }

        const std::string section =
            m_SettingsSnapshots.BuildCatalogSection();
        const wchar_t* temporaryDirectory = _wgetenv(L"TEMP");
        if (!temporaryDirectory || temporaryDirectory[0] == L'\0')
        {
            fail("TEMP is unavailable for the snapshot save round trip");
        }
        else
        {
            const std::filesystem::path path =
                std::filesystem::path(temporaryDirectory) /
                (L"uvsr-settings-contract-" +
                    std::to_wstring(GetCurrentProcessId()) + L".txt");
            {
                std::ofstream output(
                    path,
                    std::ios::binary | std::ios::trunc);
                output.write(section.data(),
                    static_cast<std::streamsize>(section.size()));
            }
            std::ifstream input(path, std::ios::binary);
            const std::string saved{
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()
            };
            input.close();
            std::error_code removeError;
            std::filesystem::remove(path, removeError);
            if (saved != section || removeError)
            {
                fail("snapshot save round trip failed");
            }
            else
            {
                const std::string opening =
                    "[" + m_SettingsSnapshots.Code() + "]\n";
                const std::string closing =
                    "[/" + m_SettingsSnapshots.Code() + "]\n";
                if (saved.size() < opening.size() + closing.size() ||
                    saved.compare(0u, opening.size(), opening) != 0 ||
                    saved.compare(
                        saved.size() - closing.size(),
                        closing.size(),
                        closing) != 0)
                {
                    fail("saved snapshot section framing was invalid");
                }
                else
                {
                    const std::string savedCanonical = saved.substr(
                        opening.size(),
                        saved.size() - opening.size() - closing.size());
                    if (BuildSettingsSnapshotCode(savedCanonical) !=
                        m_SettingsSnapshots.Code())
                    {
                        fail("saved snapshot payload failed its fingerprint");
                    }

                    std::size_t changedValueCount = 0u;
                    std::string applyError;
                    if (!ApplyCanonicalSettingsSnapshot(
                            savedCanonical,
                            changedValueCount,
                            applyError))
                    {
                        fail("saved snapshot transaction failed: " +
                            applyError);
                    }
                    else
                    {
                        if (changedValueCount != 0u)
                        {
                            fail("idempotent saved snapshot changed live "
                                "settings");
                        }
                        RefreshSettingsSnapshot();
                        if (m_SettingsSnapshots.Canonical() !=
                            savedCanonical)
                        {
                            fail("live settings differed after applying the "
                                "saved payload");
                        }
                    }
                }
            }
        }
        using SettingsValues =
            std::vector<std::pair<std::string, std::string>>;
        struct SettingsRoundTrip
        {
            std::string label;
            SettingsValues requested;
        };
        static constexpr std::array<std::string_view, 34>
            AoGiSettingNames = {
                "visibility.enabled",
                "visibility.quality",
                "visibility.estimator",
                "visibility.resolution",
                "visibility.samples",
                "visibility.radius",
                "visibility.thickness",
                "visibility.distribution",
                "visibility.specify-noise",
                "visibility.noise-pattern",
                "visibility.noise-resolution",
                "visibility.animate-samples",
                "visibility.ao.enabled",
                "visibility.ao.strength",
                "visibility.ao.output-hit-distance",
                "visibility.ao.precision",
                "visibility.gi.enabled",
                "visibility.gi.output-hit-distance",
                "visibility.gi.intensity",
                "visibility.gi.precision",
                "denoising.ao.method",
                "denoising.ao.radius",
                "denoising.ao.quality",
                "denoising.ao.resolution",
                "denoising.ao.history",
                "denoising.ao.disocclusion",
                "denoising.ao.anti-lag",
                "denoising.gi.method",
                "denoising.gi.radius",
                "denoising.gi.quality",
                "denoising.gi.resolution",
                "denoising.gi.history",
                "denoising.gi.disocclusion",
                "denoising.gi.anti-lag"
            };

        // These rows exercise the exact command dispatcher and snapshot
        // application path. Each row starts at canonical defaults, saves all
        // retained AO/GI values, resets them, applies that saved payload, and
        // compares the resulting live values. The matrix is intentionally
        // command-only; rendered correctness belongs to the DX12 diagnostic.
        std::vector<SettingsRoundTrip> roundTrips;
        const auto addRoundTrip = [&roundTrips]
        (
            std::string label,
            SettingsValues requested)
        {
            roundTrips.push_back({
                std::move(label), std::move(requested) });
        };
        addRoundTrip("representative-non-defaults", {
            { "visibility.quality", "custom" },
            { "visibility.estimator", "cosine-weighted" },
            { "visibility.resolution", "quarter" },
            { "visibility.samples", "64" },
            { "visibility.radius", "10" },
            { "visibility.thickness", "2" },
            { "visibility.distribution", "8" },
            { "visibility.specify-noise", "on" },
            { "visibility.noise-pattern", "spatial-white" },
            { "visibility.noise-resolution", "512x512" },
            { "visibility.animate-samples", "off" },
            { "visibility.ao.strength", "8" },
            { "visibility.ao.output-hit-distance", "on" },
            { "visibility.ao.precision", "32-bit" },
            { "visibility.gi.output-hit-distance", "on" },
            { "visibility.gi.intensity", "16" },
            { "visibility.gi.precision", "32-bit" },
            { "denoising.ao.method", "reblur" },
            { "denoising.ao.radius", "8" },
            { "denoising.ao.quality", "ultra" },
            { "denoising.ao.resolution", "full" },
            { "denoising.ao.history", "32" },
            { "denoising.ao.disocclusion", "0.1" },
            { "denoising.ao.anti-lag", "1" },
            { "denoising.gi.method", "relax" },
            { "denoising.gi.radius", "8" },
            { "denoising.gi.quality", "ultra" },
            { "denoising.gi.resolution", "full" },
            { "denoising.gi.history", "32" },
            { "denoising.gi.disocclusion", "0.1" },
            { "denoising.gi.anti-lag", "1" }
        });
        for (std::string_view ao : { "off", "on" })
        for (std::string_view gi : { "off", "on" })
        {
            addRoundTrip(
                "ao-gi-enable-" + std::string(ao) + "-" +
                    std::string(gi),
                {
                    { "visibility.ao.enabled", std::string(ao) },
                    { "visibility.gi.enabled", std::string(gi) }
                });
        }
        for (std::string_view quality :
            { "low", "medium", "high", "ultra" })
        {
            addRoundTrip(
                "visibility-preset-" + std::string(quality),
                {{ "visibility.quality", std::string(quality) }});
        }
        for (std::string_view estimator :
            { "projected-angle", "solid-angle", "cosine-weighted" })
        {
            addRoundTrip(
                "visibility-estimator-" + std::string(estimator),
                {
                    { "visibility.quality", "custom" },
                    { "visibility.estimator", std::string(estimator) }
                });
        }
        for (std::string_view resolution :
            { "full", "half", "quarter" })
        {
            addRoundTrip(
                "visibility-resolution-" + std::string(resolution),
                {
                    { "visibility.quality", "custom" },
                    { "visibility.resolution", std::string(resolution) }
                });
        }
        for (std::string_view samples :
            { "1", "2", "4", "8", "16", "32", "64" })
        {
            addRoundTrip(
                "visibility-samples-" + std::string(samples),
                {
                    { "visibility.quality", "custom" },
                    { "visibility.samples", std::string(samples) }
                });
        }
        for (std::string_view ao :
            { "raw", "joint-bilateral", "gaussian-bilateral", "reblur" })
        for (std::string_view gi :
            { "raw", "joint-bilateral", "gaussian-bilateral", "reblur",
                "relax" })
        {
            addRoundTrip(
                "ao-gi-method-" + std::string(ao) + "-" +
                    std::string(gi),
                {
                    { "denoising.ao.method", std::string(ao) },
                    { "denoising.gi.method", std::string(gi) }
                });
        }
        for (std::string_view ao :
            { "performance", "balanced", "quality", "ultra" })
        for (std::string_view gi :
            { "performance", "balanced", "quality", "ultra" })
        {
            addRoundTrip(
                "ao-gi-quality-" + std::string(ao) + "-" +
                    std::string(gi),
                {
                    { "denoising.ao.method", "reblur" },
                    { "denoising.gi.method", "relax" },
                    { "denoising.ao.quality", std::string(ao) },
                    { "denoising.gi.quality", std::string(gi) }
                });
        }
        for (std::string_view ao : { "quarter", "half", "full" })
        for (std::string_view gi : { "quarter", "half", "full" })
        {
            addRoundTrip(
                "ao-gi-resolution-" + std::string(ao) + "-" +
                    std::string(gi),
                {
                    { "denoising.ao.method", "reblur" },
                    { "denoising.gi.method", "relax" },
                    { "denoising.ao.resolution", std::string(ao) },
                    { "denoising.gi.resolution", std::string(gi) }
                });
        }
        for (std::string_view aoOutput : { "off", "on" })
        for (std::string_view giOutput : { "off", "on" })
        for (std::string_view aoPrecision : { "16-bit", "32-bit" })
        for (std::string_view giPrecision : { "16-bit", "32-bit" })
        {
            addRoundTrip(
                "ao-gi-hit-precision-" + std::string(aoOutput) + "-" +
                    std::string(giOutput) + "-" +
                    std::string(aoPrecision) + "-" +
                    std::string(giPrecision),
                {
                    { "visibility.ao.output-hit-distance",
                        std::string(aoOutput) },
                    { "visibility.gi.output-hit-distance",
                        std::string(giOutput) },
                    { "visibility.ao.precision",
                        std::string(aoPrecision) },
                    { "visibility.gi.precision",
                        std::string(giPrecision) }
                });
        }
        for (std::string_view bound : { "minimum", "maximum" })
        {
            const bool maximum = bound == "maximum";
            addRoundTrip(
                "ao-gi-continuous-" + std::string(bound),
                {
                    { "visibility.radius", maximum ? "10" : "0.1" },
                    { "visibility.thickness", maximum ? "2" : "0.01" },
                    { "visibility.distribution", maximum ? "8" : "0.25" },
                    { "visibility.ao.strength", maximum ? "8" : "0" },
                    { "visibility.gi.intensity", maximum ? "16" : "0" },
                    { "denoising.ao.radius", maximum ? "8" : "1" },
                    { "denoising.gi.radius", maximum ? "8" : "1" },
                    { "denoising.ao.history", maximum ? "32" : "1" },
                    { "denoising.gi.history", maximum ? "32" : "1" },
                    { "denoising.ao.disocclusion",
                        maximum ? "0.1" : "0.001" },
                    { "denoising.gi.disocclusion",
                        maximum ? "0.1" : "0.001" },
                    { "denoising.ao.anti-lag", maximum ? "1" : "0" },
                    { "denoising.gi.anti-lag", maximum ? "1" : "0" }
                });
        }

        const auto getSetting = [&]
        (
            std::string_view name,
            std::string& value)
        {
            const UiSettingsCommandDefinition* definition =
                FindSettingsCommandDefinition(name);
            std::string error;
            if (!definition || !DispatchCommandValue(
                    *definition,
                    CommandValueOperation::Get,
                    {},
                    value,
                    error))
            {
                fail("AO/GI matrix GET failed for " + std::string(name) +
                    ": " + error);
                return false;
            }
            return true;
        };
        const auto resetAoGiSettings = [&](std::string_view label)
        {
            bool succeeded = true;
            for (std::string_view name : AoGiSettingNames)
            {
                const UiSettingsCommandDefinition* definition =
                    FindSettingsCommandDefinition(name);
                std::string value;
                std::string error;
                if (!definition ||
                    !definition->Supports(UiSettingsCommandVerb::Reset) ||
                    (!DispatchCommandValue(
                        *definition,
                        CommandValueOperation::Reset,
                        {},
                        value,
                        error) && !isExpectedNoChange(error)))
                {
                    fail("AO/GI matrix " + std::string(label) +
                        " RESET failed for " + std::string(name) +
                        ": " + error);
                    succeeded = false;
                }
            }
            return succeeded;
        };

        size_t completedRoundTrips = 0u;
        for (const SettingsRoundTrip& roundTrip : roundTrips)
        {
            if (!resetAoGiSettings(roundTrip.label + " baseline"))
                continue;
            bool applied = true;
            for (const auto& [name, requestedValue] :
                roundTrip.requested)
            {
                std::string error;
                if (!ApplyRuntimeSetting(name, requestedValue, error))
                {
                    fail("AO/GI matrix " + roundTrip.label + " SET " +
                        name + "=" + requestedValue + " failed: " + error);
                    applied = false;
                }
            }
            if (!applied)
                continue;

            SettingsValues expected;
            for (std::string_view name : AoGiSettingNames)
            {
                std::string value;
                if (!getSetting(name, value))
                {
                    applied = false;
                    continue;
                }
                expected.emplace_back(std::string(name), value);
            }
            if (!applied ||
                expected.size() != AoGiSettingNames.size())
            {
                continue;
            }
            RefreshSettingsSnapshot();
            const std::string savedPayload =
                m_SettingsSnapshots.Canonical();
            const std::string savedCode =
                BuildSettingsSnapshotCode(savedPayload);
            if (!IsSettingsSnapshotCode(savedCode))
            {
                fail("AO/GI matrix " + roundTrip.label +
                    " produced an invalid snapshot fingerprint");
                continue;
            }

            if (!resetAoGiSettings(roundTrip.label + " reset"))
                continue;
            std::string applyError;
            std::size_t changedValueCount = 0u;
            if (!ApplyCanonicalSettingsSnapshot(
                    savedPayload,
                    changedValueCount,
                    applyError))
            {
                fail("AO/GI matrix " + roundTrip.label +
                    " snapshot apply failed: " + applyError);
                continue;
            }

            bool matched = true;
            for (const auto& [name, expectedValue] : expected)
            {
                std::string actualValue;
                if (!getSetting(name, actualValue))
                {
                    matched = false;
                    continue;
                }
                if (actualValue != expectedValue)
                {
                    fail("AO/GI matrix " + roundTrip.label + " live " +
                        name + " mismatch: expected '" + expectedValue +
                        "', got '" + actualValue + "'");
                    matched = false;
                }
            }
            RefreshSettingsSnapshot();
            for (const auto& [name, expectedValue] : expected)
            {
                const std::string canonicalLine =
                    FormatCanonicalSettingsSnapshot({
                        { std::string(name), expectedValue }
                    });
                if (m_SettingsSnapshots.Canonical().find(canonicalLine) ==
                    std::string::npos)
                {
                    fail("AO/GI matrix " + roundTrip.label +
                        " was absent from the live serialized snapshot: " +
                        name);
                    matched = false;
                }
            }
            if (!resetAoGiSettings(roundTrip.label + " final"))
                matched = false;
            if (matched)
                ++completedRoundTrips;
        }
        if (completedRoundTrips != roundTrips.size())
        {
            fail("AO/GI save-load-reset matrix completed " +
                std::to_string(completedRoundTrips) + " of " +
                std::to_string(roundTrips.size()) + " rows");
        }
        ResetAllSettingsToFactoryDefaults();
        RefreshSettingsSnapshot();
        if (m_SettingsSnapshots.Canonical() != expectedCanonical)
            fail("factory defaults drifted after the AO/GI round-trip matrix");

        if (!failures.empty())
        {
            for (const std::string& failure : failures)
                std::fprintf(stderr, "settings-contract: %s\n", failure.c_str());
            std::fprintf(stderr,
                "settings-contract: FAILED (%zu mismatches)\n",
                failures.size());
            return 1;
        }
        std::fprintf(stdout,
            "settings-contract: PASS (%zu persisted descriptors, "
            "%zu AO/GI round trips, %s)\n",
            serialized.size(),
            completedRoundTrips,
            m_SettingsSnapshots.Code().c_str());
        return 0;
    }
#endif
