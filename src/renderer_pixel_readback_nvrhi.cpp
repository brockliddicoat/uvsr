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

#include "renderer_pixel_readback_nvrhi.h"
#include "renderer_pixel_readback_cb.h"
#include "renderer_resource_contract.h"

#include <new>
#include <stddef.h>
#include <string.h>

namespace uvsr
{
struct RendererPixelReadback::State
{
    enum class Phase : uint8_t { Idle, Recorded, Submitted };
    nvrhi::DeviceHandle device;
    nvrhi::CommandListHandle commands;
    nvrhi::TextureHandle source;
    nvrhi::ShaderHandle shader;
    nvrhi::ComputePipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::BindingSetHandle bindings;
    nvrhi::BufferHandle constants;
    nvrhi::BufferHandle intermediate;
    nvrhi::BufferHandle readback;
    Phase phase = Phase::Idle;
    uint64_t submittedToken = 0;
};

static_assert(sizeof(RendererPixelReadbackRequest) == 8);
static_assert(sizeof(RendererReadbackUint4) == 16 && alignof(RendererReadbackUint4) == 4);
static_assert(offsetof(RendererReadbackUint4, x) == 0 && offsetof(RendererReadbackUint4, y) == 4);
static_assert(offsetof(RendererReadbackUint4, z) == 8 && offsetof(RendererReadbackUint4, w) == 12);

RendererReadbackError RendererPixelReadbackNvrhi::Initialize(
    RendererPixelReadback& destination,
    nvrhi::IDevice* device,
    nvrhi::ICommandList* graphicsCommands,
    nvrhi::IShader* shader,
    nvrhi::ITexture* source)
{
    if (destination.m_State)
        return RendererReadbackError::Busy;
    if (!device || !graphicsCommands || !shader || !source ||
        graphicsCommands->getDesc().queueType != nvrhi::CommandQueue::Graphics ||
        shader->getDesc().shaderType != nvrhi::ShaderType::Compute)
        return RendererReadbackError::InvalidInput;
    const auto& sourceDescription = source->getDesc();
    if (sourceDescription.dimension != nvrhi::TextureDimension::Texture2D ||
        sourceDescription.width == 0 || sourceDescription.height == 0 ||
        sourceDescription.depth != 1 || sourceDescription.arraySize != 1 ||
        sourceDescription.mipLevels != 1 || sourceDescription.sampleCount != 1 ||
        sourceDescription.sampleQuality != 0 ||
        (sourceDescription.format != nvrhi::Format::RG16_UINT &&
            sourceDescription.format != nvrhi::Format::RG32_UINT))
        return RendererReadbackError::InvalidInput;

    // <new> supplies checked allocation and correct lifetimes for native RAII
    // handles. only this one fixed owner is allocated, during creation.
    struct Candidate
    {
        RendererPixelReadback::State* state;
        ~Candidate() { delete state; }
    } candidate{ new (std::nothrow) RendererPixelReadback::State };
    if (!candidate.state)
        return RendererReadbackError::AllocationFailed;
    auto& state = *candidate.state;
    state.device = device;
    state.commands = graphicsCommands;
    state.source = source;
    state.shader = shader;

    nvrhi::BufferDesc buffer;
    buffer.byteSize = sizeof(RendererReadbackUint4);
    buffer.format = nvrhi::Format::RGBA32_UINT;
    buffer.canHaveUAVs = true;
    buffer.canHaveTypedViews = true;
    buffer.initialState = nvrhi::ResourceStates::CopySource;
    buffer.keepInitialState = true;
    buffer.debugName = "Renderer/Pixel Readback Intermediate";
    state.intermediate = device->createBuffer(buffer);
    if (!state.intermediate)
        return RendererReadbackError::ResourceCreationFailed;
    buffer.canHaveUAVs = false;
    buffer.cpuAccess = nvrhi::CpuAccessMode::Read;
    buffer.debugName = "Renderer/Pixel Readback CPU";
    state.readback = device->createBuffer(buffer);
    if (!state.readback)
        return RendererReadbackError::ResourceCreationFailed;

    nvrhi::BufferDesc constants;
    constants.byteSize = sizeof(RendererPixelReadbackConstants);
    constants.isConstantBuffer = true;
    constants.isVolatile = true;
    constants.maxVersions = RendererMaxConstantBufferVersions;
    constants.debugName = "Renderer/Pixel Readback Constants";
    state.constants = device->createBuffer(constants);
    if (!state.constants)
        return RendererReadbackError::ResourceCreationFailed;

    nvrhi::BindingLayoutDesc layout;
    layout.visibility = nvrhi::ShaderType::Compute;
    layout.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(0) };
    state.layout = device->createBindingLayout(layout);
    if (!state.layout)
        return RendererReadbackError::ResourceCreationFailed;
    nvrhi::BindingSetDesc bindings;
    bindings.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, state.constants),
        nvrhi::BindingSetItem::Texture_SRV(0, state.source),
        nvrhi::BindingSetItem::TypedBuffer_UAV(0, state.intermediate) };
    state.bindings = device->createBindingSet(bindings, state.layout);
    if (!state.bindings)
        return RendererReadbackError::ResourceCreationFailed;
    nvrhi::ComputePipelineDesc pipeline;
    pipeline.bindingLayouts = { state.layout };
    pipeline.CS = state.shader;
    state.pipeline = device->createComputePipeline(pipeline);
    if (!state.pipeline)
        return RendererReadbackError::ResourceCreationFailed;

    destination.m_State = candidate.state;
    candidate.state = nullptr;
    return RendererReadbackError::None;
}

RendererPixelReadback::~RendererPixelReadback()
{
    delete m_State;
}

bool RendererPixelReadback::IsValid() const noexcept
{
    return m_State != nullptr;
}

RendererReadbackError RendererPixelReadback::Capture(RendererPixelReadbackRequest request)
{
    if (!m_State)
        return RendererReadbackError::Uninitialized;
    auto& state = *m_State;
    if (state.phase != State::Phase::Idle)
        return RendererReadbackError::Busy;
    const auto& source = state.source->getDesc();
    if (request.x >= source.width || request.y >= source.height ||
        request.x > uint32_t(INT32_MAX) || request.y > uint32_t(INT32_MAX))
        return RendererReadbackError::InvalidInput;

    const RendererPixelReadbackConstants constants{
        static_cast<int32_t>(request.x), static_cast<int32_t>(request.y), 0, 0 };
    state.commands->writeBuffer(state.constants, &constants, sizeof(constants));
    nvrhi::ComputeState dispatch;
    dispatch.pipeline = state.pipeline;
    dispatch.bindings = { state.bindings };
    state.commands->setComputeState(dispatch);
    state.commands->dispatch(1, 1, 1);
    state.commands->copyBuffer(state.readback, 0, state.intermediate, 0, sizeof(RendererReadbackUint4));
    state.phase = State::Phase::Recorded;
    return RendererReadbackError::None;
}

RendererReadbackError RendererPixelReadback::NotifySubmitted(uint64_t token) noexcept
{
    if (!m_State)
        return RendererReadbackError::Uninitialized;
    if (m_State->phase != State::Phase::Recorded)
        return RendererReadbackError::Busy;
    if (token == 0)
        return RendererReadbackError::SubmissionFailed;
    m_State->submittedToken = token;
    m_State->phase = State::Phase::Submitted;
    return RendererReadbackError::None;
}

RendererReadbackError RendererPixelReadback::ReadUInts(RendererReadbackUint4& output)
{
    if (!m_State)
        return RendererReadbackError::Uninitialized;
    auto& state = *m_State;
    if (state.phase != State::Phase::Submitted || state.submittedToken == 0)
        return RendererReadbackError::NotSubmitted;
    // NVRHI DX12 stamps this staging resource at execute and blocks in mapBuffer
    // on that exact fence. the public token gates access but is not the fence.
    state.phase = State::Phase::Idle;
    state.submittedToken = 0;
    const void* mapped = state.device->mapBuffer(state.readback, nvrhi::CpuAccessMode::Read);
    if (!mapped)
        return RendererReadbackError::MapFailed;
    memcpy(&output, mapped, sizeof(output));
    state.device->unmapBuffer(state.readback);
    return RendererReadbackError::None;
}

void RendererPixelReadback::CancelRecorded() noexcept
{
    if (m_State && m_State->phase == State::Phase::Recorded)
        m_State->phase = State::Phase::Idle;
}
}
