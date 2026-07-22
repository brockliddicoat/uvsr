#ifndef UVSR_SPARSE_VIRTUAL_SHADOW_MAP_SPARSE_CB_H
#define UVSR_SPARSE_VIRTUAL_SHADOW_MAP_SPARSE_CB_H

#include <donut/shaders/view_cb.h>

#define SVSM_SPARSE_CLIPMAP_COUNT 6

#define SVSM_SPARSE_FLAG_FULL_INVALIDATION 1u
#define SVSM_SPARSE_FLAG_CACHING 2u
#define SVSM_SPARSE_FLAG_PRESERVE_REQUIRED 8u
#define SVSM_SPARSE_FLAG_COMPACT_PAGE_DISPATCH 32u
#define SVSM_SPARSE_FLAG_PACKET_PAGE_CULLING 64u
#define SVSM_SPARSE_FLAG_RECENT_PAGE_EVICTION_GRACE 128u
#define SVSM_SPARSE_FLAG_PACKET_RECTANGLE_DIRECT_SCAN 256u
#define SVSM_SPARSE_FLAG_LEVEL_EMPTY_WORK_SKIP 512u
#define SVSM_SPARSE_FLAG_PER_PIXEL_MARKING_DEDUPE 1024u
#define SVSM_SPARSE_FLAG_DIRTY_PAGE_SCATTER_RASTER 2048u
#define SVSM_SPARSE_FLAG_SCATTER_ALPHA_TEST_EARLY_REJECT 4096u
#define SVSM_SPARSE_FLAG_ALLOCATION_BUDGET_SATURATION_EARLY_OUT 8192u
#define SVSM_SPARSE_FLAG_DIRTY_PAGE_SCATTER_AMPLIFICATION_GUARD 16384u
#define SVSM_SPARSE_FLAG_COARSEST_PAGE_RENDER_BUDGET 32768u

#define SVSM_SPARSE_LEVEL_HAS_WORK_COUNTER_BASE 31u
#define SVSM_SPARSE_LEVEL_HAS_WORK_DISPATCH_GATE 1u
#define SVSM_SPARSE_COUNTER_COUNT 37u

#define SVSM_SPARSE_RECENT_PAGE_EVICTION_GRACE_FRAMES 8u

#define SVSM_SPARSE_DEPTH_FLAG_BATCHED_DRAW 1u
#define SVSM_SPARSE_DEPTH_FLAG_PACKET_PAGE_CULLING 2u
#define SVSM_SPARSE_DEPTH_FLAG_DIRTY_PAGE_SCATTER_RASTER 4u

#define SVSM_DIRTY_PAGE_RECT_WORDS_PER_LEVEL 4u

#define SVSM_PACKET_PAGE_INVALID_BOUNDS 0xffffffffu
#define SVSM_PACKET_PAGE_EMPTY_BOUNDS 0xfffffffeu
#define SVSM_PACKET_PAGE_RUNTIME_FAIL_OPEN (1u << 31u)
#define SVSM_PACKET_PAGE_RUNTIME_PER_PAGE (1u << 30u)
#define SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK 0x3fffffffu
#define SVSM_PACKET_PAGE_RUNTIME_WORDS 3u
#define SVSM_PACKET_PAGE_RUNTIME_STATE_WORD 0u
#define SVSM_PACKET_PAGE_RUNTIME_MINIMUM_WORD 1u
#define SVSM_PACKET_PAGE_RUNTIME_MAXIMUM_WORD 2u
#define SVSM_PACKET_FILL_DISPATCH_WIDTH 65535u
#define SVSM_PACKET_FILL_THREADS 64u

struct SparseVirtualShadowMapPacketMetadata
{
    uint packedMinimumPage;
    uint packedMaximumPage;
    uint pageListOffset;
    uint objectInstanceIndex;
};

struct SparseVirtualShadowMapSparseConstants
{
    PlanarViewConstants cameraView;
    float4x4 worldToClip[SVSM_SPARSE_CLIPMAP_COUNT];
    int4 pageTableOffsetAndDelta[SVSM_SPARSE_CLIPMAP_COUNT];

    uint2 cameraSize;
    uint frameIndex;
    uint physicalPageCount;

    uint pageRenderBudget;
    uint tapCount;
    uint resolutionBias;
    uint flags;

    uint selectedClipmap;
    float depthBias;
    uint debugView;
    uint padding0;

    uint markingMode;
    uint filterMode;
    uint adaptiveFiltering;
    uint drawPacketOffset;

    uint drawPacketCount;
    uint dirtyPageScatterMaximumAmplification;
    uint2 padding;
};

struct SparseVirtualShadowMapPushConstants
{
    uint startInstanceLocation;
    uint startVertexLocation;
    uint positionOffset;
    uint texCoordOffset;

    uint originalInstanceCount;
    uint physicalPageCount;
    uint flags;
    uint packetIndex;
};

#endif // UVSR_SPARSE_VIRTUAL_SHADOW_MAP_SPARSE_CB_H
