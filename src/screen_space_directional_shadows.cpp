#include "screen_space_directional_shadows.h"

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <cassert>
#include <cmath>
#include <cstddef>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include <donut/shaders/view_cb.h>
#include "screen_space_directional_shadows_cb.h"

static_assert(
    sizeof(ScreenSpaceDirectionalShadowConstants) == 96u,
    "Directional-shadow constants must occupy six HLSL registers.");
static_assert(
    offsetof(
        ScreenSpaceDirectionalShadowConstants,
        textureSize) == 32u,
    "Directional-shadow packing drifted before textureSize.");
static_assert(
    offsetof(
        ScreenSpaceDirectionalShadowConstants,
        hardShadowSamples) == 64u,
    "Directional-shadow packing drifted before sample controls.");

namespace uvsr
{
    namespace
    {
        bool HasRequiredR8Support(nvrhi::IDevice* device)
        {
            const nvrhi::FormatSupport required =
                nvrhi::FormatSupport::Texture |
                nvrhi::FormatSupport::ShaderLoad |
                nvrhi::FormatSupport::ShaderSample |
                nvrhi::FormatSupport::ShaderUavStore;
            const nvrhi::FormatSupport available =
                device->queryFormatSupport(nvrhi::Format::R8_UNORM);
            return (available & required) == required;
        }

        bool IsFinite(const float4& value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z) &&
                std::isfinite(value.w);
        }
    }

    ScreenSpaceDirectionalShadowPass::ScreenSpaceDirectionalShadowPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses)
        : m_Device(device)
        , m_ShaderFactory(shaderFactory)
        , m_CommonPasses(commonPasses)
    {
        m_Timings.supported = HasRequiredR8Support(device);
        if (!m_Timings.supported)
        {
            log::error(
                "Screen-space directional shadows require R8_UNORM texture, sample, load, and UAV-store support.");
            return;
        }

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Sampler(0)
        };
        m_BindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::BufferDesc bufferDesc;
        bufferDesc.byteSize =
            sizeof(ScreenSpaceDirectionalShadowConstants);
        bufferDesc.debugName =
            "ScreenSpaceDirectionalShadow/Constants";
        bufferDesc.isConstantBuffer = true;
        bufferDesc.isVolatile = true;
        bufferDesc.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(bufferDesc);

        m_PointClampSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(false)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

        m_TraceShader = shaderFactory->CreateShader(
            "uvsr/screen_space_directional_shadows_cs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Compute);
        if (m_TraceShader)
        {
            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = m_TraceShader;
            pipelineDesc.bindingLayouts = { m_BindingLayout };
            m_TracePipeline =
                device->createComputePipeline(pipelineDesc);
        }
        if (!m_TracePipeline)
        {
            m_Timings.supported = false;
            log::error(
                "The screen-space directional-shadow trace shader is unavailable.");
        }

        for (nvrhi::TimerQueryHandle& query : m_TimerQueries)
            query = device->createTimerQuery();

        m_DebugPixelShader = shaderFactory->CreateShader(
            "uvsr/screen_space_directional_shadows_debug_ps.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Pixel);
        nvrhi::BindingLayoutDesc debugLayoutDesc;
        debugLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
        debugLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0)
        };
        m_DebugBindingLayout =
            device->createBindingLayout(debugLayoutDesc);
    }

    bool ScreenSpaceDirectionalShadowPass::EnsureResources(
        nvrhi::ITexture* depth)
    {
        if (!depth || !m_Timings.supported)
            return false;

        const nvrhi::TextureDesc& depthDesc = depth->getDesc();
        if (depthDesc.sampleCount != 1u ||
            depthDesc.dimension != nvrhi::TextureDimension::Texture2D ||
            depthDesc.width == 0u ||
            depthDesc.height == 0u)
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Screen-space directional shadows require a non-empty, single-sample 2D depth texture.");
                m_ReportedInvalidInput = true;
            }
            return false;
        }

        const bool recreateOutput =
            !m_NearVisibility ||
            m_NearVisibility->getDesc().width != depthDesc.width ||
            m_NearVisibility->getDesc().height != depthDesc.height;
        if (recreateOutput)
        {
            nvrhi::TextureDesc outputDesc;
            outputDesc.width = depthDesc.width;
            outputDesc.height = depthDesc.height;
            outputDesc.format = nvrhi::Format::R8_UNORM;
            outputDesc.dimension = nvrhi::TextureDimension::Texture2D;
            outputDesc.isUAV = true;
            outputDesc.debugName =
                "Screen-Space Directional Shadow Visibility";
            outputDesc.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            m_NearVisibility = m_Device->createTexture(outputDesc);
            m_BindingSet = nullptr;
            m_DebugBindingSet = nullptr;
            m_Timings.outputTextureBytes =
                uint64_t(depthDesc.width) *
                uint64_t(depthDesc.height);
        }

        if (!m_NearVisibility)
            return false;

        if (!m_BindingSet || m_BoundDepth != depth)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, depth),
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_NearVisibility),
                nvrhi::BindingSetItem::Sampler(
                    0, m_PointClampSampler)
            };
            m_BindingSet =
                m_Device->createBindingSet(bindings, m_BindingLayout);
            m_BoundDepth = depth;
        }

        m_ReportedInvalidInput = false;
        return bool(m_BindingSet);
    }

    void ScreenSpaceDirectionalShadowPass::AdvanceTimer()
    {
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        m_TimerActive = false;
        if (!m_TimerPending[slot])
            return;

        nvrhi::ITimerQuery* query = m_TimerQueries[slot];
        if (!m_Device->pollTimerQuery(query))
            return;

        m_Timings.traceMilliseconds =
            m_Device->getTimerQueryTime(query) * 1000.f;
        m_Device->resetTimerQuery(query);
        m_TimerPending[slot] = false;
    }

    void ScreenSpaceDirectionalShadowPass::BeginTimer(
        nvrhi::ICommandList* commandList)
    {
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        if (m_TimerPending[slot])
            return;
        commandList->beginTimerQuery(m_TimerQueries[slot]);
        m_TimerActive = true;
    }

    void ScreenSpaceDirectionalShadowPass::EndTimer(
        nvrhi::ICommandList* commandList)
    {
        if (!m_TimerActive)
            return;
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        commandList->endTimerQuery(m_TimerQueries[slot]);
        m_TimerPending[slot] = true;
        m_TimerActive = false;
    }

    ScreenSpaceDirectionalShadowResult
        ScreenSpaceDirectionalShadowPass::Render(
            nvrhi::ICommandList* commandList,
            const ScreenSpaceDirectionalShadowSettings& settings,
            const IView& view,
            nvrhi::ITexture* depth,
            const DirectionalLight* light)
    {
        AdvanceTimer();
        m_Timings.active = false;
        m_Timings.dispatchCount = 0u;
        m_Timings.totalGroups = 0u;
        m_Timings.sampleCount =
            GetScreenSpaceShadowTraceReach(settings.length);

        if (!settings.enabled ||
            !commandList ||
            !depth ||
            !light ||
            !m_Timings.supported)
        {
            ++m_TimerFrame;
            return {};
        }

        if (!IsScreenSpaceShadowConfigurationSupported(settings))
        {
            if (!m_ReportedInvalidConfiguration)
            {
                log::error(
                    "Unsupported screen-space directional-shadow configuration: %u-pixel reach, %u hard samples, %u fade samples.",
                    GetScreenSpaceShadowTraceReach(settings.length),
                    settings.hardShadowSamples,
                    settings.fadeOutSamples);
                m_ReportedInvalidConfiguration = true;
            }
            ++m_TimerFrame;
            return {};
        }
        m_ReportedInvalidConfiguration = false;

        if (!EnsureResources(depth))
        {
            ++m_TimerFrame;
            return {};
        }

        // Directional lights store propagation direction. Rays travel from
        // the receiver toward the light, so negate it before projection.
        const float3 lightDirection =
            float3(light->GetDirection());
        const float lightDirectionLengthSquared =
            dot(lightDirection, lightDirection);
        if (!std::isfinite(lightDirection.x) ||
            !std::isfinite(lightDirection.y) ||
            !std::isfinite(lightDirection.z) ||
            !std::isfinite(lightDirectionLengthSquared) ||
            lightDirectionLengthSquared <= 1e-12f)
        {
            ++m_TimerFrame;
            return {};
        }
        const float3 directionToLight =
            -lightDirection /
            std::sqrt(lightDirectionLengthSquared);
        const float4 projectedLight =
            float4(directionToLight, 0.f) *
            view.GetViewProjectionMatrix(true);
        if (!IsFinite(projectedLight))
        {
            ++m_TimerFrame;
            return {};
        }

        PlanarViewConstants planarView{};
        view.FillPlanarViewConstants(planarView);
        const nvrhi::TextureDesc& depthDesc = depth->getDesc();

        ScreenSpaceDirectionalShadowConstants constants{};
        constants.projectedLight = projectedLight;
        constants.clipToWindowScale = planarView.clipToWindowScale;
        constants.clipToWindowBias = planarView.clipToWindowBias;
        constants.textureSize =
            uint2(depthDesc.width, depthDesc.height);
        constants.traceSampleCount =
            GetScreenSpaceShadowTraceReach(settings.length);
        constants.surfaceThickness =
            std::max(settings.surfaceThickness, 0.f);
        constants.depthDiscontinuityThreshold =
            std::max(settings.bilinearThreshold, 0.f);
        constants.shadowContrast =
            std::max(settings.shadowContrast, 0.f);
        constants.hardShadowSamples = settings.hardShadowSamples;
        constants.fadeOutSamples = settings.fadeOutSamples;
        constants.ignoreEdgePixels =
            settings.ignoreEdgePixels ? 1u : 0u;
        constants.usePrecisionOffset =
            settings.usePrecisionOffset ? 1u : 0u;
        constants.bilinearSamplingOffsetMode =
            settings.bilinearSamplingOffsetMode ? 1u : 0u;
        constants.useEarlyOut = settings.useEarlyOut ? 1u : 0u;
        constants.debugView =
            static_cast<uint32_t>(settings.debugView);
        constants.reverseDepth = view.IsReverseDepth() ? 1u : 0u;

        commandList->beginMarker(
            "Screen-Space Directional Shadows");
        BeginTimer(commandList);
        commandList->writeBuffer(
            m_ConstantBuffer,
            &constants,
            sizeof(constants));

        nvrhi::ComputeState state;
        state.pipeline = m_TracePipeline;
        state.bindings = { m_BindingSet };
        commandList->setComputeState(state);
        const uint32_t groupsX = (depthDesc.width + 7u) / 8u;
        const uint32_t groupsY = (depthDesc.height + 7u) / 8u;
        commandList->dispatch(groupsX, groupsY, 1u);
        m_Timings.dispatchCount = 1u;
        m_Timings.totalGroups = groupsX * groupsY;

        EndTimer(commandList);
        commandList->endMarker();
        m_Timings.active = true;
        ++m_TimerFrame;

        ScreenSpaceDirectionalShadowResult result;
        result.nearVisibility = m_NearVisibility;
        result.light = light;
        result.showDebug =
            settings.debugView != ScreenSpaceShadowDebugView::None;
        return result;
    }

    void ScreenSpaceDirectionalShadowPass::PresentDebug(
        nvrhi::ICommandList* commandList,
        nvrhi::IFramebuffer* framebuffer)
    {
        if (!commandList ||
            !framebuffer ||
            !m_NearVisibility ||
            !m_DebugPixelShader)
        {
            return;
        }

        if (!m_DebugPipeline)
        {
            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.primType =
                nvrhi::PrimitiveType::TriangleStrip;
            pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
            pipelineDesc.PS = m_DebugPixelShader;
            pipelineDesc.bindingLayouts = { m_DebugBindingLayout };
            pipelineDesc.renderState.rasterState.setCullNone();
            pipelineDesc.renderState.depthStencilState.depthTestEnable =
                false;
            pipelineDesc.renderState.depthStencilState.stencilEnable =
                false;
            m_DebugPipeline = m_Device->createGraphicsPipeline(
                pipelineDesc,
                framebuffer->getFramebufferInfo());
        }

        if (!m_DebugBindingSet)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::Texture_SRV(
                    0, m_NearVisibility)
            };
            m_DebugBindingSet = m_Device->createBindingSet(
                bindings,
                m_DebugBindingLayout);
        }

        nvrhi::GraphicsState state;
        state.pipeline = m_DebugPipeline;
        state.framebuffer = framebuffer;
        state.bindings = { m_DebugBindingSet };
        const nvrhi::FramebufferInfoEx& info =
            framebuffer->getFramebufferInfo();
        state.viewport.addViewport(
            nvrhi::Viewport(float(info.width), float(info.height)));
        state.viewport.addScissorRect(
            nvrhi::Rect(int(info.width), int(info.height)));
        commandList->beginMarker(
            "Directional Shadow Debug View");
        commandList->setGraphicsState(state);

        nvrhi::DrawArguments arguments;
        arguments.instanceCount = 1u;
        arguments.vertexCount = 4u;
        commandList->draw(arguments);
        commandList->endMarker();
    }
}
