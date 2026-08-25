#include "directional_ray_visibility_pass.h"
#include "renderer_common_passes.h"
#include "renderer_log.h"
#include "renderer_receiver_texture_contract.h"
#include "renderer_shader_factory.h"

#include <donut/core/math/math.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "directional_ray_visibility_cb.h"

static_assert(sizeof(DirectionalRayVisibilityConstants) % 16u == 0u);
static_assert(offsetof(
    DirectionalRayVisibilityConstants,
    directionToLightAndDistance) == sizeof(PlanarViewConstants));

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
            nvrhi::FormatSupport required)
        {
            return device &&
                (device->queryFormatSupport(format) & required) == required;
        }

        bool SameInputs(
            const DirectionalRayVisibilityInputs& left,
            const DirectionalRayVisibilityInputs& right)
        {
            return left.depth == right.depth &&
                left.material == right.material &&
                left.normals == right.normals;
        }

        float GetDepthQuantizationStep(nvrhi::Format format)
        {
            switch (format)
            {
            case nvrhi::Format::D16: return 1.f / 65535.f;
            case nvrhi::Format::D24S8: return 1.f / 16777215.f;
            default: return 0.f;
            }
        }

        bool IsFloatingPointDepth(nvrhi::Format format)
        {
            return format == nvrhi::Format::D32 ||
                format == nvrhi::Format::D32S8 ||
                format == nvrhi::Format::R32_FLOAT;
        }
    }

    bool DirectionalRayVisibilityPass::IsDeviceSupported(
        nvrhi::IDevice* device)
    {
        const nvrhi::FormatSupport visibilitySupport =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderUavStore;
        return device &&
            device->queryFeatureSupport(
                nvrhi::Feature::RayTracingAccelStruct) &&
            device->queryFeatureSupport(nvrhi::Feature::RayQuery) &&
            HasFormatSupport(
                device,
                nvrhi::Format::R8_UNORM,
                visibilitySupport) &&
            HasFormatSupport(
                device,
                nvrhi::Format::R32_FLOAT,
                visibilitySupport);
    }

    DirectionalRayVisibilityPass::DirectionalRayVisibilityPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<RendererShaderFactory>& shaderFactory,
        nvrhi::IBindingLayout* bindlessLayout)
        : m_Device(device)
        , m_BindlessLayout(bindlessLayout)
    {
        m_Supported = shaderFactory && m_BindlessLayout &&
            IsDeviceSupported(device);
        if (!m_Supported)
        {
            log::warning(
                "Directional ray visibility requires DXR 1.1 ray queries and R8_UNORM UAV support");
            return;
        }

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize =
            sizeof(DirectionalRayVisibilityConstants);
        constantBufferDesc.debugName =
            "DirectionalRayVisibilityConstants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            RendererMaxConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDesc);
        m_MaterialSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(2)
        };
        m_BindingLayout = device->createBindingLayout(layoutDesc);

        for (uint32_t variant = 0u;
            variant < ReceiverSampleCounts.size();
            ++variant)
        {
            const std::vector<RendererShaderMacro> macros = {{
                "DIRECTIONAL_VISIBILITY_SAMPLES",
                ReceiverSampleMacros[variant]
            }};
            m_Shaders[variant] = shaderFactory->CreateShader(
                "uvsr/directional_ray_visibility_cs.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Compute);
            if (!m_Shaders[variant] || !m_BindingLayout)
                continue;
            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = m_Shaders[variant];
            pipelineDesc.bindingLayouts = {
                m_BindingLayout,
                m_BindlessLayout
            };
            m_Pipelines[variant] =
                device->createComputePipeline(pipelineDesc);
        }

        m_Supported = m_ConstantBuffer && m_MaterialSampler &&
            m_BindingLayout;
        for (const nvrhi::ComputePipelineHandle& pipeline : m_Pipelines)
            m_Supported = m_Supported && bool(pipeline);
        if (!m_Supported)
        {
            log::error(
                "Directional ray visibility pipelines could not be created");
        }
    }

    bool DirectionalRayVisibilityPass::EnsureResources(
        const DirectionalRayVisibilityInputs& inputs)
    {
        if (!inputs.depth || !inputs.material || !inputs.normals)
            return false;
        const nvrhi::TextureDesc& depth = inputs.depth->getDesc();
        const nvrhi::TextureDesc& material = inputs.material->getDesc();
        const nvrhi::TextureDesc& normals = inputs.normals->getDesc();
        if (!AreRendererReceiverTextureDescriptorsCompatible(
                depth,
                material,
                normals) ||
            FindVariant(depth.sampleCount) >= ReceiverSampleCounts.size())
        {
            return false;
        }

        const nvrhi::TextureDimension outputDimension =
            depth.sampleCount > 1u
                ? nvrhi::TextureDimension::Texture2DArray
                : nvrhi::TextureDimension::Texture2D;
        if (m_Visibility && m_ClosestVisibility &&
            m_ClosestHitDistance &&
            m_Visibility->getDesc().width == depth.width &&
            m_Visibility->getDesc().height == depth.height &&
            m_Visibility->getDesc().arraySize == depth.sampleCount &&
            m_Visibility->getDesc().dimension == outputDimension)
        {
            return true;
        }

        nvrhi::TextureDesc outputDesc;
        outputDesc.width = depth.width;
        outputDesc.height = depth.height;
        outputDesc.arraySize = depth.sampleCount;
        outputDesc.dimension = outputDimension;
        outputDesc.format = nvrhi::Format::R8_UNORM;
        outputDesc.isUAV = true;
        outputDesc.debugName =
            "Directional Ray Visibility/Per Raster Sample";
        outputDesc.enableAutomaticStateTracking(
            nvrhi::ResourceStates::ShaderResource);
        nvrhi::TextureHandle output = m_Device->createTexture(outputDesc);
        nvrhi::TextureDesc closestDesc = outputDesc;
        closestDesc.arraySize = 1u;
        closestDesc.dimension = nvrhi::TextureDimension::Texture2D;
        closestDesc.debugName =
            "Directional Ray Visibility/Closest Raster Sample";
        nvrhi::TextureHandle closest =
            m_Device->createTexture(closestDesc);
        nvrhi::TextureDesc hitDistanceDesc = closestDesc;
        hitDistanceDesc.format = nvrhi::Format::R32_FLOAT;
        hitDistanceDesc.debugName =
            "Directional Ray Visibility/Closest Hit Distance";
        nvrhi::TextureHandle closestHitDistance =
            m_Device->createTexture(hitDistanceDesc);
        if (!output || !closest || !closestHitDistance)
            return false;
        ClearBindingSets();
        m_Visibility = output;
        m_ClosestVisibility = closest;
        m_ClosestHitDistance = closestHitDistance;
        return true;
    }

    bool DirectionalRayVisibilityPass::EnsureBindingSet(
        uint32_t variant,
        const DirectionalRayVisibilityInputs& inputs,
        const RayTracedMaterialVisibilityInputs& materialVisibility,
        nvrhi::rt::IAccelStruct* worldTlas)
    {
        if (variant >= m_BindingSets.size() || !worldTlas ||
            !materialVisibility || !m_Visibility || !m_ClosestVisibility ||
            !m_ClosestHitDistance)
        {
            return false;
        }
        if (m_BoundTlas != worldTlas ||
            !SameInputs(m_BoundInputs, inputs) ||
            m_BoundMaterialVisibility != materialVisibility)
        {
            ClearBindingSets();
            m_BoundTlas = worldTlas;
            m_BoundInputs = inputs;
            m_BoundMaterialVisibility = materialVisibility;
        }
        if (m_BindingSets[variant])
            return true;

        nvrhi::BindingSetDesc desc;
        desc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, worldTlas),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.depth),
            nvrhi::BindingSetItem::Texture_SRV(2, inputs.material),
            nvrhi::BindingSetItem::Texture_SRV(3, inputs.normals),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                10, materialVisibility.geometryBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                11, materialVisibility.materialBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                12, materialVisibility.geometryIndexMap),
            nvrhi::BindingSetItem::Sampler(0, m_MaterialSampler),
            nvrhi::BindingSetItem::Texture_UAV(0, m_Visibility),
            nvrhi::BindingSetItem::Texture_UAV(
                1, m_ClosestVisibility),
            nvrhi::BindingSetItem::Texture_UAV(
                2, m_ClosestHitDistance)
        };
        m_BindingSets[variant] =
            m_Device->createBindingSet(desc, m_BindingLayout);
        return bool(m_BindingSets[variant]);
    }

    DirectionalRayVisibilityResult DirectionalRayVisibilityPass::Render(
        nvrhi::ICommandList* commandList,
        const DirectionalShadowSettings& settings,
        const IView& view,
        const DirectionalRayVisibilityInputs& inputs,
        const RayTracedMaterialVisibilityInputs& materialVisibility,
        nvrhi::rt::IAccelStruct* worldTlas,
        const DirectionalLight* light,
        float sceneDiagonal)
    {
        if (!m_Supported || !settings.enabled || !commandList ||
            !materialVisibility || !worldTlas || !light ||
            !IsDirectionalShadowSettingsValid(settings) ||
            !EnsureResources(inputs))
        {
            return {};
        }

        const float rayDistance = ResolveRayVisibilityMaxDistance(
            settings.maxDistance,
            sceneDiagonal);
        const float3 propagationDirection = float3(light->GetDirection());
        const float directionLengthSquared =
            dot(propagationDirection, propagationDirection);
        if (!std::isfinite(rayDistance) || !(rayDistance > 0.f) ||
            !std::isfinite(directionLengthSquared) ||
            !(directionLengthSquared > 1.e-12f))
        {
            return {};
        }

        const uint32_t receiverSampleCount =
            inputs.depth->getDesc().sampleCount;
        const uint32_t variant = FindVariant(receiverSampleCount);
        if (variant >= m_Pipelines.size() || !m_Pipelines[variant] ||
            !EnsureBindingSet(
                variant,
                inputs,
                materialVisibility,
                worldTlas))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Directional ray visibility received incompatible resources");
                m_ReportedInvalidInput = true;
            }
            return {};
        }
        m_ReportedInvalidInput = false;

        DirectionalRayVisibilityConstants constants{};
        view.FillPlanarViewConstants(constants.view);
        const float3 directionToLight =
            -propagationDirection / std::sqrt(directionLengthSquared);
        constants.directionToLightAndDistance = {
            directionToLight.x,
            directionToLight.y,
            directionToLight.z,
            rayDistance };
        constants.rayBias = settings.rayBias;
        constants.depthQuantizationStep =
            GetDepthQuantizationStep(inputs.depth->getDesc().format);
        constants.reverseDepth = view.IsReverseDepth() ? 1u : 0u;
        constants.floatDepth = IsFloatingPointDepth(
            inputs.depth->getDesc().format) ? 1u : 0u;
        commandList->writeBuffer(
            m_ConstantBuffer,
            &constants,
            sizeof(constants));

        nvrhi::ComputeState state;
        state.pipeline = m_Pipelines[variant];
        state.bindings = {
            m_BindingSets[variant],
            materialVisibility.descriptorTable
        };
        commandList->beginMarker("Directional Ray Visibility");
        commandList->setComputeState(state);
        const nvrhi::Rect extent = view.GetViewExtent();
        commandList->dispatch(
            div_ceil(extent.width(), 8),
            div_ceil(extent.height(), 8));
        commandList->endMarker();
        return {
            m_Visibility,
            m_ClosestVisibility,
            m_ClosestHitDistance,
            light,
            receiverSampleCount,
            true
        };
    }

    void DirectionalRayVisibilityPass::ResetBindingCache()
    {
        ClearBindingSets();
        m_BoundTlas = nullptr;
        m_BoundInputs = {};
        m_BoundMaterialVisibility = {};
    }

    void DirectionalRayVisibilityPass::ClearBindingSets()
    {
        for (nvrhi::BindingSetHandle& bindingSet : m_BindingSets)
            bindingSet = nullptr;
    }
}
