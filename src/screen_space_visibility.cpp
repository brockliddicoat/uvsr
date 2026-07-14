#include "screen_space_visibility.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <cassert>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "screen_space_visibility_cb.h"

namespace
{
    constexpr uint32_t kThreadGroupSize = 8u;
    constexpr uint32_t kMinimumSecondaryBounceSamples = 8u;
    constexpr uint32_t kBlueNoiseSize = 64u;
    constexpr uint32_t kBlueNoiseTexelCount =
        kBlueNoiseSize * kBlueNoiseSize;

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

    uint32_t GetIndirectBounceSampleCount(
        uint32_t primarySampleCount,
        uint32_t bounceIndex)
    {
        primarySampleCount = std::clamp(primarySampleCount, 1u, 64u);
        if (bounceIndex == 0u)
            return primarySampleCount;

        const uint32_t floor = std::min(
            primarySampleCount, kMinimumSecondaryBounceSamples);
        return std::max(primarySampleCount >> bounceIndex, floor);
    }

    uint64_t TextureBytes(uint2 size, uint32_t bytesPerPixel)
    {
        return uint64_t(size.x) * uint64_t(size.y) * bytesPerPixel;
    }

    uint64_t DepthHierarchyBytes(uint2 fullSize)
    {
        uint32_t width = std::max((fullSize.x + 15u) & ~15u, 16u);
        uint32_t height = std::max((fullSize.y + 15u) & ~15u, 16u);
        uint64_t bytes = 0u;
        for (uint32_t mip = 0u; mip < 5u; ++mip)
        {
            bytes += uint64_t(std::max(width >> mip, 1u)) *
                uint64_t(std::max(height >> mip, 1u)) * 2u;
        }
        return bytes;
    }

    void AppendHistoryConfigurationValue(uint64_t& key, uint64_t value)
    {
        // A process-local change detector, not a serialized identifier. The
        // boost-style combiner is sufficient to prevent stale temporal and
        // adaptive values crossing materially different traversal settings.
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

        appendFloat(constants.radiusWorld);
        appendFloat(constants.thicknessWorld);
        appendFloat(constants.stepDistributionExponent);
        appendFloat(constants.adaptiveStrength);
        appendUint(constants.minimumSampleCount);
        appendUint(constants.maximumSampleCount);
        appendUint(constants.maximumRefinementSlices);
        appendUint(constants.sampleScheduler);
        appendUint(constants.useDepthHierarchy);
        appendUint(constants.resolutionScale);
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
    void ApplyScreenSpaceVisibilityQualityPreset(
        ScreenSpaceVisibilitySettings& settings,
        ScreenSpaceVisibilityQuality quality)
    {
        settings.quality = quality;
        if (quality == ScreenSpaceVisibilityQuality::Custom)
            return;

        settings.sampling.radius = 3.0f;
        settings.sampling.thickness = 0.5f;
        settings.sampling.stepDistributionExponent = 2.0f;
        settings.sampling.adaptiveStrength = 1.0f;
        settings.sampling.scheduler =
            VisibilitySampleScheduler::SpatiotemporalBlueNoise;

        switch (quality)
        {
        case ScreenSpaceVisibilityQuality::Low:
            settings.sampling.minimumSampleCount = 4u;
            settings.sampling.maximumSampleCount = 16u;
            settings.sampling.maximumRefinementSlices = 1u;
            break;
        case ScreenSpaceVisibilityQuality::Medium:
            settings.sampling.minimumSampleCount = 8u;
            settings.sampling.maximumSampleCount = 32u;
            settings.sampling.maximumRefinementSlices = 2u;
            break;
        case ScreenSpaceVisibilityQuality::High:
            settings.sampling.minimumSampleCount = 12u;
            settings.sampling.maximumSampleCount = 48u;
            settings.sampling.maximumRefinementSlices = 3u;
            break;
        case ScreenSpaceVisibilityQuality::Ultra:
            settings.sampling.minimumSampleCount = 16u;
            settings.sampling.maximumSampleCount = 64u;
            settings.sampling.maximumRefinementSlices = 4u;
            break;
        default:
            break;
        }
    }

    ScreenSpaceVisibilityPass::ScreenSpaceVisibilityPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory)
        : m_Device(device)
    {
        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(ScreenSpaceVisibilityConstants);
        constantBufferDesc.debugName = "ScreenSpaceVisibility/Constants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDesc);

        nvrhi::BufferDesc statisticsDesc;
        statisticsDesc.byteSize = sizeof(uint32_t) * 4u;
        statisticsDesc.format = nvrhi::Format::R32_UINT;
        statisticsDesc.canHaveUAVs = true;
        statisticsDesc.canHaveTypedViews = true;
        statisticsDesc.initialState = nvrhi::ResourceStates::CopySource;
        statisticsDesc.keepInitialState = true;
        statisticsDesc.debugName = "ScreenSpaceVisibility/SamplingStatistics";
        m_SamplingStatisticsBuffer = device->createBuffer(statisticsDesc);

        statisticsDesc.canHaveUAVs = false;
        statisticsDesc.canHaveTypedViews = false;
        statisticsDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
        statisticsDesc.initialState = nvrhi::ResourceStates::CopyDest;
        statisticsDesc.debugName =
            "ScreenSpaceVisibility/SamplingStatisticsReadback";
        for (nvrhi::BufferHandle& readback :
            m_SamplingStatisticsReadbackBuffers)
        {
            readback = device->createBuffer(statisticsDesc);
        }

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
        m_DummyFeedback = createDummyTexture(
            nvrhi::Format::RGBA16_FLOAT,
            "ScreenSpaceVisibility/DummyFeedbackOrMotion");
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
        m_DummyFeedbackOutput = createDummyTexture(
            nvrhi::Format::RGBA16_FLOAT,
            "ScreenSpaceVisibility/DummyFeedbackOutput");

        // Generate a deterministic, progressive toroidal rank texture. Each
        // prefix greedily chooses the texel farthest from the existing set,
        // which preserves broad spatial separation without importing a
        // licensed blue-noise asset.
        m_BlueNoiseUpload.resize(kBlueNoiseTexelCount);
        std::vector<float> nearestDistanceSquared(
            kBlueNoiseTexelCount, std::numeric_limits<float>::max());
        std::vector<bool> selected(kBlueNoiseTexelCount, false);
        uint32_t selectedIndex = 0u;
        for (uint32_t rank = 0u; rank < kBlueNoiseTexelCount; ++rank)
        {
            selected[selectedIndex] = true;
            m_BlueNoiseUpload[selectedIndex] = uint16_t(
                (uint64_t(rank) * 65535u) /
                uint64_t(kBlueNoiseTexelCount - 1u));

            const int selectedX = int(selectedIndex % kBlueNoiseSize);
            const int selectedY = int(selectedIndex / kBlueNoiseSize);
            for (uint32_t candidate = 0u;
                candidate < kBlueNoiseTexelCount;
                ++candidate)
            {
                if (selected[candidate])
                    continue;
                int dx = std::abs(int(candidate % kBlueNoiseSize) - selectedX);
                int dy = std::abs(int(candidate / kBlueNoiseSize) - selectedY);
                dx = std::min(dx, int(kBlueNoiseSize) - dx);
                dy = std::min(dy, int(kBlueNoiseSize) - dy);
                const float distanceSquared = float(dx * dx + dy * dy);
                nearestDistanceSquared[candidate] = std::min(
                    nearestDistanceSquared[candidate], distanceSquared);
            }

            float bestDistance = -1.0f;
            uint32_t nextIndex = 0u;
            for (uint32_t candidate = 0u;
                candidate < kBlueNoiseTexelCount;
                ++candidate)
            {
                if (!selected[candidate] &&
                    nearestDistanceSquared[candidate] > bestDistance)
                {
                    bestDistance = nearestDistanceSquared[candidate];
                    nextIndex = candidate;
                }
            }
            selectedIndex = nextIndex;
        }

        nvrhi::TextureDesc blueNoiseDesc;
        blueNoiseDesc.width = kBlueNoiseSize;
        blueNoiseDesc.height = kBlueNoiseSize;
        blueNoiseDesc.format = nvrhi::Format::R16_UNORM;
        blueNoiseDesc.dimension = nvrhi::TextureDimension::Texture2D;
        blueNoiseDesc.mipLevels = 1u;
        blueNoiseDesc.initialState = nvrhi::ResourceStates::CopyDest;
        blueNoiseDesc.keepInitialState = true;
        blueNoiseDesc.debugName = "ScreenSpaceVisibility/ProgressiveBlueNoise";
        m_BlueNoiseTexture = device->createTexture(blueNoiseDesc);

        CreatePipelines(shaderFactory);
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
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(2),
            nvrhi::BindingLayoutItem::TypedBuffer_UAV(3)
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
                    { "ENABLE_BOUNCE_METADATA", "0" }
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
                        nvrhi::BindingLayoutItem::Texture_UAV(0),
                        nvrhi::BindingLayoutItem::Texture_UAV(1)
                    },
                    &macros);
            }
        }

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
                nvrhi::BindingLayoutItem::Texture_UAV(0)
            });
    }

    void ScreenSpaceVisibilityPass::EnsureResources(
        uint2 fullSize,
        uint32_t resolutionScale,
        bool ambientEnabled,
        bool indirectDiffuseEnabled,
        bool multipleBouncesEnabled,
        bool adaptiveEnabled,
        bool temporalEnabled,
        bool postProcessEnabled,
        bool depthHierarchyEnabled)
    {
        resolutionScale = std::clamp(resolutionScale, 1u, 4u);
        const uint2 samplingSize(
            (fullSize.x + resolutionScale - 1u) / resolutionScale,
            (fullSize.y + resolutionScale - 1u) / resolutionScale);
        multipleBouncesEnabled = multipleBouncesEnabled &&
            indirectDiffuseEnabled;
        temporalEnabled = temporalEnabled && postProcessEnabled;

        if (all(m_FullSize == fullSize) &&
            all(m_SamplingSize == samplingSize) &&
            m_ResolutionScale == resolutionScale &&
            m_AmbientResourcesEnabled == ambientEnabled &&
            m_IndirectDiffuseResourcesEnabled == indirectDiffuseEnabled &&
            m_MultipleBounceResourcesEnabled == multipleBouncesEnabled &&
            m_AdaptiveResourcesEnabled == adaptiveEnabled &&
            m_TemporalResourcesEnabled == temporalEnabled &&
            m_PostProcessResourcesEnabled == postProcessEnabled &&
            m_DepthHierarchyResourcesEnabled == depthHierarchyEnabled &&
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
        m_AdaptiveResourcesEnabled = adaptiveEnabled;
        m_TemporalResourcesEnabled = temporalEnabled;
        m_PostProcessResourcesEnabled = postProcessEnabled;
        m_DepthHierarchyResourcesEnabled = depthHierarchyEnabled;

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
                nvrhi::Format::R16_FLOAT,
                "ScreenSpaceVisibility/RawAmbientVisibility");
        }
        if (indirectDiffuseEnabled)
        {
            m_RawIndirectDiffuse[0] = createTexture(
                samplingSize,
                nvrhi::Format::RGBA16_FLOAT,
                "ScreenSpaceVisibility/IndirectDiffuseFrontier0");
        }
        if (multipleBouncesEnabled)
        {
            m_RawIndirectDiffuse[1] = createTexture(
                samplingSize,
                nvrhi::Format::RGBA16_FLOAT,
                "ScreenSpaceVisibility/IndirectDiffuseFrontier1");
            m_CumulativeIndirectDiffuse = createTexture(
                samplingSize,
                nvrhi::Format::RGBA16_FLOAT,
                "ScreenSpaceVisibility/CumulativeIndirectDiffuse");
        }
        if (adaptiveEnabled)
        {
            for (uint32_t index = 0u; index < 2u; ++index)
            {
                m_AdaptiveFeedback[index] = createTexture(
                    samplingSize,
                    nvrhi::Format::RGBA16_FLOAT,
                    index == 0u
                        ? "ScreenSpaceVisibility/AdaptiveFeedback0"
                        : "ScreenSpaceVisibility/AdaptiveFeedback1");
            }
        }
        if (temporalEnabled)
        {
            for (uint32_t index = 0u; index < 2u; ++index)
            {
                if (ambientEnabled)
                {
                    m_TemporalAmbientVisibility[index] = createTexture(
                        samplingSize,
                        nvrhi::Format::R16_FLOAT,
                        index == 0u
                            ? "ScreenSpaceVisibility/AmbientHistory0"
                            : "ScreenSpaceVisibility/AmbientHistory1");
                }
                if (indirectDiffuseEnabled)
                {
                    m_TemporalIndirectDiffuse[index] = createTexture(
                        samplingSize,
                        nvrhi::Format::RGBA16_FLOAT,
                        index == 0u
                            ? "ScreenSpaceVisibility/IndirectHistory0"
                            : "ScreenSpaceVisibility/IndirectHistory1");
                }
                m_TemporalDepth[index] = createTexture(
                    samplingSize,
                    nvrhi::Format::R32_FLOAT,
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
            if (ambientEnabled)
            {
                m_FinalAmbientVisibility = createTexture(
                    fullSize,
                    nvrhi::Format::R16_FLOAT,
                    "ScreenSpaceVisibility/FinalAmbientVisibility");
            }
            if (indirectDiffuseEnabled)
            {
                m_FinalIndirectDiffuse = createTexture(
                    fullSize,
                    nvrhi::Format::RGBA16_FLOAT,
                    "ScreenSpaceVisibility/FinalIndirectDiffuse");
            }
        }
        if (depthHierarchyEnabled)
        {
            nvrhi::TextureDesc hierarchyDesc;
            hierarchyDesc.width = std::max(
                (fullSize.x + 15u) & ~15u, 16u);
            hierarchyDesc.height = std::max(
                (fullSize.y + 15u) & ~15u, 16u);
            hierarchyDesc.format = nvrhi::Format::R16_FLOAT;
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

        const uint64_t rawAmbientBytes = TextureBytes(samplingSize, 2u);
        const uint64_t rawIndirectBytes = TextureBytes(samplingSize, 8u);
        const uint64_t finalAmbientBytes = TextureBytes(fullSize, 2u);
        const uint64_t finalIndirectBytes = TextureBytes(fullSize, 8u);

        m_Timings.outputTextureBytes =
            (ambientEnabled ? rawAmbientBytes : 0u) +
            (indirectDiffuseEnabled ? rawIndirectBytes : 0u) +
            (multipleBouncesEnabled ? rawIndirectBytes * 2u : 0u) +
            (postProcessEnabled && ambientEnabled ? finalAmbientBytes : 0u) +
            (postProcessEnabled && indirectDiffuseEnabled
                ? finalIndirectBytes : 0u);
        m_Timings.workingTextureBytes =
            (adaptiveEnabled ? rawIndirectBytes * 2u : 0u) +
            (temporalEnabled && ambientEnabled ? rawAmbientBytes * 2u : 0u) +
            (temporalEnabled && indirectDiffuseEnabled
                ? rawIndirectBytes * 2u : 0u) +
            (temporalEnabled ? TextureBytes(samplingSize, 8u) : 0u) +
            (temporalEnabled ? TextureBytes(samplingSize, 8u) : 0u) +
            (depthHierarchyEnabled ? DepthHierarchyBytes(fullSize) : 0u);
        m_Timings.maskCacheBytes = 0u;
        m_Timings.avoidedTextureBytes =
            (!ambientEnabled
                ? rawAmbientBytes +
                    (temporalEnabled ? rawAmbientBytes * 2u : 0u) +
                    (postProcessEnabled ? finalAmbientBytes : 0u)
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
        for (nvrhi::TextureHandle& texture : m_AdaptiveFeedback)
            texture = nullptr;
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

        m_FullSize = uint2::zero();
        m_SamplingSize = uint2::zero();
        m_ResolutionScale = 1u;
        m_AmbientResourcesEnabled = false;
        m_IndirectDiffuseResourcesEnabled = false;
        m_MultipleBounceResourcesEnabled = false;
        m_AdaptiveResourcesEnabled = false;
        m_TemporalResourcesEnabled = false;
        m_PostProcessResourcesEnabled = false;
        m_DepthHierarchyResourcesEnabled = false;
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
        m_FeedbackValid = false;
        m_HistoryIndex = 0u;
        m_FeedbackIndex = 0u;
    }

    void ScreenSpaceVisibilityPass::ResetBindingCache()
    {
        for (auto& estimator : m_SamplingBindingSets)
            for (auto& consumer : estimator)
                for (nvrhi::BindingSetHandle& bindingSet : consumer)
                    bindingSet = nullptr;
        for (auto& estimator : m_MultiBounceFirstBindingSets)
            for (auto& ambientVariant : estimator)
                for (nvrhi::BindingSetHandle& bindingSet : ambientVariant)
                    bindingSet = nullptr;
        for (auto& estimator : m_IndirectBounceBindingSets)
            for (auto& cumulativeMode : estimator)
                for (auto& rotation : cumulativeMode)
                    for (nvrhi::BindingSetHandle& bindingSet : rotation)
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
        for (nvrhi::BindingSetHandle& bindingSet : m_CompositeBindingSets)
            bindingSet = nullptr;
    }

    void ScreenSpaceVisibilityPass::UploadBlueNoise(
        nvrhi::ICommandList* commandList)
    {
        if (m_BlueNoiseUploaded)
            return;

        assert(m_BlueNoiseTexture && !m_BlueNoiseUpload.empty());
        commandList->writeTexture(
            m_BlueNoiseTexture,
            0u,
            0u,
            m_BlueNoiseUpload.data(),
            size_t(kBlueNoiseSize) * sizeof(uint16_t));
        commandList->setPermanentTextureState(
            m_BlueNoiseTexture,
            nvrhi::ResourceStates::ShaderResource);
        m_BlueNoiseUploaded = true;
        m_BlueNoiseUpload.clear();
        m_BlueNoiseUpload.shrink_to_fit();
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
            m_Device->resetTimerQuery(query);
            m_TimerPending[stageIndex][slot] = false;

            if (static_cast<Stage>(stageIndex) == Stage::Sampling &&
                m_SamplingStatisticsPending[slot])
            {
                void* mappedData = m_Device->mapBuffer(
                    m_SamplingStatisticsReadbackBuffers[slot],
                    nvrhi::CpuAccessMode::Read);
                if (mappedData)
                {
                    const auto* values =
                        static_cast<const uint32_t*>(mappedData);
                    m_Timings.totalSampleCount = values[0];
                    m_Timings.totalSliceCount = values[1];
                    m_Timings.refinedPixelCount = values[2];
                    m_Timings.sampledPixelCount = values[3];
                    m_Device->unmapBuffer(
                        m_SamplingStatisticsReadbackBuffers[slot]);
                }
                m_SamplingStatisticsPending[slot] = false;
            }

            switch (static_cast<Stage>(stageIndex))
            {
            case Stage::DepthHierarchy:
                m_Timings.depthHierarchyMs = milliseconds;
                break;
            case Stage::Sampling:
                m_Timings.samplingMs = milliseconds;
                break;
            case Stage::Temporal:
                m_Timings.temporalMs = milliseconds;
                break;
            case Stage::Filtering:
                m_Timings.filteringMs = milliseconds;
                break;
            case Stage::Composition:
                m_Timings.compositionMs = milliseconds;
                break;
            default:
                break;
            }
        }
    }

    void ScreenSpaceVisibilityPass::BeginStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
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
        if (!settings.indirectDiffuse.includeEmissive ||
            !(settings.indirectDiffuse.emissiveGain > 0.f))
        {
            knownInactiveLightingSources |= LightingSource_Emissive;
        }
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
        const bool motionAvailable = inputs.motionVectors != nullptr;
        const bool adaptiveEnabled =
            settings.UsesAdaptiveSampling() && motionAvailable;
        const bool temporalEnabled = settings.reconstruction.enabled &&
            settings.reconstruction.temporalEnabled && motionAvailable;
        const bool postProcessEnabled =
            settings.reconstruction.enabled || resolutionScale > 1u;

        if ((knownInactiveLightingSources & firstBounceSources) ==
            firstBounceSources)
        {
            knownInactiveLightingSources |= LightingSource_IndirectDiffuse;
        }

        const uint32_t requestedBounceCount = indirectEnabled
            ? std::clamp(settings.indirectDiffuse.bounceCount,
                1u, MaxIndirectDiffuseBounceCount)
            : 1u;
        const bool higherBouncesReachConsumer = indirectEnabled &&
            settings.indirectDiffuse.intensity > 0.f &&
            (knownInactiveLightingSources & LightingSource_IndirectDiffuse) == 0u;
        const uint32_t activeBounceCount = higherBouncesReachConsumer
            ? requestedBounceCount : 1u;
        const bool useDepthHierarchy = ambientEnabled && !indirectEnabled &&
            settings.sampling.radius >= 8.f &&
            !view->IsOrthographicProjection();

        EnsureResources(
            fullSize,
            resolutionScale,
            ambientEnabled,
            indirectEnabled,
            activeBounceCount > 1u,
            adaptiveEnabled,
            temporalEnabled,
            postProcessEnabled,
            useDepthHierarchy);
        if (m_HistoryEstimatorInitialized &&
            m_HistoryEstimator != settings.estimator)
        {
            // The three estimators store different measures. Mixing their raw
            // AO/GI values in temporal or adaptive history produces a visible
            // transition and contaminates estimator A/B comparisons.
            ResetHistory();
        }
        m_HistoryEstimator = settings.estimator;
        m_HistoryEstimatorInitialized = true;
        UploadBlueNoise(commandList);
        AdvanceTimers();

        if (!settings.sampling.collectStatistics)
        {
            m_Timings.sampledPixelCount = 0u;
            m_Timings.totalSampleCount = 0u;
            m_Timings.totalSliceCount = 0u;
            m_Timings.refinedPixelCount = 0u;
        }

        const uint32_t consumerVariant =
            GetConsumerVariant(ambientEnabled, indirectEnabled);
        const uint32_t estimatorIndex = std::min(
            static_cast<uint32_t>(settings.estimator),
            ImplementedVisibilityEstimatorCount - 1u);
        const uint32_t feedbackWrite = adaptiveEnabled
            ? m_FeedbackIndex : 0u;
        const uint32_t feedbackRead = 1u - feedbackWrite;
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
        constants.adaptiveStrength = std::max(
            settings.sampling.adaptiveStrength, 0.f);
        constants.ambientStrength = std::max(
            settings.ambientOcclusion.strength, 0.f);
        constants.indirectDiffuseIntensity = std::max(
            settings.indirectDiffuse.intensity, 0.f);
        constants.emissiveGain = std::max(
            settings.indirectDiffuse.emissiveGain, 0.f);
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
        constants.minimumSampleCount = std::clamp(
            settings.sampling.minimumSampleCount, 1u, 64u);
        constants.maximumSampleCount = std::clamp(
            settings.sampling.maximumSampleCount,
            constants.minimumSampleCount, 64u);
        constants.maximumRefinementSlices = std::clamp(
            settings.sampling.maximumRefinementSlices, 1u, 4u);
        constants.knownInactiveLightingSources =
            knownInactiveLightingSources;
        constants.enableAmbientOcclusion = ambientEnabled ? 1u : 0u;
        constants.enableIndirectDiffuse = indirectEnabled ? 1u : 0u;
        constants.includeEmissive =
            settings.indirectDiffuse.includeEmissive ? 1u : 0u;
        constants.reverseDepth = view->IsReverseDepth() ? 1u : 0u;
        constants.orthographicProjection =
            view->IsOrthographicProjection() ? 1u : 0u;
        constants.useDepthHierarchy = useDepthHierarchy ? 1u : 0u;
        constants.resolutionScale = resolutionScale;
        constants.sampleScheduler = static_cast<uint32_t>(
            settings.sampling.scheduler);
        constants.adaptiveSamplingEnabled = adaptiveEnabled ? 1u : 0u;
        constants.collectSamplingStatistics = 0u;
        const uint64_t historyConfigurationKey =
            BuildHistoryConfigurationKey(constants, activeBounceCount);
        if (m_HistoryConfigurationInitialized &&
            historyConfigurationKey != m_HistoryConfigurationKey)
        {
            ResetHistory();
        }
        m_HistoryConfigurationKey = historyConfigurationKey;
        m_HistoryConfigurationInitialized = true;
        constants.feedbackValid = adaptiveEnabled && m_FeedbackValid ? 1u : 0u;
        constants.historyValid = temporalEnabled && m_HistoryValid ? 1u : 0u;
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));

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
            : m_DummyFeedback.Get();
        nvrhi::ITexture* previousFeedback = adaptiveEnabled
            ? m_AdaptiveFeedback[feedbackRead].Get()
            : m_DummyFeedback.Get();
        nvrhi::ITexture* currentFeedback = adaptiveEnabled
            ? m_AdaptiveFeedback[feedbackWrite].Get()
            : m_DummyFeedbackOutput.Get();
        nvrhi::ITexture* motion = motionAvailable
            ? inputs.motionVectors
            : m_DummyFeedback.Get();
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
            ? "Adaptive Visibility Sampling (Multiple Bounces)"
            : "Adaptive Visibility Sampling");
        BeginStage(commandList, Stage::Sampling);
        const bool collectStatisticsThisFrame =
            settings.sampling.collectStatistics &&
            m_TimerActive[static_cast<size_t>(Stage::Sampling)];
        constants.collectSamplingStatistics =
            collectStatisticsThisFrame ? 1u : 0u;
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));
        if (collectStatisticsThisFrame)
            commandList->clearBufferUInt(m_SamplingStatisticsBuffer, 0u);

        const bool writeBounceMetadata = activeBounceCount > 1u;
        Pipeline& firstPipeline = writeBounceMetadata
            ? m_MultiBounceFirstSampling[estimatorIndex]
                [ambientEnabled ? 1u : 0u]
            : m_Sampling[estimatorIndex][consumerVariant];
        nvrhi::BindingSetHandle& firstBindingSet = writeBounceMetadata
            ? m_MultiBounceFirstBindingSets[estimatorIndex]
                [ambientEnabled ? 1u : 0u][feedbackWrite]
            : m_SamplingBindingSets[estimatorIndex]
                [consumerVariant][feedbackWrite];
        if (!firstBindingSet)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth),
                nvrhi::BindingSetItem::Texture_SRV(1, inputs.normals),
                nvrhi::BindingSetItem::Texture_SRV(2, sourceRadiance),
                nvrhi::BindingSetItem::Texture_SRV(3, hierarchy),
                nvrhi::BindingSetItem::Texture_SRV(4, previousFeedback),
                nvrhi::BindingSetItem::Texture_SRV(5, m_BlueNoiseTexture),
                nvrhi::BindingSetItem::Texture_SRV(6, motion),
                nvrhi::BindingSetItem::Texture_UAV(0, rawAmbient),
                nvrhi::BindingSetItem::Texture_UAV(1, rawIndirect),
                nvrhi::BindingSetItem::Texture_UAV(2, currentFeedback),
                nvrhi::BindingSetItem::TypedBuffer_UAV(
                    3, m_SamplingStatisticsBuffer)
            };
            firstBindingSet = m_Device->createBindingSet(
                bindings, firstPipeline.bindingLayout);
        }

        {
            nvrhi::ComputeState state;
            state.pipeline = firstPipeline.pipeline;
            state.bindings = { firstBindingSet };
            commandList->setComputeState(state);
            commandList->dispatch(
                samplingDispatchX, samplingDispatchY, 1u);
        }

        const uint32_t primaryMinimumSampleCount =
            constants.minimumSampleCount;
        const uint32_t primaryMaximumSampleCount =
            constants.maximumSampleCount;
        for (uint32_t bounceIndex = 1u;
            bounceIndex < activeBounceCount;
            ++bounceIndex)
        {
            const uint32_t outputIndex = bounceIndex & 1u;
            const uint32_t previousIndex = 1u - outputIndex;
            constants.minimumSampleCount = GetIndirectBounceSampleCount(
                primaryMinimumSampleCount, bounceIndex);
            constants.maximumSampleCount = std::max(
                constants.minimumSampleCount,
                GetIndirectBounceSampleCount(
                    primaryMaximumSampleCount, bounceIndex));
            constants.collectSamplingStatistics = 0u;
            commandList->writeBuffer(
                m_ConstantBuffer, &constants, sizeof(constants));

            const uint32_t cumulativeMode = bounceIndex == 1u ? 0u : 1u;
            const uint32_t bounceRotation = bounceIndex - 1u;
            Pipeline& pipeline =
                m_IndirectBounceSampling[estimatorIndex][cumulativeMode];
            nvrhi::BindingSetHandle& bindingSet =
                m_IndirectBounceBindingSets[estimatorIndex]
                    [cumulativeMode][bounceRotation][feedbackWrite];
            if (!bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth),
                    nvrhi::BindingSetItem::Texture_SRV(1, inputs.normals),
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
                        7, previousFeedback),
                    nvrhi::BindingSetItem::Texture_SRV(
                        8, m_BlueNoiseTexture),
                    nvrhi::BindingSetItem::Texture_SRV(9, motion),
                    nvrhi::BindingSetItem::Texture_UAV(
                        0, m_RawIndirectDiffuse[outputIndex]),
                    nvrhi::BindingSetItem::Texture_UAV(
                        1, m_CumulativeIndirectDiffuse)
                };
                bindingSet = m_Device->createBindingSet(
                    bindings, pipeline.bindingLayout);
            }

            nvrhi::ComputeState state;
            state.pipeline = pipeline.pipeline;
            state.bindings = { bindingSet };
            commandList->setComputeState(state);
            commandList->dispatch(
                samplingDispatchX, samplingDispatchY, 1u);
        }

        if (activeBounceCount > 1u)
            rawIndirect = m_CumulativeIndirectDiffuse;
        if (collectStatisticsThisFrame)
        {
            const uint32_t slot = m_TimerFrame % c_TimerLatency;
            commandList->copyBuffer(
                m_SamplingStatisticsReadbackBuffers[slot],
                0u,
                m_SamplingStatisticsBuffer,
                0u,
                sizeof(uint32_t) * 4u);
            m_SamplingStatisticsPending[slot] = true;
        }
        EndStage(commandList, Stage::Sampling);
        commandList->endMarker();

        nvrhi::ITexture* reconstructedAmbient = rawAmbient;
        nvrhi::ITexture* reconstructedIndirect = rawIndirect;
        if (temporalEnabled)
        {
            Pipeline& pipeline = m_Temporal[consumerVariant];
            const uint32_t sourceVariant =
                activeBounceCount > 1u ? 1u : 0u;
            nvrhi::BindingSetHandle& bindingSet =
                m_TemporalBindingSets[consumerVariant]
                    [sourceVariant][historyWrite];
            if (!bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(0, rawAmbient),
                    nvrhi::BindingSetItem::Texture_SRV(1, rawIndirect),
                    nvrhi::BindingSetItem::Texture_SRV(2, inputs.depth),
                    nvrhi::BindingSetItem::Texture_SRV(3, inputs.normals),
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
                bindingSet = m_Device->createBindingSet(
                    bindings, pipeline.bindingLayout);
            }

            nvrhi::ComputeState state;
            state.pipeline = pipeline.pipeline;
            state.bindings = { bindingSet };
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

        if (postProcessEnabled)
        {
            uint32_t sourceVariant = activeBounceCount > 1u ? 1u : 0u;
            if (temporalEnabled)
                sourceVariant = 2u + historyWrite;
            const uint32_t filterIndex = std::min(
                static_cast<uint32_t>(
                    settings.reconstruction.spatialFilter), 1u);
            Pipeline& pipeline = m_Filter[filterIndex][consumerVariant];
            nvrhi::BindingSetHandle& bindingSet =
                m_FilterBindingSets[filterIndex]
                    [consumerVariant][sourceVariant];
            if (!bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(
                        0, reconstructedAmbient),
                    nvrhi::BindingSetItem::Texture_SRV(
                        1, reconstructedIndirect),
                    nvrhi::BindingSetItem::Texture_SRV(2, inputs.depth),
                    nvrhi::BindingSetItem::Texture_SRV(3, inputs.normals),
                    nvrhi::BindingSetItem::Texture_UAV(0,
                        ambientEnabled
                            ? m_FinalAmbientVisibility.Get()
                            : m_DummyAmbientOutput.Get()),
                    nvrhi::BindingSetItem::Texture_UAV(1,
                        indirectEnabled
                            ? m_FinalIndirectDiffuse.Get()
                            : m_DummyIndirectOutput.Get())
                };
                bindingSet = m_Device->createBindingSet(
                    bindings, pipeline.bindingLayout);
            }

            nvrhi::ComputeState state;
            state.pipeline = pipeline.pipeline;
            state.bindings = { bindingSet };
            commandList->beginMarker("Visibility Joint Bilateral Filter");
            BeginStage(commandList, Stage::Filtering);
            commandList->setComputeState(state);
            commandList->dispatch(fullDispatchX, fullDispatchY, 1u);
            EndStage(commandList, Stage::Filtering);
            commandList->endMarker();

            if (ambientEnabled)
                reconstructedAmbient = m_FinalAmbientVisibility;
            if (indirectEnabled)
                reconstructedIndirect = m_FinalIndirectDiffuse;
        }
        else
        {
            m_Timings.filteringMs = 0.f;
        }

        const uint32_t compositeVariant =
            !postProcessEnabled && activeBounceCount > 1u ? 1u : 0u;
        nvrhi::BindingSetHandle& compositeBindingSet =
            m_CompositeBindingSets[compositeVariant];
        if (!compositeBindingSet)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, inputs.baseLighting),
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
                    7, inputs.gbufferSpecular),
                nvrhi::BindingSetItem::Texture_UAV(0, inputs.output)
            };
            compositeBindingSet = m_Device->createBindingSet(
                bindings, m_Composite.bindingLayout);
        }

        {
            nvrhi::ComputeState state;
            state.pipeline = m_Composite.pipeline;
            state.bindings = { compositeBindingSet };
            commandList->beginMarker("Screen-Space Indirect Composite");
            BeginStage(commandList, Stage::Composition);
            commandList->setComputeState(state);
            commandList->dispatch(fullDispatchX, fullDispatchY, 1u);
            EndStage(commandList, Stage::Composition);
            commandList->endMarker();
        }

        commandList->endMarker();
        if (adaptiveEnabled)
        {
            m_FeedbackValid = true;
            m_FeedbackIndex = 1u - feedbackWrite;
        }
        else
        {
            m_FeedbackValid = false;
        }
        if (temporalEnabled)
        {
            m_HistoryValid = true;
            m_HistoryIndex = 1u - historyWrite;
        }
        ++m_TimerFrame;
    }
}
