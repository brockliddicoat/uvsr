#pragma once

#include "flashlight_shared.h"
#include "lighting_accumulation_pass.h"
#include "noise_settings.h"
#include "ray_traced_material_visibility.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class IView;
    class SpotLight;
}

namespace uvsr
{
    class RendererShaderFactory;

    struct RayTracedFlashlightShadowInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
    };

    struct RayTracedFlashlightShadowResult
    {
        // Per-covered-receiver R8 visibility: Texture2D at 1x and
        // Texture2DArray at MSAA, where slice N belongs to raster sample N.
        nvrhi::ITexture* visibility = nullptr;
        // Coherent closest-covered-receiver visibility and hit distance for
        // the single-surface AO/GI and denoising path.
        nvrhi::ITexture* closestVisibility = nullptr;
        nvrhi::ITexture* hitDistance = nullptr;
        const donut::engine::SpotLight* light = nullptr;
        uint32_t receiverSampleCount = 0u;
        bool dispatched = false;
        bool stochastic = false;

        [[nodiscard]] explicit operator bool() const
        {
            return visibility != nullptr && closestVisibility != nullptr &&
                light != nullptr && receiverSampleCount != 0u &&
                dispatched;
        }
    };

    class RayTracedFlashlightShadowPass final
    {
    public:
        [[nodiscard]] static bool IsDeviceSupported(
            nvrhi::IDevice* device);

        RayTracedFlashlightShadowPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>&
                shaderFactory,
            nvrhi::IBindingLayout* bindlessLayout);

        [[nodiscard]] bool IsSupported() const
        {
            return m_Supported;
        }

        [[nodiscard]] bool IsHitDistanceSupported() const
        {
            return m_HitDistanceSupported;
        }

        [[nodiscard]] RayTracedFlashlightShadowResult Render(
            nvrhi::ICommandList* commandList,
            const donut::engine::IView& view,
            const RayTracedFlashlightShadowInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas,
            const donut::engine::SpotLight* light,
            const FlashlightBeamProfile& beamProfile,
            const NoiseSettings& noiseSettings,
            nvrhi::ITexture* noiseTexture,
            uint32_t samplingPhase,
            float rayBiasMeters,
            bool outputHitDistance,
            const LightingSampleSchedule& sampleSchedule);

        void ResetBindingCache();

    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_BindlessLayout;
        nvrhi::BindingLayoutHandle m_VisibilityBindingLayout;
        nvrhi::BindingLayoutHandle m_HitDistanceBindingLayout;
        nvrhi::SamplerHandle m_MaterialSampler;
        nvrhi::BufferHandle m_ConstantBuffer;
        std::array<nvrhi::ShaderHandle, 5> m_VisibilityShaders;
        std::array<nvrhi::ShaderHandle, 5> m_HitDistanceShaders;
        std::array<nvrhi::ComputePipelineHandle, 5>
            m_VisibilityPipelines;
        std::array<nvrhi::ComputePipelineHandle, 5>
            m_HitDistancePipelines;
        std::array<nvrhi::BindingSetHandle, 5> m_VisibilityBindingSets;
        std::array<nvrhi::BindingSetHandle, 5> m_HitDistanceBindingSets;

        nvrhi::TextureHandle m_OutputVisibility;
        nvrhi::TextureHandle m_OutputClosestVisibility;
        nvrhi::TextureHandle m_OutputHitDistance;

        nvrhi::rt::IAccelStruct* m_BoundTlas = nullptr;
        RayTracedFlashlightShadowInputs m_BoundInputs;
        RayTracedMaterialVisibilityInputs m_BoundMaterialVisibility;
        nvrhi::ITexture* m_BoundNoiseTexture = nullptr;
        nvrhi::ITexture* m_BoundAttemptMask = nullptr;
        bool m_Supported = false;
        bool m_HitDistanceSupported = false;
        bool m_ReportedInvalidInput = false;

        [[nodiscard]] bool EnsureResources(
            const RayTracedFlashlightShadowInputs& inputs,
            bool outputHitDistance);
        [[nodiscard]] bool EnsureBindingSet(
            uint32_t variant,
            const RayTracedFlashlightShadowInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas,
            nvrhi::ITexture* noiseTexture,
            nvrhi::ITexture* attemptMask,
            bool outputHitDistance);
        void ClearBindingSets();
    };
}
