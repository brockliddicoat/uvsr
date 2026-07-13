#pragma once

#include "lighting_contribution_shared.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class ICompositeView;
    class ShaderFactory;
}

namespace uvsr
{
    inline constexpr uint32_t MaxIndirectDiffuseBounceCount = 4;
    inline constexpr uint32_t ImplementedVisibilityEstimatorCount = 2;
    inline constexpr uint32_t VisibilitySlicePermutationCount = 2;
    // Kept bit-identical to lighting_contribution.hlsli so CPU scene systems
    // can feed conservative source-availability facts into the shared shader
    // gate. Unknown sources are represented by a clear bit and stay active.
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
        PaperAngular,
        GTUniform,
        // Reserved until the cosine-weighted CDF, sector interpretation,
        // receiver weighting, and normalization pass the uniform estimator's
        // complete promotion suite. It is intentionally not exposed in UI.
        GTCosine
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
        AmbientVisibility,
        IndirectDiffuse,
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
        GtEndpointOrder
    };

    struct SharedSamplingSettings
    {
        // First-bounce stochastic depth fetches per pixel. Samples are divided
        // between the two ordered horizon directions, with the odd sample
        // alternating sides across the spatiotemporal phase sequence. Later
        // GI bounces progressively reduce this budget toward eight samples.
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
        // One bounce preserves the original screen-space GI behavior. Extra
        // bounces repeat the finite current-frame transport solve with a
        // progressive sample budget; keep the product limit small because
        // every bounce still incurs a full-resolution traversal dispatch.
        uint32_t bounceCount = 1;
        // Maximum exposed scene-linear radiance that all analytically rejected
        // pieces of a higher-order bounce may add before tone mapping. Zero
        // keeps exact-zero exits only. The gate accounts for GI Intensity and
        // uses conservative unit bounds for receiver reflectance/material AO.
        float minimumBounceContribution = 0.001f;
        float intensity = 4.0f;
        bool includeEmissive = true;
        float emissiveGain = 4.0f;
    };

    struct VisibilityDebugSettings
    {
        ScreenSpaceVisibilityDebugMode mode = ScreenSpaceVisibilityDebugMode::FinalComposite;
        bool freezeSamplingPhase = false;
        // Uses a dedicated later-bounce permutation so production traversal
        // pays no wave-reduction or atomic-counter cost.
        bool collectHigherBounceReceiverStatistics = false;
        // One selects the production static specialization. Values above one
        // select the separate general developer/HQ slice permutation.
        uint32_t developerSliceCount = 1u;
        SectorHitCriterion sectorHitCriterion = SectorHitCriterion::Round;
    };

    struct ScreenSpaceVisibilitySettings
    {
        bool enabled = true;
        ScreenSpaceVisibilityQuality quality = ScreenSpaceVisibilityQuality::Medium;
        VisibilityEstimator estimator = VisibilityEstimator::PaperAngular;
        SharedSamplingSettings sampling;
        AmbientOcclusionSettings ambientOcclusion;
        IndirectDiffuseSettings indirectDiffuse;
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
        nvrhi::ITexture* sourceRadiance = nullptr;
        nvrhi::ITexture* gbufferDiffuse = nullptr;
        nvrhi::ITexture* gbufferSpecular = nullptr;
        nvrhi::ITexture* gbufferEmissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;
        nvrhi::ITexture* baseLighting = nullptr;
        nvrhi::ITexture* output = nullptr;
        // Scene, clustering, residency, or future lighting systems may mark a
        // source class inactive only when absence is proven for this frame.
        uint32_t knownInactiveLightingSources = 0u;
    };

    struct ScreenSpaceVisibilityTimings
    {
        float depthHierarchyMs = 0.f;
        float samplingMs = 0.f;
        float compositionMs = 0.f;
        uint32_t higherBounceEligibleReceiverCount = 0u;
        uint32_t higherBounceRejectedReceiverCount = 0u;
        // Logical texel payload for full-resolution visibility working targets.
        // This intentionally excludes API allocation alignment and traffic.
        uint64_t fullResolutionTextureBytes = 0u;
        uint64_t avoidedFullResolutionTextureBytes = 0u;

        [[nodiscard]] float CompleteEffectMs() const
        {
            return depthHierarchyMs + samplingMs + compositionMs;
        }

        [[nodiscard]] float HigherBounceReceiverRejectionPercent() const
        {
            return higherBounceEligibleReceiverCount > 0u
                ? 100.f * float(higherBounceRejectedReceiverCount) /
                    float(higherBounceEligibleReceiverCount)
                : 0.f;
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
            float exposureScale,
            uint32_t frameIndex);

        void Deactivate();
        void ResetBindingCache();

        [[nodiscard]] const ScreenSpaceVisibilityTimings& GetTimings() const { return m_Timings; }

    private:
        enum class Stage : uint32_t
        {
            DepthHierarchy,
            Sampling,
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
        // The upper eight slots retain the general developer multi-slice path;
        // the lower eight are statically specialized to one production slice.
        std::array<std::array<Pipeline, 8 * VisibilitySlicePermutationCount>,
            ImplementedVisibilityEstimatorCount>
            m_Sampling;
        // The packed receiver metadata needed by a later bounce has its own GI
        // specialization. One-bounce rendering therefore avoids even the
        // receiver source-alpha read and metadata output arithmetic.
        std::array<std::array<Pipeline, 2 * VisibilitySlicePermutationCount>,
            ImplementedVisibilityEstimatorCount>
            m_MultiBounceFirstSampling;
        // Later bounces use GI-only specializations. Bounce two initializes the
        // cumulative target; bounces three and four add to it in place. Keeping
        // both variants out of m_Sampling preserves the original one-bounce
        // register and SRV footprint.
        // Bits 0 and 1 select cumulative initialization and the opt-in
        // receiver-gate statistics permutation respectively.
        std::array<std::array<Pipeline, 4 * VisibilitySlicePermutationCount>,
            ImplementedVisibilityEstimatorCount>
            m_IndirectBounceSampling;
        Pipeline m_DepthHierarchy;
        Pipeline m_Composite;

        dm::uint2 m_FullSize = dm::uint2::zero();
        dm::uint2 m_SamplingSize = dm::uint2::zero();
        bool m_MultipleBounceResourcesEnabled = false;
        bool m_DepthHierarchyResourcesEnabled = false;
        bool m_AmbientResourcesEnabled = false;
        bool m_IndirectDiffuseResourcesEnabled = false;
        bool m_TraversalDebugResourcesEnabled = false;

        nvrhi::TextureHandle m_RawAmbientVisibility;
        nvrhi::TextureHandle m_DepthHierarchyTexture;
        // Higher-order transport uses an incremental frontier B(n)=T(B(n-1))
        // in two ping-pong textures and accumulates C(n)=C(n-1)+B(n) in a third
        // UAV. The extra two allocations exist only for multiple bounces.
        nvrhi::TextureHandle m_RawIndirectDiffuse[2];
        nvrhi::TextureHandle m_CumulativeIndirectDiffuse;
        nvrhi::TextureHandle m_RawDebug;
        // Binding layouts remain invariant across consumer permutations. These
        // pass-lifetime 1x1 UAV/SRV resources stand in for compiled-out targets.
        nvrhi::TextureHandle m_DummyAmbientVisibility;
        nvrhi::TextureHandle m_DummyIndirectDiffuse;
        nvrhi::TextureHandle m_DummyDebug;

        // Sampling resources are stable for the lifetime of this pass. Cache
        // the descriptor sets instead of allocating one per active bounce on
        // every frame. The three later-bounce slots encode the fixed resource
        // rotations for requested bounces two through four.
        std::array<std::array<nvrhi::BindingSetHandle,
            8 * VisibilitySlicePermutationCount>,
            ImplementedVisibilityEstimatorCount> m_SamplingBindingSets;
        std::array<std::array<nvrhi::BindingSetHandle,
            2 * VisibilitySlicePermutationCount>,
            ImplementedVisibilityEstimatorCount> m_MultiBounceFirstBindingSets;
        // Three fixed bounce rotations, followed by the same rotations with
        // the statistics UAV bound.
        std::array<std::array<nvrhi::BindingSetHandle,
            6 * VisibilitySlicePermutationCount>,
            ImplementedVisibilityEstimatorCount> m_IndirectBounceBindingSets;
        nvrhi::BindingSetHandle m_DepthHierarchyBindingSet;
        // The first slot reads the first-bounce frontier; the second reads the
        // cumulative resource used by a multi-bounce solve.
        std::array<nvrhi::BindingSetHandle, 2> m_CompositeBindingSets;

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerPending{};
        std::array<bool, static_cast<size_t>(Stage::Count)> m_TimerActive{};
        nvrhi::BufferHandle m_HigherBounceStatisticsBuffer;
        std::array<nvrhi::BufferHandle, c_TimerLatency>
            m_HigherBounceStatisticsReadbackBuffers;
        std::array<bool, c_TimerLatency> m_HigherBounceStatisticsPending{};
        uint32_t m_TimerFrame = 0;
        ScreenSpaceVisibilityTimings m_Timings;

        void CreatePipelines(const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory);
        void EnsureResources(
            dm::uint2 fullSize,
            bool ambientEnabled,
            bool indirectDiffuseEnabled,
            bool traversalDebugEnabled,
            bool multipleBouncesEnabled,
            bool depthHierarchyEnabled);
        void ReleaseResources();

        void BeginStage(nvrhi::ICommandList* commandList, Stage stage);
        void EndStage(nvrhi::ICommandList* commandList, Stage stage);
        void AdvanceTimers();
    };
}
