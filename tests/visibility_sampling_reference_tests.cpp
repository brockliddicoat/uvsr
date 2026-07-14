#include "visibility_blue_noise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
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
}

int main()
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

    std::cout << "Visibility sampling validation passed\n";
    return EXIT_SUCCESS;
}
