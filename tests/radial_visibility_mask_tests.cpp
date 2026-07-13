#include "radial_visibility_mask.h"

#include <array>
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
        std::cerr << "Radial visibility validation failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    void RequireEqual(uint32_t actual, uint32_t expected, const std::string& message)
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

    const char* CriterionName(SectorHitCriterion criterion)
    {
        switch (criterion)
        {
        case SectorHitCriterion::Round:
            return "Round";
        case SectorHitCriterion::Ceil:
            return "Ceil";
        case SectorHitCriterion::Floor:
            return "Floor";
        }

        return "Unknown";
    }

    void TestSectorBoundaryIntervals()
    {
        constexpr std::array<SectorHitCriterion, 3> Criteria = {
            SectorHitCriterion::Round,
            SectorHitCriterion::Ceil,
            SectorHitCriterion::Floor
        };

        for (uint32_t first = 0u; first <= RadialVisibilitySectorCount; ++first)
        {
            for (uint32_t end = first; end <= RadialVisibilitySectorCount; ++end)
            {
                const float minimum = static_cast<float>(first) /
                    static_cast<float>(RadialVisibilitySectorCount);
                const float maximum = static_cast<float>(end) /
                    static_cast<float>(RadialVisibilitySectorCount);
                const uint32_t expected = ExpectedRangeMask(first, end - first);

                for (SectorHitCriterion criterion : Criteria)
                {
                    std::ostringstream context;
                    context << CriterionName(criterion) << " boundary interval ["
                        << first << ", " << end << ")";
                    RequireEqual(
                        MakeSectorRangeMask(minimum, maximum, criterion),
                        expected,
                        context.str());
                }
            }
        }
    }

    void TestHitCriteria()
    {
        constexpr float SectorWidth = 1.f / static_cast<float>(RadialVisibilitySectorCount);
        constexpr float HalfSectorWidth = SectorWidth * 0.5f;
        const float belowHalfSector = std::nextafter(HalfSectorWidth, 0.f);
        const float aboveHalfSector = std::nextafter(HalfSectorWidth, 1.f);
        const float belowFullSector = std::nextafter(SectorWidth, 0.f);
        const float aboveFullSector = std::nextafter(SectorWidth, 1.f);

        RequireEqual(
            MakeSectorRangeMask(0.f, HalfSectorWidth, SectorHitCriterion::Round),
            0x00000001u,
            "Round activates an exactly half-covered sector");
        RequireEqual(
            MakeSectorRangeMask(0.f, belowHalfSector, SectorHitCriterion::Round),
            0u,
            "Round rejects less than half a sector");
        RequireEqual(
            MakeSectorRangeMask(0.f, aboveHalfSector, SectorHitCriterion::Round),
            0x00000001u,
            "Round accepts more than half a sector");
        RequireEqual(
            MakeSectorRangeMask(0.f, 3.f * HalfSectorWidth, SectorHitCriterion::Round),
            0x00000003u,
            "Round rounds one-and-a-half sectors up");

        RequireEqual(
            MakeSectorRangeMask(0.f, std::numeric_limits<float>::denorm_min(),
                SectorHitCriterion::Ceil),
            0x00000001u,
            "Ceil activates a positive subnormal interval");
        RequireEqual(
            MakeSectorRangeMask(0.f, belowFullSector, SectorHitCriterion::Ceil),
            0x00000001u,
            "Ceil activates a partially covered sector");
        RequireEqual(
            MakeSectorRangeMask(0.f, aboveFullSector, SectorHitCriterion::Ceil),
            0x00000003u,
            "Ceil grows when the span crosses a sector boundary");

        RequireEqual(
            MakeSectorRangeMask(0.f, belowFullSector, SectorHitCriterion::Floor),
            0u,
            "Floor rejects less than one full sector");
        RequireEqual(
            MakeSectorRangeMask(0.f, SectorWidth, SectorHitCriterion::Floor),
            0x00000001u,
            "Floor accepts exactly one full sector");
        RequireEqual(
            MakeSectorRangeMask(0.f, aboveFullSector, SectorHitCriterion::Floor),
            0x00000001u,
            "Floor ignores a partial trailing sector");

        const float offsetMinimum = 5.75f * SectorWidth;
        const float offsetMaximum = 7.25f * SectorWidth;
        RequireEqual(
            MakeSectorRangeMask(offsetMinimum, offsetMaximum, SectorHitCriterion::Round),
            0x00000040u,
            "Round selects sectors covered by at least half");
        RequireEqual(
            MakeSectorRangeMask(offsetMinimum, offsetMaximum, SectorHitCriterion::Ceil),
            0x000000e0u,
            "Ceil selects every partially covered sector");
        RequireEqual(
            MakeSectorRangeMask(offsetMinimum, offsetMaximum, SectorHitCriterion::Floor),
            0x00000040u,
            "Floor selects only the completely covered sector");

        RequireEqual(
            MakeSectorRangeMask(
                5.99f * SectorWidth, 6.01f * SectorWidth, SectorHitCriterion::Ceil),
            0x00000060u,
            "Ceil claims both sectors touched across a boundary");
        RequireEqual(
            MakeSectorRangeMask(
                5.99f * SectorWidth, 6.01f * SectorWidth, SectorHitCriterion::Floor),
            0u,
            "Floor rejects boundary fragments without a complete sector");

        RequireEqual(
            MakeSectorRangeMask(0.f, HalfSectorWidth,
                static_cast<SectorHitCriterion>(255u)),
            0x00000001u,
            "An invalid criterion falls back to Round");
    }

    void TestRangeNormalization()
    {
        constexpr float SectorWidth = 1.f / static_cast<float>(RadialVisibilitySectorCount);
        constexpr float TinyInterval = SectorWidth / 1024.f;
        constexpr std::array<SectorHitCriterion, 3> Criteria = {
            SectorHitCriterion::Round,
            SectorHitCriterion::Ceil,
            SectorHitCriterion::Floor
        };

        for (SectorHitCriterion criterion : Criteria)
        {
            RequireEqual(
                MakeSectorRangeMask(0.f, 0.f, criterion),
                0u,
                std::string(CriterionName(criterion)) + " empty interval");
            RequireEqual(
                MakeSectorRangeMask(1.f, 1.f, criterion),
                0u,
                std::string(CriterionName(criterion)) + " empty interval at one");
            RequireEqual(
                MakeSectorRangeMask(0.f, 1.f, criterion),
                RadialVisibilityFullMask,
                std::string(CriterionName(criterion)) + " full interval");
            RequireEqual(
                MakeSectorRangeMask(-10.f, 10.f, criterion),
                RadialVisibilityFullMask,
                std::string(CriterionName(criterion)) + " clamped full interval");
            RequireEqual(
                MakeSectorRangeMask(0.8125f, 0.1875f, criterion),
                MakeSectorRangeMask(0.1875f, 0.8125f, criterion),
                std::string(CriterionName(criterion)) + " reversed interval");
            RequireEqual(
                MakeSectorRangeMask(2.f, 3.f, criterion),
                0u,
                std::string(CriterionName(criterion)) + " interval above one");
            RequireEqual(
                MakeSectorRangeMask(-3.f, -2.f, criterion),
                0u,
                std::string(CriterionName(criterion)) + " interval below zero");
        }

        RequireEqual(
            MakeSectorRangeMask(-1.f, SectorWidth, SectorHitCriterion::Round),
            0x00000001u,
            "Range touching zero selects the first sector");
        RequireEqual(
            MakeSectorRangeMask(1.f - SectorWidth, 2.f, SectorHitCriterion::Round),
            0x80000000u,
            "Range touching one selects the last sector");
        RequireEqual(
            MakeSectorRangeMask(0.f, TinyInterval, SectorHitCriterion::Round),
            0u,
            "Round rejects a tiny interval at zero");
        RequireEqual(
            MakeSectorRangeMask(0.f, TinyInterval, SectorHitCriterion::Floor),
            0u,
            "Floor rejects a tiny interval at zero");
        RequireEqual(
            MakeSectorRangeMask(0.f, TinyInterval, SectorHitCriterion::Ceil),
            0x00000001u,
            "Ceil accepts a tiny interval at zero");
        RequireEqual(
            MakeSectorRangeMask(1.f - TinyInterval, 1.f, SectorHitCriterion::Ceil),
            0x80000000u,
            "Ceil accepts a tiny interval at one");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        for (SectorHitCriterion criterion : Criteria)
        {
            RequireEqual(
                MakeSectorRangeMask(nan, 0.5f, criterion),
                0u,
                std::string(CriterionName(criterion)) + " NaN minimum");
            RequireEqual(
                MakeSectorRangeMask(0.5f, nan, criterion),
                0u,
                std::string(CriterionName(criterion)) + " NaN maximum");
            RequireEqual(
                MakeSectorRangeMask(-infinity, infinity, criterion),
                0u,
                std::string(CriterionName(criterion)) + " infinite endpoints");
        }
    }

    void TestAccumulation()
    {
        RadialVisibilityMask mask;
        const uint32_t firstCandidate = 0x0000000fu;
        const uint32_t overlappingCandidate = 0x0000003cu;
        const uint32_t disjointCandidate = 0xf0000000u;

        RequireEqual(
            GetNewlyCoveredBits(firstCandidate, mask.occludedBits),
            firstCandidate,
            "All bits are new for an empty mask");
        RequireEqual(
            AccumulateOccluder(mask, firstCandidate),
            firstCandidate,
            "First accumulation returns all candidate bits");
        RequireEqual(mask.occludedBits, firstCandidate, "First accumulation updates the mask");

        RequireEqual(
            GetNewlyCoveredBits(overlappingCandidate, mask.occludedBits),
            0x00000030u,
            "Only uncovered overlapping bits are new");
        RequireEqual(
            AccumulateOccluder(mask, overlappingCandidate),
            0x00000030u,
            "Accumulation returns only newly covered overlap bits");
        RequireEqual(mask.occludedBits, 0x0000003fu, "Overlapping masks are unioned");

        RequireEqual(
            AccumulateOccluder(mask, overlappingCandidate),
            0u,
            "Repeated accumulation has no newly covered bits");
        RequireEqual(mask.occludedBits, 0x0000003fu, "Repeated accumulation is idempotent");

        RequireEqual(
            AccumulateOccluder(mask, disjointCandidate),
            disjointCandidate,
            "Disjoint accumulation returns all candidate bits");
        RequireEqual(mask.occludedBits, 0xf000003fu, "Disjoint masks are unioned");

        RadialVisibilityMask monotonicMask;
        uint32_t previousCount = 0u;
        for (uint32_t sector = 0u; sector < RadialVisibilitySectorCount; ++sector)
        {
            const uint32_t previousBits = monotonicMask.occludedBits;
            const uint32_t candidate = uint32_t{ 1 } << sector;
            RequireEqual(
                AccumulateOccluder(monotonicMask, candidate),
                candidate,
                "A new single-sector candidate reports one bit");
            Require(
                (monotonicMask.occludedBits & previousBits) == previousBits,
                "Accumulation never clears existing bits");
            const uint32_t currentCount = CountOccludedSectors(monotonicMask);
            Require(currentCount >= previousCount, "Accumulated popcount is monotonic");
            RequireEqual(currentCount, sector + 1u, "Single-sector accumulation count");
            previousCount = currentCount;
        }

        RequireEqual(
            monotonicMask.occludedBits,
            RadialVisibilityFullMask,
            "Single-sector accumulation reaches the complete mask");
        RequireEqual(
            AccumulateOccluder(monotonicMask, RadialVisibilityFullMask),
            0u,
            "A complete mask has no newly covered bits");
    }

    void TestCountsAndVisibility()
    {
        for (uint32_t count = 0u; count <= RadialVisibilitySectorCount; ++count)
        {
            const RadialVisibilityMask mask{ ExpectedRangeMask(0u, count) };
            RequireEqual(
                CountOccludedSectors(mask),
                count,
                "Prefix mask popcount");
            const float expectedVisibility = 1.f - static_cast<float>(count) /
                static_cast<float>(RadialVisibilitySectorCount);
            Require(
                Near(GetSliceVisibility(mask), expectedVisibility),
                "Prefix mask visibility");
        }

        RequireEqual(
            CountOccludedSectors(RadialVisibilityMask{}),
            0u,
            "Empty mask count is zero");
        Require(Near(GetSliceVisibility(RadialVisibilityMask{}), 1.f),
            "Empty mask visibility is one");

        const RadialVisibilityMask complete{ RadialVisibilityFullMask };
        RequireEqual(CountOccludedSectors(complete), 32u, "Complete mask count is 32");
        Require(Near(GetSliceVisibility(complete), 0.f),
            "Complete mask visibility is zero");

        const RadialVisibilityMask alternating{ 0xaaaaaaaau };
        RequireEqual(CountOccludedSectors(alternating), 16u, "Alternating mask count is 16");
        Require(Near(GetSliceVisibility(alternating), 0.5f),
            "Half mask visibility is one half");
    }
}

int main()
{
    TestSectorBoundaryIntervals();
    TestHitCriteria();
    TestRangeNormalization();
    TestAccumulation();
    TestCountsAndVisibility();

    std::cout << "UVSR radial visibility mask validation passed\n";
    return EXIT_SUCCESS;
}
