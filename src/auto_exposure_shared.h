#ifndef UVSR_AUTO_EXPOSURE_SHARED_H
#define UVSR_AUTO_EXPOSURE_SHARED_H

#define UVSR_AUTO_EXPOSURE_HISTOGRAM_BIN_COUNT 256u
#define UVSR_AUTO_EXPOSURE_MINIMUM_LOG_LUMINANCE -20.0f
#define UVSR_AUTO_EXPOSURE_MAXIMUM_LOG_LUMINANCE 20.0f
#define UVSR_AUTO_EXPOSURE_MIDDLE_GRAY 0.18f

#ifdef __cplusplus

#include <cstdint>

namespace uvsr
{
    inline constexpr bool AutoExposureDefaultEnabled = false;
    inline constexpr float AutoExposureMinimumCompensationEV = -18.f;
    inline constexpr float AutoExposureMaximumCompensationEV = 8.f;
    inline constexpr float AutoExposureDefaultCompensationEV = 0.f;
    inline constexpr float AutoExposureMinimumMovementEV = 0.f;
    inline constexpr float AutoExposureMaximumMovementEV = 16.f;
    inline constexpr float AutoExposureDefaultMaximumBrighteningEV = 5.f;
    inline constexpr float AutoExposureDefaultMaximumDarkeningEV = 2.f;
    inline constexpr float AutoExposureMinimumAdjustmentPeriodSeconds =
        0.05f;
    inline constexpr float AutoExposureMaximumAdjustmentPeriodSeconds = 5.f;
    inline constexpr float AutoExposureDefaultAdjustmentPeriodSeconds = 0.2f;
    inline constexpr float AutoExposureMinimumLogLuminance =
        UVSR_AUTO_EXPOSURE_MINIMUM_LOG_LUMINANCE;
    inline constexpr float AutoExposureMaximumLogLuminance =
        UVSR_AUTO_EXPOSURE_MAXIMUM_LOG_LUMINANCE;
    inline constexpr float AutoExposureMiddleGray =
        UVSR_AUTO_EXPOSURE_MIDDLE_GRAY;
    inline constexpr std::uint32_t AutoExposureHistogramBinCount =
        UVSR_AUTO_EXPOSURE_HISTOGRAM_BIN_COUNT;

    struct AutoExposureSettings
    {
        bool enabled = AutoExposureDefaultEnabled;
        float exposureCompensationEV = AutoExposureDefaultCompensationEV;
        float maximumBrighteningEV =
            AutoExposureDefaultMaximumBrighteningEV;
        float maximumDarkeningEV =
            AutoExposureDefaultMaximumDarkeningEV;
        float adjustmentPeriodSeconds =
            AutoExposureDefaultAdjustmentPeriodSeconds;
    };
}

#endif

#endif
