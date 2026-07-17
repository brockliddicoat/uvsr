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
    inline constexpr uint32_t MaxIndirectDiffuseBounceCount = 4;
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

    struct SharedSamplingSettings
    {
        // These are scheduled radial-sample budgets per eligible tracing pixel
        // on one stochastic slice. The selected budget is stochastically
        // rounded over a nested radial prefix, so increasing a limit does not
        // move samples already present at a lower limit.
        uint32_t minimumSampleCount = 8;
        uint32_t maximumSampleCount = 20;
        // Selects the adaptive importance/feedback specialization. When false,
        // every eligible pixel traces maximumSampleCount taps on one slice and
        // the adaptive shader path is compiled out.
        bool adaptiveSparseSamplingEnabled = false;
        float radius = 3.0f;
        float thickness = 0.5f;
        float stepDistributionExponent = 2.0f;
        float adaptiveStrength = 1.0f;
        VisibilitySampleScheduler scheduler =
            VisibilitySampleScheduler::ToroidalBlueNoiseRankField;
    };

    struct AmbientOcclusionSettings
    {
        bool enabled = true;
        float strength = 1.0f;
    };

    struct IndirectDiffuseSettings
    {
        bool enabled = true;
        uint32_t bounceCount = 1;
        float minimumBounceContribution = 0.001f;
        float intensity = 4.0f;
        bool includeEmissive = true;
        float emissiveGain = 4.0f;
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
        bool enabled = true;
        ScreenSpaceVisibilityQuality quality =
            ScreenSpaceVisibilityQuality::Medium;
        VisibilityEstimator estimator =
            VisibilityEstimator::UniformSolidAngle;
        VisibilityResolution resolution = VisibilityResolution::Full;
        SharedSamplingSettings sampling;
        AmbientOcclusionSettings ambientOcclusion;
        IndirectDiffuseSettings indirectDiffuse;
        VisibilityReconstructionSettings reconstruction;
        // Reference is a hard lock to the canonical generic pipeline. Every
        // advanced profile resolves a complete CPU-side pass/resource plan;
        // no candidate shader branch or auxiliary resource exists while this
        // remains Reference.
        VisibilityPerformanceProfile performanceProfile =
            VisibilityPerformanceProfile::Reference;
        // A deliberately narrow diagnostic exception: composition displays
        // only material-applied screen-space diffuse GI.
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

        [[nodiscard]] bool UsesAdaptiveSampling() const
        {
            return sampling.adaptiveSparseSamplingEnabled &&
                sampling.adaptiveStrength > 0.f &&
                sampling.maximumSampleCount >
                    sampling.minimumSampleCount;
        }

        [[nodiscard]] bool RequiresMotionVectors() const
        {
            return HasActiveConsumer() &&
                (UsesAdaptiveSampling() ||
                    reconstruction.temporalEnabled);
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
        uint32_t knownInactiveLightingSources = 0u;
    };

    // Matches the reflected row-major layout in the pinned XeGTAO 1.30
    // adapter shaders. Keep this separate from UVSR's general visibility
    // constants so Reference never pays for or binds the candidate buffer.
    struct alignas(16) XeGtaoConstants
    {
        dm::int2 viewportSize;
        dm::float2 viewportPixelSize;
        dm::float2 depthUnpackConstants;
        dm::float2 cameraTanHalfFov;
        dm::float2 ndcToViewMultiply;
        dm::float2 ndcToViewAdd;
        dm::float2 ndcToViewMultiplyPixelSize;
        float effectRadius;
        float effectFalloffRange;
        float radiusMultiplier;
        float padding0;
        float finalValuePower;
        float denoiseBlurBeta;
        float sampleDistributionPower;
        float thinOccluderCompensation;
        float depthMipSamplingOffset;
        int32_t noiseIndex;
        dm::uint2 viewportOrigin;
        uint32_t reverseDepth;
        uint32_t padding1;
        dm::float4x4 worldToView;
    };
    static_assert(sizeof(XeGtaoConstants) == 176u);

    struct ScreenSpaceVisibilityTimings
    {
        float depthHierarchyMs = 0.f;
        float samplingMs = 0.f;
        float firstTraceMs = 0.f;
        float laterTraceMs = 0.f;
        std::array<float, MaxIndirectDiffuseBounceCount - 1u>
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
        uint64_t adaptiveFeedbackBytes = 0u;
        uint64_t temporalAmbientHistoryBytes = 0u;
        uint64_t temporalIndirectHistoryBytes = 0u;
        uint64_t temporalDepthHistoryBytes = 0u;
        uint64_t temporalNormalHistoryBytes = 0u;
        uint64_t depthHierarchyBytes = 0u;
        uint64_t packedFastNoiseBytes = 0u;
        uint64_t packedEdgeMetadataBytes = 0u;
        uint64_t activisionWorkingBytes = 0u;
        uint64_t xeGtaoWorkingAoBytes = 0u;
        uint64_t xeGtaoEdgeBytes = 0u;
        uint64_t xeGtaoHilbertLutBytes = 0u;
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
        nvrhi::BufferHandle m_XeGtaoConstantBuffer;
        nvrhi::SamplerHandle m_PointClampSampler;

        // Final dimension selects fixed/adaptive sparse sampling.
        std::array<std::array<std::array<Pipeline, 2>,
            c_ConsumerVariantCount>, ImplementedVisibilityEstimatorCount>
            m_Sampling;
        std::array<std::array<std::array<Pipeline, 2>, 2>,
            ImplementedVisibilityEstimatorCount> m_MultiBounceFirstSampling;
        std::array<std::array<std::array<Pipeline, 2>, 2>,
            ImplementedVisibilityEstimatorCount> m_IndirectBounceSampling;
        std::array<Pipeline, c_ConsumerVariantCount> m_Temporal;
        std::array<std::array<Pipeline, c_ConsumerVariantCount>, 2> m_Filter;
        Pipeline m_DepthHierarchy;
        Pipeline m_Composite;
        std::unordered_map<uint64_t, Pipeline> m_AdvancedPipelines;
        std::unordered_map<uint64_t, nvrhi::BindingSetHandle>
            m_AdvancedBindingSets;

        dm::uint2 m_FullSize = dm::uint2::zero();
        dm::uint2 m_SamplingSize = dm::uint2::zero();
        uint32_t m_ResolutionScale = 1u;
        bool m_AmbientResourcesEnabled = false;
        bool m_IndirectDiffuseResourcesEnabled = false;
        bool m_MultipleBounceResourcesEnabled = false;
        bool m_AdaptiveResourcesEnabled = false;
        bool m_TemporalResourcesEnabled = false;
        bool m_PostProcessResourcesEnabled = false;
        bool m_DepthHierarchyResourcesEnabled = false;
        bool m_PackedFastResourcesEnabled = false;
        bool m_PackedEdgeResourcesEnabled = false;
        bool m_FinalAmbientResourcesEnabled = false;
        bool m_ActivisionResourcesEnabled = false;
        bool m_XeGtaoResourcesEnabled = false;
        bool m_XeGtaoHilbertLutResourcesEnabled = false;

        nvrhi::TextureHandle m_RawAmbientVisibility;
        nvrhi::TextureHandle m_RawIndirectDiffuse[2];
        nvrhi::TextureHandle m_CumulativeIndirectDiffuse;
        nvrhi::TextureHandle m_AdaptiveFeedback[2];
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
        nvrhi::TextureHandle m_ActivisionCurrentDepth;
        nvrhi::TextureHandle m_ActivisionSpatialAmbient;
        nvrhi::TextureHandle m_XeGtaoWorkingAo;
        nvrhi::TextureHandle m_XeGtaoEdges;
        nvrhi::TextureHandle m_XeGtaoHilbertLut;

        nvrhi::TextureHandle m_DummyAmbientVisibility;
        nvrhi::TextureHandle m_DummyIndirectDiffuse;
        nvrhi::TextureHandle m_DummyFeedback;
        nvrhi::TextureHandle m_DummyAmbientOutput;
        nvrhi::TextureHandle m_DummyIndirectOutput;
        nvrhi::TextureHandle m_DummyFeedbackOutput;

        // [estimator][consumer][fixed/adaptive][feedback parity]
        std::array<std::array<std::array<std::array<
            nvrhi::BindingSetHandle, 2>, 2>, c_ConsumerVariantCount>,
            ImplementedVisibilityEstimatorCount> m_SamplingBindingSets;
        // [estimator][AO variant][fixed/adaptive][feedback parity]
        std::array<std::array<std::array<std::array<
            nvrhi::BindingSetHandle, 2>, 2>, 2>,
            ImplementedVisibilityEstimatorCount> m_MultiBounceFirstBindingSets;
        // [estimator][initialize/add][fixed/adaptive][bounce rotation][feedback parity]
        std::array<std::array<std::array<std::array<std::array<
            nvrhi::BindingSetHandle, 2>, 3>, 2>, 2>,
            ImplementedVisibilityEstimatorCount> m_IndirectBounceBindingSets;
        // [consumer][single/multiple-bounce source][history write parity]
        std::array<std::array<std::array<nvrhi::BindingSetHandle, 2>, 2>,
            c_ConsumerVariantCount> m_TemporalBindingSets;
        // Sources 0/1 are the single/multiple-bounce raw results; 2/3 select
        // the temporal ping-pong output written during the current frame.
        std::array<std::array<std::array<nvrhi::BindingSetHandle, 4>,
            c_ConsumerVariantCount>, 2> m_FilterBindingSets;
        nvrhi::BindingSetHandle m_DepthHierarchyBindingSet;
        // Raw single, raw cumulative, and the two temporal ping-pong sources.
        std::array<nvrhi::BindingSetHandle, 4> m_CompositeBindingSets;

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
        std::vector<uint16_t> m_XeGtaoHilbertUpload;
        bool m_SamplingNoiseUploaded = false;
        bool m_PackedFastNoiseUploaded = false;
        bool m_XeGtaoHilbertUploaded = false;
        bool m_HistoryValid = false;
        bool m_FeedbackValid = false;
        bool m_HistoryConfigurationInitialized = false;
        uint64_t m_HistoryConfigurationKey = 0u;
        bool m_HistoryEstimatorInitialized = false;
        VisibilityEstimator m_HistoryEstimator =
            VisibilityEstimator::UniformProjectedAngle;
        uint32_t m_HistoryIndex = 0u;
        uint32_t m_FeedbackIndex = 0u;
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
            bool adaptiveEnabled,
            bool temporalEnabled,
            bool postProcessEnabled,
            bool depthHierarchyEnabled,
            bool finalAmbientEnabled,
            bool packedFastEnabled,
            bool packedEdgesEnabled,
            bool activisionEnabled,
            bool xeGtaoEnabled,
            bool xeGtaoHilbertLutEnabled);
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
