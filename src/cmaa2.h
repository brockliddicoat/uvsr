#pragma once

#include "taa_miniengine_options.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class CommonRenderPasses;
    class ShaderFactory;
}

namespace uvsr
{
    struct Cmaa2Timings
    {
        float edgeMilliseconds = 0.f;
        float candidateMilliseconds = 0.f;
        float applyMilliseconds = 0.f;

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
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses,
            nvrhi::ITexture* sceneColor);

        [[nodiscard]] nvrhi::ITexture* Render(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* sourceColor,
            AntiAliasingQuality quality);
        void UpdateSourceColor(nvrhi::ITexture* sourceColor);
        void MarkInactiveFrame();

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

        static constexpr uint32_t c_QualityCount = 4u;
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
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::ITexture* m_BoundSource = nullptr;

        std::array<nvrhi::ComputePipelineHandle, c_QualityCount>
            m_EdgePipelines;
        std::array<nvrhi::ComputePipelineHandle, c_QualityCount>
            m_CandidatePipelines;
        std::array<nvrhi::ComputePipelineHandle, c_QualityCount>
            m_ApplyPipelines;
        std::array<nvrhi::ComputePipelineHandle, c_QualityCount>
            m_DispatchArgumentPipelines;

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
