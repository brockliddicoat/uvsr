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
    class IView;
    class ShaderFactory;
}

namespace uvsr
{
    inline constexpr uint32_t MaxIndirectDiffuseBounceCount = 4;
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
        // Scene, clustering, residency, or future lighting systems may mark a
        // source class inactive only when absence is proven for this frame.
        uint32_t knownInactiveLightingSources = 0u;
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
            float exposureScale,
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
        // The packed receiver metadata needed by a later bounce has its own GI
        // specialization. One-bounce rendering therefore avoids even the
        // receiver source-alpha read and metadata output arithmetic.
        std::array<Pipeline, 2> m_MultiBounceFirstSampling;
        // Later bounces use GI-only specializations. Bounce two initializes the
        // cumulative target; bounces three and four add to it in place. Keeping
        // both variants out of m_Sampling preserves the original one-bounce
        // register and SRV footprint.
        std::array<Pipeline, 2> m_IndirectBounceSampling;
        Pipeline m_DepthHierarchy;
        std::array<Pipeline, 4> m_Temporal;
        std::array<Pipeline, 4> m_Denoise;
        Pipeline m_Composite;

        dm::uint2 m_FullSize = dm::uint2::zero();
        dm::uint2 m_SamplingSize = dm::uint2::zero();
        bool m_TemporalResourcesEnabled = false;
        bool m_MultipleBounceResourcesEnabled = false;
        bool m_DepthHierarchyResourcesEnabled = false;

        nvrhi::TextureHandle m_RawAmbientVisibility;
        nvrhi::TextureHandle m_DepthHierarchyTexture;
        // Higher-order transport uses an incremental frontier B(n)=T(B(n-1))
        // in two ping-pong textures and accumulates C(n)=C(n-1)+B(n) in a third
        // UAV. The extra two allocations exist only for multiple bounces.
        nvrhi::TextureHandle m_RawIndirectDiffuse[2];
        nvrhi::TextureHandle m_CumulativeIndirectDiffuse;
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

        // Sampling resources are stable for the lifetime of this pass. Cache
        // the descriptor sets instead of allocating one per active bounce on
        // every frame. The three later-bounce slots encode the fixed resource
        // rotations for requested bounces two through four.
        std::array<nvrhi::BindingSetHandle, 8> m_SamplingBindingSets;
        std::array<nvrhi::BindingSetHandle, 2> m_MultiBounceFirstBindingSets;
        std::array<nvrhi::BindingSetHandle, 3> m_IndirectBounceBindingSets;
        nvrhi::BindingSetHandle m_DepthHierarchyBindingSet;
        // Finite combinations of consumer specialization, history direction,
        // bounce source, and filter source avoid rebuilding full-resolution
        // descriptor sets every frame.
        std::array<nvrhi::BindingSetHandle, 16> m_TemporalBindingSets;
        std::array<nvrhi::BindingSetHandle, 16> m_SpatialBindingSets;
        std::array<nvrhi::BindingSetHandle, 8> m_CompositeBindingSets;

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
            bool temporalEnabled,
            bool multipleBouncesEnabled,
            bool depthHierarchyEnabled);
        void ReleaseResources();
        bool UpdateHistoryValidity(
            const ScreenSpaceVisibilitySettings& settings,
            const donut::engine::IView& view,
            float exposureScale);
        static uint64_t ComputeHistorySignature(
            const ScreenSpaceVisibilitySettings& settings,
            float exposureScale);

        void BeginStage(nvrhi::ICommandList* commandList, Stage stage);
        void EndStage(nvrhi::ICommandList* commandList, Stage stage);
        void AdvanceTimers();
    };
}
