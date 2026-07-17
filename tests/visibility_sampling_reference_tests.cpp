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
#include <random>
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

    uint32_t ActivisionSpatialDirectionIndex(uint32_t x, uint32_t y)
    {
        return ((((x + y) & 3u) << 2u) + (x & 3u));
    }

    uint32_t ActivisionSpatialOffsetQuarter(uint32_t x, uint32_t y)
    {
        return (y + 4u - x) & 3u;
    }

    uint32_t ActivisionTemporalDirectionDegrees(uint32_t phase)
    {
        constexpr std::array<uint32_t, 6> rotations = {
            60u, 300u, 180u, 240u, 120u, 0u
        };
        return rotations[phase % rotations.size()];
    }

    uint32_t ActivisionTemporalSliceAxisDegrees(uint32_t phase)
    {
        // Slide 93 stores rotation / 360. The bidirectional slice multiplies
        // that normalized value by pi, so its physical axis rotation is half
        // of the published full-turn degree value.
        return ActivisionTemporalDirectionDegrees(phase) / 2u;
    }

    uint32_t ActivisionTemporalOffsetQuarter(uint32_t phase)
    {
        constexpr std::array<uint32_t, 4> offsets = { 0u, 2u, 1u, 3u };
        return offsets[(phase / 6u) & 3u];
    }

    double ActivisionFadedCandidateHorizon(
        double candidateHorizon,
        double distanceWeight)
    {
        const double weight = std::clamp(distanceWeight, 0.0, 1.0);
        return -1.0 + (candidateHorizon + 1.0) * weight;
    }

    double ActivisionSpatialDepthWeight(double relativeDepthError)
    {
        return std::clamp(1.0 - relativeDepthError * 10.0, 0.0, 1.0);
    }

    bool ActivisionTemporalDepthAccepted(
        double currentDepth,
        double previousDepth)
    {
        const bool currentValid = std::isfinite(currentDepth) &&
            currentDepth > 0.0 && currentDepth < 65503.0;
        const bool previousValid = std::isfinite(previousDepth) &&
            previousDepth > 0.0 && previousDepth < 65503.0;
        const double relativeDepthError =
            std::abs(previousDepth - currentDepth) /
            std::max({ std::abs(previousDepth),
                std::abs(currentDepth), 1e-4 });
        return currentValid && previousValid &&
            std::isfinite(relativeDepthError) &&
            relativeDepthError <= 0.10;
    }

    bool SourceProfileSupportsViewport(
        double originX,
        double originY,
        double viewportWidth,
        double viewportHeight,
        uint32_t textureWidth,
        uint32_t textureHeight)
    {
        return originX == 0.0 && originY == 0.0 &&
            viewportWidth == double(textureWidth) &&
            viewportHeight == double(textureHeight);
    }

    struct ActivisionTemporalReprojection
    {
        std::array<double, 2> previousSamplingTexel{};
        bool valid = false;
    };

    ActivisionTemporalReprojection EvaluateActivisionTemporalReprojection(
        uint32_t fullPixelX,
        uint32_t fullPixelY,
        double motionX,
        double motionY,
        uint32_t scale,
        uint32_t fullWidth,
        uint32_t fullHeight)
    {
        ActivisionTemporalReprojection result;
        if (scale == 0u || fullWidth == 0u || fullHeight == 0u)
            return result;

        const uint32_t samplingWidth = (fullWidth - 1u) / scale + 1u;
        const uint32_t samplingHeight = (fullHeight - 1u) / scale + 1u;
        const std::array<double, 2> previousFullPosition = {
            double(fullPixelX) + 0.5 + motionX,
            double(fullPixelY) + 0.5 + motionY
        };
        result.previousSamplingTexel = {
            previousFullPosition[0] / double(scale) - 0.5,
            previousFullPosition[1] / double(scale) - 0.5
        };
        result.valid =
            std::isfinite(result.previousSamplingTexel[0]) &&
            std::isfinite(result.previousSamplingTexel[1]) &&
            previousFullPosition[0] >= 0.0 &&
            previousFullPosition[1] >= 0.0 &&
            previousFullPosition[0] < double(fullWidth) &&
            previousFullPosition[1] < double(fullHeight) &&
            result.previousSamplingTexel[0] >= -0.5 &&
            result.previousSamplingTexel[1] >= -0.5 &&
            result.previousSamplingTexel[0] <
                double(samplingWidth) - 0.5 &&
            result.previousSamplingTexel[1] <
                double(samplingHeight) - 0.5;
        return result;
    }

    double ActivisionDepthSlope(
        double centerDepth,
        double negativeDepth,
        double positiveDepth)
    {
        const auto valid = [](double depth)
        {
            return std::isfinite(depth) &&
                depth > 0.0 && depth < 65503.0;
        };
        const bool negativeValid = valid(negativeDepth);
        const bool positiveValid = valid(positiveDepth);
        if (negativeValid && positiveValid)
            return 0.5 * (positiveDepth - negativeDepth);
        if (positiveValid)
            return positiveDepth - centerDepth;
        if (negativeValid)
            return centerDepth - negativeDepth;
        return 0.0;
    }

    uint32_t FloatBits(float value)
    {
        uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    float FloatFromBits(uint32_t bits)
    {
        float value = 0.f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    uint32_t PackActivisionDepthGuide(float depth, uint32_t offset)
    {
        return (FloatBits(depth) & ~3u) | (offset & 3u);
    }

    float UnpackActivisionDepth(uint32_t packedDepthGuide)
    {
        return FloatFromBits(packedDepthGuide & ~3u);
    }

    std::array<uint32_t, 2> ActivisionGuidePixel(
        uint32_t samplingX,
        uint32_t samplingY,
        uint32_t packedDepthGuide,
        uint32_t fullWidth,
        uint32_t fullHeight)
    {
        const uint32_t offset = packedDepthGuide & 3u;
        return {
            std::min(samplingX * 2u + (offset & 1u), fullWidth - 1u),
            std::min(samplingY * 2u + (offset >> 1u), fullHeight - 1u)
        };
    }

    struct ActivisionDepthCandidate
    {
        float deviceDepth;
        float linearDepth;
    };

    uint32_t SelectActivisionDepthGuide(
        const std::array<ActivisionDepthCandidate, 4>& candidates,
        bool reverseDepth)
    {
        float nearestDepth = 65504.f;
        uint32_t nearestOffset = 0u;
        for (uint32_t offset = 0u; offset < candidates.size(); ++offset)
        {
            const ActivisionDepthCandidate& candidate = candidates[offset];
            const bool validDeviceDepth = std::isfinite(candidate.deviceDepth) &&
                (reverseDepth
                    ? candidate.deviceDepth > 0.f &&
                        candidate.deviceDepth <= 1.f
                    : candidate.deviceDepth >= 0.f &&
                        candidate.deviceDepth < 1.f);
            if (validDeviceDepth &&
                candidate.linearDepth < nearestDepth)
            {
                nearestDepth = candidate.linearDepth;
                nearestOffset = offset;
            }
        }
        return PackActivisionDepthGuide(nearestDepth, nearestOffset);
    }

    void ValidateActivisionScheduleReference()
    {
        constexpr std::array<uint32_t, 16> expectedSpatialDirections = {
            0u, 5u, 10u, 15u,
            4u, 9u, 14u, 3u,
            8u, 13u, 2u, 7u,
            12u, 1u, 6u, 11u
        };
        constexpr std::array<uint32_t, 16> expectedSpatialOffsets = {
            0u, 3u, 2u, 1u,
            1u, 0u, 3u, 2u,
            2u, 1u, 0u, 3u,
            3u, 2u, 1u, 0u
        };
        std::array<uint32_t, 16> spatialDirectionHistogram{};
        std::array<uint32_t, 4> spatialOffsetHistogram{};
        for (uint32_t y = 0u; y < 4u; ++y)
        for (uint32_t x = 0u; x < 4u; ++x)
        {
            const size_t pixel = size_t(y) * 4u + x;
            const uint32_t direction =
                ActivisionSpatialDirectionIndex(x, y);
            const uint32_t offset =
                ActivisionSpatialOffsetQuarter(x, y);
            Require(direction == expectedSpatialDirections[pixel],
                "the Activision 4x4 spatial direction pattern is exact");
            Require(offset == expectedSpatialOffsets[pixel],
                "the Activision 4x4 spatial radial-offset pattern is exact");
            ++spatialDirectionHistogram[direction];
            ++spatialOffsetHistogram[offset];
        }
        Require(std::all_of(
                spatialDirectionHistogram.begin(),
                spatialDirectionHistogram.end(),
                [](uint32_t count) { return count == 1u; }),
            "the 4x4 spatial tile covers every direction exactly once");
        Require(std::all_of(
                spatialOffsetHistogram.begin(),
                spatialOffsetHistogram.end(),
                [](uint32_t count) { return count == 4u; }),
            "the 4x4 spatial tile balances all four radial offsets");

        constexpr std::array<uint32_t, 6> temporalDirectionDegrees = {
            60u, 300u, 180u, 240u, 120u, 0u
        };
        constexpr std::array<uint32_t, 6> temporalSliceAxisDegrees = {
            30u, 150u, 90u, 120u, 60u, 0u
        };
        constexpr std::array<uint32_t, 4> temporalOffsetQuarters = {
            0u, 2u, 1u, 3u
        };
        std::array<uint32_t, 6> temporalDirectionHistogram{};
        std::array<uint32_t, 4> temporalOffsetHistogram{};
        std::array<bool, 180> visitedSliceAxes{};
        std::array<std::array<bool, 4>, 6> visitedPairs{};
        for (uint32_t phase = 0u; phase < 24u; ++phase)
        {
            const uint32_t directionIndex = phase % 6u;
            const uint32_t offsetIndex = (phase / 6u) & 3u;
            Require(ActivisionTemporalDirectionDegrees(phase) ==
                    temporalDirectionDegrees[directionIndex],
                "the six Activision temporal direction rotations are exact");
            Require(ActivisionTemporalSliceAxisDegrees(phase) ==
                    temporalSliceAxisDegrees[directionIndex],
                "full-turn fractions map to six distinct pi-domain slice axes");
            Require(ActivisionTemporalOffsetQuarter(phase) ==
                    temporalOffsetQuarters[offsetIndex],
                "the four Activision temporal radial offsets are exact");
            Require(!visitedPairs[directionIndex][offsetIndex],
                "the 24-phase schedule does not repeat a direction-offset pair");
            visitedPairs[directionIndex][offsetIndex] = true;
            visitedSliceAxes[temporalSliceAxisDegrees[directionIndex]] = true;
            ++temporalDirectionHistogram[directionIndex];
            ++temporalOffsetHistogram[offsetIndex];
        }
        Require(std::count(
                    visitedSliceAxes.begin(), visitedSliceAxes.end(), true) == 6,
            "the pi-domain conversion does not alias temporal rotations");
        Require(std::all_of(
                temporalDirectionHistogram.begin(),
                temporalDirectionHistogram.end(),
                [](uint32_t count) { return count == 4u; }),
            "the temporal cycle covers every direction rotation four times");
        Require(std::all_of(
                temporalOffsetHistogram.begin(),
                temporalOffsetHistogram.end(),
                [](uint32_t count) { return count == 6u; }),
            "the temporal cycle covers every radial offset six times");
        for (const auto& directionPairs : visitedPairs)
        {
            Require(std::all_of(
                    directionPairs.begin(), directionPairs.end(),
                    [](bool visited) { return visited; }),
                "the temporal cycle covers the complete 6-by-4 schedule");
        }

        Require(Near(ActivisionFadedCandidateHorizon(0.8, 0.0), -1.0) &&
                Near(ActivisionFadedCandidateHorizon(0.8, 0.5), -0.1) &&
                Near(ActivisionFadedCandidateHorizon(0.8, 1.0), 0.8),
            "distance falloff fades each candidate from the unoccluded horizon");
        const double firstOrder = std::max(
            std::max(-1.0,
                ActivisionFadedCandidateHorizon(0.2, 0.75)),
            ActivisionFadedCandidateHorizon(0.8, 0.25));
        const double reverseOrder = std::max(
            std::max(-1.0,
                ActivisionFadedCandidateHorizon(0.8, 0.25)),
            ActivisionFadedCandidateHorizon(0.2, 0.75));
        Require(Near(firstOrder, reverseOrder),
            "candidate-local falloff remains independent of trace order");

        Require(Near(ActivisionSpatialDepthWeight(0.0), 1.0) &&
                Near(ActivisionSpatialDepthWeight(0.05), 0.5) &&
                Near(ActivisionSpatialDepthWeight(0.10), 0.0) &&
                Near(ActivisionSpatialDepthWeight(0.20), 0.0),
            "the published ten-percent relative-depth threshold uses a linear ramp");

        Require(ActivisionTemporalDepthAccepted(10.0, 9.0) &&
                ActivisionTemporalDepthAccepted(10.0, 11.0) &&
                !ActivisionTemporalDepthAccepted(10.0, 8.8) &&
                !ActivisionTemporalDepthAccepted(10.0, 11.2) &&
                !ActivisionTemporalDepthAccepted(10.0, 65504.0),
            "temporal depth rejection is symmetric and rejects sentinels");

        const double nan = std::numeric_limits<double>::quiet_NaN();
        Require(SourceProfileSupportsViewport(
                    0.0, 0.0, 1920.0, 1080.0, 1920u, 1080u) &&
                !SourceProfileSupportsViewport(
                    1.0, 0.0, 1920.0, 1080.0, 1920u, 1080u) &&
                !SourceProfileSupportsViewport(
                    0.0, 1.0, 1920.0, 1080.0, 1920u, 1080u) &&
                !SourceProfileSupportsViewport(
                    0.0, 0.0, 1919.0, 1080.0, 1920u, 1080u) &&
                !SourceProfileSupportsViewport(
                    0.0, 0.0, 1920.0, 1079.0, 1920u, 1080u) &&
                !SourceProfileSupportsViewport(
                    0.0, 0.0, 1921.0, 1080.0, 1920u, 1080u) &&
                !SourceProfileSupportsViewport(
                    nan, 0.0, 1920.0, 1080.0, 1920u, 1080u),
            "PS4 GTAO and XeGTAO accept only a finite full-texture viewport at the origin");

        const ActivisionTemporalReprojection topLeft =
            EvaluateActivisionTemporalReprojection(
                0u, 0u, 0.0, 0.0, 2u, 1920u, 1080u);
        const ActivisionTemporalReprojection exactClampEdge =
            EvaluateActivisionTemporalReprojection(
                0u, 0u, -0.5, -0.5, 2u, 1920u, 1080u);
        const ActivisionTemporalReprojection outsideLeft =
            EvaluateActivisionTemporalReprojection(
                0u, 0u, -0.5001, 0.0, 2u, 1920u, 1080u);
        const ActivisionTemporalReprojection bottomRight =
            EvaluateActivisionTemporalReprojection(
                1919u, 1079u, 0.0, 0.0, 2u, 1920u, 1080u);
        const ActivisionTemporalReprojection outsideRight =
            EvaluateActivisionTemporalReprojection(
                1919u, 1079u, 0.5, 0.0, 2u, 1920u, 1080u);
        const ActivisionTemporalReprojection oddBottomRight =
            EvaluateActivisionTemporalReprojection(
                4u, 2u, 0.0, 0.0, 2u, 5u, 3u);
        const ActivisionTemporalReprojection oddOutsideRight =
            EvaluateActivisionTemporalReprojection(
                4u, 2u, 0.5, 0.0, 2u, 5u, 3u);
        const ActivisionTemporalReprojection nonFiniteMotion =
            EvaluateActivisionTemporalReprojection(
                0u, 0u, nan, 0.0, 2u, 1920u, 1080u);
        Require(topLeft.valid &&
                Near(topLeft.previousSamplingTexel[0], -0.25) &&
                Near(topLeft.previousSamplingTexel[1], -0.25) &&
                exactClampEdge.valid &&
                Near(exactClampEdge.previousSamplingTexel[0], -0.5) &&
                Near(exactClampEdge.previousSamplingTexel[1], -0.5) &&
                !outsideLeft.valid &&
                bottomRight.valid &&
                Near(bottomRight.previousSamplingTexel[0], 959.25) &&
                Near(bottomRight.previousSamplingTexel[1], 539.25) &&
                !outsideRight.valid &&
                oddBottomRight.valid &&
                Near(oddBottomRight.previousSamplingTexel[0], 1.75) &&
                Near(oddBottomRight.previousSamplingTexel[1], 0.75) &&
                !oddOutsideRight.valid &&
                !nonFiniteMotion.valid,
            "temporal reprojection retains the clamp-safe border footprint while rejecting true viewport exits and non-finite motion");

        Require(Near(ActivisionDepthSlope(10.0, 8.0, 12.0), 2.0) &&
                Near(ActivisionDepthSlope(10.0, 65504.0, 12.0), 2.0) &&
                Near(ActivisionDepthSlope(10.0, 8.0, 65504.0), 2.0) &&
                Near(ActivisionDepthSlope(
                    10.0, 65504.0, 65504.0), 0.0),
            "spatial slope estimation excludes invalid prepared depths");

        constexpr float packedDepth = 123.456f;
        for (uint32_t offset = 0u; offset < 4u; ++offset)
        {
            const uint32_t packed =
                PackActivisionDepthGuide(packedDepth, offset);
            const float decoded = UnpackActivisionDepth(packed);
            Require((packed & 3u) == offset,
                "the packed depth preserves every 2x2 source offset");
            Require(FloatBits(decoded) == (FloatBits(packedDepth) & ~3u) &&
                    std::isfinite(decoded) && decoded > 0.f &&
                    std::abs(decoded - packedDepth) /
                        packedDepth <=
                        4.f * std::numeric_limits<float>::epsilon(),
                "masking the guide bits restores finite depth within three ULPs");
        }
        Require(UnpackActivisionDepth(
                PackActivisionDepthGuide(65504.f, 3u)) >= 65503.f,
            "the invalid-depth sentinel remains invalid after packing");

        const std::array<ActivisionDepthCandidate, 4> forwardCandidates = {{
            { 1.0f, 0.5f }, { 0.8f, 20.f },
            { 0.2f, 4.f }, { 0.4f, 8.f }
        }};
        const std::array<ActivisionDepthCandidate, 4> reverseCandidates = {{
            { 0.0f, 0.5f }, { 0.2f, 20.f },
            { 0.8f, 4.f }, { 0.6f, 8.f }
        }};
        const uint32_t forwardGuide = SelectActivisionDepthGuide(
            forwardCandidates, false);
        const uint32_t reverseGuide = SelectActivisionDepthGuide(
            reverseCandidates, true);
        Require((forwardGuide & 3u) == 2u &&
                (reverseGuide & 3u) == 2u &&
                Near(UnpackActivisionDepth(forwardGuide), 4.0) &&
                Near(UnpackActivisionDepth(reverseGuide), 4.0),
            "forward and reverse depth select the same closest receiver surface");

        const uint32_t bottomRightGuide =
            PackActivisionDepthGuide(4.f, 3u);
        const std::array<uint32_t, 2> interiorPixel =
            ActivisionGuidePixel(3u, 4u, bottomRightGuide, 10u, 12u);
        const std::array<uint32_t, 2> oddEdgePixel =
            ActivisionGuidePixel(2u, 1u, bottomRightGuide, 5u, 3u);
        Require(interiorPixel == std::array<uint32_t, 2>{ 7u, 9u } &&
                oddEdgePixel == std::array<uint32_t, 2>{ 4u, 2u },
            "packed guide reconstruction clamps odd-resolution edge blocks");
    }

    uint32_t XeGtaoHilbertIndex(uint32_t x, uint32_t y)
    {
        constexpr uint32_t width = 64u;
        uint32_t index = 0u;
        for (uint32_t level = width / 2u;
            level > 0u;
            level /= 2u)
        {
            const uint32_t regionX = (x & level) != 0u;
            const uint32_t regionY = (y & level) != 0u;
            index += level * level * ((3u * regionX) ^ regionY);
            if (regionY == 0u)
            {
                if (regionX != 0u)
                {
                    x = (width - 1u) - x;
                    y = (width - 1u) - y;
                }
                std::swap(x, y);
            }
        }
        return index;
    }

    void ValidateXeGtaoHilbertReference()
    {
        constexpr uint32_t width = 64u;
        constexpr uint32_t texelCount = width * width;
        std::array<bool, texelCount> visited{};
        std::array<std::array<uint32_t, 2>, texelCount> coordinateByIndex{};
        uint64_t indexSum = 0u;
        for (uint32_t y = 0u; y < width; ++y)
        for (uint32_t x = 0u; x < width; ++x)
        {
            const uint32_t index = XeGtaoHilbertIndex(x, y);
            Require(index < texelCount,
                "every XeGTAO Hilbert index remains in the 12-bit LUT range");
            Require(!visited[index],
                "the 64x64 XeGTAO Hilbert LUT is a permutation");
            visited[index] = true;
            coordinateByIndex[index] = { x, y };
            indexSum += index;
        }
        Require(std::all_of(
                visited.begin(), visited.end(),
                [](bool value) { return value; }),
            "the XeGTAO Hilbert LUT covers every index from zero through 4095");
        Require(indexSum == uint64_t(texelCount - 1u) * texelCount / 2u,
            "the XeGTAO Hilbert permutation retains the exact index sum");

        for (uint32_t index = 1u; index < texelCount; ++index)
        {
            const auto previous = coordinateByIndex[index - 1u];
            const auto current = coordinateByIndex[index];
            const uint32_t distanceX = std::max(previous[0], current[0]) -
                std::min(previous[0], current[0]);
            const uint32_t distanceY = std::max(previous[1], current[1]) -
                std::min(previous[1], current[1]);
            Require(distanceX + distanceY == 1u,
                "successive XeGTAO Hilbert indices remain edge-adjacent");
        }

        struct HilbertAnchor
        {
            uint32_t x;
            uint32_t y;
            uint32_t index;
        };
        constexpr std::array<HilbertAnchor, 11> anchors = {{
            { 0u, 0u, 0u },
            { 0u, 1u, 3u },
            { 1u, 0u, 1u },
            { 1u, 1u, 2u },
            { 63u, 0u, 4095u },
            { 0u, 63u, 1365u },
            { 63u, 63u, 2730u },
            { 31u, 31u, 682u },
            { 32u, 32u, 2048u },
            { 17u, 42u, 1895u },
            { 42u, 17u, 3527u }
        }};
        for (const HilbertAnchor& anchor : anchors)
        {
            Require(XeGtaoHilbertIndex(anchor.x, anchor.y) == anchor.index,
                "XeGTAO Hilbert LUT anchors match the pinned 64x64 mapping");
        }
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
    ValidateExactTraceControls();
    ValidateHorizonIntegralReference();
    ValidateActivisionScheduleReference();
    ValidateXeGtaoHilbertReference();

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

    Require(uvsr::PackVisibilityFilterAdaptedNoiseRgba8({}).empty(),
        "packed FAST rejects a malformed scalar volume");
    const std::vector<uint8_t> packedFast =
        uvsr::PackVisibilityFilterAdaptedNoiseRgba8(filterAdapted);
    Require(packedFast.size() == filterAdapted.size() * 4u,
        "packed FAST allocates one RGBA8 texel per scalar texel");
    constexpr std::array<uint32_t, 4> packedSemanticDimensions = {
        0u, 1u, 4u, 5u
    };
    for (uint32_t layer = 0u;
        layer < uvsr::VisibilityFilterAdaptedNoiseLayerCount;
        ++layer)
    {
        const size_t layerOffset = size_t(layer) *
            uvsr::VisibilityFilterAdaptedNoiseTexelCount;
        for (uint32_t y = 0u;
            y < uvsr::VisibilityFilterAdaptedNoiseSize;
            ++y)
        {
            for (uint32_t x = 0u;
                x < uvsr::VisibilityFilterAdaptedNoiseSize;
                ++x)
            {
                const size_t packedOffset = (layerOffset +
                    size_t(y) * uvsr::VisibilityFilterAdaptedNoiseSize + x) *
                    4u;
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    const auto& semanticOffset = semanticOffsets[
                        packedSemanticDimensions[channel]];
                    const uint32_t sourceX =
                        (x + semanticOffset[0]) & 63u;
                    const uint32_t sourceY =
                        (y + semanticOffset[1]) & 63u;
                    const uint8_t expected = filterAdapted[
                        layerOffset + size_t(sourceY) *
                            uvsr::VisibilityFilterAdaptedNoiseSize + sourceX];
                    Require(packedFast[packedOffset + channel] == expected,
                        "each packed FAST channel exactly reproduces its scalar semantic lookup");
                }
            }
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
