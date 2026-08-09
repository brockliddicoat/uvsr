#include "screen_space_visibility.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "screen_space_visibility_cb.h"

static_assert(
    offsetof(ScreenSpaceVisibilityConstants, packedEdgeMode) ==
        offsetof(ScreenSpaceVisibilityConstants, visibilityDebugView) +
            4u);
static_assert(
    offsetof(ScreenSpaceVisibilityConstants, specularEnvironmentEnabled) ==
        offsetof(ScreenSpaceVisibilityConstants, packedEdgeMode) + 16u);
static_assert(
    offsetof(ScreenSpaceVisibilityConstants, specularEnvironmentArrayIndex) +
            16u ==
        sizeof(ScreenSpaceVisibilityConstants));
static_assert(
    sizeof(ScreenSpaceVisibilityConstants) ==
        sizeof(PlanarViewConstants) + 128u,
    "Visibility constants must occupy eight registers after the view.");
static_assert(sizeof(ScreenSpaceVisibilityConstants) % 16u == 0u);
static_assert(static_cast<uint32_t>(
    uvsr::NoisePattern::SpatialWhite) == 0u);
static_assert(static_cast<uint32_t>(
    uvsr::NoisePattern::SpatialBlue) == 1u);
static_assert(static_cast<uint32_t>(
    uvsr::NoisePattern::SpatiotemporalBlue) == 2u);
static_assert(static_cast<uint32_t>(
    uvsr::VisibilityReconstructionMode::PackedDepthNormal) == 1u);
static_assert(static_cast<uint32_t>(
    uvsr::VisibilityReconstructionMode::PackedControlledLeakage) == 3u);

namespace
{
    constexpr uint32_t kThreadGroupSize = 8u;

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

    uint64_t TextureBytes(uint2 size, uint32_t bytesPerPixel)
    {
        return uint64_t(size.x) * uint64_t(size.y) * bytesPerPixel;
    }

    uint32_t GetRuntimeSampleParity(
        const uvsr::ScreenSpaceVisibilitySettings& settings,
        bool ambientEnabled,
        bool indirectEnabled,
        bool packedEdgesEnabled)
    {
        if (packedEdgesEnabled || !ambientEnabled || !indirectEnabled ||
            settings.estimator !=
                uvsr::VisibilityEstimator::UniformSolidAngle)
        {
            return 0u;
        }

        return (std::clamp(settings.sampling.maximumSampleCount, 1u, 64u) &
                1u) == 0u
            ? 1u
            : 2u;
    }

    uint64_t TracePipelineKey(
        uint32_t estimator,
        uint32_t consumer,
        uint32_t runtimeSampleParity,
        bool packedEdgesEnabled,
        bool ambientHitDistanceEnabled,
        bool indirectHitDistanceEnabled)
    {
        return 0x1000000000000000ull |
            uint64_t(estimator) |
            (uint64_t(consumer) << 2u) |
            (uint64_t(runtimeSampleParity) << 4u) |
            (uint64_t(packedEdgesEnabled) << 6u) |
            (uint64_t(ambientHitDistanceEnabled) << 7u) |
            (uint64_t(indirectHitDistanceEnabled) << 8u);
    }

    uint64_t PackedFilterPipelineKey(uint32_t consumer)
    {
        return 0x2000000000000000ull | uint64_t(consumer);
    }

    bool IsSkyVisibilityTextureCompatible(
        nvrhi::ITexture* texture,
        uint2 fullSize)
    {
        if (!texture)
            return false;

        const nvrhi::TextureDesc& description = texture->getDesc();
        return description.width == fullSize.x &&
            description.height == fullSize.y &&
            description.depth == 1u &&
            description.arraySize == 1u &&
            description.mipLevels == 1u &&
            description.sampleCount == 1u &&
            (description.format == nvrhi::Format::R8_UNORM ||
                description.format == nvrhi::Format::R16_FLOAT ||
                description.format == nvrhi::Format::RGBA16_FLOAT) &&
            description.dimension == nvrhi::TextureDimension::Texture2D &&
            description.isShaderResource;
    }

    bool IsFullResolutionProcessedSignal(
        nvrhi::ITexture* texture,
        uint2 fullSize)
    {
        if (!texture)
            return false;

        const nvrhi::TextureDesc& description = texture->getDesc();
        return description.width == fullSize.x &&
            description.height == fullSize.y &&
            description.depth == 1u &&
            description.arraySize == 1u &&
            description.mipLevels == 1u &&
            description.sampleCount == 1u &&
            description.dimension == nvrhi::TextureDimension::Texture2D &&
            description.isShaderResource;
    }
}

namespace uvsr
{
    void ApplyVisibilityBufferPrecisionPreset(
        VisibilityBufferPrecisionSettings& settings,
        bool use16BitAo,
        bool use16BitGi)
    {
        const VisibilityScalarBufferPrecision ambientPrecision = use16BitAo
            ? VisibilityScalarBufferPrecision::Float16
            : VisibilityScalarBufferPrecision::Float32;
        const VisibilityVectorBufferPrecision indirectPrecision = use16BitGi
            ? VisibilityVectorBufferPrecision::Rgba16Float
            : VisibilityVectorBufferPrecision::Rgba32Float;

        settings.ambient = ambientPrecision;
        settings.indirect = indirectPrecision;
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

        const bool outputAmbientHitDistance =
            settings.ambientOcclusion.outputHitDistance;
        const bool outputIndirectHitDistance =
            settings.indirectDiffuse.outputHitDistance;
        settings.enabled = true;
        settings.estimator = VisibilityEstimator::UniformSolidAngle;
        settings.resolution = VisibilityResolution::Full;
        settings.sampling.maximumSampleCount =
            DefaultVisibilitySampleCount;
        settings.sampling.radius = 3.f;
        settings.sampling.thickness = 0.5f;
        settings.sampling.stepDistributionExponent = 2.f;
        settings.ambientOcclusion = {};
        settings.ambientOcclusion.outputHitDistance =
            outputAmbientHitDistance;
        settings.indirectDiffuse = {};
        settings.indirectDiffuse.outputHitDistance =
            outputIndirectHitDistance;
        settings.reconstruction = {};
        const bool use16BitBuffers =
            quality != ScreenSpaceVisibilityQuality::Ultra;
        ApplyVisibilityBufferPrecisionPreset(
            settings.bufferPrecision,
            use16BitBuffers,
            use16BitBuffers);

        switch (quality)
        {
        case ScreenSpaceVisibilityQuality::Low:
            settings.resolution = VisibilityResolution::Quarter;
            settings.estimator = VisibilityEstimator::UniformProjectedAngle;
            settings.sampling.maximumSampleCount = 8u;
            settings.reconstruction.spatialEnabled = true;
            settings.reconstruction.spatialFilter =
                VisibilitySpatialFilter::JointBilateral;
            break;
        case ScreenSpaceVisibilityQuality::Medium:
            settings.resolution = VisibilityResolution::Half;
            settings.sampling.maximumSampleCount = 8u;
            settings.reconstruction.spatialEnabled = true;
            settings.reconstruction.spatialFilter =
                VisibilitySpatialFilter::JointBilateral;
            break;
        case ScreenSpaceVisibilityQuality::High:
            break;
        case ScreenSpaceVisibilityQuality::Ultra:
            settings.sampling.maximumSampleCount = 48u;
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

        const auto& leftBuffers = settings.bufferPrecision;
        const auto& rightBuffers = preset.bufferPrecision;
        return settings.enabled == preset.enabled &&
            settings.estimator == preset.estimator &&
            settings.resolution == preset.resolution &&
            settings.sampling.maximumSampleCount ==
                preset.sampling.maximumSampleCount &&
            settings.sampling.radius == preset.sampling.radius &&
            settings.sampling.thickness == preset.sampling.thickness &&
            settings.sampling.stepDistributionExponent ==
                preset.sampling.stepDistributionExponent &&
            settings.ambientOcclusion.enabled ==
                preset.ambientOcclusion.enabled &&
            settings.ambientOcclusion.strength ==
                preset.ambientOcclusion.strength &&
            settings.indirectDiffuse.enabled ==
                preset.indirectDiffuse.enabled &&
            settings.indirectDiffuse.intensity ==
                preset.indirectDiffuse.intensity &&
            settings.reconstruction.mode == preset.reconstruction.mode &&
            settings.reconstruction.spatialEnabled ==
                preset.reconstruction.spatialEnabled &&
            settings.reconstruction.spatialFilter ==
                preset.reconstruction.spatialFilter &&
            settings.reconstruction.spatialRadius ==
                preset.reconstruction.spatialRadius &&
            leftBuffers.ambient == rightBuffers.ambient &&
            leftBuffers.indirect == rightBuffers.indirect;
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
            ApplyScreenSpaceVisibilityQualityPreset(settings, origin);
        else
        {
            settings.qualityPresetOrigin = origin;
            settings.quality = ScreenSpaceVisibilityQuality::Custom;
        }
    }

    ScreenSpaceVisibilityPass::ScreenSpaceVisibilityPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        std::shared_ptr<CommonRenderPasses> commonPasses,
        bool deferPipelineCreation)
        : m_Device(device)
        , m_ShaderFactory(shaderFactory)
        , m_CommonPasses(std::move(commonPasses))
    {
        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(ScreenSpaceVisibilityConstants);
        constantBufferDesc.debugName = "ScreenSpaceVisibility/Constants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDesc);

        const auto createDummyTexture = [device](
            nvrhi::Format format,
            const char* debugName)
        {
            nvrhi::TextureDesc desc;
            desc.width = 1u;
            desc.height = 1u;
            desc.format = format;
            desc.dimension = nvrhi::TextureDimension::Texture2D;
            desc.mipLevels = 1u;
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

        if (!deferPipelineCreation)
        {
            while (!PreparePipelinesStep())
            {
            }
        }

        for (auto& stageQueries : m_TimerQueries)
            for (nvrhi::TimerQueryHandle& query : stageQueries)
                query = device->createTimerQuery();
    }

    bool ScreenSpaceVisibilityPass::PreparePipelinesStep()
    {
        if (m_PipelinesReady)
            return true;

        const auto createPipeline = [this](
            Pipeline& destination,
            const char* shaderName,
            const std::vector<nvrhi::BindingLayoutItem>& bindings,
            const std::vector<ShaderMacro>* macros = nullptr)
        {
            destination.shader = m_ShaderFactory->CreateShader(
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

        if (m_PipelinePreparationStep < c_ConsumerVariantCount)
        {
            const uint32_t consumer = m_PipelinePreparationStep;
            const bool ambientEnabled = consumer != 1u;
            const bool indirectEnabled = consumer != 0u;
            std::vector<ShaderMacro> macros = {
                { "ENABLE_AO", ambientEnabled ? "1" : "0" },
                { "ENABLE_GI", indirectEnabled ? "1" : "0" }
            };
            std::vector<nvrhi::BindingLayoutItem> layout = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)
            };
            if (ambientEnabled)
                layout.push_back(nvrhi::BindingLayoutItem::Texture_SRV(0));
            if (indirectEnabled)
                layout.push_back(nvrhi::BindingLayoutItem::Texture_SRV(1));
            layout.push_back(nvrhi::BindingLayoutItem::Texture_SRV(2));
            layout.push_back(nvrhi::BindingLayoutItem::Texture_SRV(3));
            if (ambientEnabled)
                layout.push_back(nvrhi::BindingLayoutItem::Texture_UAV(0));
            if (indirectEnabled)
                layout.push_back(nvrhi::BindingLayoutItem::Texture_UAV(1));
            createPipeline(
                m_Filter[consumer],
                "uvsr/screen_space_visibility_filter_cs.hlsl",
                layout,
                &macros);
        }
        else
        {
            createPipeline(
                m_Composite,
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
                    nvrhi::BindingLayoutItem::Texture_SRV(8),
                    nvrhi::BindingLayoutItem::Texture_SRV(9),
                    nvrhi::BindingLayoutItem::Texture_SRV(10),
                    nvrhi::BindingLayoutItem::Texture_SRV(11),
                    nvrhi::BindingLayoutItem::Texture_SRV(12),
                    nvrhi::BindingLayoutItem::Texture_UAV(0),
                    nvrhi::BindingLayoutItem::Sampler(0),
                    nvrhi::BindingLayoutItem::Sampler(1)
                });
        }

        ++m_PipelinePreparationStep;
        m_PipelinesReady = m_PipelinePreparationStep == 4u;
        return m_PipelinesReady;
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
        bool postProcessEnabled,
        bool packedEdgesEnabled,
        bool ambientHitDistanceEnabled,
        bool indirectHitDistanceEnabled,
        const VisibilityBufferPrecisionSettings& bufferPrecision)
    {
        resolutionScale = std::clamp(resolutionScale, 1u, 4u);
        const uint2 samplingSize(
            (fullSize.x + resolutionScale - 1u) / resolutionScale,
            (fullSize.y + resolutionScale - 1u) / resolutionScale);
        const uint64_t bufferPrecisionKey =
            uint64_t(bufferPrecision.ambient) |
            (uint64_t(bufferPrecision.indirect) << 2u);

        if (all(m_FullSize == fullSize) &&
            all(m_SamplingSize == samplingSize) &&
            m_ResolutionScale == resolutionScale &&
            m_AmbientResourcesEnabled == ambientEnabled &&
            m_IndirectDiffuseResourcesEnabled == indirectDiffuseEnabled &&
            m_PostProcessResourcesEnabled == postProcessEnabled &&
            m_PackedEdgeResourcesEnabled == packedEdgesEnabled &&
            m_AmbientHitDistanceResourcesEnabled ==
                ambientHitDistanceEnabled &&
            m_IndirectHitDistanceResourcesEnabled ==
                indirectHitDistanceEnabled &&
            m_BufferPrecisionConfigurationKey == bufferPrecisionKey &&
            (!ambientEnabled || m_RawAmbientVisibility) &&
            (!indirectDiffuseEnabled || m_RawIndirectDiffuse) &&
            (!ambientHitDistanceEnabled || m_RawAmbientHitDistance) &&
            (!indirectHitDistanceEnabled || m_RawIndirectHitDistance) &&
            (!postProcessEnabled || !ambientEnabled ||
                m_FinalAmbientVisibility) &&
            (!postProcessEnabled || !indirectDiffuseEnabled ||
                m_FinalIndirectDiffuse))
        {
            return;
        }

        ReleaseResources();
        m_FullSize = fullSize;
        m_SamplingSize = samplingSize;
        m_ResolutionScale = resolutionScale;
        m_AmbientResourcesEnabled = ambientEnabled;
        m_IndirectDiffuseResourcesEnabled = indirectDiffuseEnabled;
        m_PostProcessResourcesEnabled = postProcessEnabled;
        m_PackedEdgeResourcesEnabled = packedEdgesEnabled;
        m_AmbientHitDistanceResourcesEnabled =
            ambientHitDistanceEnabled;
        m_IndirectHitDistanceResourcesEnabled =
            indirectHitDistanceEnabled;
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
                ? 4u
                : 2u;
        };
        const auto vectorBytes = [](VisibilityVectorBufferPrecision precision)
        {
            return precision == VisibilityVectorBufferPrecision::Rgba32Float
                ? 16u
                : 8u;
        };
        const auto createTexture = [this](
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
                scalarFormat(bufferPrecision.ambient),
                "ScreenSpaceVisibility/RawAmbientVisibility");
        }
        if (indirectDiffuseEnabled)
        {
            m_RawIndirectDiffuse = createTexture(
                samplingSize,
                vectorFormat(bufferPrecision.indirect),
                "ScreenSpaceVisibility/RawIndirectDiffuse");
        }
        if (ambientHitDistanceEnabled)
        {
            m_RawAmbientHitDistance = createTexture(
                samplingSize,
                nvrhi::Format::R16_FLOAT,
                "ScreenSpaceVisibility/AmbientHitDistance");
        }
        if (indirectHitDistanceEnabled)
        {
            m_RawIndirectHitDistance = createTexture(
                samplingSize,
                nvrhi::Format::R16_FLOAT,
                "ScreenSpaceVisibility/IndirectDiffuseHitDistance");
        }
        if (postProcessEnabled && ambientEnabled)
        {
            m_FinalAmbientVisibility = createTexture(
                fullSize,
                scalarFormat(bufferPrecision.ambient),
                "ScreenSpaceVisibility/FinalAmbientVisibility");
        }
        if (postProcessEnabled && indirectDiffuseEnabled)
        {
            m_FinalIndirectDiffuse = createTexture(
                fullSize,
                vectorFormat(bufferPrecision.indirect),
                "ScreenSpaceVisibility/FinalIndirectDiffuse");
        }
        if (packedEdgesEnabled)
        {
            m_PackedEdgesTexture = createTexture(
                samplingSize,
                nvrhi::Format::R8_UINT,
                "ScreenSpaceVisibility/PackedEdges");
        }

        const uint64_t rawAmbientBytes = TextureBytes(
            samplingSize, scalarBytes(bufferPrecision.ambient));
        const uint64_t rawIndirectBytes = TextureBytes(
            samplingSize, vectorBytes(bufferPrecision.indirect));
        const uint64_t finalAmbientBytes = TextureBytes(
            fullSize, scalarBytes(bufferPrecision.ambient));
        const uint64_t finalIndirectBytes = TextureBytes(
            fullSize, vectorBytes(bufferPrecision.indirect));
        m_Timings.outputTextureBytes =
            (ambientEnabled ? rawAmbientBytes : 0u) +
            (indirectDiffuseEnabled ? rawIndirectBytes : 0u) +
            (postProcessEnabled && ambientEnabled
                ? finalAmbientBytes : 0u) +
            (postProcessEnabled && indirectDiffuseEnabled
                ? finalIndirectBytes : 0u) +
            (ambientHitDistanceEnabled
                ? TextureBytes(samplingSize, 2u) : 0u) +
            (indirectHitDistanceEnabled
                ? TextureBytes(samplingSize, 2u) : 0u);
        m_Timings.workingTextureBytes =
            packedEdgesEnabled ? TextureBytes(samplingSize, 1u) : 0u;
    }

    void ScreenSpaceVisibilityPass::ReleaseResources()
    {
        ResetBindingCache();
        m_RawAmbientVisibility = nullptr;
        m_RawIndirectDiffuse = nullptr;
        m_RawAmbientHitDistance = nullptr;
        m_RawIndirectHitDistance = nullptr;
        m_FinalAmbientVisibility = nullptr;
        m_FinalIndirectDiffuse = nullptr;
        m_PackedEdgesTexture = nullptr;
        m_FullSize = uint2::zero();
        m_SamplingSize = uint2::zero();
        m_ResolutionScale = 1u;
        m_AmbientResourcesEnabled = false;
        m_IndirectDiffuseResourcesEnabled = false;
        m_PostProcessResourcesEnabled = false;
        m_PackedEdgeResourcesEnabled = false;
        m_AmbientHitDistanceResourcesEnabled = false;
        m_IndirectHitDistanceResourcesEnabled = false;
        m_BufferPrecisionConfigurationKey = 0u;
        m_Timings = {};
    }

    void ScreenSpaceVisibilityPass::Deactivate()
    {
        ReleaseResources();
    }

    void ScreenSpaceVisibilityPass::ResetBindingCache()
    {
        for (nvrhi::BindingSetHandle& bindingSet : m_FilterBindingSets)
            bindingSet = nullptr;
        m_CompositeBindingSet = nullptr;
        m_BoundCompositeAmbient = nullptr;
        m_BoundCompositeIndirect = nullptr;
        m_BoundNoiseTexture = nullptr;
        m_AdvancedBindingSets.clear();
    }

    void ScreenSpaceVisibilityPass::AdvanceTimers()
    {
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        TimerSlot& timerSlot = m_TimerSlots[slot];
        m_TimerActive.fill(false);

        if (timerSlot.submittedStageMask == 0u)
        {
            assert(timerSlot.resolvedStageMask == 0u);
            m_TimerFrameWritable = timerSlot.resolvedStageMask == 0u;
            return;
        }

        m_TimerFrameWritable = false;
        for (uint32_t stageIndex = 0u;
            stageIndex < static_cast<uint32_t>(Stage::Count);
            ++stageIndex)
        {
            const uint32_t stageMask = 1u << stageIndex;
            if ((timerSlot.submittedStageMask & stageMask) == 0u ||
                (timerSlot.resolvedStageMask & stageMask) != 0u)
            {
                continue;
            }

            nvrhi::ITimerQuery* query = m_TimerQueries[stageIndex][slot];
            if (!m_Device->pollTimerQuery(query))
                continue;

            const float milliseconds =
                m_Device->getTimerQueryTime(query) * 1000.f;
            m_Device->resetTimerQuery(query);
            timerSlot.resolvedStageMilliseconds[stageIndex] = milliseconds;
            timerSlot.resolvedStageMask |= stageMask;
        }

        if (timerSlot.resolvedStageMask !=
            timerSlot.submittedStageMask)
        {
            return;
        }

        const auto millisecondsOrZero = [&timerSlot](Stage stage)
        {
            const uint32_t stageIndex = static_cast<uint32_t>(stage);
            const uint32_t stageMask = 1u << stageIndex;
            return (timerSlot.submittedStageMask & stageMask) != 0u
                ? timerSlot.resolvedStageMilliseconds[stageIndex]
                : 0.f;
        };

        ScreenSpaceVisibilityTimings completedTimings = m_Timings;
        completedTimings.firstTraceMs = millisecondsOrZero(
            Stage::FirstTrace);
        completedTimings.reconstructionMs = millisecondsOrZero(
            Stage::Reconstruction);
        completedTimings.compositionMs = millisecondsOrZero(
            Stage::Composition);
        completedTimings.effectEnvelopeMs = millisecondsOrZero(
            Stage::EffectEnvelope);
        completedTimings.available = true;
        m_Timings = completedTimings;

        timerSlot = {};
        m_TimerFrameWritable = true;
    }

    void ScreenSpaceVisibilityPass::BeginStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        if (!m_TimerFrameWritable)
            return;

        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        const uint32_t stageMask = 1u << stageIndex;
        TimerSlot& timerSlot = m_TimerSlots[slot];
        if ((timerSlot.submittedStageMask & stageMask) != 0u)
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
        const uint32_t stageMask = 1u << stageIndex;
        TimerSlot& timerSlot = m_TimerSlots[slot];
        assert((timerSlot.submittedStageMask & stageMask) == 0u);
        commandList->endTimerQuery(m_TimerQueries[stageIndex][slot]);
        timerSlot.submittedStageMask |= stageMask;
        m_TimerActive[stageIndex] = false;
    }

    ScreenSpaceVisibilityResult ScreenSpaceVisibilityPass::Render(
        nvrhi::ICommandList* commandList,
        const ScreenSpaceVisibilitySettings& settings,
        const ICompositeView& compositeView,
        const ScreenSpaceVisibilityInputs& inputs,
        const NoiseSettings& noiseSettings,
        nvrhi::ITexture* noiseTexture,
        uint32_t sampleSequencePhase)
    {
        if (!settings.HasActiveConsumer())
        {
            Deactivate();
            return {};
        }

        assert(commandList && m_PipelinesReady);
        assert(inputs.depth && inputs.normals);
        assert(inputs.gbufferDiffuse && inputs.gbufferSpecular &&
            inputs.gbufferEmissive && inputs.materialAmbientOcclusion &&
            inputs.baseLighting && inputs.output);
        assert(IsValidNoiseSettings(noiseSettings) && noiseTexture);
        assert(compositeView.GetNumChildViews(ViewType::PLANAR) == 1);

        const IView* view = compositeView.GetChildView(ViewType::PLANAR, 0);
        const nvrhi::TextureDesc& depthDesc = inputs.depth->getDesc();
        const uint2 fullSize(depthDesc.width, depthDesc.height);
        const uint32_t resolutionScale =
            GetResolutionScale(settings.resolution);
        const bool ambientEnabled = settings.HasActiveAmbientOcclusion();
        const bool indirectEnabled = settings.HasActiveIndirectDiffuse();
        const bool ambientHitDistanceEnabled = ambientEnabled &&
            settings.ambientOcclusion.outputHitDistance;
        const bool indirectHitDistanceEnabled = indirectEnabled &&
            settings.indirectDiffuse.outputHitDistance;
        const bool packedEdgesEnabled = IsPackedVisibilityReconstruction(
            settings.reconstruction.mode);
        const bool spatialFilterEnabled =
            settings.reconstruction.spatialEnabled && !packedEdgesEnabled;
        const bool postProcessEnabled = packedEdgesEnabled ||
            spatialFilterEnabled || resolutionScale > 1u;

        EnsureResources(
            fullSize,
            resolutionScale,
            ambientEnabled,
            indirectEnabled,
            postProcessEnabled,
            packedEdgesEnabled,
            ambientHitDistanceEnabled,
            indirectHitDistanceEnabled,
            settings.bufferPrecision);
        const bool hasSkyVisibilityConsumer =
            inputs.applySkyVisibilityToDiffuseIbl ||
            inputs.applySkyVisibilityToSpecularIbl;
        nvrhi::ITexture* activeSkyVisibility =
            hasSkyVisibilityConsumer &&
            IsSkyVisibilityTextureCompatible(
                inputs.skyVisibility,
                fullSize)
                ? inputs.skyVisibility
                : nullptr;
        const std::array<nvrhi::ITexture*, 13> inputTextures = {
            inputs.depth,
            inputs.normals,
            inputs.sourceRadiance,
            inputs.gbufferDiffuse,
            inputs.gbufferSpecular,
            inputs.gbufferEmissive,
            inputs.materialAmbientOcclusion,
            activeSkyVisibility,
            inputs.diffuseEnvironment,
            inputs.specularEnvironment,
            inputs.environmentBrdf,
            inputs.baseLighting,
            inputs.output
        };
        if (inputTextures != m_BoundInputTextures ||
            noiseTexture != m_BoundNoiseTexture)
        {
            ResetBindingCache();
            m_BoundInputTextures = inputTextures;
            m_BoundNoiseTexture = noiseTexture;
        }
        AdvanceTimers();

        const uint32_t consumerVariant =
            GetConsumerVariant(ambientEnabled, indirectEnabled);
        const uint32_t estimatorIndex = std::min(
            static_cast<uint32_t>(settings.estimator),
            ImplementedVisibilityEstimatorCount - 1u);
        const uint32_t runtimeSampleParity = GetRuntimeSampleParity(
            settings,
            ambientEnabled,
            indirectEnabled,
            packedEdgesEnabled);

        ScreenSpaceVisibilityConstants constants{};
        view->FillPlanarViewConstants(constants.view);
        constants.fullResolution = float2(fullSize);
        constants.samplingResolution = float2(m_SamplingSize);
        constants.radiusWorld = std::max(settings.sampling.radius, 0.f);
        constants.thicknessWorld = std::max(
            settings.sampling.thickness, 0.f);
        constants.stepDistributionExponent = std::clamp(
            settings.sampling.stepDistributionExponent,
            MinimumVisibilityStepDistributionExponent,
            MaximumVisibilityStepDistributionExponent);
        constants.ambientStrength = std::clamp(
            settings.ambientOcclusion.strength,
            MinimumVisibilityAmbientOcclusionStrength,
            MaximumVisibilityAmbientOcclusionStrength);
        constants.indirectDiffuseIntensity = std::max(
            settings.indirectDiffuse.intensity, 0.f);
        constants.spatialRadius = std::clamp(
            settings.reconstruction.spatialRadius, 0.f, 16.f);
        constants.sampleSequencePhase = sampleSequencePhase;
        constants.maximumSampleCount = std::clamp(
            settings.sampling.maximumSampleCount, 1u, 64u);
        constants.sourceRadianceAvailable =
            inputs.sourceRadiance ? 1u : 0u;
        constants.enableAmbientOcclusion = ambientEnabled ? 1u : 0u;
        constants.enableIndirectDiffuse = indirectEnabled ? 1u : 0u;
        constants.reverseDepth = view->IsReverseDepth() ? 1u : 0u;
        constants.orthographicProjection =
            view->IsOrthographicProjection() ? 1u : 0u;
        constants.resolutionScale = resolutionScale;
        constants.noisePattern = static_cast<uint32_t>(
            noiseSettings.pattern);
        constants.visibilityDebugView = std::min(
            static_cast<uint32_t>(settings.debugView), 3u);
        constants.packedEdgeMode = packedEdgesEnabled
            ? std::clamp(
                static_cast<uint32_t>(settings.reconstruction.mode),
                1u,
                3u)
            : 0u;
        constants.spatialFilter = spatialFilterEnabled
            ? std::min(
                static_cast<uint32_t>(
                    settings.reconstruction.spatialFilter),
                1u)
            : 0u;
        constants.lightingDebugView = inputs.lightingDebugView;
        constants.skyVisibilityApplication = activeSkyVisibility
            ? (inputs.applySkyVisibilityToDiffuseIbl
                ? (inputs.applySkyVisibilityToSpecularIbl
                    ? UVSR_SKY_VISIBILITY_APPLY_BOTH_IBL
                    : UVSR_SKY_VISIBILITY_APPLY_DIFFUSE_IBL)
                : UVSR_SKY_VISIBILITY_APPLY_SPECULAR_IBL)
            : UVSR_SKY_VISIBILITY_APPLY_NEITHER;

        const float diffuseEnvironmentScale = std::max(
            std::isfinite(inputs.diffuseEnvironmentScale)
                ? inputs.diffuseEnvironmentScale
                : 0.f,
            0.f);
        constants.diffuseEnvironmentEnabled =
            inputs.diffuseEnvironment && diffuseEnvironmentScale > 0.f
            ? 1u
            : 0u;
        constants.diffuseEnvironmentScale = diffuseEnvironmentScale;
        constants.diffuseEnvironmentArrayIndex =
            inputs.diffuseEnvironmentArrayIndex;

        const float specularEnvironmentScale = std::max(
            std::isfinite(inputs.specularEnvironmentScale)
                ? inputs.specularEnvironmentScale
                : 0.f,
            0.f);
        const float specularEnvironmentMipLevels = std::max(
            std::isfinite(inputs.specularEnvironmentMipLevels)
                ? inputs.specularEnvironmentMipLevels
                : 0.f,
            0.f);
        constants.specularEnvironmentEnabled =
            inputs.specularEnvironment && inputs.environmentBrdf &&
                specularEnvironmentScale > 0.f &&
                specularEnvironmentMipLevels > 0.f
            ? 1u
            : 0u;
        constants.specularEnvironmentScale = specularEnvironmentScale;
        constants.specularEnvironmentMipLevels =
            specularEnvironmentMipLevels;
        constants.specularEnvironmentArrayIndex =
            inputs.specularEnvironmentArrayIndex;

        BeginStage(commandList, Stage::EffectEnvelope);
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));

        const uint32_t samplingDispatchX =
            (m_SamplingSize.x + kThreadGroupSize - 1u) /
            kThreadGroupSize;
        const uint32_t samplingDispatchY =
            (m_SamplingSize.y + kThreadGroupSize - 1u) /
            kThreadGroupSize;
        const uint32_t fullDispatchX =
            (fullSize.x + kThreadGroupSize - 1u) / kThreadGroupSize;
        const uint32_t fullDispatchY =
            (fullSize.y + kThreadGroupSize - 1u) / kThreadGroupSize;

        nvrhi::ITexture* rawAmbient = ambientEnabled
            ? m_RawAmbientVisibility.Get()
            : m_DummyAmbientVisibility.Get();
        nvrhi::ITexture* rawIndirect = indirectEnabled
            ? m_RawIndirectDiffuse.Get()
            : m_DummyIndirectDiffuse.Get();
        nvrhi::ITexture* sourceRadiance = inputs.sourceRadiance
            ? inputs.sourceRadiance
            : m_DummyIndirectDiffuse.Get();

        commandList->beginMarker(indirectEnabled
            ? (ambientEnabled
                ? "Screen Space Visibility (AO + GI)"
                : "Screen Space Visibility (GI)")
            : "Screen Space Visibility (AO)");

        std::vector<ShaderMacro> traceMacros = {
            { "VISIBILITY_ESTIMATOR", std::to_string(estimatorIndex) },
            { "ENABLE_AO", ambientEnabled ? "1" : "0" },
            { "ENABLE_GI", indirectEnabled ? "1" : "0" }
        };
        if (runtimeSampleParity != 0u)
        {
            traceMacros.push_back({
                "RUNTIME_SAMPLE_PARITY",
                std::to_string(runtimeSampleParity)
            });
        }
        if (packedEdgesEnabled)
            traceMacros.push_back({ "OUTPUT_PACKED_EDGES", "1" });
        if (ambientHitDistanceEnabled)
            traceMacros.push_back({ "OUTPUT_AO_HIT_DISTANCE", "1" });
        if (indirectHitDistanceEnabled)
            traceMacros.push_back({ "OUTPUT_GI_HIT_DISTANCE", "1" });

        std::vector<nvrhi::BindingLayoutItem> traceLayout = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1)
        };
        if (indirectEnabled)
            traceLayout.push_back(nvrhi::BindingLayoutItem::Texture_SRV(2));
        traceLayout.push_back(nvrhi::BindingLayoutItem::Texture_SRV(3));
        if (ambientEnabled)
            traceLayout.push_back(nvrhi::BindingLayoutItem::Texture_UAV(0));
        if (indirectEnabled)
            traceLayout.push_back(nvrhi::BindingLayoutItem::Texture_UAV(1));
        if (ambientHitDistanceEnabled)
            traceLayout.push_back(nvrhi::BindingLayoutItem::Texture_UAV(2));
        if (packedEdgesEnabled)
            traceLayout.push_back(nvrhi::BindingLayoutItem::Texture_UAV(3));
        if (indirectHitDistanceEnabled)
            traceLayout.push_back(nvrhi::BindingLayoutItem::Texture_UAV(4));

        const uint64_t tracePipelineKey = TracePipelineKey(
            estimatorIndex,
            consumerVariant,
            runtimeSampleParity,
            packedEdgesEnabled,
            ambientHitDistanceEnabled,
            indirectHitDistanceEnabled);
        Pipeline& tracePipeline = GetOrCreateAdvancedPipeline(
            tracePipelineKey,
            "uvsr/screen_space_visibility_cs.hlsl",
            traceLayout,
            &traceMacros);
        nvrhi::BindingSetHandle& traceBindingSet =
            m_AdvancedBindingSets[tracePipelineKey];
        if (!traceBindingSet)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth),
                nvrhi::BindingSetItem::Texture_SRV(1, inputs.normals)
            };
            if (indirectEnabled)
            {
                bindings.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_SRV(2, sourceRadiance));
            }
            bindings.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    3, noiseTexture));
            if (ambientEnabled)
            {
                bindings.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_UAV(0, rawAmbient));
            }
            if (indirectEnabled)
            {
                bindings.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_UAV(1, rawIndirect));
            }
            if (ambientHitDistanceEnabled)
            {
                bindings.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_UAV(
                        2, m_RawAmbientHitDistance));
            }
            if (packedEdgesEnabled)
            {
                bindings.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_UAV(
                        3, m_PackedEdgesTexture));
            }
            if (indirectHitDistanceEnabled)
            {
                bindings.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_UAV(
                        4, m_RawIndirectHitDistance));
            }
            traceBindingSet = m_Device->createBindingSet(
                bindings, tracePipeline.bindingLayout);
        }

        {
            nvrhi::ComputeState state;
            state.pipeline = tracePipeline.pipeline;
            state.bindings = { traceBindingSet };
            commandList->beginMarker("Visibility Sampling");
            BeginStage(commandList, Stage::FirstTrace);
            commandList->setComputeState(state);
            commandList->dispatch(
                samplingDispatchX, samplingDispatchY, 1u);
            EndStage(commandList, Stage::FirstTrace);
            commandList->endMarker();
        }

        nvrhi::ITexture* processedAmbient = rawAmbient;
        nvrhi::ITexture* processedIndirect = rawIndirect;
        bool ambientProcessed = false;
        bool indirectProcessed = false;
        if (ambientEnabled && inputs.processAmbientOcclusion)
        {
            nvrhi::ITexture* candidate = inputs.processAmbientOcclusion(
                commandList,
                rawAmbient,
                ambientHitDistanceEnabled
                    ? m_RawAmbientHitDistance.Get()
                    : nullptr,
                m_SamplingSize,
                ambientHitDistanceEnabled &&
                    ScreenSpaceAmbientOcclusionHitDistanceMatchesSignal);
            if (candidate != rawAmbient &&
                IsFullResolutionProcessedSignal(candidate, fullSize))
            {
                processedAmbient = candidate;
                ambientProcessed = true;
            }
        }
        if (indirectEnabled && inputs.processIndirectDiffuse)
        {
            nvrhi::ITexture* candidate = inputs.processIndirectDiffuse(
                commandList,
                rawIndirect,
                indirectHitDistanceEnabled
                    ? m_RawIndirectHitDistance.Get()
                    : nullptr,
                m_SamplingSize,
                indirectHitDistanceEnabled &&
                    ScreenSpaceIndirectDiffuseHitDistanceMatchesSignal);
            if (candidate != rawIndirect &&
                IsFullResolutionProcessedSignal(candidate, fullSize))
            {
                processedIndirect = candidate;
                indirectProcessed = true;
            }
        }

        const bool reconstructAmbient = ambientEnabled &&
            postProcessEnabled && !ambientProcessed;
        const bool reconstructIndirect = indirectEnabled &&
            postProcessEnabled && !indirectProcessed;
        const bool reconstructionEnabled =
            reconstructAmbient || reconstructIndirect;
        nvrhi::ITexture* reconstructedAmbient = processedAmbient;
        nvrhi::ITexture* reconstructedIndirect = processedIndirect;
        if (reconstructionEnabled)
        {
            const uint32_t reconstructionConsumerVariant =
                GetConsumerVariant(
                    reconstructAmbient,
                    reconstructIndirect);
            Pipeline* reconstructionPipeline = nullptr;
            nvrhi::BindingSetHandle* reconstructionBindingSet = nullptr;
            if (packedEdgesEnabled)
            {
                std::vector<ShaderMacro> macros = {
                    { "ENABLE_AO", reconstructAmbient ? "1" : "0" },
                    { "ENABLE_GI", reconstructIndirect ? "1" : "0" },
                    { "PACKED_EDGE_RECONSTRUCTION", "1" }
                };
                std::vector<nvrhi::BindingLayoutItem> layout = {
                    nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)
                };
                if (reconstructAmbient)
                    layout.push_back(
                        nvrhi::BindingLayoutItem::Texture_SRV(0));
                if (reconstructIndirect)
                    layout.push_back(
                        nvrhi::BindingLayoutItem::Texture_SRV(1));
                layout.push_back(nvrhi::BindingLayoutItem::Texture_SRV(4));
                if (reconstructAmbient)
                    layout.push_back(
                        nvrhi::BindingLayoutItem::Texture_UAV(0));
                if (reconstructIndirect)
                    layout.push_back(
                        nvrhi::BindingLayoutItem::Texture_UAV(1));

                const uint64_t pipelineKey =
                    PackedFilterPipelineKey(reconstructionConsumerVariant);
                reconstructionPipeline = &GetOrCreateAdvancedPipeline(
                    pipelineKey,
                    "uvsr/screen_space_visibility_filter_cs.hlsl",
                    layout,
                    &macros);
                reconstructionBindingSet =
                    std::addressof(m_AdvancedBindingSets[pipelineKey]);
            }
            else
            {
                reconstructionPipeline =
                    &m_Filter[reconstructionConsumerVariant];
                reconstructionBindingSet =
                    std::addressof(
                        m_FilterBindingSets[reconstructionConsumerVariant]);
            }

            if (!*reconstructionBindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        0, m_ConstantBuffer)
                };
                if (reconstructAmbient)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(
                            0, rawAmbient));
                }
                if (reconstructIndirect)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(
                            1, rawIndirect));
                }
                if (packedEdgesEnabled)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(
                            4, m_PackedEdgesTexture));
                }
                else
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(
                            2, inputs.depth));
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_SRV(
                            3, inputs.normals));
                }
                if (reconstructAmbient)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_UAV(
                            0, m_FinalAmbientVisibility));
                }
                if (reconstructIndirect)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_UAV(
                            1, m_FinalIndirectDiffuse));
                }
                *reconstructionBindingSet = m_Device->createBindingSet(
                    bindings, reconstructionPipeline->bindingLayout);
            }

            nvrhi::ComputeState state;
            state.pipeline = reconstructionPipeline->pipeline;
            state.bindings = { *reconstructionBindingSet };
            commandList->beginMarker(packedEdgesEnabled
                ? "Visibility Packed Reconstruction"
                : (spatialFilterEnabled
                    ? "Visibility Spatial Reconstruction"
                    : "Visibility Guide-Aware Upsample"));
            BeginStage(commandList, Stage::Reconstruction);
            commandList->setComputeState(state);
            commandList->dispatch(fullDispatchX, fullDispatchY, 1u);
            EndStage(commandList, Stage::Reconstruction);
            commandList->endMarker();

            reconstructedAmbient = reconstructAmbient
                ? m_FinalAmbientVisibility.Get()
                : processedAmbient;
            reconstructedIndirect = reconstructIndirect
                ? m_FinalIndirectDiffuse.Get()
                : processedIndirect;
        }
        if (reconstructedAmbient != m_BoundCompositeAmbient ||
            reconstructedIndirect != m_BoundCompositeIndirect)
        {
            m_CompositeBindingSet = nullptr;
            m_BoundCompositeAmbient = reconstructedAmbient;
            m_BoundCompositeIndirect = reconstructedIndirect;
        }
        if (!m_CompositeBindingSet)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
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
                nvrhi::BindingSetItem::Texture_SRV(6, inputs.normals),
                nvrhi::BindingSetItem::Texture_SRV(
                    7,
                    inputs.diffuseEnvironment
                        ? inputs.diffuseEnvironment
                        : m_CommonPasses->m_BlackCubeMapArray.Get()),
                nvrhi::BindingSetItem::Texture_SRV(
                    8, inputs.gbufferSpecular),
                nvrhi::BindingSetItem::Texture_SRV(9, inputs.depth),
                nvrhi::BindingSetItem::Texture_SRV(
                    10,
                    inputs.specularEnvironment
                        ? inputs.specularEnvironment
                        : m_CommonPasses->m_BlackCubeMapArray.Get()),
                nvrhi::BindingSetItem::Texture_SRV(
                    11,
                    inputs.environmentBrdf
                        ? inputs.environmentBrdf
                        : m_CommonPasses->m_BlackTexture.Get()),
                nvrhi::BindingSetItem::Texture_SRV(
                    12,
                    activeSkyVisibility
                        ? activeSkyVisibility
                        : m_CommonPasses->m_WhiteTexture.Get()),
                nvrhi::BindingSetItem::Sampler(
                    0, m_CommonPasses->m_LinearWrapSampler),
                nvrhi::BindingSetItem::Sampler(
                    1, m_CommonPasses->m_LinearClampSampler),
                nvrhi::BindingSetItem::Texture_UAV(0, inputs.output)
            };
            m_CompositeBindingSet = m_Device->createBindingSet(
                bindings, m_Composite.bindingLayout);
        }

        {
            nvrhi::ComputeState state;
            state.pipeline = m_Composite.pipeline;
            state.bindings = { m_CompositeBindingSet };
            commandList->beginMarker("Screen Space Indirect Composite");
            BeginStage(commandList, Stage::Composition);
            commandList->setComputeState(state);
            commandList->dispatch(fullDispatchX, fullDispatchY, 1u);
            EndStage(commandList, Stage::Composition);
            commandList->endMarker();
        }

        m_Timings.activeSrvCount = 3u + (indirectEnabled ? 1u : 0u);
        m_Timings.activeUavCount =
            uint32_t(ambientEnabled) + uint32_t(indirectEnabled) +
            uint32_t(packedEdgesEnabled) +
            uint32_t(ambientHitDistanceEnabled) +
            uint32_t(indirectHitDistanceEnabled);
        m_Timings.activeDispatchCount = 2u +
            uint32_t(reconstructionEnabled);
        m_Timings.active = true;

        commandList->endMarker();
        EndStage(commandList, Stage::EffectEnvelope);
        if (m_TimerFrameWritable)
            ++m_TimerFrame;
        return {
            ambientEnabled ? m_RawAmbientVisibility.Get() : nullptr,
            indirectEnabled ? m_RawIndirectDiffuse.Get() : nullptr,
            ambientHitDistanceEnabled
                ? m_RawAmbientHitDistance.Get()
                : nullptr,
            indirectHitDistanceEnabled
                ? m_RawIndirectHitDistance.Get()
                : nullptr,
            m_SamplingSize,
            ambientHitDistanceEnabled &&
                ScreenSpaceAmbientOcclusionHitDistanceMatchesSignal,
            indirectHitDistanceEnabled &&
                ScreenSpaceIndirectDiffuseHitDistanceMatchesSignal,
            true
        };
    }
}
