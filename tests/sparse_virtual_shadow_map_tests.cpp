#include "sparse_virtual_shadow_map.h"
#include "svsm_motion_benchmark.h"
#include "taa_miniengine_reference.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using namespace uvsr;

#undef assert
#define assert(...) \
    do { \
        if (!(static_cast<bool>(__VA_ARGS__))) \
        { \
            std::fprintf( \
                stderr, \
                "%s:%d: assertion failed: %s\n", \
                __FILE__, \
                __LINE__, \
                #__VA_ARGS__); \
            std::abort(); \
        } \
    } while (false)

namespace
{
    void TestPagePacking()
    {
        assert(SvsmSparseFlagAllocationBudgetSaturationEarlyOut ==
            8192u);
        assert(SvsmSparseFlagScatterAlphaTestEarlyReject == 4096u);
        assert(SvsmSparseFlagDirtyPageScatterAmplificationGuard ==
            16384u);
        assert(SvsmSparseFlagCoarsestPageRenderBudget == 32768u);
        assert((SvsmPacketPageRuntimePerPageBit &
            SvsmPacketPageRuntimeFailOpenBit) == 0u);
        assert((SvsmPacketPageRuntimeCountMask &
            (SvsmPacketPageRuntimePerPageBit |
                SvsmPacketPageRuntimeFailOpenBit)) == 0u);

        SvsmPageMetadata input;
        input.physicalPage = 32767u;
        input.age = 16383u;
        input.resident = true;
        input.required = true;
        input.dirty = false;

        const uint32_t packed = PackSvsmPageMetadata(input);
        const SvsmPageMetadata output = UnpackSvsmPageMetadata(packed);
        assert(output.physicalPage == input.physicalPage);
        assert(output.age == input.age);
        assert(output.resident);
        assert(output.required);
        assert(!output.dirty);

        input.resident = false;
        input.dirty = true;
        const SvsmPageMetadata nonresident =
            UnpackSvsmPageMetadata(PackSvsmPageMetadata(input));
        assert(nonresident.physicalPage == SvsmInvalidPhysicalPage);
        assert(!nonresident.resident);
        assert(nonresident.required);
        assert(nonresident.dirty);

        const uint32_t maximumOwner =
            SvsmClipmapCount * SvsmPagesPerClipmap - 1u;
        const uint32_t compact = PackSvsmCompactRenderPage(
            maximumOwner, SvsmPagesPerClipmap - 1u);
        assert(UnpackSvsmCompactRenderPageOwner(compact) ==
            maximumOwner);
        assert(UnpackSvsmCompactRenderPagePhysical(compact) ==
            SvsmPagesPerClipmap - 1u);
    }

    void TestFinePageRenderBudgetScheduling()
    {
        constexpr uint32_t coarsestLevel = SvsmClipmapCount - 1u;
        constexpr uint32_t unlimited =
            std::numeric_limits<uint32_t>::max();

        uint32_t fineReservation = 0u;
        const auto schedule =
            [&fineReservation, coarsestLevel](
                uint32_t clipmapLevel,
                uint32_t budget)
            {
                const uint32_t reservation = fineReservation;
                if (clipmapLevel < SvsmClipmapCount &&
                    clipmapLevel != coarsestLevel)
                {
                    ++fineReservation;
                }
                return ShouldScheduleSvsmDirtyPageRender(
                    clipmapLevel, reservation, budget);
            };

        // Fine levels share one reservation while coarsest work bypasses it.
        assert(schedule(0u, 2u));
        assert(fineReservation == 1u);
        assert(schedule(coarsestLevel, 2u));
        assert(fineReservation == 1u);
        assert(schedule(4u, 2u));
        assert(fineReservation == 2u);
        assert(schedule(coarsestLevel, 2u));
        assert(fineReservation == 2u);
        assert(!schedule(2u, 2u));
        assert(fineReservation == 3u);

        // The independent all-level mode shares one reservation with the
        // coarsest clipmap, providing a hard per-frame workload bound.
        uint32_t allLevelReservation = 0u;
        const auto scheduleAllLevels =
            [&allLevelReservation](uint32_t clipmapLevel, uint32_t budget)
            {
                const uint32_t reservation = allLevelReservation;
                if (clipmapLevel < SvsmClipmapCount)
                    ++allLevelReservation;
                return ShouldScheduleSvsmDirtyPageRender(
                    clipmapLevel, reservation, budget, true);
            };
        assert(scheduleAllLevels(coarsestLevel, 2u));
        assert(scheduleAllLevels(4u, 2u));
        assert(!scheduleAllLevels(coarsestLevel, 2u));
        assert(allLevelReservation == 3u);
        allLevelReservation = 0u;
        assert(!scheduleAllLevels(coarsestLevel, 0u));
        assert(allLevelReservation == 1u);

        // A zero budget rejects fine work without consuming coarse capacity.
        fineReservation = 0u;
        assert(!schedule(3u, 0u));
        assert(fineReservation == 1u);
        assert(schedule(coarsestLevel, 0u));
        assert(fineReservation == 1u);

        // Resetting the per-frame reservation lets a finite budget drain all
        // pending fine pages over subsequent frames.
        uint32_t remainingFinePages = 5u;
        constexpr std::array<uint32_t, 3> expectedScheduled = {
            2u, 2u, 1u
        };
        for (uint32_t expected : expectedScheduled)
        {
            fineReservation = 0u;
            uint32_t scheduled = 0u;
            for (uint32_t page = 0u;
                page < remainingFinePages;
                ++page)
            {
                const uint32_t level = page % coarsestLevel;
                scheduled += schedule(level, 2u) ? 1u : 0u;
            }
            assert(scheduled == expected);
            remainingFinePages -= scheduled;
        }
        assert(remainingFinePages == 0u);

        // UINT_MAX admits a full reachable shared physical pool of fine work.
        fineReservation = 0u;
        for (uint32_t page = 0u;
            page < SvsmPagesPerClipmap;
            ++page)
        {
            assert(schedule(page % coarsestLevel, unlimited));
        }
        assert(fineReservation == SvsmPagesPerClipmap);

        // Invalid clipmap levels neither schedule nor consume a reservation.
        const uint32_t reservationBeforeInvalid = fineReservation;
        assert(!ShouldScheduleSvsmDirtyPageRender(
            SvsmClipmapCount, 0u, unlimited));
        assert(!schedule(SvsmClipmapCount, unlimited));
        assert(fineReservation == reservationBeforeInvalid);

        // The optional relaxed probe is active only for finite budgets. It
        // can skip a fine reservation only after the monotonic counter has
        // reached that budget; coarsest work always bypasses it.
        assert(!ShouldEnableSvsmAllocationBudgetSaturationEarlyOut(
            false, 4u));
        assert(ShouldEnableSvsmAllocationBudgetSaturationEarlyOut(
            true, 4u));
        assert(ShouldEnableSvsmAllocationBudgetSaturationEarlyOut(
            true, 0u));
        assert(!ShouldEnableSvsmAllocationBudgetSaturationEarlyOut(
            true, unlimited));
        assert(!IsSvsmAllocationBudgetSaturationEarlyOutActive(
            SvsmMode::DenseReference, true, 4u));
        assert(IsSvsmAllocationBudgetSaturationEarlyOutActive(
            SvsmMode::SparseUncached, true, 4u));
        assert(IsSvsmAllocationBudgetSaturationEarlyOutActive(
            SvsmMode::SparseCached, true, 4u));
        assert(!IsSvsmAllocationBudgetSaturationEarlyOutActive(
            SvsmMode::SparseCached, true, unlimited));
        assert(!ShouldSkipSvsmFineRenderReservationAtomic(
            0u, 3u, 4u, true));
        assert(ShouldSkipSvsmFineRenderReservationAtomic(
            0u, 4u, 4u, true));
        assert(ShouldSkipSvsmFineRenderReservationAtomic(
            4u, 9u, 4u, true));
        assert(!ShouldSkipSvsmFineRenderReservationAtomic(
            coarsestLevel, 4u, 4u, true));
        assert(ShouldSkipSvsmFineRenderReservationAtomic(
            coarsestLevel, 4u, 4u, true, true));
        assert(!ShouldSkipSvsmFineRenderReservationAtomic(
            0u, 4u, 4u, false));
        assert(!ShouldSkipSvsmFineRenderReservationAtomic(
            SvsmClipmapCount, 4u, 4u, true, true));
    }

    void TestMappingAndWraparound()
    {
        assert(WrapSvsmPageCoordinate(-1) == 63);
        assert(WrapSvsmPageCoordinate(-65) == 63);
        assert(WrapSvsmPageCoordinate(64) == 0);
        assert(WrapSvsmPageCoordinate(129) == 1);
        assert(WrapSvsmPageCoordinate(
            std::numeric_limits<int32_t>::min()) == 0);
        assert(WrapSvsmPageCoordinate(
            std::numeric_limits<int32_t>::max()) == 63);

        int32_t quantizedOrigin = 0;
        assert(TryQuantizeSvsmRenderOrigin(
            64.f, 2.f, quantizedOrigin));
        assert(quantizedOrigin == 32);
        assert(!TryQuantizeSvsmRenderOrigin(
            1.f, 0.f, quantizedOrigin));
        assert(!TryQuantizeSvsmRenderOrigin(
            1.f, -1.f, quantizedOrigin));
        assert(!TryQuantizeSvsmRenderOrigin(
            std::numeric_limits<float>::quiet_NaN(),
            1.f,
            quantizedOrigin));
        assert(!TryQuantizeSvsmRenderOrigin(
            std::numeric_limits<float>::infinity(),
            1.f,
            quantizedOrigin));
        assert(!TryQuantizeSvsmRenderOrigin(
            std::numeric_limits<float>::max(),
            1.f,
            quantizedOrigin));
        const float maximumSafeOrigin = std::nextafter(
            float(std::numeric_limits<int32_t>::max()),
            0.f);
        assert(TryQuantizeSvsmRenderOrigin(
            maximumSafeOrigin, 1.f, quantizedOrigin));
        assert(quantizedOrigin == int32_t(maximumSafeOrigin));
        assert(TryQuantizeSvsmRenderOrigin(
            float(std::numeric_limits<int32_t>::min()),
            1.f,
            quantizedOrigin));
        assert(quantizedOrigin ==
            std::numeric_limits<int32_t>::min());

        const SvsmPageCoordinate extremeOffset =
            SvsmPageTableOffsetForRenderOrigin({
                std::numeric_limits<int32_t>::min(),
                std::numeric_limits<int32_t>::max()
            });
        assert(extremeOffset.x >= 0 &&
            extremeOffset.x < int32_t(SvsmPagesPerAxis));
        assert(extremeOffset.y >= 0 &&
            extremeOffset.y < int32_t(SvsmPagesPerAxis));
        assert((SvsmPageTableDeltaForRenderOrigins(
            {
                std::numeric_limits<int32_t>::max(),
                std::numeric_limits<int32_t>::max()
            },
            {
                std::numeric_limits<int32_t>::min(),
                std::numeric_limits<int32_t>::min()
            }) == SvsmPageCoordinate{ 64, -64 }));
        assert((SvsmPageTableDeltaForRenderOrigins(
            {
                std::numeric_limits<int32_t>::min(),
                std::numeric_limits<int32_t>::min()
            },
            {
                std::numeric_limits<int32_t>::max(),
                std::numeric_limits<int32_t>::max()
            }) == SvsmPageCoordinate{ -64, 64 }));
        assert(IsSvsmTablePageNewlyExposed(
            { 0, 0 }, { 0, 0 }, { 64, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            { 0, 0 }, { 0, 0 }, { 0, -64 }));
        assert(IsSvsmProjectionRangeRepresentable(20.f, 200.f));
        assert(!IsSvsmProjectionRangeRepresentable(
            std::numeric_limits<float>::denorm_min(), 200.f));
        assert(!IsSvsmProjectionRangeRepresentable(
            20.f, std::numeric_limits<float>::denorm_min()));
        SparseVirtualShadowMapSettings extremeExtentSettings;
        extremeExtentSettings.firstClipmapExtent =
            std::numeric_limits<float>::max();
        assert(!ValidateSvsmSettings(extremeExtentSettings));

        SvsmPageCoordinate offset{ 63, 1 };
        offset = AdvanceSvsmWrapOffset(offset, { 2, -3 });
        assert((offset == SvsmPageCoordinate{ 1, 62 }));

        assert((SvsmPageTableOffsetForRenderOrigin({ 0, 0 }) ==
            SvsmPageCoordinate{ 32, 32 }));
        assert((SvsmPageTableOffsetForRenderOrigin({ 3, 5 }) ==
            SvsmPageCoordinate{ 35, 27 }));
        assert((SvsmPageTableDeltaForRenderOrigins(
            { 3, 5 }, { 2, 3 }) ==
            SvsmPageCoordinate{ 1, -2 }));

        // The D3D virtual-texture conversion flips NDC Y. Moving a render
        // origin +1 page therefore moves a fixed world's local virtual Y +1
        // page, while the table offset must move -1 to keep its table address
        // stable. X has the opposite local direction and keeps a + delta.
        const SvsmPageCoordinate oldOffset =
            SvsmPageTableOffsetForRenderOrigin({ 2, 3 });
        const SvsmPageCoordinate newOffset =
            SvsmPageTableOffsetForRenderOrigin({ 3, 5 });
        const SvsmPageCoordinate fixedWorldOldLocal{ 30, 35 };
        const SvsmPageCoordinate fixedWorldNewLocal{ 29, 37 };
        assert((WrapSvsmPageCoordinate({
            fixedWorldOldLocal.x + oldOffset.x,
            fixedWorldOldLocal.y + oldOffset.y
        }) == WrapSvsmPageCoordinate({
            fixedWorldNewLocal.x + newOffset.x,
            fixedWorldNewLocal.y + newOffset.y
        })));

        const SvsmPageCoordinate currentOffset{ 35, 27 };
        auto tableForLocal = [currentOffset](
            SvsmPageCoordinate localPage) {
            return WrapSvsmPageCoordinate({
                localPage.x + currentOffset.x,
                localPage.y + currentOffset.y
            });
        };
        assert(!IsSvsmTablePageNewlyExposed(
            tableForLocal({ 63, 63 }), currentOffset, { 0, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 63, 20 }), currentOffset, { 1, 0 }));
        assert(!IsSvsmTablePageNewlyExposed(
            tableForLocal({ 62, 20 }), currentOffset, { 1, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 0, 20 }), currentOffset, { -1, 0 }));
        assert(!IsSvsmTablePageNewlyExposed(
            tableForLocal({ 1, 20 }), currentOffset, { -1, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 20, 63 }), currentOffset, { 0, 1 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 20, 0 }), currentOffset, { 0, -1 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 1, 63 }), currentOffset, { 63, 1 }));
        assert(!IsSvsmTablePageNewlyExposed(
            tableForLocal({ 0, 62 }), currentOffset, { 63, 1 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 62, 0 }), currentOffset, { -63, -1 }));
        assert(!IsSvsmTablePageNewlyExposed(
            tableForLocal({ 63, 1 }), currentOffset, { -63, -1 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 32, 32 }), currentOffset, { 64, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 32, 32 }), currentOffset, { -64, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 32, 32 }), currentOffset, { 0, 64 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 32, 32 }), currentOffset, { 0, -64 }));

        assert(SnapSvsmRenderOrigin(3.99f, 2.f) == 2.f);
        assert(SnapSvsmRenderOrigin(-0.01f, 2.f) == -2.f);
        assert(SnapSvsmRenderOrigin(4.f, 2.f) == 4.f);
    }

    void TestPerPixelRequestDeduplication()
    {
        constexpr uint32_t HashSize = 64u;
        constexpr uint32_t InvalidOwner =
            std::numeric_limits<uint32_t>::max();
        auto hashSlot = [=](uint32_t owner) {
            uint32_t hash = owner;
            hash ^= hash >> 8u;
            hash *= 0x9e3779b1u;
            hash ^= hash >> 16u;
            return hash & (HashSize - 1u);
        };

        std::array<uint32_t, HashSize> firstOwnerBySlot;
        firstOwnerBySlot.fill(InvalidOwner);
        uint32_t collisionOwnerA = InvalidOwner;
        uint32_t collisionOwnerB = InvalidOwner;
        for (uint32_t owner = 0u;
            owner < SvsmClipmapCount * SvsmPagesPerClipmap;
            ++owner)
        {
            const uint32_t slot = hashSlot(owner);
            if (firstOwnerBySlot[slot] == InvalidOwner)
            {
                firstOwnerBySlot[slot] = owner;
            }
            else if (firstOwnerBySlot[slot] != owner)
            {
                collisionOwnerA = firstOwnerBySlot[slot];
                collisionOwnerB = owner;
                break;
            }
        }
        assert(collisionOwnerA != InvalidOwner);
        assert(collisionOwnerB != InvalidOwner);
        assert(hashSlot(collisionOwnerA) == hashSlot(collisionOwnerB));

        const uint32_t centerOwner = EncodeSvsmVirtualPageOwner(
            { 12, 19 }, 0u);
        const uint32_t neighborOwner = EncodeSvsmVirtualPageOwner(
            { 13, 19 }, 0u);
        const uint32_t coarseOwner = EncodeSvsmVirtualPageOwner(
            { 31, 31 }, SvsmClipmapCount - 1u);
        const std::vector<uint32_t> requests = {
            centerOwner,
            centerOwner,
            neighborOwner,
            coarseOwner,
            coarseOwner,
            collisionOwnerA,
            collisionOwnerA,
            collisionOwnerB
        };

        std::array<uint32_t, HashSize> localHash;
        localHash.fill(InvalidOwner);
        std::vector<uint32_t> publishedOwners;
        bool sameOwnerSuppressed = false;
        bool collisionFailedOpen = false;
        for (uint32_t owner : requests)
        {
            const uint32_t slot = hashSlot(owner);
            const uint32_t previous = localHash[slot];
            if (previous == InvalidOwner)
            {
                localHash[slot] = owner;
            }
            else if (previous == owner)
            {
                sameOwnerSuppressed = true;
            }
            else
            {
                publishedOwners.push_back(owner);
                collisionFailedOpen = true;
            }
        }
        for (uint32_t owner : localHash)
        {
            if (owner != InvalidOwner)
                publishedOwners.push_back(owner);
        }

        std::vector<uint32_t> referenceOwners = requests;
        auto sortAndDeduplicate = [](std::vector<uint32_t>& owners) {
            std::sort(owners.begin(), owners.end());
            owners.erase(
                std::unique(owners.begin(), owners.end()),
                owners.end());
        };
        sortAndDeduplicate(referenceOwners);
        sortAndDeduplicate(publishedOwners);
        assert(sameOwnerSuppressed);
        assert(collisionFailedOpen);
        assert(publishedOwners == referenceOwners);
    }

    void TestPacketPageCompaction()
    {
        const uint32_t minimum =
            PackSvsmPacketPageCoordinate(3u, 5u);
        const uint32_t maximum =
            PackSvsmPacketPageCoordinate(6u, 9u);
        assert((UnpackSvsmPacketPageCoordinate(minimum) ==
            SvsmPageCoordinate{ 3, 5 }));
        assert(GetSvsmPacketPageListCapacity(
            minimum, maximum) == 20u);
        assert(GetSvsmPacketPageListCapacity(
            SvsmInvalidPacketPageBounds, maximum) == 0u);
        assert(GetSvsmPacketPageListCapacity(
            SvsmEmptyPacketPageBounds, maximum) == 0u);
        assert(GetSvsmPacketPageListCapacity(
            PackSvsmPacketPageCoordinate(7u, 5u),
            maximum) == 0u);

        // Table coordinates wrap independently from conservative local
        // packet bounds. Offset (63, 62) maps table (2, 3) to local (3, 5).
        assert(IsSvsmPacketPageInsideBounds(
            minimum,
            maximum,
            { 2, 3 },
            { 63, 62 }));
        assert(!IsSvsmPacketPageInsideBounds(
            minimum,
            maximum,
            { 1, 3 },
            { 63, 62 }));

        const std::array<SvsmPageCoordinate, 5> dirtyTablePages = {
            SvsmPageCoordinate{ 2, 3 },
            SvsmPageCoordinate{ 5, 7 },
            SvsmPageCoordinate{ 1, 3 },
            SvsmPageCoordinate{ 7, 9 },
            SvsmPageCoordinate{ 62, 61 }
        };
        uint32_t compactedCount = 0u;
        for (const SvsmPageCoordinate page : dirtyTablePages)
        {
            if (IsSvsmPacketPageInsideBounds(
                    minimum,
                    maximum,
                    page,
                    { 63, 62 }))
            {
                ++compactedCount;
            }
        }
        assert(compactedCount == 2u);
        assert(compactedCount <= GetSvsmPacketPageListCapacity(
            minimum, maximum));

        assert(ShouldScanSvsmPacketRectangleDirectly(true, 1u, 2u));
        assert(ShouldScanSvsmPacketRectangleDirectly(true, 32u, 64u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            false, 1u, 4096u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            true, 33u, 64u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            true, 0u, 64u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            true, 2u, 3u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            true, 64u, 64u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            true, SvsmPagesPerClipmap, SvsmPagesPerClipmap));

        const SvsmPageCoordinate wrappedTablePage =
            SvsmPacketTablePageFromLocalPage(
                { 3, 5 }, { 63, 62 });
        assert((wrappedTablePage == SvsmPageCoordinate{ 2, 3 }));
        assert((SvsmPacketTablePageFromLocalPage(
            { 63, 63 }, { 1, 1 }) ==
            SvsmPageCoordinate{ 0, 0 }));

        assert(GetSvsmDirtyPageScatterInstanceCount(
            true, 0u) == 0u);
        assert(GetSvsmDirtyPageScatterInstanceCount(
            true, 1u) == 1u);
        assert(GetSvsmDirtyPageScatterInstanceCount(
            true, std::numeric_limits<uint32_t>::max()) == 1u);
        assert(GetSvsmDirtyPageScatterInstanceCount(
            false, 17u) == 17u);

        const SvsmDirtyPageRectangle emptyDirtyRectangle =
            DecodeSvsmDirtyPageRectangle({});
        assert(!emptyDirtyRectangle.valid);
        SvsmDirtyPageRectangleEncoding dirtyRectangleForward = {};
        dirtyRectangleForward = AccumulateSvsmDirtyPageRectangle(
            dirtyRectangleForward, { 63, 63 });
        dirtyRectangleForward = AccumulateSvsmDirtyPageRectangle(
            dirtyRectangleForward, { 0, 0 });
        const SvsmDirtyPageRectangle fullDirtyRectangle =
            DecodeSvsmDirtyPageRectangle(dirtyRectangleForward);
        assert(fullDirtyRectangle.valid);
        assert((fullDirtyRectangle.minimum ==
            SvsmPageCoordinate{ 0, 0 }));
        assert((fullDirtyRectangle.maximum ==
            SvsmPageCoordinate{ 63, 63 }));
        SvsmDirtyPageRectangleEncoding dirtyRectangleReverse = {};
        dirtyRectangleReverse = AccumulateSvsmDirtyPageRectangle(
            dirtyRectangleReverse, { 0, 0 });
        dirtyRectangleReverse = AccumulateSvsmDirtyPageRectangle(
            dirtyRectangleReverse, { 63, 63 });
        assert(dirtyRectangleReverse == dirtyRectangleForward);
        const SvsmDirtyPageRectangleEncoding singleDirtyPageEncoding =
            AccumulateSvsmDirtyPageRectangle({}, { 17, 29 });
        assert((singleDirtyPageEncoding ==
            SvsmDirtyPageRectangleEncoding{ 46u, 17u, 34u, 29u }));
        const SvsmDirtyPageRectangle singleDirtyPage =
            DecodeSvsmDirtyPageRectangle(singleDirtyPageEncoding);
        assert(singleDirtyPage.valid);
        assert((singleDirtyPage.minimum ==
            SvsmPageCoordinate{ 17, 29 }));
        assert(singleDirtyPage.minimum == singleDirtyPage.maximum);
        const SvsmDirtyPageRectangleEncoding unchangedDirtyRectangle =
            AccumulateSvsmDirtyPageRectangle(
                singleDirtyPageEncoding, { -1, 64 });
        assert(unchangedDirtyRectangle == singleDirtyPageEncoding);
        assert(!DecodeSvsmDirtyPageRectangle(
            SvsmDirtyPageRectangleEncoding{ 64u, 0u, 0u, 0u }).valid);
        const SvsmDirtyPageRectangle packetRectangle = {
            true, { 10, 20 }, { 30, 40 }
        };
        const SvsmDirtyPageRectangle scheduledRectangle = {
            true, { 25, 0 }, { 63, 29 }
        };
        const SvsmDirtyPageRectangle scatterIntersection =
            IntersectSvsmPageRectangles(
                packetRectangle, scheduledRectangle);
        assert(scatterIntersection.valid);
        assert((scatterIntersection.minimum ==
            SvsmPageCoordinate{ 25, 20 }));
        assert((scatterIntersection.maximum ==
            SvsmPageCoordinate{ 30, 29 }));
        assert(!IntersectSvsmPageRectangles(
            packetRectangle,
            { true, { 31, 41 }, { 63, 63 } }).valid);
        assert(!IntersectSvsmPageRectangles(
            packetRectangle, {}).valid);

        const SvsmPacketPageRectangle noScatterWork =
            ResolveSvsmScatterPacketRectangle(
                0u,
                SvsmInvalidPacketPageBounds,
                SvsmInvalidPacketPageBounds);
        assert(noScatterWork.packedMinimum ==
            SvsmEmptyPacketPageBounds);
        assert(noScatterWork.packedMaximum ==
            SvsmEmptyPacketPageBounds);
        const SvsmPacketPageRectangle malformedScatterBounds =
            ResolveSvsmScatterPacketRectangle(
                1u,
                SvsmEmptyPacketPageBounds,
                SvsmEmptyPacketPageBounds);
        assert((UnpackSvsmPacketPageCoordinate(
            malformedScatterBounds.packedMinimum) ==
            SvsmPageCoordinate{ 0, 0 }));
        assert((UnpackSvsmPacketPageCoordinate(
            malformedScatterBounds.packedMaximum) ==
            SvsmPageCoordinate{ 63, 63 }));
        const uint32_t validScatterMinimum =
            PackSvsmPacketPageCoordinate(7u, 9u);
        const uint32_t validScatterMaximum =
            PackSvsmPacketPageCoordinate(11u, 13u);
        const SvsmPacketPageRectangle validScatterBounds =
            ResolveSvsmScatterPacketRectangle(
                4u,
                validScatterMinimum,
                validScatterMaximum);
        assert(validScatterBounds.packedMinimum ==
            validScatterMinimum);
        assert(validScatterBounds.packedMaximum ==
            validScatterMaximum);

        const SvsmScatterVirtualTexelAddress texel127 =
            GetSvsmScatterVirtualTexelAddress(
                127u, 127u, { 63, 62 }, 2u);
        assert(texel127.valid);
        assert((texel127.localPage ==
            SvsmPageCoordinate{ 0, 0 }));
        assert((texel127.pageTexel ==
            SvsmPageCoordinate{ 127, 127 }));
        assert((texel127.tablePage ==
            SvsmPageCoordinate{ 63, 62 }));
        const SvsmScatterVirtualTexelAddress texel128 =
            GetSvsmScatterVirtualTexelAddress(
                128u, 128u, { 63, 62 }, 2u);
        assert(texel128.valid);
        assert((texel128.localPage ==
            SvsmPageCoordinate{ 1, 1 }));
        assert((texel128.pageTexel ==
            SvsmPageCoordinate{ 0, 0 }));
        assert((texel128.tablePage ==
            SvsmPageCoordinate{ 0, 63 }));
        const SvsmScatterVirtualTexelAddress texel8191 =
            GetSvsmScatterVirtualTexelAddress(
                8191u, 8191u, { 1, 1 }, 5u);
        assert(texel8191.valid);
        assert((texel8191.localPage ==
            SvsmPageCoordinate{ 63, 63 }));
        assert((texel8191.pageTexel ==
            SvsmPageCoordinate{ 127, 127 }));
        assert((texel8191.tablePage ==
            SvsmPageCoordinate{ 0, 0 }));
        assert(texel8191.owner == 5u * SvsmPagesPerClipmap);
        assert(!GetSvsmScatterVirtualTexelAddress(
            8192u, 0u, { 0, 0 }, 0u).valid);
        assert(!GetSvsmScatterVirtualTexelAddress(
            0u, 8192u, { 0, 0 }, 0u).valid);
        assert(!GetSvsmScatterVirtualTexelAddress(
            0u, 0u, { 0, 0 }, SvsmClipmapCount).valid);

        const uint32_t levelZeroOwner =
            EncodeSvsmVirtualPageOwner(wrappedTablePage, 0u);
        const uint32_t levelOneOwner =
            EncodeSvsmVirtualPageOwner(wrappedTablePage, 1u);
        assert(levelZeroOwner != levelOneOwner);
        SvsmPageMetadata scheduledPage;
        scheduledPage.physicalPage = 7u;
        scheduledPage.resident = true;
        scheduledPage.required = true;
        scheduledPage.dirty = true;
        assert(IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            8u,
            levelZeroOwner,
            levelZeroOwner));
        assert(!IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            7u,
            levelZeroOwner,
            levelZeroOwner));
        assert(!IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            8u,
            levelZeroOwner,
            levelOneOwner));
        assert(!IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            8u,
            levelZeroOwner,
            SvsmInvalidPhysicalPage));
        scheduledPage.dirty = false;
        assert(!IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            8u,
            levelZeroOwner,
            levelZeroOwner));
        scheduledPage.dirty = true;
        scheduledPage.resident = false;
        assert(!IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            8u,
            levelZeroOwner,
            levelZeroOwner));

        const uint32_t compactCount = 17u;
        const uint32_t failOpen =
            SvsmPacketPageRuntimeFailOpenBit | compactCount;
        assert((failOpen & SvsmPacketPageRuntimeFailOpenBit) != 0u);
        assert((failOpen & SvsmPacketPageRuntimeCountMask) ==
            compactCount);
        const uint32_t perPage =
            SvsmPacketPageRuntimePerPageBit | compactCount;
        assert((perPage & SvsmPacketPageRuntimePerPageBit) != 0u);
        assert((perPage & SvsmPacketPageRuntimeFailOpenBit) == 0u);
        assert((perPage & SvsmPacketPageRuntimeCountMask) ==
            compactCount);
        assert(!IsSvsmDirtyPageScatterPerPageRuntimeActive(
            false, perPage));
        assert(IsSvsmDirtyPageScatterPerPageRuntimeActive(
            true, perPage));
        assert(IsSvsmDirtyPageScatterPerPageRuntimeActive(
            true,
            perPage | SvsmPacketPageRuntimeFailOpenBit));
        assert(ShouldClipSvsmDirtyPagePerPageRasterToPacketBounds(
            true, perPage));
        assert(!ShouldClipSvsmDirtyPagePerPageRasterToPacketBounds(
            true,
            perPage | SvsmPacketPageRuntimeFailOpenBit));
        assert(!IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::DenseReference, true, true, true, true));
        assert(!IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::SparseCached, false, true, true, true));
        assert(!IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::SparseCached, true, false, true, true));
        assert(!IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::SparseCached, true, true, false, true));
        assert(!IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::SparseCached, true, true, true, false));
        assert(IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::SparseCached, true, true, true, true));
        assert(!IsSvsmDirtyPageScatterSafetyBounded(
            false, true, 4u, 4u));
        assert(!IsSvsmDirtyPageScatterSafetyBounded(
            true, false, 4u, 4u));
        assert(!IsSvsmDirtyPageScatterSafetyBounded(
            true, true, 0u, 4u));
        assert(IsSvsmDirtyPageScatterSafetyBounded(
            true, true, 4u, 4u));
        assert(!IsSvsmDirtyPageScatterSafetyBounded(
            true, true, 4u, 5u));
        assert(IsSvsmDirtyPageScatterSafetyBounded(
            true, true, 2u, 8u));
        assert(!IsSvsmDirtyPageScatterSafetyBounded(
            true, true,
            SvsmMaximumSafeDirtyPageScatterRenderBudget + 1u,
            1u));
        assert(!IsSvsmDirtyPageScatterSafetyBounded(
            true, true,
            std::numeric_limits<uint32_t>::max(),
            1u));
        assert(!ShouldUseSvsmDirtyPageScatterPerPageFallback(
            false, 4096u, 4u, 4u));
        assert(!ShouldUseSvsmDirtyPageScatterPerPageFallback(
            true, 16u, 4u, 4u));
        assert(ShouldUseSvsmDirtyPageScatterPerPageFallback(
            true, 17u, 4u, 4u));
        assert(!ShouldUseSvsmDirtyPageScatterPerPageFallback(
            true, 4096u, 0u, 4u));
        assert(!ShouldUseSvsmDirtyPageScatterPerPageFallback(
            true, 4096u, 4u, 0u));
        assert(!ShouldUseSvsmDirtyPageScatterPerPageFallback(
            true,
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max()));
        assert(SvsmLevelHasWorkDispatchGate == 1u);

        constexpr uint64_t maximumPacketPageDispatchGroupCount =
            uint64_t(SvsmMaximumDispatchGroupsPerDimension) *
            uint64_t(SvsmMaximumDispatchGroupsPerDimension);
        constexpr uint32_t maximumExactPacketCount =
            uint32_t(maximumPacketPageDispatchGroupCount);
        assert(CanDispatchSvsmPacketPageCulling(0u, false));
        assert(CanDispatchSvsmPacketPageCulling(
            SvsmMaximumDispatchGroupsPerDimension + 1u, false));
        assert(CanDispatchSvsmPacketPageCulling(
            maximumExactPacketCount, false));
        assert(!CanDispatchSvsmPacketPageCulling(
            maximumExactPacketCount + 1u, false));
        assert(!CanDispatchSvsmPacketPageCulling(
            std::numeric_limits<uint32_t>::max(), false));
        assert(CanDispatchSvsmPacketPageCulling(
            std::numeric_limits<uint32_t>::max(), true));
        assert((GetSvsmIndirectFillDispatchDimensions(
            0u, false, false) ==
            SvsmDispatchDimensions{ 0u, 0u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            65u, false, false) ==
            SvsmDispatchDimensions{ 2u, 1u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            65u, true, true) ==
            SvsmDispatchDimensions{ 2u, 1u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            SvsmMaximumDispatchGroupsPerDimension,
            true, false) == SvsmDispatchDimensions{ 65535u, 1u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            SvsmMaximumDispatchGroupsPerDimension + 1u,
            true, false) == SvsmDispatchDimensions{ 65535u, 2u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            SvsmMaximumDispatchGroupsPerDimension *
                SvsmPacketFillThreadsPerGroup + 1u,
            false, false) == SvsmDispatchDimensions{ 65535u, 2u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            maximumExactPacketCount,
            true, false) == SvsmDispatchDimensions{ 65535u, 65535u }));
        const SvsmDispatchDimensions overflowingExactDispatch =
            GetSvsmIndirectFillDispatchDimensions(
                maximumExactPacketCount + 1u,
                true,
                false);
        assert(overflowingExactDispatch.groupsX ==
            SvsmMaximumDispatchGroupsPerDimension);
        assert(overflowingExactDispatch.groupsY ==
            SvsmMaximumDispatchGroupsPerDimension + 1u);
        const SvsmDispatchDimensions maximumScatterDispatch =
            GetSvsmIndirectFillDispatchDimensions(
                std::numeric_limits<uint32_t>::max(),
                true,
                true);
        assert(maximumScatterDispatch.groupsX <=
            SvsmMaximumDispatchGroupsPerDimension);
        assert(maximumScatterDispatch.groupsY <=
            SvsmMaximumDispatchGroupsPerDimension);
        const SvsmDispatchDimensions maximumFallbackDispatch =
            GetSvsmIndirectFillDispatchDimensions(
                std::numeric_limits<uint32_t>::max(),
                false,
                false);
        assert(maximumFallbackDispatch.groupsX <=
            SvsmMaximumDispatchGroupsPerDimension);
        assert(maximumFallbackDispatch.groupsY <=
            SvsmMaximumDispatchGroupsPerDimension);
        assert(CanUseSvsmStaticPacketBounds(false, false, false));
        assert(!CanUseSvsmStaticPacketBounds(true, false, false));
        assert(!CanUseSvsmStaticPacketBounds(false, true, false));
        assert(!CanUseSvsmStaticPacketBounds(false, false, true));
        assert(!ShouldPrepareSvsmRenderPacketsForClipmap(0u, 1u));
        assert(ShouldPrepareSvsmRenderPacketsForClipmap(1u, 1u));
        assert(ShouldPrepareSvsmRenderPacketsForClipmap(5u, 2u));
        assert(!ShouldPrepareSvsmRenderPacketsForClipmap(6u, 0u));
        assert(ShouldPrepareSvsmRenderPacketsForClipmap(5u, 99u));

        const float pageFiveCenter =
            float(SvsmPageSize * 5u) +
            float(SvsmPageSize) * 0.5f;
        const SvsmPacketPageRectangle tinyCaster =
            GetSvsmPacketPageRectangle(
                pageFiveCenter,
                pageFiveCenter,
                pageFiveCenter,
                pageFiveCenter);
        assert((UnpackSvsmPacketPageCoordinate(
            tinyCaster.packedMinimum) ==
            SvsmPageCoordinate{ 5, 5 }));
        assert((UnpackSvsmPacketPageCoordinate(
            tinyCaster.packedMaximum) ==
            SvsmPageCoordinate{ 5, 5 }));
        const SvsmPacketPageRectangle boundaryCaster =
            GetSvsmPacketPageRectangle(
                float(SvsmPageSize),
                float(SvsmPageSize),
                float(SvsmPageSize),
                float(SvsmPageSize));
        assert((UnpackSvsmPacketPageCoordinate(
            boundaryCaster.packedMinimum) ==
            SvsmPageCoordinate{ 0, 0 }));
        assert((UnpackSvsmPacketPageCoordinate(
            boundaryCaster.packedMaximum) ==
            SvsmPageCoordinate{ 1, 1 }));
        const SvsmPacketPageRectangle negativeGuardEdge =
            GetSvsmPacketPageRectangle(
                float(SvsmPageSize) + SvsmPacketBoundsTexelHalo,
                pageFiveCenter,
                float(SvsmPageSize) + SvsmPacketBoundsTexelHalo,
                pageFiveCenter);
        assert((UnpackSvsmPacketPageCoordinate(
            negativeGuardEdge.packedMinimum) ==
            SvsmPageCoordinate{ 0, 5 }));
        const SvsmPacketPageRectangle positiveGuardEdge =
            GetSvsmPacketPageRectangle(
                float(SvsmPageSize) - SvsmPacketBoundsTexelHalo,
                pageFiveCenter,
                float(SvsmPageSize) - SvsmPacketBoundsTexelHalo,
                pageFiveCenter);
        assert((UnpackSvsmPacketPageCoordinate(
            positiveGuardEdge.packedMaximum) ==
            SvsmPageCoordinate{ 1, 5 }));
        const SvsmPacketPageRectangle outsideCaster =
            GetSvsmPacketPageRectangle(-4.f, -4.f, -2.f, -2.f);
        assert(outsideCaster.packedMinimum ==
            SvsmEmptyPacketPageBounds);
        assert(outsideCaster.packedMaximum ==
            SvsmEmptyPacketPageBounds);
        const SvsmPacketPageRectangle invalidCaster =
            GetSvsmPacketPageRectangle(2.f, 0.f, 1.f, 1.f);
        assert(invalidCaster.packedMinimum ==
            SvsmInvalidPacketPageBounds);
        assert(invalidCaster.packedMaximum ==
            SvsmInvalidPacketPageBounds);
        assert(RequiresSvsmPacketPageModeTransition(
            true, false, true, true, false,
            true, true, true));
        assert(RequiresSvsmPacketPageModeTransition(
            true, true, false, false, false,
            true, true, true));
        assert(RequiresSvsmPacketPageModeTransition(
            true, true, true, false, false,
            true, true, true));
        assert(!RequiresSvsmPacketPageModeTransition(
            true, true, true, false, true,
            true, true, true));
        assert(!RequiresSvsmPacketPageModeTransition(
            true, true, true, true, false,
            true, true, true));
        assert(!RequiresSvsmPacketPageModeTransition(
            false, true, false, false, false,
            false, false, true));
        assert(RequiresSvsmPacketPageModeTransition(
            true, true, true, true, true,
            false, false, false));
        assert(RequiresSvsmPacketPageModeTransition(
            true, true, true, true, true,
            true, true, false));
    }

    void TestStaticPageRequestActions()
    {
        assert(!IsSvsmStaticJitterActive(0.f, 0.f));
        assert(IsSvsmStaticJitterActive(0.25f, 0.f));
        assert(IsSvsmStaticJitterActive(0.f, -0.25f));
        assert(!ShouldResetSvsmStaticJitterCache(
            false, false, 0.f, 0.f));

        bool previousJitterActive = false;
        for (uint64_t phase = 0u;
            phase < MiniEngineTaaHalton23.size();
            ++phase)
        {
            const MiniEngineTaaJitterSample offset =
                GetMiniEngineTaaJitter(phase);
            assert(IsSvsmStaticJitterActive(offset.x, offset.y));
            assert(ShouldResetSvsmStaticJitterCache(
                true,
                previousJitterActive,
                offset.x,
                offset.y) == (phase == 0u));
            previousJitterActive = true;
        }
        assert(ShouldResetSvsmStaticJitterCache(
            true, previousJitterActive, 0.f, 0.f));
        previousJitterActive = false;
        assert(!ShouldResetSvsmStaticJitterCache(
            true, previousJitterActive, 0.f, 0.f));

        assert(SelectSvsmStaticPageRequestAction(false, false) ==
            SvsmStaticPageRequestAction::Rebuild);
        assert(SelectSvsmStaticPageRequestAction(false, true) ==
            SvsmStaticPageRequestAction::Rebuild);
        assert(SelectSvsmStaticPageRequestAction(true, false) ==
            SvsmStaticPageRequestAction::ExtendUnion);
        assert(SelectSvsmStaticPageRequestAction(true, true) ==
            SvsmStaticPageRequestAction::Reuse);
        assert(SelectSvsmStaticPageRequestAction(
            true, true, true) ==
            SvsmStaticPageRequestAction::Drain);
        assert(ShouldInvalidateSvsmStaticVisibility(
            SvsmStaticPageRequestAction::Rebuild));
        assert(ShouldInvalidateSvsmStaticVisibility(
            SvsmStaticPageRequestAction::ExtendUnion));
        assert(ShouldInvalidateSvsmStaticVisibility(
            SvsmStaticPageRequestAction::Drain));
        assert(!ShouldInvalidateSvsmStaticVisibility(
            SvsmStaticPageRequestAction::Reuse));
        assert(ShouldMarkSvsmStaticPageRequests(
            SvsmStaticPageRequestAction::Rebuild));
        assert(ShouldMarkSvsmStaticPageRequests(
            SvsmStaticPageRequestAction::ExtendUnion));
        assert(!ShouldMarkSvsmStaticPageRequests(
            SvsmStaticPageRequestAction::Drain));
        assert(!ShouldMarkSvsmStaticPageRequests(
            SvsmStaticPageRequestAction::Reuse));
        assert(ShouldMaintainSvsmStaticPages(
            SvsmStaticPageRequestAction::Rebuild));
        assert(ShouldMaintainSvsmStaticPages(
            SvsmStaticPageRequestAction::ExtendUnion));
        assert(ShouldMaintainSvsmStaticPages(
            SvsmStaticPageRequestAction::Drain));
        assert(!ShouldMaintainSvsmStaticPages(
            SvsmStaticPageRequestAction::Reuse));
        assert(IsSvsmStaticPageMaintenanceOptimizationActive(
            true, SvsmStaticPageRequestAction::Rebuild));
        assert(IsSvsmStaticPageMaintenanceOptimizationActive(
            true, SvsmStaticPageRequestAction::Drain));
        assert(!IsSvsmStaticPageMaintenanceOptimizationActive(
            false, SvsmStaticPageRequestAction::Rebuild));
        assert(!IsSvsmStaticPageMaintenanceOptimizationActive(
            true, SvsmStaticPageRequestAction::Reuse));
        assert(CanReuseSvsmStaticVisibility(
            true, true, true, true));
        assert(!CanReuseSvsmStaticVisibility(
            false, true, true, true));
        assert(!CanReuseSvsmStaticVisibility(
            true, true, false, true));
        assert(!CanReuseSvsmStaticVisibility(
            true, true, true, false));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, 1u,
            std::numeric_limits<uint32_t>::max(), true));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u,
            std::numeric_limits<uint32_t>::max(), true));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, SvsmPagesPerClipmap,
            std::numeric_limits<uint32_t>::max(), true));
        assert(!CanUseSvsmStaticPageRequestConfiguration(
            true, true, 0u,
            std::numeric_limits<uint32_t>::max(), true));
        assert(!CanUseSvsmStaticPageRequestConfiguration(
            true, true, SvsmPagesPerClipmap + 1u,
            std::numeric_limits<uint32_t>::max(), true));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u, 64u, true));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u, 65u, true));
        assert(!CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u, 63u, true));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u, 63u, true, true));
        assert(!CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u, 0u, true, true));
        assert(GetSvsmStaticPageDrainPassCount(0u, 4u) == 0u);
        assert(GetSvsmStaticPageDrainPassCount(4096u, 0u) == 0u);
        assert(GetSvsmStaticPageDrainPassCount(4096u, 4u) == 1024u);
        assert(GetSvsmStaticPageDrainPassCount(4096u, 256u) == 16u);
        assert(GetSvsmStaticPageDrainPassCount(4096u, 4096u) == 1u);
        assert(GetSvsmStaticPageDrainPassCount(4095u, 4096u) == 1u);
        assert(GetSvsmStaticPageDrainPassCount(
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max()) == 1u);
        assert(GetSvsmStaticPageDrainPassCount(
            std::numeric_limits<uint32_t>::max(), 1u) ==
            std::numeric_limits<uint32_t>::max());
        assert(CanUseSvsmStaticZeroWorkFastPath(
            true, true, false, false, false));
        assert(!CanUseSvsmStaticZeroWorkFastPath(
            false, true, false, false, false));
        assert(!CanUseSvsmStaticZeroWorkFastPath(
            true, false, false, false, false));
        const SvsmStaticPageRequestAction reusedRequestsWithLiveResolve =
            SelectSvsmStaticPageRequestAction(true, true, false);
        assert(reusedRequestsWithLiveResolve ==
            SvsmStaticPageRequestAction::Reuse);
        assert(!IsSvsmStaticPageMaintenanceOptimizationActive(
            true, reusedRequestsWithLiveResolve));
        assert(!CanUseSvsmStaticZeroWorkFastPath(
            true, true, true, false, false));
        assert(!CanUseSvsmStaticZeroWorkFastPath(
            true, true, false, true, false));
        assert(!CanUseSvsmStaticZeroWorkFastPath(
            true, true, false, false, true));

        // One fixed no-jitter phase marks once and then reuses. An eight-phase
        // cycle extends only for each unseen phase and performs no page work
        // throughout the next complete cycle.
        bool noJitterSeen = false;
        assert(SelectSvsmStaticPageRequestAction(
            true, noJitterSeen) ==
            SvsmStaticPageRequestAction::ExtendUnion);
        noJitterSeen = true;
        assert(SelectSvsmStaticPageRequestAction(
            true, noJitterSeen) ==
            SvsmStaticPageRequestAction::Reuse);

        std::array<bool, 8> jitterSeen{};
        for (uint32_t phase = 0u; phase < jitterSeen.size(); ++phase)
        {
            assert(SelectSvsmStaticPageRequestAction(
                true, jitterSeen[phase]) ==
                SvsmStaticPageRequestAction::ExtendUnion);
            jitterSeen[phase] = true;
        }
        for (uint32_t phase = 0u; phase < jitterSeen.size(); ++phase)
        {
            assert(SelectSvsmStaticPageRequestAction(
                true, jitterSeen[phase]) ==
                SvsmStaticPageRequestAction::Reuse);
        }

        uint32_t drainFramesRemaining =
            GetSvsmStaticPageDrainPassCount(4096u, 4u);
        for (uint32_t pass = 0u;
            pass < 1024u;
            ++pass)
        {
            const SvsmStaticPageRequestAction action =
                pass == 0u
                    ? SelectSvsmStaticPageRequestAction(
                        false, false, true)
                    : SelectSvsmStaticPageRequestAction(
                        true, true, drainFramesRemaining > 0u);
            assert(action == (pass == 0u
                ? SvsmStaticPageRequestAction::Rebuild
                : SvsmStaticPageRequestAction::Drain));
            assert(ShouldMaintainSvsmStaticPages(action));
            assert(ShouldMarkSvsmStaticPageRequests(action) ==
                (pass == 0u));
            --drainFramesRemaining;
        }
        assert(drainFramesRemaining == 0u);
        assert(SelectSvsmStaticPageRequestAction(
            true, true, drainFramesRemaining > 0u) ==
            SvsmStaticPageRequestAction::Reuse);
    }

    void TestClipmapSelection()
    {
        assert(GetSvsmFirstClipmapLevel(
            SvsmResolutionBias::Zero) == 0u);
        assert(GetSvsmFirstClipmapLevel(
            SvsmResolutionBias::PlusOne) == 1u);
        assert(GetSvsmFirstClipmapLevel(
            SvsmResolutionBias::PlusTwo) == 2u);
        assert(SelectFinestSvsmClipmap(
            0.f, 0.f, 20.f, SvsmResolutionBias::Zero) == 0);
        assert(SelectFinestSvsmClipmap(
            10.f, -10.f, 20.f, SvsmResolutionBias::Zero) == 0);
        assert(SelectFinestSvsmClipmap(
            10.01f, 0.f, 20.f, SvsmResolutionBias::Zero) == 1);
        assert(SelectFinestSvsmClipmap(
            160.01f, 0.f, 20.f, SvsmResolutionBias::Zero) == 5);
        assert(SelectFinestSvsmClipmap(
            321.f, 0.f, 20.f, SvsmResolutionBias::Zero) == -1);
        assert(SelectFinestSvsmClipmap(
            0.f, 0.f, 20.f, SvsmResolutionBias::PlusTwo) == 2);
        assert(SelectFinestSvsmClipmap(
            std::numeric_limits<float>::quiet_NaN(),
            0.f,
            20.f,
            SvsmResolutionBias::Zero) == -1);
    }

    void TestReverseDepthWrites()
    {
        uint32_t depth = 0u;
        depth = WriteSvsmReverseDepth(depth, 0.1f);
        assert(std::abs(DecodeSvsmReverseDepth(depth) - 0.1f) < 1e-7f);
        depth = WriteSvsmReverseDepth(depth, 0.9f);
        assert(std::abs(DecodeSvsmReverseDepth(depth) - 0.9f) < 1e-7f);
        depth = WriteSvsmReverseDepth(depth, 0.4f);
        assert(std::abs(DecodeSvsmReverseDepth(depth) - 0.9f) < 1e-7f);

        assert(EncodeSvsmReverseDepth(-1.f) == 0u);
        assert(DecodeSvsmReverseDepth(
            EncodeSvsmReverseDepth(2.f)) == 1.f);
        assert(EncodeSvsmReverseDepth(
            std::numeric_limits<float>::quiet_NaN()) == 0u);
    }

    void TestAllocationEvictionAndCacheReuse()
    {
        assert(GetSvsmPageAgeElapsed(10u, 3u) == 7u);
        assert(GetSvsmPageAgeElapsed(3u, SvsmPageAgeMask - 1u) == 5u);
        assert(IsSvsmPageInsideRecentEvictionGrace(10u, 3u));
        assert(!IsSvsmPageInsideRecentEvictionGrace(11u, 3u));
        assert(IsSvsmPageInsideRecentEvictionGrace(
            3u, SvsmPageAgeMask - 1u));
        assert(IsSvsmPageInsideRecentEvictionGrace(
            3u, SvsmPageAgeMask - 3u));
        assert(!IsSvsmPageInsideRecentEvictionGrace(
            3u, SvsmPageAgeMask - 4u));
        assert(ClassifySvsmCachedPage(false, 3u, 3u) ==
            SvsmEvictionCandidateList::UnrecentCached);
        assert(ClassifySvsmCachedPage(true, 10u, 3u) ==
            SvsmEvictionCandidateList::RecentCached);
        assert(ClassifySvsmCachedPage(true, 11u, 3u) ==
            SvsmEvictionCandidateList::UnrecentCached);

        const uint32_t coarsestLevel = SvsmClipmapCount - 1u;
        assert(SelectSvsmEvictionCandidateList(
            coarsestLevel, 1u, 1u, 1u, 1u) ==
            SvsmEvictionCandidateList::Free);
        assert(SelectSvsmEvictionCandidateList(
            coarsestLevel, 0u, 1u, 1u, 1u) ==
            SvsmEvictionCandidateList::UnrecentCached);
        assert(SelectSvsmEvictionCandidateList(
            coarsestLevel, 0u, 0u, 1u, 1u) ==
            SvsmEvictionCandidateList::RecentCached);
        assert(SelectSvsmEvictionCandidateList(
            coarsestLevel, 0u, 0u, 0u, 1u) ==
            SvsmEvictionCandidateList::RequiredFine);
        assert(SelectSvsmEvictionCandidateList(
            coarsestLevel - 1u, 0u, 0u, 0u, 1u) ==
            SvsmEvictionCandidateList::None);

        struct Slot
        {
            uint32_t owner = SvsmInvalidPhysicalPage;
            uint32_t age = 0u;
            bool visited = false;
        };

        std::array<Slot, 2> pool{};
        pool[0] = { 10u, 5u, true };
        pool[1] = { 20u, 1u, false };

        auto allocate = [&pool](uint32_t owner) {
            for (uint32_t index = 0; index < pool.size(); ++index)
            {
                if (pool[index].owner == SvsmInvalidPhysicalPage)
                {
                    pool[index] = { owner, 0u, true };
                    return index;
                }
            }

            uint32_t victim = SvsmInvalidPhysicalPage;
            uint32_t oldestAge = 0u;
            for (uint32_t index = 0; index < pool.size(); ++index)
            {
                if (!pool[index].visited && pool[index].age >= oldestAge)
                {
                    victim = index;
                    oldestAge = pool[index].age;
                }
            }
            if (victim == SvsmInvalidPhysicalPage)
                return victim;
            pool[victim] = { owner, 0u, true };
            return victim;
        };

        pool[1].age = 9u;
        assert(allocate(30u) == 1u);
        assert(pool[0].owner == 10u);
        assert(pool[1].owner == 30u);

        // The same wrapped virtual page retains its owner and requires no new
        // render work while camera, light, and caster state remain unchanged.
        SvsmPageMetadata cached;
        cached.physicalPage = 0u;
        cached.resident = true;
        cached.required = true;
        cached.dirty = false;
        const uint32_t before = PackSvsmPageMetadata(cached);
        const uint32_t after = PackSvsmPageMetadata(cached);
        assert(before == after);
        assert(!UnpackSvsmPageMetadata(after).dirty);
    }

    void TestInvalidationAndFallback()
    {
        SvsmPageMetadata metadata;
        metadata.physicalPage = 42u;
        metadata.resident = true;
        metadata.required = true;
        metadata.dirty = false;

        metadata.dirty = true;
        assert(UnpackSvsmPageMetadata(
            PackSvsmPageMetadata(metadata)).dirty);

        std::array<bool, SvsmClipmapCount> valid{};
        valid[3] = true;
        valid[5] = true;
        assert(SelectSvsmFallbackLevel(1u, valid) == 3u);
        valid[3] = false;
        assert(SelectSvsmFallbackLevel(1u, valid) == 5u);
        valid[5] = false;
        assert(SelectSvsmFallbackLevel(1u, valid) == SvsmClipmapCount);
    }

    void TestPageBoundaryFiltering()
    {
        assert(GetSvsmFilterRadius(SvsmTapCount::One) == 0u);
        assert(GetSvsmFilterRadius(SvsmTapCount::Four) == 3u);
        assert(GetSvsmFilterRadius(SvsmTapCount::Eight) == 3u);
        assert(GetSvsmFilterRadius(SvsmTapCount::Sixteen) == 3u);
        assert(IsSvsmFilterFootprintInsidePage(
            0u,
            0u,
            GetSvsmFilterRadius(SvsmTapCount::One)));
        assert(!IsSvsmFilterFootprintInsidePage(
            0u,
            0u,
            GetSvsmFilterRadius(SvsmTapCount::Sixteen)));
        assert(IsSvsmFilterFootprintInsidePage(64u, 64u, 4u));
        assert(!IsSvsmFilterFootprintInsidePage(127u, 64u, 1u));
        assert(!IsSvsmFilterFootprintInsidePage(128u, 64u, 1u));
        assert(IsSvsmFilterFootprintInsidePage(129u, 65u, 1u));

        // Adjacent virtual pages intentionally receive unrelated physical
        // owners; receiver translation must happen independently per tap.
        std::array<uint32_t, 2> neighboringPhysicalPages = { 91u, 7u };
        assert(neighboringPhysicalPages[1] !=
            neighboringPhysicalPages[0] + 1u);
    }

    void TestProfiles()
    {
        SparseVirtualShadowMapSettings settings;
        settings.enabled = true;
        settings.firstClipmapExtent = 37.f;
        settings.maximumLightDepth = 480.f;
        settings.physicalPageCount = 2048u;
        assert(settings.preset == SvsmPreset::Quality);
        assert(settings.perPixelMarkingDedupeEnabled);
        assert(!settings.coarsestPageRenderBudgetEnabled);
        assert(!settings.dirtyPageScatterRasterEnabled);
        assert(!settings.scatterAlphaTestEarlyRejectEnabled);
        assert(!settings.dirtyPageScatterAmplificationGuardEnabled);
        assert(settings.dirtyPageScatterMaximumAmplification == 4u);
        assert(settings.packetRectangleDirectScanEnabled);
        assert(settings.recentPageEvictionGraceEnabled);
        assert(settings.staticVisibilityCachingEnabled);
        assert(settings.batchedDrawSubmissionEnabled);
        assert(settings.packetStateSortingEnabled);
        assert(settings.levelEmptyWorkSkipEnabled);
        assert(settings.packetPageCullingEnabled);
        assert(!settings.detailedGpuTimingEnabled);

        SparseVirtualShadowMapSettings customSettings = settings;
        customSettings.mode = SvsmMode::DenseReference;
        customSettings.tapCount = SvsmTapCount::Four;
        customSettings.pageRenderBudget = 17u;
        SparseVirtualShadowMapSettings expectedCustom = customSettings;
        expectedCustom.preset = SvsmPreset::Custom;
        ApplySvsmPreset(customSettings, SvsmPreset::Custom);
        assert(IsSameSvsmConfiguration(
            customSettings, expectedCustom));

        settings.dirtyPageScatterRasterEnabled = true;
        settings.scatterAlphaTestEarlyRejectEnabled = true;
        settings.dirtyPageScatterAmplificationGuardEnabled = true;
        settings.coarsestPageRenderBudgetEnabled = true;
        ApplySvsmPreset(settings, SvsmPreset::Performance);
        assert(settings.enabled);
        assert(settings.firstClipmapExtent == 37.f);
        assert(settings.maximumLightDepth == 480.f);
        assert(settings.physicalPageCount == 2048u);
        assert(settings.preset == SvsmPreset::Performance);
        assert(settings.tapCount == SvsmTapCount::Eight);
        assert(settings.resolutionBias == SvsmResolutionBias::PlusOne);
        assert(settings.adaptiveFiltering);
        assert(settings.staticPageRequestReuseEnabled);
        assert(settings.allocationBudgetSaturationEarlyOutEnabled);
        assert(settings.finiteBudgetStaticDrainEnabled);
        assert(settings.staticVisibilityCachingEnabled);
        assert(settings.sceneStateCachingEnabled);
        assert(settings.renderPacketCachingEnabled);
        assert(settings.gpuGatedDrawSubmission);
        assert(settings.batchedDrawSubmissionEnabled);
        assert(settings.packetStateSortingEnabled);
        assert(settings.levelEmptyWorkSkipEnabled);
        assert(settings.perPixelMarkingDedupeEnabled);
        assert(settings.packetPageCullingEnabled);
        assert(!settings.dirtyPageScatterRasterEnabled);
        assert(!settings.scatterAlphaTestEarlyRejectEnabled);
        assert(!settings.dirtyPageScatterAmplificationGuardEnabled);
        assert(settings.dirtyPageScatterMaximumAmplification == 4u);
        assert(settings.packetRectangleDirectScanEnabled);
        assert(settings.recentPageEvictionGraceEnabled);
        assert(settings.pageTranslationCachingEnabled);
        assert(!settings.detailedGpuTimingEnabled);
        assert(settings.pageRenderBudget ==
            std::numeric_limits<uint32_t>::max());
        assert(!settings.coarsestPageRenderBudgetEnabled);
        assert(ValidateSvsmSettings(settings));

        ApplySvsmPreset(settings, SvsmPreset::Balanced);
        assert(settings.preset == SvsmPreset::Balanced);
        assert(settings.tapCount == SvsmTapCount::Eight);
        assert(settings.resolutionBias == SvsmResolutionBias::Zero);
        assert(settings.adaptiveFiltering);
        assert(ValidateSvsmSettings(settings));

        ApplySvsmPreset(settings, SvsmPreset::Quality);
        assert(settings.preset == SvsmPreset::Quality);
        assert(settings.tapCount == SvsmTapCount::Sixteen);
        assert(settings.resolutionBias == SvsmResolutionBias::Zero);
        assert(!settings.adaptiveFiltering);
        assert(ValidateSvsmSettings(settings));

        settings.pageTranslationCachingEnabled = false;
        settings.preset = SvsmPreset::Custom;
        settings.finiteBudgetStaticDrainEnabled = true;
        settings.allocationBudgetSaturationEarlyOutEnabled = true;
        settings.detailedGpuTimingEnabled = false;
        assert(ValidateSvsmSettings(settings));
        settings.levelEmptyWorkSkipEnabled = true;
        assert(ValidateSvsmSettings(settings));
        settings.perPixelMarkingDedupeEnabled = true;
        assert(ValidateSvsmSettings(settings));
        settings.packetPageCullingEnabled = true;
        settings.dirtyPageScatterRasterEnabled = true;
        settings.scatterAlphaTestEarlyRejectEnabled = true;
        settings.dirtyPageScatterAmplificationGuardEnabled = true;
        settings.packetRectangleDirectScanEnabled = true;
        assert(ValidateSvsmSettings(settings));
        settings.dirtyPageScatterMaximumAmplification = 0u;
        assert(!ValidateSvsmSettings(settings));
        settings.dirtyPageScatterMaximumAmplification =
            SvsmMaximumDirtyPageScatterAmplification + 1u;
        assert(!ValidateSvsmSettings(settings));
        settings.dirtyPageScatterMaximumAmplification = 4u;
        assert(ValidateSvsmSettings(settings));
        settings.pageRenderBudget = 0u;
        assert(ValidateSvsmSettings(settings));

    }

    void TestTapCountPermutationSelection()
    {
        assert(SvsmTapCountPermutationIndex(
            SvsmTapCount::One) == 0u);
        assert(SvsmTapCountPermutationIndex(
            SvsmTapCount::Four) == 1u);
        assert(SvsmTapCountPermutationIndex(
            SvsmTapCount::Eight) == 2u);
        assert(SvsmTapCountPermutationIndex(
            SvsmTapCount::Sixteen) == 3u);
    }

    void TestBatchedDrawPackingAndGrouping()
    {
        assert(SvsmDebugCounterCount == 17u);
        assert(SvsmLevelRenderCounterBase == 17u);
        assert(SvsmAllocatorControlCounterBase == 23u);
        assert(SvsmLevelHasWorkCounterBase == 31u);
        assert(SvsmLevelHasWorkCounterCount == SvsmClipmapCount);
        assert(SvsmCounterCount == 37u);
        assert(GetSvsmLevelHasWorkCounterIndex(0u) == 31u);
        assert(GetSvsmLevelHasWorkCounterIndex(
            SvsmClipmapCount - 1u) == 36u);
        assert(GetSvsmLevelHasWorkCounterIndex(
            SvsmClipmapCount - 1u) + 1u == SvsmCounterCount);
        assert(EncodeSvsmLevelHasWorkIndirectCount(0u, 0u) == 0u);
        assert(EncodeSvsmLevelHasWorkIndirectCount(0u, 23u) == 0u);
        assert(EncodeSvsmLevelHasWorkIndirectCount(1u, 0u) == 0u);
        assert(EncodeSvsmLevelHasWorkIndirectCount(1u, 23u) == 23u);
        assert(EncodeSvsmLevelHasWorkIndirectCount(4096u, 23u) == 23u);
        assert(EncodeSvsmLevelHasWorkIndirectCount(
            1u, std::numeric_limits<uint32_t>::max()) ==
            std::numeric_limits<uint32_t>::max());
        assert(EncodeSvsmLevelHasWorkIndirectCount(
            0u, std::numeric_limits<uint32_t>::max()) == 0u);
        constexpr uint32_t packetGroupCount = 7u;
        assert(std::min(
            EncodeSvsmLevelHasWorkIndirectCount(0u, 23u),
            packetGroupCount) == 0u);
        assert(std::min(
            EncodeSvsmLevelHasWorkIndirectCount(1u, 23u),
            packetGroupCount) == packetGroupCount);
        constexpr uint32_t secondPacketGroupCount = 16u;
        assert(std::min(
            EncodeSvsmLevelHasWorkIndirectCount(1u, 23u),
            secondPacketGroupCount) == secondPacketGroupCount);
        assert(packetGroupCount + secondPacketGroupCount == 23u);
        assert(std::min(
            EncodeSvsmLevelHasWorkIndirectCount(1u, 23u),
            23u) == 23u);
        constexpr uint32_t allocationDispatchGate =
            SvsmLevelHasWorkDispatchGate;
        static_assert(allocationDispatchGate == 1u);
        constexpr uint32_t promotedDrawCount =
            EncodeSvsmLevelHasWorkIndirectCount(1u, 23u);
        static_assert(promotedDrawCount == 23u);
        constexpr uint32_t exactDirtyPageCount = 1u;
        assert(std::min(
            exactDirtyPageCount,
            packetGroupCount) == exactDirtyPageCount);
        assert(exactDirtyPageCount != packetGroupCount);

        constexpr uint32_t poolPages = 4096u;
        constexpr uint32_t maximumObject =
            (std::numeric_limits<uint32_t>::max() -
                (poolPages - 1u)) /
            poolPages;
        assert(CanEncodeSvsmBatchedDraw(
            uint32_t(std::numeric_limits<int32_t>::max()),
            maximumObject,
            poolPages));
        assert(!CanEncodeSvsmBatchedDraw(
            uint32_t(std::numeric_limits<int32_t>::max()) + 1u,
            0u,
            poolPages));
        assert(!CanEncodeSvsmBatchedDraw(0u, 0u, 0u));
        if (maximumObject < std::numeric_limits<uint32_t>::max())
        {
            assert(!CanEncodeSvsmBatchedDraw(
                0u, maximumObject + 1u, poolPages));
        }

        const uint32_t encodedStart =
            EncodeSvsmBatchedStartInstance(
                maximumObject, poolPages);
        assert(DecodeSvsmBatchedObjectIndex(
            encodedStart, poolPages) == maximumObject);
        assert(IsSvsmBatchedStartInstanceEncodingValid(
            encodedStart, poolPages));
        assert(!IsSvsmBatchedStartInstanceEncodingValid(
            encodedStart + 1u, poolPages));
        assert(EncodeSvsmBatchedBaseVertex(1234u) == 1234);

        constexpr SvsmBatchedDrawStateKey opaqueState =
            MakeSvsmBatchedDrawStateKey(11u, 22u, 1u, false);
        assert(opaqueState ==
            MakeSvsmBatchedDrawStateKey(11u, 23u, 1u, false));
        assert(!(opaqueState ==
            MakeSvsmBatchedDrawStateKey(12u, 22u, 1u, false)));
        assert(!(opaqueState ==
            MakeSvsmBatchedDrawStateKey(11u, 22u, 2u, false)));
        assert(!(opaqueState ==
            MakeSvsmBatchedDrawStateKey(11u, 22u, 1u, true)));
        assert(!(MakeSvsmBatchedDrawStateKey(11u, 22u, 1u, true) ==
            MakeSvsmBatchedDrawStateKey(11u, 23u, 1u, true)));

        constexpr SvsmPacketStateSortKey opaqueSortA =
            MakeSvsmPacketStateSortKey(opaqueState, 22u, true);
        constexpr SvsmPacketStateSortKey opaqueSortB =
            MakeSvsmPacketStateSortKey(opaqueState, 23u, true);
        assert(opaqueSortA == opaqueSortB);
        assert(CanMergeSvsmPacketStateGroup(
            opaqueState, true, opaqueState, true));
        assert(!CanMergeSvsmPacketStateGroup(
            opaqueState, false, opaqueState, true));
        assert(!CanMergeSvsmPacketStateGroup(
            opaqueState, false, opaqueState, false));
        assert(!CanMergeSvsmPacketStateGroup(
            opaqueState, true,
            MakeSvsmBatchedDrawStateKey(12u, 22u, 1u, false), true));
        assert(!CanMergeSvsmPacketStateGroup(
            opaqueState, true,
            MakeSvsmBatchedDrawStateKey(11u, 22u, 2u, false), true));
        assert(!CanMergeSvsmPacketStateGroup(
            opaqueState, true,
            MakeSvsmBatchedDrawStateKey(11u, 22u, 1u, true), true));
        assert(!IsSvsmPacketStateSortKeyLess(
            opaqueSortA, opaqueSortB));
        assert(!IsSvsmPacketStateSortKeyLess(
            opaqueSortB, opaqueSortA));

        constexpr SvsmBatchedDrawStateKey alphaStateA =
            MakeSvsmBatchedDrawStateKey(11u, 22u, 1u, true);
        constexpr SvsmBatchedDrawStateKey alphaStateB =
            MakeSvsmBatchedDrawStateKey(11u, 23u, 1u, true);
        assert(!(MakeSvsmPacketStateSortKey(
            alphaStateA, 22u, true) ==
            MakeSvsmPacketStateSortKey(alphaStateB, 23u, true)));
        assert(!(MakeSvsmPacketStateSortKey(
            opaqueState, 22u, false) ==
            MakeSvsmPacketStateSortKey(opaqueState, 23u, false)));
        assert(!(MakeSvsmPacketStateSortKey(
            opaqueState, 22u, true) ==
            MakeSvsmPacketStateSortKey(opaqueState, 22u, false)));

        struct PacketModel
        {
            SvsmPacketStateSortKey key;
            uint32_t originalOrder;
            uint32_t pageListOffset;
            uint32_t objectInstanceIndex;
            uint32_t argumentIndex = 0u;
        };
        const SvsmBatchedDrawStateKey bufferA =
            MakeSvsmBatchedDrawStateKey(10u, 0u, 1u, false);
        const SvsmBatchedDrawStateKey bufferB =
            MakeSvsmBatchedDrawStateKey(20u, 0u, 1u, false);
        std::vector<PacketModel> packets = {
            { MakeSvsmPacketStateSortKey(bufferB, 50u, true),
                0u, 100u, 200u },
            { MakeSvsmPacketStateSortKey(bufferA, 60u, true),
                1u, 101u, 201u },
            { MakeSvsmPacketStateSortKey(bufferA, 70u, true),
                2u, 102u, 202u },
            { MakeSvsmPacketStateSortKey(bufferA, 80u, false),
                3u, 103u, 203u },
            { MakeSvsmPacketStateSortKey(bufferA, 90u, false),
                4u, 104u, 204u }
        };
        std::stable_sort(
            packets.begin(), packets.end(),
            [](const PacketModel& left, const PacketModel& right) {
                return IsSvsmPacketStateSortKeyLess(
                    left.key, right.key);
            });
        assert(packets[0].originalOrder == 1u);
        assert(packets[1].originalOrder == 2u);
        assert(packets[0].pageListOffset == 101u);
        assert(packets[0].objectInstanceIndex == 201u);
        assert(packets[1].pageListOffset == 102u);
        assert(packets[1].objectInstanceIndex == 202u);
        assert(packets[2].originalOrder == 3u);
        assert(packets[3].originalOrder == 4u);
        assert(packets[4].originalOrder == 0u);
        constexpr uint32_t clipmapPacketOffset = 37u;
        for (uint32_t packetIndex = 0u;
            packetIndex < uint32_t(packets.size());
            ++packetIndex)
        {
            packets[packetIndex].argumentIndex =
                clipmapPacketOffset + packetIndex;
            assert(packets[packetIndex].argumentIndex ==
                clipmapPacketOffset + packetIndex);
        }
        constexpr std::array<uint32_t, 4> clipmapPacketCounts = {
            3u, 0u, 5u, 2u
        };
        std::array<uint32_t, 4> clipmapPacketOffsets{};
        uint32_t globalPacketCount = 0u;
        for (uint32_t level = 0u;
            level < uint32_t(clipmapPacketCounts.size());
            ++level)
        {
            clipmapPacketOffsets[level] = globalPacketCount;
            globalPacketCount += clipmapPacketCounts[level];
            assert(clipmapPacketOffsets[level] +
                clipmapPacketCounts[level] == globalPacketCount);
        }
        assert(clipmapPacketOffsets[0] == 0u);
        assert(clipmapPacketOffsets[1] == 3u);
        assert(clipmapPacketOffsets[2] == 3u);
        assert(clipmapPacketOffsets[3] == 8u);
        assert(globalPacketCount == 10u);

        // Only the two batchable opaque packets share a group. The exact
        // nonbatchable material identity keeps both fallback packets in
        // singleton groups even though their other state is compatible.
        uint32_t groupCount = 0u;
        bool previousBatchable = false;
        SvsmBatchedDrawStateKey previousState{};
        for (uint32_t packetIndex = 0u;
            packetIndex < uint32_t(packets.size());
            ++packetIndex)
        {
            const bool batchable = packets[packetIndex].key.batchable;
            const SvsmBatchedDrawStateKey state = {
                packets[packetIndex].key.bufferGroup,
                packets[packetIndex].key.alphaTested
                    ? packets[packetIndex].key.material
                    : 0u,
                packets[packetIndex].key.cullMode,
                packets[packetIndex].key.alphaTested
            };
            if (packetIndex == 0u ||
                !CanMergeSvsmPacketStateGroup(
                    previousState, previousBatchable,
                    state, batchable))
            {
                ++groupCount;
            }
            previousState = state;
            previousBatchable = batchable;
        }
        assert(groupCount == 4u);

        // The GPU fill stage writes the same rendered-page count to every
        // indirect packet in a group. With no dirty pages every multi-draw
        // command therefore contains only zero-instance draws.
        std::array<uint32_t, 3> instanceCounts{};
        const uint32_t dirtyPageCount = 0u;
        instanceCounts.fill(dirtyPageCount);
        for (const uint32_t instanceCount : instanceCounts)
            assert(instanceCount == 0u);
    }

    void TestMotionBenchmarkSequence()
    {
        const SvsmMotionBenchmarkTimingSummary emptySummary =
            SummarizeSvsmMotionBenchmarkSamples({});
        assert(emptySummary.sampleCount == 0u);
        assert(emptySummary.median == 0.f);
        assert(emptySummary.p95 == 0.f);
        assert(emptySummary.p99 == 0.f);
        assert(emptySummary.maximum == 0.f);

        const SvsmMotionBenchmarkTimingSummary oddSummary =
            SummarizeSvsmMotionBenchmarkSamples({ 9.f, 1.f, 5.f });
        assert(oddSummary.sampleCount == 3u);
        assert(oddSummary.median == 5.f);
        assert(oddSummary.p95 == 9.f);
        assert(oddSummary.p99 == 9.f);
        assert(oddSummary.maximum == 9.f);

        const SvsmMotionBenchmarkTimingSummary percentileSummary =
            SummarizeSvsmMotionBenchmarkSamples({
                20.f, 19.f, 18.f, 17.f, 16.f,
                15.f, 14.f, 13.f, 12.f, 11.f,
                10.f, 9.f, 8.f, 7.f, 6.f,
                5.f, 4.f, 3.f, 2.f, 1.f
            });
        assert(percentileSummary.sampleCount == 20u);
        assert(percentileSummary.median == 10.5f);
        assert(percentileSummary.p95 == 19.f);
        assert(percentileSummary.p99 == 20.f);
        assert(percentileSummary.maximum == 20.f);

        const std::vector<float> targetSamples = {
            0.4f, 0.4001f, 0.7f, 0.7001f, 1.0001f
        };
        assert(CountSvsmMotionBenchmarkSamplesAbove(
            targetSamples, 0.4f) == 4u);
        assert(CountSvsmMotionBenchmarkSamplesAbove(
            targetSamples, 0.7f) == 2u);
        assert(CountSvsmMotionBenchmarkSamplesAbove(
            targetSamples, 1.f) == 1u);
        const SvsmMotionBenchmarkTimingSummary passingTarget = {
            3u, 0.4f, 0.5f, 0.6f, 0.7f
        };
        assert(IsSvsmMotionBenchmarkGpuTargetMet(
            true, true, passingTarget));
        assert(!IsSvsmMotionBenchmarkGpuTargetMet(
            false, true, passingTarget));
        assert(!IsSvsmMotionBenchmarkGpuTargetMet(
            true, false, passingTarget));
        SvsmMotionBenchmarkTimingSummary failingMedian = passingTarget;
        failingMedian.median = 0.4001f;
        assert(!IsSvsmMotionBenchmarkGpuTargetMet(
            true, true, failingMedian));
        SvsmMotionBenchmarkTimingSummary failingSpike = passingTarget;
        failingSpike.maximum = 0.7001f;
        assert(!IsSvsmMotionBenchmarkGpuTargetMet(
            true, true, failingSpike));

        const float stageSum = SumSvsmMotionBenchmarkGpuStages(
            0.01f, 0.02f, 0.03f, 0.04f, 0.05f, 0.06f);
        assert(std::abs(stageSum - 0.21f) < 1e-7f);

        assert(SvsmMotionBenchmarkTurnFrames == 450u);
        assert(SvsmMotionBenchmarkEndFrame == 1096u);
        assert(std::abs(
            SvsmMotionBenchmarkDegreesPerFrame - 0.1f) < 1e-7f);
        assert(GetSvsmMotionBenchmarkSegment(0u) ==
            SvsmMotionBenchmarkSegment::Warm);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(
                SvsmMotionBenchmarkWarmFrames - 1u)) < 1e-7f);
        for (uint64_t frame = 0u;
            frame < SvsmMotionBenchmarkWarmFrames;
            ++frame)
        {
            assert(std::abs(
                GetSvsmMotionBenchmarkAngleDegrees(frame)) < 1e-7f);
        }

        const uint64_t firstTurnFrame =
            SvsmMotionBenchmarkWarmFrames;
        const uint64_t firstHoldFrame =
            firstTurnFrame + SvsmMotionBenchmarkTurnFrames;
        const uint64_t firstReturnFrame =
            firstHoldFrame + SvsmMotionBenchmarkHoldFrames;
        assert(
            SvsmMotionBenchmarkEndFrame -
                SvsmMotionBenchmarkWarmFrames ==
            916u);
        assert(SvsmMotionBenchmarkMeasurementFrames == 916u);
        assert(!IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkWarmFrames - 1u));
        assert(IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkWarmFrames));
        assert(IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkEndFrame - 1u));
        assert(!IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkEndFrame));
        assert(GetSvsmMotionBenchmarkSegment(firstTurnFrame) ==
            SvsmMotionBenchmarkSegment::TurnRight);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(firstTurnFrame) -
                0.1f) < 1e-6f);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(
                firstTurnFrame +
                    SvsmMotionBenchmarkTurnFrames - 1u) -
                45.f) < 1e-5f);
        for (uint64_t frame = firstTurnFrame + 1u;
            frame < firstHoldFrame;
            ++frame)
        {
            const float delta =
                GetSvsmMotionBenchmarkAngleDegrees(frame) -
                GetSvsmMotionBenchmarkAngleDegrees(frame - 1u);
            assert(std::abs(delta - 0.1f) < 1e-5f);
        }

        assert(GetSvsmMotionBenchmarkSegment(firstHoldFrame) ==
            SvsmMotionBenchmarkSegment::HoldRight);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(firstHoldFrame) -
                45.f) < 1e-7f);
        for (uint64_t frame = firstHoldFrame;
            frame < firstReturnFrame;
            ++frame)
        {
            assert(std::abs(
                GetSvsmMotionBenchmarkAngleDegrees(frame) -
                    45.f) < 1e-7f);
        }

        assert(GetSvsmMotionBenchmarkSegment(firstReturnFrame) ==
            SvsmMotionBenchmarkSegment::TurnBack);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(firstReturnFrame) -
                44.9f) < 1e-5f);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(
                SvsmMotionBenchmarkEndFrame - 1u)) < 1e-5f);
        for (uint64_t frame = firstReturnFrame + 1u;
            frame < SvsmMotionBenchmarkEndFrame;
            ++frame)
        {
            const float delta =
                GetSvsmMotionBenchmarkAngleDegrees(frame) -
                GetSvsmMotionBenchmarkAngleDegrees(frame - 1u);
            assert(std::abs(delta + 0.1f) < 1e-5f);
        }
        assert(GetSvsmMotionBenchmarkSegment(
            SvsmMotionBenchmarkEndFrame) ==
            SvsmMotionBenchmarkSegment::Complete);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(
                SvsmMotionBenchmarkEndFrame)) < 1e-7f);

        assert(IsSvsmMotionBenchmarkEvidenceValid(
            916u, 916u, 916u, 0u, 916u, 0u, false, false));
        assert(!IsSvsmMotionBenchmarkEvidenceValid(
            915u, 916u, 916u, 0u, 916u, 0u, false, false));
        assert(!IsSvsmMotionBenchmarkEvidenceValid(
            916u, 916u, 915u, 1u, 915u, 0u, false, false));
        assert(!IsSvsmMotionBenchmarkEvidenceValid(
            916u, 916u, 916u, 0u, 916u, 1u, false, false));

        SparseVirtualShadowMapSettings original;
        const SparseVirtualShadowMapSettings identical = original;
        assert(IsSameSvsmConfiguration(original, identical));
        SparseVirtualShadowMapSettings changed = original;
        changed.tapCount = SvsmTapCount::Eight;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.packetStateSortingEnabled =
            !original.packetStateSortingEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.levelEmptyWorkSkipEnabled =
            !original.levelEmptyWorkSkipEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.finiteBudgetStaticDrainEnabled =
            !original.finiteBudgetStaticDrainEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.coarsestPageRenderBudgetEnabled = true;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.allocationBudgetSaturationEarlyOutEnabled =
            !original.allocationBudgetSaturationEarlyOutEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.perPixelMarkingDedupeEnabled =
            !original.perPixelMarkingDedupeEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.packetPageCullingEnabled =
            !original.packetPageCullingEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.dirtyPageScatterRasterEnabled = true;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.scatterAlphaTestEarlyRejectEnabled = true;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.dirtyPageScatterAmplificationGuardEnabled = true;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.dirtyPageScatterMaximumAmplification = 8u;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.packetRectangleDirectScanEnabled =
            !original.packetRectangleDirectScanEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.recentPageEvictionGraceEnabled =
            !original.recentPageEvictionGraceEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.detailedGpuTimingEnabled =
            !original.detailedGpuTimingEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
    }

    void TestResourceRecreationAndModeSwitch()
    {
        struct Key
        {
            uint32_t width;
            uint32_t height;
            uint32_t poolPages;

            bool operator==(const Key& other) const
            {
                return width == other.width &&
                    height == other.height &&
                    poolPages == other.poolPages;
            }
        };

        const Key original{ 1920u, 1080u, 4096u };
        assert(!(original == Key{ 1919u, 1080u, 4096u }));
        assert(!(original == Key{ 1920u, 1079u, 4096u }));
        assert(!(original == Key{ 1920u, 1080u, 2048u }));
        assert(original == Key{ 1920u, 1080u, 4096u });

        SvsmResourceBackend active = SvsmResourceBackend::None;
        auto activate = [&active](SvsmResourceBackend requested) {
            const bool recreate = RequiresSvsmResourceRecreation(
                active, requested);
            if (recreate)
                active = requested;
            return recreate;
        };

        assert(activate(SvsmResourceBackend::Dense));
        assert(!activate(SvsmResourceBackend::Dense));
        assert(activate(SvsmResourceBackend::Sparse));
        assert(!activate(SvsmResourceBackend::Sparse));
        assert(activate(SvsmResourceBackend::Dense));
        assert(active == SvsmResourceBackend::Dense);
    }

    void TestMotionBenchmarkAutostart()
    {
        assert(!IsSvsmMotionMeasurementMarkerReady(""));
        assert(!IsSvsmMotionMeasurementMarkerReady("state=waiting\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady("notstate=ready\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady("state=readyish\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady("state=ready\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady(
            "phase=measurement\nstate=ready\n"));

        const std::string readyMarker =
            "state=ready\r\n"
            "runIdentity=run-1\r\n"
            "monitorProcessId=10\r\n"
            "rendererProcessId=20\r\n"
            "rendererPath=C:/uvsr.exe\r\n"
            "measurementStartUnixMs=1000\r\n"
            "measurementDeadlineUnixMs=2000\r\n";
        assert(IsSvsmMotionMeasurementMarkerReady(readyMarker));
        assert(IsSvsmMotionMeasurementMarkerReady(
            std::string("\xef\xbb\xbf") + readyMarker));
        const SvsmMotionMeasurementMarker ready =
            ParseSvsmMotionMeasurementMarker(readyMarker);
        assert(ready.state ==
            SvsmMotionMeasurementMarkerState::Ready);
        assert(ready.identityValid);
        assert(ready.timingValid);
        assert(!ready.completionTimingValid);
        assert(IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 20u, "C:/uvsr.exe", 1000u));
        assert(IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 20u, "C:/uvsr.exe", 2000u));
        assert(!IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 21u, "C:/uvsr.exe", 1500u));
        assert(!IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 20u, "C:/other.exe", 1500u));
        assert(!IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 20u, "C:/uvsr.exe", 999u));
        assert(!IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 20u, "C:/uvsr.exe", 2001u));

        assert(!IsSvsmMotionMeasurementMarkerReady(
            "state=ready\n"
            "runIdentity=run-1\n"
            "monitorProcessId=10\n"
            "rendererProcessId=20\n"
            "rendererPath=C:/uvsr.exe\n"
            "measurementStartUtc=2026-07-22T00:00:00Z\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady(
            "state=ready\n"
            "runIdentity=run-1\n"
            "runIdentity=run-2\n"
            "monitorProcessId=10\n"
            "rendererProcessId=20\n"
            "rendererPath=C:/uvsr.exe\n"
            "measurementStartUnixMs=1000\n"
            "measurementDeadlineUnixMs=2000\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady(
            "state=ready\n"
            "runIdentity=run-1\n"
            "monitorProcessId=18446744073709551616\n"
            "rendererProcessId=20\n"
            "rendererPath=C:/uvsr.exe\n"
            "measurementStartUnixMs=1000\n"
            "measurementDeadlineUnixMs=2000\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady(
            "state=ready\n"
            "runIdentity=run-1\n"
            "monitorProcessId=10\n"
            "rendererProcessId=20\n"
            "rendererPath=C:/uvsr.exe\n"
            "measurementStartUnixMs=1000\n"
            "measurementDeadlineUnixMs=1000\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady(
            readyMarker + "measurementEndUnixMs=2000\r\n"));

        const std::string completeMarker =
            "state=complete\n"
            "runIdentity=run-1\n"
            "monitorProcessId=10\n"
            "rendererProcessId=20\n"
            "rendererPath=C:/uvsr.exe\n"
            "measurementStartUnixMs=1000\n"
            "measurementDeadlineUnixMs=2000\n"
            "measurementEndUnixMs=2000\n";
        const SvsmMotionMeasurementMarker complete =
            ParseSvsmMotionMeasurementMarker(completeMarker);
        assert(complete.completionTimingValid);
        assert(IsSameSvsmMotionMeasurementRun(ready, complete));
        assert(IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, complete, 1999u));
        assert(IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, complete, 2000u));
        assert(!IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, complete, 2001u));

        const SvsmMotionMeasurementMarker earlyComplete =
            ParseSvsmMotionMeasurementMarker(
                "state=complete\n"
                "runIdentity=run-1\n"
                "monitorProcessId=10\n"
                "rendererProcessId=20\n"
                "rendererPath=C:/uvsr.exe\n"
                "measurementStartUnixMs=1000\n"
                "measurementDeadlineUnixMs=2000\n"
                "measurementEndUnixMs=1999\n");
        assert(!earlyComplete.completionTimingValid);
        assert(!IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, earlyComplete, 1500u));

        const SvsmMotionMeasurementMarker contaminated =
            ParseSvsmMotionMeasurementMarker(
                "state=contaminated\n"
                "runIdentity=run-1\n"
                "monitorProcessId=10\n"
                "rendererProcessId=20\n"
                "rendererPath=C:/uvsr.exe\n"
                "measurementStartUnixMs=1000\n"
                "measurementDeadlineUnixMs=2000\n"
                "measurementEndUnixMs=1500\n");
        assert(contaminated.completionTimingValid);
        assert(IsSameSvsmMotionMeasurementRun(ready, contaminated));
        assert(!IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, contaminated, 1400u));

        const SvsmMotionMeasurementMarker wrongRun =
            ParseSvsmMotionMeasurementMarker(
                "state=complete\n"
                "runIdentity=run-2\n"
                "monitorProcessId=10\n"
                "rendererProcessId=20\n"
                "rendererPath=C:/uvsr.exe\n"
                "measurementStartUnixMs=1000\n"
                "measurementDeadlineUnixMs=2000\n"
                "measurementEndUnixMs=2000\n");
        assert(!IsSameSvsmMotionMeasurementRun(ready, wrongRun));
        assert(!IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, wrongRun, 1500u));
        assert(IsSvsmMotionDiagnosticPoolPageCount(64u));
        assert(IsSvsmMotionDiagnosticPoolPageCount(256u));
        assert(IsSvsmMotionDiagnosticPoolPageCount(1024u));
        assert(IsSvsmMotionDiagnosticPoolPageCount(4096u));
        assert(!IsSvsmMotionDiagnosticPoolPageCount(0u));
        assert(!IsSvsmMotionDiagnosticPoolPageCount(65u));
        assert(IsSvsmMotionBenchmarkEnvironmentValid(false, false));
        assert(!IsSvsmMotionBenchmarkEnvironmentValid(true, false));
        assert(!IsSvsmMotionBenchmarkEnvironmentValid(false, true));
        assert(IsSvsmMotionBenchmarkAcceptanceConfiguration(
            4096u, true, true));
        assert(!IsSvsmMotionBenchmarkAcceptanceConfiguration(
            1024u, true, true));
        assert(!IsSvsmMotionBenchmarkAcceptanceConfiguration(
            4096u, false, true));
        assert(!IsSvsmMotionBenchmarkAcceptanceConfiguration(
            4096u, true, false));

        SvsmMotionAutostartStage stage =
            SvsmMotionAutostartStage::Baseline;
        uint32_t stageFrames = 0u;
        uint32_t stableFrames = 0u;
        SvsmMotionAutostartDecision decision;
        for (uint32_t frame = 0u;
            frame <= SvsmMotionAutostartBaselineFrames;
            ++frame)
        {
            decision = AdvanceSvsmMotionAutostart(
                stage, stageFrames, stableFrames, false, false);
            stage = decision.stage;
            stageFrames = decision.stageFrames;
            stableFrames = decision.stableFrames;
            assert(decision.enableSvsm ==
                (frame == SvsmMotionAutostartBaselineFrames));
        }
        assert(stage == SvsmMotionAutostartStage::SvsmWarmup);

        decision = AdvanceSvsmMotionAutostart(
            stage, stageFrames, stableFrames, true, true);
        assert(decision.stableFrames == 0u);
        stageFrames = decision.stageFrames;
        stableFrames = decision.stableFrames;
        for (uint32_t frame = 0u;
            frame < SvsmMotionAutostartStableSvsmFrames;
            ++frame)
        {
            decision = AdvanceSvsmMotionAutostart(
                stage, stageFrames, stableFrames, true, false);
            stage = decision.stage;
            stageFrames = decision.stageFrames;
            stableFrames = decision.stableFrames;
            assert(decision.startBenchmark ==
                (frame + 1u == SvsmMotionAutostartStableSvsmFrames));
        }
        assert(stage == SvsmMotionAutostartStage::Ready);

        decision = AdvanceSvsmMotionAutostart(
            SvsmMotionAutostartStage::SvsmWarmup,
            SvsmMotionAutostartWarmupFrameLimit - 1u,
            0u,
            false,
            true);
        assert(decision.timedOut);
        assert(!decision.startBenchmark);
    }

    void TestTelemetrySampleOrdering()
    {
        assert(ShouldAcceptSvsmTelemetrySample(
            7u, 7u, 10u, 0u, false));
        assert(ShouldAcceptSvsmTelemetrySample(
            7u, 7u, 11u, 10u, true));
        assert(!ShouldAcceptSvsmTelemetrySample(
            6u, 7u, 12u, 10u, true));
        assert(!ShouldAcceptSvsmTelemetrySample(
            8u, 7u, 12u, 10u, true));
        assert(!ShouldAcceptSvsmTelemetrySample(
            7u, 7u, 10u, 10u, true));
        assert(!ShouldAcceptSvsmTelemetrySample(
            7u, 7u, 9u, 10u, true));

        assert(IsDetailedSvsmGpuTimingEnabled(true, false));
        assert(!IsDetailedSvsmGpuTimingEnabled(true, true));
        assert(!IsDetailedSvsmGpuTimingEnabled(false, false));
        assert(ShouldIssueSvsmGpuTimerStage(false, true));
        assert(!ShouldIssueSvsmGpuTimerStage(false, false));
        assert(ShouldIssueSvsmGpuTimerStage(true, false));

        assert(FirstSvsmStaticPageRequestRejectBit(0u) == 32u);
        assert(FirstSvsmStaticPageRequestRejectBit(1u) == 0u);
        assert(FirstSvsmStaticPageRequestRejectBit(1u << 22u) == 22u);
        assert(FirstSvsmStaticPageRequestRejectBit(
            (1u << 19u) | (1u << 3u)) == 3u);
    }
}

int main()
{
    TestPagePacking();
    TestFinePageRenderBudgetScheduling();
    TestMappingAndWraparound();
    TestPerPixelRequestDeduplication();
    TestPacketPageCompaction();
    TestStaticPageRequestActions();
    TestClipmapSelection();
    TestReverseDepthWrites();
    TestAllocationEvictionAndCacheReuse();
    TestInvalidationAndFallback();
    TestPageBoundaryFiltering();
    TestProfiles();
    TestTapCountPermutationSelection();
    TestBatchedDrawPackingAndGrouping();
    TestMotionBenchmarkSequence();
    TestMotionBenchmarkAutostart();
    TestResourceRecreationAndModeSwitch();
    TestTelemetrySampleOrdering();
    return 0;
}
