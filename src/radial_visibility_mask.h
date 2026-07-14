#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

constexpr uint32_t RadialVisibilitySectorCount = 32u;
constexpr uint32_t RadialVisibilityFullMask =
    std::numeric_limits<uint32_t>::max();

struct RadialVisibilityMask
{
    uint32_t occludedBits = 0u;
};

// Keep this value type field-for-field compatible with the HLSL contract.
struct VisibilityInterval
{
    float minimumAngle01 = 0.f;
    float maximumAngle01 = 0.f;
};

constexpr VisibilityInterval MakeVisibilityInterval(
    float minimumAngle01,
    float maximumAngle01) noexcept
{
    return { minimumAngle01, maximumAngle01 };
}

namespace radial_visibility_detail
{
    constexpr uint32_t MakeLowBitMask(uint32_t bitCount) noexcept
    {
        if (bitCount == 0u)
            return 0u;
        if (bitCount >= RadialVisibilitySectorCount)
            return RadialVisibilityFullMask;
        return (uint32_t{ 1 } << bitCount) - 1u;
    }
}

// Quantize an interval against one coherently shifted point lattice. Thin
// intervals survive with probability proportional to their angular measure,
// while one shared phase preserves front/back ordering and contiguous bits.
inline uint32_t MakeStochasticSectorRangeMask(
    float minimumAngle01,
    float maximumAngle01,
    float sectorPhase) noexcept
{
    if (!std::isfinite(minimumAngle01) ||
        !std::isfinite(maximumAngle01) ||
        !std::isfinite(sectorPhase))
    {
        return 0u;
    }

    double minimum = std::clamp(
        static_cast<double>(minimumAngle01), 0.0, 1.0);
    double maximum = std::clamp(
        static_cast<double>(maximumAngle01), 0.0, 1.0);
    if (maximum < minimum)
        std::swap(minimum, maximum);
    if (!(maximum > minimum))
        return 0u;

    const double phase = static_cast<double>(sectorPhase) -
        std::floor(static_cast<double>(sectorPhase));
    const uint32_t firstSector = static_cast<uint32_t>(std::clamp(
        std::floor(minimum * RadialVisibilitySectorCount + phase),
        0.0, static_cast<double>(RadialVisibilitySectorCount)));
    const uint32_t endSector = static_cast<uint32_t>(std::clamp(
        std::floor(maximum * RadialVisibilitySectorCount + phase),
        0.0, static_cast<double>(RadialVisibilitySectorCount)));
    if (endSector <= firstSector)
        return 0u;

    return radial_visibility_detail::MakeLowBitMask(
        endSector - firstSector) << firstSector;
}

inline uint32_t MakeStochasticSectorRangeMask(
    VisibilityInterval interval,
    float sectorPhase) noexcept
{
    return MakeStochasticSectorRangeMask(
        interval.minimumAngle01,
        interval.maximumAngle01,
        sectorPhase);
}

constexpr uint32_t GetNewlyCoveredBits(
    uint32_t candidateBits,
    uint32_t existingBits) noexcept
{
    return candidateBits & ~existingBits;
}

inline uint32_t AccumulateOccluder(
    RadialVisibilityMask& mask,
    uint32_t candidateBits) noexcept
{
    const uint32_t newlyCoveredBits =
        GetNewlyCoveredBits(candidateBits, mask.occludedBits);
    mask.occludedBits |= candidateBits;
    return newlyCoveredBits;
}

constexpr uint32_t CountOccludedSectors(
    RadialVisibilityMask mask) noexcept
{
    uint32_t bits = mask.occludedBits;
    uint32_t count = 0u;
    while (bits != 0u)
    {
        bits &= bits - 1u;
        ++count;
    }
    return count;
}

constexpr float GetSliceVisibility(RadialVisibilityMask mask) noexcept
{
    return 1.f - static_cast<float>(CountOccludedSectors(mask)) /
        static_cast<float>(RadialVisibilitySectorCount);
}

static_assert(
    RadialVisibilitySectorCount == std::numeric_limits<uint32_t>::digits,
    "A radial visibility mask must occupy exactly one uint32_t.");
