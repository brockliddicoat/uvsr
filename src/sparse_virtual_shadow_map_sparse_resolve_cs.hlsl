#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
#include "sparse_virtual_shadow_map_sparse_cb.h"

#ifndef SVSM_PRECOMPOSED_CLIPMAP_TRANSFORMS
#define SVSM_PRECOMPOSED_CLIPMAP_TRANSFORMS 0
#endif

#ifndef SVSM_BILINEAR_PCF
#define SVSM_BILINEAR_PCF 0
#endif

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

#ifndef SVSM_BALANCED_POISSON
#define SVSM_BALANCED_POISSON 0
#endif

cbuffer c_Svsm : register(b0)
{
    SparseVirtualShadowMapSparseConstants g_Svsm;
};

#include "sparse_virtual_shadow_map_receiver_lod.hlsli"

Texture2D<float> t_CameraDepth : register(t0);
Texture2DArray<uint> t_PageTable : register(t1);
Texture2DArray<uint> t_PhysicalDepth : register(t2);
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

#if SVSM_BALANCED_POISSON
static const uint c_PoissonOrder[16] = {
    1u, 5u, 12u, 13u,
    4u, 7u, 8u, 11u,
    0u, 2u, 3u, 6u,
    9u, 10u, 14u, 15u
};
#endif

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
        asfloat(t_PhysicalDepth.Load(int4(physicalTexel, 0, 0)));
    return casterDepth <= receiverDepth + g_Svsm.depthBias
        ? 1.0f
        : 0.0f;
}

float2 SparseTapOffset(uint tap, uint taps)
{
    if (taps == 1u)
        return 0.0f;
#if SVSM_BALANCED_POISSON
    return c_Poisson16[c_PoissonOrder[tap]] * 3.0f;
#else
    return c_Poisson16[tap * (16u / taps)] * 3.0f;
#endif
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
#if SVSM_BILINEAR_PCF
    // virtualPosition is expressed in texel-edge coordinates. Match
    // hardware bilinear/SampleCmp convention by moving to texel-center
    // coordinates before choosing the 2x2 footprint. At n + 0.5 this must
    // select texel n exactly rather than blending n and n + 1.
    float2 texelPosition = tapPosition - 0.5f;
    float2 minimumTap = floor(texelPosition);
    float2 tapFraction = texelPosition - minimumTap;
    lit = 0.0f;
    [unroll]
    for (uint y = 0u; y < 2u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 2u; ++x)
        {
            float weight =
                (x == 0u
                    ? 1.0f - tapFraction.x
                    : tapFraction.x) *
                (y == 0u
                    ? 1.0f - tapFraction.y
                    : tapFraction.y);
            // Hardware bilinear filtering has no dependency on a zero-weight
            // corner. Skipping its translation prevents a mathematically
            // unused texel in a missing neighboring virtual page from
            // forcing a needless coarser-clipmap fallback.
            if (weight == 0.0f)
                continue;

            float2 pointPosition =
                minimumTap + float2(x, y);
            if (any(pointPosition < 0.0f) ||
                any(pointPosition >= float(SVSM_ATLAS_SIZE)))
            {
                lit = 1.0f;
                return false;
            }

            uint2 physicalTexel = 0u;
            if (reuseCenterTranslation)
            {
                uint2 pageTexel =
                    uint2(pointPosition) % SVSM_PAGE_SIZE;
                uint2 physicalCoordinate = uint2(
                    centerPhysical % SVSM_PAGES_PER_AXIS,
                    centerPhysical / SVSM_PAGES_PER_AXIS);
                physicalTexel =
                    physicalCoordinate * SVSM_PAGE_SIZE +
                    pageTexel;
            }
            else
            {
                uint ignoredPacked = 0u;
                uint ignoredPhysical = SVSM_INVALID_PAGE;
                uint ignoredOwner = SVSM_INVALID_PAGE;
                if (!TryTranslateSparseTexel(
                        level,
                        pointPosition,
                        physicalTexel,
                        ignoredPacked,
                        ignoredPhysical,
                        ignoredOwner))
                {
                    lit = 1.0f;
                    return false;
                }
            }

            lit += weight *
                ReadSparseVisibility(
                    physicalTexel, receiverDepth);
        }
    }
    return true;
#else
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
#endif
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
#if SVSM_BILINEAR_PCF
    // Keep the cached and uncached page-translation paths bit-identical.
    // See TrySparseTap for the texel-edge to texel-center convention.
    float2 texelPosition = tapPosition - 0.5f;
    float2 minimumTap = floor(texelPosition);
    float2 tapFraction = texelPosition - minimumTap;
    lit = 0.0f;
    [unroll]
    for (uint y = 0u; y < 2u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 2u; ++x)
        {
            float weight =
                (x == 0u
                    ? 1.0f - tapFraction.x
                    : tapFraction.x) *
                (y == 0u
                    ? 1.0f - tapFraction.y
                    : tapFraction.y);
            if (weight == 0.0f)
                continue;

            float2 pointPosition =
                minimumTap + float2(x, y);
            if (any(pointPosition < 0.0f) ||
                any(pointPosition >= float(SVSM_ATLAS_SIZE)))
            {
                lit = 1.0f;
                return false;
            }

            int2 localPage = int2(floor(
                pointPosition / float(SVSM_PAGE_SIZE)));
            uint physical = SVSM_INVALID_PAGE;
            uint2 physicalTexel = 0u;
            if (!FindCachedPhysicalPage(
                    cache, localPage, physical))
            {
                uint ignoredPacked = 0u;
                uint ignoredOwner = SVSM_INVALID_PAGE;
                if (!TryTranslateSparseTexel(
                        level,
                        pointPosition,
                        physicalTexel,
                        ignoredPacked,
                        physical,
                        ignoredOwner))
                {
                    lit = 1.0f;
                    return false;
                }
                AddCachedPhysicalPage(
                    cache, localPage, physical);
            }
            else
            {
                uint2 pageTexel =
                    uint2(pointPosition) % SVSM_PAGE_SIZE;
                uint2 physicalCoordinate = uint2(
                    physical % SVSM_PAGES_PER_AXIS,
                    physical / SVSM_PAGES_PER_AXIS);
                physicalTexel =
                    physicalCoordinate * SVSM_PAGE_SIZE +
                    pageTexel;
            }

            lit += weight *
                ReadSparseVisibility(
                    physicalTexel, receiverDepth);
        }
    }
    return true;
#else
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
#endif
}
#endif

bool TrySparseClipmap(
    uint level,
    float4 receiverPosition,
    out float visibility,
    out uint centerPacked,
    out uint centerPhysical,
    out uint centerOwner)
{
    centerPacked = 0u;
    centerPhysical = SVSM_INVALID_PAGE;
    centerOwner = SVSM_INVALID_PAGE;
    float4 clip = mul(
        receiverPosition,
        g_Svsm.receiverToClip[level]);
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
    int filterRadius = int(g_Svsm.padding0);
    bool pageSafeFootprint =
        all(pageTexel >= filterRadius) &&
        all(pageTexel + filterRadius < int(SVSM_PAGE_SIZE));
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
            uint probeTap = probe;
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
                uint probeTap = probe;
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

    uint firstLevel =
        GetSvsmReceiverFirstClipmapLevelFromDepth(cameraDepth);
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
                receiverPosition,
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
