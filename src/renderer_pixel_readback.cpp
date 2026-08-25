/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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

#include "renderer_pixel_readback.h"

#include "renderer_common_passes.h"
#include "renderer_log.h"
#include "renderer_shader_factory.h"

#include <limits>

namespace uvsr
{
RendererPixelReadback::RendererPixelReadback(
    nvrhi::IDevice* device,
    const std::shared_ptr<RendererShaderFactory>& shaderFactory,
    nvrhi::ITexture* inputTexture)
    : m_Device(device)
{
    m_Initialization.device = bool(m_Device);
    if (!device || !shaderFactory || !inputTexture ||
        inputTexture->getDesc().dimension !=
            nvrhi::TextureDimension::Texture2D ||
        inputTexture->getDesc().sampleCount != 1u)
    {
        return;
    }
    RendererResourceCreationSequence creationSequence;

    m_Shader = shaderFactory->CreateShader(
        "uvsr/renderer_pixel_readback_cs.hlsl",
        "main",
        nullptr,
        nvrhi::ShaderType::Compute);
    m_Initialization.shader = bool(m_Shader);
    if (!creationSequence.Require([this]
        {
            return bool(m_Shader);
        }))
    {
        log::error("Renderer pixel-readback shader creation failed");
        return;
    }

    nvrhi::BufferDesc bufferDescription;
    bufferDescription.byteSize = sizeof(RendererReadbackUint4);
    bufferDescription.format = nvrhi::Format::RGBA32_UINT;
    bufferDescription.canHaveUAVs = true;
    bufferDescription.canHaveTypedViews = true;
    bufferDescription.initialState = nvrhi::ResourceStates::CopySource;
    bufferDescription.keepInitialState = true;
    bufferDescription.debugName = "Renderer/Pixel Readback Intermediate";
    m_IntermediateBuffer = device->createBuffer(bufferDescription);

    bufferDescription.canHaveUAVs = false;
    bufferDescription.cpuAccess = nvrhi::CpuAccessMode::Read;
    bufferDescription.debugName = "Renderer/Pixel Readback CPU";
    m_ReadbackBuffer = device->createBuffer(bufferDescription);

    nvrhi::BufferDesc constantDescription;
    constantDescription.byteSize = sizeof(RendererPixelReadbackConstants);
    constantDescription.isConstantBuffer = true;
    constantDescription.isVolatile = true;
    constantDescription.maxVersions = RendererMaxConstantBufferVersions;
    constantDescription.debugName = "Renderer/Pixel Readback Constants";
    m_ConstantBuffer = device->createBuffer(constantDescription);
    m_Initialization.intermediateBuffer = bool(m_IntermediateBuffer);
    m_Initialization.readbackBuffer = bool(m_ReadbackBuffer);
    m_Initialization.constantBuffer = bool(m_ConstantBuffer);
    if (!creationSequence.Require([this]
        {
            return m_IntermediateBuffer && m_ReadbackBuffer &&
                m_ConstantBuffer;
        }))
    {
        log::error("Renderer pixel-readback buffer creation failed");
        return;
    }

    nvrhi::BindingLayoutDesc layoutDescription;
    layoutDescription.visibility = nvrhi::ShaderType::Compute;
    layoutDescription.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(0)
    };
    m_BindingLayout = device->createBindingLayout(layoutDescription);
    m_Initialization.bindingLayout = bool(m_BindingLayout);
    if (!creationSequence.Require([this]
        {
            return bool(m_BindingLayout);
        }))
    {
        log::error("Renderer pixel-readback binding-layout creation failed");
        return;
    }

    nvrhi::BindingSetDesc bindingDescription;
    bindingDescription.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
        nvrhi::BindingSetItem::Texture_SRV(0, inputTexture),
        nvrhi::BindingSetItem::TypedBuffer_UAV(0, m_IntermediateBuffer)
    };
    m_BindingSet = device->createBindingSet(
        bindingDescription,
        m_BindingLayout);
    m_Initialization.bindingSet = bool(m_BindingSet);
    if (!creationSequence.Require([this]
        {
            return bool(m_BindingSet);
        }))
    {
        log::error("Renderer pixel-readback binding-set creation failed");
        return;
    }

    nvrhi::ComputePipelineDesc pipelineDescription;
    pipelineDescription.bindingLayouts = { m_BindingLayout };
    pipelineDescription.CS = m_Shader;
    m_Pipeline = device->createComputePipeline(pipelineDescription);
    m_Initialization.pipeline = bool(m_Pipeline);
    if (!creationSequence.Require([this]
        {
            return bool(m_Pipeline);
        }))
        log::error("Renderer pixel-readback pipeline creation failed");
}

bool RendererPixelReadback::IsValid() const
{
    return m_Initialization.IsComplete();
}

bool RendererPixelReadback::Capture(
    nvrhi::ICommandList* commandList,
    std::uint32_t x,
    std::uint32_t y)
{
    m_CapturePending = false;
    if (!commandList || !IsValid() ||
        x > std::uint32_t(std::numeric_limits<std::int32_t>::max()) ||
        y > std::uint32_t(std::numeric_limits<std::int32_t>::max()))
    {
        return false;
    }

    const RendererPixelReadbackConstants constants = {
        static_cast<std::int32_t>(x),
        static_cast<std::int32_t>(y),
        0,
        0
    };
    commandList->writeBuffer(
        m_ConstantBuffer,
        &constants,
        sizeof(constants));

    nvrhi::ComputeState state;
    state.pipeline = m_Pipeline;
    state.bindings = { m_BindingSet };
    commandList->setComputeState(state);
    commandList->dispatch(1u, 1u, 1u);
    commandList->copyBuffer(
        m_ReadbackBuffer,
        0u,
        m_IntermediateBuffer,
        0u,
        sizeof(RendererReadbackUint4));
    m_CapturePending = true;
    return true;
}

std::optional<RendererReadbackUint4> RendererPixelReadback::ReadUInts()
{
    if (!IsValid() || !m_CapturePending)
        return std::nullopt;
    m_CapturePending = false;
    return ReadRendererUint4(
        [this]() -> const void*
        {
            return m_Device->mapBuffer(
                m_ReadbackBuffer,
                nvrhi::CpuAccessMode::Read);
        },
        [this]()
        {
            m_Device->unmapBuffer(m_ReadbackBuffer);
        });
}
}
