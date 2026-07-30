#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
#include "sparse_virtual_shadow_map_sparse_cb.h"

#ifndef SVSM_PRECOMPOSED_CLIPMAP_TRANSFORMS
#define SVSM_PRECOMPOSED_CLIPMAP_TRANSFORMS 0
#endif

#ifndef SVSM_SCHEDULED_TILE_MASK
#define SVSM_SCHEDULED_TILE_MASK 0
#endif

#ifndef SVSM_STATIC_DEPTH_HIERARCHY
#define SVSM_STATIC_DEPTH_HIERARCHY 0
#endif

#ifndef SVSM_DEFER_STATIC_MERGE
#define SVSM_DEFER_STATIC_MERGE 0
#endif

#ifndef SVSM_RECEIVER_PAGE_MASK
#define SVSM_RECEIVER_PAGE_MASK 0
#endif

#define SVSM_PHYSICAL_MASK 0x7fffu
#define SVSM_RESIDENT_BIT (1u << 15u)
#define SVSM_REQUIRED_BIT (1u << 16u)
#define SVSM_DIRTY_BIT (1u << 17u)
#define SVSM_AGE_SHIFT 18u
#define SVSM_AGE_MASK 0x1fffu
#define SVSM_INVALID_PAGE 0xffffffffu
#define SVSM_PAGES_PER_AXIS 64u
#define SVSM_PAGES_PER_CLIPMAP 4096u
#define SVSM_VIRTUAL_RESOLUTION 8192u
#define SVSM_PAGE_SIZE 128u
#define SVSM_COMPACT_OWNER_MASK 0x7fffu
#define SVSM_COMPACT_PHYSICAL_SHIFT 15u
#define SVSM_RENDER_RESERVATION_COUNTER 8u
#define SVSM_ALLOCATION_FAILURE_COUNTER 12u
#define SVSM_PACKET_PAGE_CANDIDATE_COUNTER 14u
#define SVSM_PACKET_PAGE_COMPACTED_COUNTER 15u
#define SVSM_PACKET_PAGE_FAIL_OPEN_COUNTER 16u
#define SVSM_LEVEL_RENDER_COUNTER_BASE 17u
#define SVSM_FREE_COUNT_COUNTER 23u
#define SVSM_FREE_CURSOR_COUNTER 24u
#define SVSM_UNRECENT_CACHED_COUNT_COUNTER 25u
#define SVSM_UNRECENT_CACHED_CURSOR_COUNTER 26u
#define SVSM_FINE_REQUIRED_COUNT_COUNTER 27u
#define SVSM_FINE_REQUIRED_CURSOR_COUNTER 28u
#define SVSM_RECENT_CACHED_COUNT_COUNTER 29u
#define SVSM_RECENT_CACHED_CURSOR_COUNTER 30u
#define SVSM_MARK_HASH_SIZE 64u
#define SVSM_MARK_HASH_PROBE_COUNT 4u
#define SVSM_MAX_TILE_PAGE_REQUESTS 64u
#define SVSM_RECYCLE_CLASS_INVALID 0u
#define SVSM_RECYCLE_CLASS_FREE 1u
#define SVSM_RECYCLE_CLASS_UNRECENT 2u
#define SVSM_RECYCLE_CLASS_REQUIRED_FINE 3u
#define SVSM_RECYCLE_CLASS_RECENT 4u
#define SVSM_FINE_CLIPMAP_COUNT \
    (SVSM_SPARSE_CLIPMAP_COUNT - 1u)
#define SVSM_FINE_CANDIDATE_MASK_WORDS_PER_LEVEL \
    (SVSM_PAGES_PER_CLIPMAP / 32u)
#define SVSM_FINE_CANDIDATE_MASK_WORD_COUNT \
    (SVSM_FINE_CLIPMAP_COUNT * \
        SVSM_FINE_CANDIDATE_MASK_WORDS_PER_LEVEL)

cbuffer c_Svsm : register(b0)
{
    SparseVirtualShadowMapSparseConstants g_Svsm;
};

#include "sparse_virtual_shadow_map_receiver_lod.hlsli"

Texture2D<float> t_CameraDepth : register(t0);
StructuredBuffer<SparseVirtualShadowMapPacketMetadata>
    t_PacketPageMetadata : register(t1);
StructuredBuffer<uint> t_LocalInvalidationPages : register(t2);
RWTexture2DArray<uint> u_PageTable : register(u0);
RWStructuredBuffer<uint> u_PhysicalOwners : register(u1);
RWStructuredBuffer<uint> u_RenderPages : register(u2);
RWStructuredBuffer<uint> u_Counters : register(u3);
RWTexture2DArray<uint> u_PhysicalDepth : register(u4);
RWStructuredBuffer<uint> u_CompactRenderPages : register(u5);
RWStructuredBuffer<uint> u_IndirectDrawArguments : register(u6);
RWStructuredBuffer<uint> u_PacketPageRuntime : register(u7);
RWStructuredBuffer<uint> u_PacketRenderPages : register(u8);
RWStructuredBuffer<uint> u_DirtyPageRectangles : register(u9);
RWStructuredBuffer<uint> u_ScheduledPageTileMasks : register(u10);
RWStructuredBuffer<uint> u_StaticDepthHierarchy : register(u11);
RWStructuredBuffer<uint> u_ReceiverPageMasks : register(u12);
RWStructuredBuffer<uint> u_FinePageCandidateMasks : register(u13);

// Marking only needs a small, best-effort local dedupe cache. Bounded
// open-addressing avoids turning one popular primary-slot collision into a
// wave of global page-table atomics. Exhaustion still fails open to the exact
// global atomic, so a full table can increase contention but can never drop a
// required page.
groupshared uint s_RequiredPageHash[SVSM_MARK_HASH_SIZE];
groupshared uint s_TileMinimumDepth;
groupshared uint s_TileMaximumDepth;
groupshared uint s_TileMinimumViewDistance;
groupshared uint s_TileMaximumViewDistance;
groupshared uint s_TileValidDepthCount;
groupshared uint s_TileNeedsPerPixelFallback;
groupshared uint s_PacketPageCount;
groupshared uint s_PacketMinimumPageX;
groupshared uint s_PacketMinimumPageY;
groupshared uint s_PacketMaximumPageX;
groupshared uint s_PacketMaximumPageY;
groupshared uint s_ScheduledTileAnyLow;
groupshared uint s_ScheduledTileAnyHigh;
groupshared uint s_ScheduledTileStaticLow;
groupshared uint s_ScheduledTileStaticHigh;
groupshared uint s_ScheduledTileMaskInvalid;
groupshared uint s_ScheduledTileMaskQueryResult;
groupshared uint s_StaticDepthHierarchyValues[
    SVSM_STATIC_DEPTH_HIERARCHY_BASE_AXIS *
    SVSM_STATIC_DEPTH_HIERARCHY_BASE_AXIS];
groupshared uint s_StaticDepthHierarchyOwner;
groupshared uint s_StaticDepthHierarchyPhysical;
groupshared uint s_StaticDepthHierarchyValid;
groupshared uint s_StaticDepthHierarchyBuild;
groupshared uint s_DeferredStaticDepthMerge;

bool PairedStaticDynamicDepthEnabled()
{
    return (g_Svsm.flags &
        SVSM_SPARSE_FLAG_PAIRED_STATIC_DYNAMIC_DEPTH) != 0u;
}

bool StaticDepthHierarchyCullingEnabled()
{
    return PairedStaticDynamicDepthEnabled() &&
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_STATIC_DEPTH_HIERARCHY_CULLING) != 0u;
}

bool StaticDepthHierarchyResourceAvailable()
{
    return (g_Svsm.flags &
        SVSM_SPARSE_FLAG_STATIC_DEPTH_HIERARCHY_RESOURCE) != 0u;
}

bool StaticDepthHierarchyBootstrapEnabled()
{
    return (g_Svsm.flags &
        SVSM_SPARSE_FLAG_STATIC_DEPTH_HIERARCHY_BOOTSTRAP) != 0u;
}

bool ReceiverPageMaskCullingEnabled()
{
    return !PairedStaticDynamicDepthEnabled() &&
        (g_Svsm.flags & SVSM_SPARSE_FLAG_CACHING) == 0u &&
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_RECEIVER_PAGE_MASK_CULLING) != 0u;
}

bool UseDeterministicFinePageBudget()
{
    return (g_Svsm.flags &
            SVSM_SPARSE_FLAG_COARSEST_PAGE_RENDER_BUDGET) == 0u &&
        g_Svsm.pageRenderBudget < g_Svsm.physicalPageCount;
}

uint StaticDepthHierarchyWordBase(uint physicalPage)
{
    return physicalPage *
        SVSM_STATIC_DEPTH_HIERARCHY_WORDS_PER_PAGE;
}

uint PackStaticDepthHierarchyTag(uint owner, uint epoch)
{
    return
        ((epoch & SVSM_STATIC_DEPTH_HIERARCHY_TAG_EPOCH_MASK)
            << SVSM_STATIC_DEPTH_HIERARCHY_TAG_EPOCH_SHIFT) |
        (owner & SVSM_STATIC_DEPTH_HIERARCHY_TAG_OWNER_MASK);
}

uint FullDepthDirtyBits()
{
    return SVSM_DIRTY_BIT |
        (PairedStaticDynamicDepthEnabled()
            ? SVSM_PAGE_STATIC_DIRTY_BIT
            : 0u);
}

bool PageNeedsPacketCaster(uint packed, bool staticCaster)
{
    bool residentAndDirty =
        (packed & (SVSM_RESIDENT_BIT | SVSM_DIRTY_BIT)) ==
            (SVSM_RESIDENT_BIT | SVSM_DIRTY_BIT);
    return residentAndDirty &&
        (!PairedStaticDynamicDepthEnabled() ||
            !staticCaster ||
            (packed & SVSM_PAGE_STATIC_DIRTY_BIT) != 0u);
}

int WrapPage(int coordinate)
{
    int wrapped = coordinate % int(SVSM_PAGES_PER_AXIS);
    return wrapped < 0 ? wrapped + int(SVSM_PAGES_PER_AXIS) : wrapped;
}

uint PacketRuntimeBase(uint packetIndex)
{
    return packetIndex * SVSM_PACKET_PAGE_RUNTIME_WORDS;
}

uint PackPacketPageCoordinate(uint2 page)
{
    return (page.x & 0xffu) | ((page.y & 0xffu) << 8u);
}

void StoreEmptyPacketRectangle(uint packetRuntimeBase)
{
    u_PacketPageRuntime[
        packetRuntimeBase +
        SVSM_PACKET_PAGE_RUNTIME_MINIMUM_WORD] =
        SVSM_PACKET_PAGE_EMPTY_BOUNDS;
    u_PacketPageRuntime[
        packetRuntimeBase +
        SVSM_PACKET_PAGE_RUNTIME_MAXIMUM_WORD] =
        SVSM_PACKET_PAGE_EMPTY_BOUNDS;
}

bool TryLoadGlobalDirtyPageRectangle(
    uint level,
    out uint2 minimumPage,
    out uint2 maximumPage)
{
    minimumPage = 0u;
    maximumPage = 0u;
    if (level >= SVSM_SPARSE_CLIPMAP_COUNT)
        return false;

    uint rectangleBase =
        level * SVSM_DIRTY_PAGE_RECT_WORDS_PER_LEVEL;
    uint4 encoded = uint4(
        u_DirtyPageRectangles[rectangleBase + 0u],
        u_DirtyPageRectangles[rectangleBase + 1u],
        u_DirtyPageRectangles[rectangleBase + 2u],
        u_DirtyPageRectangles[rectangleBase + 3u]);
    if (!any(encoded != 0u))
        return false;

    minimumPage = uint2(
        (SVSM_PAGES_PER_AXIS - 1u) - encoded.x,
        (SVSM_PAGES_PER_AXIS - 1u) - encoded.z);
    maximumPage = encoded.yw;
    if (any(minimumPage > maximumPage) ||
        any(maximumPage >= SVSM_PAGES_PER_AXIS))
        return false;
    return true;
}

void StoreGlobalDirtyPacketRectangle(
    uint packetRuntimeBase,
    uint level)
{
    uint2 minimumPage;
    uint2 maximumPage;
    if (!TryLoadGlobalDirtyPageRectangle(
            level, minimumPage, maximumPage))
    {
        StoreEmptyPacketRectangle(packetRuntimeBase);
        return;
    }

    u_PacketPageRuntime[
        packetRuntimeBase +
        SVSM_PACKET_PAGE_RUNTIME_MINIMUM_WORD] =
        PackPacketPageCoordinate(minimumPage);
    u_PacketPageRuntime[
        packetRuntimeBase +
        SVSM_PACKET_PAGE_RUNTIME_MAXIMUM_WORD] =
        PackPacketPageCoordinate(maximumPage);
}

uint3 DecodeVirtualPage(uint owner)
{
    uint level = owner / SVSM_PAGES_PER_CLIPMAP;
    uint local = owner % SVSM_PAGES_PER_CLIPMAP;
    return uint3(
        local % SVSM_PAGES_PER_AXIS,
        local / SVSM_PAGES_PER_AXIS,
        level);
}

uint EncodeVirtualPage(uint3 page)
{
    return page.z * SVSM_PAGES_PER_CLIPMAP +
        page.y * SVSM_PAGES_PER_AXIS +
        page.x;
}

uint EncodeMortonAxis(uint value)
{
    value &= 0x0000ffffu;
    value = (value | (value << 8u)) & 0x00ff00ffu;
    value = (value | (value << 4u)) & 0x0f0f0f0fu;
    value = (value | (value << 2u)) & 0x33333333u;
    value = (value | (value << 1u)) & 0x55555555u;
    return value;
}

uint DecodeMortonAxis(uint value)
{
    value &= 0x55555555u;
    value = (value | (value >> 1u)) & 0x33333333u;
    value = (value | (value >> 2u)) & 0x0f0f0f0fu;
    value = (value | (value >> 4u)) & 0x00ff00ffu;
    value = (value | (value >> 8u)) & 0x0000ffffu;
    return value;
}

uint GetCenteredDistanceOrdinal(uint coordinate)
{
    const uint center = SVSM_PAGES_PER_AXIS / 2u;
    return coordinate < center
        ? (center - coordinate) * 2u - 1u
        : (coordinate - center) * 2u;
}

uint GetCenteredMortonScanIndex(uint2 localPage)
{
    return
        EncodeMortonAxis(
            GetCenteredDistanceOrdinal(localPage.x)) |
        (EncodeMortonAxis(
            GetCenteredDistanceOrdinal(localPage.y)) << 1u);
}

uint2 GetCenteredMortonLocalPage(uint scanIndex)
{
    const uint2 distanceOrdinal = uint2(
        DecodeMortonAxis(scanIndex),
        DecodeMortonAxis(scanIndex >> 1u));
    // Decode each Morton axis as a center-distance ordinal:
    // 0,1,2,3... -> 32,31,33,30... . This visits a symmetric 2x2 center
    // footprint first, then grows outward without favoring one quadrant.
    return uint2(
        distanceOrdinal.x == 0u
            ? 32u
            : ((distanceOrdinal.x & 1u) != 0u
                ? 32u - (distanceOrdinal.x + 1u) / 2u
                : 32u + distanceOrdinal.x / 2u),
        distanceOrdinal.y == 0u
            ? 32u
            : ((distanceOrdinal.y & 1u) != 0u
                ? 32u - (distanceOrdinal.y + 1u) / 2u
                : 32u + distanceOrdinal.y / 2u));
}

bool GetDeterministicFinePageMaskLocation(
    uint3 page,
    out uint word,
    out uint bit)
{
    word = 0u;
    bit = 0u;
    if (page.z >= SVSM_FINE_CLIPMAP_COUNT)
        return false;

    const int2 pageTableOffset =
        g_Svsm.pageTableOffsetAndDelta[page.z].xy;
    const uint2 localPage = uint2(
        WrapPage(int(page.x) - pageTableOffset.x),
        WrapPage(int(page.y) - pageTableOffset.y));
    const uint scanIndex =
        GetCenteredMortonScanIndex(localPage);
    word =
        page.z * SVSM_FINE_CANDIDATE_MASK_WORDS_PER_LEVEL +
        scanIndex / 32u;
    bit = 1u << (scanIndex & 31u);
    return true;
}

void RecordDeterministicFinePageCandidate(uint3 page)
{
    uint word;
    uint bit;
    if (!GetDeterministicFinePageMaskLocation(
            page, word, bit))
    {
        return;
    }

    uint ignored;
    InterlockedOr(
        u_FinePageCandidateMasks[word],
        bit,
        ignored);
}

void RecordDeterministicRequiredFineVictim(uint3 page)
{
    uint word;
    uint bit;
    if (!GetDeterministicFinePageMaskLocation(
            page, word, bit))
    {
        return;
    }

    uint ignored;
    InterlockedOr(
        u_FinePageCandidateMasks[word],
        bit,
        ignored);
    InterlockedAdd(
        u_Counters[SVSM_FINE_REQUIRED_COUNT_COUNTER],
        1u,
        ignored);
}

bool IsNewlyExposed(uint2 tablePage, uint level)
{
    int4 offsetDelta = g_Svsm.pageTableOffsetAndDelta[level];
    int2 delta = offsetDelta.zw;
    if (delta.x <= -int(SVSM_PAGES_PER_AXIS) ||
        delta.x >= int(SVSM_PAGES_PER_AXIS) ||
        delta.y <= -int(SVSM_PAGES_PER_AXIS) ||
        delta.y >= int(SVSM_PAGES_PER_AXIS))
    {
        return true;
    }

    int2 localPage = int2(
        WrapPage(int(tablePage.x) - offsetDelta.x),
        WrapPage(int(tablePage.y) - offsetDelta.y));
    bool exposedX = delta.x > 0
        ? localPage.x >= int(SVSM_PAGES_PER_AXIS) - delta.x
        : (delta.x < 0 && localPage.x < -delta.x);
    bool exposedY = delta.y > 0
        ? localPage.y >= int(SVSM_PAGES_PER_AXIS) - delta.y
        : (delta.y < 0 && localPage.y < -delta.y);
    return exposedX || exposedY;
}

[numthreads(8, 8, 1)]
void prepare(uint3 page : SV_DispatchThreadID)
{
    if (any(page.xy >= SVSM_PAGES_PER_AXIS) ||
        page.z >= SVSM_SPARSE_CLIPMAP_COUNT)
    {
        return;
    }

    uint packed = u_PageTable[page];
    const bool fullInvalidation =
        (g_Svsm.flags & SVSM_SPARSE_FLAG_FULL_INVALIDATION) != 0u;
    const bool preservePhysicalMappings =
        fullInvalidation &&
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_PRESERVE_PHYSICAL_MAPPINGS) != 0u;
    const bool newlyExposed = IsNewlyExposed(page.xy, page.z);
    if (preservePhysicalMappings)
    {
        const uint physicalPage = packed & SVSM_PHYSICAL_MASK;
        const uint expectedOwner = EncodeVirtualPage(page);
        bool validOwnedMapping = false;
        if (
            (packed & SVSM_RESIDENT_BIT) != 0u &&
            physicalPage < g_Svsm.physicalPageCount)
        {
            // Keep the range check outside the owner load. Shader boolean
            // short-circuiting is not an acceptable out-of-bounds guard.
            validOwnedMapping =
                u_PhysicalOwners[physicalPage] == expectedOwner;
        }
        if (validOwnedMapping)
        {
            // Keep only identity, residency, and age. Required is rebuilt from
            // camera depth, while both old dirty classes are replaced by the
            // effective single- or paired-depth content contract.
            packed =
                (packed &
                    (SVSM_PHYSICAL_MASK |
                        SVSM_RESIDENT_BIT |
                        (SVSM_AGE_MASK << SVSM_AGE_SHIFT))) |
                FullDepthDirtyBits();
        }
        else
        {
            // Never free an owner that failed validation: it may belong to a
            // different still-valid virtual page. Fail this entry open to an
            // unallocated dirty page and let normal allocation repair it.
            packed = FullDepthDirtyBits();
        }
    }
    else if (fullInvalidation || newlyExposed)
    {
        if ((packed & SVSM_RESIDENT_BIT) != 0u)
        {
            uint physicalPage = packed & SVSM_PHYSICAL_MASK;
            uint expectedOwner = EncodeVirtualPage(page);
            if (physicalPage < g_Svsm.physicalPageCount)
            {
                if (u_PhysicalOwners[physicalPage] == expectedOwner)
                {
                    u_PhysicalOwners[physicalPage] =
                        SVSM_INVALID_PAGE;
                }
            }
        }
        packed = FullDepthDirtyBits();
    }
    else if ((g_Svsm.flags &
            SVSM_SPARSE_FLAG_PRESERVE_REQUIRED) == 0u)
    {
        // Static request reuse intentionally performs no GPU work. When work
        // resumes, a still-set required bit proves that the page remained in
        // the last active request set, so refresh its age before clearing the
        // bit. This keeps the grace tier meaningful after a long static hold.
        if ((g_Svsm.flags &
                SVSM_SPARSE_FLAG_RECENT_PAGE_EVICTION_GRACE) != 0u &&
            (packed & SVSM_REQUIRED_BIT) != 0u)
        {
            packed =
                (packed & ~(SVSM_AGE_MASK << SVSM_AGE_SHIFT)) |
                ((g_Svsm.frameIndex & SVSM_AGE_MASK) <<
                    SVSM_AGE_SHIFT);
        }
        packed &= ~SVSM_REQUIRED_BIT;
    }
    u_PageTable[page] = packed;
}

[numthreads(64, 1, 1)]
void invalidatePages(uint invalidationIndex : SV_DispatchThreadID)
{
    if (invalidationIndex >= g_Svsm.localInvalidationPageCount)
        return;

    uint encoded = t_LocalInvalidationPages[invalidationIndex];
    uint localOwner =
        encoded & SVSM_LOCAL_INVALIDATION_OWNER_MASK;
    if (localOwner >=
        SVSM_SPARSE_CLIPMAP_COUNT * SVSM_PAGES_PER_CLIPMAP)
    {
        return;
    }

    uint level = localOwner / SVSM_PAGES_PER_CLIPMAP;
    uint localPageIndex = localOwner % SVSM_PAGES_PER_CLIPMAP;
    int2 localPage = int2(
        localPageIndex % SVSM_PAGES_PER_AXIS,
        localPageIndex / SVSM_PAGES_PER_AXIS);
    int2 pageTableOffset =
        g_Svsm.pageTableOffsetAndDelta[level].xy;
    uint2 tablePage = uint2(
        WrapPage(localPage.x + pageTableOffset.x),
        WrapPage(localPage.y + pageTableOffset.y));

    uint dirtyBits = SVSM_DIRTY_BIT;
    if (PairedStaticDynamicDepthEnabled() &&
        (encoded & SVSM_LOCAL_INVALIDATION_STATIC_BIT) != 0u)
    {
        dirtyBits |= SVSM_PAGE_STATIC_DIRTY_BIT;
    }
    InterlockedOr(
        u_PageTable[uint3(tablePage, level)],
        dirtyBits);
}

void RequestPage(int2 localPage, uint level)
{
    if (any(localPage < 0) ||
        any(localPage >= int(SVSM_PAGES_PER_AXIS)))
    {
        return;
    }

    int2 offset = g_Svsm.pageTableOffsetAndDelta[level].xy;
    uint2 tablePage = uint2(
        WrapPage(localPage.x + offset.x),
        WrapPage(localPage.y + offset.y));
    const bool useLocalDedupe =
        g_Svsm.markingMode != 0u ||
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_PER_PIXEL_MARKING_DEDUPE) != 0u;
    if (!useLocalDedupe)
    {
        uint ignored;
        InterlockedOr(
            u_PageTable[uint3(tablePage, level)],
            SVSM_REQUIRED_BIT,
            ignored);
    }
    else
    {
        uint owner = EncodeVirtualPage(
            uint3(tablePage, level));

        // Adjacent receiver pixels usually request the same fine and coarse
        // owners. Elect one lane for every distinct active-wave owner before
        // touching group shared memory, following the Shader Model 6.5
        // WaveMatch atomic-coalescing pattern. The uint4 mask and signed
        // maximum keep this correct for every supported wave width through
        // 128 lanes; inactive mask words contribute -1.
        uint4 matchingOwnerLanes = WaveMatch(owner);
        int4 matchingOwnerHighLanes = (int4)(
            firstbithigh(matchingOwnerLanes) |
            uint4(0u, 0x20u, 0x40u, 0x60u));
        uint ownerLeaderLane = uint(max(
            max(matchingOwnerHighLanes.x, matchingOwnerHighLanes.y),
            max(matchingOwnerHighLanes.z, matchingOwnerHighLanes.w)));
        if (WaveGetLaneIndex() != ownerLeaderLane)
            return;

        uint hash = owner;
        hash ^= hash >> 8u;
        hash *= 0x9e3779b1u;
        hash ^= hash >> 16u;

        const uint hashMask = SVSM_MARK_HASH_SIZE - 1u;
        uint hashSlot = hash & hashMask;
        // The table size is a power of two. An odd, non-zero step visits a
        // distinct slot for every bounded probe and would cover the complete
        // table if the probe bound were ever raised.
        const uint hashStep = ((hash >> 6u) | 1u) & hashMask;
        uint ignored;
        [unroll]
        for (uint probe = 0u;
            probe < SVSM_MARK_HASH_PROBE_COUNT;
            ++probe)
        {
            InterlockedCompareExchange(
                s_RequiredPageHash[hashSlot],
                SVSM_INVALID_PAGE,
                owner,
                ignored);
            if (ignored == SVSM_INVALID_PAGE || ignored == owner)
                return;
            hashSlot = (hashSlot + hashStep) & hashMask;
        }

        // Four occupied foreign-owner slots are not evidence that this owner
        // was requested. Preserve the reference path exactly on exhaustion.
        InterlockedOr(
            u_PageTable[uint3(tablePage, level)],
            SVSM_REQUIRED_BIT,
            ignored);
    }
}

#if SVSM_RECEIVER_PAGE_MASK
uint BuildReceiverPageMaskQuadrantRectangle(
    uint2 minimumCell,
    uint2 maximumCell)
{
    if (any(minimumCell > maximumCell) ||
        any(maximumCell >=
            SVSM_RECEIVER_PAGE_MASK_QUADRANT_CELL_AXIS))
    {
        return 0u;
    }

    const uint xMask =
        ((1u << (maximumCell.x + 1u)) - 1u) &
        ~((1u << minimumCell.x) - 1u);
    const uint repeatedRows = xMask * 0x1111u;
    const uint firstBit =
        minimumCell.y *
        SVSM_RECEIVER_PAGE_MASK_QUADRANT_CELL_AXIS;
    const uint afterLastBit =
        (maximumCell.y + 1u) *
        SVSM_RECEIVER_PAGE_MASK_QUADRANT_CELL_AXIS;
    const uint rowRange =
        ((1u << afterLastBit) - 1u) &
        ~((1u << firstBit) - 1u);
    return repeatedRows & rowRange;
}

void MarkReceiverPageMaskAtPage(
    float2 virtualMinimum,
    float2 virtualMaximum,
    int2 localPage,
    uint level)
{
    if (!ReceiverPageMaskCullingEnabled() ||
        g_Svsm.hierarchyGeneration == 0u ||
        level >= SVSM_SPARSE_CLIPMAP_COUNT ||
        any(localPage < 0) ||
        any(localPage >= int(SVSM_PAGES_PER_AXIS)) ||
        !all(isfinite(virtualMinimum)) ||
        !all(isfinite(virtualMaximum)) ||
        any(virtualMinimum > virtualMaximum))
    {
        return;
    }

    const float2 pageMinimum =
        float2(localPage) * float(SVSM_PAGE_SIZE);
    const float2 pageMaximum =
        pageMinimum + float(SVSM_PAGE_SIZE) - 0.001f;
    const float2 intersectionMinimum =
        max(virtualMinimum, pageMinimum);
    const float2 intersectionMaximum =
        min(virtualMaximum, pageMaximum);
    if (any(intersectionMinimum > intersectionMaximum))
        return;

    const uint2 minimumCell = min(
        uint2(floor(
            (intersectionMinimum - pageMinimum) /
            float(SVSM_RECEIVER_PAGE_MASK_CELL_WIDTH))),
        SVSM_RECEIVER_PAGE_MASK_AXIS - 1u);
    const uint2 maximumCell = min(
        uint2(floor(
            (intersectionMaximum - pageMinimum) /
            float(SVSM_RECEIVER_PAGE_MASK_CELL_WIDTH))),
        SVSM_RECEIVER_PAGE_MASK_AXIS - 1u);

    const int2 pageTableOffset =
        g_Svsm.pageTableOffsetAndDelta[level].xy;
    const uint2 tablePage = uint2(
        WrapPage(localPage.x + pageTableOffset.x),
        WrapPage(localPage.y + pageTableOffset.y));
    const uint owner = EncodeVirtualPage(
        uint3(tablePage, level));
    const uint maskBase =
        owner * SVSM_RECEIVER_PAGE_MASK_WORDS_PER_PAGE;

    [unroll]
    for (uint quadrantY = 0u;
        quadrantY < SVSM_RECEIVER_PAGE_MASK_QUADRANT_AXIS;
        ++quadrantY)
    {
        [unroll]
        for (uint quadrantX = 0u;
            quadrantX < SVSM_RECEIVER_PAGE_MASK_QUADRANT_AXIS;
            ++quadrantX)
        {
            const uint2 quadrantMinimum =
                uint2(quadrantX, quadrantY) *
                SVSM_RECEIVER_PAGE_MASK_QUADRANT_CELL_AXIS;
            const uint2 quadrantMaximum =
                quadrantMinimum +
                SVSM_RECEIVER_PAGE_MASK_QUADRANT_CELL_AXIS - 1u;
            const uint2 overlapMinimum =
                max(minimumCell, quadrantMinimum);
            const uint2 overlapMaximum =
                min(maximumCell, quadrantMaximum);
            if (any(overlapMinimum > overlapMaximum))
                continue;

            const uint quadrant =
                quadrantY * SVSM_RECEIVER_PAGE_MASK_QUADRANT_AXIS +
                quadrantX;
            const uint rectangle =
                BuildReceiverPageMaskQuadrantRectangle(
                    overlapMinimum - quadrantMinimum,
                    overlapMaximum - quadrantMinimum);
            uint ignored;
            InterlockedOr(
                u_ReceiverPageMasks[
                    maskBase +
                    SVSM_RECEIVER_PAGE_MASK_QUADRANT_OFFSET +
                    quadrant],
                rectangle,
                ignored);
        }
    }

    // Mark dispatch and packet compaction are separated by explicit UAV
    // barriers. Publishing this tag after the atomic bits makes a cleared,
    // stale, or partially initialized entry fail open.
    uint ignored;
    InterlockedExchange(
        u_ReceiverPageMasks[
            maskBase + SVSM_RECEIVER_PAGE_MASK_TAG_OFFSET],
        g_Svsm.hierarchyGeneration,
        ignored);
}

void MarkReceiverPageMaskBounds(
    float2 virtualMinimum,
    float2 virtualMaximum,
    uint level)
{
    virtualMinimum = max(virtualMinimum, 0.0f);
    virtualMaximum = min(
        virtualMaximum,
        float(SVSM_VIRTUAL_RESOLUTION) - 0.001f);
    if (any(virtualMinimum > virtualMaximum))
        return;

    const int2 minimumPage = int2(floor(
        virtualMinimum / float(SVSM_PAGE_SIZE)));
    const int2 maximumPage = int2(floor(
        virtualMaximum / float(SVSM_PAGE_SIZE)));
    [loop]
    for (int pageY = minimumPage.y;
        pageY <= maximumPage.y;
        ++pageY)
    {
        [loop]
        for (int pageX = minimumPage.x;
            pageX <= maximumPage.x;
            ++pageX)
        {
            MarkReceiverPageMaskAtPage(
                virtualMinimum,
                virtualMaximum,
                int2(pageX, pageY),
                level);
        }
    }
}

void MarkReceiverPageMaskFilterFootprint(
    float2 virtualPosition,
    uint level)
{
    const float filterRadius = float(g_Svsm.padding0);
    MarkReceiverPageMaskBounds(
        virtualPosition - filterRadius,
        virtualPosition + filterRadius,
        level);
}

void MarkReceiverPageMaskVirtualBounds(
    float2 virtualMinimum,
    float2 virtualMaximum,
    uint level)
{
    const float filterRadius = float(g_Svsm.padding0);
    MarkReceiverPageMaskBounds(
        virtualMinimum - filterRadius,
        virtualMaximum + filterRadius,
        level);
}
#endif

void RequestFilterFootprint(
    float2 virtualPosition,
    int2 localPage,
    uint level)
{
    RequestPage(localPage, level);

    float filterRadius = float(g_Svsm.padding0);
#if SVSM_RECEIVER_PAGE_MASK
    MarkReceiverPageMaskFilterFootprint(
        virtualPosition, level);
#endif
    if (filterRadius == 0.0f)
        return;

    float2 pageTexel = virtualPosition -
        float2(localPage) * float(SVSM_PAGE_SIZE);
    int2 neighborDirection = 0;
    if (pageTexel.x < filterRadius)
        neighborDirection.x = -1;
    else if (pageTexel.x >=
        float(SVSM_PAGE_SIZE) - filterRadius)
        neighborDirection.x = 1;
    if (pageTexel.y < filterRadius)
        neighborDirection.y = -1;
    else if (pageTexel.y >=
        float(SVSM_PAGE_SIZE) - filterRadius)
        neighborDirection.y = 1;

    if (neighborDirection.x != 0)
        RequestPage(localPage + int2(neighborDirection.x, 0), level);
    if (neighborDirection.y != 0)
        RequestPage(localPage + int2(0, neighborDirection.y), level);
    if (all(neighborDirection != 0))
        RequestPage(localPage + neighborDirection, level);
}

void AccumulateLocalTileDepth(
    uint2 pixel,
    inout uint minimumDepth,
    inout uint maximumDepth,
    inout uint minimumViewDistance,
    inout uint maximumViewDistance,
    inout uint validDepthCount)
{
    if (any(pixel >= g_Svsm.cameraSize))
        return;

    float cameraDepth = t_CameraDepth[pixel];
    if (!(cameraDepth > 0.0f) || !isfinite(cameraDepth))
        return;

    uint encodedDepth = asuint(cameraDepth);
    minimumDepth = min(minimumDepth, encodedDepth);
    maximumDepth = max(maximumDepth, encodedDepth);
    const uint encodedViewDistance =
        asuint(GetSvsmReceiverViewDistance(cameraDepth));
    minimumViewDistance = min(
        minimumViewDistance, encodedViewDistance);
    maximumViewDistance = max(
        maximumViewDistance, encodedViewDistance);
    ++validDepthCount;
}

void RequestPageDirect(int2 localPage, uint level)
{
    if (any(localPage < 0) ||
        any(localPage >= int(SVSM_PAGES_PER_AXIS)))
    {
        return;
    }

    int2 offset = g_Svsm.pageTableOffsetAndDelta[level].xy;
    uint2 tablePage = uint2(
        WrapPage(localPage.x + offset.x),
        WrapPage(localPage.y + offset.y));
    uint ignored;
    InterlockedOr(
        u_PageTable[uint3(tablePage, level)],
        SVSM_REQUIRED_BIT,
        ignored);
}

bool RequestVirtualBounds(
    float2 virtualMinimum,
    float2 virtualMaximum,
    uint level)
{
    const float margin = float(g_Svsm.padding0);
    virtualMinimum = max(virtualMinimum - margin, 0.0f);
    virtualMaximum = min(
        virtualMaximum + margin,
        float(SVSM_VIRTUAL_RESOLUTION) - 0.001f);

    int2 minimumPage = int2(floor(
        virtualMinimum / float(SVSM_PAGE_SIZE)));
    int2 maximumPage = int2(floor(
        virtualMaximum / float(SVSM_PAGE_SIZE)));
    minimumPage = clamp(
        minimumPage,
        0,
        int(SVSM_PAGES_PER_AXIS - 1u));
    maximumPage = clamp(
        maximumPage,
        0,
        int(SVSM_PAGES_PER_AXIS - 1u));
    uint2 pageSpan = uint2(maximumPage - minimumPage + 1);
    if (pageSpan.x * pageSpan.y > SVSM_MAX_TILE_PAGE_REQUESTS)
        return false;

    [loop]
    for (int pageY = minimumPage.y;
        pageY <= maximumPage.y;
        ++pageY)
    {
        [loop]
        for (int pageX = minimumPage.x;
            pageX <= maximumPage.x;
            ++pageX)
        {
            RequestPageDirect(int2(pageX, pageY), level);
#if SVSM_RECEIVER_PAGE_MASK
            MarkReceiverPageMaskAtPage(
                virtualMinimum,
                virtualMaximum,
                int2(pageX, pageY),
                level);
#endif
        }
    }
    return true;
}

bool ProjectTileVolumeAtLevel(
    float4 receiverCorners[8],
    uint level,
    out float2 virtualMinimum,
    out float2 virtualMaximum,
    out bool fullyCovered)
{
    virtualMinimum = 1.0f;
    virtualMaximum = 0.0f;
    fullyCovered = false;
    float3 ndcMinimum = float3(3.402823466e+38f, 3.402823466e+38f, 3.402823466e+38f);
    float3 ndcMaximum = -ndcMinimum;
    [loop]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        float4 clip = mul(
            receiverCorners[corner],
            g_Svsm.receiverToClip[level]);
        if (!(clip.w != 0.0f) || !all(isfinite(clip)))
            return false;
        float3 ndc = clip.xyz / clip.w;
        if (!all(isfinite(ndc)))
            return false;
        ndcMinimum = min(ndcMinimum, ndc);
        ndcMaximum = max(ndcMaximum, ndc);
    }

    bool intersects =
        ndcMaximum.x >= -1.0f && ndcMinimum.x <= 1.0f &&
        ndcMaximum.y >= -1.0f && ndcMinimum.y <= 1.0f &&
        ndcMaximum.z >= 0.0f && ndcMinimum.z <= 1.0f;
    fullyCovered =
        ndcMinimum.x >= -1.0f && ndcMaximum.x <= 1.0f &&
        ndcMinimum.y >= -1.0f && ndcMaximum.y <= 1.0f &&
        ndcMinimum.z >= 0.0f && ndcMaximum.z <= 1.0f;
    if (!intersects)
        return true;

    float2 clippedMinimum = max(ndcMinimum.xy, -1.0f);
    float2 clippedMaximum = min(ndcMaximum.xy, 1.0f);
    virtualMinimum = float2(
        clippedMinimum.x * 0.5f + 0.5f,
        clippedMaximum.y * -0.5f + 0.5f) *
        float(SVSM_VIRTUAL_RESOLUTION);
    virtualMaximum = float2(
        clippedMaximum.x * 0.5f + 0.5f,
        clippedMinimum.y * -0.5f + 0.5f) *
        float(SVSM_VIRTUAL_RESOLUTION);
    return true;
}

bool MarkTile(uint2 tileOrigin, uint tileCoverage)
{
    if (s_TileValidDepthCount == 0u)
        return true;

    uint2 tileEnd = min(
        tileOrigin + tileCoverage,
        g_Svsm.cameraSize);
    if (any(tileOrigin >= tileEnd))
        return true;

    float2 minimumPixel = float2(tileOrigin) + 0.5f;
    float2 maximumPixel = float2(tileEnd) - 0.5f;
    float2 pixelCorners[4] = {
        float2(minimumPixel.x, minimumPixel.y),
        float2(maximumPixel.x, minimumPixel.y),
        float2(minimumPixel.x, maximumPixel.y),
        float2(maximumPixel.x, maximumPixel.y)
    };
    float depthBounds[2] = {
        asfloat(s_TileMinimumDepth),
        asfloat(s_TileMaximumDepth)
    };
    float4 receiverCorners[8];
    [unroll]
    for (uint depthIndex = 0u; depthIndex < 2u; ++depthIndex)
    {
        [loop]
        for (uint corner = 0u; corner < 4u; ++corner)
        {
#if SVSM_PRECOMPOSED_CLIPMAP_TRANSFORMS
            float4 receiverPosition = ReconstructClipPosition(
                g_Svsm.cameraView,
                pixelCorners[corner],
                depthBounds[depthIndex]);
#else
            float4 receiverPosition = float4(
                ReconstructWorldPosition(
                    g_Svsm.cameraView,
                    pixelCorners[corner],
                    depthBounds[depthIndex]),
                1.0f);
#endif
            if (!all(isfinite(receiverPosition)))
                return false;
            receiverCorners[depthIndex * 4u + corner] =
                receiverPosition;
        }
    }

    uint firstLevel = GetSvsmReceiverFirstClipmapLevel(
        asfloat(s_TileMinimumViewDistance));
    const uint maximumReceiverFirstLevel =
        GetSvsmReceiverFirstClipmapLevel(
            asfloat(s_TileMaximumViewDistance));
    const uint coarsestLevel = SVSM_SPARSE_CLIPMAP_COUNT - 1u;
    bool requestedAny = false;
    bool requestedCoarsest = false;
    [loop]
    for (uint level = firstLevel;
        level < SVSM_SPARSE_CLIPMAP_COUNT;
        ++level)
    {
        float2 virtualMinimum = 0.0f;
        float2 virtualMaximum = 0.0f;
        bool fullyCovered = false;
        if (!ProjectTileVolumeAtLevel(
                receiverCorners,
                level,
                virtualMinimum,
                virtualMaximum,
                fullyCovered))
        {
            return false;
        }

        if (virtualMaximum.x >= virtualMinimum.x &&
            virtualMaximum.y >= virtualMinimum.y)
        {
            if (!RequestVirtualBounds(
                    virtualMinimum,
                    virtualMaximum,
                    level))
            {
                return false;
            }
            requestedAny = true;
            requestedCoarsest = level == coarsestLevel;
        }

        // Geometric coverage alone is not enough to stop. Distance-based
        // receiver LOD can make farther pixels in the same tile begin at an
        // intermediate clipmap, so request through the coarsest first level
        // represented by this tile before using the normal early exit.
        if (fullyCovered && level >= maximumReceiverFirstLevel)
        {
#if SVSM_RECEIVER_PAGE_MASK
            // The tile request policy intentionally stops once an eligible
            // level covers the complete receiver volume, then guarantees only
            // the coarsest request. Resolve can nevertheless consume a clean
            // resident intermediate page requested elsewhere. Cover every
            // such geometrically intersecting fallback level in the receiver
            // mask without setting REQUIRED for it.
            [loop]
            for (uint fallbackLevel = level + 1u;
                fallbackLevel < coarsestLevel;
                ++fallbackLevel)
            {
                float2 fallbackMinimum = 0.0f;
                float2 fallbackMaximum = 0.0f;
                bool fallbackFullyCovered = false;
                if (!ProjectTileVolumeAtLevel(
                        receiverCorners,
                        fallbackLevel,
                        fallbackMinimum,
                        fallbackMaximum,
                        fallbackFullyCovered))
                {
                    return false;
                }
                if (fallbackMaximum.x >= fallbackMinimum.x &&
                    fallbackMaximum.y >= fallbackMinimum.y)
                {
                    MarkReceiverPageMaskVirtualBounds(
                        fallbackMinimum,
                        fallbackMaximum,
                        fallbackLevel);
                }
            }
#endif
            if (!requestedCoarsest)
            {
                float2 coarseMinimum = 0.0f;
                float2 coarseMaximum = 0.0f;
                bool coarseFullyCovered = false;
                if (!ProjectTileVolumeAtLevel(
                        receiverCorners,
                        coarsestLevel,
                        coarseMinimum,
                        coarseMaximum,
                        coarseFullyCovered))
                {
                    return false;
                }
                if (coarseMaximum.x >= coarseMinimum.x &&
                    coarseMaximum.y >= coarseMinimum.y)
                {
                    if (!RequestVirtualBounds(
                            coarseMinimum,
                            coarseMaximum,
                            coarsestLevel))
                    {
                        return false;
                    }
                }
            }
            break;
        }
    }

    if (!requestedAny && g_Svsm.debugView != 0u)
    {
        uint ignored;
        InterlockedAdd(
            u_Counters[5],
            s_TileValidDepthCount,
            ignored);
    }
    return true;
}

bool MarkReceiverPositionAtLevel(float4 receiverPosition, uint level)
{
    float4 clip = mul(
        receiverPosition,
        g_Svsm.receiverToClip[level]);
    if (!(clip.w != 0.0f) || !all(isfinite(clip)))
        return false;
    float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0f) ||
        ndc.z < 0.0f ||
        ndc.z > 1.0f)
    {
        return false;
    }

    float2 virtualPosition =
        (ndc.xy * float2(0.5f, -0.5f) + 0.5f) *
        float(SVSM_VIRTUAL_RESOLUTION);
    if (any(virtualPosition < 0.0f) ||
        any(virtualPosition >= float(SVSM_VIRTUAL_RESOLUTION)))
    {
        return false;
    }

    int2 localPage = int2(floor(
        virtualPosition / float(SVSM_PAGE_SIZE)));
    RequestFilterFootprint(virtualPosition, localPage, level);
    return true;
}

#if SVSM_RECEIVER_PAGE_MASK
void MarkReceiverFallbackMaskAtLevel(
    float4 receiverPosition,
    uint level)
{
    float4 clip = mul(
        receiverPosition,
        g_Svsm.receiverToClip[level]);
    if (!(clip.w != 0.0f) || !all(isfinite(clip)))
        return;
    const float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0f) ||
        ndc.z < 0.0f ||
        ndc.z > 1.0f)
    {
        return;
    }

    const float2 virtualPosition =
        (ndc.xy * float2(0.5f, -0.5f) + 0.5f) *
        float(SVSM_VIRTUAL_RESOLUTION);
    if (any(virtualPosition < 0.0f) ||
        any(virtualPosition >= float(SVSM_VIRTUAL_RESOLUTION)))
    {
        return;
    }

    // This is deliberately mask-only. Resolve may select any clean resident
    // intermediate fallback page requested by another receiver, but marking
    // those receiver footprints must not set REQUIRED or allocate extra pages.
    MarkReceiverPageMaskFilterFootprint(
        virtualPosition, level);
}
#endif

void MarkPixel(uint2 pixel)
{
    if (any(pixel >= g_Svsm.cameraSize))
        return;
    float cameraDepth = t_CameraDepth[pixel];
    if (!(cameraDepth > 0.0f) || !isfinite(cameraDepth))
        return;

#if SVSM_PRECOMPOSED_CLIPMAP_TRANSFORMS
    float4 receiverPosition = ReconstructClipPosition(
        g_Svsm.cameraView,
        float2(pixel) + 0.5f,
        cameraDepth);
#else
    float4 receiverPosition = float4(
        ReconstructWorldPosition(
            g_Svsm.cameraView,
            float2(pixel) + 0.5f,
            cameraDepth),
        1.0f);
#endif
    if (!all(isfinite(receiverPosition)))
        return;

    uint firstLevel =
        GetSvsmReceiverFirstClipmapLevelFromDepth(cameraDepth);
    uint selectedLevel = SVSM_SPARSE_CLIPMAP_COUNT;
    [loop]
    for (uint level = firstLevel;
        level < SVSM_SPARSE_CLIPMAP_COUNT;
        ++level)
    {
        if (MarkReceiverPositionAtLevel(receiverPosition, level))
        {
            selectedLevel = level;
            break;
        }
    }

    if (selectedLevel == SVSM_SPARSE_CLIPMAP_COUNT)
    {
        if (g_Svsm.debugView != 0u)
            InterlockedAdd(u_Counters[5], 1u);
        return;
    }

    const uint coarsestLevel = SVSM_SPARSE_CLIPMAP_COUNT - 1u;
#if SVSM_RECEIVER_PAGE_MASK
    // Resolve accepts every geometrically covering clean resident level, not
    // only the requested finest and coarsest pages. Populate mask-only
    // coverage for those intermediate candidates so packet culling cannot
    // remove casters that a fallback receiver still needs.
    [loop]
    for (uint fallbackLevel = selectedLevel + 1u;
        fallbackLevel < coarsestLevel;
        ++fallbackLevel)
    {
        MarkReceiverFallbackMaskAtLevel(
            receiverPosition, fallbackLevel);
    }
#endif

    // The coarsest clipmap is the guaranteed complete fallback under pool or
    // render-budget pressure. It is requested independently of the selected
    // finest page, matching the published SVSM budget design.
    if (selectedLevel != coarsestLevel)
        MarkReceiverPositionAtLevel(receiverPosition, coarsestLevel);
}

[numthreads(8, 8, 1)]
void mark(
    uint2 dispatchThread : SV_DispatchThreadID,
    uint2 group : SV_GroupID,
    uint2 groupThread : SV_GroupThreadID)
{
    if (g_Svsm.markingMode == 0u)
    {
        if ((g_Svsm.flags &
                SVSM_SPARSE_FLAG_PER_PIXEL_MARKING_DEDUPE) != 0u)
        {
            uint lane = groupThread.y * 8u + groupThread.x;
            s_RequiredPageHash[lane] = SVSM_INVALID_PAGE;
            GroupMemoryBarrierWithGroupSync();

            MarkPixel(dispatchThread);

            GroupMemoryBarrierWithGroupSync();
            uint owner = s_RequiredPageHash[lane];
            if (owner != SVSM_INVALID_PAGE)
            {
                uint ignored;
                InterlockedOr(
                    u_PageTable[DecodeVirtualPage(owner)],
                    SVSM_REQUIRED_BIT,
                    ignored);
            }
            return;
        }

        MarkPixel(dispatchThread);
        return;
    }

    uint lane = groupThread.y * 8u + groupThread.x;
    s_RequiredPageHash[lane] = SVSM_INVALID_PAGE;
    if (lane == 0u)
    {
        s_TileMinimumDepth = 0xffffffffu;
        s_TileMaximumDepth = 0u;
        s_TileMinimumViewDistance = 0xffffffffu;
        s_TileMaximumViewDistance = 0u;
        s_TileValidDepthCount = 0u;
        s_TileNeedsPerPixelFallback = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint localMinimumDepth = 0xffffffffu;
    uint localMaximumDepth = 0u;
    uint localMinimumViewDistance = 0xffffffffu;
    uint localMaximumViewDistance = 0u;
    uint localValidDepthCount = 0u;
    if (g_Svsm.markingMode == 1u)
    {
        AccumulateLocalTileDepth(
            group * 8u + groupThread,
            localMinimumDepth,
            localMaximumDepth,
            localMinimumViewDistance,
            localMaximumViewDistance,
            localValidDepthCount);
    }
    else
    {
        uint2 firstPixel =
            group * 16u + groupThread * 2u;
        [unroll]
        for (uint y = 0u; y < 2u; ++y)
        {
            [unroll]
            for (uint x = 0u; x < 2u; ++x)
            {
                AccumulateLocalTileDepth(
                    firstPixel + uint2(x, y),
                    localMinimumDepth,
                    localMaximumDepth,
                    localMinimumViewDistance,
                    localMaximumViewDistance,
                    localValidDepthCount);
            }
        }
    }

    uint waveMinimumDepth = WaveActiveMin(localMinimumDepth);
    uint waveMaximumDepth = WaveActiveMax(localMaximumDepth);
    uint waveMinimumViewDistance =
        WaveActiveMin(localMinimumViewDistance);
    uint waveMaximumViewDistance =
        WaveActiveMax(localMaximumViewDistance);
    uint waveValidDepthCount = WaveActiveSum(localValidDepthCount);
    if (WaveIsFirstLane() && waveValidDepthCount > 0u)
    {
        uint ignored;
        InterlockedMin(
            s_TileMinimumDepth,
            waveMinimumDepth,
            ignored);
        InterlockedMax(
            s_TileMaximumDepth,
            waveMaximumDepth,
            ignored);
        InterlockedMin(
            s_TileMinimumViewDistance,
            waveMinimumViewDistance,
            ignored);
        InterlockedMax(
            s_TileMaximumViewDistance,
            waveMaximumViewDistance,
            ignored);
        InterlockedAdd(
            s_TileValidDepthCount,
            waveValidDepthCount,
            ignored);
    }

    GroupMemoryBarrierWithGroupSync();
    if (lane == 0u)
    {
        uint tileCoverage = g_Svsm.markingMode == 1u
            ? 8u
            : 16u;
        s_TileNeedsPerPixelFallback = MarkTile(
            group * tileCoverage,
            tileCoverage)
            ? 0u
            : 1u;
    }

    GroupMemoryBarrierWithGroupSync();
    if (s_TileNeedsPerPixelFallback != 0u)
    {
        if (g_Svsm.markingMode == 1u)
        {
            MarkPixel(group * 8u + groupThread);
        }
        else
        {
            uint2 firstPixel =
                group * 16u + groupThread * 2u;
            [unroll]
            for (uint y = 0u; y < 2u; ++y)
            {
                [unroll]
                for (uint x = 0u; x < 2u; ++x)
                    MarkPixel(firstPixel + uint2(x, y));
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();
    uint owner = s_RequiredPageHash[lane];
    if (owner != SVSM_INVALID_PAGE)
    {
        uint ignored;
        InterlockedOr(
            u_PageTable[DecodeVirtualPage(owner)],
            SVSM_REQUIRED_BIT,
            ignored);
    }
}

uint LoadPageByOwner(uint owner)
{
    return u_PageTable[DecodeVirtualPage(owner)];
}

bool IsValidOwner(uint owner)
{
    return owner <
        SVSM_SPARSE_CLIPMAP_COUNT * SVSM_PAGES_PER_CLIPMAP;
}

uint ClassifyPhysicalPageForRecycle(uint physical)
{
    if (physical >= g_Svsm.physicalPageCount)
        return SVSM_RECYCLE_CLASS_INVALID;

    uint owner = u_PhysicalOwners[physical];
    bool freePage = owner == SVSM_INVALID_PAGE;
    bool unrecentCachedPage = false;
    bool recentCachedPage = false;
    bool requiredFinePage = false;
    if (!freePage)
    {
        if (!IsValidOwner(owner))
        {
            u_PhysicalOwners[physical] = SVSM_INVALID_PAGE;
            freePage = true;
        }
        else
        {
            uint packed = LoadPageByOwner(owner);
            bool mappingIsValid =
                (packed & SVSM_RESIDENT_BIT) != 0u &&
                (packed & SVSM_PHYSICAL_MASK) == physical;
            if (!mappingIsValid)
            {
                u_PhysicalOwners[physical] = SVSM_INVALID_PAGE;
                freePage = true;
            }
            else
            {
                bool required =
                    (packed & SVSM_REQUIRED_BIT) != 0u;
                if (!required)
                {
                    const bool useRecentGrace =
                        (g_Svsm.flags &
                            SVSM_SPARSE_FLAG_RECENT_PAGE_EVICTION_GRACE) !=
                        0u;
                    if (useRecentGrace)
                    {
                        const uint lastRequiredFrame =
                            (packed >> SVSM_AGE_SHIFT) & SVSM_AGE_MASK;
                        const uint elapsedFrames =
                            (g_Svsm.frameIndex - lastRequiredFrame) &
                            SVSM_AGE_MASK;
                        recentCachedPage =
                            elapsedFrames <
                                SVSM_SPARSE_RECENT_PAGE_EVICTION_GRACE_FRAMES;
                    }
                    unrecentCachedPage = !recentCachedPage;
                }
                requiredFinePage =
                    required &&
                    DecodeVirtualPage(owner).z <
                        SVSM_SPARSE_CLIPMAP_COUNT - 1u;
            }
        }
    }

    if (freePage)
        return SVSM_RECYCLE_CLASS_FREE;
    if (unrecentCachedPage)
        return SVSM_RECYCLE_CLASS_UNRECENT;
    if (requiredFinePage)
        return SVSM_RECYCLE_CLASS_REQUIRED_FINE;
    if (recentCachedPage)
        return SVSM_RECYCLE_CLASS_RECENT;
    return SVSM_RECYCLE_CLASS_INVALID;
}

void AppendPhysicalPageForRecycleParallel(
    uint recycleClass,
    uint physical)
{
    uint listBase =
        g_Svsm.physicalPageCount *
        SVSM_SPARSE_CLIPMAP_COUNT;
    if (recycleClass == SVSM_RECYCLE_CLASS_FREE)
    {
        uint freeIndex;
        InterlockedAdd(
            u_Counters[SVSM_FREE_COUNT_COUNTER],
            1u,
            freeIndex);
        u_CompactRenderPages[listBase + freeIndex] =
            physical;
    }
    else if (recycleClass == SVSM_RECYCLE_CLASS_UNRECENT)
    {
        uint cachedIndex;
        InterlockedAdd(
            u_Counters[SVSM_UNRECENT_CACHED_COUNT_COUNTER],
            1u,
            cachedIndex);
        u_CompactRenderPages[
            listBase +
            g_Svsm.physicalPageCount +
            cachedIndex] = physical;
    }
    else if (recycleClass == SVSM_RECYCLE_CLASS_REQUIRED_FINE)
    {
        if (UseDeterministicFinePageBudget())
        {
            RecordDeterministicRequiredFineVictim(
                DecodeVirtualPage(
                    u_PhysicalOwners[physical]));
        }
        else
        {
            uint fineIndex;
            InterlockedAdd(
                u_Counters[SVSM_FINE_REQUIRED_COUNT_COUNTER],
                1u,
                fineIndex);
            u_CompactRenderPages[
                listBase +
                g_Svsm.physicalPageCount * 2u +
                fineIndex] = physical;
        }
    }
    else if (recycleClass == SVSM_RECYCLE_CLASS_RECENT)
    {
        uint recentIndex;
        InterlockedAdd(
            u_Counters[SVSM_RECENT_CACHED_COUNT_COUNTER],
            1u,
            recentIndex);
        u_CompactRenderPages[
            listBase +
            g_Svsm.physicalPageCount * 3u +
            recentIndex] = physical;
    }
}

[numthreads(64, 1, 1)]
void recycle(uint physical : SV_DispatchThreadID)
{
    if (physical >= g_Svsm.physicalPageCount)
        return;
    AppendPhysicalPageForRecycleParallel(
        ClassifyPhysicalPageForRecycle(physical),
        physical);
}

uint ReserveDeterministicRequiredFinePhysicalPage()
{
    uint victimRank;
    InterlockedAdd(
        u_Counters[SVSM_FINE_REQUIRED_CURSOR_COUNTER],
        1u,
        victimRank);
    const uint victimCount =
        u_Counters[SVSM_FINE_REQUIRED_COUNT_COUNTER];
    if (victimRank >= victimCount)
        return SVSM_INVALID_PAGE;

    // Every coarse requester owns a unique reverse-priority rank. The mask is
    // immutable after recycle, so concurrent requesters can independently
    // select their exact bit without contended claims or ordering races.
    uint remainingRank = victimRank;
    [loop]
    for (int word =
            int(SVSM_FINE_CANDIDATE_MASK_WORD_COUNT) - 1;
        word >= 0;
        --word)
    {
        uint candidates =
            u_FinePageCandidateMasks[uint(word)];
        const uint wordCount = countbits(candidates);
        if (remainingRank >= wordCount)
        {
            remainingRank -= wordCount;
            continue;
        }

        [loop]
        while (remainingRank > 0u)
        {
            candidates &=
                ~(1u << uint(firstbithigh(candidates)));
            --remainingRank;
        }
        const uint bitIndex =
            uint(firstbithigh(candidates));

        const uint globalScanIndex =
            uint(word) * 32u + bitIndex;
        const uint level =
            globalScanIndex / SVSM_PAGES_PER_CLIPMAP;
        const uint scanIndex =
            globalScanIndex % SVSM_PAGES_PER_CLIPMAP;
        const uint2 localPage =
            GetCenteredMortonLocalPage(scanIndex);
        const int2 pageTableOffset =
            g_Svsm.pageTableOffsetAndDelta[level].xy;
        const uint2 tablePage = uint2(
            WrapPage(
                int(localPage.x) +
                pageTableOffset.x),
            WrapPage(
                int(localPage.y) +
                pageTableOffset.y));
        const uint3 page = uint3(tablePage, level);
        const uint owner = EncodeVirtualPage(page);
        const uint packed = u_PageTable[page];
        const uint physical =
            packed & SVSM_PHYSICAL_MASK;
        if ((packed &
                (SVSM_RESIDENT_BIT |
                    SVSM_REQUIRED_BIT)) ==
                (SVSM_RESIDENT_BIT |
                    SVSM_REQUIRED_BIT) &&
            physical < g_Svsm.physicalPageCount &&
            u_PhysicalOwners[physical] == owner)
        {
            return physical;
        }
        // A malformed entry is unique to this rank. Fail it closed instead of
        // aliasing a later rank's valid physical page.
        return SVSM_INVALID_PAGE;
    }
    return SVSM_INVALID_PAGE;
}

uint ReserveAvailablePhysicalPage(uint requesterLevel)
{
    uint listBase =
        g_Svsm.physicalPageCount *
        SVSM_SPARSE_CLIPMAP_COUNT;
    uint freeIndex;
    InterlockedAdd(
        u_Counters[SVSM_FREE_CURSOR_COUNTER],
        1u,
        freeIndex);
    if (freeIndex < u_Counters[SVSM_FREE_COUNT_COUNTER])
    {
        return u_CompactRenderPages[listBase + freeIndex];
    }

    uint cachedIndex;
    InterlockedAdd(
        u_Counters[SVSM_UNRECENT_CACHED_CURSOR_COUNTER],
        1u,
        cachedIndex);
    if (cachedIndex <
        u_Counters[SVSM_UNRECENT_CACHED_COUNT_COUNTER])
    {
        return u_CompactRenderPages[
            listBase +
            g_Svsm.physicalPageCount +
            cachedIndex];
    }

    if ((g_Svsm.flags &
            SVSM_SPARSE_FLAG_RECENT_PAGE_EVICTION_GRACE) != 0u)
    {
        uint recentIndex;
        InterlockedAdd(
            u_Counters[SVSM_RECENT_CACHED_CURSOR_COUNTER],
            1u,
            recentIndex);
        if (recentIndex <
            u_Counters[SVSM_RECENT_CACHED_COUNT_COUNTER])
        {
            return u_CompactRenderPages[
                listBase +
                g_Svsm.physicalPageCount * 3u +
                recentIndex];
        }
    }

    if (requesterLevel ==
        SVSM_SPARSE_CLIPMAP_COUNT - 1u)
    {
        if (UseDeterministicFinePageBudget())
        {
            return
                ReserveDeterministicRequiredFinePhysicalPage();
        }

        uint fineIndex;
        InterlockedAdd(
            u_Counters[SVSM_FINE_REQUIRED_CURSOR_COUNTER],
            1u,
            fineIndex);
        if (fineIndex <
            u_Counters[SVSM_FINE_REQUIRED_COUNT_COUNTER])
        {
            return u_CompactRenderPages[
                listBase +
                g_Svsm.physicalPageCount * 2u +
                fineIndex];
        }
    }
    return SVSM_INVALID_PAGE;
}

void InvalidatePreviousOwner(uint physical, uint newOwner)
{
    uint oldOwner = u_PhysicalOwners[physical];
    if (oldOwner == newOwner ||
        oldOwner == SVSM_INVALID_PAGE ||
        !IsValidOwner(oldOwner))
    {
        return;
    }

    uint3 oldPage = DecodeVirtualPage(oldOwner);
    uint oldPacked = u_PageTable[oldPage];
    if ((oldPacked & SVSM_RESIDENT_BIT) != 0u &&
        (oldPacked & SVSM_PHYSICAL_MASK) == physical)
    {
        oldPacked &= ~SVSM_RESIDENT_BIT;
        oldPacked |= FullDepthDirtyBits();
        u_PageTable[oldPage] = oldPacked;
    }
}

void PublishScheduledPageForLevel(
    uint3 page,
    uint owner,
    uint packed,
    uint levelRenderIndex,
    uint clipmapLevel)
{
    uint physical = packed & SVSM_PHYSICAL_MASK;
    u_RenderPages[physical] = owner;
    uint compactIndex =
        clipmapLevel *
            g_Svsm.physicalPageCount +
        levelRenderIndex;
    u_CompactRenderPages[compactIndex] =
        (owner & SVSM_COMPACT_OWNER_MASK) |
        (physical << SVSM_COMPACT_PHYSICAL_SHIFT);

    if ((g_Svsm.flags &
            SVSM_SPARSE_FLAG_LEVEL_EMPTY_WORK_SKIP) != 0u)
    {
        uint ignored;
        // This counter is copied into DispatchIndirect::groupsZ before the
        // packet-fill stage. When packets exist, FillIndirect replaces it with
        // the exact per-level packet count for DrawIndexedIndirectCount.
        InterlockedMax(
            u_Counters[
                SVSM_SPARSE_LEVEL_HAS_WORK_COUNTER_BASE +
                clipmapLevel],
            SVSM_SPARSE_LEVEL_HAS_WORK_DISPATCH_GATE,
            ignored);
    }

    if ((g_Svsm.flags &
            SVSM_SPARSE_FLAG_DIRTY_PAGE_SCATTER_RASTER) != 0u &&
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_DIRTY_PAGE_SCATTER_AMPLIFICATION_GUARD) == 0u)
    {
        int2 pageTableOffset =
            g_Svsm.pageTableOffsetAndDelta[
                clipmapLevel].xy;
        uint2 localPage = uint2(
            WrapPage(int(page.x) - pageTableOffset.x),
            WrapPage(int(page.y) - pageTableOffset.y));
        uint rectangleBase =
            clipmapLevel *
            SVSM_DIRTY_PAGE_RECT_WORDS_PER_LEVEL;
        uint ignored;
        // Zero is the empty value. Encoding the minima as 63-coordinate
        // lets all four bounds use InterlockedMax after a zero clear.
        InterlockedMax(
            u_DirtyPageRectangles[rectangleBase + 0u],
            (SVSM_PAGES_PER_AXIS - 1u) - localPage.x,
            ignored);
        InterlockedMax(
            u_DirtyPageRectangles[rectangleBase + 1u],
            localPage.x,
            ignored);
        InterlockedMax(
            u_DirtyPageRectangles[rectangleBase + 2u],
            (SVSM_PAGES_PER_AXIS - 1u) - localPage.y,
            ignored);
        InterlockedMax(
            u_DirtyPageRectangles[rectangleBase + 3u],
            localPage.y,
            ignored);
    }
}

void PublishScheduledPage(
    uint3 page,
    uint owner,
    uint packed,
    uint levelRenderIndex)
{
    PublishScheduledPageForLevel(
        page,
        owner,
        packed,
        levelRenderIndex,
        g_Svsm.selectedClipmap);
}

void AllocatePageParallel(uint localPageIndex)
{
    if (localPageIndex >= SVSM_PAGES_PER_CLIPMAP ||
        g_Svsm.selectedClipmap >= SVSM_SPARSE_CLIPMAP_COUNT)
    {
        return;
    }

    uint3 page = uint3(
        localPageIndex % SVSM_PAGES_PER_AXIS,
        localPageIndex / SVSM_PAGES_PER_AXIS,
        g_Svsm.selectedClipmap);
    uint owner = EncodeVirtualPage(page);
    uint packed = u_PageTable[page];
    if ((packed & SVSM_REQUIRED_BIT) == 0u)
        return;

    if ((packed & SVSM_RESIDENT_BIT) != 0u)
    {
        uint physical = packed & SVSM_PHYSICAL_MASK;
        bool mappingIsValid =
            physical < g_Svsm.physicalPageCount &&
            u_PhysicalOwners[physical] == owner;
        if (!mappingIsValid)
        {
            packed &= ~SVSM_RESIDENT_BIT;
            packed |= FullDepthDirtyBits();
            u_PageTable[page] = packed;
        }
    }

    if ((packed & SVSM_RESIDENT_BIT) == 0u)
    {
        // The compact deterministic pass below owns new fine residency in
        // finite fine-only mode. Keeping the broad parallel pass read-only for
        // missing fine pages prevents atomic arrival order from deciding which
        // virtual pages survive pool pressure.
        if (UseDeterministicFinePageBudget() &&
            g_Svsm.selectedClipmap <
                SVSM_SPARSE_CLIPMAP_COUNT - 1u)
        {
            RecordDeterministicFinePageCandidate(page);
            return;
        }

        uint physical = ReserveAvailablePhysicalPage(
            g_Svsm.selectedClipmap);
        if (physical == SVSM_INVALID_PAGE ||
            physical >= g_Svsm.physicalPageCount)
        {
            if (g_Svsm.debugView != 0u)
            {
                InterlockedAdd(
                    u_Counters[SVSM_ALLOCATION_FAILURE_COUNTER],
                    1u);
            }
            return;
        }

        InvalidatePreviousOwner(physical, owner);
        u_PhysicalOwners[physical] = owner;
        packed =
            (physical & SVSM_PHYSICAL_MASK) |
            SVSM_RESIDENT_BIT |
            SVSM_REQUIRED_BIT |
            FullDepthDirtyBits();
    }

    uint frameAge = g_Svsm.frameIndex & SVSM_AGE_MASK;
    packed = (packed & ~(SVSM_AGE_MASK << SVSM_AGE_SHIFT)) |
        (frameAge << SVSM_AGE_SHIFT);
    u_PageTable[page] = packed;

    if ((packed & SVSM_DIRTY_BIT) == 0u)
        return;

    // Finite fine-only budgets publish their stable winners in one global
    // follow-up pass after every fine residency scan is complete.
    if (UseDeterministicFinePageBudget() &&
        g_Svsm.selectedClipmap <
            SVSM_SPARSE_CLIPMAP_COUNT - 1u)
    {
        RecordDeterministicFinePageCandidate(page);
        return;
    }

    // The published/reference path keeps the coarsest clipmap outside the
    // fine-page budget so coarse fallback cannot be starved. The independent
    // all-level safety toggle instead shares the same reservation with the
    // coarsest clipmap, hard-bounding scheduled clear, cull, and raster work
    // after the fixed allocation scan.
    bool pageRenderBudgetApplies =
        g_Svsm.selectedClipmap !=
            SVSM_SPARSE_CLIPMAP_COUNT - 1u ||
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_COARSEST_PAGE_RENDER_BUDGET) != 0u;
    if (pageRenderBudgetApplies)
    {
        // Best-effort relaxed read: once the monotonic per-frame counter is
        // saturated, avoid sending every remaining dirty page through the
        // same atomic. A stale-low read only reaches the unchanged atomic and
        // its post-check, which remain the allocation authority.
        [branch]
        if ((g_Svsm.flags &
                SVSM_SPARSE_FLAG_ALLOCATION_BUDGET_SATURATION_EARLY_OUT) !=
                0u)
        {
            if (u_Counters[SVSM_RENDER_RESERVATION_COUNTER] >=
                    g_Svsm.pageRenderBudget)
            {
                return;
            }
        }
        uint fineRenderReservation;
        InterlockedAdd(
            u_Counters[SVSM_RENDER_RESERVATION_COUNTER],
            1u,
            fineRenderReservation);
        if (fineRenderReservation >= g_Svsm.pageRenderBudget)
            return;
    }

    uint levelRenderIndex;
    InterlockedAdd(
        u_Counters[
            SVSM_LEVEL_RENDER_COUNTER_BASE +
            g_Svsm.selectedClipmap],
        1u,
        levelRenderIndex);
    PublishScheduledPage(
        page,
        owner,
        packed,
        levelRenderIndex);
}

[numthreads(64, 1, 1)]
void allocate(uint localPageIndex : SV_DispatchThreadID)
{
    AllocatePageParallel(localPageIndex);
}

[numthreads(1, 1, 1)]
void scheduleFine(uint3 groupId : SV_GroupID)
{
    // C++ intentionally dispatches exactly one global selector thread after
    // all five fine allocation scans have populated their compact masks. Keep
    // a defensive guard so a future caller cannot race the direct counter
    // stores below.
    if (any(groupId != uint3(0u, 0u, 0u)) ||
        !UseDeterministicFinePageBudget())
    {
        return;
    }

    uint reservation =
        u_Counters[SVSM_RENDER_RESERVATION_COUNTER];
    if (reservation >= g_Svsm.pageRenderBudget)
        return;

    // A failed reservation exhausts every recycle class available to fine
    // levels. Keep walking the ordered masks, but only reload and publish
    // resident-dirty pages after that point.
    bool poolExhausted = false;
    [loop]
    for (uint level = 0u;
        level < SVSM_FINE_CLIPMAP_COUNT &&
            reservation < g_Svsm.pageRenderBudget;
        ++level)
    {
        const int2 pageTableOffset =
            g_Svsm.pageTableOffsetAndDelta[level].xy;
        uint levelRenderIndex =
            u_Counters[
                SVSM_LEVEL_RENDER_COUNTER_BASE + level];

        [loop]
        for (uint word = 0u;
            word < SVSM_FINE_CANDIDATE_MASK_WORDS_PER_LEVEL &&
                reservation < g_Svsm.pageRenderBudget;
            ++word)
        {
            uint candidates =
                u_FinePageCandidateMasks[
                    level *
                        SVSM_FINE_CANDIDATE_MASK_WORDS_PER_LEVEL +
                    word];
            [loop]
            while (candidates != 0u &&
                reservation < g_Svsm.pageRenderBudget)
            {
                const uint bit =
                    uint(firstbitlow(candidates));
                candidates &= candidates - 1u;
                const uint scanIndex = word * 32u + bit;
                const uint2 localPage =
                    GetCenteredMortonLocalPage(scanIndex);
                const uint2 tablePage = uint2(
                    WrapPage(
                        int(localPage.x) +
                        pageTableOffset.x),
                    WrapPage(
                        int(localPage.y) +
                        pageTableOffset.y));
                const uint3 page = uint3(tablePage, level);
                const uint owner = EncodeVirtualPage(page);

                // The preceding parallel scans repaired the complete fine
                // range and their UAV barrier is authoritative. Reload every
                // selected bit so earlier recycling cannot publish stale
                // ownership and so pool exhaustion can retain resident work
                // without a second mask.
                uint packed = u_PageTable[page];
                if ((packed & SVSM_REQUIRED_BIT) == 0u)
                    continue;
                uint physical = packed & SVSM_PHYSICAL_MASK;
                const bool resident =
                    (packed & SVSM_RESIDENT_BIT) != 0u;
                const bool validResident =
                    resident &&
                    physical < g_Svsm.physicalPageCount &&
                    u_PhysicalOwners[physical] == owner;
                if (validResident)
                {
                    if ((packed & SVSM_DIRTY_BIT) == 0u)
                        continue;
                }
                else
                {
                    if (resident)
                    {
                        packed &= ~SVSM_RESIDENT_BIT;
                        packed |= FullDepthDirtyBits();
                        u_PageTable[page] = packed;
                    }
                    if (poolExhausted)
                        continue;

                    physical = ReserveAvailablePhysicalPage(level);
                    if (physical == SVSM_INVALID_PAGE ||
                        physical >= g_Svsm.physicalPageCount)
                    {
                        if (g_Svsm.debugView != 0u)
                        {
                            uint ignored;
                            InterlockedAdd(
                                u_Counters[
                                    SVSM_ALLOCATION_FAILURE_COUNTER],
                                1u,
                                ignored);
                        }
                        poolExhausted = true;
                        continue;
                    }

                    InvalidatePreviousOwner(physical, owner);
                    u_PhysicalOwners[physical] = owner;
                    packed =
                        (physical & SVSM_PHYSICAL_MASK) |
                        SVSM_RESIDENT_BIT |
                        SVSM_REQUIRED_BIT |
                        FullDepthDirtyBits();
                    const uint frameAge =
                        g_Svsm.frameIndex & SVSM_AGE_MASK;
                    packed =
                        (packed &
                            ~(SVSM_AGE_MASK <<
                                SVSM_AGE_SHIFT)) |
                        (frameAge << SVSM_AGE_SHIFT);
                    u_PageTable[page] = packed;
                }

                if (physical >= g_Svsm.physicalPageCount ||
                    u_PhysicalOwners[physical] != owner ||
                    levelRenderIndex >=
                        g_Svsm.physicalPageCount)
                {
                    continue;
                }

                PublishScheduledPageForLevel(
                    page,
                    owner,
                    packed,
                    levelRenderIndex,
                    level);
                ++levelRenderIndex;
                ++reservation;
            }
        }

        u_Counters[
            SVSM_LEVEL_RENDER_COUNTER_BASE + level] =
            levelRenderIndex;
    }

    u_Counters[SVSM_RENDER_RESERVATION_COUNTER] =
        reservation;
}

bool TryLoadCompactRenderPage(
    uint renderPageIndex,
    out uint owner,
    out uint physical)
{
    owner = SVSM_INVALID_PAGE;
    physical = SVSM_INVALID_PAGE;
    if (g_Svsm.selectedClipmap >= SVSM_SPARSE_CLIPMAP_COUNT)
        return false;

    uint compactIndex =
        g_Svsm.selectedClipmap * g_Svsm.physicalPageCount +
        renderPageIndex;
    uint compactPage = u_CompactRenderPages[compactIndex];
    if (compactPage == SVSM_INVALID_PAGE)
        return false;

    owner = compactPage & SVSM_COMPACT_OWNER_MASK;
    physical = compactPage >> SVSM_COMPACT_PHYSICAL_SHIFT;
    if (!IsValidOwner(owner) ||
        physical >= g_Svsm.physicalPageCount ||
        DecodeVirtualPage(owner).z != g_Svsm.selectedClipmap)
    {
        return false;
    }

    const uint packed = LoadPageByOwner(owner);
    return PageNeedsPacketCaster(packed, false) &&
        (packed & SVSM_PHYSICAL_MASK) == physical &&
        u_PhysicalOwners[physical] == owner &&
        u_RenderPages[physical] == owner;
}

[numthreads(128, 1, 1)]
void clearPages(
    uint3 page : SV_GroupID,
    uint3 pageThread : SV_GroupThreadID)
{
    uint physical = page.x;
    uint owner = SVSM_INVALID_PAGE;
    if ((g_Svsm.flags &
            SVSM_SPARSE_FLAG_COMPACT_PAGE_DISPATCH) != 0u)
    {
        if (!TryLoadCompactRenderPage(
                page.x, owner, physical))
        {
            return;
        }
    }
    else if (physical >= g_Svsm.physicalPageCount)
    {
        return;
    }
    else
    {
        owner = u_RenderPages[physical];
        if (owner == SVSM_INVALID_PAGE)
            return;
    }

    if (!IsValidOwner(owner))
        return;
    uint packed = LoadPageByOwner(owner);
    bool validScheduledPage =
        (packed & (SVSM_RESIDENT_BIT | SVSM_DIRTY_BIT)) ==
            (SVSM_RESIDENT_BIT | SVSM_DIRTY_BIT) &&
        (packed & SVSM_PHYSICAL_MASK) == physical &&
        u_PhysicalOwners[physical] == owner;
    if (!validScheduledPage)
        return;

    uint2 physicalCoordinate = uint2(
        physical % SVSM_PAGES_PER_AXIS,
        physical / SVSM_PAGES_PER_AXIS);
    uint2 pageBase = physicalCoordinate * SVSM_PAGE_SIZE;
    bool pairedDepth = PairedStaticDynamicDepthEnabled();
    bool rebuildStatic =
        pairedDepth &&
        (packed & SVSM_PAGE_STATIC_DIRTY_BIT) != 0u;
    if (rebuildStatic &&
        StaticDepthHierarchyResourceAvailable() &&
        pageThread.x == 0u)
    {
        // Invalidate first. The later build pass publishes a nonzero
        // owner/epoch tag only after every hierarchy node is complete.
        u_StaticDepthHierarchy[
            StaticDepthHierarchyWordBase(physical) +
            SVSM_STATIC_DEPTH_HIERARCHY_TAG_OFFSET] = 0u;
    }
    [loop]
    for (uint y = 0u; y < SVSM_PAGE_SIZE; ++y)
    {
        uint2 texel = pageBase + uint2(pageThread.x, y);
        if (rebuildStatic)
        {
            u_PhysicalDepth[uint3(texel, 0u)] = 0u;
            u_PhysicalDepth[uint3(texel, 1u)] = 0u;
        }
        else if (pairedDepth)
        {
            u_PhysicalDepth[uint3(texel, 0u)] =
                u_PhysicalDepth[uint3(texel, 1u)];
        }
        else
        {
            u_PhysicalDepth[uint3(texel, 0u)] = 0u;
        }
    }
}

[numthreads(64, 1, 1)]
void buildStaticDepthHierarchy(
    uint3 page : SV_GroupID,
    uint3 pageThread : SV_GroupThreadID)
{
    if (pageThread.x == 0u)
    {
        s_StaticDepthHierarchyOwner = SVSM_INVALID_PAGE;
        s_StaticDepthHierarchyPhysical = SVSM_INVALID_PAGE;
        s_StaticDepthHierarchyValid = 0u;
        s_StaticDepthHierarchyBuild = 0u;
        s_DeferredStaticDepthMerge = 0u;

        uint owner = SVSM_INVALID_PAGE;
        uint physical = SVSM_INVALID_PAGE;
        uint packed = 0u;
        bool validPage = false;
        const bool buildHierarchy =
            StaticDepthHierarchyCullingEnabled() &&
            StaticDepthHierarchyResourceAvailable() &&
            g_Svsm.hierarchyGeneration != 0u;
#if SVSM_DEFER_STATIC_MERGE
        const bool mergeStaticDepth =
            PairedStaticDynamicDepthEnabled();
#else
        const bool mergeStaticDepth = false;
#endif
        if (buildHierarchy || mergeStaticDepth)
        {
            if (buildHierarchy &&
                StaticDepthHierarchyBootstrapEnabled())
            {
                physical = page.x;
                if (physical < g_Svsm.physicalPageCount)
                {
                    owner = u_PhysicalOwners[physical];
                    if (IsValidOwner(owner))
                    {
                        packed = LoadPageByOwner(owner);
                        const bool staticDirty =
                            (packed &
                                SVSM_PAGE_STATIC_DIRTY_BIT) != 0u;
                        const bool scheduledStaticDirty =
                            (packed & SVSM_DIRTY_BIT) != 0u &&
                            u_RenderPages[physical] == owner;
                        validPage =
                            (packed & SVSM_RESIDENT_BIT) != 0u &&
                            (packed & SVSM_PHYSICAL_MASK) ==
                                physical &&
                            (!staticDirty ||
                                scheduledStaticDirty);
                    }
                }
            }
            else if ((g_Svsm.flags &
                    SVSM_SPARSE_FLAG_COMPACT_PAGE_DISPATCH) != 0u)
            {
                if (TryLoadCompactRenderPage(
                        page.x, owner, physical))
                {
                    packed = LoadPageByOwner(owner);
                    validPage = true;
                }
            }
            else
            {
                // The reference submission path has no compact indirect
                // dispatch. Scan the fixed pool, but accept only the current
                // scheduled owner so over-budget pages remain untouched.
                physical = page.x;
                if (physical < g_Svsm.physicalPageCount)
                {
                    owner = u_RenderPages[physical];
                    if (IsValidOwner(owner))
                    {
                        packed = LoadPageByOwner(owner);
                        validPage =
                            PageNeedsPacketCaster(packed, false) &&
                            (packed & SVSM_PHYSICAL_MASK) ==
                                physical &&
                            u_PhysicalOwners[physical] == owner &&
                            u_RenderPages[physical] == owner;
                    }
                }
            }
        }
        if (validPage)
        {
            const bool staticDirty =
                (packed & SVSM_PAGE_STATIC_DIRTY_BIT) != 0u;
            const bool scheduledStaticDirty =
                (packed &
                    (SVSM_RESIDENT_BIT |
                        SVSM_DIRTY_BIT |
                        SVSM_PAGE_STATIC_DIRTY_BIT)) ==
                    (SVSM_RESIDENT_BIT |
                        SVSM_DIRTY_BIT |
                        SVSM_PAGE_STATIC_DIRTY_BIT) &&
                (packed & SVSM_PHYSICAL_MASK) == physical &&
                physical < g_Svsm.physicalPageCount &&
                u_PhysicalOwners[physical] == owner &&
                u_RenderPages[physical] == owner;
#if SVSM_DEFER_STATIC_MERGE
            s_DeferredStaticDepthMerge =
                mergeStaticDepth && scheduledStaticDirty
                ? 1u
                : 0u;
#endif

            if (buildHierarchy)
            {
                const uint hierarchyBase =
                    StaticDepthHierarchyWordBase(physical);
                const uint tag = u_StaticDepthHierarchy[
                    hierarchyBase +
                    SVSM_STATIC_DEPTH_HIERARCHY_TAG_OFFSET];
                const bool tagValid =
                    (tag >>
                        SVSM_STATIC_DEPTH_HIERARCHY_TAG_EPOCH_SHIFT) !=
                        0u &&
                    (tag &
                        SVSM_STATIC_DEPTH_HIERARCHY_TAG_OWNER_MASK) ==
                        owner;
                if (staticDirty || !tagValid)
                {
                    // A missing tag can remain after queries were disabled
                    // while static content changed. The complete paired
                    // static slice can rebuild it without a shadow redraw.
                    s_StaticDepthHierarchyBuild = 1u;
                }
            }

            if (s_DeferredStaticDepthMerge != 0u ||
                s_StaticDepthHierarchyBuild != 0u)
            {
                s_StaticDepthHierarchyOwner = owner;
                s_StaticDepthHierarchyPhysical = physical;
                s_StaticDepthHierarchyValid = 1u;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (s_StaticDepthHierarchyValid == 0u)
        return;

    const uint physical = s_StaticDepthHierarchyPhysical;
    const uint cell = pageThread.x;
    const uint2 cellCoordinate = uint2(
        cell % SVSM_STATIC_DEPTH_HIERARCHY_BASE_AXIS,
        cell / SVSM_STATIC_DEPTH_HIERARCHY_BASE_AXIS);
    const uint2 physicalPageCoordinate = uint2(
        physical % SVSM_PAGES_PER_AXIS,
        physical / SVSM_PAGES_PER_AXIS);
    const uint2 cellBase =
        physicalPageCoordinate * SVSM_PAGE_SIZE +
        cellCoordinate *
            SVSM_STATIC_DEPTH_HIERARCHY_BASE_CELL_WIDTH;

    uint minimumDepth = 0xffffffffu;
    [unroll]
    for (uint y = 0u;
        y < SVSM_STATIC_DEPTH_HIERARCHY_BASE_CELL_WIDTH;
        ++y)
    {
        [unroll]
        for (uint x = 0u;
            x < SVSM_STATIC_DEPTH_HIERARCHY_BASE_CELL_WIDTH;
            ++x)
        {
            const uint2 texel = cellBase + uint2(x, y);
            const uint staticDepth =
                u_PhysicalDepth[uint3(texel, 1u)];
#if SVSM_DEFER_STATIC_MERGE
            if (s_DeferredStaticDepthMerge != 0u)
            {
                // Reverse Z keeps the nearest caster with uint max. Every
                // lane owns distinct texels, so the post-raster merge needs
                // no atomics and cannot overwrite nearer dynamic depth.
                u_PhysicalDepth[uint3(texel, 0u)] = max(
                    u_PhysicalDepth[uint3(texel, 0u)],
                    staticDepth);
            }
#endif
            if (s_StaticDepthHierarchyBuild != 0u)
                minimumDepth = min(minimumDepth, staticDepth);
        }
    }

    if (s_StaticDepthHierarchyBuild == 0u)
        return;

    const uint hierarchyBase =
        StaticDepthHierarchyWordBase(physical);
    s_StaticDepthHierarchyValues[cell] = minimumDepth;
    u_StaticDepthHierarchy[
        hierarchyBase +
        SVSM_STATIC_DEPTH_HIERARCHY_BASE_OFFSET +
        cell] = minimumDepth;
    GroupMemoryBarrierWithGroupSync();

    uint levelOneMinimum = 0xffffffffu;
    if (cell < 16u)
    {
        const uint2 node = uint2(cell & 3u, cell >> 2u);
        const uint2 child = node * 2u;
        const uint childRow =
            SVSM_STATIC_DEPTH_HIERARCHY_BASE_AXIS;
        const uint childIndex =
            child.y * childRow + child.x;
        levelOneMinimum = min(
            min(
                s_StaticDepthHierarchyValues[childIndex],
                s_StaticDepthHierarchyValues[childIndex + 1u]),
            min(
                s_StaticDepthHierarchyValues[
                    childIndex + childRow],
                s_StaticDepthHierarchyValues[
                    childIndex + childRow + 1u]));
    }
    // The level-one outputs alias base-level shared indices. All lanes must
    // finish reading the complete base first or an early writer can corrupt a
    // later lane's reduction input.
    GroupMemoryBarrierWithGroupSync();
    if (cell < 16u)
    {
        s_StaticDepthHierarchyValues[cell] = levelOneMinimum;
        u_StaticDepthHierarchy[
            hierarchyBase +
            SVSM_STATIC_DEPTH_HIERARCHY_LEVEL_ONE_OFFSET +
            cell] = levelOneMinimum;
    }
    GroupMemoryBarrierWithGroupSync();

    uint levelTwoMinimum = 0xffffffffu;
    if (cell < 4u)
    {
        const uint2 node = uint2(cell & 1u, cell >> 1u);
        const uint2 child = node * 2u;
        const uint childRow =
            SVSM_STATIC_DEPTH_HIERARCHY_LEVEL_ONE_AXIS;
        const uint childIndex =
            child.y * childRow + child.x;
        levelTwoMinimum = min(
            min(
                s_StaticDepthHierarchyValues[childIndex],
                s_StaticDepthHierarchyValues[childIndex + 1u]),
            min(
                s_StaticDepthHierarchyValues[
                    childIndex + childRow],
                s_StaticDepthHierarchyValues[
                    childIndex + childRow + 1u]));
    }
    // Level two aliases level-one shared indices in the same way.
    GroupMemoryBarrierWithGroupSync();
    if (cell < 4u)
    {
        s_StaticDepthHierarchyValues[cell] = levelTwoMinimum;
        u_StaticDepthHierarchy[
            hierarchyBase +
            SVSM_STATIC_DEPTH_HIERARCHY_LEVEL_TWO_OFFSET +
            cell] = levelTwoMinimum;
    }
    GroupMemoryBarrierWithGroupSync();

    if (cell == 0u)
    {
        const uint rootMinimum = min(
            min(
                s_StaticDepthHierarchyValues[0u],
                s_StaticDepthHierarchyValues[1u]),
            min(
                s_StaticDepthHierarchyValues[2u],
                s_StaticDepthHierarchyValues[3u]));
        u_StaticDepthHierarchy[
            hierarchyBase +
            SVSM_STATIC_DEPTH_HIERARCHY_ROOT_OFFSET] =
                rootMinimum;
    }
    DeviceMemoryBarrierWithGroupSync();
    if (cell == 0u)
    {
        const uint epoch =
            ((g_Svsm.hierarchyGeneration - 1u) %
                SVSM_STATIC_DEPTH_HIERARCHY_TAG_EPOCH_MASK) + 1u;
        u_StaticDepthHierarchy[
            hierarchyBase +
            SVSM_STATIC_DEPTH_HIERARCHY_TAG_OFFSET] =
                PackStaticDepthHierarchyTag(
                    s_StaticDepthHierarchyOwner,
                    epoch);
        if (g_Svsm.debugView != 0u)
        {
            InterlockedAdd(
                u_Counters[
                    SVSM_STATIC_DEPTH_HIERARCHY_BUILT_PAGE_COUNTER],
                1u);
        }
    }
}

[numthreads(64, 1, 1)]
void finalize(
    uint3 dispatchThread : SV_DispatchThreadID,
    uint3 group : SV_GroupID,
    uint3 groupThread : SV_GroupThreadID)
{
    uint physical = dispatchThread.x;
    uint owner = SVSM_INVALID_PAGE;
    if ((g_Svsm.flags &
            SVSM_SPARSE_FLAG_COMPACT_PAGE_DISPATCH) != 0u)
    {
        if (groupThread.x != 0u ||
            !TryLoadCompactRenderPage(
                group.x, owner, physical))
        {
            return;
        }
    }
    else
    {
        if (physical >= g_Svsm.physicalPageCount)
            return;
        owner = u_RenderPages[physical];
        if (owner == SVSM_INVALID_PAGE)
            return;
    }
    uint3 page = DecodeVirtualPage(owner);
    uint packed = u_PageTable[page];
    if ((packed & SVSM_RESIDENT_BIT) != 0u &&
        (packed & SVSM_PHYSICAL_MASK) == physical &&
        u_PhysicalOwners[physical] == owner &&
        u_RenderPages[physical] == owner)
    {
        u_PageTable[page] =
            packed &
                ~(SVSM_DIRTY_BIT | SVSM_PAGE_STATIC_DIRTY_BIT);
        if (g_Svsm.debugView != 0u)
            InterlockedAdd(u_Counters[3], 1u);
    }
}

[numthreads(64, 1, 1)]
void stats(uint virtualPageIndex : SV_DispatchThreadID)
{
    const uint virtualPageCount =
        SVSM_SPARSE_CLIPMAP_COUNT *
        SVSM_PAGES_PER_CLIPMAP;
    if (virtualPageIndex >= virtualPageCount)
        return;

    uint3 page = DecodeVirtualPage(virtualPageIndex);
    uint packed = u_PageTable[page];
    bool resident = (packed & SVSM_RESIDENT_BIT) != 0u;
    bool required = (packed & SVSM_REQUIRED_BIT) != 0u;
    bool dirty = (packed & SVSM_DIRTY_BIT) != 0u;
    uint physical = packed & SVSM_PHYSICAL_MASK;
    bool validResident =
        resident &&
        physical < g_Svsm.physicalPageCount &&
        u_PhysicalOwners[physical] == virtualPageIndex;
    bool scheduled = false;
    if (validResident)
        scheduled = u_RenderPages[physical] == virtualPageIndex;
    if (required)
        InterlockedAdd(u_Counters[0], 1u);
    // Nonresident entries intentionally retain full dirty bits so that a
    // later allocation cannot expose uninitialized depth.  The user-facing
    // dirty-page counter is actionable resident work, not that conservative
    // initialization state.
    if (validResident && dirty)
        InterlockedAdd(u_Counters[10], 1u);
    if (validResident)
        InterlockedAdd(u_Counters[4], 1u);
    if (validResident && !required)
        InterlockedAdd(u_Counters[9], 1u);
    if (required && dirty && !scheduled)
        InterlockedAdd(u_Counters[11], 1u);
}

[numthreads(64, 1, 1)]
void buildScheduledPageTileMasks(
    uint3 group : SV_GroupID,
    uint3 groupThread : SV_GroupThreadID)
{
    const uint level = group.z;
    if (level >= SVSM_SPARSE_CLIPMAP_COUNT)
        return;

    if (groupThread.x == 0u)
    {
        s_ScheduledTileAnyLow = 0u;
        s_ScheduledTileAnyHigh = 0u;
        s_ScheduledTileStaticLow = 0u;
        s_ScheduledTileStaticHigh = 0u;
        s_ScheduledTileMaskInvalid =
            g_Svsm.hierarchyGeneration == 0u ? 1u : 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint rawPageCount =
        u_Counters[SVSM_LEVEL_RENDER_COUNTER_BASE + level];
    if (rawPageCount > g_Svsm.physicalPageCount)
    {
        InterlockedOr(s_ScheduledTileMaskInvalid, 1u);
    }
    const uint pageCount = min(
        rawPageCount, g_Svsm.physicalPageCount);
    const int2 pageTableOffset =
        g_Svsm.pageTableOffsetAndDelta[level].xy;
    [loop]
    for (uint pageIndex = groupThread.x;
        pageIndex < pageCount;
        pageIndex += 64u)
    {
        const uint compactIndex =
            level * g_Svsm.physicalPageCount + pageIndex;
        const uint compactPage =
            u_CompactRenderPages[compactIndex];
        const uint owner =
            compactPage & SVSM_COMPACT_OWNER_MASK;
        const uint physical =
            compactPage >> SVSM_COMPACT_PHYSICAL_SHIFT;
        const uint ownerLevel =
            owner / SVSM_PAGES_PER_CLIPMAP;
        const uint tablePageIndex =
            owner % SVSM_PAGES_PER_CLIPMAP;
        const uint2 tablePage = uint2(
            tablePageIndex % SVSM_PAGES_PER_AXIS,
            tablePageIndex / SVSM_PAGES_PER_AXIS);

        bool valid =
            ownerLevel == level &&
            physical < g_Svsm.physicalPageCount;
        if (valid)
        {
            const uint packed =
                u_PageTable[uint3(tablePage, level)];
            const bool staticDirty =
                (packed & SVSM_PAGE_STATIC_DIRTY_BIT) != 0u;
            valid =
                PageNeedsPacketCaster(packed, false) &&
                (packed & SVSM_PHYSICAL_MASK) == physical &&
                u_RenderPages[physical] == owner &&
                u_PhysicalOwners[physical] == owner &&
                (!staticDirty ||
                    PairedStaticDynamicDepthEnabled());
            if (valid)
            {
                const int2 localPage = int2(
                    WrapPage(
                        int(tablePage.x) -
                        pageTableOffset.x),
                    WrapPage(
                        int(tablePage.y) -
                        pageTableOffset.y));
                const uint tile =
                    uint(localPage.y) /
                        SVSM_SCHEDULED_TILE_PAGE_WIDTH *
                        SVSM_SCHEDULED_TILES_PER_AXIS +
                    uint(localPage.x) /
                        SVSM_SCHEDULED_TILE_PAGE_WIDTH;
                const uint bit = 1u << (tile & 31u);
                if (tile < 32u)
                {
                    InterlockedOr(
                        s_ScheduledTileAnyLow, bit);
                    if (staticDirty)
                    {
                        InterlockedOr(
                            s_ScheduledTileStaticLow, bit);
                    }
                }
                else
                {
                    InterlockedOr(
                        s_ScheduledTileAnyHigh, bit);
                    if (staticDirty)
                    {
                        InterlockedOr(
                            s_ScheduledTileStaticHigh, bit);
                    }
                }
            }
        }
        if (!valid)
            InterlockedOr(s_ScheduledTileMaskInvalid, 1u);
    }

    GroupMemoryBarrierWithGroupSync();
    if (groupThread.x == 0u)
    {
        const uint maskBase =
            level *
            SVSM_SCHEDULED_TILE_MASK_WORDS_PER_LEVEL;
        // Generation is the validity word and is published last. Any failed
        // entry validation leaves it at zero so the query path fails open.
        u_ScheduledPageTileMasks[maskBase] = 0u;
        u_ScheduledPageTileMasks[maskBase + 1u] =
            s_ScheduledTileAnyLow;
        u_ScheduledPageTileMasks[maskBase + 2u] =
            s_ScheduledTileAnyHigh;
        u_ScheduledPageTileMasks[maskBase + 3u] =
            s_ScheduledTileStaticLow;
        u_ScheduledPageTileMasks[maskBase + 4u] =
            s_ScheduledTileStaticHigh;
        DeviceMemoryBarrier();
        if (s_ScheduledTileMaskInvalid == 0u)
        {
            u_ScheduledPageTileMasks[maskBase] =
                g_Svsm.hierarchyGeneration;
        }
    }
}

#if SVSM_SCHEDULED_TILE_MASK
uint QueryScheduledPageTileMask(
    uint level,
    uint2 minimumPage,
    uint2 maximumPage,
    bool staticCaster)
{
    const uint maskBase =
        level * SVSM_SCHEDULED_TILE_MASK_WORDS_PER_LEVEL;
    const uint generation =
        u_ScheduledPageTileMasks[maskBase];
    if (g_Svsm.hierarchyGeneration == 0u ||
        generation != g_Svsm.hierarchyGeneration)
    {
        return 0u;
    }
    const uint anyLow =
        u_ScheduledPageTileMasks[maskBase + 1u];
    const uint anyHigh =
        u_ScheduledPageTileMasks[maskBase + 2u];
    const uint staticLow =
        u_ScheduledPageTileMasks[maskBase + 3u];
    const uint staticHigh =
        u_ScheduledPageTileMasks[maskBase + 4u];
    if ((staticLow & ~anyLow) != 0u ||
        (staticHigh & ~anyHigh) != 0u)
    {
        return 0u;
    }

    const uint2 minimumTile =
        minimumPage / SVSM_SCHEDULED_TILE_PAGE_WIDTH;
    const uint2 maximumTile =
        maximumPage / SVSM_SCHEDULED_TILE_PAGE_WIDTH;
    const uint xMask =
        ((1u << (maximumTile.x + 1u)) - 1u) &
        ~((1u << minimumTile.x) - 1u);
    uint queryLow = 0u;
    uint queryHigh = 0u;
    [loop]
    for (uint tileY = minimumTile.y;
        tileY <= maximumTile.y;
        ++tileY)
    {
        if (tileY < 4u)
            queryLow |= xMask << (tileY * 8u);
        else
            queryHigh |= xMask << ((tileY - 4u) * 8u);
    }

    const bool useStatic =
        staticCaster && PairedStaticDynamicDepthEnabled();
    const uint selectedLow =
        useStatic ? staticLow : anyLow;
    const uint selectedHigh =
        useStatic ? staticHigh : anyHigh;
    return ((selectedLow & queryLow) |
        (selectedHigh & queryHigh)) == 0u
        ? 1u
        : 2u;
}
#endif

#if SVSM_RECEIVER_PAGE_MASK
// 0: unavailable/invalid (fail open), 1: receiver overlap,
// 2: valid mask with no receiver overlap.
uint QueryReceiverPageMask(
    SparseVirtualShadowMapPacketMetadata metadata,
    uint owner,
    uint2 localPage)
{
    if (!ReceiverPageMaskCullingEnabled() ||
        g_Svsm.hierarchyGeneration == 0u ||
        owner >=
            SVSM_SPARSE_CLIPMAP_COUNT * SVSM_PAGES_PER_CLIPMAP ||
        any(localPage >= SVSM_PAGES_PER_AXIS) ||
        metadata.packedMinimumTexel ==
            SVSM_PACKET_PAGE_INVALID_BOUNDS ||
        metadata.packedMinimumTexel ==
            SVSM_PACKET_PAGE_EMPTY_BOUNDS ||
        metadata.packedMaximumTexel ==
            SVSM_PACKET_PAGE_INVALID_BOUNDS ||
        metadata.packedMaximumTexel ==
            SVSM_PACKET_PAGE_EMPTY_BOUNDS)
    {
        return 0u;
    }

    const uint2 packetMinimumTexel = uint2(
        metadata.packedMinimumTexel & 0xffffu,
        metadata.packedMinimumTexel >> 16u);
    const uint2 packetMaximumTexel = uint2(
        metadata.packedMaximumTexel & 0xffffu,
        metadata.packedMaximumTexel >> 16u);
    if (any(packetMinimumTexel > packetMaximumTexel) ||
        any(packetMaximumTexel >= SVSM_VIRTUAL_RESOLUTION))
    {
        return 0u;
    }

    const uint2 pageMinimumTexel =
        localPage * SVSM_PAGE_SIZE;
    const uint2 pageMaximumTexel =
        pageMinimumTexel + SVSM_PAGE_SIZE - 1u;
    const uint2 intersectionMinimum =
        max(packetMinimumTexel, pageMinimumTexel);
    const uint2 intersectionMaximum =
        min(packetMaximumTexel, pageMaximumTexel);
    if (any(intersectionMinimum > intersectionMaximum))
        return 0u;

    const uint2 minimumCell =
        (intersectionMinimum - pageMinimumTexel) /
        SVSM_RECEIVER_PAGE_MASK_CELL_WIDTH;
    const uint2 maximumCell =
        (intersectionMaximum - pageMinimumTexel) /
        SVSM_RECEIVER_PAGE_MASK_CELL_WIDTH;
    if (any(minimumCell > maximumCell) ||
        any(maximumCell >= SVSM_RECEIVER_PAGE_MASK_AXIS))
    {
        return 0u;
    }

    const uint maskBase =
        owner * SVSM_RECEIVER_PAGE_MASK_WORDS_PER_PAGE;
    if (u_ReceiverPageMasks[
            maskBase + SVSM_RECEIVER_PAGE_MASK_TAG_OFFSET] !=
        g_Svsm.hierarchyGeneration)
    {
        return 0u;
    }

    [unroll]
    for (uint quadrantY = 0u;
        quadrantY < SVSM_RECEIVER_PAGE_MASK_QUADRANT_AXIS;
        ++quadrantY)
    {
        [unroll]
        for (uint quadrantX = 0u;
            quadrantX < SVSM_RECEIVER_PAGE_MASK_QUADRANT_AXIS;
            ++quadrantX)
        {
            const uint quadrant =
                quadrantY * SVSM_RECEIVER_PAGE_MASK_QUADRANT_AXIS +
                quadrantX;
            const uint receiverMask = u_ReceiverPageMasks[
                maskBase +
                SVSM_RECEIVER_PAGE_MASK_QUADRANT_OFFSET +
                quadrant];
            if ((receiverMask & 0xffff0000u) != 0u)
                return 0u;

            const uint2 quadrantMinimum =
                uint2(quadrantX, quadrantY) *
                SVSM_RECEIVER_PAGE_MASK_QUADRANT_CELL_AXIS;
            const uint2 quadrantMaximum =
                quadrantMinimum +
                SVSM_RECEIVER_PAGE_MASK_QUADRANT_CELL_AXIS - 1u;
            const uint2 overlapMinimum =
                max(minimumCell, quadrantMinimum);
            const uint2 overlapMaximum =
                min(maximumCell, quadrantMaximum);
            if (any(overlapMinimum > overlapMaximum))
                continue;

            const uint queryMask =
                BuildReceiverPageMaskQuadrantRectangle(
                    overlapMinimum - quadrantMinimum,
                    overlapMaximum - quadrantMinimum);
            if ((receiverMask & queryMask) != 0u)
                return 1u;
        }
    }
    return 2u;
}

bool ReceiverPageMaskKeepsCaster(
    SparseVirtualShadowMapPacketMetadata metadata,
    uint owner,
    uint2 localPage,
    bool collectDebugCounters)
{
    const uint query = QueryReceiverPageMask(
        metadata, owner, localPage);
    if (collectDebugCounters)
    {
        InterlockedAdd(
            u_Counters[SVSM_RECEIVER_PAGE_MASK_QUERY_COUNTER],
            1u);
        if (query == 0u)
        {
            InterlockedAdd(
                u_Counters[
                    SVSM_RECEIVER_PAGE_MASK_FAIL_OPEN_COUNTER],
                1u);
        }
        else if (query == 2u)
        {
            InterlockedAdd(
                u_Counters[
                    SVSM_RECEIVER_PAGE_MASK_CULL_COUNTER],
                1u);
        }
    }
    return query != 2u;
}
#endif

#if SVSM_STATIC_DEPTH_HIERARCHY
uint LoadStaticDepthHierarchyRegionMinimum(
    uint hierarchyBase,
    uint2 minimumCell,
    uint2 maximumCell)
{
    if (all(minimumCell == 0u) &&
        all(maximumCell ==
            SVSM_STATIC_DEPTH_HIERARCHY_BASE_AXIS - 1u))
    {
        return u_StaticDepthHierarchy[
            hierarchyBase +
            SVSM_STATIC_DEPTH_HIERARCHY_ROOT_OFFSET];
    }

    uint minimumDepth = 0xffffffffu;
    [unroll]
    for (uint coarseY = 0u; coarseY < 2u; ++coarseY)
    {
        [unroll]
        for (uint coarseX = 0u; coarseX < 2u; ++coarseX)
        {
            const uint2 coarseMinimum =
                uint2(coarseX, coarseY) * 4u;
            const uint2 coarseMaximum = coarseMinimum + 3u;
            if (any(maximumCell < coarseMinimum) ||
                any(minimumCell > coarseMaximum))
            {
                continue;
            }

            const bool containsCoarse =
                all(minimumCell <= coarseMinimum) &&
                all(maximumCell >= coarseMaximum);
            if (containsCoarse)
            {
                minimumDepth = min(
                    minimumDepth,
                    u_StaticDepthHierarchy[
                        hierarchyBase +
                        SVSM_STATIC_DEPTH_HIERARCHY_LEVEL_TWO_OFFSET +
                        coarseY *
                            SVSM_STATIC_DEPTH_HIERARCHY_LEVEL_TWO_AXIS +
                        coarseX]);
                continue;
            }

            [unroll]
            for (uint fineY = 0u; fineY < 2u; ++fineY)
            {
                [unroll]
                for (uint fineX = 0u; fineX < 2u; ++fineX)
                {
                    const uint2 fineMinimum =
                        coarseMinimum +
                        uint2(fineX, fineY) * 2u;
                    const uint2 fineMaximum = fineMinimum + 1u;
                    if (any(maximumCell < fineMinimum) ||
                        any(minimumCell > fineMaximum))
                    {
                        continue;
                    }

                    const bool containsFine =
                        all(minimumCell <= fineMinimum) &&
                        all(maximumCell >= fineMaximum);
                    if (containsFine)
                    {
                        const uint2 fineNode =
                            fineMinimum / 2u;
                        minimumDepth = min(
                            minimumDepth,
                            u_StaticDepthHierarchy[
                                hierarchyBase +
                                SVSM_STATIC_DEPTH_HIERARCHY_LEVEL_ONE_OFFSET +
                                fineNode.y *
                                    SVSM_STATIC_DEPTH_HIERARCHY_LEVEL_ONE_AXIS +
                                fineNode.x]);
                        continue;
                    }

                    [unroll]
                    for (uint cellY = 0u; cellY < 2u; ++cellY)
                    {
                        [unroll]
                        for (uint cellX = 0u; cellX < 2u; ++cellX)
                        {
                            const uint2 cell =
                                fineMinimum +
                                uint2(cellX, cellY);
                            if (all(cell >= minimumCell) &&
                                all(cell <= maximumCell))
                            {
                                minimumDepth = min(
                                    minimumDepth,
                                    u_StaticDepthHierarchy[
                                        hierarchyBase +
                                        SVSM_STATIC_DEPTH_HIERARCHY_BASE_OFFSET +
                                        cell.y *
                                            SVSM_STATIC_DEPTH_HIERARCHY_BASE_AXIS +
                                        cell.x]);
                            }
                        }
                    }
                }
            }
        }
    }
    return minimumDepth;
}

// 0: unavailable/invalid (fail open), 1: valid and visible,
// 2: valid and fully occluded by the clean static slice.
uint QueryStaticDepthHierarchy(
    SparseVirtualShadowMapPacketMetadata metadata,
    uint owner,
    uint physical,
    uint packedPage,
    uint2 localPage)
{
    if (!StaticDepthHierarchyCullingEnabled() ||
        (metadata.objectInstanceIndex &
            SVSM_PACKET_STATIC_CASTER_BIT) != 0u ||
        metadata.packedMinimumTexel ==
            SVSM_PACKET_PAGE_INVALID_BOUNDS ||
        metadata.packedMinimumTexel ==
            SVSM_PACKET_PAGE_EMPTY_BOUNDS ||
        metadata.packedMaximumTexel ==
            SVSM_PACKET_PAGE_INVALID_BOUNDS ||
        metadata.packedMaximumTexel ==
            SVSM_PACKET_PAGE_EMPTY_BOUNDS ||
        (packedPage & (SVSM_RESIDENT_BIT | SVSM_DIRTY_BIT)) !=
            (SVSM_RESIDENT_BIT | SVSM_DIRTY_BIT) ||
        (packedPage & SVSM_PAGE_STATIC_DIRTY_BIT) != 0u ||
        (packedPage & SVSM_PHYSICAL_MASK) != physical ||
        physical >= g_Svsm.physicalPageCount)
    {
        return 0u;
    }

    // Keep all physical-buffer reads after the explicit range test.
    if (u_PhysicalOwners[physical] != owner ||
        u_RenderPages[physical] != owner)
    {
        return 0u;
    }

    const uint hierarchyBase =
        StaticDepthHierarchyWordBase(physical);
    const uint tag = u_StaticDepthHierarchy[
        hierarchyBase +
        SVSM_STATIC_DEPTH_HIERARCHY_TAG_OFFSET];
    const uint tagOwner =
        tag & SVSM_STATIC_DEPTH_HIERARCHY_TAG_OWNER_MASK;
    const uint tagEpoch =
        tag >> SVSM_STATIC_DEPTH_HIERARCHY_TAG_EPOCH_SHIFT;
    if (tagEpoch == 0u || tagOwner != owner)
        return 0u;

    const uint2 packetMinimumTexel = uint2(
        metadata.packedMinimumTexel & 0xffffu,
        metadata.packedMinimumTexel >> 16u);
    const uint2 packetMaximumTexel = uint2(
        metadata.packedMaximumTexel & 0xffffu,
        metadata.packedMaximumTexel >> 16u);
    if (any(packetMinimumTexel > packetMaximumTexel) ||
        any(packetMaximumTexel >= SVSM_VIRTUAL_RESOLUTION))
    {
        return 0u;
    }

    const uint2 pageMinimumTexel =
        localPage * SVSM_PAGE_SIZE;
    const uint2 pageMaximumTexel =
        pageMinimumTexel + SVSM_PAGE_SIZE - 1u;
    const uint2 intersectionMinimum =
        max(packetMinimumTexel, pageMinimumTexel);
    const uint2 intersectionMaximum =
        min(packetMaximumTexel, pageMaximumTexel);
    if (any(intersectionMinimum > intersectionMaximum))
        return 0u;

    const uint2 localMinimum =
        intersectionMinimum - pageMinimumTexel;
    const uint2 localMaximum =
        intersectionMaximum - pageMinimumTexel;
    const uint2 minimumCell =
        localMinimum /
        SVSM_STATIC_DEPTH_HIERARCHY_BASE_CELL_WIDTH;
    const uint2 maximumCell =
        localMaximum /
        SVSM_STATIC_DEPTH_HIERARCHY_BASE_CELL_WIDTH;
    const uint minimumStaticDepthBits =
        LoadStaticDepthHierarchyRegionMinimum(
            hierarchyBase, minimumCell, maximumCell);
    if (minimumStaticDepthBits == 0u ||
        minimumStaticDepthBits > asuint(1.0f))
    {
        return 0u;
    }

    const float nearestCasterDepth =
        asfloat(metadata.nearestReverseDepth);
    const float minimumStaticDepth =
        asfloat(minimumStaticDepthBits);
    if (!isfinite(nearestCasterDepth) ||
        !isfinite(minimumStaticDepth) ||
        !isfinite(g_Svsm.staticDepthHierarchyBias) ||
        nearestCasterDepth < 0.0f ||
        nearestCasterDepth > 1.0f ||
        minimumStaticDepth <= 0.0f ||
        minimumStaticDepth > 1.0f ||
        g_Svsm.staticDepthHierarchyBias < 0.0f ||
        g_Svsm.staticDepthHierarchyBias > 0.05f)
    {
        return 0u;
    }

    return nearestCasterDepth +
            g_Svsm.staticDepthHierarchyBias <
        minimumStaticDepth
        ? 2u
        : 1u;
}

bool StaticDepthHierarchyKeepsDynamicCaster(
    SparseVirtualShadowMapPacketMetadata metadata,
    uint owner,
    uint physical,
    uint packedPage,
    uint2 localPage,
    bool collectDebugCounters)
{
    const uint query = QueryStaticDepthHierarchy(
        metadata,
        owner,
        physical,
        packedPage,
        localPage);
    if (collectDebugCounters)
    {
        InterlockedAdd(
            u_Counters[SVSM_STATIC_DEPTH_HIERARCHY_QUERY_COUNTER],
            1u);
        if (query == 0u)
        {
            InterlockedAdd(
                u_Counters[
                    SVSM_STATIC_DEPTH_HIERARCHY_FAIL_OPEN_COUNTER],
                1u);
        }
        else if (query == 2u)
        {
            InterlockedAdd(
                u_Counters[
                    SVSM_STATIC_DEPTH_HIERARCHY_CULL_COUNTER],
                1u);
        }
    }
    return query != 2u;
}
#endif

bool ScatterPacketPageKeepsCaster(
    SparseVirtualShadowMapPacketMetadata metadata,
    bool staticCaster,
    uint owner,
    uint physical,
    uint packed,
    uint2 localPage,
    bool collectDebugCounters)
{
    if (!IsValidOwner(owner) ||
        owner / SVSM_PAGES_PER_CLIPMAP !=
            g_Svsm.selectedClipmap ||
        physical >= g_Svsm.physicalPageCount)
    {
        return false;
    }
    // Keep physical-buffer reads after the explicit range check. Shader
    // short-circuit lowering is not a sufficient out-of-bounds guarantee.
    if (!PageNeedsPacketCaster(packed, staticCaster) ||
        (packed & SVSM_PHYSICAL_MASK) != physical ||
        u_PhysicalOwners[physical] != owner ||
        u_RenderPages[physical] != owner)
    {
        return false;
    }
#if SVSM_RECEIVER_PAGE_MASK
    if (!ReceiverPageMaskKeepsCaster(
            metadata,
            owner,
            localPage,
            collectDebugCounters))
    {
        return false;
    }
#endif
    return true;
}

void AppendScatterPacketPage(
    SparseVirtualShadowMapPacketMetadata metadata,
    uint pageListCapacity,
    uint compactPage,
    uint2 localPage)
{
    uint writeIndex;
    InterlockedAdd(
        s_PacketPageCount, 1u, writeIndex);
    if (writeIndex < pageListCapacity)
    {
        u_PacketRenderPages[
            metadata.pageListOffset + writeIndex] =
            compactPage;
    }
    InterlockedMin(
        s_PacketMinimumPageX, localPage.x);
    InterlockedMin(
        s_PacketMinimumPageY, localPage.y);
    InterlockedMax(
        s_PacketMaximumPageX, localPage.x);
    InterlockedMax(
        s_PacketMaximumPageY, localPage.y);
}

[numthreads(SVSM_PACKET_FILL_THREADS, 1, 1)]
void fillIndirect(
    uint3 group : SV_GroupID,
    uint3 groupThread : SV_GroupThreadID)
{
    bool packetPageCulling =
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_PACKET_PAGE_CULLING) != 0u;
    bool dirtyPageScatterRaster =
        packetPageCulling &&
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_DIRTY_PAGE_SCATTER_RASTER) != 0u;
    bool dirtyPageScatterAmplificationGuard =
        dirtyPageScatterRaster &&
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_DIRTY_PAGE_SCATTER_AMPLIFICATION_GUARD) != 0u;
    // A guarded scatter failure must use the compact scheduled-page list.
    // Drawing one fail-open packet across the complete 8192-square virtual
    // viewport defeated the amplification guard and could monopolize the GPU.
    // The unguarded shader behavior remains compiled for reference inspection;
    // CPU runtime activation routes that configuration to exact per-page work.
    uint guardedFailOpenMode = dirtyPageScatterAmplificationGuard
        ? SVSM_PACKET_PAGE_RUNTIME_PER_PAGE
        : 0u;
    uint linearGroup =
        group.y * SVSM_PACKET_FILL_DISPATCH_WIDTH + group.x;
    // Packet-page culling assigns one cooperative group to each packet for
    // both exact and scatter raster. Scatter needs the exact scheduled-page
    // fanout and bounds to make its amplification guard authoritative and to
    // populate a bounded per-page fallback list.
    bool onePacketPerGroup = packetPageCulling;
    uint packetThread = onePacketPerGroup
        ? linearGroup
        : linearGroup * SVSM_PACKET_FILL_THREADS + groupThread.x;
    if (packetThread >= g_Svsm.drawPacketCount ||
        g_Svsm.selectedClipmap >= SVSM_SPARSE_CLIPMAP_COUNT)
    {
        return;
    }

    uint packetIndex =
        g_Svsm.drawPacketOffset + packetThread;
    uint packetRuntimeBase = PacketRuntimeBase(packetIndex);
    bool packetControlThread =
        !packetPageCulling || groupThread.x == 0u;
    bool collectPacketDebugCounters = g_Svsm.debugView != 0u;
    if (packetPageCulling &&
        collectPacketDebugCounters &&
        packetControlThread)
    {
        InterlockedAdd(
            u_Counters[SVSM_PACKET_PAGE_CANDIDATE_COUNTER],
            1u);
    }
    uint argumentWord =
        packetIndex * 5u + 1u;
    // Allocation should never publish more pages than the physical pool, but
    // clamp before indexing the compact list so corrupted or stale counters
    // fail conservatively instead of producing an out-of-bounds read.
    uint levelPageCount = min(
        u_Counters[
            SVSM_LEVEL_RENDER_COUNTER_BASE +
            g_Svsm.selectedClipmap],
        g_Svsm.physicalPageCount);
    if ((g_Svsm.flags &
            SVSM_SPARSE_FLAG_LEVEL_EMPTY_WORK_SKIP) != 0u &&
        packetThread == 0u &&
        packetControlThread)
    {
        u_Counters[
            SVSM_SPARSE_LEVEL_HAS_WORK_COUNTER_BASE +
            g_Svsm.selectedClipmap] =
            levelPageCount == 0u
                ? 0u
                : g_Svsm.drawPacketCount;
    }
    if (!packetPageCulling)
    {
        u_IndirectDrawArguments[argumentWord] = levelPageCount;
        return;
    }

    SparseVirtualShadowMapPacketMetadata metadata =
        t_PacketPageMetadata[packetIndex];
    bool staticCaster =
        PairedStaticDynamicDepthEnabled() &&
        (metadata.objectInstanceIndex &
            SVSM_PACKET_STATIC_CASTER_BIT) != 0u;
    if (metadata.packedMinimumPage ==
            SVSM_PACKET_PAGE_EMPTY_BOUNDS)
    {
        if (packetControlThread)
        {
            if (collectPacketDebugCounters)
            {
                InterlockedAdd(
                    u_Counters[SVSM_PACKET_PAGE_COMPACTED_COUNTER],
                    1u);
            }
            u_PacketPageRuntime[
                packetRuntimeBase +
                SVSM_PACKET_PAGE_RUNTIME_STATE_WORD] = 0u;
            if (dirtyPageScatterRaster)
                StoreEmptyPacketRectangle(packetRuntimeBase);
            u_IndirectDrawArguments[argumentWord] = 0u;
        }
        return;
    }
    if (metadata.packedMinimumPage ==
            SVSM_PACKET_PAGE_INVALID_BOUNDS)
    {
        if (packetControlThread)
        {
            if (collectPacketDebugCounters)
            {
                InterlockedAdd(
                    u_Counters[SVSM_PACKET_PAGE_FAIL_OPEN_COUNTER],
                    1u);
            }
            u_PacketPageRuntime[
                packetRuntimeBase +
                SVSM_PACKET_PAGE_RUNTIME_STATE_WORD] =
                SVSM_PACKET_PAGE_RUNTIME_FAIL_OPEN |
                guardedFailOpenMode |
                min(levelPageCount,
                    SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK);
            if (dirtyPageScatterRaster)
            {
                if (dirtyPageScatterAmplificationGuard)
                    StoreEmptyPacketRectangle(packetRuntimeBase);
                else
                    StoreGlobalDirtyPacketRectangle(
                        packetRuntimeBase,
                        g_Svsm.selectedClipmap);
            }
            u_IndirectDrawArguments[argumentWord] =
                dirtyPageScatterRaster
                    ? (dirtyPageScatterAmplificationGuard
                        ? levelPageCount
                        : (levelPageCount == 0u ? 0u : 1u))
                    : levelPageCount;
        }
        return;
    }

    uint2 minimumPage = uint2(
        metadata.packedMinimumPage & 0xffu,
        (metadata.packedMinimumPage >> 8u) & 0xffu);
    uint2 maximumPage = uint2(
        metadata.packedMaximumPage & 0xffu,
        (metadata.packedMaximumPage >> 8u) & 0xffu);
    bool validBounds =
        all(minimumPage <= maximumPage) &&
        all(maximumPage < SVSM_PAGES_PER_AXIS);
    uint2 pageExtent = maximumPage - minimumPage + 1u;
    uint pageListCapacity = validBounds
        ? min(
            pageExtent.x * pageExtent.y,
            g_Svsm.physicalPageCount)
        : 0u;
    if (!validBounds || pageListCapacity == 0u)
    {
        if (packetControlThread)
        {
            if (collectPacketDebugCounters)
            {
                InterlockedAdd(
                    u_Counters[SVSM_PACKET_PAGE_FAIL_OPEN_COUNTER],
                    1u);
            }
            u_PacketPageRuntime[
                packetRuntimeBase +
                SVSM_PACKET_PAGE_RUNTIME_STATE_WORD] =
                SVSM_PACKET_PAGE_RUNTIME_FAIL_OPEN |
                guardedFailOpenMode |
                min(levelPageCount,
                    SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK);
            if (dirtyPageScatterRaster)
            {
                if (dirtyPageScatterAmplificationGuard)
                    StoreEmptyPacketRectangle(packetRuntimeBase);
                else
                    StoreGlobalDirtyPacketRectangle(
                        packetRuntimeBase,
                        g_Svsm.selectedClipmap);
            }
            u_IndirectDrawArguments[argumentWord] =
                dirtyPageScatterRaster
                    ? (dirtyPageScatterAmplificationGuard
                        ? levelPageCount
                        : (levelPageCount == 0u ? 0u : 1u))
                    : levelPageCount;
        }
        return;
    }

#if SVSM_SCHEDULED_TILE_MASK
    if (groupThread.x == 0u)
    {
        s_ScheduledTileMaskQueryResult =
            QueryScheduledPageTileMask(
                g_Svsm.selectedClipmap,
                minimumPage,
                maximumPage,
                staticCaster);
        if (collectPacketDebugCounters)
        {
            InterlockedAdd(
                u_Counters[
                    SVSM_SCHEDULED_TILE_MASK_QUERY_COUNTER],
                1u);
            if (s_ScheduledTileMaskQueryResult == 0u)
            {
                InterlockedAdd(
                    u_Counters[
                        SVSM_SCHEDULED_TILE_MASK_FAIL_OPEN_COUNTER],
                    1u);
            }
            else if (s_ScheduledTileMaskQueryResult == 1u)
            {
                InterlockedAdd(
                    u_Counters[
                        SVSM_SCHEDULED_TILE_MASK_EARLY_REJECT_COUNTER],
                    1u);
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (s_ScheduledTileMaskQueryResult == 1u)
    {
        if (groupThread.x == 0u)
        {
            if (collectPacketDebugCounters)
            {
                InterlockedAdd(
                    u_Counters[
                        SVSM_PACKET_PAGE_COMPACTED_COUNTER],
                    1u);
            }
            u_PacketPageRuntime[
                packetRuntimeBase +
                SVSM_PACKET_PAGE_RUNTIME_STATE_WORD] = 0u;
            u_IndirectDrawArguments[argumentWord] = 0u;
        }
        return;
    }
#endif

    // UE-style scatter submits the caster once in virtual clipmap space, but
    // first derives the exact scheduled pages that overlap this packet. The
    // list provides three independent guarantees: an authoritative any-page
    // test, tight scatter bounds, and a bounded per-page fallback when holes
    // would make virtual raster more expensive than the exact reference path.
    if (dirtyPageScatterRaster)
    {
        if (groupThread.x == 0u)
        {
            s_PacketPageCount = 0u;
            s_PacketMinimumPageX = SVSM_PAGES_PER_AXIS;
            s_PacketMinimumPageY = SVSM_PAGES_PER_AXIS;
            s_PacketMaximumPageX = 0u;
            s_PacketMaximumPageY = 0u;
        }
        GroupMemoryBarrierWithGroupSync();

        int2 pageTableOffset =
            g_Svsm.pageTableOffsetAndDelta[
                g_Svsm.selectedClipmap].xy;
        const uint rectanglePageCount =
            pageExtent.x * pageExtent.y;
        const bool scanRectangleDirectly =
            (g_Svsm.flags &
                SVSM_SPARSE_FLAG_PACKET_RECTANGLE_DIRECT_SCAN) != 0u &&
            rectanglePageCount * 2u <= levelPageCount;
        if (scanRectangleDirectly)
        {
            [loop]
            for (uint rectanglePageIndex = groupThread.x;
                rectanglePageIndex < rectanglePageCount;
                rectanglePageIndex += SVSM_PACKET_FILL_THREADS)
            {
                const uint2 localPage =
                    minimumPage + uint2(
                        rectanglePageIndex % pageExtent.x,
                        rectanglePageIndex / pageExtent.x);
                const uint2 tablePage = uint2(
                    WrapPage(
                        int(localPage.x) +
                        pageTableOffset.x),
                    WrapPage(
                        int(localPage.y) +
                        pageTableOffset.y));
                const uint owner =
                    g_Svsm.selectedClipmap *
                        SVSM_PAGES_PER_CLIPMAP +
                    tablePage.y * SVSM_PAGES_PER_AXIS +
                    tablePage.x;
                const uint packed = u_PageTable[uint3(
                    tablePage, g_Svsm.selectedClipmap)];
                const uint physical =
                    packed & SVSM_PHYSICAL_MASK;
                if (ScatterPacketPageKeepsCaster(
                        metadata,
                        staticCaster,
                        owner,
                        physical,
                        packed,
                        localPage,
                        collectPacketDebugCounters))
                {
                    const uint compactPage =
                        (owner & SVSM_COMPACT_OWNER_MASK) |
                        (physical <<
                            SVSM_COMPACT_PHYSICAL_SHIFT);
                    AppendScatterPacketPage(
                        metadata,
                        pageListCapacity,
                        compactPage,
                        localPage);
                }
            }
        }
        else
        {
            [loop]
            for (uint pageIndex = groupThread.x;
                pageIndex < levelPageCount;
                pageIndex += SVSM_PACKET_FILL_THREADS)
            {
                const uint compactIndex =
                    g_Svsm.selectedClipmap *
                        g_Svsm.physicalPageCount +
                    pageIndex;
                const uint compactPage =
                    u_CompactRenderPages[compactIndex];
                const uint owner =
                    compactPage & SVSM_COMPACT_OWNER_MASK;
                const uint physical =
                    compactPage >> SVSM_COMPACT_PHYSICAL_SHIFT;
                const uint ownerLevel =
                    owner / SVSM_PAGES_PER_CLIPMAP;
                const uint tablePageIndex =
                    owner % SVSM_PAGES_PER_CLIPMAP;
                const int2 tablePage = int2(
                    tablePageIndex % SVSM_PAGES_PER_AXIS,
                    tablePageIndex / SVSM_PAGES_PER_AXIS);
                const int2 localPage = int2(
                    WrapPage(
                        tablePage.x -
                        pageTableOffset.x),
                    WrapPage(
                        tablePage.y -
                        pageTableOffset.y));
                if (IsValidOwner(owner) &&
                    ownerLevel == g_Svsm.selectedClipmap &&
                    all(localPage >= int2(minimumPage)) &&
                    all(localPage <= int2(maximumPage)))
                {
                    const uint packed =
                        physical < g_Svsm.physicalPageCount
                        ? u_PageTable[uint3(
                            uint2(tablePage), ownerLevel)]
                        : 0u;
                    if (ScatterPacketPageKeepsCaster(
                            metadata,
                            staticCaster,
                            owner,
                            physical,
                            packed,
                            uint2(localPage),
                            collectPacketDebugCounters))
                    {
                        AppendScatterPacketPage(
                            metadata,
                            pageListCapacity,
                            compactPage,
                            uint2(localPage));
                    }
                }
            }
        }

        GroupMemoryBarrierWithGroupSync();
        if (groupThread.x == 0u)
        {
            const bool overflow =
                s_PacketPageCount > pageListCapacity;
            if (collectPacketDebugCounters)
            {
                InterlockedAdd(
                    u_Counters[overflow
                        ? SVSM_PACKET_PAGE_FAIL_OPEN_COUNTER
                        : SVSM_PACKET_PAGE_COMPACTED_COUNTER],
                    1u);
#if SVSM_SCHEDULED_TILE_MASK
                if (!overflow &&
                    s_ScheduledTileMaskQueryResult == 2u &&
                    s_PacketPageCount == 0u)
                {
                    InterlockedAdd(
                        u_Counters[
                            SVSM_SCHEDULED_TILE_MASK_POSITIVE_EXACT_ZERO_COUNTER],
                        1u);
                }
#endif
            }

            if (overflow)
            {
                // Corrupt/duplicate metadata cannot safely index the reserved
                // packet list. Fail open through the bounded compact-page
                // reference path rather than a full virtual viewport.
                u_PacketPageRuntime[
                    packetRuntimeBase +
                    SVSM_PACKET_PAGE_RUNTIME_STATE_WORD] =
                    SVSM_PACKET_PAGE_RUNTIME_FAIL_OPEN |
                    SVSM_PACKET_PAGE_RUNTIME_PER_PAGE |
                    min(levelPageCount,
                        SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK);
                StoreEmptyPacketRectangle(packetRuntimeBase);
                u_IndirectDrawArguments[argumentWord] =
                    levelPageCount;
            }
            else if (s_PacketPageCount == 0u)
            {
                u_PacketPageRuntime[
                    packetRuntimeBase +
                    SVSM_PACKET_PAGE_RUNTIME_STATE_WORD] = 0u;
                StoreEmptyPacketRectangle(packetRuntimeBase);
                u_IndirectDrawArguments[argumentWord] = 0u;
            }
            else
            {
                const uint2 scatterMinimum = uint2(
                    s_PacketMinimumPageX,
                    s_PacketMinimumPageY);
                const uint2 scatterMaximum = uint2(
                    s_PacketMaximumPageX,
                    s_PacketMaximumPageY);
                const bool scatterBoundsValid =
                    all(scatterMinimum <= scatterMaximum) &&
                    all(scatterMaximum < SVSM_PAGES_PER_AXIS);
                const uint2 scatterExtent = scatterBoundsValid
                    ? scatterMaximum - scatterMinimum + 1u
                    : 0u;
                const uint scatterArea = scatterBoundsValid
                    ? scatterExtent.x * scatterExtent.y
                    : 0u;
                const uint maximumAmplification = max(
                    1u,
                    min(
                        g_Svsm.dirtyPageScatterMaximumAmplification,
                        SVSM_PAGES_PER_CLIPMAP));
                const bool usePerPageFallback =
                    !scatterBoundsValid ||
                    (dirtyPageScatterAmplificationGuard &&
                        scatterArea >
                            maximumAmplification *
                                s_PacketPageCount);
                u_PacketPageRuntime[
                    packetRuntimeBase +
                    SVSM_PACKET_PAGE_RUNTIME_STATE_WORD] =
                    usePerPageFallback
                        ? SVSM_PACKET_PAGE_RUNTIME_PER_PAGE |
                            min(
                                s_PacketPageCount,
                                SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK)
                        : 1u;
                if (scatterBoundsValid)
                {
                    u_PacketPageRuntime[
                        packetRuntimeBase +
                        SVSM_PACKET_PAGE_RUNTIME_MINIMUM_WORD] =
                        PackPacketPageCoordinate(
                            scatterMinimum);
                    u_PacketPageRuntime[
                        packetRuntimeBase +
                        SVSM_PACKET_PAGE_RUNTIME_MAXIMUM_WORD] =
                        PackPacketPageCoordinate(
                            scatterMaximum);
                }
                else
                {
                    StoreEmptyPacketRectangle(packetRuntimeBase);
                }
                u_IndirectDrawArguments[argumentWord] =
                    usePerPageFallback
                        ? s_PacketPageCount
                        : 1u;
            }
        }
        return;
    }

    if (groupThread.x == 0u)
    {
        s_PacketPageCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    int2 pageTableOffset =
        g_Svsm.pageTableOffsetAndDelta[
            g_Svsm.selectedClipmap].xy;
    uint rectanglePageCount = pageExtent.x * pageExtent.y;
    // Direct probes cost more state reads than compact-list entries. Use
    // them only with at least a two-to-one candidate reduction. Since the
    // level count is pool-clamped, this also proves that the rectangle fits
    // its CPU-reserved packet-list capacity.
    bool scanRectangleDirectly =
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_PACKET_RECTANGLE_DIRECT_SCAN) != 0u &&
        rectanglePageCount * 2u <= levelPageCount;
    if (scanRectangleDirectly)
    {
        for (uint rectanglePageIndex = groupThread.x;
            rectanglePageIndex < rectanglePageCount;
            rectanglePageIndex += 64u)
        {
            uint2 localPage = minimumPage + uint2(
                rectanglePageIndex % pageExtent.x,
                rectanglePageIndex / pageExtent.x);
            uint2 tablePage = uint2(
                WrapPage(int(localPage.x) + pageTableOffset.x),
                WrapPage(int(localPage.y) + pageTableOffset.y));
            uint owner =
                g_Svsm.selectedClipmap *
                    SVSM_PAGES_PER_CLIPMAP +
                tablePage.y * SVSM_PAGES_PER_AXIS +
                tablePage.x;
            uint packed = u_PageTable[uint3(
                tablePage, g_Svsm.selectedClipmap)];
            if (PageNeedsPacketCaster(packed, staticCaster))
            {
                uint physical = packed & SVSM_PHYSICAL_MASK;
                // Keep the range check in a separate branch; do not rely on
                // shader short-circuit evaluation before indexing the fixed
                // physical-page buffers.
                if (physical < g_Svsm.physicalPageCount)
                {
                    if (u_PhysicalOwners[physical] == owner &&
                        u_RenderPages[physical] == owner)
                    {
#if SVSM_RECEIVER_PAGE_MASK
                        if (!ReceiverPageMaskKeepsCaster(
                                metadata,
                                owner,
                                localPage,
                                collectPacketDebugCounters))
                        {
                            continue;
                        }
#endif
#if SVSM_STATIC_DEPTH_HIERARCHY
                        if (!staticCaster &&
                            !StaticDepthHierarchyKeepsDynamicCaster(
                                metadata,
                                owner,
                                physical,
                                packed,
                                localPage,
                                collectPacketDebugCounters))
                        {
                            continue;
                        }
#endif
                        uint writeIndex;
                        InterlockedAdd(
                            s_PacketPageCount, 1u, writeIndex);
                        if (writeIndex < pageListCapacity)
                        {
                            u_PacketRenderPages[
                                metadata.pageListOffset +
                                writeIndex] =
                                (owner & SVSM_COMPACT_OWNER_MASK) |
                                (physical <<
                                    SVSM_COMPACT_PHYSICAL_SHIFT);
                        }
                    }
                }
            }
        }
    }
    else
    {
        for (uint pageIndex = groupThread.x;
            pageIndex < levelPageCount;
            pageIndex += 64u)
        {
            uint compactIndex =
                g_Svsm.selectedClipmap *
                    g_Svsm.physicalPageCount +
                pageIndex;
            uint compactPage =
                u_CompactRenderPages[compactIndex];
            uint owner = compactPage & SVSM_COMPACT_OWNER_MASK;
            uint physical =
                compactPage >> SVSM_COMPACT_PHYSICAL_SHIFT;
            uint ownerLevel = owner / SVSM_PAGES_PER_CLIPMAP;
            uint tablePageIndex = owner % SVSM_PAGES_PER_CLIPMAP;
            int2 tablePage = int2(
                tablePageIndex % SVSM_PAGES_PER_AXIS,
                tablePageIndex / SVSM_PAGES_PER_AXIS);
            int2 localPage = int2(
                WrapPage(tablePage.x - pageTableOffset.x),
                WrapPage(tablePage.y - pageTableOffset.y));
            bool intersects =
                IsValidOwner(owner) &&
                ownerLevel == g_Svsm.selectedClipmap &&
                physical < g_Svsm.physicalPageCount &&
                all(localPage >= int2(minimumPage)) &&
                all(localPage <= int2(maximumPage));
            uint packed = 0u;
            if (intersects)
            {
                packed = u_PageTable[uint3(
                    uint2(tablePage), ownerLevel)];
                intersects =
                    PageNeedsPacketCaster(packed, staticCaster) &&
                    (packed & SVSM_PHYSICAL_MASK) == physical &&
                    u_PhysicalOwners[physical] == owner &&
                    u_RenderPages[physical] == owner;
            }
#if SVSM_RECEIVER_PAGE_MASK
            if (intersects &&
                !ReceiverPageMaskKeepsCaster(
                    metadata,
                    owner,
                    uint2(localPage),
                    collectPacketDebugCounters))
            {
                intersects = false;
            }
#endif
#if SVSM_STATIC_DEPTH_HIERARCHY
            if (intersects &&
                !staticCaster &&
                !StaticDepthHierarchyKeepsDynamicCaster(
                    metadata,
                    owner,
                    physical,
                    packed,
                    uint2(localPage),
                    collectPacketDebugCounters))
            {
                intersects = false;
            }
#endif
            if (intersects)
            {
                uint writeIndex;
                InterlockedAdd(
                    s_PacketPageCount, 1u, writeIndex);
                if (writeIndex < pageListCapacity)
                {
                    u_PacketRenderPages[
                        metadata.pageListOffset +
                        writeIndex] = compactPage;
                }
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (groupThread.x == 0u)
    {
        bool overflow = s_PacketPageCount > pageListCapacity;
        if (collectPacketDebugCounters)
        {
            InterlockedAdd(
                u_Counters[overflow
                    ? SVSM_PACKET_PAGE_FAIL_OPEN_COUNTER
                    : SVSM_PACKET_PAGE_COMPACTED_COUNTER],
                1u);
#if SVSM_SCHEDULED_TILE_MASK
            if (!overflow &&
                s_ScheduledTileMaskQueryResult == 2u &&
                s_PacketPageCount == 0u)
            {
                InterlockedAdd(
                    u_Counters[
                        SVSM_SCHEDULED_TILE_MASK_POSITIVE_EXACT_ZERO_COUNTER],
                    1u);
            }
#endif
        }
        uint runtimeState = overflow
            ? SVSM_PACKET_PAGE_RUNTIME_FAIL_OPEN |
                min(levelPageCount,
                    SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK)
            : min(s_PacketPageCount,
                SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK);
        u_PacketPageRuntime[
            packetRuntimeBase +
            SVSM_PACKET_PAGE_RUNTIME_STATE_WORD] = runtimeState;
        uint packetRenderPageCount = overflow
            ? levelPageCount
            : s_PacketPageCount;
        u_IndirectDrawArguments[argumentWord] = packetRenderPageCount;
    }
}
