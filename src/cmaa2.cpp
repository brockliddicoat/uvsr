#include "cmaa2.h"

#include <donut/core/log.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::engine;

namespace
{
    struct alignas(16) Cmaa2Constants
    {
        float edgeThreshold = uvsr::Cmaa2DefaultEdgeThreshold;
        float padding[3] = {};
    };

    static_assert(sizeof(Cmaa2Constants) == 16u);

    nvrhi::TextureHandle CreateTexture(
        nvrhi::IDevice* device,
        uint32_t width,
        uint32_t height,
        nvrhi::Format format,
        const char* debugName)
    {
        nvrhi::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.dimension = nvrhi::TextureDimension::Texture2D;
        desc.mipLevels = 1u;
        desc.format = format;
        desc.isUAV = true;
        desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        desc.keepInitialState = true;
        desc.debugName = debugName;
        return device->createTexture(desc);
    }

    nvrhi::BufferHandle CreateStructuredBuffer(
        nvrhi::IDevice* device,
        uint64_t elementCount,
        uint32_t elementSize,
        const char* debugName)
    {
        nvrhi::BufferDesc desc;
        desc.byteSize =
            std::max<uint64_t>(elementCount, 1u) * elementSize;
        desc.structStride = elementSize;
        desc.canHaveUAVs = true;
        desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        desc.keepInitialState = true;
        desc.debugName = debugName;
        return device->createBuffer(desc);
    }

    nvrhi::BufferHandle CreateRawBuffer(
        nvrhi::IDevice* device,
        uint64_t byteSize,
        bool indirectArguments,
        const char* debugName)
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = std::max<uint64_t>(byteSize, 16u);
        desc.canHaveUAVs = true;
        desc.canHaveRawViews = true;
        desc.isDrawIndirectArgs = indirectArguments;
        desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        desc.keepInitialState = true;
        desc.debugName = debugName;
        return device->createBuffer(desc);
    }
}

namespace uvsr
{
    Cmaa2Pass::Cmaa2Pass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        nvrhi::ITexture* sceneColor)
        : m_Device(device)
    {
        if (!device || !shaderFactory || !sceneColor)
            return;

        const nvrhi::TextureDesc& sceneDesc =
            sceneColor->getDesc();
        if (sceneDesc.sampleCount != 1u ||
            sceneDesc.dimension !=
                nvrhi::TextureDimension::Texture2D ||
            sceneDesc.format != nvrhi::Format::RGBA16_FLOAT)
        {
            log::error(
                "Intel CMAA2 requires UVSR's single-sample "
                "RGBA16F color target");
            return;
        }
        const uint64_t pixelCount =
            uint64_t(sceneDesc.width) * uint64_t(sceneDesc.height);
        if (sceneDesc.width >= (1u << 14u) ||
            sceneDesc.height >= (1u << 14u) ||
            pixelCount > (uint64_t(1u) << 26u))
        {
            // The pinned shader packs X and Y in 14 bits and a deferred-item
            // address in 26 bits. Accepting a larger dense list aliases the
            // address with its header flags and silently corrupts edge work.
            log::error(
                "Intel CMAA2 does not support a render extent of "
                "%ux%u",
                sceneDesc.width,
                sceneDesc.height);
            return;
        }

        m_Size = math::uint2(sceneDesc.width, sceneDesc.height);

        m_OutputColor = CreateTexture(
            device,
            m_Size.x,
            m_Size.y,
            sceneDesc.format,
            "CMAA2/OutputColor");
        m_WorkingEdges = CreateTexture(
            device,
            (m_Size.x + 1u) / 2u,
            m_Size.y,
            nvrhi::Format::R8_UINT,
            "CMAA2/WorkingEdges");
        m_DeferredItemHeads = CreateTexture(
            device,
            (m_Size.x + 1u) / 2u,
            (m_Size.y + 1u) / 2u,
            nvrhi::Format::R32_UINT,
            "CMAA2/DeferredItemHeads");

        // Use the official fully safe capacities rather than its optional
        // reduced-memory estimates. Extreme foliage/noise therefore cannot
        // silently drop candidates because a compact append list overflowed.
        // Deferred-location addresses cover every 2x2 item-head quad, so odd
        // extents require each dimension to be rounded up independently.
        const uint64_t deferredLocationCount =
            uint64_t((sceneDesc.width + 1u) / 2u) *
            uint64_t((sceneDesc.height + 1u) / 2u);
        m_ShapeCandidates = CreateStructuredBuffer(
            device,
            pixelCount,
            sizeof(uint32_t),
            "CMAA2/ShapeCandidates");
        m_DeferredItems = CreateStructuredBuffer(
            device,
            pixelCount,
            sizeof(uint32_t) * 2u,
            "CMAA2/DeferredItems");
        m_DeferredLocations = CreateStructuredBuffer(
            device,
            deferredLocationCount,
            sizeof(uint32_t),
            "CMAA2/DeferredLocations");
        m_Control = CreateRawBuffer(
            device,
            16u * sizeof(uint32_t),
            false,
            "CMAA2/Control");
        m_IndirectArguments = CreateRawBuffer(
            device,
            sizeof(nvrhi::DispatchIndirectArguments),
            true,
            "CMAA2/IndirectArguments");

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(Cmaa2Constants);
        constantBufferDesc.debugName = "CMAA2 Constants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDesc);

        nvrhi::BindingLayoutDesc layout;
        layout.visibility = nvrhi::ShaderType::Compute;
        layout.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(5),
            nvrhi::BindingLayoutItem::RawBuffer_UAV(6),
            nvrhi::BindingLayoutItem::RawBuffer_UAV(7)
        };
        m_BindingLayout = device->createBindingLayout(layout);
        RebuildBindingSet(sceneColor);

        nvrhi::ComputePipelineDesc pipeline;
        pipeline.bindingLayouts = { m_BindingLayout };
        for (uint32_t detector = 0u;
            detector < c_DetectorCount;
            ++detector)
        {
            std::vector<ShaderMacro> macros = {
                { "CMAA2_EDGE_DETECTION_LUMA_PATH",
                    detector == static_cast<uint32_t>(
                        Cmaa2EdgeDetector::Luma)
                        ? "1"
                        : "0" }
            };
            pipeline.CS = shaderFactory->CreateShader(
                "uvsr/cmaa2.hlsl",
                "EdgesColor2x2CS",
                &macros,
                nvrhi::ShaderType::Compute);
            m_EdgePipelines[detector] = pipeline.CS
                ? device->createComputePipeline(pipeline)
                : nullptr;
        }

        std::vector<ShaderMacro> sharedMacros = {
            { "CMAA2_EDGE_DETECTION_LUMA_PATH", "1" }
        };
        const auto createSharedPipeline =
            [&](const char* entryPoint)
        {
            pipeline.CS = shaderFactory->CreateShader(
                "uvsr/cmaa2.hlsl",
                entryPoint,
                &sharedMacros,
                nvrhi::ShaderType::Compute);
            return pipeline.CS
                ? device->createComputePipeline(pipeline)
                : nullptr;
        };
        m_CandidatePipeline =
            createSharedPipeline("ProcessCandidatesCS");
        m_ApplyPipeline =
            createSharedPipeline("DeferredColorApply2x2CS");
        m_DispatchArgumentPipeline =
            createSharedPipeline("ComputeDispatchArgsCS");

        for (auto& stageQueries : m_TimerQueries)
            for (auto& query : stageQueries)
                query = device->createTimerQuery();

    }

    bool Cmaa2Pass::IsValid() const
    {
        if (!m_OutputColor ||
            !m_WorkingEdges ||
            !m_DeferredItemHeads ||
            !m_ShapeCandidates ||
            !m_DeferredLocations ||
            !m_DeferredItems ||
            !m_Control ||
            !m_IndirectArguments ||
            !m_ConstantBuffer ||
            !m_BindingLayout ||
            !m_BindingSet ||
            !m_CandidatePipeline ||
            !m_ApplyPipeline ||
            !m_DispatchArgumentPipeline)
        {
            return false;
        }
        for (uint32_t detector = 0u;
            detector < c_DetectorCount;
            ++detector)
        {
            if (!m_EdgePipelines[detector])
                return false;
        }
        return true;
    }

    void Cmaa2Pass::RebuildBindingSet(
        nvrhi::ITexture* sourceColor)
    {
        if (!sourceColor || sourceColor == m_BoundSource)
            return;

        // t1-t3 are compiled out in the single-sample, in-place-luma
        // permutation. Bind valid descriptors anyway so one layout can
        // faithfully serve all four official entry points.
        nvrhi::BindingSetDesc set;
        set.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, sourceColor),
            nvrhi::BindingSetItem::Texture_SRV(1, sourceColor),
            nvrhi::BindingSetItem::Texture_SRV(2, sourceColor),
            nvrhi::BindingSetItem::Texture_SRV(3, sourceColor),
            nvrhi::BindingSetItem::Texture_UAV(0, m_OutputColor),
            nvrhi::BindingSetItem::Texture_UAV(1, m_WorkingEdges),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                2, m_ShapeCandidates),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                3, m_DeferredLocations),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                4, m_DeferredItems),
            nvrhi::BindingSetItem::Texture_UAV(
                5, m_DeferredItemHeads),
            nvrhi::BindingSetItem::RawBuffer_UAV(6, m_Control),
            nvrhi::BindingSetItem::RawBuffer_UAV(
                7, m_IndirectArguments)
        };
        m_BindingSet =
            m_Device->createBindingSet(set, m_BindingLayout);
        m_BoundSource = sourceColor;
    }

    void Cmaa2Pass::UpdateSourceColor(
        nvrhi::ITexture* sourceColor)
    {
        if (!sourceColor)
            return;

        const nvrhi::TextureDesc& sourceDesc =
            sourceColor->getDesc();
        if (sourceDesc.width != m_Size.x ||
            sourceDesc.height != m_Size.y ||
            sourceDesc.sampleCount != 1u ||
            sourceDesc.dimension !=
                nvrhi::TextureDimension::Texture2D ||
            sourceDesc.format != nvrhi::Format::RGBA16_FLOAT)
        {
            return;
        }

        RebuildBindingSet(sourceColor);
    }

    void Cmaa2Pass::AdvanceTimers()
    {
        for (uint32_t stageIndex = 0u;
            stageIndex < static_cast<uint32_t>(Stage::Count);
            ++stageIndex)
        {
            for (uint32_t slot = 0u; slot < c_TimerLatency; ++slot)
            {
                if (!m_TimerPending[stageIndex][slot])
                    continue;
                nvrhi::ITimerQuery* query =
                    m_TimerQueries[stageIndex][slot];
                if (!m_Device->pollTimerQuery(query))
                    continue;
                const float milliseconds =
                    m_Device->getTimerQueryTime(query) * 1000.f;
                m_Device->resetTimerQuery(query);
                m_TimerPending[stageIndex][slot] = false;
                switch (static_cast<Stage>(stageIndex))
                {
                case Stage::Edge:
                    m_Timings.edgeMilliseconds = milliseconds;
                    break;
                case Stage::Candidate:
                    m_Timings.candidateMilliseconds = milliseconds;
                    break;
                case Stage::Apply:
                    m_Timings.applyMilliseconds = milliseconds;
                    m_Timings.available = true;
                    break;
                default:
                    break;
                }
            }
        }
    }

    void Cmaa2Pass::BeginStage(
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

    void Cmaa2Pass::EndStage(
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

    void Cmaa2Pass::PublishUavWrites(
        nvrhi::ICommandList* commandList)
    {
        nvrhi::utils::TextureUavBarrier(
            commandList, m_WorkingEdges);
        nvrhi::utils::TextureUavBarrier(
            commandList, m_DeferredItemHeads);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_ShapeCandidates);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_DeferredLocations);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_DeferredItems);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_Control);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_IndirectArguments);
        commandList->commitBarriers();
    }

    nvrhi::ITexture* Cmaa2Pass::Render(
        nvrhi::ICommandList* commandList,
        nvrhi::ITexture* sourceColor,
        const ResolvedAntiAliasingSettings& settings)
    {
        AdvanceTimers();
        m_TimerActive.fill(false);
        if (!commandList ||
            !sourceColor ||
            !settings.cmaa2Enabled ||
            !IsValid())
        {
            m_Timings = {};
            return sourceColor;
        }

        const nvrhi::TextureDesc& sourceDesc =
            sourceColor->getDesc();
        if (sourceDesc.width != m_Size.x ||
            sourceDesc.height != m_Size.y ||
            sourceDesc.sampleCount != 1u ||
            sourceDesc.dimension !=
                nvrhi::TextureDimension::Texture2D ||
            sourceDesc.format != nvrhi::Format::RGBA16_FLOAT)
        {
            // copyTexture requires format-compatible resources. Keep the
            // presentation unchanged if a caller supplies a compact temporal
            // history or any other incompatible per-frame source.
            m_Timings = {};
            return sourceColor;
        }

        RebuildBindingSet(sourceColor);
        if (!m_BindingSet)
            return sourceColor;

        const Cmaa2EdgeDetector detector =
            SanitizeCmaa2EdgeDetector(settings.cmaa2EdgeDetector);
        const uint32_t detectorIndex =
            static_cast<uint32_t>(detector);
        Cmaa2Constants constants{};
        constants.edgeThreshold =
            ClampCmaa2EdgeThreshold(settings.cmaa2EdgeThreshold);
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));

        commandList->beginMarker("Intel CMAA2");
        commandList->copyTexture(
            m_OutputColor,
            nvrhi::TextureSlice(),
            sourceColor,
            nvrhi::TextureSlice());
        commandList->setTextureState(
            m_OutputColor,
            nvrhi::AllSubresources,
            nvrhi::ResourceStates::UnorderedAccess);
        commandList->setBufferState(
            m_IndirectArguments,
            nvrhi::ResourceStates::UnorderedAccess);
        commandList->commitBarriers();

        nvrhi::ComputeState state;
        state.bindings = { m_BindingSet };

        if (m_InitializeControl)
        {
            // The official implementation clears these counters by running
            // its second argument-generation branch once. An explicit clear
            // is equivalent and cannot inherit garbage from a fresh buffer.
            commandList->clearBufferUInt(m_Control, 0u);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_Control);
            commandList->commitBarriers();
            m_InitializeControl = false;
        }

        BeginStage(commandList, Stage::Edge);
        state.pipeline = m_EdgePipelines[detectorIndex];
        state.indirectParams = nullptr;
        commandList->setComputeState(state);
        // The official 16x16 input tile has a one-thread border and emits
        // 14x14 quads, i.e. 28x28 output pixels.
        commandList->dispatch(
            (m_Size.x + 27u) / 28u,
            (m_Size.y + 27u) / 28u,
            1u);
        EndStage(commandList, Stage::Edge);
        PublishUavWrites(commandList);

        state.pipeline = m_DispatchArgumentPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(2u, 1u, 1u);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_Control);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_IndirectArguments);
        commandList->commitBarriers();
        commandList->setBufferState(
            m_IndirectArguments,
            nvrhi::ResourceStates::IndirectArgument);
        commandList->commitBarriers();

        BeginStage(commandList, Stage::Candidate);
        state.pipeline = m_CandidatePipeline;
        state.indirectParams = m_IndirectArguments;
        commandList->setComputeState(state);
        commandList->dispatchIndirect(0u);
        EndStage(commandList, Stage::Candidate);
        commandList->setBufferState(
            m_IndirectArguments,
            nvrhi::ResourceStates::UnorderedAccess);
        commandList->commitBarriers();
        PublishUavWrites(commandList);

        state.pipeline = m_DispatchArgumentPipeline;
        state.indirectParams = nullptr;
        commandList->setComputeState(state);
        commandList->dispatch(1u, 2u, 1u);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_Control);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_IndirectArguments);
        commandList->commitBarriers();
        commandList->setBufferState(
            m_IndirectArguments,
            nvrhi::ResourceStates::IndirectArgument);
        commandList->commitBarriers();

        BeginStage(commandList, Stage::Apply);
        state.pipeline = m_ApplyPipeline;
        state.indirectParams = m_IndirectArguments;
        commandList->setComputeState(state);
        commandList->dispatchIndirect(0u);
        EndStage(commandList, Stage::Apply);
        commandList->setBufferState(
            m_IndirectArguments,
            nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(
            m_OutputColor,
            nvrhi::AllSubresources,
            nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();
        commandList->endMarker();

        ++m_TimerFrame;
        return m_OutputColor;
    }
}
