#pragma once

#include "tone_mapping_settings.h"
#include "gpu_adapter_catalog.h"

#include "auto_exposure.h"
#include "camera_controllers.h"
#include "directional_shadow_settings.h"
#include "display_presentation.h"
#include "flashlight.h"
#include "image_based_lighting_shared.h"
#include "image_based_lighting_sources.h"
#include "noise_settings.h"
#include "path_tracing_settings.h"
#include "pixel_zoom.h"
#include "ray_traced_sky_visibility_settings.h"
#include "fast_approximate_aa_options.h"
#include "world_space_representation_nvrhi.h"
#include "renderer_scene.h"
#include "renderer_scene_ray.h"
#include "renderer_scene_material_mode.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace uvsr
{
    enum class PbrLightingDebugView : std::uint32_t
    {
        None,
        ShadingNormal,
        GeometricNormal,
        NormalDifference,
        DiffuseEnvironment,
        EnvironmentDirection,
        PrefilteredSpecularEnvironment,
        EnvironmentBrdf,
        FinalSpecularEnvironment,
        CombinedEnvironment,
        SpecularOcclusion,
        EnvironmentMip,
        SkyVisibility
    };

    static_assert(
        static_cast<std::uint32_t>(PbrLightingDebugView::SkyVisibility) ==
        12u);

    struct UIData
    {
        bool ShowUI = false;
        bool DisplaySyncTestActive = false;
        double DisplaySyncPosition = 0.0;
        DisplayPresentationSettings Presentation = DefaultDisplayPresentationSettings;
        bool OverrideVisualMaxes = false;
        PixelZoomMode PixelZoom = PixelZoomMode::Off;
        GpuAdapterCatalog GpuAdapterChoices;
        int ActiveGpuAdapterIndex = -1;
        LightingSolution Lighting = LightingSolution::RayMarching;
        PathTracingSettings PathTracing;
        bool AccumulateSamples = false;
        AntiAliasingSettings AntiAliasing;
        ToneMappingSettings ToneMapping;
        DirectionalShadowSettings DirectionalShadows;
        WorldSpaceRepresentationSettings Representation;
        NoiseSettings Noise;
        RayTracedSkyVisibilitySettings RayTracedSkyVisibility;
        bool ShaderReloadRequested = false;
        bool FlashlightEnabled = DefaultFlashlightEnabled;
        FlashlightSettings Flashlight = DefaultFlashlightSettings;
        bool ShowEnvironmentBackground = true;
        bool EnableAmbientFill = true;
        bool EnableDiffuseIbl = true;
        float DiffuseIblStrength = 1.f;
        bool EnableSpecularIbl = true;
        float SpecularIblStrength = 1.f;
        WhiteWorldMode WhiteWorld = WhiteWorldMode::Off;
        ImageBasedLightingSource EnvironmentSource =
            ImageBasedLightingSource::Kloppenheim03Day;
        float EnvironmentExposureStops = GetImageBasedLightingSourceInfo(
            EnvironmentSource).defaultExposureStops;
        AutoExposureSettings AutoExposure;
        PbrLightingDebugView LightingDebugView = PbrLightingDebugView::None;
        CameraMode Camera = CameraMode::ThirdPerson;
        RendererSceneHandle SelectedMaterial;
        RendererSceneHandle SelectedNode;
        bool ShowMaterialDrawer = false;
        bool CopyScreenshotToClipboard = false;

        [[nodiscard]] ResolvedAntiAliasingSettings
            GetResolvedAntiAliasingSettings(
                const AntiAliasingSettings& settings) const
        {
            return ResolveAntiAliasingSettings(settings);
        }

        [[nodiscard]] ResolvedAntiAliasingSettings
            GetResolvedAntiAliasingSettings() const
        {
            return GetResolvedAntiAliasingSettings(AntiAliasing);
        }



        [[nodiscard]] bool UsesFastApproximateAA() const
        {
            return GetResolvedAntiAliasingSettings().fastApproximateEnabled;
        }
    };

    enum class RendererTimingStage : std::uint32_t
    {
        CompleteFrame,
        SceneSetup,
        Geometry,
        PathTransport,
        ShadowRayDispatch,
        SkyVisibilityRayDispatch,
        DirectLighting,
        MaterialPicking,
        EnvironmentBackground,
        AutoExposure,
        ToneMapping,
        FastApproximate,
        OutputBlit,
        Count
    };

    struct RendererTimings
    {
        std::array<float,
            static_cast<std::size_t>(RendererTimingStage::Count)>
            milliseconds{};
        std::array<bool,
            static_cast<std::size_t>(RendererTimingStage::Count)>
            available{};

        [[nodiscard]] float Get(RendererTimingStage stage) const
        {
            return milliseconds[static_cast<std::size_t>(stage)];
        }

        [[nodiscard]] bool IsAvailable(RendererTimingStage stage) const
        {
            return available[static_cast<std::size_t>(stage)];
        }
    };
}
