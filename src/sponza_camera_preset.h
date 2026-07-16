#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

#include <donut/core/math/math.h>

namespace donut::vfs
{
    class IFileSystem;
}

namespace uvsr
{
    enum class SponzaCameraLocation : std::uint32_t
    {
        Free,
        SimplifiedApproximation,
        Count
    };

    inline constexpr std::array<SponzaCameraLocation, 2>
        SelectableSponzaCameraLocations = {
            SponzaCameraLocation::Free,
            SponzaCameraLocation::SimplifiedApproximation
        };

    struct SponzaCameraPreset
    {
        const char* Id;
        const char* Label;
        donut::math::float3 Position;
        donut::math::float3 Direction;
        donut::math::float3 Up;
        donut::math::float3 Right;
        float VerticalFovDegrees;
        std::uint32_t ReferenceWidth;
        std::uint32_t ReferenceHeight;
    };

    const char* GetSponzaCameraLocationLabel(SponzaCameraLocation location);
    const SponzaCameraPreset* FindSponzaCameraPreset(SponzaCameraLocation location);
    SponzaCameraLocation ResolveSponzaCameraLocation(
        SponzaCameraLocation selectedLocation,
        bool benchmarkCameraRequested);
    const SponzaCameraPreset& GetDefaultSponzaCameraPreset();
    bool IsSponzaCameraAtPreset(
        const SponzaCameraPreset& preset,
        donut::math::float3 position,
        donut::math::float3 direction,
        donut::math::float3 up);

    const SponzaCameraPreset* FindStandardSponzaCameraPreset(
        donut::vfs::IFileSystem& fileSystem,
        const std::filesystem::path& currentScene);
}
