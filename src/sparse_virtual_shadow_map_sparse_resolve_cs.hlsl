#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
#include "sparse_virtual_shadow_map_sparse_cb.h"

#define SVSM_PHYSICAL_MASK 0x7fffu
#define SVSM_RESIDENT_BIT (1u << 15u)
#define SVSM_REQUIRED_BIT (1u << 16u)
#define SVSM_DIRTY_BIT (1u << 17u)
#define SVSM_INVALID_PAGE 0xffffffffu
#define SVSM_PAGES_PER_AXIS 64u
#define SVSM_PAGES_PER_CLIPMAP 4096u
#define SVSM_PAGE_SIZE 128u
#define SVSM_ATLAS_SIZE 8192u
#define SVSM_RESOLVE_MISSING_COUNTER 13u

#ifndef SVSM_PAGE_TRANSLATION_CACHE
#define SVSM_PAGE_TRANSLATION_CACHE 0
#endif

#ifndef SVSM_FILTER_TAPS
#define SVSM_FILTER_TAPS 16
#endif

cbuffer c_Svsm : register(b0)
{
    SparseVirtualShadowMapSparseConstants g_Svsm;
};

Texture2D<float> t_CameraDepth : register(t0);
Texture2DArray<uint> t_PageTable : register(t1);
Texture2D<uint> t_PhysicalDepth : register(t2);
StructuredBuffer<uint> t_PhysicalOwners : register(t3);
StructuredBuffer<uint> t_RenderPages : register(t4);
RWTexture2D<float> u_Visibility : register(u0);
RWTexture2D<float> u_Debug : register(u1);
RWStructuredBuffer<uint> u_Counters : register(u2);

static const float2 c_Poisson16[16] = {
    float2(-0.3935238f, 0.7530643f),
    float2(-0.3022015f, 0.2976640f),
    float2(0.09813362f, 0.1924510f),
    float2(-0.7593753f, 0.5187950f),
    float2(0.2293134f, 0.7607011f),
    float2(0.6505286f, 0.6297367f),
    float2(0.5322764f, 0.2350069f),
    float2(0.8581018f, -0.01624052f),
    float2(-0.6928226f, 0.07119545f),
    float2(-0.3114384f, -0.3017288f),
    float2(0.2837671f, -0.1797430f),
    float2(-0.3093514f, -0.7492560f),
    float2(-0.7386893f, -0.5215692f),
    float2(0.3988827f, -0.6170120f),
    float2(0.8114883f, -0.4580260f),
    float2(0.08265103f, -0.8939569f)
};

int WrapPage(int coordinate)
{
    int wrapped = coordinate % int(SVSM_PAGES_PER_AXIS);
    return wrapped < 0 ? wrapped + int(SVSM_PAGES_PER_AXIS) : wrapped;
}

uint EncodeVirtualPage(uint3 page)
{
    return page.z * SVSM_PAGES_PER_CLIPMAP +
        page.y * SVSM_PAGES_PER_AXIS +
        page.x;
}

bool TryTranslateSparseTexel(
    uint level,
    float2 tapPosition,
    out uint2 physicalTexel,
    out uint packed,
    out uint physical,
    out uint owner)
{
    physicalTexel = 0u;
    packed = 0u;
    physical = SVSM_INVALID_PAGE;
    owner = SVSM_INVALID_PAGE;
    if (any(tapPosition < 0.0f) ||
        any(tapPosition >= float(SVSM_ATLAS_SIZE)))
    {
        return false;
    }

    int2 localPage = int2(floor(
        tapPosition / float(SVSM_PAGE_SIZE)));
    int2 tableOffset =
        g_Svsm.pageTableOffsetAndDelta[level].xy;
    uint2 tablePage = uint2(
        WrapPage(localPage.x + tableOffset.x),
        WrapPage(localPage.y + tableOffset.y));
    packed = t_PageTable.Load(int4(tablePage, level, 0));
    if ((packed & (SVSM_RESIDENT_BIT | SVSM_DIRTY_BIT)) !=
        SVSM_RESIDENT_BIT)
    {
        return false;
    }

    physical = packed & SVSM_PHYSICAL_MASK;
    owner = EncodeVirtualPage(uint3(tablePage, level));
    if (physical >= g_Svsm.physicalPageCount ||
        t_PhysicalOwners[physical] != owner)
    {
        physical = SVSM_INVALID_PAGE;
        return false;
    }

    uint2 pageTexel = uint2(tapPosition) % SVSM_PAGE_SIZE;
    uint2 physicalCoordinate = uint2(
        physical % SVSM_PAGES_PER_AXIS,
        physical / SVSM_PAGES_PER_AXIS);
    physicalTexel =
        physicalCoordinate * SVSM_PAGE_SIZE + pageTexel;
    return true;
}

float ReadSparseVisibility(
    uint2 physicalTexel,
    float receiverDepth)
{
    float casterDepth =
        asfloat(t_PhysicalDepth.Load(int3(physicalTexel, 0)));
    return casterDepth <= receiverDepth + g_Svsm.depthBias
        ? 1.0f
        : 0.0f;
}

float2 SparseTapOffset(uint tap, uint taps)
{
    return taps == 1u
        ? 0.0f
        : c_Poisson16[tap * (16u / taps)] * 3.0f;
}

bool TrySparseTap(
    uint level,
    float2 virtualPosition,
    float receiverDepth,
    uint tap,
    uint taps,
    bool reuseCenterTranslation,
    uint centerPhysical,
    out float lit)
{
    float2 tapPosition =
        virtualPosition + SparseTapOffset(tap, taps);
    uint2 physicalTexel = 0u;
    if (reuseCenterTranslation)
    {
        if (any(tapPosition < 0.0f) ||
            any(tapPosition >= float(SVSM_ATLAS_SIZE)))
        {
            lit = 1.0f;
            return false;
        }
        uint2 pageTexel =
            uint2(tapPosition) % SVSM_PAGE_SIZE;
        uint2 physicalCoordinate = uint2(
            centerPhysical % SVSM_PAGES_PER_AXIS,
            centerPhysical / SVSM_PAGES_PER_AXIS);
        physicalTexel =
            physicalCoordinate * SVSM_PAGE_SIZE + pageTexel;
    }
    else
    {
        uint ignoredPacked = 0u;
        uint ignoredPhysical = SVSM_INVALID_PAGE;
        uint ignoredOwner = SVSM_INVALID_PAGE;
        if (!TryTranslateSparseTexel(
                level,
                tapPosition,
                physicalTexel,
                ignoredPacked,
                ignoredPhysical,
                ignoredOwner))
        {
            lit = 1.0f;
            return false;
        }
    }
    lit = ReadSparseVisibility(physicalTexel, receiverDepth);
    return true;
}

#if SVSM_PAGE_TRANSLATION_CACHE
struct SparsePageTranslationCache
{
    int2 localPage0;
    int2 localPage1;
    int2 localPage2;
    int2 localPage3;
    uint physical0;
    uint physical1;
    uint physical2;
    uint physical3;
    uint count;
};

bool FindCachedPhysicalPage(
    SparsePageTranslationCache cache,
    int2 localPage,
    out uint physical)
{
    physical = SVSM_INVALID_PAGE;
    if (cache.count > 0u && all(cache.localPage0 == localPage))
        physical = cache.physical0;
    else if (cache.count > 1u && all(cache.localPage1 == localPage))
        physical = cache.physical1;
    else if (cache.count > 2u && all(cache.localPage2 == localPage))
        physical = cache.physical2;
    else if (cache.count > 3u && all(cache.localPage3 == localPage))
        physical = cache.physical3;
    return physical != SVSM_INVALID_PAGE;
}

void AddCachedPhysicalPage(
    inout SparsePageTranslationCache cache,
    int2 localPage,
    uint physical)
{
    if (cache.count == 0u)
    {
        cache.localPage0 = localPage;
        cache.physical0 = physical;
    }
    else if (cache.count == 1u)
    {
        cache.localPage1 = localPage;
        cache.physical1 = physical;
    }
    else if (cache.count == 2u)
    {
        cache.localPage2 = localPage;
        cache.physical2 = physical;
    }
    else if (cache.count == 3u)
    {
        cache.localPage3 = localPage;
        cache.physical3 = physical;
    }
    if (cache.count < 4u)
        ++cache.count;
}

bool TrySparseTapWithTranslationCache(
    uint level,
    float2 virtualPosition,
    float receiverDepth,
    uint tap,
    uint taps,
    inout SparsePageTranslationCache cache,
    out float lit)
{
    float2 tapPosition =
        virtualPosition + SparseTapOffset(tap, taps);
    if (any(tapPosition < 0.0f) ||
        any(tapPosition >= float(SVSM_ATLAS_SIZE)))
    {
        lit = 1.0f;
        return false;
    }

    int2 localPage = int2(floor(
        tapPosition / float(SVSM_PAGE_SIZE)));
    uint physical = SVSM_INVALID_PAGE;
    uint2 physicalTexel = 0u;
    if (!FindCachedPhysicalPage(cache, localPage, physical))
    {
        uint ignoredPacked = 0u;
        uint ignoredOwner = SVSM_INVALID_PAGE;
        if (!TryTranslateSparseTexel(
                level,
                tapPosition,
                physicalTexel,
                ignoredPacked,
                physical,
                ignoredOwner))
        {
            lit = 1.0f;
            return false;
        }
        AddCachedPhysicalPage(cache, localPage, physical);
    }
    else
    {
        uint2 pageTexel =
            uint2(tapPosition) % SVSM_PAGE_SIZE;
        uint2 physicalCoordinate = uint2(
            physical % SVSM_PAGES_PER_AXIS,
            physical / SVSM_PAGES_PER_AXIS);
        physicalTexel =
            physicalCoordinate * SVSM_PAGE_SIZE + pageTexel;
    }

    lit = ReadSparseVisibility(physicalTexel, receiverDepth);
    return true;
}
#endif

bool TrySparseClipmap(
    uint level,
    float3 worldPosition,
    out float visibility,
    out uint centerPacked,
    out uint centerPhysical,
    out uint centerOwner)
{
    centerPacked = 0u;
    centerPhysical = SVSM_INVALID_PAGE;
    centerOwner = SVSM_INVALID_PAGE;
    float4 clip = mul(
        float4(worldPosition, 1.0f),
        g_Svsm.worldToClip[level]);
    if (!(clip.w != 0.0f) || !all(isfinite(clip)))
    {
        visibility = 1.0f;
        return false;
    }
    float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0f) ||
        ndc.z < 0.0f ||
        ndc.z > 1.0f)
    {
        visibility = 1.0f;
        return false;
    }

    float2 virtualPosition =
        (ndc.xy * float2(0.5f, -0.5f) + 0.5f) *
        float(SVSM_ATLAS_SIZE);
    uint2 centerPhysicalTexel = 0u;
    if (!TryTranslateSparseTexel(
            level,
            virtualPosition,
            centerPhysicalTexel,
            centerPacked,
            centerPhysical,
            centerOwner))
    {
        visibility = 1.0f;
        return false;
    }

    static const uint taps = SVSM_FILTER_TAPS;
    int2 centerTexel = int2(floor(virtualPosition));
    int2 pageTexel = centerTexel % int(SVSM_PAGE_SIZE);
    bool pageSafeFootprint =
        all(pageTexel >= 3) &&
        all(pageTexel <= int(SVSM_PAGE_SIZE) - 4);
    bool reuseCenterTranslation =
        g_Svsm.filterMode == 1u &&
        pageSafeFootprint;

    uint probeCount = min(taps, 4u);
    float probeLit[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool adaptive =
        g_Svsm.adaptiveFiltering != 0u &&
        taps > 1u &&
        pageSafeFootprint;
#if SVSM_PAGE_TRANSLATION_CACHE
    SparsePageTranslationCache translationCache;
    translationCache.localPage0 = int2(floor(
        virtualPosition / float(SVSM_PAGE_SIZE)));
    translationCache.localPage1 = 0;
    translationCache.localPage2 = 0;
    translationCache.localPage3 = 0;
    translationCache.physical0 = centerPhysical;
    translationCache.physical1 = SVSM_INVALID_PAGE;
    translationCache.physical2 = SVSM_INVALID_PAGE;
    translationCache.physical3 = SVSM_INVALID_PAGE;
    translationCache.count = 1u;
#endif
    if (adaptive)
    {
        bool probesAgree = true;
        [unroll]
        for (uint probe = 0u; probe < probeCount; ++probe)
        {
            uint probeTap =
                probe * (taps / probeCount);
            if (!TrySparseTap(
                    level,
                    virtualPosition,
                    ndc.z,
                    probeTap,
                    taps,
                    true,
                    centerPhysical,
                    probeLit[probe]))
            {
                visibility = 1.0f;
                return false;
            }
            probesAgree = probesAgree &&
                (probe == 0u ||
                    probeLit[probe] == probeLit[0]);
        }
        if (probesAgree)
        {
            visibility = probeLit[0];
            return true;
        }
    }

    float lit = 0.0f;
    [unroll]
    for (uint tap = 0u; tap < taps; ++tap)
    {
        float tapLit = 0.0f;
        bool reusedProbe = false;
        if (adaptive)
        {
            [unroll]
            for (uint probe = 0u;
                probe < probeCount;
                ++probe)
            {
                uint probeTap =
                    probe * (taps / probeCount);
                if (tap == probeTap)
                {
                    tapLit = probeLit[probe];
                    reusedProbe = true;
                }
            }
        }
        if (!reusedProbe)
        {
#if SVSM_PAGE_TRANSLATION_CACHE
            if (!reuseCenterTranslation)
            {
                if (!TrySparseTapWithTranslationCache(
                        level,
                        virtualPosition,
                        ndc.z,
                        tap,
                        taps,
                        translationCache,
                        tapLit))
                {
                    visibility = 1.0f;
                    return false;
                }
            }
            else
#endif
            if (!TrySparseTap(
                    level,
                    virtualPosition,
                    ndc.z,
                    tap,
                    taps,
                    reuseCenterTranslation,
                    centerPhysical,
                    tapLit))
            {
                visibility = 1.0f;
                return false;
            }
        }
        lit += tapLit;
    }
    visibility = lit / float(taps);
    return true;
}

float SparseDebugValue(
    uint selectedLevel,
    uint firstLevel,
    float visibility,
    bool missing,
    uint packed,
    uint physical,
    uint owner)
{
    bool resident =
        (packed & SVSM_RESIDENT_BIT) != 0u &&
        physical < g_Svsm.physicalPageCount;
    bool dirty = (packed & SVSM_DIRTY_BIT) != 0u;
    bool rendered = false;
    if (resident)
        rendered = t_RenderPages[physical] == owner;
    if (g_Svsm.debugView == 0u)
        return visibility;
    if (g_Svsm.debugView == 1u)
        return missing ? 0.0f :
            float(selectedLevel + 1u) /
                float(SVSM_SPARSE_CLIPMAP_COUNT);
    if (g_Svsm.debugView == 2u)
        return (packed & SVSM_REQUIRED_BIT) != 0u ? 1.0f : 0.0f;
    if (g_Svsm.debugView == 3u)
        return resident ? 1.0f : 0.0f;
    if (g_Svsm.debugView == 4u)
        return resident && !dirty && !rendered ? 1.0f : 0.0f;
    if (g_Svsm.debugView == 5u)
        return dirty ? 1.0f : 0.0f;
    if (g_Svsm.debugView == 6u)
        return rendered ? 1.0f : 0.0f;
    if (g_Svsm.debugView == 7u)
        return resident
            ? float(physical + 1u) /
                float(g_Svsm.physicalPageCount)
            : 0.0f;
    if (g_Svsm.debugView == 8u)
        return missing ? 1.0f :
            float(selectedLevel - firstLevel) /
                float(SVSM_SPARSE_CLIPMAP_COUNT - 1u);
    if (g_Svsm.debugView == 9u)
        return missing ? 1.0f : 0.0f;
    if (g_Svsm.debugView == 10u)
        return float(g_Svsm.tapCount) / 16.0f;
    if (g_Svsm.debugView == 11u)
        return visibility;
    return visibility;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_Svsm.cameraSize))
        return;

    float cameraDepth = t_CameraDepth[pixel];
    if (!(cameraDepth > 0.0f) || !isfinite(cameraDepth))
    {
        u_Visibility[pixel] = 1.0f;
        if (g_Svsm.debugView != 0u)
        {
            u_Debug[pixel] = SparseDebugValue(
                0u,
                0u,
                1.0f,
                true,
                0u,
                SVSM_INVALID_PAGE,
                SVSM_INVALID_PAGE);
        }
        return;
    }
    float3 worldPosition = ReconstructWorldPosition(
        g_Svsm.cameraView,
        float2(pixel) + 0.5f,
        cameraDepth);
    if (!all(isfinite(worldPosition)))
    {
        u_Visibility[pixel] = 1.0f;
        if (g_Svsm.debugView != 0u)
        {
            u_Debug[pixel] = SparseDebugValue(
                0u,
                0u,
                1.0f,
                true,
                0u,
                SVSM_INVALID_PAGE,
                SVSM_INVALID_PAGE);
        }
        return;
    }

    uint firstLevel = min(
        g_Svsm.resolutionBias,
        uint(SVSM_SPARSE_CLIPMAP_COUNT - 1));
    float visibility = 1.0f;
    uint debugPacked = 0u;
    uint debugPhysical = SVSM_INVALID_PAGE;
    uint debugOwner = SVSM_INVALID_PAGE;
    [loop]
    for (uint level = firstLevel;
        level < SVSM_SPARSE_CLIPMAP_COUNT;
        ++level)
    {
        if (TrySparseClipmap(
                level,
                worldPosition,
                visibility,
                debugPacked,
                debugPhysical,
                debugOwner))
        {
            u_Visibility[pixel] = visibility;
            if (g_Svsm.debugView != 0u)
            {
                if (level > firstLevel)
                    InterlockedAdd(u_Counters[7], 1u);
                u_Debug[pixel] = SparseDebugValue(
                    level,
                    firstLevel,
                    visibility,
                    false,
                    debugPacked,
                    debugPhysical,
                    debugOwner);
            }
            return;
        }
    }
    u_Visibility[pixel] = 1.0f;
    if (g_Svsm.debugView != 0u)
    {
        InterlockedAdd(
            u_Counters[SVSM_RESOLVE_MISSING_COUNTER],
            1u);
        u_Debug[pixel] = SparseDebugValue(
            0u,
            firstLevel,
            1.0f,
            true,
            debugPacked,
            debugPhysical,
            debugOwner);
    }
}
