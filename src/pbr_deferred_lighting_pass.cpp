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
#include <donut/engine/View.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "pbr_deferred_lighting_cb.h"

static_assert(sizeof(PbrDeferredLightingConstants) % 16 == 0,
    "Deferred lighting constants must preserve HLSL constant-buffer alignment.");
static_assert(offsetof(PbrDeferredLightingConstants, lightingDebugView) ==
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
    sizeof(DeferredLightingConstants) + 8u,
    "The direct visibility indices must complete the first UVSR register.");
static_assert(offsetof(
        PbrDeferredLightingConstants,
        flashlightLightIndex) ==
    sizeof(DeferredLightingConstants) + 16u,
    "The flashlight index must begin the second UVSR register.");
static_assert(offsetof(
        PbrDeferredLightingConstants,
        flashlightBeamProfile) ==
    sizeof(DeferredLightingConstants) + 32u,
    "The flashlight profile must begin the third UVSR register.");

namespace
{


    bool ValidateOutputs(
        const uvsr::PbrDeferredLightingInputs& inputs)
    {
        if (!inputs.output)
        {
            uvsr::log::error("PbrDeferredLightingPass requires an HDR output texture.");
            return false;
        }

        const auto& output = inputs.output->getDesc();
        if (output.dimension != nvrhi::TextureDimension::Texture2D ||
            output.sampleCount != 1u || output.arraySize != 1u ||
            output.format != nvrhi::Format::RGBA16_FLOAT || !output.isUAV)
            return false;
        for (auto* texture : { inputs.surface.depth, inputs.surface.diffuse,
            inputs.surface.material, inputs.surface.normals, inputs.surface.emissive,
            inputs.surface.materialAmbientOcclusion })
        {
            if (!texture)
                return false;
            const auto& receiver = texture->getDesc();
            if (receiver.dimension != nvrhi::TextureDimension::Texture2D ||
                receiver.sampleCount != 1u || receiver.arraySize != 1u ||
                receiver.width != output.width || receiver.height != output.height)
                return false;
        }
        return true;
    }

    bool IsDirectVisibilityTextureCompatible(
        const uvsr::DirectLightVisibility& visibility,
        const uvsr::PbrDeferredLightingInputs& inputs)
    {
        if (!visibility.IsComplete() ||
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
                textureDesc.isShaderResource
            },
            outputDesc.width,
            outputDesc.height);
    }

    bool IsSkyVisibilityTextureCompatible(
        nvrhi::ITexture* texture,
        const uvsr::PbrDeferredLightingInputs& inputs)
    {
        if (!texture || !inputs.output)
            return false;

        const nvrhi::TextureDesc& textureDesc = texture->getDesc();
        const nvrhi::TextureDesc& outputDesc = inputs.output->getDesc();
        return textureDesc.dimension == nvrhi::TextureDimension::Texture2D &&
            textureDesc.arraySize == 1u &&
            textureDesc.width == outputDesc.width &&
            textureDesc.height == outputDesc.height &&
            textureDesc.depth == 1u &&
            textureDesc.mipLevels == 1u &&
            textureDesc.sampleCount == 1u &&
            (textureDesc.format == nvrhi::Format::R8_UNORM ||
                textureDesc.format == nvrhi::Format::R16_FLOAT ||
                textureDesc.format == nvrhi::Format::RGBA16_FLOAT) &&
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

    nvrhi::BufferDesc constantBufferDesc;
    constantBufferDesc.byteSize = sizeof(PbrDeferredLightingConstants);
    constantBufferDesc.debugName = "PbrDeferredLightingConstants";
    constantBufferDesc.isConstantBuffer = true;
    constantBufferDesc.isVolatile = true;
    constantBufferDesc.maxVersions =
        uvsr::RendererMaxConstantBufferVersions;
    m_DeferredLightingCB = m_Device->createBuffer(constantBufferDesc);
    if (!m_DeferredLightingCB)
    {
        m_PipelinePreparationFailed = true;
        return;
    }
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

    {
        Pipeline& pipeline = m_Pipeline;

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
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
        layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(2));
        layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(3));
        nvrhi::BindingLayoutHandle bindingLayout =
            m_Device->createBindingLayout(layoutDesc);

        nvrhi::ShaderHandle shader = m_ShaderFactory->CreateShader(
            "uvsr/pbr_deferred_lighting_cs.hlsl",
            "main",
            nullptr,
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


    m_PipelinesReady = true;
    return m_PipelinesReady;
}

uvsr::PbrDeferredLightingRenderResult PbrDeferredLightingPass::Render(
    nvrhi::ICommandList* commandList,
    const uvsr::PbrDeferredLightingInputs& inputs)
{
    if (!m_PipelinesReady)
    {
        uvsr::log::error(
            "PbrDeferredLightingPass rendered before pipeline preparation completed.");
        return {};
    }
    if (!commandList || !inputs.view ||
        !inputs.surface.HasCompleteGBuffer())
    {
        uvsr::log::error("PbrDeferredLightingPass received incomplete G-buffer inputs.");
        return {};
    }

    if (!ValidateOutputs(inputs))
        return {};

    uvsr::DirectLightVisibilities activeVisibilities;
    const auto acceptVisibility = [&](
        const uvsr::DirectLightVisibility& visibility)
    {
        return IsDirectVisibilityTextureCompatible(
                visibility,
                inputs)
            ? visibility
            : uvsr::DirectLightVisibility{};
    };
    const auto hasVisibilityInput = [](
        const uvsr::DirectLightVisibility& visibility)
    {
        return visibility.texture || visibility.light;
    };
    for (const uvsr::DirectLightVisibility* visibility : {
            &inputs.directLightVisibilities.flashlight,
            &inputs.directLightVisibilities.sun })
    {
        if (hasVisibilityInput(*visibility) &&
            !IsDirectVisibilityTextureCompatible(
                *visibility, inputs))
        {
            uvsr::log::error(
                "PbrDeferredLightingPass rejected an incompatible active direct-light visibility input.");
            return {};
        }
    }
    activeVisibilities.flashlight = acceptVisibility(
        inputs.directLightVisibilities.flashlight);
    activeVisibilities.sun = acceptVisibility(
        inputs.directLightVisibilities.sun);
    const bool hasSkyVisibilityConsumer =
        PbrNeedsSkyVisibilitySample(
            inputs.lightingDebugView,
            inputs.applySkyVisibilityToDiffuseIbl,
            inputs.applySkyVisibilityToSpecularIbl);
    nvrhi::ITexture* activeSkyVisibility =
            hasSkyVisibilityConsumer &&
        IsSkyVisibilityTextureCompatible(
            inputs.skyVisibility,
            inputs)
            ? inputs.skyVisibility
            : nullptr;
    if (hasSkyVisibilityConsumer && inputs.skyVisibility &&
        !activeSkyVisibility)
    {
        uvsr::log::error(
            "PbrDeferredLightingPass rejected an incompatible active sky visibility input.");
        return {};
    }
    nvrhi::ITexture* neutralVisibility = m_CommonPasses->WhiteTexture();
    const uint32_t viewCount =
        inputs.view->GetNumChildViews(ViewType::PLANAR);
    uvsr::PbrDeferredLightingRenderTransaction transaction(viewCount);
    if (viewCount == 0u)
    {
        uvsr::log::error(
            "PbrDeferredLightingPass received no planar output views.");
        transaction.MarkFailed();
        return transaction.Finish();
    }

    commandList->beginMarker("PBR Deferred Lighting");

    PbrDeferredLightingConstants constants = {};
    DeferredLightingConstants& deferredConstants = constants.deferred;
    constants.lightingDebugView = inputs.lightingDebugView;
    constants.skyVisibilityApplication =
        uvsr::ResolveSkyVisibilityApplication(
            activeSkyVisibility != nullptr,
            inputs.applySkyVisibilityToDiffuseIbl,
            inputs.applySkyVisibilityToSpecularIbl);
    constants.directVisibilityLightIndices = { -1, -1 };
    constants.flashlightLightIndex = -1;
    deferredConstants.numLightProbes =
        inputs.environment && inputs.environment->IsActive() ? 1u : 0u;
    if (deferredConstants.numLightProbes > 0u)
    {
        inputs.environment->FillLightProbeConstants(
            deferredConstants.lightProbes[0]);
    }
    deferredConstants.indirectDiffuseScale = 1.f;

    if (inputs.lights)
    {
        for (const auto& light : *inputs.lights)
        {
            if (deferredConstants.numLights >= UVSR_DEFERRED_MAX_LIGHTS)
            {
                uvsr::log::warning("Maximum number of active lights (%d) exceeded in PbrDeferredLightingPass",
                    UVSR_DEFERRED_MAX_LIGHTS);
                break;
            }

            LightConstants& lightConstants =
                deferredConstants.lights[deferredConstants.numLights];
            light->FillLightConstants(lightConstants);
            if (inputs.hardShadows)
            {
                lightConstants.radius = 0.f;
                if (lightConstants.lightType == LightType_Directional)
                    lightConstants.angularSizeOrInvRange = 0.f;
            }
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
            if (inputs.flashlight && light.get() == inputs.flashlight)
            {
                constants.flashlightLightIndex =
                    int(deferredConstants.numLights);
                constants.flashlightBeamProfile =
                    inputs.flashlightBeamProfile;
            }

            ++deferredConstants.numLights;
        }
    }

    for (uint viewIndex = 0; viewIndex < viewCount; ++viewIndex)
    {
        const Pipeline& pipeline = m_Pipeline;
        const IView* view = inputs.view->GetChildView(
            ViewType::PLANAR, viewIndex);
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
            nvrhi::BindingSetItem::Texture_SRV(8, inputs.surface.depth,
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(9, inputs.surface.diffuse,
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(10, inputs.surface.material,
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(11, inputs.surface.normals,
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(12, inputs.surface.emissive,
                nvrhi::Format::UNKNOWN, viewSubresources),
            nvrhi::BindingSetItem::Texture_SRV(14,
                inputs.surface.materialAmbientOcclusion
                    ? inputs.surface.materialAmbientOcclusion
                    : m_CommonPasses->BlackTexture(),
                nvrhi::Format::UNKNOWN, viewSubresources)
        };
        uvsr::PbrDeferredEnvironmentResources<nvrhi::ITexture*>
            activeEnvironmentResources;
        activeEnvironmentResources.diffuseEnvironment =
            inputs.environment && inputs.environment->diffuseMap
                ? inputs.environment->diffuseMap.Get()
                : nullptr;
        activeEnvironmentResources.specularEnvironment =
            inputs.environment && inputs.environment->specularMap
                ? inputs.environment->specularMap.Get()
                : nullptr;
        activeEnvironmentResources.environmentBrdf =
            inputs.environment && inputs.environment->environmentBrdf
                ? inputs.environment->environmentBrdf.Get()
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

        bindingSetDesc.bindings.push_back(
            nvrhi::BindingSetItem::Texture_UAV(
                0, inputs.output, nvrhi::Format::UNKNOWN, viewSubresources));
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
