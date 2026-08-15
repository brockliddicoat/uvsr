#include "heitz_ratio_estimator_shadows.h"

#include "ratio_estimator_shared.h"

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "heitz_ratio_estimator_shadows_cb.h"

static_assert(sizeof(HeitzRatioEstimatorShadowConstants) % 16u == 0u,
    "Heitz shadow constants must preserve HLSL register alignment.");
static_assert(offsetof(
        HeitzRatioEstimatorShadowConstants,
        directionToLightAndAngularRadius) == sizeof(PlanarViewConstants),
    "Heitz shadow constants drifted after the view block.");
static_assert(static_cast<uint32_t>(
    uvsr::NoisePattern::SpatialWhite) == 0u);
static_assert(static_cast<uint32_t>(
    uvsr::NoisePattern::SpatialBlue) == 1u);
static_assert(static_cast<uint32_t>(
    uvsr::NoisePattern::SpatiotemporalBlue) == 2u);

namespace uvsr
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;

        struct HeitzPipelineVariant
        {
            uint32_t receiverSampleCount;
            const char* receiverSampleCountMacro;
            bool outputSourceModulation;
            bool outputHitDistance;
        };

        constexpr std::array<HeitzPipelineVariant, 7>
            HeitzPipelineVariants = {{
                { 1u, "1", false, false },
                { 1u, "1", false, true },
                { 1u, "1", true, false },
                { 2u, "2", true, false },
                { 4u, "4", true, false },
                { 8u, "8", true, false },
                { 16u, "16", true, false }
            }};

        uint32_t FindHeitzPipelineVariant(
            uint32_t receiverSampleCount,
            bool outputSourceModulation,
            bool outputHitDistance)
        {
            for (uint32_t index = 0u;
                index < HeitzPipelineVariants.size();
                ++index)
            {
                const HeitzPipelineVariant& variant =
                    HeitzPipelineVariants[index];
                if (variant.receiverSampleCount == receiverSampleCount &&
                    variant.outputSourceModulation ==
                        outputSourceModulation &&
                    variant.outputHitDistance == outputHitDistance)
                {
                    return index;
                }
            }
            return uint32_t(HeitzPipelineVariants.size());
        }

        bool IsSupportedReceiverSampleCount(uint32_t sampleCount)
        {
            return FindHeitzPipelineVariant(
                sampleCount,
                sampleCount > 1u,
                false) <
                HeitzPipelineVariants.size();
        }

        uint32_t GetHeitzBindingLayoutIndex(
            const HeitzPipelineVariant& variant)
        {
            if (variant.outputHitDistance)
                return 1u;
            return variant.outputSourceModulation ? 2u : 0u;
        }

        bool HasFormatSupport(
            nvrhi::IDevice* device,
            nvrhi::Format format,
            bool requiresUav)
        {
            nvrhi::FormatSupport required =
                nvrhi::FormatSupport::Texture |
                nvrhi::FormatSupport::ShaderLoad |
                nvrhi::FormatSupport::ShaderSample;
            if (requiresUav)
                required = required | nvrhi::FormatSupport::ShaderUavStore;
            return (device->queryFormatSupport(format) & required) == required;
        }

        bool SameInputs(
            const HeitzRatioEstimatorShadowInputs& left,
            const HeitzRatioEstimatorShadowInputs& right)
        {
            return left.depth == right.depth &&
                left.diffuse == right.diffuse &&
                left.material == right.material &&
                left.normals == right.normals &&
                left.emissive == right.emissive &&
                left.materialAmbientOcclusion ==
                    right.materialAmbientOcclusion;
        }

        float GetDepthQuantizationStep(nvrhi::Format format)
        {
            switch (format)
            {
            case nvrhi::Format::D16:
                return 1.f / 65535.f;
            case nvrhi::Format::D24S8:
                return 1.f / 16777215.f;
            default:
                // D32 and D32S8 are floating-point depth. The shader advances
                // them by one representable float when this value is zero.
                return 0.f;
            }
        }

        bool IsFloatingPointDepth(nvrhi::Format format)
        {
            return format == nvrhi::Format::D32 ||
                format == nvrhi::Format::D32S8 ||
                format == nvrhi::Format::R32_FLOAT;
        }

        nvrhi::TextureHandle CreateOutputTexture(
            nvrhi::IDevice* device,
            uint32_t width,
            uint32_t height,
            nvrhi::Format format,
            const char* debugName)
        {
            nvrhi::TextureDesc description;
            description.width = width;
            description.height = height;
            description.format = format;
            description.dimension = nvrhi::TextureDimension::Texture2D;
            description.isUAV = true;
            description.debugName = debugName;
            description.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            return device->createTexture(description);
        }

    }

    bool HeitzRatioEstimatorShadowPass::IsDeviceSupported(
        nvrhi::IDevice* device)
    {
        return device &&
            device->queryFeatureSupport(
                nvrhi::Feature::RayTracingAccelStruct) &&
            device->queryFeatureSupport(nvrhi::Feature::RayQuery) &&
            HasFormatSupport(device, nvrhi::Format::RGBA16_FLOAT, true) &&
            HasFormatSupport(device, nvrhi::Format::R8_UNORM, false);
    }

    HeitzRatioEstimatorShadowPass::HeitzRatioEstimatorShadowPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        nvrhi::IBindingLayout* bindlessLayout)
        : m_Device(device)
        , m_BindlessLayout(bindlessLayout)
    {
        m_Supported = shaderFactory && m_BindlessLayout &&
            IsDeviceSupported(device);
        m_HitDistanceSupported = m_Supported &&
            HasFormatSupport(device, nvrhi::Format::R16_FLOAT, true);
        if (!m_Supported)
        {
            log::warning(
                "Correlated ratio sun shadows require DXR 1.1 ray queries, RGBA16F UAV support, and R8_UNORM sampling");
            return;
        }

        nvrhi::BufferDesc constantBufferDescription;
        constantBufferDescription.byteSize =
            sizeof(HeitzRatioEstimatorShadowConstants);
        constantBufferDescription.debugName =
            "HeitzRatioEstimatorShadowConstants";
        constantBufferDescription.isConstantBuffer = true;
        constantBufferDescription.isVolatile = true;
        constantBufferDescription.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(
            constantBufferDescription);
        m_MaterialSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));

        for (uint32_t layoutIndex = 0u;
            layoutIndex < m_BindingLayouts.size();
            ++layoutIndex)
        {
            const bool outputHitDistance = layoutIndex == 1u;
            if (outputHitDistance && !m_HitDistanceSupported)
                continue;

            nvrhi::BindingLayoutDesc layoutDescription;
            layoutDescription.visibility = nvrhi::ShaderType::Compute;
            layoutDescription.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_SRV(3),
                nvrhi::BindingLayoutItem::Texture_SRV(4),
                nvrhi::BindingLayoutItem::Texture_SRV(5),
                nvrhi::BindingLayoutItem::Texture_SRV(6),
                nvrhi::BindingLayoutItem::Texture_SRV(7),
                nvrhi::BindingLayoutItem::Texture_SRV(8),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),
                nvrhi::BindingLayoutItem::Sampler(0),
                nvrhi::BindingLayoutItem::Texture_UAV(0)
            };
            if (layoutIndex != 0u)
            {
                layoutDescription.bindings.push_back(
                    nvrhi::BindingLayoutItem::Texture_UAV(1));
            }
            m_BindingLayouts[layoutIndex] =
                device->createBindingLayout(layoutDescription);
        }

        for (uint32_t variantIndex = 0u;
            variantIndex < HeitzPipelineVariants.size();
            ++variantIndex)
        {
            const HeitzPipelineVariant& variant =
                HeitzPipelineVariants[variantIndex];
            if (variant.outputHitDistance && !m_HitDistanceSupported)
                continue;

            std::vector<ShaderMacro> macros;
            macros.push_back({
                "OUTPUT_HIT_DISTANCE",
                variant.outputHitDistance ? "1" : "0" });
            macros.push_back({
                "OUTPUT_SOURCE_MODULATION",
                variant.outputSourceModulation ? "1" : "0" });
            macros.push_back({
                "HEITZ_RASTER_SAMPLES",
                variant.receiverSampleCountMacro });
            m_Shaders[variantIndex] = shaderFactory->CreateShader(
                "uvsr/heitz_ratio_estimator_shadows_cs.hlsl",
                "Generate",
                &macros,
                nvrhi::ShaderType::Compute);
            const uint32_t layoutIndex =
                GetHeitzBindingLayoutIndex(variant);
            if (m_Shaders[variantIndex] &&
                m_BindingLayouts[layoutIndex])
            {
                nvrhi::ComputePipelineDesc pipelineDescription;
                pipelineDescription.CS = m_Shaders[variantIndex];
                pipelineDescription.bindingLayouts = {
                    m_BindingLayouts[layoutIndex],
                    m_BindlessLayout
                };
                m_Pipelines[variantIndex] =
                    device->createComputePipeline(pipelineDescription);
            }
        }

        const uint32_t hitDistanceVariant =
            FindHeitzPipelineVariant(1u, false, true);
        if (m_HitDistanceSupported &&
            (!m_BindingLayouts[1] ||
                !m_Shaders[hitDistanceVariant] ||
                !m_Pipelines[hitDistanceVariant]))
        {
            m_HitDistanceSupported = false;
            m_BindingLayouts[1] = nullptr;
            m_Shaders[hitDistanceVariant] = nullptr;
            m_Pipelines[hitDistanceVariant] = nullptr;
        }

        bool completeNonHitPipelines = true;
        for (uint32_t variantIndex = 0u;
            variantIndex < HeitzPipelineVariants.size();
            ++variantIndex)
        {
            if (!HeitzPipelineVariants[variantIndex].outputHitDistance)
            {
                completeNonHitPipelines = completeNonHitPipelines &&
                    m_Shaders[variantIndex] &&
                    m_Pipelines[variantIndex];
            }
        }
        if (!m_BindingLayouts[0] || !m_BindingLayouts[2] ||
            !m_ConstantBuffer ||
            !m_MaterialSampler || !completeNonHitPipelines)
        {
            m_Supported = false;
            log::error(
                "The correlated ratio sun shadow pipeline could not be created");
        }
    }

    bool HeitzRatioEstimatorShadowPass::EnsureResources(
        const HeitzRatioEstimatorShadowInputs& inputs,
        bool outputSourceModulation,
        bool outputHitDistance)
    {
        const nvrhi::ITexture* textures[] = {
            inputs.depth,
            inputs.diffuse,
            inputs.material,
            inputs.normals,
            inputs.emissive,
            inputs.materialAmbientOcclusion
        };
        if (!inputs.depth)
            return false;
        const nvrhi::TextureDesc& depthDescription =
            inputs.depth->getDesc();
        const uint32_t receiverSampleCount =
            depthDescription.sampleCount;
        if (outputSourceModulation && outputHitDistance)
            return false;
        if (!IsSupportedReceiverSampleCount(receiverSampleCount) ||
            (outputHitDistance && receiverSampleCount != 1u))
        {
            return false;
        }
        const nvrhi::TextureDimension expectedDimension =
            receiverSampleCount == 1u
                ? nvrhi::TextureDimension::Texture2D
                : nvrhi::TextureDimension::Texture2DMS;
        for (const nvrhi::ITexture* texture : textures)
        {
            if (!texture ||
                texture->getDesc().sampleCount != receiverSampleCount ||
                texture->getDesc().dimension != expectedDimension ||
                texture->getDesc().width != depthDescription.width ||
                texture->getDesc().height != depthDescription.height)
            {
                return false;
            }
        }
        const bool modulationSizeMatches = m_OutputModulation &&
            m_OutputModulation->getDesc().width == depthDescription.width &&
            m_OutputModulation->getDesc().height == depthDescription.height;
        const bool closestSourceRequired = outputSourceModulation;
        const bool closestSourceSizeMatches =
            m_OutputClosestSourceModulation &&
            m_OutputClosestSourceModulation->getDesc().width ==
                depthDescription.width &&
            m_OutputClosestSourceModulation->getDesc().height ==
                depthDescription.height;
        const bool hitDistanceSizeMatches = m_OutputHitDistance &&
            m_OutputHitDistance->getDesc().width == depthDescription.width &&
            m_OutputHitDistance->getDesc().height == depthDescription.height;
        bool resourcesChanged = false;
        if (!modulationSizeMatches)
        {
            nvrhi::TextureHandle outputModulation = CreateOutputTexture(
                m_Device,
                depthDescription.width,
                depthDescription.height,
                nvrhi::Format::RGBA16_FLOAT,
                "Ray Traced Sun Shadows/Modulation");
            if (!outputModulation)
                return false;
            m_OutputModulation = outputModulation;
            resourcesChanged = true;
        }

        if (closestSourceRequired && !closestSourceSizeMatches)
        {
            nvrhi::TextureHandle outputClosestSourceModulation =
                CreateOutputTexture(
                    m_Device,
                    depthDescription.width,
                    depthDescription.height,
                    nvrhi::Format::RGBA16_FLOAT,
                    "Ray Traced Sun Shadows/Closest Source Modulation");
            if (!outputClosestSourceModulation)
                return false;
            m_OutputClosestSourceModulation =
                outputClosestSourceModulation;
            resourcesChanged = true;
        }
        else if (!closestSourceRequired &&
            m_OutputClosestSourceModulation)
        {
            m_OutputClosestSourceModulation = nullptr;
            resourcesChanged = true;
        }

        if (outputHitDistance && !hitDistanceSizeMatches)
        {
            m_OutputHitDistance = CreateOutputTexture(
                m_Device,
                depthDescription.width,
                depthDescription.height,
                nvrhi::Format::R16_FLOAT,
                "Ray Traced Sun Shadows/Hit Distance");
            if (!m_OutputHitDistance)
                m_HitDistanceSupported = false;
            resourcesChanged = true;
        }
        else if (!outputHitDistance && m_OutputHitDistance)
        {
            m_OutputHitDistance = nullptr;
            resourcesChanged = true;
        }

        if (resourcesChanged)
            ClearBindingSets();
        return true;
    }

    bool HeitzRatioEstimatorShadowPass::EnsureBindingSets(
        const HeitzRatioEstimatorShadowInputs& inputs,
        const RayTracedMaterialVisibilityInputs& materialVisibility,
        nvrhi::rt::IAccelStruct* worldTlas,
        nvrhi::ITexture* noiseTexture,
        nvrhi::ITexture* attemptMask,
        bool outputSourceModulation,
        bool outputHitDistance)
    {
        const uint32_t receiverSampleCount = inputs.depth
            ? inputs.depth->getDesc().sampleCount
            : 0u;
        const uint32_t variant = FindHeitzPipelineVariant(
            receiverSampleCount,
            outputSourceModulation,
            outputHitDistance);
        if (variant >= HeitzPipelineVariants.size())
            return false;
        const HeitzPipelineVariant& pipelineVariant =
            HeitzPipelineVariants[variant];
        const uint32_t layoutIndex =
            GetHeitzBindingLayoutIndex(pipelineVariant);
        if (!worldTlas || !materialVisibility || !noiseTexture ||
            !m_OutputModulation ||
            (outputSourceModulation &&
                !m_OutputClosestSourceModulation) ||
            (outputHitDistance && !m_OutputHitDistance))
            return false;
        if (m_BoundTlas != worldTlas ||
            !SameInputs(m_BoundInputs, inputs) ||
            m_BoundMaterialVisibility != materialVisibility ||
            m_BoundNoiseTexture != noiseTexture ||
            m_BoundAttemptMask != attemptMask)
        {
            ClearBindingSets();
            m_BoundTlas = worldTlas;
            m_BoundInputs = inputs;
            m_BoundMaterialVisibility = materialVisibility;
            m_BoundNoiseTexture = noiseTexture;
            m_BoundAttemptMask = attemptMask;
        }
        if (m_BindingSets[variant])
            return true;

        nvrhi::BindingSetDesc description;
        description.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, worldTlas),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.depth),
            nvrhi::BindingSetItem::Texture_SRV(2, inputs.diffuse),
            nvrhi::BindingSetItem::Texture_SRV(3, inputs.material),
            nvrhi::BindingSetItem::Texture_SRV(4, inputs.normals),
            nvrhi::BindingSetItem::Texture_SRV(
                5, inputs.materialAmbientOcclusion),
            nvrhi::BindingSetItem::Texture_SRV(6, noiseTexture),
            nvrhi::BindingSetItem::Texture_SRV(7, inputs.emissive),
            nvrhi::BindingSetItem::Texture_SRV(8, attemptMask),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                10, materialVisibility.geometryBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                11, materialVisibility.materialBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                12, materialVisibility.geometryIndexMap),
            nvrhi::BindingSetItem::Sampler(0, m_MaterialSampler),
            nvrhi::BindingSetItem::Texture_UAV(0, m_OutputModulation)
        };
        if (outputHitDistance)
        {
            description.bindings.push_back(
                nvrhi::BindingSetItem::Texture_UAV(
                    1, m_OutputHitDistance));
        }
        else if (outputSourceModulation)
        {
            description.bindings.push_back(
                nvrhi::BindingSetItem::Texture_UAV(
                    1, m_OutputClosestSourceModulation));
        }
        m_BindingSets[variant] = m_Device->createBindingSet(
            description, m_BindingLayouts[layoutIndex]);
        if (!m_BindingSets[variant])
        {
            ClearBindingSets();
            return false;
        }
        return true;
    }

    HeitzRatioEstimatorShadowResult
        HeitzRatioEstimatorShadowPass::Render(
            nvrhi::ICommandList* commandList,
            const HeitzRatioEstimatorShadowSettings& settings,
            bool traceAllMsaaReceivers,
            const IView& view,
            const HeitzRatioEstimatorShadowInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas,
            const DirectionalLight* light,
            const NoiseSettings& noiseSettings,
            nvrhi::ITexture* noiseTexture,
            uint32_t samplingPhase,
            float sceneDiagonal,
            const LightingSampleSchedule& sampleSchedule)
    {
        if (!m_Supported || !commandList || !materialVisibility ||
            !worldTlas || !light || !noiseTexture || !sampleSchedule ||
            !IsValidNoiseSettings(noiseSettings) ||
            !IsHeitzRatioEstimatorConfigurationSupported(settings))
        {
            if (!m_ReportedInvalidInput && m_Supported &&
                commandList && worldTlas && light)
            {
                log::error(
                    "Correlated ratio sun shadows received incomplete or unsupported inputs");
                m_ReportedInvalidInput = true;
            }
            return {};
        }

        const float rayDistance = ResolveRayVisibilityMaxDistance(
            settings.maxDistance,
            sceneDiagonal);
        if (std::isnan(rayDistance))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Correlated ratio sun shadows received an invalid scene extent");
                m_ReportedInvalidInput = true;
            }
            return {};
        }

        const float3 propagationDirection =
            float3(light->GetDirection());
        const float directionLengthSquared =
            dot(propagationDirection, propagationDirection);
        if (!(directionLengthSquared > 1e-12f) ||
            !std::isfinite(directionLengthSquared))
        {
            return {};
        }
        const float3 directionToLight =
            -propagationDirection /
            std::sqrt(directionLengthSquared);
        const float angularRadius =
            std::clamp(light->angularSize, 0.f, 90.f) *
            (Pi / 360.f);
        const bool stochastic = !settings.hardShadows &&
            angularRadius > 1e-6f;
        const bool useRatioEstimator = stochastic &&
            settings.useRatioEstimator;
        const uint32_t sampleCount = ResolveHeitzShadowTraceCount(
            settings, stochastic);
        const uint32_t receiverSampleCount = inputs.depth
            ? inputs.depth->getDesc().sampleCount
            : 0u;
        const bool requestedSourceModulation = useRatioEstimator ||
            receiverSampleCount > 1u;
        const bool requestedHitDistance = settings.outputHitDistance &&
            m_HitDistanceSupported && receiverSampleCount == 1u &&
            !requestedSourceModulation;
        if (settings.outputHitDistance && !m_HitDistanceSupported &&
            !m_ReportedHitDistanceUnavailable)
        {
            log::warning(
                "Ray traced sun shadow hit distance output is unavailable");
            m_ReportedHitDistanceUnavailable = true;
        }
        if (settings.outputHitDistance && receiverSampleCount > 1u &&
            !m_ReportedHitDistanceUnavailable)
        {
            log::warning(
                "Ray traced sun shadow hit distance is unavailable for resolved MSAA receivers");
            m_ReportedHitDistanceUnavailable = true;
        }
        if (settings.outputHitDistance && useRatioEstimator &&
            receiverSampleCount == 1u &&
            !m_ReportedHitDistanceUnavailable)
        {
            log::warning(
                "Ray traced sun shadow hit distance is unavailable for a ratio-estimated signal");
            m_ReportedHitDistanceUnavailable = true;
        }
        if (!EnsureResources(
                inputs,
                requestedSourceModulation,
                requestedHitDistance))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Correlated ratio sun shadow textures were missing, mismatched, or could not be allocated");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        const bool outputSourceModulation =
            requestedSourceModulation &&
            m_OutputClosestSourceModulation;
        const bool outputHitDistance = requestedHitDistance &&
            m_OutputHitDistance;
        if (settings.outputHitDistance && !outputHitDistance &&
            !m_ReportedHitDistanceUnavailable)
        {
            log::warning(
                "Ray traced sun shadow hit distance allocation failed");
            m_ReportedHitDistanceUnavailable = true;
        }
        if (!EnsureBindingSets(
                inputs,
                materialVisibility,
                worldTlas,
                noiseTexture,
                sampleSchedule.attemptMask,
                outputSourceModulation,
                outputHitDistance))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Correlated ratio sun shadow binding set creation failed");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        m_ReportedInvalidInput = false;

        HeitzRatioEstimatorShadowConstants constants = {};
        view.FillPlanarViewConstants(constants.view);
        constants.directionToLightAndAngularRadius = float4(
            directionToLight, angularRadius);
        constants.sampleSequencePhase = samplingPhase;
        constants.sampleCount = sampleCount;
        constants.hardShadows = stochastic ? 0u : 1u;
        constants.noisePattern =
            static_cast<uint32_t>(noiseSettings.pattern);
        constants.rayDistance = rayDistance;
        constants.denominatorEpsilon =
            RatioEstimatorDefaultDenominatorEpsilon;
        constants.depthQuantizationStep = GetDepthQuantizationStep(
            inputs.depth->getDesc().format);
        constants.rayBias = settings.rayBias;
        constants.reverseDepth = view.IsReverseDepth() ? 1u : 0u;
        constants.floatDepth = IsFloatingPointDepth(
            inputs.depth->getDesc().format) ? 1u : 0u;
        constants.useRatioEstimator = useRatioEstimator ? 1u : 0u;
        constants.sampleSequenceMode = static_cast<uint32_t>(
            ResolveLightingSampleSequenceMode(
                sampleSchedule,
                stochastic,
                noiseSettings.animate));
        constants.traceAllMsaaReceivers =
            receiverSampleCount <= 1u || traceAllMsaaReceivers ? 1u : 0u;
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));

        const uint32_t variant = FindHeitzPipelineVariant(
            receiverSampleCount,
            outputSourceModulation,
            outputHitDistance);
        if (variant >= HeitzPipelineVariants.size())
            return {};
        commandList->beginMarker("Ray Traced Sun Shadows");
        nvrhi::ComputeState state;
        state.pipeline = m_Pipelines[variant];
        state.bindings = {
            m_BindingSets[variant],
            materialVisibility.descriptorTable
        };
        commandList->setComputeState(state);
        const nvrhi::Rect viewExtent = view.GetViewExtent();
        commandList->dispatch(
            div_ceil(viewExtent.width(), 8),
            div_ceil(viewExtent.height(), 8));
        commandList->endMarker();
        return {
            m_OutputModulation,
            outputSourceModulation
                ? m_OutputClosestSourceModulation.Get()
                : nullptr,
            outputHitDistance ? m_OutputHitDistance.Get() : nullptr,
            light,
            true,
            stochastic,
            useRatioEstimator,
            outputHitDistance && !useRatioEstimator,
            receiverSampleCount
        };
    }

    void HeitzRatioEstimatorShadowPass::ResetBindingCache()
    {
        ClearBindingSets();
        m_BoundTlas = nullptr;
        m_BoundInputs = {};
        m_BoundMaterialVisibility = {};
        m_BoundNoiseTexture = nullptr;
        m_BoundAttemptMask = nullptr;
    }

    void HeitzRatioEstimatorShadowPass::ClearBindingSets()
    {
        for (nvrhi::BindingSetHandle& bindingSet : m_BindingSets)
            bindingSet = nullptr;
    }
}
