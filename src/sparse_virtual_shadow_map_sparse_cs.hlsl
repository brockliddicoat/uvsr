#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
#include "sparse_virtual_shadow_map_sparse_cb.h"

#define SVSM_PHYSICAL_MASK 0x7fffu
#define SVSM_RESIDENT_BIT (1u << 15u)
#define SVSM_REQUIRED_BIT (1u << 16u)
#define SVSM_DIRTY_BIT (1u << 17u)
#define SVSM_AGE_SHIFT 18u
#define SVSM_AGE_MASK 0x3fffu
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
#define SVSM_MAX_TILE_PAGE_REQUESTS 64u

cbuffer c_Svsm : register(b0)
{
    SparseVirtualShadowMapSparseConstants g_Svsm;
};

Texture2D<float> t_CameraDepth : register(t0);
StructuredBuffer<SparseVirtualShadowMapPacketMetadata>
    t_PacketPageMetadata : register(t1);
RWTexture2DArray<uint> u_PageTable : register(u0);
RWStructuredBuffer<uint> u_PhysicalOwners : register(u1);
RWStructuredBuffer<uint> u_RenderPages : register(u2);
RWStructuredBuffer<uint> u_Counters : register(u3);
RWTexture2D<uint> u_PhysicalDepth : register(u4);
RWStructuredBuffer<uint> u_CompactRenderPages : register(u5);
RWStructuredBuffer<uint> u_IndirectDrawArguments : register(u6);
RWStructuredBuffer<uint> u_PacketPageRuntime : register(u7);
RWStructuredBuffer<uint> u_PacketRenderPages : register(u8);
RWStructuredBuffer<uint> u_DirtyPageRectangles : register(u9);

// Marking only needs a small, best-effort local dedupe cache. A hash collision
// falls back to the page table atomic immediately, so a full table can increase
// contention but can never drop a required page.
groupshared uint s_RequiredPageHash[SVSM_MARK_HASH_SIZE];
groupshared uint s_TileMinimumDepth;
groupshared uint s_TileMaximumDepth;
groupshared uint s_TileValidDepthCount;
groupshared uint s_TileNeedsPerPixelFallback;
groupshared uint s_PacketPageCount;

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
    bool invalidate =
        (g_Svsm.flags & SVSM_SPARSE_FLAG_FULL_INVALIDATION) != 0u ||
        IsNewlyExposed(page.xy, page.z);
    if (invalidate)
    {
        if ((packed & SVSM_RESIDENT_BIT) != 0u)
        {
            uint physicalPage = packed & SVSM_PHYSICAL_MASK;
            uint expectedOwner = EncodeVirtualPage(page);
            if (physicalPage < g_Svsm.physicalPageCount &&
                u_PhysicalOwners[physicalPage] == expectedOwner)
            {
                u_PhysicalOwners[physicalPage] = SVSM_INVALID_PAGE;
            }
        }
        packed = SVSM_DIRTY_BIT;
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
        uint hash = owner;
        hash ^= hash >> 8u;
        hash *= 0x9e3779b1u;
        hash ^= hash >> 16u;

        uint ignored;
        InterlockedCompareExchange(
            s_RequiredPageHash[hash & (SVSM_MARK_HASH_SIZE - 1u)],
            SVSM_INVALID_PAGE,
            owner,
            ignored);
        if (ignored != SVSM_INVALID_PAGE && ignored != owner)
        {
            InterlockedOr(
                u_PageTable[uint3(tablePage, level)],
                SVSM_REQUIRED_BIT,
                ignored);
        }
    }
}

void RequestFilterFootprint(
    float2 virtualPosition,
    int2 localPage,
    uint level)
{
    RequestPage(localPage, level);

    float filterRadius = g_Svsm.tapCount == 1u
        ? 0.0f
        : 3.0f;
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
    const float margin = g_Svsm.tapCount == 1u
        ? 0.0f
        : 3.0f;
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
        }
    }
    return true;
}

bool ProjectTileVolumeAtLevel(
    float3 worldCorners[8],
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
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        float4 clip = mul(
            float4(worldCorners[corner], 1.0f),
            g_Svsm.worldToClip[level]);
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
    float3 worldCorners[8];
    [unroll]
    for (uint depthIndex = 0u; depthIndex < 2u; ++depthIndex)
    {
        [unroll]
        for (uint corner = 0u; corner < 4u; ++corner)
        {
            float3 worldPosition = ReconstructWorldPosition(
                g_Svsm.cameraView,
                pixelCorners[corner],
                depthBounds[depthIndex]);
            if (!all(isfinite(worldPosition)))
                return false;
            worldCorners[depthIndex * 4u + corner] = worldPosition;
        }
    }

    uint firstLevel = min(
        g_Svsm.resolutionBias,
        uint(SVSM_SPARSE_CLIPMAP_COUNT - 1));
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
                worldCorners,
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

        if (fullyCovered)
        {
            if (!requestedCoarsest)
            {
                float2 coarseMinimum = 0.0f;
                float2 coarseMaximum = 0.0f;
                bool coarseFullyCovered = false;
                if (!ProjectTileVolumeAtLevel(
                        worldCorners,
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

bool MarkWorldPositionAtLevel(float3 worldPosition, uint level)
{
    float4 clip = mul(
        float4(worldPosition, 1.0f),
        g_Svsm.worldToClip[level]);
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

void MarkPixel(uint2 pixel)
{
    if (any(pixel >= g_Svsm.cameraSize))
        return;
    float cameraDepth = t_CameraDepth[pixel];
    if (!(cameraDepth > 0.0f) || !isfinite(cameraDepth))
        return;

    float3 worldPosition = ReconstructWorldPosition(
        g_Svsm.cameraView,
        float2(pixel) + 0.5f,
        cameraDepth);
    if (!all(isfinite(worldPosition)))
        return;

    uint firstLevel = min(
        g_Svsm.resolutionBias,
        uint(SVSM_SPARSE_CLIPMAP_COUNT - 1));
    uint selectedLevel = SVSM_SPARSE_CLIPMAP_COUNT;
    [loop]
    for (uint level = firstLevel;
        level < SVSM_SPARSE_CLIPMAP_COUNT;
        ++level)
    {
        if (MarkWorldPositionAtLevel(worldPosition, level))
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

    // The coarsest clipmap is the guaranteed complete fallback under pool or
    // render-budget pressure. It is requested independently of the selected
    // finest page, matching the published SVSM budget design.
    const uint coarsestLevel = SVSM_SPARSE_CLIPMAP_COUNT - 1u;
    if (selectedLevel != coarsestLevel)
        MarkWorldPositionAtLevel(worldPosition, coarsestLevel);
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
        s_TileValidDepthCount = 0u;
        s_TileNeedsPerPixelFallback = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint localMinimumDepth = 0xffffffffu;
    uint localMaximumDepth = 0u;
    uint localValidDepthCount = 0u;
    if (g_Svsm.markingMode == 1u)
    {
        AccumulateLocalTileDepth(
            group * 8u + groupThread,
            localMinimumDepth,
            localMaximumDepth,
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
                    localValidDepthCount);
            }
        }
    }

    uint waveMinimumDepth = WaveActiveMin(localMinimumDepth);
    uint waveMaximumDepth = WaveActiveMax(localMaximumDepth);
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

[numthreads(64, 1, 1)]
void recycle(uint physical : SV_DispatchThreadID)
{
    if (physical >= g_Svsm.physicalPageCount)
        return;

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

    uint listBase =
        g_Svsm.physicalPageCount *
        SVSM_SPARSE_CLIPMAP_COUNT;
    if (freePage)
    {
        uint freeIndex;
        InterlockedAdd(
            u_Counters[SVSM_FREE_COUNT_COUNTER],
            1u,
            freeIndex);
        u_CompactRenderPages[listBase + freeIndex] =
            physical;
    }
    else if (unrecentCachedPage)
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
    else if (requiredFinePage)
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
    else if (recentCachedPage)
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
        oldPacked |= SVSM_DIRTY_BIT;
        u_PageTable[oldPage] = oldPacked;
    }
}

[numthreads(64, 1, 1)]
void allocate(uint localPageIndex : SV_DispatchThreadID)
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
            packed |= SVSM_DIRTY_BIT;
            u_PageTable[page] = packed;
        }
    }

    if ((packed & SVSM_RESIDENT_BIT) == 0u)
    {
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
            SVSM_DIRTY_BIT;
    }

    uint frameAge = g_Svsm.frameIndex & SVSM_AGE_MASK;
    packed = (packed & ~(SVSM_AGE_MASK << SVSM_AGE_SHIFT)) |
        (frameAge << SVSM_AGE_SHIFT);
    u_PageTable[page] = packed;

    if ((packed & SVSM_DIRTY_BIT) == 0u)
        return;

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

    uint physical = packed & SVSM_PHYSICAL_MASK;
    uint levelRenderIndex;
    InterlockedAdd(
        u_Counters[
            SVSM_LEVEL_RENDER_COUNTER_BASE +
            g_Svsm.selectedClipmap],
        1u,
        levelRenderIndex);
    u_RenderPages[physical] = owner;
    uint compactIndex =
        g_Svsm.selectedClipmap *
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
                g_Svsm.selectedClipmap],
            SVSM_SPARSE_LEVEL_HAS_WORK_DISPATCH_GATE,
            ignored);
    }

    if ((g_Svsm.flags &
            SVSM_SPARSE_FLAG_DIRTY_PAGE_SCATTER_RASTER) != 0u)
    {
        int2 pageTableOffset =
            g_Svsm.pageTableOffsetAndDelta[
                g_Svsm.selectedClipmap].xy;
        uint2 localPage = uint2(
            WrapPage(int(page.x) - pageTableOffset.x),
            WrapPage(int(page.y) - pageTableOffset.y));
        uint rectangleBase =
            g_Svsm.selectedClipmap *
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
    return physical < g_Svsm.physicalPageCount &&
        DecodeVirtualPage(owner).z == g_Svsm.selectedClipmap &&
        u_RenderPages[physical] == owner;
}

[numthreads(128, 1, 1)]
void clearPages(
    uint3 page : SV_GroupID,
    uint3 pageThread : SV_GroupThreadID)
{
    uint physical = page.x;
    if ((g_Svsm.flags &
            SVSM_SPARSE_FLAG_COMPACT_PAGE_DISPATCH) != 0u)
    {
        uint ignoredOwner;
        if (!TryLoadCompactRenderPage(
                page.x, ignoredOwner, physical))
        {
            return;
        }
    }
    else if (physical >= g_Svsm.physicalPageCount ||
        u_RenderPages[physical] == SVSM_INVALID_PAGE)
    {
        return;
    }

    uint2 physicalCoordinate = uint2(
        physical % SVSM_PAGES_PER_AXIS,
        physical / SVSM_PAGES_PER_AXIS);
    uint2 pageBase = physicalCoordinate * SVSM_PAGE_SIZE;
    [loop]
    for (uint y = 0u; y < SVSM_PAGE_SIZE; ++y)
    {
        u_PhysicalDepth[pageBase + uint2(pageThread.x, y)] = 0u;
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
        (packed & SVSM_PHYSICAL_MASK) == physical)
    {
        u_PageTable[page] = packed & ~SVSM_DIRTY_BIT;
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
    if (dirty)
        InterlockedAdd(u_Counters[10], 1u);
    if (validResident)
        InterlockedAdd(u_Counters[4], 1u);
    if (validResident && !required)
        InterlockedAdd(u_Counters[9], 1u);
    if (required && dirty && !scheduled)
        InterlockedAdd(u_Counters[11], 1u);
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
    bool packedPacketDispatch =
        !packetPageCulling || dirtyPageScatterRaster;
    uint packetThread = packedPacketDispatch
        ? linearGroup * SVSM_PACKET_FILL_THREADS + groupThread.x
        : linearGroup;
    if (packetThread >= g_Svsm.drawPacketCount ||
        g_Svsm.selectedClipmap >= SVSM_SPARSE_CLIPMAP_COUNT)
    {
        return;
    }

    uint packetIndex =
        g_Svsm.drawPacketOffset + packetThread;
    uint packetRuntimeBase = PacketRuntimeBase(packetIndex);
    bool packetControlThread =
        dirtyPageScatterRaster || groupThread.x == 0u;
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

    // Stephano's scatter path only needs one conservative object/dirty-bounds
    // intersection. The pixel shader validates the exact resident, dirty, and
    // scheduled owner before every atomic write, so holes inside this rectangle
    // are harmless. Avoid scanning and rewriting an exact page list that the
    // scatter vertex shader never consumes.
    if (dirtyPageScatterRaster)
    {
        if (packetControlThread)
        {
            uint2 dirtyMinimumPage;
            uint2 dirtyMaximumPage;
            bool dirtyBoundsValid =
                TryLoadGlobalDirtyPageRectangle(
                    g_Svsm.selectedClipmap,
                    dirtyMinimumPage,
                    dirtyMaximumPage);
            if (levelPageCount == 0u)
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
                StoreEmptyPacketRectangle(packetRuntimeBase);
                u_IndirectDrawArguments[argumentWord] = 0u;
            }
            else if (!dirtyBoundsValid)
            {
                // A nonzero scheduled count must have contributed to the
                // rectangle. Treat any disagreement as corruption. A guarded
                // path repeats this packet over the compact scheduled pages;
                // the unguarded reference retains full-clipmap fail-open.
                if (collectPacketDebugCounters)
                {
                    InterlockedAdd(
                        u_Counters[
                            SVSM_PACKET_PAGE_FAIL_OPEN_COUNTER],
                        1u);
                }
                u_PacketPageRuntime[
                    packetRuntimeBase +
                    SVSM_PACKET_PAGE_RUNTIME_STATE_WORD] =
                    SVSM_PACKET_PAGE_RUNTIME_FAIL_OPEN |
                    guardedFailOpenMode |
                    min(levelPageCount,
                        SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK);
                StoreEmptyPacketRectangle(packetRuntimeBase);
                u_IndirectDrawArguments[argumentWord] =
                    dirtyPageScatterAmplificationGuard
                        ? levelPageCount
                        : 1u;
            }
            else
            {
                uint2 intersectionMinimum = max(
                    minimumPage, dirtyMinimumPage);
                uint2 intersectionMaximum = min(
                    maximumPage, dirtyMaximumPage);
                bool intersects = all(
                    intersectionMinimum <= intersectionMaximum);
                if (collectPacketDebugCounters)
                {
                    InterlockedAdd(
                        u_Counters[
                            SVSM_PACKET_PAGE_COMPACTED_COUNTER],
                        1u);
                }
                uint2 intersectionExtent = intersects
                    ? intersectionMaximum - intersectionMinimum + 1u
                    : 0u;
                uint intersectionArea = intersects
                    ? intersectionExtent.x * intersectionExtent.y
                    : 0u;
                uint maximumAmplification = max(
                    1u,
                    min(
                        g_Svsm.dirtyPageScatterMaximumAmplification,
                        SVSM_PAGES_PER_CLIPMAP));
                bool usePerPageFallback =
                    intersects &&
                    dirtyPageScatterAmplificationGuard &&
                    intersectionArea >
                        maximumAmplification * levelPageCount;
                u_PacketPageRuntime[
                    packetRuntimeBase +
                    SVSM_PACKET_PAGE_RUNTIME_STATE_WORD] =
                    !intersects
                        ? 0u
                        : (usePerPageFallback
                            ? SVSM_PACKET_PAGE_RUNTIME_PER_PAGE |
                                min(
                                    levelPageCount,
                                    SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK)
                            : 1u);
                if (intersects)
                {
                    u_PacketPageRuntime[
                        packetRuntimeBase +
                        SVSM_PACKET_PAGE_RUNTIME_MINIMUM_WORD] =
                        PackPacketPageCoordinate(
                            intersectionMinimum);
                    u_PacketPageRuntime[
                        packetRuntimeBase +
                        SVSM_PACKET_PAGE_RUNTIME_MAXIMUM_WORD] =
                        PackPacketPageCoordinate(
                            intersectionMaximum);
                }
                else
                {
                    StoreEmptyPacketRectangle(packetRuntimeBase);
                }
                u_IndirectDrawArguments[argumentWord] =
                    !intersects
                        ? 0u
                        : (usePerPageFallback
                            ? levelPageCount
                            : 1u);
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
            bool residentAndDirty =
                (packed & (SVSM_RESIDENT_BIT | SVSM_DIRTY_BIT)) ==
                (SVSM_RESIDENT_BIT | SVSM_DIRTY_BIT);
            if (residentAndDirty)
            {
                uint physical = packed & SVSM_PHYSICAL_MASK;
                // Keep the range check in a separate branch; do not rely on
                // shader short-circuit evaluation before indexing the fixed
                // physical-page buffers.
                if (physical < g_Svsm.physicalPageCount)
                {
                    if (u_RenderPages[physical] == owner)
                    {
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
            uint ownerLevel = owner / SVSM_PAGES_PER_CLIPMAP;
            uint tablePageIndex = owner % SVSM_PAGES_PER_CLIPMAP;
            int2 tablePage = int2(
                tablePageIndex % SVSM_PAGES_PER_AXIS,
                tablePageIndex / SVSM_PAGES_PER_AXIS);
            int2 localPage = int2(
                WrapPage(tablePage.x - pageTableOffset.x),
                WrapPage(tablePage.y - pageTableOffset.y));
            bool intersects =
                ownerLevel == g_Svsm.selectedClipmap &&
                all(localPage >= int2(minimumPage)) &&
                all(localPage <= int2(maximumPage));
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
