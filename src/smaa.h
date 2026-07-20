#pragma once

#include "taa_miniengine_options.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>

namespace donut::engine
{
    class CommonRenderPasses;
    class ShaderFactory;
}

namespace uvsr
{
    // The renderer sets this bit only on benchmark measurement frames. It is
    // separate from the renderer-owned benchmark tag so the pass can drain
    // delayed queries without knowing how source-frame indices are encoded.
    inline constexpr uint64_t SmaaTimingCollectTagFlag = 1ull << 62u;

    struct SmaaTimings
    {
        float edgeMilliseconds = 0.f;
        float weightMilliseconds = 0.f;
        float neighborhoodMilliseconds = 0.f;
        bool executionValid = true;
        uint64_t completedSerial = 0u;
        uint64_t completedTag = 0u;

        [[nodiscard]] float CompleteEffectMilliseconds() const
        {
            return edgeMilliseconds +
                weightMilliseconds +
                neighborhoodMilliseconds;
        }
    };

    struct SmaaTimingAccounting
    {
        uint64_t issuedSampleCount = 0u;
        uint64_t droppedSampleCount = 0u;
        uint64_t retiredSampleCount = 0u;

        [[nodiscard]] uint64_t OutstandingSampleCount() const
        {
            return issuedSampleCount > retiredSampleCount
                ? issuedSampleCount - retiredSampleCount
                : 0u;
        }
    };

    // Fixed spatial SMAA used as an image-quality diagnostic and as an
    // optional presentation-only morphology pass after long-term TAA. SMAA
    // owns no history and exposes no execution experiments.
    class SmaaPass
    {
    public:
        SmaaPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>&
                shaderFactory,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses,
            nvrhi::ITexture* sceneColor);

        [[nodiscard]] nvrhi::ITexture* Render1x(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* sourceColor,
            AntiAliasingQuality quality,
            uint64_t timingTag = 0u);

        [[nodiscard]] nvrhi::ITexture* RenderFullScreenPresentation(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* sourceColor,
            AntiAliasingQuality quality,
            uint64_t timingTag = 0u);

        [[nodiscard]] nvrhi::ITexture* RenderSelective(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* deJitteredCurrent,
            nvrhi::ITexture* resolvedTemporal,
            nvrhi::ITexture* rejectionMask,
            AntiAliasingQuality quality,
            uint64_t timingTag = 0u);

#if UVSR_AA_DEVELOPER_OVERRIDES
        [[nodiscard]] nvrhi::ITexture* RenderDebugView(
            nvrhi::ICommandList* commandList,
            MiniEngineTaaDebugView debugView,
            nvrhi::ITexture* sourceColor,
            nvrhi::ITexture* filteredColor);
#endif

        [[nodiscard]] const SmaaTimings& GetTimings() const
        {
            return m_Timings;
        }

        [[nodiscard]] bool PopCompletedTiming(SmaaTimings& timing);
        void MarkInactiveFrame();
        void ResetTimingAccounting();

        [[nodiscard]] SmaaTimingAccounting
            GetTimingAccounting() const
        {
            return m_TimingAccounting;
        }

    private:
        enum class Stage : uint32_t
        {
            Edge,
            Weight,
            Neighborhood,
            Count
        };

        static constexpr uint32_t c_TimerLatency = 8u;
        static constexpr size_t c_CompletedTimingQueueCapacity = 256u;
        static constexpr uint32_t c_QualityCount =
            static_cast<uint32_t>(AntiAliasingQuality::Count);

        nvrhi::IDevice* m_Device = nullptr;
        donut::math::uint2 m_Size = donut::math::uint2::zero();
        bool m_LastSpatialExecutionValid = true;
        bool m_MissingSpatialPermutationReported = false;
        bool m_MissingSelectivePermutationReported = false;

        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::SamplerHandle m_LinearClampSampler;
        nvrhi::SamplerHandle m_PointClampSampler;

        nvrhi::TextureHandle m_Edges;
        nvrhi::TextureHandle m_BlendWeights;
        nvrhi::TextureHandle m_OutputColor;
#if UVSR_AA_DEVELOPER_OVERRIDES
        nvrhi::TextureHandle m_DebugOutput;
#endif
        nvrhi::TextureHandle m_AreaTexture;
        nvrhi::TextureHandle m_SearchTexture;

        nvrhi::FramebufferHandle m_EdgeFramebuffer;
        nvrhi::FramebufferHandle m_BlendFramebuffer;
        nvrhi::FramebufferHandle m_OutputFramebuffer;
#if UVSR_AA_DEVELOPER_OVERRIDES
        nvrhi::FramebufferHandle m_DebugFramebuffer;
#endif

        nvrhi::BindingLayoutHandle m_EdgeBindingLayout;
        nvrhi::BindingLayoutHandle m_WeightBindingLayout;
        nvrhi::BindingLayoutHandle m_NeighborhoodBindingLayout;
        nvrhi::BindingLayoutHandle m_SelectiveNeighborhoodBindingLayout;
#if UVSR_AA_DEVELOPER_OVERRIDES
        nvrhi::BindingLayoutHandle m_DebugBindingLayout;
#endif

        std::array<nvrhi::GraphicsPipelineHandle, c_QualityCount>
            m_EdgePipelines;
        std::array<nvrhi::GraphicsPipelineHandle, c_QualityCount>
            m_WeightPipelines;
        nvrhi::GraphicsPipelineHandle m_NeighborhoodPipeline;
        nvrhi::ComputePipelineHandle m_SelectiveNeighborhoodPipeline;
#if UVSR_AA_DEVELOPER_OVERRIDES
        static constexpr uint32_t c_DebugViewCount = 3u;
        std::array<nvrhi::GraphicsPipelineHandle, c_DebugViewCount>
            m_DebugPipelines;
#endif

        nvrhi::BindingSetHandle m_WeightBindingSet;
        nvrhi::ITexture* m_BoundEdgeSource = nullptr;
        nvrhi::BindingSetHandle m_EdgeBindingSet;
        nvrhi::ITexture* m_BoundNeighborhoodSource = nullptr;
        nvrhi::BindingSetHandle m_NeighborhoodBindingSet;
        nvrhi::ITexture* m_BoundSelectiveCurrent = nullptr;
        nvrhi::ITexture* m_BoundSelectiveTemporal = nullptr;
        nvrhi::ITexture* m_BoundSelectiveRejection = nullptr;
        nvrhi::BindingSetHandle m_SelectiveNeighborhoodBindingSet;
#if UVSR_AA_DEVELOPER_OVERRIDES
        nvrhi::ITexture* m_BoundDebugSource = nullptr;
        nvrhi::ITexture* m_BoundDebugFiltered = nullptr;
        nvrhi::BindingSetHandle m_DebugBindingSet;
#endif

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerPending{};
        std::array<std::array<float, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerValues{};
        std::array<bool, static_cast<size_t>(Stage::Count)> m_TimerActive{};
        std::array<uint32_t, c_TimerLatency> m_TimerExpectedMasks{};
        std::array<uint32_t, c_TimerLatency> m_TimerReadyMasks{};
        std::array<bool, c_TimerLatency> m_TimerExecutionValid{};
        std::array<uint64_t, c_TimerLatency> m_TimerSerials{};
        std::array<uint64_t, c_TimerLatency> m_TimerTags{};
        int32_t m_CurrentTimerSlot = -1;
        uint64_t m_NextTimerSerial = 0u;
        uint64_t m_NextCompletedTimerSerial = 1u;
        uint32_t m_TimerFrame = 0u;
        std::map<uint64_t, SmaaTimings> m_ReadyTimingSamples;
        std::deque<SmaaTimings> m_CompletedTimingSamples;
        SmaaTimingAccounting m_TimingAccounting;

        bool m_LookupsUploaded = false;
        SmaaTimings m_Timings;

        void UploadLookups(nvrhi::ICommandList* commandList);
        void AdvanceTimers();
        void PrepareTimingFrame(
            uint32_t expectedStageMask,
            uint64_t timingTag);
        void FinishTimingFrame(bool executionValid);
        void BeginStage(nvrhi::ICommandList* commandList, Stage stage);
        void EndStage(nvrhi::ICommandList* commandList, Stage stage);
        void DrawFullscreen(
            nvrhi::ICommandList* commandList,
            nvrhi::IGraphicsPipeline* pipeline,
            nvrhi::IFramebuffer* framebuffer,
            nvrhi::IBindingSet* bindingSet);
        [[nodiscard]] nvrhi::ITexture* RunSpatial(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* sourceColor,
            nvrhi::ITexture* destination,
            nvrhi::IFramebuffer* destinationFramebuffer,
            AntiAliasingQuality quality,
            bool skipNeighborhood = false);
    };
}
