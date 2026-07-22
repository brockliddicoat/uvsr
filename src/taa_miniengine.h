#pragma once

#include "taa_miniengine_reference.h"
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
    struct MiniEngineTemporalAATimings
    {
        float blendMilliseconds = 0.f;
        float outputMilliseconds = 0.f;
        float presentationSharpenMilliseconds = 0.f;
        uint64_t historyTextureBytes = 0u;
        uint64_t debugTextureBytes = 0u;
        uint32_t historyColorSamples = 1u;
        uint32_t historyMomentSamples = 0u;
        uint32_t historyDepthGathers = 1u;
        uint32_t historyDepthSamples = 0u;
        uint32_t accumulationCount = 0u;
        uint32_t historyResetCount = 0u;
        bool historyValid = false;
        bool outputWasSharpened = true;

        [[nodiscard]] float CompleteEffectMilliseconds() const
        {
            return blendMilliseconds +
                outputMilliseconds +
                presentationSharpenMilliseconds;
        }
    };

    class MiniEngineTemporalAAPass
    {
    public:
        MiniEngineTemporalAAPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses,
            nvrhi::ITexture* sceneColor,
            nvrhi::ITexture* currentDepth,
            nvrhi::ITexture* motionVectors,
            bool enableMomentHistory);

        void ResetHistory();

        [[nodiscard]] nvrhi::ITexture* Render(
            nvrhi::ICommandList* commandList,
            const donut::engine::IView& currentView,
            const donut::engine::IView* previousView,
            uint64_t frameIndex,
            const ResolvedAntiAliasingSettings& settings,
            MiniEngineTaaDebugView debugView,
            bool exportSelectiveMorphology,
            bool enableSharpen,
            bool deferSharpenToPresentation,
            float sharpness);

        [[nodiscard]] nvrhi::ITexture* SharpenPresentation(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* sourceTexture);

        [[nodiscard]] const MiniEngineTemporalAATimings& GetTimings() const
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

        [[nodiscard]] bool IsMomentHistoryRequested() const
        {
            return m_MomentHistoryEnabled;
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
        bool m_MomentHistoryEnabled = false;

        nvrhi::BufferHandle m_BlendConstantBuffer;
        nvrhi::BufferHandle m_OutputConstantBuffer;
        nvrhi::SamplerHandle m_LinearClampSampler;

        static constexpr uint32_t c_BlendBaselinePermutationCount =
            MiniEngineTaaBlendPermutationCount *
            2u *
            MiniEngineTaaSampleResurrectionCount *
            2u;
#if UVSR_AA_DEVELOPER_OVERRIDES
        static constexpr uint32_t c_RuntimeAlgorithmPermutationCount =
            MiniEngineTaaBlendPermutationCount;
#else
        // Production packages contain only the four shipping temporal
        // bundles. Developer packages retain the complete override matrix.
        static constexpr uint32_t c_RuntimeAlgorithmPermutationCount = 4u;
#endif
        static constexpr uint32_t c_PerformancePermutationCount =
            c_RuntimeAlgorithmPermutationCount *
            MiniEngineTaaStaticPerformanceCount *
            2u;
        static constexpr uint32_t c_PixelPermutationCount =
            c_RuntimeAlgorithmPermutationCount * 2u * 2u * 2u;

        std::array<nvrhi::ShaderHandle,
            c_BlendBaselinePermutationCount> m_BlendShaders;
        std::array<nvrhi::ShaderHandle,
            c_PerformancePermutationCount> m_PerformanceBlendShaders;
        std::array<nvrhi::ShaderHandle,
            MiniEngineTaaResolveDebugViewCount> m_ResolveShaders;
        nvrhi::ShaderHandle m_SharpenShader;
        nvrhi::ShaderHandle m_PresentationSharpenShader;
        nvrhi::BindingLayoutHandle m_BlendBindingLayout;
        nvrhi::BindingLayoutHandle m_OutputBindingLayout;
        std::array<nvrhi::ComputePipelineHandle,
            c_BlendBaselinePermutationCount> m_BlendPipelines;
        std::array<nvrhi::ComputePipelineHandle,
            c_PerformancePermutationCount>
                m_PerformanceBlendPipelines;
        std::array<nvrhi::ComputePipelineHandle,
            MiniEngineTaaResolveDebugViewCount> m_ResolvePipelines;
        nvrhi::ComputePipelineHandle m_SharpenPipeline;
        nvrhi::ComputePipelineHandle m_PresentationSharpenPipeline;
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
        std::array<nvrhi::TextureHandle, 2> m_MomentHistory;
#if UVSR_AA_DEVELOPER_OVERRIDES
        std::array<nvrhi::TextureHandle, 2> m_PersistentColor;
        std::array<nvrhi::TextureHandle, 2> m_PersistentDepth;
#endif
        nvrhi::TextureHandle m_DebugValues;
        nvrhi::TextureHandle m_FusedOutput;
        nvrhi::TextureHandle m_SelectiveCurrent;
        nvrhi::TextureHandle m_SelectiveRejection;
        std::array<nvrhi::BindingSetHandle, 2> m_BlendBindingSets;
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
        std::array<bool, 2> m_PersistentValid{};
        std::array<std::shared_ptr<donut::engine::PlanarView>, 2>
            m_PersistentViews{};
        MiniEngineTemporalAATimings m_Timings;
        bool m_ReportedMissingComputePermutation = false;
#if UVSR_AA_DEVELOPER_OVERRIDES
        bool m_ReportedMissingPixelPermutation = false;
#endif

        bool CreateBlendComputePermutation(
            const MiniEngineTaaOptions& options,
            uint32_t exportSelective,
            uint32_t sampleResurrection,
            const MiniEngineTaaStaticPerformanceOptions& performance,
            nvrhi::ShaderHandle& shader,
            nvrhi::ComputePipelineHandle& pipeline);
#if UVSR_AA_DEVELOPER_OVERRIDES
        bool CreateBlendPixelPermutation(
            const MiniEngineTaaOptions& options,
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
