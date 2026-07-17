#pragma once

#include "taa_miniengine_reference.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class IView;
    class ShaderFactory;
}

namespace uvsr
{
    struct MiniEngineTemporalAATimings
    {
        float blendMilliseconds = 0.f;
        float outputMilliseconds = 0.f;
        uint64_t historyTextureBytes = 0u;
        bool outputWasSharpened = true;

        [[nodiscard]] float CompleteEffectMilliseconds() const
        {
            return blendMilliseconds + outputMilliseconds;
        }
    };

    class MiniEngineTemporalAAPass
    {
    public:
        MiniEngineTemporalAAPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory,
            nvrhi::ITexture* sceneColor,
            nvrhi::ITexture* currentDepth,
            nvrhi::ITexture* motionVectors);

        void ResetHistory();

        void Render(
            nvrhi::ICommandList* commandList,
            const donut::engine::IView& currentView,
            const donut::engine::IView* previousView,
            uint64_t frameIndex,
            bool enableSharpen,
            float sharpness);

        [[nodiscard]] const MiniEngineTemporalAATimings& GetTimings() const
        {
            return m_Timings;
        }

    private:
        enum class Stage : uint32_t
        {
            Blend,
            Output,
            Count
        };

        static constexpr uint32_t c_TimerLatency = 4u;

        nvrhi::IDevice* m_Device = nullptr;
        donut::math::uint2 m_Size = donut::math::uint2::zero();

        nvrhi::BufferHandle m_BlendConstantBuffer;
        nvrhi::BufferHandle m_OutputConstantBuffer;
        nvrhi::SamplerHandle m_LinearClampSampler;

        nvrhi::ShaderHandle m_BlendShader;
        nvrhi::ShaderHandle m_ResolveShader;
        nvrhi::ShaderHandle m_SharpenShader;
        nvrhi::BindingLayoutHandle m_BlendBindingLayout;
        nvrhi::BindingLayoutHandle m_OutputBindingLayout;
        nvrhi::ComputePipelineHandle m_BlendPipeline;
        nvrhi::ComputePipelineHandle m_ResolvePipeline;
        nvrhi::ComputePipelineHandle m_SharpenPipeline;

        std::array<nvrhi::TextureHandle, 2> m_TemporalColor;
        std::array<nvrhi::TextureHandle, 2> m_DepthHistory;
        std::array<nvrhi::BindingSetHandle, 2> m_BlendBindingSets;
        std::array<nvrhi::BindingSetHandle, 2> m_OutputBindingSets;

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerPending{};
        std::array<bool, static_cast<size_t>(Stage::Count)> m_TimerActive{};
        uint32_t m_TimerFrame = 0u;

        bool m_HistoryValid = false;
        MiniEngineTemporalAATimings m_Timings;

        void AdvanceTimers();
        void BeginStage(nvrhi::ICommandList* commandList, Stage stage);
        void EndStage(nvrhi::ICommandList* commandList, Stage stage);
    };
}
