#pragma once

#include <cstdint>
#include <vector>

namespace uvsr
{
    inline constexpr uint32_t VisibilityBlueNoiseSize = 64u;
    inline constexpr uint32_t VisibilityBlueNoiseLayerCount = 8u;
    inline constexpr uint32_t VisibilityBlueNoiseTexelCount =
        VisibilityBlueNoiseSize * VisibilityBlueNoiseSize;

    // Returns independent, toroidal scalar rank layers in array-slice order.
    // Every layer contains each progressive rank exactly once.
    std::vector<uint16_t> GenerateVisibilityBlueNoise();
}
