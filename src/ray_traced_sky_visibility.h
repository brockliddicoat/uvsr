#pragma once

#include "ray_traced_sky_visibility_settings.h"
#include "ray_traced_material_visibility.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class IView;
    class ShaderFactory;
}

namespace uvsr
{
    struct RayTracedSkyVisibilityInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
    };

    struct RayTracedSkyVisibilityResult
    {
        nvrhi::ITexture* visibility = nullptr;
        nvrhi::ITexture* hitDistance = nullptr;
        bool dispatched = false;
        bool ratioEstimator = false;

        [[nodiscard]] explicit operator bool() const
        {
            return visibility != nullptr;
        }
    };

    class RayTracedSkyVisibilityPass final
    {
    public:
        [[nodiscard]] static bool IsDeviceSupported(
            nvrhi::IDevice* device);

        RayTracedSkyVisibilityPass(
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

        [[nodiscard]] RayTracedSkyVisibilityResult Render(
            nvrhi::ICommandList* commandList,
            const RayTracedSkyVisibilitySettings& settings,
            const donut::engine::IView& view,
            const RayTracedSkyVisibilityInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas,
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

        nvrhi::TextureHandle m_OutputVisibility;
        nvrhi::TextureHandle m_OutputHitDistance;

        nvrhi::rt::IAccelStruct* m_BoundTlas = nullptr;
        RayTracedSkyVisibilityInputs m_BoundInputs;
        RayTracedMaterialVisibilityInputs m_BoundMaterialVisibility;
        nvrhi::ITexture* m_BoundNoiseTexture = nullptr;
        bool m_Supported = false;
        bool m_HitDistanceSupported = false;
        bool m_ReportedInvalidInput = false;
        bool m_ReportedHitDistanceUnavailable = false;

        [[nodiscard]] bool EnsureResources(
            const RayTracedSkyVisibilityInputs& inputs,
            bool outputHitDistance);
        [[nodiscard]] bool EnsureBindingSet(
            const RayTracedSkyVisibilityInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas,
            nvrhi::ITexture* noiseTexture,
            bool outputHitDistance);
        void ClearBindingSets();
    };
}
