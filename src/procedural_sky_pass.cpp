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

#include "procedural_sky_pass.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/Scene.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <cmath>

using namespace donut::math;
#include "procedural_sky_cb.h"

using namespace donut::engine;

namespace uvsr
{
    static_assert(
        sizeof(ProceduralSkyConstants) % 16u == 0u,
        "Procedural sky constants must remain 16-byte aligned for HLSL.");

    ProceduralSkyPass::ProceduralSkyPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        const std::shared_ptr<FramebufferFactory>& framebufferFactory,
        const ICompositeView& compositeView)
        : m_FramebufferFactory(framebufferFactory)
    {
        m_PixelShader = shaderFactory->CreateShader(
            "uvsr/procedural_sky_ps.hlsl", "main", nullptr,
            nvrhi::ShaderType::Pixel);

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(ProceduralSkyConstants);
        constantBufferDesc.debugName = "UVSR Procedural Sky Constants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions = c_MaxRenderPassConstantBufferVersions;
        m_SkyCB = device->createBuffer(constantBufferDesc);

        const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)
        };
        m_RenderBindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_SkyCB)
        };
        m_RenderBindingSet = device->createBindingSet(
            bindingSetDesc, m_RenderBindingLayout);

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = sampleView->IsReverseDepth()
            ? commonPasses->m_FullscreenVS
            : commonPasses->m_FullscreenAtOneVS;
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_RenderBindingLayout };
        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState
            .enableDepthTest()
            .disableDepthWrite()
            .disableStencil()
            .setDepthFunc(sampleView->IsReverseDepth()
                ? nvrhi::ComparisonFunc::GreaterOrEqual
                : nvrhi::ComparisonFunc::LessOrEqual);

        m_RenderPso = device->createGraphicsPipeline(
            pipelineDesc, m_FramebufferFactory->GetFramebufferInfo());
    }

    void ProceduralSkyPass::Render(
        nvrhi::ICommandList* commandList,
        const ICompositeView& compositeView,
        const DirectionalLight& light,
        const ProceduralSkySettings& settings) const
    {
        commandList->beginMarker("Three-Band Procedural Sky");

        for (uint32_t viewIndex = 0;
            viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR);
            ++viewIndex)
        {
            const IView* view = compositeView.GetChildView(
                ViewType::PLANAR, viewIndex);

            nvrhi::GraphicsState state;
            state.pipeline = m_RenderPso;
            state.framebuffer = m_FramebufferFactory->GetFramebuffer(*view);
            state.bindings = { m_RenderBindingSet };
            state.viewport = view->GetViewportState();

            dm::affine viewToWorld = view->GetInverseViewMatrix();
            viewToWorld.m_translation = 0.f;
            const dm::float4x4 clipToTranslatedWorld =
                view->GetInverseProjectionMatrix(true) *
                affineToHomogeneous(viewToWorld);

            ProceduralSkyConstants constants{};
            constants.matClipToTranslatedWorld = clipToTranslatedWorld;
            FillShaderParameters(light, settings, constants.params);
            commandList->writeBuffer(m_SkyCB, &constants, sizeof(constants));

            commandList->setGraphicsState(state);
            nvrhi::DrawArguments args;
            args.instanceCount = 1;
            args.vertexCount = 4;
            commandList->draw(args);
        }

        commandList->endMarker();
    }

    void ProceduralSkyPass::FillShaderParameters(
        const DirectionalLight& light,
        const ProceduralSkySettings& settings,
        ProceduralSkyShaderParameters& output)
    {
        const ProceduralSkyTimeState timeState =
            GetProceduralSkyTimeState(settings);
        const ProceduralSkyPalette palette =
            GetProceduralSkyPalette(settings);
        const float brightness = GetEffectiveProceduralSkyBrightness(
            settings, palette, timeState);

        const float lightAngularSize = dm::radians(
            dm::clamp(light.angularSize, 0.1f, 90.f));
        const float lightSolidAngle = 4.f * dm::PI_f *
            std::pow(std::sin(lightAngularSize * 0.5f), 2.f);
        float lightRadiance = std::max(light.irradiance, 0.f) /
            std::max(lightSolidAngle, 1e-6f);
        if (timeState.moonActive)
        {
            // Cap the reference preset first, then apply the physical
            // Irradiance ratio so both the disk and halo respond linearly
            // instead of remaining pinned at the lunar detail-preserving cap.
            lightRadiance = GetNightMoonLightPreset().irradiance /
                std::max(lightSolidAngle, 1e-6f);
        }
        float lightRadianceCap = timeState.moonActive
            ? GetDeepNightProceduralSkyPalette().celestialRadianceCap
            : palette.celestialRadianceCap;
        if (settings.maxLightRadiance > 0.f)
        {
            lightRadianceCap = lightRadianceCap > 0.f
                ? std::min(lightRadianceCap, settings.maxLightRadiance)
                : settings.maxLightRadiance;
        }
        if (lightRadianceCap > 0.f)
            lightRadiance = std::min(lightRadiance, lightRadianceCap);
        if (timeState.moonActive)
            lightRadiance *= GetNightMoonRadianceScale(light.irradiance);

        output.directionToLight = dm::float3(
            dm::normalize(-light.GetDirection()));
        output.angularSizeOfLight = lightAngularSize;
        output.lightColor = lightRadiance * light.color;
        output.glowSize = dm::radians(
            GetProceduralSkyHaloOuterRadiusDegrees(settings));
        output.zenithColor = palette.zenithColor * brightness;
        output.glowIntensity = dm::clamp(
            settings.glowIntensity * palette.glowIntensityScale, 0.f, 1.f);
        output.middleColor = palette.middleColor * brightness;
        output.horizonSize = dm::radians(dm::clamp(
            settings.horizonSize, 0.f, 90.f));
        output.horizonColor = palette.horizonColor * brightness;
        output.glowSharpness = dm::clamp(
            settings.glowSharpness, 1.f, 10.f);
        output.groundColor = palette.groundColor * brightness;
        output.celestialSoftness = dm::clamp(
            timeState.moonActive ? 0.04f : 0.08f, 0.f, 0.95f);
        output.horizonAccentColor =
            palette.horizonAccentColor * brightness;
        output.horizonAccentStrength = dm::clamp(
            palette.horizonAccentStrength, 0.f, 1.f);
        output.directionUp = dm::float3(0.f, 1.f, 0.f);
        output.middleBandElevation = dm::clamp(
            palette.middleBandElevation, 0.01f, 0.99f);
        output.starColor = palette.starColor * brightness;
        output.elevationCurve = dm::clamp(
            palette.elevationCurve, 0.05f, 4.f);
        output.starIntensity = GetEffectiveProceduralSkyStarIntensity(
            settings, palette);
        output.starDensityThreshold = dm::clamp(
            palette.starDensityThreshold, 0.f, 1.f);
        output.starCellScale = std::max(palette.starCellScale, 1.f);
        output.moonSurfaceAmount = timeState.moonActive ? 1.f : 0.f;
    }
}
