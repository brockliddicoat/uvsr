#include "screen_space_visibility.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

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
        return mode >= uvsr::ScreenSpaceVisibilityDebugMode::RawIndirectDiffuse &&
            mode <= uvsr::ScreenSpaceVisibilityDebugMode::GiLightOnly;
    }

    uint32_t GetBounceTransportPolicy(
        uvsr::ScreenSpaceVisibilityDebugMode mode)
    {
        if (mode == uvsr::ScreenSpaceVisibilityDebugMode::FinalComposite)
            return 0u;
        return IsExactGiTransportDebug(mode) ? 1u : 2u;
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

    uint64_t HashBytes(uint64_t hash, const void* data, size_t size)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    template <typename T>
    uint64_t HashValue(uint64_t hash, const T& value)
    {
        return HashBytes(hash, &value, sizeof(value));
    }

    float SquaredDistance(float3 a, float3 b)
    {
        const float3 difference = a - b;
        return dot(difference, difference);
    }

    float SafeDirectionDot(float3 a, float3 b)
    {
        const float aLengthSquared = dot(a, a);
        const float bLengthSquared = dot(b, b);
        if (aLengthSquared <= 1e-12f || bLengthSquared <= 1e-12f)
            return -1.f;
        return dot(a, b) / std::sqrt(aLengthSquared * bLengthSquared);
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
            settings.filtering.temporalEnabled = true;
            settings.filtering.spatialEnabled = false;
            settings.filtering.spatialRadius = 1;
            break;

        case ScreenSpaceVisibilityQuality::Medium:
            settings.sampling.sampleCount = 32;
            settings.filtering.temporalEnabled = true;
            settings.filtering.spatialEnabled = false;
            settings.filtering.spatialRadius = 1;
            break;

        case ScreenSpaceVisibilityQuality::High:
            settings.sampling.sampleCount = 48;
            settings.filtering.temporalEnabled = true;
            settings.filtering.spatialEnabled = false;
            settings.filtering.spatialRadius = 1;
            break;

        case ScreenSpaceVisibilityQuality::Ultra:
            settings.sampling.sampleCount = 64;
            settings.filtering.temporalEnabled = true;
            settings.filtering.spatialEnabled = false;
            settings.filtering.spatialRadius = 2;
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
            std::initializer_list<nvrhi::BindingLayoutItem> bindings,
            const std::vector<ShaderMacro>* macros = nullptr)
        {
            destination.shader = shaderFactory->CreateShader(
                shaderName, "main", macros, nvrhi::ShaderType::Compute);

            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility = nvrhi::ShaderType::Compute;
            layoutDesc.bindings.assign(bindings.begin(), bindings.end());
            destination.bindingLayout = m_Device->createBindingLayout(layoutDesc);

            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = destination.shader;
            pipelineDesc.bindingLayouts = { destination.bindingLayout };
            destination.pipeline = m_Device->createComputePipeline(pipelineDesc);
        };

        for (uint32_t variant = 0; variant < m_Sampling.size(); ++variant)
        {
            // Render() releases the pass when neither AO nor GI has a consumer,
            // so the 0/4 no-consumer variants are unreachable.
            if ((variant & 3u) == 0u)
                continue;

            std::vector<ShaderMacro> macros;
            macros.emplace_back("ENABLE_AO", (variant & 1u) != 0u ? "1" : "0");
            macros.emplace_back("ENABLE_GI", (variant & 2u) != 0u ? "1" : "0");
            macros.emplace_back("ENABLE_TRAVERSAL_DEBUG", (variant & 4u) != 0u ? "1" : "0");
            macros.emplace_back("ENABLE_BOUNCE_METADATA", "0");
            createPipeline(m_Sampling[variant], "uvsr/screen_space_visibility_cs.hlsl", {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_SRV(3),
                nvrhi::BindingLayoutItem::Texture_UAV(0),
                nvrhi::BindingLayoutItem::Texture_UAV(1),
                nvrhi::BindingLayoutItem::Texture_UAV(2)
            }, &macros);
        }

        for (uint32_t variant = 0;
            variant < m_MultiBounceFirstSampling.size();
            ++variant)
        {
            std::vector<ShaderMacro> macros;
            macros.emplace_back("ENABLE_AO", (variant & 1u) != 0u ? "1" : "0");
            macros.emplace_back("ENABLE_GI", "1");
            // Traversal-debug views never consume higher-bounce GI and are
            // clamped to one bounce, so metadata needs no debug permutation.
            macros.emplace_back("ENABLE_TRAVERSAL_DEBUG", "0");
            macros.emplace_back("ENABLE_BOUNCE_METADATA", "1");
            createPipeline(
                m_MultiBounceFirstSampling[variant],
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

        for (uint32_t variant = 0;
            variant < m_IndirectBounceSampling.size();
            ++variant)
        {
            std::vector<ShaderMacro> indirectBounceMacros;
            indirectBounceMacros.emplace_back("ENABLE_AO", "0");
            indirectBounceMacros.emplace_back("ENABLE_GI", "1");
            indirectBounceMacros.emplace_back("ENABLE_TRAVERSAL_DEBUG", "0");
            indirectBounceMacros.emplace_back("ENABLE_BOUNCE_REINJECTION", "1");
            indirectBounceMacros.emplace_back(
                "INITIALIZE_BOUNCE_CUMULATIVE", variant == 0u ? "1" : "0");
            createPipeline(
                m_IndirectBounceSampling[variant],
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
                nvrhi::BindingLayoutItem::Texture_UAV(0),
                nvrhi::BindingLayoutItem::Texture_UAV(1)
                },
                &indirectBounceMacros);
        }

        createPipeline(m_DepthHierarchy, "uvsr/screen_space_depth_hierarchy_cs.hlsl", {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0).setSize(5)
        });

        for (uint32_t variant = 0; variant < m_Temporal.size(); ++variant)
        {
            if (variant == 0u)
                continue;

            std::vector<ShaderMacro> macros;
            macros.emplace_back("ENABLE_AO", (variant & 1u) != 0u ? "1" : "0");
            macros.emplace_back("ENABLE_GI", (variant & 2u) != 0u ? "1" : "0");
            createPipeline(m_Temporal[variant], "uvsr/screen_space_visibility_temporal_cs.hlsl", {
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
                nvrhi::BindingLayoutItem::Texture_UAV(3),
                nvrhi::BindingLayoutItem::Texture_UAV(4)
            }, &macros);

            createPipeline(m_Denoise[variant], "uvsr/screen_space_visibility_denoise_cs.hlsl", {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_SRV(3),
                nvrhi::BindingLayoutItem::Texture_SRV(4),
                nvrhi::BindingLayoutItem::Texture_UAV(0),
                nvrhi::BindingLayoutItem::Texture_UAV(1),
                nvrhi::BindingLayoutItem::Texture_UAV(2)
            }, &macros);
        }

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
            nvrhi::BindingLayoutItem::Texture_SRV(10),
            nvrhi::BindingLayoutItem::Texture_SRV(11),
            nvrhi::BindingLayoutItem::Texture_SRV(12),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        });
    }

    void ScreenSpaceVisibilityPass::EnsureResources(
        uint2 fullSize,
        bool temporalEnabled,
        bool multipleBouncesEnabled,
        bool depthHierarchyEnabled)
    {
        const uint2 samplingSize = fullSize;

        if (all(m_FullSize == fullSize) && all(m_SamplingSize == samplingSize) &&
            m_TemporalResourcesEnabled == temporalEnabled &&
            m_MultipleBounceResourcesEnabled == multipleBouncesEnabled &&
            m_DepthHierarchyResourcesEnabled == depthHierarchyEnabled &&
            m_RawAmbientVisibility)
        {
            return;
        }

        ReleaseResources();
        m_FullSize = fullSize;
        m_SamplingSize = samplingSize;
        m_TemporalResourcesEnabled = temporalEnabled;
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

        for (uint32_t index = 0; temporalEnabled && index < 2; ++index)
        {
            const char* suffix = index == 0 ? "0" : "1";
            std::string name = "ScreenSpaceVisibility/HistoryAmbientVisibility";
            name += suffix;
            m_HistoryAmbientVisibility[index] = createTexture(
                samplingSize, nvrhi::Format::R16_FLOAT, name.c_str());
            name = "ScreenSpaceVisibility/HistoryIndirectDiffuse";
            name += suffix;
            m_HistoryIndirectDiffuse[index] = createTexture(
                samplingSize, nvrhi::Format::RGBA16_FLOAT, name.c_str());
            name = "ScreenSpaceVisibility/HistoryDepth";
            name += suffix;
            m_HistoryDepth[index] = createTexture(
                samplingSize, nvrhi::Format::R32_FLOAT, name.c_str());
            name = "ScreenSpaceVisibility/HistoryNormal";
            name += suffix;
            m_HistoryNormal[index] = createTexture(
                samplingSize, nvrhi::Format::RGBA8_UNORM, name.c_str());
        }

        if (temporalEnabled)
        {
            m_HistoryValidity = createTexture(samplingSize, nvrhi::Format::R8_UNORM,
                "ScreenSpaceVisibility/HistoryValidity");
        }
        m_DenoisedAmbientVisibility = createTexture(samplingSize, nvrhi::Format::R16_FLOAT,
            "ScreenSpaceVisibility/DenoisedAmbientVisibility");
        m_DenoisedIndirectDiffuse = createTexture(samplingSize, nvrhi::Format::RGBA16_FLOAT,
            "ScreenSpaceVisibility/DenoisedIndirectDiffuse");
        m_DenoisedDebug = createTexture(samplingSize, nvrhi::Format::RGBA8_UNORM,
            "ScreenSpaceVisibility/DenoisedDebug");
        ResetHistory();
    }

    void ScreenSpaceVisibilityPass::ReleaseResources()
    {
        for (nvrhi::BindingSetHandle& bindingSet : m_SamplingBindingSets)
            bindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_MultiBounceFirstBindingSets)
            bindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_IndirectBounceBindingSets)
            bindingSet = nullptr;
        m_DepthHierarchyBindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_TemporalBindingSets)
            bindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_SpatialBindingSets)
            bindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_CompositeBindingSets)
            bindingSet = nullptr;

        m_RawAmbientVisibility = nullptr;
        m_DepthHierarchyTexture = nullptr;
        for (nvrhi::TextureHandle& rawIndirectDiffuse : m_RawIndirectDiffuse)
            rawIndirectDiffuse = nullptr;
        m_CumulativeIndirectDiffuse = nullptr;
        m_RawDebug = nullptr;
        for (uint32_t index = 0; index < 2; ++index)
        {
            m_HistoryAmbientVisibility[index] = nullptr;
            m_HistoryIndirectDiffuse[index] = nullptr;
            m_HistoryDepth[index] = nullptr;
            m_HistoryNormal[index] = nullptr;
        }
        m_HistoryValidity = nullptr;
        m_DenoisedAmbientVisibility = nullptr;
        m_DenoisedIndirectDiffuse = nullptr;
        m_DenoisedDebug = nullptr;
        m_OutputAmbientVisibility = nullptr;
        m_OutputIndirectDiffuse = nullptr;
        m_FullSize = uint2::zero();
        m_SamplingSize = uint2::zero();
        m_TemporalResourcesEnabled = false;
        m_MultipleBounceResourcesEnabled = false;
        m_DepthHierarchyResourcesEnabled = false;
        m_Timings = {};
        ResetHistory();
    }

    void ScreenSpaceVisibilityPass::ResetHistory()
    {
        m_HistoryReadIndex = 0;
        m_HistoryValid = false;
        m_HistorySignature = 0;
        m_HasPreviousCamera = false;
    }

    void ScreenSpaceVisibilityPass::Deactivate()
    {
        ReleaseResources();
    }

    void ScreenSpaceVisibilityPass::ResetBindingCache()
    {
        for (nvrhi::BindingSetHandle& bindingSet : m_SamplingBindingSets)
            bindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_MultiBounceFirstBindingSets)
            bindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_IndirectBounceBindingSets)
            bindingSet = nullptr;
        m_DepthHierarchyBindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_TemporalBindingSets)
            bindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_SpatialBindingSets)
            bindingSet = nullptr;
        for (nvrhi::BindingSetHandle& bindingSet : m_CompositeBindingSets)
            bindingSet = nullptr;
        ResetHistory();
    }

    uint64_t ScreenSpaceVisibilityPass::ComputeHistorySignature(
        const ScreenSpaceVisibilitySettings& settings,
        float exposureScale)
    {
        uint64_t hash = 1469598103934665603ull;
        hash = HashValue(hash, settings.sampling.sampleCount);
        hash = HashValue(hash, settings.sampling.radius);
        hash = HashValue(hash, settings.sampling.thickness);
        hash = HashValue(hash, settings.sampling.distanceScaledThickness);
        hash = HashValue(hash, settings.sampling.thicknessDistanceScale);
        hash = HashValue(hash, settings.sampling.useDepthHierarchy);
        hash = HashValue(hash, settings.sampling.stepDistributionExponent);
        hash = HashValue(hash, settings.sampling.radialJitter);
        hash = HashValue(hash, settings.ambientOcclusion.enabled);
        hash = HashValue(hash, settings.indirectDiffuse.enabled);
        hash = HashValue(hash, settings.indirectDiffuse.bounceCount);
        const uint32_t bounceTransportPolicy =
            GetBounceTransportPolicy(settings.debug.mode);
        if (settings.indirectDiffuse.enabled &&
            settings.indirectDiffuse.bounceCount > 1u)
        {
            // Final, exact-GI diagnostic, and unrelated one-bounce diagnostic
            // modes produce different histories even when the cutoff is zero.
            hash = HashValue(hash, bounceTransportPolicy);
        }
        const float effectiveContributionCutoff =
            bounceTransportPolicy == 0u
                ? std::max(
                    settings.indirectDiffuse.minimumBounceContribution, 0.f)
                : 0.f;
        const bool contributionGateAffectsTransport =
            settings.indirectDiffuse.enabled &&
            settings.indirectDiffuse.bounceCount > 1u &&
            effectiveContributionCutoff > 0.f;
        if (settings.indirectDiffuse.enabled &&
            settings.indirectDiffuse.bounceCount > 1u)
        {
            hash = HashValue(hash, effectiveContributionCutoff);
        }
        if (contributionGateAffectsTransport)
        {
            hash = HashValue(hash, settings.indirectDiffuse.intensity);
            hash = HashValue(hash, exposureScale);
        }
        if (settings.indirectDiffuse.enabled)
        {
            hash = HashValue(hash, settings.indirectDiffuse.includeEmissive);
            if (settings.indirectDiffuse.includeEmissive)
                hash = HashValue(hash, settings.indirectDiffuse.emissiveGain);
        }
        hash = HashValue(hash, settings.filtering.temporalEnabled);
        hash = HashValue(hash, settings.filtering.aoTemporalResponse);
        hash = HashValue(hash, settings.filtering.giTemporalResponse);
        hash = HashValue(hash, settings.filtering.depthRejection);
        hash = HashValue(hash, settings.filtering.normalRejection);
        hash = HashValue(hash, settings.debug.freezeSamplingPhase);
        hash = HashValue(hash, settings.debug.sectorHitCriterion);
        return hash;
    }

    bool ScreenSpaceVisibilityPass::UpdateHistoryValidity(
        const ScreenSpaceVisibilitySettings& settings,
        const IView& view,
        float exposureScale)
    {
        const uint64_t signature = ComputeHistorySignature(settings, exposureScale);
        bool valid = m_HistoryValid && signature == m_HistorySignature;

        const float3 cameraOrigin = view.GetViewOrigin();
        const float3 cameraDirection = view.GetViewDirection();
        const float4x4 projection = view.GetProjectionMatrix(false);

        if (!m_HasPreviousCamera)
        {
            valid = false;
        }
        else
        {
            const float cutDistance = std::max(settings.sampling.radius * 4.f, 1.f);
            if (SquaredDistance(cameraOrigin, m_PreviousCameraOrigin) > cutDistance * cutDistance)
                valid = false;
            if (SafeDirectionDot(cameraDirection, m_PreviousCameraDirection) < 0.75f)
                valid = false;

            float maximumProjectionDifference = 0.f;
            for (uint32_t row = 0; row < 4; ++row)
            {
                for (uint32_t column = 0; column < 4; ++column)
                {
                    maximumProjectionDifference = std::max(maximumProjectionDifference,
                        std::abs(projection[row][column] - m_PreviousProjection[row][column]));
                }
            }
            if (maximumProjectionDifference > 1e-4f)
                valid = false;
        }

        m_HistorySignature = signature;
        m_PreviousCameraOrigin = cameraOrigin;
        m_PreviousCameraDirection = cameraDirection;
        m_PreviousProjection = projection;
        m_HasPreviousCamera = true;
        return valid;
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

            switch (static_cast<Stage>(stageIndex))
            {
            case Stage::DepthHierarchy: m_Timings.depthHierarchyMs = milliseconds; break;
            case Stage::Sampling: m_Timings.samplingMs = milliseconds; break;
            case Stage::Temporal: m_Timings.temporalMs = milliseconds; break;
            case Stage::Spatial: m_Timings.spatialMs = milliseconds; break;
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
        assert(inputs.depth && inputs.normals && inputs.motionVectors);
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
        const bool finalCompositeNeedsHigherBounces =
            settings.debug.mode == ScreenSpaceVisibilityDebugMode::FinalComposite &&
            (knownInactiveLightingSources & LightingSource_IndirectDiffuse) == 0u &&
            settings.indirectDiffuse.intensity > 0.f;
        const bool higherBouncesCanReachConsumer =
            diagnosticNeedsExactBounceChain || finalCompositeNeedsHigherBounces;
        const uint32_t activeBounceCount = higherBouncesCanReachConsumer
            ? requestedBounceCount
            : 1u;
        const bool useDepthHierarchyThisFrame =
            settings.sampling.useDepthHierarchy &&
            !settings.indirectDiffuse.enabled &&
            !view->IsOrthographicProjection();
        EnsureResources(
            fullSize,
            settings.filtering.temporalEnabled,
            requestedBounceCount > 1u,
            useDepthHierarchyThisFrame);
        AdvanceTimers();

        exposureScale = std::max(exposureScale, 0.f);
        const bool historyValid = UpdateHistoryValidity(
            settings, *view, exposureScale);
        const uint32_t historyWriteIndex = 1u - m_HistoryReadIndex;
        const uint32_t consumerVariant =
            (settings.ambientOcclusion.enabled ? 1u : 0u) |
            (settings.indirectDiffuse.enabled ? 2u : 0u);
        const bool traversalDebugActive =
            settings.debug.mode >= ScreenSpaceVisibilityDebugMode::ReceiverNormal &&
            settings.debug.mode <= ScreenSpaceVisibilityDebugMode::ThicknessInterval;
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
        constants.aoTemporalResponse = std::clamp(settings.filtering.aoTemporalResponse, 0.f, 0.99f);
        constants.giTemporalResponse = std::clamp(settings.filtering.giTemporalResponse, 0.f, 0.99f);
        constants.depthRejection = std::max(settings.filtering.depthRejection, 1e-6f);
        constants.normalRejection = std::clamp(settings.filtering.normalRejection, -1.f, 1.f);
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
        constants.sliceCount = 1u;
        const uint32_t primarySampleCount = std::clamp(
            settings.sampling.sampleCount, 1u, 64u);
        constants.sampleCount = primarySampleCount;
        constants.knownInactiveLightingSources = knownInactiveLightingSources;
        constants.enableAmbientOcclusion = settings.ambientOcclusion.enabled ? 1u : 0u;
        constants.enableIndirectDiffuse = settings.indirectDiffuse.enabled ? 1u : 0u;
        constants.includeEmissive = settings.indirectDiffuse.includeEmissive ? 1u : 0u;
        constants.distanceScaledThickness = settings.sampling.distanceScaledThickness ? 1u : 0u;
        constants.temporalEnabled = settings.filtering.temporalEnabled ? 1u : 0u;
        constants.spatialEnabled = settings.filtering.spatialEnabled ? 1u : 0u;
        constants.spatialRadius = std::clamp(settings.filtering.spatialRadius, 0u, 4u);
        constants.historyValid = historyValid ? 1u : 0u;
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

        nvrhi::ITexture* currentAmbientVisibility = m_RawAmbientVisibility;
        nvrhi::ITexture* rawIndirectDiffuse = m_RawIndirectDiffuse[0];
        nvrhi::ITexture* currentIndirectDiffuse = rawIndirectDiffuse;
        nvrhi::ITexture* currentDebug = m_RawDebug;
        bool temporalExecuted = false;

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

        // Keep the default one-bounce path on the original compact shader.
        // Packed receiver metadata is emitted only when a later bounce will
        // consume it this frame; both variants retain the same four-SRV layout.
        {
            const bool writeBounceMetadata = activeBounceCount > 1u;
            const uint32_t multiBounceVariant =
                settings.ambientOcclusion.enabled ? 1u : 0u;
            Pipeline& pipeline = writeBounceMetadata
                ? m_MultiBounceFirstSampling[multiBounceVariant]
                : m_Sampling[samplingVariant];
            nvrhi::BindingSetHandle& bindingSet = writeBounceMetadata
                ? m_MultiBounceFirstBindingSets[multiBounceVariant]
                : m_SamplingBindingSets[samplingVariant];
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

            nvrhi::BindingSetHandle& bindingSet =
                m_IndirectBounceBindingSets[bounceIndex - 1u];
            const uint32_t pipelineVariant = bounceIndex > 1u ? 1u : 0u;
            Pipeline& pipeline = m_IndirectBounceSampling[pipelineVariant];
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
        EndStage(commandList, Stage::Sampling);
        commandList->endMarker();
        currentIndirectDiffuse = rawIndirectDiffuse;

        if (settings.filtering.temporalEnabled)
        {
            Pipeline& pipeline = m_Temporal[consumerVariant];
            const uint32_t temporalBindingIndex = consumerVariant * 4u +
                m_HistoryReadIndex * 2u +
                (activeBounceCount > 1u ? 1u : 0u);
            nvrhi::BindingSetHandle& bindingSet =
                m_TemporalBindingSets[temporalBindingIndex];
            if (!bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(0, m_RawAmbientVisibility),
                    nvrhi::BindingSetItem::Texture_SRV(1, rawIndirectDiffuse),
                    nvrhi::BindingSetItem::Texture_SRV(2, m_HistoryAmbientVisibility[m_HistoryReadIndex]),
                    nvrhi::BindingSetItem::Texture_SRV(3, m_HistoryIndirectDiffuse[m_HistoryReadIndex]),
                    nvrhi::BindingSetItem::Texture_SRV(4, inputs.depth),
                    nvrhi::BindingSetItem::Texture_SRV(5, inputs.normals),
                    nvrhi::BindingSetItem::Texture_SRV(6, m_HistoryDepth[m_HistoryReadIndex]),
                    nvrhi::BindingSetItem::Texture_SRV(7, m_HistoryNormal[m_HistoryReadIndex]),
                    nvrhi::BindingSetItem::Texture_SRV(8, inputs.motionVectors),
                    nvrhi::BindingSetItem::Texture_UAV(0, m_HistoryAmbientVisibility[historyWriteIndex]),
                    nvrhi::BindingSetItem::Texture_UAV(1, m_HistoryIndirectDiffuse[historyWriteIndex]),
                    nvrhi::BindingSetItem::Texture_UAV(2, m_HistoryDepth[historyWriteIndex]),
                    nvrhi::BindingSetItem::Texture_UAV(3, m_HistoryNormal[historyWriteIndex]),
                    nvrhi::BindingSetItem::Texture_UAV(4, m_HistoryValidity)
                };
                bindingSet = m_Device->createBindingSet(
                    bindings, pipeline.bindingLayout);
            }
            nvrhi::ComputeState state;
            state.pipeline = pipeline.pipeline;
            state.bindings = { bindingSet };
            commandList->beginMarker("Visibility Temporal Output Accumulation");
            BeginStage(commandList, Stage::Temporal);
            commandList->setComputeState(state);
            commandList->dispatch(samplingDispatchX, samplingDispatchY, 1);
            EndStage(commandList, Stage::Temporal);
            commandList->endMarker();
            currentAmbientVisibility = m_HistoryAmbientVisibility[historyWriteIndex];
            currentIndirectDiffuse = m_HistoryIndirectDiffuse[historyWriteIndex];
            temporalExecuted = true;
        }
        else
        {
            m_Timings.temporalMs = 0.f;
            m_HistoryValid = false;
        }

        // Radius zero is exactly the center-value copy performed by the
        // spatial shader, so retain the current resources and skip a complete
        // full-resolution dispatch and descriptor allocation.
        if (settings.filtering.spatialEnabled && constants.spatialRadius > 0u)
        {
            Pipeline& pipeline = m_Denoise[consumerVariant];
            const uint32_t spatialSourceVariant = temporalExecuted
                ? 2u + historyWriteIndex
                : (activeBounceCount > 1u ? 1u : 0u);
            const uint32_t spatialBindingIndex =
                consumerVariant * 4u + spatialSourceVariant;
            nvrhi::BindingSetHandle& bindingSet =
                m_SpatialBindingSets[spatialBindingIndex];
            if (!bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(0, currentAmbientVisibility),
                    nvrhi::BindingSetItem::Texture_SRV(1, currentIndirectDiffuse),
                    nvrhi::BindingSetItem::Texture_SRV(2, inputs.depth),
                    nvrhi::BindingSetItem::Texture_SRV(3, inputs.normals),
                    nvrhi::BindingSetItem::Texture_SRV(4, m_RawDebug),
                    nvrhi::BindingSetItem::Texture_UAV(0, m_DenoisedAmbientVisibility),
                    nvrhi::BindingSetItem::Texture_UAV(1, m_DenoisedIndirectDiffuse),
                    nvrhi::BindingSetItem::Texture_UAV(2, m_DenoisedDebug)
                };
                bindingSet = m_Device->createBindingSet(
                    bindings, pipeline.bindingLayout);
            }
            nvrhi::ComputeState state;
            state.pipeline = pipeline.pipeline;
            state.bindings = { bindingSet };
            commandList->beginMarker("Visibility Spatial Filter");
            BeginStage(commandList, Stage::Spatial);
            commandList->setComputeState(state);
            commandList->dispatch(samplingDispatchX, samplingDispatchY, 1);
            EndStage(commandList, Stage::Spatial);
            commandList->endMarker();
            currentAmbientVisibility = m_DenoisedAmbientVisibility;
            currentIndirectDiffuse = m_DenoisedIndirectDiffuse;
            currentDebug = m_DenoisedDebug;
        }
        else
        {
            m_Timings.spatialMs = 0.f;
        }

        m_OutputAmbientVisibility = currentAmbientVisibility;
        m_OutputIndirectDiffuse = currentIndirectDiffuse;

        {
            uint32_t filteredSourceVariant = 0u;
            if (settings.filtering.spatialEnabled && constants.spatialRadius > 0u)
                filteredSourceVariant = 3u;
            else if (temporalExecuted)
                filteredSourceVariant = 1u + historyWriteIndex;
            const uint32_t compositeBindingIndex = filteredSourceVariant * 2u +
                (activeBounceCount > 1u ? 1u : 0u);
            nvrhi::BindingSetHandle& bindingSet =
                m_CompositeBindingSets[compositeBindingIndex];
            if (!bindingSet)
            {
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(0, inputs.baseLighting),
                    nvrhi::BindingSetItem::Texture_SRV(1, currentAmbientVisibility),
                    nvrhi::BindingSetItem::Texture_SRV(2, currentIndirectDiffuse),
                    nvrhi::BindingSetItem::Texture_SRV(3, inputs.gbufferDiffuse),
                    nvrhi::BindingSetItem::Texture_SRV(4, inputs.gbufferEmissive),
                    nvrhi::BindingSetItem::Texture_SRV(5, inputs.materialAmbientOcclusion),
                    nvrhi::BindingSetItem::Texture_SRV(6, inputs.normals),
                    nvrhi::BindingSetItem::Texture_SRV(7, m_RawAmbientVisibility),
                    nvrhi::BindingSetItem::Texture_SRV(8, rawIndirectDiffuse),
                    nvrhi::BindingSetItem::Texture_SRV(9, currentDebug),
                    nvrhi::BindingSetItem::Texture_SRV(10, inputs.sourceRadiance),
                    nvrhi::BindingSetItem::Texture_SRV(11,
                        m_HistoryValidity ? m_HistoryValidity.Get() : m_RawAmbientVisibility.Get()),
                    nvrhi::BindingSetItem::Texture_SRV(12, inputs.gbufferSpecular),
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

        if (temporalExecuted)
        {
            m_HistoryReadIndex = historyWriteIndex;
            m_HistoryValid = true;
        }
        ++m_TimerFrame;
    }
}
