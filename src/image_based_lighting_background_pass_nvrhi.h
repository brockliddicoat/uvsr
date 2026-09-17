#pragma once

#include "image_based_lighting_background_pass.h"
#include <nvrhi/nvrhi.h>
#include <memory>

namespace uvsr
{
    class RendererCommonPasses;
    class RendererShaderFactory;
    struct RendererView;

    class ImageBasedLightingBackgroundPass
    {
    public:
        ImageBasedLightingBackgroundPass(
            nvrhi::IDevice* device,
            RendererShaderFactory*
                shaderFactory,
            RendererCommonPasses*
                commonPasses,
            nvrhi::FramebufferHandle framebuffer,
            const RendererView& frameView,
            nvrhi::ITexture* radianceCube);

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_Framebuffer && m_PixelShader && m_ConstantBuffer &&
                m_BindingLayout && m_BindingSet && m_Pipeline;
        }

        [[nodiscard]] ImageBasedLightingBackgroundRenderResult Render(
            nvrhi::ICommandList* commandList,
            const RendererView& frameView,
            float radianceScale);

    private:
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::GraphicsPipelineHandle m_Pipeline;
        RendererCommonPasses* m_CommonPasses = nullptr;
        nvrhi::FramebufferHandle m_Framebuffer;
        bool m_ReverseDepth = true;
    };
}
