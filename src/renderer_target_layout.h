#pragma once

#include <stdint.h>

namespace uvsr
{
    // a failed placement leaves the candidate layout and caller output unchanged.
    [[nodiscard]] inline bool AppendRendererTargetPlacement(
        uint64_t size, uint64_t alignment, uint64_t& capacity, uint64_t& offset) noexcept
    {
        if (size == 0 || size == UINT64_MAX || alignment == 0 || (alignment & (alignment - 1)) != 0 ||
            capacity > UINT64_MAX - (alignment - 1))
            return false;
        const uint64_t aligned = (capacity + alignment - 1) & ~(alignment - 1);
        if (size > UINT64_MAX - aligned)
            return false;
        offset = aligned;
        capacity = aligned + size;
        return true;
    }
}
