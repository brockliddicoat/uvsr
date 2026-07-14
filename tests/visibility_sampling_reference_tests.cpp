#include "visibility_blue_noise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Visibility sampling validation failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    double Correlation(const uint16_t* left, const uint16_t* right)
    {
        constexpr uint32_t count = uvsr::VisibilityBlueNoiseTexelCount;
        const double leftMean = std::accumulate(
            left, left + count, 0.0) / double(count);
        const double rightMean = std::accumulate(
            right, right + count, 0.0) / double(count);
        double covariance = 0.0;
        double leftVariance = 0.0;
        double rightVariance = 0.0;
        for (uint32_t index = 0u; index < count; ++index)
        {
            const double centeredLeft = double(left[index]) - leftMean;
            const double centeredRight = double(right[index]) - rightMean;
            covariance += centeredLeft * centeredRight;
            leftVariance += centeredLeft * centeredLeft;
            rightVariance += centeredRight * centeredRight;
        }
        return covariance / std::sqrt(leftVariance * rightVariance);
    }

    bool IsIndependentContributionDiscovery(
        float centerSeed,
        float neighboringSeed,
        float independentImportance)
    {
        return centerSeed > 0.f ||
            neighboringSeed * 0.75f <= independentImportance + 1e-6f;
    }

    double Variance(const std::vector<double>& values)
    {
        const double mean = std::accumulate(
            values.begin(), values.end(), 0.0) / double(values.size());
        double variance = 0.0;
        for (double value : values)
        {
            const double centered = value - mean;
            variance += centered * centered;
        }
        return variance / double(values.size());
    }

    std::vector<double> Normalize(const std::vector<uint8_t>& values)
    {
        std::vector<double> result(values.size());
        std::transform(values.begin(), values.end(), result.begin(),
            [](uint8_t value) { return double(value) / 255.0; });
        return result;
    }

    std::vector<double> GaussianFilter(
        const std::vector<double>& values)
    {
        constexpr int radius = 4;
        std::array<double, radius * 2 + 1> weights{};
        double totalWeight = 0.0;
        for (int offset = -radius; offset <= radius; ++offset)
        {
            const double weight = std::exp(
                -double(offset * offset) * 0.5);
            weights[offset + radius] = weight;
            totalWeight += weight;
        }
        for (double& weight : weights)
            weight /= totalWeight;

        constexpr uint32_t size =
            uvsr::VisibilityFilterAdaptedNoiseSize;
        constexpr uint32_t layerSize =
            uvsr::VisibilityFilterAdaptedNoiseTexelCount;
        std::vector<double> horizontal(values.size(), 0.0);
        std::vector<double> result(values.size(), 0.0);
        for (uint32_t layer = 0u;
            layer < uvsr::VisibilityFilterAdaptedNoiseLayerCount;
            ++layer)
        {
            for (uint32_t y = 0u; y < size; ++y)
            {
                for (uint32_t x = 0u; x < size; ++x)
                {
                    const size_t destination =
                        layer * layerSize + y * size + x;
                    for (int offset = -radius; offset <= radius; ++offset)
                    {
                        const uint32_t sourceX =
                            uint32_t(int(x) + offset) & (size - 1u);
                        horizontal[destination] +=
                            values[layer * layerSize + y * size + sourceX] *
                            weights[offset + radius];
                    }
                }
            }
        }
        for (uint32_t layer = 0u;
            layer < uvsr::VisibilityFilterAdaptedNoiseLayerCount;
            ++layer)
        {
            for (uint32_t y = 0u; y < size; ++y)
            {
                for (uint32_t x = 0u; x < size; ++x)
                {
                    const size_t destination =
                        layer * layerSize + y * size + x;
                    for (int offset = -radius; offset <= radius; ++offset)
                    {
                        const uint32_t sourceY =
                            uint32_t(int(y) + offset) & (size - 1u);
                        result[destination] += horizontal[
                            layer * layerSize + sourceY * size + x] *
                            weights[offset + radius];
                    }
                }
            }
        }
        return result;
    }

    std::vector<double> ExponentialFilter(
        const std::vector<double>& values)
    {
        constexpr double alpha = 0.35;
        constexpr uint32_t layerCount =
            uvsr::VisibilityFilterAdaptedNoiseLayerCount;
        constexpr uint32_t layerSize =
            uvsr::VisibilityFilterAdaptedNoiseTexelCount;
        std::array<double, layerCount> weights{};
        double totalWeight = 0.0;
        for (uint32_t offset = 0u; offset < layerCount; ++offset)
        {
            weights[offset] = alpha * std::pow(1.0 - alpha, offset);
            totalWeight += weights[offset];
        }
        for (double& weight : weights)
            weight /= totalWeight;

        std::vector<double> result(values.size(), 0.0);
        for (uint32_t layer = 0u; layer < layerCount; ++layer)
        {
            for (uint32_t pixel = 0u; pixel < layerSize; ++pixel)
            {
                const size_t destination = layer * layerSize + pixel;
                for (uint32_t offset = 0u; offset < layerCount; ++offset)
                {
                    const uint32_t sourceLayer =
                        (layer - offset) & (layerCount - 1u);
                    result[destination] +=
                        values[sourceLayer * layerSize + pixel] *
                        weights[offset];
                }
            }
        }
        return result;
    }

    double ShiftedVolumeCorrelation(
        const std::vector<uint8_t>& values,
        std::array<uint32_t, 2> leftOffset,
        std::array<uint32_t, 2> rightOffset)
    {
        constexpr uint32_t size =
            uvsr::VisibilityFilterAdaptedNoiseSize;
        constexpr uint32_t layerSize =
            uvsr::VisibilityFilterAdaptedNoiseTexelCount;
        constexpr double mean = 127.5;
        double covariance = 0.0;
        double leftVariance = 0.0;
        double rightVariance = 0.0;
        for (uint32_t layer = 0u;
            layer < uvsr::VisibilityFilterAdaptedNoiseLayerCount;
            ++layer)
        {
            for (uint32_t y = 0u; y < size; ++y)
            {
                for (uint32_t x = 0u; x < size; ++x)
                {
                    const double left = double(values[
                        layer * layerSize +
                        ((y + leftOffset[1]) & 63u) * size +
                        ((x + leftOffset[0]) & 63u)]) - mean;
                    const double right = double(values[
                        layer * layerSize +
                        ((y + rightOffset[1]) & 63u) * size +
                        ((x + rightOffset[0]) & 63u)]) - mean;
                    covariance += left * right;
                    leftVariance += left * left;
                    rightVariance += right * right;
                }
            }
        }
        return covariance / std::sqrt(leftVariance * rightVariance);
    }
}

int main(int argc, char* argv[])
{
    const std::vector<uint16_t> noise =
        uvsr::GenerateVisibilityBlueNoise();
    Require(noise.size() ==
        size_t(uvsr::VisibilityBlueNoiseTexelCount) *
            uvsr::VisibilityBlueNoiseLayerCount,
        "all independent rank layers are generated");

    constexpr std::array<uint32_t, 3> thresholds = {
        8192u, 16384u, 32768u
    };
    for (uint32_t layer = 0u;
        layer < uvsr::VisibilityBlueNoiseLayerCount;
        ++layer)
    {
        const uint16_t* values = noise.data() +
            layer * uvsr::VisibilityBlueNoiseTexelCount;
        std::vector<uint16_t> sorted(
            values,
            values + uvsr::VisibilityBlueNoiseTexelCount);
        std::sort(sorted.begin(), sorted.end());
        for (uint32_t rank = 0u;
            rank < uvsr::VisibilityBlueNoiseTexelCount;
            ++rank)
        {
            const uint16_t expected = uint16_t(
                (uint64_t(rank) * 65536u + 32768u) /
                uvsr::VisibilityBlueNoiseTexelCount);
            Require(sorted[rank] == expected,
                "each layer contains every centered rank exactly once");
        }

        for (uint32_t threshold : thresholds)
        {
            const int expectedPerLine = int(
                uvsr::VisibilityBlueNoiseSize * threshold / 65536u);
            int maximumRowDeviation = 0;
            int maximumColumnDeviation = 0;
            for (uint32_t line = 0u;
                line < uvsr::VisibilityBlueNoiseSize;
                ++line)
            {
                int rowCount = 0;
                int columnCount = 0;
                for (uint32_t coordinate = 0u;
                    coordinate < uvsr::VisibilityBlueNoiseSize;
                    ++coordinate)
                {
                    rowCount += values[
                        line * uvsr::VisibilityBlueNoiseSize +
                        coordinate] < threshold;
                    columnCount += values[
                        coordinate * uvsr::VisibilityBlueNoiseSize +
                        line] < threshold;
                }
                maximumRowDeviation = std::max(
                    maximumRowDeviation,
                    std::abs(rowCount - expectedPerLine));
                maximumColumnDeviation = std::max(
                    maximumColumnDeviation,
                    std::abs(columnCount - expectedPerLine));
            }
            Require(maximumRowDeviation <= 8,
                "progressive prefixes do not collapse into row bands");
            Require(maximumColumnDeviation <= 8,
                "progressive prefixes do not collapse into column bands");
        }
    }

    for (uint32_t leftLayer = 0u;
        leftLayer < uvsr::VisibilityBlueNoiseLayerCount;
        ++leftLayer)
    {
        for (uint32_t rightLayer = leftLayer + 1u;
            rightLayer < uvsr::VisibilityBlueNoiseLayerCount;
            ++rightLayer)
        {
            const double correlation = Correlation(
                noise.data() + leftLayer *
                    uvsr::VisibilityBlueNoiseTexelCount,
                noise.data() + rightLayer *
                    uvsr::VisibilityBlueNoiseTexelCount);
            Require(std::abs(correlation) < 0.15,
                "semantic random dimensions are decorrelated");
        }
    }

    constexpr std::array<std::array<uint32_t, 2>,
        uvsr::VisibilityBlueNoiseLayerCount> temporalSteps = {{
        {{ 13u, 29u }}, {{ 31u, 11u }},
        {{ 17u, 27u }}, {{ 23u, 19u }},
        {{ 7u, 25u }}, {{ 29u, 15u }},
        {{ 21u, 31u }}, {{ 11u, 23u }}
    }};
    for (uint32_t layer = 0u;
        layer < uvsr::VisibilityBlueNoiseLayerCount;
        ++layer)
    {
        std::array<uint32_t, 8> histogram{};
        const uint16_t* values = noise.data() +
            layer * uvsr::VisibilityBlueNoiseTexelCount;
        for (uint32_t probe = 0u; probe < 16u; ++probe)
        {
            const uint32_t probeX = (probe * 7u + 3u) & 63u;
            const uint32_t probeY = (probe * 13u + 5u) & 63u;
            for (uint32_t frame = 0u; frame < 64u; ++frame)
            {
                const uint32_t x = (probeX +
                    temporalSteps[layer][0] * frame) & 63u;
                const uint32_t y = (probeY +
                    temporalSteps[layer][1] * frame) & 63u;
                const uint32_t bin = std::min(
                    uint32_t(values[y * uvsr::VisibilityBlueNoiseSize + x]) *
                        8u / 65536u,
                    7u);
                ++histogram[bin];
            }
        }
        for (uint32_t count : histogram)
        {
            Require(count >= 80u && count <= 176u,
                "toroidal temporal traversal covers the scalar rank range");
        }
    }

    Require(IsIndependentContributionDiscovery(0.f, 0.f, 0.f),
        "a baseline discovery may create an initial contribution seed");
    Require(!IsIndependentContributionDiscovery(0.f, 1.f, 0.f),
        "neighbor-driven work cannot become a second-hop seed");
    Require(IsIndependentContributionDiscovery(1.f, 1.f, 0.f),
        "a reprojected center seed can persist without spatial dilation");
    Require(IsIndependentContributionDiscovery(0.f, 1.f, 1.f),
        "independently difficult pixels remain eligible contribution seeds");

    Require(argc == 2,
        "the filter-adapted rank-field path is supplied by CTest");
    std::vector<uint8_t> filterAdapted =
        uvsr::LoadVisibilityFilterAdaptedNoise(argv[1]);
    Require(filterAdapted.size() ==
        size_t(uvsr::VisibilityFilterAdaptedNoiseTexelCount) *
            uvsr::VisibilityFilterAdaptedNoiseLayerCount,
        "the complete filter-adapted volume loads");

    for (uint32_t layer = 0u;
        layer < uvsr::VisibilityFilterAdaptedNoiseLayerCount;
        ++layer)
    {
        std::array<uint32_t, 256> histogram{};
        const size_t offset = size_t(layer) *
            uvsr::VisibilityFilterAdaptedNoiseTexelCount;
        for (uint32_t pixel = 0u;
            pixel < uvsr::VisibilityFilterAdaptedNoiseTexelCount;
            ++pixel)
        {
            ++histogram[filterAdapted[offset + pixel]];
        }
        for (uint32_t count : histogram)
            Require(count == 16u,
                "FAST optimization preserves each slice's uniform histogram");
    }

    constexpr std::array<std::array<uint32_t, 2>, 8> semanticOffsets = {{
        {{ 0u, 0u }}, {{ 48u, 36u }},
        {{ 32u, 8u }}, {{ 16u, 45u }},
        {{ 1u, 17u }}, {{ 49u, 54u }},
        {{ 33u, 26u }}, {{ 18u, 63u }}
    }};
    for (uint32_t left = 0u; left < semanticOffsets.size(); ++left)
    {
        for (uint32_t right = left + 1u;
            right < semanticOffsets.size();
            ++right)
        {
            Require(std::abs(ShiftedVolumeCorrelation(
                filterAdapted,
                semanticOffsets[left],
                semanticOffsets[right])) < 0.03,
                "R2 semantic reads remain decorrelated");
        }
    }

    std::array<bool, 4096> visitedCycleOffsets{};
    for (uint32_t cycle = 0u; cycle < visitedCycleOffsets.size(); ++cycle)
    {
        const uint32_t offset = (1741u + 2531u * cycle) & 4095u;
        Require(!visitedCycleOffsets[offset],
            "low-discrepancy cycle offsets do not repeat early");
        visitedCycleOffsets[offset] = true;
    }

    std::vector<uint8_t> shuffled = filterAdapted;
    std::mt19937 random(0xa5a5a5a5u);
    for (uint32_t layer = 0u;
        layer < uvsr::VisibilityFilterAdaptedNoiseLayerCount;
        ++layer)
    {
        auto first = shuffled.begin() + size_t(layer) *
            uvsr::VisibilityFilterAdaptedNoiseTexelCount;
        std::shuffle(first,
            first + uvsr::VisibilityFilterAdaptedNoiseTexelCount,
            random);
    }
    const std::vector<double> optimizedValues = Normalize(filterAdapted);
    const std::vector<double> shuffledValues = Normalize(shuffled);
    const std::vector<double> optimizedSpatial =
        GaussianFilter(optimizedValues);
    const std::vector<double> shuffledSpatial =
        GaussianFilter(shuffledValues);
    const std::vector<double> optimizedTemporal =
        ExponentialFilter(optimizedValues);
    const std::vector<double> shuffledTemporal =
        ExponentialFilter(shuffledValues);
    Require(Variance(optimizedSpatial) < Variance(shuffledSpatial) * 0.25,
        "the rank field suppresses Gaussian-filtered spatial error");
    Require(Variance(optimizedTemporal) < Variance(shuffledTemporal) * 0.5,
        "the rank field suppresses EMA-filtered temporal error");
    Require(Variance(ExponentialFilter(optimizedSpatial)) <
        Variance(ExponentialFilter(shuffledSpatial)) * 0.1,
        "the rank field suppresses combined reconstruction error");

    std::cout << "Visibility sampling validation passed\n";
    return EXIT_SUCCESS;
}
