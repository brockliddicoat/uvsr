#include "radial_visibility_mask.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Radial visibility validation failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    void RequireEqual(
        uint32_t actual,
        uint32_t expected,
        const std::string& message)
    {
        if (actual == expected)
            return;

        std::ostringstream details;
        details << message << " (expected 0x" << std::hex << expected
            << ", got 0x" << actual << ')';
        Fail(details.str());
    }

    bool Near(float actual, float expected, float tolerance = 1e-6f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    uint32_t ExpectedRangeMask(uint32_t firstSector, uint32_t sectorCount)
    {
        if (sectorCount == 0u)
            return 0u;
        if (sectorCount >= RadialVisibilitySectorCount)
            return RadialVisibilityFullMask;
        return ((uint32_t{ 1 } << sectorCount) - 1u) << firstSector;
    }

    uint32_t ReverseFiveBits(uint32_t value)
    {
        uint32_t reversed = 0u;
        for (uint32_t bit = 0u; bit < 5u; ++bit)
            reversed = (reversed << 1u) | ((value >> bit) & 1u);
        return reversed;
    }

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

    uint32_t RotateLeft(uint32_t value, uint32_t shift)
    {
        shift &= 31u;
        return shift == 0u
            ? value
            : (value << shift) | (value >> (32u - shift));
    }

    void TestProgressiveRadialPrefixes()
    {
        // Mirrored by ProgressiveRadialPrefixMasks in the visibility shader.
        // The exact constants catch accidental changes to the nested set while
        // the extraction check documents the required near-to-far traversal.
        constexpr uint32_t expectedMasks[33] = {
            0x00000000u, 0x00000001u, 0x00010001u, 0x00010101u,
            0x01010101u, 0x01010111u, 0x01110111u, 0x01111111u,
            0x11111111u, 0x11111115u, 0x11151115u, 0x11151515u,
            0x15151515u, 0x15151555u, 0x15551555u, 0x15555555u,
            0x55555555u, 0x55555557u, 0x55575557u, 0x55575757u,
            0x57575757u, 0x57575777u, 0x57775777u, 0x57777777u,
            0x77777777u, 0x7777777fu, 0x777f777fu, 0x777f7f7fu,
            0x7f7f7f7fu, 0x7f7f7fffu, 0x7fff7fffu, 0x7fffffffu,
            0xffffffffu
        };

        uint32_t prefixMask = 0u;
        uint32_t previousMask = 0u;
        for (uint32_t budget = 0u; budget <= 32u; ++budget)
        {
            if (budget != 0u)
            {
                prefixMask |= uint32_t{ 1 } <<
                    ReverseFiveBits(budget - 1u);
            }

            RequireEqual(prefixMask, expectedMasks[budget],
                "Progressive radial prefix constant");
            RequireEqual(CountBits(prefixMask), budget,
                "Progressive radial prefix popcount equals its budget");
            Require((prefixMask & previousMask) == previousMask,
                "Increasing a radial budget preserves every prior stratum");

            uint32_t remaining = prefixMask;
            uint32_t previousStratum = 0u;
            bool hasPreviousStratum = false;
            for (uint32_t stratum = 0u; stratum < 32u; ++stratum)
            {
                const uint32_t bit = uint32_t{ 1 } << stratum;
                if ((remaining & bit) == 0u)
                    continue;
                if (hasPreviousStratum)
                {
                    Require(stratum > previousStratum,
                        "Selected radial strata traverse near to far");
                }
                previousStratum = stratum;
                hasPreviousStratum = true;
                remaining &= ~bit;
            }
            RequireEqual(remaining, 0u,
                "Near-to-far traversal consumes the selected prefix");
            previousMask = prefixMask;
        }

        for (uint32_t shift = 0u; shift < 32u; ++shift)
        {
            uint32_t previousRotatedMask = 0u;
            for (uint32_t budget = 0u; budget <= 32u; ++budget)
            {
                const uint32_t rotatedMask = RotateLeft(
                    expectedMasks[budget], shift);
                RequireEqual(CountBits(rotatedMask), budget,
                    "Rotated radial prefix retains its sample count");
                Require((previousRotatedMask & ~rotatedMask) == 0u,
                    "Rotated radial budgets remain nested");
                previousRotatedMask = rotatedMask;
            }
            RequireEqual(RotateLeft(expectedMasks[1], shift),
                uint32_t{ 1 } << shift,
                "First radial sample visits every stratum across rotations");
        }
    }

    void TestLowBitMaskBounds()
    {
        // MakeStochasticSectorRangeMask only ever calls the helper with a
        // strictly positive, in-range span, so exercise the degenerate and
        // saturating counts directly to pin the contiguous-run construction.
        using radial_visibility_detail::MakeLowBitMask;
        RequireEqual(MakeLowBitMask(0u), 0u,
            "A zero sector count masks no bits");
        RequireEqual(MakeLowBitMask(1u), 0x00000001u,
            "A single sector count masks the lowest bit");
        RequireEqual(MakeLowBitMask(5u), 0x0000001fu,
            "A partial sector count masks a contiguous low run");
        RequireEqual(
            MakeLowBitMask(RadialVisibilitySectorCount),
            RadialVisibilityFullMask,
            "A full sector count saturates to the complete mask");
        RequireEqual(
            MakeLowBitMask(RadialVisibilitySectorCount + 4u),
            RadialVisibilityFullMask,
            "An over-full sector count clamps without shifting past the word");
    }

    void TestDegenerateIntervalCollapses()
    {
        // A collapsed interval carries no angular measure and must survive no
        // point lattice, independent of the sector phase or overload used.
        RequireEqual(
            MakeStochasticSectorRangeMask(0.5f, 0.5f, 0.3f),
            0u,
            "A zero-width interval quantizes to no sectors");
        RequireEqual(
            MakeStochasticSectorRangeMask(0.5f, 0.5f, 0.87f),
            0u,
            "A zero-width interval stays empty under a shifted lattice");
        RequireEqual(
            MakeStochasticSectorRangeMask(
                MakeVisibilityInterval(0.42f, 0.42f), 0.6f),
            0u,
            "The interval overload also collapses a zero-width span");
    }

    void TestStochasticPointQuantization()
    {
        constexpr float sectorWidth =
            1.f / float(RadialVisibilitySectorCount);
        const float minimum = 5.10f * sectorWidth;
        const float maximum = 5.35f * sectorWidth;

        RequireEqual(
            MakeStochasticSectorRangeMask(minimum, maximum, 0.0f),
            0u,
            "A sub-sector interval may miss the unshifted point lattice");
        RequireEqual(
            MakeStochasticSectorRangeMask(minimum, maximum, 0.75f),
            0x00000020u,
            "A shifted point lattice retains the thin interval");
        RequireEqual(
            MakeStochasticSectorRangeMask(0.f, 1.f, 0.37f),
            RadialVisibilityFullMask,
            "A full interval remains a complete mask");
        RequireEqual(
            MakeStochasticSectorRangeMask(minimum, maximum, 1.75f),
            MakeStochasticSectorRangeMask(minimum, maximum, 0.75f),
            "Sector phase wraps at one");
        RequireEqual(
            MakeStochasticSectorRangeMask(0.8f, 0.2f, 0.4f),
            MakeStochasticSectorRangeMask(0.2f, 0.8f, 0.4f),
            "Reversed endpoints preserve the same interval");
        RequireEqual(
            MakeStochasticSectorRangeMask(-4.f, 5.f, 0.1f),
            RadialVisibilityFullMask,
            "Finite endpoints clamp to the unit interval");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        RequireEqual(
            MakeStochasticSectorRangeMask(nan, 0.5f, 0.f),
            0u,
            "NaN minimum fails open");
        RequireEqual(
            MakeStochasticSectorRangeMask(0.5f, nan, 0.f),
            0u,
            "NaN maximum fails open");
        RequireEqual(
            MakeStochasticSectorRangeMask(0.f, 1.f, infinity),
            0u,
            "Non-finite phase fails open");

        constexpr uint32_t phaseCount = 4096u;
        uint32_t hitCount = 0u;
        for (uint32_t phaseIndex = 0u;
            phaseIndex < phaseCount;
            ++phaseIndex)
        {
            const float phase = (float(phaseIndex) + 0.5f) /
                float(phaseCount);
            if (MakeStochasticSectorRangeMask(
                    minimum, maximum, phase) != 0u)
            {
                ++hitCount;
            }
        }
        const float observedProbability =
            float(hitCount) / float(phaseCount);
        const float expectedProbability =
            (maximum - minimum) * float(RadialVisibilitySectorCount);
        Require(Near(observedProbability, expectedProbability,
            1.f / float(phaseCount)),
            "Thin-interval survival probability equals angular sector mass");
    }

    void TestAccumulation()
    {
        RadialVisibilityMask mask;
        const uint32_t firstCandidate = 0x0000000fu;
        const uint32_t overlappingCandidate = 0x0000003cu;
        const uint32_t disjointCandidate = 0xf0000000u;

        RequireEqual(
            AccumulateOccluder(mask, firstCandidate),
            firstCandidate,
            "First accumulation reports every candidate bit");
        RequireEqual(
            AccumulateOccluder(mask, overlappingCandidate),
            0x00000030u,
            "Overlap reports only newly covered bits");
        RequireEqual(mask.occludedBits, 0x0000003fu,
            "Overlapping masks are unioned");
        RequireEqual(
            AccumulateOccluder(mask, overlappingCandidate),
            0u,
            "Repeated accumulation is idempotent");
        RequireEqual(
            AccumulateOccluder(mask, disjointCandidate),
            disjointCandidate,
            "Disjoint accumulation reports every new bit");
        RequireEqual(mask.occludedBits, 0xf000003fu,
            "Disjoint masks are unioned");

        RadialVisibilityMask monotonicMask;
        for (uint32_t sector = 0u;
            sector < RadialVisibilitySectorCount;
            ++sector)
        {
            const uint32_t previousBits = monotonicMask.occludedBits;
            const uint32_t candidate = uint32_t{ 1 } << sector;
            RequireEqual(
                AccumulateOccluder(monotonicMask, candidate),
                candidate,
                "A new single-sector candidate reports one bit");
            Require((monotonicMask.occludedBits & previousBits) ==
                previousBits,
                "Accumulation never clears an existing bit");
            RequireEqual(
                CountOccludedSectors(monotonicMask),
                sector + 1u,
                "Single-sector accumulation has monotonic popcount");
        }
        RequireEqual(
            monotonicMask.occludedBits,
            RadialVisibilityFullMask,
            "Single-sector accumulation reaches the complete mask");
    }

    void TestCountsAndVisibility()
    {
        for (uint32_t count = 0u;
            count <= RadialVisibilitySectorCount;
            ++count)
        {
            const RadialVisibilityMask mask{
                ExpectedRangeMask(0u, count)
            };
            RequireEqual(
                CountOccludedSectors(mask),
                count,
                "Prefix mask popcount");
            const float expectedVisibility = 1.f -
                float(count) / float(RadialVisibilitySectorCount);
            Require(Near(GetSliceVisibility(mask), expectedVisibility),
                "Prefix mask visibility");
        }

        const RadialVisibilityMask alternating{ 0xaaaaaaaau };
        RequireEqual(
            CountOccludedSectors(alternating),
            16u,
            "Alternating mask count is 16");
        Require(Near(GetSliceVisibility(alternating), 0.5f),
            "Alternating mask visibility is one half");
    }
}

int main()
{
    TestProgressiveRadialPrefixes();
    TestLowBitMaskBounds();
    TestDegenerateIntervalCollapses();
    TestStochasticPointQuantization();
    TestAccumulation();
    TestCountsAndVisibility();

    std::cout << "UVSR radial visibility mask validation passed\n";
    return EXIT_SUCCESS;
}
