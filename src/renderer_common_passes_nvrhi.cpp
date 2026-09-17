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

#include "renderer_common_passes_nvrhi.h"

#include "renderer_log.h"
#include "renderer_shader_factory_nvrhi.h"

#include <new>
#include <stdlib.h>
#include <type_traits>
#include <utility>

namespace uvsr
{
#if defined(UVSR_BLIT_PIPELINE_TEST_HOOKS)
namespace { bool g_FailNextBlitPipelineAllocation = false; }
void FailNextRendererBlitPipelineAllocation() noexcept
{
    g_FailNextBlitPipelineAllocation = true;
}
bool RendererBlitPipelineAllocationFailurePending() noexcept
{
    return g_FailNextBlitPipelineAllocation;
}
#endif
RendererCommonPasses::BlitPipelineCache::~BlitPipelineCache() noexcept
{
    for (size_t index = 0; index < m_Count; ++index)
        m_Entries[index].~Entry();
    free(m_Entries);
}

nvrhi::GraphicsPipelineHandle RendererCommonPasses::BlitPipelineCache::Find(
    const nvrhi::FramebufferInfo& framebuffer) const noexcept
{
    for (size_t index = 0; index < m_Count; ++index)
        if (m_Entries[index].framebuffer == framebuffer)
            return m_Entries[index].pipeline;
    return nullptr;
}

bool RendererCommonPasses::BlitPipelineCache::Append(
    const nvrhi::FramebufferInfo& framebuffer,
    const nvrhi::GraphicsPipelineHandle& pipeline) noexcept
{
    static_assert(std::is_nothrow_copy_constructible_v<Entry>);
    static_assert(std::is_nothrow_move_constructible_v<Entry>);
    static_assert(std::is_nothrow_destructible_v<Entry>);
    static_assert(alignof(Entry) <= alignof(max_align_t));
    constexpr size_t maximum = size_t(PTRDIFF_MAX) / sizeof(Entry);
    if (m_Count >= maximum) return false;
    if (m_Count < m_Capacity)
    {
        new (m_Entries + m_Count) Entry{framebuffer, pipeline};
        ++m_Count;
        return true;
    }
    const size_t capacity = !m_Capacity ? 1 :
        m_Capacity <= maximum / 2 ? m_Capacity * 2 : maximum;
#if defined(UVSR_BLIT_PIPELINE_TEST_HOOKS)
    if (g_FailNextBlitPipelineAllocation)
    {
        g_FailNextBlitPipelineAllocation = false;
        return false;
    }
#endif
    auto* candidate = static_cast<Entry*>(malloc(capacity * sizeof(Entry)));
    if (!candidate) return false;
    // retain both inputs until the appended entry exists.
    new (candidate + m_Count) Entry{framebuffer, pipeline};
    for (size_t index = 0; index < m_Count; ++index)
    {
        new (candidate + index) Entry(std::move(m_Entries[index]));
        m_Entries[index].~Entry();
    }
    free(m_Entries);
    m_Entries = candidate;
    m_Capacity = capacity;
    ++m_Count;
    return true;
}

RendererCommonPasses::RendererCommonPasses(
    nvrhi::IDevice* device,
    RendererShaderFactory* shaderFactory)
    : m_Device(device)
{
    m_Initialization.device = bool(m_Device);
    if (!device || !shaderFactory)
        return;
    RendererResourceCreationSequence creationSequence;

    shader_blob::Constant fullscreenMacros[] = {
        { "UVSR_FULLSCREEN_DEPTH", "0" }
    };
    m_FullscreenVS = shaderFactory->CreateShader(
        "uvsr/renderer_fullscreen_vs.hlsl",
        "main",
        fullscreenMacros,
        nvrhi::ShaderType::Vertex);
    fullscreenMacros[0].value = "1";
    m_FullscreenAtOneVS = shaderFactory->CreateShader(
        "uvsr/renderer_fullscreen_vs.hlsl",
        "main",
        fullscreenMacros,
        nvrhi::ShaderType::Vertex);
    m_BlitPS = shaderFactory->CreateShader(
        "uvsr/renderer_blit_ps.hlsl",
        "main",
        {},
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

    m_Initialization.blackTexture = bool(m_BlackTexture);
    m_Initialization.whiteTexture = bool(m_WhiteTexture);
    m_Initialization.blackCubeArray = bool(m_BlackCubeArray);

    if (!creationSequence.Require([this]
        {
            return m_BlackTexture && m_WhiteTexture && m_BlackCubeArray;
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
    commandList->writeTexture(m_BlackTexture, 0u, 0u, &black, 0u);
    commandList->writeTexture(m_WhiteTexture, 0u, 0u, &white, 0u);
    for (std::uint32_t slice = 0u; slice < 6u; ++slice)
        commandList->writeTexture(m_BlackCubeArray, slice, 0u, &black, 0u);
    commandList->setPermanentTextureState(
        m_BlackTexture,
        nvrhi::ResourceStates::ShaderResource);
    commandList->setPermanentTextureState(
        m_WhiteTexture,
        nvrhi::ResourceStates::ShaderResource);
    commandList->setPermanentTextureState(
        m_BlackCubeArray,
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

bool RendererCommonPasses::HasBlitPipelineFailure() const
{
    return m_BlitPipelineFailure.HasFailed();
}

nvrhi::GraphicsPipelineHandle RendererCommonPasses::GetBlitPipeline(
    const nvrhi::FramebufferInfo& framebufferInfo)
{
    if (!m_BlitPipelineFailure.CanAttempt())
        return nullptr;
    if (auto existing = m_BlitPipelines.Find(framebufferInfo))
        return existing;

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
    const bool available = pipeline && m_BlitPipelines.Append(framebufferInfo, pipeline);
    m_BlitPipelineFailure.RecordResult(available);
    if (!available)
    {
        log::error("Renderer common blit pipeline creation failed");
        return nullptr;
    }
    return pipeline;
}

bool RendererCommonPasses::BlitTexture(
    nvrhi::ICommandList* commandList,
    nvrhi::IFramebuffer* targetFramebuffer,
    nvrhi::ITexture* sourceTexture)
{
    return BlitTextureSubresources(commandList, targetFramebuffer, sourceTexture, nvrhi::AllSubresources);
}

bool RendererCommonPasses::BlitTextureMip(
    nvrhi::ICommandList* commandList,
    nvrhi::IFramebuffer* targetFramebuffer,
    nvrhi::ITexture* sourceTexture,
    uint32_t sourceMip)
{
    if (!sourceTexture || sourceMip >= sourceTexture->getDesc().mipLevels) return false;
    return BlitTextureSubresources(commandList, targetFramebuffer, sourceTexture,
        nvrhi::TextureSubresourceSet(sourceMip, 1, 0, 1));
}

bool RendererCommonPasses::BlitTextureSubresources(
    nvrhi::ICommandList* commandList,
    nvrhi::IFramebuffer* targetFramebuffer,
    nvrhi::ITexture* sourceTexture,
    nvrhi::TextureSubresourceSet sourceSubresources)
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
        nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture, nvrhi::Format::UNKNOWN, sourceSubresources),
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
