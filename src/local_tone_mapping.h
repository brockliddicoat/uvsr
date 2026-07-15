#pragma once

#include <donut/core/math/math.h>
#include <donut/engine/BindingCache.h>
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
    enum class LocalToneMappingQuality : uint32_t
    {
        Performance = 0,
        Balanced = 1,
        MaximumQuality = 2
    };

    enum class LocalToneMappingDebugMode : uint32_t
    {
        Off = 0,
        InputLogLuminance = 1,
        BaseLocalEV = 2,
        FinalLocalEV = 3,
        GridConfidence = 4
    };

    struct LocalToneMappingSettings
    {
        bool enabled = false;
        float strength = 0.35f;
        float radiusAt1080p = 64.0f;
        float edgePreservation = 0.70f;
        float shadowRecovery = 0.70f;
        float highlightCompression = 0.70f;
        float maximumCorrectionEV = 1.25f;
        float referenceResponseMs = 200.0f;

        LocalToneMappingQuality quality =
            LocalToneMappingQuality::Balanced;

        LocalToneMappingDebugMode debugMode =
            LocalToneMappingDebugMode::Off;
    };

    inline constexpr float LocalToneMappingMinEV = -16.0f;
    inline constexpr float LocalToneMappingMaxEV = 16.0f;
    inline constexpr float LocalToneMappingFixedPointScale = 4096.0f;
    inline constexpr uint32_t LocalToneMappingMaximum2DTextureDimension = 16384u;
    inline constexpr uint32_t LocalToneMappingMaximum3DTextureDimension = 2048u;

    struct LocalToneMappingQualityParameters
    {
        uint32_t spatialCellSizeAt1080p = 24u;
        uint32_t samplingStrideAt1080p = 4u;
        uint32_t rangeBinCount = 48u;
    };

    struct LocalToneMappingGridConfiguration
    {
        dm::uint2 inputResolution = dm::uint2::zero();
        dm::uint3 gridDimensions = dm::uint3(1u);
        uint32_t spatialCellSize = 1u;
        uint32_t samplingStride = 1u;
        uint32_t voxelCount = 1u;
    };

    void SanitizeLocalToneMappingSettings(LocalToneMappingSettings& settings);
    void ApplyLocalToneMappingQualityPreset(
        LocalToneMappingSettings& settings,
        LocalToneMappingQuality quality);

    [[nodiscard]] LocalToneMappingQualityParameters
        GetLocalToneMappingQualityParameters(LocalToneMappingQuality quality);
    [[nodiscard]] LocalToneMappingGridConfiguration
        ComputeLocalToneMappingGridConfiguration(
            uint32_t width,
            uint32_t height,
            LocalToneMappingQuality quality);

    [[nodiscard]] float ComputeLocalToneMappingLogLuminance(
        float red, float green, float blue);
    [[nodiscard]] float ComputeLocalToneMappingRangeCoordinate(float logLuminance);
    [[nodiscard]] uint32_t EncodeLocalToneMappingLogLuminance(float logLuminance);
    [[nodiscard]] float ComputeLocalToneMappingBaseDeltaEV(
        float globalReferenceLogY, float filteredLogY);
    [[nodiscard]] float ComputeLocalToneMappingCorrectionEV(
        float baseDeltaEV,
        float confidence,
        const LocalToneMappingSettings& settings,
        bool eligibleForFinalOutput = true);
    [[nodiscard]] float ComputeMaximumPositiveLocalCorrectionEV(
        const LocalToneMappingSettings& settings,
        bool eligibleForFinalOutput = true);
    [[nodiscard]] float ComputeLocalToneMappingReferenceCurrentWeight(
        float deltaSeconds, float responseMilliseconds);
    [[nodiscard]] float ComputeLocalToneMappingRangeSigmaBins(float edgePreservation);
    [[nodiscard]] uint32_t ComputeLocalToneMappingMaximumCellSampleCount(
        const LocalToneMappingGridConfiguration& configuration);
    [[nodiscard]] uint64_t ComputeLocalToneMappingMaximumFixedPointAccumulation(
        const LocalToneMappingGridConfiguration& configuration);
    [[nodiscard]] bool IsLocalToneMappingFixedPointAccumulationSafe(
        const LocalToneMappingGridConfiguration& configuration);

    [[nodiscard]] const char* GetLocalToneMappingQualityName(
        LocalToneMappingQuality quality);
    [[nodiscard]] const char* GetLocalToneMappingDebugModeName(
        LocalToneMappingDebugMode mode);

    struct LocalToneMappingTimings
    {
        float splatMs = 0.f;
        float referenceMs = 0.f;
        float blurMs = 0.f;
        float finalizeMs = 0.f;
        float totalMs = 0.f;
    };

    struct LocalToneMappingMemoryStats
    {
        LocalToneMappingGridConfiguration configuration;
        uint64_t accumulationBytes = 0;
        uint64_t blurBytes = 0;
        uint64_t finalGridBytes = 0;
        uint64_t referenceBytes = 0;
        uint64_t totalBytes = 0;
    };

    struct LocalToneMappingDisplayState
    {
        nvrhi::ITexture* grid = nullptr;
        dm::uint3 gridDimensions = dm::uint3(1u);
        uint32_t spatialCellSize = 1u;
        bool active = false;
    };

    class LocalToneMappingPass
    {
    public:
        LocalToneMappingPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory,
            std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses);

        bool Render(
            nvrhi::ICommandList* commandList,
            LocalToneMappingSettings& settings,
            nvrhi::ITexture* displaySource,
            float deltaSeconds);

        void ResetHistory();
        void ReleaseResources();
        void Deactivate();

        [[nodiscard]] LocalToneMappingDisplayState GetDisplayState() const;
        [[nodiscard]] const LocalToneMappingTimings& GetTimings() const
        {
            return m_Timings;
        }
        [[nodiscard]] const LocalToneMappingMemoryStats& GetMemoryStats() const
        {
            return m_MemoryStats;
        }

    private:
        enum class Stage : uint32_t
        {
            Splat,
            Reference,
            Blur,
            Finalize,
            Total,
            Count
        };

        struct Pipeline
        {
            nvrhi::ShaderHandle shader;
            nvrhi::BindingLayoutHandle bindingLayout;
            nvrhi::ComputePipelineHandle pipeline;
        };

        static constexpr uint32_t c_TimerLatency = 4;

        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;
        donut::engine::BindingCache m_BindingSets;
        nvrhi::BufferHandle m_ConstantBuffer;
        Pipeline m_SplatPipeline;
        Pipeline m_ReferencePipeline;
        std::array<Pipeline, 3> m_BlurPipelines;
        Pipeline m_FinalizePipeline;

        LocalToneMappingGridConfiguration m_Configuration;
        nvrhi::TextureHandle m_AccumulatedLog;
        nvrhi::TextureHandle m_AccumulatedWeight;
        std::array<nvrhi::TextureHandle, 2> m_BlurGrid;
        nvrhi::TextureHandle m_FinalGrid;
        std::array<nvrhi::TextureHandle, 2> m_ReferenceHistory;
        nvrhi::TextureHandle m_NeutralGrid;

        uint32_t m_ReferenceReadIndex = 0u;
        bool m_HistoryValid = false;
        bool m_WasActive = false;
        bool m_OutputValid = false;
        bool m_FormatSupportValid = false;
        bool m_PipelinesValid = false;
        bool m_LoggedFrameFailure = false;

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerPending{};
        std::array<bool, static_cast<size_t>(Stage::Count)> m_TimerActive{};
        uint32_t m_TimerFrame = 0u;

        LocalToneMappingTimings m_Timings;
        LocalToneMappingMemoryStats m_MemoryStats;

        void CreatePipelines(
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory);
        void CreateNeutralGrid();
        bool EnsureResources(const LocalToneMappingGridConfiguration& configuration);
        void ReportFrameFailure(const char* reason);

        void BeginStage(nvrhi::ICommandList* commandList, Stage stage);
        void EndStage(nvrhi::ICommandList* commandList, Stage stage);
        void AdvanceTimers();
    };
}
