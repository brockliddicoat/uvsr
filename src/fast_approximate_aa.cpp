#include "fast_approximate_aa.h"

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

namespace
{
    struct alignas(16) FastApproximateAaConstants
    {
        float2 reciprocalSourceSize;
        float edgeSharpness =
            uvsr::FastApproximateAaDefaultEdgeSharpness;
        float edgeThreshold =
            uvsr::FastApproximateAaDefaultEdgeThreshold;

        float darkEdgeThreshold =
            uvsr::FastApproximateAaDefaultDarkEdgeThreshold;
        float3 padding = float3::zero();
    };

    static_assert(sizeof(FastApproximateAaConstants) == 32u);
}

namespace uvsr
{
    FastApproximateAAPass::FastApproximateAAPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        nvrhi::ITexture* sourceColor)
        : m_Device(device)
        , m_CommonPasses(commonPasses)
    {
        if (!device || !shaderFactory || !commonPasses || !sourceColor)
            return;

        const nvrhi::TextureDesc& sourceDesc = sourceColor->getDesc();
        if (sourceDesc.width == 0u ||
            sourceDesc.height == 0u ||
            sourceDesc.sampleCount != 1u ||
            sourceDesc.dimension != nvrhi::TextureDimension::Texture2D ||
            sourceDesc.format != nvrhi::Format::RGBA16_FLOAT)
        {
            log::error(
                "Fast Approximate AA requires UVSR's single-sample "
                "RGBA16F display-linear target");
            return;
        }

        m_Width = sourceDesc.width;
        m_Height = sourceDesc.height;
        m_PixelShader = shaderFactory->CreateShader(
            "uvsr/fast_approximate_aa_ps.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Pixel);

        nvrhi::TextureDesc outputDesc;
        outputDesc.width = m_Width;
        outputDesc.height = m_Height;
        outputDesc.dimension = nvrhi::TextureDimension::Texture2D;
        outputDesc.mipLevels = 1u;
        outputDesc.format = nvrhi::Format::RGBA16_FLOAT;
        outputDesc.isRenderTarget = true;
        outputDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        outputDesc.keepInitialState = true;
        outputDesc.debugName = "Fast Approximate AA/Output Color";
        m_OutputColor = device->createTexture(outputDesc);
        m_OutputFramebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(m_OutputColor));

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(FastApproximateAaConstants);
        constantBufferDesc.debugName = "Fast Approximate AA Constants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDesc);

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Sampler(0)
        };
        m_BindingLayout = device->createBindingLayout(layoutDesc);
        RebuildBindingSet(sourceColor);

        if (!m_OutputFramebuffer || !m_BindingLayout)
            return;

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = commonPasses->m_FullscreenVS;
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_BindingLayout };
        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;
        m_Pipeline = device->createGraphicsPipeline(
            pipelineDesc,
            m_OutputFramebuffer->getFramebufferInfo());
    }

    bool FastApproximateAAPass::IsCompatibleSource(
        nvrhi::ITexture* sourceColor) const
    {
        if (!sourceColor || sourceColor == m_OutputColor.Get())
            return false;
        const nvrhi::TextureDesc& sourceDesc = sourceColor->getDesc();
        return sourceDesc.width == m_Width &&
            sourceDesc.height == m_Height &&
            sourceDesc.sampleCount == 1u &&
            sourceDesc.dimension == nvrhi::TextureDimension::Texture2D &&
            sourceDesc.format == nvrhi::Format::RGBA16_FLOAT;
    }

    void FastApproximateAAPass::RebuildBindingSet(
        nvrhi::ITexture* sourceColor)
    {
        if (!IsCompatibleSource(sourceColor))
        {
            // Do not pin a replaced render target or leave an apparently
            // valid binding alive after an incompatible source update.
            m_BindingSet = nullptr;
            m_BoundSource = nullptr;
            return;
        }
        if (sourceColor == m_BoundSource ||
            !m_BindingLayout ||
            !m_ConstantBuffer ||
            !m_CommonPasses)
        {
            return;
        }

        nvrhi::BindingSetDesc setDesc;
        setDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, sourceColor),
            nvrhi::BindingSetItem::Sampler(
                0, m_CommonPasses->m_LinearClampSampler)
        };
        m_BindingSet = m_Device->createBindingSet(
            setDesc, m_BindingLayout);
        m_BoundSource = m_BindingSet ? sourceColor : nullptr;
    }

    void FastApproximateAAPass::UpdateSourceColor(
        nvrhi::ITexture* sourceColor)
    {
        RebuildBindingSet(sourceColor);
    }

    bool FastApproximateAAPass::IsValid() const
    {
        return m_Device &&
            m_CommonPasses &&
            m_OutputColor &&
            m_OutputFramebuffer &&
            m_PixelShader &&
            m_ConstantBuffer &&
            m_BindingLayout &&
            m_BindingSet &&
            m_Pipeline;
    }

    nvrhi::ITexture* FastApproximateAAPass::Render(
        nvrhi::ICommandList* commandList,
        const ICompositeView& compositeView,
        nvrhi::ITexture* sourceColor,
        const ResolvedAntiAliasingSettings& settings)
    {
        if (!commandList ||
            !settings.fastApproximateEnabled ||
            !IsValid() ||
            !IsCompatibleSource(sourceColor))
        {
            return sourceColor;
        }

        if (compositeView.GetNumChildViews(ViewType::PLANAR) != 1u)
            return sourceColor;
        const IView* view = compositeView.GetChildView(
            ViewType::PLANAR, 0u);
        if (!view)
            return sourceColor;
        const nvrhi::TextureSubresourceSet subresources =
            view->GetSubresources();
        const nvrhi::Rect extent = view->GetViewExtent();
        if (subresources.baseMipLevel != 0u ||
            subresources.numMipLevels != 1u ||
            subresources.baseArraySlice != 0u ||
            subresources.numArraySlices != 1u ||
            extent.minX != 0 ||
            extent.minY != 0 ||
            extent.maxX != static_cast<int>(m_Width) ||
            extent.maxY != static_cast<int>(m_Height))
        {
            // The owned output framebuffer represents one complete 2D image.
            // Fail closed instead of returning partly stale multi-view data.
            return sourceColor;
        }

        RebuildBindingSet(sourceColor);
        if (!m_BindingSet)
            return sourceColor;

        FastApproximateAaConstants constants{};
        constants.reciprocalSourceSize = float2(
            1.f / float(m_Width),
            1.f / float(m_Height));
        constants.edgeSharpness = settings.fastApproximateEdgeSharpness;
        constants.edgeThreshold = settings.fastApproximateEdgeThreshold;
        constants.darkEdgeThreshold =
            settings.fastApproximateDarkEdgeThreshold;

        commandList->beginMarker("Fast Approximate AA");
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));
        nvrhi::GraphicsState state;
        state.pipeline = m_Pipeline;
        state.framebuffer = m_OutputFramebuffer;
        state.bindings = { m_BindingSet };
        state.viewport = view->GetViewportState();
        commandList->setGraphicsState(state);

        nvrhi::DrawArguments arguments;
        arguments.instanceCount = 1u;
        arguments.vertexCount = 4u;
        commandList->draw(arguments);
        commandList->endMarker();
        return m_OutputColor;
    }
}
