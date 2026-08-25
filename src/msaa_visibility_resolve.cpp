#include "msaa_visibility_resolve.h"
#include "renderer_log.h"
#include "renderer_shader_factory.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

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
        const std::shared_ptr<RendererShaderFactory>& shaderFactory,
        bool deferPipelineCreation)
    {
        m_Pipelines = {};
        m_ShaderFactory = shaderFactory;
        m_PipelinePreparationStep = 0u;
        m_PipelinesReady = false;
        m_PipelinePreparationFailed = false;
        if (!deferPipelineCreation)
        {
            while (!PreparePipelinesStep())
            {
            }
        }
    }

    bool MsaaVisibilityResolvePass::PreparePipelinesStep()
    {
        if (m_PipelinesReady || m_PipelinePreparationFailed)
            return true;

        if (!m_Device || !m_ShaderFactory ||
            m_PipelinePreparationStep >= m_Pipelines.size())
        {
            m_Pipelines = {};
            m_ShaderFactory.reset();
            m_PipelinePreparationFailed = true;
            log::error(
                "MSAA visibility resolve pipeline preparation dependencies are unavailable.");
            return true;
        }

        const uint32_t variant = m_PipelinePreparationStep;
        const uint32_t sampleCount = 2u << variant;
        Pipeline candidate;

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        for (const uint32_t slot : MsaaVisibilityResolveBindingSlots)
        {
            layoutDesc.bindings.push_back(
                nvrhi::BindingLayoutItem::Texture_SRV(slot));
        }
        for (const uint32_t slot : MsaaVisibilityResolveBindingSlots)
        {
            layoutDesc.bindings.push_back(
                nvrhi::BindingLayoutItem::Texture_UAV(slot));
        }
        candidate.bindingLayout =
            m_Device->createBindingLayout(layoutDesc);

        std::vector<RendererShaderMacro> macros;
        macros.emplace_back(
            "MSAA_VISIBILITY_SAMPLES",
            std::to_string(sampleCount));
        candidate.shader = m_ShaderFactory->CreateShader(
            "uvsr/msaa_visibility_resolve_cs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Compute);

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = candidate.shader;
        pipelineDesc.bindingLayouts = { candidate.bindingLayout };
        if (candidate.shader && candidate.bindingLayout)
        {
            candidate.pso =
                m_Device->createComputePipeline(pipelineDesc);
        }
        const MsaaVisibilityResolvePipelineResources resources = {
            bool(m_Device),
            bool(m_ShaderFactory),
            bool(candidate.bindingLayout),
            bool(candidate.shader),
            bool(candidate.pso)
        };
        if (!resources.AreComplete())
        {
            m_Pipelines = {};
            m_ShaderFactory.reset();
            m_PipelinePreparationFailed = true;
            log::error(
                "MSAA visibility resolve pipeline preparation failed.");
            return true;
        }

        m_Pipelines[variant] = std::move(candidate);
        ++m_PipelinePreparationStep;
        m_PipelinesReady =
            m_PipelinePreparationStep == m_Pipelines.size();
        if (m_PipelinesReady)
            m_ShaderFactory.reset();
        return m_PipelinesReady;
    }

    bool MsaaVisibilityResolvePass::Render(
        nvrhi::ICommandList* commandList,
        const MsaaVisibilityResolveInputs& inputs,
        const MsaaVisibilityResolveOutputs& outputs,
        uint32_t sampleCount) const
    {
        if (!m_PipelinesReady)
        {
            log::error(
                "MSAA visibility resolve rendered before pipeline preparation completed.");
            return false;
        }

        const int pipelineIndex =
            GetPipelineIndex(sampleCount);
        if (!commandList || pipelineIndex < 0)
        {
            log::error(
                "MSAA visibility resolve requires a command list and "
                "a static 2x, 4x, 8x, or 16x sample count.");
            return false;
        }

        const auto inputTextures =
            GetMsaaVisibilityResolveInputTextures(inputs);
        const auto outputTextures =
            GetMsaaVisibilityResolveOutputTextures(outputs);
        if (std::any_of(
                inputTextures.begin(),
                inputTextures.end(),
                [](nvrhi::ITexture* texture) { return !texture; }) ||
            std::any_of(
                outputTextures.begin(),
                outputTextures.end(),
                [](nvrhi::ITexture* texture) { return !texture; }))
        {
            log::error(
                "MSAA visibility resolve requires all seven inputs and outputs.");
            return false;
        }
        std::array<nvrhi::TextureDesc,
            MsaaVisibilityResolveResourceCount> inputDescriptors;
        std::array<nvrhi::TextureDesc,
            MsaaVisibilityResolveResourceCount> outputDescriptors;
        for (std::size_t index = 0u; index < inputTextures.size(); ++index)
        {
            inputDescriptors[index] = inputTextures[index]->getDesc();
            outputDescriptors[index] = outputTextures[index]->getDesc();
        }
        if (!AreMsaaVisibilityResolveDescriptorsSupported(
                inputDescriptors,
                outputDescriptors,
                sampleCount))
        {
            log::error(
                "MSAA visibility resolve resources violate the exact 7-in/"
                "7-out extent, format, mip, slice, sample, or UAV contract.");
            return false;
        }

        const nvrhi::TextureDesc& extent = outputDescriptors[0];
        nvrhi::BindingSetDesc bindingSetDesc;
        for (std::size_t index = 0u; index < inputTextures.size(); ++index)
        {
            const uint32_t slot = MsaaVisibilityResolveBindingSlots[index];
            bindingSetDesc.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    slot,
                    inputTextures[index]));
        }
        for (std::size_t index = 0u; index < outputTextures.size(); ++index)
        {
            const uint32_t slot = MsaaVisibilityResolveBindingSlots[index];
            bindingSetDesc.bindings.push_back(
                nvrhi::BindingSetItem::Texture_UAV(
                    slot,
                    outputTextures[index]));
        }
        const Pipeline& pipeline =
            m_Pipelines[size_t(pipelineIndex)];
        if (!pipeline.bindingLayout || !pipeline.pso)
        {
            log::error(
                "MSAA visibility resolve requires a valid prepared pipeline.");
            return false;
        }
        nvrhi::BindingSetHandle bindingSet =
            m_Device->createBindingSet(
                bindingSetDesc,
                pipeline.bindingLayout);
        if (!bindingSet)
        {
            log::error(
                "MSAA visibility resolve could not create its binding set.");
            return false;
        }

        MsaaVisibilityResolveDispatchState dispatchState = {
            m_PipelinesReady,
            commandList != nullptr,
            true,
            bool(pipeline.pso),
            bool(bindingSet),
            false
        };
        if (!dispatchState.CanDispatch())
            return false;

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
        dispatchState.dispatchSubmitted = true;
        return dispatchState.CanPublish();
    }
}
