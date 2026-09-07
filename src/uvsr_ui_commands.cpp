#include "uvsr_ui_internal.h"

#include <type_traits>

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

auto UIRenderer::ApplyLightingSolution(
        LightingSolution solution,
        bool invalidateHistory) -> void {
        const LightingSolutionTransition transition =
            ResolveLightingSolutionTransition(m_ui.Lighting, solution);
        if (!transition.accepted)
            return;
        m_ui.Lighting = transition.selection;
        if (transition.openPathTracingDrawer)
            m_PathingDrawerOpenRequested = true;
        if (transition.resetHistory && invalidateHistory)
            m_app->ResetImageBasedLightingHistory();
    }

auto UIRenderer::ResetAllSettingsToFactoryDefaults(
        std::string& resetError) -> bool {
        resetError.clear();
        if (m_app->IsSceneBusy())
        {
            resetError =
                "Factory settings cannot be restored while a scene is loading.";
            return false;
        }
        bool succeeded = true;
        const auto reportFailure = [&resetError, &succeeded](
            const UiSettingsCommandDefinition& definition,
            std::string_view error)
        {
            succeeded = false;
            const std::string message = std::string(definition.name) +
                ": " + std::string(error);
            if (resetError.empty())
                resetError = message;
            std::fprintf(stderr, "settings-reset: %s\n", message.c_str());
        };
        for (const UiSettingsCommandDefinition& definition :
            UiSettingsCommandCatalog)
        {
            if (definition.factoryReset ==
                    UiSettingsFactoryResetPolicy::Preserve ||
                !definition.Supports(UiSettingsCommandVerb::Reset))
            {
                continue;
            }
            const bool globalFlashlight = definition.factoryReset ==
                UiSettingsFactoryResetPolicy::GlobalFlashlight;
            if (!globalFlashlight && !IsSettingAvailable(definition.id))
                continue;
            UiSettingsValue defaultValue;
            std::string error;
            const bool globalFlashlightColor = globalFlashlight &&
                definition.id == SettingId::LightSelectedColor;
            const bool resolved = globalFlashlightColor
                ? GetDeclaredUiSettingsDefaultValue(definition, defaultValue)
                : ResolveSettingDefaultValue(definition, defaultValue, error);
            if (!resolved)
            {
                reportFailure(definition, error);
                continue;
            }
            if (!globalFlashlight)
            {
                if (!ApplySettingValue(
                        definition.id, defaultValue, error, true))
                {
                    reportFailure(definition, error);
                }
                continue;
            }
            if (globalFlashlightColor)
            {
                m_ui.Flashlight.colorLinearRed = defaultValue.vector[0];
                m_ui.Flashlight.colorLinearGreen = defaultValue.vector[1];
                m_ui.Flashlight.colorLinearBlue = defaultValue.vector[2];
                continue;
            }
            UiSettingsValue applied;
            if (!DispatchTypedSetting(
                    definition, &defaultValue, applied, error, false, true))
            {
                if (error.rfind("No change: ", 0u) != 0u)
                    reportFailure(definition, error);
                continue;
            }
        }
        m_app->ResetFactorySettingsRuntimeState();
        m_StatisticsEffect =
            static_cast<int>(StatisticsEffect::CompleteRenderer);
        m_PerformanceCollapsedRequest = true;
        m_PathingDrawerOpenRequested = false;
        ImGui::CloseUvsrColorPickerPopup();
        return succeeded;
    }

auto UIRenderer::RunAction(
        ActionId id,
        std::string& error) -> bool {
        error.clear();
        switch (id)
        {
        case ActionId::OpenSceneFolder:
        {
            const HINSTANCE result = ShellExecuteW(
                nullptr,
                L"open",
                m_app->GetSceneDir().c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL);
            if (reinterpret_cast<std::intptr_t>(result) > 32)
                return true;
            error = "Windows could not open the scene folder.";
            return false;
        }
        case ActionId::ResetSettings:
            return ResetAllSettingsToFactoryDefaults(error);
        case ActionId::Capture:
            m_ui.CopyScreenshotToClipboard = true;
            return true;
        case ActionId::Restart:
            g_RestartRequested = true;
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            return true;
        case ActionId::Invalid:
            break;
        }
        error = "Unknown settings action.";
        return false;
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

namespace
{
    template<typename Field>
    [[nodiscard]] bool BindTyped(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue* requested,
        Field& current,
        UiSettingsValue& result,
        std::string& error)
    {
        const auto read = [](const Field& field) {
            if constexpr (std::is_same_v<Field, bool>)
                return UiSettingsValue::Boolean(field);
            else if constexpr (std::is_integral_v<Field>)
                return UiSettingsValue::Integer(static_cast<std::int64_t>(field));
            else if constexpr (std::is_same_v<Field, float>)
                return UiSettingsValue::Float(field);
            else
            {
                static_assert(std::is_same_v<Field, float3>);
                return UiSettingsValue::Vector({ field.x, field.y, field.z, 0.f }, 3u);
            }
        };
        result = read(current);
        if (!requested)
            return true;
        if (requested->kind != result.kind ||
            (result.kind == UiSettingsValueKind::Vector && requested->componentCount != result.componentCount))
        {
            const char* shape = result.kind == UiSettingsValueKind::Boolean ? "a boolean value" :
                result.kind == UiSettingsValueKind::Integer ? "an integer value" :
                result.kind == UiSettingsValueKind::Float ? "a numeric value" :
                result.componentCount == 3u ? "a three component vector" : "a four component vector";
            error = std::string(definition.name) + " expects " + shape + ".";
            return false;
        }
        Field candidate;
        if constexpr (std::is_same_v<Field, bool>)
            candidate = requested->boolean;
        else if constexpr (std::is_integral_v<Field>)
            candidate = static_cast<Field>(requested->integer);
        else if constexpr (std::is_same_v<Field, float>)
            candidate = requested->scalar;
        else
            candidate = float3(requested->vector[0], requested->vector[1], requested->vector[2]);
        const UiSettingsValue next = read(candidate);
        if (next == result)
            return RejectUnchangedCommandMutation(definition.name, error);
        current = candidate;
        result = next;
        return true;
    }
    template<typename Enum, std::size_t Count>
    [[nodiscard]] bool BindTyped(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue* requested,
        Enum& current,
        const std::array<Enum, Count>& values,
        UiSettingsValue& result,
        std::string& error,
        bool allowSameValueMutation = false)
    {
        if (definition.typedDomain.tokenCount != Count)
        {
            error = std::string(definition.name) +
                " has a mismatched typed token binding.";
            return false;
        }
        std::size_t index = Count;
        if (requested)
        {
            if (requested->kind != UiSettingsValueKind::Token)
            {
                error = std::string(definition.name) +
                    " expects a token value.";
                return false;
            }
            for (std::size_t candidate = 0u; candidate < Count; ++candidate)
            {
                if (definition.typedDomain.tokens[candidate] == requested->text)
                {
                    index = candidate;
                    break;
                }
            }
            if (index == Count)
            {
                error = std::string(definition.name) +
                    " has an unknown token.";
                return false;
            }
            if (current == values[index] && !allowSameValueMutation)
                return RejectUnchangedCommandMutation(definition.name, error);
            current = values[index];
        }
        else
        {
            for (std::size_t candidate = 0u; candidate < Count; ++candidate)
            {
                if (values[candidate] == current)
                {
                    index = candidate;
                    break;
                }
            }
            if (index == Count)
            {
                error = std::string(definition.name) +
                    " has an unknown live token.";
                return false;
            }
        }
        result = UiSettingsValue::Token(
            std::string(definition.typedDomain.tokens[index]));
        return true;
    }
    template<typename State>
    [[nodiscard]] bool BindCatalogField(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue* requested,
        State& state,
        UiSettingsValue& value,
        std::string& error)
    {
        switch (definition.id)
        {
#define UVSR_SETTING(symbol, name, metadata)
#define UVSR_FIELD(symbol, name, metadata, owner, ...) \
        case SettingId::symbol: \
            if constexpr (std::is_same_v<State, owner>) \
                return BindTyped(definition, requested, state.__VA_ARGS__, value, error); \
            break;
#include "ui_settings_catalog.def"
#undef UVSR_FIELD
#undef UVSR_SETTING
        default: break;
        }
        return false;
    }
}

auto UIRenderer::DispatchTypedSetting(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue* requested,
        UiSettingsValue& value,
        std::string& error,
        bool allowLatentMutation,
        bool deferMutationEffects) -> bool {
        const auto bindUIData = [&] {
            return BindCatalogField(definition, requested, m_ui, value, error);
        };
        const auto bindFastApproximateAaSettings = [&] {
            return BindCatalogField(definition, requested, m_ui.AntiAliasing.fastApproximate, value, error);
        };
        const auto bindPathTracingSettings = [&] {
            auto candidate = m_ui.PathTracing;
            if (!BindCatalogField(definition, requested, candidate, value, error))
                return false;
            if (requested)
            {
                if (definition.id == SettingId::PathingMinimumBounces &&
                    candidate.minimumBounces > candidate.maximumBounces)
                {
                    error = "minimum bounces must not exceed maximum bounces";
                    return false;
                }
                candidate.minimumBounces = std::min(candidate.minimumBounces, candidate.maximumBounces);
                m_ui.PathTracing = candidate;
            }
            return true;
        };
        const auto bindFlashlightSettings = [&] {
            FlashlightSettings candidate = m_ui.Flashlight;
            const bool handled = BindCatalogField(definition, requested, candidate, value, error);
            if (handled && requested)
                m_ui.Flashlight = SanitizeFlashlightSettings(candidate);
            return handled;
        };
        const auto bindMaterial = [&] {
            const std::shared_ptr<Material> material = m_ui.SelectedMaterial;
            if (!material)
            {
                error = "No scene material is selected.";
                return false;
            }
            if (requested && !allowLatentMutation && !IsSettingAvailable(definition.id))
            {
                error = "setting is not available in the current context";
                return false;
            }
            Material candidate = *material;
            const bool handled = BindCatalogField(definition, requested, candidate, value, error);
            if (handled && requested)
                *material = candidate;
            return handled;
        };
        switch (definition.id)
        {
#define UVSR_SETTING(symbol, name, metadata)
#define UVSR_FIELD(symbol, name, metadata, owner, ...) \
        case SettingId::symbol: return bind##owner();
#include "ui_settings_catalog.def"
#undef UVSR_FIELD
#undef UVSR_SETTING
        case SettingId::TonemapperLut:
        {
            ToneMappingLut candidate = m_ui.ToneMapping.lut;
            if (!BindTyped(definition, requested, candidate,
                    std::array{ ToneMappingLut::None, ToneMappingLut::Print2383,
                        ToneMappingLut::Portra400, ToneMappingLut::Ektar100 }, value, error))
                return false;
            return !requested || m_app->SetToneMappingLut(candidate, error);
        }
        case SettingId::UiSettingsCollapsed:
        {
            bool candidate = m_SettingsCollapsedRequest.value_or(
                m_SettingsCollapsed);
            if (!BindTyped(
                    definition, requested, candidate, value, error))
            {
                return false;
            }
            if (requested)
            {
                m_SettingsCollapsedRequest = candidate;
                m_SettingsCollapsed = candidate;
            }
            return true;
        }
        case SettingId::MaterialEditorVisible:
        {
            bool candidate = m_ui.ShowMaterialDrawer;
            if (!BindTyped(
                    definition, requested, candidate, value, error))
            {
                return false;
            }
            if (requested)
                RequestMaterialDrawerVisible(candidate);
            return true;
        }
        case SettingId::LightingSolution:
        {
            LightingSolution candidate = m_ui.Lighting;
            if (!BindTyped(
                    definition, requested, candidate,
                    std::array{ LightingSolution::RayMarching,
                        LightingSolution::PathTracing }, value, error))
            {
                return false;
            }
            if (requested)
                ApplyLightingSolution(candidate, !deferMutationEffects);
            return true;
        }
        case SettingId::GpuAdapter:
        {
            if (!requested)
            {
                value = UiSettingsValue::Selector(
                    FormatSettingsSnapshotAdapterToken(
                        m_ui.ActiveGpuAdapterIndex));
                return true;
            }
            if (requested->kind != UiSettingsValueKind::Selector)
            {
                error = "gpu.adapter expects a selector value.";
                return false;
            }
            std::vector<SettingsSnapshotAdapterOption> options;
            options.reserve(m_ui.GpuAdapterChoices.size());
            for (const GpuAdapterChoice& adapter : m_ui.GpuAdapterChoices)
                options.push_back({ adapter.adapterIndex, adapter.name });
            std::int64_t requestedIndex = -1;
            std::string canonical;
            if (!ResolveSettingsSnapshotAdapterToken(
                    requested->text, options, requestedIndex,
                    canonical, error))
            {
                return false;
            }
            if (requestedIndex == m_ui.ActiveGpuAdapterIndex)
                return RejectUnchangedCommandMutation(definition.name, error);
            g_RestartAdapterIndex = static_cast<int>(requestedIndex);
            g_RestartRequested = true;
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(), GLFW_TRUE);
            value = UiSettingsValue::Selector(std::move(canonical));
            return true;
        }
        case SettingId::CameraMode:
        {
            CameraMode candidate = m_ui.Camera;
            if (!BindTyped(
                    definition, requested, candidate,
                    std::array{ CameraMode::ThirdPerson, CameraMode::Static },
                    value, error))
            {
                return false;
            }
            if (requested)
                m_app->SetCameraMode(candidate);
            return true;
        }
        case SettingId::SceneCurrent:
        {
            if (!requested)
            {
                const SceneCatalogEntry* scene = FindSceneCatalogEntry(
                    m_app->GetAvailableScenes(), m_app->GetCurrentSceneName());
                if (!scene)
                {
                    error = "scene.current is not a canonical catalog filename.";
                    return false;
                }
                value = UiSettingsValue::Selector(
                    FormatSettingsSnapshotSceneToken(MakeSceneDisplayName(
                        m_app->GetSceneDir(), scene->FileName)));
                return true;
            }
            if (requested->kind != UiSettingsValueKind::Selector)
            {
                error = "scene.current expects a selector value.";
                return false;
            }
            std::vector<SettingsSnapshotSceneOption> options;
            options.reserve(m_app->GetAvailableScenes().size());
            for (const SceneCatalogEntry& scene : m_app->GetAvailableScenes())
            {
                options.push_back({
                    FormatSettingsSnapshotSceneToken(MakeSceneDisplayName(
                        m_app->GetSceneDir(), scene.FileName)),
                    scene.DisplayName, scene.FileName });
            }
            std::string requestedFileName;
            std::string canonical;
            if (!ResolveSettingsSnapshotSceneToken(
                    requested->text, options, requestedFileName,
                    canonical, error))
            {
                return false;
            }
            if (requestedFileName == m_app->GetCurrentSceneName())
                return RejectUnchangedCommandMutation(definition.name, error);
            m_app->SetCurrentSceneName(requestedFileName);
            value = UiSettingsValue::Selector(std::move(canonical));
            return true;
        }
        case SettingId::AntiAliasingFxaaQuality:
        {
            AntiAliasingQuality quality =
                m_ui.AntiAliasing.fastApproximate.quality;
            const bool custom = !MatchesFastApproximateAaQualityPreset(
                m_ui.AntiAliasing.fastApproximate);
            if (!BindTyped(
                    definition, requested, quality,
                    std::array{ AntiAliasingQuality::Low,
                        AntiAliasingQuality::Medium,
                        AntiAliasingQuality::High,
                        AntiAliasingQuality::Ultra },
                    value, error, custom))
            {
                return false;
            }
            if (requested)
            {
                ApplyFastApproximateAaQualityPreset(
                    m_ui.AntiAliasing.fastApproximate, quality);
            }
            return true;
        }
        case SettingId::DebugWorldMaterials:
        {
            WhiteWorldMode mode = m_ui.WhiteWorld;
            if (!BindTyped(
                    definition, requested, mode,
                    std::array{ WhiteWorldMode::Off, WhiteWorldMode::On,
                        WhiteWorldMode::PreserveDetail,
                        WhiteWorldMode::PreserveLighting }, value, error))
            {
                return false;
            }
            if (requested)
                m_app->SetWhiteWorldMode(mode);
            return true;
        }
        case SettingId::DebugPbrFilter:
        {
            PbrLightingDebugView candidate = m_ui.LightingDebugView;
            if (!BindTyped(
                    definition, requested, candidate,
                    std::array{ PbrLightingDebugView::None,
                        PbrLightingDebugView::ShadingNormal,
                        PbrLightingDebugView::GeometricNormal,
                        PbrLightingDebugView::NormalDifference,
                        PbrLightingDebugView::DiffuseEnvironment,
                        PbrLightingDebugView::EnvironmentDirection,
                        PbrLightingDebugView::PrefilteredSpecularEnvironment,
                        PbrLightingDebugView::EnvironmentBrdf,
                        PbrLightingDebugView::FinalSpecularEnvironment,
                        PbrLightingDebugView::CombinedEnvironment,
                        PbrLightingDebugView::SpecularOcclusion,
                        PbrLightingDebugView::EnvironmentMip,
                        PbrLightingDebugView::SkyVisibility }, value, error))
            {
                return false;
            }
            m_ui.LightingDebugView = candidate;
            return true;
        }
        case SettingId::SkyEnvironment:
        {
            ImageBasedLightingSource candidate = m_ui.EnvironmentSource;
            if (!BindTyped(
                    definition, requested, candidate,
                    std::array{ ImageBasedLightingSource::Kloppenheim03Day,
                        ImageBasedLightingSource::SnowField2BrightOvercast,
                        ImageBasedLightingSource::FarmFieldSoftDay,
                        ImageBasedLightingSource::Kloppenheim07Night,
                        ImageBasedLightingSource::QwantaniStarryNight,
                        ImageBasedLightingSource::QuadrangleCloudy },
                    value, error))
            {
                return false;
            }
            if (requested)
            {
                m_ui.EnvironmentSource = candidate;
                m_ui.EnvironmentExposureStops =
                    GetImageBasedLightingSourceInfo(candidate)
                        .defaultExposureStops;
            }
            return true;
        }
        case SettingId::SkyVisibilityEnabled:
        case SettingId::SkyVisibilityDiffuseIbl:
        case SettingId::SkyVisibilitySpecularIbl:
        case SettingId::SkyVisibilitySamplesPerPixel:
        case SettingId::SkyVisibilitySpecifyNoise:
        case SettingId::SkyVisibilityNoisePattern:
        case SettingId::SkyVisibilityNoiseResolution:
        case SettingId::SkyVisibilityAnimateSamples:
        case SettingId::SkyVisibilityMaxDistance:
        case SettingId::SkyVisibilityRayBias:
        {
            RayTracedSkyVisibilitySettings candidate =
                m_ui.RayTracedSkyVisibility;
            bool handled = false;
            switch (definition.id)
            {
            case SettingId::SkyVisibilityEnabled:
                handled = BindTyped(
                    definition, requested, candidate.enabled, value, error);
                break;
            case SettingId::SkyVisibilityDiffuseIbl:
                handled = BindTyped(
                    definition, requested, candidate.applyToDiffuseIbl,
                    value, error);
                break;
            case SettingId::SkyVisibilitySpecularIbl:
                handled = BindTyped(
                    definition, requested, candidate.applyToSpecularIbl,
                    value, error);
                break;
            case SettingId::SkyVisibilitySamplesPerPixel:
                handled = BindTyped(
                    definition, requested, candidate.sampleRateLog2,
                    std::array<std::int32_t, 7>{ 0, 1, 2, 3, 4, 5, 6 },
                    value, error);
                break;
            case SettingId::SkyVisibilitySpecifyNoise:
                handled = BindTyped(
                    definition, requested, candidate.noise.specifyNoise,
                    value, error);
                break;
            case SettingId::SkyVisibilityNoisePattern:
                handled = BindTyped(
                    definition, requested, candidate.noise.custom.pattern,
                    std::array{ NoisePattern::SpatialWhite,
                        NoisePattern::SpatialBlue,
                        NoisePattern::SpatiotemporalBlue }, value, error);
                break;
            case SettingId::SkyVisibilityNoiseResolution:
                handled = BindTyped(
                    definition, requested, candidate.noise.custom.resolution,
                    std::array{ NoiseResolution::Size64,
                        NoiseResolution::Size128,
                        NoiseResolution::Size256,
                        NoiseResolution::Size512 }, value, error);
                break;
            case SettingId::SkyVisibilityAnimateSamples:
                handled = BindTyped(
                    definition, requested, candidate.noise.custom.animate,
                    value, error);
                break;
            case SettingId::SkyVisibilityMaxDistance:
                handled = BindTyped(
                    definition, requested, candidate.maxDistance,
                    std::array{ RayVisibilityMaxDistance::Maximum,
                        RayVisibilityMaxDistance::Meters32,
                        RayVisibilityMaxDistance::Meters16,
                        RayVisibilityMaxDistance::Meters8,
                        RayVisibilityMaxDistance::Meters4,
                        RayVisibilityMaxDistance::Meters2 }, value, error);
                break;
            case SettingId::SkyVisibilityRayBias:
                handled = BindTyped(
                    definition, requested, candidate.rayBias, value, error);
                break;
            default:
                break;
            }
            if (!handled || !requested)
                return handled;
            if (!IsRayTracedSkyVisibilityConfigurationSupported(candidate))
            {
                error = "The requested ray traced sky visibility configuration is not supported.";
                return false;
            }
            if (candidate.enabled && !m_app->SupportsRayTracedSkyVisibility())
            {
                error = "Ray-traced sky visibility requires DXR 1.1 support.";
                return false;
            }
            if (!IsValidNoiseSettings(candidate.noise.custom))
            {
                error = "The requested sky visibility noise configuration is invalid.";
                return false;
            }
            m_ui.RayTracedSkyVisibility = candidate;
            return true;
        }
        case SettingId::LightSelected:
        {
            const auto& lights = m_app->GetEditableLights();
            const std::shared_ptr<Light> selected = EnsureCommandSelectedLight();
            const auto indexOf = [&lights](const std::shared_ptr<Light>& light)
                -> std::optional<std::size_t>
            {
                const auto found = std::find(lights.begin(), lights.end(), light);
                if (found == lights.end())
                    return std::nullopt;
                return static_cast<std::size_t>(
                    std::distance(lights.begin(), found));
            };
            if (!requested)
            {
                const auto index = indexOf(selected);
                if (!selected || !index)
                {
                    error = "The current scene has no selected light.";
                    return false;
                }
                value = UiSettingsValue::Selector(
                    FormatSettingsSnapshotLightToken(
                        *index, selected->GetName()));
                return true;
            }
            if (requested->kind != UiSettingsValueKind::Selector)
            {
                error = "light.selected expects a selector value.";
                return false;
            }
            std::vector<SettingsSnapshotLightOption> options;
            options.reserve(lights.size());
            for (std::size_t index = 0u; index < lights.size(); ++index)
            {
                if (lights[index])
                    options.push_back({ index, lights[index]->GetName() });
            }
            std::size_t requestedIndex = 0u;
            std::string canonical;
            if (!ResolveSettingsSnapshotLightToken(
                    requested->text, options, requestedIndex,
                    canonical, error) || requestedIndex >= lights.size())
            {
                return false;
            }
            if (lights[requestedIndex] == selected)
                return RejectUnchangedCommandMutation(definition.name, error);
            m_SelectedLight = lights[requestedIndex];
            GetCommandLightDefaults(m_SelectedLight);
            value = UiSettingsValue::Selector(std::move(canonical));
            return true;
        }
        case SettingId::LightSelectedFlashlightEnabled:
        {
            bool candidate = m_ui.FlashlightEnabled;
            if (!BindTyped(
                    definition, requested, candidate, value, error))
            {
                return false;
            }
            if (requested)
                m_app->SetFlashlightEnabled(
                    candidate, !deferMutationEffects);
            return true;
        }
        case SettingId::LightSelectedAzimuth:
        case SettingId::LightSelectedElevation:
        case SettingId::LightSelectedColor:
        case SettingId::LightSelectedIrradiance:
        case SettingId::LightSelectedAngularSize:
        case SettingId::LightSelectedRadius:
        case SettingId::LightSelectedIntensity:
        case SettingId::LightSelectedInnerAngle:
        case SettingId::LightSelectedOuterAngle:
        {
            const std::shared_ptr<Light> selected = EnsureCommandSelectedLight();
            if (!selected)
            {
                error = "The current scene has no selected light.";
                return false;
            }
            if (m_app->IsFlashlight(selected))
            {
                if (definition.id != SettingId::LightSelectedColor)
                {
                    error = std::string(definition.name) +
                        " is not an editable generic flashlight property.";
                    return false;
                }
                float3 color(
                    m_ui.Flashlight.colorLinearRed,
                    m_ui.Flashlight.colorLinearGreen,
                    m_ui.Flashlight.colorLinearBlue);
                if (!BindTyped(
                        definition, requested, color, value, error))
                {
                    return false;
                }
                if (requested)
                {
                    m_ui.Flashlight.colorLinearRed = color.x;
                    m_ui.Flashlight.colorLinearGreen = color.y;
                    m_ui.Flashlight.colorLinearBlue = color.z;
                }
                return true;
            }
            const int type = selected->GetLightType();
            const bool directional = type == UVSR_LIGHT_TYPE_DIRECTIONAL;
            const bool spot = type == UVSR_LIGHT_TYPE_SPOT;
            const bool pointOrSpot = type == UVSR_LIGHT_TYPE_POINT || spot;
            if (definition.id == SettingId::LightSelectedAzimuth ||
                definition.id == SettingId::LightSelectedElevation)
            {
                if (!directional && !spot)
                {
                    error = std::string(definition.name) +
                        " requires a directional or spot light.";
                    return false;
                }
                auto [azimuth, elevation] = GetCommandLightAngles(
                    selected->GetDirection(), directional);
                float& component = definition.id == SettingId::LightSelectedAzimuth
                    ? azimuth : elevation;
                if (!BindTyped(
                        definition, requested, component, value, error))
                {
                    return false;
                }
                if (requested)
                {
                    selected->SetDirection(MakeCommandLightDirection(
                        azimuth, elevation, directional));
                }
                return true;
            }
            if (definition.id == SettingId::LightSelectedColor)
                return BindTyped(
                    definition, requested, selected->color, value, error);
            if (definition.id == SettingId::LightSelectedIrradiance ||
                definition.id == SettingId::LightSelectedAngularSize)
            {
                if (!directional)
                {
                    error = std::string(definition.name) +
                        " requires a directional light.";
                    return false;
                }
                auto& light = static_cast<DirectionalLight&>(*selected);
                float& property =
                    definition.id == SettingId::LightSelectedIrradiance
                    ? light.irradiance : light.angularSize;
                return BindTyped(
                    definition, requested, property, value, error);
            }
            if (definition.id == SettingId::LightSelectedRadius ||
                definition.id == SettingId::LightSelectedIntensity)
            {
                if (!pointOrSpot)
                {
                    error = std::string(definition.name) +
                        " requires a point or spot light.";
                    return false;
                }
                float* property = nullptr;
                if (type == UVSR_LIGHT_TYPE_POINT)
                {
                    auto& light = static_cast<PointLight&>(*selected);
                    property = definition.id == SettingId::LightSelectedRadius
                        ? &light.radius : &light.intensity;
                }
                else
                {
                    auto& light = static_cast<SpotLight&>(*selected);
                    property = definition.id == SettingId::LightSelectedRadius
                        ? &light.radius : &light.intensity;
                }
                return BindTyped(
                    definition, requested, *property, value, error);
            }
            if (!spot)
            {
                error = std::string(definition.name) +
                    " requires a spot light.";
                return false;
            }
            auto& light = static_cast<SpotLight&>(*selected);
            float candidate = definition.id == SettingId::LightSelectedInnerAngle
                ? light.innerAngle : light.outerAngle;
            if (!BindTyped(
                    definition, requested, candidate, value, error))
            {
                return false;
            }
            if (!requested)
                return true;
            if (definition.id == SettingId::LightSelectedInnerAngle)
            {
                if (candidate > light.outerAngle)
                {
                    error = "The inner spot angle cannot exceed the outer angle.";
                    return false;
                }
                light.innerAngle = candidate;
            }
            else
            {
                if (candidate < light.innerAngle)
                {
                    error = "The outer spot angle cannot be below the inner angle.";
                    return false;
                }
                light.outerAngle = candidate;
            }
            return true;
        }
        case SettingId::ShadowsRayTracedEnabled:
        case SettingId::ShadowsRayTracedMaxDistance:
        case SettingId::ShadowsRayTracedRayBias:
        {
            DirectionalShadowSettings candidate = m_ui.DirectionalShadows;
            bool handled = false;
            if (definition.id == SettingId::ShadowsRayTracedEnabled)
                handled = BindTyped(
                    definition, requested, candidate.enabled, value, error);
            else if (definition.id == SettingId::ShadowsRayTracedMaxDistance)
                handled = BindTyped(
                    definition, requested, candidate.maxDistance,
                    std::array{ RayVisibilityMaxDistance::Maximum,
                        RayVisibilityMaxDistance::Meters32,
                        RayVisibilityMaxDistance::Meters16,
                        RayVisibilityMaxDistance::Meters8,
                        RayVisibilityMaxDistance::Meters4,
                        RayVisibilityMaxDistance::Meters2 }, value, error);
            else
                handled = BindTyped(
                    definition, requested, candidate.rayBias, value, error);
            if (!handled || !requested)
                return handled;
            if (!IsDirectionalShadowSettingsValid(candidate))
            {
                error = "The requested directional shadow configuration is invalid.";
                return false;
            }
            if (candidate.enabled && !m_app->HasPrimaryDirectionalLight())
            {
                error = "Directional ray shadows require a primary directional light.";
                return false;
            }
            if (candidate.enabled && !m_app->SupportsDirectionalRayVisibility())
            {
                error = "Directional ray shadows require DXR 1.1 support.";
                return false;
            }
            m_ui.DirectionalShadows = candidate;
            return true;
        }
        case SettingId::MaterialSelected:
        {
            const std::shared_ptr<Scene> scene = m_app->GetScene();
            if (!scene || !scene->GetSceneGraph())
            {
                error = "No loaded scene provides material controls.";
                return false;
            }
            const auto& materials = scene->GetSceneGraph()->GetMaterials();
            if (!requested)
            {
                if (!m_ui.SelectedMaterial)
                {
                    value = UiSettingsValue::Selector(
                        FormatSettingsSnapshotMaterialToken(true));
                    return true;
                }
                if (m_ui.SelectedMaterial->materialID < 0)
                {
                    error = "The selected material has an invalid id.";
                    return false;
                }
                value = UiSettingsValue::Selector(
                    FormatSettingsSnapshotMaterialToken(
                        false, static_cast<std::uint32_t>(
                            m_ui.SelectedMaterial->materialID)));
                return true;
            }
            if (requested->kind != UiSettingsValueKind::Selector)
            {
                error = "material.selected expects a selector value.";
                return false;
            }
            std::vector<SettingsSnapshotMaterialOption> options;
            options.reserve(materials.size());
            for (const std::shared_ptr<Material>& material : materials)
            {
                if (material && material->materialID >= 0)
                {
                    options.push_back({
                        static_cast<std::uint32_t>(material->materialID),
                        material->name });
                }
            }
            bool none = false;
            std::uint32_t requestedId = 0u;
            std::string canonical;
            if (!ResolveSettingsSnapshotMaterialToken(
                    requested->text, options, none, requestedId,
                    canonical, error))
            {
                return false;
            }
            if (none)
            {
                if (!m_ui.SelectedMaterial)
                    return RejectUnchangedCommandMutation(definition.name, error);
                m_ui.SelectedMaterial.reset();
                m_ui.SelectedNode.reset();
                value = UiSettingsValue::Selector(std::move(canonical));
                return true;
            }
            auto match = std::find_if(
                materials.begin(), materials.end(),
                [requestedId](const std::shared_ptr<Material>& material)
                {
                    return material && material->materialID >= 0 &&
                        static_cast<std::uint32_t>(material->materialID) ==
                            requestedId;
                });
            if (match == materials.end())
            {
                error = "Resolved material is not owned by this scene.";
                return false;
            }
            if (*match == m_ui.SelectedMaterial)
                return RejectUnchangedCommandMutation(definition.name, error);
            m_ui.SelectedMaterial = *match;
            value = UiSettingsValue::Selector(std::move(canonical));
            return true;
        }
        default:
            break;
        }
        error = "typed setting operation is missing for '" +
            std::string(definition.name) + "'.";
        return false;
    }

auto UIRenderer::ApplySettingMutationEffects(
        const UiSettingsCommandDefinition& definition) -> void {
        const auto hasEffect = [&definition](UiSettingsMutationEffect effect)
        {
            return (static_cast<std::uint32_t>(definition.effects) &
                static_cast<std::uint32_t>(effect)) != 0u;
        };
        switch (definition.id)
        {
        case SettingId::LightingSolution:
        case SettingId::GpuAdapter:
        case SettingId::CameraMode:
        case SettingId::SceneCurrent:
            return;
        case SettingId::NoisePattern:
        case SettingId::NoiseResolution:
        case SettingId::NoiseAnimateSamples:
            m_app->ResetNoiseSamplingHistory(
                false,
                !m_ui.RayTracedSkyVisibility.noise.specifyNoise,
                true);
            return;
        case SettingId::NoiseAccumulateSamples:
            if (m_ui.Lighting == LightingSolution::RayMarching)
                m_app->ResetImageBasedLightingHistory();
            return;
        case SettingId::SkyVisibilitySpecifyNoise:
        case SettingId::SkyVisibilityNoisePattern:
        case SettingId::SkyVisibilityNoiseResolution:
        case SettingId::SkyVisibilityAnimateSamples:
            m_app->ResetNoiseSamplingHistory(false, true, false);
            return;
        case SettingId::AntiAliasingFxaaEdgeSharpness:
        case SettingId::AntiAliasingFxaaEdgeThreshold:
        case SettingId::AntiAliasingFxaaMinimumEdgeThreshold:
            m_ui.AntiAliasing.fastApproximate.edgeSharpness =
                ClampFastApproximateAaEdgeSharpness(
                    m_ui.AntiAliasing.fastApproximate.edgeSharpness);
            m_ui.AntiAliasing.fastApproximate.edgeThreshold =
                ClampFastApproximateAaEdgeThreshold(
                    m_ui.AntiAliasing.fastApproximate.edgeThreshold);
            m_ui.AntiAliasing.fastApproximate.darkEdgeThreshold =
                ClampFastApproximateAaDarkEdgeThreshold(
                    m_ui.AntiAliasing.fastApproximate.darkEdgeThreshold);
            break;
        default:
            break;
        }
        const bool notifiedMaterial =
            hasEffect(UiSettingsMutationEffect::Material) &&
            m_ui.SelectedMaterial;
        if (notifiedMaterial)
        {
            m_app->NotifyMaterialCommandChanged(m_ui.SelectedMaterial);
        }
        if (hasEffect(UiSettingsMutationEffect::RendererHistory) &&
            !notifiedMaterial)
        {
            m_app->ResetImageBasedLightingHistory();
        }
    }

auto UIRenderer::ResolveSettingDefaultValue(
        const UiSettingsCommandDefinition& definition,
        UiSettingsValue& value,
        std::string& error) -> bool {
        if (definition.id == SettingId::PathingMinimumBounces)
        {
            value = UiSettingsValue::Integer(std::min(
                DefaultPathTracingSettings.minimumBounces, m_ui.PathTracing.maximumBounces));
            return true;
        }
        switch (definition.typedDefault.policy)
        {
        case UiSettingsDefaultPolicy::Literal:
        case UiSettingsDefaultPolicy::RetainedBistro:
        case UiSettingsDefaultPolicy::FlashlightDefault:
        case UiSettingsDefaultPolicy::NoMaterial:
            if (GetDeclaredUiSettingsDefaultValue(definition, value))
                return true;
            break;
        case UiSettingsDefaultPolicy::EnvironmentExposure:
            value = UiSettingsValue::Float(
                GetImageBasedLightingSourceInfo(m_ui.EnvironmentSource)
                    .defaultExposureStops);
            return true;
        case UiSettingsDefaultPolicy::FxaaQualityProfile:
        {
            FastApproximateAaSettings preset;
            ApplyFastApproximateAaQualityPreset(
                preset, m_ui.AntiAliasing.fastApproximate.quality);
            return BindCatalogField(definition, nullptr, preset, value, error);
        }
        case UiSettingsDefaultPolicy::SceneDefaultLight:
        {
            const auto& lights = m_app->GetEditableLights();
            const std::shared_ptr<Light> selected = GetDefaultCommandLight();
            const auto found = std::find(lights.begin(), lights.end(), selected);
            if (selected && found != lights.end())
            {
                value = UiSettingsValue::Selector(
                    FormatSettingsSnapshotLightToken(
                        static_cast<std::size_t>(
                            std::distance(lights.begin(), found)),
                        selected->GetName()));
                return true;
            }
            break;
        }
        case UiSettingsDefaultPolicy::SceneAuthored:
        case UiSettingsDefaultPolicy::SelectedLightColor:
        {
            const std::shared_ptr<Light> selected = EnsureCommandSelectedLight();
            if (!selected)
                break;
            if (definition.typedDefault.policy ==
                    UiSettingsDefaultPolicy::SelectedLightColor &&
                m_app->IsFlashlight(selected))
            {
                const FlashlightSettings& flashlight =
                    DefaultFlashlightSettings;
                value = UiSettingsValue::Vector({
                    flashlight.colorLinearRed,
                    flashlight.colorLinearGreen,
                    flashlight.colorLinearBlue,
                    0.f }, 3u);
                return true;
            }
            const LightDefaultState& defaults =
                GetCommandLightDefaults(selected);
            switch (definition.id)
            {
            case SettingId::LightSelectedAzimuth:
            case SettingId::LightSelectedElevation:
            {
                const bool directional = selected->GetLightType() ==
                    UVSR_LIGHT_TYPE_DIRECTIONAL;
                const auto angles = GetCommandLightAngles(
                    defaults.direction, directional);
                value = UiSettingsValue::Float(
                    definition.id == SettingId::LightSelectedAzimuth
                        ? angles.first : angles.second);
                return true;
            }
            case SettingId::LightSelectedColor:
                value = UiSettingsValue::Vector(
                    { defaults.color.x, defaults.color.y, defaults.color.z,
                        0.f }, 3u);
                return true;
            case SettingId::LightSelectedIrradiance:
                value = UiSettingsValue::Float(defaults.irradiance); return true;
            case SettingId::LightSelectedAngularSize:
                value = UiSettingsValue::Float(defaults.angularSize); return true;
            case SettingId::LightSelectedRadius:
                value = UiSettingsValue::Float(defaults.radius); return true;
            case SettingId::LightSelectedIntensity:
                value = UiSettingsValue::Float(defaults.intensity); return true;
            case SettingId::LightSelectedInnerAngle:
                value = UiSettingsValue::Float(defaults.innerAngle); return true;
            case SettingId::LightSelectedOuterAngle:
                value = UiSettingsValue::Float(defaults.outerAngle); return true;
            default:
                break;
            }
            break;
        }
        case UiSettingsDefaultPolicy::MaterialAuthored:
        {
            const std::shared_ptr<Material> material = m_ui.SelectedMaterial;
            const Material* original = material
                ? m_app->GetOriginalMaterial(material)
                : nullptr;
            if (!original)
                break;
            Material defaults = *original;
            return BindCatalogField(definition, nullptr, defaults, value, error);
        }
        case UiSettingsDefaultPolicy::HighestMemoryAdapter:
            break;
        }
        error = "No contextual default is available for '" +
            std::string(definition.name) + "'.";
        return false;
    }

auto UIRenderer::ReadSettingValue(
        SettingId id,
        std::string& value,
        std::string& error) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition || definition->kind == UiSettingsCommandKind::Action)
        {
            error = "unknown setting id";
            return false;
        }
        UiSettingsValue typed;
        return ReadSettingValue(id, typed, error) &&
            FormatUiSettingsValue(*definition, typed, value, error);
    }

auto UIRenderer::ReadSettingValue(
        SettingId id,
        UiSettingsValue& value,
        std::string& error) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition || definition->kind == UiSettingsCommandKind::Action)
        {
            error = "unknown setting id";
            return false;
        }
        if (!IsSettingAvailable(id))
        {
            error = "setting is not available in the current context";
            return false;
        }
        return DispatchTypedSetting(*definition, nullptr, value, error);
    }

auto UIRenderer::ApplySettingValue(
        SettingId id,
        std::string_view canonicalValue,
        std::string& error) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition || definition->kind == UiSettingsCommandKind::Action ||
            !definition->Supports(UiSettingsCommandVerb::Set))
        {
            error = "setting is not mutable";
            return false;
        }
        SettingsSnapshotValidationContext context;
        if (id == SettingId::MaterialSelectedOpacity && m_ui.SelectedMaterial)
        {
            context.hasMaterialBaseTexture = true;
            context.materialHasBaseTexture = static_cast<bool>(
                m_ui.SelectedMaterial->baseOrDiffuseTexture);
        }
        UiSettingsValue typed;
        if (!ParseCanonicalUiSettingsValue(
                *definition, canonicalValue, typed, error, context))
        {
            return false;
        }
        return ApplySettingValue(id, typed, error);
    }

auto UIRenderer::ApplySettingValue(
        SettingId id,
        const UiSettingsValue& requested,
        std::string& error,
        bool deferMutationEffects) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition || definition->kind == UiSettingsCommandKind::Action ||
            !definition->Supports(UiSettingsCommandVerb::Set))
        {
            error = "setting is not mutable";
            return false;
        }
        if (!CheckCommandMutationAllowed(*definition, error) ||
            !IsSettingAvailable(id))
        {
            if (error.empty())
                error = "setting is not available in the current context";
            return false;
        }
        SettingsSnapshotValidationContext context;
        if (id == SettingId::MaterialSelectedOpacity && m_ui.SelectedMaterial)
        {
            context.hasMaterialBaseTexture = true;
            context.materialHasBaseTexture = static_cast<bool>(
                m_ui.SelectedMaterial->baseOrDiffuseTexture);
        }
        if (definition->kind != UiSettingsCommandKind::DynamicSelection &&
            !ValidateUiSettingsValue(*definition, requested, error, context))
        {
            return false;
        }
        UiSettingsValue applied;
        const bool succeeded = DispatchTypedSetting(
            *definition, &requested, applied, error,
            false, deferMutationEffects);
        if (!succeeded && error.rfind("No change: ", 0u) == 0u)
        {
            error.clear();
            return true;
        }
        if (succeeded && !deferMutationEffects)
            ApplySettingMutationEffects(*definition);
        return succeeded;
    }

auto UIRenderer::ResetSettingValue(
        SettingId id,
        std::string& error) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition || definition->kind == UiSettingsCommandKind::Action ||
            !definition->Supports(UiSettingsCommandVerb::Reset))
        {
            error = "setting has no reset operation";
            return false;
        }
        if (!CheckCommandMutationAllowed(*definition, error))
            return false;
        UiSettingsValue defaultValue;
        if (!ResolveSettingDefaultValue(*definition, defaultValue, error))
            return false;
        return ApplySettingValue(id, defaultValue, error);
    }

auto UIRenderer::IsSettingAvailable(SettingId id) const -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition)
            return false;
        const std::shared_ptr<Light>& light = m_SelectedLight;
        const std::shared_ptr<Material>& material = m_ui.SelectedMaterial;
        switch (definition->availability)
        {
        case UiSettingsAvailability::Always:
            return true;
        case UiSettingsAvailability::SelectedLight:
            return static_cast<bool>(light);
        case UiSettingsAvailability::DirectionalOrSpotLight:
            return light && !m_app->IsFlashlight(light) &&
                (light->GetLightType() == LightType_Directional ||
                 light->GetLightType() == LightType_Spot);
        case UiSettingsAvailability::DirectionalLight:
            return light && light->GetLightType() == LightType_Directional;
        case UiSettingsAvailability::PointOrSpotLight:
            return light && !m_app->IsFlashlight(light) &&
                (light->GetLightType() == LightType_Point ||
                 light->GetLightType() == LightType_Spot);
        case UiSettingsAvailability::SpotLight:
            return light && !m_app->IsFlashlight(light) &&
                light->GetLightType() == LightType_Spot;
        case UiSettingsAvailability::SelectedFlashlight:
            return light && m_app->IsFlashlight(light);
        case UiSettingsAvailability::SelectedMaterial:
            return static_cast<bool>(material);
        case UiSettingsAvailability::BaseTexture:
            return material && material->baseOrDiffuseTexture;
        case UiSettingsAvailability::MetalSpecularTexture:
            return material && material->metalRoughOrSpecularTexture;
        case UiSettingsAvailability::SpecularGlossMaterial:
            return material && material->useSpecularGlossModel;
        case UiSettingsAvailability::MetalRoughMaterial:
            return material && !material->useSpecularGlossModel;
        case UiSettingsAvailability::AlphaBlendedMaterial:
            return material && IsCommandMaterialAlphaBlended(material->domain);
        case UiSettingsAvailability::AlphaTestedMaterial:
            return material && IsCommandMaterialAlphaTested(material->domain) &&
                material->baseOrDiffuseTexture;
        case UiSettingsAvailability::NormalTexture:
            return material && material->normalTexture;
        case UiSettingsAvailability::OcclusionTexture:
            return material && material->occlusionTexture;
        case UiSettingsAvailability::EmissiveTexture:
            return material && material->emissiveTexture;
        case UiSettingsAvailability::TransmissiveMaterial:
            return material && IsCommandMaterialTransmissive(material->domain);
        case UiSettingsAvailability::TransmissionTexture:
            return material && IsCommandMaterialTransmissive(material->domain) &&
                material->transmissionTexture;
        case UiSettingsAvailability::OpacityTexture:
            return material && material->opacityTexture;
        }
        return false;
    }

auto UIRenderer::IsSettingAtContextualDefault(
        SettingId id,
        std::string& error) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition || !definition->Supports(UiSettingsCommandVerb::Reset))
        {
            error = "setting has no contextual reset default";
            return false;
        }
        if (!IsSettingAvailable(id))
        {
            error = "setting is not available in the current context";
            return false;
        }

        UiSettingsValue current;
        UiSettingsValue expected;
        if (!ReadSettingValue(id, current, error) ||
            !ResolveSettingDefaultValue(*definition, expected, error))
        {
            return false;
        }
        return current == expected;
    }

auto UIRenderer::MakeSettingsSnapshotRuntimeAccess() -> SettingsSnapshotRuntimeAccess {
        SettingsSnapshotRuntimeAccess access;
        access.sceneReady =
            !m_app->IsSceneBusy() && m_app->IsSceneLoaded();
        access.validateValue =
            [this](SettingId id,
               std::string_view requestedValue,
               std::string_view dependencySelector,
               std::string& validationError)
            {
                const UiSettingsCommandDefinition* definition =
                    FindSettingsCommandDefinition(id);
                if (!definition ||
                    !IsSettingsSnapshotValue(*definition))
                {
                    validationError =
                        "setting is absent from the live SnapshotCatalog";
                    return false;
                }

                SettingsSnapshotValidationContext context;
                if (id == SettingId::MaterialSelectedOpacity)
                {
                    std::shared_ptr<Material> target = m_ui.SelectedMaterial;
                    if (!dependencySelector.empty())
                    {
                        target.reset();
                        const std::shared_ptr<Scene> scene = m_app->GetScene();
                        if (scene && scene->GetSceneGraph())
                        {
                            for (const std::shared_ptr<Material>& material :
                                scene->GetSceneGraph()->GetMaterials())
                            {
                                if (material && material->materialID >= 0 &&
                                    FormatSettingsSnapshotMaterialToken(false,
                                        static_cast<std::uint32_t>(material->materialID)) ==
                                        dependencySelector)
                                {
                                    target = material;
                                    break;
                                }
                            }
                        }
                        if (!target && dependencySelector != "none")
                        {
                            // A different scene's material is validated again after selection.
                            context.hasMaterialBaseTexture = true;
                            context.materialHasBaseTexture = true;
                        }
                    }
                    if (target)
                    {
                        context.hasMaterialBaseTexture = true;
                        context.materialHasBaseTexture =
                            static_cast<bool>(target->baseOrDiffuseTexture);
                    }
                }
                return ValidateSettingsSnapshotCatalogValue(
                    *definition,
                    requestedValue,
                    validationError,
                    context);
            };

        const auto readSnapshotValue =
            [this](SettingId id,
                   bool raw,
                   std::string& liveValue,
                   std::string& readError)
            {
                const UiSettingsCommandDefinition* definition =
                    FindSettingsCommandDefinition(id);
                if (!definition ||
                    !IsSettingsSnapshotValue(*definition))
                {
                    readError =
                        "setting is absent from the live SnapshotCatalog";
                    return false;
                }

                const bool latentRead = raw ||
                    definition->snapshotRead ==
                        UiSettingsSnapshotReadPolicy::LatentValue;
                if (!latentRead)
                {
                    if (ReadSettingValue(id, liveValue, readError))
                        return true;
                    if (!IsSettingAvailable(id))
                    {
                        liveValue = "<unavailable>";
                        readError.clear();
                        return true;
                    }
                    return false;
                }
                if (raw &&
                    definition->storage ==
                        UiSettingsStoragePolicy::ContextOnly &&
                    !IsSettingAvailable(id))
                {
                    liveValue = "<unavailable>";
                    readError.clear();
                    return true;
                }

                UiSettingsValue typed;
                if (DispatchTypedSetting(
                        *definition,
                        nullptr,
                        typed,
                        readError,
                        latentRead) &&
                    FormatUiSettingsValue(
                        *definition,
                        typed,
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
        access.readValue =
            [readSnapshotValue](SettingId id,
               std::string& liveValue,
               std::string& readError)
            {
                return readSnapshotValue(
                    id, false, liveValue, readError);
            };
        access.readRawValue =
            [readSnapshotValue](SettingId id,
               std::string& liveValue,
               std::string& readError)
            {
                return readSnapshotValue(
                    id, true, liveValue, readError);
            };
        access.writeValue =
            [this](SettingId id,
                   std::string_view requestedValue,
                   std::string& writeError)
            {
                const UiSettingsCommandDefinition* definition =
                    FindSettingsCommandDefinition(id);
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
                if (definition->storage != UiSettingsStoragePolicy::Latent)
                    return ApplySettingValue(id, requestedValue, writeError);
                if (!CheckCommandMutationAllowed(*definition, writeError))
                    return false;

                SettingsSnapshotValidationContext context;
                if (id == SettingId::MaterialSelectedOpacity &&
                    m_ui.SelectedMaterial)
                {
                    context.hasMaterialBaseTexture = true;
                    context.materialHasBaseTexture = static_cast<bool>(
                        m_ui.SelectedMaterial->baseOrDiffuseTexture);
                }
                UiSettingsValue typed;
                if (!ParseCanonicalUiSettingsValue(
                        *definition,
                        requestedValue,
                        typed,
                        writeError,
                        context))
                {
                    return false;
                }
                UiSettingsValue applied;
                const bool succeeded = DispatchTypedSetting(
                    *definition,
                    &typed,
                    applied,
                    writeError,
                    true);
                if (!succeeded &&
                    writeError.rfind("No change: ", 0u) == 0u)
                {
                    writeError.clear();
                    return true;
                }
                if (succeeded)
                    ApplySettingMutationEffects(*definition);
                return succeeded;
            };
        access.driveSelector =
            [this](SettingId id,
                   std::string_view canonicalToken,
                   bool begin,
                   bool rollback,
                   std::string& selectorError)
            {
                const auto ready = [&]
                {
                    std::string current;
                    if (ReadSettingValue(id, current, selectorError) && current == canonicalToken)
                        return SettingsSnapshotSelectorTransition::Ready;
                    if (selectorError.empty())
                        selectorError = id == SettingId::SceneCurrent
                            ? "loaded scene did not publish the requested canonical token"
                            : "selector did not publish its canonical token";
                    return SettingsSnapshotSelectorTransition::Failed;
                };
                if (!begin)
                {
                    if (id != SettingId::SceneCurrent)
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
                    return ready();
                }

                try
                {
                    if (!ApplySettingValue(
                            id, canonicalToken, selectorError))
                    {
                        return SettingsSnapshotSelectorTransition::Failed;
                    }
                    if (id != SettingId::SceneCurrent)
                        return ready();
                }
                catch (const std::exception& exception)
                {
                    selectorError = std::string(
                        rollback ? "selector rollback threw: " :
                            "selector apply threw: ") + exception.what();
                    return SettingsSnapshotSelectorTransition::Failed;
                }
                return SettingsSnapshotSelectorTransition::Pending;
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

auto UIRenderer::FailStartupSettingsSnapshot(
        std::string_view code,
        std::string_view error) -> void {
        g_RestartRequested = false;
        g_RestartAdapterIndex = -1;
        g_StartupSettingsSnapshotFailed = true;
        uvsr::log::error(
            "Startup settings snapshot %s failed: %s",
            code.empty() ? "<none>" : std::string(code).c_str(),
            error.empty() ? "settings snapshot transaction failed" :
                std::string(error).c_str());
        glfwSetWindowShouldClose(
            GetDeviceManager()->GetWindow(), GLFW_TRUE);
    }

auto UIRenderer::HandleStagedSettingsSnapshotStep(
        const SettingsSnapshotTransactionStep& step) -> void {
        if (step.progress == SettingsSnapshotTransactionProgress::Pending)
            return;
        const std::string code = m_PendingSettingsSnapshotCode;
        const bool failed =
            step.progress == SettingsSnapshotTransactionProgress::Failed;
        const std::string terminalError = step.result.error.empty()
            ? "settings snapshot transaction failed"
            : step.result.error;
        m_PendingSettingsSnapshotCode.clear();
        if (failed)
        {
            FailStartupSettingsSnapshot(code, terminalError);
            return;
        }

        uvsr::log::info(
            "Loaded startup settings snapshot %s (%zu values changed)",
            code.c_str(), step.result.changedValueCount);
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

        if (m_StartupSettingsSnapshotCode.empty() ||
            m_StartupSettingsSnapshotAttempted)
        {
            return;
        }

        m_StartupSettingsSnapshotAttempted = true;
        m_PendingSettingsSnapshotCode = m_StartupSettingsSnapshotCode;
        HandleStagedSettingsSnapshotStep(
            m_SettingsSnapshots.BeginLoadCodeStaged(
                m_StartupSettingsSnapshotCode,
                MakeSettingsSnapshotRuntimeAccess()));
    }

#if defined(UVSR_BUILD_TESTING)
auto UIRenderer::VerifyCanonicalSettingsContract() -> int {
        std::vector<std::string> failures;
        const auto fail = [&](std::string message) { failures.push_back(std::move(message)); };
        if (m_app->IsSceneBusy() || !m_app->IsSceneLoaded())
            fail("default scene did not finish loading");
        const auto isExpectedNoChange = [](std::string_view error) {
            return error.rfind("No change: ", 0u) == 0u;
        };
        const auto checkDefault = [&](const UiSettingsCommandDefinition& definition,
            bool read, const std::string& value, const std::string& error) {
            bool matches = false;
            switch (definition.typedDefault.policy)
            {
            case UiSettingsDefaultPolicy::HighestMemoryAdapter:
            {
                const auto* active = GetActiveGpuAdapterChoice();
                matches = read && active && std::all_of(m_ui.GpuAdapterChoices.begin(),
                    m_ui.GpuAdapterChoices.end(), [active](const auto& adapter) {
                        return adapter.dedicatedVideoMemory <= active->dedicatedVideoMemory;
                    });
                break;
            }
            case UiSettingsDefaultPolicy::RetainedBistro:
                matches = read && NormalizeCommandAscii(m_app->GetCurrentSceneName(), true)
                    .find("bistrointeriorretextured") != std::string::npos;
                break;
            case UiSettingsDefaultPolicy::SceneDefaultLight:
            {
                const auto selected = EnsureCommandSelectedLight();
                const auto& lights = m_app->GetEditableLights();
                const auto position = std::find(lights.begin(), lights.end(), selected);
                matches = read && selected && selected == GetDefaultCommandLight() &&
                    position != lights.end() && value == FormatSettingsSnapshotLightToken(
                        static_cast<size_t>(std::distance(lights.begin(), position)), selected->GetName());
                break;
            }
            case UiSettingsDefaultPolicy::SceneAuthored:
            case UiSettingsDefaultPolicy::SelectedLightColor:
                matches = EnsureCommandSelectedLight() == GetDefaultCommandLight();
                break;
            case UiSettingsDefaultPolicy::NoMaterial:
                matches = read && !m_ui.SelectedMaterial &&
                    value == FormatUiSettingsDefaultAnchor(definition);
                break;
            case UiSettingsDefaultPolicy::MaterialAuthored:
                matches = !m_ui.SelectedMaterial;
                break;
            case UiSettingsDefaultPolicy::Literal:
                matches = read && value == FormatUiSettingsDefaultAnchor(definition);
                break;
            case UiSettingsDefaultPolicy::EnvironmentExposure:
            case UiSettingsDefaultPolicy::FxaaQualityProfile:
            case UiSettingsDefaultPolicy::FlashlightDefault:
                matches = !IsSettingAvailable(definition.id) ||
                    (read && value == FormatUiSettingsDefaultAnchor(definition));
                break;
            }
            if (!matches)
                fail(std::string(definition.name) + " default mismatch: " + value + " / " + error);
        };
        for (const auto& definition : UiSettingsCommandCatalog)
        {
            if (!IsSettingsSnapshotValue(definition))
                continue;
            const auto policy = definition.typedDefault.policy;
            const bool mayBeUnavailable = policy == UiSettingsDefaultPolicy::SceneAuthored ||
                policy == UiSettingsDefaultPolicy::SelectedLightColor ||
                policy == UiSettingsDefaultPolicy::FlashlightDefault ||
                policy == UiSettingsDefaultPolicy::MaterialAuthored;
            const bool resettable = definition.Supports(UiSettingsCommandVerb::Reset);
            std::tuple<bool, std::string, std::string> first;
            for (unsigned attempt = 0u; attempt < (resettable ? 2u : 1u); ++attempt)
            {
                std::string error;
                if (resettable && !ResetSettingValue(definition.id, error) &&
                    !isExpectedNoChange(error) && !mayBeUnavailable)
                    fail(std::string(definition.name) + " RESET failed: " + error);
                std::string value;
                error.clear();
                const bool read = ReadSettingValue(definition.id, value, error);
                checkDefault(definition, read, value, error);
                const auto current = std::make_tuple(read, value, error);
                if (attempt == 0u)
                    first = current;
                else if (first != current)
                    fail(std::string(definition.name) + " was not stable after a second RESET");
            }
        }

        DecodedSettings serialized;
        for (const auto& definition : UiSettingsCommandCatalog)
        {
            if (!IsSettingsSnapshotValue(definition))
                continue;
            std::string value;
            std::string error;
            if (!ReadSettingValue(definition.id, value, error))
                value = "<unavailable>";
            serialized.emplace(definition.name, std::move(value));
        }
        const std::string expectedCanonical = FormatCanonicalSettingsSnapshot(serialized);
        RefreshSettingsSnapshot();
        if (m_SettingsSnapshots.Canonical() != expectedCanonical)
            fail("snapshot serialization membership or values drifted");
        if (!IsSettingsSnapshotCode(m_SettingsSnapshots.Code()) ||
            BuildSettingsSnapshotCode(expectedCanonical) != m_SettingsSnapshots.Code())
            fail("snapshot code did not identify its canonical payload");
        const wchar_t* temporaryDirectory = _wgetenv(L"TEMP");
        if (!temporaryDirectory || temporaryDirectory[0] == L'\0')
            fail("TEMP is unavailable for the snapshot save round trip");
        else
        {
            const auto path = std::filesystem::path(temporaryDirectory) /
                (L"uvsr-settings-contract-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
            if (!m_SettingsSnapshots.Persist(path))
                fail("snapshot save failed");
            const auto saved = ReadMatchingSettingsSnapshots(path, m_SettingsSnapshots.Code());
            std::error_code removeError;
            std::filesystem::remove(path, removeError);
            if (saved != std::vector<std::string>{ expectedCanonical } || removeError)
                fail("saved snapshot did not decode to its exact canonical payload");
            else
            {
                const auto applied = m_SettingsSnapshots.BeginApplyCanonicalStaged(
                    saved.front(), MakeSettingsSnapshotRuntimeAccess());
                RefreshSettingsSnapshot();
                if (applied.progress != SettingsSnapshotTransactionProgress::Succeeded ||
                    applied.result.changedValueCount != 0u || m_SettingsSnapshots.Canonical() != saved.front())
                    fail("saved snapshot was not an idempotent live transaction: " + applied.result.error);
            }
        }

        const auto tokenValue = [](SettingId id, std::size_t index) {
            return UiSettingsValue::Token(std::string(FindSettingsCommandDefinition(id)->typedDomain.tokens[index]));
        };
        const auto applySentinel = [&]
        (
            SettingId id,
            UiSettingsValue value,
            bool allowNoChange = false
        )
        {
            std::string error;
            if (ApplySettingValue(id, value, error) ||
                allowNoChange && isExpectedNoChange(error))
            {
                return true;
            }
            fail("Reset All sentinel SET failed for " +
                std::string(SettingName(id)) + ": " + error);
            return false;
        };
        const auto readSentinel = [&]
        (
            SettingId id,
            UiSettingsValue& value
        )
        {
            std::string error;
            if (ReadSettingValue(id, value, error))
                return true;
            fail("Reset All sentinel GET failed for " +
                std::string(SettingName(id)) + ": " + error);
            return false;
        };
        const auto nextHistoryEpoch = [](uint64_t epoch)
        {
            ++epoch;
            return epoch == 0u ? 1u : epoch;
        };
        const auto expectSingleHistoryMutation = [&]
        (
            uint64_t before,
            std::string_view label
        )
        {
            const uint64_t after =
                m_app->GetLightingHistoryEpochForRuntimeDiagnostic();
            if (after != nextHistoryEpoch(before))
            {
                fail(std::string(label) +
                    " did not invalidate renderer history exactly once");
            }
        };

        UiSettingsValue sceneSentinel;
        UiSettingsValue adapterSentinel;
        readSentinel(SettingId::SceneCurrent, sceneSentinel);
        readSentinel(SettingId::GpuAdapter, adapterSentinel);
        applySentinel(
            SettingId::CameraMode,
            tokenValue(SettingId::CameraMode, 1u));
        m_app->NudgeCameraForRuntimeDiagnostic();
        const RetainedRuntimeCameraPose cameraSentinel =
            m_app->CaptureRetainedRuntimeCameraPose();
        applySentinel(
            SettingId::UiVisible,
            UiSettingsValue::Boolean(true));
        applySentinel(
            SettingId::UiSettingsCollapsed,
            UiSettingsValue::Boolean(true));
        applySentinel(
            SettingId::MaterialEditorVisible,
            UiSettingsValue::Boolean(true));

        std::shared_ptr<Material> materialSentinel;
        const std::shared_ptr<Scene> scene = m_app->GetScene();
        if (scene && scene->GetSceneGraph())
        {
            const auto& materials = scene->GetSceneGraph()->GetMaterials();
            auto found = std::find_if(
                materials.begin(), materials.end(),
                [](const std::shared_ptr<Material>& material)
                {
                    return material && material->materialID >= 0 &&
                        material->normalTexture;
                });
            if (found != materials.end())
                materialSentinel = *found;
        }
        if (!materialSentinel)
        {
            fail("Reset All sentinel found no material with a normal texture");
        }
        else
        {
            const uint64_t selectionEpoch =
                m_app->GetLightingHistoryEpochForRuntimeDiagnostic();
            if (applySentinel(
                    SettingId::MaterialSelected,
                    UiSettingsValue::Selector(
                        FormatSettingsSnapshotMaterialToken(
                            false,
                            static_cast<std::uint32_t>(
                                materialSentinel->materialID)))) &&
                m_app->GetLightingHistoryEpochForRuntimeDiagnostic() !=
                    selectionEpoch)
            {
                fail("material selection invalidated renderer history");
            }
            const uint64_t materialEpoch =
                m_app->GetLightingHistoryEpochForRuntimeDiagnostic();
            if (applySentinel(
                    SettingId::MaterialSelectedNormalScale,
                    UiSettingsValue::Float(0.5f)))
            {
                expectSingleHistoryMutation(
                    materialEpoch, "material mutation");
            }
        }

        const auto& lights = m_app->GetEditableLights();
        std::size_t flashlightIndex = lights.size();
        std::size_t directionalIndex = lights.size();
        for (std::size_t index = 0u; index < lights.size(); ++index)
        {
            if (!lights[index])
                continue;
            if (m_app->IsFlashlight(lights[index]))
                flashlightIndex = index;
            else if (directionalIndex == lights.size() &&
                lights[index]->GetLightType() == UVSR_LIGHT_TYPE_DIRECTIONAL)
            {
                directionalIndex = index;
            }
        }
        if (flashlightIndex == lights.size() ||
            directionalIndex == lights.size())
        {
            fail("Reset All sentinel requires flashlight and directional lights");
        }
        else
        {
            applySentinel(
                SettingId::LightSelected,
                UiSettingsValue::Selector(
                    FormatSettingsSnapshotLightToken(
                        flashlightIndex,
                        lights[flashlightIndex]->GetName())),
                true);
            const uint64_t flashlightEpoch =
                m_app->GetLightingHistoryEpochForRuntimeDiagnostic();
            if (applySentinel(
                    SettingId::LightSelectedFlashlightEnabled,
                    UiSettingsValue::Boolean(true)))
            {
                expectSingleHistoryMutation(
                    flashlightEpoch, "flashlight state mutation");
            }
            applySentinel(
                SettingId::LightSelectedFlashlightBrightness,
                UiSettingsValue::Float(500.f));
            applySentinel(
                SettingId::LightSelectedColor,
                UiSettingsValue::Vector(
                    { 0.1f, 0.2f, 0.3f, 0.f }, 3u));
            applySentinel(
                SettingId::LightSelected,
                UiSettingsValue::Selector(
                    FormatSettingsSnapshotLightToken(
                        directionalIndex,
                        lights[directionalIndex]->GetName())),
                true);
            applySentinel(
                SettingId::LightSelectedAngularSize,
                UiSettingsValue::Float(0.75f));
            applySentinel(
                SettingId::LightSelectedColor,
                UiSettingsValue::Vector(
                    { 0.2f, 0.3f, 0.4f, 0.f }, 3u));
        }

        applySentinel(
            SettingId::UiOverrideVisualMaxes,
            UiSettingsValue::Boolean(true));
        applySentinel(
            SettingId::UiZoom,
            tokenValue(SettingId::UiZoom, 3u));
        const uint64_t lightingEpoch =
            m_app->GetLightingHistoryEpochForRuntimeDiagnostic();
        if (applySentinel(
                SettingId::SkyDiffuseIbl,
                UiSettingsValue::Boolean(false)))
        {
            expectSingleHistoryMutation(
                lightingEpoch, "renderer setting mutation");
        }
        m_app->SeedNoiseSamplingPhasesForRuntimeDiagnostic();
        const uint64_t noiseEpoch =
            m_app->GetLightingHistoryEpochForRuntimeDiagnostic();
        if (applySentinel(
                SettingId::SkyVisibilitySpecifyNoise,
                UiSettingsValue::Boolean(true)))
        {
            expectSingleHistoryMutation(noiseEpoch, "sky noise mutation");
            if (m_app->GetNoiseSamplingPhasesForRuntimeDiagnostic() !=
                    std::array<uint64_t, 2>{ 13u, 0u })
            {
                fail("sky noise mutation reset unrelated sampling phases");
            }
        }
        applySentinel(
            SettingId::SkyAutoExposureEnabled,
            UiSettingsValue::Boolean(true));
        applySentinel(
            SettingId::SkyAutoExposureExposureCompensation,
            UiSettingsValue::Float(5.f));
        applySentinel(
            SettingId::SkyExposure,
            UiSettingsValue::Float(0.f));
        const uint64_t environmentEpoch =
            m_app->GetLightingHistoryEpochForRuntimeDiagnostic();
        if (applySentinel(
                SettingId::SkyEnvironment,
                tokenValue(SettingId::SkyEnvironment, 3u)))
        {
            expectSingleHistoryMutation(
                environmentEpoch, "environment mutation");
        }
        UiSettingsValue environmentExposure;
        if (readSentinel(SettingId::SkyExposure, environmentExposure) &&
            (environmentExposure.kind != UiSettingsValueKind::Float ||
             environmentExposure.scalar != GetImageBasedLightingSourceInfo(
                ImageBasedLightingSource::Kloppenheim07Night)
                    .defaultExposureStops))
        {
            fail("typed environment selection did not apply source exposure");
        }

        constexpr std::array PreservedSentinelIds = {
            SettingId::CameraMode,
            SettingId::SceneCurrent,
            SettingId::GpuAdapter,
            SettingId::UiVisible,
            SettingId::UiSettingsCollapsed,
            SettingId::MaterialEditorVisible,
            SettingId::LightSelected,
            SettingId::LightSelectedAngularSize,
            SettingId::LightSelectedColor,
            SettingId::MaterialSelected,
            SettingId::MaterialSelectedNormalScale
        };
        std::array<UiSettingsValue, PreservedSentinelIds.size()>
            preservedSentinels{};
        for (std::size_t index = 0u;
            index < PreservedSentinelIds.size(); ++index)
        {
            readSentinel(PreservedSentinelIds[index],
                preservedSentinels[index]);
        }
        m_app->SeedNoiseSamplingPhasesForRuntimeDiagnostic();
        m_app->ClearShaderReloadRequestForRuntimeDiagnostic();
        const uint64_t factoryResetEpoch =
            m_app->GetLightingHistoryEpochForRuntimeDiagnostic();
        std::string factoryResetError;
        if (!ResetAllSettingsToFactoryDefaults(factoryResetError))
            fail("Reset All sentinel failed: " + factoryResetError);
        expectSingleHistoryMutation(factoryResetEpoch, "Reset All");
        if (m_app->GetNoiseSamplingPhasesForRuntimeDiagnostic() !=
                std::array<uint64_t, 2>{ 0u, 0u } ||
            !m_app->IsShaderReloadRequestedForRuntimeDiagnostic())
        {
            fail("Reset All did not reset sampling phases and shader reload");
        }
        for (std::size_t index = 0u;
            index < PreservedSentinelIds.size(); ++index)
        {
            UiSettingsValue actual;
            if (readSentinel(PreservedSentinelIds[index], actual) &&
                !(actual == preservedSentinels[index]))
            {
                fail("Reset All changed preserved sentinel " +
                    std::string(SettingName(PreservedSentinelIds[index])));
            }
        }
        UiSettingsValue sceneAfterReset;
        UiSettingsValue adapterAfterReset;
        if (readSentinel(SettingId::SceneCurrent, sceneAfterReset) &&
            !(sceneAfterReset == sceneSentinel))
        {
            fail("Reset All changed the active scene");
        }
        if (readSentinel(SettingId::GpuAdapter, adapterAfterReset) &&
            !(adapterAfterReset == adapterSentinel))
        {
            fail("Reset All changed the active adapter");
        }
        const RetainedRuntimeCameraPose cameraAfterReset =
            m_app->CaptureRetainedRuntimeCameraPose();
        if (cameraAfterReset.position.x != cameraSentinel.position.x ||
            cameraAfterReset.position.y != cameraSentinel.position.y ||
            cameraAfterReset.position.z != cameraSentinel.position.z ||
            cameraAfterReset.direction.x != cameraSentinel.direction.x ||
            cameraAfterReset.direction.y != cameraSentinel.direction.y ||
            cameraAfterReset.direction.z != cameraSentinel.direction.z ||
            cameraAfterReset.verticalFovDegrees !=
                cameraSentinel.verticalFovDegrees)
        {
            fail("Reset All changed the active camera pose");
        }
        constexpr std::array ResetSentinelIds = {
            SettingId::UiOverrideVisualMaxes,
            SettingId::UiZoom,
            SettingId::SkyDiffuseIbl,
            SettingId::SkyEnvironment,
            SettingId::SkyExposure,
            SettingId::SkyAutoExposureEnabled,
            SettingId::SkyAutoExposureExposureCompensation
        };
        for (const SettingId id : ResetSentinelIds)
        {
            std::string error;
            if (!IsSettingAtContextualDefault(id, error))
            {
                fail("Reset All did not restore " +
                    std::string(SettingName(id)) + ": " + error);
            }
        }
        if (m_ui.FlashlightEnabled != DefaultFlashlightEnabled ||
            m_ui.Flashlight != DefaultFlashlightSettings)
        {
            fail("Reset All did not restore global flashlight defaults");
        }

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
            "%s)\n",
            serialized.size(),
            m_SettingsSnapshots.Code().c_str());
        return 0;
    }
#endif
