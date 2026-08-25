#pragma once

#include "lighting_accumulation_pass.h"
#include "ray_traced_sky_visibility_result.h"
#include "ray_traced_sky_visibility_settings.h"
#include "ray_traced_material_visibility.h"
#include "ray_traced_sky_visibility_bindings.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class IView;
}

namespace uvsr
{
    class RendererShaderFactory;

    struct RayTracedSkyVisibilityInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
    };

    class RayTracedSkyVisibilityPass final
    {
    public:
        [[nodiscard]] static bool IsDeviceSupported(
            nvrhi::IDevice* device);

        RayTracedSkyVisibilityPass(
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
            float sceneDiagonal,
            const LightingSampleSchedule& sampleSchedule);

        void ResetBindingCache();

    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_BindlessLayout;
        std::array<nvrhi::BindingLayoutHandle, 2> m_BindingLayouts;
        nvrhi::SamplerHandle m_MaterialSampler;
        nvrhi::BufferHandle m_ConstantBuffer;
        std::array<std::array<nvrhi::ShaderHandle, 5>, 2> m_Shaders;
        std::array<std::array<nvrhi::ComputePipelineHandle, 5>, 2>
            m_Pipelines;
        std::array<std::array<nvrhi::BindingSetHandle, 5>, 2>
            m_BindingSets;

        nvrhi::TextureHandle m_OutputVisibility;
        nvrhi::TextureHandle m_OutputClosestVisibility;
        nvrhi::TextureHandle m_OutputHitDistance;

        RayTracedSkyVisibilityBindingIdentity m_BoundIdentity;
        bool m_Supported = false;
        bool m_HitDistanceSupported = false;
        bool m_ReportedInvalidInput = false;
        bool m_ReportedHitDistanceUnavailable = false;

        [[nodiscard]] bool EnsureResources(
            const RayTracedSkyVisibilityInputs& inputs,
            bool outputHitDistance);
        [[nodiscard]] bool EnsureBindingSet(
            uint32_t variant,
            const RayTracedSkyVisibilityInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas,
            nvrhi::ITexture* noiseTexture,
            nvrhi::ITexture* attemptMask,
            bool outputHitDistance);
        void ClearBindingSets();
    };
}
