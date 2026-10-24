#pragma once

#include <algorithm>
#include <cmath>

namespace uvsr
{
    struct DisplaySyncTestSettings
    {
        bool vsync = false;
        bool paused = false;
        bool sweep = false;
        float targetFps = 83.f;
        float minimumFps = 65.f;
        float maximumFps = 117.f;
        float speedPixelsPerSecond = 1200.f;
    };

    inline double DisplaySyncTargetFps(const DisplaySyncTestSettings& settings, double elapsedSeconds)
    {
        if (!settings.sweep)
            return std::clamp(double(settings.targetFps), 20.0, 500.0);
        const double low = std::clamp(double(settings.minimumFps), 20.0, 500.0);
        const double high = std::clamp(double(settings.maximumFps), low, 500.0);
        return low + (high - low) * (0.5 - 0.5 * std::cos(elapsedSeconds * 6.283185307179586 / 12.0));
    }

    inline double NextDisplaySyncDeadline(double deadline, double now, double period)
    {
        const double next = deadline + period;
        // a missed deadline starts a fresh interval, never a burst of catch-up frames.
        return next > now ? next : now + period;
    }

    inline double AdvanceDisplaySyncPosition(double position, double elapsedSeconds,
        double pixelsPerSecond, bool paused)
    {
        return paused ? position : std::fmod(position + std::max(0.0, elapsedSeconds) * pixelsPerSecond, 384.0);
    }
}
