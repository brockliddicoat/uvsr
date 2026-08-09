#pragma once

#include "directional_shadow_settings.h"
#include "ray_traced_material_visibility.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class DirectionalLight;
    class IView;
    class Light;
    class ShaderFactory;
}

namespace uvsr
{
    struct HeitzRatioEstimatorShadowInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* diffuse = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* emissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;
    };

    struct HeitzRatioEstimatorShadowResult
    {
        nvrhi::ITexture* modulation = nullptr;
        nvrhi::ITexture* hitDistance = nullptr;
        const donut::engine::Light* light = nullptr;
        bool dispatched = false;
        bool stochastic = false;
        bool ratioEstimator = false;

        [[nodiscard]] explicit operator bool() const
        {
            return modulation && light;
        }
    };

    class HeitzRatioEstimatorShadowPass final
    {
    public:
        [[nodiscard]] static bool IsDeviceSupported(
            nvrhi::IDevice* device);

        HeitzRatioEstimatorShadowPass(
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

        [[nodiscard]] HeitzRatioEstimatorShadowResult Render(
            nvrhi::ICommandList* commandList,
            const HeitzRatioEstimatorShadowSettings& settings,
            const donut::engine::IView& view,
            const HeitzRatioEstimatorShadowInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas,
            const donut::engine::DirectionalLight* light,
            const NoiseSettings& noiseSettings,
            nvrhi::ITexture* noiseTexture,
            uint32_t samplingPhase,
            float sceneDiagonal);

        void ResetBindingCache();

    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_BindlessLayout;
        std::array<nvrhi::BindingLayoutHandle, 2> m_BindingLayouts;
        nvrhi::SamplerHandle m_MaterialSampler;
        nvrhi::BufferHandle m_ConstantBuffer;
        std::array<nvrhi::ShaderHandle, 2> m_Shaders;
        std::array<nvrhi::ComputePipelineHandle, 2> m_Pipelines;
        std::array<nvrhi::BindingSetHandle, 2> m_BindingSets;

        nvrhi::TextureHandle m_OutputModulation;
        nvrhi::TextureHandle m_OutputHitDistance;

        nvrhi::rt::IAccelStruct* m_BoundTlas = nullptr;
        HeitzRatioEstimatorShadowInputs m_BoundInputs;
        RayTracedMaterialVisibilityInputs m_BoundMaterialVisibility;
        nvrhi::ITexture* m_BoundNoiseTexture = nullptr;
        bool m_Supported = false;
        bool m_HitDistanceSupported = false;
        bool m_ReportedInvalidInput = false;
        bool m_ReportedHitDistanceUnavailable = false;

        [[nodiscard]] bool EnsureResources(
            const HeitzRatioEstimatorShadowInputs& inputs,
            bool outputHitDistance);
        [[nodiscard]] bool EnsureBindingSets(
            const HeitzRatioEstimatorShadowInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas,
            nvrhi::ITexture* noiseTexture,
            bool outputHitDistance);
        void ClearBindingSets();
    };
}
