#pragma once

#include <stddef.h>
#include <stdint.h>

namespace uvsr
{
    [[nodiscard]] inline bool GetRendererUploadByteCount(
        size_t count, size_t stride, uint64_t& bytes) noexcept
    {
        static_assert(sizeof(size_t) <= sizeof(uint64_t), "upload sizes must fit uint64_t");
        if (stride == 0 || count > SIZE_MAX / stride)
            return false;
        bytes = uint64_t(count) * stride;
        return true;
    }

    // fixed 16-byte attribute packing. failure leaves all layout outputs unchanged.
    [[nodiscard]] inline bool AppendRendererUploadRange(size_t count, size_t stride,
        uint64_t& capacity, uint64_t& offset, uint64_t& size) noexcept
    {
        uint64_t bytes = 0;
        if (!GetRendererUploadByteCount(count, stride, bytes) || bytes > UINT64_MAX - 15)
            return false;
        const uint64_t aligned = (bytes + 15) & ~uint64_t(15);
        if (capacity > UINT64_MAX - aligned)
            return false;
        offset = capacity;
        size = aligned;
        capacity += aligned;
        return true;
    }

    [[nodiscard]] inline bool IsRendererUploadRangeValid(uint64_t sourceBytes,
        uint64_t consumedBytes, uint64_t destinationBytes, uint64_t destinationOffset) noexcept
    {
        return sourceBytes <= SIZE_MAX && consumedBytes <= sourceBytes &&
            destinationOffset <= destinationBytes && sourceBytes <= destinationBytes - destinationOffset;
    }
}
