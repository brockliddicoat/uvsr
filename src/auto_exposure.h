#pragma once

#include "auto_exposure_shared.h"

#include <nvrhi/nvrhi.h>

#include <cmath>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class ICompositeView;
}

namespace uvsr
{
    class RendererShaderFactory;

    struct AutoExposureFrameHistory
    {
        bool resetRequested = true;
        bool wasEnabled = false;
        bool exposureInitialized = false;
    };

    struct AutoExposureFrameDecision
    {
        bool dispatch = false;
        bool resetExposure = false;
    };

    inline void AbandonAutoExposureFrame(
        AutoExposureFrameHistory& history) noexcept
    {
        history = {};
    }

    [[nodiscard]] inline AutoExposureFrameDecision BeginAutoExposureFrame(
        AutoExposureFrameHistory& history,
        const AutoExposureSettings& settings,
        bool diagnosticView,
        bool resourcesAvailable,
        bool sceneColorAvailable) noexcept
    {
        if (!settings.enabled || diagnosticView || !resourcesAvailable ||
            !sceneColorAvailable)
        {
            AbandonAutoExposureFrame(history);
            return {};
        }

        const bool resetExposure = history.resetRequested ||
            !history.wasEnabled || !history.exposureInitialized;
        history.resetRequested = false;
        history.wasEnabled = true;
        return { true, resetExposure };
    }

    inline void CompleteAutoExposureFrame(
        AutoExposureFrameHistory& history,
        bool histogramDispatched) noexcept
    {
        if (!histogramDispatched)
        {
            AbandonAutoExposureFrame(history);
            return;
        }
        history.exposureInitialized = true;
    }

    inline void RequestAutoExposureReset(
        AutoExposureFrameHistory& history) noexcept
    {
        history.resetRequested = true;
    }

    [[nodiscard]] inline AutoExposureSettings SanitizeAutoExposureSettings(
        AutoExposureSettings settings)
    {
        if (!std::isfinite(settings.exposureCompensationEV))
        {
            settings.exposureCompensationEV =
                AutoExposureDefaultCompensationEV;
        }
        settings.exposureCompensationEV = std::fmax(
            AutoExposureMinimumCompensationEV,
            std::fmin(
                AutoExposureMaximumCompensationEV,
                settings.exposureCompensationEV));
        if (!std::isfinite(settings.maximumBrighteningEV))
        {
            settings.maximumBrighteningEV =
                AutoExposureDefaultMaximumBrighteningEV;
        }
        settings.maximumBrighteningEV = std::fmax(
            AutoExposureMinimumMovementEV,
            std::fmin(
                AutoExposureMaximumMovementEV,
                settings.maximumBrighteningEV));
        if (!std::isfinite(settings.maximumDarkeningEV))
        {
            settings.maximumDarkeningEV =
                AutoExposureDefaultMaximumDarkeningEV;
        }
        settings.maximumDarkeningEV = std::fmax(
            AutoExposureMinimumMovementEV,
            std::fmin(
                AutoExposureMaximumMovementEV,
                settings.maximumDarkeningEV));
        if (!std::isfinite(settings.adjustmentPeriodSeconds))
        {
            settings.adjustmentPeriodSeconds =
                AutoExposureDefaultAdjustmentPeriodSeconds;
        }
        settings.adjustmentPeriodSeconds = std::fmax(
            AutoExposureMinimumAdjustmentPeriodSeconds,
            std::fmin(
                AutoExposureMaximumAdjustmentPeriodSeconds,
                settings.adjustmentPeriodSeconds));
        return settings;
    }

    class AutoExposurePass final
    {
    public:
        AutoExposurePass(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>&
                shaderFactory);

        [[nodiscard]] bool IsAvailable() const;
        [[nodiscard]] bool DidDispatchThisFrame() const
        {
            return m_DispatchedThisFrame;
        }

        [[nodiscard]] nvrhi::IBuffer* Render(
            nvrhi::ICommandList* commandList,
            const donut::engine::ICompositeView& compositeView,
            nvrhi::ITexture* sceneColor,
            const AutoExposureSettings& settings,
            float frameDeltaSeconds,
            bool diagnosticView);

        void Reset();

    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BufferHandle m_HistogramBuffer;
        nvrhi::BufferHandle m_ExposureBuffer;
        nvrhi::ShaderHandle m_HistogramShader;
        nvrhi::ShaderHandle m_ResolveShader;
        nvrhi::BindingLayoutHandle m_HistogramBindingLayout;
        nvrhi::BindingLayoutHandle m_ResolveBindingLayout;
        nvrhi::BindingSetHandle m_HistogramBindingSet;
        nvrhi::BindingSetHandle m_ResolveBindingSet;
        nvrhi::ComputePipelineHandle m_HistogramPipeline;
        nvrhi::ComputePipelineHandle m_ResolvePipeline;
        nvrhi::ITexture* m_BoundSceneColor = nullptr;
        AutoExposureFrameHistory m_FrameHistory;
        bool m_DispatchedThisFrame = false;
    };
}
