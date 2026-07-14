#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace uvsr
{
    inline constexpr uint32_t VisibilityBlueNoiseSize = 64u;
    inline constexpr uint32_t VisibilityBlueNoiseLayerCount = 8u;
    inline constexpr uint32_t VisibilityBlueNoiseTexelCount =
        VisibilityBlueNoiseSize * VisibilityBlueNoiseSize;
    inline constexpr uint32_t VisibilityFilterAdaptedNoiseSize = 64u;
    inline constexpr uint32_t VisibilityFilterAdaptedNoiseLayerCount = 32u;
    inline constexpr uint32_t VisibilityFilterAdaptedNoiseTexelCount =
        VisibilityFilterAdaptedNoiseSize * VisibilityFilterAdaptedNoiseSize;

    // Returns independent, toroidal scalar rank layers in array-slice order.
    // Every layer contains each progressive rank exactly once.
    std::vector<uint16_t> GenerateVisibilityBlueNoise();

    // Loads a scalar-uniform 64x64x32 rank field that was optimized offline
    // for a Gaussian spatial filter and alpha=0.35 EMA temporal filter.
    // An empty result reports a missing or malformed field.
    std::vector<uint8_t> LoadVisibilityFilterAdaptedNoise(
        const std::filesystem::path& path);
}
