#pragma once

#include <cstdint>
#include <vector>

namespace uvsr
{
    inline constexpr uint32_t VisibilityBlueNoiseSize = 64u;
    inline constexpr uint32_t VisibilityBlueNoiseLayerCount = 5u;
    inline constexpr uint32_t VisibilityBlueNoiseTexelCount =
        VisibilityBlueNoiseSize * VisibilityBlueNoiseSize;

    // Returns the Void Cluster Blue Noise scheduler's independent, toroidal
    // scalar rank layers. Each layer contains every progressive rank once.
    std::vector<uint16_t> GenerateVisibilityBlueNoise();
}
