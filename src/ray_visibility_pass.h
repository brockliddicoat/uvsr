#pragma once

#include "directional_shadow_settings.h"
#include "denoising_replay.h"
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




        nvrhi::ITexture* hitDistance = nullptr;
        const donut::engine::Light* light = nullptr;
        bool dispatched = false;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return visibility != nullptr &&
                hitDistance != nullptr && light != nullptr &&
                dispatched;
        }
    };

    struct RayTracedFlashlightShadowResult
    {


        nvrhi::ITexture* visibility = nullptr;


        nvrhi::ITexture* hitDistance = nullptr;
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
        bool IsHitDistanceSupported() const { return m_HitDistanceSupported; }

        DirectionalRayVisibilityResult RenderDirectional(nvrhi::ICommandList* commandList,
            const DirectionalShadowSettings& settings, const donut::engine::IView& view,
            const LightingSurfaceView& surface, const RaySceneView& rayScene,
            const donut::engine::DirectionalLight* light, float sceneDiagonal, const DenoisingReplayInputs& replay = {});
        RayTracedFlashlightShadowResult RenderFlashlight(nvrhi::ICommandList* commandList,
            const donut::engine::IView& view, const LightingSurfaceView& surface,
            const RaySceneView& rayScene, const donut::engine::SpotLight* light,
            const FlashlightBeamProfile& beamProfile, const NoiseSettings& noiseSettings,
            nvrhi::ITexture* noiseTexture, uint32_t samplingPhase, float rayBiasMeters,
            bool outputHitDistance, const LightingSampleSchedule& sampleSchedule, const DenoisingReplayInputs& replay = {});
        RayTracedSkyVisibilityResult RenderSky(nvrhi::ICommandList* commandList,
            const RayTracedSkyVisibilitySettings& settings, const donut::engine::IView& view,
            const LightingSurfaceView& surface, const RaySceneView& rayScene,
            const NoiseSettings& noiseSettings, nvrhi::ITexture* noiseTexture,
            uint32_t samplingPhase, float sceneDiagonal, const LightingSampleSchedule& sampleSchedule, const DenoisingReplayInputs& replay = {});
        void ResetBindingCache();
        nvrhi::ITexture* GetTemporalGradient() const { return m_Gradient; }

    private:
        bool Prepare(const LightingSurfaceView& surface, const RaySceneView& rayScene,
            nvrhi::ITexture* noise, nvrhi::ITexture* attemptMask, bool& outputHitDistance);
        void Dispatch(nvrhi::ICommandList* commandList, const donut::engine::IView& view,
            const LightingSurfaceView& surface, const RaySceneView& rayScene, bool outputHitDistance,
            const void* constants, size_t constantSize, uint32_t phase, const DenoisingReplayInputs& replay);
        void ReportInvalidInput();

        nvrhi::BindingLayoutHandle m_ReplayLayout;
        nvrhi::ComputePipelineHandle m_ReplayPipeline;
        nvrhi::BufferHandle m_ReplayConstants;
        nvrhi::TextureHandle m_Gradient;
        uint64_t m_ReplayFrame = ~uint64_t(0);
        uint32_t m_PreviousPhase = 0u;
        nvrhi::DeviceHandle m_Device;
        Kind m_Kind;
        nvrhi::BindingLayoutHandle m_BindlessLayout;
        std::array<nvrhi::BindingLayoutHandle, 2> m_BindingLayouts;
        nvrhi::SamplerHandle m_MaterialSampler;
        nvrhi::BufferHandle m_ConstantBuffer;
        std::array<nvrhi::ComputePipelineHandle, 2> m_Pipelines;
        std::array<nvrhi::BindingSetHandle, 2> m_BindingSets;

        std::array<nvrhi::TextureHandle, 2> m_Outputs;
        LightingSurfaceView m_BoundSurface;
        RaySceneView m_BoundRayScene;
        nvrhi::ITexture* m_BoundNoise = nullptr;
        nvrhi::ITexture* m_BoundAttemptMask = nullptr;
        bool m_Supported = false;
        bool m_HitDistanceSupported = false;
        bool m_ReportedInvalidInput = false;
        bool m_ReportedHitDistanceUnavailable = false;
    };
}
