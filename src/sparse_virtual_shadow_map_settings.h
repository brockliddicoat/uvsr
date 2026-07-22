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
    constexpr uint32_t SvsmPageAgeMask = 0x3fffu;
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
    constexpr uint32_t SvsmMaximumDirtyPageScatterAmplification = 64u;
    // The scatter path rasterizes an object over a conservative virtual-page
    // rectangle. Keep its independently enabled safety envelope small enough
    // that both rectangle raster and per-page fail-open work remain bounded.
    // Larger budgets retain the exact compact per-page reference path.
    constexpr uint32_t SvsmMaximumSafeDirtyPageScatterRenderBudget = 4u;
    constexpr uint32_t SvsmMaximumSafeDirtyPageScatterRectanglePages = 16u;
    constexpr uint32_t SvsmLevelHasWorkCounterCount =
        SvsmClipmapCount;
    constexpr uint32_t SvsmCounterCount =
        SvsmLevelHasWorkCounterBase +
        SvsmLevelHasWorkCounterCount;

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

    [[nodiscard]] constexpr uint32_t
    GetSvsmLevelHasWorkCounterIndex(uint32_t level)
    {
        return SvsmLevelHasWorkCounterBase + level;
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

    struct SvsmPacketPageRectangle
    {
        uint32_t packedMinimum = SvsmInvalidPacketPageBounds;
        uint32_t packedMaximum = SvsmInvalidPacketPageBounds;
    };

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
        return amplificationGuardEnabled &&
            coarsestPageRenderBudgetEnabled &&
            pageRenderBudget > 0u &&
            pageRenderBudget <=
                SvsmMaximumSafeDirtyPageScatterRenderBudget &&
            maximumAmplification > 0u &&
            uint64_t(pageRenderBudget) * uint64_t(maximumAmplification) <=
                SvsmMaximumSafeDirtyPageScatterRectanglePages;
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
        const uint64_t packetCount64 = packetCount;
        const uint64_t groupCount = dirtyPageScatterRaster
            ? packetCount64 / SvsmPacketFillThreadsPerGroup +
                (packetCount64 % SvsmPacketFillThreadsPerGroup != 0u
                    ? 1u
                    : 0u)
            : packetCount64;
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
            packetPageCulling && !dirtyPageScatterRaster;
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
            bool pageMaintenancePending = false)
    {
        if (!stateCompatible)
            return SvsmStaticPageRequestAction::Rebuild;
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

    enum class SvsmTapCount : uint32_t
    {
        One = 1u,
        Four = 4u,
        Eight = 8u,
        Sixteen = 16u
    };

    constexpr uint32_t GetSvsmFilterRadius(
        SvsmTapCount tapCount)
    {
        return tapCount == SvsmTapCount::One ? 0u : 3u;
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

    enum class SvsmResolutionBias : uint32_t
    {
        Zero = 0u,
        PlusOne = 1u,
        PlusTwo = 2u
    };

    constexpr uint32_t GetSvsmFirstClipmapLevel(
        SvsmResolutionBias bias)
    {
        return std::min(
            uint32_t(bias), SvsmClipmapCount - 1u);
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

    struct SvsmPageMetadata
    {
        uint32_t physicalPage = SvsmInvalidPhysicalPage;
        uint32_t age = 0u;
        bool resident = false;
        bool required = false;
        bool dirty = true;
    };

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
            ((metadata.age & SvsmPageAgeMask) << 18u);
    }

    constexpr SvsmPageMetadata UnpackSvsmPageMetadata(uint32_t packed)
    {
        SvsmPageMetadata metadata;
        metadata.resident = (packed & (1u << 15u)) != 0u;
        metadata.required = (packed & (1u << 16u)) != 0u;
        metadata.dirty = (packed & (1u << 17u)) != 0u;
        metadata.age = (packed >> 18u) & SvsmPageAgeMask;
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
        uint32_t scheduledOwner)
    {
        const SvsmPageMetadata page =
            UnpackSvsmPageMetadata(packedPageMetadata);
        return page.resident &&
            page.dirty &&
            page.physicalPage < physicalPageCount &&
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

    struct SparseVirtualShadowMapSettings
    {
        bool enabled = false;
        SvsmPreset preset = SvsmPreset::Quality;
        SvsmMode mode = SvsmMode::SparseCached;
        SvsmMarkingMode markingMode = SvsmMarkingMode::PerPixel;
        SvsmFilterMode filterMode = SvsmFilterMode::ManualPageSafe;
        SvsmTapCount tapCount = SvsmTapCount::Sixteen;
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
        bool staticPageRequestReuseEnabled = true;
        bool allocationBudgetSaturationEarlyOutEnabled = true;
        bool finiteBudgetStaticDrainEnabled = true;
        bool staticVisibilityCachingEnabled = true;
        bool sceneStateCachingEnabled = true;
        bool renderPacketCachingEnabled = true;
        bool gpuGatedDrawSubmission = true;
        bool batchedDrawSubmissionEnabled = true;
        bool packetStateSortingEnabled = true;
        bool levelEmptyWorkSkipEnabled = true;
        bool packetPageCullingEnabled = true;
        bool dirtyPageScatterRasterEnabled = false;
        bool scatterAlphaTestEarlyRejectEnabled = false;
        bool dirtyPageScatterAmplificationGuardEnabled = false;
        uint32_t dirtyPageScatterMaximumAmplification = 4u;
        bool packetRectangleDirectScanEnabled = true;
        bool recentPageEvictionGraceEnabled = true;
        bool pageTranslationCachingEnabled = true;
        bool detailedGpuTimingEnabled = false;
        bool adaptiveFiltering = false;
        bool fineCasterExclusion = false;
    };

    [[nodiscard]] constexpr bool IsSameSvsmConfiguration(
        const SparseVirtualShadowMapSettings& left,
        const SparseVirtualShadowMapSettings& right)
    {
        return left.enabled == right.enabled &&
            left.preset == right.preset &&
            left.mode == right.mode &&
            left.markingMode == right.markingMode &&
            left.filterMode == right.filterMode &&
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
            left.renderPacketCachingEnabled ==
                right.renderPacketCachingEnabled &&
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
            left.pageTranslationCachingEnabled ==
                right.pageTranslationCachingEnabled &&
            left.detailedGpuTimingEnabled ==
                right.detailedGpuTimingEnabled &&
            left.adaptiveFiltering == right.adaptiveFiltering &&
            left.fineCasterExclusion == right.fineCasterExclusion;
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
            settings.tapCount = SvsmTapCount::Eight;
            settings.resolutionBias = SvsmResolutionBias::PlusOne;
            settings.adaptiveFiltering = true;
            break;
        case SvsmPreset::Balanced:
            settings.tapCount = SvsmTapCount::Eight;
            settings.adaptiveFiltering = true;
            break;
        case SvsmPreset::Quality:
            break;
        case SvsmPreset::Custom:
            // Handled before reset so selecting Custom retains every edit.
            break;
        }
    }

    inline bool ValidateSvsmSettings(
        const SparseVirtualShadowMapSettings& settings)
    {
        if (!(settings.firstClipmapExtent > 0.f) ||
            !std::isfinite(settings.firstClipmapExtent) ||
            !(settings.maximumLightDepth > 0.f) ||
            !std::isfinite(settings.maximumLightDepth) ||
            settings.physicalPageCount == 0u ||
            settings.physicalPageCount > SvsmPagesPerClipmap ||
            settings.dirtyPageScatterMaximumAmplification == 0u ||
            settings.dirtyPageScatterMaximumAmplification >
                SvsmMaximumDirtyPageScatterAmplification)
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
