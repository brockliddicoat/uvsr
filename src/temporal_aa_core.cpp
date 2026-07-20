#include "temporal_aa_core.h"

#include <algorithm>

namespace
{
    nvrhi::TextureHandle CreateTemporalHistoryTexture(
        nvrhi::IDevice* device,
        const uvsr::TemporalHistoryDesc& historyDesc,
        nvrhi::Format format,
        const std::string& debugName,
        bool renderTarget,
        bool unorderedAccess)
    {
        nvrhi::TextureDesc desc;
        desc.width = historyDesc.size.x;
        desc.height = historyDesc.size.y;
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
    float GetTemporalAaSourceDepthPairQuantizationError(
        nvrhi::Format format)
    {
        switch (format)
        {
        case nvrhi::Format::D24S8:
            return 1.f / 16777215.f;
        case nvrhi::Format::D16:
            return 1.f / 65535.f;
        case nvrhi::Format::D32:
        case nvrhi::Format::D32S8:
        case nvrhi::Format::R32_FLOAT:
            // SV_Position.z and the previous clip calculation are already
            // float32 in the motion producer, so a float32 depth target does
            // not add a second storage quantization domain.
            return 0.f;
        default:
            // Temporal AA receives one of the G-buffer formats above. Unknown
            // test or future resources fail to add an invented tolerance.
            return 0.f;
        }
    }

    void TemporalHistoryState::Initialize(
        nvrhi::IDevice* device,
        const TemporalHistoryDesc& desc)
    {
        m_Size = desc.size;
        m_MaximumAccumulation =
            std::max(desc.maximumAccumulation, 1u);
        m_AccumulationCount = 0u;
        m_ResetCount = 0u;
        m_Valid = {};
        m_CommittedSequence = {};
        m_LastCommittedSequence = 0u;
        m_HasCommittedSequence = false;
        m_ClearPending = true;

        for (uint32_t slot = 0u; slot < 2u; ++slot)
        {
            const std::string suffix = std::to_string(slot);
            m_Color[slot] = CreateTemporalHistoryTexture(
                device,
                desc,
                nvrhi::Format::RGBA16_FLOAT,
                desc.debugName + "/Color" + suffix,
                desc.colorRenderTarget,
                desc.colorUnorderedAccess);
            m_Depth[slot] = CreateTemporalHistoryTexture(
                device,
                desc,
                nvrhi::Format::R32_FLOAT,
                desc.debugName + "/Depth" + suffix,
                desc.depthRenderTarget,
                desc.depthUnorderedAccess);
        }
    }

    bool TemporalHistoryState::Invalidate()
    {
        const bool changed =
            m_Valid[0] ||
            m_Valid[1] ||
            m_AccumulationCount != 0u ||
            !m_ClearPending;
        m_Valid = {};
        m_CommittedSequence = {};
        m_AccumulationCount = 0u;
        m_LastCommittedSequence = 0u;
        m_HasCommittedSequence = false;
        if (changed)
        {
            m_ClearPending = true;
            ++m_ResetCount;
        }
        return changed;
    }

    bool TemporalHistoryState::PrepareForFirstUse(
        nvrhi::ICommandList* commandList)
    {
        if (!m_ClearPending)
            return false;

        for (uint32_t slot = 0u; slot < 2u; ++slot)
        {
            commandList->clearTextureFloat(
                m_Color[slot],
                nvrhi::AllSubresources,
                nvrhi::Color(0.f));
            commandList->clearTextureFloat(
                m_Depth[slot],
                nvrhi::AllSubresources,
                nvrhi::Color(0.f));
        }
        m_ClearPending = false;
        return true;
    }

    bool TemporalHistoryState::CanRead(
        uint32_t slot,
        bool previousViewAvailable,
        uint64_t sequenceIndex) const
    {
        return slot < m_Valid.size() &&
            previousViewAvailable &&
            m_Valid[slot] &&
            m_AccumulationCount > 0u &&
            m_HasCommittedSequence &&
            sequenceIndex > 0u &&
            m_LastCommittedSequence == sequenceIndex - 1u &&
            m_CommittedSequence[slot] == sequenceIndex - 1u;
    }

    void TemporalHistoryState::Commit(
        uint32_t slot,
        uint64_t sequenceIndex)
    {
        if (slot >= m_Valid.size())
            return;

        const bool sequenceIsContinuous =
            m_HasCommittedSequence &&
            sequenceIndex > 0u &&
            m_LastCommittedSequence == sequenceIndex - 1u;
        if (!sequenceIsContinuous)
        {
            // Physical contents do not need clearing here: validity is the
            // authority, and the new slot is fully overwritten before Commit.
            // Preserve m_ClearPending so a sequence discontinuity does not
            // introduce an unnecessary clear or reset count.
            m_Valid = {};
            m_CommittedSequence = {};
            m_AccumulationCount = 0u;
        }

        m_Valid[slot] = true;
        m_CommittedSequence[slot] = sequenceIndex;
        m_AccumulationCount = std::min(
            m_AccumulationCount + 1u,
            m_MaximumAccumulation);
        m_LastCommittedSequence = sequenceIndex;
        m_HasCommittedSequence = true;
        m_ClearPending = false;
    }

    nvrhi::ITexture* TemporalHistoryState::Color(uint32_t slot) const
    {
        return slot < m_Color.size()
            ? m_Color[slot].Get()
            : nullptr;
    }

    nvrhi::ITexture* TemporalHistoryState::Depth(uint32_t slot) const
    {
        return slot < m_Depth.size()
            ? m_Depth[slot].Get()
            : nullptr;
    }

    bool TemporalHistoryState::IsSlotValid(uint32_t slot) const
    {
        return slot < m_Valid.size() && m_Valid[slot];
    }

    uint32_t TemporalHistoryState::ValidSlotCount() const
    {
        return uint32_t(m_Valid[0]) + uint32_t(m_Valid[1]);
    }

    uint32_t TemporalHistoryState::AccumulationCount() const
    {
        return m_AccumulationCount;
    }

    uint32_t TemporalHistoryState::ResetCount() const
    {
        return m_ResetCount;
    }

    uint32_t TemporalHistoryState::MaximumAccumulation() const
    {
        return m_MaximumAccumulation;
    }

    uint64_t TemporalHistoryState::LogicalBytes() const
    {
        // Two RGBA16F color slots plus two R32F depth slots.
        return uint64_t(m_Size.x) * uint64_t(m_Size.y) * 24u;
    }

    bool TemporalHistoryState::IsInitialized() const
    {
        return m_Color[0] &&
            m_Color[1] &&
            m_Depth[0] &&
            m_Depth[1];
    }

    bool TemporalHistoryState::HasCommittedSequence() const
    {
        return m_HasCommittedSequence;
    }

    uint64_t TemporalHistoryState::LastCommittedSequence() const
    {
        return m_LastCommittedSequence;
    }
}
