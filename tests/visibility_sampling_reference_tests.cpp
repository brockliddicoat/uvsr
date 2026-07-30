#include "visibility_blue_noise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

namespace
{
    constexpr double Pi = 3.1415926535897932384626433832795;

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

    struct VisibilityTraceFixtureSample
    {
        uint32_t intervalMask = 0u;
        uint32_t samplePixel = 0u;
        uint32_t side = 0u;
        float sourceRadiance = 0.f;
    };

    struct VisibilityTraceFixtureResult
    {
        uint32_t mask = 0u;
        uint32_t newlyClaimedBitCount = 0u;
        float rawAmbient = 1.f;
        float rawIndirect = 0.f;
        std::array<int32_t, 32> sourceOwner{};
    };

    uint32_t CountBits(uint32_t value)
    {
        uint32_t count = 0u;
        while (value != 0u)
        {
            value &= value - 1u;
            ++count;
        }
        return count;
    }

    VisibilityTraceFixtureResult RunVisibilityTraceFixture(
        const std::vector<VisibilityTraceFixtureSample>& samples,
        bool rejectDuplicatePixels,
        bool exitOnFullMask)
    {
        VisibilityTraceFixtureResult result;
        result.sourceOwner.fill(-1);
        std::array<uint32_t, 2> previousPixel{};
        std::array<bool, 2> hasPrevious{};
        for (size_t sampleIndex = 0u;
            sampleIndex < samples.size();
            ++sampleIndex)
        {
            const VisibilityTraceFixtureSample& sample = samples[sampleIndex];
            Require(sample.side < previousPixel.size(),
                "trace fixture uses a valid radial side");
            if (rejectDuplicatePixels && hasPrevious[sample.side] &&
                previousPixel[sample.side] == sample.samplePixel)
            {
                continue;
            }
            hasPrevious[sample.side] = true;
            previousPixel[sample.side] = sample.samplePixel;

            const uint32_t newlyVisible = sample.intervalMask & ~result.mask;
            result.mask |= sample.intervalMask;
            const uint32_t claimedCount = CountBits(newlyVisible);
            result.newlyClaimedBitCount += claimedCount;
            result.rawIndirect += sample.sourceRadiance *
                (float(claimedCount) / 32.f);
            for (uint32_t bit = 0u; bit < 32u; ++bit)
            {
                if ((newlyVisible & (uint32_t{ 1 } << bit)) != 0u)
                    result.sourceOwner[bit] = int32_t(sampleIndex);
            }
            if (exitOnFullMask && result.mask == ~uint32_t{ 0 })
                break;
        }
        result.rawAmbient = 1.f -
            float(CountBits(result.mask)) / 32.f;
        return result;
    }

    void RequireEquivalentVisibilityResult(
        const VisibilityTraceFixtureResult& left,
        const VisibilityTraceFixtureResult& right,
        const std::string& control)
    {
        Require(left.mask == right.mask,
            control + " preserves the final mask");
        Require(left.newlyClaimedBitCount == right.newlyClaimedBitCount,
            control + " preserves newly claimed bit counts");
        Require(std::abs(left.rawAmbient - right.rawAmbient) < 1e-7f,
            control + " preserves raw AO");
        Require(std::abs(left.rawIndirect - right.rawIndirect) < 1e-7f,
            control + " preserves raw GI");
        Require(left.sourceOwner == right.sourceOwner,
            control + " preserves near-to-far source ownership");
    }

    void ValidateExactTraceControls()
    {
        const std::vector<VisibilityTraceFixtureSample> duplicateFixture = {
            { 0x000000ffu, 17u, 0u, 0.25f },
            // Same side, pixel, interval, and source as the preceding tap. The
            // second OR is idempotent and owns no new sector when retained.
            { 0x000000ffu, 17u, 0u, 0.25f },
            { 0x0000ff00u, 31u, 0u, 0.5f },
            { 0xffff0000u, 42u, 1u, 0.75f }
        };
        RequireEquivalentVisibilityResult(
            RunVisibilityTraceFixture(duplicateFixture, true, false),
            RunVisibilityTraceFixture(duplicateFixture, false, false),
            "duplicate-pixel rejection");

        const std::vector<VisibilityTraceFixtureSample> fullMaskFixture = {
            { 0x0000ffffu, 11u, 0u, 0.25f },
            { 0xffff0000u, 12u, 1u, 0.75f },
            // This farther source cannot own a sector after the mask is full.
            { 0xffffffffu, 13u, 1u, 100.f }
        };
        RequireEquivalentVisibilityResult(
            RunVisibilityTraceFixture(fullMaskFixture, true, true),
            RunVisibilityTraceFixture(fullMaskFixture, true, false),
            "full-mask early exit");
    }

    bool Near(double left, double right, double tolerance = 1e-9)
    {
        return std::abs(left - right) <= tolerance;
    }

    double HorizonAcos(double horizonCosine)
    {
        return std::acos(std::clamp(horizonCosine, -1.0, 1.0));
    }

    double HorizonArc(double horizonAngle, double normalAngle)
    {
        return 0.25 * (
            std::cos(normalAngle) +
            2.0 * horizonAngle * std::sin(normalAngle) -
            std::cos(2.0 * horizonAngle - normalAngle));
    }

    double NumericalHorizonArc(double horizonAngle, double normalAngle)
    {
        constexpr uint32_t intervalCount = 32768u;
        const double interval = horizonAngle / double(intervalCount);
        double integral = 0.0;
        for (uint32_t index = 0u; index < intervalCount; ++index)
        {
            const double angle = (double(index) + 0.5) * interval;
            integral += std::sin(angle) *
                std::cos(angle - normalAngle) * interval;
        }
        return integral;
    }

    struct HorizonIntegral
    {
        double negativeAngle = 0.0;
        double positiveAngle = 0.0;
        double rawVisibility = 0.0;
        double boundedVisibility = 0.0;
    };

    HorizonIntegral EvaluateHorizonIntegral(
        double negativeHorizonCosine,
        double positiveHorizonCosine,
        double signedNormalAngle)
    {
        constexpr double halfPi = Pi * 0.5;
        HorizonIntegral result;
        result.negativeAngle = -HorizonAcos(negativeHorizonCosine);
        result.positiveAngle = HorizonAcos(positiveHorizonCosine);

        // Intersect the view-facing horizon interval with the cosine lobe of
        // the projected normal. This happens after acos so a negative horizon
        // cosine is not silently reinterpreted as an open, zero-cosine edge.
        result.negativeAngle = signedNormalAngle + std::max(
            result.negativeAngle - signedNormalAngle, -halfPi);
        result.positiveAngle = signedNormalAngle + std::min(
            result.positiveAngle - signedNormalAngle, halfPi);
        result.rawVisibility =
            HorizonArc(result.negativeAngle, signedNormalAngle) +
            HorizonArc(result.positiveAngle, signedNormalAngle);
        result.boundedVisibility = std::clamp(
            result.rawVisibility, 0.0, 1.0);
        return result;
    }

    void ValidateHorizonIntegralReference()
    {
        constexpr double halfPi = Pi * 0.5;
        for (double horizonCosine : { -1.0, -0.75, -0.25, -0.001 })
        {
            const double horizonAngle = HorizonAcos(horizonCosine);
            Require(horizonAngle > halfPi && horizonAngle <= Pi,
                "negative horizon cosines survive full-domain acos");
            Require(Near(std::cos(horizonAngle), horizonCosine),
                "full-domain acos preserves a negative horizon cosine");
        }
        Require(Near(HorizonAcos(-1.25), Pi) &&
                Near(HorizonAcos(1.25), 0.0),
            "horizon acos clamps numerical drift to the full unit domain");

        constexpr std::array<double, 5> signedNormalAngles = {
            -Pi / 3.0, -Pi / 6.0, 0.0, Pi / 6.0, Pi / 3.0
        };
        for (double normalAngle : signedNormalAngles)
        {
            const HorizonIntegral open = EvaluateHorizonIntegral(
                0.0, 0.0, normalAngle);
            const double expectedNegativeAngle = normalAngle > 0.0
                ? normalAngle - halfPi
                : -halfPi;
            Require(Near(open.negativeAngle, expectedNegativeAngle),
                "an open lower horizon is clipped only by the normal lobe");
            Require(Near(
                    HorizonArc(open.negativeAngle, normalAngle),
                    NumericalHorizonArc(open.negativeAngle, normalAngle),
                    1e-8),
                "the open lower-horizon analytic integral matches quadrature");
            Require(std::isfinite(open.rawVisibility) &&
                    open.rawVisibility >= 0.0 &&
                    open.rawVisibility <= 1.0 + 1e-12,
                "representative open-horizon integrals are finite and bounded");
            const HorizonIntegral mirrored = EvaluateHorizonIntegral(
                0.0, 0.0, -normalAngle);
            Require(Near(open.rawVisibility, mirrored.rawVisibility),
                "open-horizon visibility is symmetric about the view vector");
        }

        constexpr std::array<double, 7> horizonCosines = {
            -1.0, -0.75, -0.25, 0.0, 0.25, 0.75, 1.0
        };
        for (uint32_t normalIndex = 0u; normalIndex <= 32u; ++normalIndex)
        {
            const double normalAngle = -halfPi +
                double(normalIndex) * Pi / 32.0;
            for (double negativeCosine : horizonCosines)
            for (double positiveCosine : horizonCosines)
            {
                const HorizonIntegral value = EvaluateHorizonIntegral(
                    negativeCosine, positiveCosine, normalAngle);
                const HorizonIntegral mirrored = EvaluateHorizonIntegral(
                    positiveCosine, negativeCosine, -normalAngle);
                Require(std::isfinite(value.rawVisibility) &&
                        value.rawVisibility >= -1e-12 &&
                        value.rawVisibility <= halfPi + 1e-12,
                    "horizon integration remains finite within its analytic bound");
                Require(value.boundedVisibility >= 0.0 &&
                        value.boundedVisibility <= 1.0,
                    "the resolved visibility remains in the AO output domain");
                Require(Near(
                        value.rawVisibility, mirrored.rawVisibility, 1e-8),
                    "swapping horizon sides and normal sign preserves visibility");
            }
        }
    }
}

int main()
{
    ValidateExactTraceControls();
    ValidateHorizonIntegralReference();

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
