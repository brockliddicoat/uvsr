/*
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "renderer_common_passes.h"

#include "renderer_log.h"
#include "renderer_shader_factory.h"

#include <algorithm>

namespace uvsr
{
RendererCommonPasses::RendererCommonPasses(
    nvrhi::IDevice* device,
    const std::shared_ptr<RendererShaderFactory>& shaderFactory)
    : m_Device(device)
{
    m_Initialization.device = bool(m_Device);
    if (!device || !shaderFactory)
        return;
    RendererResourceCreationSequence creationSequence;

    std::vector<RendererShaderMacro> fullscreenMacros = {
        { "UVSR_FULLSCREEN_DEPTH", "0" }
    };
    m_FullscreenVS = shaderFactory->CreateShader(
        "uvsr/renderer_fullscreen_vs.hlsl",
        "main",
        &fullscreenMacros,
        nvrhi::ShaderType::Vertex);
    fullscreenMacros[0].definition = "1";
    m_FullscreenAtOneVS = shaderFactory->CreateShader(
        "uvsr/renderer_fullscreen_vs.hlsl",
        "main",
        &fullscreenMacros,
        nvrhi::ShaderType::Vertex);
    m_BlitPS = shaderFactory->CreateShader(
        "uvsr/renderer_blit_ps.hlsl",
        "main",
        nullptr,
        nvrhi::ShaderType::Pixel);
    m_Initialization.fullscreenZeroShader = bool(m_FullscreenVS);
    m_Initialization.fullscreenOneShader = bool(m_FullscreenAtOneVS);
    m_Initialization.blitShader = bool(m_BlitPS);
    if (!creationSequence.Require([this]
        {
            return m_FullscreenVS && m_FullscreenAtOneVS && m_BlitPS;
        }))
    {
        log::error("Renderer common shader creation failed");
        return;
    }

    auto samplerDescription = nvrhi::SamplerDesc()
        .setAllFilters(true)
        .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
    m_LinearClampSampler = device->createSampler(samplerDescription);
    samplerDescription.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap);
    m_LinearWrapSampler = device->createSampler(samplerDescription);
    m_Initialization.linearClampSampler = bool(m_LinearClampSampler);
    m_Initialization.linearWrapSampler = bool(m_LinearWrapSampler);
    if (!creationSequence.Require([this]
        {
            return m_LinearClampSampler && m_LinearWrapSampler;
        }))
    {
        log::error("Renderer common sampler creation failed");
        return;
    }

    const std::uint32_t black = 0xff000000u;
    const std::uint32_t white = 0xffffffffu;
    nvrhi::TextureDesc textureDescription;
    textureDescription.format = nvrhi::Format::RGBA8_UNORM;
    textureDescription.width = 1u;
    textureDescription.height = 1u;
    textureDescription.mipLevels = 1u;
    textureDescription.debugName = "Renderer/Black Texture";
    m_BlackTexture = device->createTexture(textureDescription);
    textureDescription.debugName = "Renderer/White Texture";
    m_WhiteTexture = device->createTexture(textureDescription);

    textureDescription.dimension = nvrhi::TextureDimension::TextureCubeArray;
    textureDescription.arraySize = 6u;
    textureDescription.debugName = "Renderer/Black Cube Array";
    m_BlackCubeArray = device->createTexture(textureDescription);

    textureDescription.dimension = nvrhi::TextureDimension::Texture2DArray;
    textureDescription.arraySize = 1u;
    textureDescription.format = nvrhi::Format::D24S8;
    textureDescription.isRenderTarget = true;
    textureDescription.isTypeless = true;
    textureDescription.debugName = "Renderer/Black Depth Array";
    m_BlackDepthArray = device->createTexture(textureDescription);

    m_Initialization.blackTexture = bool(m_BlackTexture);
    m_Initialization.whiteTexture = bool(m_WhiteTexture);
    m_Initialization.blackCubeArray = bool(m_BlackCubeArray);
    m_Initialization.blackDepthArray = bool(m_BlackDepthArray);

    if (!creationSequence.Require([this]
        {
            return m_BlackTexture && m_WhiteTexture && m_BlackCubeArray &&
                m_BlackDepthArray;
        }))
    {
        log::error("Renderer common fallback texture creation failed");
        return;
    }

    nvrhi::BindingLayoutDesc layoutDescription;
    layoutDescription.visibility = nvrhi::ShaderType::All;
    layoutDescription.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Sampler(0)
    };
    m_BlitBindingLayout = device->createBindingLayout(layoutDescription);
    m_Initialization.blitBindingLayout = bool(m_BlitBindingLayout);
    if (!creationSequence.Require([this]
        {
            return bool(m_BlitBindingLayout);
        }))
    {
        log::error("Renderer common blit binding-layout creation failed");
        return;
    }

    nvrhi::CommandListHandle commandList = device->createCommandList();
    m_Initialization.uploadCommandList = bool(commandList);
    if (!creationSequence.Require([&commandList]
        {
            return bool(commandList);
        }))
    {
        log::error("Renderer common upload command-list creation failed");
        return;
    }
    commandList->open();
    commandList->beginTrackingTextureState(
        m_BlackTexture,
        nvrhi::AllSubresources,
        nvrhi::ResourceStates::Common);
    commandList->beginTrackingTextureState(
        m_WhiteTexture,
        nvrhi::AllSubresources,
        nvrhi::ResourceStates::Common);
    commandList->beginTrackingTextureState(
        m_BlackCubeArray,
        nvrhi::AllSubresources,
        nvrhi::ResourceStates::Common);
    commandList->beginTrackingTextureState(
        m_BlackDepthArray,
        nvrhi::AllSubresources,
        nvrhi::ResourceStates::Common);
    commandList->writeTexture(m_BlackTexture, 0u, 0u, &black, 0u);
    commandList->writeTexture(m_WhiteTexture, 0u, 0u, &white, 0u);
    for (std::uint32_t slice = 0u; slice < 6u; ++slice)
        commandList->writeTexture(m_BlackCubeArray, slice, 0u, &black, 0u);
    commandList->clearDepthStencilTexture(
        m_BlackDepthArray,
        nvrhi::AllSubresources,
        true,
        0.f,
        true,
        0u);
    commandList->setPermanentTextureState(
        m_BlackTexture,
        nvrhi::ResourceStates::ShaderResource);
    commandList->setPermanentTextureState(
        m_WhiteTexture,
        nvrhi::ResourceStates::ShaderResource);
    commandList->setPermanentTextureState(
        m_BlackCubeArray,
        nvrhi::ResourceStates::ShaderResource);
    commandList->setPermanentTextureState(
        m_BlackDepthArray,
        nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();
    commandList->close();
    device->executeCommandList(commandList);
    m_Initialization.uploadSubmitted = true;
}

bool RendererCommonPasses::IsValid() const
{
    return m_Initialization.IsComplete();
}

nvrhi::IShader* RendererCommonPasses::FullscreenVertexShader(
    bool farDepth) const
{
    return farDepth ? m_FullscreenAtOneVS.Get() : m_FullscreenVS.Get();
}

nvrhi::ISampler* RendererCommonPasses::LinearClampSampler() const
{
    return m_LinearClampSampler.Get();
}

nvrhi::ISampler* RendererCommonPasses::LinearWrapSampler() const
{
    return m_LinearWrapSampler.Get();
}

nvrhi::ITexture* RendererCommonPasses::BlackTexture() const
{
    return m_BlackTexture.Get();
}

nvrhi::ITexture* RendererCommonPasses::WhiteTexture() const
{
    return m_WhiteTexture.Get();
}

nvrhi::ITexture* RendererCommonPasses::BlackCubeArray() const
{
    return m_BlackCubeArray.Get();
}

nvrhi::ITexture* RendererCommonPasses::BlackDepthArray() const
{
    return m_BlackDepthArray.Get();
}

bool RendererCommonPasses::HasBlitPipelineFailure() const
{
    return m_BlitPipelineFailure.HasFailed();
}

nvrhi::GraphicsPipelineHandle RendererCommonPasses::GetBlitPipeline(
    const nvrhi::FramebufferInfo& framebufferInfo)
{
    if (!m_BlitPipelineFailure.CanAttempt())
        return nullptr;
    const auto existing = std::find_if(
        m_BlitPipelines.begin(),
        m_BlitPipelines.end(),
        [&framebufferInfo](const auto& item)
        {
            return item.first == framebufferInfo;
        });
    if (existing != m_BlitPipelines.end())
        return existing->second;

    nvrhi::GraphicsPipelineDesc description;
    description.bindingLayouts = { m_BlitBindingLayout };
    description.VS = m_FullscreenVS;
    description.PS = m_BlitPS;
    description.primType = nvrhi::PrimitiveType::TriangleStrip;
    description.renderState.rasterState.setCullNone();
    description.renderState.depthStencilState.depthTestEnable = false;
    description.renderState.depthStencilState.stencilEnable = false;
    nvrhi::GraphicsPipelineHandle pipeline =
        m_Device->createGraphicsPipeline(description, framebufferInfo);
    m_BlitPipelineFailure.RecordResult(bool(pipeline));
    RendererResourceCreationSequence creationSequence;
    if (!creationSequence.Require([&pipeline]
        {
            return bool(pipeline);
        }))
    {
        log::error("Renderer common blit pipeline creation failed");
        return nullptr;
    }
    m_BlitPipelines.emplace_back(framebufferInfo, pipeline);
    return pipeline;
}

bool RendererCommonPasses::BlitTexture(
    nvrhi::ICommandList* commandList,
    nvrhi::IFramebuffer* targetFramebuffer,
    nvrhi::ITexture* sourceTexture)
{
    if (!commandList || !targetFramebuffer || !sourceTexture || !IsValid())
        return false;
    if (sourceTexture->getDesc().dimension !=
        nvrhi::TextureDimension::Texture2D)
    {
        return false;
    }

    const nvrhi::FramebufferInfoEx& framebufferInfo =
        targetFramebuffer->getFramebufferInfo();
    nvrhi::GraphicsPipelineHandle pipeline = GetBlitPipeline(framebufferInfo);
    if (!pipeline)
        return false;

    nvrhi::BindingSetDesc bindingDescription;
    bindingDescription.bindings = {
        nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture),
        nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler)
    };
    nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(
        bindingDescription,
        m_BlitBindingLayout);
    if (!RendererBlitDispatchContract{
            IsValid(), bool(pipeline), bool(bindingSet) }.CanDispatch())
    {
        log::error("Renderer common blit binding-set creation failed");
        return false;
    }

    nvrhi::GraphicsState state;
    state.pipeline = pipeline;
    state.framebuffer = targetFramebuffer;
    state.bindings = { bindingSet };
    state.viewport.addViewport(nvrhi::Viewport(
        float(framebufferInfo.width),
        float(framebufferInfo.height)));
    state.viewport.addScissorRect(nvrhi::Rect(
        int(framebufferInfo.width),
        int(framebufferInfo.height)));
    commandList->setGraphicsState(state);

    nvrhi::DrawArguments arguments;
    arguments.instanceCount = 1u;
    arguments.vertexCount = 4u;
    commandList->draw(arguments);
    return true;
}
}
