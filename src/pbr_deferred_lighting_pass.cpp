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

#include <donut/core/log.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
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
static_assert(sizeof(PbrDeferredLightingConstants) ==
    sizeof(DeferredLightingConstants) + 16,
    "The UVSR deferred extension must occupy one constant-buffer register.");

namespace
{
    bool ValidateOutputs(
        const DeferredLightingPass::Inputs& inputs,
        nvrhi::ITexture* sourceRadianceOutput,
        bool writeSourceRadiance)
    {
        if (!inputs.output)
        {
            log::error("PbrDeferredLightingPass requires an HDR output texture.");
            return false;
        }

        if (!writeSourceRadiance)
            return true;

        if (!sourceRadianceOutput)
        {
            log::error("PbrDeferredLightingPass requires a source-radiance output texture.");
            return false;
        }

        if (inputs.output == sourceRadianceOutput)
        {
            log::error("PbrDeferredLightingPass HDR and source-radiance outputs must be distinct textures.");
            return false;
        }

        const nvrhi::TextureDesc& hdrDesc = inputs.output->getDesc();
        const nvrhi::TextureDesc& sourceDesc = sourceRadianceOutput->getDesc();
        if (hdrDesc.format != nvrhi::Format::RGBA16_FLOAT ||
            !hdrDesc.isUAV ||
            !hdrDesc.isShaderResource ||
            hdrDesc.sampleCount != 1)
        {
            log::error("PbrDeferredLightingPass HDR output must be a single-sample RGBA16_FLOAT UAV/SRV.");
            return false;
        }

        if (sourceDesc.format != nvrhi::Format::RGBA16_FLOAT ||
            !sourceDesc.isUAV ||
            !sourceDesc.isShaderResource ||
            sourceDesc.sampleCount != 1)
        {
            log::error("PbrDeferredLightingPass source-radiance output must be a single-sample RGBA16_FLOAT UAV/SRV.");
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
            log::error("PbrDeferredLightingPass outputs must have matching dimensions and subresource layout.");
            return false;
        }

        return true;
    }
}

PbrDeferredLightingPass::PbrDeferredLightingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<CommonRenderPasses> commonPasses)
    : m_Device(device)
    , m_BindingSets(device)
    , m_CommonPasses(std::move(commonPasses))
{
}

void PbrDeferredLightingPass::Init(const std::shared_ptr<ShaderFactory>& shaderFactory)
{
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
    constantBufferDesc.maxVersions = c_MaxRenderPassConstantBufferVersions;
    m_DeferredLightingCB = m_Device->createBuffer(constantBufferDesc);

    for (uint32_t variant = 0; variant < m_Pipelines.size(); ++variant)
    {
        const bool writeSourceRadiance = variant != 0u;
        const bool writeBounceMetadata = variant == 2u;
        Pipeline& pipeline = m_Pipelines[variant];

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(8),
            nvrhi::BindingLayoutItem::Texture_SRV(9),
            nvrhi::BindingLayoutItem::Texture_SRV(10),
            nvrhi::BindingLayoutItem::Texture_SRV(11),
            nvrhi::BindingLayoutItem::Texture_SRV(12),
            nvrhi::BindingLayoutItem::Texture_SRV(14),
            nvrhi::BindingLayoutItem::Texture_SRV(15),
            nvrhi::BindingLayoutItem::Texture_SRV(16),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        if (writeSourceRadiance)
            layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Texture_UAV(1));
        layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(1));
        pipeline.bindingLayout = m_Device->createBindingLayout(layoutDesc);

        std::vector<ShaderMacro> macros;
        macros.emplace_back(
            "WRITE_SOURCE_RADIANCE", writeSourceRadiance ? "1" : "0");
        macros.emplace_back(
            "WRITE_BOUNCE_METADATA", writeBounceMetadata ? "1" : "0");
        pipeline.shader = shaderFactory->CreateShader(
            "uvsr/pbr_deferred_lighting_cs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Compute);

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = pipeline.shader;
        pipelineDesc.bindingLayouts = { pipeline.bindingLayout };
        pipeline.pso = m_Device->createComputePipeline(pipelineDesc);
    }
}

void PbrDeferredLightingPass::Render(
    nvrhi::ICommandList* commandList,
    const ICompositeView& compositeView,
    const DeferredLightingPass::Inputs& inputs,
    nvrhi::ITexture* sourceRadianceOutput,
    bool separateIndirect,
    bool writeSourceRadiance,
    bool writeBounceMetadata,
    bool includeEmissiveSource,
    float emissiveSourceGain,
    float2 randomOffset)
{
    assert(!writeBounceMetadata || writeSourceRadiance);
    if (!commandList ||
        !inputs.depth ||
        !inputs.gbufferNormals ||
        !inputs.gbufferDiffuse ||
        !inputs.gbufferSpecular ||
        !inputs.gbufferEmissive)
    {
        log::error("PbrDeferredLightingPass received incomplete G-buffer inputs.");
        return;
    }

    if (!ValidateOutputs(inputs, sourceRadianceOutput, writeSourceRadiance))
        return;

    commandList->beginMarker("PBR Deferred Lighting");

    PbrDeferredLightingConstants constants = {};
    DeferredLightingConstants& deferredConstants = constants.deferred;
    constants.separateIndirect = separateIndirect ? 1 : 0;
    constants.writeSourceRadiance = writeSourceRadiance ? 1 : 0;
    constants.includeEmissiveSource = includeEmissiveSource ? 1 : 0;
    constants.emissiveSourceGain = std::max(emissiveSourceGain, 0.f);
    deferredConstants.randomOffset = randomOffset;
    deferredConstants.noisePattern[0] = float4(0.059f, 0.529f, 0.176f, 0.647f);
    deferredConstants.noisePattern[1] = float4(0.765f, 0.294f, 0.882f, 0.412f);
    deferredConstants.noisePattern[2] = float4(0.235f, 0.706f, 0.118f, 0.588f);
    deferredConstants.noisePattern[3] = float4(0.941f, 0.471f, 0.824f, 0.353f);
    deferredConstants.ambientColorTop = float4(inputs.ambientColorTop, 0.f);
    deferredConstants.ambientColorBottom = float4(inputs.ambientColorBottom, 0.f);
    deferredConstants.indirectDiffuseScale = 1.f;
    deferredConstants.indirectSpecularScale = inputs.indirectSpecular ? 1.f : 0.f;

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
                    deferredConstants.shadowMapTextureSize =
                        float2(light->shadowMap->GetTextureSize());
                }
                else if (shadowMapTexture != light->shadowMap->GetTexture())
                {
                    log::error("All lights submitted to PbrDeferredLightingPass must use the same shadow-map texture.");
                    commandList->endMarker();
                    return;
                }
            }

            if (deferredConstants.numLights >= DEFERRED_MAX_LIGHTS)
            {
                log::warning("Maximum number of active lights (%d) exceeded in PbrDeferredLightingPass",
                    DEFERRED_MAX_LIGHTS);
                break;
            }

            LightConstants& lightConstants =
                deferredConstants.lights[deferredConstants.numLights];
            light->FillLightConstants(lightConstants);

            if (light->shadowMap)
            {
                const uint32_t cascadeCount = light->shadowMap->GetNumberOfCascades();
                if (cascadeCount > 4)
                {
                    log::warning("PbrDeferredLightingPass supports at most four cascades per light; extra cascades are ignored.");
                }

                for (uint32_t cascade = 0; cascade < std::min(cascadeCount, 4u); ++cascade)
                {
                    if (numShadows < DEFERRED_MAX_SHADOWS)
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
                    log::warning("PbrDeferredLightingPass supports at most four per-object shadows per light; extras are ignored.");
                }

                for (uint32_t perObjectShadow = 0;
                    perObjectShadow < std::min(perObjectShadowCount, 4u);
                    ++perObjectShadow)
                {
                    if (numShadows < DEFERRED_MAX_SHADOWS)
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

    for (uint viewIndex = 0;
        viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR);
        ++viewIndex)
    {
        const uint32_t pipelineIndex = writeBounceMetadata
            ? 2u
            : (writeSourceRadiance ? 1u : 0u);
        const Pipeline& pipeline = m_Pipelines[pipelineIndex];
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);
        const nvrhi::TextureSubresourceSet viewSubresources = view->GetSubresources();

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_DeferredLightingCB),
            nvrhi::BindingSetItem::Texture_SRV(0,
                shadowMapTexture ? shadowMapTexture : m_CommonPasses->m_BlackDepthStencilTexture2DArray.Get()),
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
                inputs.indirectDiffuse ? inputs.indirectDiffuse : m_CommonPasses->m_BlackTexture.Get(),
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(15,
                inputs.indirectSpecular ? inputs.indirectSpecular : m_CommonPasses->m_BlackTexture.Get(),
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(16,
                inputs.shadowChannels ? inputs.shadowChannels : m_CommonPasses->m_BlackTexture.Get()),
            nvrhi::BindingSetItem::Texture_UAV(0, inputs.output,
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Sampler(1, m_ShadowSamplerComparison)
        };
        if (writeSourceRadiance)
        {
            bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(
                1, sourceRadianceOutput, nvrhi::Format::UNKNOWN, viewSubresources));
        }

        nvrhi::BindingSetHandle bindingSet =
            m_BindingSets.GetOrCreateBindingSet(bindingSetDesc, pipeline.bindingLayout);

        view->FillPlanarViewConstants(deferredConstants.view);
        commandList->writeBuffer(m_DeferredLightingCB, &constants, sizeof(constants));

        nvrhi::ComputeState state;
        state.pipeline = pipeline.pso;
        state.bindings = { bindingSet };
        commandList->setComputeState(state);

        const nvrhi::Rect viewExtent = view->GetViewExtent();
        commandList->dispatch(
            div_ceil(viewExtent.width(), 16),
            div_ceil(viewExtent.height(), 16));
    }

    commandList->endMarker();
}

void PbrDeferredLightingPass::ResetBindingCache()
{
    m_BindingSets.Clear();
}
