#include "taa_miniengine.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <algorithm>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

namespace
{
    // MiniEngine source:
    // Microsoft/DirectX-Graphics-Samples
    // commit 357ade6ec6ff0d9dcadc48f35c7a28e37c0cdf7a
    //
    // The scalar defaults remain verbatim: maximum temporal lerp 1.0, speed
    // limit 64 pixels, and sharpness 0.5. The projection is an integration-only
    // addition that lets the shader express MiniEngine's forward-linear-depth
    // disocclusion inequality over UVSR's infinite reverse-Z device depth.
    struct alignas(16) MiniEngineTaaBlendConstants
    {
        float4x4 projection;
        float2 reciprocalBufferDimensions;
        float temporalBlendFactor;
        float reciprocalSpeedLimiter;
        float2 viewportJitter;
        uint2 bufferDimensions;
        uint32_t historyValid;
        float padding[3];
    };

    struct alignas(16) MiniEngineTaaOutputConstants
    {
        float centerWeight;
        float lateralWeight;
        uint2 bufferDimensions;
    };

    static_assert(sizeof(MiniEngineTaaBlendConstants) == 112u);
    static_assert(sizeof(MiniEngineTaaOutputConstants) == 16u);
}

namespace uvsr
{
    MiniEngineTemporalAAPass::MiniEngineTemporalAAPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        nvrhi::ITexture* sceneColor,
        nvrhi::ITexture* currentDepth,
        nvrhi::ITexture* motionVectors)
        : m_Device(device)
    {
        const nvrhi::TextureDesc& sceneColorDesc = sceneColor->getDesc();
        m_Size = uint2(sceneColorDesc.width, sceneColorDesc.height);
        m_Timings.historyTextureBytes =
            GetMiniEngineTaaHistoryBytes(m_Size.x, m_Size.y);

        nvrhi::TextureDesc historyDesc;
        historyDesc.width = m_Size.x;
        historyDesc.height = m_Size.y;
        historyDesc.dimension = nvrhi::TextureDimension::Texture2D;
        historyDesc.mipLevels = 1u;
        historyDesc.format = nvrhi::Format::RGBA16_FLOAT;
        historyDesc.isUAV = true;
        historyDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        historyDesc.keepInitialState = true;
        historyDesc.debugName = "MiniEngineTAA/TemporalColor0";
        m_TemporalColor[0] = device->createTexture(historyDesc);
        historyDesc.debugName = "MiniEngineTAA/TemporalColor1";
        m_TemporalColor[1] = device->createTexture(historyDesc);

        historyDesc.format = nvrhi::Format::R32_FLOAT;
        historyDesc.debugName = "MiniEngineTAA/DepthHistory0";
        m_DepthHistory[0] = device->createTexture(historyDesc);
        historyDesc.debugName = "MiniEngineTAA/DepthHistory1";
        m_DepthHistory[1] = device->createTexture(historyDesc);

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(MiniEngineTaaBlendConstants);
        constantBufferDesc.debugName = "MiniEngineTAA/BlendConstants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_BlendConstantBuffer = device->createBuffer(constantBufferDesc);

        constantBufferDesc.byteSize = sizeof(MiniEngineTaaOutputConstants);
        constantBufferDesc.debugName = "MiniEngineTAA/OutputConstants";
        m_OutputConstantBuffer = device->createBuffer(constantBufferDesc);

        // NVRHI's default sampler is MiniEngine's s0 contract: min/mag/mip
        // linear with clamp addressing.
        m_LinearClampSampler = device->createSampler(nvrhi::SamplerDesc());

        m_BlendShader = shaderFactory->CreateShader(
            "uvsr/taa_miniengine_blend_cs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Compute);
        m_ResolveShader = shaderFactory->CreateShader(
            "uvsr/taa_miniengine_resolve_cs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Compute);
        m_SharpenShader = shaderFactory->CreateShader(
            "uvsr/taa_miniengine_sharpen_cs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Compute);

        nvrhi::BindingLayoutDesc blendLayoutDesc;
        blendLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        blendLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1)
        };
        m_BlendBindingLayout = device->createBindingLayout(blendLayoutDesc);

        nvrhi::ComputePipelineDesc blendPipelineDesc;
        blendPipelineDesc.CS = m_BlendShader;
        blendPipelineDesc.bindingLayouts = { m_BlendBindingLayout };
        m_BlendPipeline = device->createComputePipeline(blendPipelineDesc);

        nvrhi::BindingLayoutDesc outputLayoutDesc;
        outputLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        outputLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_OutputBindingLayout = device->createBindingLayout(outputLayoutDesc);

        nvrhi::ComputePipelineDesc outputPipelineDesc;
        outputPipelineDesc.bindingLayouts = { m_OutputBindingLayout };
        outputPipelineDesc.CS = m_ResolveShader;
        m_ResolvePipeline = device->createComputePipeline(outputPipelineDesc);
        outputPipelineDesc.CS = m_SharpenShader;
        m_SharpenPipeline = device->createComputePipeline(outputPipelineDesc);

        for (uint32_t source = 0u; source < 2u; ++source)
        {
            const uint32_t destination = source ^ 1u;
            nvrhi::BindingSetDesc blendBindings;
            blendBindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(1, m_BlendConstantBuffer),
                nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
                nvrhi::BindingSetItem::Texture_SRV(0, motionVectors),
                nvrhi::BindingSetItem::Texture_SRV(1, sceneColor),
                nvrhi::BindingSetItem::Texture_SRV(
                    2, m_TemporalColor[source]),
                nvrhi::BindingSetItem::Texture_SRV(3, currentDepth),
                nvrhi::BindingSetItem::Texture_SRV(
                    4, m_DepthHistory[source]),
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_TemporalColor[destination]),
                nvrhi::BindingSetItem::Texture_UAV(
                    1, m_DepthHistory[destination])
            };
            m_BlendBindingSets[source] = device->createBindingSet(
                blendBindings, m_BlendBindingLayout);

            nvrhi::BindingSetDesc outputBindings;
            outputBindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_OutputConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, m_TemporalColor[destination]),
                nvrhi::BindingSetItem::Texture_UAV(0, sceneColor)
            };
            m_OutputBindingSets[source] = device->createBindingSet(
                outputBindings, m_OutputBindingLayout);
        }

        for (auto& stageQueries : m_TimerQueries)
            for (nvrhi::TimerQueryHandle& query : stageQueries)
                query = device->createTimerQuery();
    }

    void MiniEngineTemporalAAPass::ResetHistory()
    {
        m_HistoryValid = false;
    }

    void MiniEngineTemporalAAPass::AdvanceTimers()
    {
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        for (uint32_t stageIndex = 0u;
            stageIndex < static_cast<uint32_t>(Stage::Count);
            ++stageIndex)
        {
            m_TimerActive[stageIndex] = false;
            if (!m_TimerPending[stageIndex][slot])
                continue;

            nvrhi::ITimerQuery* query = m_TimerQueries[stageIndex][slot];
            if (!m_Device->pollTimerQuery(query))
                continue;

            const float milliseconds =
                m_Device->getTimerQueryTime(query) * 1000.f;
            m_Device->resetTimerQuery(query);
            m_TimerPending[stageIndex][slot] = false;

            switch (static_cast<Stage>(stageIndex))
            {
            case Stage::Blend:
                m_Timings.blendMilliseconds = milliseconds;
                break;
            case Stage::Output:
                m_Timings.outputMilliseconds = milliseconds;
                break;
            default:
                break;
            }
        }
    }

    void MiniEngineTemporalAAPass::BeginStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        if (m_TimerPending[stageIndex][slot])
            return;

        commandList->beginTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerActive[stageIndex] = true;
    }

    void MiniEngineTemporalAAPass::EndStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        if (!m_TimerActive[stageIndex])
            return;

        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        commandList->endTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerPending[stageIndex][slot] = true;
        m_TimerActive[stageIndex] = false;
    }

    void MiniEngineTemporalAAPass::Render(
        nvrhi::ICommandList* commandList,
        const IView& currentView,
        const IView* previousView,
        uint64_t frameIndex,
        bool enableSharpen,
        float sharpness)
    {
        AdvanceTimers();

        if (!previousView)
            ResetHistory();

        if (!m_HistoryValid)
        {
            // MiniEngine clears both ping-pong histories when TAA is enabled or
            // reset. Keep that exact state transition rather than relying only
            // on the explicit first-frame confidence gate.
            for (uint32_t index = 0u; index < 2u; ++index)
            {
                commandList->clearTextureFloat(
                    m_TemporalColor[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
                commandList->clearTextureFloat(
                    m_DepthHistory[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
            }
        }

        const uint32_t source = uint32_t(frameIndex & 1u);

        MiniEngineTaaBlendConstants blendConstants{};
        blendConstants.projection =
            currentView.GetProjectionMatrix(false);
        blendConstants.reciprocalBufferDimensions =
            1.f / float2(m_Size);
        blendConstants.temporalBlendFactor = 1.f;
        blendConstants.reciprocalSpeedLimiter = 1.f / 64.f;
        blendConstants.viewportJitter = previousView
            ? previousView->GetPixelOffset() - currentView.GetPixelOffset()
            : float2(0.f);
        blendConstants.bufferDimensions = m_Size;
        blendConstants.historyValid =
            m_HistoryValid && previousView ? 1u : 0u;
        commandList->writeBuffer(
            m_BlendConstantBuffer,
            &blendConstants,
            sizeof(blendConstants));

        nvrhi::ComputeState blendState;
        blendState.pipeline = m_BlendPipeline;
        blendState.bindings = { m_BlendBindingSets[source] };

        commandList->beginMarker("MiniEngine TAA Temporal Blend");
        BeginStage(commandList, Stage::Blend);
        commandList->setComputeState(blendState);
        // MiniEngine dispatches 16x8 output pixels per 8x8 group because every
        // thread resolves a horizontal pair.
        commandList->dispatch(
            (m_Size.x + 15u) / 16u,
            (m_Size.y + 7u) / 8u,
            1u);
        EndStage(commandList, Stage::Blend);
        commandList->endMarker();

        const float clampedSharpness =
            ClampMiniEngineTaaSharpness(sharpness);
        const bool useSharpen =
            ShouldSharpenMiniEngineTaa(enableSharpen, clampedSharpness);
        const MiniEngineTaaSharpenWeights sharpenWeights =
            GetMiniEngineTaaSharpenWeights(clampedSharpness);
        MiniEngineTaaOutputConstants outputConstants{};
        outputConstants.centerWeight = sharpenWeights.center;
        outputConstants.lateralWeight = sharpenWeights.lateral;
        outputConstants.bufferDimensions = m_Size;
        commandList->writeBuffer(
            m_OutputConstantBuffer,
            &outputConstants,
            sizeof(outputConstants));

        nvrhi::ComputeState outputState;
        outputState.pipeline =
            useSharpen ? m_SharpenPipeline : m_ResolvePipeline;
        outputState.bindings = { m_OutputBindingSets[source] };
        m_Timings.outputWasSharpened = useSharpen;

        commandList->beginMarker(
            useSharpen ? "MiniEngine TAA Sharpen" : "MiniEngine TAA Resolve");
        BeginStage(commandList, Stage::Output);
        commandList->setComputeState(outputState);
        commandList->dispatch(
            (m_Size.x + 7u) / 8u,
            (m_Size.y + 7u) / 8u,
            1u);
        EndStage(commandList, Stage::Output);
        commandList->endMarker();

        m_HistoryValid = true;
        ++m_TimerFrame;
    }
}
