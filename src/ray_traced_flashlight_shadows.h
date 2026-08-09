#pragma once

#include "flashlight_shared.h"
#include "noise_settings.h"
#include "ray_traced_material_visibility.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>

namespace donut::engine
{
    class IView;
    class ShaderFactory;
    class SpotLight;
}

namespace uvsr
{
    struct RayTracedFlashlightShadowInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
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
            return visibility != nullptr && light != nullptr;
        }
    };

    class RayTracedFlashlightShadowPass final
    {
    public:
        [[nodiscard]] static bool IsDeviceSupported(
            nvrhi::IDevice* device);

        RayTracedFlashlightShadowPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>&
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
            bool outputHitDistance);

        void ResetBindingCache();

    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_BindlessLayout;
        nvrhi::BindingLayoutHandle m_VisibilityBindingLayout;
        nvrhi::BindingLayoutHandle m_HitDistanceBindingLayout;
        nvrhi::SamplerHandle m_MaterialSampler;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::ShaderHandle m_VisibilityShader;
        nvrhi::ShaderHandle m_HitDistanceShader;
        nvrhi::ComputePipelineHandle m_VisibilityPipeline;
        nvrhi::ComputePipelineHandle m_HitDistancePipeline;
        nvrhi::BindingSetHandle m_VisibilityBindingSet;
        nvrhi::BindingSetHandle m_HitDistanceBindingSet;

        nvrhi::TextureHandle m_OutputVisibility;
        nvrhi::TextureHandle m_OutputHitDistance;

        nvrhi::rt::IAccelStruct* m_BoundTlas = nullptr;
        RayTracedFlashlightShadowInputs m_BoundInputs;
        RayTracedMaterialVisibilityInputs m_BoundMaterialVisibility;
        nvrhi::ITexture* m_BoundNoiseTexture = nullptr;
        bool m_Supported = false;
        bool m_HitDistanceSupported = false;
        bool m_ReportedInvalidInput = false;

        [[nodiscard]] bool EnsureResources(
            const RayTracedFlashlightShadowInputs& inputs,
            bool outputHitDistance);
        [[nodiscard]] bool EnsureBindingSet(
            const RayTracedFlashlightShadowInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas,
            nvrhi::ITexture* noiseTexture,
            bool outputHitDistance);
        void ClearBindingSets();
    };
}
