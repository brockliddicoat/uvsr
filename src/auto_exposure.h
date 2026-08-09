#pragma once

#include <nvrhi/nvrhi.h>

#include <cmath>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class ICompositeView;
    class ShaderFactory;
}

namespace uvsr
{
    inline constexpr float AutoExposureMinimumCompensationEV = -18.f;
    inline constexpr float AutoExposureMaximumCompensationEV = 8.f;
    inline constexpr float AutoExposureDefaultCompensationEV = 0.f;
    inline constexpr float AutoExposureMinimumMovementEV = 0.f;
    inline constexpr float AutoExposureMaximumMovementEV = 16.f;
    inline constexpr float AutoExposureDefaultMaximumBrighteningEV = 5.f;
    inline constexpr float AutoExposureDefaultMaximumDarkeningEV = 2.f;
    inline constexpr float AutoExposureMinimumAdjustmentPeriodSeconds = 0.05f;
    inline constexpr float AutoExposureMaximumAdjustmentPeriodSeconds = 5.f;
    inline constexpr float AutoExposureDefaultAdjustmentPeriodSeconds = 0.2f;
    inline constexpr float AutoExposureMiddleGray = 0.18f;
    inline constexpr uint32_t AutoExposureHistogramBinCount = 256u;

    struct AutoExposureSettings
    {
        bool enabled = false;
        float exposureCompensationEV = AutoExposureDefaultCompensationEV;
        float maximumBrighteningEV =
            AutoExposureDefaultMaximumBrighteningEV;
        float maximumDarkeningEV =
            AutoExposureDefaultMaximumDarkeningEV;
        float adjustmentPeriodSeconds =
            AutoExposureDefaultAdjustmentPeriodSeconds;
    };

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

    [[nodiscard]] inline float ResolveAutoExposureTarget(
        float meteredLuminance,
        AutoExposureSettings requestedSettings = {})
    {
        if (!std::isfinite(meteredLuminance) ||
            !(meteredLuminance > 0.f))
        {
            return 1.f;
        }
        const AutoExposureSettings settings =
            SanitizeAutoExposureSettings(requestedSettings);
        const float targetEV = std::log2(
            AutoExposureMiddleGray / meteredLuminance);
        return std::exp2(std::fmax(
            -settings.maximumDarkeningEV,
            std::fmin(settings.maximumBrighteningEV, targetEV)));
    }

    [[nodiscard]] inline float ResolveAdaptedExposure(
        float previousExposure,
        float targetExposure,
        float deltaSeconds,
        float adjustmentPeriodSeconds,
        float maximumBrighteningEV =
            AutoExposureDefaultMaximumBrighteningEV,
        float maximumDarkeningEV =
            AutoExposureDefaultMaximumDarkeningEV)
    {
        AutoExposureSettings bounds;
        bounds.maximumBrighteningEV = maximumBrighteningEV;
        bounds.maximumDarkeningEV = maximumDarkeningEV;
        bounds = SanitizeAutoExposureSettings(bounds);
        if (!std::isfinite(targetExposure) ||
            !(targetExposure > 0.f))
        {
            targetExposure = 1.f;
        }
        const float targetEV = std::fmax(
            -bounds.maximumDarkeningEV,
            std::fmin(
                bounds.maximumBrighteningEV,
                std::log2(targetExposure)));
        if (!std::isfinite(previousExposure) ||
            !(previousExposure > 0.f))
        {
            return std::exp2(targetEV);
        }
        const float safeDelta = std::isfinite(deltaSeconds)
            ? std::fmax(deltaSeconds, 0.f)
            : 0.f;
        const float safePeriod = std::isfinite(adjustmentPeriodSeconds)
            ? std::fmax(
                AutoExposureMinimumAdjustmentPeriodSeconds,
                std::fmin(
                    AutoExposureMaximumAdjustmentPeriodSeconds,
                    adjustmentPeriodSeconds))
            : AutoExposureDefaultAdjustmentPeriodSeconds;
        const float blend = 1.f - std::exp2(
            -safeDelta / safePeriod);
        const float previousEV = std::fmax(
            -bounds.maximumDarkeningEV,
            std::fmin(
                bounds.maximumBrighteningEV,
                std::log2(previousExposure)));
        return std::exp2(
            previousEV + (targetEV - previousEV) * blend);
    }

    class AutoExposurePass final
    {
    public:
        AutoExposurePass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>&
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
        bool m_ResetRequested = true;
        bool m_WasEnabled = false;
        bool m_ExposureInitialized = false;
        bool m_DispatchedThisFrame = false;
    };
}
