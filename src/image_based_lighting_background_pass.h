#pragma once

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

    enum class ImageBasedLightingBackgroundRenderStatus : std::uint8_t
    {
        Failed,
        Dispatched
    };

    struct ImageBasedLightingBackgroundRenderResult
    {
        ImageBasedLightingBackgroundRenderStatus status =
            ImageBasedLightingBackgroundRenderStatus::Failed;
        std::uint32_t dispatchedViewCount = 0u;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status ==
                ImageBasedLightingBackgroundRenderStatus::Dispatched;
        }
    };

    template <typename RenderView>
    [[nodiscard]] ImageBasedLightingBackgroundRenderResult
        ExecuteImageBasedLightingBackgroundViews(
            bool passReady,
            std::uint32_t viewCount,
            RenderView&& renderView)
    {
        if (!passReady || viewCount == 0u)
            return {};

        std::uint32_t dispatchedViewCount = 0u;
        for (std::uint32_t viewIndex = 0u;
            viewIndex < viewCount;
            ++viewIndex)
        {
            if (!renderView(viewIndex))
            {
                return {
                    ImageBasedLightingBackgroundRenderStatus::Failed,
                    dispatchedViewCount
                };
            }
            ++dispatchedViewCount;
        }
        return {
            ImageBasedLightingBackgroundRenderStatus::Dispatched,
            dispatchedViewCount
        };
    }

    class ImageBasedLightingBackgroundPass
    {
    public:
        ImageBasedLightingBackgroundPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>&
                shaderFactory,
            const std::shared_ptr<RendererCommonPasses>&
                commonPasses,
            nvrhi::FramebufferHandle framebuffer,
            const donut::engine::ICompositeView& compositeView,
            nvrhi::ITexture* radianceCube);

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_Framebuffer && m_PixelShader && m_ConstantBuffer &&
                m_BindingLayout && m_BindingSet && m_Pipeline;
        }

        [[nodiscard]] ImageBasedLightingBackgroundRenderResult Render(
            nvrhi::ICommandList* commandList,
            const donut::engine::ICompositeView& compositeView,
            float radianceScale);

    private:
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::GraphicsPipelineHandle m_Pipeline;
        std::shared_ptr<RendererCommonPasses> m_CommonPasses;
        nvrhi::FramebufferHandle m_Framebuffer;
        bool m_ReverseDepth = true;
    };
}
