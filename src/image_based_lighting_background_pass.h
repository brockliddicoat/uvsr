#pragma once

#include <nvrhi/nvrhi.h>

#include <memory>

namespace donut::engine
{
    class CommonRenderPasses;
    class FramebufferFactory;
    class ICompositeView;
    class ShaderFactory;
}

namespace uvsr
{
    class ImageBasedLightingBackgroundPass
    {
    public:
        ImageBasedLightingBackgroundPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>&
                shaderFactory,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses,
            const std::shared_ptr<donut::engine::FramebufferFactory>&
                framebufferFactory,
            const donut::engine::ICompositeView& compositeView,
            nvrhi::ITexture* radianceCube);

        void Render(
            nvrhi::ICommandList* commandList,
            const donut::engine::ICompositeView& compositeView,
            float radianceScale);

    private:
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::GraphicsPipelineHandle m_Pipeline;
        std::shared_ptr<donut::engine::CommonRenderPasses>
            m_CommonPasses;
        std::shared_ptr<donut::engine::FramebufferFactory>
            m_FramebufferFactory;
    };
}
