#include "visibility_blue_noise.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>

namespace
{
    constexpr float kSpatialSigma = 1.9f;
    constexpr float kKernelCutoff = 0.005f;
    constexpr uint32_t kInitialPointCount =
        uvsr::VisibilityBlueNoiseTexelCount / 10u;
    constexpr uint32_t kLayerSeeds[
        uvsr::VisibilityBlueNoiseLayerCount] = {
        0x212ca684u, 0xaa3d4b62u, 0xbd62607cu, 0x785ad35eu,
        0x4909ee03u, 0xb1da2d4eu, 0x4f8a460du, 0x6f55a3bau
    };

    struct KernelTap
    {
        int x = 0;
        int y = 0;
        float weight = 0.f;
    };

    uint32_t Hash32(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    std::vector<KernelTap> BuildKernel()
    {
        const int radius = int(std::ceil(kSpatialSigma * std::sqrt(
            -2.f * std::log(kKernelCutoff))));
        std::vector<KernelTap> taps;
        for (int y = -radius; y <= radius; ++y)
        {
            for (int x = -radius; x <= radius; ++x)
            {
                const float distanceSquared = float(x * x + y * y);
                const float weight = std::exp(
                    -distanceSquared /
                    (2.f * kSpatialSigma * kSpatialSigma));
                if (weight >= kKernelCutoff)
                    taps.push_back({ x, y, weight });
            }
        }
        return taps;
    }

    class VoidAndClusterLayer
    {
    public:
        VoidAndClusterLayer(
            const std::vector<KernelTap>& kernel,
            uint32_t seed)
            : m_Kernel(kernel)
            , m_On(uvsr::VisibilityBlueNoiseTexelCount, false)
            , m_Energy(uvsr::VisibilityBlueNoiseTexelCount, 0.f)
            , m_Rank(
                uvsr::VisibilityBlueNoiseTexelCount,
                std::numeric_limits<uint32_t>::max())
            , m_TieBreak(uvsr::VisibilityBlueNoiseTexelCount)
        {
            std::vector<uint32_t> initialOrder(
                uvsr::VisibilityBlueNoiseTexelCount);
            std::iota(initialOrder.begin(), initialOrder.end(), 0u);
            for (uint32_t index = 0u;
                index < uvsr::VisibilityBlueNoiseTexelCount;
                ++index)
            {
                m_TieBreak[index] = Hash32(
                    index ^ seed ^ 0x68bc21ebu);
            }
            std::sort(initialOrder.begin(), initialOrder.end(),
                [seed](uint32_t left, uint32_t right)
                {
                    return Hash32(left ^ seed) < Hash32(right ^ seed);
                });

            for (uint32_t index = 0u;
                index < kInitialPointCount;
                ++index)
            {
                const uint32_t pixel = initialOrder[index];
                m_On[pixel] = true;
                Splat(pixel, 1.f);
            }
        }

        void Generate(uint16_t* destination)
        {
            RelaxInitialPattern();
            const std::vector<bool> initialPattern = m_On;

            uint32_t remaining = CountOn();
            while (remaining > 0u)
            {
                const uint32_t cluster = SelectTightestCluster();
                --remaining;
                m_On[cluster] = false;
                m_Rank[cluster] = remaining;
                Splat(cluster, -1.f);
            }

            m_On = initialPattern;
            RebuildEnergy();
            uint32_t onCount = CountOn();
            while (onCount < uvsr::VisibilityBlueNoiseTexelCount / 2u)
            {
                const uint32_t voidPixel = SelectLargestVoid();
                m_On[voidPixel] = true;
                m_Rank[voidPixel] = onCount;
                Splat(voidPixel, 1.f);
                ++onCount;
            }

            for (uint32_t index = 0u;
                index < uvsr::VisibilityBlueNoiseTexelCount;
                ++index)
            {
                m_On[index] = !m_On[index];
            }
            RebuildEnergy();
            remaining = CountOn();
            while (remaining > 0u)
            {
                const uint32_t cluster = SelectTightestCluster();
                m_On[cluster] = false;
                m_Rank[cluster] =
                    uvsr::VisibilityBlueNoiseTexelCount - remaining;
                Splat(cluster, -1.f);
                --remaining;
            }

            for (uint32_t index = 0u;
                index < uvsr::VisibilityBlueNoiseTexelCount;
                ++index)
            {
                assert(m_Rank[index] <
                    uvsr::VisibilityBlueNoiseTexelCount);
                // Store rank-bin centers, not 0/1 endpoints. This keeps every
                // scheduler result in [0,1) and avoids a clamped last stratum.
                destination[index] = uint16_t(
                    (uint64_t(m_Rank[index]) * 65536u + 32768u) /
                    uint64_t(uvsr::VisibilityBlueNoiseTexelCount));
            }
        }

    private:
        const std::vector<KernelTap>& m_Kernel;
        std::vector<bool> m_On;
        std::vector<float> m_Energy;
        std::vector<uint32_t> m_Rank;
        std::vector<uint32_t> m_TieBreak;

        void Splat(uint32_t pixel, float sign)
        {
            constexpr uint32_t mask =
                uvsr::VisibilityBlueNoiseSize - 1u;
            const uint32_t centerX =
                pixel & mask;
            const uint32_t centerY =
                pixel / uvsr::VisibilityBlueNoiseSize;
            for (const KernelTap& tap : m_Kernel)
            {
                const uint32_t x = uint32_t(
                    int(centerX) + tap.x) & mask;
                const uint32_t y = uint32_t(
                    int(centerY) + tap.y) & mask;
                m_Energy[y * uvsr::VisibilityBlueNoiseSize + x] +=
                    sign * tap.weight;
            }
        }

        void RebuildEnergy()
        {
            std::fill(m_Energy.begin(), m_Energy.end(), 0.f);
            for (uint32_t index = 0u;
                index < uvsr::VisibilityBlueNoiseTexelCount;
                ++index)
            {
                if (m_On[index])
                    Splat(index, 1.f);
            }
        }

        uint32_t CountOn() const
        {
            return uint32_t(std::count(
                m_On.begin(), m_On.end(), true));
        }

        uint32_t SelectTightestCluster() const
        {
            uint32_t selected = std::numeric_limits<uint32_t>::max();
            float selectedEnergy = -std::numeric_limits<float>::infinity();
            for (uint32_t index = 0u;
                index < uvsr::VisibilityBlueNoiseTexelCount;
                ++index)
            {
                if (!m_On[index])
                    continue;
                if (m_Energy[index] > selectedEnergy ||
                    (m_Energy[index] == selectedEnergy &&
                        (selected == std::numeric_limits<uint32_t>::max() ||
                            m_TieBreak[index] > m_TieBreak[selected])))
                {
                    selected = index;
                    selectedEnergy = m_Energy[index];
                }
            }
            assert(selected != std::numeric_limits<uint32_t>::max());
            return selected;
        }

        uint32_t SelectLargestVoid() const
        {
            uint32_t selected = std::numeric_limits<uint32_t>::max();
            float selectedEnergy = std::numeric_limits<float>::infinity();
            for (uint32_t index = 0u;
                index < uvsr::VisibilityBlueNoiseTexelCount;
                ++index)
            {
                if (m_On[index])
                    continue;
                if (m_Energy[index] < selectedEnergy ||
                    (m_Energy[index] == selectedEnergy &&
                        (selected == std::numeric_limits<uint32_t>::max() ||
                            m_TieBreak[index] > m_TieBreak[selected])))
                {
                    selected = index;
                    selectedEnergy = m_Energy[index];
                }
            }
            assert(selected != std::numeric_limits<uint32_t>::max());
            return selected;
        }

        void RelaxInitialPattern()
        {
            constexpr uint32_t maximumIterations =
                uvsr::VisibilityBlueNoiseTexelCount * 16u;
            for (uint32_t iteration = 0u;
                iteration < maximumIterations;
                ++iteration)
            {
                const uint32_t cluster = SelectTightestCluster();
                m_On[cluster] = false;
                Splat(cluster, -1.f);

                const uint32_t voidPixel = SelectLargestVoid();
                m_On[voidPixel] = true;
                Splat(voidPixel, 1.f);
                if (cluster == voidPixel)
                    return;
            }
            assert(false && "void-and-cluster relaxation did not converge");
        }
    };
}

namespace uvsr
{
    std::vector<uint16_t> GenerateVisibilityBlueNoise()
    {
        const std::vector<KernelTap> kernel = BuildKernel();
        std::vector<uint16_t> result(
            VisibilityBlueNoiseTexelCount * VisibilityBlueNoiseLayerCount);
        for (uint32_t layer = 0u;
            layer < VisibilityBlueNoiseLayerCount;
            ++layer)
        {
            VoidAndClusterLayer generator(kernel, kLayerSeeds[layer]);
            generator.Generate(result.data() +
                layer * VisibilityBlueNoiseTexelCount);
        }
        return result;
    }

    std::vector<uint8_t> LoadVisibilityFilterAdaptedNoise(
        const std::filesystem::path& path)
    {
        constexpr size_t expectedSize =
            size_t(VisibilityFilterAdaptedNoiseTexelCount) *
            VisibilityFilterAdaptedNoiseLayerCount;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() != std::streamoff(expectedSize))
            return {};

        std::vector<uint8_t> result(expectedSize);
        file.seekg(0, std::ios::beg);
        file.read(
            reinterpret_cast<char*>(result.data()),
            std::streamsize(result.size()));
        if (!file)
            return {};
        return result;
    }
}
