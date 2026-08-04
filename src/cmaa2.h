#pragma once

#include "temporal_aa_options.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class ShaderFactory;
}

namespace uvsr
{
    struct Cmaa2Timings
    {
        float edgeMilliseconds = 0.f;
        float candidateMilliseconds = 0.f;
        float applyMilliseconds = 0.f;
        bool available = false;

        [[nodiscard]] float CompleteEffectMilliseconds() const
        {
            return edgeMilliseconds +
                candidateMilliseconds +
                applyMilliseconds;
        }
    };

    // Source-faithful Intel CMAA2 2.3 integration. The official three-kernel
    // topology, compact candidate/deferred lists, and two indirect dispatches
    // are retained. UVSR uses a distinct RGBA16F output initialized from the
    // input to avoid aliasing one resource as SRV and UAV simultaneously.
    class Cmaa2Pass
    {
    public:
        Cmaa2Pass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>&
                shaderFactory,
            nvrhi::ITexture* sceneColor);

        [[nodiscard]] nvrhi::ITexture* Render(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* sourceColor,
            const ResolvedAntiAliasingSettings& settings);
        void UpdateSourceColor(nvrhi::ITexture* sourceColor);

        [[nodiscard]] bool IsValid() const;
        [[nodiscard]] const Cmaa2Timings& GetTimings() const
        {
            return m_Timings;
        }

    private:
        enum class Stage : uint32_t
        {
            Edge,
            Candidate,
            Apply,
            Count
        };

        static constexpr uint32_t c_DetectorCount =
            static_cast<uint32_t>(Cmaa2EdgeDetector::Count);
        static constexpr uint32_t c_TimerLatency = 4u;

        nvrhi::IDevice* m_Device = nullptr;
        donut::math::uint2 m_Size = donut::math::uint2::zero();
        nvrhi::TextureHandle m_OutputColor;
        nvrhi::TextureHandle m_WorkingEdges;
        nvrhi::TextureHandle m_DeferredItemHeads;
        nvrhi::BufferHandle m_ShapeCandidates;
        nvrhi::BufferHandle m_DeferredLocations;
        nvrhi::BufferHandle m_DeferredItems;
        nvrhi::BufferHandle m_Control;
        nvrhi::BufferHandle m_IndirectArguments;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::ITexture* m_BoundSource = nullptr;

        using DetectorPipelines = std::array<
            nvrhi::ComputePipelineHandle,
            c_DetectorCount>;
        DetectorPipelines m_EdgePipelines;
        nvrhi::ComputePipelineHandle m_CandidatePipeline;
        nvrhi::ComputePipelineHandle m_ApplyPipeline;
        nvrhi::ComputePipelineHandle m_DispatchArgumentPipeline;

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerPending{};
        std::array<bool, static_cast<size_t>(Stage::Count)>
            m_TimerActive{};
        uint32_t m_TimerFrame = 0u;
        bool m_InitializeControl = true;
        Cmaa2Timings m_Timings;

        void RebuildBindingSet(nvrhi::ITexture* sourceColor);
        void AdvanceTimers();
        void BeginStage(nvrhi::ICommandList* commandList, Stage stage);
        void EndStage(nvrhi::ICommandList* commandList, Stage stage);
        void PublishUavWrites(nvrhi::ICommandList* commandList);
    };
}
