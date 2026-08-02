#pragma once

#include "temporal_aa_reference.h"
#include "temporal_aa_core.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class CommonRenderPasses;
    class IView;
    class PlanarView;
    class ShaderFactory;
}

namespace uvsr
{
    struct TemporalAATimings
    {
        float blendMilliseconds = 0.f;
        float outputMilliseconds = 0.f;
        float presentationSharpenMilliseconds = 0.f;
        uint64_t activeHistoryTextureBytes = 0u;
        uint64_t residentHistoryTextureBytes = 0u;
        uint64_t robustHistoryTextureBytes = 0u;
        uint64_t persistentHistoryTextureBytes = 0u;
        uint64_t minimumHistoryTextureBytes = 0u;
        uint64_t debugTextureBytes = 0u;
        uint32_t historyColorSamples = 1u;
        uint32_t historyDepthGathers = 1u;
        uint32_t historyDepthSamples = 0u;
        uint32_t dispatchCount = 0u;
        uint32_t accumulationCount = 0u;
        uint32_t historyResetCount = 0u;
        TemporalAaCostMode effectiveCostMode =
            TemporalAaCostMode::FullQuality;
        bool historyValid = false;
        bool outputWasSharpened = true;
        bool minimumPathSupported = false;
        bool minimumColorIsR11G11B10 = false;
        bool minimumDepthIsR16 = false;

        [[nodiscard]] float CompleteEffectMilliseconds() const
        {
            return blendMilliseconds +
                outputMilliseconds +
                presentationSharpenMilliseconds;
        }
    };

    class TemporalAAPass
    {
    public:
        TemporalAAPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses,
            nvrhi::ITexture* sceneColor,
            nvrhi::ITexture* currentDepth,
            nvrhi::ITexture* motionVectors,
            bool deferPipelineCreation = false);

        // Pipeline creation can be spread over loading frames. Construction
        // remains eager by default for standalone component users.
        [[nodiscard]] bool PreparePipelinesStep();
        [[nodiscard]] bool ArePipelinesReady() const
        {
            return m_PipelinesReady;
        }

        void ResetHistory();

        [[nodiscard]] nvrhi::ITexture* Render(
            nvrhi::ICommandList* commandList,
            const donut::engine::IView& currentView,
            const donut::engine::IView* previousView,
            uint64_t frameIndex,
            const ResolvedAntiAliasingSettings& settings,
            TemporalAaDebugView debugView,
            bool exportSelectiveMorphology,
            bool enableSharpen,
            bool deferSharpenToPresentation,
            float sharpness);

        [[nodiscard]] nvrhi::ITexture* SharpenPresentation(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* sourceTexture);

        [[nodiscard]] const TemporalAATimings& GetTimings() const
        {
            return m_Timings;
        }

        [[nodiscard]] nvrhi::ITexture* GetSelectiveCurrentTexture() const
        {
            return m_SelectiveCurrent;
        }

        [[nodiscard]] nvrhi::ITexture* GetSelectiveRejectionTexture() const
        {
            return m_SelectiveRejection;
        }

        [[nodiscard]] bool WasHistoryInputValidLastRender() const
        {
            return m_LastHistoryInputValid;
        }

    private:
        enum class Stage : uint32_t
        {
            Blend,
            Output,
            PresentationSharpen,
            Count
        };

        static constexpr uint32_t c_TimerLatency = 4u;

        nvrhi::IDevice* m_Device = nullptr;
        std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
        nvrhi::ITexture* m_SceneColor = nullptr;
        donut::math::uint2 m_Size = donut::math::uint2::zero();
        float m_SourceDepthPairQuantizationError = 0.f;

        nvrhi::BufferHandle m_BlendConstantBuffer;
        nvrhi::BufferHandle m_OutputConstantBuffer;
        nvrhi::SamplerHandle m_LinearClampSampler;

        static constexpr uint32_t c_BlendBaselinePermutationCount =
            TemporalAaBlendPermutationCount *
            2u *
            TemporalAaSampleResurrectionCount *
            2u;
        static constexpr uint32_t c_RuntimeAlgorithmPermutationCount =
            TemporalAaBlendPermutationCount;
        static constexpr uint32_t c_PerformancePermutationCount =
            c_RuntimeAlgorithmPermutationCount *
            TemporalAaStaticPerformanceCount *
            2u;
        static constexpr uint32_t c_PixelPermutationCount =
            c_RuntimeAlgorithmPermutationCount * 2u * 2u * 2u;

        std::array<nvrhi::ShaderHandle,
            c_BlendBaselinePermutationCount> m_BlendShaders;
        std::array<nvrhi::ShaderHandle,
            c_PerformancePermutationCount> m_PerformanceBlendShaders;
        std::array<nvrhi::ShaderHandle,
            TemporalAaResolveDebugViewCount> m_ResolveShaders;
        nvrhi::ShaderHandle m_SharpenShader;
        nvrhi::ShaderHandle m_PresentationSharpenShader;
        std::array<nvrhi::ShaderHandle, 2> m_MinimumShaders;
        nvrhi::BindingLayoutHandle m_BlendBindingLayout;
        nvrhi::BindingLayoutHandle m_MinimumBindingLayout;
        nvrhi::BindingLayoutHandle m_OutputBindingLayout;
        std::array<nvrhi::ComputePipelineHandle,
            c_BlendBaselinePermutationCount> m_BlendPipelines;
        std::array<nvrhi::ComputePipelineHandle,
            c_PerformancePermutationCount>
                m_PerformanceBlendPipelines;
        std::array<nvrhi::ComputePipelineHandle,
            TemporalAaResolveDebugViewCount> m_ResolvePipelines;
        nvrhi::ComputePipelineHandle m_SharpenPipeline;
        nvrhi::ComputePipelineHandle m_PresentationSharpenPipeline;
        std::array<nvrhi::ComputePipelineHandle, 2> m_MinimumPipelines;
#if UVSR_AA_DEVELOPER_OVERRIDES
        std::array<nvrhi::ShaderHandle,
            c_PixelPermutationCount> m_PixelBlendShaders;
        nvrhi::ShaderHandle m_FullscreenVS;
        nvrhi::BindingLayoutHandle m_PixelBlendBindingLayout;
        std::array<nvrhi::GraphicsPipelineHandle,
            c_PixelPermutationCount> m_PixelBlendPipelines;
        std::array<nvrhi::FramebufferHandle, 2>
            m_PixelBlendFramebuffers;
        std::array<nvrhi::BindingSetHandle, 2>
            m_PixelBlendBindingSets;
#endif

        TemporalHistoryState m_History;
#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
        std::array<nvrhi::TextureHandle, 2> m_PersistentColor;
        std::array<nvrhi::TextureHandle, 2> m_PersistentDepth;
#endif
        nvrhi::TextureHandle m_DebugValues;
        nvrhi::TextureHandle m_FusedOutput;
        nvrhi::TextureHandle m_SelectiveCurrent;
        nvrhi::TextureHandle m_SelectiveRejection;
        std::array<nvrhi::TextureHandle, 2> m_MinimumColor;
        std::array<nvrhi::TextureHandle, 2> m_MinimumDepth;
        std::array<nvrhi::BindingSetHandle, 2> m_BlendBindingSets;
        std::array<nvrhi::BindingSetHandle, 2> m_MinimumBindingSets;
        std::array<nvrhi::BindingSetHandle, 2> m_OutputBindingSets;
        nvrhi::BindingSetHandle m_PresentationSharpenBindingSet;
        nvrhi::ITexture* m_BoundPresentationSharpenSource = nullptr;

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerPending{};
        std::array<bool, static_cast<size_t>(Stage::Count)> m_TimerActive{};
        uint32_t m_TimerFrame = 0u;

        bool m_LastHistoryInputValid = false;
        std::array<bool, 2> m_MinimumValid{};
        std::array<uint64_t, 2> m_MinimumCommittedSequence{};
        uint64_t m_MinimumLastCommittedSequence = 0u;
        uint32_t m_MinimumAccumulationCount = 0u;
        uint32_t m_MinimumResetCount = 0u;
        bool m_MinimumHasCommittedSequence = false;
        bool m_LastRenderUsedMinimum = false;
#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
        std::array<bool, 2> m_PersistentValid{};
        std::array<std::shared_ptr<donut::engine::PlanarView>, 2>
            m_PersistentViews{};
#endif
        TemporalAATimings m_Timings;
        bool m_ReportedMissingComputePermutation = false;
        bool m_ReportedMinimumFallback = false;
        uint32_t m_PipelinePreparationStep = 0u;
        bool m_PipelinesReady = false;
#if UVSR_AA_DEVELOPER_OVERRIDES
        bool m_ReportedMissingPixelPermutation = false;
#endif

        bool CreateBlendComputePermutation(
            const TemporalAaOptions& options,
            uint32_t exportSelective,
            uint32_t sampleResurrection,
            const TemporalAaStaticPerformanceOptions& performance,
            nvrhi::ShaderHandle& shader,
            nvrhi::ComputePipelineHandle& pipeline);
#if UVSR_AA_DEVELOPER_OVERRIDES
        bool CreateBlendPixelPermutation(
            const TemporalAaOptions& options,
            uint32_t exportSelective,
            bool earlyHistoryRejection,
            bool fusedOutput,
            nvrhi::ShaderHandle& shader,
            nvrhi::GraphicsPipelineHandle& pipeline);
#endif
        void AdvanceTimers();
        void BeginStage(nvrhi::ICommandList* commandList, Stage stage);
        void EndStage(nvrhi::ICommandList* commandList, Stage stage);
    };
}
