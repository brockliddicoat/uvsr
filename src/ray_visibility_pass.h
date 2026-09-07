#pragma once

#include "directional_shadow_settings.h"
#include "flashlight_shared.h"
#include "lighting_surface.h"
#include "lighting_accumulation_pass.h"
#include "noise_settings.h"
#include "ray_scene_view.h"
#include "ray_traced_sky_visibility_result.h"
#include "ray_traced_sky_visibility_settings.h"

#include <nvrhi/nvrhi.h>
#include <array>
#include <memory>

namespace donut::engine
{
    class DirectionalLight;
    class IView;
    class Light;
    class SpotLight;
}

namespace uvsr
{
    class RendererShaderFactory;

    struct DirectionalRayVisibilityResult
    {

        nvrhi::ITexture* visibility = nullptr;

        const donut::engine::Light* light = nullptr;
        bool dispatched = false;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return visibility != nullptr &&
                light != nullptr &&
                dispatched;
        }
    };

    struct RayTracedFlashlightShadowResult
    {

        nvrhi::ITexture* visibility = nullptr;

        const donut::engine::SpotLight* light = nullptr;
        bool dispatched = false;
        bool stochastic = false;

        [[nodiscard]] explicit operator bool() const
        {
            return visibility != nullptr &&
                light != nullptr &&
                dispatched;
        }
    };

    class RayVisibilityPass final
    {
    public:
        enum class Kind { Sun, Flashlight, Sky };
        static bool IsDeviceSupported(nvrhi::IDevice* device, Kind kind);
        RayVisibilityPass(nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>& shaderFactory,
            nvrhi::IBindingLayout* bindlessLayout, Kind kind);
        bool IsSupported() const { return m_Supported; }

        DirectionalRayVisibilityResult RenderDirectional(nvrhi::ICommandList* commandList,
            const DirectionalShadowSettings& settings, const donut::engine::IView& view,
            const LightingSurfaceView& surface, const RaySceneView& rayScene,
            const donut::engine::DirectionalLight* light, float sceneDiagonal,
            const NoiseSettings& noiseSettings, nvrhi::ITexture* noiseTexture,
            uint32_t samplingPhase, const LightingSampleSchedule& sampleSchedule);
        RayTracedFlashlightShadowResult RenderFlashlight(nvrhi::ICommandList* commandList,
            const donut::engine::IView& view, const LightingSurfaceView& surface,
            const RaySceneView& rayScene, const donut::engine::SpotLight* light,
            const FlashlightBeamProfile& beamProfile, const NoiseSettings& noiseSettings,
            nvrhi::ITexture* noiseTexture, uint32_t samplingPhase, float rayBiasMeters,
            const LightingSampleSchedule& sampleSchedule, uint32_t sampleCount);
        RayTracedSkyVisibilityResult RenderSky(nvrhi::ICommandList* commandList,
            const RayTracedSkyVisibilitySettings& settings, const donut::engine::IView& view,
            const LightingSurfaceView& surface, const RaySceneView& rayScene,
            const NoiseSettings& noiseSettings, nvrhi::ITexture* noiseTexture,
            uint32_t samplingPhase, float sceneDiagonal, const LightingSampleSchedule& sampleSchedule);
        void ResetBindingCache();

    private:
        bool Prepare(const LightingSurfaceView& surface, const RaySceneView& rayScene,
            nvrhi::ITexture* noise, nvrhi::ITexture* attemptMask);
        void Dispatch(nvrhi::ICommandList* commandList, const donut::engine::IView& view,
            const LightingSurfaceView& surface, const RaySceneView& rayScene);
        void ReportInvalidInput();

        nvrhi::DeviceHandle m_Device;
        Kind m_Kind;
        nvrhi::BindingLayoutHandle m_BindlessLayout;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::SamplerHandle m_MaterialSampler;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::ComputePipelineHandle m_Pipeline;
        nvrhi::BindingSetHandle m_BindingSet;

        nvrhi::TextureHandle m_Output;
        LightingSurfaceView m_BoundSurface;
        RaySceneView m_BoundRayScene;
        nvrhi::ITexture* m_BoundNoise = nullptr;
        nvrhi::ITexture* m_BoundAttemptMask = nullptr;
        bool m_Supported = false;
        bool m_ReportedInvalidInput = false;
    };
}
