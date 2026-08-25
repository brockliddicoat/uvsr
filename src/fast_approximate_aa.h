#pragma once

#include "temporal_aa_options.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>

namespace donut::engine
{
    class ICompositeView;
}
namespace uvsr
{
    class RendererCommonPasses;
    class RendererShaderFactory;

    class FastApproximateAAPass
    {
    public:
        FastApproximateAAPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>&
                shaderFactory,
            const std::shared_ptr<RendererCommonPasses>&
                commonPasses,
            nvrhi::ITexture* sourceColor);

        [[nodiscard]] nvrhi::ITexture* Render(
            nvrhi::ICommandList* commandList,
            const donut::engine::ICompositeView& compositeView,
            nvrhi::ITexture* sourceColor,
            const ResolvedAntiAliasingSettings& settings);
        void UpdateSourceColor(nvrhi::ITexture* sourceColor);

        [[nodiscard]] bool IsValid() const;

    private:
        nvrhi::IDevice* m_Device = nullptr;
        std::shared_ptr<RendererCommonPasses> m_CommonPasses;
        uint32_t m_Width = 0u;
        uint32_t m_Height = 0u;
        nvrhi::TextureHandle m_OutputColor;
        nvrhi::FramebufferHandle m_OutputFramebuffer;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::GraphicsPipelineHandle m_Pipeline;
        nvrhi::ITexture* m_BoundSource = nullptr;

        [[nodiscard]] bool IsCompatibleSource(
            nvrhi::ITexture* sourceColor) const;
        void RebuildBindingSet(nvrhi::ITexture* sourceColor);
    };
}
