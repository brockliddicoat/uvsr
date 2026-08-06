#include "ray_traced_sky_visibility.h"

#include "visibility_blue_noise.h"

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <cstddef>

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
    uvsr::RayTracedSkyVisibilityNoisePattern::PermutatedWhiteNoise) == 0u);
static_assert(static_cast<uint32_t>(
    uvsr::RayTracedSkyVisibilityNoisePattern::VoidClusterBlueNoise) == 1u);

namespace uvsr
{
    namespace
    {
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
            const RayTracedSkyVisibilityInputs& left,
            const RayTracedSkyVisibilityInputs& right)
        {
            return left.depth == right.depth &&
                left.material == right.material &&
                left.normals == right.normals;
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
            description.format = nvrhi::Format::R8_UNORM;
            description.dimension = nvrhi::TextureDimension::Texture2D;
            description.isUAV = true;
            description.debugName = "Ray-Traced Sky Visibility";
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
            HasFormatSupport(device, nvrhi::Format::R16_UNORM, false);
    }

    RayTracedSkyVisibilityPass::RayTracedSkyVisibilityPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::vector<uint16_t>* preparedBlueNoise)
        : m_Device(device)
    {
        m_Supported = shaderFactory && IsDeviceSupported(device);
        if (!m_Supported)
        {
            log::warning(
                "Ray-traced sky visibility requires DXR 1.1 ray queries, "
                "R8_UNORM UAV support, and R16_UNORM sampling");
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
            "Ray-Traced Sky Visibility/Void-Cluster Blue Noise";
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
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_BindingLayout = device->createBindingLayout(layoutDescription);

        nvrhi::BufferDesc constantBufferDescription;
        constantBufferDescription.byteSize =
            sizeof(RayTracedSkyVisibilityConstants);
        constantBufferDescription.debugName =
            "RayTracedSkyVisibilityConstants";
        constantBufferDescription.isConstantBuffer = true;
        constantBufferDescription.isVolatile = true;
        constantBufferDescription.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(
            constantBufferDescription);

        m_Shader = shaderFactory->CreateShader(
            "uvsr/ray_traced_sky_visibility_cs.hlsl",
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
                "The ray-traced sky-visibility pipeline could not be created");
        }
    }

    void RayTracedSkyVisibilityPass::UploadBlueNoise(
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

    bool RayTracedSkyVisibilityPass::EnsureResources(
        const RayTracedSkyVisibilityInputs& inputs)
    {
        const nvrhi::ITexture* textures[] = {
            inputs.depth,
            inputs.material,
            inputs.normals
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
        const bool outputSizeMatches = m_OutputVisibility &&
            m_OutputVisibility->getDesc().width == depthDescription.width &&
            m_OutputVisibility->getDesc().height == depthDescription.height;
        if (outputSizeMatches)
            return true;

        nvrhi::TextureHandle outputVisibility = CreateOutputTexture(
            m_Device,
            depthDescription.width,
            depthDescription.height);
        if (!outputVisibility)
            return false;

        ClearBindingSet();
        m_OutputVisibility = outputVisibility;
        return true;
    }

    bool RayTracedSkyVisibilityPass::EnsureBindingSet(
        const RayTracedSkyVisibilityInputs& inputs,
        nvrhi::rt::IAccelStruct* worldTlas)
    {
        if (!worldTlas || !m_OutputVisibility)
            return false;
        if (m_BindingSet &&
            m_BoundTlas == worldTlas &&
            SameInputs(m_BoundInputs, inputs))
        {
            return true;
        }

        ClearBindingSet();
        nvrhi::BindingSetDesc description;
        description.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, worldTlas),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.depth),
            nvrhi::BindingSetItem::Texture_SRV(2, inputs.material),
            nvrhi::BindingSetItem::Texture_SRV(3, inputs.normals),
            nvrhi::BindingSetItem::Texture_SRV(4, m_BlueNoise),
            nvrhi::BindingSetItem::Texture_UAV(0, m_OutputVisibility)
        };
        m_BindingSet = m_Device->createBindingSet(
            description, m_BindingLayout);
        if (!m_BindingSet)
        {
            ClearBindingSet();
            return false;
        }

        m_BoundTlas = worldTlas;
        m_BoundInputs = inputs;
        return true;
    }

    RayTracedSkyVisibilityResult RayTracedSkyVisibilityPass::Render(
        nvrhi::ICommandList* commandList,
        const RayTracedSkyVisibilitySettings& settings,
        const IView& view,
        const RayTracedSkyVisibilityInputs& inputs,
        nvrhi::rt::IAccelStruct* worldTlas,
        uint32_t samplingPhase,
        float sceneDiagonal)
    {
        if (!m_Supported || !commandList || !worldTlas ||
            !IsRayTracedSkyVisibilityConfigurationSupported(settings))
        {
            if (!m_ReportedInvalidInput && m_Supported &&
                commandList && worldTlas)
            {
                log::error(
                    "Ray-traced sky visibility received incomplete or unsupported inputs");
                m_ReportedInvalidInput = true;
            }
            return {};
        }

        if (!EnsureResources(inputs))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Ray-traced sky-visibility textures were missing, "
                    "mismatched, or could not be allocated");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        if (!EnsureBindingSet(inputs, worldTlas))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Ray-traced sky-visibility binding-set creation failed");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        m_ReportedInvalidInput = false;
        if (settings.noisePattern ==
            RayTracedSkyVisibilityNoisePattern::VoidClusterBlueNoise)
        {
            UploadBlueNoise(commandList);
        }

        RayTracedSkyVisibilityConstants constants = {};
        view.FillPlanarViewConstants(constants.view);
        constants.sampleSequencePhase = settings.animateSamples
            ? samplingPhase
            : 0u;
        constants.sampleCount = ResolveRayTracedSkyVisibilitySampleCount(
            settings.sampleRateLog2);
        constants.noisePattern =
            static_cast<uint32_t>(settings.noisePattern);
        constants.rayDistance = std::max(sceneDiagonal * 2.f, 1.f);
        constants.depthQuantizationStep = GetDepthQuantizationStep(
            inputs.depth->getDesc().format);
        constants.rayBias = settings.rayBias;
        constants.reverseDepth = view.IsReverseDepth() ? 1u : 0u;
        constants.floatDepth = IsFloatingPointDepth(
            inputs.depth->getDesc().format) ? 1u : 0u;
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));

        commandList->beginMarker("Ray-Traced Sky Visibility");
        nvrhi::ComputeState state;
        state.pipeline = m_Pipeline;
        state.bindings = { m_BindingSet };
        commandList->setComputeState(state);
        const nvrhi::Rect viewExtent = view.GetViewExtent();
        commandList->dispatch(
            div_ceil(viewExtent.width(), 8),
            div_ceil(viewExtent.height(), 8));
        commandList->endMarker();
        return { m_OutputVisibility, true };
    }

    void RayTracedSkyVisibilityPass::ResetBindingCache()
    {
        ClearBindingSet();
        m_BoundTlas = nullptr;
        m_BoundInputs = {};
    }

    void RayTracedSkyVisibilityPass::ClearBindingSet()
    {
        m_BindingSet = nullptr;
    }
}
