#pragma once

#include "directional_shadow_settings.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>
#include <vector>

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
        const donut::engine::Light* light = nullptr;
        bool dispatched = false;
        bool stochastic = false;

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
            const std::vector<uint16_t>* preparedBlueNoise);

        [[nodiscard]] bool IsSupported() const
        {
            return m_Supported;
        }

        [[nodiscard]] HeitzRatioEstimatorShadowResult Render(
            nvrhi::ICommandList* commandList,
            const HeitzRatioEstimatorShadowSettings& settings,
            const donut::engine::IView& view,
            const HeitzRatioEstimatorShadowInputs& inputs,
            nvrhi::rt::IAccelStruct* worldTlas,
            const donut::engine::DirectionalLight* light,
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

        nvrhi::TextureHandle m_OutputModulation;

        nvrhi::rt::IAccelStruct* m_BoundTlas = nullptr;
        HeitzRatioEstimatorShadowInputs m_BoundInputs;
        bool m_Supported = false;
        bool m_ReportedInvalidInput = false;

        [[nodiscard]] bool EnsureResources(
            const HeitzRatioEstimatorShadowInputs& inputs);
        [[nodiscard]] bool EnsureBindingSets(
            const HeitzRatioEstimatorShadowInputs& inputs,
            nvrhi::rt::IAccelStruct* worldTlas);
        void ClearBindingSets();
        void UploadBlueNoise(nvrhi::ICommandList* commandList);
    };
}
