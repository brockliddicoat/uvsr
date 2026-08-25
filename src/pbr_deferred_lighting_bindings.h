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
    inline constexpr std::uint32_t PbrFlashlightRawClosestSlot = 23u;
    inline constexpr std::uint32_t PbrFlashlightDenoisedClosestSlot = 24u;
    inline constexpr std::uint32_t PbrSkyRawClosestSlot = 25u;
    inline constexpr std::uint32_t PbrSkyDenoisedClosestSlot = 26u;
    inline constexpr std::uint32_t PbrSunRawClosestSlot = 27u;
    inline constexpr std::uint32_t PbrSunDenoisedClosestSlot = 28u;

    inline constexpr std::array<std::uint32_t, 3>
        PbrVisibilitySlots = {
            PbrFlashlightVisibilitySlot,
            PbrSunVisibilitySlot,
            PbrSkyVisibilitySlot
        };
    inline constexpr std::array<std::uint32_t, 4>
        PbrMsaaSampleCounts = { 2u, 4u, 8u, 16u };
    inline constexpr std::uint8_t PbrNeutralVisibilityByte = 0xffu;

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

    template<class Resource>
    struct PbrClosestVisibilityResources
    {
        Resource flashlightRaw;
        Resource flashlightDenoised;
        Resource sunRaw;
        Resource sunDenoised;
        Resource skyRaw;
        Resource skyDenoised;
    };

    template<class Resource>
    [[nodiscard]] constexpr PbrClosestVisibilityResources<Resource>
        ResolvePbrClosestVisibilityResources(
            PbrClosestVisibilityResources<Resource> active,
            Resource whiteFallback)
    {
        return {
            active.flashlightRaw ? active.flashlightRaw : whiteFallback,
            active.flashlightDenoised
                ? active.flashlightDenoised
                : whiteFallback,
            active.sunRaw ? active.sunRaw : whiteFallback,
            active.sunDenoised ? active.sunDenoised : whiteFallback,
            active.skyRaw ? active.skyRaw : whiteFallback,
            active.skyDenoised ? active.skyDenoised : whiteFallback
        };
    }
}

#else

#define UVSR_PBR_FLASHLIGHT_VISIBILITY_REGISTER t20
#define UVSR_PBR_SUN_VISIBILITY_REGISTER t21
#define UVSR_PBR_SKY_VISIBILITY_REGISTER t22
#define UVSR_PBR_FLASHLIGHT_RAW_CLOSEST_REGISTER t23
#define UVSR_PBR_FLASHLIGHT_DENOISED_CLOSEST_REGISTER t24
#define UVSR_PBR_SKY_RAW_CLOSEST_REGISTER t25
#define UVSR_PBR_SKY_DENOISED_CLOSEST_REGISTER t26
#define UVSR_PBR_SUN_RAW_CLOSEST_REGISTER t27
#define UVSR_PBR_SUN_DENOISED_CLOSEST_REGISTER t28

#endif

#endif
