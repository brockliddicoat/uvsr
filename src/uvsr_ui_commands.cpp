#include "uvsr_ui_internal.h"
#include "renderer_scene_records.h"

#include <type_traits>

namespace
{
    void ReportPersistenceFailure(const SettingsSnapshotError& error, const char* code) noexcept
    {
        if (error.code == SettingsSnapshotErrorCode::Collision)
            uvsr::log::warning("Settings snapshot catalog contains a conflicting entry for %s", code);
        else
            uvsr::log::warning("Could not persist the settings snapshot catalog: %s (native error %lu)",
                error.Message(), static_cast<unsigned long>(error.nativeCode));
        if (error.cleanupCode)
            uvsr::log::warning("Settings snapshot temporary cleanup also failed (native error %lu)",
                static_cast<unsigned long>(error.cleanupCode));
    }

    bool RejectUnchangedCommandMutation(
        std::string_view path,
        SettingsSnapshotError& error) noexcept
    {
        error = ComposeSettingsSnapshotError({"No change: ", path, " already has the requested value."});
        return false;
    }

    bool ReportSettingsValueError(SettingsSnapshotError& failure, SettingsSnapshotError& error) noexcept
    {
        error = std::move(failure);
        return false;
    }

}

auto UIRenderer::IsCommandRuntimeMutationLocked(
        const UiSettingsCommandDefinition& definition) const -> bool {
        return uvsr::IsUiSettingsRuntimeMutationLocked(
            definition.section,
            m_app->IsSceneBusy());
    }

auto UIRenderer::CheckCommandMutationAllowed(
        const UiSettingsCommandDefinition& definition,
        SettingsSnapshotError& error) const -> bool {
        if (IsCommandRuntimeMutationLocked(definition))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "This setting cannot change while a scene is loading.", {}};
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
        SettingsSnapshotError& resetError) -> bool {
        resetError = {};
        if (m_app->IsSceneBusy())
        {
            resetError = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Factory settings cannot be restored while a scene is loading.", {}};
            return false;
        }
        bool succeeded = true;
        const auto reportFailure = [&resetError, &succeeded](
            const UiSettingsCommandDefinition& definition,
            const SettingsSnapshotError& error)
        {
            succeeded = false;
            auto message = ComposeSettingsSnapshotError({definition.name, ": ", error.MessageView()},
                error.code == SettingsSnapshotErrorCode::None ? SettingsSnapshotErrorCode::InvalidInput : error.code,
                error.nativeCode, error.cleanupCode);
            std::fprintf(stderr, "settings-reset: %s\n", message.Message());
            if (resetError.MessageView().empty())
                resetError = std::move(message);
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
            SettingsSnapshotError error;
            SettingsSnapshotError valueError;
            const bool globalFlashlightColor = globalFlashlight &&
                definition.id == SettingId::LightSelectedColor;
            const bool resolved = globalFlashlightColor
                ? GetDeclaredUiSettingsDefaultValue(definition, defaultValue, valueError)
                : ResolveSettingDefaultValue(definition, defaultValue, error);
            if (!resolved)
            {
                if (globalFlashlightColor) error = std::move(valueError);
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
                if (error.MessageView().rfind("No change: ", 0u) != 0u)
                    reportFailure(definition, error);
                continue;
            }
        }
        if (!m_app->ResetFactorySettingsRuntimeState())
        {
            succeeded = false;
            if (resetError.MessageView().empty()) resetError = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "renderer runtime reset failed", {}};
        }
        m_StatisticsEffect =
            static_cast<int>(StatisticsEffect::CompleteRenderer);
        m_PerformanceCollapsedRequest = true;
        m_PathingDrawerOpenRequested = false;
        ImGui::CloseUvsrColorPickerPopup();
        return succeeded;
    }

auto UIRenderer::RunAction(
        ActionId id,
        SettingsSnapshotError& error) -> bool {
        error = {};
        switch (id)
        {
        case ActionId::OpenSceneFolder:
        {
            const HINSTANCE result = ShellExecuteW(
                nullptr,
                L"open",
                m_app->GetSceneDir().data(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL);
            if (reinterpret_cast<std::intptr_t>(result) > 32)
                return true;
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Windows could not open the scene folder.", {}};
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
        error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Unknown settings action.", {}};
        return false;
    }

auto UIRenderer::GetDefaultCommandLight() const -> RendererSceneHandle {
        const auto lights = m_app->GetEditableLights();
        const auto selected = m_app->GetPrimaryDirectionalLight();
        return lights.Ordinal(selected) != InvalidSceneIndex ? selected : lights.At(0);
    }

auto UIRenderer::EnsureCommandSelectedLight() -> RendererSceneHandle {
        const auto lights = m_app->GetEditableLights();
        if (lights.Ordinal(m_SelectedLight) == InvalidSceneIndex)
            m_SelectedLight = GetDefaultCommandLight();
        return m_SelectedLight;
    }

auto UIRenderer::GetCommandLightDefaults(RendererSceneHandle light,
        UiLightDefaults& output, SettingsSnapshotError& error) -> bool {
        const auto lights = m_app->GetEditableLights();
        const auto* record = m_app->GetSceneLight(light);
        if (!record)
        {
            output = {};
            return true;
        }
        const uint32_t index = lights.Ordinal(light);
        const auto scene = m_app->GetCurrentSceneName();
        const auto name = m_app->GetSceneLightNameView(light);
        UiLightDefaults result;
        result.type = record->kind;
        (void)m_app->ReadSceneLightDirection(light, result.direction);
        const auto& values = record->values;
        result.color = {values.color.x, values.color.y, values.color.z};
        result.irradiance = values.irradiance;
        result.angularSize = values.angularSize;
        result.radius = values.radius;
        result.intensity = values.intensity;
        result.innerAngle = values.innerAngle;
        result.outerAngle = values.outerAngle;
        UiLightDefaultsError cacheError;
        if (m_LightDefaults.ReadOrCapture({scene.data(), scene.size(), index, name.data(), name.size()},
                result, output, cacheError))
            return true;
        error = {cacheError == UiLightDefaultsError::Allocation ? SettingsSnapshotErrorCode::OutOfMemory :
            cacheError == UiLightDefaultsError::Capacity ? SettingsSnapshotErrorCode::Capacity :
            SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Could not capture light defaults.", {}};
        return false;
    }

auto UIRenderer::IsCommandMaterialTransmissive(RendererMaterialDomain domain) -> bool {
        return domain == RendererMaterialDomain::Transmissive ||
            domain == RendererMaterialDomain::TransmissiveAlphaTested ||
            domain == RendererMaterialDomain::TransmissiveAlphaBlended;
    }

auto UIRenderer::IsCommandMaterialAlphaTested(RendererMaterialDomain domain) -> bool {
        return domain == RendererMaterialDomain::AlphaTested ||
            domain == RendererMaterialDomain::TransmissiveAlphaTested;
    }

auto UIRenderer::IsCommandMaterialAlphaBlended(RendererMaterialDomain domain) -> bool {
        return domain == RendererMaterialDomain::AlphaBlended ||
            domain == RendererMaterialDomain::TransmissiveAlphaBlended;
    }

namespace
{
    template<typename Field>
    [[nodiscard]] bool BindTyped(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue* requested,
        Field& current,
        UiSettingsValue& result,
        SettingsSnapshotError& error,
        SettingsSnapshotErrorCode* = nullptr) noexcept
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
                static_assert(std::is_same_v<Field, gpu_contract::Float3>);
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
            error = ComposeSettingsSnapshotError({definition.name, " expects ", shape, "."});
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
            candidate = {requested->vector[0], requested->vector[1], requested->vector[2]};
        UiSettingsValue next = read(candidate);
        if (next == result)
            return RejectUnchangedCommandMutation(definition.name, error);
        current = candidate;
        result = std::move(next);
        return true;
    }
    template<typename Enum, std::size_t Count>
    [[nodiscard]] bool BindTyped(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue* requested,
        Enum& current,
        const std::array<Enum, Count>& values,
        UiSettingsValue& result,
        SettingsSnapshotError& error,
        SettingsSnapshotErrorCode* valueFailure = nullptr,
        bool allowSameValueMutation = false) noexcept
    {
        if (definition.typedDomain.tokenCount != Count)
        {
            error = ComposeSettingsSnapshotError({definition.name, " has a mismatched typed token binding."});
            return false;
        }
        std::size_t index = Count;
        if (requested)
        {
            if (requested->kind != UiSettingsValueKind::Token)
            {
                error = ComposeSettingsSnapshotError({definition.name, " expects a token value."});
                return false;
            }
            for (std::size_t candidate = 0u; candidate < Count; ++candidate)
            {
                if (definition.typedDomain.tokens[candidate] == requested->Text())
                {
                    index = candidate;
                    break;
                }
            }
            if (index == Count)
            {
                error = ComposeSettingsSnapshotError({definition.name, " has an unknown token."});
                return false;
            }
            if (current == values[index] && !allowSameValueMutation)
                return RejectUnchangedCommandMutation(definition.name, error);
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
                error = ComposeSettingsSnapshotError({definition.name, " has an unknown live token."});
                return false;
            }
        }
        UiSettingsValue prepared;
        SettingsSnapshotError valueError;
        if (!prepared.SetToken(definition.typedDomain.tokens[index], valueError))
        {
            if (valueFailure) *valueFailure = valueError.code;
            return ReportSettingsValueError(valueError, error);
        }
        if (requested) current = values[index];
        result = std::move(prepared);
        return true;
    }
    template<typename State>
    [[nodiscard]] bool BindCatalogField(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue* requested,
        State& state,
        UiSettingsValue& value,
        SettingsSnapshotError& error,
        SettingsSnapshotErrorCode* valueFailure = nullptr) noexcept
    {
        switch (definition.id)
        {
#define UVSR_SETTING(symbol, name, metadata)
#define UVSR_FIELD(symbol, name, metadata, owner, ...) \
        case SettingId::symbol: \
            if constexpr (std::is_same_v<State, owner>) \
                return BindTyped(definition, requested, state.__VA_ARGS__, value, error, valueFailure); \
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
        SettingsSnapshotError& error,
        bool allowLatentMutation,
        bool deferMutationEffects,
        SettingsSnapshotErrorCode* valueFailure) -> bool {
        if (valueFailure) *valueFailure = SettingsSnapshotErrorCode::None;
        SettingsSnapshotError selectorError;
        const auto reportSelectorError = [&] {
            if (valueFailure) *valueFailure = selectorError.code;
            return ReportSettingsValueError(selectorError, error);
        };
        const auto acceptFormatted = [&](bool formatted) {
            return AcceptFormattedSelector(formatted, value, selectorError) || reportSelectorError();
        };
        const auto bindUIData = [&] {
            return BindCatalogField(definition, requested, m_ui, value, error, valueFailure);
        };
        const auto bindFastApproximateAaSettings = [&] {
            return BindCatalogField(definition, requested, m_ui.AntiAliasing.fastApproximate, value, error, valueFailure);
        };
        const auto bindPathTracingSettings = [&] {
            auto candidate = m_ui.PathTracing;
            if (!BindCatalogField(definition, requested, candidate, value, error, valueFailure))
                return false;
            if (requested)
            {
                if (definition.id == SettingId::PathingMinimumBounces &&
                    candidate.minimumBounces > candidate.maximumBounces)
                {
                    error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "minimum bounces must not exceed maximum bounces", {}};
                    return false;
                }
                candidate.minimumBounces = std::min(candidate.minimumBounces, candidate.maximumBounces);
                m_ui.PathTracing = candidate;
            }
            return true;
        };
        const auto bindFlashlightSettings = [&] {
            FlashlightSettings candidate = m_ui.Flashlight;
            const bool handled = BindCatalogField(definition, requested, candidate, value, error, valueFailure);
            if (handled && requested)
                m_ui.Flashlight = SanitizeFlashlightSettings(candidate);
            return handled;
        };
        const auto bindRendererSceneMaterialValues = [&] {
            const auto* material = m_app->GetSceneMaterial(m_ui.SelectedMaterial);
            if (!material)
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "No scene material is selected.", {}};
                return false;
            }
            if (requested && !allowLatentMutation && !IsSettingAvailable(definition.id))
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "setting is not available in the current context", {}};
                return false;
            }
            auto candidate = material->values;
            const bool handled = BindCatalogField(definition, requested, candidate, value, error, valueFailure);
            if (handled && requested && !m_app->SetSceneMaterial(m_ui.SelectedMaterial, candidate))
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "scene material transaction failed", {}};
                return false;
            }
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
                        ToneMappingLut::Portra400, ToneMappingLut::Ektar100 }, value, error, valueFailure))
                return false;
            return !requested || m_app->SetToneMappingLut(candidate, error);
        }
        case SettingId::UiSettingsCollapsed:
        {
            bool candidate = m_SettingsCollapsedRequest.value_or(
                m_SettingsCollapsed);
            if (!BindTyped(
                    definition, requested, candidate, value, error, valueFailure))
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
                    definition, requested, candidate, value, error, valueFailure))
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
                        LightingSolution::PathTracing }, value, error, valueFailure))
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
                return acceptFormatted(FormatSettingsSnapshotAdapterToken(
                    m_ui.ActiveGpuAdapterIndex, value, selectorError));
            }
            if (requested->kind != UiSettingsValueKind::Selector)
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "gpu.adapter expects a selector value.", {}};
                return false;
            }
            const SettingsSnapshotOptionSource<SettingsSnapshotAdapterOption> options{
                &m_ui.GpuAdapterChoices, m_ui.GpuAdapterChoices.Count(),
                [](const void* context, size_t index, SettingsSnapshotAdapterOption& option, SettingsSnapshotError&) noexcept {
                    const auto& adapter = (*static_cast<const GpuAdapterCatalog*>(context))[index];
                    option = {adapter.adapterIndex, adapter.name};
                    return true;
                }};
            std::int64_t requestedIndex = -1;
            UiSettingsValue canonical;
            if (!ResolveSettingsSnapshotAdapterToken(
                    requested->Text(), options, requestedIndex,
                    canonical, selectorError))
            {
                return reportSelectorError();
            }
            if (requestedIndex == m_ui.ActiveGpuAdapterIndex)
                return RejectUnchangedCommandMutation(definition.name, error);
            value = std::move(canonical);
            g_RestartAdapterIndex = static_cast<int>(requestedIndex);
            g_RestartRequested = true;
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(), GLFW_TRUE);
            return true;
        }
        case SettingId::CameraMode:
        {
            CameraMode candidate = m_ui.Camera;
            if (!BindTyped(
                    definition, requested, candidate,
                    std::array{ CameraMode::ThirdPerson, CameraMode::Static },
                    value, error, valueFailure))
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
                const SceneCatalogEntry* scene = m_app->GetCurrentSceneCatalogEntry();
                if (!scene)
                {
                    error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "scene.current is not a canonical catalog filename.", {}};
                    return false;
                }
                return acceptFormatted(FormatSettingsSnapshotSceneToken(
                    scene->CommandName, value, selectorError));
            }
            if (requested->kind != UiSettingsValueKind::Selector)
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "scene.current expects a selector value.", {}};
                return false;
            }
            const auto& scenes = m_app->GetAvailableScenes();
            const SettingsSnapshotOptionSource<SettingsSnapshotSceneOption> options{
                &scenes, scenes.Count(),
                [](const void* context, size_t index, SettingsSnapshotSceneOption& option, SettingsSnapshotError&) noexcept {
                    const auto& scene = (*static_cast<const SceneCatalog*>(context))[index];
                    SettingsSnapshotError ignored;
                    const bool canonical = ValidateSettingsSnapshotSelectorToken(SettingId::SceneCurrent, scene.CommandName, ignored);
                    option = {canonical ? std::string_view(scene.CommandName) : std::string_view{}, scene.DisplayName};
                    return true;
                }};
            size_t requestedOrdinal = 0;
            UiSettingsValue canonical;
            if (!ResolveSettingsSnapshotSceneToken(
                    requested->Text(), options, requestedOrdinal,
                    canonical, selectorError))
            {
                return reportSelectorError();
            }
            const auto& requestedFileName = scenes[requestedOrdinal].FileName;
            if (requestedFileName == m_app->GetCurrentSceneName())
                return RejectUnchangedCommandMutation(definition.name, error);
            if (!m_app->SetCurrentSceneName(requestedFileName, error))
                return false;
            value = std::move(canonical);
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
                    value, error, valueFailure, custom))
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
                        WhiteWorldMode::PreserveLighting }, value, error, valueFailure))
            {
                return false;
            }
            if (requested && !m_app->SetWhiteWorldMode(mode))
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "scene material batch transaction failed", {}};
                return false;
            }
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
                        PbrLightingDebugView::SkyVisibility }, value, error, valueFailure))
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
                    value, error, valueFailure))
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
                    definition, requested, candidate.enabled, value, error, valueFailure);
                break;
            case SettingId::SkyVisibilityDiffuseIbl:
                handled = BindTyped(
                    definition, requested, candidate.applyToDiffuseIbl,
                    value, error, valueFailure);
                break;
            case SettingId::SkyVisibilitySpecularIbl:
                handled = BindTyped(
                    definition, requested, candidate.applyToSpecularIbl,
                    value, error, valueFailure);
                break;
            case SettingId::SkyVisibilitySamplesPerPixel:
                handled = BindTyped(
                    definition, requested, candidate.sampleRateLog2,
                    std::array<std::int32_t, 7>{ 0, 1, 2, 3, 4, 5, 6 },
                    value, error, valueFailure);
                break;
            case SettingId::SkyVisibilitySpecifyNoise:
                handled = BindTyped(
                    definition, requested, candidate.noise.specifyNoise,
                    value, error, valueFailure);
                break;
            case SettingId::SkyVisibilityNoisePattern:
                handled = BindTyped(
                    definition, requested, candidate.noise.custom.pattern,
                    std::array{ NoisePattern::SpatialWhite,
                        NoisePattern::SpatialBlue,
                        NoisePattern::SpatiotemporalBlue }, value, error, valueFailure);
                break;
            case SettingId::SkyVisibilityNoiseResolution:
                handled = BindTyped(
                    definition, requested, candidate.noise.custom.resolution,
                    std::array{ NoiseResolution::Size64,
                        NoiseResolution::Size128,
                        NoiseResolution::Size256,
                        NoiseResolution::Size512 }, value, error, valueFailure);
                break;
            case SettingId::SkyVisibilityAnimateSamples:
                handled = BindTyped(
                    definition, requested, candidate.noise.custom.animate,
                    value, error, valueFailure);
                break;
            case SettingId::SkyVisibilityMaxDistance:
                handled = BindTyped(
                    definition, requested, candidate.maxDistance,
                    std::array{ RayVisibilityMaxDistance::Maximum,
                        RayVisibilityMaxDistance::Meters32,
                        RayVisibilityMaxDistance::Meters16,
                        RayVisibilityMaxDistance::Meters8,
                        RayVisibilityMaxDistance::Meters4,
                        RayVisibilityMaxDistance::Meters2 }, value, error, valueFailure);
                break;
            case SettingId::SkyVisibilityRayBias:
                handled = BindTyped(
                    definition, requested, candidate.rayBias, value, error, valueFailure);
                break;
            default:
                break;
            }
            if (!handled || !requested)
                return handled;
            if (!IsRayTracedSkyVisibilityConfigurationSupported(candidate))
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "The requested ray traced sky visibility configuration is not supported.", {}};
                return false;
            }
            if (candidate.enabled && !m_app->SupportsRayTracedSkyVisibility())
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Ray-traced sky visibility requires DXR 1.1 support.", {}};
                return false;
            }
            if (!IsValidNoiseSettings(candidate.noise.custom))
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "The requested sky visibility noise configuration is invalid.", {}};
                return false;
            }
            m_ui.RayTracedSkyVisibility = candidate;
            return true;
        }
        case SettingId::LightSelected:
        {
            const auto lights = m_app->GetEditableLights();
            const auto selected = lights.Ordinal(m_SelectedLight) == InvalidSceneIndex
                ? GetDefaultCommandLight() : m_SelectedLight;
            if (!requested)
            {
                const uint32_t index = lights.Ordinal(selected);
                if (!selected || index == InvalidSceneIndex)
                {
                    error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "The current scene has no selected light.", {}};
                    return false;
                }
                if (!acceptFormatted(FormatSettingsSnapshotLightToken(
                        index, m_app->GetSceneLightNameView(selected), value, selectorError))) return false;
                m_SelectedLight = selected;
                return true;
            }
            if (requested->kind != UiSettingsValueKind::Selector)
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "light.selected expects a selector value.", {}};
                return false;
            }
            struct LightSource
            {
                const UvsrSceneViewer* app;
                RendererSceneLightRange lights;
            } source{m_app, lights};
            const SettingsSnapshotOptionSource<SettingsSnapshotLightOption> options{
                &source, lights.Count(),
                [](const void* context, size_t index, SettingsSnapshotLightOption& option, SettingsSnapshotError&) noexcept {
                    const auto& source = *static_cast<const LightSource*>(context);
                    option = {index, source.app->GetSceneLightNameView(source.lights.At(uint32_t(index)))};
                    return true;
                }};
            std::size_t requestedIndex = 0u;
            UiSettingsValue canonical;
            if (!ResolveSettingsSnapshotLightToken(
                    requested->Text(), options, requestedIndex,
                    canonical, selectorError))
            {
                return reportSelectorError();
            }
            if (requestedIndex >= lights.Count()) return false;
            if (lights.At(uint32_t(requestedIndex)) == selected)
            {
                if (m_SelectedLight != selected)
                {
                    value = std::move(canonical);
                    m_SelectedLight = selected;
                }
                return RejectUnchangedCommandMutation(definition.name, error);
            }
            const auto candidate = lights.At(uint32_t(requestedIndex));
            UiLightDefaults defaults;
            if (!GetCommandLightDefaults(candidate, defaults, error)) return false;
            value = std::move(canonical);
            m_SelectedLight = candidate;
            return true;
        }
        case SettingId::LightSelectedFlashlightEnabled:
        {
            bool candidate = m_ui.FlashlightEnabled;
            if (!BindTyped(
                    definition, requested, candidate, value, error, valueFailure))
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
            const auto selected = EnsureCommandSelectedLight();
            if (!selected)
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "The current scene has no selected light.", {}};
                return false;
            }
            if (m_app->IsFlashlight(selected))
            {
                if (definition.id != SettingId::LightSelectedColor)
                {
                    error = ComposeSettingsSnapshotError({definition.name, " is not an editable generic flashlight property."});
                    return false;
                }
                gpu_contract::Float3 color{
                    m_ui.Flashlight.colorLinearRed,
                    m_ui.Flashlight.colorLinearGreen,
                    m_ui.Flashlight.colorLinearBlue};
                if (!BindTyped(
                        definition, requested, color, value, error, valueFailure))
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
            const auto* light = m_app->GetSceneLight(selected);
            if (!light) { error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "selected light is not in the active scene", {}}; return false; }
            const bool directional = light->kind == RendererSceneLightKind::Directional;
            const bool spot = light->kind == RendererSceneLightKind::Spot;
            const bool pointOrSpot = light->kind == RendererSceneLightKind::Point || spot;
            if (definition.id == SettingId::LightSelectedAzimuth ||
                definition.id == SettingId::LightSelectedElevation)
            {
                if (!directional && !spot)
                {
                    error = ComposeSettingsSnapshotError({definition.name, " requires a directional or spot light."});
                    return false;
                }
                RendererSceneLightDirection direction;
                if (!m_app->ReadSceneLightDirection(selected, direction))
                {
                    error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "selected light is not in the active scene", {}};
                    return false;
                }
                auto [azimuth, elevation] = GetRendererSceneLightAngles(direction, directional);
                float& component = definition.id == SettingId::LightSelectedAzimuth ? azimuth : elevation;
                if (!BindTyped(definition, requested, component, value, error, valueFailure)) return false;
                if (requested)
                {
                    const RendererSceneLightDirection candidate = MakeRendererSceneLightDirection(azimuth, elevation, directional);
                    if (!m_app->SetSceneLightPose(selected, nullptr, &candidate))
                    {
                        error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "scene light direction transaction failed", {}};
                        return false;
                    }
                }
                return true;
            }
            RendererSceneLightValues candidate;
            if (!m_app->ReadSceneLightValues(selected, candidate))
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "selected light is not in the active scene", {}};
                return false;
            }
            const auto commit = [&]
            {
                if (!requested || m_app->SetSceneLightValues(selected, candidate)) return true;
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "scene light transaction failed", {}};
                return false;
            };
            if (definition.id == SettingId::LightSelectedColor)
            {
                gpu_contract::Float3 color{candidate.color.x, candidate.color.y, candidate.color.z};
                if (!BindTyped(definition, requested, color, value, error, valueFailure)) return false;
                candidate.color = {color.x, color.y, color.z};
                return commit();
            }
            if (definition.id == SettingId::LightSelectedIrradiance ||
                definition.id == SettingId::LightSelectedAngularSize)
            {
                if (!directional)
                {
                    error = ComposeSettingsSnapshotError({definition.name, " requires a directional light."});
                    return false;
                }
                float& property = definition.id == SettingId::LightSelectedIrradiance
                    ? candidate.irradiance : candidate.angularSize;
                return BindTyped(definition, requested, property, value, error, valueFailure) && commit();
            }
            if (definition.id == SettingId::LightSelectedRadius ||
                definition.id == SettingId::LightSelectedIntensity)
            {
                if (!pointOrSpot)
                {
                    error = ComposeSettingsSnapshotError({definition.name, " requires a point or spot light."});
                    return false;
                }
                float& property = definition.id == SettingId::LightSelectedRadius ? candidate.radius : candidate.intensity;
                return BindTyped(definition, requested, property, value, error, valueFailure) && commit();
            }
            if (!spot)
            {
                error = ComposeSettingsSnapshotError({definition.name, " requires a spot light."});
                return false;
            }
            float& property = definition.id == SettingId::LightSelectedInnerAngle ? candidate.innerAngle : candidate.outerAngle;
            if (!BindTyped(definition, requested, property, value, error, valueFailure)) return false;
            if (requested && candidate.innerAngle > candidate.outerAngle)
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, definition.id == SettingId::LightSelectedInnerAngle
                    ? "The inner spot angle cannot exceed the outer angle."
                    : "The outer spot angle cannot be below the inner angle.", {}};
                return false;
            }
            return commit();
        }

        case SettingId::ShadowsRayTracedEnabled:
        case SettingId::ShadowsRayTracedMaxDistance:
        case SettingId::ShadowsRayTracedRayBias:
        {
            DirectionalShadowSettings candidate = m_ui.DirectionalShadows;
            bool handled = false;
            if (definition.id == SettingId::ShadowsRayTracedEnabled)
                handled = BindTyped(
                    definition, requested, candidate.enabled, value, error, valueFailure);
            else if (definition.id == SettingId::ShadowsRayTracedMaxDistance)
                handled = BindTyped(
                    definition, requested, candidate.maxDistance,
                    std::array{ RayVisibilityMaxDistance::Maximum,
                        RayVisibilityMaxDistance::Meters32,
                        RayVisibilityMaxDistance::Meters16,
                        RayVisibilityMaxDistance::Meters8,
                        RayVisibilityMaxDistance::Meters4,
                        RayVisibilityMaxDistance::Meters2 }, value, error, valueFailure);
            else
                handled = BindTyped(
                    definition, requested, candidate.rayBias, value, error, valueFailure);
            if (!handled || !requested)
                return handled;
            if (!IsDirectionalShadowSettingsValid(candidate))
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "The requested directional shadow configuration is invalid.", {}};
                return false;
            }
            if (candidate.enabled && !m_app->HasPrimaryDirectionalLight())
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Directional ray shadows require a primary directional light.", {}};
                return false;
            }
            if (candidate.enabled && !m_app->SupportsDirectionalRayVisibility())
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Directional ray shadows require DXR 1.1 support.", {}};
                return false;
            }
            m_ui.DirectionalShadows = candidate;
            return true;
        }
        case SettingId::MaterialSelected:
        {
            const auto scene = m_app->GetSceneView();
            if (!scene.generation)
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "No loaded scene provides material controls.", {}};
                return false;
            }
            if (!requested)
            {
                const auto* material = FindRendererSceneMaterial(scene, m_ui.SelectedMaterial);
                if (!material)
                {
                    return acceptFormatted(FormatSettingsSnapshotMaterialToken(true, 0, value, selectorError));
                }
                if (material->selectionId == InvalidSceneIndex)
                {
                    error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "The selected material has an invalid id.", {}};
                    return false;
                }
                return acceptFormatted(FormatSettingsSnapshotMaterialToken(false, material->selectionId, value, selectorError));
            }
            if (requested->kind != UiSettingsValueKind::Selector)
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "material.selected expects a selector value.", {}};
                return false;
            }
            const SettingsSnapshotOptionSource<SettingsSnapshotMaterialOption> options{
                &scene, scene.materials.count,
                [](const void* context, size_t index, SettingsSnapshotMaterialOption& option, SettingsSnapshotError&) noexcept {
                    const auto& scene = *static_cast<const RendererSceneView*>(context);
                    const auto& material = scene.materials.data[index];
                    if (material.selectionId == InvalidSceneIndex)
                        option = {material.selectionId, {}, false};
                    else
                    {
                        const auto name = RendererSceneText(scene, material.name);
                        option = {material.selectionId, name.count ? std::string_view(name.data, name.count) : std::string_view{}};
                    }
                    return true;
                }};
            bool none = false;
            std::uint32_t requestedId = 0u;
            UiSettingsValue canonical;
            if (!ResolveSettingsSnapshotMaterialToken(requested->Text(), options, none, requestedId, canonical, selectorError))
                return reportSelectorError();
            if (none)
            {
                if (!m_ui.SelectedMaterial)
                    return RejectUnchangedCommandMutation(definition.name, error);
                value = std::move(canonical);
                m_ui.SelectedMaterial = {};
                m_ui.SelectedNode = {};
                return true;
            }
            const auto match = FindRendererSceneMaterialSelection(scene, requestedId);
            if (!match)
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Resolved material is not uniquely owned by this scene.", {}};
                return false;
            }
            if (match == m_ui.SelectedMaterial)
                return RejectUnchangedCommandMutation(definition.name, error);
            value = std::move(canonical);
            m_ui.SelectedMaterial = match;
            return true;
        }
        default:
            break;
        }
        error = ComposeSettingsSnapshotError({"typed setting operation is missing for '", definition.name, "'."});
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
            m_app->NotifyMaterialCommandChanged();
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
        SettingsSnapshotError& error) -> bool {
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
        {
            SettingsSnapshotError valueError;
            if (GetDeclaredUiSettingsDefaultValue(definition, value, valueError))
                return true;
            if (valueError.code == SettingsSnapshotErrorCode::OutOfMemory)
                return ReportSettingsValueError(valueError, error);
            break;
        }
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
            const auto lights = m_app->GetEditableLights();
            const auto selected = GetDefaultCommandLight();
            const uint32_t index = lights.Ordinal(selected);
            if (selected && index != InvalidSceneIndex)
            {
                SettingsSnapshotError valueError;
                return AcceptFormattedSelector(FormatSettingsSnapshotLightToken(
                        index, m_app->GetSceneLightNameView(selected), value, valueError), value, valueError) ||
                    ReportSettingsValueError(valueError, error);
            }
            break;
        }
        case UiSettingsDefaultPolicy::SceneAuthored:
        case UiSettingsDefaultPolicy::SelectedLightColor:
        {
            const auto selected = EnsureCommandSelectedLight();
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
            UiLightDefaults defaults;
            if (!GetCommandLightDefaults(selected, defaults, error)) return false;
            switch (definition.id)
            {
            case SettingId::LightSelectedAzimuth:
            case SettingId::LightSelectedElevation:
            {
                const bool directional = defaults.type == RendererSceneLightKind::Directional;
                const auto angles = GetRendererSceneLightAngles(
                    defaults.direction, directional);
                value = UiSettingsValue::Float(
                    definition.id == SettingId::LightSelectedAzimuth
                        ? angles.azimuth : angles.elevation);
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
            const auto* material = m_app->GetSceneMaterial(m_ui.SelectedMaterial);
            if (!material)
                break;
            auto defaults = material->originalValues;
            return BindCatalogField(definition, nullptr, defaults, value, error);
        }
        case UiSettingsDefaultPolicy::HighestMemoryAdapter:
            break;
        }
        error = ComposeSettingsSnapshotError({"No contextual default is available for '", definition.name, "'."});
        return false;
    }

auto UIRenderer::ReadSettingValue(
        SettingId id,
        SettingsSnapshotText& value,
        SettingsSnapshotError& error) -> bool {
        error = {};
        const UiSettingsCommandDefinition* definition = FindSettingsCommandDefinition(id);
        if (!definition || definition->kind == UiSettingsCommandKind::Action)
        {
            error.code = SettingsSnapshotErrorCode::InvalidInput;
            error.message = "unknown setting id";
            return false;
        }
        if (!IsSettingAvailable(id))
        {
            error.code = SettingsSnapshotErrorCode::InvalidInput;
            error.message = "setting is not available in the current context";
            return false;
        }
        UiSettingsValue typed;
        if (!DispatchTypedSetting(*definition, nullptr, typed, error))
        {
            if (error.code == SettingsSnapshotErrorCode::None) error.code = SettingsSnapshotErrorCode::InvalidInput;
            return false;
        }
        return FormatUiSettingsValue(*definition, typed, value, error);
    }

auto UIRenderer::ReadSettingValue(
        SettingId id,
        UiSettingsValue& value,
        SettingsSnapshotError& error) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition || definition->kind == UiSettingsCommandKind::Action)
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "unknown setting id", {}};
            return false;
        }
        if (!IsSettingAvailable(id))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "setting is not available in the current context", {}};
            return false;
        }
        return DispatchTypedSetting(*definition, nullptr, value, error);
    }

auto UIRenderer::ApplySettingValue(
        SettingId id,
        std::string_view canonicalValue,
        SettingsSnapshotError& error) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition || definition->kind == UiSettingsCommandKind::Action ||
            !definition->Supports(UiSettingsCommandVerb::Set))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "setting is not mutable", {}};
            return false;
        }
        SettingsSnapshotValidationContext context;
        if (const auto* material = m_app->GetSceneMaterial(m_ui.SelectedMaterial);
            id == SettingId::MaterialSelectedOpacity && material)
        {
            context.hasMaterialBaseTexture = true;
            context.materialHasBaseTexture = material->values.textures[uint32_t(RendererSceneMaterialTextureSlot::BaseOrDiffuse)] != InvalidSceneIndex;
        }
        UiSettingsValue typed;
        SettingsSnapshotError valueError;
        if (!ParseCanonicalUiSettingsValue(
                *definition, canonicalValue, typed, valueError, context))
        {
            return ReportSettingsValueError(valueError, error);
        }
        return ApplySettingValue(id, typed, error);
    }

auto UIRenderer::ApplySettingValue(
        SettingId id,
        const UiSettingsValue& requested,
        SettingsSnapshotError& error,
        bool deferMutationEffects) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition || definition->kind == UiSettingsCommandKind::Action ||
            !definition->Supports(UiSettingsCommandVerb::Set))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "setting is not mutable", {}};
            return false;
        }
        if (!CheckCommandMutationAllowed(*definition, error) ||
            !IsSettingAvailable(id))
        {
            if (error.MessageView().empty())
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "setting is not available in the current context", {}};
            return false;
        }
        SettingsSnapshotValidationContext context;
        if (const auto* material = m_app->GetSceneMaterial(m_ui.SelectedMaterial);
            id == SettingId::MaterialSelectedOpacity && material)
        {
            context.hasMaterialBaseTexture = true;
            context.materialHasBaseTexture = material->values.textures[uint32_t(RendererSceneMaterialTextureSlot::BaseOrDiffuse)] != InvalidSceneIndex;
        }
        SettingsSnapshotError valueError;
        if (definition->kind != UiSettingsCommandKind::DynamicSelection &&
            !ValidateUiSettingsValue(*definition, requested, valueError, context))
        {
            return ReportSettingsValueError(valueError, error);
        }
        UiSettingsValue applied;
        const bool succeeded = DispatchTypedSetting(
            *definition, &requested, applied, error,
            false, deferMutationEffects);
        if (!succeeded && error.MessageView().rfind("No change: ", 0u) == 0u)
        {
            error = {};
            return true;
        }
        if (succeeded && !deferMutationEffects)
            ApplySettingMutationEffects(*definition);
        return succeeded;
    }

auto UIRenderer::ResetSettingValue(
        SettingId id,
        SettingsSnapshotError& error) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition || definition->kind == UiSettingsCommandKind::Action ||
            !definition->Supports(UiSettingsCommandVerb::Reset))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "setting has no reset operation", {}};
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
        const auto* light = m_app->GetSceneLight(m_SelectedLight);
        const auto* selectedMaterial = m_app->GetSceneMaterial(m_ui.SelectedMaterial);
        const auto* material = selectedMaterial ? &selectedMaterial->values : nullptr;
        switch (definition->availability)
        {
        case UiSettingsAvailability::Always:
            return true;
        case UiSettingsAvailability::SelectedLight:
            return static_cast<bool>(light);
        case UiSettingsAvailability::DirectionalOrSpotLight:
            return light && !m_app->IsFlashlight(m_SelectedLight) &&
                (light->kind == RendererSceneLightKind::Directional || light->kind == RendererSceneLightKind::Spot);
        case UiSettingsAvailability::DirectionalLight:
            return light && light->kind == RendererSceneLightKind::Directional;
        case UiSettingsAvailability::PointOrSpotLight:
            return light && !m_app->IsFlashlight(m_SelectedLight) &&
                (light->kind == RendererSceneLightKind::Point || light->kind == RendererSceneLightKind::Spot);
        case UiSettingsAvailability::SpotLight:
            return light && !m_app->IsFlashlight(m_SelectedLight) && light->kind == RendererSceneLightKind::Spot;
        case UiSettingsAvailability::SelectedFlashlight:
            return light && m_app->IsFlashlight(m_SelectedLight);
        case UiSettingsAvailability::SelectedMaterial:
            return static_cast<bool>(material);
        case UiSettingsAvailability::BaseTexture:
            return material && (material->textures[uint32_t(RendererSceneMaterialTextureSlot::BaseOrDiffuse)] != InvalidSceneIndex);
        case UiSettingsAvailability::MetalSpecularTexture:
            return material && (material->textures[uint32_t(RendererSceneMaterialTextureSlot::MetalRoughOrSpecular)] != InvalidSceneIndex);
        case UiSettingsAvailability::SpecularGlossMaterial:
            return material && material->useSpecularGlossModel;
        case UiSettingsAvailability::MetalRoughMaterial:
            return material && !material->useSpecularGlossModel;
        case UiSettingsAvailability::AlphaBlendedMaterial:
            return material && IsCommandMaterialAlphaBlended(material->domain);
        case UiSettingsAvailability::AlphaTestedMaterial:
            return material && IsCommandMaterialAlphaTested(material->domain) &&
                (material->textures[uint32_t(RendererSceneMaterialTextureSlot::BaseOrDiffuse)] != InvalidSceneIndex);
        case UiSettingsAvailability::NormalTexture:
            return material && (material->textures[uint32_t(RendererSceneMaterialTextureSlot::Normal)] != InvalidSceneIndex);
        case UiSettingsAvailability::OcclusionTexture:
            return material && (material->textures[uint32_t(RendererSceneMaterialTextureSlot::Occlusion)] != InvalidSceneIndex);
        case UiSettingsAvailability::EmissiveTexture:
            return material && (material->textures[uint32_t(RendererSceneMaterialTextureSlot::Emissive)] != InvalidSceneIndex);
        case UiSettingsAvailability::TransmissiveMaterial:
            return material && IsCommandMaterialTransmissive(material->domain);
        case UiSettingsAvailability::TransmissionTexture:
            return material && IsCommandMaterialTransmissive(material->domain) &&
                (material->textures[uint32_t(RendererSceneMaterialTextureSlot::Transmission)] != InvalidSceneIndex);
        case UiSettingsAvailability::OpacityTexture:
            return material && (material->textures[uint32_t(RendererSceneMaterialTextureSlot::Opacity)] != InvalidSceneIndex);
        }
        return false;
    }

auto UIRenderer::IsSettingAtContextualDefault(
        SettingId id,
        SettingsSnapshotError& error) -> bool {
        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(id);
        if (!definition || !definition->Supports(UiSettingsCommandVerb::Reset))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "setting has no contextual reset default", {}};
            return false;
        }
        if (!IsSettingAvailable(id))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "setting is not available in the current context", {}};
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

bool UIRenderer::ValidateSettingsSnapshotValue(SettingId id, std::string_view requestedValue,
    std::string_view dependencySelector, SettingsSnapshotError& validationError) noexcept
{
    const UiSettingsCommandDefinition* definition =
        FindSettingsCommandDefinition(id);
    if (!definition ||
        !IsSettingsSnapshotValue(*definition))
    {
        validationError = {SettingsSnapshotErrorCode::InvalidInput, 0, 0,
            "setting is absent from the live SnapshotCatalog", {}};
        return false;
    }

    SettingsSnapshotValidationContext context;
    if (id == SettingId::MaterialSelectedOpacity)
    {
        const auto* target = m_app->GetSceneMaterial(m_ui.SelectedMaterial);
        if (!dependencySelector.empty())
        {
            target = nullptr;
            const auto scene = m_app->GetSceneView();
            if (scene.generation)
            {
                for (size_t index = 0; index < scene.materials.count; ++index)
                {
                    const auto& material = scene.materials.data[index];
                    if (material.selectionId == InvalidSceneIndex) continue;
                    UiSettingsValue selector;
                    if (!FormatSettingsSnapshotMaterialToken(false, material.selectionId, selector, validationError)) return false;
                    if (selector.Text() == dependencySelector)
                    {
                        target = &material;
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
                target->values.textures[uint32_t(RendererSceneMaterialTextureSlot::BaseOrDiffuse)] != InvalidSceneIndex;
        }
    }
    return ValidateSettingsSnapshotCatalogValue(
        *definition,
        requestedValue,
        validationError,
        context);
}

bool UIRenderer::ReadSettingsSnapshotValue(SettingId id, bool raw, SettingsSnapshotText& liveValue,
    SettingsSnapshotError& readError) noexcept
{
    readError = {};
    const UiSettingsCommandDefinition* definition = FindSettingsCommandDefinition(id);
    if (!definition || !IsSettingsSnapshotValue(*definition))
    {
        readError.code = SettingsSnapshotErrorCode::InvalidInput;
        readError.message = "setting is absent from the live SnapshotCatalog";
        return false;
    }
    const bool latentRead = raw ||
        definition->snapshotRead == UiSettingsSnapshotReadPolicy::LatentValue;
    if (!latentRead)
    {
        if (ReadSettingValue(id, liveValue, readError)) return true;
        if (readError.code == SettingsSnapshotErrorCode::InvalidInput && !IsSettingAvailable(id))
            return liveValue.Assign("<unavailable>", readError);
        return false;
    }
    if (raw && definition->storage == UiSettingsStoragePolicy::ContextOnly && !IsSettingAvailable(id))
        return liveValue.Assign("<unavailable>", readError);

    UiSettingsValue typed;
    SettingsSnapshotError dispatchError;
    SettingsSnapshotErrorCode dispatchFailure = SettingsSnapshotErrorCode::None;
    const bool dispatched = DispatchTypedSetting(*definition, nullptr, typed,
        dispatchError, latentRead, false, &dispatchFailure);
    if (dispatched && FormatUiSettingsValue(*definition, typed, liveValue, readError))
        return true;
    // checked formatter failures differ from an unavailable dynamic setting.
    if (dispatchFailure != SettingsSnapshotErrorCode::None ||
        (!dispatched && dispatchError.code != SettingsSnapshotErrorCode::None &&
            dispatchError.code != SettingsSnapshotErrorCode::InvalidInput))
    {
        readError = std::move(dispatchError);
        return false;
    }
    if (readError.code != SettingsSnapshotErrorCode::None &&
        readError.code != SettingsSnapshotErrorCode::InvalidInput)
        return false;
    if (definition->dynamic) return liveValue.Assign("<unavailable>", readError);
    if (!dispatched)
    {
        readError = std::move(dispatchError);
        if (readError.code == SettingsSnapshotErrorCode::None) readError.code = SettingsSnapshotErrorCode::InvalidInput;
        return false;
    }
    return false;
}

bool UIRenderer::WriteSettingsSnapshotValue(SettingId id, std::string_view requestedValue,
    SettingsSnapshotError& error) noexcept
{
    error = {};
    const UiSettingsCommandDefinition* definition =
        FindSettingsCommandDefinition(id);
    if (!definition ||
        !IsSettingsSnapshotValue(*definition) ||
        definition->kind ==
            UiSettingsCommandKind::DynamicSelection ||
        !definition->Supports(UiSettingsCommandVerb::Set))
    {
        error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0,
            "setting is not a mutable SnapshotCatalog value", {}};
        return false;
    }
    if (definition->storage != UiSettingsStoragePolicy::Latent)
    {
        const bool succeeded = ApplySettingValue(id, requestedValue, error);
        if (!succeeded && error.code == SettingsSnapshotErrorCode::None) error.code = SettingsSnapshotErrorCode::InvalidInput;
        return succeeded;
    }
    if (!CheckCommandMutationAllowed(*definition, error)) return false;

    SettingsSnapshotValidationContext context;
    if (const auto* material = m_app->GetSceneMaterial(m_ui.SelectedMaterial);
        id == SettingId::MaterialSelectedOpacity && material)
    {
        context.hasMaterialBaseTexture = true;
        context.materialHasBaseTexture = material->values.textures[uint32_t(RendererSceneMaterialTextureSlot::BaseOrDiffuse)] != InvalidSceneIndex;
    }
    UiSettingsValue typed;
    SettingsSnapshotError valueError;
    if (!ParseCanonicalUiSettingsValue(
            *definition,
            requestedValue,
            typed,
            valueError,
            context))
    {
        error = std::move(valueError);
        return false;
    }
    UiSettingsValue applied;
    const bool succeeded = DispatchTypedSetting(
        *definition,
        &typed,
        applied,
        error,
        true);
    if (!succeeded &&
        error.MessageView().rfind("No change: ", 0u) == 0u)
    {
        error = {};
        return true;
    }
    if (succeeded)
        ApplySettingMutationEffects(*definition);
    else if (error.code == SettingsSnapshotErrorCode::None)
        error.code = SettingsSnapshotErrorCode::InvalidInput;
    return succeeded;
}

SettingsSnapshotSelectorTransition UIRenderer::DriveSettingsSnapshotSelector(SettingId id,
    std::string_view canonicalToken, bool begin, bool /*rollback*/, SettingsSnapshotError& selectorError) noexcept
{
    // staged apply and rollback drive only these checked selector owners.
    if (id != SettingId::SceneCurrent && id != SettingId::LightSelected && id != SettingId::MaterialSelected)
    {
        selectorError = {SettingsSnapshotErrorCode::InvalidInput, 0, 0,
            "setting does not support staged selection", {}};
        return SettingsSnapshotSelectorTransition::Failed;
    }
    const auto ready = [&]
    {
        SettingsSnapshotText current;
        SettingsSnapshotError readError;
        if (ReadSettingValue(id, current, readError) && current.View() == canonicalToken)
            return SettingsSnapshotSelectorTransition::Ready;
        if (readError.code != SettingsSnapshotErrorCode::None)
            selectorError = std::move(readError);
        if (selectorError.MessageView().empty())
            selectorError = {SettingsSnapshotErrorCode::InvalidInput, 0, 0,
                id == SettingId::SceneCurrent ? "loaded scene did not publish the requested canonical token"
                    : "selector did not publish its canonical token", {}};
        return SettingsSnapshotSelectorTransition::Failed;
    };
    if (!begin)
    {
        if (id != SettingId::SceneCurrent)
        {
            selectorError = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "selector remained pending without a poll contract", {}};
            return SettingsSnapshotSelectorTransition::Failed;
        }
        if (m_app->IsSceneBusy())
            return SettingsSnapshotSelectorTransition::Pending;
        if (m_app->HasSceneLoadFailure() ||
            !m_app->IsSceneLoaded())
        {
            selectorError = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "selected scene failed to load", {}};
            return SettingsSnapshotSelectorTransition::Failed;
        }
        return ready();
    }

    if (!ApplySettingValue(id, canonicalToken, selectorError))
    {
        if (selectorError.code == SettingsSnapshotErrorCode::None) selectorError.code = SettingsSnapshotErrorCode::InvalidInput;
        return SettingsSnapshotSelectorTransition::Failed;
    }
    if (id != SettingId::SceneCurrent)
        return ready();
    return SettingsSnapshotSelectorTransition::Pending;
}

auto UIRenderer::MakeSettingsSnapshotRuntimeAccess() -> SettingsSnapshotRuntimeAccess
{
    return {
        !m_app->IsSceneBusy() && m_app->IsSceneLoaded(),
        this,
        [](void* context, SettingId id, std::string_view requested,
            std::string_view dependency, SettingsSnapshotError& error) noexcept {
            return static_cast<UIRenderer*>(context)->ValidateSettingsSnapshotValue(id, requested, dependency, error);
        },
        [](void* context, SettingId id, SettingsSnapshotText& value, SettingsSnapshotError& error) noexcept {
            return static_cast<UIRenderer*>(context)->ReadSettingsSnapshotValue(id, false, value, error);
        },
        [](void* context, SettingId id, SettingsSnapshotText& value, SettingsSnapshotError& error) noexcept {
            return static_cast<UIRenderer*>(context)->ReadSettingsSnapshotValue(id, true, value, error);
        },
        [](void* context, SettingId id, std::string_view requested, SettingsSnapshotError& error) noexcept {
            return static_cast<UIRenderer*>(context)->WriteSettingsSnapshotValue(id, requested, error);
        },
        [](void* context, SettingId id, std::string_view token, bool begin, bool rollback,
            SettingsSnapshotError& error) noexcept {
            return static_cast<UIRenderer*>(context)->DriveSettingsSnapshotSelector(id, token, begin, rollback, error);
        }
    };
}

auto UIRenderer::RefreshSettingsSnapshot() -> bool {
        SettingsSnapshotError error;
        if (m_SettingsSnapshots.Refresh(MakeSettingsSnapshotRuntimeAccess(), error)) return true;
        uvsr::log::warning("Could not refresh the settings snapshot: %s", error.Message());
        return false;
    }

auto UIRenderer::CopySettingsSnapshot() -> void {
        if (!RefreshSettingsSnapshot()) return;
        SettingsSnapshotError error;
        if (!m_SettingsSnapshots.PersistToLocalCatalog(error))
        {
            ReportPersistenceFailure(error, m_SettingsSnapshots.Code().data());
            uvsr::log::warning(
                "The settings snapshot code was not copied because its "
                "local catalog entry could not be saved.");
            return;
        }
        ImGui::SetClipboardText(m_SettingsSnapshots.Code().data());
    }

auto UIRenderer::FailStartupSettingsSnapshot(
        std::string_view code, std::string_view error) -> void {
        g_RestartRequested = false;
        g_RestartAdapterIndex = -1;
        g_StartupSettingsSnapshotFailed = true;
        uvsr::log::error(
            "Startup settings snapshot %s failed: %s",
            code.empty() ? "<none>" : code.data(),
            error.empty() ? "settings snapshot transaction failed" : error.data());
        glfwSetWindowShouldClose(
            GetDeviceManager()->GetWindow(), GLFW_TRUE);
    }

auto UIRenderer::HandleStagedSettingsSnapshotStep(
        const SettingsSnapshotTransactionStep& step) -> void {
        if (step.progress == SettingsSnapshotTransactionProgress::Pending)
            return;
        const SettingsSnapshotText code = static_cast<SettingsSnapshotText&&>(m_PendingSettingsSnapshotCode);
        const bool failed =
            step.progress == SettingsSnapshotTransactionProgress::Failed;
        const std::string_view terminalError = step.result.error.MessageView();
        if (failed)
        {
            FailStartupSettingsSnapshot(code.View(), terminalError);
            return;
        }

        uvsr::log::info(
            "Loaded startup settings snapshot %s (%zu values changed)",
            code.View().data(), step.result.changedValueCount);
    }

auto UIRenderer::TryApplyStartupSettingsSnapshot() -> void {
        if (m_SettingsSnapshots.HasStagedApply())
        {
            HandleStagedSettingsSnapshotStep(
                m_SettingsSnapshots.ContinueStagedApply());
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
        SettingsSnapshotText pending;
        SettingsSnapshotError error;
        if (!pending.Assign(m_StartupSettingsSnapshotCode, error))
        {
            FailStartupSettingsSnapshot(m_StartupSettingsSnapshotCode, error.MessageView());
            return;
        }
        m_PendingSettingsSnapshotCode = static_cast<SettingsSnapshotText&&>(pending);
        HandleStagedSettingsSnapshotStep(
            m_SettingsSnapshots.BeginLoadCodeStaged(
                m_StartupSettingsSnapshotCode,
                MakeSettingsSnapshotRuntimeAccess()));
    }

#if defined(UVSR_BUILD_TESTING)
namespace
{
    class SettingsContractFailures
    {
    public:
        void Add(std::initializer_list<std::string_view> parts) noexcept
        {
            if (m_Count == SIZE_MAX) m_StorageFailed = true;
            else ++m_Count;
            if (m_StorageFailed) return;
            SettingsSnapshotText message;
            SettingsSnapshotError error;
            if (!message.AssignParts(parts, error))
            {
                m_StorageFailed = true;
                return;
            }
            // preserve the former per-message %s boundary, including later records.
            const auto visible = message.View().substr(0, message.View().find('\0'));
            if (!m_Text.AssignParts({m_Text.View(), "settings-contract: ", visible, "\n"}, error))
                m_StorageFailed = true;
        }

        bool Report() const noexcept
        {
            if (!m_Count && !m_StorageFailed) return false;
            const auto text = m_Text.View();
            if (!text.empty()) std::fwrite(text.data(), 1, text.size(), stderr);
            if (m_StorageFailed)
                std::fprintf(stderr, "settings-contract: could not retain the complete failure transcript\n");
            std::fprintf(stderr, "settings-contract: FAILED (%zu mismatches)\n", m_Count);
            return true;
        }

    private:
        SettingsSnapshotText m_Text;
        size_t m_Count = 0;
        bool m_StorageFailed = false;
    };

    bool PrepareSettingsContractPath(const wchar_t* directory, DWORD processId, WindowsPath& path) noexcept
    {
        wchar_t basename[38]; // prefix, ten DWORD digits, .txt and terminator.
        if (swprintf_s(basename, L"uvsr-settings-contract-%lu.txt", processId) < 0)
            return false;
        WindowsPathResult result;
        return JoinWindowsRelativePath(directory, basename, path, result);
    }

    bool SettingsContractDeleteFallback(DWORD error) noexcept
    {
        return error == ERROR_INVALID_PARAMETER || error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED;
    }

    DWORD SetSettingsContractDeleteFlag(HANDLE file) noexcept
    {
        FILE_DISPOSITION_INFO_EX extended{FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS};
        if (SetFileInformationByHandle(file, FileDispositionInfoEx, &extended, sizeof(extended)))
            return ERROR_SUCCESS;
        const DWORD error = GetLastError();
        if (!SettingsContractDeleteFallback(error)) return error;
        FILE_DISPOSITION_INFO basic{TRUE};
        return SetFileInformationByHandle(file, FileDispositionInfo, &basic, sizeof(basic)) ? ERROR_SUCCESS : GetLastError();
    }

    bool RemoveSettingsContractEntry(const wchar_t* path) noexcept
    {
        constexpr DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        constexpr DWORD flags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
        HANDLE file = CreateFileW(path, DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
            share, nullptr, OPEN_EXISTING, flags, nullptr);
        const bool canChangeAttributes = file != INVALID_HANDLE_VALUE;
        if (!canChangeAttributes)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_ACCESS_DENIED)
            {
                file = CreateFileW(path, DELETE, share, nullptr, OPEN_EXISTING, flags, nullptr);
                if (file == INVALID_HANDLE_VALUE) return false;
            }
            else
            {
                return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
                    error == ERROR_BAD_NETPATH || error == ERROR_INVALID_NAME ||
                    error == ERROR_DIRECTORY || error == ERROR_NETNAME_DELETED;
            }
        }
        // the former filesystem cleanup also deletes directories, reparse entries and readonly files.
        const bool removed = [file, canChangeAttributes]() noexcept {
            FILE_DISPOSITION_INFO_EX extended{FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE};
            if (SetFileInformationByHandle(file, FileDispositionInfoEx, &extended, sizeof(extended)))
                return true;
            if (!SettingsContractDeleteFallback(GetLastError())) return false;
            DWORD error = SetSettingsContractDeleteFlag(file);
            if (error == ERROR_SUCCESS) return true;
            if (error != ERROR_ACCESS_DENIED || !canChangeAttributes) return false;
            FILE_BASIC_INFO attributes;
            if (!GetFileInformationByHandleEx(file, FileBasicInfo, &attributes, sizeof(attributes)) ||
                !(attributes.FileAttributes & FILE_ATTRIBUTE_READONLY))
                return false;
            attributes.FileAttributes ^= FILE_ATTRIBUTE_READONLY;
            if (!SetFileInformationByHandle(file, FileBasicInfo, &attributes, sizeof(attributes)))
                return false;
            error = SetSettingsContractDeleteFlag(file);
            if (error == ERROR_SUCCESS) return true;
            if (error == ERROR_ACCESS_DENIED)
            {
                attributes.FileAttributes |= FILE_ATTRIBUTE_READONLY;
                (void)SetFileInformationByHandle(file, FileBasicInfo, &attributes, sizeof(attributes));
            }
            return false;
        }();
        const bool closed = CloseHandle(file) != FALSE;
        return removed && closed;
    }
}

auto UIRenderer::VerifyCanonicalSettingsContract() -> int {
        SettingsContractFailures failures;
        const auto fail = [&](std::initializer_list<std::string_view> parts) { failures.Add(parts); };
        if (m_app->IsSceneBusy() || !m_app->IsSceneLoaded())
            fail({"default scene did not finish loading"});
        const auto isExpectedNoChange = [](const SettingsSnapshotError& error) {
            return error.MessageView().rfind("No change: ", 0u) == 0u;
        };
        const auto checkDefault = [&](const UiSettingsCommandDefinition& definition,
            bool read, std::string_view value, const SettingsSnapshotError& error) {
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
                matches = read && ContainsNormalizedCommandAscii(m_app->GetCurrentSceneName(), "bistrointeriorretextured", true);
                break;
            case UiSettingsDefaultPolicy::SceneDefaultLight:
            {
                const auto lights = m_app->GetEditableLights();
                const auto selected = lights.Ordinal(m_SelectedLight) == InvalidSceneIndex
                    ? GetDefaultCommandLight() : m_SelectedLight;
                const uint32_t position = lights.Ordinal(selected);
                bool prepared = true;
                matches = read && selected && selected == GetDefaultCommandLight() && position != InvalidSceneIndex;
                if (matches)
                {
                    UiSettingsValue selector;
                    SettingsSnapshotError selectorError;
                    prepared = AcceptFormattedSelector(FormatSettingsSnapshotLightToken(
                        position, m_app->GetSceneLightNameView(selected), selector, selectorError), selector, selectorError);
                    if (!prepared) fail({selectorError.MessageView()});
                    matches = prepared && value == selector.Text();
                }
                if (prepared) m_SelectedLight = selected;
                break;
            }
            case UiSettingsDefaultPolicy::SceneAuthored:
            case UiSettingsDefaultPolicy::SelectedLightColor:
                matches = EnsureCommandSelectedLight() == GetDefaultCommandLight();
                break;
            case UiSettingsDefaultPolicy::NoMaterial:
                matches = read && !m_ui.SelectedMaterial &&
                    value == FormatUiSettingsDefaultAnchor(definition).View();
                break;
            case UiSettingsDefaultPolicy::MaterialAuthored:
                matches = !m_ui.SelectedMaterial;
                break;
            case UiSettingsDefaultPolicy::Literal:
                matches = read && value == FormatUiSettingsDefaultAnchor(definition).View();
                break;
            case UiSettingsDefaultPolicy::EnvironmentExposure:
            case UiSettingsDefaultPolicy::FxaaQualityProfile:
            case UiSettingsDefaultPolicy::FlashlightDefault:
                matches = !IsSettingAvailable(definition.id) ||
                    (read && value == FormatUiSettingsDefaultAnchor(definition).View());
                break;
            }
            if (!matches)
                fail({definition.name, " default mismatch: ", value, " / ", error.MessageView()});
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
            bool firstRead = false;
            SettingsSnapshotText firstValue;
            SettingsSnapshotError firstError;
            for (unsigned attempt = 0u; attempt < (resettable ? 2u : 1u); ++attempt)
            {
                SettingsSnapshotError error;
                if (resettable && !ResetSettingValue(definition.id, error) &&
                    !isExpectedNoChange(error) && !mayBeUnavailable)
                    fail({definition.name, " RESET failed: ", error.MessageView()});
                SettingsSnapshotText value;
                SettingsSnapshotError readError;
                const bool read = ReadSettingValue(definition.id, value, readError);
                error = std::move(readError);
                checkDefault(definition, read, value.View(), error);
                if (attempt == 0u)
                {
                    firstRead = read;
                    firstValue = std::move(value);
                    firstError = std::move(error);
                }
                else if (firstRead != read || firstValue.View() != value.View() ||
                    firstError.MessageView() != error.MessageView())
                    fail({definition.name, " was not stable after a second RESET"});
            }
        }

        DecodedSettings serialized;
        SettingsSnapshotError snapshotError;
        bool snapshotReady = true;
        for (const auto& definition : UiSettingsCommandCatalog)
        {
            if (!IsSettingsSnapshotValue(definition))
                continue;
            SettingsSnapshotText value;
            SettingsSnapshotError readError;
            if (!ReadSettingValue(definition.id, value, readError))
            {
                if (readError.code != SettingsSnapshotErrorCode::InvalidInput || IsSettingAvailable(definition.id))
                {
                    fail({"snapshot value read failed: ", readError.MessageView()});
                    snapshotReady = false;
                    break;
                }
                if (!value.Assign("<unavailable>", snapshotError))
                {
                    fail({"snapshot unavailable value failed: ", snapshotError.MessageView()});
                    snapshotReady = false;
                    break;
                }
            }
            if (!serialized.Insert(definition.name, value.View(), snapshotError))
            {
                fail({"snapshot serialization failed: ", snapshotError.Message()});
                snapshotReady = false;
                break;
            }
        }
        json::EncodedText expectedText;
        if (snapshotReady && !FormatCanonicalSettingsSnapshot(serialized, expectedText, snapshotError))
        {
            fail({"snapshot formatting failed: ", snapshotError.Message()});
            snapshotReady = false;
        }
        if (snapshotReady)
        {
            const std::string_view expectedCanonical(expectedText.Data(), expectedText.Size());
            if (!RefreshSettingsSnapshot()) fail({"snapshot refresh failed"});
            if (m_SettingsSnapshots.Canonical() != expectedCanonical)
                fail({"snapshot serialization membership or values drifted"});
            if (!IsSettingsSnapshotCode(m_SettingsSnapshots.Code()) ||
                BuildSettingsSnapshotCode(expectedCanonical).View() != m_SettingsSnapshots.Code())
                fail({"snapshot code did not identify its canonical payload"});
            const wchar_t* temporaryDirectory = _wgetenv(L"TEMP");
            if (!temporaryDirectory || temporaryDirectory[0] == L'\0')
                fail({"TEMP is unavailable for the snapshot save round trip"});
            else
            {
                WindowsPath path;
                if (!PrepareSettingsContractPath(temporaryDirectory, GetCurrentProcessId(), path))
                    fail({"could not prepare the snapshot save round trip path"});
                else
                {
                    if (!m_SettingsSnapshots.Persist(path.Data(), snapshotError))
                    {
                        ReportPersistenceFailure(snapshotError, m_SettingsSnapshots.Code().data());
                        fail({"snapshot save failed"});
                    }
                    SettingsSnapshotMatches saved;
                    const bool readSaved = ReadMatchingSettingsSnapshots(path.Data(), m_SettingsSnapshots.Code(), saved, snapshotError);
                    const bool removed = RemoveSettingsContractEntry(path.Data());
                    if (!readSaved || saved.Count() != 1 || saved.Text(0) != expectedCanonical || !removed)
                        fail({"saved snapshot did not decode to its exact canonical payload"});
                    else
                    {
                        const auto applied = m_SettingsSnapshots.BeginApplyCanonicalStaged(
                            saved.Text(0), MakeSettingsSnapshotRuntimeAccess());
                        if (!RefreshSettingsSnapshot()) fail({"snapshot refresh failed"});
                        if (applied.progress != SettingsSnapshotTransactionProgress::Succeeded ||
                            applied.result.changedValueCount != 0u || m_SettingsSnapshots.Canonical() != saved.Text(0))
                            fail({"saved snapshot was not an idempotent live transaction: ", applied.result.error.MessageView()});
                    }
                }
            }

        }

        const auto tokenValue = [](SettingId id, std::size_t index) {
            return FindSettingsCommandDefinition(id)->typedDomain.tokens[index];
        };
        const auto applySentinel = [&]
        (
            SettingId id,
            const auto& value,
            bool allowNoChange = false
        )
        {
            SettingsSnapshotError error;
            if (ApplySettingValue(id, value, error) ||
                allowNoChange && isExpectedNoChange(error))
            {
                return true;
            }
            fail({"Reset All sentinel SET failed for ",
                SettingName(id), ": ", error.MessageView()});
            return false;
        };
        const auto readSentinel = [&]
        (
            SettingId id,
            UiSettingsValue& value
        )
        {
            SettingsSnapshotError error;
            if (ReadSettingValue(id, value, error))
                return true;
            fail({"Reset All sentinel GET failed for ",
                SettingName(id), ": ", error.MessageView()});
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
                fail({label,
                    " did not invalidate renderer history exactly once"});
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

        const RendererSceneMaterial* materialSentinel = nullptr;
        const auto materialScene = m_app->GetSceneView();
        for (size_t index = 0; index < materialScene.materials.count; ++index)
        {
            const auto& material = materialScene.materials.data[index];
            if (material.selectionId != InvalidSceneIndex &&
                material.values.textures[uint32_t(RendererSceneMaterialTextureSlot::Normal)] != InvalidSceneIndex &&
                (!materialSentinel || material.selectionId < materialSentinel->selectionId))
                materialSentinel = &material;
        }
        if (!materialSentinel)
        {
            fail({"Reset All sentinel found no material with a normal texture"});
        }
        else
        {
            const uint64_t selectionEpoch =
                m_app->GetLightingHistoryEpochForRuntimeDiagnostic();
            UiSettingsValue selector;
            SettingsSnapshotError selectorError;
            const bool prepared = FormatSettingsSnapshotMaterialToken(false, materialSentinel->selectionId, selector, selectorError);
            if (!prepared) fail({selectorError.MessageView()});
            if (prepared && applySentinel(SettingId::MaterialSelected, selector) &&
                m_app->GetLightingHistoryEpochForRuntimeDiagnostic() !=
                    selectionEpoch)
            {
                fail({"material selection invalidated renderer history"});
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

        const auto lights = m_app->GetEditableLights();
        uint32_t flashlightIndex = lights.Count();
        uint32_t directionalIndex = lights.Count();
        for (uint32_t index = 0u; index < lights.Count(); ++index)
        {
            const auto light = lights.At(index);
            const auto* record = m_app->GetSceneLight(light);
            if (!record) continue;
            if (m_app->IsFlashlight(light))
                flashlightIndex = index;
            else if (directionalIndex == lights.Count() && record->kind == RendererSceneLightKind::Directional)
            {
                directionalIndex = index;
            }
        }
        if (flashlightIndex == lights.Count() || directionalIndex == lights.Count())
        {
            fail({"Reset All sentinel requires flashlight and directional lights"});
        }
        else
        {
            UiSettingsValue flashlightSelector, directionalSelector;
            SettingsSnapshotError selectorError;
            if (!AcceptFormattedSelector(FormatSettingsSnapshotLightToken(flashlightIndex,
                    m_app->GetSceneLightNameView(lights.At(flashlightIndex)), flashlightSelector, selectorError), flashlightSelector, selectorError) ||
                !AcceptFormattedSelector(FormatSettingsSnapshotLightToken(directionalIndex,
                    m_app->GetSceneLightNameView(lights.At(directionalIndex)), directionalSelector, selectorError), directionalSelector, selectorError))
                fail({selectorError.MessageView()});
            else
            {
                applySentinel(
                    SettingId::LightSelected,
                    flashlightSelector,
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
                    directionalSelector,
                    true);
                applySentinel(
                    SettingId::LightSelectedAngularSize,
                    UiSettingsValue::Float(0.75f));
                applySentinel(
                    SettingId::LightSelectedColor,
                    UiSettingsValue::Vector(
                        { 0.2f, 0.3f, 0.4f, 0.f }, 3u));
            }
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
                fail({"sky noise mutation reset unrelated sampling phases"});
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
            fail({"typed environment selection did not apply source exposure"});
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
        SettingsSnapshotError factoryResetError;
        if (!ResetAllSettingsToFactoryDefaults(factoryResetError))
            fail({"Reset All sentinel failed: ", factoryResetError.MessageView()});
        expectSingleHistoryMutation(factoryResetEpoch, "Reset All");
        if (m_app->GetNoiseSamplingPhasesForRuntimeDiagnostic() !=
                std::array<uint64_t, 2>{ 0u, 0u } ||
            !m_app->IsShaderReloadRequestedForRuntimeDiagnostic())
        {
            fail({"Reset All did not reset sampling phases and shader reload"});
        }
        for (std::size_t index = 0u;
            index < PreservedSentinelIds.size(); ++index)
        {
            UiSettingsValue actual;
            if (readSentinel(PreservedSentinelIds[index], actual) &&
                !(actual == preservedSentinels[index]))
            {
                fail({"Reset All changed preserved sentinel ",
                    SettingName(PreservedSentinelIds[index])});
            }
        }
        UiSettingsValue sceneAfterReset;
        UiSettingsValue adapterAfterReset;
        if (readSentinel(SettingId::SceneCurrent, sceneAfterReset) &&
            !(sceneAfterReset == sceneSentinel))
        {
            fail({"Reset All changed the active scene"});
        }
        if (readSentinel(SettingId::GpuAdapter, adapterAfterReset) &&
            !(adapterAfterReset == adapterSentinel))
        {
            fail({"Reset All changed the active adapter"});
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
            fail({"Reset All changed the active camera pose"});
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
            SettingsSnapshotError error;
            if (!IsSettingAtContextualDefault(id, error))
            {
                fail({"Reset All did not restore ",
                    SettingName(id), ": ", error.MessageView()});
            }
        }
        if (m_ui.FlashlightEnabled != DefaultFlashlightEnabled ||
            m_ui.Flashlight != DefaultFlashlightSettings)
        {
            fail({"Reset All did not restore global flashlight defaults"});
        }

        if (failures.Report()) return 1;
        std::fprintf(stdout,
            "settings-contract: PASS (%zu persisted descriptors, "
            "%s)\n",
            serialized.Count(),
            m_SettingsSnapshots.Code().data());
        return 0;
    }
#endif
