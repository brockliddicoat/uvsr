#pragma once

#include "lighting_contribution_shared.h"
#include "visibility_benchmark_statistics.h"
#include "visibility_performance_plan.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace donut::engine
{
    class ICompositeView;
    class ShaderFactory;
    struct ShaderMacro;
}

namespace uvsr
{
    inline constexpr uint32_t MaxIndirectDiffuseBounceCount = 8;
    inline constexpr uint32_t InstrumentedLaterBounceCount = 3;
    // Contribution-terminated GI is recorded as a finite command stream. GPU
    // indirect dispatch removes all full-screen work after convergence; this
    // guard remains to contain malformed or non-contracting scene data.
    inline constexpr uint32_t MaxContributionTerminatedBounceCount = 16;
    inline constexpr float MinimumContributionTerminatedThreshold = 0.001f;
    inline constexpr float MaximumBounceContributionCutoff = 0.02f;
    inline constexpr float ContributionTerminatedThresholdGrowth = 4.0f;
    inline constexpr float VisibilityEmissiveSourceGain = 4.0f;
    inline constexpr uint32_t ImplementedVisibilityEstimatorCount = 3;

    inline constexpr uint32_t LightingSource_Direct =
        UVSR_LIGHTING_SOURCE_DIRECT;
    inline constexpr uint32_t LightingSource_Emissive =
        UVSR_LIGHTING_SOURCE_EMISSIVE;
    inline constexpr uint32_t LightingSource_Environment =
        UVSR_LIGHTING_SOURCE_ENVIRONMENT;
    inline constexpr uint32_t LightingSource_IndirectDiffuse =
        UVSR_LIGHTING_SOURCE_INDIRECT_DIFFUSE;
    inline constexpr uint32_t LightingSource_IndirectSpecular =
        UVSR_LIGHTING_SOURCE_INDIRECT_SPECULAR;
    inline constexpr uint32_t LightingSource_All = UVSR_LIGHTING_SOURCE_ALL;

    enum class ScreenSpaceVisibilityQuality : uint32_t
    {
        Low,
        Medium,
        High,
        Ultra,
        Custom
    };

    enum class VisibilityEstimator : uint32_t
    {
        UniformProjectedAngle,
        UniformSolidAngle,
        CosineWeightedSolidAngle
    };

    enum class VisibilityResolution : uint32_t
    {
        Full,
        Half,
        Quarter
    };

    enum class VisibilitySampleScheduler : uint32_t
    {
        IndependentHash,
        ToroidalBlueNoiseRankField,
        FilterAdaptedSpatiotemporalRankField
    };

    enum class VisibilitySpatialFilter : uint32_t
    {
        JointBilateral,
        GaussianJointBilateral
    };

    enum class VisibilityScalarBufferPrecision : uint32_t
    {
        Float16,
        Float32
    };

    enum class VisibilityVectorBufferPrecision : uint32_t
    {
        Rgba16Float,
        Rgba32Float
    };

    struct VisibilityBufferPrecisionSettings
    {
        VisibilityScalarBufferPrecision rawAmbient =
            VisibilityScalarBufferPrecision::Float16;
        VisibilityVectorBufferPrecision rawIndirect =
            VisibilityVectorBufferPrecision::Rgba16Float;
        VisibilityVectorBufferPrecision cumulativeIndirect =
            VisibilityVectorBufferPrecision::Rgba16Float;
        VisibilityScalarBufferPrecision temporalAmbient =
            VisibilityScalarBufferPrecision::Float16;
        VisibilityVectorBufferPrecision temporalIndirect =
            VisibilityVectorBufferPrecision::Rgba16Float;
        VisibilityScalarBufferPrecision temporalDepth =
            VisibilityScalarBufferPrecision::Float32;
        VisibilityScalarBufferPrecision finalAmbient =
            VisibilityScalarBufferPrecision::Float16;
        VisibilityVectorBufferPrecision finalIndirect =
            VisibilityVectorBufferPrecision::Rgba16Float;
        VisibilityScalarBufferPrecision depthHierarchy =
            VisibilityScalarBufferPrecision::Float16;
    };

    void ApplyVisibilityBufferPrecisionPreset(
        VisibilityBufferPrecisionSettings& settings,
        bool use16BitAo,
        bool use16BitGi);

    enum class VisibilityPackedEdgeMode : uint32_t
    {
        Depth,
        DepthAndNormal,
        SlopeAdjustedDepthAndNormal,
        ControlledLeakage
    };

    struct VisibilityComposableSettings
    {
        VisibilityPerformanceProfileConfiguration configuration =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::GenericFallback);
        VisibilityBufferPrecisionSettings bufferPrecision;
        VisibilityPackedEdgeMode packedEdgeMode =
            VisibilityPackedEdgeMode::DepthAndNormal;
    };

    struct SharedSamplingSettings
    {
        // This is the scheduled radial-sample budget per eligible tracing
        // pixel on one stochastic slice.
        uint32_t maximumSampleCount = 20;
        float radius = 3.0f;
        float thickness = 0.5f;
        float stepDistributionExponent = 2.0f;
        VisibilitySampleScheduler scheduler =
            VisibilitySampleScheduler::ToroidalBlueNoiseRankField;
    };

    struct AmbientOcclusionSettings
    {
        bool enabled = true;
        float strength = 1.0f;
        float power = 1.0f;
    };

    struct IndirectDiffuseSettings
    {
        bool enabled = true;
        bool limitBounces = true;
        uint32_t bounceCount = 1;
        float minimumBounceContribution = 0.001f;
        float intensity = 4.0f;
    };

    struct VisibilityReconstructionSettings
    {
        bool temporalEnabled = false;
        bool spatialEnabled = false;
        // SSRT3's default current-frame blend response.
        float temporalResponse = 0.35f;
        VisibilitySpatialFilter spatialFilter =
            VisibilitySpatialFilter::GaussianJointBilateral;
        float spatialRadius = 4.0f;
    };

    struct ScreenSpaceVisibilitySettings
    {
        ScreenSpaceVisibilitySettings();

        bool enabled = true;
        ScreenSpaceVisibilityQuality quality =
            ScreenSpaceVisibilityQuality::High;
        ScreenSpaceVisibilityQuality qualityPresetOrigin =
            ScreenSpaceVisibilityQuality::High;
        VisibilityEstimator estimator =
            VisibilityEstimator::UniformSolidAngle;
        VisibilityResolution resolution = VisibilityResolution::Full;
        SharedSamplingSettings sampling;
        AmbientOcclusionSettings ambientOcclusion;
        IndirectDiffuseSettings indirectDiffuse;
        VisibilityReconstructionSettings reconstruction;
        // Start with the compiled exact 20-sample path. Every advanced profile
        // resolves a complete CPU-side pass/resource plan.
        VisibilityPerformanceProfile performanceProfile =
            VisibilityPerformanceProfile::ExactFixed20;
        // Presets remain useful reproducible starting points. Once an
        // individual control changes, this configuration becomes the live
        // composed plan so unrelated controls no longer erase optimizations.
        VisibilityComposableSettings performance;
        // Presentation mode that displays only material-applied screen-space
        // diffuse GI.
        bool showIndirectDiffuseOnly = false;

        [[nodiscard]] bool HasActiveAmbientOcclusion() const
        {
            return ambientOcclusion.enabled && ambientOcclusion.strength > 0.f;
        }

        [[nodiscard]] bool HasActiveIndirectDiffuse() const
        {
            return indirectDiffuse.enabled &&
                indirectDiffuse.intensity > 0.f;
        }

        [[nodiscard]] bool HasActiveConsumer() const
        {
            return enabled &&
                (HasActiveAmbientOcclusion() || HasActiveIndirectDiffuse());
        }

        [[nodiscard]] bool RequiresMotionVectors() const
        {
            return HasActiveConsumer() &&
                reconstruction.temporalEnabled;
        }
    };

    void ApplyScreenSpaceVisibilityQualityPreset(
        ScreenSpaceVisibilitySettings& settings,
        ScreenSpaceVisibilityQuality quality);

    void MarkScreenSpaceVisibilityQualityCustom(
        ScreenSpaceVisibilitySettings& settings);

    [[nodiscard]] bool MatchesScreenSpaceVisibilityQualityPreset(
        const ScreenSpaceVisibilitySettings& settings,
        ScreenSpaceVisibilityQuality quality);

    void ReconcileScreenSpaceVisibilityQualityPreset(
        ScreenSpaceVisibilitySettings& settings);

    [[nodiscard]] VisibilityPerformanceProfileConfiguration
        GetEffectiveVisibilityPerformanceConfiguration(
            const ScreenSpaceVisibilitySettings& settings);

    void ResetVisibilityComposableSettings(
        ScreenSpaceVisibilitySettings& settings,
        VisibilityPerformanceProfile profile);

    void MakeVisibilityPerformanceComposable(
        ScreenSpaceVisibilitySettings& settings);

    [[nodiscard]] uint64_t GetVisibilityRuntimeConfigurationKey(
        const ScreenSpaceVisibilitySettings& settings);

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
        uint32_t knownInactiveLightingSources = 0u;
    };

    struct ScreenSpaceVisibilityTimings
    {
        float depthHierarchyMs = 0.f;
        float samplingMs = 0.f;
        float firstTraceMs = 0.f;
        float laterTraceMs = 0.f;
        std::array<float, InstrumentedLaterBounceCount>
            laterBounceMs{};
        float spatialDenoiseMs = 0.f;
        float temporalMs = 0.f;
        float fusedSpatialDenoiseUpsampleMs = 0.f;
        float requiredUpsampleMs = 0.f;
        float fullResolutionApplyMs = 0.f;
        float compositionMs = 0.f;
        float effectEnvelopeMs = 0.f;

        // Exact logical texel arithmetic, excluding API alignment/residency.
        uint64_t outputTextureBytes = 0u;
        uint64_t workingTextureBytes = 0u;
        uint64_t rawAmbientTextureBytes = 0u;
        uint64_t rawIndirectFrontierBytes = 0u;
        uint64_t multiBounceIndirectBytes = 0u;
        uint64_t finalAmbientTextureBytes = 0u;
        uint64_t finalIndirectTextureBytes = 0u;
        uint64_t schedulerResourceBytes = 0u;
        uint64_t temporalAmbientHistoryBytes = 0u;
        uint64_t temporalIndirectHistoryBytes = 0u;
        uint64_t temporalDepthHistoryBytes = 0u;
        uint64_t temporalNormalHistoryBytes = 0u;
        uint64_t depthHierarchyBytes = 0u;
        uint64_t packedFastNoiseBytes = 0u;
        uint64_t packedEdgeMetadataBytes = 0u;
        uint64_t maskCacheBytes = 0u;
        uint64_t avoidedTextureBytes = 0u;
        // An estimate of duplicate R32 mask payload that the shared
        // register-local producer avoids when AO and GI are both active.
        uint64_t sharedMaskPayloadBytes = 0u;
        uint64_t optionalTextureBytes = 0u;
        uint64_t fullResolutionIntermediateBytes = 0u;
        uint64_t logicalTrafficAvoidedBytes = 0u;
        uint32_t activeSrvCount = 0u;
        uint32_t activeUavCount = 0u;
        uint32_t peakSrvCount = 0u;
        uint32_t peakUavCount = 0u;
        uint32_t activeDispatchCount = 0u;
        // The renderer-resolved workload can differ from the settings-only
        // forecast when motion vectors are unavailable or inactive lighting
        // prunes higher GI bounces. UI readiness and benchmarking must compare
        // against what was actually rendered.
        VisibilityPerformanceWorkload activeWorkload;
        bool hasActiveWorkload = false;
        bool profileValid = true;
        std::string activePermutation = "Reference";
        std::string profileError;

        [[nodiscard]] float SummedStageMs() const
        {
            return depthHierarchyMs + firstTraceMs + laterTraceMs +
                spatialDenoiseMs + temporalMs +
                fusedSpatialDenoiseUpsampleMs + requiredUpsampleMs +
                fullResolutionApplyMs + compositionMs;
        }

        [[nodiscard]] float CompleteEffectMs() const
        {
            return effectEnvelopeMs;
        }

    };

    class ScreenSpaceVisibilityPass
    {
    public:
        ScreenSpaceVisibilityPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory,
            const std::filesystem::path& filterAdaptedNoisePath);

        void Render(
            nvrhi::ICommandList* commandList,
            const ScreenSpaceVisibilitySettings& settings,
            const donut::engine::ICompositeView& compositeView,
            const ScreenSpaceVisibilityInputs& inputs,
            dm::float3 ambientColorTop,
            dm::float3 ambientColorBottom,
            float exposureScale,
            uint32_t frameIndex);

        void Deactivate();
        void ResetHistory();
        void ResetBindingCache();

        [[nodiscard]] const ScreenSpaceVisibilityTimings& GetTimings() const
        {
            return m_Timings;
        }

        bool BeginBenchmark(
            const VisibilityBenchmarkRunMetadata& metadata,
            uint32_t warmupFrameCount = 120u,
            uint32_t measuredFrameCount = 240u);
        void CancelBenchmark();
        [[nodiscard]] bool IsBenchmarkActive() const
        {
            return m_BenchmarkActive;
        }
        [[nodiscard]] bool IsBenchmarkComplete() const
        {
            return m_BenchmarkActive && m_BenchmarkStatistics.IsComplete();
        }
        [[nodiscard]] VisibilityBenchmarkSummary GetBenchmarkSummary() const
        {
            return m_BenchmarkStatistics.BuildSummary();
        }
        // Post-Render ID of the next fully instrumented logical timer frame.
        // It intentionally does not advance while a query-ring slot is busy.
        [[nodiscard]] uint64_t GetBenchmarkNextLogicalFrameId() const
        {
            return m_TimerFrame;
        }

    private:
        enum class Stage : uint32_t
        {
            DepthHierarchy,
            FirstTrace,
            LaterTrace,
            LaterTraceBounce2,
            LaterTraceBounce3,
            LaterTraceBounce4,
            SpatialDenoise,
            Temporal,
            FusedSpatialDenoiseUpsample,
            RequiredUpsample,
            FullResolutionApply,
            Composition,
            EffectEnvelope,
            Count
        };

        struct Pipeline
        {
            nvrhi::ShaderHandle shader;
            nvrhi::BindingLayoutHandle bindingLayout;
            nvrhi::ComputePipelineHandle pipeline;
        };

        static constexpr uint32_t c_TimerLatency = 4;
        static constexpr uint32_t c_ConsumerVariantCount = 3;

        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BufferHandle m_BounceContinuation;
        nvrhi::BufferHandle m_BounceIndirectArguments;
        nvrhi::SamplerHandle m_PointClampSampler;

        std::array<std::array<Pipeline,
            c_ConsumerVariantCount>, ImplementedVisibilityEstimatorCount>
            m_Sampling;
        std::array<std::array<Pipeline, 2>,
            ImplementedVisibilityEstimatorCount> m_MultiBounceFirstSampling;
        std::array<std::array<Pipeline, 2>,
            ImplementedVisibilityEstimatorCount> m_IndirectBounceSampling;
        Pipeline m_BounceDispatchControl;
        std::array<Pipeline, c_ConsumerVariantCount> m_Temporal;
        std::array<std::array<Pipeline, c_ConsumerVariantCount>, 2> m_Filter;
        Pipeline m_DepthHierarchy;
        // AO Power is a compile-time on/off specialization so its identity
        // value does not leave a pow instruction in the default compositor.
        std::array<Pipeline, 2> m_Composite;
        std::unordered_map<uint64_t, Pipeline> m_AdvancedPipelines;
        std::unordered_map<uint64_t, nvrhi::BindingSetHandle>
            m_AdvancedBindingSets;

        dm::uint2 m_FullSize = dm::uint2::zero();
        dm::uint2 m_SamplingSize = dm::uint2::zero();
        uint32_t m_ResolutionScale = 1u;
        bool m_AmbientResourcesEnabled = false;
        bool m_IndirectDiffuseResourcesEnabled = false;
        bool m_MultipleBounceResourcesEnabled = false;
        bool m_TemporalResourcesEnabled = false;
        bool m_PostProcessResourcesEnabled = false;
        bool m_DepthHierarchyResourcesEnabled = false;
        bool m_PackedFastResourcesEnabled = false;
        bool m_PackedEdgeResourcesEnabled = false;
        bool m_FinalAmbientResourcesEnabled = false;
        uint64_t m_BufferPrecisionConfigurationKey = 0u;

        nvrhi::TextureHandle m_RawAmbientVisibility;
        nvrhi::TextureHandle m_RawIndirectDiffuse[2];
        nvrhi::TextureHandle m_CumulativeIndirectDiffuse;
        nvrhi::TextureHandle m_TemporalAmbientVisibility[2];
        nvrhi::TextureHandle m_TemporalIndirectDiffuse[2];
        nvrhi::TextureHandle m_TemporalDepth[2];
        nvrhi::TextureHandle m_TemporalNormal[2];
        nvrhi::TextureHandle m_FinalAmbientVisibility;
        nvrhi::TextureHandle m_FinalIndirectDiffuse;
        nvrhi::TextureHandle m_DepthHierarchyTexture;
        nvrhi::TextureHandle m_BlueNoiseTexture;
        nvrhi::TextureHandle m_FilterAdaptedNoiseTexture;
        nvrhi::TextureHandle m_PackedFastNoiseTexture;
        nvrhi::TextureHandle m_PackedEdgesTexture;

        nvrhi::TextureHandle m_DummyAmbientVisibility;
        nvrhi::TextureHandle m_DummyIndirectDiffuse;
        nvrhi::TextureHandle m_DummyVector;
        nvrhi::TextureHandle m_DummyAmbientOutput;
        nvrhi::TextureHandle m_DummyIndirectOutput;

        // [estimator][consumer]
        std::array<std::array<nvrhi::BindingSetHandle,
            c_ConsumerVariantCount>,
            ImplementedVisibilityEstimatorCount> m_SamplingBindingSets;
        // [estimator][AO variant]
        std::array<std::array<nvrhi::BindingSetHandle, 2>,
            ImplementedVisibilityEstimatorCount> m_MultiBounceFirstBindingSets;
        // [estimator][initialize/add][bounce rotation]
        std::array<std::array<std::array<
            nvrhi::BindingSetHandle, 3>, 2>,
            ImplementedVisibilityEstimatorCount> m_IndirectBounceBindingSets;
        nvrhi::BindingSetHandle m_BounceDispatchControlBindingSet;
        // [consumer][single/multiple-bounce source][history write parity]
        std::array<std::array<std::array<nvrhi::BindingSetHandle, 2>, 2>,
            c_ConsumerVariantCount> m_TemporalBindingSets;
        // Sources 0/1 are the single/multiple-bounce raw results; 2/3 select
        // the temporal ping-pong output written during the current frame.
        std::array<std::array<std::array<nvrhi::BindingSetHandle, 4>,
            c_ConsumerVariantCount>, 2> m_FilterBindingSets;
        nvrhi::BindingSetHandle m_DepthHierarchyBindingSet;
        // [AO Power off/on][raw single, raw cumulative, temporal ping-pong].
        std::array<std::array<nvrhi::BindingSetHandle, 4>, 2>
            m_CompositeBindingSets;

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerPending{};
        std::array<std::array<uint64_t, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerFrameIds{};
        std::array<bool, static_cast<size_t>(Stage::Count)> m_TimerActive{};

        std::vector<uint16_t> m_BlueNoiseUpload;
        std::vector<uint8_t> m_FilterAdaptedNoiseUpload;
        std::vector<uint8_t> m_PackedFastNoiseUpload;
        bool m_SamplingNoiseUploaded = false;
        bool m_PackedFastNoiseUploaded = false;
        bool m_HistoryValid = false;
        bool m_HistoryConfigurationInitialized = false;
        uint64_t m_HistoryConfigurationKey = 0u;
        bool m_HistoryEstimatorInitialized = false;
        VisibilityEstimator m_HistoryEstimator =
            VisibilityEstimator::UniformProjectedAngle;
        uint32_t m_HistoryIndex = 0u;
        uint32_t m_TimerFrame = 0u;
        bool m_TimerFrameWritable = true;
        ScreenSpaceVisibilityTimings m_Timings;
        VisibilityExecutionPlan m_ExecutionPlan;
        VisibilityBenchmarkStatistics m_BenchmarkStatistics;
        bool m_BenchmarkActive = false;

        void CreatePipelines(
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory);
        void EnsureResources(
            dm::uint2 fullSize,
            uint32_t resolutionScale,
            bool ambientEnabled,
            bool indirectDiffuseEnabled,
            bool multipleBouncesEnabled,
            bool temporalEnabled,
            bool postProcessEnabled,
            bool depthHierarchyEnabled,
            bool finalAmbientEnabled,
            bool packedFastEnabled,
            bool packedEdgesEnabled,
            const VisibilityBufferPrecisionSettings& bufferPrecision);
        void ReleaseResources();
        void UploadSamplingNoise(nvrhi::ICommandList* commandList);
        Pipeline& GetOrCreateAdvancedPipeline(
            uint64_t key,
            const char* shaderName,
            const std::vector<nvrhi::BindingLayoutItem>& bindings,
            const std::vector<donut::engine::ShaderMacro>* macros = nullptr);

        void BeginStage(nvrhi::ICommandList* commandList, Stage stage);
        void EndStage(nvrhi::ICommandList* commandList, Stage stage);
        void AdvanceTimers();
    };
}
