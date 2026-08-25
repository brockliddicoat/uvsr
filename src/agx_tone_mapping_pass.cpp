#include "agx_tone_mapping_pass.h"

#include <utility>
#include <vector>

#include <donut/engine/View.h>

#include "renderer_common_passes.h"
#include "renderer_shader_factory.h"

namespace uvsr
{
    AgxToneMappingPass::AgxToneMappingPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<RendererShaderFactory>& shaderFactory,
        const std::shared_ptr<RendererCommonPasses>& commonPasses,
        nvrhi::FramebufferHandle framebuffer)
        : m_Device(device)
        , m_CommonPasses(commonPasses)
        , m_Framebuffer(std::move(framebuffer))
    {
        if (!device || !shaderFactory || !commonPasses || !m_Framebuffer ||
            !commonPasses->FullscreenVertexShader())
        {
            return;
        }

        const std::vector<RendererShaderMacro> automaticExposureMacros = {
            RendererShaderMacro("UVSR_UNITY_EXPOSURE", "0")
        };
        m_PixelShader = shaderFactory->CreateShader(
            "uvsr/agx_tonemapping_ps.hlsl",
            "main",
            &automaticExposureMacros,
            nvrhi::ShaderType::Pixel);
        const std::vector<RendererShaderMacro> unityExposureMacros = {
            RendererShaderMacro("UVSR_UNITY_EXPOSURE", "1")
        };
        m_UnityExposurePixelShader = shaderFactory->CreateShader(
            "uvsr/agx_tonemapping_ps.hlsl",
            "main",
            &unityExposureMacros,
            nvrhi::ShaderType::Pixel);
        m_OutputPixelShader = shaderFactory->CreateShader(
            "uvsr/display_output_ps.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Pixel);

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::TypedBuffer_SRV(1)
        };
        m_BindingLayout = device->createBindingLayout(layoutDesc);
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0)
        };
        m_TextureOnlyBindingLayout = device->createBindingLayout(layoutDesc);

        if (!m_PixelShader || !m_UnityExposurePixelShader ||
            !m_OutputPixelShader || !m_BindingLayout ||
            !m_TextureOnlyBindingLayout)
        {
            return;
        }

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = commonPasses->FullscreenVertexShader();
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_BindingLayout };
        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;
        m_Pipeline = device->createGraphicsPipeline(
            pipelineDesc,
            m_Framebuffer->getFramebufferInfo());
        pipelineDesc.PS = m_UnityExposurePixelShader;
        pipelineDesc.bindingLayouts = { m_TextureOnlyBindingLayout };
        m_UnityExposurePipeline = device->createGraphicsPipeline(
            pipelineDesc,
            m_Framebuffer->getFramebufferInfo());
    }

    bool AgxToneMappingPass::Render(
        nvrhi::ICommandList* commandList,
        const donut::engine::ICompositeView& compositeView,
        nvrhi::ITexture* sourceTexture,
        nvrhi::IBuffer* exposureBuffer)
    {
        if (!commandList || !sourceTexture || !IsValid())
            return false;

        const bool useAutomaticExposure = exposureBuffer != nullptr;
        nvrhi::IGraphicsPipeline* pipeline = useAutomaticExposure
            ? m_Pipeline.Get()
            : m_UnityExposurePipeline.Get();

        nvrhi::IBindingSet* bindingSet = nullptr;
        if (useAutomaticExposure)
        {
            if (!m_BindingSet || m_BoundSource != sourceTexture ||
                m_BoundExposure != exposureBuffer)
            {
                nvrhi::BindingSetDesc bindingSetDesc;
                bindingSetDesc.bindings = {
                    nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture),
                    nvrhi::BindingSetItem::TypedBuffer_SRV(1, exposureBuffer)
                };
                m_BindingSet = m_Device->createBindingSet(
                    bindingSetDesc,
                    m_BindingLayout);
                if (m_BindingSet)
                {
                    m_BoundSource = sourceTexture;
                    m_BoundExposure = exposureBuffer;
                }
            }
            bindingSet = m_BindingSet;
        }
        if (!useAutomaticExposure)
        {
            if (!m_UnityExposureBindingSet ||
                m_BoundUnityExposureSource != sourceTexture)
            {
                nvrhi::BindingSetDesc bindingSetDesc;
                bindingSetDesc.bindings = {
                    nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture)
                };
                m_UnityExposureBindingSet = m_Device->createBindingSet(
                    bindingSetDesc,
                    m_TextureOnlyBindingLayout);
                if (m_UnityExposureBindingSet)
                    m_BoundUnityExposureSource = sourceTexture;
            }
            bindingSet = m_UnityExposureBindingSet;
        }
        if (!pipeline || !bindingSet)
            return false;

        const std::uint32_t viewCount = compositeView.GetNumChildViews(
            donut::engine::ViewType::PLANAR);
        if (viewCount == 0u)
            return false;

        commandList->beginMarker("AgX Tone Mapping");
        for (std::uint32_t viewIndex = 0; viewIndex < viewCount; ++viewIndex)
        {
            const donut::engine::IView* view = compositeView.GetChildView(
                donut::engine::ViewType::PLANAR,
                viewIndex);
            if (!view)
            {
                commandList->endMarker();
                return false;
            }
            nvrhi::GraphicsState state;
            state.pipeline = pipeline;
            state.framebuffer = m_Framebuffer;
            state.bindings = { bindingSet };
            state.viewport = view->GetViewportState();
            commandList->setGraphicsState(state);

            nvrhi::DrawArguments arguments;
            arguments.instanceCount = 1;
            arguments.vertexCount = 4;
            commandList->draw(arguments);
        }
        commandList->endMarker();
        return true;
    }

    bool AgxToneMappingPass::RenderOutput(
        nvrhi::ICommandList* commandList,
        const donut::engine::ICompositeView& compositeView,
        nvrhi::IFramebuffer* framebuffer,
        nvrhi::ITexture* sourceTexture)
    {
        if (!commandList || !framebuffer || !sourceTexture ||
            !m_Device || !m_CommonPasses || !m_OutputPixelShader ||
            !m_TextureOnlyBindingLayout ||
            !m_CommonPasses->FullscreenVertexShader())
        {
            return false;
        }

        const nvrhi::FramebufferInfoEx& framebufferInfo =
            framebuffer->getFramebufferInfo();
        if (framebufferInfo.colorFormats.empty())
            return false;
        const nvrhi::Format framebufferFormat =
            framebufferInfo.colorFormats[0];
        if (!m_OutputPipeline ||
            framebufferFormat != m_OutputFramebufferFormat)
        {
            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
            pipelineDesc.VS = m_CommonPasses->FullscreenVertexShader();
            pipelineDesc.PS = m_OutputPixelShader;
            pipelineDesc.bindingLayouts = { m_TextureOnlyBindingLayout };
            pipelineDesc.renderState.rasterState.setCullNone();
            pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
            pipelineDesc.renderState.depthStencilState.stencilEnable = false;
            m_OutputPipeline = m_Device->createGraphicsPipeline(
                pipelineDesc,
                framebufferInfo);
            m_OutputFramebufferFormat = framebufferFormat;
        }
        if (!m_OutputPipeline)
            return false;

        if (!m_OutputBindingSet || m_BoundOutputSource != sourceTexture)
        {
            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture)
            };
            m_OutputBindingSet = m_Device->createBindingSet(
                bindingSetDesc,
                m_TextureOnlyBindingLayout);
            if (m_OutputBindingSet)
                m_BoundOutputSource = sourceTexture;
        }
        if (!m_OutputBindingSet)
            return false;

        const std::uint32_t viewCount = compositeView.GetNumChildViews(
            donut::engine::ViewType::PLANAR);
        if (viewCount == 0u)
            return false;

        commandList->beginMarker("Display Transfer and Dither");
        for (std::uint32_t viewIndex = 0; viewIndex < viewCount; ++viewIndex)
        {
            const donut::engine::IView* view = compositeView.GetChildView(
                donut::engine::ViewType::PLANAR,
                viewIndex);
            if (!view)
            {
                commandList->endMarker();
                return false;
            }
            nvrhi::GraphicsState state;
            state.pipeline = m_OutputPipeline;
            state.framebuffer = framebuffer;
            state.bindings = { m_OutputBindingSet };
            state.viewport = view->GetViewportState();
            commandList->setGraphicsState(state);

            nvrhi::DrawArguments arguments;
            arguments.instanceCount = 1;
            arguments.vertexCount = 4;
            commandList->draw(arguments);
        }
        commandList->endMarker();
        return true;
    }
}
