#ifndef UVSR_SKY_VISIBILITY_APPLICATION_H
#define UVSR_SKY_VISIBILITY_APPLICATION_H

#define UVSR_SKY_VISIBILITY_APPLY_NEITHER 0u
#define UVSR_SKY_VISIBILITY_APPLY_DIFFUSE_IBL 1u
#define UVSR_SKY_VISIBILITY_APPLY_SPECULAR_IBL 2u
#define UVSR_SKY_VISIBILITY_APPLY_BOTH_IBL 3u

#ifdef __cplusplus

#include <cstdint>

namespace uvsr
{
    [[nodiscard]] constexpr std::uint32_t ResolveSkyVisibilityApplication(
        bool skyVisibilityAvailable,
        bool applyToDiffuseIbl,
        bool applyToSpecularIbl) noexcept
    {
        return skyVisibilityAvailable
            ? (applyToDiffuseIbl
                ? (applyToSpecularIbl
                    ? UVSR_SKY_VISIBILITY_APPLY_BOTH_IBL
                    : UVSR_SKY_VISIBILITY_APPLY_DIFFUSE_IBL)
                : (applyToSpecularIbl
                    ? UVSR_SKY_VISIBILITY_APPLY_SPECULAR_IBL
                    : UVSR_SKY_VISIBILITY_APPLY_NEITHER))
            : UVSR_SKY_VISIBILITY_APPLY_NEITHER;
    }

    [[nodiscard]] constexpr bool SkyVisibilityAppliesToDiffuseIbl(
        std::uint32_t application) noexcept
    {
        return application == UVSR_SKY_VISIBILITY_APPLY_DIFFUSE_IBL ||
            application == UVSR_SKY_VISIBILITY_APPLY_BOTH_IBL;
    }

    [[nodiscard]] constexpr bool SkyVisibilityAppliesToSpecularIbl(
        std::uint32_t application) noexcept
    {
        return application == UVSR_SKY_VISIBILITY_APPLY_SPECULAR_IBL ||
            application == UVSR_SKY_VISIBILITY_APPLY_BOTH_IBL;
    }
}

#else

bool SkyVisibilityAppliesToDiffuseIbl(uint application)
{
    return application == UVSR_SKY_VISIBILITY_APPLY_DIFFUSE_IBL ||
        application == UVSR_SKY_VISIBILITY_APPLY_BOTH_IBL;
}

bool SkyVisibilityAppliesToSpecularIbl(uint application)
{
    return application == UVSR_SKY_VISIBILITY_APPLY_SPECULAR_IBL ||
        application == UVSR_SKY_VISIBILITY_APPLY_BOTH_IBL;
}

#endif

#endif // UVSR_SKY_VISIBILITY_APPLICATION_H
