#include "screen_space_directional_shadows.h"

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "screen_space_directional_shadows_cb.h"
#include "bend_sss_cpu.h"

static_assert(sizeof(ScreenSpaceDirectionalShadowConstants) == 96u,
    "Directional-shadow constants must occupy six HLSL registers.");
static_assert(offsetof(ScreenSpaceDirectionalShadowConstants, waveOffset) == 16u,
    "Directional-shadow packing drifted before waveOffset.");
static_assert(offsetof(ScreenSpaceDirectionalShadowConstants, depthBounds) == 64u,
    "Directional-shadow packing drifted before depthBounds.");

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
        bufferDesc.byteSize = sizeof(ScreenSpaceDirectionalShadowConstants);
        bufferDesc.debugName = "ScreenSpaceDirectionalShadowConstants";
        bufferDesc.isConstantBuffer = true;
        bufferDesc.isVolatile = true;
        bufferDesc.maxVersions = engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(bufferDesc);

        m_PointBorderSamplers[0] = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(false)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Border)
                .setBorderColor(0.f));
        m_PointBorderSamplers[1] = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(false)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Border)
                .setBorderColor(1.f));

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
        m_DebugBindingLayout = device->createBindingLayout(debugLayoutDesc);

        ScreenSpaceDirectionalShadowSettings defaultSettings;
        if (!EnsurePipeline(defaultSettings))
        {
            m_Timings.supported = false;
            log::error(
                "The default screen-space directional-shadow shader variant is unavailable.");
        }
    }

    bool ScreenSpaceDirectionalShadowPass::EnsureResources(
        nvrhi::ITexture* depth,
        bool reverseDepth)
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
            outputDesc.debugName = "Screen-Space Directional Shadow Visibility";
            outputDesc.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            m_NearVisibility = m_Device->createTexture(outputDesc);
            m_BindingSet = nullptr;
            m_DebugBindingSet = nullptr;
            m_Timings.outputTextureBytes =
                uint64_t(depthDesc.width) * uint64_t(depthDesc.height);
        }

        if (!m_NearVisibility)
            return false;

        if (!m_BindingSet ||
            m_BoundDepth != depth ||
            m_BoundReverseDepth != reverseDepth)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, depth),
                nvrhi::BindingSetItem::Texture_UAV(0, m_NearVisibility),
                nvrhi::BindingSetItem::Sampler(
                    0, m_PointBorderSamplers[reverseDepth ? 0u : 1u])
            };
            m_BindingSet =
                m_Device->createBindingSet(bindings, m_BindingLayout);
            m_BoundDepth = depth;
            m_BoundReverseDepth = reverseDepth;
        }

        m_ReportedInvalidInput = false;
        return bool(m_BindingSet);
    }

    ScreenSpaceDirectionalShadowPass::Pipeline*
        ScreenSpaceDirectionalShadowPass::EnsurePipeline(
            const ScreenSpaceDirectionalShadowSettings& settings)
    {
        if (!m_Timings.supported ||
            !IsScreenSpaceShadowConfigurationSupported(settings))
        {
            if (!m_ReportedInvalidVariant)
            {
                log::error(
                    "Requested screen-space directional-shadow variant is not compiled: %u samples, %u hard, %u fade.",
                    GetScreenSpaceShadowTraceReach(settings.length),
                    settings.hardShadowSamples,
                    settings.fadeOutSamples);
                m_ReportedInvalidVariant = true;
            }
            return nullptr;
        }

        const int lengthIndex = FindScreenSpaceShadowSupportedValue(
            ScreenSpaceShadowTraceReaches,
            GetScreenSpaceShadowTraceReach(settings.length));
        const int hardIndex = FindScreenSpaceShadowSupportedValue(
            ScreenSpaceShadowHardSampleCounts,
            settings.hardShadowSamples);
        const int fadeIndex = FindScreenSpaceShadowSupportedValue(
            ScreenSpaceShadowFadeSampleCounts,
            settings.fadeOutSamples);
        assert(lengthIndex >= 0 && hardIndex >= 0 && fadeIndex >= 0);

        Pipeline& pipeline =
            m_Pipelines[size_t(lengthIndex)][size_t(hardIndex)]
                [size_t(fadeIndex)];
        if (!pipeline.pso)
        {
            std::vector<ShaderMacro> macros = {
                { "SAMPLE_COUNT", std::to_string(
                    GetScreenSpaceShadowTraceReach(settings.length)) },
                { "HARD_SHADOW_SAMPLES", std::to_string(
                    settings.hardShadowSamples) },
                { "FADE_OUT_SAMPLES", std::to_string(
                    settings.fadeOutSamples) }
            };
            pipeline.shader = m_ShaderFactory->CreateShader(
                "uvsr/screen_space_directional_shadows_cs.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Compute);
            if (!pipeline.shader)
                return nullptr;

            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = pipeline.shader;
            pipelineDesc.bindingLayouts = { m_BindingLayout };
            pipeline.pso =
                m_Device->createComputePipeline(pipelineDesc);
        }

        m_ReportedInvalidVariant = false;
        return pipeline.pso ? &pipeline : nullptr;
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

    ScreenSpaceDirectionalShadowResult ScreenSpaceDirectionalShadowPass::Render(
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

        const bool reverseDepth = view.IsReverseDepth();
        Pipeline* pipeline = EnsurePipeline(settings);
        if (!pipeline ||
            !EnsureResources(depth, reverseDepth))
        {
            ++m_TimerFrame;
            return {};
        }

        // Donut stores directional-light propagation. The vendored tracer marches
        // toward the projected light, so convert to receiver-to-light here.
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

        const nvrhi::TextureDesc& depthDesc = depth->getDesc();
        float lightProjection[4] = {
            projectedLight.x,
            projectedLight.y,
            projectedLight.z,
            projectedLight.w
        };
        int viewportSize[2] = {
            int(depthDesc.width),
            int(depthDesc.height)
        };
        int minimumBounds[2] = { 0, 0 };
        int maximumBounds[2] = {
            int(depthDesc.width),
            int(depthDesc.height)
        };
        const Bend::DispatchList dispatchList = Bend::BuildDispatchList(
            lightProjection,
            viewportSize,
            minimumBounds,
            maximumBounds,
            false,
            64);

        ScreenSpaceDirectionalShadowConstants constants = {};
        constants.lightCoordinate = float4(
            dispatchList.LightCoordinate_Shader[0],
            dispatchList.LightCoordinate_Shader[1],
            dispatchList.LightCoordinate_Shader[2],
            dispatchList.LightCoordinate_Shader[3]);
        constants.surfaceThickness =
            std::max(settings.surfaceThickness, 0.f);
        constants.bilinearThreshold =
            std::max(settings.bilinearThreshold, 0.f);
        constants.shadowContrast =
            std::max(settings.shadowContrast, 0.f);
        constants.ignoreEdgePixels =
            settings.ignoreEdgePixels ? 1u : 0u;
        constants.usePrecisionOffset =
            settings.usePrecisionOffset ? 1u : 0u;
        constants.bilinearSamplingOffsetMode =
            settings.bilinearSamplingOffsetMode ? 1u : 0u;
        constants.debugOutputEdgeMask =
            settings.debugView == ScreenSpaceShadowDebugView::Edge ? 1u : 0u;
        constants.debugOutputThreadIndex =
            settings.debugView == ScreenSpaceShadowDebugView::Thread ? 1u : 0u;
        constants.debugOutputWaveIndex =
            settings.debugView == ScreenSpaceShadowDebugView::Wave ? 1u : 0u;
        constants.useEarlyOut = settings.useEarlyOut ? 1u : 0u;
        constants.depthBounds = float2(0.f, 1.f);
        constants.farDepthValue = reverseDepth ? 0.f : 1.f;
        constants.nearDepthValue = reverseDepth ? 1.f : 0.f;
        constants.invDepthTextureSize = float2(
            1.f / float(depthDesc.width),
            1.f / float(depthDesc.height));

        commandList->beginMarker("Screen-Space Directional Shadows");
        BeginTimer(commandList);
        commandList->clearTextureFloat(
            m_NearVisibility,
            nvrhi::AllSubresources,
            nvrhi::Color(1.f));

        nvrhi::ComputeState state;
        state.pipeline = pipeline->pso;
        state.bindings = { m_BindingSet };
        for (int dispatchIndex = 0;
            dispatchIndex < dispatchList.DispatchCount;
            ++dispatchIndex)
        {
            const Bend::DispatchData& dispatch =
                dispatchList.Dispatch[dispatchIndex];
            if (dispatch.WaveCount[0] <= 0 ||
                dispatch.WaveCount[1] <= 0 ||
                dispatch.WaveCount[2] <= 0)
            {
                continue;
            }

            constants.waveOffset = int2(
                dispatch.WaveOffset_Shader[0],
                dispatch.WaveOffset_Shader[1]);
            commandList->writeBuffer(
                m_ConstantBuffer,
                &constants,
                sizeof(constants));
            commandList->setComputeState(state);
            commandList->dispatch(
                uint32_t(dispatch.WaveCount[0]),
                uint32_t(dispatch.WaveCount[1]),
                uint32_t(dispatch.WaveCount[2]));
            ++m_Timings.dispatchCount;
            m_Timings.totalGroups +=
                uint32_t(dispatch.WaveCount[0]) *
                uint32_t(dispatch.WaveCount[1]) *
                uint32_t(dispatch.WaveCount[2]);
        }

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
        commandList->beginMarker("Screen-Space Directional Shadow Debug View");
        commandList->setGraphicsState(state);

        nvrhi::DrawArguments arguments;
        arguments.instanceCount = 1u;
        arguments.vertexCount = 4u;
        commandList->draw(arguments);
        commandList->endMarker();
    }
}
