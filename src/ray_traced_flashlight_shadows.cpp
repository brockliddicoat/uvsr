#include "ray_traced_flashlight_shadows.h"
#include "renderer_common_passes.h"
#include "renderer_log.h"
#include "renderer_shader_factory.h"

#include "ray_traced_flashlight_shadows_shared.h"

#include <donut/core/math/math.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/View.h>

#include <cmath>
#include <cstddef>
#include <array>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "ray_traced_flashlight_shadows_cb.h"

static_assert(sizeof(RayTracedFlashlightShadowConstants) % 16u == 0u,
    "Flashlight shadow constants must preserve HLSL register alignment.");
static_assert(offsetof(
        RayTracedFlashlightShadowConstants,
        lightPositionAndRange) == sizeof(PlanarViewConstants),
    "Flashlight shadow constants drifted after the view block.");
static_assert(offsetof(
        RayTracedFlashlightShadowConstants,
        beamProfile) == sizeof(PlanarViewConstants) + 32u,
    "Flashlight beam profile must start on a constant register boundary.");

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
            nvrhi::Format format)
        {
            const nvrhi::FormatSupport required =
                nvrhi::FormatSupport::Texture |
                nvrhi::FormatSupport::ShaderLoad |
                nvrhi::FormatSupport::ShaderSample |
                nvrhi::FormatSupport::ShaderUavStore;
            return (device->queryFormatSupport(format) & required) == required;
        }

        bool SameInputs(
            const RayTracedFlashlightShadowInputs& left,
            const RayTracedFlashlightShadowInputs& right)
        {
            return left.depth == right.depth &&
                left.material == right.material &&
                left.normals == right.normals;
        }

        RayTracedFlashlightTextureShape GetTextureShape(
            const nvrhi::TextureDesc& description)
        {
            return {
                description.width,
                description.height,
                description.depth,
                description.arraySize,
                description.mipLevels,
                description.sampleCount,
                description.dimension == nvrhi::TextureDimension::Texture2D ||
                    description.dimension ==
                        nvrhi::TextureDimension::Texture2DMS,
                description.isShaderResource
            };
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
                // Floating point depth is advanced by one representable value
                // in the shader when this value is zero.
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

        bool IsFiniteFloat3(float3 value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }
    }

    bool RayTracedFlashlightShadowPass::IsDeviceSupported(
        nvrhi::IDevice* device)
    {
        return device &&
            device->queryFeatureSupport(
                nvrhi::Feature::RayTracingAccelStruct) &&
            device->queryFeatureSupport(nvrhi::Feature::RayQuery) &&
            HasFormatSupport(device, nvrhi::Format::R8_UNORM);
    }

    RayTracedFlashlightShadowPass::RayTracedFlashlightShadowPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<RendererShaderFactory>& shaderFactory,
        nvrhi::IBindingLayout* bindlessLayout)
        : m_Device(device)
        , m_BindlessLayout(bindlessLayout)
    {
        m_Supported = shaderFactory && m_BindlessLayout &&
            IsDeviceSupported(device);
        m_HitDistanceSupported = m_Supported &&
            HasFormatSupport(device, nvrhi::Format::R16_FLOAT);
        if (!m_Supported)
        {
            log::warning(
                "Ray traced flashlight shadows require DXR 1.1 ray queries and R8_UNORM UAV support");
            return;
        }

        nvrhi::BindingLayoutDesc visibilityLayoutDescription;
        visibilityLayoutDescription.visibility = nvrhi::ShaderType::Compute;
        visibilityLayoutDescription.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_SRV(5),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1)
        };
        m_VisibilityBindingLayout = device->createBindingLayout(
            visibilityLayoutDescription);

        if (m_HitDistanceSupported)
        {
            nvrhi::BindingLayoutDesc hitDistanceLayoutDescription =
                visibilityLayoutDescription;
            hitDistanceLayoutDescription.bindings.push_back(
                nvrhi::BindingLayoutItem::Texture_UAV(2));
            m_HitDistanceBindingLayout = device->createBindingLayout(
                hitDistanceLayoutDescription);
        }

        nvrhi::BufferDesc constantBufferDescription;
        constantBufferDescription.byteSize =
            sizeof(RayTracedFlashlightShadowConstants);
        constantBufferDescription.debugName =
            "RayTracedFlashlightShadowConstants";
        constantBufferDescription.isConstantBuffer = true;
        constantBufferDescription.isVolatile = true;
        constantBufferDescription.maxVersions =
            RendererMaxConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDescription);
        m_MaterialSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));

        for (uint32_t variant = 0u;
            variant < ReceiverSampleCounts.size();
            ++variant)
        {
            const std::vector<RendererShaderMacro> macros = {{
                "FLASHLIGHT_VISIBILITY_SAMPLES",
                ReceiverSampleMacros[variant]
            }};
            m_VisibilityShaders[variant] = shaderFactory->CreateShader(
                "uvsr/ray_traced_flashlight_shadows_cs.hlsl",
                "GenerateVisibility",
                &macros,
                nvrhi::ShaderType::Compute);
            if (m_VisibilityShaders[variant] &&
                m_VisibilityBindingLayout)
            {
                nvrhi::ComputePipelineDesc pipelineDescription;
                pipelineDescription.CS = m_VisibilityShaders[variant];
                pipelineDescription.bindingLayouts = {
                    m_VisibilityBindingLayout,
                    m_BindlessLayout
                };
                m_VisibilityPipelines[variant] =
                    device->createComputePipeline(pipelineDescription);
            }

            if (!m_HitDistanceSupported)
                continue;
            m_HitDistanceShaders[variant] = shaderFactory->CreateShader(
                "uvsr/ray_traced_flashlight_shadows_cs.hlsl",
                "GenerateVisibilityAndHitDistance",
                &macros,
                nvrhi::ShaderType::Compute);
            if (m_HitDistanceShaders[variant] &&
                m_HitDistanceBindingLayout)
            {
                nvrhi::ComputePipelineDesc pipelineDescription;
                pipelineDescription.CS = m_HitDistanceShaders[variant];
                pipelineDescription.bindingLayouts = {
                    m_HitDistanceBindingLayout,
                    m_BindlessLayout
                };
                m_HitDistancePipelines[variant] =
                    device->createComputePipeline(pipelineDescription);
            }
        }

        if (m_HitDistanceSupported)
        {
            for (const auto& pipeline : m_HitDistancePipelines)
                m_HitDistanceSupported = m_HitDistanceSupported &&
                    bool(pipeline);
        }
        if (!m_HitDistanceSupported)
        {
            m_HitDistanceBindingLayout = nullptr;
            for (auto& shader : m_HitDistanceShaders)
                shader = nullptr;
            for (auto& pipeline : m_HitDistancePipelines)
                pipeline = nullptr;
        }

        if (!m_VisibilityBindingLayout || !m_ConstantBuffer ||
            !m_MaterialSampler)
        {
            m_Supported = false;
        }
        for (const auto& pipeline : m_VisibilityPipelines)
            m_Supported = m_Supported && bool(pipeline);
        if (!m_Supported)
        {
            m_HitDistanceSupported = false;
            log::error(
                "The ray traced flashlight shadow pipelines could not be created");
        }
    }

    bool RayTracedFlashlightShadowPass::EnsureResources(
        const RayTracedFlashlightShadowInputs& inputs,
        bool outputHitDistance)
    {
        if (!inputs.depth || !inputs.material || !inputs.normals)
            return false;

        const nvrhi::TextureDesc& depthDescription =
            inputs.depth->getDesc();
        const RayTracedFlashlightTextureShape depthShape =
            GetTextureShape(depthDescription);
        if (!IsRayTracedFlashlightTextureShapeValid(depthShape) ||
            !IsRayTracedFlashlightTextureShapeCompatible(
                depthShape,
                GetTextureShape(inputs.material->getDesc())) ||
            !IsRayTracedFlashlightTextureShapeCompatible(
                depthShape,
                GetTextureShape(inputs.normals->getDesc())))
        {
            return false;
        }

        const uint32_t receiverSampleCount = depthDescription.sampleCount;
        const nvrhi::TextureDimension visibilityDimension =
            receiverSampleCount > 1u
                ? nvrhi::TextureDimension::Texture2DArray
                : nvrhi::TextureDimension::Texture2D;
        const bool visibilityMatches = m_OutputVisibility &&
            m_OutputVisibility->getDesc().width == depthDescription.width &&
            m_OutputVisibility->getDesc().height == depthDescription.height &&
            m_OutputVisibility->getDesc().arraySize == receiverSampleCount &&
            m_OutputVisibility->getDesc().dimension == visibilityDimension;
        const bool closestMatches = m_OutputClosestVisibility &&
            m_OutputClosestVisibility->getDesc().width ==
                depthDescription.width &&
            m_OutputClosestVisibility->getDesc().height ==
                depthDescription.height;
        const bool hitDistanceMatches = m_OutputHitDistance &&
            m_OutputHitDistance->getDesc().width == depthDescription.width &&
            m_OutputHitDistance->getDesc().height == depthDescription.height;
        if (visibilityMatches && closestMatches &&
            (outputHitDistance
                ? hitDistanceMatches
                : !m_OutputHitDistance))
        {
            return true;
        }

        nvrhi::TextureHandle visibility = m_OutputVisibility;
        nvrhi::TextureHandle closestVisibility =
            m_OutputClosestVisibility;
        nvrhi::TextureHandle hitDistance = m_OutputHitDistance;
        if (!visibilityMatches)
        {
            visibility = CreateOutputTexture(
                m_Device,
                depthDescription.width,
                depthDescription.height,
                receiverSampleCount,
                nvrhi::Format::R8_UNORM,
                "Ray Traced Flashlight Shadows/Per Raster Sample");
        }
        if (!closestMatches)
        {
            closestVisibility = CreateOutputTexture(
                m_Device,
                depthDescription.width,
                depthDescription.height,
                1u,
                nvrhi::Format::R8_UNORM,
                "Ray Traced Flashlight Shadows/Closest Raster Sample");
        }
        if (outputHitDistance && !m_HitDistanceSupported)
            return false;
        if (outputHitDistance && !hitDistanceMatches)
        {
            hitDistance = CreateOutputTexture(
                m_Device,
                depthDescription.width,
                depthDescription.height,
                1u,
                nvrhi::Format::R16_FLOAT,
                "Ray Traced Flashlight Shadows/Closest Hit Distance");
        }
        else if (!outputHitDistance && m_OutputHitDistance)
        {
            hitDistance = nullptr;
        }
        if (!visibility || !closestVisibility ||
            (outputHitDistance && !hitDistance))
            return false;

        ClearBindingSets();
        m_OutputVisibility = visibility;
        m_OutputClosestVisibility = closestVisibility;
        m_OutputHitDistance = hitDistance;
        return true;
    }

    bool RayTracedFlashlightShadowPass::EnsureBindingSet(
        uint32_t variant,
        const RayTracedFlashlightShadowInputs& inputs,
        const RayTracedMaterialVisibilityInputs& materialVisibility,
        nvrhi::rt::IAccelStruct* worldTlas,
        nvrhi::ITexture* noiseTexture,
        nvrhi::ITexture* attemptMask,
        bool outputHitDistance)
    {
        if (variant >= ReceiverSampleCounts.size() || !worldTlas ||
            !materialVisibility || !noiseTexture ||
            !m_OutputVisibility || !m_OutputClosestVisibility ||
            (outputHitDistance && !m_OutputHitDistance))
        {
            return false;
        }

        const bool inputsMatch = m_BoundTlas == worldTlas &&
            SameInputs(m_BoundInputs, inputs) &&
            m_BoundMaterialVisibility == materialVisibility &&
            m_BoundNoiseTexture == noiseTexture &&
            m_BoundAttemptMask == attemptMask;
        if (!inputsMatch)
        {
            ClearBindingSets();
            m_BoundTlas = nullptr;
            m_BoundInputs = {};
            m_BoundMaterialVisibility = {};
            m_BoundNoiseTexture = nullptr;
            m_BoundAttemptMask = nullptr;
        }

        nvrhi::BindingSetHandle& selectedBindingSet = outputHitDistance
            ? m_HitDistanceBindingSets[variant]
            : m_VisibilityBindingSets[variant];
        if (selectedBindingSet)
            return true;

        nvrhi::BindingSetDesc description;
        description.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, worldTlas),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.depth),
            nvrhi::BindingSetItem::Texture_SRV(2, inputs.material),
            nvrhi::BindingSetItem::Texture_SRV(3, inputs.normals),
            nvrhi::BindingSetItem::Texture_SRV(4, noiseTexture),
            nvrhi::BindingSetItem::Texture_SRV(5, attemptMask),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                10, materialVisibility.geometryBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                11, materialVisibility.materialBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                12, materialVisibility.geometryIndexMap),
            nvrhi::BindingSetItem::Sampler(0, m_MaterialSampler),
            nvrhi::BindingSetItem::Texture_UAV(0, m_OutputVisibility),
            nvrhi::BindingSetItem::Texture_UAV(
                1, m_OutputClosestVisibility)
        };
        nvrhi::BindingLayoutHandle selectedLayout =
            m_VisibilityBindingLayout;
        if (outputHitDistance)
        {
            description.bindings.push_back(
                nvrhi::BindingSetItem::Texture_UAV(
                    2, m_OutputHitDistance));
            selectedLayout = m_HitDistanceBindingLayout;
        }
        selectedBindingSet = m_Device->createBindingSet(
            description,
            selectedLayout);
        if (!selectedBindingSet)
            return false;

        m_BoundTlas = worldTlas;
        m_BoundInputs = inputs;
        m_BoundMaterialVisibility = materialVisibility;
        m_BoundNoiseTexture = noiseTexture;
        m_BoundAttemptMask = attemptMask;
        return true;
    }

    RayTracedFlashlightShadowResult
        RayTracedFlashlightShadowPass::Render(
            nvrhi::ICommandList* commandList,
            const IView& view,
            const RayTracedFlashlightShadowInputs& inputs,
            const RayTracedMaterialVisibilityInputs& materialVisibility,
            nvrhi::rt::IAccelStruct* worldTlas,
            const SpotLight* light,
            const FlashlightBeamProfile& beamProfile,
            const NoiseSettings& noiseSettings,
            nvrhi::ITexture* noiseTexture,
            uint32_t samplingPhase,
            float rayBiasMeters,
            bool outputHitDistance,
            const LightingSampleSchedule& sampleSchedule)
    {
        const bool baseInputsPresent = m_Supported && commandList &&
            materialVisibility && worldTlas && light && noiseTexture &&
            sampleSchedule;
        if (!baseInputsPresent ||
            (outputHitDistance && !m_HitDistanceSupported) ||
            !FlashlightBeamProfileIsValid(beamProfile) ||
            !IsValidNoiseSettings(noiseSettings) ||
            !std::isfinite(rayBiasMeters) ||
            rayBiasMeters < 0.f ||
            rayBiasMeters > RayTracedFlashlightMaximumRayBias ||
            !std::isfinite(light->range) ||
            !(light->range > beamProfile.emitterRadiusMeters) ||
            !std::isfinite(light->radius) ||
            std::abs(light->radius -
                beamProfile.emitterRadiusMeters) > 1e-6f)
        {
            if (!m_ReportedInvalidInput && baseInputsPresent)
            {
                log::error(
                    "Ray traced flashlight shadows received incomplete or unsupported inputs");
                m_ReportedInvalidInput = true;
            }
            return {};
        }

        const float3 lightPosition = float3(light->GetPosition());
        float3 lightDirection = float3(light->GetDirection());
        const float directionLengthSquared = dot(
            lightDirection,
            lightDirection);
        if (!IsFiniteFloat3(lightPosition) ||
            !IsFiniteFloat3(lightDirection) ||
            !(directionLengthSquared > 1e-12f) ||
            !std::isfinite(directionLengthSquared))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Ray traced flashlight shadows received an invalid light transform");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        lightDirection /= std::sqrt(directionLengthSquared);

        if (!EnsureResources(inputs, outputHitDistance))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Ray traced flashlight shadow textures were missing, mismatched, or could not be allocated");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        const uint32_t receiverSampleCount =
            inputs.depth->getDesc().sampleCount;
        const uint32_t variant = FindVariant(receiverSampleCount);
        if (variant >= ReceiverSampleCounts.size() ||
            !m_VisibilityPipelines[variant] ||
            (outputHitDistance && !m_HitDistancePipelines[variant]))
        {
            return {};
        }
        if (!EnsureBindingSet(
                variant,
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
                    "Ray traced flashlight shadow binding set creation failed");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        m_ReportedInvalidInput = false;

        RayTracedFlashlightShadowConstants constants = {};
        view.FillPlanarViewConstants(constants.view);
        constants.lightPositionAndRange = {
            lightPosition.x,
            lightPosition.y,
            lightPosition.z,
            light->range };
        constants.lightDirectionAndEmitterRadius = {
            lightDirection.x,
            lightDirection.y,
            lightDirection.z,
            light->radius };
        constants.beamProfile = beamProfile;
        constants.depthQuantizationStep = GetDepthQuantizationStep(
            inputs.depth->getDesc().format);
        constants.rayBias = rayBiasMeters;
        constants.reverseDepth = view.IsReverseDepth() ? 1u : 0u;
        constants.floatDepth = IsFloatingPointDepth(
            inputs.depth->getDesc().format) ? 1u : 0u;
        const bool stochastic = beamProfile.emitterRadiusMeters > 0.f;
        constants.sampleSequencePhase = samplingPhase;
        constants.sampleCount = stochastic
            ? RayTracedFlashlightFiniteEmitterSampleCount
            : 1u;
        constants.noisePattern =
            static_cast<uint32_t>(noiseSettings.pattern);
        constants.sampleSequenceMode = static_cast<uint32_t>(
            ResolveLightingSampleSequenceMode(
                sampleSchedule,
                stochastic,
                noiseSettings.animate));
        commandList->writeBuffer(
            m_ConstantBuffer,
            &constants,
            sizeof(constants));

        commandList->beginMarker("Ray Traced Flashlight Shadows");
        nvrhi::ComputeState state;
        state.pipeline = outputHitDistance
            ? m_HitDistancePipelines[variant]
            : m_VisibilityPipelines[variant];
        state.bindings = {
            outputHitDistance
                ? m_HitDistanceBindingSets[variant]
                : m_VisibilityBindingSets[variant],
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
            light,
            receiverSampleCount,
            true,
            stochastic
        };
    }

    void RayTracedFlashlightShadowPass::ResetBindingCache()
    {
        ClearBindingSets();
        m_BoundTlas = nullptr;
        m_BoundInputs = {};
        m_BoundMaterialVisibility = {};
        m_BoundNoiseTexture = nullptr;
        m_BoundAttemptMask = nullptr;
    }

    void RayTracedFlashlightShadowPass::ClearBindingSets()
    {
        for (auto& bindingSet : m_VisibilityBindingSets)
            bindingSet = nullptr;
        for (auto& bindingSet : m_HitDistanceBindingSets)
            bindingSet = nullptr;
    }
}
