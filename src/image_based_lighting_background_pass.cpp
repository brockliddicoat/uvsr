#include "image_based_lighting_background_pass.h"

#include <donut/core/math/math.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <cmath>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "image_based_lighting_background_cb.h"

namespace uvsr
{
    ImageBasedLightingBackgroundPass::ImageBasedLightingBackgroundPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        const std::shared_ptr<FramebufferFactory>& framebufferFactory,
        const ICompositeView& compositeView,
        nvrhi::ITexture* radianceCube)
        : m_CommonPasses(commonPasses)
        , m_FramebufferFactory(framebufferFactory)
    {
        if (!device ||
            !shaderFactory ||
            !commonPasses ||
            !framebufferFactory ||
            !radianceCube)
        {
            return;
        }

        m_PixelShader = shaderFactory->CreateShader(
            "uvsr/image_based_lighting_background_ps.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Pixel);

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize =
            sizeof(ImageBasedLightingBackgroundConstants);
        constantBufferDesc.debugName = "UVSR IBL Background Constants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            c_MaxRenderPassConstantBufferVersions;
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

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(
                0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(
                0, radianceCube),
            nvrhi::BindingSetItem::Sampler(
                0, commonPasses->m_LinearWrapSampler)
        };
        m_BindingSet = device->createBindingSet(
            bindingSetDesc, m_BindingLayout);

        const IView* sampleView =
            compositeView.GetChildView(ViewType::PLANAR, 0u);
        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = sampleView->IsReverseDepth()
            ? commonPasses->m_FullscreenVS
            : commonPasses->m_FullscreenAtOneVS;
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_BindingLayout };
        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState
            .enableDepthTest()
            .disableDepthWrite()
            .disableStencil()
            .setDepthFunc(sampleView->IsReverseDepth()
                ? nvrhi::ComparisonFunc::GreaterOrEqual
                : nvrhi::ComparisonFunc::LessOrEqual);
        m_Pipeline = device->createGraphicsPipeline(
            pipelineDesc,
            framebufferFactory->GetFramebufferInfo());
    }

    void ImageBasedLightingBackgroundPass::Render(
        nvrhi::ICommandList* commandList,
        const ICompositeView& compositeView,
        float radianceScale)
    {
        if (!commandList ||
            !m_Pipeline ||
            !m_BindingSet ||
            !m_FramebufferFactory)
        {
            return;
        }

        commandList->beginMarker("Image-Based Lighting Background");
        for (uint32_t viewIndex = 0u;
            viewIndex <
                compositeView.GetNumChildViews(ViewType::PLANAR);
            ++viewIndex)
        {
            const IView* view = compositeView.GetChildView(
                ViewType::PLANAR, viewIndex);
            ImageBasedLightingBackgroundConstants constants{};
            constants.matClipToTranslatedWorld =
                view->GetInverseViewProjectionMatrix() *
                affineToHomogeneous(
                    translation(-view->GetViewOrigin()));
            constants.radianceScale = std::max(
                std::isfinite(radianceScale)
                    ? radianceScale
                    : 0.f,
                0.f);
            commandList->writeBuffer(
                m_ConstantBuffer, &constants, sizeof(constants));

            nvrhi::GraphicsState state;
            state.pipeline = m_Pipeline;
            state.framebuffer =
                m_FramebufferFactory->GetFramebuffer(*view);
            state.bindings = { m_BindingSet };
            state.viewport = view->GetViewportState();
            commandList->setGraphicsState(state);

            nvrhi::DrawArguments arguments;
            arguments.instanceCount = 1u;
            arguments.vertexCount = 4u;
            commandList->draw(arguments);
        }
        commandList->endMarker();
    }
}
