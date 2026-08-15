#include "msaa_visibility_resolve.h"

#include <donut/core/log.h>
#include <donut/engine/ShaderFactory.h>

#include <array>
#include <cstddef>
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

    bool IsSupportedDepthFormat(nvrhi::Format format)
    {
        return format == nvrhi::Format::D16 ||
            format == nvrhi::Format::D24S8 ||
            format == nvrhi::Format::D32 ||
            format == nvrhi::Format::D32S8;
    }

    bool HasExpectedTextureTopology(
        const nvrhi::TextureDesc& description,
        uint32_t width,
        uint32_t height,
        uint32_t sampleCount,
        nvrhi::TextureDimension dimension)
    {
        return description.width == width &&
            description.height == height &&
            description.depth == 1u &&
            description.arraySize == 1u &&
            description.mipLevels == 1u &&
            description.sampleCount == sampleCount &&
            description.dimension == dimension;
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
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        bool deferPipelineCreation)
    {
        m_ShaderFactory = shaderFactory;
        m_PipelinePreparationStep = 0u;
        m_PipelinesReady = false;
        if (!deferPipelineCreation)
        {
            while (!PreparePipelinesStep())
            {
            }
        }
    }

    bool MsaaVisibilityResolvePass::PreparePipelinesStep()
    {
        if (m_PipelinesReady)
            return true;

        const uint32_t variant = m_PipelinePreparationStep;
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
        pipeline.shader = m_ShaderFactory->CreateShader(
            "uvsr/msaa_visibility_resolve_cs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Compute);

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = pipeline.shader;
        pipelineDesc.bindingLayouts = { pipeline.bindingLayout };
        pipeline.pso = m_Device->createComputePipeline(pipelineDesc);

        ++m_PipelinePreparationStep;
        m_PipelinesReady =
            m_PipelinePreparationStep == m_Pipelines.size();
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
        if (!inputs.depth)
        {
            log::error(
                "MSAA visibility resolve requires a depth input.");
            return false;
        }
        const nvrhi::TextureDesc& inputExtent =
            inputs.depth->getDesc();
        const std::array<nvrhi::Format, 6> expectedInputFormats = {
            nvrhi::Format::SRGBA8_UNORM,
            nvrhi::Format::RGBA8_UNORM,
            nvrhi::Format::RGBA16_SNORM,
            nvrhi::Format::RGBA16_FLOAT,
            nvrhi::Format::R8_UNORM,
            nvrhi::Format::RGBA16_FLOAT
        };
        for (std::size_t textureIndex = 0u;
            textureIndex < inputTextures.size();
            ++textureIndex)
        {
            nvrhi::ITexture* texture = inputTextures[textureIndex];
            if (!texture || !HasExpectedTextureTopology(
                    texture->getDesc(),
                    inputExtent.width,
                    inputExtent.height,
                    sampleCount,
                    nvrhi::TextureDimension::Texture2DMS) ||
                (textureIndex == 0u
                    ? !IsSupportedDepthFormat(texture->getDesc().format)
                    : texture->getDesc().format !=
                        expectedInputFormats[textureIndex - 1u]))
            {
                log::error(
                    "MSAA visibility resolve inputs must match the exact "
                    "2DMS extent, sample count, single-mip topology, and "
                    "G-buffer formats.");
                return false;
            }
        }
        const std::array<nvrhi::Format, 7> expectedOutputFormats = {
            nvrhi::Format::R32_FLOAT,
            nvrhi::Format::RGBA16_FLOAT,
            nvrhi::Format::RGBA16_FLOAT,
            nvrhi::Format::RGBA16_FLOAT,
            nvrhi::Format::RGBA16_FLOAT,
            nvrhi::Format::R16_FLOAT,
            nvrhi::Format::RGBA16_FLOAT
        };
        for (std::size_t textureIndex = 0u;
            textureIndex < outputTextures.size();
            ++textureIndex)
        {
            nvrhi::ITexture* texture = outputTextures[textureIndex];
            if (!texture || !HasExpectedTextureTopology(
                    texture->getDesc(),
                    inputExtent.width,
                    inputExtent.height,
                    1u,
                    nvrhi::TextureDimension::Texture2D) ||
                texture->getDesc().format !=
                    expectedOutputFormats[textureIndex] ||
                !texture->getDesc().isUAV)
            {
                log::error(
                    "MSAA visibility resolve outputs must match the input "
                    "extent and exact single-sample UAV topology and formats.");
                return false;
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
        return true;
    }
}
