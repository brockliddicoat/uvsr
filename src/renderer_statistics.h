#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace uvsr
{
    [[nodiscard]] inline uint64_t CountSubmittedTriangleListPrimitives(
        uint32_t indexCount,
        uint32_t instanceCount) noexcept
    {
        return uint64_t(indexCount / 3u) *
            uint64_t(instanceCount);
    }

    [[nodiscard]] inline bool FormatTriangleCount(
        uint64_t triangleCount, char (&buffer)[32]) noexcept
    {
        int written = 0;
        if (triangleCount >= 999'950'000'000ull)
        {
            std::memcpy(buffer, "999.9b+ tris", sizeof("999.9b+ tris"));
            return true;
        }
        if (triangleCount >= 999'950'000ull)
        {
            written = std::snprintf(
                buffer,
                sizeof(buffer),
                "%.1fb tris",
                double(triangleCount) / 1'000'000'000.0);
        }
        else if (triangleCount >= 999'950ull)
        {
            written = std::snprintf(
                buffer,
                sizeof(buffer),
                "%.1fm tris",
                double(triangleCount) / 1'000'000.0);
        }
        else if (triangleCount >= 1'000ull)
        {
            written = std::snprintf(
                buffer,
                sizeof(buffer),
                "%.1fk tris",
                double(triangleCount) / 1'000.0);
        }
        else
        {
            written = std::snprintf(
                buffer,
                sizeof(buffer),
                "%llu tris",
                static_cast<unsigned long long>(triangleCount));
        }
        return written >= 0 && std::size_t(written) < sizeof(buffer);
    }
}
