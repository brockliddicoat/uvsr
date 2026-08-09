#include "auto_exposure.h"

#include <donut/core/math/math.h>

#include <donut/core/log.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <algorithm>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "auto_exposure_cb.h"

static_assert(sizeof(AutoExposureConstants) % 16u == 0u,
    "Auto-exposure constants must preserve constant-buffer packing");

namespace uvsr
{
    AutoExposurePass::AutoExposurePass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory)
        : m_Device(device)
    {
        if (!device || !shaderFactory)
            return;

        nvrhi::BufferDesc constantBufferDescription;
        constantBufferDescription.byteSize = sizeof(AutoExposureConstants);
        constantBufferDescription.debugName = "AutoExposureConstants";
        constantBufferDescription.isConstantBuffer = true;
        constantBufferDescription.isVolatile = true;
        constantBufferDescription.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(
            constantBufferDescription);

        nvrhi::BufferDesc histogramDescription;
        histogramDescription.byteSize =
            sizeof(uint32_t) * AutoExposureHistogramBinCount;
        histogramDescription.format = nvrhi::Format::R32_UINT;
        histogramDescription.canHaveUAVs = true;
        histogramDescription.canHaveTypedViews = true;
        histogramDescription.initialState =
            nvrhi::ResourceStates::UnorderedAccess;
        histogramDescription.keepInitialState = true;
        histogramDescription.debugName = "Auto Exposure Histogram";
        m_HistogramBuffer = device->createBuffer(histogramDescription);

        nvrhi::BufferDesc exposureDescription = histogramDescription;
        exposureDescription.byteSize = sizeof(float) * 2u;
        exposureDescription.format = nvrhi::Format::R32_FLOAT;
        exposureDescription.debugName = "Auto Exposure Values";
        m_ExposureBuffer = device->createBuffer(exposureDescription);

        m_HistogramShader = shaderFactory->CreateShader(
            "uvsr/auto_exposure_histogram_cs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Compute);
        m_ResolveShader = shaderFactory->CreateShader(
            "uvsr/auto_exposure_resolve_cs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Compute);

        nvrhi::BindingLayoutDesc histogramLayoutDescription;
        histogramLayoutDescription.visibility = nvrhi::ShaderType::Compute;
        histogramLayoutDescription.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::TypedBuffer_UAV(0)
        };
        m_HistogramBindingLayout = device->createBindingLayout(
            histogramLayoutDescription);

        nvrhi::BindingLayoutDesc resolveLayoutDescription;
        resolveLayoutDescription.visibility = nvrhi::ShaderType::Compute;
        resolveLayoutDescription.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::TypedBuffer_SRV(0),
            nvrhi::BindingLayoutItem::TypedBuffer_UAV(0)
        };
        m_ResolveBindingLayout = device->createBindingLayout(
            resolveLayoutDescription);

        if (m_HistogramShader && m_HistogramBindingLayout)
        {
            nvrhi::ComputePipelineDesc pipelineDescription;
            pipelineDescription.CS = m_HistogramShader;
            pipelineDescription.bindingLayouts = {
                m_HistogramBindingLayout
            };
            m_HistogramPipeline = device->createComputePipeline(
                pipelineDescription);
        }
        if (m_ResolveShader && m_ResolveBindingLayout)
        {
            nvrhi::ComputePipelineDesc pipelineDescription;
            pipelineDescription.CS = m_ResolveShader;
            pipelineDescription.bindingLayouts = {
                m_ResolveBindingLayout
            };
            m_ResolvePipeline = device->createComputePipeline(
                pipelineDescription);
        }

        if (m_ConstantBuffer && m_HistogramBuffer && m_ExposureBuffer &&
            m_ResolveBindingLayout)
        {
            nvrhi::BindingSetDesc description;
            description.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_ConstantBuffer),
                nvrhi::BindingSetItem::TypedBuffer_SRV(
                    0, m_HistogramBuffer),
                nvrhi::BindingSetItem::TypedBuffer_UAV(
                    0, m_ExposureBuffer)
            };
            m_ResolveBindingSet = device->createBindingSet(
                description,
                m_ResolveBindingLayout);
        }

        if (!IsAvailable())
            log::error("The auto-exposure pipelines could not be created");
    }

    bool AutoExposurePass::IsAvailable() const
    {
        return m_ConstantBuffer && m_HistogramBuffer && m_ExposureBuffer &&
            m_HistogramPipeline && m_ResolvePipeline &&
            m_HistogramBindingLayout && m_ResolveBindingSet;
    }

    nvrhi::IBuffer* AutoExposurePass::Render(
        nvrhi::ICommandList* commandList,
        const ICompositeView& compositeView,
        nvrhi::ITexture* sceneColor,
        const AutoExposureSettings& requestedSettings,
        float frameDeltaSeconds,
        bool diagnosticView)
    {
        m_DispatchedThisFrame = false;
        if (!commandList || !m_ExposureBuffer)
            return nullptr;

        const auto returnUnityExposure = [&]() -> nvrhi::IBuffer*
        {
            m_ResetRequested = true;
            m_WasEnabled = false;
            m_ExposureInitialized = false;
            return nullptr;
        };

        const AutoExposureSettings settings =
            SanitizeAutoExposureSettings(requestedSettings);
        if (!settings.enabled || diagnosticView)
            return returnUnityExposure();

        // Auto exposure is optional. Missing shaders, pipelines, bindings, or
        // scene input must preserve rendering with a unity exposure instead of
        // abandoning the frame while its command list is still open.
        if (!IsAvailable() || !sceneColor)
            return returnUnityExposure();

        if (!m_HistogramBindingSet || m_BoundSceneColor != sceneColor)
        {
            nvrhi::BindingSetDesc description;
            description.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, sceneColor),
                nvrhi::BindingSetItem::TypedBuffer_UAV(
                    0, m_HistogramBuffer)
            };
            m_HistogramBindingSet = m_Device->createBindingSet(
                description,
                m_HistogramBindingLayout);
            m_BoundSceneColor = sceneColor;
        }
        if (!m_HistogramBindingSet)
            return returnUnityExposure();

        const bool resetExposure = m_ResetRequested || !m_WasEnabled ||
            !m_ExposureInitialized;
        m_ResetRequested = false;
        m_WasEnabled = true;

        commandList->beginMarker("Auto Exposure");
        commandList->clearBufferUInt(m_HistogramBuffer, 0u);

        bool histogramDispatched = false;
        AutoExposureConstants resolveConstants = {};
        for (uint32_t viewIndex = 0u;
            viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR);
            ++viewIndex)
        {
            const IView* view = compositeView.GetChildView(
                ViewType::PLANAR,
                viewIndex);
            if (!view)
                continue;
            const nvrhi::Rect extent = view->GetViewExtent();
            if (extent.width() <= 0 || extent.height() <= 0)
                continue;
            AutoExposureConstants constants = {};
            constants.viewOrigin = uint2(
                std::max(extent.minX, 0),
                std::max(extent.minY, 0));
            constants.viewSize = uint2(
                std::max(extent.width(), 0),
                std::max(extent.height(), 0));
            constants.frameDeltaSeconds = std::isfinite(frameDeltaSeconds)
                ? std::clamp(frameDeltaSeconds, 0.f, 1.f)
                : 0.f;
            constants.exposureCompensationEV =
                settings.exposureCompensationEV;
            constants.adjustmentPeriodSeconds =
                settings.adjustmentPeriodSeconds;
            constants.resetExposure = resetExposure ? 1u : 0u;
            constants.maximumBrighteningEV =
                settings.maximumBrighteningEV;
            constants.maximumDarkeningEV =
                settings.maximumDarkeningEV;
            commandList->writeBuffer(
                m_ConstantBuffer,
                &constants,
                sizeof(constants));

            nvrhi::ComputeState state;
            state.pipeline = m_HistogramPipeline;
            state.bindings = { m_HistogramBindingSet };
            commandList->setComputeState(state);
            commandList->dispatch(
                div_ceil(constants.viewSize.x, 16u),
                div_ceil(constants.viewSize.y, 16u));
            resolveConstants = constants;
            histogramDispatched = true;
        }
        if (histogramDispatched)
        {
            commandList->writeBuffer(
                m_ConstantBuffer,
                &resolveConstants,
                sizeof(resolveConstants));
            nvrhi::ComputeState state;
            state.pipeline = m_ResolvePipeline;
            state.bindings = { m_ResolveBindingSet };
            commandList->setComputeState(state);
            commandList->dispatch(1u);
            m_ExposureInitialized = true;
        }
        commandList->endMarker();
        if (!histogramDispatched)
            return returnUnityExposure();
        m_DispatchedThisFrame = true;
        return m_ExposureBuffer;
    }

    void AutoExposurePass::Reset()
    {
        m_ResetRequested = true;
    }
}
