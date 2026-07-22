#include "screen_space_visibility.h"
#include "visibility_blue_noise.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "screen_space_visibility_cb.h"

namespace
{
    constexpr uint32_t kThreadGroupSize = 8u;
    constexpr uint32_t kHistoryContractVersion = 2u;

    constexpr bool IsFullTextureViewport(
        float originX,
        float originY,
        float viewportWidth,
        float viewportHeight,
        uint32_t textureWidth,
        uint32_t textureHeight)
    {
        return originX == 0.f && originY == 0.f &&
            viewportWidth == static_cast<float>(textureWidth) &&
            viewportHeight == static_cast<float>(textureHeight);
    }

    static_assert(IsFullTextureViewport(
        0.f, 0.f, 1920.f, 1080.f, 1920u, 1080u));
    static_assert(!IsFullTextureViewport(
        1.f, 0.f, 1920.f, 1080.f, 1920u, 1080u));
    static_assert(!IsFullTextureViewport(
        0.f, 0.f, 1919.f, 1080.f, 1920u, 1080u));
    static_assert(!IsFullTextureViewport(
        0.f, 0.f, 1920.f, 1079.f, 1920u, 1080u));

    uint32_t GetResolutionScale(uvsr::VisibilityResolution resolution)
    {
        switch (resolution)
        {
        case uvsr::VisibilityResolution::Half: return 2u;
        case uvsr::VisibilityResolution::Quarter: return 4u;
        default: return 1u;
        }
    }

    uint32_t GetConsumerVariant(bool ambientEnabled, bool indirectEnabled)
    {
        assert(ambientEnabled || indirectEnabled);
        if (ambientEnabled)
            return indirectEnabled ? 2u : 0u;
        return 1u;
    }

    bool IsAmbientPowerEnabled(float power)
    {
        const float clampedPower = std::clamp(power, 0.1f, 4.0f);
        return std::abs(clampedPower - 1.0f) > 1e-6f;
    }

    uint32_t GetIndirectBounceSampleCount(
        uint32_t primarySampleCount,
        uint32_t)
    {
        return std::clamp(primarySampleCount, 1u, 64u);
    }

    uint64_t TextureBytes(uint2 size, uint32_t bytesPerPixel)
    {
        return uint64_t(size.x) * uint64_t(size.y) * bytesPerPixel;
    }

    uint64_t DepthHierarchyBytes(
        uint2 fullSize,
        uint32_t bytesPerPixel = 2u)
    {
        uint32_t width = std::max((fullSize.x + 15u) & ~15u, 16u);
        uint32_t height = std::max((fullSize.y + 15u) & ~15u, 16u);
        uint64_t bytes = 0u;
        for (uint32_t mip = 0u; mip < 5u; ++mip)
        {
            bytes += uint64_t(std::max(width >> mip, 1u)) *
                uint64_t(std::max(height >> mip, 1u)) *
                    bytesPerPixel;
        }
        return bytes;
    }

    uvsr::VisibilityPerformanceConsumer GetPerformanceConsumer(
        bool ambientEnabled,
        bool indirectEnabled)
    {
        if (ambientEnabled)
        {
            return indirectEnabled
                ? uvsr::VisibilityPerformanceConsumer::
                    AmbientOcclusionAndIndirectDiffuse
                : uvsr::VisibilityPerformanceConsumer::AmbientOcclusion;
        }
        return uvsr::VisibilityPerformanceConsumer::IndirectDiffuse;
    }

    uvsr::VisibilityPerformanceEstimator GetPerformanceEstimator(
        uvsr::VisibilityEstimator estimator)
    {
        switch (estimator)
        {
        case uvsr::VisibilityEstimator::UniformProjectedAngle:
            return uvsr::VisibilityPerformanceEstimator::
                UniformProjectedAngle;
        case uvsr::VisibilityEstimator::CosineWeightedSolidAngle:
            return uvsr::VisibilityPerformanceEstimator::
                CosineWeightedSolidAngle;
        default:
            return uvsr::VisibilityPerformanceEstimator::UniformSolidAngle;
        }
    }

    uvsr::VisibilityPerformanceResolution GetPerformanceResolution(
        uvsr::VisibilityResolution resolution)
    {
        switch (resolution)
        {
        case uvsr::VisibilityResolution::Half:
            return uvsr::VisibilityPerformanceResolution::Half;
        case uvsr::VisibilityResolution::Quarter:
            return uvsr::VisibilityPerformanceResolution::Quarter;
        default:
            return uvsr::VisibilityPerformanceResolution::Full;
        }
    }

    uvsr::VisibilityPerformanceScheduler GetPerformanceScheduler(
        uvsr::VisibilitySampleScheduler scheduler)
    {
        switch (scheduler)
        {
        case uvsr::VisibilitySampleScheduler::ToroidalBlueNoiseRankField:
            return uvsr::VisibilityPerformanceScheduler::
                ToroidalBlueNoiseRankField;
        case uvsr::VisibilitySampleScheduler::
            FilterAdaptedSpatiotemporalRankField:
            return uvsr::VisibilityPerformanceScheduler::
                FilterAdaptedSpatiotemporalRankField;
        default:
            return uvsr::VisibilityPerformanceScheduler::IndependentHash;
        }
    }

    uint64_t AdvancedPipelineKey(uint64_t permutationKey, uint32_t stage)
    {
        uint64_t key = permutationKey;
        key ^= uint64_t(stage) + 0x9e3779b97f4a7c15ull +
            (key << 6u) + (key >> 2u);
        return key;
    }

    void AppendHistoryConfigurationValue(uint64_t& key, uint64_t value)
    {
        // A process-local change detector, not a serialized identifier. The
        // boost-style combiner is sufficient to prevent stale temporal values
        // crossing materially different traversal settings.
        key ^= value + 0x9e3779b97f4a7c15ull + (key << 6u) + (key >> 2u);
    }

    uint64_t BuildHistoryConfigurationKey(
        const ScreenSpaceVisibilityConstants& constants,
        uint32_t activeBounceCount)
    {
        uint64_t key = 0xcbf29ce484222325ull;
        const auto appendFloat = [&key](float value)
        {
            AppendHistoryConfigurationValue(
                key, uint64_t(std::hash<float>{}(value)));
        };
        const auto appendUint = [&key](uint32_t value)
        {
            AppendHistoryConfigurationValue(key, uint64_t(value));
        };

        appendUint(kHistoryContractVersion);
        appendFloat(constants.radiusWorld);
        appendFloat(constants.thicknessWorld);
        appendFloat(constants.stepDistributionExponent);
        appendUint(constants.maximumSampleCount);
        appendUint(constants.sampleScheduler);
        appendUint(constants.useDepthHierarchy);
        appendUint(constants.resolutionScale);
        appendUint(constants.packedEdgeMode);
        if (constants.enableIndirectDiffuse != 0u)
        {
            appendFloat(constants.emissiveGain);
            appendUint(constants.includeEmissive);
            appendUint(constants.knownInactiveLightingSources);
            appendUint(activeBounceCount);
            if (activeBounceCount > 1u)
            {
                appendFloat(constants.minimumBounceContribution);
                if (constants.minimumBounceContribution > 0.f)
                {
                    // Intensity and exposure participate in the conservative
                    // higher-bounce contribution gate even though first-bounce
                    // composition applies intensity later.
                    appendFloat(constants.indirectDiffuseIntensity);
                    appendFloat(constants.lightingExposureScale);
                }
            }
        }
        return key;
    }
}

namespace uvsr
{
    void ApplyVisibilityBufferPrecisionPreset(
        VisibilityBufferPrecisionSettings& settings,
        bool use16BitAo,
        bool use16BitGi)
    {
        const VisibilityScalarBufferPrecision ambientPrecision =
            use16BitAo
                ? VisibilityScalarBufferPrecision::Float16
                : VisibilityScalarBufferPrecision::Float32;
        const VisibilityVectorBufferPrecision indirectPrecision =
            use16BitGi
                ? VisibilityVectorBufferPrecision::Rgba16Float
                : VisibilityVectorBufferPrecision::Rgba32Float;

        settings.rawAmbient = ambientPrecision;
        settings.temporalAmbient = ambientPrecision;
        settings.finalAmbient = ambientPrecision;
        settings.depthHierarchy = ambientPrecision;
        settings.rawIndirect = indirectPrecision;
        settings.cumulativeIndirect = indirectPrecision;
        settings.temporalIndirect = indirectPrecision;
        settings.finalIndirect = indirectPrecision;
        // The temporal-depth contract remains full precision in every preset.
        settings.temporalDepth =
            VisibilityScalarBufferPrecision::Float32;
    }

    VisibilityPerformanceProfileConfiguration
        GetEffectiveVisibilityPerformanceConfiguration(
            const ScreenSpaceVisibilitySettings& settings)
    {
        if (settings.performanceProfile ==
            VisibilityPerformanceProfile::GenericFallback)
        {
            return settings.performance.configuration;
        }
        return GetVisibilityPerformanceProfileConfiguration(
            settings.performanceProfile);
    }

    void ResetVisibilityComposableSettings(
        ScreenSpaceVisibilitySettings& settings,
        VisibilityPerformanceProfile profile)
    {
        const VisibilityBufferPrecisionSettings bufferPrecision =
            settings.performance.bufferPrecision;
        settings.performance = {};
        settings.performance.bufferPrecision = bufferPrecision;
        settings.performance.configuration =
            GetVisibilityPerformanceProfileConfiguration(profile);

        switch (profile)
        {
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdges2x2:
            settings.performance.packedEdgeMode =
                VisibilityPackedEdgeMode::Depth;
            break;
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesSlope2x2:
            settings.performance.packedEdgeMode =
                VisibilityPackedEdgeMode::SlopeAdjustedDepthAndNormal;
            break;
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesLeakage2x2:
            settings.performance.packedEdgeMode =
                VisibilityPackedEdgeMode::ControlledLeakage;
            break;
        default:
            break;
        }

    }

    void MakeVisibilityPerformanceComposable(
        ScreenSpaceVisibilitySettings& settings)
    {
        if (settings.performanceProfile ==
            VisibilityPerformanceProfile::GenericFallback)
        {
            return;
        }

        ResetVisibilityComposableSettings(
            settings, settings.performanceProfile);
        VisibilityPerformanceProfileConfiguration& configuration =
            settings.performance.configuration;
        configuration.profile =
            VisibilityPerformanceProfile::GenericFallback;
        configuration.name = "Custom Composite";
        configuration.optimizationClass =
            VisibilityOptimizationClass::Exact;
        configuration.implementationStatus =
            VisibilityImplementationStatus::Implemented;
        configuration.implementationNote =
            "Independent legacy UVSR optimization categories.";

        const bool bitmaskTrace =
            configuration.trace ==
                VisibilityTraceImplementation::LegacyGenericBitmask ||
            configuration.trace ==
                VisibilityTraceImplementation::FixedInterleavedBitmask;
        if (bitmaskTrace)
        {
            configuration.bindings =
                VisibilityBindingStrategy::MinimalConditional;
            configuration.benchmarkOnly = false;
            if (configuration.laterBounceSamples ==
                VisibilitySampleSpecialization::Runtime)
            {
                configuration.consumerRequirement =
                    configuration.application ==
                            VisibilityApplicationMode::
                                FusedResolveAndApplyExact ||
                        configuration.application ==
                            VisibilityApplicationMode::
                                FusedResolveAndApplyPackedEdges
                        ? VisibilityConsumerRequirement::
                            AmbientOcclusionOnly
                        : VisibilityConsumerRequirement::Any;
            }
            configuration.resolutionRequirement =
                configuration.application ==
                        VisibilityApplicationMode::
                            FusedResolveAndApplyExact ||
                    configuration.application ==
                        VisibilityApplicationMode::
                            FusedResolveAndApplyPackedEdges
                    ? VisibilityResolutionRequirement::Reduced
                    : VisibilityResolutionRequirement::Any;
        }
        settings.performanceProfile =
            VisibilityPerformanceProfile::GenericFallback;
    }

    uint64_t GetVisibilityRuntimeConfigurationKey(
        const ScreenSpaceVisibilitySettings& settings)
    {
        uint64_t key = 0xcbf29ce484222325ull;
        const auto append = [&key](uint64_t value)
        {
            for (uint32_t byte = 0u; byte < 8u; ++byte)
            {
                key ^= uint8_t(value >> (byte * 8u));
                key *= 0x100000001b3ull;
            }
        };
        const auto& performance = settings.performance;
        const auto& buffers = performance.bufferPrecision;
        append(static_cast<uint32_t>(buffers.rawAmbient));
        append(static_cast<uint32_t>(buffers.rawIndirect));
        append(static_cast<uint32_t>(buffers.cumulativeIndirect));
        append(static_cast<uint32_t>(buffers.temporalAmbient));
        append(static_cast<uint32_t>(buffers.temporalIndirect));
        append(static_cast<uint32_t>(buffers.temporalDepth));
        append(static_cast<uint32_t>(buffers.finalAmbient));
        append(static_cast<uint32_t>(buffers.finalIndirect));
        append(static_cast<uint32_t>(buffers.depthHierarchy));
        append(static_cast<uint32_t>(performance.packedEdgeMode));
        append(IsAmbientPowerEnabled(
            settings.ambientOcclusion.power) ? 1u : 0u);
        return key;
    }

    ScreenSpaceVisibilitySettings::ScreenSpaceVisibilitySettings()
    {
        ApplyScreenSpaceVisibilityQualityPreset(
            *this, ScreenSpaceVisibilityQuality::High);
    }

    void MarkScreenSpaceVisibilityQualityCustom(
        ScreenSpaceVisibilitySettings& settings)
    {
        if (settings.quality != ScreenSpaceVisibilityQuality::Custom)
            settings.qualityPresetOrigin = settings.quality;
        settings.quality = ScreenSpaceVisibilityQuality::Custom;
    }

    void ApplyScreenSpaceVisibilityQualityPreset(
        ScreenSpaceVisibilitySettings& settings,
        ScreenSpaceVisibilityQuality quality)
    {
        if (quality == ScreenSpaceVisibilityQuality::Custom)
        {
            MarkScreenSpaceVisibilityQualityCustom(settings);
            return;
        }

        settings.enabled = true;
        settings.estimator = VisibilityEstimator::UniformSolidAngle;
        settings.sampling.radius = 3.0f;
        settings.sampling.thickness = 0.5f;
        settings.sampling.stepDistributionExponent = 2.0f;
        settings.sampling.scheduler =
            VisibilitySampleScheduler::
                FilterAdaptedSpatiotemporalRankField;
        settings.ambientOcclusion.enabled = true;
        settings.ambientOcclusion.strength = 1.0f;
        settings.ambientOcclusion.power = 1.0f;
        settings.indirectDiffuse.enabled = true;
        settings.indirectDiffuse.limitBounces = true;
        settings.indirectDiffuse.bounceCount = 1u;
        settings.indirectDiffuse.minimumBounceContribution = 0.001f;
        settings.indirectDiffuse.intensity = 4.0f;
        settings.reconstruction.temporalEnabled = false;
        settings.reconstruction.temporalResponse = 0.35f;
        settings.reconstruction.spatialFilter =
            VisibilitySpatialFilter::GaussianJointBilateral;
        settings.reconstruction.spatialRadius = 4.0f;
        settings.reconstruction.spatialEnabled = false;
        settings.showIndirectDiffuseOnly = false;

        VisibilityPerformanceProfile implementationProfile =
            VisibilityPerformanceProfile::ExactFixed20;

        switch (quality)
        {
        case ScreenSpaceVisibilityQuality::Low:
            settings.resolution = VisibilityResolution::Quarter;
            settings.estimator =
                VisibilityEstimator::UniformProjectedAngle;
            settings.sampling.maximumSampleCount = 8u;
            settings.reconstruction.spatialEnabled = true;
            settings.reconstruction.spatialFilter =
                VisibilitySpatialFilter::JointBilateral;
            implementationProfile =
                VisibilityPerformanceProfile::ExactFixed8;
            break;
        case ScreenSpaceVisibilityQuality::Medium:
            settings.resolution = VisibilityResolution::Half;
            settings.sampling.maximumSampleCount = 8u;
            settings.reconstruction.spatialEnabled = true;
            settings.reconstruction.spatialFilter =
                VisibilitySpatialFilter::JointBilateral;
            implementationProfile =
                VisibilityPerformanceProfile::ExactFixed8;
            break;
        case ScreenSpaceVisibilityQuality::High:
            settings.resolution = VisibilityResolution::Full;
            settings.sampling.maximumSampleCount = 20u;
            implementationProfile =
                VisibilityPerformanceProfile::ExactFixed20;
            break;
        case ScreenSpaceVisibilityQuality::Ultra:
            settings.resolution = VisibilityResolution::Full;
            settings.sampling.maximumSampleCount = 48u;
            settings.indirectDiffuse.bounceCount = 2u;
            implementationProfile =
                VisibilityPerformanceProfile::ExactFixed48;
            break;
        default:
            break;
        }

        settings.performanceProfile = implementationProfile;
        ResetVisibilityComposableSettings(settings, implementationProfile);
        MakeVisibilityPerformanceComposable(settings);

        auto& configuration = settings.performance.configuration;
        configuration.noise = VisibilityNoiseDelivery::PackedCurrentFast;
        configuration.bindings =
            VisibilityBindingStrategy::MinimalConditional;
        configuration.benchmarkOnly = false;
        configuration.implementationStatus =
            VisibilityImplementationStatus::Implemented;
        configuration.implementationNote =
            "Composable product quality preset.";

        const bool reducedResolution =
            quality == ScreenSpaceVisibilityQuality::Low ||
            quality == ScreenSpaceVisibilityQuality::Medium;
        if (reducedResolution)
        {
            configuration.optimizationClass =
                VisibilityOptimizationClass::Exact;
            configuration.edgeStorage = VisibilityEdgeStorage::None;
            configuration.reconstruction =
                VisibilityReconstructionMode::Legacy;
            configuration.application =
                VisibilityApplicationMode::LegacySeparateComposition;
            configuration.consumerRequirement =
                VisibilityConsumerRequirement::Any;
            configuration.resolutionRequirement =
                VisibilityResolutionRequirement::Any;
            settings.performance.packedEdgeMode =
                VisibilityPackedEdgeMode::DepthAndNormal;
            ApplyVisibilityBufferPrecisionPreset(
                settings.performance.bufferPrecision, true, true);
        }
        else
        {
            configuration.optimizationClass =
                VisibilityOptimizationClass::Exact;
            configuration.edgeStorage = VisibilityEdgeStorage::None;
            configuration.reconstruction =
                VisibilityReconstructionMode::Legacy;
            configuration.application =
                VisibilityApplicationMode::LegacySeparateComposition;
            configuration.consumerRequirement =
                VisibilityConsumerRequirement::Any;
            configuration.resolutionRequirement =
                VisibilityResolutionRequirement::Any;
            settings.performance.packedEdgeMode =
                VisibilityPackedEdgeMode::DepthAndNormal;
            ApplyVisibilityBufferPrecisionPreset(
                settings.performance.bufferPrecision, false, false);
        }

        switch (quality)
        {
        case ScreenSpaceVisibilityQuality::Low:
            configuration.name = "Low";
            break;
        case ScreenSpaceVisibilityQuality::Medium:
            configuration.name = "Medium";
            break;
        case ScreenSpaceVisibilityQuality::High:
            configuration.name = "High";
            break;
        case ScreenSpaceVisibilityQuality::Ultra:
            configuration.name = "Ultra";
            break;
        default:
            break;
        }

        settings.qualityPresetOrigin = quality;
        settings.quality = quality;
    }

    bool MatchesScreenSpaceVisibilityQualityPreset(
        const ScreenSpaceVisibilitySettings& settings,
        ScreenSpaceVisibilityQuality quality)
    {
        if (quality == ScreenSpaceVisibilityQuality::Custom)
            return false;

        ScreenSpaceVisibilitySettings preset;
        ApplyScreenSpaceVisibilityQualityPreset(preset, quality);

        const auto configurationsMatch =
            [](const VisibilityPerformanceProfileConfiguration& left,
                const VisibilityPerformanceProfileConfiguration& right)
            {
                return
                    left.optimizationClass == right.optimizationClass &&
                    left.trace == right.trace &&
                    left.firstBounceSamples == right.firstBounceSamples &&
                    left.laterBounceSamples == right.laterBounceSamples &&
                    left.noise == right.noise &&
                    left.math == right.math &&
                    left.rawAoStorage == right.rawAoStorage &&
                    left.edgeStorage == right.edgeStorage &&
                    left.reconstruction == right.reconstruction &&
                    left.temporal == right.temporal &&
                    left.application == right.application &&
                    left.depth == right.depth &&
                    left.bindings == right.bindings &&
                    left.traversal == right.traversal &&
                    left.consumerRequirement ==
                        right.consumerRequirement &&
                    left.estimatorRequirement ==
                        right.estimatorRequirement &&
                    left.resolutionRequirement ==
                        right.resolutionRequirement &&
                    left.implementationStatus ==
                        right.implementationStatus &&
                    left.benchmarkOnly == right.benchmarkOnly &&
                    left.explicitHalfRoundtrip ==
                        right.explicitHalfRoundtrip;
            };
        const auto bufferPrecisionsMatch =
            [](const VisibilityBufferPrecisionSettings& left,
                const VisibilityBufferPrecisionSettings& right)
            {
                return left.rawAmbient == right.rawAmbient &&
                    left.rawIndirect == right.rawIndirect &&
                    left.cumulativeIndirect == right.cumulativeIndirect &&
                    left.temporalAmbient == right.temporalAmbient &&
                    left.temporalIndirect == right.temporalIndirect &&
                    left.temporalDepth == right.temporalDepth &&
                    left.finalAmbient == right.finalAmbient &&
                    left.finalIndirect == right.finalIndirect &&
                    left.depthHierarchy == right.depthHierarchy;
            };

        const VisibilityPerformanceProfileConfiguration actualConfiguration =
            GetEffectiveVisibilityPerformanceConfiguration(settings);
        const VisibilityPerformanceProfileConfiguration presetConfiguration =
            GetEffectiveVisibilityPerformanceConfiguration(preset);

        return settings.enabled == preset.enabled &&
            settings.estimator == preset.estimator &&
            settings.resolution == preset.resolution &&
            settings.sampling.maximumSampleCount ==
                preset.sampling.maximumSampleCount &&
            settings.sampling.radius == preset.sampling.radius &&
            settings.sampling.thickness == preset.sampling.thickness &&
            settings.sampling.stepDistributionExponent ==
                preset.sampling.stepDistributionExponent &&
            settings.sampling.scheduler == preset.sampling.scheduler &&
            settings.ambientOcclusion.enabled ==
                preset.ambientOcclusion.enabled &&
            settings.ambientOcclusion.strength ==
                preset.ambientOcclusion.strength &&
            settings.ambientOcclusion.power ==
                preset.ambientOcclusion.power &&
            settings.indirectDiffuse.enabled ==
                preset.indirectDiffuse.enabled &&
            settings.indirectDiffuse.limitBounces ==
                preset.indirectDiffuse.limitBounces &&
            settings.indirectDiffuse.bounceCount ==
                preset.indirectDiffuse.bounceCount &&
            settings.indirectDiffuse.minimumBounceContribution ==
                preset.indirectDiffuse.minimumBounceContribution &&
            settings.indirectDiffuse.intensity ==
                preset.indirectDiffuse.intensity &&
            settings.reconstruction.temporalEnabled ==
                preset.reconstruction.temporalEnabled &&
            settings.reconstruction.spatialEnabled ==
                preset.reconstruction.spatialEnabled &&
            settings.reconstruction.temporalResponse ==
                preset.reconstruction.temporalResponse &&
            settings.reconstruction.spatialFilter ==
                preset.reconstruction.spatialFilter &&
            settings.reconstruction.spatialRadius ==
                preset.reconstruction.spatialRadius &&
            settings.performance.packedEdgeMode ==
                preset.performance.packedEdgeMode &&
            settings.showIndirectDiffuseOnly ==
                preset.showIndirectDiffuseOnly &&
            configurationsMatch(
                actualConfiguration, presetConfiguration) &&
            bufferPrecisionsMatch(
                settings.performance.bufferPrecision,
                preset.performance.bufferPrecision);
    }

    void ReconcileScreenSpaceVisibilityQualityPreset(
        ScreenSpaceVisibilitySettings& settings)
    {
        ScreenSpaceVisibilityQuality origin =
            settings.quality == ScreenSpaceVisibilityQuality::Custom
                ? settings.qualityPresetOrigin
                : settings.quality;
        if (origin == ScreenSpaceVisibilityQuality::Custom)
            origin = ScreenSpaceVisibilityQuality::High;

        if (MatchesScreenSpaceVisibilityQualityPreset(settings, origin))
        {
            ApplyScreenSpaceVisibilityQualityPreset(settings, origin);
        }
        else
        {
            settings.qualityPresetOrigin = origin;
            settings.quality = ScreenSpaceVisibilityQuality::Custom;
        }
    }

    ScreenSpaceVisibilityPass::ScreenSpaceVisibilityPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::filesystem::path& filterAdaptedNoisePath)
        : m_Device(device)
        , m_ShaderFactory(shaderFactory)
    {
        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(ScreenSpaceVisibilityConstants);
        constantBufferDesc.debugName = "ScreenSpaceVisibility/Constants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDesc);

        nvrhi::BufferDesc continuationDesc;
        continuationDesc.byteSize = sizeof(uint32_t);
        continuationDesc.debugName =
            "ScreenSpaceVisibility/BounceContinuation";
        continuationDesc.canHaveUAVs = true;
        continuationDesc.canHaveRawViews = true;
        continuationDesc.initialState = nvrhi::ResourceStates::Common;
        continuationDesc.keepInitialState = true;
        m_BounceContinuation = device->createBuffer(continuationDesc);

        nvrhi::BufferDesc indirectArgumentsDesc;
        indirectArgumentsDesc.byteSize =
            sizeof(nvrhi::DispatchIndirectArguments) * 2u;
        indirectArgumentsDesc.debugName =
            "ScreenSpaceVisibility/BounceIndirectArguments";
        indirectArgumentsDesc.canHaveUAVs = true;
        indirectArgumentsDesc.canHaveRawViews = true;
        indirectArgumentsDesc.isDrawIndirectArgs = true;
        indirectArgumentsDesc.initialState = nvrhi::ResourceStates::Common;
        indirectArgumentsDesc.keepInitialState = true;
        m_BounceIndirectArguments =
            device->createBuffer(indirectArgumentsDesc);

        m_PointClampSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(false)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

        auto createDummyTexture = [device](
            nvrhi::Format format,
            const char* debugName)
        {
            nvrhi::TextureDesc desc;
            desc.width = 1u;
            desc.height = 1u;
            desc.format = format;
            desc.dimension = nvrhi::TextureDimension::Texture2D;
            desc.mipLevels = 1u;
            desc.isUAV = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;
            desc.debugName = debugName;
            return device->createTexture(desc);
        };
        m_DummyAmbientVisibility = createDummyTexture(
            nvrhi::Format::R16_FLOAT,
            "ScreenSpaceVisibility/DummyAmbientVisibility");
        m_DummyIndirectDiffuse = createDummyTexture(
            nvrhi::Format::RGBA16_FLOAT,
            "ScreenSpaceVisibility/DummyIndirectDiffuse");
        m_DummyVector = createDummyTexture(
            nvrhi::Format::RGBA16_FLOAT,
            "ScreenSpaceVisibility/DummyVector");
        // Keep placeholder UAVs distinct from placeholder SRVs. Disabled
        // shader permutations dead-strip these accesses, but binding the same
        // subresource for read and write is still an illegal API state and can
        // trip NVRHI validation before shader liveness matters.
        m_DummyAmbientOutput = createDummyTexture(
            nvrhi::Format::R16_FLOAT,
            "ScreenSpaceVisibility/DummyAmbientOutput");
        m_DummyIndirectOutput = createDummyTexture(
            nvrhi::Format::RGBA16_FLOAT,
            "ScreenSpaceVisibility/DummyIndirectOutput");
        // Independent void-and-cluster rank layers prevent the ray dimensions
        // from becoming translated copies of one structured scalar field.
        m_BlueNoiseUpload = GenerateVisibilityBlueNoise();

        nvrhi::TextureDesc blueNoiseDesc;
        blueNoiseDesc.width = VisibilityBlueNoiseSize;
        blueNoiseDesc.height = VisibilityBlueNoiseSize;
        blueNoiseDesc.arraySize = VisibilityBlueNoiseLayerCount;
        blueNoiseDesc.format = nvrhi::Format::R16_UNORM;
        blueNoiseDesc.dimension = nvrhi::TextureDimension::Texture2DArray;
        blueNoiseDesc.mipLevels = 1u;
        blueNoiseDesc.initialState = nvrhi::ResourceStates::CopyDest;
        blueNoiseDesc.keepInitialState = true;
        blueNoiseDesc.debugName =
            "ScreenSpaceVisibility/ToroidalBlueNoiseRankField";
        m_BlueNoiseTexture = device->createTexture(blueNoiseDesc);

        m_FilterAdaptedNoiseUpload =
            LoadVisibilityFilterAdaptedNoise(filterAdaptedNoisePath);
        if (m_FilterAdaptedNoiseUpload.empty())
        {
            log::warning(
                "Filter-adapted visibility rank field is missing or malformed: %s; using a safe toroidal fallback",
                filterAdaptedNoisePath.string().c_str());
            m_FilterAdaptedNoiseUpload.resize(
                VisibilityFilterAdaptedNoiseTexelCount *
                VisibilityFilterAdaptedNoiseLayerCount);
            for (uint32_t layer = 0u;
                layer < VisibilityFilterAdaptedNoiseLayerCount;
                ++layer)
            {
                const uint32_t sourceLayer =
                    layer % VisibilityBlueNoiseLayerCount;
                const uint32_t offsetX = (layer * 13u) & 63u;
                const uint32_t offsetY = (layer * 29u) & 63u;
                for (uint32_t y = 0u; y < VisibilityFilterAdaptedNoiseSize; ++y)
                {
                    for (uint32_t x = 0u; x < VisibilityFilterAdaptedNoiseSize; ++x)
                    {
                        const uint32_t sourceX = (x + offsetX) & 63u;
                        const uint32_t sourceY = (y + offsetY) & 63u;
                        m_FilterAdaptedNoiseUpload[
                            layer * VisibilityFilterAdaptedNoiseTexelCount +
                            y * VisibilityFilterAdaptedNoiseSize + x] =
                            uint8_t(m_BlueNoiseUpload[
                                sourceLayer * VisibilityBlueNoiseTexelCount +
                                sourceY * VisibilityBlueNoiseSize + sourceX] >> 8u);
                    }
                }
            }
        }
        m_PackedFastNoiseUpload =
            PackVisibilityFilterAdaptedNoiseRgba8(
                m_FilterAdaptedNoiseUpload);

        nvrhi::TextureDesc filterAdaptedNoiseDesc;
        filterAdaptedNoiseDesc.width = VisibilityFilterAdaptedNoiseSize;
        filterAdaptedNoiseDesc.height = VisibilityFilterAdaptedNoiseSize;
        filterAdaptedNoiseDesc.arraySize =
            VisibilityFilterAdaptedNoiseLayerCount;
        filterAdaptedNoiseDesc.format = nvrhi::Format::R8_UNORM;
        filterAdaptedNoiseDesc.dimension =
            nvrhi::TextureDimension::Texture2DArray;
        filterAdaptedNoiseDesc.mipLevels = 1u;
        filterAdaptedNoiseDesc.initialState =
            nvrhi::ResourceStates::CopyDest;
        filterAdaptedNoiseDesc.keepInitialState = true;
        filterAdaptedNoiseDesc.debugName =
            "ScreenSpaceVisibility/FilterAdaptedSpatiotemporalRankField";
        m_FilterAdaptedNoiseTexture =
            device->createTexture(filterAdaptedNoiseDesc);

        CreatePipelines(shaderFactory);
        m_BounceDispatchControlBindingSet = device->createBindingSet(
            nvrhi::BindingSetDesc().addItem(
                nvrhi::BindingSetItem::RawBuffer_UAV(
                    0, m_BounceContinuation)).addItem(
                nvrhi::BindingSetItem::RawBuffer_UAV(
                    1, m_BounceIndirectArguments)),
            m_BounceDispatchControl.bindingLayout);
        for (auto& stageQueries : m_TimerQueries)
            for (nvrhi::TimerQueryHandle& query : stageQueries)
                query = device->createTimerQuery();
    }

    void ScreenSpaceVisibilityPass::CreatePipelines(
        const std::shared_ptr<ShaderFactory>& shaderFactory)
    {
        auto createPipeline = [this, &shaderFactory](
            Pipeline& destination,
            const char* shaderName,
            const std::vector<nvrhi::BindingLayoutItem>& bindings,
            const std::vector<ShaderMacro>* macros = nullptr)
        {
            destination.shader = shaderFactory->CreateShader(
                shaderName, "main", macros, nvrhi::ShaderType::Compute);

            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility = nvrhi::ShaderType::Compute;
            layoutDesc.bindings = bindings;
            destination.bindingLayout =
                m_Device->createBindingLayout(layoutDesc);

            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = destination.shader;
            pipelineDesc.bindingLayouts = { destination.bindingLayout };
            destination.pipeline =
                m_Device->createComputePipeline(pipelineDesc);
        };

        const std::vector<nvrhi::BindingLayoutItem> samplingBindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_SRV(5),
            nvrhi::BindingLayoutItem::Texture_SRV(6),
            nvrhi::BindingLayoutItem::Texture_SRV(7),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(2)
        };

        for (uint32_t estimator = 0u;
            estimator < ImplementedVisibilityEstimatorCount;
            ++estimator)
        {
            for (uint32_t consumer = 0u;
                consumer < c_ConsumerVariantCount;
                ++consumer)
            {
                const bool ambientEnabled = consumer != 1u;
                const bool indirectEnabled = consumer != 0u;
                std::vector<ShaderMacro> macros = {
                    { "VISIBILITY_ESTIMATOR", std::to_string(estimator) },
                    { "ENABLE_AO", ambientEnabled ? "1" : "0" },
                    { "ENABLE_GI", indirectEnabled ? "1" : "0" },
                    { "ENABLE_BOUNCE_REINJECTION", "0" },
                    { "INITIALIZE_BOUNCE_CUMULATIVE", "0" },
                    { "ENABLE_BOUNCE_METADATA", "0" }
                };
                createPipeline(
                    m_Sampling[estimator][consumer],
                    "uvsr/screen_space_visibility_cs.hlsl",
                    samplingBindings,
                    &macros);
            }

            for (uint32_t ambientVariant = 0u;
                ambientVariant < 2u;
                ++ambientVariant)
            {
                std::vector<ShaderMacro> macros = {
                    { "VISIBILITY_ESTIMATOR", std::to_string(estimator) },
                    { "ENABLE_AO", ambientVariant != 0u ? "1" : "0" },
                    { "ENABLE_GI", "1" },
                    { "ENABLE_BOUNCE_REINJECTION", "0" },
                    { "INITIALIZE_BOUNCE_CUMULATIVE", "0" },
                    { "ENABLE_BOUNCE_METADATA", "1" }
                };
                createPipeline(
                    m_MultiBounceFirstSampling[estimator][ambientVariant],
                    "uvsr/screen_space_visibility_cs.hlsl",
                    samplingBindings,
                    &macros);
            }

            for (uint32_t cumulativeMode = 0u;
                cumulativeMode < 2u;
                ++cumulativeMode)
            {
                const bool initializeCumulative = cumulativeMode == 0u;
                std::vector<ShaderMacro> macros = {
                    { "VISIBILITY_ESTIMATOR", std::to_string(estimator) },
                    { "ENABLE_AO", "0" },
                    { "ENABLE_GI", "1" },
                    { "ENABLE_BOUNCE_REINJECTION", "1" },
                    { "INITIALIZE_BOUNCE_CUMULATIVE",
                        initializeCumulative ? "1" : "0" },
                    { "ENABLE_BOUNCE_METADATA", "0" },
                    { "ENABLE_BOUNCE_CONTINUATION", "0" }
                };
                createPipeline(
                    m_IndirectBounceSampling[estimator][cumulativeMode],
                    "uvsr/screen_space_visibility_cs.hlsl",
                    {
                        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(1),
                        nvrhi::BindingLayoutItem::Texture_SRV(2),
                        nvrhi::BindingLayoutItem::Texture_SRV(3),
                        nvrhi::BindingLayoutItem::Texture_SRV(4),
                        nvrhi::BindingLayoutItem::Texture_SRV(5),
                        nvrhi::BindingLayoutItem::Texture_SRV(6),
                        nvrhi::BindingLayoutItem::Texture_SRV(7),
                        nvrhi::BindingLayoutItem::Texture_SRV(8),
                        nvrhi::BindingLayoutItem::Texture_SRV(9),
                        nvrhi::BindingLayoutItem::Texture_SRV(10),
                        nvrhi::BindingLayoutItem::Texture_UAV(0),
                        nvrhi::BindingLayoutItem::Texture_UAV(1)
                    },
                    &macros);
            }
        }

        createPipeline(
            m_BounceDispatchControl,
            "uvsr/screen_space_visibility_bounce_control_cs.hlsl",
            {
                nvrhi::BindingLayoutItem::RawBuffer_UAV(0),
                nvrhi::BindingLayoutItem::RawBuffer_UAV(1)
            });

        for (uint32_t consumer = 0u;
            consumer < c_ConsumerVariantCount;
            ++consumer)
        {
            const bool ambientEnabled = consumer != 1u;
            const bool indirectEnabled = consumer != 0u;
            std::vector<ShaderMacro> macros = {
                { "ENABLE_AO", ambientEnabled ? "1" : "0" },
                { "ENABLE_GI", indirectEnabled ? "1" : "0" }
            };
            createPipeline(
                m_Temporal[consumer],
                "uvsr/screen_space_visibility_temporal_cs.hlsl",
                {
                    nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                    nvrhi::BindingLayoutItem::Texture_SRV(0),
                    nvrhi::BindingLayoutItem::Texture_SRV(1),
                    nvrhi::BindingLayoutItem::Texture_SRV(2),
                    nvrhi::BindingLayoutItem::Texture_SRV(3),
                    nvrhi::BindingLayoutItem::Texture_SRV(4),
                    nvrhi::BindingLayoutItem::Texture_SRV(5),
                    nvrhi::BindingLayoutItem::Texture_SRV(6),
                    nvrhi::BindingLayoutItem::Texture_SRV(7),
                    nvrhi::BindingLayoutItem::Texture_SRV(8),
                    nvrhi::BindingLayoutItem::Texture_UAV(0),
                    nvrhi::BindingLayoutItem::Texture_UAV(1),
                    nvrhi::BindingLayoutItem::Texture_UAV(2),
                    nvrhi::BindingLayoutItem::Texture_UAV(3)
                },
                &macros);
            for (uint32_t filter = 0u; filter < 2u; ++filter)
            {
                std::vector<ShaderMacro> filterMacros = macros;
                filterMacros.push_back({
                    "SPATIAL_FILTER", std::to_string(filter) });
                createPipeline(
                    m_Filter[filter][consumer],
                    "uvsr/screen_space_visibility_filter_cs.hlsl",
                    {
                        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(1),
                        nvrhi::BindingLayoutItem::Texture_SRV(2),
                        nvrhi::BindingLayoutItem::Texture_SRV(3),
                        nvrhi::BindingLayoutItem::Texture_UAV(0),
                        nvrhi::BindingLayoutItem::Texture_UAV(1)
                    },
                    &filterMacros);
            }
        }

        createPipeline(
            m_DepthHierarchy,
            "uvsr/screen_space_depth_hierarchy_cs.hlsl",
            {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_UAV(0).setSize(5)
            });

        for (uint32_t aoPowerEnabled = 0u;
            aoPowerEnabled < 2u;
            ++aoPowerEnabled)
        {
            std::vector<ShaderMacro> macros = {{
                "ENABLE_AO_POWER", aoPowerEnabled != 0u ? "1" : "0"
            }};
            createPipeline(
                m_Composite[aoPowerEnabled],
                "uvsr/screen_space_indirect_composite_cs.hlsl",
                {
                    nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                    nvrhi::BindingLayoutItem::Texture_SRV(0),
                    nvrhi::BindingLayoutItem::Texture_SRV(1),
                    nvrhi::BindingLayoutItem::Texture_SRV(2),
                    nvrhi::BindingLayoutItem::Texture_SRV(3),
                    nvrhi::BindingLayoutItem::Texture_SRV(4),
                    nvrhi::BindingLayoutItem::Texture_SRV(5),
                    nvrhi::BindingLayoutItem::Texture_SRV(6),
                    nvrhi::BindingLayoutItem::Texture_SRV(7),
                    nvrhi::BindingLayoutItem::Texture_UAV(0)
                },
                &macros);
        }
    }

    ScreenSpaceVisibilityPass::Pipeline&
        ScreenSpaceVisibilityPass::GetOrCreateAdvancedPipeline(
            uint64_t key,
            const char* shaderName,
            const std::vector<nvrhi::BindingLayoutItem>& bindings,
            const std::vector<ShaderMacro>* macros)
    {
        const auto existing = m_AdvancedPipelines.find(key);
        if (existing != m_AdvancedPipelines.end())
            return existing->second;

        Pipeline pipeline;
        pipeline.shader = m_ShaderFactory->CreateShader(
            shaderName, "main", macros, nvrhi::ShaderType::Compute);
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = bindings;
        pipeline.bindingLayout = m_Device->createBindingLayout(layoutDesc);
        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = pipeline.shader;
        pipelineDesc.bindingLayouts = { pipeline.bindingLayout };
        pipeline.pipeline = m_Device->createComputePipeline(pipelineDesc);
        return m_AdvancedPipelines.emplace(key, std::move(pipeline))
            .first->second;
    }

    void ScreenSpaceVisibilityPass::EnsureResources(
        uint2 fullSize,
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
        const VisibilityBufferPrecisionSettings& bufferPrecision)
    {
        resolutionScale = std::clamp(resolutionScale, 1u, 4u);
        const uint2 samplingSize(
            (fullSize.x + resolutionScale - 1u) / resolutionScale,
            (fullSize.y + resolutionScale - 1u) / resolutionScale);
        multipleBouncesEnabled = multipleBouncesEnabled &&
            indirectDiffuseEnabled;
        const uint64_t bufferPrecisionKey =
            uint64_t(bufferPrecision.rawAmbient) |
            (uint64_t(bufferPrecision.rawIndirect) << 2u) |
            (uint64_t(bufferPrecision.cumulativeIndirect) << 4u) |
            (uint64_t(bufferPrecision.temporalAmbient) << 6u) |
            (uint64_t(bufferPrecision.temporalIndirect) << 8u) |
            (uint64_t(bufferPrecision.temporalDepth) << 10u) |
            (uint64_t(bufferPrecision.finalAmbient) << 12u) |
            (uint64_t(bufferPrecision.finalIndirect) << 14u) |
            (uint64_t(bufferPrecision.depthHierarchy) << 16u);

        if (all(m_FullSize == fullSize) &&
            all(m_SamplingSize == samplingSize) &&
            m_ResolutionScale == resolutionScale &&
            m_AmbientResourcesEnabled == ambientEnabled &&
            m_IndirectDiffuseResourcesEnabled == indirectDiffuseEnabled &&
            m_MultipleBounceResourcesEnabled == multipleBouncesEnabled &&
            m_TemporalResourcesEnabled == temporalEnabled &&
            m_PostProcessResourcesEnabled == postProcessEnabled &&
            m_DepthHierarchyResourcesEnabled == depthHierarchyEnabled &&
            m_FinalAmbientResourcesEnabled == finalAmbientEnabled &&
            m_PackedFastResourcesEnabled == packedFastEnabled &&
            m_PackedEdgeResourcesEnabled == packedEdgesEnabled &&
            m_BufferPrecisionConfigurationKey == bufferPrecisionKey &&
            (!ambientEnabled || m_RawAmbientVisibility) &&
            (!indirectDiffuseEnabled || m_RawIndirectDiffuse[0]))
        {
            return;
        }

        ReleaseResources();
        m_FullSize = fullSize;
        m_SamplingSize = samplingSize;
        m_ResolutionScale = resolutionScale;
        m_AmbientResourcesEnabled = ambientEnabled;
        m_IndirectDiffuseResourcesEnabled = indirectDiffuseEnabled;
        m_MultipleBounceResourcesEnabled = multipleBouncesEnabled;
        m_TemporalResourcesEnabled = temporalEnabled;
        m_PostProcessResourcesEnabled = postProcessEnabled;
        m_DepthHierarchyResourcesEnabled = depthHierarchyEnabled;
        m_FinalAmbientResourcesEnabled = finalAmbientEnabled;
        m_PackedFastResourcesEnabled = packedFastEnabled;
        m_PackedEdgeResourcesEnabled = packedEdgesEnabled;
        m_BufferPrecisionConfigurationKey = bufferPrecisionKey;

        const auto scalarFormat = [](VisibilityScalarBufferPrecision precision)
        {
            return precision == VisibilityScalarBufferPrecision::Float32
                ? nvrhi::Format::R32_FLOAT
                : nvrhi::Format::R16_FLOAT;
        };
        const auto vectorFormat = [](VisibilityVectorBufferPrecision precision)
        {
            return precision == VisibilityVectorBufferPrecision::Rgba32Float
                ? nvrhi::Format::RGBA32_FLOAT
                : nvrhi::Format::RGBA16_FLOAT;
        };
        const auto scalarBytes = [](VisibilityScalarBufferPrecision precision)
        {
            return precision == VisibilityScalarBufferPrecision::Float32
                ? 4u : 2u;
        };
        const auto vectorBytes = [](VisibilityVectorBufferPrecision precision)
        {
            return precision == VisibilityVectorBufferPrecision::Rgba32Float
                ? 16u : 8u;
        };

        auto createTexture = [this](
            uint2 size,
            nvrhi::Format format,
            const char* debugName)
        {
            nvrhi::TextureDesc desc;
            desc.width = size.x;
            desc.height = size.y;
            desc.format = format;
            desc.dimension = nvrhi::TextureDimension::Texture2D;
            desc.mipLevels = 1u;
            desc.isUAV = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;
            desc.debugName = debugName;
            return m_Device->createTexture(desc);
        };

        if (ambientEnabled)
        {
            m_RawAmbientVisibility = createTexture(
                samplingSize,
                scalarFormat(bufferPrecision.rawAmbient),
                "ScreenSpaceVisibility/RawAmbientVisibility");
        }
        if (indirectDiffuseEnabled)
        {
            m_RawIndirectDiffuse[0] = createTexture(
                samplingSize,
                vectorFormat(bufferPrecision.rawIndirect),
                "ScreenSpaceVisibility/IndirectDiffuseFrontier0");
        }
        if (multipleBouncesEnabled)
        {
            m_RawIndirectDiffuse[1] = createTexture(
                samplingSize,
                vectorFormat(bufferPrecision.rawIndirect),
                "ScreenSpaceVisibility/IndirectDiffuseFrontier1");
            m_CumulativeIndirectDiffuse = createTexture(
                samplingSize,
                vectorFormat(bufferPrecision.cumulativeIndirect),
                "ScreenSpaceVisibility/CumulativeIndirectDiffuse");
        }
        if (temporalEnabled)
        {
            for (uint32_t index = 0u; index < 2u; ++index)
            {
                if (ambientEnabled)
                {
                    m_TemporalAmbientVisibility[index] = createTexture(
                        samplingSize,
                        scalarFormat(bufferPrecision.temporalAmbient),
                        index == 0u
                            ? "ScreenSpaceVisibility/AmbientHistory0"
                            : "ScreenSpaceVisibility/AmbientHistory1");
                }
                if (indirectDiffuseEnabled)
                {
                    m_TemporalIndirectDiffuse[index] = createTexture(
                        samplingSize,
                        vectorFormat(bufferPrecision.temporalIndirect),
                        index == 0u
                            ? "ScreenSpaceVisibility/IndirectHistory0"
                            : "ScreenSpaceVisibility/IndirectHistory1");
                }
                m_TemporalDepth[index] = createTexture(
                    samplingSize,
                    scalarFormat(bufferPrecision.temporalDepth),
                    index == 0u
                        ? "ScreenSpaceVisibility/DepthHistory0"
                        : "ScreenSpaceVisibility/DepthHistory1");
                m_TemporalNormal[index] = createTexture(
                    samplingSize,
                    nvrhi::Format::RGBA8_UNORM,
                    index == 0u
                        ? "ScreenSpaceVisibility/NormalHistory0"
                        : "ScreenSpaceVisibility/NormalHistory1");
            }
        }
        if (postProcessEnabled)
        {
            if (ambientEnabled && finalAmbientEnabled)
            {
                m_FinalAmbientVisibility = createTexture(
                    fullSize,
                    scalarFormat(bufferPrecision.finalAmbient),
                    "ScreenSpaceVisibility/FinalAmbientVisibility");
            }
            if (indirectDiffuseEnabled)
            {
                m_FinalIndirectDiffuse = createTexture(
                    fullSize,
                    vectorFormat(bufferPrecision.finalIndirect),
                    "ScreenSpaceVisibility/FinalIndirectDiffuse");
            }
        }
        if (packedEdgesEnabled)
        {
            m_PackedEdgesTexture = createTexture(
                samplingSize,
                nvrhi::Format::R8_UINT,
                "ScreenSpaceVisibility/PackedEdges");
        }
        if (packedFastEnabled)
        {
            nvrhi::TextureDesc packedFastDesc;
            packedFastDesc.width = VisibilityFilterAdaptedNoiseSize;
            packedFastDesc.height = VisibilityFilterAdaptedNoiseSize;
            packedFastDesc.arraySize =
                VisibilityFilterAdaptedNoiseLayerCount;
            packedFastDesc.format = nvrhi::Format::RGBA8_UNORM;
            packedFastDesc.dimension =
                nvrhi::TextureDimension::Texture2DArray;
            packedFastDesc.mipLevels = 1u;
            packedFastDesc.initialState =
                nvrhi::ResourceStates::CopyDest;
            packedFastDesc.keepInitialState = true;
            packedFastDesc.debugName =
                "ScreenSpaceVisibility/PackedCurrentFAST";
            m_PackedFastNoiseTexture =
                m_Device->createTexture(packedFastDesc);
            m_PackedFastNoiseUploaded = false;
        }
        if (depthHierarchyEnabled)
        {
            nvrhi::TextureDesc hierarchyDesc;
            hierarchyDesc.width = std::max(
                (fullSize.x + 15u) & ~15u, 16u);
            hierarchyDesc.height = std::max(
                (fullSize.y + 15u) & ~15u, 16u);
            hierarchyDesc.format =
                scalarFormat(bufferPrecision.depthHierarchy);
            hierarchyDesc.dimension = nvrhi::TextureDimension::Texture2D;
            hierarchyDesc.mipLevels = 5u;
            hierarchyDesc.isUAV = true;
            hierarchyDesc.initialState = nvrhi::ResourceStates::ShaderResource;
            hierarchyDesc.keepInitialState = true;
            hierarchyDesc.debugName =
                "ScreenSpaceVisibility/DepthHierarchy";
            m_DepthHierarchyTexture =
                m_Device->createTexture(hierarchyDesc);
        }

        const uint64_t rawAmbientBytes = TextureBytes(
            samplingSize, scalarBytes(bufferPrecision.rawAmbient));
        const uint64_t rawIndirectBytes = TextureBytes(
            samplingSize, vectorBytes(bufferPrecision.rawIndirect));
        const uint64_t cumulativeIndirectBytes = TextureBytes(
            samplingSize,
            vectorBytes(bufferPrecision.cumulativeIndirect));
        const uint64_t temporalAmbientBytes = TextureBytes(
            samplingSize,
            scalarBytes(bufferPrecision.temporalAmbient));
        const uint64_t temporalIndirectBytes = TextureBytes(
            samplingSize,
            vectorBytes(bufferPrecision.temporalIndirect));
        const uint64_t temporalDepthBytes = TextureBytes(
            samplingSize,
            scalarBytes(bufferPrecision.temporalDepth));
        const uint64_t finalAmbientBytes = TextureBytes(
            fullSize, scalarBytes(bufferPrecision.finalAmbient));
        const uint64_t finalIndirectBytes = TextureBytes(
            fullSize, vectorBytes(bufferPrecision.finalIndirect));
        const uint64_t schedulerBytes =
            uint64_t(VisibilityBlueNoiseTexelCount) *
                uint64_t(VisibilityBlueNoiseLayerCount) * sizeof(uint16_t) +
            uint64_t(VisibilityFilterAdaptedNoiseTexelCount) *
                uint64_t(VisibilityFilterAdaptedNoiseLayerCount) *
                sizeof(uint8_t);

        m_Timings.rawAmbientTextureBytes =
            ambientEnabled ? rawAmbientBytes : 0u;
        m_Timings.rawIndirectFrontierBytes =
            indirectDiffuseEnabled ? rawIndirectBytes : 0u;
        m_Timings.multiBounceIndirectBytes =
            multipleBouncesEnabled
                ? rawIndirectBytes + cumulativeIndirectBytes : 0u;
        m_Timings.finalAmbientTextureBytes =
            postProcessEnabled && ambientEnabled && finalAmbientEnabled
                ? finalAmbientBytes : 0u;
        m_Timings.finalIndirectTextureBytes =
            postProcessEnabled && indirectDiffuseEnabled
                ? finalIndirectBytes : 0u;
        m_Timings.schedulerResourceBytes = schedulerBytes;
        m_Timings.temporalAmbientHistoryBytes =
            temporalEnabled && ambientEnabled
                ? temporalAmbientBytes * 2u : 0u;
        m_Timings.temporalIndirectHistoryBytes =
            temporalEnabled && indirectDiffuseEnabled
                ? temporalIndirectBytes * 2u : 0u;
        m_Timings.temporalDepthHistoryBytes = temporalEnabled
            ? temporalDepthBytes * 2u : 0u;
        m_Timings.temporalNormalHistoryBytes =
            temporalEnabled ? TextureBytes(samplingSize, 8u) : 0u;
        m_Timings.depthHierarchyBytes = depthHierarchyEnabled
            ? DepthHierarchyBytes(
                fullSize,
                scalarBytes(bufferPrecision.depthHierarchy)) : 0u;
        m_Timings.packedFastNoiseBytes = packedFastEnabled
            ? uint64_t(VisibilityFilterAdaptedNoiseTexelCount) *
                VisibilityFilterAdaptedNoiseLayerCount * 4u
            : 0u;
        m_Timings.packedEdgeMetadataBytes = packedEdgesEnabled
            ? TextureBytes(samplingSize, 1u) : 0u;
        m_Timings.outputTextureBytes =
            m_Timings.rawAmbientTextureBytes +
            m_Timings.rawIndirectFrontierBytes +
            m_Timings.multiBounceIndirectBytes +
            m_Timings.finalAmbientTextureBytes +
            m_Timings.finalIndirectTextureBytes;
        m_Timings.workingTextureBytes =
            m_Timings.schedulerResourceBytes +
            m_Timings.temporalAmbientHistoryBytes +
            m_Timings.temporalIndirectHistoryBytes +
            m_Timings.temporalDepthHistoryBytes +
            m_Timings.temporalNormalHistoryBytes +
            m_Timings.depthHierarchyBytes +
            m_Timings.packedFastNoiseBytes +
            m_Timings.packedEdgeMetadataBytes;
        m_Timings.optionalTextureBytes =
            m_Timings.packedFastNoiseBytes +
            m_Timings.packedEdgeMetadataBytes;
        m_Timings.fullResolutionIntermediateBytes =
            ambientEnabled && finalAmbientEnabled
                ? finalAmbientBytes : 0u;
        m_Timings.logicalTrafficAvoidedBytes =
            ambientEnabled && postProcessEnabled && !finalAmbientEnabled
                ? finalAmbientBytes * 2u : 0u;
        m_Timings.maskCacheBytes = 0u;
        m_Timings.avoidedTextureBytes =
            (!ambientEnabled
                ? rawAmbientBytes +
                    (temporalEnabled ? rawAmbientBytes * 2u : 0u) +
                    (postProcessEnabled && finalAmbientEnabled
                        ? finalAmbientBytes : 0u)
                : 0u) +
            (!indirectDiffuseEnabled
                ? rawIndirectBytes +
                    (temporalEnabled ? rawIndirectBytes * 2u : 0u) +
                    (postProcessEnabled ? finalIndirectBytes : 0u)
                : 0u);
        m_Timings.sharedMaskPayloadBytes =
            ambientEnabled && indirectDiffuseEnabled
                ? TextureBytes(samplingSize, 4u) : 0u;
    }

    void ScreenSpaceVisibilityPass::ReleaseResources()
    {
        ResetBindingCache();
        m_RawAmbientVisibility = nullptr;
        for (nvrhi::TextureHandle& texture : m_RawIndirectDiffuse)
            texture = nullptr;
        m_CumulativeIndirectDiffuse = nullptr;
        for (nvrhi::TextureHandle& texture : m_TemporalAmbientVisibility)
            texture = nullptr;
        for (nvrhi::TextureHandle& texture : m_TemporalIndirectDiffuse)
            texture = nullptr;
        for (nvrhi::TextureHandle& texture : m_TemporalDepth)
            texture = nullptr;
        for (nvrhi::TextureHandle& texture : m_TemporalNormal)
            texture = nullptr;
        m_FinalAmbientVisibility = nullptr;
        m_FinalIndirectDiffuse = nullptr;
        m_DepthHierarchyTexture = nullptr;
        m_PackedFastNoiseTexture = nullptr;
        m_PackedEdgesTexture = nullptr;

        m_FullSize = uint2::zero();
        m_SamplingSize = uint2::zero();
        m_ResolutionScale = 1u;
        m_AmbientResourcesEnabled = false;
        m_IndirectDiffuseResourcesEnabled = false;
        m_MultipleBounceResourcesEnabled = false;
        m_TemporalResourcesEnabled = false;
        m_PostProcessResourcesEnabled = false;
        m_DepthHierarchyResourcesEnabled = false;
        m_FinalAmbientResourcesEnabled = false;
        m_PackedFastResourcesEnabled = false;
        m_PackedEdgeResourcesEnabled = false;
        m_BufferPrecisionConfigurationKey = 0u;
        m_PackedFastNoiseUploaded = false;
        ResetHistory();
        m_Timings = {};
    }

    void ScreenSpaceVisibilityPass::Deactivate()
    {
        ReleaseResources();
    }

    void ScreenSpaceVisibilityPass::ResetHistory()
    {
        m_HistoryValid = false;
        m_HistoryIndex = 0u;
    }

    void ScreenSpaceVisibilityPass::ResetBindingCache()
    {
        for (auto& estimator : m_SamplingBindingSets)
            for (nvrhi::BindingSetHandle& bindingSet : estimator)
                bindingSet = nullptr;
        for (auto& estimator : m_MultiBounceFirstBindingSets)
            for (nvrhi::BindingSetHandle& bindingSet : estimator)
                bindingSet = nullptr;
        for (auto& estimator : m_IndirectBounceBindingSets)
            for (auto& cumulativeMode : estimator)
                for (nvrhi::BindingSetHandle& bindingSet : cumulativeMode)
                    bindingSet = nullptr;
        for (auto& consumer : m_TemporalBindingSets)
            for (auto& source : consumer)
                for (nvrhi::BindingSetHandle& bindingSet : source)
                    bindingSet = nullptr;
        for (auto& filter : m_FilterBindingSets)
            for (auto& consumer : filter)
                for (nvrhi::BindingSetHandle& bindingSet : consumer)
                    bindingSet = nullptr;
        m_DepthHierarchyBindingSet = nullptr;
        for (auto& powerVariant : m_CompositeBindingSets)
            for (nvrhi::BindingSetHandle& bindingSet : powerVariant)
                bindingSet = nullptr;
        m_AdvancedBindingSets.clear();
    }

    void ScreenSpaceVisibilityPass::UploadSamplingNoise(
        nvrhi::ICommandList* commandList)
    {
        if (!m_SamplingNoiseUploaded)
        {
            assert(m_BlueNoiseTexture &&
                m_FilterAdaptedNoiseTexture &&
                !m_BlueNoiseUpload.empty() &&
                !m_FilterAdaptedNoiseUpload.empty());
            for (uint32_t layer = 0u;
                layer < VisibilityBlueNoiseLayerCount;
                ++layer)
            {
                commandList->writeTexture(
                    m_BlueNoiseTexture,
                    layer,
                    0u,
                    m_BlueNoiseUpload.data() +
                        layer * VisibilityBlueNoiseTexelCount,
                    size_t(VisibilityBlueNoiseSize) * sizeof(uint16_t));
            }
            commandList->setPermanentTextureState(
                m_BlueNoiseTexture,
                nvrhi::ResourceStates::ShaderResource);
            for (uint32_t layer = 0u;
                layer < VisibilityFilterAdaptedNoiseLayerCount;
                ++layer)
            {
                commandList->writeTexture(
                    m_FilterAdaptedNoiseTexture,
                    layer,
                    0u,
                    m_FilterAdaptedNoiseUpload.data() +
                        layer * VisibilityFilterAdaptedNoiseTexelCount,
                    size_t(VisibilityFilterAdaptedNoiseSize) *
                        sizeof(uint8_t));
            }
            commandList->setPermanentTextureState(
                m_FilterAdaptedNoiseTexture,
                nvrhi::ResourceStates::ShaderResource);
            m_SamplingNoiseUploaded = true;
            m_BlueNoiseUpload.clear();
            m_BlueNoiseUpload.shrink_to_fit();
            m_FilterAdaptedNoiseUpload.clear();
            m_FilterAdaptedNoiseUpload.shrink_to_fit();
        }

        if (m_PackedFastNoiseTexture && !m_PackedFastNoiseUploaded)
        {
            assert(!m_PackedFastNoiseUpload.empty());
            for (uint32_t layer = 0u;
                layer < VisibilityFilterAdaptedNoiseLayerCount;
                ++layer)
            {
                commandList->writeTexture(
                    m_PackedFastNoiseTexture,
                    layer,
                    0u,
                    m_PackedFastNoiseUpload.data() +
                        size_t(layer) *
                            VisibilityFilterAdaptedNoiseTexelCount * 4u,
                    size_t(VisibilityFilterAdaptedNoiseSize) * 4u);
            }
            commandList->setPermanentTextureState(
                m_PackedFastNoiseTexture,
                nvrhi::ResourceStates::ShaderResource);
            m_PackedFastNoiseUploaded = true;
        }

    }

    bool ScreenSpaceVisibilityPass::BeginBenchmark(
        const VisibilityBenchmarkRunMetadata& metadata,
        uint32_t warmupFrameCount,
        uint32_t measuredFrameCount)
    {
        if (!m_ExecutionPlan.valid || measuredFrameCount == 0u)
            return false;

        VisibilityBenchmarkStageMask requiredStageMask =
            VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::FirstTrace) |
            VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::EffectEnvelope);
        if (HasVisibilityExecutionPass(m_ExecutionPlan.passMask,
                VisibilityExecutionPass::DepthPreparation))
        {
            requiredStageMask |= VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::DepthPreparation);
        }
        if (HasVisibilityExecutionPass(m_ExecutionPlan.passMask,
                VisibilityExecutionPass::LegacyLaterBounceTrace) ||
            HasVisibilityExecutionPass(m_ExecutionPlan.passMask,
                VisibilityExecutionPass::FixedLaterBounceTrace))
        {
            requiredStageMask |= VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::LaterTrace);
            if (m_ExecutionPlan.workload.bounceCount >= 2u)
            {
                requiredStageMask |= VisibilityBenchmarkStageBit(
                    VisibilityBenchmarkStage::LaterTraceBounce2);
            }
            if (m_ExecutionPlan.workload.bounceCount >= 3u)
            {
                requiredStageMask |= VisibilityBenchmarkStageBit(
                    VisibilityBenchmarkStage::LaterTraceBounce3);
            }
            if (m_ExecutionPlan.workload.bounceCount >= 4u)
            {
                requiredStageMask |= VisibilityBenchmarkStageBit(
                    VisibilityBenchmarkStage::LaterTraceBounce4);
            }
        }
        if (HasVisibilityExecutionPass(m_ExecutionPlan.passMask,
                VisibilityExecutionPass::Temporal))
        {
            requiredStageMask |= VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::Temporal);
        }
        if (HasVisibilityExecutionPass(m_ExecutionPlan.passMask,
                VisibilityExecutionPass::SpatialDenoise))
        {
            requiredStageMask |= VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::SpatialDenoise);
        }
        if (HasVisibilityExecutionPass(m_ExecutionPlan.passMask,
                VisibilityExecutionPass::Reconstruction))
        {
            requiredStageMask |= VisibilityBenchmarkStageBit(
                m_ExecutionPlan.workload.spatialEnabled
                    ? VisibilityBenchmarkStage::
                        FusedSpatialDenoiseUpsample
                    : VisibilityBenchmarkStage::RequiredUpsample);
        }
        if (HasVisibilityExecutionPass(m_ExecutionPlan.passMask,
                VisibilityExecutionPass::FusedResolveAndApply))
        {
            requiredStageMask |= VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::FullResolutionApply);
        }
        if (HasVisibilityExecutionPass(m_ExecutionPlan.passMask,
                VisibilityExecutionPass::Composition))
        {
            requiredStageMask |= VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::Composition);
        }

        VisibilityBenchmarkRunConfiguration configuration;
        configuration.metadata = metadata;
        configuration.firstFrameId = m_TimerFrame;
        configuration.warmupFrameCount = warmupFrameCount;
        configuration.measuredFrameCount = measuredFrameCount;
        configuration.requiredStageMask = requiredStageMask;
        configuration.summedStageMask =
            VisibilityBenchmarkDefaultSummedStageMask & requiredStageMask;
        configuration.producerStageMask =
            VisibilityBenchmarkDefaultProducerStageMask & requiredStageMask;
        if (HasVisibilityExecutionPass(m_ExecutionPlan.passMask,
                VisibilityExecutionPass::FusedResolveAndApply))
        {
            // In fused profiles, the full-resolution apply dispatch owns the
            // final producer work; excluding it would make producer totals
            // incomparable with the unfused reconstruction pipeline.
            configuration.producerStageMask |= VisibilityBenchmarkStageBit(
                VisibilityBenchmarkStage::FullResolutionApply);
        }
        if (!m_BenchmarkStatistics.Reset(configuration))
            return false;

        ResetHistory();
        m_BenchmarkActive = true;
        return true;
    }

    void ScreenSpaceVisibilityPass::CancelBenchmark()
    {
        m_BenchmarkActive = false;
    }

    void ScreenSpaceVisibilityPass::AdvanceTimers()
    {
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        for (uint32_t stageIndex = 0u;
            stageIndex < static_cast<uint32_t>(Stage::Count);
            ++stageIndex)
        {
            m_TimerActive[stageIndex] = false;
            if (!m_TimerPending[stageIndex][slot])
                continue;

            nvrhi::ITimerQuery* query = m_TimerQueries[stageIndex][slot];
            if (!m_Device->pollTimerQuery(query))
                continue;

            const float milliseconds =
                m_Device->getTimerQueryTime(query) * 1000.f;
            const uint64_t originatingFrame =
                m_TimerFrameIds[stageIndex][slot];
            m_Device->resetTimerQuery(query);
            m_TimerPending[stageIndex][slot] = false;

            if (m_BenchmarkActive)
            {
                (void)m_BenchmarkStatistics.AddSample(
                    originatingFrame,
                    static_cast<VisibilityBenchmarkStage>(stageIndex),
                    milliseconds);
            }

            switch (static_cast<Stage>(stageIndex))
            {
            case Stage::DepthHierarchy:
                m_Timings.depthHierarchyMs = milliseconds;
                break;
            case Stage::FirstTrace:
                m_Timings.firstTraceMs = milliseconds;
                m_Timings.samplingMs =
                    m_Timings.firstTraceMs + m_Timings.laterTraceMs;
                break;
            case Stage::LaterTrace:
                m_Timings.laterTraceMs = milliseconds;
                m_Timings.samplingMs =
                    m_Timings.firstTraceMs + m_Timings.laterTraceMs;
                break;
            case Stage::LaterTraceBounce2:
                m_Timings.laterBounceMs[0] = milliseconds;
                break;
            case Stage::LaterTraceBounce3:
                m_Timings.laterBounceMs[1] = milliseconds;
                break;
            case Stage::LaterTraceBounce4:
                m_Timings.laterBounceMs[2] = milliseconds;
                break;
            case Stage::SpatialDenoise:
                m_Timings.spatialDenoiseMs = milliseconds;
                break;
            case Stage::Temporal:
                m_Timings.temporalMs = milliseconds;
                break;
            case Stage::FusedSpatialDenoiseUpsample:
                m_Timings.fusedSpatialDenoiseUpsampleMs = milliseconds;
                break;
            case Stage::RequiredUpsample:
                m_Timings.requiredUpsampleMs = milliseconds;
                break;
            case Stage::FullResolutionApply:
                m_Timings.fullResolutionApplyMs = milliseconds;
                break;
            case Stage::Composition:
                m_Timings.compositionMs = milliseconds;
                break;
            case Stage::EffectEnvelope:
                m_Timings.effectEnvelopeMs = milliseconds;
                break;
            default:
                break;
            }
        }

        // Do not partially instrument a frame when even one query in the
        // selected ring slot is still owned by the GPU. Holding the logical
        // timer frame here makes the next render poll the same slot again;
        // once it is free, every required stage is issued under one frame ID.
        // This avoids permanent holes in a benchmark on a GPU whose query
        // latency exceeds c_TimerLatency physical frames.
        m_TimerFrameWritable = true;
        for (uint32_t stageIndex = 0u;
            stageIndex < static_cast<uint32_t>(Stage::Count);
            ++stageIndex)
        {
            if (m_TimerPending[stageIndex][slot])
            {
                m_TimerFrameWritable = false;
                break;
            }
        }
    }

    void ScreenSpaceVisibilityPass::BeginStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        if (!m_TimerFrameWritable)
            return;

        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        if (m_TimerPending[stageIndex][slot])
            return;

        commandList->beginTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerActive[stageIndex] = true;
    }

    void ScreenSpaceVisibilityPass::EndStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        if (!m_TimerActive[stageIndex])
            return;

        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        commandList->endTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerPending[stageIndex][slot] = true;
        m_TimerFrameIds[stageIndex][slot] = m_TimerFrame;
        m_TimerActive[stageIndex] = false;
    }

    void ScreenSpaceVisibilityPass::Render(
        nvrhi::ICommandList* commandList,
        const ScreenSpaceVisibilitySettings& settings,
        const ICompositeView& compositeView,
        const ScreenSpaceVisibilityInputs& inputs,
        float3 ambientColorTop,
        float3 ambientColorBottom,
        float exposureScale,
        uint32_t frameIndex)
    {
        if (!settings.HasActiveConsumer())
        {
            Deactivate();
            return;
        }

        uint32_t knownInactiveLightingSources =
            inputs.knownInactiveLightingSources & LightingSource_All;
        const uint32_t firstBounceSources =
            LightingSource_Direct | LightingSource_Emissive;
        const bool firstBounceSourceIsPotentiallyActive =
            (knownInactiveLightingSources & firstBounceSources) !=
            firstBounceSources;
        assert(commandList);
        assert(inputs.depth && inputs.normals);
        assert((inputs.sourceRadiance ||
                !settings.HasActiveIndirectDiffuse() ||
                !firstBounceSourceIsPotentiallyActive) &&
            inputs.gbufferDiffuse && inputs.gbufferSpecular &&
            inputs.gbufferEmissive);
        assert(inputs.materialAmbientOcclusion && inputs.baseLighting &&
            inputs.output);
        assert(compositeView.GetNumChildViews(ViewType::PLANAR) == 1);
        assert(!settings.RequiresMotionVectors() || inputs.motionVectors);

        const IView* view = compositeView.GetChildView(ViewType::PLANAR, 0);
        const nvrhi::TextureDesc& depthDesc = inputs.depth->getDesc();
        const uint2 fullSize(depthDesc.width, depthDesc.height);
        const uint32_t resolutionScale =
            GetResolutionScale(settings.resolution);
        const bool ambientEnabled = settings.HasActiveAmbientOcclusion();
        const bool indirectEnabled = settings.HasActiveIndirectDiffuse();
        const bool ambientPowerEnabled = ambientEnabled &&
            IsAmbientPowerEnabled(settings.ambientOcclusion.power);
        const bool motionAvailable = inputs.motionVectors != nullptr;
        const bool temporalEnabled =
            settings.reconstruction.temporalEnabled && motionAvailable;
        const bool spatialFilterEnabled =
            settings.reconstruction.spatialEnabled;
        // A reduced-resolution signal still requires guide-aware
        // reconstruction. Spatial filtering off selects only the minimal
        // compact upsampler; full resolution can bypass the pass completely.
        const bool legacyPostProcessRequested = spatialFilterEnabled ||
            resolutionScale > 1u;

        if ((knownInactiveLightingSources & firstBounceSources) ==
            firstBounceSources)
        {
            knownInactiveLightingSources |= LightingSource_IndirectDiffuse;
        }

        const bool contributionTerminatedBounces = indirectEnabled &&
            !settings.indirectDiffuse.limitBounces;
        const uint32_t requestedBounceCount = indirectEnabled
            ? (contributionTerminatedBounces
                ? MaxContributionTerminatedBounceCount
                : std::clamp(settings.indirectDiffuse.bounceCount,
                    1u, MaxIndirectDiffuseBounceCount))
            : 1u;
        const bool higherBouncesReachConsumer = indirectEnabled &&
            settings.indirectDiffuse.intensity > 0.f &&
            (knownInactiveLightingSources & LightingSource_IndirectDiffuse) == 0u;
        const uint32_t activeBounceCount = higherBouncesReachConsumer
            ? requestedBounceCount : 1u;
        const bool legacyDepthHierarchyEligible =
            ambientEnabled && !indirectEnabled &&
            settings.sampling.radius >= 8.f &&
            !view->IsOrthographicProjection();

        VisibilityPerformanceWorkload performanceWorkload;
        performanceWorkload.consumer = GetPerformanceConsumer(
            ambientEnabled, indirectEnabled);
        performanceWorkload.estimator = GetPerformanceEstimator(
            settings.estimator);
        performanceWorkload.resolution = GetPerformanceResolution(
            settings.resolution);
        performanceWorkload.scheduler = GetPerformanceScheduler(
            settings.sampling.scheduler);
        const VisibilityPerformanceProfileConfiguration selectedConfig =
            GetEffectiveVisibilityPerformanceConfiguration(settings);
        const bool useDepthHierarchy = legacyDepthHierarchyEligible &&
            selectedConfig.depth == VisibilityDepthMode::Legacy;
        performanceWorkload.firstBounceSampleCount = std::clamp(
            settings.sampling.maximumSampleCount, 1u, 64u);
        performanceWorkload.laterBounceSampleCount =
            GetIndirectBounceSampleCount(
                performanceWorkload.firstBounceSampleCount, 1u);
        performanceWorkload.bounceCount = activeBounceCount;
        performanceWorkload.temporalEnabled = temporalEnabled;
        performanceWorkload.spatialEnabled = spatialFilterEnabled;
        performanceWorkload.depthHierarchyEnabled = useDepthHierarchy;
        performanceWorkload.runtimeConfigurationKey =
            GetVisibilityRuntimeConfigurationKey(settings);
        performanceWorkload.outputWidth = fullSize.x;
        performanceWorkload.outputHeight = fullSize.y;
        performanceWorkload.radius = settings.sampling.radius;
        performanceWorkload.thickness = settings.sampling.thickness;
        performanceWorkload.radialExponent =
            settings.sampling.stepDistributionExponent;
        performanceWorkload.threadGroupSizeX = kThreadGroupSize;
        performanceWorkload.threadGroupSizeY = kThreadGroupSize;
        m_Timings.activeWorkload = performanceWorkload;
        m_Timings.hasActiveWorkload = true;

        const VisibilityExecutionPlan selectedPlan =
            ResolveVisibilityExecutionPlan(
                selectedConfig, performanceWorkload);
        const bool selectedPlanUsable = selectedPlan.valid;
        VisibilityExecutionPlan executionPlan = selectedPlan;
        if (!selectedPlanUsable)
        {
            VisibilityPerformanceWorkload referenceWorkload =
                performanceWorkload;
            referenceWorkload.scheduler = GetPerformanceScheduler(
                settings.sampling.scheduler);
            referenceWorkload.threadGroupSizeX = kThreadGroupSize;
            referenceWorkload.threadGroupSizeY = kThreadGroupSize;
            executionPlan = ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Reference,
                referenceWorkload);
            assert(executionPlan.valid);
        }
        if (m_ExecutionPlan.valid &&
            m_ExecutionPlan.historyResetKey != executionPlan.historyResetKey)
        {
            ResetHistory();
        }
        m_ExecutionPlan = executionPlan;

        const VisibilityPerformanceProfileConfiguration& performanceConfig =
            executionPlan.configuration;
        const bool fusedResolveApply = performanceConfig.application ==
                VisibilityApplicationMode::FusedResolveAndApplyExact ||
            performanceConfig.application ==
                VisibilityApplicationMode::FusedResolveAndApplyPackedEdges;
        const bool packedFastEnabled = performanceConfig.noise ==
            VisibilityNoiseDelivery::PackedCurrentFast;
        const bool packedEdgesEnabled =
            performanceConfig.reconstruction ==
                VisibilityReconstructionMode::PackedEdges2x2;
        const bool postProcessEnabled = legacyPostProcessRequested;
        const VisibilityPerformanceProfile activePerformanceProfile =
            performanceConfig.profile;
        uint32_t schedulerSpecialization = 0u;
        switch (executionPlan.workload.scheduler)
        {
        case VisibilityPerformanceScheduler::IndependentHash:
            schedulerSpecialization = 1u;
            break;
        case VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField:
            schedulerSpecialization = 2u;
            break;
        case VisibilityPerformanceScheduler::
                FilterAdaptedSpatiotemporalRankField:
            schedulerSpecialization = 3u;
            break;
        default:
            break;
        }
        const bool finalAmbientEnabled = postProcessEnabled &&
            ambientEnabled && !fusedResolveApply;
        const VisibilityBufferPrecisionSettings& bufferPrecision =
            settings.performance.bufferPrecision;

        EnsureResources(
            fullSize,
            resolutionScale,
            ambientEnabled,
            indirectEnabled,
            activeBounceCount > 1u,
            temporalEnabled,
            postProcessEnabled,
            useDepthHierarchy,
            finalAmbientEnabled,
            packedFastEnabled,
            packedEdgesEnabled,
            bufferPrecision);
        m_Timings.profileValid = selectedPlanUsable;
        m_Timings.profileError = selectedPlan.valid
            ? std::string() : selectedPlan.errorMessage;
        m_Timings.activePermutation = selectedPlanUsable
            ? selectedPlan.permutationName
            : std::string("Reference Fallback: ") +
                std::string(selectedConfig.name);
        m_Timings.activeSrvCount = executionPlan.firstTraceSrvCount;
        m_Timings.activeUavCount = executionPlan.firstTraceUavCount;
        m_Timings.peakSrvCount = executionPlan.peakSrvCount;
        m_Timings.peakUavCount = executionPlan.peakUavCount;
        m_Timings.activeDispatchCount = executionPlan.dispatchCount;
        if (m_HistoryEstimatorInitialized &&
            m_HistoryEstimator != settings.estimator)
        {
            // The three estimators store different measures. Mixing their raw
            // AO/GI values in temporal history produces a visible
            // transition and contaminates estimator A/B comparisons.
            ResetHistory();
        }
        m_HistoryEstimator = settings.estimator;
        m_HistoryEstimatorInitialized = true;
        UploadSamplingNoise(commandList);
        AdvanceTimers();

        const uint32_t consumerVariant =
            GetConsumerVariant(ambientEnabled, indirectEnabled);
        const uint32_t estimatorIndex = std::min(
            static_cast<uint32_t>(settings.estimator),
            ImplementedVisibilityEstimatorCount - 1u);
        const uint32_t historyWrite = temporalEnabled
            ? m_HistoryIndex : 0u;
        const uint32_t historyRead = 1u - historyWrite;

        ScreenSpaceVisibilityConstants constants{};
        view->FillPlanarViewConstants(constants.view);
        constants.fullResolution = float2(fullSize);
        constants.samplingResolution = float2(m_SamplingSize);
        constants.radiusWorld = std::max(settings.sampling.radius, 0.f);
        constants.thicknessWorld = std::max(
            settings.sampling.thickness, 0.f);
        constants.stepDistributionExponent = std::clamp(
            settings.sampling.stepDistributionExponent, 0.25f, 4.f);
        constants.ambientPower = std::clamp(
            settings.ambientOcclusion.power, 0.1f, 4.f);
        constants.ambientStrength = std::max(
            settings.ambientOcclusion.strength, 0.f);
        constants.indirectDiffuseIntensity = std::max(
            settings.indirectDiffuse.intensity, 0.f);
        constants.emissiveGain = VisibilityEmissiveSourceGain;
        constants.minimumBounceContribution = std::max(
            settings.indirectDiffuse.minimumBounceContribution, 0.f);
        constants.lightingExposureScale = std::max(exposureScale, 0.f);
        constants.temporalResponse = std::clamp(
            settings.reconstruction.temporalResponse, 0.f, 1.f);
        constants.spatialRadius = std::clamp(
            settings.reconstruction.spatialRadius, 0.f, 16.f);
        constants.ambientColorTop = ambientColorTop;
        constants.ambientColorBottom = ambientColorBottom;
        constants.frameIndex = frameIndex;
        constants.maximumSampleCount = std::clamp(
            settings.sampling.maximumSampleCount, 1u, 64u);
        constants.knownInactiveLightingSources =
            knownInactiveLightingSources;
        constants.enableAmbientOcclusion = ambientEnabled ? 1u : 0u;
        constants.enableIndirectDiffuse = indirectEnabled ? 1u : 0u;
        constants.includeEmissive = 1u;
        constants.reverseDepth = view->IsReverseDepth() ? 1u : 0u;
        constants.orthographicProjection =
            view->IsOrthographicProjection() ? 1u : 0u;
        constants.useDepthHierarchy = useDepthHierarchy ? 1u : 0u;
        constants.resolutionScale = resolutionScale;
        constants.sampleScheduler = static_cast<uint32_t>(
            settings.sampling.scheduler);
        constants.showIndirectDiffuseOnly =
            settings.showIndirectDiffuseOnly ? 1u : 0u;
        constants.packedEdgeMode = static_cast<uint32_t>(
            settings.performance.packedEdgeMode) + 1u;
        const uint64_t historyConfigurationKey =
            BuildHistoryConfigurationKey(constants, activeBounceCount);
        if (m_HistoryConfigurationInitialized &&
            historyConfigurationKey != m_HistoryConfigurationKey)
        {
            ResetHistory();
        }
        m_HistoryConfigurationKey = historyConfigurationKey;
        m_HistoryConfigurationInitialized = true;
        constants.historyValid = temporalEnabled && m_HistoryValid ? 1u : 0u;
        BeginStage(commandList, Stage::EffectEnvelope);
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));

        const uint32_t traceDispatchX =
            (m_SamplingSize.x + kThreadGroupSize - 1u) / kThreadGroupSize;
        const uint32_t traceDispatchY =
            (m_SamplingSize.y + kThreadGroupSize - 1u) / kThreadGroupSize;
        const uint32_t samplingDispatchX =
            (m_SamplingSize.x + kThreadGroupSize - 1u) / kThreadGroupSize;
        const uint32_t samplingDispatchY =
            (m_SamplingSize.y + kThreadGroupSize - 1u) / kThreadGroupSize;
        const uint32_t fullDispatchX =
            (fullSize.x + kThreadGroupSize - 1u) / kThreadGroupSize;
        const uint32_t fullDispatchY =
            (fullSize.y + kThreadGroupSize - 1u) / kThreadGroupSize;

        nvrhi::ITexture* rawAmbient = m_RawAmbientVisibility
            ? m_RawAmbientVisibility.Get()
            : m_DummyAmbientVisibility.Get();
        nvrhi::ITexture* rawIndirect = m_RawIndirectDiffuse[0]
            ? m_RawIndirectDiffuse[0].Get()
            : m_DummyIndirectDiffuse.Get();
        nvrhi::ITexture* sourceRadiance = inputs.sourceRadiance
            ? inputs.sourceRadiance
            : m_DummyVector.Get();
        nvrhi::ITexture* motion = motionAvailable
            ? inputs.motionVectors
            : m_DummyVector.Get();
        nvrhi::ITexture* hierarchy = useDepthHierarchy
            ? m_DepthHierarchyTexture.Get()
            : inputs.depth;

        commandList->beginMarker(indirectEnabled
            ? (ambientEnabled
                ? "Screen-Space Visibility (AO + GI)"
                : "Screen-Space Visibility (GI)")
            : "Screen-Space Visibility (AO)");

        if (useDepthHierarchy)
        {
            if (!m_DepthHierarchyBindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth)
                };
                for (uint32_t mip = 0u; mip < 5u; ++mip)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_UAV(
                            0, m_DepthHierarchyTexture)
                            .setArrayElement(mip)
                            .setSubresources(nvrhi::TextureSubresourceSet(
                                mip, 1, 0, 1)));
                }
                m_DepthHierarchyBindingSet = m_Device->createBindingSet(
                    bindings, m_DepthHierarchy.bindingLayout);
            }

            nvrhi::ComputeState state;
            state.pipeline = m_DepthHierarchy.pipeline;
            state.bindings = { m_DepthHierarchyBindingSet };
            commandList->beginMarker("Visibility Depth Hierarchy");
            BeginStage(commandList, Stage::DepthHierarchy);
            commandList->setComputeState(state);
            commandList->dispatch(
                (fullSize.x + 15u) / 16u,
                (fullSize.y + 15u) / 16u,
                1u);
            EndStage(commandList, Stage::DepthHierarchy);
            commandList->endMarker();
        }
        else
        {
            m_Timings.depthHierarchyMs = 0.f;
        }

        commandList->beginMarker(activeBounceCount > 1u
            ? "Visibility Sampling (Multiple Bounces)"
            : "Visibility Sampling");
        const bool writeBounceMetadata = activeBounceCount > 1u;
        Pipeline* firstPipeline = nullptr;
        nvrhi::BindingSetHandle* firstBindingSet = nullptr;
        const char* advancedFirstShader = nullptr;
        std::vector<ShaderMacro> advancedFirstMacros;
        // Composed legacy controls use the packaged generic/fixed families.
        // Their math and traversal switches are uniform constants, avoiding a
        // Cartesian shader-permutation explosion.
        if (packedFastEnabled)
        {
            advancedFirstShader =
                "uvsr/screen_space_visibility_composed_packed_fast_cs.hlsl";
            const uint32_t fixedSampleCount =
                performanceConfig.trace ==
                        VisibilityTraceImplementation::
                            FixedInterleavedBitmask
                    ? executionPlan.fixedFirstBounceSampleCount
                    : 0u;
            advancedFirstMacros = {
                { "VISIBILITY_ESTIMATOR",
                    std::to_string(estimatorIndex) },
                { "ENABLE_AO", ambientEnabled ? "1" : "0" },
                { "ENABLE_GI", indirectEnabled ? "1" : "0" },
                { "ENABLE_BOUNCE_REINJECTION", "0" },
                { "INITIALIZE_BOUNCE_CUMULATIVE", "0" },
                { "ENABLE_BOUNCE_METADATA",
                    writeBounceMetadata ? "1" : "0" },
                { "FIXED_SAMPLE_COUNT",
                    std::to_string(fixedSampleCount) },
                { "FIXED_RADIAL_EXPONENT_TWO",
                    fixedSampleCount != 0u ? "1" : "0" },
                { "FIXED_DIRECT_DEPTH", useDepthHierarchy ? "0" : "1" },
                { "OUTPUT_PACKED_EDGES",
                    packedEdgesEnabled ? "1" : "0" }
            };
        }
        else if (packedEdgesEnabled)
        {
            advancedFirstShader =
                "uvsr/screen_space_visibility_composed_edges_cs.hlsl";
            const uint32_t fixedSampleCount =
                performanceConfig.trace ==
                        VisibilityTraceImplementation::
                            FixedInterleavedBitmask
                    ? executionPlan.fixedFirstBounceSampleCount
                    : 0u;
            advancedFirstMacros = {
                { "VISIBILITY_ESTIMATOR",
                    std::to_string(estimatorIndex) },
                { "ENABLE_AO", ambientEnabled ? "1" : "0" },
                { "ENABLE_GI", indirectEnabled ? "1" : "0" },
                { "ENABLE_BOUNCE_REINJECTION", "0" },
                { "INITIALIZE_BOUNCE_CUMULATIVE", "0" },
                { "ENABLE_BOUNCE_METADATA",
                    writeBounceMetadata ? "1" : "0" },
                { "FIXED_SAMPLE_COUNT",
                    std::to_string(fixedSampleCount) },
                { "FIXED_RADIAL_EXPONENT_TWO",
                    fixedSampleCount != 0u ? "1" : "0" },
                { "FIXED_DIRECT_DEPTH", useDepthHierarchy ? "0" : "1" },
                { "SCHEDULER_SPECIALIZATION",
                    std::to_string(schedulerSpecialization) }
            };
        }
        else
        {
            switch (performanceConfig.trace)
            {
            case VisibilityTraceImplementation::FixedInterleavedBitmask:
                advancedFirstShader =
                    "uvsr/screen_space_visibility_fixed_cs.hlsl";
                advancedFirstMacros = {
                    { "VISIBILITY_ESTIMATOR",
                        std::to_string(estimatorIndex) },
                    { "ENABLE_AO", ambientEnabled ? "1" : "0" },
                    { "ENABLE_GI", indirectEnabled ? "1" : "0" },
                    { "ENABLE_BOUNCE_REINJECTION", "0" },
                    { "INITIALIZE_BOUNCE_CUMULATIVE", "0" },
                    { "ENABLE_BOUNCE_METADATA",
                        writeBounceMetadata ? "1" : "0" },
                    { "FIXED_DIRECT_DEPTH",
                        useDepthHierarchy ? "0" : "1" },
                    { "SCHEDULER_SPECIALIZATION",
                        std::to_string(schedulerSpecialization) },
                    { "FIXED_SAMPLE_COUNT", std::to_string(
                        executionPlan.fixedFirstBounceSampleCount) }
                };
                break;
            default:
                break;
            }
        }
        if (advancedFirstShader)
        {
            // Advanced permutations use their reflected resource subset rather
            // than inheriting the generic shader's dummy-resource layout. This
            // is the concrete off-state/minimal-binding experiment: fixed AO,
            // for example, binds depth, normals, the selected scalar noise
            // sources, and its AO UAV only.
            std::vector<nvrhi::BindingLayoutItem> layout = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)
            };
            layout.push_back(nvrhi::BindingLayoutItem::Texture_SRV(0));
            layout.push_back(nvrhi::BindingLayoutItem::Texture_SRV(1));
            if (indirectEnabled)
                layout.push_back(
                    nvrhi::BindingLayoutItem::Texture_SRV(2));
            if (useDepthHierarchy)
                layout.push_back(
                    nvrhi::BindingLayoutItem::Texture_SRV(3));

            if (packedFastEnabled)
            {
                layout.push_back(
                    nvrhi::BindingLayoutItem::Texture_SRV(6));
            }
            else if (schedulerSpecialization == 2u)
            {
                layout.push_back(
                    nvrhi::BindingLayoutItem::Texture_SRV(5));
            }
            else if (schedulerSpecialization == 3u)
            {
                layout.push_back(
                    nvrhi::BindingLayoutItem::Texture_SRV(6));
            }
            if (ambientEnabled)
                layout.push_back(
                    nvrhi::BindingLayoutItem::Texture_UAV(0));
            if (indirectEnabled)
                layout.push_back(
                    nvrhi::BindingLayoutItem::Texture_UAV(1));
            if (packedEdgesEnabled)
                layout.push_back(
                    nvrhi::BindingLayoutItem::Texture_UAV(3));
            const uint64_t pipelineKey = AdvancedPipelineKey(
                executionPlan.shaderPermutationKey, 1u);
            firstPipeline = &GetOrCreateAdvancedPipeline(
                pipelineKey, advancedFirstShader, layout,
                &advancedFirstMacros);
            const uint64_t bindingKey = AdvancedPipelineKey(
                pipelineKey, 0x100u);
            firstBindingSet = std::addressof(
                m_AdvancedBindingSets[bindingKey]);
        }
        else
        {
            firstPipeline = writeBounceMetadata
                ? &m_MultiBounceFirstSampling[estimatorIndex]
                    [ambientEnabled ? 1u : 0u]
                : &m_Sampling[estimatorIndex][consumerVariant];
            firstBindingSet = writeBounceMetadata
                ? std::addressof(
                    m_MultiBounceFirstBindingSets[estimatorIndex]
                        [ambientEnabled ? 1u : 0u])
                : std::addressof(
                    m_SamplingBindingSets[estimatorIndex]
                        [consumerVariant]);
        }

        if (!*firstBindingSet)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer)
            };
            if (advancedFirstShader)
            {
                bindings.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth));
                bindings.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_SRV(1, inputs.normals));
                if (indirectEnabled)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(
                        2, sourceRadiance));
                }
                if (useDepthHierarchy)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(
                            3, hierarchy));
                }
                if (packedFastEnabled)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(
                            6, m_PackedFastNoiseTexture));
                }
                else if (schedulerSpecialization == 2u)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(
                            5, m_BlueNoiseTexture));
                }
                else if (schedulerSpecialization == 3u)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(
                            6, m_FilterAdaptedNoiseTexture));
                }
                if (ambientEnabled)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_UAV(
                            0, rawAmbient));
                }
                if (indirectEnabled)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_UAV(
                            1, rawIndirect));
                }
                if (packedEdgesEnabled)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_UAV(
                            3, m_PackedEdgesTexture));
                }
            }
            else
            {
                bindings.bindings.insert(bindings.bindings.end(), {
                    nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth),
                    nvrhi::BindingSetItem::Texture_SRV(1, inputs.normals),
                    nvrhi::BindingSetItem::Texture_SRV(2, sourceRadiance),
                    nvrhi::BindingSetItem::Texture_SRV(3, hierarchy),
                    nvrhi::BindingSetItem::Texture_SRV(4, m_DummyVector),
                    nvrhi::BindingSetItem::Texture_SRV(
                        5, m_BlueNoiseTexture),
                    nvrhi::BindingSetItem::Texture_SRV(
                        6, m_FilterAdaptedNoiseTexture),
                    nvrhi::BindingSetItem::Texture_SRV(7, m_DummyVector),
                    nvrhi::BindingSetItem::Texture_UAV(0, rawAmbient),
                    nvrhi::BindingSetItem::Texture_UAV(1, rawIndirect),
                    nvrhi::BindingSetItem::Texture_UAV(
                        2, m_DummyIndirectOutput)
                });
            }
            *firstBindingSet = m_Device->createBindingSet(
                bindings, firstPipeline->bindingLayout);
        }

        {
            nvrhi::ComputeState state;
            state.pipeline = firstPipeline->pipeline;
            state.bindings = { *firstBindingSet };
            BeginStage(commandList, Stage::FirstTrace);
            commandList->setComputeState(state);
            commandList->dispatch(
                traceDispatchX, traceDispatchY, 1u);
            EndStage(commandList, Stage::FirstTrace);
        }

        const uint32_t primaryMaximumSampleCount =
            constants.maximumSampleCount;
        const float boundedBounceContribution =
            std::max(settings.indirectDiffuse.minimumBounceContribution, 0.f);
        const float contributionTerminatedBase =
            std::max(boundedBounceContribution,
                MinimumContributionTerminatedThreshold);
        if (contributionTerminatedBounces && activeBounceCount > 1u)
        {
            nvrhi::DispatchIndirectArguments maximumDispatch;
            maximumDispatch.setGroups3D(
                traceDispatchX, traceDispatchY, 1u);
            const std::array<nvrhi::DispatchIndirectArguments, 2>
                initialArguments = { maximumDispatch, maximumDispatch };
            commandList->clearBufferUInt(m_BounceContinuation, 0u);
            commandList->writeBuffer(
                m_BounceIndirectArguments,
                initialArguments.data(),
                sizeof(initialArguments));
        }
        if (activeBounceCount > 1u)
            BeginStage(commandList, Stage::LaterTrace);
        else
        {
            m_Timings.laterTraceMs = 0.f;
            m_Timings.laterBounceMs.fill(0.f);
        }
        for (uint32_t bounceIndex = 1u;
            bounceIndex < activeBounceCount;
            ++bounceIndex)
        {
            const bool instrumentBounce =
                bounceIndex <= InstrumentedLaterBounceCount;
            const Stage bounceStage = static_cast<Stage>(
                static_cast<uint32_t>(Stage::LaterTraceBounce2) +
                bounceIndex - 1u);
            if (instrumentBounce)
                BeginStage(commandList, bounceStage);
            const uint32_t outputIndex = bounceIndex & 1u;
            const uint32_t previousIndex = 1u - outputIndex;
            constants.maximumSampleCount = GetIndirectBounceSampleCount(
                primaryMaximumSampleCount, bounceIndex);
            constants.minimumBounceContribution =
                contributionTerminatedBounces
                ? contributionTerminatedBase * std::pow(
                    ContributionTerminatedThresholdGrowth,
                    float(bounceIndex - 1u))
                : boundedBounceContribution;
            commandList->writeBuffer(
                m_ConstantBuffer, &constants, sizeof(constants));

            const uint32_t cumulativeMode = bounceIndex == 1u ? 0u : 1u;
            const uint32_t bounceRotation = bounceIndex - 1u;
            Pipeline* pipeline = nullptr;
            nvrhi::BindingSetHandle* bindingSet = nullptr;
            const bool fixedLaterTrace =
                executionPlan.fixedLaterBounceSampleCount != 0u;
            if (fixedLaterTrace || contributionTerminatedBounces)
            {
                std::vector<ShaderMacro> macros = {
                    { "VISIBILITY_ESTIMATOR",
                        std::to_string(estimatorIndex) },
                    { "ENABLE_AO", "0" },
                    { "ENABLE_GI", "1" },
                    { "ENABLE_BOUNCE_REINJECTION", "1" },
                    { "INITIALIZE_BOUNCE_CUMULATIVE",
                        cumulativeMode == 0u ? "1" : "0" },
                    { "ENABLE_BOUNCE_METADATA", "0" },
                    { "ENABLE_BOUNCE_CONTINUATION",
                        contributionTerminatedBounces ? "1" : "0" }
                };
                const char* shader = fixedLaterTrace
                    ? "uvsr/screen_space_visibility_fixed_cs.hlsl"
                    : "uvsr/screen_space_visibility_cs.hlsl";
                std::vector<nvrhi::BindingLayoutItem> layout;
                if (fixedLaterTrace)
                {
                    macros.push_back(
                        { "SCHEDULER_SPECIALIZATION",
                            std::to_string(schedulerSpecialization) });
                    macros.push_back(
                        { "FIXED_SAMPLE_COUNT", std::to_string(
                            executionPlan.fixedLaterBounceSampleCount) });
                    layout = {
                        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(1),
                        nvrhi::BindingLayoutItem::Texture_SRV(2),
                        nvrhi::BindingLayoutItem::Texture_SRV(4),
                        nvrhi::BindingLayoutItem::Texture_SRV(5),
                        nvrhi::BindingLayoutItem::Texture_SRV(6)
                    };
                    if (schedulerSpecialization == 2u)
                        layout.push_back(
                            nvrhi::BindingLayoutItem::Texture_SRV(8));
                    else if (schedulerSpecialization == 3u)
                        layout.push_back(
                            nvrhi::BindingLayoutItem::Texture_SRV(9));
                }
                else
                {
                    layout = {
                        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(1),
                        nvrhi::BindingLayoutItem::Texture_SRV(2),
                        nvrhi::BindingLayoutItem::Texture_SRV(3),
                        nvrhi::BindingLayoutItem::Texture_SRV(4),
                        nvrhi::BindingLayoutItem::Texture_SRV(5),
                        nvrhi::BindingLayoutItem::Texture_SRV(6),
                        nvrhi::BindingLayoutItem::Texture_SRV(7),
                        nvrhi::BindingLayoutItem::Texture_SRV(8),
                        nvrhi::BindingLayoutItem::Texture_SRV(9),
                        nvrhi::BindingLayoutItem::Texture_SRV(10)
                    };
                }
                layout.push_back(
                    nvrhi::BindingLayoutItem::Texture_UAV(0));
                layout.push_back(
                    nvrhi::BindingLayoutItem::Texture_UAV(1));
                if (contributionTerminatedBounces)
                {
                    layout.push_back(
                        nvrhi::BindingLayoutItem::RawBuffer_UAV(2));
                }
                const uint64_t pipelineKey = AdvancedPipelineKey(
                    executionPlan.shaderPermutationKey,
                    0x20u + cumulativeMode +
                        (contributionTerminatedBounces ? 0x10u : 0u));
                pipeline = &GetOrCreateAdvancedPipeline(
                    pipelineKey,
                    shader,
                    layout, &macros);
                const uint64_t bindingKey = AdvancedPipelineKey(
                    pipelineKey,
                    0x200u + bounceRotation);
                bindingSet = std::addressof(
                    m_AdvancedBindingSets[bindingKey]);
            }
            else
            {
                pipeline = &m_IndirectBounceSampling[estimatorIndex]
                    [cumulativeMode];
                bindingSet = std::addressof(
                    m_IndirectBounceBindingSets[estimatorIndex]
                        [cumulativeMode][bounceRotation]);
            }
            if (!*bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                if (fixedLaterTrace)
                {
                    bindings.bindings = {
                        nvrhi::BindingSetItem::ConstantBuffer(
                            0, m_ConstantBuffer),
                        nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth),
                        nvrhi::BindingSetItem::Texture_SRV(
                            1, inputs.normals),
                        nvrhi::BindingSetItem::Texture_SRV(
                            2, m_RawIndirectDiffuse[previousIndex]),
                    };
                    if (!fixedLaterTrace && useDepthHierarchy)
                    {
                        bindings.bindings.push_back(
                            nvrhi::BindingSetItem::Texture_SRV(
                                3, hierarchy));
                    }
                    bindings.bindings.insert(bindings.bindings.end(), {
                        nvrhi::BindingSetItem::Texture_SRV(
                            4, inputs.gbufferDiffuse),
                        nvrhi::BindingSetItem::Texture_SRV(
                            5, inputs.gbufferEmissive),
                        nvrhi::BindingSetItem::Texture_SRV(
                            6, inputs.materialAmbientOcclusion)
                    });
                    if (schedulerSpecialization == 2u)
                    {
                        bindings.bindings.push_back(
                            nvrhi::BindingSetItem::Texture_SRV(
                                8, m_BlueNoiseTexture));
                    }
                    else if (schedulerSpecialization == 3u)
                    {
                        bindings.bindings.push_back(
                            nvrhi::BindingSetItem::Texture_SRV(
                                9, m_FilterAdaptedNoiseTexture));
                    }
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_UAV(
                            0, m_RawIndirectDiffuse[outputIndex]));
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_UAV(
                            1, m_CumulativeIndirectDiffuse));
                    if (contributionTerminatedBounces)
                    {
                        bindings.bindings.push_back(
                            nvrhi::BindingSetItem::RawBuffer_UAV(
                                2, m_BounceContinuation));
                    }
                }
                else
                {
                    bindings.bindings = {
                        nvrhi::BindingSetItem::ConstantBuffer(
                            0, m_ConstantBuffer),
                        nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth),
                        nvrhi::BindingSetItem::Texture_SRV(
                            1, inputs.normals),
                        nvrhi::BindingSetItem::Texture_SRV(
                            2, m_RawIndirectDiffuse[previousIndex]),
                        nvrhi::BindingSetItem::Texture_SRV(3, hierarchy),
                        nvrhi::BindingSetItem::Texture_SRV(
                            4, inputs.gbufferDiffuse),
                        nvrhi::BindingSetItem::Texture_SRV(
                            5, inputs.gbufferEmissive),
                        nvrhi::BindingSetItem::Texture_SRV(
                            6, inputs.materialAmbientOcclusion),
                        nvrhi::BindingSetItem::Texture_SRV(
                            7, m_DummyVector),
                        nvrhi::BindingSetItem::Texture_SRV(
                            8, m_BlueNoiseTexture),
                        nvrhi::BindingSetItem::Texture_SRV(
                            9, m_FilterAdaptedNoiseTexture),
                        nvrhi::BindingSetItem::Texture_SRV(
                            10, m_DummyVector),
                        nvrhi::BindingSetItem::Texture_UAV(
                            0, m_RawIndirectDiffuse[outputIndex]),
                        nvrhi::BindingSetItem::Texture_UAV(
                            1, m_CumulativeIndirectDiffuse)
                    };
                    if (contributionTerminatedBounces)
                    {
                        bindings.bindings.push_back(
                            nvrhi::BindingSetItem::RawBuffer_UAV(
                                2, m_BounceContinuation));
                    }
                }
                *bindingSet = m_Device->createBindingSet(
                    bindings, pipeline->bindingLayout);
            }

            nvrhi::ComputeState state;
            state.pipeline = pipeline->pipeline;
            state.bindings = { *bindingSet };
            if (contributionTerminatedBounces)
                state.indirectParams = m_BounceIndirectArguments;
            commandList->setComputeState(state);
            if (contributionTerminatedBounces)
                commandList->dispatchIndirect(0u);
            else
                commandList->dispatch(
                    samplingDispatchX, samplingDispatchY, 1u);
            if (contributionTerminatedBounces)
            {
                nvrhi::ComputeState controlState;
                controlState.pipeline = m_BounceDispatchControl.pipeline;
                controlState.bindings = {
                    m_BounceDispatchControlBindingSet
                };
                commandList->setComputeState(controlState);
                commandList->dispatch(1u);
            }
            if (instrumentBounce)
                EndStage(commandList, bounceStage);
        }

        if (activeBounceCount > 1u)
        {
            for (uint32_t bounceIndex = activeBounceCount;
                bounceIndex <= InstrumentedLaterBounceCount;
                ++bounceIndex)
            {
                m_Timings.laterBounceMs[bounceIndex - 1u] = 0.f;
            }
            rawIndirect = m_CumulativeIndirectDiffuse;
            EndStage(commandList, Stage::LaterTrace);
        }
        commandList->endMarker();

        nvrhi::ITexture* reconstructedAmbient = rawAmbient;
        nvrhi::ITexture* reconstructedIndirect = rawIndirect;
        m_Timings.spatialDenoiseMs = 0.f;

        if (temporalEnabled)
        {
            const uint32_t sourceVariant =
                activeBounceCount > 1u ? 1u : 0u;
            Pipeline* pipeline = &m_Temporal[consumerVariant];
            nvrhi::BindingSetHandle* bindingSet = std::addressof(
                m_TemporalBindingSets[consumerVariant]
                    [sourceVariant][historyWrite]);
            if (!*bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(0, rawAmbient),
                    nvrhi::BindingSetItem::Texture_SRV(1, rawIndirect),
                    nvrhi::BindingSetItem::Texture_SRV(2, inputs.depth),
                    nvrhi::BindingSetItem::Texture_SRV(
                        3, inputs.normals),
                    nvrhi::BindingSetItem::Texture_SRV(4, motion),
                    nvrhi::BindingSetItem::Texture_SRV(5,
                        ambientEnabled
                            ? m_TemporalAmbientVisibility[historyRead].Get()
                            : m_DummyAmbientVisibility.Get()),
                    nvrhi::BindingSetItem::Texture_SRV(6,
                        indirectEnabled
                            ? m_TemporalIndirectDiffuse[historyRead].Get()
                            : m_DummyIndirectDiffuse.Get()),
                    nvrhi::BindingSetItem::Texture_SRV(
                        7, m_TemporalDepth[historyRead]),
                    nvrhi::BindingSetItem::Texture_SRV(
                        8, m_TemporalNormal[historyRead]),
                    nvrhi::BindingSetItem::Texture_UAV(0,
                        ambientEnabled
                            ? m_TemporalAmbientVisibility[historyWrite].Get()
                            : m_DummyAmbientOutput.Get()),
                    nvrhi::BindingSetItem::Texture_UAV(1,
                        indirectEnabled
                            ? m_TemporalIndirectDiffuse[historyWrite].Get()
                            : m_DummyIndirectOutput.Get()),
                    nvrhi::BindingSetItem::Texture_UAV(
                        2, m_TemporalDepth[historyWrite]),
                    nvrhi::BindingSetItem::Texture_UAV(
                        3, m_TemporalNormal[historyWrite])
                };
                *bindingSet = m_Device->createBindingSet(
                    bindings, pipeline->bindingLayout);
            }

            nvrhi::ComputeState state;
            state.pipeline = pipeline->pipeline;
            state.bindings = { *bindingSet };
            commandList->beginMarker("Visibility Temporal Reconstruction");
            BeginStage(commandList, Stage::Temporal);
            commandList->setComputeState(state);
            commandList->dispatch(
                samplingDispatchX, samplingDispatchY, 1u);
            EndStage(commandList, Stage::Temporal);
            commandList->endMarker();

            if (ambientEnabled)
            {
                reconstructedAmbient =
                    m_TemporalAmbientVisibility[historyWrite];
            }
            if (indirectEnabled)
            {
                reconstructedIndirect =
                    m_TemporalIndirectDiffuse[historyWrite];
            }
        }
        else
        {
            m_Timings.temporalMs = 0.f;
            m_HistoryValid = false;
        }

        if (fusedResolveApply)
        {
            const bool fusedPackedEdges = performanceConfig.application ==
                VisibilityApplicationMode::FusedResolveAndApplyPackedEdges;
            std::vector<nvrhi::BindingLayoutItem> layout = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0)
            };
            if (!fusedPackedEdges)
                layout.push_back(nvrhi::BindingLayoutItem::Texture_SRV(1));
            layout.insert(layout.end(), {
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_SRV(3),
                nvrhi::BindingLayoutItem::Texture_SRV(4),
                nvrhi::BindingLayoutItem::Texture_SRV(5),
                nvrhi::BindingLayoutItem::Texture_SRV(6),
                nvrhi::BindingLayoutItem::Texture_SRV(7)
            });
            if (fusedPackedEdges)
                layout.push_back(nvrhi::BindingLayoutItem::Texture_SRV(8));
            layout.push_back(nvrhi::BindingLayoutItem::Texture_UAV(0));
            std::vector<ShaderMacro> macros = {
                {
                    "FUSED_PACKED_EDGE_RECONSTRUCTION",
                    fusedPackedEdges
                        ? (performanceConfig.reconstruction ==
                                VisibilityReconstructionMode::PackedEdges2x2
                            ? "1" : "2")
                        : "0"
                },
                {
                    "ENABLE_AO_POWER",
                    ambientPowerEnabled ? "1" : "0"
                }
            };
            const uint64_t pipelineKey = AdvancedPipelineKey(
                executionPlan.shaderPermutationKey, 0x60u);
            Pipeline& pipeline = GetOrCreateAdvancedPipeline(
                pipelineKey,
                "uvsr/screen_space_visibility_fused_apply_cs.hlsl",
                layout, &macros);
            const uint32_t sourceVariant = temporalEnabled
                ? 2u + historyWrite : 0u;
            const uint64_t bindingKey = AdvancedPipelineKey(
                pipelineKey, 0x600u + sourceVariant);
            nvrhi::BindingSetHandle& bindingSet =
                m_AdvancedBindingSets[bindingKey];
            if (!bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(
                        0, reconstructedAmbient)
                };
                if (!fusedPackedEdges)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(1, inputs.depth));
                }
                bindings.bindings.insert(bindings.bindings.end(), {
                    nvrhi::BindingSetItem::Texture_SRV(2, inputs.normals),
                    nvrhi::BindingSetItem::Texture_SRV(
                        3, inputs.baseLighting),
                    nvrhi::BindingSetItem::Texture_SRV(
                        4, inputs.gbufferDiffuse),
                    nvrhi::BindingSetItem::Texture_SRV(
                        5, inputs.gbufferEmissive),
                    nvrhi::BindingSetItem::Texture_SRV(
                        6, inputs.materialAmbientOcclusion),
                    nvrhi::BindingSetItem::Texture_SRV(
                        7, inputs.gbufferSpecular),
                });
                if (fusedPackedEdges)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(
                            8, m_PackedEdgesTexture));
                }
                bindings.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_UAV(0, inputs.output));
                bindingSet = m_Device->createBindingSet(
                    bindings, pipeline.bindingLayout);
            }
            nvrhi::ComputeState state;
            state.pipeline = pipeline.pipeline;
            state.bindings = { bindingSet };
            commandList->beginMarker(
                "Visibility Fused Resolve And Full-Resolution Apply");
            BeginStage(commandList, Stage::FullResolutionApply);
            commandList->setComputeState(state);
            commandList->dispatch(fullDispatchX, fullDispatchY, 1u);
            EndStage(commandList, Stage::FullResolutionApply);
            commandList->endMarker();
            m_Timings.fusedSpatialDenoiseUpsampleMs = 0.f;
            m_Timings.requiredUpsampleMs = 0.f;
        }
        else if (postProcessEnabled)
        {
            uint32_t sourceVariant = activeBounceCount > 1u ? 1u : 0u;
            if (temporalEnabled)
                sourceVariant = 2u + historyWrite;
            const uint32_t filterIndex = std::min(
                spatialFilterEnabled
                ? static_cast<uint32_t>(
                    settings.reconstruction.spatialFilter)
                : 0u,
                1u);
            Pipeline* pipeline = nullptr;
            nvrhi::BindingSetHandle* bindingSet = nullptr;
            const char* advancedFilterShader = nullptr;
            std::vector<ShaderMacro> advancedFilterMacros;
            if (packedEdgesEnabled)
            {
                advancedFilterShader =
                    "uvsr/screen_space_visibility_filter_packed_edge_cs.hlsl";
                advancedFilterMacros = {
                    { "ENABLE_AO", ambientEnabled ? "1" : "0" },
                    { "ENABLE_GI", indirectEnabled ? "1" : "0" },
                    { "PACKED_EDGE_RECONSTRUCTION", "1" },
                    { "PACKED_EDGE_CONTROLLED_LEAKAGE", "0" }
                };
                if (settings.performance.packedEdgeMode ==
                    VisibilityPackedEdgeMode::ControlledLeakage)
                {
                    advancedFilterMacros[3].definition = "1";
                }
            }
            if (advancedFilterShader)
            {
                if (!packedEdgesEnabled)
                {
                    advancedFilterMacros.push_back({
                        "ENABLE_AO", ambientEnabled ? "1" : "0" });
                    advancedFilterMacros.push_back({
                        "ENABLE_GI", indirectEnabled ? "1" : "0" });
                }
                std::vector<nvrhi::BindingLayoutItem> layout = {
                    nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                    nvrhi::BindingLayoutItem::Texture_SRV(0)
                };
                if (indirectEnabled)
                    layout.push_back(
                        nvrhi::BindingLayoutItem::Texture_SRV(1));
                if (packedEdgesEnabled)
                {
                    layout.push_back(
                        nvrhi::BindingLayoutItem::Texture_SRV(4));
                }
                layout.push_back(nvrhi::BindingLayoutItem::Texture_UAV(0));
                if (indirectEnabled)
                    layout.push_back(
                        nvrhi::BindingLayoutItem::Texture_UAV(1));
                const uint64_t pipelineKey = AdvancedPipelineKey(
                    executionPlan.shaderPermutationKey,
                    0x50u + filterIndex);
                pipeline = &GetOrCreateAdvancedPipeline(
                    pipelineKey, advancedFilterShader, layout,
                    &advancedFilterMacros);
                const uint64_t bindingKey = AdvancedPipelineKey(
                    pipelineKey, 0x500u + sourceVariant);
                bindingSet = std::addressof(
                    m_AdvancedBindingSets[bindingKey]);
            }
            else
            {
                pipeline = &m_Filter[filterIndex][consumerVariant];
                bindingSet = std::addressof(
                    m_FilterBindingSets[filterIndex]
                        [consumerVariant][sourceVariant]);
            }
            if (!*bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                if (advancedFilterShader)
                {
                    bindings.bindings = {
                        nvrhi::BindingSetItem::ConstantBuffer(
                            0, m_ConstantBuffer),
                        nvrhi::BindingSetItem::Texture_SRV(
                            0, reconstructedAmbient)
                    };
                    if (indirectEnabled)
                    {
                        bindings.bindings.push_back(
                            nvrhi::BindingSetItem::Texture_SRV(
                                1, reconstructedIndirect));
                    }
                    if (packedEdgesEnabled)
                    {
                        bindings.bindings.push_back(
                            nvrhi::BindingSetItem::Texture_SRV(
                                4, m_PackedEdgesTexture));
                    }
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_UAV(
                            0, ambientEnabled
                                ? m_FinalAmbientVisibility.Get()
                                : m_DummyAmbientOutput.Get()));
                    if (indirectEnabled)
                    {
                        bindings.bindings.push_back(
                            nvrhi::BindingSetItem::Texture_UAV(
                                1, m_FinalIndirectDiffuse));
                    }
                }
                else
                {
                    bindings.bindings = {
                        nvrhi::BindingSetItem::ConstantBuffer(
                            0, m_ConstantBuffer),
                        nvrhi::BindingSetItem::Texture_SRV(
                            0, reconstructedAmbient),
                        nvrhi::BindingSetItem::Texture_SRV(
                            1, reconstructedIndirect),
                        nvrhi::BindingSetItem::Texture_SRV(
                            2, inputs.depth),
                        nvrhi::BindingSetItem::Texture_SRV(
                            3, inputs.normals),
                        nvrhi::BindingSetItem::Texture_UAV(0,
                            ambientEnabled
                                ? m_FinalAmbientVisibility.Get()
                                : m_DummyAmbientOutput.Get()),
                        nvrhi::BindingSetItem::Texture_UAV(1,
                            indirectEnabled
                                ? m_FinalIndirectDiffuse.Get()
                                : m_DummyIndirectOutput.Get())
                    };
                }
                *bindingSet = m_Device->createBindingSet(
                    bindings, pipeline->bindingLayout);
            }

            nvrhi::ComputeState state;
            state.pipeline = pipeline->pipeline;
            state.bindings = { *bindingSet };
            const bool fusedSpatialDenoiseUpsample =
                spatialFilterEnabled;
            const Stage resolveStage = fusedSpatialDenoiseUpsample
                ? Stage::FusedSpatialDenoiseUpsample
                : Stage::RequiredUpsample;
            commandList->beginMarker(fusedSpatialDenoiseUpsample
                ? "Visibility Fused Spatial Denoise And Upsample"
                : "Visibility Required Guide-Aware Upsample");
            BeginStage(commandList, resolveStage);
            commandList->setComputeState(state);
            commandList->dispatch(fullDispatchX, fullDispatchY, 1u);
            EndStage(commandList, resolveStage);
            commandList->endMarker();
            if (fusedSpatialDenoiseUpsample)
                m_Timings.requiredUpsampleMs = 0.f;
            else
                m_Timings.fusedSpatialDenoiseUpsampleMs = 0.f;

            if (ambientEnabled)
                reconstructedAmbient = m_FinalAmbientVisibility;
            if (indirectEnabled)
                reconstructedIndirect = m_FinalIndirectDiffuse;
            m_Timings.fullResolutionApplyMs = 0.f;
        }
        else
        {
            m_Timings.fusedSpatialDenoiseUpsampleMs = 0.f;
            m_Timings.requiredUpsampleMs = 0.f;
            m_Timings.fullResolutionApplyMs = 0.f;
        }

        if (fusedResolveApply)
        {
            m_Timings.compositionMs = 0.f;
        }
        else
        {
            uint32_t compositeVariant = 0u;
            if (!postProcessEnabled)
            {
                if (temporalEnabled)
                    compositeVariant = 2u + historyWrite;
                else if (activeBounceCount > 1u)
                    compositeVariant = 1u;
            }
            const uint32_t aoPowerVariant =
                ambientPowerEnabled ? 1u : 0u;
            nvrhi::BindingSetHandle& compositeBindingSet =
                m_CompositeBindingSets[aoPowerVariant][compositeVariant];
            if (!compositeBindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(
                        0, inputs.baseLighting),
                    nvrhi::BindingSetItem::Texture_SRV(
                        1, reconstructedAmbient),
                    nvrhi::BindingSetItem::Texture_SRV(
                        2, reconstructedIndirect),
                    nvrhi::BindingSetItem::Texture_SRV(
                        3, inputs.gbufferDiffuse),
                    nvrhi::BindingSetItem::Texture_SRV(
                        4, inputs.gbufferEmissive),
                    nvrhi::BindingSetItem::Texture_SRV(
                        5, inputs.materialAmbientOcclusion),
                    nvrhi::BindingSetItem::Texture_SRV(
                        6, inputs.normals),
                    nvrhi::BindingSetItem::Texture_SRV(
                        7, inputs.gbufferSpecular),
                    nvrhi::BindingSetItem::Texture_UAV(0, inputs.output)
                };
                compositeBindingSet = m_Device->createBindingSet(
                    bindings,
                    m_Composite[aoPowerVariant].bindingLayout);
            }
            nvrhi::ComputeState state;
            state.pipeline = m_Composite[aoPowerVariant].pipeline;
            state.bindings = { compositeBindingSet };
            commandList->beginMarker("Screen-Space Indirect Composite");
            BeginStage(commandList, Stage::Composition);
            commandList->setComputeState(state);
            commandList->dispatch(fullDispatchX, fullDispatchY, 1u);
            EndStage(commandList, Stage::Composition);
            commandList->endMarker();
        }

        commandList->endMarker();
        EndStage(commandList, Stage::EffectEnvelope);
        if (temporalEnabled)
        {
            m_HistoryValid = true;
            m_HistoryIndex = 1u - historyWrite;
        }
        if (m_TimerFrameWritable)
            ++m_TimerFrame;
    }
}
