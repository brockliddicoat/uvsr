#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

constexpr uint32_t RadialVisibilitySectorCount = 32u;
constexpr uint32_t RadialVisibilityFullMask = std::numeric_limits<uint32_t>::max();

enum class SectorHitCriterion : uint8_t
{
    Round,
    Ceil,
    Floor
};

struct RadialVisibilityMask
{
    uint32_t occludedBits = 0u;
};

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

inline uint32_t MakeSectorRangeMask(
    float minimumAngle01,
    float maximumAngle01,
    SectorHitCriterion criterion = SectorHitCriterion::Round) noexcept
{
    if (!std::isfinite(minimumAngle01) || !std::isfinite(maximumAngle01))
        return 0u;

    double minimum = std::clamp(static_cast<double>(minimumAngle01), 0.0, 1.0);
    double maximum = std::clamp(static_cast<double>(maximumAngle01), 0.0, 1.0);
    if (maximum < minimum)
        std::swap(minimum, maximum);

    const double span = maximum - minimum;
    if (!(span > 0.0))
        return 0u;

    const double scaledMinimum = minimum * static_cast<double>(RadialVisibilitySectorCount);
    const double scaledMaximum = maximum * static_cast<double>(RadialVisibilitySectorCount);
    double firstSectorValue = 0.0;
    double endSectorValue = 0.0;

    switch (criterion)
    {
    case SectorHitCriterion::Ceil:
        // Claim every sector with positive angular overlap.
        firstSectorValue = std::floor(scaledMinimum);
        endSectorValue = std::ceil(scaledMaximum);
        break;
    case SectorHitCriterion::Floor:
        // Claim only sectors completely contained by the interval.
        firstSectorValue = std::ceil(scaledMinimum);
        endSectorValue = std::floor(scaledMaximum);
        break;
    case SectorHitCriterion::Round:
    default:
        // A sector is hit when at least half of its angular width is covered.
        if (span * static_cast<double>(RadialVisibilitySectorCount) < 0.5)
            return 0u;
        firstSectorValue = std::ceil(scaledMinimum - 0.5);
        endSectorValue = std::floor(scaledMaximum + 0.5);
        break;
    }

    const uint32_t firstSector = static_cast<uint32_t>(std::clamp(
        firstSectorValue, 0.0, static_cast<double>(RadialVisibilitySectorCount)));
    const uint32_t endSector = static_cast<uint32_t>(std::clamp(
        endSectorValue, 0.0, static_cast<double>(RadialVisibilitySectorCount)));
    if (endSector <= firstSector)
        return 0u;

    return radial_visibility_detail::MakeLowBitMask(endSector - firstSector) << firstSector;
}

constexpr uint32_t GetNewlyCoveredBits(uint32_t candidateBits, uint32_t existingBits) noexcept
{
    return candidateBits & ~existingBits;
}

inline uint32_t AccumulateOccluder(
    RadialVisibilityMask& mask,
    uint32_t candidateBits) noexcept
{
    const uint32_t newlyCoveredBits = GetNewlyCoveredBits(candidateBits, mask.occludedBits);
    mask.occludedBits |= candidateBits;
    return newlyCoveredBits;
}

constexpr uint32_t CountOccludedSectors(RadialVisibilityMask mask) noexcept
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
