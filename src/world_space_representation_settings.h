#pragma once

#include <cstdint>

namespace uvsr
{
    enum class BvhBuildPreference : uint32_t
    {
        FastTrace,
        Balanced,
        FastBuild
    };

    enum class BlasUpdateMode : uint32_t
    {
        Rebuild,
        Refit
    };

    enum class TlasUpdateMode : uint32_t
    {
        Rebuild,
        Refit
    };

    struct WorldSpaceRepresentationSettings
    {
        BvhBuildPreference bvhBuildPreference =
            BvhBuildPreference::FastTrace;
        BlasUpdateMode blasUpdateMode = BlasUpdateMode::Refit;
        TlasUpdateMode tlasUpdateMode = TlasUpdateMode::Refit;
        bool allowRayTraversal = true;
    };

    enum class WorldSpaceRepresentationInvalidation : uint32_t
    {
        None,
        Tlas,
        BlasAndTlas
    };

    [[nodiscard]] inline constexpr
        WorldSpaceRepresentationInvalidation
        GetWorldSpaceRepresentationInvalidation(
            const WorldSpaceRepresentationSettings& previous,
            const WorldSpaceRepresentationSettings& next)
    {
        if (previous.bvhBuildPreference != next.bvhBuildPreference ||
            previous.blasUpdateMode != next.blasUpdateMode)
        {
            return WorldSpaceRepresentationInvalidation::BlasAndTlas;
        }
        if (previous.tlasUpdateMode != next.tlasUpdateMode)
            return WorldSpaceRepresentationInvalidation::Tlas;
        return WorldSpaceRepresentationInvalidation::None;
    }
}
