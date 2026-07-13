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
        // therefore the measured AgX-plus-sharpen display interval (an upper
        // bound on sharpening), retained under the shorter field name.
        float fusedSharpeningMs = 0.f;
        float totalMs = 0.f;

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

        // Conservative full-screen traffic estimates. Conditional persistent
        // history reads are excluded because their coverage is scene-dependent.
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
            nvrhi::ITexture* normals = nullptr;
            nvrhi::ITexture* gbufferDiffuse = nullptr;
            nvrhi::ITexture* gbufferSpecular = nullptr;
            nvrhi::ITexture* gbufferEmissive = nullptr;
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
        // sharpening. EndFusedSharpeningTimer also closes the end-to-end RTAA
        // interval; neither query is an isolated sharpening measurement.
        void BeginFusedSharpeningTimer(nvrhi::ICommandList* commandList);
        void EndFusedSharpeningTimer(nvrhi::ICommandList* commandList);

        [[nodiscard]] nvrhi::ITexture* GetOutput() const { return m_Output; }
        [[nodiscard]] nvrhi::ITexture* GetHistoryMetadata() const;
        [[nodiscard]] nvrhi::ITexture* GetHistoryMoments() const;
        [[nodiscard]] bool IsHistoryValid() const { return m_HistoryValid; }
        [[nodiscard]] const ReconstructiveTemporalAATimings& GetTimings() const { return m_Timings; }
        [[nodiscard]] const ReconstructiveTemporalWeights& GetWeights() const { return m_Weights; }
        [[nodiscard]] const ReconstructiveTemporalAAMemoryStats& GetMemoryStats() const { return m_MemoryStats; }

    private:
        enum class Stage : uint32_t
        {
            Prepare,
            Resolve,
            FusedSharpening,
            Total,
            Count
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

        nvrhi::DeviceHandle m_Device;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::SamplerHandle m_LinearClampSampler;
        Pipeline m_PreparePipeline;
        std::array<Pipeline, 4> m_ResolvePipelines;

        dm::uint2 m_Resolution = dm::uint2::zero();
        uint32_t m_PersistentCount = 0;
        uint32_t m_PersistentInterval = 1;
        nvrhi::TextureHandle m_PreparedData;
        nvrhi::TextureHandle m_Classification;
        nvrhi::TextureHandle m_DebugOutput;
        nvrhi::TextureHandle m_DebugSink;
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
        uint32_t m_TimerFrame = 0;
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
        void AdvanceTimers();
    };
}
