#pragma once

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class ICompositeView;
    class IView;
    class ShaderFactory;
}

namespace uvsr
{
    enum class ScreenSpaceVisibilityQuality : uint32_t
    {
        Low,
        Medium,
        High,
        Ultra,
        Custom
    };

    enum class SectorHitCriterion : uint32_t
    {
        Round,
        Ceil,
        Floor
    };

    enum class ScreenSpaceVisibilityDebugMode : uint32_t
    {
        FinalComposite,
        RawAmbientVisibility,
        FilteredAmbientVisibility,
        RawIndirectDiffuse,
        FilteredIndirectDiffuse,
        GiLightOnly,
        DirectRadianceSource,
        ReceiverNormal,
        SampledSourceNormal,
        SampleCount,
        NewlyCoveredSectorCount,
        FinalMaskPopcount,
        CurrentSliceOrientation,
        ProjectedNormal,
        FrontHorizonAngle,
        BackHorizonAngle,
        ThicknessInterval,
        TemporalHistoryWeight,
        TemporalValidity
    };

    struct SharedSamplingSettings
    {
        // Total stochastic depth fetches per pixel. Samples are divided between
        // the two ordered horizon directions, with the odd sample alternating
        // sides across the spatiotemporal phase sequence.
        uint32_t sampleCount = 32;
        float radius = 3.0f;
        float thickness = 0.5f;
        bool distanceScaledThickness = false;
        float thicknessDistanceScale = 0.0025f;
        float stepDistributionExponent = 1.0f;
        float radialJitter = 1.0f;
        // The compact five-level hierarchy is useful for long AO rays, but its
        // full-screen construction costs more than it saves for the default
        // 3 m radius on current discrete GPUs. Keep it as an opt-in diagnostic.
        bool useDepthHierarchy = false;
    };

    struct AmbientOcclusionSettings
    {
        bool enabled = true;
        // Apply the visibility estimate at full strength. Tonemapping and the
        // neutral lighting calibration handle presentation contrast without
        // washing out the AO signal itself.
        float strength = 1.0f;
        float power = 1.0f;
    };

    struct IndirectDiffuseSettings
    {
        bool enabled = true;
        float intensity = 4.0f;
        bool includeEmissive = true;
        float emissiveGain = 4.0f;
    };

    struct VisibilityFilteringSettings
    {
        bool temporalEnabled = true;
        float aoTemporalResponse = 0.90f;
        float giTemporalResponse = 0.94f;
        bool spatialEnabled = false;
        uint32_t spatialRadius = 1;
        float depthRejection = 0.02f;
        float normalRejection = 0.85f;
    };

    struct VisibilityDebugSettings
    {
        ScreenSpaceVisibilityDebugMode mode = ScreenSpaceVisibilityDebugMode::FinalComposite;
        bool freezeSamplingPhase = false;
        SectorHitCriterion sectorHitCriterion = SectorHitCriterion::Round;
    };

    struct ScreenSpaceVisibilitySettings
    {
        bool enabled = true;
        ScreenSpaceVisibilityQuality quality = ScreenSpaceVisibilityQuality::Medium;
        SharedSamplingSettings sampling;
        AmbientOcclusionSettings ambientOcclusion;
        IndirectDiffuseSettings indirectDiffuse;
        VisibilityFilteringSettings filtering;
        VisibilityDebugSettings debug;

        [[nodiscard]] bool HasActiveConsumer() const
        {
            return enabled && (ambientOcclusion.enabled || indirectDiffuse.enabled);
        }
    };

    void ApplyScreenSpaceVisibilityQualityPreset(
        ScreenSpaceVisibilitySettings& settings,
        ScreenSpaceVisibilityQuality quality);

    struct ScreenSpaceVisibilityInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* motionVectors = nullptr;
        nvrhi::ITexture* sourceRadiance = nullptr;
        nvrhi::ITexture* gbufferDiffuse = nullptr;
        nvrhi::ITexture* gbufferSpecular = nullptr;
        nvrhi::ITexture* gbufferEmissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;
        nvrhi::ITexture* baseLighting = nullptr;
        nvrhi::ITexture* output = nullptr;
    };

    struct ScreenSpaceVisibilityTimings
    {
        float depthHierarchyMs = 0.f;
        float samplingMs = 0.f;
        float temporalMs = 0.f;
        float spatialMs = 0.f;
        float compositionMs = 0.f;

        [[nodiscard]] float CompleteEffectMs() const
        {
            return depthHierarchyMs + samplingMs + temporalMs + spatialMs +
                compositionMs;
        }
    };

    class ScreenSpaceVisibilityPass
    {
    public:
        ScreenSpaceVisibilityPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory);

        void Render(
            nvrhi::ICommandList* commandList,
            const ScreenSpaceVisibilitySettings& settings,
            const donut::engine::ICompositeView& compositeView,
            const ScreenSpaceVisibilityInputs& inputs,
            dm::float3 ambientColorTop,
            dm::float3 ambientColorBottom,
            uint32_t frameIndex);

        void ResetHistory();
        void Deactivate();
        void ResetBindingCache();

        [[nodiscard]] const ScreenSpaceVisibilityTimings& GetTimings() const { return m_Timings; }
        [[nodiscard]] nvrhi::ITexture* GetAmbientVisibility() const { return m_OutputAmbientVisibility; }
        [[nodiscard]] nvrhi::ITexture* GetIndirectDiffuseIrradiance() const { return m_OutputIndirectDiffuse; }

    private:
        enum class Stage : uint32_t
        {
            DepthHierarchy,
            Sampling,
            Temporal,
            Spatial,
            Composition,
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
        nvrhi::BufferHandle m_ConstantBuffer;

        // AO, GI and traversal-debug specialization prevents the full sampling
        // kernel from carrying every consumer's registers and texture traffic.
        std::array<Pipeline, 8> m_Sampling;
        Pipeline m_DepthHierarchy;
        std::array<Pipeline, 4> m_Temporal;
        std::array<Pipeline, 4> m_Denoise;
        Pipeline m_Composite;

        dm::uint2 m_FullSize = dm::uint2::zero();
        dm::uint2 m_SamplingSize = dm::uint2::zero();
        uint32_t m_ResolutionScale = 0;
        bool m_TemporalResourcesEnabled = false;

        nvrhi::TextureHandle m_RawAmbientVisibility;
        nvrhi::TextureHandle m_DepthHierarchyTexture;
        nvrhi::TextureHandle m_RawIndirectDiffuse;
        nvrhi::TextureHandle m_RawDebug;
        nvrhi::TextureHandle m_HistoryAmbientVisibility[2];
        nvrhi::TextureHandle m_HistoryIndirectDiffuse[2];
        nvrhi::TextureHandle m_HistoryDepth[2];
        nvrhi::TextureHandle m_HistoryNormal[2];
        nvrhi::TextureHandle m_HistoryValidity;
        nvrhi::TextureHandle m_DenoisedAmbientVisibility;
        nvrhi::TextureHandle m_DenoisedIndirectDiffuse;
        nvrhi::TextureHandle m_DenoisedDebug;
        nvrhi::TextureHandle m_OutputAmbientVisibility;
        nvrhi::TextureHandle m_OutputIndirectDiffuse;

        uint32_t m_HistoryReadIndex = 0;
        bool m_HistoryValid = false;
        uint64_t m_HistorySignature = 0;
        bool m_HasPreviousCamera = false;
        dm::float3 m_PreviousCameraOrigin = dm::float3::zero();
        dm::float3 m_PreviousCameraDirection = dm::float3(0.f, 0.f, 1.f);
        dm::float4x4 m_PreviousProjection = dm::float4x4::identity();

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerPending{};
        std::array<bool, static_cast<size_t>(Stage::Count)> m_TimerActive{};
        uint32_t m_TimerFrame = 0;
        ScreenSpaceVisibilityTimings m_Timings;

        void CreatePipelines(const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory);
        void EnsureResources(
            dm::uint2 fullSize,
            bool temporalEnabled);
        void ReleaseResources();
        bool UpdateHistoryValidity(
            const ScreenSpaceVisibilitySettings& settings,
            const donut::engine::IView& view);
        static uint64_t ComputeHistorySignature(const ScreenSpaceVisibilitySettings& settings);

        void BeginStage(nvrhi::ICommandList* commandList, Stage stage);
        void EndStage(nvrhi::ICommandList* commandList, Stage stage);
        void AdvanceTimers();
    };
}
