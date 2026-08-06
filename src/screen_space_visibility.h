#pragma once

#include "screen_space_visibility_defaults.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace donut::engine
{
    class CommonRenderPasses;
    class ICompositeView;
    class ShaderFactory;
    struct ShaderMacro;
}

namespace uvsr
{
    inline constexpr uint32_t ImplementedVisibilityEstimatorCount = 3;

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
        PermutatedWhiteNoise,
        VoidClusterBlueNoise
    };

    enum class VisibilityDebugView : uint32_t
    {
        FinalImage,
        AmbientVisibility,
        TracedIndirect,
        AppliedIndirect
    };

    enum class VisibilitySpatialFilter : uint32_t
    {
        JointBilateral,
        GaussianJointBilateral
    };

    enum class VisibilityReconstructionMode : uint32_t
    {
        Standard,
        PackedDepthNormal,
        PackedSlopeAdjustedDepthNormal,
        PackedControlledLeakage
    };

    [[nodiscard]] constexpr bool IsPackedVisibilityReconstruction(
        VisibilityReconstructionMode mode)
    {
        return mode != VisibilityReconstructionMode::Standard;
    }

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
        VisibilityScalarBufferPrecision ambient =
            VisibilityScalarBufferPrecision::Float16;
        VisibilityVectorBufferPrecision indirect =
            VisibilityVectorBufferPrecision::Rgba16Float;
    };

    void ApplyVisibilityBufferPrecisionPreset(
        VisibilityBufferPrecisionSettings& settings,
        bool use16BitAo,
        bool use16BitGi);

    struct SharedSamplingSettings
    {
        uint32_t maximumSampleCount = 20;
        float radius = 3.0f;
        float thickness = 0.5f;
        float stepDistributionExponent = 2.0f;
        VisibilitySampleScheduler scheduler =
            VisibilitySampleScheduler::VoidClusterBlueNoise;
    };

    struct AmbientOcclusionSettings
    {
        bool enabled = true;
        float strength = 1.0f;
    };

    struct IndirectDiffuseSettings
    {
        bool enabled = true;
        float intensity = ScreenSpaceIndirectDiffuseReferenceIntensity;
    };

    struct VisibilityReconstructionSettings
    {
        VisibilityReconstructionMode mode =
            VisibilityReconstructionMode::Standard;
        bool spatialEnabled = false;
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
        VisibilityBufferPrecisionSettings bufferPrecision;
        VisibilityDebugView debugView = VisibilityDebugView::FinalImage;

        [[nodiscard]] bool HasActiveAmbientOcclusion() const
        {
            return ambientOcclusion.enabled && ambientOcclusion.strength > 0.f;
        }

        [[nodiscard]] bool HasActiveIndirectDiffuse() const
        {
            return indirectDiffuse.enabled && indirectDiffuse.intensity > 0.f;
        }

        [[nodiscard]] bool HasActiveConsumer() const
        {
            return enabled &&
                (HasActiveAmbientOcclusion() || HasActiveIndirectDiffuse());
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

    struct ScreenSpaceVisibilityInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* sourceRadiance = nullptr;
        nvrhi::ITexture* gbufferDiffuse = nullptr;
        nvrhi::ITexture* gbufferSpecular = nullptr;
        nvrhi::ITexture* gbufferEmissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;
        nvrhi::ITexture* skyVisibility = nullptr;
        bool applySkyVisibilityToDiffuseIbl = false;
        bool applySkyVisibilityToSpecularIbl = false;
        nvrhi::ITexture* diffuseEnvironment = nullptr;
        float diffuseEnvironmentScale = 1.f;
        uint32_t diffuseEnvironmentArrayIndex = 0u;
        nvrhi::ITexture* specularEnvironment = nullptr;
        nvrhi::ITexture* environmentBrdf = nullptr;
        float specularEnvironmentScale = 1.f;
        float specularEnvironmentMipLevels = 0.f;
        uint32_t specularEnvironmentArrayIndex = 0u;
        uint32_t lightingDebugView = 0u;
        nvrhi::ITexture* baseLighting = nullptr;
        nvrhi::ITexture* output = nullptr;
    };

    struct ScreenSpaceVisibilityTimings
    {
        float firstTraceMs = 0.f;
        float reconstructionMs = 0.f;
        float compositionMs = 0.f;
        float effectEnvelopeMs = 0.f;

        // Exact logical texel arithmetic, excluding API alignment/residency.
        uint64_t outputTextureBytes = 0u;
        uint64_t workingTextureBytes = 0u;
        uint64_t schedulerResourceBytes = 0u;
        uint32_t activeSrvCount = 0u;
        uint32_t activeUavCount = 0u;
        uint32_t activeDispatchCount = 0u;
        bool active = false;
        bool available = false;

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
            std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
            const std::vector<uint16_t>* preparedVoidClusterNoise = nullptr,
            bool deferPipelineCreation = false);

        [[nodiscard]] bool PreparePipelinesStep();
        [[nodiscard]] bool ArePipelinesReady() const
        {
            return m_PipelinesReady;
        }

        void Render(
            nvrhi::ICommandList* commandList,
            const ScreenSpaceVisibilitySettings& settings,
            const donut::engine::ICompositeView& compositeView,
            const ScreenSpaceVisibilityInputs& inputs,
            uint32_t frameIndex);

        void Deactivate();
        void ResetBindingCache();

        [[nodiscard]] const ScreenSpaceVisibilityTimings& GetTimings() const
        {
            return m_Timings;
        }

    private:
        enum class Stage : uint32_t
        {
            FirstTrace,
            Reconstruction,
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
        std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;
        nvrhi::BufferHandle m_ConstantBuffer;

        std::array<Pipeline, c_ConsumerVariantCount> m_Filter;
        Pipeline m_Composite;
        std::unordered_map<uint64_t, Pipeline> m_AdvancedPipelines;
        std::unordered_map<uint64_t, nvrhi::BindingSetHandle>
            m_AdvancedBindingSets;
        std::array<nvrhi::ITexture*, 13> m_BoundInputTextures{};

        dm::uint2 m_FullSize = dm::uint2::zero();
        dm::uint2 m_SamplingSize = dm::uint2::zero();
        uint32_t m_ResolutionScale = 1u;
        bool m_AmbientResourcesEnabled = false;
        bool m_IndirectDiffuseResourcesEnabled = false;
        bool m_PostProcessResourcesEnabled = false;
        bool m_PackedEdgeResourcesEnabled = false;
        uint64_t m_BufferPrecisionConfigurationKey = 0u;

        nvrhi::TextureHandle m_RawAmbientVisibility;
        nvrhi::TextureHandle m_RawIndirectDiffuse;
        nvrhi::TextureHandle m_FinalAmbientVisibility;
        nvrhi::TextureHandle m_FinalIndirectDiffuse;
        nvrhi::TextureHandle m_VoidClusterNoiseTexture;
        nvrhi::TextureHandle m_PackedEdgesTexture;
        nvrhi::TextureHandle m_DummyAmbientVisibility;
        nvrhi::TextureHandle m_DummyIndirectDiffuse;

        std::array<nvrhi::BindingSetHandle, c_ConsumerVariantCount>
            m_FilterBindingSets;
        nvrhi::BindingSetHandle m_CompositeBindingSet;

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            static_cast<size_t>(Stage::Count)> m_TimerPending{};
        std::array<bool, static_cast<size_t>(Stage::Count)> m_TimerActive{};

        std::vector<uint16_t> m_VoidClusterNoiseUpload;
        bool m_VoidClusterNoiseUploaded = false;
        uint32_t m_TimerFrame = 0u;
        bool m_TimerFrameWritable = true;
        ScreenSpaceVisibilityTimings m_Timings;
        uint32_t m_PipelinePreparationStep = 0u;
        bool m_PipelinesReady = false;

        void EnsureResources(
            dm::uint2 fullSize,
            uint32_t resolutionScale,
            bool ambientEnabled,
            bool indirectDiffuseEnabled,
            bool postProcessEnabled,
            bool packedEdgesEnabled,
            const VisibilityBufferPrecisionSettings& bufferPrecision);
        void ReleaseResources();
        void UploadVoidClusterNoise(nvrhi::ICommandList* commandList);
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
