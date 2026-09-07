#ifndef UVSR_PBR_DEFERRED_LIGHTING_BINDINGS_H
#define UVSR_PBR_DEFERRED_LIGHTING_BINDINGS_H

#ifdef __cplusplus

#include <array>
#include <cstdint>

namespace uvsr
{
    inline constexpr std::uint32_t PbrFlashlightVisibilitySlot = 20u;
    inline constexpr std::uint32_t PbrSunVisibilitySlot = 21u;
    inline constexpr std::uint32_t PbrSkyVisibilitySlot = 22u;

    inline constexpr std::array<std::uint32_t, 3>
        PbrVisibilitySlots = {
            PbrFlashlightVisibilitySlot,
            PbrSunVisibilitySlot,
            PbrSkyVisibilitySlot
        };
    template<class Resource>
    struct PbrVisibilityResources
    {
        Resource flashlight;
        Resource sun;
        Resource sky;
    };

    template<class Resource>
    [[nodiscard]] constexpr PbrVisibilityResources<Resource>
        ResolvePbrVisibilityResources(
            PbrVisibilityResources<Resource> active,
            Resource whiteFallback)
    {
        return {
            active.flashlight ? active.flashlight : whiteFallback,
            active.sun ? active.sun : whiteFallback,
            active.sky ? active.sky : whiteFallback
        };
    }


}

#else

#define UVSR_PBR_FLASHLIGHT_VISIBILITY_REGISTER t20
#define UVSR_PBR_SUN_VISIBILITY_REGISTER t21
#define UVSR_PBR_SKY_VISIBILITY_REGISTER t22

#endif

#endif
