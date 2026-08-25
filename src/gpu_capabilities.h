#pragma once

#include <cstdint>

#ifndef UVSR_REQUIRED_SHADER_MODEL
#error UVSR_REQUIRED_SHADER_MODEL must match the direct DXC target profile.
#endif

namespace uvsr
{
    constexpr uint32_t MinimumShaderModel = UVSR_REQUIRED_SHADER_MODEL;
    constexpr uint32_t MinimumD3DFeatureLevel = 0xb000u;
    constexpr uint32_t MinimumBindlessResourceBindingTier = 2u;
    constexpr uint32_t MinimumInlineRayTracingTier = 11u;

    [[nodiscard]] constexpr bool SupportsRequiredShaderModel(
        uint32_t highestShaderModel) noexcept
    {
        return highestShaderModel >= MinimumShaderModel;
    }

    [[nodiscard]] constexpr bool SupportsRequiredFeatureLevel(
        uint32_t highestFeatureLevel) noexcept
    {
        return highestFeatureLevel >= MinimumD3DFeatureLevel;
    }

    [[nodiscard]] constexpr bool SupportsBindlessResourceTables(
        uint32_t resourceBindingTier) noexcept
    {
        return resourceBindingTier >=
            MinimumBindlessResourceBindingTier;
    }

    [[nodiscard]] constexpr bool SupportsOptionalRayQueryRendering(
        uint32_t resourceBindingTier,
        uint32_t rayTracingTier) noexcept
    {
        return SupportsBindlessResourceTables(resourceBindingTier) &&
            rayTracingTier >= MinimumInlineRayTracingTier;
    }
}
