#include "heitz_ratio_estimator_shadows.h"

#include "ratio_estimator_shared.h"
#include "visibility_blue_noise.h"

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

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
    uvsr::HeitzRatioEstimatorNoisePattern::PermutatedWhiteNoise) == 0u);
static_assert(static_cast<uint32_t>(
    uvsr::HeitzRatioEstimatorNoisePattern::VoidClusterBlueNoise) == 1u);

namespace uvsr
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;

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
            uint32_t height)
        {
            nvrhi::TextureDesc description;
            description.width = width;
            description.height = height;
            description.format = nvrhi::Format::RGBA16_FLOAT;
            description.dimension = nvrhi::TextureDimension::Texture2D;
            description.isUAV = true;
            description.debugName =
                "Heitz Shadows/RGB Ratio Modulation";
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
            HasFormatSupport(device, nvrhi::Format::R16_UNORM, false);
    }

    HeitzRatioEstimatorShadowPass::HeitzRatioEstimatorShadowPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::vector<uint16_t>* preparedBlueNoise)
        : m_Device(device)
    {
        m_Supported = shaderFactory && IsDeviceSupported(device);
        if (!m_Supported)
        {
            log::warning(
                "Heitz ratio-estimator shadows require DXR 1.1 ray queries, RGBA16F UAV support, and R16_UNORM sampling");
            return;
        }

        const size_t expectedNoiseValues =
            size_t(VisibilityBlueNoiseTexelCount) *
            VisibilityBlueNoiseLayerCount;
        if (preparedBlueNoise &&
            preparedBlueNoise->size() == expectedNoiseValues)
        {
            m_BlueNoiseUpload = *preparedBlueNoise;
        }
        else
        {
            m_BlueNoiseUpload = GenerateVisibilityBlueNoise();
        }

        nvrhi::TextureDesc noiseDescription;
        noiseDescription.width = VisibilityBlueNoiseSize;
        noiseDescription.height = VisibilityBlueNoiseSize;
        noiseDescription.arraySize = VisibilityBlueNoiseLayerCount;
        noiseDescription.format = nvrhi::Format::R16_UNORM;
        noiseDescription.dimension =
            nvrhi::TextureDimension::Texture2DArray;
        noiseDescription.initialState = nvrhi::ResourceStates::CopyDest;
        noiseDescription.keepInitialState = true;
        noiseDescription.debugName =
            "Heitz Shadows/Void-Cluster Blue Noise";
        m_BlueNoise = device->createTexture(noiseDescription);

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
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_BindingLayout = device->createBindingLayout(layoutDescription);

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

        m_Shader = shaderFactory->CreateShader(
            "uvsr/heitz_ratio_estimator_shadows_cs.hlsl",
            "Generate",
            nullptr,
            nvrhi::ShaderType::Compute);
        if (m_Shader && m_BindingLayout)
        {
            nvrhi::ComputePipelineDesc pipelineDescription;
            pipelineDescription.CS = m_Shader;
            pipelineDescription.bindingLayouts = { m_BindingLayout };
            m_Pipeline = device->createComputePipeline(
                pipelineDescription);
        }

        if (!m_BlueNoise || !m_BindingLayout || !m_ConstantBuffer ||
            !m_Shader || !m_Pipeline)
        {
            m_Supported = false;
            log::error(
                "The Heitz ratio-estimator shadow pipeline could not be created");
        }
    }

    void HeitzRatioEstimatorShadowPass::UploadBlueNoise(
        nvrhi::ICommandList* commandList)
    {
        if (m_BlueNoiseUploaded || !commandList || !m_BlueNoise)
            return;
        for (uint32_t layer = 0u;
            layer < VisibilityBlueNoiseLayerCount;
            ++layer)
        {
            commandList->writeTexture(
                m_BlueNoise,
                layer,
                0u,
                m_BlueNoiseUpload.data() +
                    layer * VisibilityBlueNoiseTexelCount,
                size_t(VisibilityBlueNoiseSize) * sizeof(uint16_t));
        }
        commandList->setPermanentTextureState(
            m_BlueNoise,
            nvrhi::ResourceStates::ShaderResource);
        m_BlueNoiseUploaded = true;
        m_BlueNoiseUpload.clear();
        m_BlueNoiseUpload.shrink_to_fit();
    }

    bool HeitzRatioEstimatorShadowPass::EnsureResources(
        const HeitzRatioEstimatorShadowInputs& inputs)
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
        for (const nvrhi::ITexture* texture : textures)
        {
            if (!texture || texture->getDesc().sampleCount != 1u ||
                texture->getDesc().dimension !=
                    nvrhi::TextureDimension::Texture2D ||
                texture->getDesc().width != depthDescription.width ||
                texture->getDesc().height != depthDescription.height)
            {
                return false;
            }
        }
        const bool outputSizeMatches = m_OutputModulation &&
            m_OutputModulation->getDesc().width == depthDescription.width &&
            m_OutputModulation->getDesc().height == depthDescription.height;
        if (outputSizeMatches)
            return true;

        nvrhi::TextureHandle outputModulation = CreateOutputTexture(
            m_Device,
            depthDescription.width,
            depthDescription.height);
        if (!outputModulation)
            return false;

        ClearBindingSets();
        m_OutputModulation = outputModulation;
        return true;
    }

    bool HeitzRatioEstimatorShadowPass::EnsureBindingSets(
        const HeitzRatioEstimatorShadowInputs& inputs,
        nvrhi::rt::IAccelStruct* worldTlas)
    {
        if (!worldTlas || !m_OutputModulation)
            return false;
        if (m_BindingSet &&
            m_BoundTlas == worldTlas &&
            SameInputs(m_BoundInputs, inputs))
        {
            return true;
        }

        ClearBindingSets();
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
            nvrhi::BindingSetItem::Texture_SRV(6, m_BlueNoise),
            nvrhi::BindingSetItem::Texture_SRV(7, inputs.emissive),
            nvrhi::BindingSetItem::Texture_UAV(0, m_OutputModulation)
        };
        m_BindingSet = m_Device->createBindingSet(
            description, m_BindingLayout);
        if (!m_BindingSet)
        {
            ClearBindingSets();
            return false;
        }

        m_BoundTlas = worldTlas;
        m_BoundInputs = inputs;
        return true;
    }

    HeitzRatioEstimatorShadowResult
        HeitzRatioEstimatorShadowPass::Render(
            nvrhi::ICommandList* commandList,
            const HeitzRatioEstimatorShadowSettings& settings,
            const IView& view,
            const HeitzRatioEstimatorShadowInputs& inputs,
            nvrhi::rt::IAccelStruct* worldTlas,
            const DirectionalLight* light,
            uint32_t samplingPhase,
            float sceneDiagonal)
    {
        if (!m_Supported || !commandList || !worldTlas || !light ||
            !IsHeitzRatioEstimatorConfigurationSupported(settings))
        {
            if (!m_ReportedInvalidInput && m_Supported &&
                commandList && worldTlas && light)
            {
                log::error(
                    "Heitz ratio-estimator shadows received incomplete or unsupported inputs");
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
                    "Heitz ratio-estimator shadows received an invalid scene extent");
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
        const uint32_t sampleCount = stochastic
            ? ResolveHeitzRatioEstimatorSampleCount(
                settings.sampleRateLog2)
            : 1u;
        if (!EnsureResources(inputs))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Heitz ratio-estimator shadow textures were missing, mismatched, or could not be allocated");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        if (!EnsureBindingSets(inputs, worldTlas))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Heitz ratio-estimator shadow binding-set creation failed");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        m_ReportedInvalidInput = false;
        if (stochastic && settings.noisePattern ==
                HeitzRatioEstimatorNoisePattern::VoidClusterBlueNoise)
        {
            UploadBlueNoise(commandList);
        }

        HeitzRatioEstimatorShadowConstants constants = {};
        view.FillPlanarViewConstants(constants.view);
        constants.directionToLightAndAngularRadius = float4(
            directionToLight, angularRadius);
        constants.sampleSequencePhase = settings.animateSamples
            ? samplingPhase
            : 0u;
        constants.sampleCount = sampleCount;
        constants.hardShadows = stochastic ? 0u : 1u;
        constants.noisePattern =
            static_cast<uint32_t>(settings.noisePattern);
        constants.rayDistance = rayDistance;
        constants.denominatorEpsilon =
            RatioEstimatorDefaultDenominatorEpsilon;
        constants.depthQuantizationStep = GetDepthQuantizationStep(
            inputs.depth->getDesc().format);
        constants.rayBias = settings.rayBias;
        constants.reverseDepth = view.IsReverseDepth() ? 1u : 0u;
        constants.floatDepth = IsFloatingPointDepth(
            inputs.depth->getDesc().format) ? 1u : 0u;
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));

        commandList->beginMarker("Heitz RGB Ratio-Estimator Shadows");
        nvrhi::ComputeState state;
        state.pipeline = m_Pipeline;
        state.bindings = { m_BindingSet };
        commandList->setComputeState(state);
        const nvrhi::Rect viewExtent = view.GetViewExtent();
        commandList->dispatch(
            div_ceil(viewExtent.width(), 8),
            div_ceil(viewExtent.height(), 8));
        commandList->endMarker();
        return {
            m_OutputModulation,
            light,
            true,
            stochastic
        };
    }

    void HeitzRatioEstimatorShadowPass::ResetBindingCache()
    {
        ClearBindingSets();
        m_BoundTlas = nullptr;
        m_BoundInputs = {};
    }

    void HeitzRatioEstimatorShadowPass::ClearBindingSets()
    {
        m_BindingSet = nullptr;
    }
}
