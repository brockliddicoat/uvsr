#include "screen_space_visibility.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <cassert>
#include <string>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "screen_space_visibility_cb.h"

namespace
{
    constexpr uint32_t kThreadGroupSize = 8;
    constexpr uint32_t kMinimumSecondaryBounceSamples = 8;

    bool IsExactGiTransportDebug(
        uvsr::ScreenSpaceVisibilityDebugMode mode)
    {
        return mode >= uvsr::ScreenSpaceVisibilityDebugMode::IndirectDiffuse &&
            mode <= uvsr::ScreenSpaceVisibilityDebugMode::GiLightOnly;
    }

    uint32_t GetIndirectBounceSampleCount(
        uint32_t primarySampleCount,
        uint32_t bounceIndex)
    {
        primarySampleCount = std::clamp(primarySampleCount, 1u, 64u);
        if (bounceIndex == 0u)
            return primarySampleCount;

        // Higher-order diffuse transport carries progressively less energy and
        // spatial detail. Halve its stochastic budget per bounce, but retain a
        // small floor for stable thin-emitter discovery. Never raise a custom
        // primary budget that was already below the floor.
        const uint32_t minimumSampleCount = std::min(
            primarySampleCount, kMinimumSecondaryBounceSamples);
        return std::max(primarySampleCount >> bounceIndex, minimumSampleCount);
    }

}

namespace uvsr
{
    void ApplyScreenSpaceVisibilityQualityPreset(
        ScreenSpaceVisibilitySettings& settings,
        ScreenSpaceVisibilityQuality quality)
    {
        settings.quality = quality;

        if (quality != ScreenSpaceVisibilityQuality::Custom)
        {
            settings.sampling.radius = 3.0f;
            settings.sampling.thickness = 0.5f;
            settings.sampling.distanceScaledThickness = false;
            settings.sampling.thicknessDistanceScale = 0.0025f;
            settings.sampling.stepDistributionExponent = 1.0f;
            settings.sampling.radialJitter = 1.0f;
            settings.sampling.useDepthHierarchy = false;
        }

        switch (quality)
        {
        case ScreenSpaceVisibilityQuality::Low:
            settings.sampling.sampleCount = 16;
            break;

        case ScreenSpaceVisibilityQuality::Medium:
            settings.sampling.sampleCount = 32;
            break;

        case ScreenSpaceVisibilityQuality::High:
            settings.sampling.sampleCount = 48;
            break;

        case ScreenSpaceVisibilityQuality::Ultra:
            settings.sampling.sampleCount = 64;
            break;

        case ScreenSpaceVisibilityQuality::Custom:
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
        constantBufferDesc.debugName = "ScreenSpaceVisibilityConstants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions = engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDesc);

        // Two uints hold the number of depth-eligible later-bounce receiver
        // attempts and the subset rejected by diffuse-transport metadata. The
        // tiny ring is read only after the matching sampling timer proves the
        // command list complete.
        nvrhi::BufferDesc statisticsBufferDesc;
        statisticsBufferDesc.byteSize = sizeof(uint32_t) * 2u;
        statisticsBufferDesc.format = nvrhi::Format::R32_UINT;
        statisticsBufferDesc.canHaveUAVs = true;
        statisticsBufferDesc.canHaveTypedViews = true;
        statisticsBufferDesc.initialState = nvrhi::ResourceStates::CopySource;
        statisticsBufferDesc.keepInitialState = true;
        statisticsBufferDesc.debugName =
            "ScreenSpaceVisibility/HigherBounceReceiverStatistics";
        m_HigherBounceStatisticsBuffer =
            device->createBuffer(statisticsBufferDesc);

        statisticsBufferDesc.canHaveUAVs = false;
        statisticsBufferDesc.canHaveTypedViews = false;
        statisticsBufferDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
        statisticsBufferDesc.debugName =
            "ScreenSpaceVisibility/HigherBounceReceiverStatisticsReadback";
        for (nvrhi::BufferHandle& readbackBuffer :
            m_HigherBounceStatisticsReadbackBuffers)
        {
            readbackBuffer = device->createBuffer(statisticsBufferDesc);
        }

        CreatePipelines(shaderFactory);

        for (auto& stageQueries : m_TimerQueries)
        {
            for (auto& query : stageQueries)
                query = device->createTimerQuery();
        }
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
            destination.bindingLayout = m_Device->createBindingLayout(layoutDesc);

            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = destination.shader;
            pipelineDesc.bindingLayouts = { destination.bindingLayout };
            destination.pipeline = m_Device->createComputePipeline(pipelineDesc);
        };

        for (uint32_t estimatorIndex = 0;
            estimatorIndex < ImplementedVisibilityEstimatorCount;
            ++estimatorIndex)
        {
            for (uint32_t variant = 0;
                variant < m_Sampling[estimatorIndex].size();
                ++variant)
            {
                const uint32_t slicePermutation = variant / 8u;
                const uint32_t samplingVariant = variant % 8u;
                // Render() releases the pass when neither AO nor GI has a
                // consumer, so the 0/4 no-consumer variants are unreachable.
                if ((samplingVariant & 3u) == 0u)
                    continue;

                std::vector<ShaderMacro> macros;
                macros.emplace_back(
                    "VISIBILITY_ESTIMATOR", std::to_string(estimatorIndex));
                macros.emplace_back(
                    "STATIC_SLICE_COUNT", slicePermutation == 0u ? "1" : "0");
                macros.emplace_back("ENABLE_AO", (samplingVariant & 1u) != 0u ? "1" : "0");
                macros.emplace_back("ENABLE_GI", (samplingVariant & 2u) != 0u ? "1" : "0");
                macros.emplace_back("ENABLE_TRAVERSAL_DEBUG", (samplingVariant & 4u) != 0u ? "1" : "0");
                macros.emplace_back("ENABLE_BOUNCE_METADATA", "0");
                createPipeline(
                    m_Sampling[estimatorIndex][variant],
                    "uvsr/screen_space_visibility_cs.hlsl",
                    {
                        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(1),
                        nvrhi::BindingLayoutItem::Texture_SRV(2),
                        nvrhi::BindingLayoutItem::Texture_SRV(3),
                        nvrhi::BindingLayoutItem::Texture_UAV(0),
                        nvrhi::BindingLayoutItem::Texture_UAV(1),
                        nvrhi::BindingLayoutItem::Texture_UAV(2)
                    },
                    &macros);
            }
        }

        for (uint32_t estimatorIndex = 0;
            estimatorIndex < ImplementedVisibilityEstimatorCount;
            ++estimatorIndex)
        {
            for (uint32_t variant = 0;
                variant < m_MultiBounceFirstSampling[estimatorIndex].size();
                ++variant)
            {
                const uint32_t slicePermutation = variant / 2u;
                const uint32_t samplingVariant = variant % 2u;
                std::vector<ShaderMacro> macros;
                macros.emplace_back(
                    "VISIBILITY_ESTIMATOR", std::to_string(estimatorIndex));
                macros.emplace_back(
                    "STATIC_SLICE_COUNT", slicePermutation == 0u ? "1" : "0");
                macros.emplace_back("ENABLE_AO", (samplingVariant & 1u) != 0u ? "1" : "0");
                macros.emplace_back("ENABLE_GI", "1");
                // Traversal-debug views never consume higher-bounce GI and are
                // clamped to one bounce, so metadata needs no debug permutation.
                macros.emplace_back("ENABLE_TRAVERSAL_DEBUG", "0");
                macros.emplace_back("ENABLE_BOUNCE_METADATA", "1");
                createPipeline(
                    m_MultiBounceFirstSampling[estimatorIndex][variant],
                    "uvsr/screen_space_visibility_cs.hlsl",
                    {
                        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(1),
                        nvrhi::BindingLayoutItem::Texture_SRV(2),
                        nvrhi::BindingLayoutItem::Texture_SRV(3),
                        nvrhi::BindingLayoutItem::Texture_UAV(0),
                        nvrhi::BindingLayoutItem::Texture_UAV(1),
                        nvrhi::BindingLayoutItem::Texture_UAV(2)
                    },
                    &macros);
            }
        }

        for (uint32_t estimatorIndex = 0;
            estimatorIndex < ImplementedVisibilityEstimatorCount;
            ++estimatorIndex)
        {
            for (uint32_t variant = 0;
                variant < m_IndirectBounceSampling[estimatorIndex].size();
                ++variant)
            {
                const uint32_t slicePermutation = variant / 4u;
                const uint32_t bounceVariant = variant % 4u;
                const bool initializeCumulative = (bounceVariant & 1u) == 0u;
                const bool collectReceiverStatistics = (bounceVariant & 2u) != 0u;
                std::vector<ShaderMacro> indirectBounceMacros;
                indirectBounceMacros.emplace_back(
                    "VISIBILITY_ESTIMATOR", std::to_string(estimatorIndex));
                indirectBounceMacros.emplace_back(
                    "STATIC_SLICE_COUNT", slicePermutation == 0u ? "1" : "0");
                indirectBounceMacros.emplace_back("ENABLE_AO", "0");
                indirectBounceMacros.emplace_back("ENABLE_GI", "1");
                indirectBounceMacros.emplace_back("ENABLE_TRAVERSAL_DEBUG", "0");
                indirectBounceMacros.emplace_back("ENABLE_BOUNCE_REINJECTION", "1");
                indirectBounceMacros.emplace_back(
                    "INITIALIZE_BOUNCE_CUMULATIVE",
                    initializeCumulative ? "1" : "0");
                indirectBounceMacros.emplace_back(
                    "ENABLE_BOUNCE_STATISTICS",
                    collectReceiverStatistics ? "1" : "0");
                std::vector<nvrhi::BindingLayoutItem> bindings = {
                    nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                    nvrhi::BindingLayoutItem::Texture_SRV(0),
                    nvrhi::BindingLayoutItem::Texture_SRV(1),
                    nvrhi::BindingLayoutItem::Texture_SRV(2),
                    nvrhi::BindingLayoutItem::Texture_SRV(3),
                    nvrhi::BindingLayoutItem::Texture_SRV(4),
                    nvrhi::BindingLayoutItem::Texture_SRV(5),
                    nvrhi::BindingLayoutItem::Texture_SRV(6),
                    nvrhi::BindingLayoutItem::Texture_UAV(0),
                    nvrhi::BindingLayoutItem::Texture_UAV(1)
                };
                if (collectReceiverStatistics)
                    bindings.push_back(nvrhi::BindingLayoutItem::TypedBuffer_UAV(2));
                createPipeline(
                    m_IndirectBounceSampling[estimatorIndex][variant],
                    "uvsr/screen_space_visibility_cs.hlsl",
                    bindings,
                    &indirectBounceMacros);
            }
        }

        createPipeline(m_DepthHierarchy, "uvsr/screen_space_depth_hierarchy_cs.hlsl", {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0).setSize(5)
        });

        createPipeline(m_Composite, "uvsr/screen_space_indirect_composite_cs.hlsl", {
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
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        });
    }

    void ScreenSpaceVisibilityPass::EnsureResources(
        uint2 fullSize,
        bool multipleBouncesEnabled,
        bool depthHierarchyEnabled)
    {
        const uint2 samplingSize = fullSize;

        if (all(m_FullSize == fullSize) && all(m_SamplingSize == samplingSize) &&
            m_MultipleBounceResourcesEnabled == multipleBouncesEnabled &&
            m_DepthHierarchyResourcesEnabled == depthHierarchyEnabled &&
            m_RawAmbientVisibility)
        {
            return;
        }

        ReleaseResources();
        m_FullSize = fullSize;
        m_SamplingSize = samplingSize;
        m_MultipleBounceResourcesEnabled = multipleBouncesEnabled;
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
            desc.mipLevels = 1;
            desc.isUAV = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;
            desc.debugName = debugName;
            return m_Device->createTexture(desc);
        };

        m_RawAmbientVisibility = createTexture(samplingSize, nvrhi::Format::R16_FLOAT,
            "ScreenSpaceVisibility/RawAmbientVisibility");

        if (depthHierarchyEnabled)
        {
            nvrhi::TextureDesc hierarchyDesc;
            hierarchyDesc.width = std::max((fullSize.x + 15u) & ~15u, 16u);
            hierarchyDesc.height = std::max((fullSize.y + 15u) & ~15u, 16u);
            hierarchyDesc.format = nvrhi::Format::R16_FLOAT;
            hierarchyDesc.dimension = nvrhi::TextureDimension::Texture2D;
            hierarchyDesc.mipLevels = 5;
            hierarchyDesc.isUAV = true;
            hierarchyDesc.initialState = nvrhi::ResourceStates::ShaderResource;
            hierarchyDesc.keepInitialState = true;
            hierarchyDesc.debugName = "ScreenSpaceVisibility/DepthHierarchy";
            m_DepthHierarchyTexture = m_Device->createTexture(hierarchyDesc);
        }
        m_RawIndirectDiffuse[0] = createTexture(
            samplingSize, nvrhi::Format::RGBA16_FLOAT,
            "ScreenSpaceVisibility/IndirectDiffuseFrontier0");
        if (multipleBouncesEnabled)
        {
            m_RawIndirectDiffuse[1] = createTexture(
                samplingSize, nvrhi::Format::RGBA16_FLOAT,
                "ScreenSpaceVisibility/IndirectDiffuseFrontier1");
            m_CumulativeIndirectDiffuse = createTexture(
                samplingSize, nvrhi::Format::RGBA16_FLOAT,
                "ScreenSpaceVisibility/CumulativeIndirectDiffuseIrradiance");
        }
        m_RawDebug = createTexture(samplingSize, nvrhi::Format::RGBA8_UNORM,
            "ScreenSpaceVisibility/RawDebug");
    }

    void ScreenSpaceVisibilityPass::ReleaseResources()
    {
        for (auto& estimatorBindingSets : m_SamplingBindingSets)
            for (nvrhi::BindingSetHandle& bindingSet : estimatorBindingSets)
                bindingSet = nullptr;
        for (auto& estimatorBindingSets : m_MultiBounceFirstBindingSets)
            for (nvrhi::BindingSetHandle& bindingSet : estimatorBindingSets)
                bindingSet = nullptr;
        for (auto& estimatorBindingSets : m_IndirectBounceBindingSets)
            for (nvrhi::BindingSetHandle& bindingSet : estimatorBindingSets)
                bindingSet = nullptr;
        m_DepthHierarchyBindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_CompositeBindingSets)
            bindingSet = nullptr;

        m_RawAmbientVisibility = nullptr;
        m_DepthHierarchyTexture = nullptr;
        for (nvrhi::TextureHandle& rawIndirectDiffuse : m_RawIndirectDiffuse)
            rawIndirectDiffuse = nullptr;
        m_CumulativeIndirectDiffuse = nullptr;
        m_RawDebug = nullptr;
        m_FullSize = uint2::zero();
        m_SamplingSize = uint2::zero();
        m_MultipleBounceResourcesEnabled = false;
        m_DepthHierarchyResourcesEnabled = false;
        m_Timings = {};
    }

    void ScreenSpaceVisibilityPass::Deactivate()
    {
        ReleaseResources();
    }

    void ScreenSpaceVisibilityPass::ResetBindingCache()
    {
        for (auto& estimatorBindingSets : m_SamplingBindingSets)
            for (nvrhi::BindingSetHandle& bindingSet : estimatorBindingSets)
                bindingSet = nullptr;
        for (auto& estimatorBindingSets : m_MultiBounceFirstBindingSets)
            for (nvrhi::BindingSetHandle& bindingSet : estimatorBindingSets)
                bindingSet = nullptr;
        for (auto& estimatorBindingSets : m_IndirectBounceBindingSets)
            for (nvrhi::BindingSetHandle& bindingSet : estimatorBindingSets)
                bindingSet = nullptr;
        m_DepthHierarchyBindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_CompositeBindingSets)
            bindingSet = nullptr;
    }

    void ScreenSpaceVisibilityPass::AdvanceTimers()
    {
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        for (uint32_t stageIndex = 0; stageIndex < static_cast<uint32_t>(Stage::Count); ++stageIndex)
        {
            m_TimerActive[stageIndex] = false;
            if (!m_TimerPending[stageIndex][slot])
                continue;

            nvrhi::ITimerQuery* query = m_TimerQueries[stageIndex][slot];
            if (!m_Device->pollTimerQuery(query))
                continue;

            const float milliseconds = m_Device->getTimerQueryTime(query) * 1000.f;
            m_Device->resetTimerQuery(query);
            m_TimerPending[stageIndex][slot] = false;

            if (static_cast<Stage>(stageIndex) == Stage::Sampling &&
                m_HigherBounceStatisticsPending[slot])
            {
                void* mappedData = m_Device->mapBuffer(
                    m_HigherBounceStatisticsReadbackBuffers[slot],
                    nvrhi::CpuAccessMode::Read);
                if (mappedData)
                {
                    const auto* values = static_cast<const uint32_t*>(mappedData);
                    m_Timings.higherBounceEligibleReceiverCount = values[0];
                    m_Timings.higherBounceRejectedReceiverCount = values[1];
                    m_Device->unmapBuffer(
                        m_HigherBounceStatisticsReadbackBuffers[slot]);
                }
                m_HigherBounceStatisticsPending[slot] = false;
            }

            switch (static_cast<Stage>(stageIndex))
            {
            case Stage::DepthHierarchy: m_Timings.depthHierarchyMs = milliseconds; break;
            case Stage::Sampling: m_Timings.samplingMs = milliseconds; break;
            case Stage::Composition: m_Timings.compositionMs = milliseconds; break;
            default: break;
            }
        }
    }

    void ScreenSpaceVisibilityPass::BeginStage(nvrhi::ICommandList* commandList, Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        if (m_TimerPending[stageIndex][slot])
            return;

        commandList->beginTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerActive[stageIndex] = true;
    }

    void ScreenSpaceVisibilityPass::EndStage(nvrhi::ICommandList* commandList, Stage stage)
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
            ReleaseResources();
            return;
        }

        assert(commandList);
        assert(inputs.depth && inputs.normals);
        assert(inputs.sourceRadiance && inputs.gbufferDiffuse && inputs.gbufferSpecular &&
            inputs.gbufferEmissive);
        assert(inputs.materialAmbientOcclusion && inputs.baseLighting && inputs.output);
        assert(compositeView.GetNumChildViews(ViewType::PLANAR) == 1);

        const IView* view = compositeView.GetChildView(ViewType::PLANAR, 0);
        const nvrhi::TextureDesc& depthDesc = inputs.depth->getDesc();
        const uint2 fullSize(depthDesc.width, depthDesc.height);
        const uint32_t requestedBounceCount = settings.indirectDiffuse.enabled
            ? std::clamp(settings.indirectDiffuse.bounceCount,
                1u, MaxIndirectDiffuseBounceCount)
            : 1u;
        uint32_t knownInactiveLightingSources =
            inputs.knownInactiveLightingSources & LightingSource_All;
        if (!settings.indirectDiffuse.includeEmissive ||
            !(settings.indirectDiffuse.emissiveGain > 0.f))
        {
            knownInactiveLightingSources |= LightingSource_Emissive;
        }
        const uint32_t firstBounceSources =
            LightingSource_Direct | LightingSource_Emissive;
        if ((knownInactiveLightingSources & firstBounceSources) ==
            firstBounceSources)
        {
            knownInactiveLightingSources |= LightingSource_IndirectDiffuse;
        }
        // Configuration controls allocation lifetime. Proven scene inactivity
        // and a zero final GI scale can still terminate the higher-bounce chain
        // for this frame without reallocating textures or invalidating caches.
        const bool diagnosticNeedsExactBounceChain =
            IsExactGiTransportDebug(settings.debug.mode);
        const bool statisticsNeedBounceChain =
            settings.debug.collectHigherBounceReceiverStatistics &&
            requestedBounceCount > 1u;
        const bool finalCompositeNeedsHigherBounces =
            settings.debug.mode == ScreenSpaceVisibilityDebugMode::FinalComposite &&
            (knownInactiveLightingSources & LightingSource_IndirectDiffuse) == 0u &&
            settings.indirectDiffuse.intensity > 0.f;
        const bool higherBouncesCanReachConsumer =
            diagnosticNeedsExactBounceChain || finalCompositeNeedsHigherBounces ||
            statisticsNeedBounceChain;
        const uint32_t activeBounceCount = higherBouncesCanReachConsumer
            ? requestedBounceCount
            : 1u;
        const bool useDepthHierarchyThisFrame =
            settings.sampling.useDepthHierarchy &&
            !settings.indirectDiffuse.enabled &&
            !view->IsOrthographicProjection();
        EnsureResources(
            fullSize,
            requestedBounceCount > 1u,
            useDepthHierarchyThisFrame);
        AdvanceTimers();
        if (!settings.debug.collectHigherBounceReceiverStatistics)
        {
            m_Timings.higherBounceEligibleReceiverCount = 0u;
            m_Timings.higherBounceRejectedReceiverCount = 0u;
        }

        exposureScale = std::max(exposureScale, 0.f);
        const uint32_t consumerVariant =
            (settings.ambientOcclusion.enabled ? 1u : 0u) |
            (settings.indirectDiffuse.enabled ? 2u : 0u);
        assert(settings.estimator != VisibilityEstimator::GTCosine &&
            "GTCosine remains gated on GTUniform promotion");
        const uint32_t estimatorIndex =
            settings.estimator == VisibilityEstimator::GTUniform ? 1u : 0u;
        const bool traversalDebugActive =
            settings.debug.mode >= ScreenSpaceVisibilityDebugMode::ReceiverNormal &&
            settings.debug.mode <= ScreenSpaceVisibilityDebugMode::GtEndpointOrder;
        const uint32_t samplingVariant = consumerVariant |
            (traversalDebugActive ? 4u : 0u);

        ScreenSpaceVisibilityConstants constants{};
        view->FillPlanarViewConstants(constants.view);
        constants.fullResolution = float2(fullSize);
        constants.samplingResolution = float2(m_SamplingSize);
        constants.radiusWorld = std::max(settings.sampling.radius, 0.f);
        constants.thicknessWorld = std::max(settings.sampling.thickness, 0.f);
        constants.thicknessDistanceScale = std::max(settings.sampling.thicknessDistanceScale, 0.f);
        constants.stepDistributionExponent = std::max(settings.sampling.stepDistributionExponent, 0.01f);
        constants.radialJitter = std::clamp(settings.sampling.radialJitter, 0.f, 1.f);
        constants.ambientStrength = std::max(settings.ambientOcclusion.strength, 0.f);
        constants.ambientPower = std::max(settings.ambientOcclusion.power, 0.01f);
        constants.indirectDiffuseIntensity = std::max(settings.indirectDiffuse.intensity, 0.f);
        constants.emissiveGain = std::max(settings.indirectDiffuse.emissiveGain, 0.f);
        constants.minimumBounceContribution =
            diagnosticNeedsExactBounceChain
                ? 0.f
                : std::max(
                    settings.indirectDiffuse.minimumBounceContribution, 0.f);
        constants.lightingExposureScale = exposureScale;
        constants.ambientColorTop = ambientColorTop;
        constants.ambientColorBottom = ambientColorBottom;
        constants.frameIndex = settings.debug.freezeSamplingPhase ? 0u : frameIndex;
        constants.sliceCount = std::clamp(
            settings.debug.developerSliceCount, 1u, 8u);
        const uint32_t slicePermutation = constants.sliceCount > 1u ? 1u : 0u;
        const uint32_t primarySampleCount = std::clamp(
            settings.sampling.sampleCount, 1u, 64u);
        constants.sampleCount = primarySampleCount;
        constants.knownInactiveLightingSources = knownInactiveLightingSources;
        constants.enableAmbientOcclusion = settings.ambientOcclusion.enabled ? 1u : 0u;
        constants.enableIndirectDiffuse = settings.indirectDiffuse.enabled ? 1u : 0u;
        constants.includeEmissive = settings.indirectDiffuse.includeEmissive ? 1u : 0u;
        constants.distanceScaledThickness = settings.sampling.distanceScaledThickness ? 1u : 0u;
        constants.freezeSamplingPhase = settings.debug.freezeSamplingPhase ? 1u : 0u;
        constants.sectorHitCriterion = static_cast<uint32_t>(settings.debug.sectorHitCriterion);
        constants.debugMode = static_cast<uint32_t>(settings.debug.mode);
        constants.reverseDepth = view->IsReverseDepth() ? 1u : 0u;
        constants.orthographicProjection = view->IsOrthographicProjection() ? 1u : 0u;
        constants.useDepthHierarchy = useDepthHierarchyThisFrame ? 1u : 0u;
        commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

        const uint32_t samplingDispatchX = (m_SamplingSize.x + kThreadGroupSize - 1u) / kThreadGroupSize;
        const uint32_t samplingDispatchY = (m_SamplingSize.y + kThreadGroupSize - 1u) / kThreadGroupSize;
        const uint32_t fullDispatchX = (fullSize.x + kThreadGroupSize - 1u) / kThreadGroupSize;
        const uint32_t fullDispatchY = (fullSize.y + kThreadGroupSize - 1u) / kThreadGroupSize;

        commandList->beginMarker(settings.indirectDiffuse.enabled
            ? (settings.ambientOcclusion.enabled ? "Screen-Space Visibility (AO + GI)" : "Screen-Space Visibility (GI)")
            : "Screen-Space Visibility (AO)");

        nvrhi::ITexture* rawIndirectDiffuse = m_RawIndirectDiffuse[0];

        if (useDepthHierarchyThisFrame)
        {
            if (!m_DepthHierarchyBindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth)
                };
                for (uint32_t mipLevel = 0; mipLevel < 5; ++mipLevel)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::Texture_UAV(
                            0, m_DepthHierarchyTexture)
                            .setArrayElement(mipLevel)
                            .setSubresources(nvrhi::TextureSubresourceSet(
                                mipLevel, 1, 0, 1)));
                }
                m_DepthHierarchyBindingSet = m_Device->createBindingSet(
                    bindings, m_DepthHierarchy.bindingLayout);
            }
            nvrhi::ComputeState state;
            state.pipeline = m_DepthHierarchy.pipeline;
            state.bindings = { m_DepthHierarchyBindingSet };
            commandList->beginMarker("XeGTAO View-Depth Hierarchy");
            BeginStage(commandList, Stage::DepthHierarchy);
            commandList->setComputeState(state);
            commandList->dispatch((fullSize.x + 15u) / 16u,
                (fullSize.y + 15u) / 16u, 1);
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
        BeginStage(commandList, Stage::Sampling);
        // Tie statistics collection to an available timer slot. The readback
        // for that slot is mapped only after its sampling query reports GPU
        // completion, avoiding a CPU/GPU synchronization point.
        const bool collectHigherBounceStatisticsThisFrame =
            settings.debug.collectHigherBounceReceiverStatistics &&
            activeBounceCount > 1u &&
            m_TimerActive[static_cast<size_t>(Stage::Sampling)];
        if (collectHigherBounceStatisticsThisFrame)
            commandList->clearBufferUInt(m_HigherBounceStatisticsBuffer, 0u);

        // Keep the default one-bounce path on the original compact shader.
        // Packed receiver metadata is emitted only when a later bounce will
        // consume it this frame; both variants retain the same four-SRV layout.
        {
            const bool writeBounceMetadata = activeBounceCount > 1u;
            const uint32_t multiBounceVariant =
                settings.ambientOcclusion.enabled ? 1u : 0u;
            const uint32_t sliceSamplingOffset = slicePermutation * 8u;
            const uint32_t sliceMultiBounceOffset = slicePermutation * 2u;
            Pipeline& pipeline = writeBounceMetadata
                ? m_MultiBounceFirstSampling[estimatorIndex]
                    [multiBounceVariant + sliceMultiBounceOffset]
                : m_Sampling[estimatorIndex]
                    [samplingVariant + sliceSamplingOffset];
            nvrhi::BindingSetHandle& bindingSet = writeBounceMetadata
                ? m_MultiBounceFirstBindingSets[estimatorIndex]
                    [multiBounceVariant + sliceMultiBounceOffset]
                : m_SamplingBindingSets[estimatorIndex]
                    [samplingVariant + sliceSamplingOffset];
            if (!bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth),
                    nvrhi::BindingSetItem::Texture_SRV(1, inputs.normals),
                    nvrhi::BindingSetItem::Texture_SRV(2, inputs.sourceRadiance),
                    nvrhi::BindingSetItem::Texture_SRV(3,
                        m_DepthHierarchyTexture
                            ? m_DepthHierarchyTexture.Get()
                            : inputs.depth),
                    nvrhi::BindingSetItem::Texture_UAV(0, m_RawAmbientVisibility),
                    nvrhi::BindingSetItem::Texture_UAV(1, m_RawIndirectDiffuse[0]),
                    nvrhi::BindingSetItem::Texture_UAV(2, m_RawDebug)
                };
                bindingSet = m_Device->createBindingSet(
                    bindings, pipeline.bindingLayout);
            }

            nvrhi::ComputeState state;
            state.pipeline = pipeline.pipeline;
            state.bindings = { bindingSet };
            commandList->setComputeState(state);
            commandList->dispatch(samplingDispatchX, samplingDispatchY, 1);
        }

        // Higher-order diffuse transport carries less energy and fine detail,
        // so halve its sampling budget toward a small floor. Secondary passes
        // transport only the incremental frontier B(n)=T(B(n-1)); a separate
        // UAV accumulates C(n)=C(n-1)+B(n). This makes source-sample energy
        // gates decay with bounce order without risking FP16 cancellation.
        for (uint32_t bounceIndex = 1u;
            bounceIndex < activeBounceCount;
            ++bounceIndex)
        {
            const uint32_t outputIndex = bounceIndex & 1u;
            const uint32_t previousIndex = 1u - outputIndex;
            constants.sampleCount = GetIndirectBounceSampleCount(
                primarySampleCount, bounceIndex);
            commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

            const uint32_t statisticsBindingOffset =
                collectHigherBounceStatisticsThisFrame ? 3u : 0u;
            const uint32_t sliceBindingOffset = slicePermutation * 6u;
            nvrhi::BindingSetHandle& bindingSet =
                m_IndirectBounceBindingSets[estimatorIndex]
                    [bounceIndex - 1u + statisticsBindingOffset +
                        sliceBindingOffset];
            const uint32_t pipelineVariant =
                (bounceIndex > 1u ? 1u : 0u) |
                (collectHigherBounceStatisticsThisFrame ? 2u : 0u) |
                (slicePermutation * 4u);
            Pipeline& pipeline =
                m_IndirectBounceSampling[estimatorIndex][pipelineVariant];
            if (!bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth),
                    nvrhi::BindingSetItem::Texture_SRV(1, inputs.normals),
                    nvrhi::BindingSetItem::Texture_SRV(
                        2, m_RawIndirectDiffuse[previousIndex]),
                    nvrhi::BindingSetItem::Texture_SRV(3,
                        m_DepthHierarchyTexture
                            ? m_DepthHierarchyTexture.Get()
                            : inputs.depth),
                    nvrhi::BindingSetItem::Texture_SRV(4, inputs.gbufferDiffuse),
                    nvrhi::BindingSetItem::Texture_SRV(5, inputs.gbufferEmissive),
                    nvrhi::BindingSetItem::Texture_SRV(6, inputs.materialAmbientOcclusion),
                    nvrhi::BindingSetItem::Texture_UAV(
                        0, m_RawIndirectDiffuse[outputIndex]),
                    nvrhi::BindingSetItem::Texture_UAV(
                        1, m_CumulativeIndirectDiffuse)
                };
                if (collectHigherBounceStatisticsThisFrame)
                {
                    bindings.bindings.push_back(
                        nvrhi::BindingSetItem::TypedBuffer_UAV(
                            2, m_HigherBounceStatisticsBuffer));
                }
                bindingSet = m_Device->createBindingSet(
                    bindings, pipeline.bindingLayout);
            }

            nvrhi::ComputeState state;
            state.pipeline = pipeline.pipeline;
            state.bindings = { bindingSet };
            commandList->setComputeState(state);
            commandList->dispatch(samplingDispatchX, samplingDispatchY, 1);
        }
        if (activeBounceCount > 1u)
            rawIndirectDiffuse = m_CumulativeIndirectDiffuse;
        if (collectHigherBounceStatisticsThisFrame)
        {
            const uint32_t statisticsSlot = m_TimerFrame % c_TimerLatency;
            commandList->copyBuffer(
                m_HigherBounceStatisticsReadbackBuffers[statisticsSlot],
                0u,
                m_HigherBounceStatisticsBuffer,
                0u,
                sizeof(uint32_t) * 2u);
            m_HigherBounceStatisticsPending[statisticsSlot] = true;
        }
        EndStage(commandList, Stage::Sampling);
        commandList->endMarker();

        {
            const uint32_t compositeBindingIndex =
                activeBounceCount > 1u ? 1u : 0u;
            nvrhi::BindingSetHandle& bindingSet =
                m_CompositeBindingSets[compositeBindingIndex];
            if (!bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(0, inputs.baseLighting),
                    nvrhi::BindingSetItem::Texture_SRV(1, m_RawAmbientVisibility),
                    nvrhi::BindingSetItem::Texture_SRV(2, rawIndirectDiffuse),
                    nvrhi::BindingSetItem::Texture_SRV(3, inputs.gbufferDiffuse),
                    nvrhi::BindingSetItem::Texture_SRV(4, inputs.gbufferEmissive),
                    nvrhi::BindingSetItem::Texture_SRV(5, inputs.materialAmbientOcclusion),
                    nvrhi::BindingSetItem::Texture_SRV(6, inputs.normals),
                    nvrhi::BindingSetItem::Texture_SRV(7, m_RawDebug),
                    nvrhi::BindingSetItem::Texture_SRV(8, inputs.sourceRadiance),
                    nvrhi::BindingSetItem::Texture_SRV(9, inputs.gbufferSpecular),
                    nvrhi::BindingSetItem::Texture_UAV(0, inputs.output)
                };
                bindingSet = m_Device->createBindingSet(
                    bindings, m_Composite.bindingLayout);
            }
            nvrhi::ComputeState state;
            state.pipeline = m_Composite.pipeline;
            state.bindings = { bindingSet };
            commandList->beginMarker("Screen-Space Indirect Composite");
            BeginStage(commandList, Stage::Composition);
            commandList->setComputeState(state);
            commandList->dispatch(fullDispatchX, fullDispatchY, 1);
            EndStage(commandList, Stage::Composition);
            commandList->endMarker();
        }

        commandList->endMarker();
        ++m_TimerFrame;
    }
}
