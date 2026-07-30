#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace uvsr
{
    constexpr uint32_t SvsmClipmapCount = 6u;
    constexpr uint32_t SvsmVirtualResolution = 8192u;
    constexpr uint32_t SvsmPageSize = 128u;
    constexpr uint32_t SvsmPagesPerAxis =
        SvsmVirtualResolution / SvsmPageSize;
    constexpr uint32_t SvsmPagesPerClipmap =
        SvsmPagesPerAxis * SvsmPagesPerAxis;
    constexpr uint32_t SvsmInvalidPhysicalPage =
        std::numeric_limits<uint32_t>::max();
    // Bit 31 records static-depth dirtiness for the paired static/merged
    // physical pool. Thirteen age bits still provide an 8192-frame wrap
    // interval, far beyond the bounded recent-page eviction grace.
    constexpr uint32_t SvsmPageAgeMask = 0x1fffu;
    constexpr uint32_t SvsmPageStaticDirtyBit = 1u << 31u;
    constexpr uint32_t SvsmRecentPageEvictionGraceFrames = 8u;
    constexpr uint32_t SvsmCompactRenderPageOwnerMask = 0x7fffu;
    constexpr uint32_t SvsmCompactRenderPagePhysicalShift = 15u;
    constexpr uint32_t SvsmInvalidPacketPageBounds = 0xffffffffu;
    constexpr uint32_t SvsmEmptyPacketPageBounds = 0xfffffffeu;
    constexpr uint32_t SvsmPacketPageRuntimePerPageBit = 1u << 30u;
    constexpr uint32_t SvsmPacketPageRuntimeFailOpenBit = 1u << 31u;
    constexpr uint32_t SvsmPacketPageRuntimeCountMask =
        ~(SvsmPacketPageRuntimePerPageBit |
            SvsmPacketPageRuntimeFailOpenBit);
    constexpr uint32_t SvsmPacketPageRuntimeWords = 3u;
    constexpr uint32_t SvsmPacketPageRuntimeStateWord = 0u;
    constexpr uint32_t SvsmPacketPageRuntimeMinimumWord = 1u;
    constexpr uint32_t SvsmPacketPageRuntimeMaximumWord = 2u;
    constexpr uint32_t SvsmDirtyPageRectangleWordsPerLevel = 4u;
    constexpr uint32_t SvsmMaximumDispatchGroupsPerDimension = 65535u;
    constexpr uint32_t SvsmMaximumPacketPageDispatchGroups =
        SvsmMaximumDispatchGroupsPerDimension;
    constexpr uint32_t SvsmPacketFillThreadsPerGroup = 64u;
    constexpr uint32_t SvsmDebugCounterCount = 17u;
    constexpr uint32_t SvsmLevelRenderCounterBase =
        SvsmDebugCounterCount;
    constexpr uint32_t SvsmAllocatorControlCounterBase =
        SvsmLevelRenderCounterBase + SvsmClipmapCount;
    constexpr uint32_t SvsmAllocatorControlCounterCount = 8u;
    constexpr uint32_t SvsmLevelHasWorkCounterBase =
        SvsmAllocatorControlCounterBase +
        SvsmAllocatorControlCounterCount;
    constexpr uint32_t SvsmLevelHasWorkDispatchGate = 1u;
    constexpr uint32_t
        SvsmSparseFlagScatterAlphaTestEarlyReject = 4096u;
    constexpr uint32_t
        SvsmSparseFlagAllocationBudgetSaturationEarlyOut = 8192u;
    constexpr uint32_t
        SvsmSparseFlagDirtyPageScatterAmplificationGuard = 16384u;
    constexpr uint32_t
        SvsmSparseFlagCoarsestPageRenderBudget = 32768u;
    constexpr uint32_t
        SvsmSparseFlagBilinearPcf = 65536u;
    constexpr uint32_t
        SvsmSparseFlagPairedStaticDynamicDepth = 131072u;
    constexpr uint32_t
        SvsmSparseFlagPreservePhysicalMappings = 262144u;
    constexpr uint32_t
        SvsmSparseFlagStaticDepthHierarchyCulling = 524288u;
    constexpr uint32_t
        SvsmSparseFlagStaticDepthHierarchyResource = 1048576u;
    constexpr uint32_t
        SvsmSparseFlagStaticDepthHierarchyBootstrap = 2097152u;
    constexpr uint32_t
        SvsmSparseFlagReceiverPageMaskCulling = 4194304u;

    struct SvsmDeferredStaticDepthMergePageState
    {
        bool deferredMergeEnabled = false;
        bool pairedDepthActive = false;
        bool resident = false;
        bool dirty = false;
        bool staticDirty = false;
        bool ownerValid = false;
        bool ownerClipmapMatches = false;
        bool physicalPageInRange = false;
        bool pagePhysicalMatches = false;
        bool physicalOwnerMatches = false;
        bool renderPageScheduled = false;
    };

    [[nodiscard]] constexpr bool
    ShouldMergeSvsmDeferredStaticDepthPage(
        const SvsmDeferredStaticDepthMergePageState& state)
    {
        // renderPageScheduled is the authoritative output of allocation and
        // therefore includes finite-budget, coarsest-policy, and compact-list
        // eligibility. Requiring every identity check prevents an evicted or
        // recycled physical page from receiving another owner's static depth.
        return state.deferredMergeEnabled &&
            state.pairedDepthActive &&
            state.resident &&
            state.dirty &&
            state.staticDirty &&
            state.ownerValid &&
            state.ownerClipmapMatches &&
            state.physicalPageInRange &&
            state.pagePhysicalMatches &&
            state.physicalOwnerMatches &&
            state.renderPageScheduled;
    }

    enum class SvsmStaticDepthPostRasterWork : uint32_t
    {
        None,
        MergeOnly,
        HierarchyOnly,
        MergeAndHierarchy
    };

    [[nodiscard]] constexpr SvsmStaticDepthPostRasterWork
    GetSvsmStaticDepthPostRasterWork(
        bool deferredStaticDepthMergeActive,
        bool staticDepthHierarchyActive)
    {
        if (deferredStaticDepthMergeActive)
        {
            return staticDepthHierarchyActive
                ? SvsmStaticDepthPostRasterWork::MergeAndHierarchy
                : SvsmStaticDepthPostRasterWork::MergeOnly;
        }
        return staticDepthHierarchyActive
            ? SvsmStaticDepthPostRasterWork::HierarchyOnly
            : SvsmStaticDepthPostRasterWork::None;
    }

    [[nodiscard]] constexpr bool
    IsSvsmDeferredStaticDepthMergeActive(
        bool deferredStaticDepthMergeEnabled,
        bool effectivePairedStaticDynamicDepthEnabled,
        bool sparseBackend,
        bool mergePipelineAvailable)
    {
        return deferredStaticDepthMergeEnabled &&
            effectivePairedStaticDynamicDepthEnabled &&
            sparseBackend &&
            mergePipelineAvailable;
    }

    [[nodiscard]] constexpr bool
    ShouldInvalidateSvsmStaticDepthHierarchyTag(
        bool hierarchyResourceAvailable,
        bool pairedDepthActive,
        bool staticDirty)
    {
        // Resource presence, not query enablement, owns invalidation. This
        // prevents disable -> static content change -> re-enable from making
        // an old same-owner tag authoritative.
        return hierarchyResourceAvailable &&
            pairedDepthActive &&
            staticDirty;
    }

    [[nodiscard]] constexpr bool
    ShouldBootstrapSvsmStaticDepthHierarchy(
        bool bootstrapRequired,
        bool hierarchyCullingActive)
    {
        return bootstrapRequired && hierarchyCullingActive;
    }

    [[nodiscard]] constexpr bool
    GetNextSvsmStaticDepthHierarchyBootstrapRequired(
        bool bootstrapRequired,
        bool hierarchyCullingActive,
        bool bootstrapCompleted)
    {
        // Any interval in which the effective path is disabled may contain
        // static changes that invalidate tags without rebuilding them.
        return !hierarchyCullingActive ||
            (bootstrapRequired && !bootstrapCompleted);
    }
    constexpr uint32_t SvsmPacketStaticCasterBit = 1u << 31u;
    constexpr uint32_t SvsmPacketObjectInstanceMask =
        ~SvsmPacketStaticCasterBit;
    constexpr uint32_t SvsmLocalInvalidationStaticBit = 1u << 31u;
    constexpr uint32_t SvsmLocalInvalidationOwnerMask = 0x7fffu;
    constexpr uint32_t SvsmDynamicToStaticPromotionFrames = 100u;
    constexpr uint32_t SvsmDefaultMovingLightLodRecoveryFrames = 10u;
    constexpr uint32_t SvsmMaximumMovingLightLodRecoveryFrames = 1024u;
    constexpr uint32_t SvsmMaximumReceiverDistanceMipClampLevel =
        SvsmClipmapCount - 2u;
    constexpr uint32_t SvsmMaximumDirtyPageScatterAmplification = 64u;
    constexpr uint32_t SvsmLevelHasWorkCounterCount =
        SvsmClipmapCount;
    constexpr uint32_t SvsmScheduledTileMaskCounterBase =
        SvsmLevelHasWorkCounterBase +
        SvsmLevelHasWorkCounterCount;
    constexpr uint32_t SvsmScheduledTileMaskQueryCounter =
        SvsmScheduledTileMaskCounterBase;
    constexpr uint32_t SvsmScheduledTileMaskEarlyRejectCounter =
        SvsmScheduledTileMaskCounterBase + 1u;
    constexpr uint32_t SvsmScheduledTileMaskFailOpenCounter =
        SvsmScheduledTileMaskCounterBase + 2u;
    constexpr uint32_t
        SvsmScheduledTileMaskPositiveExactZeroCounter =
            SvsmScheduledTileMaskCounterBase + 3u;
    constexpr uint32_t SvsmScheduledTileMaskCounterCount = 4u;
    constexpr uint32_t SvsmCounterCountBeforeStaticDepthHierarchy =
        SvsmScheduledTileMaskCounterBase +
        SvsmScheduledTileMaskCounterCount;
    constexpr uint32_t SvsmScheduledTilePageWidth = 8u;
    constexpr uint32_t SvsmScheduledTilesPerAxis =
        SvsmPagesPerAxis / SvsmScheduledTilePageWidth;
    constexpr uint32_t SvsmScheduledTileCount =
        SvsmScheduledTilesPerAxis * SvsmScheduledTilesPerAxis;
    constexpr uint32_t SvsmScheduledTileMaskWordsPerLevel = 5u;
    constexpr uint32_t SvsmStaticDepthHierarchyBaseCellWidth = 16u;
    constexpr uint32_t SvsmStaticDepthHierarchyBaseAxis =
        SvsmPageSize / SvsmStaticDepthHierarchyBaseCellWidth;
    constexpr uint32_t SvsmStaticDepthHierarchyBaseOffset = 0u;
    constexpr uint32_t SvsmStaticDepthHierarchyBaseCount =
        SvsmStaticDepthHierarchyBaseAxis *
        SvsmStaticDepthHierarchyBaseAxis;
    constexpr uint32_t SvsmStaticDepthHierarchyLevelOneOffset =
        SvsmStaticDepthHierarchyBaseOffset +
        SvsmStaticDepthHierarchyBaseCount;
    constexpr uint32_t SvsmStaticDepthHierarchyLevelOneAxis =
        SvsmStaticDepthHierarchyBaseAxis / 2u;
    constexpr uint32_t SvsmStaticDepthHierarchyLevelOneCount =
        SvsmStaticDepthHierarchyLevelOneAxis *
        SvsmStaticDepthHierarchyLevelOneAxis;
    constexpr uint32_t SvsmStaticDepthHierarchyLevelTwoOffset =
        SvsmStaticDepthHierarchyLevelOneOffset +
        SvsmStaticDepthHierarchyLevelOneCount;
    constexpr uint32_t SvsmStaticDepthHierarchyLevelTwoAxis =
        SvsmStaticDepthHierarchyLevelOneAxis / 2u;
    constexpr uint32_t SvsmStaticDepthHierarchyLevelTwoCount =
        SvsmStaticDepthHierarchyLevelTwoAxis *
        SvsmStaticDepthHierarchyLevelTwoAxis;
    constexpr uint32_t SvsmStaticDepthHierarchyRootOffset =
        SvsmStaticDepthHierarchyLevelTwoOffset +
        SvsmStaticDepthHierarchyLevelTwoCount;
    constexpr uint32_t SvsmStaticDepthHierarchyTagOffset =
        SvsmStaticDepthHierarchyRootOffset + 1u;
    constexpr uint32_t SvsmStaticDepthHierarchyWordsPerPage =
        SvsmStaticDepthHierarchyTagOffset + 1u;
    constexpr uint32_t SvsmStaticDepthHierarchyTagOwnerMask =
        SvsmCompactRenderPageOwnerMask;
    constexpr uint32_t SvsmStaticDepthHierarchyTagEpochShift = 15u;
    constexpr uint32_t SvsmStaticDepthHierarchyTagEpochMask =
        (1u << (32u - SvsmStaticDepthHierarchyTagEpochShift)) - 1u;
    constexpr uint32_t SvsmStaticDepthHierarchyQueryCounter =
        SvsmCounterCountBeforeStaticDepthHierarchy;
    constexpr uint32_t SvsmStaticDepthHierarchyCullCounter =
        SvsmStaticDepthHierarchyQueryCounter + 1u;
    constexpr uint32_t SvsmStaticDepthHierarchyFailOpenCounter =
        SvsmStaticDepthHierarchyCullCounter + 1u;
    constexpr uint32_t SvsmStaticDepthHierarchyBuiltPageCounter =
        SvsmStaticDepthHierarchyFailOpenCounter + 1u;
    constexpr uint32_t SvsmStaticDepthHierarchyCounterCount = 4u;
    constexpr uint32_t SvsmReceiverPageMaskCounterBase =
        SvsmCounterCountBeforeStaticDepthHierarchy +
        SvsmStaticDepthHierarchyCounterCount;
    constexpr uint32_t SvsmReceiverPageMaskQueryCounter =
        SvsmReceiverPageMaskCounterBase;
    constexpr uint32_t SvsmReceiverPageMaskCullCounter =
        SvsmReceiverPageMaskCounterBase + 1u;
    constexpr uint32_t SvsmReceiverPageMaskFailOpenCounter =
        SvsmReceiverPageMaskCounterBase + 2u;
    constexpr uint32_t SvsmReceiverPageMaskCounterCount = 3u;
    constexpr uint32_t SvsmCounterCount =
        SvsmReceiverPageMaskCounterBase +
        SvsmReceiverPageMaskCounterCount;
    constexpr uint32_t SvsmReceiverPageMaskCellWidth = 16u;
    constexpr uint32_t SvsmReceiverPageMaskAxis =
        SvsmPageSize / SvsmReceiverPageMaskCellWidth;
    constexpr uint32_t SvsmReceiverPageMaskQuadrantAxis = 2u;
    constexpr uint32_t SvsmReceiverPageMaskQuadrantCellAxis =
        SvsmReceiverPageMaskAxis / SvsmReceiverPageMaskQuadrantAxis;
    constexpr uint32_t SvsmReceiverPageMaskTagOffset = 0u;
    constexpr uint32_t SvsmReceiverPageMaskQuadrantOffset = 1u;
    constexpr uint32_t SvsmReceiverPageMaskQuadrantCount =
        SvsmReceiverPageMaskQuadrantAxis *
        SvsmReceiverPageMaskQuadrantAxis;
    constexpr uint32_t SvsmReceiverPageMaskWordsPerPage =
        SvsmReceiverPageMaskQuadrantOffset +
        SvsmReceiverPageMaskQuadrantCount;

    static_assert(SvsmVirtualResolution % SvsmPageSize == 0u);
    static_assert(SvsmPagesPerAxis == 64u);
    static_assert(
        SvsmRecentPageEvictionGraceFrames > 0u &&
        SvsmRecentPageEvictionGraceFrames <= SvsmPageAgeMask);
    static_assert(
        SvsmClipmapCount * SvsmPagesPerClipmap - 1u <=
        SvsmCompactRenderPageOwnerMask);
    static_assert(
        SvsmPacketPageRuntimeMaximumWord + 1u ==
        SvsmPacketPageRuntimeWords);
    static_assert(
        SvsmPagesPerAxis % SvsmScheduledTilePageWidth == 0u &&
        SvsmScheduledTilesPerAxis == 8u &&
        SvsmScheduledTileCount == 64u);
    static_assert(
        SvsmPageSize % SvsmStaticDepthHierarchyBaseCellWidth == 0u &&
        SvsmStaticDepthHierarchyBaseAxis == 8u &&
        SvsmStaticDepthHierarchyBaseCount == 64u &&
        SvsmStaticDepthHierarchyLevelOneOffset == 64u &&
        SvsmStaticDepthHierarchyLevelOneCount == 16u &&
        SvsmStaticDepthHierarchyLevelTwoOffset == 80u &&
        SvsmStaticDepthHierarchyLevelTwoCount == 4u &&
        SvsmStaticDepthHierarchyRootOffset == 84u &&
        SvsmStaticDepthHierarchyTagOffset == 85u &&
        SvsmStaticDepthHierarchyWordsPerPage == 86u);
    static_assert(
        SvsmPageSize % SvsmReceiverPageMaskCellWidth == 0u &&
        SvsmReceiverPageMaskAxis == 8u &&
        SvsmReceiverPageMaskQuadrantCellAxis == 4u &&
        SvsmReceiverPageMaskQuadrantCount == 4u &&
        SvsmReceiverPageMaskWordsPerPage == 5u);

    [[nodiscard]] constexpr uint32_t
    GetSvsmLevelHasWorkCounterIndex(uint32_t level)
    {
        return SvsmLevelHasWorkCounterBase + level;
    }

    [[nodiscard]] constexpr uint32_t
    GetSvsmScheduledTileMaskWordBase(uint32_t level)
    {
        return level * SvsmScheduledTileMaskWordsPerLevel;
    }

    [[nodiscard]] constexpr bool ShouldScheduleSvsmDirtyPageRender(
        uint32_t clipmapLevel,
        uint32_t pageRenderReservation,
        uint32_t pageRenderBudget,
        bool coarsestPageRenderBudgetEnabled = false)
    {
        return clipmapLevel < SvsmClipmapCount &&
            (clipmapLevel == SvsmClipmapCount - 1u &&
                    !coarsestPageRenderBudgetEnabled ||
                pageRenderReservation < pageRenderBudget);
    }

    [[nodiscard]] constexpr bool
    ShouldEnableSvsmAllocationBudgetSaturationEarlyOut(
        bool enabled,
        uint32_t finePageRenderBudget)
    {
        return enabled &&
            finePageRenderBudget != std::numeric_limits<uint32_t>::max();
    }

    [[nodiscard]] constexpr bool
    ShouldSkipSvsmFineRenderReservationAtomic(
        uint32_t clipmapLevel,
        uint32_t relaxedFineRenderReservation,
        uint32_t finePageRenderBudget,
        bool saturationEarlyOutActive,
        bool coarsestPageRenderBudgetEnabled = false)
    {
        return saturationEarlyOutActive &&
            clipmapLevel < SvsmClipmapCount &&
            (clipmapLevel < SvsmClipmapCount - 1u ||
                coarsestPageRenderBudgetEnabled) &&
            relaxedFineRenderReservation >= finePageRenderBudget;
    }

    [[nodiscard]] constexpr uint32_t
    EncodeSvsmLevelHasWorkIndirectCount(
        uint32_t dirtyPageCount,
        uint32_t drawPacketCount)
    {
        return dirtyPageCount == 0u
            ? 0u
            : drawPacketCount;
    }

    constexpr uint32_t PackSvsmCompactRenderPage(
        uint32_t virtualOwner,
        uint32_t physicalPage)
    {
        return (virtualOwner & SvsmCompactRenderPageOwnerMask) |
            (physicalPage << SvsmCompactRenderPagePhysicalShift);
    }

    constexpr uint32_t UnpackSvsmCompactRenderPageOwner(
        uint32_t packed)
    {
        return packed & SvsmCompactRenderPageOwnerMask;
    }

    constexpr uint32_t UnpackSvsmCompactRenderPagePhysical(
        uint32_t packed)
    {
        return packed >> SvsmCompactRenderPagePhysicalShift;
    }

    constexpr uint32_t PackSvsmPacketPageCoordinate(
        uint32_t x,
        uint32_t y)
    {
        return (x & 0xffu) | ((y & 0xffu) << 8u);
    }

    constexpr float SvsmPacketBoundsTexelHalo = 1.f;

    constexpr uint32_t PackSvsmPacketTexelCoordinate(
        uint32_t x,
        uint32_t y)
    {
        return (x & 0xffffu) | ((y & 0xffffu) << 16u);
    }

    constexpr uint32_t UnpackSvsmPacketTexelCoordinateX(
        uint32_t packed)
    {
        return packed & 0xffffu;
    }

    constexpr uint32_t UnpackSvsmPacketTexelCoordinateY(
        uint32_t packed)
    {
        return packed >> 16u;
    }

    struct SvsmPacketPageRectangle
    {
        uint32_t packedMinimum = SvsmInvalidPacketPageBounds;
        uint32_t packedMaximum = SvsmInvalidPacketPageBounds;
    };

    struct SvsmPacketTexelRectangle
    {
        uint32_t packedMinimum = SvsmInvalidPacketPageBounds;
        uint32_t packedMaximum = SvsmInvalidPacketPageBounds;
    };

    inline SvsmPacketTexelRectangle GetSvsmPacketTexelRectangle(
        float minimumX,
        float minimumY,
        float maximumX,
        float maximumY)
    {
        if (!std::isfinite(minimumX) ||
            !std::isfinite(minimumY) ||
            !std::isfinite(maximumX) ||
            !std::isfinite(maximumY) ||
            minimumX > maximumX ||
            minimumY > maximumY)
        {
            return {};
        }

        minimumX = std::nextafter(
            minimumX - SvsmPacketBoundsTexelHalo,
            -std::numeric_limits<float>::infinity());
        minimumY = std::nextafter(
            minimumY - SvsmPacketBoundsTexelHalo,
            -std::numeric_limits<float>::infinity());
        maximumX = std::nextafter(
            maximumX + SvsmPacketBoundsTexelHalo,
            std::numeric_limits<float>::infinity());
        maximumY = std::nextafter(
            maximumY + SvsmPacketBoundsTexelHalo,
            std::numeric_limits<float>::infinity());
        if (maximumX < 0.f ||
            maximumY < 0.f ||
            minimumX > float(SvsmVirtualResolution) ||
            minimumY > float(SvsmVirtualResolution))
        {
            return {
                SvsmEmptyPacketPageBounds,
                SvsmEmptyPacketPageBounds
            };
        }

        const auto toMinimumTexel = [](float coordinate) {
            const float clamped = std::clamp(
                coordinate,
                0.f,
                std::nextafter(
                    float(SvsmVirtualResolution), 0.f));
            return uint32_t(std::floor(clamped));
        };
        const auto toMaximumTexel = [](float coordinate) {
            const float clamped = std::clamp(
                coordinate,
                0.f,
                std::nextafter(
                    float(SvsmVirtualResolution), 0.f));
            return uint32_t(std::floor(clamped));
        };
        return {
            PackSvsmPacketTexelCoordinate(
                toMinimumTexel(minimumX),
                toMinimumTexel(minimumY)),
            PackSvsmPacketTexelCoordinate(
                toMaximumTexel(maximumX),
                toMaximumTexel(maximumY))
        };
    }

    constexpr uint32_t PackSvsmStaticDepthHierarchyTag(
        uint32_t virtualOwner,
        uint32_t epoch)
    {
        return
            ((epoch & SvsmStaticDepthHierarchyTagEpochMask)
                << SvsmStaticDepthHierarchyTagEpochShift) |
            (virtualOwner & SvsmStaticDepthHierarchyTagOwnerMask);
    }

    constexpr uint32_t UnpackSvsmStaticDepthHierarchyTagOwner(
        uint32_t tag)
    {
        return tag & SvsmStaticDepthHierarchyTagOwnerMask;
    }

    constexpr uint32_t UnpackSvsmStaticDepthHierarchyTagEpoch(
        uint32_t tag)
    {
        return tag >> SvsmStaticDepthHierarchyTagEpochShift;
    }

    [[nodiscard]] constexpr bool IsSvsmStaticDepthHierarchyTagValid(
        uint32_t tag,
        uint32_t expectedOwner)
    {
        return UnpackSvsmStaticDepthHierarchyTagEpoch(tag) != 0u &&
            UnpackSvsmStaticDepthHierarchyTagOwner(tag) ==
                expectedOwner;
    }

    using SvsmStaticDepthHierarchyWords =
        std::array<uint32_t, SvsmStaticDepthHierarchyWordsPerPage>;

    [[nodiscard]] constexpr SvsmStaticDepthHierarchyWords
    BuildSvsmStaticDepthHierarchyFromBaseCells(
        const std::array<
            uint32_t,
            SvsmStaticDepthHierarchyBaseCount>& baseCells,
        uint32_t tag)
    {
        SvsmStaticDepthHierarchyWords hierarchy = {};
        for (uint32_t cell = 0u;
            cell < SvsmStaticDepthHierarchyBaseCount;
            ++cell)
        {
            hierarchy[
                SvsmStaticDepthHierarchyBaseOffset + cell] =
                baseCells[cell];
        }
        for (uint32_t y = 0u;
            y < SvsmStaticDepthHierarchyLevelOneAxis;
            ++y)
        {
            for (uint32_t x = 0u;
                x < SvsmStaticDepthHierarchyLevelOneAxis;
                ++x)
            {
                const uint32_t child =
                    (y * 2u) *
                        SvsmStaticDepthHierarchyBaseAxis +
                    x * 2u;
                hierarchy[
                    SvsmStaticDepthHierarchyLevelOneOffset +
                    y * SvsmStaticDepthHierarchyLevelOneAxis + x] =
                    std::min(
                        std::min(
                            baseCells[child],
                            baseCells[child + 1u]),
                        std::min(
                            baseCells[
                                child +
                                SvsmStaticDepthHierarchyBaseAxis],
                            baseCells[
                                child +
                                SvsmStaticDepthHierarchyBaseAxis +
                                1u]));
            }
        }
        for (uint32_t y = 0u;
            y < SvsmStaticDepthHierarchyLevelTwoAxis;
            ++y)
        {
            for (uint32_t x = 0u;
                x < SvsmStaticDepthHierarchyLevelTwoAxis;
                ++x)
            {
                const uint32_t child =
                    (y * 2u) *
                        SvsmStaticDepthHierarchyLevelOneAxis +
                    x * 2u;
                hierarchy[
                    SvsmStaticDepthHierarchyLevelTwoOffset +
                    y * SvsmStaticDepthHierarchyLevelTwoAxis + x] =
                    std::min(
                        std::min(
                            hierarchy[
                                SvsmStaticDepthHierarchyLevelOneOffset +
                                child],
                            hierarchy[
                                SvsmStaticDepthHierarchyLevelOneOffset +
                                child + 1u]),
                        std::min(
                            hierarchy[
                                SvsmStaticDepthHierarchyLevelOneOffset +
                                child +
                                SvsmStaticDepthHierarchyLevelOneAxis],
                            hierarchy[
                                SvsmStaticDepthHierarchyLevelOneOffset +
                                child +
                                SvsmStaticDepthHierarchyLevelOneAxis +
                                1u]));
            }
        }
        hierarchy[SvsmStaticDepthHierarchyRootOffset] =
            std::min(
                std::min(
                    hierarchy[
                        SvsmStaticDepthHierarchyLevelTwoOffset],
                    hierarchy[
                        SvsmStaticDepthHierarchyLevelTwoOffset + 1u]),
                std::min(
                    hierarchy[
                        SvsmStaticDepthHierarchyLevelTwoOffset + 2u],
                    hierarchy[
                        SvsmStaticDepthHierarchyLevelTwoOffset + 3u]));
        hierarchy[SvsmStaticDepthHierarchyTagOffset] = tag;
        return hierarchy;
    }

    [[nodiscard]] constexpr uint32_t
    QuerySvsmStaticDepthHierarchyBaseCellRegion(
        const SvsmStaticDepthHierarchyWords& hierarchy,
        uint32_t minimumX,
        uint32_t minimumY,
        uint32_t maximumX,
        uint32_t maximumY)
    {
        if (minimumX > maximumX ||
            minimumY > maximumY ||
            maximumX >= SvsmStaticDepthHierarchyBaseAxis ||
            maximumY >= SvsmStaticDepthHierarchyBaseAxis)
        {
            return std::numeric_limits<uint32_t>::max();
        }
        if (minimumX == 0u &&
            minimumY == 0u &&
            maximumX ==
                SvsmStaticDepthHierarchyBaseAxis - 1u &&
            maximumY ==
                SvsmStaticDepthHierarchyBaseAxis - 1u)
        {
            return hierarchy[
                SvsmStaticDepthHierarchyRootOffset];
        }

        uint32_t minimumDepth =
            std::numeric_limits<uint32_t>::max();
        for (uint32_t y = minimumY; y <= maximumY; ++y)
        {
            for (uint32_t x = minimumX; x <= maximumX; ++x)
            {
                minimumDepth = std::min(
                    minimumDepth,
                    hierarchy[
                        SvsmStaticDepthHierarchyBaseOffset +
                        y * SvsmStaticDepthHierarchyBaseAxis + x]);
            }
        }
        return minimumDepth;
    }

    [[nodiscard]] constexpr uint32_t
    QuerySvsmStaticDepthHierarchyRegion(
        const SvsmStaticDepthHierarchyWords& hierarchy,
        uint32_t minimumX,
        uint32_t minimumY,
        uint32_t maximumX,
        uint32_t maximumY)
    {
        if (minimumX > maximumX ||
            minimumY > maximumY ||
            maximumX >= SvsmStaticDepthHierarchyBaseAxis ||
            maximumY >= SvsmStaticDepthHierarchyBaseAxis)
        {
            return std::numeric_limits<uint32_t>::max();
        }
        if (minimumX == 0u &&
            minimumY == 0u &&
            maximumX ==
                SvsmStaticDepthHierarchyBaseAxis - 1u &&
            maximumY ==
                SvsmStaticDepthHierarchyBaseAxis - 1u)
        {
            return hierarchy[
                SvsmStaticDepthHierarchyRootOffset];
        }

        uint32_t minimumDepth =
            std::numeric_limits<uint32_t>::max();
        for (uint32_t coarseY = 0u; coarseY < 2u; ++coarseY)
        {
            for (uint32_t coarseX = 0u; coarseX < 2u; ++coarseX)
            {
                const uint32_t coarseMinimumX = coarseX * 4u;
                const uint32_t coarseMinimumY = coarseY * 4u;
                const uint32_t coarseMaximumX =
                    coarseMinimumX + 3u;
                const uint32_t coarseMaximumY =
                    coarseMinimumY + 3u;
                if (maximumX < coarseMinimumX ||
                    maximumY < coarseMinimumY ||
                    minimumX > coarseMaximumX ||
                    minimumY > coarseMaximumY)
                {
                    continue;
                }
                if (minimumX <= coarseMinimumX &&
                    minimumY <= coarseMinimumY &&
                    maximumX >= coarseMaximumX &&
                    maximumY >= coarseMaximumY)
                {
                    minimumDepth = std::min(
                        minimumDepth,
                        hierarchy[
                            SvsmStaticDepthHierarchyLevelTwoOffset +
                            coarseY *
                                SvsmStaticDepthHierarchyLevelTwoAxis +
                            coarseX]);
                    continue;
                }

                for (uint32_t fineY = 0u; fineY < 2u; ++fineY)
                {
                    for (uint32_t fineX = 0u; fineX < 2u; ++fineX)
                    {
                        const uint32_t fineMinimumX =
                            coarseMinimumX + fineX * 2u;
                        const uint32_t fineMinimumY =
                            coarseMinimumY + fineY * 2u;
                        const uint32_t fineMaximumX =
                            fineMinimumX + 1u;
                        const uint32_t fineMaximumY =
                            fineMinimumY + 1u;
                        if (maximumX < fineMinimumX ||
                            maximumY < fineMinimumY ||
                            minimumX > fineMaximumX ||
                            minimumY > fineMaximumY)
                        {
                            continue;
                        }
                        if (minimumX <= fineMinimumX &&
                            minimumY <= fineMinimumY &&
                            maximumX >= fineMaximumX &&
                            maximumY >= fineMaximumY)
                        {
                            const uint32_t fineNodeX =
                                fineMinimumX / 2u;
                            const uint32_t fineNodeY =
                                fineMinimumY / 2u;
                            minimumDepth = std::min(
                                minimumDepth,
                                hierarchy[
                                    SvsmStaticDepthHierarchyLevelOneOffset +
                                    fineNodeY *
                                        SvsmStaticDepthHierarchyLevelOneAxis +
                                    fineNodeX]);
                            continue;
                        }

                        for (uint32_t cellY = 0u;
                            cellY < 2u;
                            ++cellY)
                        {
                            for (uint32_t cellX = 0u;
                                cellX < 2u;
                                ++cellX)
                            {
                                const uint32_t cell =
                                    (fineMinimumY + cellY) *
                                        SvsmStaticDepthHierarchyBaseAxis +
                                    fineMinimumX + cellX;
                                const uint32_t x =
                                    fineMinimumX + cellX;
                                const uint32_t y =
                                    fineMinimumY + cellY;
                                if (x >= minimumX &&
                                    x <= maximumX &&
                                    y >= minimumY &&
                                    y <= maximumY)
                                {
                                    minimumDepth = std::min(
                                        minimumDepth,
                                        hierarchy[
                                            SvsmStaticDepthHierarchyBaseOffset +
                                            cell]);
                                }
                            }
                        }
                    }
                }
            }
        }
        return minimumDepth;
    }

    [[nodiscard]] inline bool ShouldCullSvsmDynamicCasterAgainstStaticDepth(
        bool optimizationEnabled,
        bool pairedDepthActive,
        bool dirtyPageScatterRasterActive,
        bool staticCaster,
        bool packetBoundsReliable,
        bool resident,
        bool dirty,
        bool staticDirty,
        bool ownerMatches,
        bool scheduledOwnerMatches,
        bool hierarchyTagMatches,
        float casterNearestReverseDepth,
        float minimumStaticReverseDepth,
        float conservativeBias)
    {
        if (!optimizationEnabled ||
            !pairedDepthActive ||
            dirtyPageScatterRasterActive ||
            staticCaster ||
            !packetBoundsReliable ||
            !resident ||
            !dirty ||
            staticDirty ||
            !ownerMatches ||
            !scheduledOwnerMatches ||
            !hierarchyTagMatches ||
            !std::isfinite(casterNearestReverseDepth) ||
            !std::isfinite(minimumStaticReverseDepth) ||
            !std::isfinite(conservativeBias) ||
            casterNearestReverseDepth < 0.f ||
            casterNearestReverseDepth > 1.f ||
            minimumStaticReverseDepth <= 0.f ||
            minimumStaticReverseDepth > 1.f ||
            conservativeBias < 0.f ||
            conservativeBias > 0.05f)
        {
            return false;
        }

        // Reverse Z stores the nearest depth as the largest value. The strict
        // comparison preserves equality and adds the caller's conservative
        // depth guard before rejecting a complete dynamic packet-page pair.
        return casterNearestReverseDepth + conservativeBias <
            minimumStaticReverseDepth;
    }

    inline SvsmPacketPageRectangle GetSvsmPacketPageRectangle(
        float minimumX,
        float minimumY,
        float maximumX,
        float maximumY)
    {
        if (!std::isfinite(minimumX) ||
            !std::isfinite(minimumY) ||
            !std::isfinite(maximumX) ||
            !std::isfinite(maximumY) ||
            minimumX > maximumX ||
            minimumY > maximumY)
        {
            return {};
        }

        // CPU corner projection and GPU vertex projection may differ by a few
        // floating-point roundoff bits. One virtual texel is a conservative
        // numerical guard without expanding every caster by a whole page.
        minimumX = std::nextafter(
            minimumX - SvsmPacketBoundsTexelHalo,
            -std::numeric_limits<float>::infinity());
        minimumY = std::nextafter(
            minimumY - SvsmPacketBoundsTexelHalo,
            -std::numeric_limits<float>::infinity());
        maximumX = std::nextafter(
            maximumX + SvsmPacketBoundsTexelHalo,
            std::numeric_limits<float>::infinity());
        maximumY = std::nextafter(
            maximumY + SvsmPacketBoundsTexelHalo,
            std::numeric_limits<float>::infinity());
        if (maximumX < 0.f ||
            maximumY < 0.f ||
            minimumX > float(SvsmVirtualResolution) ||
            minimumY > float(SvsmVirtualResolution))
        {
            return {
                SvsmEmptyPacketPageBounds,
                SvsmEmptyPacketPageBounds
            };
        }

        const auto toPage = [](float coordinate) {
            const float clamped = std::clamp(
                coordinate,
                0.f,
                std::nextafter(
                    float(SvsmVirtualResolution), 0.f));
            return uint32_t(std::floor(
                clamped / float(SvsmPageSize)));
        };
        return {
            PackSvsmPacketPageCoordinate(
                toPage(minimumX), toPage(minimumY)),
            PackSvsmPacketPageCoordinate(
                toPage(maximumX), toPage(maximumY))
        };
    }

    struct SvsmTightInvalidationProjection
    {
        SvsmPacketPageRectangle pages;
        float minimumReverseDepth =
            std::numeric_limits<float>::infinity();
        float maximumReverseDepth =
            -std::numeric_limits<float>::infinity();
        bool valid = false;
        bool overlapsDepthRange = false;
    };

    [[nodiscard]] inline SvsmTightInvalidationProjection
    ProjectSvsmClipCornersForInvalidation(
        const std::array<std::array<float, 4u>, 8u>& clipCorners)
    {
        SvsmTightInvalidationProjection result;
        float minimumVirtualX =
            std::numeric_limits<float>::infinity();
        float minimumVirtualY =
            std::numeric_limits<float>::infinity();
        float maximumVirtualX =
            -std::numeric_limits<float>::infinity();
        float maximumVirtualY =
            -std::numeric_limits<float>::infinity();

        for (const std::array<float, 4u>& clip : clipCorners)
        {
            if (!std::all_of(
                    clip.begin(),
                    clip.end(),
                    [](float value) { return std::isfinite(value); }) ||
                !(clip[3] > 1e-8f))
            {
                return result;
            }

            const float inverseW = 1.f / clip[3];
            const float ndcX = clip[0] * inverseW;
            const float ndcY = clip[1] * inverseW;
            const float reverseDepth = clip[2] * inverseW;
            if (!std::isfinite(ndcX) ||
                !std::isfinite(ndcY) ||
                !std::isfinite(reverseDepth))
            {
                return result;
            }

            const float virtualX =
                (ndcX * 0.5f + 0.5f) *
                float(SvsmVirtualResolution);
            const float virtualY =
                (-ndcY * 0.5f + 0.5f) *
                float(SvsmVirtualResolution);
            minimumVirtualX =
                std::min(minimumVirtualX, virtualX);
            minimumVirtualY =
                std::min(minimumVirtualY, virtualY);
            maximumVirtualX =
                std::max(maximumVirtualX, virtualX);
            maximumVirtualY =
                std::max(maximumVirtualY, virtualY);
            result.minimumReverseDepth =
                std::min(result.minimumReverseDepth, reverseDepth);
            result.maximumReverseDepth =
                std::max(result.maximumReverseDepth, reverseDepth);
        }

        result.valid = true;
        const float conservativeMinimumDepth =
            std::nextafter(
                0.f,
                -std::numeric_limits<float>::infinity());
        const float conservativeMaximumDepth =
            std::nextafter(
                1.f,
                std::numeric_limits<float>::infinity());
        result.overlapsDepthRange =
            result.maximumReverseDepth >=
                conservativeMinimumDepth &&
            result.minimumReverseDepth <=
                conservativeMaximumDepth;
        if (!result.overlapsDepthRange)
        {
            result.pages = {
                SvsmEmptyPacketPageBounds,
                SvsmEmptyPacketPageBounds
            };
            return result;
        }

        result.pages = GetSvsmPacketPageRectangle(
            minimumVirtualX,
            minimumVirtualY,
            maximumVirtualX,
            maximumVirtualY);
        if (result.pages.packedMinimum ==
                SvsmInvalidPacketPageBounds ||
            result.pages.packedMaximum ==
                SvsmInvalidPacketPageBounds)
        {
            result.valid = false;
        }
        return result;
    }

    enum class SvsmMode : uint32_t
    {
        DenseReference,
        SparseUncached,
        SparseCached
    };

    [[nodiscard]] constexpr bool
    IsSvsmAllocationBudgetSaturationEarlyOutActive(
        SvsmMode mode,
        bool enabled,
        uint32_t finePageRenderBudget)
    {
        return mode != SvsmMode::DenseReference &&
            ShouldEnableSvsmAllocationBudgetSaturationEarlyOut(
                enabled,
                finePageRenderBudget);
    }

    [[nodiscard]] constexpr bool
    IsSvsmDirtyPageScatterOptimizationActive(
        SvsmMode mode,
        bool gpuGatedDrawSubmission,
        bool packetPageCullingEnabled,
        bool dirtyPageScatterRasterEnabled,
        bool optimizationEnabled)
    {
        return mode != SvsmMode::DenseReference &&
            gpuGatedDrawSubmission &&
            packetPageCullingEnabled &&
            dirtyPageScatterRasterEnabled &&
            optimizationEnabled;
    }

    [[nodiscard]] constexpr bool
    IsSvsmDirtyPageScatterSafetyBounded(
        bool amplificationGuardEnabled,
        bool coarsestPageRenderBudgetEnabled,
        uint32_t pageRenderBudget,
        uint32_t maximumAmplification)
    {
        // The production scatter path derives an exact scheduled-page list
        // for every packet and falls back to that list when its conservative
        // rectangle would amplify raster work too far. Consequently safety is
        // bounded per packet and no longer depends on a tiny global page
        // budget. Retain the legacy parameters so saved settings and callers
        // remain source-compatible.
        (void)coarsestPageRenderBudgetEnabled;
        (void)pageRenderBudget;
        return amplificationGuardEnabled &&
            maximumAmplification > 0u &&
            maximumAmplification <=
                SvsmMaximumDirtyPageScatterAmplification;
    }

    enum class SvsmResourceBackend : uint32_t
    {
        None,
        Dense,
        Sparse
    };

    constexpr bool RequiresSvsmResourceRecreation(
        SvsmResourceBackend current,
        SvsmResourceBackend requested)
    {
        return current != requested;
    }

    constexpr bool ShouldAcceptSvsmTelemetrySample(
        uint64_t sampleGeneration,
        uint64_t currentGeneration,
        uint64_t sampleSourceFrame,
        uint64_t lastAcceptedSourceFrame,
        bool lastAcceptedSourceFrameValid)
    {
        return sampleGeneration == currentGeneration &&
            (!lastAcceptedSourceFrameValid ||
                sampleSourceFrame > lastAcceptedSourceFrame);
    }

    constexpr bool IsDetailedSvsmGpuTimingEnabled(
        bool configuredDetailedTiming,
        bool forceTotalOnlyTiming)
    {
        return configuredDetailedTiming && !forceTotalOnlyTiming;
    }

    constexpr bool ShouldIssueSvsmGpuTimerStage(
        bool detailedTimingEnabled,
        bool totalStage)
    {
        return totalStage || detailedTimingEnabled;
    }

    constexpr bool ShouldPrepareSvsmRenderPacketsForClipmap(
        uint32_t clipmap,
        uint32_t firstClipmap)
    {
        return clipmap < SvsmClipmapCount &&
            clipmap >= std::min(firstClipmap, SvsmClipmapCount - 1u);
    }

    constexpr bool CanDispatchSvsmPacketPageCulling(
        uint32_t packetCount,
        bool dirtyPageScatterRaster)
    {
        (void)dirtyPageScatterRaster;
        const uint64_t packetCount64 = packetCount;
        // Packet-page culling uses one cooperative threadgroup per packet for
        // both exact and scatter raster. Scatter needs the same exact scan to
        // derive a trustworthy per-packet fallback list and amplification
        // bound; packing 64 packets into a group prevents that cooperation.
        const uint64_t groupCount = packetCount64;
        return groupCount <=
            uint64_t(SvsmMaximumDispatchGroupsPerDimension) *
                uint64_t(SvsmMaximumDispatchGroupsPerDimension);
    }

    [[nodiscard]] constexpr bool
    ShouldUseSvsmDirtyPageScatterPerPageFallback(
        bool guardEnabled,
        uint32_t rectangleArea,
        uint32_t scheduledPageCount,
        uint32_t maximumAmplification)
    {
        return guardEnabled &&
            scheduledPageCount > 0u &&
            maximumAmplification > 0u &&
            uint64_t(rectangleArea) >
                uint64_t(maximumAmplification) *
                    uint64_t(scheduledPageCount);
    }

    [[nodiscard]] constexpr bool
    IsSvsmDirtyPageScatterPerPageRuntimeActive(
        bool guardEnabled,
        uint32_t runtimeState)
    {
        return guardEnabled &&
            (runtimeState & SvsmPacketPageRuntimePerPageBit) != 0u;
    }

    [[nodiscard]] constexpr bool
    ShouldClipSvsmDirtyPagePerPageRasterToPacketBounds(
        bool guardEnabled,
        uint32_t runtimeState)
    {
        return IsSvsmDirtyPageScatterPerPageRuntimeActive(
                guardEnabled, runtimeState) &&
            (runtimeState & SvsmPacketPageRuntimeFailOpenBit) == 0u;
    }

    struct SvsmDispatchDimensions
    {
        uint32_t groupsX = 0u;
        uint32_t groupsY = 0u;

        constexpr bool operator==(
            const SvsmDispatchDimensions& other) const
        {
            return groupsX == other.groupsX &&
                groupsY == other.groupsY;
        }
    };

    constexpr SvsmDispatchDimensions
        GetSvsmIndirectFillDispatchDimensions(
            uint32_t packetCount,
            bool packetPageCulling,
            bool dirtyPageScatterRaster)
    {
        const bool onePacketPerGroup =
            packetPageCulling;
        (void)dirtyPageScatterRaster;
        const uint32_t groupCount = onePacketPerGroup
            ? packetCount
            : packetCount / SvsmPacketFillThreadsPerGroup +
                (packetCount % SvsmPacketFillThreadsPerGroup != 0u
                    ? 1u
                    : 0u);
        if (groupCount == 0u)
            return {};

        return {
            std::min(
                groupCount,
                SvsmMaximumDispatchGroupsPerDimension),
            groupCount / SvsmMaximumDispatchGroupsPerDimension +
                (groupCount %
                        SvsmMaximumDispatchGroupsPerDimension != 0u
                    ? 1u
                    : 0u)
        };
    }

    constexpr bool CanUseSvsmStaticPacketBounds(
        bool hasSkinPrototype,
        bool isSkinPrototype,
        bool isMorphTargetAnimationMesh)
    {
        return !hasSkinPrototype &&
            !isSkinPrototype &&
            !isMorphTargetAnimationMesh;
    }

    enum class SvsmStaticPageRequestAction : uint32_t
    {
        Rebuild,
        ExtendUnion,
        Drain,
        Reuse
    };

    constexpr bool IsSvsmStaticJitterActive(
        float pixelOffsetX,
        float pixelOffsetY)
    {
        return pixelOffsetX != 0.f || pixelOffsetY != 0.f;
    }

    constexpr bool ShouldResetSvsmStaticJitterCache(
        bool cacheReady,
        bool previousJitterActive,
        float currentPixelOffsetX,
        float currentPixelOffsetY)
    {
        return cacheReady &&
            previousJitterActive != IsSvsmStaticJitterActive(
                currentPixelOffsetX,
                currentPixelOffsetY);
    }

    constexpr SvsmStaticPageRequestAction
        SelectSvsmStaticPageRequestAction(
            bool stateCompatible,
            bool jitterPreviouslySeen,
            bool pageMaintenancePending = false,
            bool receiverDepthMayHaveChanged = false)
    {
        if (!stateCompatible)
            return SvsmStaticPageRequestAction::Rebuild;
        // The camera depth texture is persistent, so matching its handle does
        // not prove that the receiver samples inside it are unchanged. Preserve
        // the existing union and conservatively remark after a scene revision
        // that may have changed GBuffer depth.
        if (receiverDepthMayHaveChanged)
            return SvsmStaticPageRequestAction::ExtendUnion;
        if (!jitterPreviouslySeen)
            return SvsmStaticPageRequestAction::ExtendUnion;
        return pageMaintenancePending
            ? SvsmStaticPageRequestAction::Drain
            : SvsmStaticPageRequestAction::Reuse;
    }

    constexpr uint32_t GetSvsmStaticPageDrainPassCount(
        uint32_t physicalPageCount,
        uint32_t pageRenderBudget)
    {
        if (physicalPageCount == 0u || pageRenderBudget == 0u)
            return 0u;
        return physicalPageCount / pageRenderBudget +
            (physicalPageCount % pageRenderBudget != 0u ? 1u : 0u);
    }

    constexpr bool CanReuseSvsmStaticVisibility(
        bool visibilityCachingEnabled,
        bool debugViewDisabled,
        bool pageRequestsReused,
        bool visibilitySlotValid)
    {
        return visibilityCachingEnabled &&
            debugViewDisabled &&
            pageRequestsReused &&
            visibilitySlotValid;
    }

    constexpr bool CanUseSvsmStaticPageRequestConfiguration(
        bool reuseEnabled,
        bool cacheEnabled,
        uint32_t physicalPageCount,
        uint32_t pageRenderBudget,
        bool jitterSupported,
        bool finiteBudgetStaticDrainEnabled = false)
    {
        return reuseEnabled &&
            cacheEnabled &&
            physicalPageCount > 0u &&
            physicalPageCount <= SvsmPagesPerClipmap &&
            (pageRenderBudget >= physicalPageCount ||
                (finiteBudgetStaticDrainEnabled &&
                    pageRenderBudget > 0u)) &&
            jitterSupported;
    }

    constexpr bool CanUseSvsmStaticZeroWorkFastPath(
        bool pageRequestsReused,
        bool visibilityReused,
        bool packetMetadataUploadPending,
        bool indirectArgumentTemplatesPrepared,
        bool timingSampleRequested)
    {
        return pageRequestsReused &&
            visibilityReused &&
            !packetMetadataUploadPending &&
            !indirectArgumentTemplatesPrepared &&
            !timingSampleRequested;
    }

    constexpr uint32_t FirstSvsmStaticPageRequestRejectBit(
        uint32_t rejectMask)
    {
        for (uint32_t bit = 0u; bit < 32u; ++bit)
        {
            if ((rejectMask & (1u << bit)) != 0u)
                return bit;
        }
        return 32u;
    }

    constexpr bool ShouldInvalidateSvsmStaticVisibility(
        SvsmStaticPageRequestAction action)
    {
        return action != SvsmStaticPageRequestAction::Reuse;
    }

    constexpr bool ShouldMarkSvsmStaticPageRequests(
        SvsmStaticPageRequestAction action)
    {
        return action == SvsmStaticPageRequestAction::Rebuild ||
            action == SvsmStaticPageRequestAction::ExtendUnion;
    }

    constexpr bool ShouldMaintainSvsmStaticPages(
        SvsmStaticPageRequestAction action)
    {
        return action != SvsmStaticPageRequestAction::Reuse;
    }

    constexpr bool IsSvsmStaticPageMaintenanceOptimizationActive(
        bool optimizationEnabled,
        SvsmStaticPageRequestAction action)
    {
        return optimizationEnabled &&
            ShouldMaintainSvsmStaticPages(action);
    }

    constexpr bool RequiresSvsmPacketPageModeTransition(
        bool gpuGatedDrawSubmission,
        bool packetPageCullingRequested,
        bool indirectArgumentsUsePacketPageCulling,
        bool packetPageCullingReady,
        bool packetPageCullingUnavailable,
        bool packetMetadataCacheValid,
        bool packetMetadataUsesExactPageLists,
        bool exactPacketPageListsRequested)
    {
        return gpuGatedDrawSubmission &&
            ((!packetPageCullingRequested &&
                indirectArgumentsUsePacketPageCulling) ||
            (packetPageCullingRequested &&
                ((!packetMetadataCacheValid ||
                    packetMetadataUsesExactPageLists !=
                        exactPacketPageListsRequested) ||
                ((!indirectArgumentsUsePacketPageCulling ||
                    !packetPageCullingReady) &&
                    !packetPageCullingUnavailable))));
    }

    enum class SvsmMarkingMode : uint32_t
    {
        PerPixel,
        Tile8,
        Tile16
    };

    enum class SvsmFilterMode : uint32_t
    {
        ManualPageSafe,
        Hybrid
    };

    enum class SvsmFilterKernel : uint32_t
    {
        NearestPoisson,
        BilinearPcf
    };

    enum class SvsmPoissonOrdering : uint32_t
    {
        LegacyStride,
        BalancedProgressive
    };

    enum class SvsmTapCount : uint32_t
    {
        One = 1u,
        Four = 4u,
        Eight = 8u,
        Sixteen = 16u
    };

    constexpr std::array<uint32_t, 16u>
        SvsmBalancedProgressivePoissonOrder = {
            1u, 5u, 12u, 13u,
            4u, 7u, 8u, 11u,
            0u, 2u, 3u, 6u,
            9u, 10u, 14u, 15u
        };

    constexpr uint32_t GetSvsmPoissonSampleIndex(
        SvsmPoissonOrdering ordering,
        SvsmTapCount tapCount,
        uint32_t tapOrdinal)
    {
        const uint32_t taps = uint32_t(tapCount);
        if (taps <= 1u)
            return 0u;
        return ordering == SvsmPoissonOrdering::BalancedProgressive
            ? SvsmBalancedProgressivePoissonOrder[tapOrdinal]
            : tapOrdinal * (16u / taps);
    }

    constexpr uint32_t GetSvsmAdaptiveProbeCount(
        SvsmTapCount tapCount)
    {
        return std::min(uint32_t(tapCount), 4u);
    }

    constexpr uint32_t GetSvsmAdaptiveProbeTapOrdinal(
        uint32_t probeOrdinal)
    {
        return probeOrdinal;
    }

    constexpr uint32_t GetSvsmAdaptiveProbeReuseIndex(
        SvsmTapCount tapCount,
        uint32_t tapOrdinal)
    {
        return tapOrdinal < GetSvsmAdaptiveProbeCount(tapCount)
            ? tapOrdinal
            : std::numeric_limits<uint32_t>::max();
    }

    constexpr uint32_t GetSvsmFilterRadius(
        SvsmTapCount tapCount)
    {
        return tapCount == SvsmTapCount::One ? 0u : 3u;
    }

    constexpr uint32_t GetSvsmFilterRadius(
        SvsmTapCount tapCount,
        SvsmFilterKernel kernel)
    {
        return kernel == SvsmFilterKernel::BilinearPcf
            ? (tapCount == SvsmTapCount::One ? 1u : 4u)
            : GetSvsmFilterRadius(tapCount);
    }

    struct SvsmBilinearFootprint
    {
        int32_t minimumX = 0;
        int32_t minimumY = 0;
        float fractionX = 0.f;
        float fractionY = 0.f;
    };

    inline SvsmBilinearFootprint GetSvsmBilinearFootprint(
        float texelEdgeX,
        float texelEdgeY)
    {
        // Shader virtual coordinates are texture-edge coordinates. Hardware
        // bilinear filtering chooses its 2x2 footprint in texel-center
        // coordinates, so n + 0.5 addresses texel n with zero fractional
        // weight.
        const float texelCenterX = texelEdgeX - 0.5f;
        const float texelCenterY = texelEdgeY - 0.5f;
        const float minimumX = std::floor(texelCenterX);
        const float minimumY = std::floor(texelCenterY);
        return {
            int32_t(minimumX),
            int32_t(minimumY),
            texelCenterX - minimumX,
            texelCenterY - minimumY
        };
    }

    constexpr uint32_t SvsmTapCountPermutationIndex(
        SvsmTapCount tapCount)
    {
        switch (tapCount)
        {
        case SvsmTapCount::One:
            return 0u;
        case SvsmTapCount::Four:
            return 1u;
        case SvsmTapCount::Eight:
            return 2u;
        case SvsmTapCount::Sixteen:
            return 3u;
        }
        return 3u;
    }

    constexpr uint32_t GetSvsmSparseResolvePermutationIndex(
        SvsmPoissonOrdering ordering,
        SvsmFilterKernel filterKernel,
        bool precomposedClipmapTransformsEnabled,
        bool pageTranslationCachingEnabled,
        SvsmTapCount tapCount)
    {
        return ((((uint32_t(ordering) * 2u +
            (filterKernel == SvsmFilterKernel::BilinearPcf
                ? 1u
                : 0u)) * 2u +
            (precomposedClipmapTransformsEnabled ? 1u : 0u)) * 2u +
            (pageTranslationCachingEnabled ? 1u : 0u)) * 4u +
            SvsmTapCountPermutationIndex(tapCount));
    }

    enum class SvsmResolutionBias : uint32_t
    {
        Zero = 0u,
        PlusOne = 1u,
        PlusTwo = 2u
    };

    constexpr bool IsSvsmStaticVisibilityConfigurationCompatible(
        bool cachedSettingsValid,
        SvsmFilterMode cachedFilterMode,
        SvsmFilterKernel cachedFilterKernel,
        SvsmPoissonOrdering cachedPoissonOrdering,
        SvsmTapCount cachedTapCount,
        SvsmResolutionBias cachedResolutionBias,
        bool cachedPageTranslationCaching,
        bool cachedAdaptiveFiltering,
        SvsmFilterMode currentFilterMode,
        SvsmFilterKernel currentFilterKernel,
        SvsmPoissonOrdering currentPoissonOrdering,
        SvsmTapCount currentTapCount,
        SvsmResolutionBias currentResolutionBias,
        bool currentPageTranslationCaching,
        bool currentAdaptiveFiltering)
    {
        return cachedSettingsValid &&
            cachedFilterMode == currentFilterMode &&
            cachedFilterKernel == currentFilterKernel &&
            cachedPoissonOrdering == currentPoissonOrdering &&
            cachedTapCount == currentTapCount &&
            cachedResolutionBias == currentResolutionBias &&
            cachedPageTranslationCaching ==
                currentPageTranslationCaching &&
            cachedAdaptiveFiltering == currentAdaptiveFiltering;
    }

    struct SvsmMovingLightFramePolicy
    {
        bool effectiveCacheEnabled = false;
        bool effectivePairedDepthEnabled = false;
        bool forceContentInvalidation = false;
        bool uncached = false;
        bool transitioningToCached = false;
        bool previousUncachedAfterCommit = false;
        uint32_t recoveryFramesAfterCommit = 0u;
        float lodRecoveryFactor = 0.f;
    };

    [[nodiscard]] constexpr SvsmMovingLightFramePolicy
    GetSvsmMovingLightFramePolicy(
        bool policyEnabled,
        bool configuredCacheEnabled,
        bool configuredPairedDepthEnabled,
        bool committedLightKeyChanged,
        bool previousUncached,
        uint32_t recoveryFramesRemaining,
        uint32_t recoveryFrameCount)
    {
        if (!configuredCacheEnabled)
        {
            // An explicitly uncached frame uses only merged depth. Keep that
            // fact transactional so reenabling caching must initialize the
            // configured paired slice before it can be reused.
            return {
                false,
                false,
                false,
                false,
                false,
                true,
                0u,
                0.f
            };
        }
        if (!policyEnabled)
        {
            return {
                true,
                configuredPairedDepthEnabled,
                previousUncached,
                false,
                previousUncached,
                false,
                0u,
                0.f
            };
        }

        if (committedLightKeyChanged)
        {
            return {
                false,
                false,
                true,
                true,
                false,
                true,
                recoveryFrameCount,
                1.f
            };
        }

        const uint32_t clampedRecoveryFramesRemaining =
            std::min(recoveryFramesRemaining, recoveryFrameCount);
        const uint32_t recoveryFramesAfterCommit =
            clampedRecoveryFramesRemaining > 0u
            ? clampedRecoveryFramesRemaining - 1u
            : 0u;
        const float recoveryFactor = recoveryFrameCount > 0u
            ? float(recoveryFramesAfterCommit) /
                float(recoveryFrameCount)
            : 0.f;
        return {
            true,
            configuredPairedDepthEnabled,
            previousUncached,
            false,
            previousUncached,
            false,
            recoveryFramesAfterCommit,
            recoveryFactor
        };
    }

    [[nodiscard]] inline SvsmResolutionBias
    GetEffectiveSvsmMovingLightResolutionBias(
        SvsmResolutionBias configuredBias,
        bool movingLightLodBiasEnabled,
        SvsmResolutionBias maximumMovingBias,
        float recoveryFactor)
    {
        if (!movingLightLodBiasEnabled ||
            !(recoveryFactor > 0.f))
        {
            return configuredBias;
        }

        const float finiteRecoveryFactor =
            std::isfinite(recoveryFactor)
            ? std::clamp(recoveryFactor, 0.f, 1.f)
            : 0.f;
        const uint32_t configuredBiasLevels = std::min(
            uint32_t(configuredBias),
            uint32_t(SvsmResolutionBias::PlusTwo));
        const uint32_t maximumMovingBiasLevels = std::min(
            uint32_t(maximumMovingBias),
            uint32_t(SvsmResolutionBias::PlusTwo));
        const uint32_t recoveredMovingBias = uint32_t(std::ceil(
            std::max(
                0.f,
                float(maximumMovingBiasLevels) *
                    finiteRecoveryFactor -
                1e-6f)));
        return SvsmResolutionBias(std::min(
            configuredBiasLevels + recoveredMovingBias,
            uint32_t(SvsmResolutionBias::PlusTwo)));
    }

    [[nodiscard]] inline SvsmResolutionBias
    GetEffectiveSvsmReceiverAwareMovingLightResolutionBias(
        SvsmResolutionBias configuredBias,
        bool movingLightLodBiasEnabled,
        SvsmResolutionBias maximumMovingBias,
        float recoveryFactor,
        bool receiverDistanceMipClampEnabled,
        bool continuousReceiverDistanceBiasEnabled)
    {
        // Continuous mode spends the moving-light increment spatially by
        // shifting receiver-distance thresholds. Applying the same increment
        // globally would double-bias distant receivers and unnecessarily
        // reduce nearby quality.
        const bool useDiscreteGlobalBias =
            movingLightLodBiasEnabled &&
            !(receiverDistanceMipClampEnabled &&
                continuousReceiverDistanceBiasEnabled);
        return GetEffectiveSvsmMovingLightResolutionBias(
            configuredBias,
            useDiscreteGlobalBias,
            maximumMovingBias,
            recoveryFactor);
    }

    constexpr uint32_t GetSvsmFirstClipmapLevel(
        SvsmResolutionBias bias)
    {
        return std::min(
            uint32_t(bias), SvsmClipmapCount - 1u);
    }

    [[nodiscard]] inline float
    GetEffectiveSvsmReceiverDistanceMipClampStart(
        bool receiverDistanceMipClampEnabled,
        float firstClipmapExtent,
        float startScale,
        bool continuousMovingLightBiasEnabled,
        bool movingLightLodBiasEnabled,
        SvsmResolutionBias maximumMovingBias,
        float movingLightRecoveryFactor)
    {
        if (!receiverDistanceMipClampEnabled ||
            !(firstClipmapExtent > 0.f) ||
            !std::isfinite(firstClipmapExtent) ||
            !(startScale > 0.f) ||
            !std::isfinite(startScale))
        {
            return 0.f;
        }

        float startDistance = firstClipmapExtent * startScale;
        if (continuousMovingLightBiasEnabled &&
            movingLightLodBiasEnabled)
        {
            const float finiteRecoveryFactor =
                std::isfinite(movingLightRecoveryFactor)
                ? std::clamp(
                    movingLightRecoveryFactor, 0.f, 1.f)
                : 0.f;
            const uint32_t maximumMovingBiasLevels = std::min(
                uint32_t(maximumMovingBias),
                uint32_t(SvsmResolutionBias::PlusTwo));
            startDistance *= std::exp2(
                -float(maximumMovingBiasLevels) *
                    finiteRecoveryFactor);
        }

        return startDistance > 0.f &&
            std::isfinite(startDistance)
            ? startDistance
            : 0.f;
    }

    [[nodiscard]] inline uint32_t
    GetSvsmReceiverFirstClipmapLevel(
        SvsmResolutionBias configuredBias,
        float receiverViewDistance,
        float effectiveClampStartDistance,
        uint32_t maximumClampLevel)
    {
        const uint32_t configuredLevel =
            GetSvsmFirstClipmapLevel(configuredBias);
        if (!(receiverViewDistance >= 0.f) ||
            !std::isfinite(receiverViewDistance) ||
            !(effectiveClampStartDistance > 0.f) ||
            !std::isfinite(effectiveClampStartDistance) ||
            maximumClampLevel == 0u)
        {
            return configuredLevel;
        }

        const float distanceRatio =
            receiverViewDistance / effectiveClampStartDistance;
        if (!(distanceRatio >= 1.f) ||
            !std::isfinite(distanceRatio))
        {
            return configuredLevel;
        }

        const float logarithmicLevel =
            std::floor(std::log2(distanceRatio)) + 1.f;
        if (!(logarithmicLevel >= 1.f) ||
            !std::isfinite(logarithmicLevel))
        {
            return configuredLevel;
        }

        const uint32_t distanceLevel = std::min(
            uint32_t(logarithmicLevel),
            std::min(
                maximumClampLevel,
                SvsmMaximumReceiverDistanceMipClampLevel));
        return std::max(configuredLevel, distanceLevel);
    }

    [[nodiscard]] constexpr bool
    ShouldContinueSvsmTiledReceiverLodMarking(
        uint32_t currentLevel,
        uint32_t maximumReceiverFirstLevel,
        bool tileFullyCovered)
    {
        // A geometrically covered tile can still contain farther receivers
        // whose distance clamp begins at an intermediate clipmap. Mark through
        // the coarsest represented first level before taking the normal
        // coverage early exit.
        return !tileFullyCovered ||
            currentLevel < maximumReceiverFirstLevel;
    }

    enum class SvsmPreset : uint32_t
    {
        Performance,
        Balanced,
        Quality,
        Custom
    };

    enum class SvsmDebugView : uint32_t
    {
        None,
        ClipmapSelection,
        RequiredPages,
        ResidentPages,
        CachedPages,
        DirtyPages,
        RenderedPages,
        PhysicalPool,
        FallbackLevel,
        MissingPages,
        TapCount,
        Visibility
    };

    enum class SvsmObjectInvalidationMode : uint32_t
    {
        // Observe every shadow-relevant caster change.
        Auto,
        // Refresh active caster coverage every update.
        Always,
        // Suppress deformation/WPO changes only.
        Rigid,
        // Suppress transform and deformation/WPO changes only.
        Static
    };

    [[nodiscard]] constexpr bool IsSvsmObjectInvalidationModeValid(
        SvsmObjectInvalidationMode mode)
    {
        return uint32_t(mode) <=
            uint32_t(SvsmObjectInvalidationMode::Static);
    }

    enum class SvsmSparseAlphaBindingLayout : uint32_t
    {
        FullGBuffer,
        ShadowOnly
    };

    [[nodiscard]] constexpr SvsmSparseAlphaBindingLayout
    GetSvsmSparseAlphaBindingLayout(
        bool leanAlphaTestedBindingsEnabled)
    {
        return leanAlphaTestedBindingsEnabled
            ? SvsmSparseAlphaBindingLayout::ShadowOnly
            : SvsmSparseAlphaBindingLayout::FullGBuffer;
    }

    [[nodiscard]] constexpr bool
    RequiresSvsmSparseDepthPassRecreation(
        SvsmSparseAlphaBindingLayout allocatedLayout,
        bool allocatedDeferredStaticDepthMergeRequest,
        bool leanAlphaTestedBindingsEnabled,
        bool deferredStaticDepthMergeRequest)
    {
        return allocatedLayout != GetSvsmSparseAlphaBindingLayout(
                leanAlphaTestedBindingsEnabled) ||
            allocatedDeferredStaticDepthMergeRequest !=
                deferredStaticDepthMergeRequest;
    }

    enum class SvsmDeferredStaticDepthPassAttempt : uint32_t
    {
        ReferenceOnly,
        DeferredThenReference
    };

    [[nodiscard]] constexpr bool
    IsSvsmDeferredStaticDepthMergeRequestEffective(
        bool deferredStaticDepthMergeRequested,
        bool rasterFallbackLatched)
    {
        return deferredStaticDepthMergeRequested &&
            !rasterFallbackLatched;
    }

    [[nodiscard]] constexpr bool
    ShouldLatchSvsmDeferredStaticDepthRasterFallback(
        bool deferredStaticDepthMergeActive,
        bool rasterSucceeded)
    {
        return deferredStaticDepthMergeActive &&
            !rasterSucceeded;
    }

    [[nodiscard]] constexpr bool
    GetNextSvsmDeferredStaticDepthRasterFallbackLatched(
        bool rasterFallbackLatched,
        bool deferredStaticDepthMergeRequested,
        bool deferredStaticDepthMergeActive,
        bool rasterSucceeded)
    {
        return deferredStaticDepthMergeRequested &&
            (rasterFallbackLatched ||
                ShouldLatchSvsmDeferredStaticDepthRasterFallback(
                    deferredStaticDepthMergeActive,
                    rasterSucceeded));
    }

    [[nodiscard]] constexpr SvsmDeferredStaticDepthPassAttempt
    GetSvsmDeferredStaticDepthPassAttempt(
        bool deferredStaticDepthMergeRequested,
        bool pairedStaticDynamicDepthConfigured,
        bool deferredMergeComputePipelineAvailable)
    {
        return deferredStaticDepthMergeRequested &&
                pairedStaticDynamicDepthConfigured &&
                deferredMergeComputePipelineAvailable
            ? SvsmDeferredStaticDepthPassAttempt::
                DeferredThenReference
            : SvsmDeferredStaticDepthPassAttempt::ReferenceOnly;
    }

    [[nodiscard]] constexpr bool
    ShouldFallbackSvsmDeferredStaticDepthPass(
        SvsmDeferredStaticDepthPassAttempt attempt,
        bool deferredRasterPassReady)
    {
        return attempt ==
                SvsmDeferredStaticDepthPassAttempt::
                    DeferredThenReference &&
            !deferredRasterPassReady;
    }

    [[nodiscard]] constexpr float EvaluateSvsmAlphaTestOpacity(
        float materialOpacity,
        bool opacityTextureEnabled,
        float opacityTextureSample,
        bool baseTextureEnabled,
        float baseTextureAlpha)
    {
        const float textureOpacity = opacityTextureEnabled
            ? opacityTextureSample
            : (baseTextureEnabled ? baseTextureAlpha : 1.f);
        const float opacity = materialOpacity * textureOpacity;
        return opacity < 0.f
            ? 0.f
            : (opacity > 1.f ? 1.f : opacity);
    }

    [[nodiscard]] constexpr bool
    HasSvsmAlphaTestScalarDepthChange(
        bool alphaTested,
        float previousOpacity,
        float currentOpacity,
        float previousAlphaCutoff,
        float currentAlphaCutoff)
    {
        return alphaTested &&
            (previousOpacity != currentOpacity ||
                previousAlphaCutoff != currentAlphaCutoff);
    }

    [[nodiscard]] constexpr bool ShouldInvalidateSvsmObject(
        SvsmObjectInvalidationMode mode,
        bool transformChanged,
        bool materialChanged,
        bool deformationChanged,
        bool topologyChanged)
    {
        // Geometry and shadow-relevant material changes alter the caster draw
        // itself and therefore cannot be suppressed by a per-object update
        // policy. Static and Rigid only suppress the motion/deformation
        // channels named by those policies.
        if (topologyChanged || materialChanged)
            return true;

        switch (mode)
        {
        case SvsmObjectInvalidationMode::Always:
            return true;
        case SvsmObjectInvalidationMode::Rigid:
            return transformChanged;
        case SvsmObjectInvalidationMode::Static:
            return false;
        case SvsmObjectInvalidationMode::Auto:
        default:
            return transformChanged || deformationChanged;
        }
    }

    enum class SvsmCasterEventCategory : uint32_t
    {
        Unchanged,
        Invalidating,
        PolicySuppressed,
        BindingOnly,
        Unexplained
    };

    enum class SvsmPublishedCasterAction : uint32_t
    {
        Retain,
        PublishCurrent,
        Remove
    };

    struct SvsmCasterEvent
    {
        bool previousExists = true;
        bool currentExists = true;
        bool transformChanged = false;
        bool depthMaterialChanged = false;
        bool bindingOnlyMaterialChanged = false;
        bool deformationChanged = false;
        bool topologyChanged = false;
        bool invalidationModeChanged = false;
        bool staticClassificationChanged = false;
        bool reliable = true;
    };

    struct SvsmCasterEventDecision
    {
        SvsmCasterEventCategory category =
            SvsmCasterEventCategory::Unchanged;
        SvsmPublishedCasterAction publishedAction =
            SvsmPublishedCasterAction::Retain;
        bool eventPresent = false;
        bool sceneEventPresent = false;
        bool invalidatePreviousCoverage = false;
        bool invalidateCurrentCoverage = false;
    };

    [[nodiscard]] constexpr SvsmCasterEventDecision
    ReconcileSvsmCasterEvent(
        SvsmObjectInvalidationMode mode,
        const SvsmCasterEvent& event)
    {
        const bool added =
            !event.previousExists && event.currentExists;
        const bool removed =
            event.previousExists && !event.currentExists;
        const bool malformedExistence =
            !event.previousExists && !event.currentExists &&
            (event.transformChanged ||
                event.depthMaterialChanged ||
                event.bindingOnlyMaterialChanged ||
                event.deformationChanged ||
                event.topologyChanged ||
                event.invalidationModeChanged ||
                event.staticClassificationChanged);
        const bool depthPolicyEvent =
            event.transformChanged ||
            event.depthMaterialChanged ||
            event.deformationChanged;
        const bool sceneEventPresent =
            added ||
            removed ||
            event.transformChanged ||
            event.depthMaterialChanged ||
            event.bindingOnlyMaterialChanged ||
            event.deformationChanged ||
            event.topologyChanged;
        const bool eventPresent =
            sceneEventPresent ||
            event.invalidationModeChanged ||
            event.staticClassificationChanged;

        SvsmCasterEventCategory category =
            SvsmCasterEventCategory::Unchanged;
        if (!event.reliable || malformedExistence)
        {
            category = SvsmCasterEventCategory::Unexplained;
        }
        else if (added || removed || event.topologyChanged)
        {
            category = SvsmCasterEventCategory::Invalidating;
        }
        else if (mode == SvsmObjectInvalidationMode::Always &&
            (event.previousExists || event.currentExists))
        {
            // Always is an update policy: every active caster is refreshed,
            // even when no CPU-visible property changed this frame.
            category = SvsmCasterEventCategory::Invalidating;
        }
        else if (event.invalidationModeChanged)
        {
            // Changing an object's update policy is an authoritative cache
            // transition. Invalidate both old and current coverage before the
            // new policy is allowed to suppress any coincident property
            // changes.
            category = SvsmCasterEventCategory::Invalidating;
        }
        else if (event.staticClassificationChanged)
        {
            // A layer transition changes which paired depth surface receives
            // the caster. It is authoritative even when the object's selected
            // invalidation policy suppresses the accompanying property change.
            category = SvsmCasterEventCategory::Invalidating;
        }
        else if (depthPolicyEvent)
        {
            const bool policyInvalidates =
                ShouldInvalidateSvsmObject(
                    mode,
                    event.transformChanged,
                    event.depthMaterialChanged,
                    event.deformationChanged,
                    event.topologyChanged);
            category = policyInvalidates
                ? SvsmCasterEventCategory::Invalidating
                : SvsmCasterEventCategory::PolicySuppressed;
        }
        else if (event.bindingOnlyMaterialChanged)
        {
            category = SvsmCasterEventCategory::BindingOnly;
        }

        SvsmPublishedCasterAction action =
            SvsmPublishedCasterAction::Retain;
        const bool invalidating =
            category == SvsmCasterEventCategory::Invalidating;
        if (invalidating)
        {
            action = event.currentExists
                ? SvsmPublishedCasterAction::PublishCurrent
                : SvsmPublishedCasterAction::Remove;
        }
        return {
            category,
            action,
            eventPresent,
            sceneEventPresent,
            invalidating && event.previousExists,
            invalidating && event.currentExists
        };
    }

    [[nodiscard]] constexpr bool
    ShouldAccumulateSvsmSuppressedCoverageDebt(
        bool previousDebt,
        SvsmCasterEventCategory currentCategory)
    {
        return previousDebt ||
            currentCategory ==
                SvsmCasterEventCategory::PolicySuppressed;
    }

    [[nodiscard]] constexpr bool
    RequiresSvsmFullRefreshForSuppressedCoverageDebt(
        bool previousDebt,
        const SvsmCasterEventDecision& currentDecision)
    {
        // Policy-suppressed A -> B -> C motion can leave different cached
        // pages containing any of those states when unrelated page work
        // rasterizes the live scene. A single published snapshot cannot
        // enumerate that mixed history, so the next authoritative event must
        // clear it with a full refresh.
        return previousDebt &&
            currentDecision.category ==
                SvsmCasterEventCategory::Invalidating;
    }

    [[nodiscard]] constexpr bool CommitSvsmSuppressedCoverageDebt(
        bool previousDebt,
        bool pendingDebt,
        bool fullRefresh,
        bool transactionSucceeded)
    {
        if (!transactionSucceeded)
            return previousDebt;
        return fullRefresh ? false : pendingDebt;
    }

    [[nodiscard]] constexpr bool
    IsSvsmObservedSceneChangeExplained(
        bool explanationRequired,
        bool observedSceneEvent,
        bool unexplainedEvent,
        bool callerChangeChannelsExhaustive)
    {
        return !explanationRequired ||
            (callerChangeChannelsExhaustive &&
                observedSceneEvent &&
                !unexplainedEvent);
    }

    [[nodiscard]] constexpr bool
    GetEffectiveSvsmDepthBindingCacheReset(
        bool resetLatched,
        bool structuralRebase,
        bool committedSignatureValid,
        uint64_t committedResourceHash,
        uint32_t committedCasterCount,
        uint64_t currentResourceHash,
        uint32_t currentCasterCount)
    {
        return resetLatched ||
            structuralRebase ||
            !committedSignatureValid ||
            committedResourceHash != currentResourceHash ||
            committedCasterCount != currentCasterCount;
    }

    [[nodiscard]] constexpr uint32_t PackSvsmLocalInvalidationPage(
        uint32_t localOwner,
        bool staticDirty)
    {
        return (localOwner & SvsmLocalInvalidationOwnerMask) |
            (staticDirty ? SvsmLocalInvalidationStaticBit : 0u);
    }

    [[nodiscard]] constexpr uint32_t
    UnpackSvsmLocalInvalidationOwner(uint32_t packed)
    {
        return packed & SvsmLocalInvalidationOwnerMask;
    }

    [[nodiscard]] constexpr bool
    IsSvsmLocalInvalidationStatic(uint32_t packed)
    {
        return (packed & SvsmLocalInvalidationStaticBit) != 0u;
    }

    [[nodiscard]] constexpr bool
    ShouldInvalidateSvsmStaticDepth(
        bool previousStaticCacheCandidate,
        bool currentStaticCacheCandidate)
    {
        return previousStaticCacheCandidate ||
            currentStaticCacheCandidate;
    }

    struct SvsmCasterInvalidationClasses
    {
        bool previousStatic = false;
        bool currentStatic = false;
    };

    [[nodiscard]] constexpr SvsmCasterInvalidationClasses
    GetSvsmCasterInvalidationClasses(
        bool previousStaticCacheCandidate,
        bool currentStaticCacheCandidate)
    {
        // Old and new coverage are classified independently. Their bitsets are
        // merged later with static dominance, so an overlap still clears both
        // layers without unnecessarily upgrading non-overlapping dynamic pages.
        return {
            previousStaticCacheCandidate,
            currentStaticCacheCandidate
        };
    }

    constexpr uint64_t SvsmNoPromotionDeadline =
        std::numeric_limits<uint64_t>::max();

    [[nodiscard]] constexpr uint64_t
    GetSvsmDynamicCasterPromotionDeadline(
        uint64_t successfulSparseStateCommits)
    {
        constexpr uint64_t Delay =
            uint64_t(SvsmDynamicToStaticPromotionFrames) + 1u;
        return successfulSparseStateCommits >
                SvsmNoPromotionDeadline - Delay
            ? SvsmNoPromotionDeadline - 1u
            : successfulSparseStateCommits + Delay;
    }

    [[nodiscard]] constexpr bool IsSvsmDynamicCasterPromotionDue(
        uint64_t successfulSparseStateCommits,
        uint64_t promotionDeadline)
    {
        return promotionDeadline != SvsmNoPromotionDeadline &&
            successfulSparseStateCommits >= promotionDeadline;
    }

    struct SvsmAdaptiveCasterDeadlineClassification
    {
        bool staticCacheCandidate = false;
        uint64_t promotionDeadline = SvsmNoPromotionDeadline;
    };

    [[nodiscard]] constexpr SvsmAdaptiveCasterDeadlineClassification
    AdvanceSvsmAdaptiveCasterDeadlineClassification(
        bool adaptiveClassificationEnabled,
        bool staticCacheEligible,
        bool invalidated,
        bool previousStaticCacheCandidate,
        uint64_t previousPromotionDeadline,
        uint64_t successfulSparseStateCommits)
    {
        if (!staticCacheEligible)
            return {};
        if (!adaptiveClassificationEnabled)
            return { true, SvsmNoPromotionDeadline };
        if (invalidated)
        {
            return {
                false,
                GetSvsmDynamicCasterPromotionDeadline(
                    successfulSparseStateCommits)
            };
        }
        if (previousStaticCacheCandidate)
            return { true, SvsmNoPromotionDeadline };
        if (IsSvsmDynamicCasterPromotionDue(
                successfulSparseStateCommits,
                previousPromotionDeadline))
        {
            return { true, SvsmNoPromotionDeadline };
        }
        return {
            false,
            previousPromotionDeadline == SvsmNoPromotionDeadline
                ? GetSvsmDynamicCasterPromotionDeadline(
                    successfulSparseStateCommits)
                : previousPromotionDeadline
        };
    }

    [[nodiscard]] constexpr bool GetEffectiveSvsmFullInvalidation(
        bool latchedFullInvalidation,
        bool requestedFullInvalidation)
    {
        return latchedFullInvalidation || requestedFullInvalidation;
    }

    [[nodiscard]] constexpr bool IsSvsmCasterSnapshotTrackable(
        bool deforming,
        bool reliableBounds,
        bool deformationRevisionReliable)
    {
        return deforming
            ? deformationRevisionReliable
            : reliableBounds;
    }

    [[nodiscard]] constexpr bool
    CanRetainSvsmUnboundedDeformingCaster(
        bool previousDeforming,
        bool currentDeforming,
        bool previousRevisionReliable,
        bool currentRevisionReliable,
        bool deformationRevisionChanged,
        bool transformChanged,
        bool materialOrTopologyChanged,
        bool staticClassificationChanged)
    {
        return previousDeforming &&
            currentDeforming &&
            previousRevisionReliable &&
            currentRevisionReliable &&
            !deformationRevisionChanged &&
            !transformChanged &&
            !materialOrTopologyChanged &&
            !staticClassificationChanged;
    }

    [[nodiscard]] constexpr bool CanUseSvsmLocalizedInvalidation(
        bool cacheEnabled,
        bool localizedInvalidationEnabled,
        bool cacheStateValid,
        bool snapshotValid,
        bool rootMatches,
        bool snapshotConfigurationMatches,
        bool currentSnapshotReliable,
        bool currentSceneRevisionReliable,
        bool previousSceneRevisionReliable,
        bool requiresFullSceneInvalidation,
        bool mappingChanged)
    {
        return cacheEnabled &&
            localizedInvalidationEnabled &&
            cacheStateValid &&
            snapshotValid &&
            rootMatches &&
            snapshotConfigurationMatches &&
            currentSnapshotReliable &&
            currentSceneRevisionReliable &&
            previousSceneRevisionReliable &&
            !requiresFullSceneInvalidation &&
            !mappingChanged;
    }

    [[nodiscard]] constexpr bool
    ShouldUpgradeSvsmLocalizedPagesToStatic(
        bool pairedStaticDynamicDepthEnabled,
        bool packetStaticClassificationActive)
    {
        return pairedStaticDynamicDepthEnabled &&
            !packetStaticClassificationActive;
    }

    [[nodiscard]] constexpr bool
    ShouldInvalidateSvsmCasterSnapshotsOnSuccess(
        bool cacheEnabled,
        bool localizedInvalidationEnabled)
    {
        return !cacheEnabled ||
            !localizedInvalidationEnabled;
    }

    [[nodiscard]] constexpr bool
    ShouldBlockSvsmStaticZeroWorkForSnapshotTransaction(
        bool snapshotTransactionPending,
        bool sceneStateChanged,
        bool requiresFullSceneInvalidation)
    {
        return snapshotTransactionPending ||
            sceneStateChanged ||
            requiresFullSceneInvalidation;
    }

    struct SvsmAdaptiveCasterClassification
    {
        bool staticCacheCandidate = false;
        uint32_t stableFrameCount = 0u;
    };

    [[nodiscard]] constexpr SvsmAdaptiveCasterClassification
    AdvanceSvsmAdaptiveCasterClassification(
        bool adaptiveClassificationEnabled,
        bool staticCacheEligible,
        bool invalidated,
        bool previousStaticCacheCandidate,
        uint32_t previousStableFrameCount)
    {
        if (!staticCacheEligible)
            return {};
        if (!adaptiveClassificationEnabled)
        {
            return {
                true,
                SvsmDynamicToStaticPromotionFrames + 1u
            };
        }
        if (invalidated)
            return {};
        if (previousStaticCacheCandidate)
        {
            return {
                true,
                previousStableFrameCount
            };
        }

        const uint32_t stableFrameCount = std::min(
            previousStableFrameCount + 1u,
            SvsmDynamicToStaticPromotionFrames + 1u);
        return {
            stableFrameCount >
                SvsmDynamicToStaticPromotionFrames,
            stableFrameCount
        };
    }

    struct SvsmPageMetadata
    {
        uint32_t physicalPage = SvsmInvalidPhysicalPage;
        uint32_t age = 0u;
        bool resident = false;
        bool required = false;
        bool dirty = true;
        bool staticDirty = false;
    };

    struct SvsmContentInvalidationResult
    {
        SvsmPageMetadata metadata;
        bool retainedPhysicalMapping = false;
        bool releasePhysicalOwner = false;
    };

    [[nodiscard]] constexpr SvsmContentInvalidationResult
    InvalidateSvsmPageContent(
        const SvsmPageMetadata& previous,
        uint32_t physicalPageCount,
        bool physicalOwnerMatches,
        bool preservePhysicalMappings,
        bool effectivePairedDepthEnabled)
    {
        const bool validOwnedMapping =
            previous.resident &&
            previous.physicalPage < physicalPageCount &&
            physicalOwnerMatches;
        if (preservePhysicalMappings && validOwnedMapping)
        {
            SvsmPageMetadata retained = previous;
            retained.required = false;
            retained.dirty = true;
            retained.staticDirty = effectivePairedDepthEnabled;
            return { retained, true, false };
        }

        SvsmPageMetadata invalidated;
        invalidated.dirty = true;
        invalidated.staticDirty = effectivePairedDepthEnabled;
        return {
            invalidated,
            false,
            !preservePhysicalMappings && validOwnedMapping
        };
    }

    [[nodiscard]] constexpr uint32_t
    GetSvsmPhysicalDepthArraySize(
        bool configuredPairedStaticDynamicDepthEnabled)
    {
        return configuredPairedStaticDynamicDepthEnabled ? 2u : 1u;
    }

    [[nodiscard]] constexpr bool ShouldWriteSvsmStaticDepth(
        bool effectivePairedStaticDynamicDepthEnabled,
        bool staticCaster)
    {
        return effectivePairedStaticDynamicDepthEnabled &&
            staticCaster;
    }

    [[nodiscard]] constexpr bool ShouldRenderSvsmPacketCaster(
        bool effectivePairedStaticDynamicDepthEnabled,
        bool staticCaster,
        bool resident,
        bool dirty,
        bool staticDirty)
    {
        return resident &&
            dirty &&
            (!effectivePairedStaticDynamicDepthEnabled ||
                !staticCaster ||
                staticDirty);
    }

    enum class SvsmPairedDepthPageAction : uint32_t
    {
        None,
        ClearMerged,
        ClearBoth,
        RestoreStaticToMerged
    };

    [[nodiscard]] constexpr SvsmPairedDepthPageAction
    GetSvsmPairedDepthPageAction(
        bool pairedStaticDynamicDepthEnabled,
        bool scheduled,
        bool dirty,
        bool staticDirty)
    {
        if (!scheduled || !dirty)
            return SvsmPairedDepthPageAction::None;
        if (!pairedStaticDynamicDepthEnabled)
            return SvsmPairedDepthPageAction::ClearMerged;
        return staticDirty
            ? SvsmPairedDepthPageAction::ClearBoth
            : SvsmPairedDepthPageAction::RestoreStaticToMerged;
    }

    [[nodiscard]] constexpr uint32_t MergeSvsmReverseDepth(
        uint32_t staticDepth,
        uint32_t dynamicDepth)
    {
        return staticDepth > dynamicDepth
            ? staticDepth
            : dynamicDepth;
    }

    struct SvsmPairedDepthValues
    {
        uint32_t merged = 0u;
        uint32_t staticDepth = 0u;
    };

    [[nodiscard]] constexpr SvsmPairedDepthValues
    ApplySvsmPairedDepthFragment(
        SvsmPairedDepthValues values,
        bool deferredStaticDepthMergeEnabled,
        bool effectivePairedStaticDynamicDepthEnabled,
        bool staticCaster,
        uint32_t reverseDepth)
    {
        const bool pairedStaticCaster =
            effectivePairedStaticDynamicDepthEnabled &&
            staticCaster;
        if (!deferredStaticDepthMergeEnabled ||
            !pairedStaticCaster)
        {
            values.merged = MergeSvsmReverseDepth(
                values.merged, reverseDepth);
        }
        if (pairedStaticCaster)
        {
            values.staticDepth = MergeSvsmReverseDepth(
                values.staticDepth, reverseDepth);
        }
        return values;
    }

    [[nodiscard]] constexpr SvsmPairedDepthValues
    FinishSvsmDeferredStaticDepthMerge(
        SvsmPairedDepthValues values,
        bool deferredStaticDepthMergeEnabled,
        bool effectivePairedStaticDynamicDepthEnabled,
        bool scheduledStaticDirtyPage)
    {
        if (deferredStaticDepthMergeEnabled &&
            effectivePairedStaticDynamicDepthEnabled &&
            scheduledStaticDirtyPage)
        {
            values.merged = MergeSvsmReverseDepth(
                values.merged, values.staticDepth);
        }
        return values;
    }

    constexpr uint32_t GetSvsmPageAgeElapsed(
        uint32_t currentFrame,
        uint32_t lastRequiredFrame)
    {
        return (currentFrame - lastRequiredFrame) & SvsmPageAgeMask;
    }

    constexpr bool IsSvsmPageInsideRecentEvictionGrace(
        uint32_t currentFrame,
        uint32_t lastRequiredFrame)
    {
        return GetSvsmPageAgeElapsed(
            currentFrame, lastRequiredFrame) <
            SvsmRecentPageEvictionGraceFrames;
    }

    enum class SvsmEvictionCandidateList : uint32_t
    {
        None,
        Free,
        UnrecentCached,
        RecentCached,
        RequiredFine
    };

    constexpr SvsmEvictionCandidateList ClassifySvsmCachedPage(
        bool recentPageEvictionGraceEnabled,
        uint32_t currentFrame,
        uint32_t lastRequiredFrame)
    {
        return recentPageEvictionGraceEnabled &&
                IsSvsmPageInsideRecentEvictionGrace(
                    currentFrame, lastRequiredFrame)
            ? SvsmEvictionCandidateList::RecentCached
            : SvsmEvictionCandidateList::UnrecentCached;
    }

    constexpr SvsmEvictionCandidateList SelectSvsmEvictionCandidateList(
        uint32_t requesterLevel,
        uint32_t freeCount,
        uint32_t unrecentCachedCount,
        uint32_t recentCachedCount,
        uint32_t requiredFineCount)
    {
        if (freeCount > 0u)
            return SvsmEvictionCandidateList::Free;
        if (unrecentCachedCount > 0u)
            return SvsmEvictionCandidateList::UnrecentCached;
        if (recentCachedCount > 0u)
            return SvsmEvictionCandidateList::RecentCached;
        if (requesterLevel == SvsmClipmapCount - 1u &&
            requiredFineCount > 0u)
        {
            return SvsmEvictionCandidateList::RequiredFine;
        }
        return SvsmEvictionCandidateList::None;
    }

    // Fifteen physical-page bits support pools up to 32768 pages. The remaining
    // bits retain all reference-path state in one R32_UINT page-table entry.
    constexpr uint32_t PackSvsmPageMetadata(
        const SvsmPageMetadata& metadata)
    {
        const uint32_t physicalPage = metadata.resident
            ? (metadata.physicalPage & 0x7fffu)
            : 0u;
        return physicalPage |
            (metadata.resident ? 1u << 15u : 0u) |
            (metadata.required ? 1u << 16u : 0u) |
            (metadata.dirty ? 1u << 17u : 0u) |
            ((metadata.age & SvsmPageAgeMask) << 18u) |
            (metadata.staticDirty ? SvsmPageStaticDirtyBit : 0u);
    }

    constexpr SvsmPageMetadata UnpackSvsmPageMetadata(uint32_t packed)
    {
        SvsmPageMetadata metadata;
        metadata.resident = (packed & (1u << 15u)) != 0u;
        metadata.required = (packed & (1u << 16u)) != 0u;
        metadata.dirty = (packed & (1u << 17u)) != 0u;
        metadata.age = (packed >> 18u) & SvsmPageAgeMask;
        metadata.staticDirty =
            (packed & SvsmPageStaticDirtyBit) != 0u;
        metadata.physicalPage = metadata.resident
            ? packed & 0x7fffu
            : SvsmInvalidPhysicalPage;
        return metadata;
    }

    constexpr int32_t WrapSvsmPageCoordinate(int64_t coordinate)
    {
        const int64_t modulus = int64_t(SvsmPagesPerAxis);
        const int64_t remainder = coordinate % modulus;
        return int32_t(remainder < 0 ? remainder + modulus : remainder);
    }

    inline bool TryQuantizeSvsmRenderOrigin(
        float coordinate,
        float pageWorldSize,
        int32_t& quantizedOrigin)
    {
        if (!std::isfinite(coordinate) ||
            !std::isfinite(pageWorldSize) ||
            !(pageWorldSize > 0.f))
        {
            return false;
        }

        const double rounded = std::round(
            double(coordinate) / double(pageWorldSize));
        if (!std::isfinite(rounded) ||
            rounded < double(std::numeric_limits<int32_t>::min()) ||
            rounded > double(std::numeric_limits<int32_t>::max()))
        {
            return false;
        }

        quantizedOrigin = int32_t(rounded);
        return true;
    }

    constexpr int32_t SaturateSvsmRenderOriginDelta(int64_t delta)
    {
        const int64_t fullExposure = int64_t(SvsmPagesPerAxis);
        if (delta <= -fullExposure)
            return -int32_t(SvsmPagesPerAxis);
        if (delta >= fullExposure)
            return int32_t(SvsmPagesPerAxis);
        return int32_t(delta);
    }

    inline bool IsSvsmProjectionRangeRepresentable(
        float extent,
        float maximumLightDepth)
    {
        if (!(extent > 0.f) ||
            !std::isfinite(extent) ||
            !(maximumLightDepth > 0.f) ||
            !std::isfinite(maximumLightDepth))
        {
            return false;
        }

        const float halfExtent = extent * 0.5f;
        const float halfDepth = maximumLightDepth * 0.5f;
        const float extentRange = halfExtent - (-halfExtent);
        const float depthRange = -halfDepth - halfDepth;
        if (!(halfExtent > 0.f) ||
            !(halfDepth > 0.f) ||
            !std::isfinite(extentRange) ||
            !std::isfinite(depthRange) ||
            extentRange == 0.f ||
            depthRange == 0.f)
        {
            return false;
        }

        const float xyScale = 2.f / extentRange;
        const float zScale = 1.f / depthRange;
        return std::isfinite(xyScale) &&
            xyScale != 0.f &&
            std::isfinite(zScale) &&
            zScale != 0.f;
    }

    struct SvsmProjectedDepthInterval
    {
        float minimum = 0.f;
        float maximum = 0.f;
    };

    [[nodiscard]] inline bool TryBuildSvsmProjectedDepthInterval(
        float center,
        float radius,
        bool includeAnchor,
        float anchor,
        SvsmProjectedDepthInterval& interval)
    {
        if (!std::isfinite(center) ||
            !std::isfinite(radius) ||
            radius < 0.f ||
            (includeAnchor && !std::isfinite(anchor)))
        {
            return false;
        }

        float minimum = center - radius;
        float maximum = center + radius;
        if (!std::isfinite(minimum) ||
            !std::isfinite(maximum) ||
            minimum > maximum)
        {
            return false;
        }
        if (includeAnchor)
        {
            minimum = std::min(minimum, anchor);
            maximum = std::max(maximum, anchor);
        }
        interval = { minimum, maximum };
        return true;
    }

    [[nodiscard]] inline bool
    TryBuildSvsmProjectedAabbDepthInterval(
        float projectedCenter,
        const std::array<float, 3>& boundsMinimum,
        const std::array<float, 3>& boundsMaximum,
        const std::array<float, 3>& projectedDepthAxis,
        bool includeAnchor,
        float anchor,
        SvsmProjectedDepthInterval& interval)
    {
        double projectedRadius = 0.0;
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            if (!std::isfinite(boundsMinimum[axis]) ||
                !std::isfinite(boundsMaximum[axis]) ||
                boundsMinimum[axis] > boundsMaximum[axis] ||
                !std::isfinite(projectedDepthAxis[axis]))
            {
                return false;
            }
            const double halfExtent =
                (double(boundsMaximum[axis]) -
                    double(boundsMinimum[axis])) * 0.5;
            projectedRadius +=
                std::abs(double(projectedDepthAxis[axis])) *
                halfExtent;
        }
        if (!std::isfinite(projectedRadius) ||
            projectedRadius >
                double(std::numeric_limits<float>::max()))
        {
            return false;
        }
        return TryBuildSvsmProjectedDepthInterval(
            projectedCenter,
            float(projectedRadius),
            includeAnchor,
            anchor,
            interval);
    }

    struct SvsmLightDepthOriginDecision
    {
        float selectedOrigin = 0.f;
        bool valid = false;
        bool retainedCommittedOrigin = false;
    };

    [[nodiscard]] inline SvsmLightDepthOriginDecision
    SelectSvsmLightDepthOrigin(
        bool guardBandEnabled,
        bool sparseCachedMode,
        bool cacheStateValid,
        bool committedOriginValid,
        bool sameProducingLight,
        bool sameLightBasis,
        bool compatibleMaximumLightDepth,
        float requestedOrigin,
        float committedOrigin,
        float maximumLightDepth,
        float guardBandFraction,
        bool projectedIntervalValid,
        const SvsmProjectedDepthInterval& projectedInterval)
    {
        SvsmLightDepthOriginDecision decision;
        decision.selectedOrigin = requestedOrigin;
        decision.valid = std::isfinite(requestedOrigin);
        if (!decision.valid ||
            !guardBandEnabled ||
            !sparseCachedMode ||
            !cacheStateValid ||
            !committedOriginValid ||
            !sameProducingLight ||
            !sameLightBasis ||
            !compatibleMaximumLightDepth ||
            !projectedIntervalValid ||
            !std::isfinite(committedOrigin) ||
            !(maximumLightDepth > 0.f) ||
            !std::isfinite(maximumLightDepth) ||
            !(guardBandFraction > 0.f) ||
            guardBandFraction > 1.f ||
            !std::isfinite(guardBandFraction) ||
            !std::isfinite(projectedInterval.minimum) ||
            !std::isfinite(projectedInterval.maximum) ||
            projectedInterval.minimum > projectedInterval.maximum)
        {
            return decision;
        }

        const float guardBandHalfDepth =
            maximumLightDepth * 0.5f * guardBandFraction;
        const float guardBandMinimum =
            committedOrigin - guardBandHalfDepth;
        const float guardBandMaximum =
            committedOrigin + guardBandHalfDepth;
        if (!(guardBandHalfDepth > 0.f) ||
            !std::isfinite(guardBandHalfDepth) ||
            !std::isfinite(guardBandMinimum) ||
            !std::isfinite(guardBandMaximum))
        {
            return decision;
        }

        // Match UE's inclusive 90% cached-Z guard: equality remains cached,
        // while the first value outside either boundary rebases.
        if (projectedInterval.minimum >= guardBandMinimum &&
            projectedInterval.maximum <= guardBandMaximum)
        {
            decision.selectedOrigin = committedOrigin;
            decision.retainedCommittedOrigin = true;
        }
        return decision;
    }

    [[nodiscard]] constexpr float
    GetNextSvsmCommittedLightDepthOrigin(
        float committedOrigin,
        float selectedOrigin,
        bool sparseFrameCommitted)
    {
        return sparseFrameCommitted
            ? selectedOrigin
            : committedOrigin;
    }

    struct SvsmPageCoordinate
    {
        int32_t x = 0;
        int32_t y = 0;

        constexpr bool operator==(const SvsmPageCoordinate& other) const
        {
            return x == other.x && y == other.y;
        }
    };

    using SvsmDirtyPageRectangleEncoding = std::array<uint32_t, 4>;

    struct SvsmDirtyPageRectangle
    {
        bool valid = false;
        SvsmPageCoordinate minimum;
        SvsmPageCoordinate maximum;
    };

    constexpr SvsmDirtyPageRectangleEncoding
    AccumulateSvsmDirtyPageRectangle(
        SvsmDirtyPageRectangleEncoding encoded,
        SvsmPageCoordinate localPage)
    {
        if (localPage.x < 0 || localPage.y < 0 ||
            localPage.x >= int32_t(SvsmPagesPerAxis) ||
            localPage.y >= int32_t(SvsmPagesPerAxis))
        {
            return encoded;
        }

        encoded[0] = std::max(
            encoded[0],
            SvsmPagesPerAxis - 1u - uint32_t(localPage.x));
        encoded[1] = std::max(encoded[1], uint32_t(localPage.x));
        encoded[2] = std::max(
            encoded[2],
            SvsmPagesPerAxis - 1u - uint32_t(localPage.y));
        encoded[3] = std::max(encoded[3], uint32_t(localPage.y));
        return encoded;
    }

    constexpr SvsmDirtyPageRectangle DecodeSvsmDirtyPageRectangle(
        const SvsmDirtyPageRectangleEncoding& encoded)
    {
        const bool empty =
            encoded[0] == 0u && encoded[1] == 0u &&
            encoded[2] == 0u && encoded[3] == 0u;
        if (empty ||
            encoded[0] >= SvsmPagesPerAxis ||
            encoded[1] >= SvsmPagesPerAxis ||
            encoded[2] >= SvsmPagesPerAxis ||
            encoded[3] >= SvsmPagesPerAxis)
        {
            return {};
        }

        const SvsmPageCoordinate minimum = {
            int32_t(SvsmPagesPerAxis - 1u - encoded[0]),
            int32_t(SvsmPagesPerAxis - 1u - encoded[2])
        };
        const SvsmPageCoordinate maximum = {
            int32_t(encoded[1]),
            int32_t(encoded[3])
        };
        if (minimum.x > maximum.x || minimum.y > maximum.y)
            return {};
        return { true, minimum, maximum };
    }

    constexpr SvsmDirtyPageRectangle IntersectSvsmPageRectangles(
        const SvsmDirtyPageRectangle& first,
        const SvsmDirtyPageRectangle& second)
    {
        if (!first.valid || !second.valid)
            return {};
        const SvsmPageCoordinate minimum = {
            std::max(first.minimum.x, second.minimum.x),
            std::max(first.minimum.y, second.minimum.y)
        };
        const SvsmPageCoordinate maximum = {
            std::min(first.maximum.x, second.maximum.x),
            std::min(first.maximum.y, second.maximum.y)
        };
        if (minimum.x > maximum.x || minimum.y > maximum.y)
            return {};
        return { true, minimum, maximum };
    }

    constexpr SvsmPageCoordinate UnpackSvsmPacketPageCoordinate(
        uint32_t packed)
    {
        return {
            int32_t(packed & 0xffu),
            int32_t((packed >> 8u) & 0xffu)
        };
    }

    struct SvsmScheduledPageTileMask
    {
        uint32_t generation = 0u;
        uint32_t anyLow = 0u;
        uint32_t anyHigh = 0u;
        uint32_t staticLow = 0u;
        uint32_t staticHigh = 0u;
    };

    enum class SvsmScheduledPageTileMaskQuery : uint32_t
    {
        FailOpen,
        Reject,
        Positive
    };

    constexpr SvsmScheduledPageTileMask
    AddSvsmScheduledPageTile(
        SvsmScheduledPageTileMask mask,
        SvsmPageCoordinate tablePage,
        SvsmPageCoordinate pageTableOffset,
        bool staticPage)
    {
        if (tablePage.x < 0 || tablePage.y < 0 ||
            tablePage.x >= int32_t(SvsmPagesPerAxis) ||
            tablePage.y >= int32_t(SvsmPagesPerAxis))
        {
            mask.generation = 0u;
            return mask;
        }

        const SvsmPageCoordinate localPage = {
            WrapSvsmPageCoordinate(
                int64_t(tablePage.x) - int64_t(pageTableOffset.x)),
            WrapSvsmPageCoordinate(
                int64_t(tablePage.y) - int64_t(pageTableOffset.y))
        };
        const uint32_t tile =
            uint32_t(localPage.y) / SvsmScheduledTilePageWidth *
                SvsmScheduledTilesPerAxis +
            uint32_t(localPage.x) / SvsmScheduledTilePageWidth;
        const uint32_t bit = 1u << (tile & 31u);
        uint32_t& any = tile < 32u ? mask.anyLow : mask.anyHigh;
        any |= bit;
        if (staticPage)
        {
            uint32_t& staticMask =
                tile < 32u ? mask.staticLow : mask.staticHigh;
            staticMask |= bit;
        }
        return mask;
    }

    constexpr SvsmScheduledPageTileMaskQuery
    QuerySvsmScheduledPageTileMask(
        const SvsmScheduledPageTileMask& mask,
        uint32_t expectedGeneration,
        uint32_t packedMinimum,
        uint32_t packedMaximum,
        bool staticCaster,
        bool pairedStaticDynamicDepth)
    {
        if (expectedGeneration == 0u ||
            mask.generation != expectedGeneration ||
            (mask.staticLow & ~mask.anyLow) != 0u ||
            (mask.staticHigh & ~mask.anyHigh) != 0u ||
            packedMinimum == SvsmInvalidPacketPageBounds ||
            packedMinimum == SvsmEmptyPacketPageBounds ||
            packedMaximum == SvsmInvalidPacketPageBounds ||
            packedMaximum == SvsmEmptyPacketPageBounds)
        {
            return SvsmScheduledPageTileMaskQuery::FailOpen;
        }

        const SvsmPageCoordinate minimum =
            UnpackSvsmPacketPageCoordinate(packedMinimum);
        const SvsmPageCoordinate maximum =
            UnpackSvsmPacketPageCoordinate(packedMaximum);
        if (minimum.x < 0 || minimum.y < 0 ||
            maximum.x < minimum.x || maximum.y < minimum.y ||
            maximum.x >= int32_t(SvsmPagesPerAxis) ||
            maximum.y >= int32_t(SvsmPagesPerAxis))
        {
            return SvsmScheduledPageTileMaskQuery::FailOpen;
        }

        const uint32_t minimumTileX =
            uint32_t(minimum.x) / SvsmScheduledTilePageWidth;
        const uint32_t maximumTileX =
            uint32_t(maximum.x) / SvsmScheduledTilePageWidth;
        const uint32_t minimumTileY =
            uint32_t(minimum.y) / SvsmScheduledTilePageWidth;
        const uint32_t maximumTileY =
            uint32_t(maximum.y) / SvsmScheduledTilePageWidth;
        const uint32_t xMask =
            ((1u << (maximumTileX + 1u)) - 1u) &
            ~((1u << minimumTileX) - 1u);
        uint32_t queryLow = 0u;
        uint32_t queryHigh = 0u;
        for (uint32_t tileY = minimumTileY;
            tileY <= maximumTileY;
            ++tileY)
        {
            if (tileY < 4u)
                queryLow |= xMask << (tileY * 8u);
            else
                queryHigh |= xMask << ((tileY - 4u) * 8u);
        }

        const bool useStatic =
            staticCaster && pairedStaticDynamicDepth;
        const uint32_t low = useStatic
            ? mask.staticLow
            : mask.anyLow;
        const uint32_t high = useStatic
            ? mask.staticHigh
            : mask.anyHigh;
        return ((low & queryLow) | (high & queryHigh)) == 0u
            ? SvsmScheduledPageTileMaskQuery::Reject
            : SvsmScheduledPageTileMaskQuery::Positive;
    }

    struct SvsmReceiverPageMask
    {
        uint32_t generation = 0u;
        std::array<
            uint32_t,
            SvsmReceiverPageMaskQuadrantCount> quadrants{};
    };

    enum class SvsmReceiverPageMaskQuery : uint32_t
    {
        FailOpen,
        Reject,
        Positive
    };

    [[nodiscard]] constexpr uint32_t
    GetSvsmReceiverPageMaskWordBase(uint32_t virtualOwner)
    {
        return virtualOwner * SvsmReceiverPageMaskWordsPerPage;
    }

    [[nodiscard]] constexpr uint32_t
    BuildSvsmReceiverPageMaskQuadrantRectangle(
        uint32_t minimumX,
        uint32_t minimumY,
        uint32_t maximumX,
        uint32_t maximumY)
    {
        if (minimumX > maximumX ||
            minimumY > maximumY ||
            maximumX >= SvsmReceiverPageMaskQuadrantCellAxis ||
            maximumY >= SvsmReceiverPageMaskQuadrantCellAxis)
        {
            return 0u;
        }

        uint32_t mask = 0u;
        for (uint32_t y = minimumY; y <= maximumY; ++y)
        {
            for (uint32_t x = minimumX; x <= maximumX; ++x)
            {
                mask |= 1u << (
                    y * SvsmReceiverPageMaskQuadrantCellAxis + x);
            }
        }
        return mask;
    }

    constexpr SvsmReceiverPageMask AddSvsmReceiverPageMaskRectangle(
        SvsmReceiverPageMask mask,
        uint32_t generation,
        uint32_t minimumCellX,
        uint32_t minimumCellY,
        uint32_t maximumCellX,
        uint32_t maximumCellY)
    {
        if (generation == 0u ||
            minimumCellX > maximumCellX ||
            minimumCellY > maximumCellY ||
            maximumCellX >= SvsmReceiverPageMaskAxis ||
            maximumCellY >= SvsmReceiverPageMaskAxis)
        {
            mask.generation = 0u;
            return mask;
        }

        for (uint32_t quadrantY = 0u;
            quadrantY < SvsmReceiverPageMaskQuadrantAxis;
            ++quadrantY)
        {
            for (uint32_t quadrantX = 0u;
                quadrantX < SvsmReceiverPageMaskQuadrantAxis;
                ++quadrantX)
            {
                const uint32_t quadrantMinimumX =
                    quadrantX *
                    SvsmReceiverPageMaskQuadrantCellAxis;
                const uint32_t quadrantMinimumY =
                    quadrantY *
                    SvsmReceiverPageMaskQuadrantCellAxis;
                const uint32_t quadrantMaximumX =
                    quadrantMinimumX +
                    SvsmReceiverPageMaskQuadrantCellAxis - 1u;
                const uint32_t quadrantMaximumY =
                    quadrantMinimumY +
                    SvsmReceiverPageMaskQuadrantCellAxis - 1u;
                const uint32_t intersectionMinimumX = std::max(
                    minimumCellX, quadrantMinimumX);
                const uint32_t intersectionMinimumY = std::max(
                    minimumCellY, quadrantMinimumY);
                const uint32_t intersectionMaximumX = std::min(
                    maximumCellX, quadrantMaximumX);
                const uint32_t intersectionMaximumY = std::min(
                    maximumCellY, quadrantMaximumY);
                if (intersectionMinimumX > intersectionMaximumX ||
                    intersectionMinimumY > intersectionMaximumY)
                {
                    continue;
                }

                const uint32_t quadrant =
                    quadrantY * SvsmReceiverPageMaskQuadrantAxis +
                    quadrantX;
                mask.quadrants[quadrant] |=
                    BuildSvsmReceiverPageMaskQuadrantRectangle(
                        intersectionMinimumX -
                            quadrantMinimumX,
                        intersectionMinimumY -
                            quadrantMinimumY,
                        intersectionMaximumX -
                            quadrantMinimumX,
                        intersectionMaximumY -
                            quadrantMinimumY);
            }
        }
        mask.generation = generation;
        return mask;
    }

    [[nodiscard]] constexpr SvsmReceiverPageMaskQuery
    QuerySvsmReceiverPageMask(
        const SvsmReceiverPageMask& mask,
        uint32_t expectedGeneration,
        uint32_t minimumCellX,
        uint32_t minimumCellY,
        uint32_t maximumCellX,
        uint32_t maximumCellY)
    {
        if (expectedGeneration == 0u ||
            mask.generation != expectedGeneration ||
            minimumCellX > maximumCellX ||
            minimumCellY > maximumCellY ||
            maximumCellX >= SvsmReceiverPageMaskAxis ||
            maximumCellY >= SvsmReceiverPageMaskAxis)
        {
            return SvsmReceiverPageMaskQuery::FailOpen;
        }
        for (uint32_t word : mask.quadrants)
        {
            if ((word & 0xffff0000u) != 0u)
                return SvsmReceiverPageMaskQuery::FailOpen;
        }

        for (uint32_t quadrantY = 0u;
            quadrantY < SvsmReceiverPageMaskQuadrantAxis;
            ++quadrantY)
        {
            for (uint32_t quadrantX = 0u;
                quadrantX < SvsmReceiverPageMaskQuadrantAxis;
                ++quadrantX)
            {
                const uint32_t quadrantMinimumX =
                    quadrantX *
                    SvsmReceiverPageMaskQuadrantCellAxis;
                const uint32_t quadrantMinimumY =
                    quadrantY *
                    SvsmReceiverPageMaskQuadrantCellAxis;
                const uint32_t quadrantMaximumX =
                    quadrantMinimumX +
                    SvsmReceiverPageMaskQuadrantCellAxis - 1u;
                const uint32_t quadrantMaximumY =
                    quadrantMinimumY +
                    SvsmReceiverPageMaskQuadrantCellAxis - 1u;
                const uint32_t intersectionMinimumX = std::max(
                    minimumCellX, quadrantMinimumX);
                const uint32_t intersectionMinimumY = std::max(
                    minimumCellY, quadrantMinimumY);
                const uint32_t intersectionMaximumX = std::min(
                    maximumCellX, quadrantMaximumX);
                const uint32_t intersectionMaximumY = std::min(
                    maximumCellY, quadrantMaximumY);
                if (intersectionMinimumX > intersectionMaximumX ||
                    intersectionMinimumY > intersectionMaximumY)
                {
                    continue;
                }

                const uint32_t quadrant =
                    quadrantY * SvsmReceiverPageMaskQuadrantAxis +
                    quadrantX;
                const uint32_t queryMask =
                    BuildSvsmReceiverPageMaskQuadrantRectangle(
                        intersectionMinimumX -
                            quadrantMinimumX,
                        intersectionMinimumY -
                            quadrantMinimumY,
                        intersectionMaximumX -
                            quadrantMinimumX,
                        intersectionMaximumY -
                            quadrantMinimumY);
                if ((mask.quadrants[quadrant] & queryMask) != 0u)
                    return SvsmReceiverPageMaskQuery::Positive;
            }
        }

        return SvsmReceiverPageMaskQuery::Reject;
    }

    constexpr SvsmPacketPageRectangle
    ResolveSvsmScatterPacketRectangle(
        uint32_t runtimeState,
        uint32_t packedMinimum,
        uint32_t packedMaximum)
    {
        if ((runtimeState & SvsmPacketPageRuntimeCountMask) == 0u)
        {
            return {
                SvsmEmptyPacketPageBounds,
                SvsmEmptyPacketPageBounds
            };
        }

        const SvsmPageCoordinate minimum =
            UnpackSvsmPacketPageCoordinate(packedMinimum);
        const SvsmPageCoordinate maximum =
            UnpackSvsmPacketPageCoordinate(packedMaximum);
        const bool valid =
            packedMinimum != SvsmInvalidPacketPageBounds &&
            packedMinimum != SvsmEmptyPacketPageBounds &&
            packedMaximum != SvsmInvalidPacketPageBounds &&
            packedMaximum != SvsmEmptyPacketPageBounds &&
            minimum.x >= 0 && minimum.y >= 0 &&
            maximum.x >= minimum.x &&
            maximum.y >= minimum.y &&
            maximum.x < int32_t(SvsmPagesPerAxis) &&
            maximum.y < int32_t(SvsmPagesPerAxis);
        if (!valid)
        {
            return {
                PackSvsmPacketPageCoordinate(0u, 0u),
                PackSvsmPacketPageCoordinate(
                    SvsmPagesPerAxis - 1u,
                    SvsmPagesPerAxis - 1u)
            };
        }
        return { packedMinimum, packedMaximum };
    }

    constexpr uint32_t GetSvsmPacketPageListCapacity(
        uint32_t packedMinimum,
        uint32_t packedMaximum)
    {
        if (packedMinimum == SvsmInvalidPacketPageBounds ||
            packedMinimum == SvsmEmptyPacketPageBounds)
        {
            return 0u;
        }

        const SvsmPageCoordinate minimum =
            UnpackSvsmPacketPageCoordinate(packedMinimum);
        const SvsmPageCoordinate maximum =
            UnpackSvsmPacketPageCoordinate(packedMaximum);
        if (minimum.x < 0 || minimum.y < 0 ||
            maximum.x < minimum.x || maximum.y < minimum.y ||
            maximum.x >= int32_t(SvsmPagesPerAxis) ||
            maximum.y >= int32_t(SvsmPagesPerAxis))
        {
            return 0u;
        }
        return uint32_t(maximum.x - minimum.x + 1) *
            uint32_t(maximum.y - minimum.y + 1);
    }

    constexpr bool ShouldScanSvsmPacketRectangleDirectly(
        bool directScanEnabled,
        uint32_t rectanglePageCount,
        uint32_t levelRenderPageCount)
    {
        // A direct page-table probe needs more state reads than one compact
        // list entry. Require at least a two-to-one reduction in candidate
        // pages so large casters and already-small dirty lists retain the
        // original compact-list scan.
        return directScanEnabled &&
            rectanglePageCount > 0u &&
            uint64_t(rectanglePageCount) * 2u <=
                uint64_t(levelRenderPageCount);
    }

    constexpr uint32_t GetSvsmDirtyPageScatterInstanceCount(
        bool scatterEnabled,
        uint32_t scheduledPageCount)
    {
        return scatterEnabled
            ? (scheduledPageCount == 0u ? 0u : 1u)
            : scheduledPageCount;
    }

    constexpr bool IsSvsmPacketPageInsideBounds(
        uint32_t packedMinimum,
        uint32_t packedMaximum,
        SvsmPageCoordinate tablePage,
        SvsmPageCoordinate pageTableOffset)
    {
        if (packedMinimum == SvsmInvalidPacketPageBounds ||
            packedMinimum == SvsmEmptyPacketPageBounds)
        {
            return false;
        }
        const SvsmPageCoordinate minimum =
            UnpackSvsmPacketPageCoordinate(packedMinimum);
        const SvsmPageCoordinate maximum =
            UnpackSvsmPacketPageCoordinate(packedMaximum);
        const SvsmPageCoordinate localPage = {
            WrapSvsmPageCoordinate(
                int64_t(tablePage.x) - int64_t(pageTableOffset.x)),
            WrapSvsmPageCoordinate(
                int64_t(tablePage.y) - int64_t(pageTableOffset.y))
        };
        return localPage.x >= minimum.x &&
            localPage.y >= minimum.y &&
            localPage.x <= maximum.x &&
            localPage.y <= maximum.y;
    }

    constexpr SvsmPageCoordinate WrapSvsmPageCoordinate(
        SvsmPageCoordinate coordinate)
    {
        return {
            WrapSvsmPageCoordinate(coordinate.x),
            WrapSvsmPageCoordinate(coordinate.y)
        };
    }

    constexpr SvsmPageCoordinate SvsmPacketTablePageFromLocalPage(
        SvsmPageCoordinate localPage,
        SvsmPageCoordinate pageTableOffset)
    {
        return {
            WrapSvsmPageCoordinate(
                int64_t(localPage.x) + int64_t(pageTableOffset.x)),
            WrapSvsmPageCoordinate(
                int64_t(localPage.y) + int64_t(pageTableOffset.y))
        };
    }

    struct SvsmScatterVirtualTexelAddress
    {
        bool valid = false;
        SvsmPageCoordinate localPage;
        SvsmPageCoordinate pageTexel;
        SvsmPageCoordinate tablePage;
        uint32_t owner = SvsmInvalidPhysicalPage;
    };

    constexpr SvsmScatterVirtualTexelAddress
    GetSvsmScatterVirtualTexelAddress(
        uint32_t virtualTexelX,
        uint32_t virtualTexelY,
        SvsmPageCoordinate pageTableOffset,
        uint32_t clipmap)
    {
        if (virtualTexelX >= SvsmVirtualResolution ||
            virtualTexelY >= SvsmVirtualResolution ||
            clipmap >= SvsmClipmapCount)
        {
            return {};
        }

        const SvsmPageCoordinate localPage = {
            int32_t(virtualTexelX / SvsmPageSize),
            int32_t(virtualTexelY / SvsmPageSize)
        };
        const SvsmPageCoordinate pageTexel = {
            int32_t(virtualTexelX % SvsmPageSize),
            int32_t(virtualTexelY % SvsmPageSize)
        };
        const SvsmPageCoordinate tablePage =
            SvsmPacketTablePageFromLocalPage(
                localPage, pageTableOffset);
        return {
            true,
            localPage,
            pageTexel,
            tablePage,
            clipmap * SvsmPagesPerClipmap +
                uint32_t(tablePage.y) * SvsmPagesPerAxis +
                uint32_t(tablePage.x)
        };
    }

    constexpr uint32_t EncodeSvsmVirtualPageOwner(
        SvsmPageCoordinate tablePage,
        uint32_t clipmap)
    {
        return clipmap * SvsmPagesPerClipmap +
            uint32_t(tablePage.y) * SvsmPagesPerAxis +
            uint32_t(tablePage.x);
    }

    constexpr bool IsSvsmPacketPageScheduledForRender(
        uint32_t packedPageMetadata,
        uint32_t physicalPageCount,
        uint32_t expectedOwner,
        uint32_t physicalOwner,
        uint32_t scheduledOwner)
    {
        const SvsmPageMetadata page =
            UnpackSvsmPageMetadata(packedPageMetadata);
        return page.resident &&
            page.dirty &&
            page.physicalPage < physicalPageCount &&
            physicalOwner == expectedOwner &&
            scheduledOwner == expectedOwner;
    }

    constexpr SvsmPageCoordinate SvsmPageTableOffsetForRenderOrigin(
        SvsmPageCoordinate renderOriginInPages)
    {
        const int32_t halfPageTable =
            int32_t(SvsmPagesPerAxis / 2u);
        return {
            WrapSvsmPageCoordinate(
                int64_t(renderOriginInPages.x) - halfPageTable),
            WrapSvsmPageCoordinate(
                -int64_t(renderOriginInPages.y) - halfPageTable)
        };
    }

    constexpr SvsmPageCoordinate SvsmPageTableDeltaForRenderOrigins(
        SvsmPageCoordinate currentRenderOriginInPages,
        SvsmPageCoordinate previousRenderOriginInPages)
    {
        return {
            SaturateSvsmRenderOriginDelta(
                int64_t(currentRenderOriginInPages.x) -
                    int64_t(previousRenderOriginInPages.x)),
            SaturateSvsmRenderOriginDelta(
                -(int64_t(currentRenderOriginInPages.y) -
                    int64_t(previousRenderOriginInPages.y)))
        };
    }

    constexpr bool IsSvsmTablePageNewlyExposed(
        SvsmPageCoordinate tablePage,
        SvsmPageCoordinate currentTableOffset,
        SvsmPageCoordinate renderOriginDelta)
    {
        const int32_t pageCount = int32_t(SvsmPagesPerAxis);
        if (renderOriginDelta.x <= -pageCount ||
            renderOriginDelta.x >= pageCount ||
            renderOriginDelta.y <= -pageCount ||
            renderOriginDelta.y >= pageCount)
        {
            return true;
        }

        const SvsmPageCoordinate localPage = {
            WrapSvsmPageCoordinate(
                int64_t(tablePage.x) - int64_t(currentTableOffset.x)),
            WrapSvsmPageCoordinate(
                int64_t(tablePage.y) - int64_t(currentTableOffset.y))
        };
        const bool exposedX = renderOriginDelta.x > 0
            ? localPage.x >= pageCount - renderOriginDelta.x
            : (renderOriginDelta.x < 0 &&
                localPage.x < -renderOriginDelta.x);
        const bool exposedY = renderOriginDelta.y > 0
            ? localPage.y >= pageCount - renderOriginDelta.y
            : (renderOriginDelta.y < 0 &&
                localPage.y < -renderOriginDelta.y);
        return exposedX || exposedY;
    }

    constexpr SvsmPageCoordinate AdvanceSvsmWrapOffset(
        SvsmPageCoordinate previousOffset,
        SvsmPageCoordinate renderOriginDeltaInPages)
    {
        return {
            WrapSvsmPageCoordinate(
                int64_t(previousOffset.x) +
                    int64_t(renderOriginDeltaInPages.x)),
            WrapSvsmPageCoordinate(
                int64_t(previousOffset.y) +
                    int64_t(renderOriginDeltaInPages.y))
        };
    }

    inline float SnapSvsmRenderOrigin(float coordinate, float pageWorldSize)
    {
        if (!(pageWorldSize > 0.f) || !std::isfinite(coordinate))
            return 0.f;
        return std::floor(coordinate / pageWorldSize) * pageWorldSize;
    }

    inline int32_t SelectFinestSvsmClipmap(
        float lightSpaceX,
        float lightSpaceY,
        float firstClipmapExtent,
        SvsmResolutionBias bias)
    {
        if (!(firstClipmapExtent > 0.f) ||
            !std::isfinite(lightSpaceX) ||
            !std::isfinite(lightSpaceY))
        {
            return -1;
        }

        const float maximumCoordinate =
            std::max(std::abs(lightSpaceX), std::abs(lightSpaceY));
        const uint32_t firstLevel =
            GetSvsmFirstClipmapLevel(bias);
        float halfExtent = firstClipmapExtent * 0.5f *
            float(1u << firstLevel);
        for (uint32_t level = firstLevel;
            level < SvsmClipmapCount;
            ++level)
        {
            if (maximumCoordinate <= halfExtent)
                return int32_t(level);
            halfExtent *= 2.f;
        }
        return -1;
    }

    inline uint32_t EncodeSvsmReverseDepth(float depth)
    {
        const float finiteDepth = std::isfinite(depth)
            ? std::clamp(depth, 0.f, 1.f)
            : 0.f;
        union
        {
            float value;
            uint32_t bits;
        } encoded{ finiteDepth };
        return encoded.bits;
    }

    inline uint32_t WriteSvsmReverseDepth(
        uint32_t currentDepth,
        float candidateDepth)
    {
        return std::max(currentDepth, EncodeSvsmReverseDepth(candidateDepth));
    }

    inline float DecodeSvsmReverseDepth(uint32_t depth)
    {
        union
        {
            uint32_t bits;
            float value;
        } decoded{ depth };
        return decoded.value;
    }

    constexpr uint32_t
    GetSvsmPerPixelReceiverRequestedLevelMask(
        uint32_t selectedLevel)
    {
        if (selectedLevel >= SvsmClipmapCount)
            return 0u;
        return (1u << selectedLevel) |
            (1u << (SvsmClipmapCount - 1u));
    }

    constexpr uint32_t
    GetSvsmPerPixelReceiverMaskOnlyFallbackLevelMask(
        uint32_t selectedLevel)
    {
        if (selectedLevel >= SvsmClipmapCount)
            return 0u;
        uint32_t levels = 0u;
        for (uint32_t level = selectedLevel + 1u;
            level + 1u < SvsmClipmapCount;
            ++level)
        {
            levels |= 1u << level;
        }
        return levels;
    }

    constexpr uint32_t
    BuildSvsmReceiverFallbackCoverageLevelMask(
        uint32_t firstLevel,
        const std::array<bool, SvsmClipmapCount>&
            geometricallyCoveredLevels)
    {
        uint32_t levels = 0u;
        for (uint32_t level = firstLevel;
            level < SvsmClipmapCount;
            ++level)
        {
            if (geometricallyCoveredLevels[level])
                levels |= 1u << level;
        }
        return levels;
    }

    constexpr uint32_t SelectSvsmFallbackLevel(
        uint32_t firstLevel,
        const std::array<bool, SvsmClipmapCount>& validLevels)
    {
        for (uint32_t level = firstLevel;
            level < SvsmClipmapCount;
            ++level)
        {
            if (validLevels[level])
                return level;
        }
        return SvsmClipmapCount;
    }

    constexpr bool IsSvsmFilterFootprintInsidePage(
        uint32_t texelX,
        uint32_t texelY,
        uint32_t radius)
    {
        const uint32_t pageX = texelX % SvsmPageSize;
        const uint32_t pageY = texelY % SvsmPageSize;
        return pageX >= radius &&
            pageY >= radius &&
            pageX + radius < SvsmPageSize &&
            pageY + radius < SvsmPageSize;
    }

    struct SvsmBatchedDrawStateKey
    {
        uintptr_t bufferGroup = 0u;
        uintptr_t material = 0u;
        uint32_t cullMode = 0u;
        bool alphaTested = false;

        constexpr bool operator==(
            const SvsmBatchedDrawStateKey& other) const
        {
            return bufferGroup == other.bufferGroup &&
                material == other.material &&
                cullMode == other.cullMode &&
                alphaTested == other.alphaTested;
        }
    };

    struct SvsmPacketStateSortKey
    {
        uintptr_t bufferGroup = 0u;
        uintptr_t material = 0u;
        uint32_t cullMode = 0u;
        bool alphaTested = false;
        bool batchable = false;

        constexpr bool operator==(
            const SvsmPacketStateSortKey& other) const
        {
            return bufferGroup == other.bufferGroup &&
                material == other.material &&
                cullMode == other.cullMode &&
                alphaTested == other.alphaTested &&
                batchable == other.batchable;
        }
    };

    [[nodiscard]] constexpr SvsmPacketStateSortKey
    MakeSvsmPacketStateSortKey(
        const SvsmBatchedDrawStateKey& stateKey,
        uintptr_t exactMaterial,
        bool batchable)
    {
        // Batched opaque depth never reads material resources. Alpha-tested
        // and nonbatchable fallback packets retain exact material identity.
        return {
            stateKey.bufferGroup,
            stateKey.alphaTested || !batchable
                ? exactMaterial
                : 0u,
            stateKey.cullMode,
            stateKey.alphaTested,
            batchable
        };
    }

    [[nodiscard]] constexpr bool IsSvsmPacketStateSortKeyLess(
        const SvsmPacketStateSortKey& left,
        const SvsmPacketStateSortKey& right)
    {
        if (left.bufferGroup != right.bufferGroup)
            return left.bufferGroup < right.bufferGroup;
        if (left.alphaTested != right.alphaTested)
            return left.alphaTested < right.alphaTested;
        if (left.cullMode != right.cullMode)
            return left.cullMode < right.cullMode;
        if (left.batchable != right.batchable)
            return left.batchable && !right.batchable;
        return left.material < right.material;
    }

    [[nodiscard]] constexpr bool CanMergeSvsmPacketStateGroup(
        const SvsmBatchedDrawStateKey& left,
        bool leftBatchable,
        const SvsmBatchedDrawStateKey& right,
        bool rightBatchable)
    {
        return leftBatchable &&
            rightBatchable &&
            left == right;
    }

    [[nodiscard]] constexpr SvsmBatchedDrawStateKey
    MakeSvsmBatchedDrawStateKey(
        uintptr_t bufferGroup,
        uintptr_t material,
        uint32_t cullMode,
        bool alphaTested)
    {
        // Opaque shadow depth never reads material constants or textures, so
        // one valid binding can serve every consecutive opaque packet. Alpha
        // tested packets still require the exact material and texture set.
        return {
            bufferGroup,
            alphaTested ? material : 0u,
            cullMode,
            alphaTested
        };
    }

    // Batched indirect draws carry the vertex offset in the signed
    // base-vertex field. On the reference path, the object index is multiplied
    // by the physical pool size in startInstanceLocation while the local
    // SV_InstanceID identifies the compact page. Packet-page culling instead
    // carries the stable packet index there and reads the original object
    // index from metadata. Check the complete reference encoding range here.
    constexpr bool CanEncodeSvsmBatchedDraw(
        uint32_t vertexOffset,
        uint32_t objectIndex,
        uint32_t physicalPageCount)
    {
        if (vertexOffset >
                uint32_t(std::numeric_limits<int32_t>::max()) ||
            physicalPageCount == 0u)
        {
            return false;
        }

        const uint64_t encodedObject =
            uint64_t(objectIndex) * physicalPageCount;
        const uint64_t maximumInstance =
            encodedObject + physicalPageCount - 1u;
        return maximumInstance <=
            std::numeric_limits<uint32_t>::max();
    }

    constexpr int32_t EncodeSvsmBatchedBaseVertex(
        uint32_t vertexOffset)
    {
        return int32_t(vertexOffset);
    }

    constexpr uint32_t EncodeSvsmBatchedStartInstance(
        uint32_t objectIndex,
        uint32_t physicalPageCount)
    {
        return uint32_t(
            uint64_t(objectIndex) * physicalPageCount);
    }

    constexpr uint32_t DecodeSvsmBatchedObjectIndex(
        uint32_t encodedStartInstance,
        uint32_t physicalPageCount)
    {
        return physicalPageCount == 0u
            ? 0u
            : encodedStartInstance / physicalPageCount;
    }

    constexpr bool IsSvsmBatchedStartInstanceEncodingValid(
        uint32_t encodedStartInstance,
        uint32_t physicalPageCount)
    {
        return physicalPageCount != 0u &&
            encodedStartInstance % physicalPageCount == 0u;
    }

    template <typename Matrix>
    [[nodiscard]] inline Matrix BuildSvsmReceiverTransform(
        bool precomposedClipmapTransformsEnabled,
        const Matrix& cameraClipToWorld,
        const Matrix& worldToClip)
    {
        // Donut shaders use row vectors. The optimized permutation keeps the
        // reconstructed camera position homogeneous, so its projective W
        // cancels during the same clipmap divide as the world-space reference.
        return precomposedClipmapTransformsEnabled
            ? cameraClipToWorld * worldToClip
            : worldToClip;
    }

    template <typename Matrix>
    struct SvsmClipmapTransformPair
    {
        Matrix worldToClip;
        Matrix receiverToClip;
    };

    template <typename Matrix>
    [[nodiscard]] inline SvsmClipmapTransformPair<Matrix>
        BuildSvsmClipmapTransformPair(
            bool precomposedClipmapTransformsEnabled,
            const Matrix& cameraClipToWorld,
            const Matrix& worldToClip)
    {
        // Caster raster always consumes world positions. Receiver marking and
        // resolve optionally consume homogeneous camera-clip positions. Keep
        // those semantic domains separate even though the reference path uses
        // the same world-to-clip matrix for both.
        return {
            worldToClip,
            BuildSvsmReceiverTransform(
                precomposedClipmapTransformsEnabled,
                cameraClipToWorld,
                worldToClip)
        };
    }

    struct SparseVirtualShadowMapSettings
    {
        bool enabled = false;
        SvsmPreset preset = SvsmPreset::Quality;
        SvsmMode mode = SvsmMode::SparseCached;
        SvsmMarkingMode markingMode = SvsmMarkingMode::PerPixel;
        SvsmFilterMode filterMode = SvsmFilterMode::ManualPageSafe;
        SvsmFilterKernel filterKernel = SvsmFilterKernel::BilinearPcf;
        SvsmPoissonOrdering poissonOrdering =
            SvsmPoissonOrdering::BalancedProgressive;
        SvsmTapCount tapCount = SvsmTapCount::Eight;
        SvsmResolutionBias resolutionBias = SvsmResolutionBias::Zero;
        SvsmDebugView debugView = SvsmDebugView::None;

        float firstClipmapExtent = 20.f;
        float maximumLightDepth = 200.f;
        uint32_t physicalPageCount = 4096u;
        // Finite values cap dirty renders across fine levels 0 through 4.
        // The coarsest level remains exempt unless the independent safety
        // toggle below opts it into the same shared reservation.
        uint32_t pageRenderBudget = std::numeric_limits<uint32_t>::max();
        bool coarsestPageRenderBudgetEnabled = false;

        bool perPixelMarkingDedupeEnabled = true;
        bool cachingEnabled = true;
        bool lightDepthOriginGuardBandEnabled = true;
        float lightDepthOriginGuardBandFraction = 0.9f;
        bool staticPageRequestReuseEnabled = true;
        bool allocationBudgetSaturationEarlyOutEnabled = true;
        bool finiteBudgetStaticDrainEnabled = true;
        bool staticVisibilityCachingEnabled = true;
        bool sceneStateCachingEnabled = true;
        bool casterOnlySceneRevisionEnabled = true;
        bool renderPacketCachingEnabled = true;
        bool sharedClipmapPacketBuilderEnabled = true;
        bool persistentCasterSourceCachingEnabled = true;
        bool opaqueRasterSpecializationEnabled = true;
        bool leanAlphaTestedBindingsEnabled = true;
        bool pairedStaticDynamicDepthEnabled = true;
        bool deferredStaticDepthMergeEnabled = true;
        bool movingLightUncachedEnabled = true;
        bool retainPhysicalMappingsOnContentInvalidationEnabled = true;
        bool movingLightLodBiasEnabled = false;
        SvsmResolutionBias movingLightResolutionBias =
            SvsmResolutionBias::Zero;
        // The discrete global clipmap bias stays conservative until each
        // integer threshold is crossed. The continuous recovery factor remains
        // available in timings and is the input for the later receiver-distance
        // clamp, where recovery can vary spatially without a global pop.
        uint32_t movingLightLodRecoveryFrames =
            SvsmDefaultMovingLightLodRecoveryFrames;
        // Quality keeps exact dense/sparse clipmap selection until a
        // transition fade is available. Performance-oriented presets enable
        // this independent sparse optimization below.
        bool receiverDistanceMipClampEnabled = false;
        float receiverDistanceMipClampStartScale = 1.5f;
        uint32_t receiverDistanceMipClampMaximumLevel =
            SvsmMaximumReceiverDistanceMipClampLevel;
        // Move the distance thresholds continuously while the light recovers
        // instead of globally discarding an entire fine clipmap. Nearby
        // receivers keep their configured quality and the discrete reference
        // path remains available by disabling this toggle.
        bool movingLightContinuousReceiverBiasEnabled = true;
        bool localizedInvalidationEnabled = true;
        // Project the original object box through its transform instead of
        // inflating it to a world-space AABB, and reject boxes wholly outside
        // the clipmap reverse-Z range. Invalid inputs retain the conservative
        // full-invalidation fallback.
        bool tightLocalizedInvalidationBoundsEnabled = true;
        bool adaptiveCasterCacheClassificationEnabled = true;
        SvsmObjectInvalidationMode defaultObjectInvalidationMode =
            SvsmObjectInvalidationMode::Auto;
        bool gpuGatedDrawSubmission = true;
        bool batchedDrawSubmissionEnabled = true;
        bool packetStateSortingEnabled = true;
        bool levelEmptyWorkSkipEnabled = true;
        bool packetPageCullingEnabled = true;
        bool hierarchicalScheduledPageMaskEnabled = true;
        bool receiverPageMaskCullingEnabled = true;
        bool staticDepthHierarchyCullingEnabled = true;
        float staticDepthHierarchyBias = 0.0002f;
        bool dirtyPageScatterRasterEnabled = false;
        bool scatterAlphaTestEarlyRejectEnabled = false;
        bool dirtyPageScatterAmplificationGuardEnabled = false;
        uint32_t dirtyPageScatterMaximumAmplification = 4u;
        bool packetRectangleDirectScanEnabled = true;
        bool recentPageEvictionGraceEnabled = true;
        bool precomposedClipmapTransformsEnabled = true;
        bool pageTranslationCachingEnabled = true;
        bool detailedGpuTimingEnabled = false;
        bool adaptiveFiltering = false;
    };

    inline void ApplySvsmFinePageRenderBudget(
        SparseVirtualShadowMapSettings& settings,
        uint32_t pageRenderBudget)
    {
        settings.pageRenderBudget = pageRenderBudget;
        // A finite motion budget limits fine detail only. The coarsest
        // current-light fallback must remain complete because dirty pages
        // from an incompatible light basis are deliberately unsampleable.
        settings.coarsestPageRenderBudgetEnabled = false;
    }

    [[nodiscard]] constexpr bool
    ShouldUseSvsmDeterministicFinePageBudget(
        uint32_t pageRenderBudget,
        uint32_t physicalPageCount,
        bool coarsestPageRenderBudgetEnabled)
    {
        // The unlimited/reference allocator remains fully parallel. A finite
        // fine-only budget needs stable page publication; otherwise atomic
        // arrival order changes both visible refinement and raster cost
        // between identical runs.
        return !coarsestPageRenderBudgetEnabled &&
            pageRenderBudget < physicalPageCount;
    }

    [[nodiscard]] constexpr uint32_t
    InterleaveSvsmDeterministicPageCoordinate(uint32_t coordinate)
    {
        coordinate &= 0x0000ffffu;
        coordinate =
            (coordinate | (coordinate << 8u)) & 0x00ff00ffu;
        coordinate =
            (coordinate | (coordinate << 4u)) & 0x0f0f0f0fu;
        coordinate =
            (coordinate | (coordinate << 2u)) & 0x33333333u;
        coordinate =
            (coordinate | (coordinate << 1u)) & 0x55555555u;
        return coordinate;
    }

    [[nodiscard]] constexpr uint32_t
    GetSvsmDeterministicFinePageOrderKey(
        uint32_t clipmapLevel,
        uint32_t localPageIndex)
    {
        if (clipmapLevel >= SvsmClipmapCount - 1u ||
            localPageIndex >= SvsmPagesPerClipmap)
        {
            return std::numeric_limits<uint32_t>::max();
        }

        const uint32_t localX =
            localPageIndex % SvsmPagesPerAxis;
        const uint32_t localY =
            localPageIndex / SvsmPagesPerAxis;
        const uint32_t center = SvsmPagesPerAxis / 2u;
        // Convert local coordinates back to the symmetric distance ordinals
        // decoded by HLSL: 32,31,33,30... -> 0,1,2,3... .
        const auto getDistanceOrdinal =
            [center](uint32_t coordinate) constexpr {
                return coordinate < center
                    ? (center - coordinate) * 2u - 1u
                    : (coordinate - center) * 2u;
            };
        const uint32_t centeredX =
            getDistanceOrdinal(localX);
        const uint32_t centeredY =
            getDistanceOrdinal(localY);
        const uint32_t centeredMorton =
            InterleaveSvsmDeterministicPageCoordinate(centeredX) |
            (InterleaveSvsmDeterministicPageCoordinate(centeredY) << 1u);
        return clipmapLevel * SvsmPagesPerClipmap +
            centeredMorton;
    }

    [[nodiscard]] constexpr bool ShouldUseSvsmShadowDrawLists(
        bool cachedShadowDrawListsEnabled,
        bool gpuGatedDrawSubmission)
    {
        return cachedShadowDrawListsEnabled ||
            gpuGatedDrawSubmission;
    }

    [[nodiscard]] constexpr bool
    ShouldUseSvsmOpaqueRasterSpecialization(
        bool opaqueRasterSpecializationEnabled,
        bool opaqueRasterSpecializationSupported,
        bool opaqueMaterial)
    {
        return opaqueRasterSpecializationEnabled &&
            opaqueRasterSpecializationSupported &&
            opaqueMaterial;
    }

    [[nodiscard]] constexpr bool
    ShouldUseSvsmScheduledPageTileMask(
        bool enabled,
        bool packetPageCullingActive,
        bool dirtyPageScatterRasterActive,
        bool resourcesAvailable,
        uint32_t generation)
    {
        (void)dirtyPageScatterRasterActive;
        return enabled &&
            packetPageCullingActive &&
            resourcesAvailable &&
            generation != 0u;
    }

    [[nodiscard]] constexpr bool
    ShouldUseSvsmReceiverPageMaskCulling(
        SvsmMode mode,
        bool enabled,
        bool packetPageCullingActive,
        bool cacheActive,
        bool pairedDepthActive,
        bool dirtyPageScatterRasterActive,
        bool pageRequestsRebuilt,
        bool resourcesAvailable,
        uint32_t generation)
    {
        // Receiver masks describe only the current camera request. They are
        // safe for an uncached merged-depth transaction, including scatter
        // raster, but cached paired static pages must remain complete for
        // later cameras.
        (void)dirtyPageScatterRasterActive;
        return mode != SvsmMode::DenseReference &&
            enabled &&
            packetPageCullingActive &&
            !cacheActive &&
            !pairedDepthActive &&
            pageRequestsRebuilt &&
            resourcesAvailable &&
            generation != 0u;
    }

    [[nodiscard]] constexpr bool
    ShouldUseSvsmStaticDepthHierarchyCulling(
        SvsmMode mode,
        bool enabled,
        bool packetPageCullingActive,
        bool pairedDepthActive,
        bool dirtyPageScatterRasterActive,
        bool resourcesAvailable)
    {
        return mode != SvsmMode::DenseReference &&
            enabled &&
            packetPageCullingActive &&
            pairedDepthActive &&
            !dirtyPageScatterRasterActive &&
            resourcesAvailable;
    }

    struct SvsmRasterSubmissionTransactionAction
    {
        bool publishDepth = false;
        bool publishStaticDepthHierarchy = false;
        bool finalizePages = false;
        bool resolveVisibility = false;
        bool commitCacheState = false;
        bool returnWhite = true;
        bool latchFullRebuild = false;
        bool clearSparseResources = false;
        bool invalidateVisibilityCaches = true;
        bool resetDepthBindings = true;
    };

    [[nodiscard]] constexpr SvsmRasterSubmissionTransactionAction
    GetSvsmRasterSubmissionTransactionAction(
        bool submissionSucceeded,
        bool sparseBackend)
    {
        if (submissionSucceeded)
        {
            return {
                true,
                true,
                true,
                true,
                true,
                false,
                false,
                false,
                false,
                false
            };
        }

        // A page clear followed by a partial caster submission is not a
        // renderable cache state. Sparse mode must discard the whole
        // transaction and clear persistent resources before trying again.
        // Dense mode is redrawn from a clear texture every frame, but it still
        // returns white and resets binding caches instead of resolving a
        // partially rendered depth texture.
        return {
            false,
            false,
            false,
            false,
            false,
            true,
            sparseBackend,
            sparseBackend,
            true,
            true
        };
    }

    struct SvsmTimerRetirementAction
    {
        bool allowUiPublication = false;
        bool retireTaggedSample = false;
        bool enqueueTaggedSample = false;
        bool dropTaggedSample = false;
    };

    [[nodiscard]] constexpr SvsmTimerRetirementAction
    GetSvsmTimerRetirementAction(
        bool discarded,
        bool hasSourceTag,
        bool completedQueueHasCapacity)
    {
        return {
            !discarded,
            hasSourceTag,
            hasSourceTag &&
                !discarded &&
                completedQueueHasCapacity,
            hasSourceTag &&
                (discarded || !completedQueueHasCapacity)
        };
    }

    enum class SvsmPacketDrawItemDisposition : uint8_t
    {
        Skip,
        Accept,
        Abort
    };

    [[nodiscard]] constexpr SvsmPacketDrawItemDisposition
    ClassifySvsmPacketDrawItem(
        bool hasMaterial,
        bool hasInstance,
        bool hasMesh,
        bool hasGeometry,
        bool hasBuffers,
        bool geometryHasMaterial,
        bool meshHasBuffers,
        bool materialIdentityMatches,
        bool bufferIdentityMatches,
        bool instanceIndexValid)
    {
        // Donut deliberately emits non-shadow-casting items with no material.
        // Every other missing or inconsistent field is malformed required draw
        // state and must abort the depth transaction instead of omitting a
        // caster from an otherwise publishable shadow map.
        if (!hasMaterial)
            return SvsmPacketDrawItemDisposition::Skip;

        return hasInstance &&
                hasMesh &&
                hasGeometry &&
                hasBuffers &&
                geometryHasMaterial &&
                meshHasBuffers &&
                materialIdentityMatches &&
                bufferIdentityMatches &&
                instanceIndexValid
            ? SvsmPacketDrawItemDisposition::Accept
            : SvsmPacketDrawItemDisposition::Abort;
    }

    [[nodiscard]] constexpr bool CanAttemptSvsmRenderPacketReuse(
        bool cachedShadowDrawListsEnabled)
    {
        // This is only permission to attempt the exact cache comparison.
        // Camera matrices, light, scene identity, packet layout, sorting,
        // builder mode, classification state, and all remaining inputs are
        // compared by the packet cache itself on every enabled frame.
        //
        // Content invalidation is deliberately not an outer gate. It can
        // dirty depth without changing any draw packet input, so denying the
        // exact comparison here would rebuild an identical packet list.
        return cachedShadowDrawListsEnabled;
    }

    [[nodiscard]] constexpr bool ShouldReuseSvsmRenderPackets(
        bool reuseAttemptEnabled,
        bool exactPacketKeyMatches)
    {
        return reuseAttemptEnabled && exactPacketKeyMatches;
    }

    [[nodiscard]] constexpr bool
    KeepSvsmIndirectArgumentTemplatesInitialized(
        bool currentlyInitialized,
        bool renderPacketsRebuilt)
    {
        // Packet preparation can rebuild while GPU-gated submission is
        // disabled. Latch the templates dirty here so re-enabling GPU gating
        // cannot reuse indirect arguments for the previous packet cache.
        return currentlyInitialized && !renderPacketsRebuilt;
    }

    [[nodiscard]] constexpr uint64_t
    GetNextSvsmPacketClassificationGeneration(
        uint64_t currentGeneration)
    {
        return currentGeneration ==
                std::numeric_limits<uint64_t>::max()
            ? 1u
            : currentGeneration + 1u;
    }

    struct SvsmPersistentCasterSourceKey
    {
        const void* rootIdentity = nullptr;
        uint64_t sourceGeneration = 0u;
        uint64_t casterStateHash = 0u;
        uint32_t sourceRecordCount = 0u;
        bool reliable = false;
    };

    [[nodiscard]] constexpr bool
    IsSameSvsmPersistentCasterSourceKey(
        const SvsmPersistentCasterSourceKey& left,
        const SvsmPersistentCasterSourceKey& right)
    {
        // Light and clipmap matrices change projected packets, not their
        // stable caster source.
        return left.reliable &&
            right.reliable &&
            left.rootIdentity == right.rootIdentity &&
            left.sourceGeneration == right.sourceGeneration &&
            left.casterStateHash == right.casterStateHash &&
            left.sourceRecordCount == right.sourceRecordCount;
    }

    [[nodiscard]] constexpr bool
    ShouldUseSvsmPersistentCasterSource(
        bool enabled,
        bool sharedBuilderCompatible,
        bool snapshotsAvailable,
        bool snapshotsReliable,
        bool duplicateKeysDetected)
    {
        return enabled &&
            sharedBuilderCompatible &&
            snapshotsAvailable &&
            snapshotsReliable &&
            !duplicateKeysDetected;
    }

    struct SvsmCasterDirtyNodeDecision
    {
        bool casterStateChanged = false;
        bool inspectChildren = false;
    };

    [[nodiscard]] constexpr SvsmCasterDirtyNodeDecision
    GetSvsmCasterDirtyNodeDecision(
        bool localTransformChanged,
        bool leafChanged,
        bool subgraphStructureChanged,
        bool subgraphTransformChanged,
        bool subgraphContentChanged,
        bool containsCasterContent)
    {
        // Donut's structure flag does not expose enough information to prove
        // what was added or removed before RefreshSceneGraph. Fail open even
        // when the pre-refresh content flags do not yet contain a caster.
        if (subgraphStructureChanged)
            return { true, false };

        const bool localCasterStateChanged =
            containsCasterContent &&
            (localTransformChanged ||
                leafChanged ||
                subgraphContentChanged);
        return {
            localCasterStateChanged,
            !localCasterStateChanged &&
                (subgraphTransformChanged ||
                    subgraphContentChanged)
        };
    }

    [[nodiscard]] constexpr bool
    ShouldReuseSvsmAdaptiveCasterClassification(
        bool active,
        const void* cachedRootIdentity,
        uint64_t cachedGeneration,
        uint32_t cachedRecordCount,
        const void* currentRootIdentity,
        uint64_t currentGeneration,
        uint32_t currentRecordCount)
    {
        return active &&
            cachedRootIdentity != nullptr &&
            cachedRootIdentity == currentRootIdentity &&
            cachedGeneration != 0u &&
            cachedGeneration == currentGeneration &&
            cachedRecordCount == currentRecordCount;
    }

    struct SvsmAffineSignatureParts
    {
        std::array<float, 9u> linear{};
        std::array<float, 3u> translation{};
    };

    [[nodiscard]] inline std::array<float, 12u>
    MakeSvsmAffineSignature(
        const SvsmAffineSignatureParts& transform)
    {
        std::array<float, 12u> result{};
        std::copy(
            transform.linear.begin(),
            transform.linear.end(),
            result.begin());
        std::copy(
            transform.translation.begin(),
            transform.translation.end(),
            result.begin() + 9u);
        return result;
    }

    [[nodiscard]] inline bool DecodeSvsmAffineSignature(
        const std::array<float, 12u>& signature,
        SvsmAffineSignatureParts& transform)
    {
        if (!std::all_of(
                signature.begin(),
                signature.end(),
                [](float value) { return std::isfinite(value); }))
        {
            return false;
        }
        std::copy_n(
            signature.begin(),
            transform.linear.size(),
            transform.linear.begin());
        std::copy_n(
            signature.begin() + 9u,
            transform.translation.size(),
            transform.translation.begin());
        return true;
    }

    struct SvsmCopiedBoundsSignature
    {
        std::array<float, 3u> minimum{};
        std::array<float, 3u> maximum{};
    };

    [[nodiscard]] inline bool HasSvsmCopiedBoundsChanged(
        const SvsmCopiedBoundsSignature& previous,
        const SvsmCopiedBoundsSignature& current)
    {
        return previous.minimum != current.minimum ||
            previous.maximum != current.maximum;
    }

    [[nodiscard]] constexpr bool IsSameSvsmConfiguration(
        const SparseVirtualShadowMapSettings& left,
        const SparseVirtualShadowMapSettings& right)
    {
        return left.enabled == right.enabled &&
            left.preset == right.preset &&
            left.mode == right.mode &&
            left.markingMode == right.markingMode &&
            left.filterMode == right.filterMode &&
            left.filterKernel == right.filterKernel &&
            left.poissonOrdering == right.poissonOrdering &&
            left.tapCount == right.tapCount &&
            left.resolutionBias == right.resolutionBias &&
            left.debugView == right.debugView &&
            left.firstClipmapExtent == right.firstClipmapExtent &&
            left.maximumLightDepth == right.maximumLightDepth &&
            left.physicalPageCount == right.physicalPageCount &&
            left.pageRenderBudget == right.pageRenderBudget &&
            left.coarsestPageRenderBudgetEnabled ==
                right.coarsestPageRenderBudgetEnabled &&
            left.perPixelMarkingDedupeEnabled ==
                right.perPixelMarkingDedupeEnabled &&
            left.cachingEnabled == right.cachingEnabled &&
            left.lightDepthOriginGuardBandEnabled ==
                right.lightDepthOriginGuardBandEnabled &&
            left.lightDepthOriginGuardBandFraction ==
                right.lightDepthOriginGuardBandFraction &&
            left.staticPageRequestReuseEnabled ==
                right.staticPageRequestReuseEnabled &&
            left.allocationBudgetSaturationEarlyOutEnabled ==
                right.allocationBudgetSaturationEarlyOutEnabled &&
            left.finiteBudgetStaticDrainEnabled ==
                right.finiteBudgetStaticDrainEnabled &&
            left.staticVisibilityCachingEnabled ==
                right.staticVisibilityCachingEnabled &&
            left.sceneStateCachingEnabled ==
                right.sceneStateCachingEnabled &&
            left.casterOnlySceneRevisionEnabled ==
                right.casterOnlySceneRevisionEnabled &&
            left.renderPacketCachingEnabled ==
                right.renderPacketCachingEnabled &&
            left.sharedClipmapPacketBuilderEnabled ==
                right.sharedClipmapPacketBuilderEnabled &&
            left.persistentCasterSourceCachingEnabled ==
                right.persistentCasterSourceCachingEnabled &&
            left.opaqueRasterSpecializationEnabled ==
                right.opaqueRasterSpecializationEnabled &&
            left.leanAlphaTestedBindingsEnabled ==
                right.leanAlphaTestedBindingsEnabled &&
            left.pairedStaticDynamicDepthEnabled ==
                right.pairedStaticDynamicDepthEnabled &&
            left.deferredStaticDepthMergeEnabled ==
                right.deferredStaticDepthMergeEnabled &&
            left.movingLightUncachedEnabled ==
                right.movingLightUncachedEnabled &&
            left.retainPhysicalMappingsOnContentInvalidationEnabled ==
                right.
                    retainPhysicalMappingsOnContentInvalidationEnabled &&
            left.movingLightLodBiasEnabled ==
                right.movingLightLodBiasEnabled &&
            left.movingLightResolutionBias ==
                right.movingLightResolutionBias &&
            left.movingLightLodRecoveryFrames ==
                right.movingLightLodRecoveryFrames &&
            left.receiverDistanceMipClampEnabled ==
                right.receiverDistanceMipClampEnabled &&
            left.receiverDistanceMipClampStartScale ==
                right.receiverDistanceMipClampStartScale &&
            left.receiverDistanceMipClampMaximumLevel ==
                right.receiverDistanceMipClampMaximumLevel &&
            left.movingLightContinuousReceiverBiasEnabled ==
                right.movingLightContinuousReceiverBiasEnabled &&
            left.localizedInvalidationEnabled ==
                right.localizedInvalidationEnabled &&
            left.tightLocalizedInvalidationBoundsEnabled ==
                right.tightLocalizedInvalidationBoundsEnabled &&
            left.adaptiveCasterCacheClassificationEnabled ==
                right.adaptiveCasterCacheClassificationEnabled &&
            left.defaultObjectInvalidationMode ==
                right.defaultObjectInvalidationMode &&
            left.gpuGatedDrawSubmission ==
                right.gpuGatedDrawSubmission &&
            left.batchedDrawSubmissionEnabled ==
                right.batchedDrawSubmissionEnabled &&
            left.packetStateSortingEnabled ==
                right.packetStateSortingEnabled &&
            left.levelEmptyWorkSkipEnabled ==
                right.levelEmptyWorkSkipEnabled &&
            left.packetPageCullingEnabled ==
                right.packetPageCullingEnabled &&
            left.hierarchicalScheduledPageMaskEnabled ==
                right.hierarchicalScheduledPageMaskEnabled &&
            left.receiverPageMaskCullingEnabled ==
                right.receiverPageMaskCullingEnabled &&
            left.staticDepthHierarchyCullingEnabled ==
                right.staticDepthHierarchyCullingEnabled &&
            left.staticDepthHierarchyBias ==
                right.staticDepthHierarchyBias &&
            left.dirtyPageScatterRasterEnabled ==
                right.dirtyPageScatterRasterEnabled &&
            left.scatterAlphaTestEarlyRejectEnabled ==
                right.scatterAlphaTestEarlyRejectEnabled &&
            left.dirtyPageScatterAmplificationGuardEnabled ==
                right.dirtyPageScatterAmplificationGuardEnabled &&
            left.dirtyPageScatterMaximumAmplification ==
                right.dirtyPageScatterMaximumAmplification &&
            left.packetRectangleDirectScanEnabled ==
                right.packetRectangleDirectScanEnabled &&
            left.recentPageEvictionGraceEnabled ==
                right.recentPageEvictionGraceEnabled &&
            left.precomposedClipmapTransformsEnabled ==
                right.precomposedClipmapTransformsEnabled &&
            left.pageTranslationCachingEnabled ==
                right.pageTranslationCachingEnabled &&
            left.detailedGpuTimingEnabled ==
                right.detailedGpuTimingEnabled &&
            left.adaptiveFiltering == right.adaptiveFiltering;
    }

    inline void ApplySvsmPreset(
        SparseVirtualShadowMapSettings& settings,
        SvsmPreset preset)
    {
        if (preset == SvsmPreset::Custom)
        {
            settings.preset = SvsmPreset::Custom;
            return;
        }

        const bool enabled = settings.enabled;
        const float firstClipmapExtent = settings.firstClipmapExtent;
        const float maximumLightDepth = settings.maximumLightDepth;
        const uint32_t physicalPageCount = settings.physicalPageCount;

        settings = SparseVirtualShadowMapSettings{};
        settings.enabled = enabled;
        settings.preset = preset;
        settings.firstClipmapExtent = firstClipmapExtent;
        settings.maximumLightDepth = maximumLightDepth;
        settings.physicalPageCount = physicalPageCount;

        switch (preset)
        {
        case SvsmPreset::Performance:
            settings.markingMode = SvsmMarkingMode::PerPixel;
            settings.filterKernel = SvsmFilterKernel::NearestPoisson;
            settings.tapCount = SvsmTapCount::Eight;
            settings.resolutionBias = SvsmResolutionBias::PlusOne;
            settings.movingLightResolutionBias =
                SvsmResolutionBias::PlusTwo;
            settings.receiverDistanceMipClampEnabled = true;
            settings.receiverDistanceMipClampStartScale = 0.75f;
            settings.adaptiveFiltering = true;
            break;
        case SvsmPreset::Balanced:
            settings.tapCount = SvsmTapCount::Four;
            settings.movingLightResolutionBias =
                SvsmResolutionBias::PlusOne;
            settings.receiverDistanceMipClampEnabled = true;
            settings.receiverDistanceMipClampStartScale = 1.f;
            break;
        case SvsmPreset::Quality:
            break;
        case SvsmPreset::Custom:
            // Handled before reset so selecting Custom retains every edit.
            break;
        }
        settings.movingLightLodBiasEnabled =
            settings.movingLightResolutionBias !=
                SvsmResolutionBias::Zero;
    }

    inline bool ValidateSvsmSettings(
        const SparseVirtualShadowMapSettings& settings)
    {
        if (!(settings.firstClipmapExtent > 0.f) ||
            !std::isfinite(settings.firstClipmapExtent) ||
            !(settings.maximumLightDepth > 0.f) ||
            !std::isfinite(settings.maximumLightDepth) ||
            !(settings.lightDepthOriginGuardBandFraction > 0.f) ||
            settings.lightDepthOriginGuardBandFraction > 1.f ||
            !std::isfinite(
                settings.lightDepthOriginGuardBandFraction) ||
            settings.physicalPageCount == 0u ||
            settings.physicalPageCount > SvsmPagesPerClipmap ||
            settings.dirtyPageScatterMaximumAmplification == 0u ||
            settings.dirtyPageScatterMaximumAmplification >
                SvsmMaximumDirtyPageScatterAmplification ||
            settings.movingLightLodRecoveryFrames >
                SvsmMaximumMovingLightLodRecoveryFrames ||
            !(settings.receiverDistanceMipClampStartScale > 0.f) ||
            !std::isfinite(
                settings.receiverDistanceMipClampStartScale) ||
            settings.receiverDistanceMipClampMaximumLevel >
                SvsmMaximumReceiverDistanceMipClampLevel ||
            !std::isfinite(settings.staticDepthHierarchyBias) ||
            settings.staticDepthHierarchyBias < 0.f ||
            settings.staticDepthHierarchyBias > 0.05f ||
            uint32_t(settings.poissonOrdering) >
                uint32_t(SvsmPoissonOrdering::BalancedProgressive) ||
            !IsSvsmObjectInvalidationModeValid(
                settings.defaultObjectInvalidationMode))
        {
            return false;
        }

        float extent = settings.firstClipmapExtent;
        for (uint32_t level = 0u;
            level < SvsmClipmapCount;
            ++level)
        {
            if (!IsSvsmProjectionRangeRepresentable(
                    extent,
                    settings.maximumLightDepth))
            {
                return false;
            }
            if (level + 1u < SvsmClipmapCount)
                extent *= 2.f;
        }

        const uint32_t taps = uint32_t(settings.tapCount);
        if (taps != 1u && taps != 4u && taps != 8u && taps != 16u)
            return false;

        return true;
    }
}
