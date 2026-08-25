#include "lighting_accumulation_pass.h"
#include "renderer_common_passes.h"
#include "renderer_shader_factory.h"

#include <donut/core/math/math.h>

#include <algorithm>
#include <cstddef>

using namespace donut::math;

#include "lighting_accumulation_cb.h"

static_assert(sizeof(LightingAccumulationConstants) % 16u == 0u,
    "Lighting accumulation constants must preserve HLSL alignment.");
static_assert(offsetof(
        LightingAccumulationConstants,
        resetHistory) == 8u,
    "Lighting accumulation reset flag drifted within its register.");

namespace uvsr
{
    namespace
    {
        nvrhi::TextureHandle CreateHistoryTexture(
            nvrhi::IDevice* device,
            uint32_t width,
            uint32_t height,
            nvrhi::Format format,
            const char* debugName)
        {
            nvrhi::TextureDesc description;
            description.width = width;
            description.height = height;
            description.format = format;
            description.dimension = nvrhi::TextureDimension::Texture2D;
            description.isUAV = true;
            description.debugName = debugName;
            description.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            return device->createTexture(description);
        }
    }

    LightingAccumulationPass::LightingAccumulationPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<RendererShaderFactory>& shaderFactory)
        : m_Device(device)
    {
        if (!device || !shaderFactory)
            return;

        nvrhi::BufferDesc constantDescription;
        constantDescription.byteSize =
            sizeof(LightingAccumulationConstants);
        constantDescription.debugName =
            "LightingAccumulationConstants";
        constantDescription.isConstantBuffer = true;
        constantDescription.isVolatile = true;
        constantDescription.maxVersions =
            RendererMaxConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantDescription);

        m_DisabledAttemptMask = CreateHistoryTexture(
            device,
            1u,
            1u,
            nvrhi::Format::R32_UINT,
            "Disabled Lighting Sample Attempt Mask");

        nvrhi::BindingLayoutDesc prepareLayoutDescription;
        prepareLayoutDescription.visibility = nvrhi::ShaderType::Compute;
        prepareLayoutDescription.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_PrepareBindingLayout = device->createBindingLayout(
            prepareLayoutDescription);

        nvrhi::BindingLayoutDesc resolveLayoutDescription;
        resolveLayoutDescription.visibility = nvrhi::ShaderType::Compute;
        resolveLayoutDescription.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1)
        };
        m_ResolveBindingLayout = device->createBindingLayout(
            resolveLayoutDescription);

        m_PrepareShader = shaderFactory->CreateShader(
            "uvsr/lighting_accumulation_prepare_cs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Compute);
        m_ResolveShader = shaderFactory->CreateShader(
            "uvsr/lighting_accumulation_cs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Compute);
        if (m_PrepareShader && m_PrepareBindingLayout)
        {
            nvrhi::ComputePipelineDesc pipelineDescription;
            pipelineDescription.CS = m_PrepareShader;
            pipelineDescription.bindingLayouts = {
                m_PrepareBindingLayout };
            m_PreparePipeline = device->createComputePipeline(
                pipelineDescription);
        }
        if (m_ResolveShader && m_ResolveBindingLayout)
        {
            nvrhi::ComputePipelineDesc pipelineDescription;
            pipelineDescription.CS = m_ResolveShader;
            pipelineDescription.bindingLayouts = {
                m_ResolveBindingLayout };
            m_ResolvePipeline = device->createComputePipeline(
                pipelineDescription);
        }
    }

    bool LightingAccumulationPass::IsValid() const
    {
        return m_Device && m_ConstantBuffer &&
            m_PrepareBindingLayout && m_ResolveBindingLayout &&
            m_PrepareShader && m_ResolveShader &&
            m_PreparePipeline && m_ResolvePipeline &&
            m_DisabledAttemptMask;
    }

    LightingSampleSchedule
        LightingAccumulationPass::GetDisabledSchedule() const
    {
        return { m_DisabledAttemptMask, false, false, 0u };
    }

    bool LightingAccumulationPass::EnsureResources(
        uint32_t width,
        uint32_t height)
    {
        if (width == 0u || height == 0u)
            return false;
        if (m_Mean[0] && m_Count[0] && m_AttemptMask &&
            m_Width == width && m_Height == height)
        {
            return true;
        }

        ResetBindingCache();
        m_Mean = {};
        m_Count = {};
        m_AttemptMask = nullptr;
        m_Width = width;
        m_Height = height;
        for (uint32_t index = 0u; index < 2u; ++index)
        {
            m_Mean[index] = CreateHistoryTexture(
                m_Device,
                width,
                height,
                nvrhi::Format::RGBA32_FLOAT,
                index == 0u
                    ? "Lighting Accumulation Mean A"
                    : "Lighting Accumulation Mean B");
            m_Count[index] = CreateHistoryTexture(
                m_Device,
                width,
                height,
                nvrhi::Format::R32_UINT,
                index == 0u
                    ? "Lighting Accumulation Count A"
                    : "Lighting Accumulation Count B");
            if (!m_Mean[index] || !m_Count[index])
            {
                m_Mean = {};
                m_Count = {};
                return false;
            }
        }
        m_AttemptMask = CreateHistoryTexture(
            m_Device,
            width,
            height,
            nvrhi::Format::R32_UINT,
            "Lighting Sample Attempt Mask");
        if (!m_AttemptMask)
        {
            m_Mean = {};
            m_Count = {};
            return false;
        }
        m_WriteIndex = 0u;
        m_PreparedToken = 0u;
        m_HasHistoryEpoch = false;
        return true;
    }

    bool LightingAccumulationPass::EnsurePrepareBindingSet(
        uint32_t writeIndex)
    {
        if (writeIndex > 1u || !m_AttemptMask ||
            !m_Count[writeIndex ^ 1u])
        {
            return false;
        }
        if (m_PrepareBindingSets[writeIndex])
            return true;

        const uint32_t readIndex = writeIndex ^ 1u;
        nvrhi::BindingSetDesc description;
        description.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, m_Count[readIndex]),
            nvrhi::BindingSetItem::Texture_UAV(0, m_AttemptMask)
        };
        m_PrepareBindingSets[writeIndex] = m_Device->createBindingSet(
            description,
            m_PrepareBindingLayout);
        return bool(m_PrepareBindingSets[writeIndex]);
    }

    bool LightingAccumulationPass::EnsureResolveBindingSet(
        nvrhi::ITexture* source,
        nvrhi::ITexture* attemptMask,
        uint32_t writeIndex)
    {
        if (!source || !attemptMask || writeIndex > 1u ||
            !m_Mean[writeIndex] || !m_Count[writeIndex])
        {
            return false;
        }
        if (m_BoundSource != source ||
            m_BoundAttemptMask != attemptMask)
        {
            ResetBindingCache();
            m_BoundSource = source;
            m_BoundAttemptMask = attemptMask;
        }
        if (m_ResolveBindingSets[writeIndex])
            return true;

        const uint32_t readIndex = writeIndex ^ 1u;
        nvrhi::BindingSetDesc description;
        description.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, source),
            nvrhi::BindingSetItem::Texture_SRV(1, m_Mean[readIndex]),
            nvrhi::BindingSetItem::Texture_SRV(2, m_Count[readIndex]),
            nvrhi::BindingSetItem::Texture_SRV(3, attemptMask),
            nvrhi::BindingSetItem::Texture_UAV(0, m_Mean[writeIndex]),
            nvrhi::BindingSetItem::Texture_UAV(1, m_Count[writeIndex])
        };
        m_ResolveBindingSets[writeIndex] = m_Device->createBindingSet(
            description,
            m_ResolveBindingLayout);
        return bool(m_ResolveBindingSets[writeIndex]);
    }

    LightingSampleSchedule LightingAccumulationPass::PrepareAttempts(
        nvrhi::ICommandList* commandList,
        uint32_t width,
        uint32_t height,
        uint64_t historyEpoch)
    {
        if (!IsValid() || !commandList ||
            !EnsureResources(width, height))
        {
            return {};
        }

        // A second prepare without a completed resolve means the previous
        // producer transaction was abandoned. Force a full retry rather than
        // trusting any partially updated producer texture.
        if (m_PreparedToken != 0u)
            ResetHistory();

        const bool resetHistory = !m_HasHistoryEpoch ||
            m_HistoryEpoch != historyEpoch;
        const uint32_t writeIndex = m_WriteIndex;
        uint64_t token = m_NextScheduleToken++;
        if (token == 0u)
        {
            token = m_NextScheduleToken++;
            if (token == 0u)
                token = 1u;
        }
        m_PreparedToken = token;
        m_PreparedHistoryEpoch = historyEpoch;
        m_PreparedWriteIndex = writeIndex;
        m_PreparedResetHistory = resetHistory;
        if (!EnsurePrepareBindingSet(writeIndex))
        {
            m_PreparedToken = 0u;
            m_HasHistoryEpoch = false;
            return {};
        }

        LightingAccumulationConstants constants{};
        constants.extent = { width, height };
        constants.resetHistory = resetHistory ? 1u : 0u;
        commandList->writeBuffer(
            m_ConstantBuffer,
            &constants,
            sizeof(constants));

        nvrhi::ComputeState state;
        state.pipeline = m_PreparePipeline;
        state.bindings = { m_PrepareBindingSets[writeIndex] };
        commandList->beginMarker("Prepare Lighting Sample Attempts");
        commandList->setComputeState(state);
        commandList->dispatch(
            (width + 7u) / 8u,
            (height + 7u) / 8u);
        commandList->endMarker();

        return { m_AttemptMask, true, resetHistory, token };
    }

    LightingAccumulationResult LightingAccumulationPass::Resolve(
        nvrhi::ICommandList* commandList,
        nvrhi::ITexture* source,
        const LightingSampleSchedule& schedule)
    {
        if (!IsValid() || !commandList || !source || !schedule ||
            schedule.token != m_PreparedToken ||
            !schedule.enabled ||
            schedule.historyReset != m_PreparedResetHistory ||
            schedule.attemptMask != m_AttemptMask.Get())
        {
            CancelPreparedSchedule(schedule);
            return { source, false };
        }

        const nvrhi::TextureDesc& sourceDescription = source->getDesc();
        if (sourceDescription.dimension !=
                nvrhi::TextureDimension::Texture2D ||
            sourceDescription.sampleCount != 1u ||
            sourceDescription.width != m_Width ||
            sourceDescription.height != m_Height ||
            m_PreparedWriteIndex != m_WriteIndex ||
            !EnsureResolveBindingSet(
                source,
                schedule.attemptMask,
                m_PreparedWriteIndex))
        {
            CancelPreparedSchedule(schedule);
            return { source, false };
        }

        LightingAccumulationConstants constants{};
        constants.extent = {
            sourceDescription.width,
            sourceDescription.height };
        constants.resetHistory = m_PreparedResetHistory ? 1u : 0u;
        commandList->writeBuffer(
            m_ConstantBuffer,
            &constants,
            sizeof(constants));

        nvrhi::ComputeState state;
        state.pipeline = m_ResolvePipeline;
        state.bindings = {
            m_ResolveBindingSets[m_PreparedWriteIndex] };
        commandList->beginMarker("Resolve Lighting Sample Accumulation");
        commandList->setComputeState(state);
        commandList->dispatch(
            (sourceDescription.width + 7u) / 8u,
            (sourceDescription.height + 7u) / 8u);
        commandList->endMarker();

        const uint32_t committedWriteIndex = m_PreparedWriteIndex;
        m_HistoryEpoch = m_PreparedHistoryEpoch;
        m_HasHistoryEpoch = true;
        m_WriteIndex ^= 1u;
        m_PreparedToken = 0u;
        return { m_Mean[committedWriteIndex], true };
    }

    void LightingAccumulationPass::CancelPreparedSchedule(
        const LightingSampleSchedule& schedule)
    {
        if (schedule.token == 0u || schedule.token != m_PreparedToken)
            return;

        m_PreparedToken = 0u;
        m_HasHistoryEpoch = false;
    }

    void LightingAccumulationPass::ResetHistory()
    {
        m_PreparedToken = 0u;
        m_HasHistoryEpoch = false;
    }

    void LightingAccumulationPass::ResetBindingCache()
    {
        m_PrepareBindingSets = {};
        m_ResolveBindingSets = {};
        m_BoundSource = nullptr;
        m_BoundAttemptMask = nullptr;
    }
}
