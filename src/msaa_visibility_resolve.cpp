#include "msaa_visibility_resolve.h"

#include <donut/core/log.h>
#include <donut/engine/ShaderFactory.h>

#include <array>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::engine;

namespace
{
    int GetPipelineIndex(uint32_t sampleCount)
    {
        switch (sampleCount)
        {
        case 2u: return 0;
        case 4u: return 1;
        case 8u: return 2;
        case 16u: return 3;
        default: return -1;
        }
    }
}

namespace uvsr
{
    MsaaVisibilityResolvePass::MsaaVisibilityResolvePass(
        nvrhi::IDevice* device)
        : m_Device(device)
    {
    }

    void MsaaVisibilityResolvePass::Init(
        const std::shared_ptr<ShaderFactory>& shaderFactory)
    {
        for (uint32_t variant = 0u;
            variant < m_Pipelines.size();
            ++variant)
        {
            const uint32_t sampleCount = 2u << variant;
            Pipeline& pipeline = m_Pipelines[variant];

            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility = nvrhi::ShaderType::Compute;
            layoutDesc.bindings = {
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_SRV(3),
                nvrhi::BindingLayoutItem::Texture_SRV(4),
                nvrhi::BindingLayoutItem::Texture_SRV(5),
                nvrhi::BindingLayoutItem::Texture_SRV(6),
                nvrhi::BindingLayoutItem::Texture_UAV(0),
                nvrhi::BindingLayoutItem::Texture_UAV(1),
                nvrhi::BindingLayoutItem::Texture_UAV(2),
                nvrhi::BindingLayoutItem::Texture_UAV(3),
                nvrhi::BindingLayoutItem::Texture_UAV(4),
                nvrhi::BindingLayoutItem::Texture_UAV(5),
                nvrhi::BindingLayoutItem::Texture_UAV(6)
            };
            pipeline.bindingLayout =
                m_Device->createBindingLayout(layoutDesc);

            std::vector<ShaderMacro> macros;
            macros.emplace_back(
                "MSAA_VISIBILITY_SAMPLES",
                std::to_string(sampleCount));
            pipeline.shader = shaderFactory->CreateShader(
                "uvsr/msaa_visibility_resolve_cs.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Compute);

            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = pipeline.shader;
            pipelineDesc.bindingLayouts = {
                pipeline.bindingLayout
            };
            pipeline.pso =
                m_Device->createComputePipeline(pipelineDesc);
        }
    }

    void MsaaVisibilityResolvePass::Render(
        nvrhi::ICommandList* commandList,
        const MsaaVisibilityResolveInputs& inputs,
        const MsaaVisibilityResolveOutputs& outputs,
        uint32_t sampleCount) const
    {
        const int pipelineIndex =
            GetPipelineIndex(sampleCount);
        if (!commandList || pipelineIndex < 0)
        {
            log::error(
                "MSAA visibility resolve requires a command list and "
                "a static 2x, 4x, 8x, or 16x sample count.");
            return;
        }

        const std::array<nvrhi::ITexture*, 7> inputTextures = {
            inputs.depth,
            inputs.diffuse,
            inputs.material,
            inputs.normals,
            inputs.emissive,
            inputs.materialAmbientOcclusion,
            inputs.motionVectors
        };
        const std::array<nvrhi::ITexture*, 7> outputTextures = {
            outputs.depth,
            outputs.diffuse,
            outputs.material,
            outputs.normals,
            outputs.emissive,
            outputs.materialAmbientOcclusion,
            outputs.motionVectors
        };
        for (nvrhi::ITexture* texture : inputTextures)
        {
            if (!texture ||
                texture->getDesc().sampleCount != sampleCount)
            {
                log::error(
                    "MSAA visibility resolve inputs must all match "
                    "the selected multisample count.");
                return;
            }
        }
        for (nvrhi::ITexture* texture : outputTextures)
        {
            if (!texture ||
                texture->getDesc().sampleCount != 1u ||
                !texture->getDesc().isUAV)
            {
                log::error(
                    "MSAA visibility resolve outputs must be "
                    "single-sample UAV textures.");
                return;
            }
        }

        const nvrhi::TextureDesc& extent =
            outputs.depth->getDesc();
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.diffuse),
            nvrhi::BindingSetItem::Texture_SRV(2, inputs.material),
            nvrhi::BindingSetItem::Texture_SRV(3, inputs.normals),
            nvrhi::BindingSetItem::Texture_SRV(4, inputs.emissive),
            nvrhi::BindingSetItem::Texture_SRV(
                5, inputs.materialAmbientOcclusion),
            nvrhi::BindingSetItem::Texture_SRV(
                6, inputs.motionVectors),
            nvrhi::BindingSetItem::Texture_UAV(0, outputs.depth),
            nvrhi::BindingSetItem::Texture_UAV(1, outputs.diffuse),
            nvrhi::BindingSetItem::Texture_UAV(2, outputs.material),
            nvrhi::BindingSetItem::Texture_UAV(3, outputs.normals),
            nvrhi::BindingSetItem::Texture_UAV(4, outputs.emissive),
            nvrhi::BindingSetItem::Texture_UAV(
                5, outputs.materialAmbientOcclusion),
            nvrhi::BindingSetItem::Texture_UAV(
                6, outputs.motionVectors)
        };
        const Pipeline& pipeline =
            m_Pipelines[size_t(pipelineIndex)];
        nvrhi::BindingSetHandle bindingSet =
            m_Device->createBindingSet(
                bindingSetDesc,
                pipeline.bindingLayout);

        nvrhi::ComputeState state;
        state.pipeline = pipeline.pso;
        state.bindings = { bindingSet };
        commandList->beginMarker(
            "MSAA Closest-Surface Visibility Resolve");
        commandList->setComputeState(state);
        commandList->dispatch(
            (extent.width + 7u) / 8u,
            (extent.height + 7u) / 8u);
        commandList->endMarker();
    }
}
