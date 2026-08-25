/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "pbr_deferred_lighting_pass.h"
#include "pbr_deferred_dispatch_contract.h"
#include "image_based_lighting_environment.h"
#include "pbr_deferred_lighting_bindings.h"
#include "renderer_common_passes.h"
#include "renderer_environment_bindings.h"
#include "renderer_log.h"
#include "renderer_shader_factory.h"

#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;
using namespace donut::render;

#include "pbr_deferred_lighting_cb.h"

static_assert(sizeof(PbrDeferredLightingConstants) % 16 == 0,
    "Deferred lighting constants must preserve HLSL constant-buffer alignment.");
static_assert(offsetof(PbrDeferredLightingConstants, separateIndirect) ==
    sizeof(DeferredLightingConstants),
    "The UVSR extension must follow Donut's deferred constants without padding drift.");
static_assert(sizeof(FlashlightBeamProfile) == 48u,
    "The flashlight profile must occupy three constant buffer registers.");
static_assert(sizeof(PbrDeferredLightingConstants) ==
    sizeof(DeferredLightingConstants) + 80,
    "The UVSR deferred extension must occupy five constant buffer registers.");
static_assert(offsetof(
        PbrDeferredLightingConstants,
        directVisibilityLightIndices) ==
    sizeof(DeferredLightingConstants) + 16u,
    "The direct visibility indices must begin the second UVSR register.");
static_assert(offsetof(
        PbrDeferredLightingConstants,
        flashlightLightIndex) ==
    sizeof(DeferredLightingConstants) + 24u,
    "The flashlight index must complete the second UVSR register.");
static_assert(offsetof(
        PbrDeferredLightingConstants,
        flashlightBeamProfile) ==
    sizeof(DeferredLightingConstants) + 32u,
    "The flashlight profile must begin the third UVSR register.");

namespace
{
    int GetMsaaPipelineIndex(uint32_t sampleCount)
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

    nvrhi::TextureHandle CreateNeutralMsaaVisibility(
        nvrhi::IDevice* device,
        uint32_t sampleCount)
    {
        nvrhi::TextureDesc description;
        description.width = 1u;
        description.height = 1u;
        description.arraySize = sampleCount;
        description.dimension = nvrhi::TextureDimension::Texture2DArray;
        description.format = nvrhi::Format::R8_UNORM;
        description.debugName = "Neutral MSAA Visibility";
        description.enableAutomaticStateTracking(
            nvrhi::ResourceStates::ShaderResource);
        return device ? device->createTexture(description) : nullptr;
    }

    bool ValidateOutputs(
        const DeferredLightingPass::Inputs& inputs,
        nvrhi::ITexture* sourceRadianceOutput,
        bool writeSourceRadiance)
    {
        if (!inputs.output)
        {
            uvsr::log::error("PbrDeferredLightingPass requires an HDR output texture.");
            return false;
        }

        if (!writeSourceRadiance)
            return true;

        if (!sourceRadianceOutput)
        {
            uvsr::log::error("PbrDeferredLightingPass requires a source-radiance output texture.");
            return false;
        }

        if (inputs.output == sourceRadianceOutput)
        {
            uvsr::log::error("PbrDeferredLightingPass HDR and source-radiance outputs must be distinct textures.");
            return false;
        }

        const nvrhi::TextureDesc& hdrDesc = inputs.output->getDesc();
        const nvrhi::TextureDesc& sourceDesc = sourceRadianceOutput->getDesc();
        if (hdrDesc.format != nvrhi::Format::RGBA16_FLOAT ||
            !hdrDesc.isUAV ||
            !hdrDesc.isShaderResource ||
            hdrDesc.sampleCount != 1)
        {
            uvsr::log::error("PbrDeferredLightingPass HDR output must be a single-sample RGBA16_FLOAT UAV/SRV.");
            return false;
        }

        if (sourceDesc.format != nvrhi::Format::RGBA16_FLOAT ||
            !sourceDesc.isUAV ||
            !sourceDesc.isShaderResource ||
            sourceDesc.sampleCount != 1)
        {
            uvsr::log::error("PbrDeferredLightingPass source-radiance output must be a single-sample RGBA16_FLOAT UAV/SRV.");
            return false;
        }

        if (sourceDesc.width != hdrDesc.width ||
            sourceDesc.height != hdrDesc.height ||
            sourceDesc.depth != hdrDesc.depth ||
            sourceDesc.arraySize != hdrDesc.arraySize ||
            sourceDesc.mipLevels != hdrDesc.mipLevels ||
            sourceDesc.sampleCount != hdrDesc.sampleCount ||
            sourceDesc.sampleQuality != hdrDesc.sampleQuality ||
            sourceDesc.dimension != hdrDesc.dimension)
        {
            uvsr::log::error("PbrDeferredLightingPass outputs must have matching dimensions and subresource layout.");
            return false;
        }

        return true;
    }

    bool ValidateMsaaInputs(
        const DeferredLightingPass::Inputs& inputs,
        nvrhi::ITexture* resolvedBackground,
        uint32_t sampleCount,
        nvrhi::ITexture* visibilityBaseLighting,
        nvrhi::ITexture* visibilityComposite)
    {
        if (GetMsaaPipelineIndex(sampleCount) < 0)
        {
            uvsr::log::error(
                "PbrDeferredLightingPass supports only 2x, 4x, 8x, or 16x diagnostic MSAA.");
            return false;
        }
        if (!inputs.output || !resolvedBackground)
        {
            uvsr::log::error(
                "PbrDeferredLightingPass MSAA requires resolved background and output textures.");
            return false;
        }

        const nvrhi::TextureDesc& outputDesc =
            inputs.output->getDesc();
        const nvrhi::TextureDesc& backgroundDesc =
            resolvedBackground->getDesc();
        if (outputDesc.format != nvrhi::Format::RGBA16_FLOAT ||
            outputDesc.sampleCount != 1u ||
            !outputDesc.isUAV ||
            backgroundDesc.format != nvrhi::Format::RGBA16_FLOAT ||
            backgroundDesc.sampleCount != 1u ||
            outputDesc.width != backgroundDesc.width ||
            outputDesc.height != backgroundDesc.height)
        {
            uvsr::log::error(
                "PbrDeferredLightingPass MSAA resolve surfaces must be matching single-sample RGBA16F textures and the output must be a UAV.");
            return false;
        }

        constexpr size_t gbufferInputCount = 6u;
        const nvrhi::ITexture* gbufferInputs[gbufferInputCount] = {
            inputs.depth,
            inputs.gbufferDiffuse,
            inputs.gbufferSpecular,
            inputs.gbufferNormals,
            inputs.gbufferEmissive,
            inputs.indirectDiffuse
        };
        for (const nvrhi::ITexture* texture : gbufferInputs)
        {
            if (!texture ||
                texture->getDesc().sampleCount != sampleCount ||
                texture->getDesc().width != outputDesc.width ||
                texture->getDesc().height != outputDesc.height)
            {
                uvsr::log::error(
                    "PbrDeferredLightingPass MSAA G-buffer inputs must match the selected sample count and output extent.");
                return false;
            }
        }

        if (bool(visibilityBaseLighting) !=
            bool(visibilityComposite))
        {
            uvsr::log::error(
                "PbrDeferredLightingPass MSAA visibility requires both "
                "the base and composited lighting surfaces.");
            return false;
        }
        if (visibilityBaseLighting)
        {
            const nvrhi::TextureDesc& baseDesc =
                visibilityBaseLighting->getDesc();
            const nvrhi::TextureDesc& compositeDesc =
                visibilityComposite->getDesc();
            if (baseDesc.format != nvrhi::Format::RGBA16_FLOAT ||
                compositeDesc.format !=
                    nvrhi::Format::RGBA16_FLOAT ||
                baseDesc.sampleCount != 1u ||
                compositeDesc.sampleCount != 1u ||
                baseDesc.width != outputDesc.width ||
                baseDesc.height != outputDesc.height ||
                compositeDesc.width != outputDesc.width ||
                compositeDesc.height != outputDesc.height)
            {
                uvsr::log::error(
                    "PbrDeferredLightingPass MSAA visibility surfaces "
                    "must be matching single-sample RGBA16F textures.");
                return false;
            }
        }
        return true;
    }

    bool IsDirectVisibilityTextureCompatible(
        const uvsr::DirectLightVisibility& visibility,
        const DeferredLightingPass::Inputs& inputs,
        uint32_t expectedReceiverSampleCount)
    {
        if (!visibility.IsComplete() ||
            visibility.receiverSampleCount != expectedReceiverSampleCount ||
            !inputs.output)
        {
            return false;
        }

        const nvrhi::TextureDesc& textureDesc =
            visibility.texture->getDesc();
        const nvrhi::TextureDesc& outputDesc = inputs.output->getDesc();
        return uvsr::IsDirectLightVisibilityTextureCompatible(
            {
                textureDesc.width,
                textureDesc.height,
                textureDesc.depth,
                textureDesc.arraySize,
                textureDesc.mipLevels,
                textureDesc.sampleCount,
                textureDesc.format == nvrhi::Format::R8_UNORM,
                textureDesc.dimension ==
                    nvrhi::TextureDimension::Texture2D,
                textureDesc.dimension ==
                    nvrhi::TextureDimension::Texture2DArray,
                textureDesc.isShaderResource
            },
            outputDesc.width,
            outputDesc.height,
            expectedReceiverSampleCount);
    }

    bool IsSkyVisibilityTextureCompatible(
        nvrhi::ITexture* texture,
        const DeferredLightingPass::Inputs& inputs,
        uint32_t expectedReceiverSampleCount)
    {
        if (!texture || !inputs.output)
            return false;

        const nvrhi::TextureDesc& textureDesc = texture->getDesc();
        const nvrhi::TextureDesc& outputDesc = inputs.output->getDesc();
        const bool topologyCompatible =
            expectedReceiverSampleCount == 1u
                ? textureDesc.dimension ==
                        nvrhi::TextureDimension::Texture2D &&
                    textureDesc.arraySize == 1u
                : textureDesc.dimension ==
                        nvrhi::TextureDimension::Texture2DArray &&
                    textureDesc.arraySize ==
                        expectedReceiverSampleCount;
        return uvsr::IsDirectLightReceiverSampleCountSupported(
                expectedReceiverSampleCount) &&
            textureDesc.width == outputDesc.width &&
            textureDesc.height == outputDesc.height &&
            textureDesc.depth == 1u &&
            textureDesc.mipLevels == 1u &&
            textureDesc.sampleCount == 1u &&
            (textureDesc.format == nvrhi::Format::R8_UNORM ||
                textureDesc.format == nvrhi::Format::R16_FLOAT ||
                textureDesc.format == nvrhi::Format::RGBA16_FLOAT) &&
            topologyCompatible &&
            textureDesc.isShaderResource;
    }
}

PbrDeferredLightingPass::PbrDeferredLightingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<uvsr::RendererCommonPasses> commonPasses)
    : m_Device(device)
    , m_BindingSets(device)
    , m_CommonPasses(std::move(commonPasses))
{
}

void PbrDeferredLightingPass::Init(
    const std::shared_ptr<uvsr::RendererShaderFactory>& shaderFactory,
    bool deferPipelineCreation)
{
    m_ShaderFactory = shaderFactory;
    m_PipelinePreparationStep = 0u;
    m_PipelinesReady = false;
    m_PipelinePreparationFailed = false;
    m_ResourcesValid = false;

    if (!m_Device || !m_ShaderFactory || !m_CommonPasses ||
        !m_CommonPasses->IsValid())
    {
        uvsr::log::error(
            "PbrDeferredLightingPass initialization dependencies are invalid.");
        m_PipelinePreparationFailed = true;
        return;
    }

    auto samplerDesc = nvrhi::SamplerDesc()
        .setAllAddressModes(nvrhi::SamplerAddressMode::Border)
        .setBorderColor(1.0f)
        .setReductionType(nvrhi::SamplerReductionType::Comparison);
    m_ShadowSamplerComparison = m_Device->createSampler(samplerDesc);

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(PbrDeferredLightingConstants);
    constantBufferDesc.debugName = "PbrDeferredLightingConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.maxVersions =
        uvsr::RendererMaxConstantBufferVersions;
    m_DeferredLightingCB = m_Device->createBuffer(constantBufferDesc);
    std::array<nvrhi::TextureHandle, 4> neutralMsaaVisibility;
    for (uint32_t index = 0u;
        index < neutralMsaaVisibility.size();
        ++index)
    {
        neutralMsaaVisibility[index] =
            CreateNeutralMsaaVisibility(
                m_Device,
                2u << index);
    }

    if (!m_ShadowSamplerComparison || !m_DeferredLightingCB ||
        std::any_of(
            neutralMsaaVisibility.begin(),
            neutralMsaaVisibility.end(),
            [](const nvrhi::TextureHandle& texture)
            {
                return !texture;
            }))
    {
        uvsr::log::error(
            "PbrDeferredLightingPass resource creation failed.");
        m_PipelinePreparationFailed = true;
        return;
    }

    nvrhi::CommandListHandle uploadCommandList =
        m_Device->createCommandList();
    if (!uploadCommandList)
    {
        uvsr::log::error(
            "PbrDeferredLightingPass neutral-visibility upload failed.");
        m_PipelinePreparationFailed = true;
        return;
    }
    uploadCommandList->open();
    for (uint32_t textureIndex = 0u;
        textureIndex < neutralMsaaVisibility.size();
        ++textureIndex)
    {
        const uint32_t sampleCount =
            uvsr::PbrMsaaSampleCounts[textureIndex];
        for (uint32_t slice = 0u; slice < sampleCount; ++slice)
        {
            uploadCommandList->writeTexture(
                neutralMsaaVisibility[textureIndex],
                slice,
                0u,
                &uvsr::PbrNeutralVisibilityByte,
                0u);
        }
        uploadCommandList->setPermanentTextureState(
            neutralMsaaVisibility[textureIndex],
            nvrhi::ResourceStates::ShaderResource);
    }
    uploadCommandList->commitBarriers();
    uploadCommandList->close();
    m_Device->executeCommandList(uploadCommandList);
    m_NeutralMsaaVisibility = std::move(neutralMsaaVisibility);
    m_ResourcesValid = true;

    if (!deferPipelineCreation)
    {
        while (!PreparePipelinesStep())
        {
        }
    }
}

bool PbrDeferredLightingPass::PreparePipelinesStep()
{
    if (m_PipelinesReady || m_PipelinePreparationFailed)
        return true;
    if (!m_ResourcesValid)
    {
        m_PipelinePreparationFailed = true;
        return true;
    }

    if (m_PipelinePreparationStep < m_Pipelines.size())
    {
        const uint32_t variant = m_PipelinePreparationStep;
        const bool writeSourceRadiance = variant != 0u;
        Pipeline& pipeline = m_Pipelines[variant];

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(
                uvsr::PbrDeferredDiffuseEnvironmentSlot),
            nvrhi::BindingLayoutItem::Texture_SRV(
                uvsr::PbrDeferredSpecularEnvironmentSlot),
            nvrhi::BindingLayoutItem::Texture_SRV(
                uvsr::PbrDeferredEnvironmentBrdfSlot),
            nvrhi::BindingLayoutItem::Texture_SRV(8),
            nvrhi::BindingLayoutItem::Texture_SRV(9),
            nvrhi::BindingLayoutItem::Texture_SRV(10),
            nvrhi::BindingLayoutItem::Texture_SRV(11),
            nvrhi::BindingLayoutItem::Texture_SRV(12),
            nvrhi::BindingLayoutItem::Texture_SRV(14),
            nvrhi::BindingLayoutItem::Texture_SRV(
                uvsr::PbrFlashlightVisibilitySlot),
            nvrhi::BindingLayoutItem::Texture_SRV(
                uvsr::PbrSunVisibilitySlot),
            nvrhi::BindingLayoutItem::Texture_SRV(
                uvsr::PbrSkyVisibilitySlot),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        if (writeSourceRadiance)
        {
            layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Texture_UAV(1));
        }
        layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(1));
        layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(2));
        layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(3));
        nvrhi::BindingLayoutHandle bindingLayout =
            m_Device->createBindingLayout(layoutDesc);

        std::vector<uvsr::RendererShaderMacro> macros;
        macros.emplace_back(
            "WRITE_SOURCE_RADIANCE", writeSourceRadiance ? "1" : "0");
        nvrhi::ShaderHandle shader = m_ShaderFactory->CreateShader(
            "uvsr/pbr_deferred_lighting_cs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Compute);

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = shader;
        pipelineDesc.bindingLayouts = { bindingLayout };
        nvrhi::ComputePipelineHandle pso;
        if (shader && bindingLayout)
            pso = m_Device->createComputePipeline(pipelineDesc);
        if (!shader || !bindingLayout || !pso)
        {
            uvsr::log::error(
                "PbrDeferredLightingPass pipeline creation failed.");
            m_PipelinePreparationFailed = true;
            return true;
        }
        pipeline = { shader, pso, bindingLayout };
    }
    else
    {
        const uint32_t msaaStep =
            m_PipelinePreparationStep -
            static_cast<uint32_t>(m_Pipelines.size());
        const uint32_t sampleVariant =
            msaaStep %
            static_cast<uint32_t>(m_MsaaPipelines[0].size());
        const uint32_t visibilityVariant =
            msaaStep /
            static_cast<uint32_t>(m_MsaaPipelines[0].size());
        const uint32_t sampleCount = 2u << sampleVariant;
        Pipeline& pipeline =
            m_MsaaPipelines[visibilityVariant][sampleVariant];

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrDeferredDiffuseEnvironmentSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrDeferredSpecularEnvironmentSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrDeferredEnvironmentBrdfSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(8),
                nvrhi::BindingLayoutItem::Texture_SRV(9),
                nvrhi::BindingLayoutItem::Texture_SRV(10),
                nvrhi::BindingLayoutItem::Texture_SRV(11),
                nvrhi::BindingLayoutItem::Texture_SRV(12),
                nvrhi::BindingLayoutItem::Texture_SRV(14),
                nvrhi::BindingLayoutItem::Texture_SRV(17),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrFlashlightVisibilitySlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrSunVisibilitySlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrSkyVisibilitySlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrFlashlightRawClosestSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrFlashlightDenoisedClosestSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrSkyRawClosestSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrSkyDenoisedClosestSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrSunRawClosestSlot),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    uvsr::PbrSunDenoisedClosestSlot),
                nvrhi::BindingLayoutItem::Texture_UAV(0),
                nvrhi::BindingLayoutItem::Sampler(1),
                nvrhi::BindingLayoutItem::Sampler(2),
                nvrhi::BindingLayoutItem::Sampler(3)
        };
        if (visibilityVariant != 0u)
        {
            layoutDesc.bindings.push_back(
                nvrhi::BindingLayoutItem::Texture_SRV(18));
            layoutDesc.bindings.push_back(
                nvrhi::BindingLayoutItem::Texture_SRV(19));
        }
        nvrhi::BindingLayoutHandle bindingLayout =
            m_Device->createBindingLayout(layoutDesc);

        std::vector<uvsr::RendererShaderMacro> macros;
        macros.emplace_back(
            "PBR_DEFERRED_MSAA_SAMPLES",
            std::to_string(sampleCount));
        macros.emplace_back(
            "PBR_DEFERRED_MSAA_VISIBILITY",
            visibilityVariant != 0u ? "1" : "0");
        nvrhi::ShaderHandle shader = m_ShaderFactory->CreateShader(
            "uvsr/pbr_deferred_lighting_msaa_cs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Compute);

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = shader;
        pipelineDesc.bindingLayouts = { bindingLayout };
        nvrhi::ComputePipelineHandle pso;
        if (shader && bindingLayout)
            pso = m_Device->createComputePipeline(pipelineDesc);
        if (!shader || !bindingLayout || !pso)
        {
            uvsr::log::error(
                "PbrDeferredLightingPass MSAA pipeline creation failed.");
            m_PipelinePreparationFailed = true;
            return true;
        }
        pipeline = { shader, pso, bindingLayout };
    }

    ++m_PipelinePreparationStep;
    constexpr uint32_t pipelineCount = 2u + 2u * 4u;
    m_PipelinesReady = m_PipelinePreparationStep == pipelineCount;
    return m_PipelinesReady;
}

uvsr::PbrDeferredLightingRenderResult PbrDeferredLightingPass::Render(
    nvrhi::ICommandList* commandList,
    const ICompositeView& compositeView,
    const DeferredLightingPass::Inputs& inputs,
    const uvsr::DirectLightVisibilities& directLightVisibilities,
    const Light* flashlight,
    const FlashlightBeamProfile& flashlightBeamProfile,
    const uvsr::ImageBasedLightingProbe* environment,
    nvrhi::ITexture* skyVisibility,
    nvrhi::ITexture* rawClosestSkyVisibility,
    nvrhi::ITexture* denoisedClosestSkyVisibility,
    uint32_t skyVisibilityReceiverSampleCount,
    bool applySkyVisibilityToDiffuseIbl,
    bool applySkyVisibilityToSpecularIbl,
    nvrhi::ITexture* sourceRadianceOutput,
    bool separateIndirect,
    bool writeSourceRadiance,
    uint32_t lightingDebugView,
    uint32_t visibilityDebugView,
    float2 randomOffset,
    nvrhi::ITexture* resolvedBackground,
    uint32_t msaaSampleCount,
    nvrhi::ITexture* visibilityBaseLighting,
    nvrhi::ITexture* visibilityComposite)
{
    if (!m_PipelinesReady)
    {
        uvsr::log::error(
            "PbrDeferredLightingPass rendered before pipeline preparation completed.");
        return {};
    }
    const bool msaa = msaaSampleCount > 1u;
    if (!commandList ||
        !inputs.depth ||
        !inputs.gbufferNormals ||
        !inputs.gbufferDiffuse ||
        !inputs.gbufferSpecular ||
        !inputs.gbufferEmissive)
    {
        uvsr::log::error("PbrDeferredLightingPass received incomplete G-buffer inputs.");
        return {};
    }

    const bool applyMsaaVisibility =
        visibilityBaseLighting &&
        visibilityComposite;
    if (msaa &&
        (writeSourceRadiance ||
            (separateIndirect &&
                !applyMsaaVisibility)))
    {
        uvsr::log::error(
            "Diagnostic deferred MSAA cannot consume visibility history or emit source-radiance metadata.");
        return {};
    }
    if (msaa
            ? !ValidateMsaaInputs(
                  inputs,
                  resolvedBackground,
                  msaaSampleCount,
                  visibilityBaseLighting,
                  visibilityComposite)
            : !ValidateOutputs(
                  inputs,
                  sourceRadianceOutput,
                  writeSourceRadiance))
    {
        return {};
    }

    uvsr::DirectLightVisibilities activeVisibilities;
    const auto acceptVisibility = [&](
        const uvsr::DirectLightVisibility& visibility,
        uint32_t expectedReceiverSampleCount)
    {
        return IsDirectVisibilityTextureCompatible(
                visibility,
                inputs,
                expectedReceiverSampleCount)
            ? visibility
            : uvsr::DirectLightVisibility{};
    };
    const uint32_t expectedReceiverSampleCount =
        msaa ? msaaSampleCount : 1u;
    const auto hasVisibilityInput = [](
        const uvsr::DirectLightVisibility& visibility)
    {
        return visibility.texture || visibility.light ||
            visibility.rawClosestTexture ||
            visibility.denoisedClosestTexture;
    };
    for (const uvsr::DirectLightVisibility* visibility : {
            &directLightVisibilities.flashlight,
            &directLightVisibilities.sun })
    {
        if (hasVisibilityInput(*visibility) &&
            !IsDirectVisibilityTextureCompatible(
                *visibility, inputs, expectedReceiverSampleCount))
        {
            uvsr::log::error(
                "PbrDeferredLightingPass rejected an incompatible active direct-light visibility input.");
            return {};
        }
    }
    activeVisibilities.flashlight = acceptVisibility(
        directLightVisibilities.flashlight,
        expectedReceiverSampleCount);
    activeVisibilities.sun = acceptVisibility(
        directLightVisibilities.sun,
        expectedReceiverSampleCount);
    const auto acceptClosest = [&](nvrhi::ITexture* texture)
    {
        return msaa && IsSkyVisibilityTextureCompatible(
            texture, inputs, 1u)
            ? texture
            : nullptr;
    };
    nvrhi::ITexture* activeFlashlightRawClosest = acceptClosest(
        activeVisibilities.flashlight.rawClosestTexture);
    nvrhi::ITexture* activeFlashlightDenoisedClosest = acceptClosest(
        activeVisibilities.flashlight.denoisedClosestTexture);
    if (!activeFlashlightRawClosest ||
        !activeFlashlightDenoisedClosest)
    {
        if (msaa && activeVisibilities.flashlight.texture)
        {
            uvsr::log::error(
                "PbrDeferredLightingPass requires the active MSAA flashlight closest-surface pair.");
            return {};
        }
        activeFlashlightRawClosest = nullptr;
        activeFlashlightDenoisedClosest = nullptr;
    }
    nvrhi::ITexture* activeSunRawClosest = acceptClosest(
        activeVisibilities.sun.rawClosestTexture);
    nvrhi::ITexture* activeSunDenoisedClosest = acceptClosest(
        activeVisibilities.sun.denoisedClosestTexture);
    if (!activeSunRawClosest || !activeSunDenoisedClosest)
    {
        if (msaa && activeVisibilities.sun.texture)
        {
            uvsr::log::error(
                "PbrDeferredLightingPass requires the active MSAA sun closest-surface pair.");
            return {};
        }
        activeSunRawClosest = nullptr;
        activeSunDenoisedClosest = nullptr;
    }
    const bool hasSkyVisibilityConsumer =
        PbrNeedsSkyVisibilitySample(
            lightingDebugView,
            applySkyVisibilityToDiffuseIbl,
            applySkyVisibilityToSpecularIbl);
    nvrhi::ITexture* activeSkyVisibility =
        hasSkyVisibilityConsumer &&
        IsSkyVisibilityTextureCompatible(
            skyVisibility,
            inputs,
            msaa ? msaaSampleCount : 1u) &&
        skyVisibilityReceiverSampleCount ==
            (msaa ? msaaSampleCount : 1u)
            ? skyVisibility
            : nullptr;
    if (hasSkyVisibilityConsumer && skyVisibility &&
        !activeSkyVisibility)
    {
        uvsr::log::error(
            "PbrDeferredLightingPass rejected an incompatible active sky visibility input.");
        return {};
    }
    nvrhi::ITexture* activeSkyRawClosest = acceptClosest(
        rawClosestSkyVisibility);
    nvrhi::ITexture* activeSkyDenoisedClosest = acceptClosest(
        denoisedClosestSkyVisibility);
    if (!activeSkyRawClosest || !activeSkyDenoisedClosest)
    {
        if (msaa && activeSkyVisibility)
        {
            uvsr::log::error(
                "PbrDeferredLightingPass requires the active MSAA sky closest-surface pair.");
            return {};
        }
        activeSkyRawClosest = nullptr;
        activeSkyDenoisedClosest = nullptr;
    }
    nvrhi::ITexture* neutralVisibility =
        m_CommonPasses->WhiteTexture();
    if (msaa)
    {
        const int sampleIndex = GetMsaaPipelineIndex(msaaSampleCount);
        neutralVisibility = sampleIndex >= 0
            ? m_NeutralMsaaVisibility[size_t(sampleIndex)].Get()
            : nullptr;
        if (!neutralVisibility)
        {
            uvsr::log::error(
                "PbrDeferredLightingPass could not bind neutral per-sample visibility.");
            return {};
        }
    }

    const uint32_t viewCount =
        compositeView.GetNumChildViews(ViewType::PLANAR);
    uvsr::PbrDeferredLightingRenderTransaction transaction(viewCount);
    if (viewCount == 0u)
    {
        uvsr::log::error(
            "PbrDeferredLightingPass received no planar output views.");
        transaction.MarkFailed();
        return transaction.Finish();
    }

    commandList->beginMarker(
        msaa
            ? "PBR Deferred MSAA Per-Sample Lighting"
            : "PBR Deferred Lighting");

    PbrDeferredLightingConstants constants = {};
    DeferredLightingConstants& deferredConstants = constants.deferred;
    constants.separateIndirect = separateIndirect ? 1 : 0;
    constants.lightingDebugView = lightingDebugView;
    constants.visibilityDebugView = visibilityDebugView;
    constants.skyVisibilityApplication =
        uvsr::ResolveSkyVisibilityApplication(
            activeSkyVisibility != nullptr,
            applySkyVisibilityToDiffuseIbl,
            applySkyVisibilityToSpecularIbl);
    constants.directVisibilityLightIndices = { -1, -1 };
    constants.flashlightLightIndex = -1;
    deferredConstants.randomOffset = { randomOffset.x, randomOffset.y };
    deferredConstants.noisePattern[0] = {
        0.059f, 0.529f, 0.176f, 0.647f };
    deferredConstants.noisePattern[1] = {
        0.765f, 0.294f, 0.882f, 0.412f };
    deferredConstants.noisePattern[2] = {
        0.235f, 0.706f, 0.118f, 0.588f };
    deferredConstants.noisePattern[3] = {
        0.941f, 0.471f, 0.824f, 0.353f };
    deferredConstants.numLightProbes =
        environment && environment->IsActive() ? 1u : 0u;
    if (deferredConstants.numLightProbes > 0u)
    {
        environment->FillLightProbeConstants(
            deferredConstants.lightProbes[0]);
    }
    deferredConstants.indirectDiffuseScale = 1.f;

    nvrhi::ITexture* shadowMapTexture = nullptr;
    int numShadows = 0;

    if (inputs.lights)
    {
        for (const auto& light : *inputs.lights)
        {
            if (light->shadowMap)
            {
                if (!shadowMapTexture)
                {
                    shadowMapTexture = light->shadowMap->GetTexture();
                    const auto shadowMapSize =
                        light->shadowMap->GetTextureSize();
                    deferredConstants.shadowMapTextureSize = {
                        float(shadowMapSize.x), float(shadowMapSize.y) };
                }
                else if (shadowMapTexture != light->shadowMap->GetTexture())
                {
                    uvsr::log::error("All lights submitted to PbrDeferredLightingPass must use the same shadow-map texture.");
                    commandList->endMarker();
                    transaction.MarkFailed();
                    return transaction.Finish();
                }
            }

            if (deferredConstants.numLights >= UVSR_DEFERRED_MAX_LIGHTS)
            {
                uvsr::log::warning("Maximum number of active lights (%d) exceeded in PbrDeferredLightingPass",
                    UVSR_DEFERRED_MAX_LIGHTS);
                break;
            }

            LightConstants& lightConstants =
                deferredConstants.lights[deferredConstants.numLights];
            light->FillLightConstants(lightConstants);
            if (uvsr::TargetsDirectLight(
                    activeVisibilities.flashlight, light.get()))
            {
                constants.directVisibilityLightIndices.x =
                    int(deferredConstants.numLights);
            }
            if (uvsr::TargetsDirectLight(
                    activeVisibilities.sun, light.get()))
            {
                constants.directVisibilityLightIndices.y =
                    int(deferredConstants.numLights);
            }
            if (flashlight && light.get() == flashlight)
            {
                constants.flashlightLightIndex =
                    int(deferredConstants.numLights);
                constants.flashlightBeamProfile =
                    flashlightBeamProfile;
            }

            if (light->shadowMap)
            {
                const uint32_t cascadeCount = light->shadowMap->GetNumberOfCascades();
                if (cascadeCount > 4)
                {
                    uvsr::log::warning("PbrDeferredLightingPass supports at most four cascades per light; extra cascades are ignored.");
                }

                for (uint32_t cascade = 0; cascade < std::min(cascadeCount, 4u); ++cascade)
                {
                    if (numShadows < UVSR_DEFERRED_MAX_SHADOWS)
                    {
                        light->shadowMap->GetCascade(cascade)->FillShadowConstants(
                            deferredConstants.shadows[numShadows]);
                        lightConstants.shadowCascades[cascade] = numShadows++;
                    }
                }

                const uint32_t perObjectShadowCount =
                    light->shadowMap->GetNumberOfPerObjectShadows();
                if (perObjectShadowCount > 4)
                {
                    uvsr::log::warning("PbrDeferredLightingPass supports at most four per-object shadows per light; extras are ignored.");
                }

                for (uint32_t perObjectShadow = 0;
                    perObjectShadow < std::min(perObjectShadowCount, 4u);
                    ++perObjectShadow)
                {
                    if (numShadows < UVSR_DEFERRED_MAX_SHADOWS)
                    {
                        light->shadowMap->GetPerObjectShadow(perObjectShadow)->FillShadowConstants(
                            deferredConstants.shadows[numShadows]);
                        lightConstants.perObjectShadows[perObjectShadow] = numShadows++;
                    }
                }
            }

            ++deferredConstants.numLights;
        }
    }

    for (uint viewIndex = 0; viewIndex < viewCount; ++viewIndex)
    {
        const uint32_t pipelineIndex = writeSourceRadiance ? 1u : 0u;
        const Pipeline& pipeline = msaa
            ? m_MsaaPipelines[
                  applyMsaaVisibility ? 1u : 0u][size_t(
                  GetMsaaPipelineIndex(msaaSampleCount))]
            : m_Pipelines[pipelineIndex];
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);
        if (!view)
        {
            uvsr::log::error(
                "PbrDeferredLightingPass received a null planar output view.");
            commandList->endMarker();
            transaction.MarkFailed();
            return transaction.Finish();
        }
        const nvrhi::TextureSubresourceSet viewSubresources = view->GetSubresources();

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_DeferredLightingCB),
            nvrhi::BindingSetItem::Texture_SRV(0,
                shadowMapTexture
                    ? shadowMapTexture
                    : m_CommonPasses->BlackDepthArray()),
            nvrhi::BindingSetItem::Texture_SRV(8, inputs.depth,
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(9, inputs.gbufferDiffuse,
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(10, inputs.gbufferSpecular,
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(11, inputs.gbufferNormals,
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(12, inputs.gbufferEmissive,
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(14,
                inputs.indirectDiffuse
                    ? inputs.indirectDiffuse
                    : m_CommonPasses->BlackTexture(),
                nvrhi::Format::UNKNOWN, viewSubresources)
        };
        uvsr::PbrDeferredEnvironmentResources<nvrhi::ITexture*>
            activeEnvironmentResources;
        activeEnvironmentResources.diffuseEnvironment =
            environment && environment->diffuseMap
                ? environment->diffuseMap.Get()
                : nullptr;
        activeEnvironmentResources.specularEnvironment =
            environment && environment->specularMap
                ? environment->specularMap.Get()
                : nullptr;
        activeEnvironmentResources.environmentBrdf =
            environment && environment->environmentBrdf
                ? environment->environmentBrdf.Get()
                : nullptr;
        const auto environmentResources =
            uvsr::ResolvePbrDeferredEnvironmentResources(
                activeEnvironmentResources,
                m_CommonPasses->BlackCubeArray(),
                m_CommonPasses->BlackTexture());
        for (const auto& binding :
            uvsr::MakePbrDeferredEnvironmentBindings(
                environmentResources))
        {
            bindingSetDesc.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    binding.slot, binding.resource));
        }
        if (msaa)
        {
            bindingSetDesc.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    17,
                    resolvedBackground,
                    nvrhi::Format::UNKNOWN,
                    viewSubresources));
            if (applyMsaaVisibility)
            {
                bindingSetDesc.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_SRV(
                        18,
                        visibilityBaseLighting,
                        nvrhi::Format::UNKNOWN,
                        viewSubresources));
                bindingSetDesc.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_SRV(
                        19,
                        visibilityComposite,
                        nvrhi::Format::UNKNOWN,
                        viewSubresources));
            }
        }
        else
        {
            if (writeSourceRadiance)
            {
                bindingSetDesc.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_UAV(
                        1,
                        sourceRadianceOutput,
                        nvrhi::Format::UNKNOWN,
                        viewSubresources));
            }
        }
        const auto visibilityResources =
            uvsr::ResolvePbrVisibilityResources(
                uvsr::PbrVisibilityResources<nvrhi::ITexture*>{
                    activeVisibilities.flashlight.texture,
                    activeVisibilities.sun.texture,
                    activeSkyVisibility
                },
                neutralVisibility);
        bindingSetDesc.bindings.push_back(
            nvrhi::BindingSetItem::Texture_SRV(
                uvsr::PbrFlashlightVisibilitySlot,
                visibilityResources.flashlight));
        bindingSetDesc.bindings.push_back(
            nvrhi::BindingSetItem::Texture_SRV(
                uvsr::PbrSunVisibilitySlot,
                visibilityResources.sun));
        bindingSetDesc.bindings.push_back(
            nvrhi::BindingSetItem::Texture_SRV(
                uvsr::PbrSkyVisibilitySlot,
                visibilityResources.sky));
        if (msaa)
        {
            const auto closestResources =
                uvsr::ResolvePbrClosestVisibilityResources(
                    uvsr::PbrClosestVisibilityResources<nvrhi::ITexture*>{
                        activeFlashlightRawClosest,
                        activeFlashlightDenoisedClosest,
                        activeSunRawClosest,
                        activeSunDenoisedClosest,
                        activeSkyRawClosest,
                        activeSkyDenoisedClosest
                    },
                    m_CommonPasses->WhiteTexture());
            bindingSetDesc.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    uvsr::PbrFlashlightRawClosestSlot,
                    closestResources.flashlightRaw));
            bindingSetDesc.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    uvsr::PbrFlashlightDenoisedClosestSlot,
                    closestResources.flashlightDenoised));
            bindingSetDesc.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    uvsr::PbrSunRawClosestSlot,
                    closestResources.sunRaw));
            bindingSetDesc.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    uvsr::PbrSunDenoisedClosestSlot,
                    closestResources.sunDenoised));
            bindingSetDesc.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    uvsr::PbrSkyRawClosestSlot,
                    closestResources.skyRaw));
            bindingSetDesc.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    uvsr::PbrSkyDenoisedClosestSlot,
                    closestResources.skyDenoised));
        }
        bindingSetDesc.bindings.push_back(
            nvrhi::BindingSetItem::Texture_UAV(
                0, inputs.output, nvrhi::Format::UNKNOWN, viewSubresources));
        bindingSetDesc.bindings.push_back(
            nvrhi::BindingSetItem::Sampler(
                1, m_ShadowSamplerComparison));
        bindingSetDesc.bindings.push_back(
            nvrhi::BindingSetItem::Sampler(
                2, m_CommonPasses->LinearWrapSampler()));
        bindingSetDesc.bindings.push_back(
            nvrhi::BindingSetItem::Sampler(
                3, m_CommonPasses->LinearClampSampler()));

        nvrhi::BindingSetHandle bindingSet =
            m_BindingSets.GetOrCreateBindingSet(bindingSetDesc, pipeline.bindingLayout);

        const bool dispatched = uvsr::ExecutePbrDeferredLightingView(
            transaction,
            uvsr::PbrDeferredDispatchIsReady(
                bool(pipeline.pso),
                bool(pipeline.bindingLayout),
                bool(bindingSet),
                bool(m_DeferredLightingCB),
                bool(inputs.output)),
            [&]()
            {
                view->FillPlanarViewConstants(deferredConstants.view);
                commandList->writeBuffer(
                    m_DeferredLightingCB, &constants, sizeof(constants));

                nvrhi::ComputeState state;
                state.pipeline = pipeline.pso;
                state.bindings = { bindingSet };
                commandList->setComputeState(state);

                const nvrhi::Rect viewExtent = view->GetViewExtent();
                commandList->dispatch(
                    div_ceil(viewExtent.width(), 16),
                    div_ceil(viewExtent.height(), 16));
            });
        if (!dispatched)
        {
            uvsr::log::error(
                "PbrDeferredLightingPass binding allocation failed; "
                "deferred lighting was not dispatched.");
            commandList->endMarker();
            return transaction.Finish();
        }
    }

    commandList->endMarker();
    return transaction.Finish();
}

void PbrDeferredLightingPass::ResetBindingCache()
{
    m_BindingSets.Clear();
}
