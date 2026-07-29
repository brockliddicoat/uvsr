#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace uvsr
{
    [[nodiscard]] inline uint64_t CountSubmittedTriangleListPrimitives(
        uint32_t indexCount,
        uint32_t instanceCount) noexcept
    {
        return uint64_t(indexCount / 3u) *
            uint64_t(instanceCount);
    }

    [[nodiscard]] inline std::string FormatTriangleCount(
        uint64_t triangleCount)
    {
        std::array<char, 32> buffer{};
        if (triangleCount >= 999'950'000'000ull)
        {
            return "999.9b+ tris";
        }
        if (triangleCount >= 999'950'000ull)
        {
            std::snprintf(
                buffer.data(),
                buffer.size(),
                "%.1fb tris",
                double(triangleCount) / 1'000'000'000.0);
        }
        else if (triangleCount >= 999'950ull)
        {
            std::snprintf(
                buffer.data(),
                buffer.size(),
                "%.1fm tris",
                double(triangleCount) / 1'000'000.0);
        }
        else if (triangleCount >= 1'000ull)
        {
            std::snprintf(
                buffer.data(),
                buffer.size(),
                "%.1fk tris",
                double(triangleCount) / 1'000.0);
        }
        else
        {
            std::snprintf(
                buffer.data(),
                buffer.size(),
                "%llu tris",
                static_cast<unsigned long long>(triangleCount));
        }
        return buffer.data();
    }
}
