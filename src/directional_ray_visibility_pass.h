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
}

namespace uvsr
{
    class RendererShaderFactory;

    struct DirectionalRayVisibilityInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
    };

    struct DirectionalRayVisibilityResult
    {
        // R8_UNORM Texture2D at 1x or Texture2DArray at MSAA. Array slice N
        // is raster sample N.
        nvrhi::ITexture* visibility = nullptr;
        // R8_UNORM Texture2D visibility for the closest covered receiver.
        // This preserves the coherent single-surface input used by AO/GI.
        nvrhi::ITexture* closestVisibility = nullptr;
        // R32_FLOAT Texture2D physical blocker distance for the same closest
        // receiver. A finite maximum sentinel represents an unoccluded ray.
        nvrhi::ITexture* closestHitDistance = nullptr;
        const donut::engine::Light* light = nullptr;
        uint32_t receiverSampleCount = 0u;
        bool dispatched = false;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return visibility != nullptr && closestVisibility != nullptr &&
                closestHitDistance != nullptr && light != nullptr &&
                dispatched;
        }
    };

    class DirectionalRayVisibilityPass final
    {
    public:
        [[nodiscard]] static bool IsDeviceSupported(nvrhi::IDevice* device);

        DirectionalRayVisibilityPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>&
                shaderFactory,
            nvrhi::IBindingLayout* bindlessLayout);

        [[nodiscard]] bool IsSupported() const noexcept
        {
            return m_Supported;
        }

        [[nodiscard]] DirectionalRayVisibilityResult Render(
            nvrhi::ICommandList* commandList,
            const DirectionalShadowSettings& settings,
            const donut::engine::IView& view,
            const DirectionalRayVisibilityInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas,
            const donut::engine::DirectionalLight* light,
            float sceneDiagonal);

        void ResetBindingCache();

    private:
        [[nodiscard]] bool EnsureResources(
            const DirectionalRayVisibilityInputs& inputs);
        [[nodiscard]] bool EnsureBindingSet(
            uint32_t variant,
            const DirectionalRayVisibilityInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas);
        void ClearBindingSets();

        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_BindlessLayout;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::SamplerHandle m_MaterialSampler;
        nvrhi::BufferHandle m_ConstantBuffer;
        std::array<nvrhi::ShaderHandle, 5> m_Shaders;
        std::array<nvrhi::ComputePipelineHandle, 5> m_Pipelines;
        std::array<nvrhi::BindingSetHandle, 5> m_BindingSets;
        nvrhi::TextureHandle m_Visibility;
        nvrhi::TextureHandle m_ClosestVisibility;
        nvrhi::TextureHandle m_ClosestHitDistance;

        nvrhi::rt::IAccelStruct* m_BoundTlas = nullptr;
        DirectionalRayVisibilityInputs m_BoundInputs;
        RayTracedMaterialVisibilityInputs m_BoundMaterialVisibility;
        bool m_Supported = false;
        bool m_ReportedInvalidInput = false;
    };
}
