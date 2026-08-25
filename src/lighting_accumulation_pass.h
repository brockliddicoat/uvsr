#pragma once

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace uvsr
{
    class RendererShaderFactory;

    enum class LightingSampleSequenceMode : uint32_t
    {
        FramePhase = 0u,
        SuccessfulSampleCount = 1u,
        AnimatedFrameOnHistoryReset = 2u
    };

    struct LightingSampleSchedule
    {
        nvrhi::ITexture* attemptMask = nullptr;
        bool enabled = false;
        bool historyReset = false;

        // Renderer-private transaction identity. Producers must treat this as
        // opaque and consume only attemptMask/enabled. When enabled, each
        // nonzero mask value encodes successfulSampleCount + 1 so producers
        // can use the accepted sample's per-pixel sequence index.
        uint64_t token = 0u;

        [[nodiscard]] explicit operator bool() const
        {
            return attemptMask != nullptr;
        }
    };

    [[nodiscard]] inline constexpr LightingSampleSequenceMode
        ResolveLightingSampleSequenceMode(
            const LightingSampleSchedule& schedule,
            bool stochastic,
            bool animateSamples)
    {
        if (!schedule.enabled)
            return LightingSampleSequenceMode::FramePhase;
        return stochastic && schedule.historyReset && animateSamples
            ? LightingSampleSequenceMode::AnimatedFrameOnHistoryReset
            : LightingSampleSequenceMode::SuccessfulSampleCount;
    }

    struct LightingAccumulationResult
    {
        nvrhi::ITexture* sceneLinear = nullptr;
        bool committed = false;

        [[nodiscard]] explicit operator bool() const
        {
            return sceneLinear != nullptr && committed;
        }
    };

    // Scene-linear per-pixel accumulation used by the Ray Marching lighting
    // solution. The path tracer owns an equivalent producer-side history so it
    // can skip traversal before doing expensive work.
    class LightingAccumulationPass final
    {
    public:
        LightingAccumulationPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>&
                shaderFactory);

        [[nodiscard]] bool IsValid() const;

        [[nodiscard]] LightingSampleSchedule GetDisabledSchedule() const;

        [[nodiscard]] LightingSampleSchedule PrepareAttempts(
            nvrhi::ICommandList* commandList,
            uint32_t width,
            uint32_t height,
            uint64_t historyEpoch);

        [[nodiscard]] LightingAccumulationResult Resolve(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* source,
            const LightingSampleSchedule& schedule);

        void CancelPreparedSchedule(
            const LightingSampleSchedule& schedule);

        void ResetHistory();
        void ResetBindingCache();

    private:
        [[nodiscard]] bool EnsureResources(
            uint32_t width,
            uint32_t height);
        [[nodiscard]] bool EnsurePrepareBindingSet(uint32_t writeIndex);
        [[nodiscard]] bool EnsureResolveBindingSet(
            nvrhi::ITexture* source,
            nvrhi::ITexture* attemptMask,
            uint32_t writeIndex);

        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_PrepareBindingLayout;
        nvrhi::BindingLayoutHandle m_ResolveBindingLayout;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::ShaderHandle m_PrepareShader;
        nvrhi::ShaderHandle m_ResolveShader;
        nvrhi::ComputePipelineHandle m_PreparePipeline;
        nvrhi::ComputePipelineHandle m_ResolvePipeline;
        std::array<nvrhi::TextureHandle, 2> m_Mean;
        std::array<nvrhi::TextureHandle, 2> m_Count;
        nvrhi::TextureHandle m_AttemptMask;
        nvrhi::TextureHandle m_DisabledAttemptMask;
        std::array<nvrhi::BindingSetHandle, 2> m_PrepareBindingSets;
        std::array<nvrhi::BindingSetHandle, 2> m_ResolveBindingSets;
        nvrhi::ITexture* m_BoundSource = nullptr;
        nvrhi::ITexture* m_BoundAttemptMask = nullptr;
        uint32_t m_Width = 0u;
        uint32_t m_Height = 0u;
        uint32_t m_WriteIndex = 0u;
        uint64_t m_HistoryEpoch = 0u;
        uint64_t m_NextScheduleToken = 1u;
        uint64_t m_PreparedToken = 0u;
        uint64_t m_PreparedHistoryEpoch = 0u;
        uint32_t m_PreparedWriteIndex = 0u;
        bool m_PreparedResetHistory = true;
        bool m_HasHistoryEpoch = false;
    };
}
