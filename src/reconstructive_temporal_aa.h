#pragma once

#include "reconstructive_temporal_aa_settings.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace donut::engine
{
    class IView;
    class PlanarView;
    class ShaderFactory;
}

namespace uvsr
{
    struct ReconstructiveTemporalAATimings
    {
        float prepareMs = 0.f;
        float resolveMs = 0.f;
        // Sharpening is executed inside the downstream tone mapper. This is
        // the separate AgX-plus-sharpen display interval, not part of totalMs.
        float fusedSharpeningMs = 0.f;
        // A coherent Prepare+Resolve pair from one tagged frame. The stages
        // are summed instead of nesting another timestamp interval around
        // their query resolves, which would perturb the measured interval.
        float totalMs = 0.f;
        float totalMedianMs = 0.f;
        float totalP95Ms = 0.f;
        uint32_t coherentSampleCount = 0;

        // Resurrection is a divergent conditional read inside Resolve. It is
        // deliberately not measured with a misleading full-dispatch query.
        float resurrectionOverheadMs = 0.f;
        bool resurrectionOverheadIsFused = true;
    };

    struct ReconstructiveTemporalAAMemoryStats
    {
        dm::uint2 resolution = dm::uint2::zero();
        uint32_t historySlotCount = 0;
        uint32_t persistentHistoryCount = 0;
        uint64_t transientBytes = 0;
        uint64_t historyBytes = 0;
        uint64_t debugBytes = 0;
        uint64_t totalBytes = 0;

        // Baseline full-screen logical traffic estimates before cache reuse.
        // Scene-dependent background dilation, rejected-history color skips,
        // and conditional persistent-history reads are not folded into them.
        float approximateReadBytesPerPixel = 0.f;
        float approximateWriteBytesPerPixel = 0.f;
    };

    class ReconstructiveTemporalAAPass
    {
    public:
        struct Inputs
        {
            nvrhi::ITexture* sceneColor = nullptr;
            nvrhi::ITexture* depth = nullptr;
            nvrhi::ITexture* gbufferDiffuse = nullptr;
            nvrhi::ITexture* gbufferSpecular = nullptr;
            nvrhi::ITexture* surfaceIds = nullptr;
            nvrhi::ITexture* motionVectors = nullptr;
            nvrhi::ITexture* explicitReactiveMask = nullptr;
        };

        ReconstructiveTemporalAAPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory);

        // settings is mutable so one-shot reset requests can be consumed and
        // sanitized values can be reflected by UI/serialization immediately.
        void Render(
            nvrhi::ICommandList* commandList,
            ReconstructiveTemporalAASettings& settings,
            const donut::engine::IView& currentView,
            const donut::engine::IView* previousView,
            const Inputs& inputs,
            float deltaSeconds,
            uint64_t frameIndex);

        void ResetHistory();
        void Deactivate();

        // Bracket the downstream display pass that contains fused RTAA
        // sharpening. This AgX interval is reported separately and never
        // contaminates the coherent Prepare+Resolve RTAA total.
        void BeginFusedSharpeningTimer(nvrhi::ICommandList* commandList);
        void EndFusedSharpeningTimer(nvrhi::ICommandList* commandList);

        [[nodiscard]] nvrhi::ITexture* GetOutput() const { return m_Output; }
        [[nodiscard]] nvrhi::ITexture* GetHistoryMetadata() const;
        [[nodiscard]] nvrhi::ITexture* GetHistoryMoments() const;
        [[nodiscard]] bool IsHistoryValid() const { return m_HistoryValid; }
        [[nodiscard]] bool IsNativeFp16Supported() const
        {
            return m_NativeFp16Supported;
        }
        [[nodiscard]] const ReconstructiveTemporalAATimings& GetTimings() const { return m_Timings; }
        [[nodiscard]] const ReconstructiveTemporalWeights& GetWeights() const { return m_Weights; }
        [[nodiscard]] const ReconstructiveTemporalAAMemoryStats& GetMemoryStats() const { return m_MemoryStats; }

    private:
        enum class Stage : uint32_t
        {
            Prepare,
            Resolve,
            FusedSharpening,
            Count
        };

        struct TimerSample
        {
            uint64_t epoch = 0;
            dm::uint2 resolution = dm::uint2::zero();
            uint32_t performanceProfile = 0;
            std::array<float, static_cast<size_t>(Stage::Count)> milliseconds{};
            std::array<bool, static_cast<size_t>(Stage::Count)> ready{};
            bool valid = false;
            bool coherentPairPublished = false;
            bool fusedPublished = false;
        };

        struct Pipeline
        {
            nvrhi::ShaderHandle shader;
            nvrhi::BindingLayoutHandle bindingLayout;
            nvrhi::ComputePipelineHandle pipeline;
        };

        struct HistorySlot
        {
            nvrhi::TextureHandle color;
            nvrhi::TextureHandle moments;
            nvrhi::TextureHandle metadata;
            nvrhi::TextureHandle depth;
            nvrhi::TextureHandle normalMaterial;
            std::shared_ptr<donut::engine::PlanarView> view;
            uint64_t frameIndex = 0;
            bool valid = false;
        };

        static constexpr uint32_t c_TimerLatency = 4;
        static constexpr uint32_t c_TimerStatisticsWindow = 120;

        nvrhi::DeviceHandle m_Device;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::SamplerHandle m_LinearClampSampler;
        Pipeline m_PreparePipeline;
        // First index: filter bit | resurrection bit | (performance tier << 2).
        // Second index: statically compiled production/debug-output path.
        // Third index: FP32/native-FP16 arithmetic path.
        // Keeping the diagnostic switch out of production shaders shortens
        // live ranges and removes the debug UAV from their root signatures.
        std::array<std::array<std::array<Pipeline, 2>, 2>, 12>
            m_ResolvePipelines;
        bool m_NativeFp16Supported = false;

        dm::uint2 m_Resolution = dm::uint2::zero();
        uint32_t m_PersistentCount = 0;
        uint32_t m_PersistentInterval = 1;
        nvrhi::TextureHandle m_PreparedData;
        nvrhi::TextureHandle m_Classification;
        nvrhi::TextureHandle m_DebugOutput;
        std::vector<HistorySlot> m_History;

        uint32_t m_NextWriteIndex = 0;
        uint32_t m_LatestOutputIndex = 0;
        bool m_HistoryValid = false;
        bool m_WasEnabled = false;
        bool m_HasPreviousCamera = false;
        bool m_ReverseDepth = false;
        uint64_t m_HistorySignature = 0;
        dm::float3 m_PreviousCameraOrigin = dm::float3::zero();
        dm::float3 m_PreviousCameraDirection = dm::float3(0.f, 0.f, 1.f);
        dm::float4x4 m_PreviousProjection = dm::float4x4::identity();
        dm::float2 m_PreviousJitter = dm::float2::zero();
        nvrhi::ITexture* m_Output = nullptr;

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerPending{};
        std::array<bool, static_cast<size_t>(Stage::Count)> m_TimerActive{};
        std::array<uint32_t, static_cast<size_t>(Stage::Count)> m_TimerActiveSlot{};
        std::array<TimerSample, c_TimerLatency> m_TimerSamples{};
        std::array<float, c_TimerStatisticsWindow> m_TotalTimingHistory{};
        uint32_t m_TimerFrame = 0;
        uint32_t m_TotalTimingHistoryCount = 0;
        uint32_t m_TotalTimingHistoryWriteIndex = 0;
        uint64_t m_TimerEpoch = 1;
        uint64_t m_TimerConfigurationSignature = 0;
        dm::uint2 m_TimerResolution = dm::uint2::zero();
        uint32_t m_TimerPerformanceProfile = 0;
        bool m_TimerCaptureActive = false;
        bool m_AwaitingFusedSharpening = false;

        ReconstructiveTemporalAATimings m_Timings;
        ReconstructiveTemporalWeights m_Weights;
        ReconstructiveTemporalAAMemoryStats m_MemoryStats;

        void CreatePipelines(const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory);
        bool EnsureResources(dm::uint2 resolution, const ReconstructiveTemporalAASettings& settings);
        void ReleaseResources();
        bool UpdateHistoryValidity(
            const ReconstructiveTemporalAASettings& settings,
            const donut::engine::IView& currentView,
            const donut::engine::IView* previousView,
            bool resourcesChanged,
            bool forceReset);
        static uint64_t ComputeHistorySignature(const ReconstructiveTemporalAASettings& settings);
        static bool DebugNeedsFullResolution(ReconstructiveTemporalDebugMode mode);

        [[nodiscard]] uint32_t SlotAtAge(uint32_t age) const;
        [[nodiscard]] HistorySlot* GetValidSlotAtAge(uint32_t age);
        [[nodiscard]] const HistorySlot* GetLatestSlot() const;

        void BeginStage(nvrhi::ICommandList* commandList, Stage stage);
        void EndStage(nvrhi::ICommandList* commandList, Stage stage);
        void BeginTimerSample(
            const ReconstructiveTemporalAASettings& settings,
            dm::uint2 resolution,
            bool historyValid,
            bool resetEpoch);
        void PublishTimerSample(TimerSample& sample);
        void ResetTimerStatistics();
        void AdvanceTimers();
    };
}
