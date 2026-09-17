#include "image_based_lighting_background_pass_nvrhi.h"
#include "renderer_common_passes_nvrhi.h"
#include "renderer_shader_factory_nvrhi.h"

#include "renderer_view_nvrhi.h"

#include <algorithm>
#include <cmath>
#include <utility>


#include "image_based_lighting_background_cb.h"

namespace uvsr
{
    ImageBasedLightingBackgroundPass::ImageBasedLightingBackgroundPass(
        nvrhi::IDevice* device,
        RendererShaderFactory* shaderFactory,
        RendererCommonPasses* commonPasses,
        nvrhi::FramebufferHandle framebuffer,
        const RendererView& frameView,
        nvrhi::ITexture* radianceCube)
        : m_CommonPasses(commonPasses)
        , m_Framebuffer(std::move(framebuffer))
    {
        if (!device ||
            !shaderFactory ||
            !commonPasses ||
            !m_Framebuffer ||
            !radianceCube)
        {
            return;
        }

        m_PixelShader = shaderFactory->CreateShader(
            "uvsr/image_based_lighting_background_ps.hlsl",
            "main",
            {},
            nvrhi::ShaderType::Pixel);

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize =
            sizeof(ImageBasedLightingBackgroundConstants);
        constantBufferDesc.debugName = "UVSR IBL Background Constants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            RendererMaxConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDesc);

        nvrhi::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
        bindingLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Sampler(0)
        };
        m_BindingLayout =
            device->createBindingLayout(bindingLayoutDesc);

        if (!m_PixelShader || !m_ConstantBuffer || !m_BindingLayout ||
            !commonPasses->LinearWrapSampler() ||
            !commonPasses->FullscreenVertexShader() ||
            !commonPasses->FullscreenVertexShader(true))
        {
            return;
        }

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(
                0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(
                0, radianceCube),
            nvrhi::BindingSetItem::Sampler(
                0, commonPasses->LinearWrapSampler())
        };
        m_BindingSet = device->createBindingSet(
            bindingSetDesc, m_BindingLayout);

        if (!m_BindingSet ||
            !frameView.valid)
        {
            return;
        }
        m_ReverseDepth = frameView.reverseDepth;
        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = m_ReverseDepth
            ? commonPasses->FullscreenVertexShader()
            : commonPasses->FullscreenVertexShader(true);
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_BindingLayout };
        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState
            .enableDepthTest()
            .disableDepthWrite()
            .disableStencil()
            .setDepthFunc(m_ReverseDepth
                ? nvrhi::ComparisonFunc::GreaterOrEqual
                : nvrhi::ComparisonFunc::LessOrEqual);
        m_Pipeline = device->createGraphicsPipeline(
            pipelineDesc,
            m_Framebuffer->getFramebufferInfo());
    }

    ImageBasedLightingBackgroundRenderResult
        ImageBasedLightingBackgroundPass::Render(
        nvrhi::ICommandList* commandList,
        const RendererView& frameView,
        float radianceScale)
    {
        if (!commandList || !IsValid() || !frameView.valid || frameView.reverseDepth != m_ReverseDepth)
            return {};
        commandList->beginMarker("Image-Based Lighting Background");
        ImageBasedLightingBackgroundConstants constants{};
        constants.matClipToTranslatedWorld = RendererClipToTranslatedWorld(frameView);
        constants.radianceScale = std::max(std::isfinite(radianceScale) ? radianceScale : 0.f, 0.f);
        commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));
        nvrhi::GraphicsState state;
        state.pipeline = m_Pipeline;
        state.framebuffer = m_Framebuffer;
        state.bindings = { m_BindingSet };
        state.viewport = RendererViewportNvrhi(frameView);
        commandList->setGraphicsState(state);
        nvrhi::DrawArguments arguments;
        arguments.instanceCount = 1u;
        arguments.vertexCount = 4u;
        commandList->draw(arguments);
        commandList->endMarker();
        return {ImageBasedLightingBackgroundRenderStatus::Dispatched, 1u};
    }
}
