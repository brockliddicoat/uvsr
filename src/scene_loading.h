#pragma once

#include <cstdint>

namespace uvsr
{
    inline constexpr uint32_t MaximumSceneLoadWorkerCount = 8u;

    // Keep the renderer, compositor, and operating system schedulable while
    // CPU-heavy image decoding and glTF conversion run in the background.
    [[nodiscard]] constexpr uint32_t ResolveSceneLoadWorkerCount(
        uint32_t logicalProcessorCount)
    {
        if (logicalProcessorCount <= 2u)
            return 1u;

        const uint32_t workersWithReservedCores =
            logicalProcessorCount - 2u;
        return workersWithReservedCores < MaximumSceneLoadWorkerCount
            ? workersWithReservedCores
            : MaximumSceneLoadWorkerCount;
    }
}
