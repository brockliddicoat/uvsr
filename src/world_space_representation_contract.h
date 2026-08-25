#pragma once

#include "world_space_representation_settings.h"

#include <cstdint>

namespace uvsr
{
    inline constexpr std::uint32_t
        MaximumRayVisibilityGeometryMapOffset = 0x00ffffffu;

    [[nodiscard]] inline constexpr bool
        IsRayVisibilityGeometryMapOffsetSupported(std::uint64_t offset)
    {
        return offset <= MaximumRayVisibilityGeometryMapOffset;
    }

    [[nodiscard]] inline constexpr bool
        TryResolveRayVisibilityGeometryMapOffset(
            std::uint64_t entryCount,
            std::uint32_t& offset) noexcept
    {
        if (!IsRayVisibilityGeometryMapOffsetSupported(entryCount))
            return false;
        offset = static_cast<std::uint32_t>(entryCount);
        return true;
    }

    template<class Domain>
    [[nodiscard]] constexpr bool IsRayVisibilityMaterialDomainSupported(
        Domain domain,
        Domain opaque,
        Domain alphaTested) noexcept
    {
        return domain == opaque || domain == alphaTested;
    }

    template<class Domain>
    [[nodiscard]] constexpr bool IsRayVisibilityMaterialDomainOpaque(
        Domain domain,
        Domain opaque) noexcept
    {
        return domain == opaque;
    }

    [[nodiscard]] constexpr bool RetainsRayVisibilityGeometryMap(
        WorldSpaceRepresentationInvalidation invalidation) noexcept
    {
        return invalidation !=
            WorldSpaceRepresentationInvalidation::BlasAndTlas;
    }
}
