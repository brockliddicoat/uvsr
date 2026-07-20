#include "smaa.h"

#include <donut/core/log.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

namespace
{
#include "third_party/smaa/AreaTex.h"
#include "third_party/smaa/SearchTex.h"

    // Upstream source: iryoku/smaa
    // commit 71c806a838bdd7d517df19192a20f0c61b3ca29d.
    struct alignas(16) SmaaConstants
    {
        float4 rtMetrics;
        float4 subsampleIndices;
    };

    static_assert(sizeof(SmaaConstants) == 32u);

    uvsr::AntiAliasingQuality SanitizeSmaaQuality(
        uvsr::AntiAliasingQuality requested)
    {
        return requested < uvsr::AntiAliasingQuality::Count
            ? requested
            : uvsr::AntiAliasingQuality::High;
    }

    constexpr uint32_t StageBit(uint32_t stage)
    {
        return 1u << stage;
    }

    nvrhi::TextureHandle CreateSmaaTexture(
        nvrhi::IDevice* device,
        uint32_t width,
        uint32_t height,
        nvrhi::Format format,
        const char* debugName,
        bool renderTarget,
        bool unorderedAccess = false)
    {
        nvrhi::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.dimension = nvrhi::TextureDimension::Texture2D;
        desc.mipLevels = 1u;
        desc.format = format;
        desc.isRenderTarget = renderTarget;
        desc.isUAV = unorderedAccess;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        desc.keepInitialState = true;
        desc.debugName = debugName;
        return device->createTexture(desc);
    }
}

namespace uvsr
{
    SmaaPass::SmaaPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        nvrhi::ITexture* sceneColor)
        : m_Device(device)
        , m_LinearClampSampler(commonPasses->m_LinearClampSampler)
        , m_PointClampSampler(commonPasses->m_PointClampSampler)
    {
        const nvrhi::TextureDesc& sceneDesc = sceneColor->getDesc();
        m_Size = uint2(sceneDesc.width, sceneDesc.height);

        nvrhi::BufferDesc constantDesc;
        constantDesc.byteSize = sizeof(SmaaConstants);
        constantDesc.debugName = "SMAA/Constants";
        constantDesc.isConstantBuffer = true;
        constantDesc.isVolatile = true;
        constantDesc.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantDesc);

        m_Edges = CreateSmaaTexture(
            device,
            m_Size.x,
            m_Size.y,
            nvrhi::Format::RG8_UNORM,
            "SMAA/Edges",
            true);
        m_BlendWeights = CreateSmaaTexture(
            device,
            m_Size.x,
            m_Size.y,
            nvrhi::Format::RGBA8_UNORM,
            "SMAA/BlendWeights",
            true);
        m_OutputColor = CreateSmaaTexture(
            device,
            m_Size.x,
            m_Size.y,
            sceneDesc.format,
            "SMAA/Output",
            true,
            true);
#if UVSR_AA_DEVELOPER_OVERRIDES
        m_DebugOutput = CreateSmaaTexture(
            device,
            m_Size.x,
            m_Size.y,
            sceneDesc.format,
            "SMAA/DeveloperDebugOutput",
            true);
#endif
        m_AreaTexture = CreateSmaaTexture(
            device,
            AREATEX_WIDTH,
            AREATEX_HEIGHT,
            nvrhi::Format::RG8_UNORM,
            "SMAA/AreaLookup",
            false);
        m_SearchTexture = CreateSmaaTexture(
            device,
            SEARCHTEX_WIDTH,
            SEARCHTEX_HEIGHT,
            nvrhi::Format::R8_UNORM,
            "SMAA/SearchLookup",
            false);

        m_EdgeFramebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(m_Edges));
        m_BlendFramebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(m_BlendWeights));
        m_OutputFramebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(m_OutputColor));
#if UVSR_AA_DEVELOPER_OVERRIDES
        m_DebugFramebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(m_DebugOutput));
#endif

        nvrhi::BindingLayoutDesc edgeLayout;
        edgeLayout.visibility = nvrhi::ShaderType::Pixel;
        edgeLayout.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0)
        };
        m_EdgeBindingLayout =
            device->createBindingLayout(edgeLayout);

        nvrhi::BindingLayoutDesc weightLayout;
        weightLayout.visibility = nvrhi::ShaderType::Pixel;
        weightLayout.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Sampler(1),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2)
        };
        m_WeightBindingLayout =
            device->createBindingLayout(weightLayout);

        nvrhi::BindingLayoutDesc neighborhoodLayout;
        neighborhoodLayout.visibility = nvrhi::ShaderType::Pixel;
        neighborhoodLayout.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Sampler(1),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1)
        };
        m_NeighborhoodBindingLayout =
            device->createBindingLayout(neighborhoodLayout);

        nvrhi::BindingLayoutDesc selectiveNeighborhoodLayout;
        selectiveNeighborhoodLayout.visibility =
            nvrhi::ShaderType::Compute;
        selectiveNeighborhoodLayout.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Sampler(1),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_SelectiveNeighborhoodBindingLayout =
            device->createBindingLayout(
                selectiveNeighborhoodLayout);

#if UVSR_AA_DEVELOPER_OVERRIDES
        nvrhi::BindingLayoutDesc debugLayout;
        debugLayout.visibility = nvrhi::ShaderType::Pixel;
        debugLayout.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3)
        };
        m_DebugBindingLayout =
            device->createBindingLayout(debugLayout);
#endif

        std::array<nvrhi::ShaderHandle, c_QualityCount>
            edgeShaders;
        std::array<nvrhi::ShaderHandle, c_QualityCount>
            weightShaders;
        for (uint32_t quality = 0u;
            quality < c_QualityCount;
            ++quality)
        {
            std::vector<ShaderMacro> macros = {
                { "SMAA_QUALITY_PRESET", std::to_string(quality) }
            };
            edgeShaders[quality] = shaderFactory->CreateShader(
                "uvsr/smaa_edge_ps.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Pixel);
            weightShaders[quality] = shaderFactory->CreateShader(
                "uvsr/smaa_blend_weight_ps.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Pixel);
        }

        nvrhi::ShaderHandle neighborhoodShader =
            shaderFactory->CreateShader(
                "uvsr/smaa_neighborhood_ps.hlsl",
                "main",
                nullptr,
                nvrhi::ShaderType::Pixel);
        std::vector<ShaderMacro> selectiveMacros = {
            { "SMAA_SELECTIVE_INDIRECT_TILES", "0" }
        };
        nvrhi::ShaderHandle selectiveNeighborhoodShader =
            shaderFactory->CreateShader(
                "uvsr/smaa_selective_neighborhood_cs.hlsl",
                "main",
                &selectiveMacros,
                nvrhi::ShaderType::Compute);

#if UVSR_AA_DEVELOPER_OVERRIDES
        std::array<nvrhi::ShaderHandle, c_DebugViewCount>
            debugShaders;
        for (uint32_t debugView = 0u;
            debugView < c_DebugViewCount;
            ++debugView)
        {
            std::vector<ShaderMacro> macros = {
                { "SMAA_DEBUG_VIEW", std::to_string(debugView) }
            };
            debugShaders[debugView] =
                shaderFactory->CreateShader(
                    "uvsr/smaa_debug_ps.hlsl",
                    "main",
                    &macros,
                    nvrhi::ShaderType::Pixel);
        }
#endif

        auto createPipeline = [&](nvrhi::IShader* shader,
                                  nvrhi::IBindingLayout* layout,
                                  nvrhi::IFramebuffer* framebuffer)
        {
            nvrhi::GraphicsPipelineDesc desc;
            desc.primType = nvrhi::PrimitiveType::TriangleStrip;
            desc.VS = commonPasses->m_FullscreenVS;
            desc.PS = shader;
            desc.bindingLayouts = { layout };
            desc.renderState.rasterState.setCullNone();
            desc.renderState.depthStencilState.depthTestEnable = false;
            desc.renderState.depthStencilState.stencilEnable = false;
            return device->createGraphicsPipeline(
                desc,
                framebuffer->getFramebufferInfo());
        };

        for (uint32_t index = 0u;
            index < c_QualityCount;
            ++index)
        {
            if (edgeShaders[index])
            {
                m_EdgePipelines[index] = createPipeline(
                    edgeShaders[index],
                    m_EdgeBindingLayout,
                    m_EdgeFramebuffer);
            }
        }
        for (uint32_t quality = 0u;
            quality < c_QualityCount;
            ++quality)
        {
            if (weightShaders[quality])
            {
                m_WeightPipelines[quality] = createPipeline(
                    weightShaders[quality],
                    m_WeightBindingLayout,
                    m_BlendFramebuffer);
            }
        }
        if (neighborhoodShader)
        {
            m_NeighborhoodPipeline = createPipeline(
                neighborhoodShader,
                m_NeighborhoodBindingLayout,
                m_OutputFramebuffer);
        }

        if (selectiveNeighborhoodShader)
        {
            nvrhi::ComputePipelineDesc computeDesc;
            computeDesc.bindingLayouts = {
                m_SelectiveNeighborhoodBindingLayout
            };
            computeDesc.CS = selectiveNeighborhoodShader;
            m_SelectiveNeighborhoodPipeline =
                device->createComputePipeline(computeDesc);
        }

#if UVSR_AA_DEVELOPER_OVERRIDES
        for (uint32_t debugView = 0u;
            debugView < c_DebugViewCount;
            ++debugView)
        {
            if (debugShaders[debugView])
            {
                m_DebugPipelines[debugView] = createPipeline(
                    debugShaders[debugView],
                    m_DebugBindingLayout,
                    m_DebugFramebuffer);
            }
        }
#endif

        nvrhi::BindingSetDesc weightSet;
        weightSet.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
            nvrhi::BindingSetItem::Sampler(1, m_PointClampSampler),
            nvrhi::BindingSetItem::Texture_SRV(0, m_Edges),
            nvrhi::BindingSetItem::Texture_SRV(1, m_AreaTexture),
            nvrhi::BindingSetItem::Texture_SRV(2, m_SearchTexture)
        };
        m_WeightBindingSet =
            device->createBindingSet(weightSet, m_WeightBindingLayout);

        for (auto& stageQueries : m_TimerQueries)
            for (nvrhi::TimerQueryHandle& query : stageQueries)
                query = device->createTimerQuery();
    }

    void SmaaPass::UploadLookups(nvrhi::ICommandList* commandList)
    {
        if (m_LookupsUploaded)
            return;

        commandList->writeTexture(
            m_AreaTexture,
            0u,
            0u,
            areaTexBytes,
            AREATEX_PITCH);
        commandList->writeTexture(
            m_SearchTexture,
            0u,
            0u,
            searchTexBytes,
            SEARCHTEX_PITCH);
        commandList->setPermanentTextureState(
            m_AreaTexture,
            nvrhi::ResourceStates::ShaderResource);
        commandList->setPermanentTextureState(
            m_SearchTexture,
            nvrhi::ResourceStates::ShaderResource);
        m_LookupsUploaded = true;
    }

    void SmaaPass::AdvanceTimers()
    {
        constexpr uint32_t stageCount =
            static_cast<uint32_t>(Stage::Count);
        for (uint32_t slot = 0u; slot < c_TimerLatency; ++slot)
        {
            const uint32_t expectedMask =
                m_TimerExpectedMasks[slot];
            if (expectedMask == 0u)
                continue;

            for (uint32_t stageIndex = 0u;
                stageIndex < stageCount;
                ++stageIndex)
            {
                const uint32_t bit = StageBit(stageIndex);
                if ((expectedMask & bit) == 0u ||
                    (m_TimerReadyMasks[slot] & bit) != 0u ||
                    !m_TimerPending[stageIndex][slot])
                {
                    continue;
                }
                nvrhi::ITimerQuery* query =
                    m_TimerQueries[stageIndex][slot];
                if (!m_Device->pollTimerQuery(query))
                    continue;
                m_TimerValues[stageIndex][slot] =
                    m_Device->getTimerQueryTime(query) * 1000.f;
                m_Device->resetTimerQuery(query);
                m_TimerPending[stageIndex][slot] = false;
                m_TimerReadyMasks[slot] |= bit;
            }

            if (m_TimerReadyMasks[slot] != expectedMask)
                continue;

            SmaaTimings sample;
            const auto value = [&](Stage stage)
            {
                const uint32_t stageIndex =
                    static_cast<uint32_t>(stage);
                return (expectedMask & StageBit(stageIndex)) != 0u
                    ? m_TimerValues[stageIndex][slot]
                    : 0.f;
            };
            sample.edgeMilliseconds = value(Stage::Edge);
            sample.weightMilliseconds = value(Stage::Weight);
            sample.neighborhoodMilliseconds =
                value(Stage::Neighborhood);
            sample.executionValid =
                m_TimerExecutionValid[slot];
            sample.completedSerial = m_TimerSerials[slot];
            sample.completedTag = m_TimerTags[slot];
            m_ReadyTimingSamples.emplace(
                sample.completedSerial,
                sample);
            m_TimerExpectedMasks[slot] = 0u;
            m_TimerReadyMasks[slot] = 0u;
        }

        for (;;)
        {
            const auto ready = m_ReadyTimingSamples.find(
                m_NextCompletedTimerSerial);
            if (ready == m_ReadyTimingSamples.end())
                break;

            m_Timings = ready->second;
            if (m_CompletedTimingSamples.size() ==
                c_CompletedTimingQueueCapacity)
            {
                if ((m_CompletedTimingSamples.front().completedTag &
                        SmaaTimingCollectTagFlag) != 0u)
                {
                    ++m_TimingAccounting.droppedSampleCount;
                    ++m_TimingAccounting.retiredSampleCount;
                }
                m_CompletedTimingSamples.pop_front();
            }
            m_CompletedTimingSamples.push_back(ready->second);
            m_ReadyTimingSamples.erase(ready);
            ++m_NextCompletedTimerSerial;
        }
    }

    bool SmaaPass::PopCompletedTiming(SmaaTimings& timing)
    {
        if (m_CompletedTimingSamples.empty())
            return false;
        timing = m_CompletedTimingSamples.front();
        m_CompletedTimingSamples.pop_front();
        if ((timing.completedTag & SmaaTimingCollectTagFlag) != 0u)
            ++m_TimingAccounting.retiredSampleCount;
        return true;
    }

    void SmaaPass::MarkInactiveFrame()
    {
        // Continue retiring delayed queries while exposing the true cost of
        // this bypassed frame instead of freezing the last active sample.
        AdvanceTimers();
        m_Timings = {};
    }

    void SmaaPass::ResetTimingAccounting()
    {
        m_TimingAccounting = {};
    }

    void SmaaPass::PrepareTimingFrame(
        uint32_t expectedStageMask,
        uint64_t timingTag)
    {
        m_CurrentTimerSlot = -1;
        m_TimerActive.fill(false);
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        if (m_TimerExpectedMasks[slot] != 0u)
        {
            if ((timingTag & SmaaTimingCollectTagFlag) != 0u)
                ++m_TimingAccounting.droppedSampleCount;
            return;
        }

        m_CurrentTimerSlot = int32_t(slot);
        m_TimerExpectedMasks[slot] = expectedStageMask;
        m_TimerReadyMasks[slot] = 0u;
        m_TimerExecutionValid[slot] = false;
        m_TimerSerials[slot] = ++m_NextTimerSerial;
        m_TimerTags[slot] = timingTag;
        if ((timingTag & SmaaTimingCollectTagFlag) != 0u)
            ++m_TimingAccounting.issuedSampleCount;
    }

    void SmaaPass::FinishTimingFrame(bool executionValid)
    {
        if (m_CurrentTimerSlot >= 0)
        {
            m_TimerExecutionValid[
                uint32_t(m_CurrentTimerSlot)] = executionValid;
        }
        m_CurrentTimerSlot = -1;
        m_TimerActive.fill(false);
    }

    void SmaaPass::BeginStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        if (m_CurrentTimerSlot < 0)
            return;
        const uint32_t slot = uint32_t(m_CurrentTimerSlot);
        if ((m_TimerExpectedMasks[slot] &
                StageBit(stageIndex)) == 0u ||
            m_TimerPending[stageIndex][slot])
        {
            return;
        }
        commandList->beginTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerActive[stageIndex] = true;
    }

    void SmaaPass::EndStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        if (!m_TimerActive[stageIndex] ||
            m_CurrentTimerSlot < 0)
        {
            return;
        }
        const uint32_t slot = uint32_t(m_CurrentTimerSlot);
        commandList->endTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerPending[stageIndex][slot] = true;
        m_TimerActive[stageIndex] = false;
    }

    void SmaaPass::DrawFullscreen(
        nvrhi::ICommandList* commandList,
        nvrhi::IGraphicsPipeline* pipeline,
        nvrhi::IFramebuffer* framebuffer,
        nvrhi::IBindingSet* bindingSet)
    {
        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = framebuffer;
        state.bindings = { bindingSet };
        state.viewport = nvrhi::ViewportState()
            .addViewportAndScissorRect(
                nvrhi::Viewport(float(m_Size.x), float(m_Size.y)));
        commandList->setGraphicsState(state);
        nvrhi::DrawArguments arguments;
        arguments.instanceCount = 1u;
        arguments.vertexCount = 4u;
        commandList->draw(arguments);
    }

    nvrhi::ITexture* SmaaPass::RunSpatial(
        nvrhi::ICommandList* commandList,
        nvrhi::ITexture* sourceColor,
        nvrhi::ITexture* destination,
        nvrhi::IFramebuffer* destinationFramebuffer,
        AntiAliasingQuality requestedQuality,
        bool skipNeighborhood)
    {
        m_LastSpatialExecutionValid = true;
        UploadLookups(commandList);
        const uint32_t qualityIndex = static_cast<uint32_t>(
            SanitizeSmaaQuality(requestedQuality));

        SmaaConstants constants{};
        // Spatial SMAA uses the official zero subsample indices. Explicitly
        // initialize every vector lane because Donut vector default
        // construction does not guarantee scalar zeroing.
        constants.subsampleIndices = float4::zero();
        constants.rtMetrics = float4(
            1.f / float(m_Size.x),
            1.f / float(m_Size.y),
            float(m_Size.x),
            float(m_Size.y));
        commandList->writeBuffer(
            m_ConstantBuffer,
            &constants,
            sizeof(constants));

        if (!m_EdgeBindingSet || m_BoundEdgeSource != sourceColor)
        {
            nvrhi::BindingSetDesc set;
            set.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Sampler(
                    0, m_PointClampSampler),
                nvrhi::BindingSetItem::Texture_SRV(0, sourceColor)
            };
            m_EdgeBindingSet =
                m_Device->createBindingSet(set, m_EdgeBindingLayout);
            m_BoundEdgeSource = sourceColor;
        }
        if (!m_NeighborhoodBindingSet ||
            m_BoundNeighborhoodSource != sourceColor)
        {
            nvrhi::BindingSetDesc set;
            set.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Sampler(
                    0, m_LinearClampSampler),
                nvrhi::BindingSetItem::Sampler(
                    1, m_PointClampSampler),
                nvrhi::BindingSetItem::Texture_SRV(0, sourceColor),
                nvrhi::BindingSetItem::Texture_SRV(
                    1, m_BlendWeights)
            };
            m_NeighborhoodBindingSet =
                m_Device->createBindingSet(
                    set,
                    m_NeighborhoodBindingLayout);
            m_BoundNeighborhoodSource = sourceColor;
        }

        const bool edgeAndWeightReady =
            sourceColor &&
            m_EdgePipelines[qualityIndex] &&
            m_EdgeBindingSet &&
            m_WeightPipelines[qualityIndex] &&
            m_WeightBindingSet &&
            m_BlendFramebuffer;
        const bool spatialReady =
            edgeAndWeightReady &&
            (skipNeighborhood ||
                (destination &&
                    destinationFramebuffer &&
                    m_NeighborhoodPipeline &&
                    m_NeighborhoodBindingSet));
        if (!spatialReady)
        {
            if (!m_MissingSpatialPermutationReported)
            {
                donut::log::error(
                    "SMAA spatial permutation is unavailable "
                    "(quality=%u); using an identity copy",
                    qualityIndex);
                m_MissingSpatialPermutationReported = true;
            }
            m_LastSpatialExecutionValid = false;
            BeginStage(commandList, Stage::Edge);
            EndStage(commandList, Stage::Edge);
            BeginStage(commandList, Stage::Weight);
            EndStage(commandList, Stage::Weight);
            if (!skipNeighborhood)
            {
                BeginStage(commandList, Stage::Neighborhood);
                if (sourceColor && destination &&
                    sourceColor != destination)
                {
                    commandList->copyTexture(
                        destination,
                        nvrhi::TextureSlice(),
                        sourceColor,
                        nvrhi::TextureSlice());
                }
                EndStage(commandList, Stage::Neighborhood);
            }
            return destination ? destination : sourceColor;
        }

        commandList->clearTextureFloat(
            m_Edges,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_BlendWeights,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));

        commandList->beginMarker("SMAA Edge Detection");
        BeginStage(commandList, Stage::Edge);
        DrawFullscreen(
            commandList,
            m_EdgePipelines[qualityIndex],
            m_EdgeFramebuffer,
            m_EdgeBindingSet);
        EndStage(commandList, Stage::Edge);
        commandList->endMarker();

        commandList->setTextureState(
            m_Edges,
            nvrhi::AllSubresources,
            nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();

        commandList->beginMarker(
            "SMAA Source Preset Blending Weights");
        BeginStage(commandList, Stage::Weight);
        DrawFullscreen(
            commandList,
            m_WeightPipelines[qualityIndex],
            m_BlendFramebuffer,
            m_WeightBindingSet);
        EndStage(commandList, Stage::Weight);
        commandList->endMarker();

        commandList->setTextureState(
            m_BlendWeights,
            nvrhi::AllSubresources,
            nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();

        if (skipNeighborhood)
            return sourceColor;

        commandList->beginMarker("SMAA Neighborhood Blending");
        BeginStage(commandList, Stage::Neighborhood);
        DrawFullscreen(
            commandList,
            m_NeighborhoodPipeline,
            destinationFramebuffer,
            m_NeighborhoodBindingSet);
        EndStage(commandList, Stage::Neighborhood);
        commandList->endMarker();
        return destination;
    }

    nvrhi::ITexture* SmaaPass::Render1x(
        nvrhi::ICommandList* commandList,
        nvrhi::ITexture* sourceColor,
        AntiAliasingQuality quality,
        uint64_t timingTag)
    {
        constexpr uint32_t spatialStageMask =
            StageBit(static_cast<uint32_t>(Stage::Edge)) |
            StageBit(static_cast<uint32_t>(Stage::Weight)) |
            StageBit(static_cast<uint32_t>(Stage::Neighborhood));
        AdvanceTimers();
        PrepareTimingFrame(spatialStageMask, timingTag);
        nvrhi::ITexture* output = RunSpatial(
            commandList,
            sourceColor,
            m_OutputColor,
            m_OutputFramebuffer,
            quality);
        FinishTimingFrame(m_LastSpatialExecutionValid);
        ++m_TimerFrame;
        return output;
    }

    nvrhi::ITexture* SmaaPass::RenderFullScreenPresentation(
        nvrhi::ICommandList* commandList,
        nvrhi::ITexture* sourceColor,
        AntiAliasingQuality quality,
        uint64_t timingTag)
    {
        return Render1x(
            commandList,
            sourceColor,
            quality,
            timingTag);
    }

    nvrhi::ITexture* SmaaPass::RenderSelective(
        nvrhi::ICommandList* commandList,
        nvrhi::ITexture* deJitteredCurrent,
        nvrhi::ITexture* resolvedTemporal,
        nvrhi::ITexture* rejectionMask,
        AntiAliasingQuality quality,
        uint64_t timingTag)
    {
        constexpr uint32_t spatialStageMask =
            StageBit(static_cast<uint32_t>(Stage::Edge)) |
            StageBit(static_cast<uint32_t>(Stage::Weight)) |
            StageBit(static_cast<uint32_t>(Stage::Neighborhood));
        AdvanceTimers();
        PrepareTimingFrame(spatialStageMask, timingTag);

        (void)RunSpatial(
            commandList,
            deJitteredCurrent,
            nullptr,
            nullptr,
            quality,
            true);

        const bool selectiveInputsChanged =
            m_BoundSelectiveCurrent != deJitteredCurrent ||
            m_BoundSelectiveTemporal != resolvedTemporal ||
            m_BoundSelectiveRejection != rejectionMask;
        if (selectiveInputsChanged)
        {
            m_SelectiveNeighborhoodBindingSet = nullptr;
            m_BoundSelectiveCurrent = deJitteredCurrent;
            m_BoundSelectiveTemporal = resolvedTemporal;
            m_BoundSelectiveRejection = rejectionMask;
        }
        if (!m_SelectiveNeighborhoodBindingSet &&
            deJitteredCurrent &&
            resolvedTemporal &&
            rejectionMask)
        {
            nvrhi::BindingSetDesc set;
            set.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Sampler(
                    0, m_LinearClampSampler),
                nvrhi::BindingSetItem::Sampler(
                    1, m_PointClampSampler),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, deJitteredCurrent),
                nvrhi::BindingSetItem::Texture_SRV(
                    1, resolvedTemporal),
                nvrhi::BindingSetItem::Texture_SRV(
                    2, m_BlendWeights),
                nvrhi::BindingSetItem::Texture_SRV(
                    3, rejectionMask),
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_OutputColor)
            };
            m_SelectiveNeighborhoodBindingSet =
                m_Device->createBindingSet(
                    set,
                    m_SelectiveNeighborhoodBindingLayout);
        }

        const bool selectiveReady =
            m_LastSpatialExecutionValid &&
            deJitteredCurrent &&
            resolvedTemporal &&
            rejectionMask &&
            m_OutputColor &&
            m_SelectiveNeighborhoodPipeline &&
            m_SelectiveNeighborhoodBindingSet;
        if (!selectiveReady)
        {
            if (!m_MissingSelectivePermutationReported)
            {
                donut::log::error(
                    "Selective SMAA is unavailable; using the temporal input");
                m_MissingSelectivePermutationReported = true;
            }
            BeginStage(commandList, Stage::Neighborhood);
            if (resolvedTemporal && m_OutputColor &&
                resolvedTemporal != m_OutputColor.Get())
            {
                commandList->copyTexture(
                    m_OutputColor,
                    nvrhi::TextureSlice(),
                    resolvedTemporal,
                    nvrhi::TextureSlice());
            }
            EndStage(commandList, Stage::Neighborhood);
            FinishTimingFrame(false);
            ++m_TimerFrame;
            return m_OutputColor
                ? m_OutputColor.Get()
                : resolvedTemporal;
        }

        commandList->beginMarker(
            "Selective SMAA Neighborhood Blending");
        BeginStage(commandList, Stage::Neighborhood);
        commandList->setTextureState(
            m_OutputColor,
            nvrhi::AllSubresources,
            nvrhi::ResourceStates::UnorderedAccess);
        commandList->commitBarriers();
        nvrhi::ComputeState state;
        state.pipeline = m_SelectiveNeighborhoodPipeline;
        state.bindings = {
            m_SelectiveNeighborhoodBindingSet
        };
        commandList->setComputeState(state);
        commandList->dispatch(
            (m_Size.x + 7u) / 8u,
            (m_Size.y + 7u) / 8u,
            1u);
        EndStage(commandList, Stage::Neighborhood);
        commandList->endMarker();

        FinishTimingFrame(true);
        ++m_TimerFrame;
        return m_OutputColor;
    }

#if UVSR_AA_DEVELOPER_OVERRIDES
    nvrhi::ITexture* SmaaPass::RenderDebugView(
        nvrhi::ICommandList* commandList,
        MiniEngineTaaDebugView debugView,
        nvrhi::ITexture* sourceColor,
        nvrhi::ITexture* filteredColor)
    {
        if (!IsSmaaDebugVisualization(debugView) ||
            !sourceColor ||
            !filteredColor)
        {
            return filteredColor;
        }

        const uint32_t permutation =
            GetSmaaDebugPermutationIndex(debugView);
        if (permutation >= c_DebugViewCount ||
            !m_DebugPipelines[permutation])
        {
            return filteredColor;
        }

        if (!m_DebugBindingSet ||
            m_BoundDebugSource != sourceColor ||
            m_BoundDebugFiltered != filteredColor)
        {
            nvrhi::BindingSetDesc set;
            set.bindings = {
                nvrhi::BindingSetItem::Texture_SRV(0, sourceColor),
                nvrhi::BindingSetItem::Texture_SRV(1, m_Edges),
                nvrhi::BindingSetItem::Texture_SRV(
                    2, m_BlendWeights),
                nvrhi::BindingSetItem::Texture_SRV(3, filteredColor)
            };
            m_DebugBindingSet = m_Device->createBindingSet(
                set,
                m_DebugBindingLayout);
            m_BoundDebugSource = sourceColor;
            m_BoundDebugFiltered = filteredColor;
        }
        if (!m_DebugBindingSet ||
            !m_DebugFramebuffer ||
            !m_DebugOutput)
        {
            return filteredColor;
        }

        commandList->beginMarker("SMAA Developer Visualization");
        DrawFullscreen(
            commandList,
            m_DebugPipelines[permutation],
            m_DebugFramebuffer,
            m_DebugBindingSet);
        commandList->endMarker();
        return m_DebugOutput;
    }
#endif
}
