#pragma once

#include "ray_traced_sky_visibility_settings.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>
#include <vector>

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
        bool dispatched = false;

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
            const std::vector<uint16_t>* preparedBlueNoise);

        [[nodiscard]] bool IsSupported() const
        {
            return m_Supported;
        }

        [[nodiscard]] RayTracedSkyVisibilityResult Render(
            nvrhi::ICommandList* commandList,
            const RayTracedSkyVisibilitySettings& settings,
            const donut::engine::IView& view,
            const RayTracedSkyVisibilityInputs& inputs,
            nvrhi::rt::IAccelStruct* worldTlas,
            uint32_t samplingPhase,
            float sceneDiagonal);

        void ResetBindingCache();

    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::ShaderHandle m_Shader;
        nvrhi::ComputePipelineHandle m_Pipeline;
        nvrhi::BindingSetHandle m_BindingSet;

        nvrhi::TextureHandle m_BlueNoise;
        std::vector<uint16_t> m_BlueNoiseUpload;
        bool m_BlueNoiseUploaded = false;

        nvrhi::TextureHandle m_OutputVisibility;

        nvrhi::rt::IAccelStruct* m_BoundTlas = nullptr;
        RayTracedSkyVisibilityInputs m_BoundInputs;
        bool m_Supported = false;
        bool m_ReportedInvalidInput = false;

        [[nodiscard]] bool EnsureResources(
            const RayTracedSkyVisibilityInputs& inputs);
        [[nodiscard]] bool EnsureBindingSet(
            const RayTracedSkyVisibilityInputs& inputs,
            nvrhi::rt::IAccelStruct* worldTlas);
        void ClearBindingSet();
        void UploadBlueNoise(nvrhi::ICommandList* commandList);
    };
}
