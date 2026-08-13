#include "path_tracing_stable_plane_resolve_pass.h"

#include <donut/core/math/math.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>

#include <algorithm>

using namespace donut::engine;
using namespace donut::math;

#include "path_tracing_stable_plane_resolve_cb.h"

static_assert(sizeof(PathTracingStablePlaneResolveConstants) % 16u == 0u,
    "Stable-plane resolve constants must preserve HLSL alignment.");

namespace uvsr
{
    namespace
    {
        bool IsMatchingTexture2D(
            nvrhi::ITexture* texture,
            uint32_t width,
            uint32_t height)
        {
            if (!texture)
                return false;
            const nvrhi::TextureDesc& description = texture->getDesc();
            return description.dimension ==
                    nvrhi::TextureDimension::Texture2D &&
                description.sampleCount == 1u &&
                description.width == width &&
                description.height == height;
        }
    }

    PathTracingStablePlaneResolvePass::
        PathTracingStablePlaneResolvePass(
            nvrhi::IDevice* device,
            const std::shared_ptr<ShaderFactory>& shaderFactory)
        : m_Device(device)
    {
        if (!device || !shaderFactory)
            return;

        nvrhi::BufferDesc constantDescription;
        constantDescription.byteSize =
            sizeof(PathTracingStablePlaneResolveConstants);
        constantDescription.debugName =
            "PathTracingStablePlaneResolveConstants";
        constantDescription.isConstantBuffer = true;
        constantDescription.initialState =
            nvrhi::ResourceStates::ConstantBuffer;
        constantDescription.keepInitialState = true;
        m_ConstantBuffer = device->createBuffer(constantDescription);

        nvrhi::BindingLayoutDesc layoutDescription;
        layoutDescription.visibility = nvrhi::ShaderType::Compute;
        layoutDescription.bindings = {
            nvrhi::BindingLayoutItem::ConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_BindingLayout = device->createBindingLayout(layoutDescription);
        m_Shader = shaderFactory->CreateShader(
            "uvsr/path_tracing_stable_plane_resolve_cs.hlsl",
            "main",
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
    }

    bool PathTracingStablePlaneResolvePass::IsSupported() const
    {
        return m_Device && m_ConstantBuffer && m_BindingLayout &&
            m_Shader && m_Pipeline;
    }

    bool PathTracingStablePlaneResolvePass::EnsureBindingSet(
        const PathTracingStablePlaneResolveInputs& inputs)
    {
        const bool bindingChanged =
            m_BoundInputs.rawMean != inputs.rawMean ||
            m_BoundInputs.residualMean != inputs.residualMean ||
            m_BoundInputs.diffuseSuffixMean != inputs.diffuseSuffixMean ||
            m_BoundInputs.primaryNormalRoughness !=
                inputs.primaryNormalRoughness ||
            m_BoundInputs.primaryViewZ != inputs.primaryViewZ ||
            m_BoundInputs.output != inputs.output;
        if (bindingChanged)
        {
            ResetBindingCache();
            m_BoundInputs = inputs;
        }
        if (m_BindingSet)
            return true;

        nvrhi::BindingSetDesc description;
        description.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, inputs.rawMean),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.residualMean),
            nvrhi::BindingSetItem::Texture_SRV(
                2, inputs.diffuseSuffixMean),
            nvrhi::BindingSetItem::Texture_SRV(
                3, inputs.primaryNormalRoughness),
            nvrhi::BindingSetItem::Texture_SRV(4, inputs.primaryViewZ),
            nvrhi::BindingSetItem::Texture_UAV(0, inputs.output)
        };
        m_BindingSet = m_Device->createBindingSet(
            description,
            m_BindingLayout);
        return bool(m_BindingSet);
    }

    bool PathTracingStablePlaneResolvePass::Render(
        nvrhi::ICommandList* commandList,
        const PathTracingStablePlaneResolveInputs& inputs)
    {
        if (!IsSupported() || !commandList || !inputs.rawMean ||
            inputs.stablePlaneCount < 1u ||
            inputs.stablePlaneCount > 3u)
        {
            return false;
        }

        const nvrhi::TextureDesc& rawDescription =
            inputs.rawMean->getDesc();
        if (rawDescription.dimension !=
                nvrhi::TextureDimension::Texture2D ||
            rawDescription.sampleCount != 1u ||
            rawDescription.width == 0u || rawDescription.height == 0u ||
            !IsMatchingTexture2D(
                inputs.residualMean,
                rawDescription.width,
                rawDescription.height) ||
            !IsMatchingTexture2D(
                inputs.diffuseSuffixMean,
                rawDescription.width,
                rawDescription.height) ||
            !IsMatchingTexture2D(
                inputs.primaryNormalRoughness,
                rawDescription.width,
                rawDescription.height) ||
            !IsMatchingTexture2D(
                inputs.primaryViewZ,
                rawDescription.width,
                rawDescription.height) ||
            !IsMatchingTexture2D(
                inputs.output,
                rawDescription.width,
                rawDescription.height) ||
            !EnsureBindingSet(inputs))
        {
            return false;
        }

        PathTracingStablePlaneResolveConstants constants{};
        constants.extent = {
            rawDescription.width,
            rawDescription.height };
        constants.stablePlaneCount = inputs.stablePlaneCount;
        commandList->writeBuffer(
            m_ConstantBuffer,
            &constants,
            sizeof(constants));

        nvrhi::ComputeState state;
        state.pipeline = m_Pipeline;
        state.bindings = { m_BindingSet };
        commandList->beginMarker("Path Stable Plane Resolve");
        commandList->setComputeState(state);
        commandList->dispatch(
            div_ceil(rawDescription.width, 8u),
            div_ceil(rawDescription.height, 8u));
        commandList->endMarker();
        return true;
    }

    void PathTracingStablePlaneResolvePass::ResetBindingCache()
    {
        m_BindingSet = nullptr;
        m_BoundInputs = {};
    }
}
