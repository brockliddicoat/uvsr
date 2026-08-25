#pragma once

#include <cstdint>

namespace uvsr
{
    inline constexpr std::uint32_t VisibilitySchedulerMinimumSampleCount = 1u;
    inline constexpr std::uint32_t VisibilitySchedulerMaximumSampleCount = 64u;
    inline constexpr std::uint32_t
        VisibilitySchedulerParityEstimatorIndex = 1u;

    enum class VisibilitySchedulerMode : std::uint32_t
    {
        Guarded = 0u,
        Even = 1u,
        Odd = 2u
    };

    struct VisibilitySchedulerState
    {
        std::uint32_t selectedSampleCount =
            VisibilitySchedulerMinimumSampleCount;
        VisibilitySchedulerMode mode = VisibilitySchedulerMode::Guarded;
    };

    [[nodiscard]] inline constexpr VisibilitySchedulerState
        ResolveVisibilitySchedulerState(
            std::uint32_t estimatorIndex,
            bool ambientEnabled,
            bool indirectEnabled,
            std::uint32_t requestedSampleCount) noexcept
    {
        const std::uint32_t selectedSampleCount =
            requestedSampleCount < VisibilitySchedulerMinimumSampleCount
            ? VisibilitySchedulerMinimumSampleCount
            : requestedSampleCount > VisibilitySchedulerMaximumSampleCount
                ? VisibilitySchedulerMaximumSampleCount
                : requestedSampleCount;
        if (!ambientEnabled || !indirectEnabled ||
            estimatorIndex != VisibilitySchedulerParityEstimatorIndex)
        {
            return {
                selectedSampleCount,
                VisibilitySchedulerMode::Guarded
            };
        }

        return {
            selectedSampleCount,
            (selectedSampleCount & 1u) == 0u
                ? VisibilitySchedulerMode::Even
                : VisibilitySchedulerMode::Odd
        };
    }
}
