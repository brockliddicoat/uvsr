#pragma once

#include "auto_exposure_shared.h"
#include <cmath>

namespace uvsr
{
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

}
