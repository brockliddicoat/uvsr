#include "ray_traced_flashlight_shadows.h"

#include "ray_traced_flashlight_shadows_shared.h"

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <cmath>
#include <cstddef>

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
                description.dimension == nvrhi::TextureDimension::Texture2D,
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
        const std::shared_ptr<ShaderFactory>& shaderFactory,
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
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_VisibilityBindingLayout = device->createBindingLayout(
            visibilityLayoutDescription);

        if (m_HitDistanceSupported)
        {
            nvrhi::BindingLayoutDesc hitDistanceLayoutDescription =
                visibilityLayoutDescription;
            hitDistanceLayoutDescription.bindings.push_back(
                nvrhi::BindingLayoutItem::Texture_UAV(1));
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
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDescription);
        m_MaterialSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));

        m_VisibilityShader = shaderFactory->CreateShader(
            "uvsr/ray_traced_flashlight_shadows_cs.hlsl",
            "GenerateVisibility",
            nullptr,
            nvrhi::ShaderType::Compute);
        if (m_HitDistanceSupported)
        {
            m_HitDistanceShader = shaderFactory->CreateShader(
                "uvsr/ray_traced_flashlight_shadows_cs.hlsl",
                "GenerateVisibilityAndHitDistance",
                nullptr,
                nvrhi::ShaderType::Compute);
        }

        if (m_VisibilityShader && m_VisibilityBindingLayout)
        {
            nvrhi::ComputePipelineDesc pipelineDescription;
            pipelineDescription.CS = m_VisibilityShader;
            pipelineDescription.bindingLayouts = {
                m_VisibilityBindingLayout,
                m_BindlessLayout
            };
            m_VisibilityPipeline = device->createComputePipeline(
                pipelineDescription);
        }
        if (m_HitDistanceSupported &&
            m_HitDistanceShader && m_HitDistanceBindingLayout)
        {
            nvrhi::ComputePipelineDesc pipelineDescription;
            pipelineDescription.CS = m_HitDistanceShader;
            pipelineDescription.bindingLayouts = {
                m_HitDistanceBindingLayout,
                m_BindlessLayout
            };
            m_HitDistancePipeline = device->createComputePipeline(
                pipelineDescription);
        }

        if (m_HitDistanceSupported &&
            (!m_HitDistanceBindingLayout || !m_HitDistanceShader ||
                !m_HitDistancePipeline))
        {
            m_HitDistanceSupported = false;
            m_HitDistanceBindingLayout = nullptr;
            m_HitDistanceShader = nullptr;
            m_HitDistancePipeline = nullptr;
        }

        if (!m_VisibilityBindingLayout || !m_ConstantBuffer ||
            !m_MaterialSampler ||
            !m_VisibilityShader || !m_VisibilityPipeline)
        {
            m_Supported = false;
            m_HitDistanceSupported = false;
            log::error(
                "The ray traced flashlight shadow pipeline could not be created");
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

        const bool visibilityMatches = m_OutputVisibility &&
            m_OutputVisibility->getDesc().width == depthDescription.width &&
            m_OutputVisibility->getDesc().height == depthDescription.height;
        const bool hitDistanceMatches = m_OutputHitDistance &&
            m_OutputHitDistance->getDesc().width == depthDescription.width &&
            m_OutputHitDistance->getDesc().height == depthDescription.height;
        if (visibilityMatches &&
            (outputHitDistance
                ? hitDistanceMatches
                : !m_OutputHitDistance))
        {
            return true;
        }

        nvrhi::TextureHandle visibility = m_OutputVisibility;
        nvrhi::TextureHandle hitDistance = m_OutputHitDistance;
        if (!visibilityMatches)
        {
            visibility = CreateOutputTexture(
                m_Device,
                depthDescription.width,
                depthDescription.height,
                nvrhi::Format::R8_UNORM,
                "Ray Traced Flashlight Shadows/Visibility");
        }
        if (outputHitDistance && !m_HitDistanceSupported)
            return false;
        if (outputHitDistance && !hitDistanceMatches)
        {
            hitDistance = CreateOutputTexture(
                m_Device,
                depthDescription.width,
                depthDescription.height,
                nvrhi::Format::R16_FLOAT,
                "Ray Traced Flashlight Shadows/Hit Distance");
        }
        else if (!outputHitDistance && m_OutputHitDistance)
        {
            hitDistance = nullptr;
        }
        if (!visibility || (outputHitDistance && !hitDistance))
            return false;

        ClearBindingSets();
        m_OutputVisibility = visibility;
        m_OutputHitDistance = hitDistance;
        return true;
    }

    bool RayTracedFlashlightShadowPass::EnsureBindingSet(
        const RayTracedFlashlightShadowInputs& inputs,
        const RayTracedMaterialVisibilityInputs& materialVisibility,
        nvrhi::rt::IAccelStruct* worldTlas,
        nvrhi::ITexture* noiseTexture,
        nvrhi::ITexture* attemptMask,
        bool outputHitDistance)
    {
        if (!worldTlas || !materialVisibility || !noiseTexture ||
            !m_OutputVisibility ||
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
            ? m_HitDistanceBindingSet
            : m_VisibilityBindingSet;
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
            nvrhi::BindingSetItem::Texture_UAV(0, m_OutputVisibility)
        };
        nvrhi::BindingLayoutHandle selectedLayout =
            m_VisibilityBindingLayout;
        if (outputHitDistance)
        {
            description.bindings.push_back(
                nvrhi::BindingSetItem::Texture_UAV(
                    1, m_OutputHitDistance));
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
        if (!EnsureBindingSet(
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
        constants.lightPositionAndRange = float4(
            lightPosition,
            light->range);
        constants.lightDirectionAndEmitterRadius = float4(
            lightDirection,
            light->radius);
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
        constants.attemptMaskEnabled =
            sampleSchedule.enabled && stochastic ? 1u : 0u;
        commandList->writeBuffer(
            m_ConstantBuffer,
            &constants,
            sizeof(constants));

        commandList->beginMarker("Ray Traced Flashlight Shadows");
        nvrhi::ComputeState state;
        state.pipeline = outputHitDistance
            ? m_HitDistancePipeline
            : m_VisibilityPipeline;
        state.bindings = {
            outputHitDistance
                ? m_HitDistanceBindingSet
                : m_VisibilityBindingSet,
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
            outputHitDistance ? m_OutputHitDistance.Get() : nullptr,
            light,
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
        m_VisibilityBindingSet = nullptr;
        m_HitDistanceBindingSet = nullptr;
    }
}
