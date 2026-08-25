#include "ray_traced_sky_visibility.h"
#include "renderer_common_passes.h"
#include "renderer_log.h"
#include "renderer_receiver_texture_contract.h"
#include "renderer_resource_contract.h"
#include "renderer_shader_factory.h"

#include <donut/core/math/math.h>
#include <donut/engine/View.h>

#include <cmath>
#include <cstddef>
#include <array>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "ray_traced_sky_visibility_cb.h"

static_assert(sizeof(RayTracedSkyVisibilityConstants) % 16u == 0u,
    "Ray-traced sky-visibility constants must preserve HLSL register alignment.");
static_assert(offsetof(
        RayTracedSkyVisibilityConstants,
        sampleSequencePhase) == sizeof(PlanarViewConstants),
    "Ray-traced sky-visibility constants drifted after the view block.");
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
        constexpr std::array<uint32_t, 5> ReceiverSampleCounts = {
            1u, 2u, 4u, 8u, 16u
        };
        constexpr std::array<const char*, 5> ReceiverSampleMacros = {
            "1", "2", "4", "8", "16"
        };

        uint32_t FindVariant(uint32_t sampleCount)
        {
            for (uint32_t index = 0u;
                index < ReceiverSampleCounts.size();
                ++index)
            {
                if (ReceiverSampleCounts[index] == sampleCount)
                    return index;
            }
            return uint32_t(ReceiverSampleCounts.size());
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
            uint32_t receiverSampleCount,
            nvrhi::Format format,
            const char* debugName)
        {
            nvrhi::TextureDesc description;
            description.width = width;
            description.height = height;
            description.arraySize = receiverSampleCount;
            description.format = format;
            description.dimension = receiverSampleCount > 1u
                ? nvrhi::TextureDimension::Texture2DArray
                : nvrhi::TextureDimension::Texture2D;
            description.isUAV = true;
            description.debugName = debugName;
            description.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            return device->createTexture(description);
        }
    }

    bool RayTracedSkyVisibilityPass::IsDeviceSupported(
        nvrhi::IDevice* device)
    {
        return device &&
            device->queryFeatureSupport(
                nvrhi::Feature::RayTracingAccelStruct) &&
            device->queryFeatureSupport(nvrhi::Feature::RayQuery) &&
            HasFormatSupport(device, nvrhi::Format::R8_UNORM, true) &&
            HasFormatSupport(device, nvrhi::Format::R8_UNORM, false);
    }

    RayTracedSkyVisibilityPass::RayTracedSkyVisibilityPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<RendererShaderFactory>& shaderFactory,
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
                "Ray-traced sky visibility requires DXR 1.1 ray queries, "
                "R8_UNORM UAV and sampling support");
            return;
        }

        nvrhi::BufferDesc constantBufferDescription;
        constantBufferDescription.byteSize =
            sizeof(RayTracedSkyVisibilityConstants);
        constantBufferDescription.debugName =
            "RayTracedSkyVisibilityConstants";
        constantBufferDescription.isConstantBuffer = true;
        constantBufferDescription.isVolatile = true;
        constantBufferDescription.maxVersions =
            RendererMaxConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(
            constantBufferDescription);
        m_MaterialSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));

        for (uint32_t variant = 0u; variant < 2u; ++variant)
        {
            const bool outputHitDistance = variant != 0u;
            if (outputHitDistance && !m_HitDistanceSupported)
                continue;

            nvrhi::BindingLayoutDesc layoutDescription;
            layoutDescription.visibility = nvrhi::ShaderType::Compute;
            layoutDescription.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    SkyVisibilityConstantBufferSlot),
                nvrhi::BindingLayoutItem::RayTracingAccelStruct(
                    SkyVisibilityWorldTlasSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    SkyVisibilityDepthSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    SkyVisibilityMaterialSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    SkyVisibilityNormalsSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    SkyVisibilityNoiseSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    SkyVisibilityAttemptMaskSlot),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    RayMaterialGeometrySlot),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    RayMaterialConstantsSlot),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    RayMaterialGeometryIndexSlot),
                nvrhi::BindingLayoutItem::Sampler(
                    RayMaterialSamplerSlot),
                nvrhi::BindingLayoutItem::Texture_UAV(
                    SkyVisibilityOutputSlot),
                nvrhi::BindingLayoutItem::Texture_UAV(
                    SkyVisibilityClosestOutputSlot)
            };
            if (outputHitDistance)
            {
                layoutDescription.bindings.push_back(
                    nvrhi::BindingLayoutItem::Texture_UAV(
                        SkyVisibilityHitDistanceOutputSlot));
            }
            m_BindingLayouts[variant] =
                device->createBindingLayout(layoutDescription);

            for (uint32_t receiverVariant = 0u;
                receiverVariant < ReceiverSampleCounts.size();
                ++receiverVariant)
            {
                const std::vector<RendererShaderMacro> macros = {
                    { "OUTPUT_HIT_DISTANCE",
                        outputHitDistance ? "1" : "0" },
                    { "SKY_VISIBILITY_SAMPLES",
                        ReceiverSampleMacros[receiverVariant] }
                };
                m_Shaders[variant][receiverVariant] =
                    shaderFactory->CreateShader(
                        "uvsr/ray_traced_sky_visibility_cs.hlsl",
                        "Generate",
                        &macros,
                        nvrhi::ShaderType::Compute);
                if (m_Shaders[variant][receiverVariant] &&
                    m_BindingLayouts[variant])
                {
                    nvrhi::ComputePipelineDesc pipelineDescription;
                    pipelineDescription.CS =
                        m_Shaders[variant][receiverVariant];
                    pipelineDescription.bindingLayouts = {
                        m_BindingLayouts[variant],
                        m_BindlessLayout
                    };
                    m_Pipelines[variant][receiverVariant] =
                        device->createComputePipeline(
                            pipelineDescription);
                }
            }
        }

        if (m_HitDistanceSupported)
        {
            for (const auto& pipeline : m_Pipelines[1])
                m_HitDistanceSupported = m_HitDistanceSupported &&
                    bool(pipeline);
        }
        if (!m_HitDistanceSupported)
        {
            m_BindingLayouts[1] = nullptr;
            for (auto& shader : m_Shaders[1])
                shader = nullptr;
            for (auto& pipeline : m_Pipelines[1])
                pipeline = nullptr;
        }

        if (!m_BindingLayouts[0] || !m_ConstantBuffer ||
            !m_MaterialSampler)
        {
            m_Supported = false;
        }
        for (const auto& pipeline : m_Pipelines[0])
            m_Supported = m_Supported && bool(pipeline);
        if (!m_Supported)
        {
            log::error(
                "The ray traced sky visibility pipelines could not be created");
        }
    }

    bool RayTracedSkyVisibilityPass::EnsureResources(
        const RayTracedSkyVisibilityInputs& inputs,
        bool outputHitDistance)
    {
        if (!inputs.depth || !inputs.material || !inputs.normals)
            return false;
        const nvrhi::TextureDesc& depthDescription =
            inputs.depth->getDesc();
        const nvrhi::TextureDesc& materialDescription =
            inputs.material->getDesc();
        const nvrhi::TextureDesc& normalsDescription =
            inputs.normals->getDesc();
        const uint32_t receiverSampleCount = depthDescription.sampleCount;
        if (!AreRendererReceiverTextureDescriptorsCompatible(
                depthDescription,
                materialDescription,
                normalsDescription) ||
            FindVariant(receiverSampleCount) >=
                ReceiverSampleCounts.size())
        {
            return false;
        }
        const nvrhi::TextureDimension visibilityDimension =
            receiverSampleCount > 1u
                ? nvrhi::TextureDimension::Texture2DArray
                : nvrhi::TextureDimension::Texture2D;
        const bool visibilitySizeMatches = m_OutputVisibility &&
            m_OutputVisibility->getDesc().width == depthDescription.width &&
            m_OutputVisibility->getDesc().height == depthDescription.height &&
            m_OutputVisibility->getDesc().arraySize == receiverSampleCount &&
            m_OutputVisibility->getDesc().dimension == visibilityDimension;
        const bool closestVisibilitySizeMatches =
            m_OutputClosestVisibility &&
            m_OutputClosestVisibility->getDesc().width ==
                depthDescription.width &&
            m_OutputClosestVisibility->getDesc().height ==
                depthDescription.height;
        const bool hitDistanceSizeMatches = m_OutputHitDistance &&
            m_OutputHitDistance->getDesc().width == depthDescription.width &&
            m_OutputHitDistance->getDesc().height == depthDescription.height;
        const std::array<bool, 2> replaceMandatory = {
            !visibilitySizeMatches,
            !closestVisibilitySizeMatches
        };
        std::array<nvrhi::TextureHandle, 2> mandatory = {
            m_OutputVisibility,
            m_OutputClosestVisibility
        };
        if (!TryReplaceRendererResources(
                mandatory,
                replaceMandatory,
                [this, &depthDescription, receiverSampleCount](
                    std::size_t index)
                {
                    return CreateOutputTexture(
                        m_Device,
                        depthDescription.width,
                        depthDescription.height,
                        index == 0u ? receiverSampleCount : 1u,
                        nvrhi::Format::R8_UNORM,
                        index == 0u
                            ? "Ray Traced Sky Visibility/Per Raster Sample"
                            : "Ray Traced Sky Visibility/Closest Raster Sample");
                }))
        {
            return false;
        }
        bool resourcesChanged = replaceMandatory[0] || replaceMandatory[1];
        m_OutputVisibility = mandatory[0];
        m_OutputClosestVisibility = mandatory[1];

        if (outputHitDistance && !hitDistanceSizeMatches)
        {
            nvrhi::TextureHandle hitDistance = CreateOutputTexture(
                m_Device,
                depthDescription.width,
                depthDescription.height,
                1u,
                nvrhi::Format::R16_FLOAT,
                "Ray Traced Sky Visibility/Closest Hit Distance");
            if (hitDistance)
                m_OutputHitDistance = hitDistance;
            else
            {
                m_OutputHitDistance = nullptr;
                m_HitDistanceSupported = false;
            }
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

    bool RayTracedSkyVisibilityPass::EnsureBindingSet(
        uint32_t receiverVariant,
        const RayTracedSkyVisibilityInputs& inputs,
        const RayTracedMaterialVisibilityInputs& materialVisibility,
        nvrhi::rt::IAccelStruct* worldTlas,
        nvrhi::ITexture* noiseTexture,
        nvrhi::ITexture* attemptMask,
        bool outputHitDistance)
    {
        const uint32_t outputVariant = outputHitDistance ? 1u : 0u;
        if (receiverVariant >= ReceiverSampleCounts.size() ||
            !worldTlas || !materialVisibility || !noiseTexture ||
            !m_OutputVisibility || !m_OutputClosestVisibility ||
            (outputHitDistance && !m_OutputHitDistance))
            return false;
        const RayTracedSkyVisibilityBindingIdentity identity = {
            worldTlas,
            inputs.depth,
            inputs.material,
            inputs.normals,
            materialVisibility.geometryBuffer,
            materialVisibility.materialBuffer,
            materialVisibility.geometryIndexMap,
            materialVisibility.descriptorTable,
            noiseTexture,
            attemptMask
        };
        if (m_BoundIdentity != identity)
        {
            ClearBindingSets();
            m_BoundIdentity = identity;
        }
        if (m_BindingSets[outputVariant][receiverVariant])
            return true;

        nvrhi::BindingSetDesc description;
        description.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(
                SkyVisibilityConstantBufferSlot, m_ConstantBuffer),
            nvrhi::BindingSetItem::RayTracingAccelStruct(
                SkyVisibilityWorldTlasSlot, worldTlas),
            nvrhi::BindingSetItem::Texture_SRV(
                SkyVisibilityDepthSlot, inputs.depth),
            nvrhi::BindingSetItem::Texture_SRV(
                SkyVisibilityMaterialSlot, inputs.material),
            nvrhi::BindingSetItem::Texture_SRV(
                SkyVisibilityNormalsSlot, inputs.normals),
            nvrhi::BindingSetItem::Texture_SRV(
                SkyVisibilityNoiseSlot, noiseTexture),
            nvrhi::BindingSetItem::Texture_SRV(
                SkyVisibilityAttemptMaskSlot, attemptMask),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                RayMaterialGeometrySlot,
                materialVisibility.geometryBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                RayMaterialConstantsSlot,
                materialVisibility.materialBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                RayMaterialGeometryIndexSlot,
                materialVisibility.geometryIndexMap),
            nvrhi::BindingSetItem::Sampler(
                RayMaterialSamplerSlot, m_MaterialSampler),
            nvrhi::BindingSetItem::Texture_UAV(
                SkyVisibilityOutputSlot, m_OutputVisibility),
            nvrhi::BindingSetItem::Texture_UAV(
                SkyVisibilityClosestOutputSlot,
                m_OutputClosestVisibility)
        };
        if (outputHitDistance)
        {
            description.bindings.push_back(
                nvrhi::BindingSetItem::Texture_UAV(
                    SkyVisibilityHitDistanceOutputSlot,
                    m_OutputHitDistance));
        }
        m_BindingSets[outputVariant][receiverVariant] =
            m_Device->createBindingSet(
                description,
                m_BindingLayouts[outputVariant]);
        if (!m_BindingSets[outputVariant][receiverVariant])
        {
            ClearBindingSets();
            return false;
        }
        return true;
    }

    RayTracedSkyVisibilityResult RayTracedSkyVisibilityPass::Render(
        nvrhi::ICommandList* commandList,
        const RayTracedSkyVisibilitySettings& settings,
        const IView& view,
        const RayTracedSkyVisibilityInputs& inputs,
        const RayTracedMaterialVisibilityInputs& materialVisibility,
        nvrhi::rt::IAccelStruct* worldTlas,
        const NoiseSettings& noiseSettings,
        nvrhi::ITexture* noiseTexture,
        uint32_t samplingPhase,
        float sceneDiagonal,
        const LightingSampleSchedule& sampleSchedule)
    {
        if (!m_Supported || !commandList || !materialVisibility ||
            !worldTlas || !noiseTexture || !sampleSchedule ||
            !IsValidNoiseSettings(noiseSettings) ||
            !IsRayTracedSkyVisibilityConfigurationSupported(settings))
        {
            if (!m_ReportedInvalidInput && m_Supported &&
                commandList && worldTlas)
            {
                log::error(
                    "Ray traced sky visibility received incomplete or unsupported inputs");
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
                    "Ray traced sky visibility received an invalid scene extent");
                m_ReportedInvalidInput = true;
            }
            return {};
        }

        const bool requestedHitDistance = settings.outputHitDistance &&
            m_HitDistanceSupported;
        if (settings.outputHitDistance && !m_HitDistanceSupported &&
            !m_ReportedHitDistanceUnavailable)
        {
            log::warning(
                "Ray traced sky visibility hit distance output is unavailable");
            m_ReportedHitDistanceUnavailable = true;
        }
        if (!EnsureResources(inputs, requestedHitDistance))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Ray traced sky visibility textures were missing, "
                    "mismatched, or could not be allocated");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        const bool outputHitDistance = requestedHitDistance &&
            m_OutputHitDistance;
        if (settings.outputHitDistance && !outputHitDistance &&
            !m_ReportedHitDistanceUnavailable)
        {
            log::warning(
                "Ray traced sky visibility hit distance allocation failed");
            m_ReportedHitDistanceUnavailable = true;
        }
        const uint32_t receiverSampleCount =
            inputs.depth->getDesc().sampleCount;
        const uint32_t receiverVariant =
            FindVariant(receiverSampleCount);
        if (!EnsureBindingSet(
                receiverVariant,
                inputs,
                materialVisibility,
                worldTlas,
                noiseTexture,
                sampleSchedule.attemptMask,
                outputHitDistance))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Ray traced sky visibility binding set creation failed");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        m_ReportedInvalidInput = false;

        RayTracedSkyVisibilityConstants constants = {};
        view.FillPlanarViewConstants(constants.view);
        constants.sampleSequencePhase = samplingPhase;
        constants.sampleCount = ResolveRayTracedSkyVisibilitySampleCount(
            settings.sampleRateLog2);
        constants.noisePattern =
            static_cast<uint32_t>(noiseSettings.pattern);
        constants.rayDistance = rayDistance;
        constants.depthQuantizationStep = GetDepthQuantizationStep(
            inputs.depth->getDesc().format);
        constants.rayBias = settings.rayBias;
        constants.reverseDepth = view.IsReverseDepth() ? 1u : 0u;
        constants.floatDepth = IsFloatingPointDepth(
            inputs.depth->getDesc().format) ? 1u : 0u;
        constants.sampleSequenceMode = static_cast<uint32_t>(
            ResolveLightingSampleSequenceMode(
                sampleSchedule,
                true,
                noiseSettings.animate));
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));

        const uint32_t outputVariant = outputHitDistance ? 1u : 0u;
        commandList->beginMarker("Ray Traced Sky Visibility");
        nvrhi::ComputeState state;
        state.pipeline = m_Pipelines[outputVariant][receiverVariant];
        state.bindings = {
            m_BindingSets[outputVariant][receiverVariant],
            materialVisibility.descriptorTable
        };
        commandList->setComputeState(state);
        const nvrhi::Rect viewExtent = view.GetViewExtent();
        commandList->dispatch(
            div_ceil(viewExtent.width(), 8),
            div_ceil(viewExtent.height(), 8));
        commandList->endMarker();
        return {
            m_OutputVisibility,
            m_OutputClosestVisibility,
            outputHitDistance ? m_OutputHitDistance.Get() : nullptr,
            receiverSampleCount,
            true
        };
    }

    void RayTracedSkyVisibilityPass::ResetBindingCache()
    {
        ClearBindingSets();
        m_BoundIdentity = {};
    }

    void RayTracedSkyVisibilityPass::ClearBindingSets()
    {
        for (auto& outputVariant : m_BindingSets)
        {
            for (nvrhi::BindingSetHandle& bindingSet : outputVariant)
                bindingSet = nullptr;
        }
    }
}
