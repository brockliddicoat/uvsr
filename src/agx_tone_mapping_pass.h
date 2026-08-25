#pragma once

#include <memory>

#include <nvrhi/nvrhi.h>

namespace donut::engine
{
    class ICompositeView;
}

namespace uvsr
{
    class RendererShaderFactory;
    class RendererCommonPasses;

    class AgxToneMappingPass
    {
    public:
        AgxToneMappingPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>& shaderFactory,
            const std::shared_ptr<RendererCommonPasses>& commonPasses,
            nvrhi::FramebufferHandle framebuffer);

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_Device && m_Framebuffer && m_PixelShader &&
                m_UnityExposurePixelShader && m_OutputPixelShader &&
                m_BindingLayout && m_TextureOnlyBindingLayout &&
                m_Pipeline && m_UnityExposurePipeline;
        }

        bool Render(
            nvrhi::ICommandList* commandList,
            const donut::engine::ICompositeView& compositeView,
            nvrhi::ITexture* sourceTexture,
            nvrhi::IBuffer* exposureBuffer);

        bool RenderOutput(
            nvrhi::ICommandList* commandList,
            const donut::engine::ICompositeView& compositeView,
            nvrhi::IFramebuffer* framebuffer,
            nvrhi::ITexture* sourceTexture);

    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::ShaderHandle m_UnityExposurePixelShader;
        nvrhi::ShaderHandle m_OutputPixelShader;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BindingLayoutHandle m_TextureOnlyBindingLayout;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::BindingSetHandle m_UnityExposureBindingSet;
        nvrhi::BindingSetHandle m_OutputBindingSet;
        nvrhi::GraphicsPipelineHandle m_Pipeline;
        nvrhi::GraphicsPipelineHandle m_UnityExposurePipeline;
        nvrhi::GraphicsPipelineHandle m_OutputPipeline;
        nvrhi::ITexture* m_BoundSource = nullptr;
        nvrhi::IBuffer* m_BoundExposure = nullptr;
        nvrhi::ITexture* m_BoundUnityExposureSource = nullptr;
        nvrhi::ITexture* m_BoundOutputSource = nullptr;
        nvrhi::Format m_OutputFramebufferFormat = nvrhi::Format::UNKNOWN;
        std::shared_ptr<RendererCommonPasses> m_CommonPasses;
        nvrhi::FramebufferHandle m_Framebuffer;
    };
}
