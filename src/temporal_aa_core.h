#pragma once

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <string>

namespace uvsr
{
    // Motion.z is produced before the hardware depth target quantizes the
    // current and previous endpoints. The comparison therefore needs the
    // combined endpoint-pair error of the actual selected source format in
    // addition to motion.z's independent RGBA16F error. Float32 depth stores
    // the pixel shader's float value exactly; UNORM formats contribute one
    // complete quantization step across their two independently rounded
    // endpoints.
    [[nodiscard]] float
        GetTemporalAaSourceDepthPairQuantizationError(
            nvrhi::Format format);

    // Long-term TAA uses a two-slot physical base: RGBA16F color and R32F raw
    // reverse-Z depth.
    struct TemporalHistoryDesc
    {
        donut::math::uint2 size = donut::math::uint2::zero();
        std::string debugName;
        bool colorRenderTarget = false;
        bool depthRenderTarget = false;
        bool colorUnorderedAccess = false;
        bool depthUnorderedAccess = false;
        uint32_t maximumAccumulation = 2u;
    };

    class TemporalHistoryState
    {
    public:
        void Initialize(
            nvrhi::IDevice* device,
            const TemporalHistoryDesc& desc);

        // Invalidation is idempotent. Repeated notifications while the state
        // is already invalid do not manufacture reset counts or repeated
        // clears. This is important because one effective settings change can
        // also invalidate the previous view in the renderer.
        [[nodiscard]] bool Invalidate();

        // Clears the two base color/depth pairs exactly once after an
        // invalidation. Returns true so a technique can clear its optional
        // attachments (moments or developer-only snapshots) in the same
        // transaction.
        [[nodiscard]] bool PrepareForFirstUse(
            nvrhi::ICommandList* commandList);

        [[nodiscard]] bool CanRead(
            uint32_t slot,
            bool previousViewAvailable,
            uint64_t sequenceIndex) const;

        // Commits a slot for one renderer sequence index. If a frame was
        // skipped, repeated, or rebased, older slot validity is discarded
        // before the new sample is published. This prevents a skipped frame
        // from authorizing stale data based only on ping-pong parity.
        void Commit(uint32_t slot, uint64_t sequenceIndex);

        [[nodiscard]] nvrhi::ITexture* Color(uint32_t slot) const;
        [[nodiscard]] nvrhi::ITexture* Depth(uint32_t slot) const;

        [[nodiscard]] bool IsSlotValid(uint32_t slot) const;
        [[nodiscard]] uint32_t ValidSlotCount() const;
        [[nodiscard]] uint32_t AccumulationCount() const;
        [[nodiscard]] uint32_t ResetCount() const;
        [[nodiscard]] uint32_t MaximumAccumulation() const;
        [[nodiscard]] uint64_t LogicalBytes() const;
        [[nodiscard]] bool IsInitialized() const;
        [[nodiscard]] bool HasCommittedSequence() const;
        [[nodiscard]] uint64_t LastCommittedSequence() const;

    private:
        std::array<nvrhi::TextureHandle, 2> m_Color;
        std::array<nvrhi::TextureHandle, 2> m_Depth;
        std::array<bool, 2> m_Valid{};
        std::array<uint64_t, 2> m_CommittedSequence{};
        donut::math::uint2 m_Size = donut::math::uint2::zero();
        uint32_t m_MaximumAccumulation = 2u;
        uint32_t m_AccumulationCount = 0u;
        uint32_t m_ResetCount = 0u;
        uint64_t m_LastCommittedSequence = 0u;
        bool m_HasCommittedSequence = false;
        bool m_ClearPending = true;
    };
}
