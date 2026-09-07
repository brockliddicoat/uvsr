#pragma once

#include "display_presentation.h"

#include <algorithm>
#include <cmath>

namespace uvsr
{
    enum class DisplaySyncRateMode { FrameRateLimit, BelowRefresh, AboveRefresh, Fixed, Sweep };

    struct DisplaySyncTestSettings
    {
        bool paused = false;
        DisplaySyncRateMode rateMode = DisplaySyncRateMode::FrameRateLimit;
        float targetFps = 83.f;
        float minimumFps = 65.f;
        float maximumFps = 117.f;
        float speedPixelsPerSecond = 1200.f;
    };

    inline double DisplaySyncTargetFps(
        const DisplaySyncTestSettings& settings, double elapsedSeconds, double refreshRate)
    {
        if (settings.rateMode == DisplaySyncRateMode::FrameRateLimit)
            return 0.0;
        if (refreshRate > 0.0)
        {
            if (settings.rateMode == DisplaySyncRateMode::BelowRefresh)
                return std::clamp(refreshRate - std::max(3.0, refreshRate * 0.03), 20.0, double(MaximumFrameRateLimit));
            if (settings.rateMode == DisplaySyncRateMode::AboveRefresh)
                return std::clamp(refreshRate + 13.0, 20.0, double(MaximumFrameRateLimit));
        }
        if (settings.rateMode != DisplaySyncRateMode::Sweep)
            return std::clamp(double(settings.targetFps), 20.0, double(MaximumFrameRateLimit));
        const double low = std::clamp(double(settings.minimumFps), 20.0, double(MaximumFrameRateLimit));
        const double high = std::clamp(double(settings.maximumFps), low, double(MaximumFrameRateLimit));
        return low + (high - low) * (0.5 - 0.5 * std::cos(elapsedSeconds * 6.283185307179586 / 12.0));
    }

    inline double AdvanceDisplaySyncPosition(double position, double elapsedSeconds,
        double pixelsPerSecond, bool paused)
    {
        return paused ? position : std::fmod(position + std::max(0.0, elapsedSeconds) * pixelsPerSecond, 384.0);
    }
}
