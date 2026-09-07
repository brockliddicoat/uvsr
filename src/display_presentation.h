#pragma once

#include <algorithm>

namespace uvsr
{
    inline constexpr int MaximumFrameRateLimit = 960;

    struct DisplayPresentationSettings
    {
        bool verticalSynchronization = false;
        bool frameRateLimitEnabled = false;
        int frameRateLimit = 240;

        constexpr bool operator==(const DisplayPresentationSettings& other) const
        {
            return verticalSynchronization == other.verticalSynchronization &&
                frameRateLimitEnabled == other.frameRateLimitEnabled &&
                frameRateLimit == other.frameRateLimit;
        }
    };

    inline constexpr DisplayPresentationSettings DefaultDisplayPresentationSettings{};

    [[nodiscard]] inline int PresentationFrameRateMaximum(bool verticalSynchronization, double refreshRate)
    {
        return verticalSynchronization && refreshRate > 0.0
            ? static_cast<int>(std::clamp(refreshRate, 1.0, double(MaximumFrameRateLimit)))
            : MaximumFrameRateLimit;
    }

    [[nodiscard]] constexpr bool CanPresentWithoutSynchronization(
        bool verticalSynchronization, bool windowed, bool supported)
    {
        return !verticalSynchronization && windowed && supported;
    }

    inline void ReconcileDisplayPresentation(DisplayPresentationSettings& settings,
        const DisplayPresentationSettings& previous, double refreshRate, double previousRefreshRate)
    {
        const int maximum = PresentationFrameRateMaximum(settings.verticalSynchronization, refreshRate);
        if (settings.verticalSynchronization && !previous.verticalSynchronization)
        {
            settings.frameRateLimitEnabled = true;
            if (refreshRate > 0.0)
                settings.frameRateLimit = maximum;
        }
        else if (!settings.verticalSynchronization && previous.verticalSynchronization)
        {
            settings.frameRateLimitEnabled = false;
        }
        else if (settings.verticalSynchronization && refreshRate > 0.0 && previousRefreshRate > 0.0 &&
            refreshRate != previousRefreshRate && settings.frameRateLimit == PresentationFrameRateMaximum(true, previousRefreshRate))
        {
            settings.frameRateLimit = maximum;
        }
        settings.frameRateLimit = std::clamp(settings.frameRateLimit, 1, maximum);
    }

    [[nodiscard]] inline double DisplayPresentationTargetFps(
        const DisplayPresentationSettings& settings, bool testActive, double testTarget)
    {
        double limit = settings.frameRateLimitEnabled
            ? std::clamp(settings.frameRateLimit, 1, MaximumFrameRateLimit) : 0;
        const auto applyCeiling = [&](double ceiling)
        {
            limit = limit > 0.0 ? std::min(limit, ceiling) : ceiling;
        };
        if (testActive && testTarget > 0.0)
            applyCeiling(std::min(testTarget, double(MaximumFrameRateLimit)));
        return limit;
    }

    [[nodiscard]] inline double EffectivePresentationCeiling(
        double frameLimit, bool verticalSynchronization, double refreshRate)
    {
        if (verticalSynchronization && refreshRate > 0.0)
            return frameLimit > 0.0 ? std::min(frameLimit, refreshRate) : refreshRate;
        return frameLimit;
    }

    [[nodiscard]] inline double NextPresentationDeadline(double now, double targetFps)
    {
        if (targetFps <= 0.0)
            return 0.0;
        // schedule from the actual frame start, so lateness never causes catch-up bursts.
        return now + 1.0 / targetFps;
    }
}
