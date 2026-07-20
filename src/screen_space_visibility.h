#pragma once

#include "lighting_contribution_shared.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace donut::engine
{
    class ICompositeView;
    class ShaderFactory;
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
        // Fixed radial-sample budget per eligible tracing pixel on one
        // stochastic slice. The nested radial prefix preserves the locations
        // of samples already present when this count is increased.
        uint32_t sampleCount = 20;
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

        [[nodiscard]] bool RequiresMotionVectors() const
        {
            return HasActiveConsumer() &&
                reconstruction.temporalEnabled;
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

    struct ScreenSpaceVisibilityTimings
    {
        float depthHierarchyMs = 0.f;
        float samplingMs = 0.f;
        float temporalMs = 0.f;
        float filteringMs = 0.f;
        float compositionMs = 0.f;

        // Exact logical texel arithmetic, excluding API alignment/residency.
        uint64_t outputTextureBytes = 0u;
        uint64_t workingTextureBytes = 0u;
        uint64_t maskCacheBytes = 0u;
        uint64_t avoidedTextureBytes = 0u;
        // An estimate of duplicate R32 mask payload that the shared
        // register-local producer avoids when AO and GI are both active.
        uint64_t sharedMaskPayloadBytes = 0u;

        [[nodiscard]] float CompleteEffectMs() const
        {
            return depthHierarchyMs + samplingMs + temporalMs +
                filteringMs + compositionMs;
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

    private:
        enum class Stage : uint32_t
        {
            DepthHierarchy,
            Sampling,
            Temporal,
            Filtering,
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
        static constexpr uint32_t c_ConsumerVariantCount = 3;

        nvrhi::DeviceHandle m_Device;
        nvrhi::BufferHandle m_ConstantBuffer;

        std::array<std::array<Pipeline,
            c_ConsumerVariantCount>, ImplementedVisibilityEstimatorCount>
            m_Sampling;
        std::array<std::array<Pipeline, 2>,
            ImplementedVisibilityEstimatorCount> m_MultiBounceFirstSampling;
        std::array<std::array<Pipeline, 2>,
            ImplementedVisibilityEstimatorCount> m_IndirectBounceSampling;
        std::array<Pipeline, c_ConsumerVariantCount> m_Temporal;
        std::array<std::array<Pipeline, c_ConsumerVariantCount>, 2> m_Filter;
        Pipeline m_DepthHierarchy;
        Pipeline m_Composite;

        dm::uint2 m_FullSize = dm::uint2::zero();
        dm::uint2 m_SamplingSize = dm::uint2::zero();
        uint32_t m_ResolutionScale = 1u;
        bool m_AmbientResourcesEnabled = false;
        bool m_IndirectDiffuseResourcesEnabled = false;
        bool m_MultipleBounceResourcesEnabled = false;
        bool m_TemporalResourcesEnabled = false;
        bool m_PostProcessResourcesEnabled = false;
        bool m_DepthHierarchyResourcesEnabled = false;

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

        nvrhi::TextureHandle m_DummyAmbientVisibility;
        nvrhi::TextureHandle m_DummyIndirectDiffuse;
        nvrhi::TextureHandle m_DummyRgba;
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
        std::array<bool, static_cast<size_t>(Stage::Count)> m_TimerActive{};

        std::vector<uint16_t> m_BlueNoiseUpload;
        std::vector<uint8_t> m_FilterAdaptedNoiseUpload;
        bool m_SamplingNoiseUploaded = false;
        bool m_HistoryValid = false;
        bool m_HistoryConfigurationInitialized = false;
        uint64_t m_HistoryConfigurationKey = 0u;
        bool m_HistoryEstimatorInitialized = false;
        VisibilityEstimator m_HistoryEstimator =
            VisibilityEstimator::UniformProjectedAngle;
        uint32_t m_HistoryIndex = 0u;
        uint32_t m_TimerFrame = 0u;
        ScreenSpaceVisibilityTimings m_Timings;

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
            bool depthHierarchyEnabled);
        void ReleaseResources();
        void UploadSamplingNoise(nvrhi::ICommandList* commandList);

        void BeginStage(nvrhi::ICommandList* commandList, Stage stage);
        void EndStage(nvrhi::ICommandList* commandList, Stage stage);
        void AdvanceTimers();
    };
}
