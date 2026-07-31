#include "screen_space_directional_shadows_cb.h"

cbuffer Constants : register(b0)
{
    ScreenSpaceDirectionalShadowConstants g_Shadow;
}

Texture2D<float> t_Depth : register(t0);
RWTexture2D<float> u_Visibility : register(u0);

static const float kMinimumTangentLengthSquared = 1e-12f;
static const float kMinimumDepthBias = 1e-6f;
static const float kMinimumInterpolationWeight = 1e-5f;
static const uint kMaximumTraceSamples = 960u;
static const uint kTraceChunkSamples = 32u;
static const uint kDepthCacheSideLimit = 48u;
static const uint kDepthCacheCapacity =
    kDepthCacheSideLimit * kDepthCacheSideLimit;
static const int kEmptyMinimumBound = 0x3ffffff;
static const int kEmptyMaximumBound = -0x3ffffff;

groupshared int2 s_CacheMinimum;
groupshared int2 s_CacheMaximum;
groupshared uint s_DirectReadCount;
groupshared float s_DepthCache[kDepthCacheCapacity];

float GetFarDepth()
{
    return g_Shadow.reverseDepth != 0u ? 0.0f : 1.0f;
}

bool IsDepthValid(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Shadow.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

float OccluderDepthDelta(float sceneDepth, float rayDepth)
{
    return g_Shadow.reverseDepth != 0u
        ? sceneDepth - rayDepth
        : rayDepth - sceneDepth;
}

bool EvaluateOccluder(
    float sceneDepth,
    float rayDepth,
    out float evidence)
{
    evidence = 0.0f;
    if (!IsDepthValid(sceneDepth) || !IsDepthValid(rayDepth))
        return false;

    // Thickness is a fraction of the remaining nonlinear depth range. This
    // keeps the exposed 0.005 value stable across the reverse-Z distribution
    // instead of interpreting it as an absolute device-depth window.
    float remainingDepth = abs(sceneDepth - GetFarDepth());
    float thickness =
        g_Shadow.surfaceThickness * remainingDepth;
    float receiverBias = max(
        kMinimumDepthBias,
        thickness * 0.02f);
    float delta = OccluderDepthDelta(sceneDepth, rayDepth);
    if (!(delta > receiverBias && delta <= thickness))
        return false;

    evidence = saturate(
        1.0f -
        (delta - receiverBias) /
            max(thickness - receiverBias, kMinimumDepthBias));
    return true;
}

bool IsDepthDiscontinuity(
    float sceneDepth,
    float neighborDepth)
{
    if (!IsDepthValid(sceneDepth) ||
        !IsDepthValid(neighborDepth))
    {
        return true;
    }

    float remainingDepth = min(
        abs(sceneDepth - GetFarDepth()),
        abs(neighborDepth - GetFarDepth()));
    float discontinuityLimit =
        g_Shadow.depthDiscontinuityThreshold *
        remainingDepth;
    return abs(neighborDepth - sceneDepth) >
        discontinuityLimit;
}

bool IsInsideTexture(int2 pixel)
{
    return all(pixel >= 0) &&
        all(pixel < int2(g_Shadow.textureSize));
}

float LoadTraceDepth(
    int2 pixel,
    bool useCache,
    int2 cacheMinimum,
    uint cacheWidth,
    uint cacheHeight)
{
    if (!IsInsideTexture(pixel))
        return GetFarDepth();

    if (useCache)
    {
        int2 local = pixel - cacheMinimum;
        if (all(local >= 0) &&
            all(local < int2(cacheWidth, cacheHeight)))
        {
            return s_DepthCache[
                uint(local.y) * cacheWidth + uint(local.x)];
        }
    }

    // A very divergent projected-light field can exceed the bounded shared
    // cache. Preserve correctness by falling back to the original texture.
    return t_Depth.Load(int3(pixel, 0));
}

[numthreads(8, 8, 1)]
void main(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint groupIndex : SV_GroupIndex)
{
    uint2 pixel = dispatchThreadId.xy;
    bool outputPixelValid =
        all(pixel < g_Shadow.textureSize);
    float receiverDepth = GetFarDepth();
    if (outputPixelValid)
        receiverDepth = t_Depth.Load(int3(pixel, 0));
    bool active =
        outputPixelValid && IsDepthValid(receiverDepth);

    float receiverRayDepth = receiverDepth;
    if (active && g_Shadow.usePrecisionOffset != 0u)
    {
        receiverRayDepth = saturate(
            receiverDepth +
            (receiverDepth - GetFarDepth()) / 65535.0f);
    }

    float2 pixelCenter = float2(pixel) + 0.5f;
    float2 ndc =
        (pixelCenter - g_Shadow.clipToWindowBias) /
        g_Shadow.clipToWindowScale;

    // The homogeneous light direction and receiver share the same projective
    // denominator. Their screen tangent and device-depth derivative therefore
    // reduce to this exact per-pixel affine DDA with no matrix operation or
    // reciprocal inside the trace loop.
    float2 tangentPixels =
        (g_Shadow.projectedLight.xy -
            ndc * g_Shadow.projectedLight.w) *
        g_Shadow.clipToWindowScale;
    float tangentLengthSquared =
        dot(tangentPixels, tangentPixels);
    float tangentMajorLength =
        max(abs(tangentPixels.x), abs(tangentPixels.y));
    bool tangentValid =
        tangentLengthSquared >
            kMinimumTangentLengthSquared &&
        tangentMajorLength > 0.0f &&
        isfinite(tangentLengthSquared) &&
        isfinite(tangentMajorLength);

    float2 directionPixels = 0.0f.xx;
    float depthPerStep = 0.0f;
    if (active && tangentValid)
    {
        float inverseMajorLength = rcp(tangentMajorLength);
        directionPixels =
            tangentPixels * inverseMajorLength;
        depthPerStep =
            (g_Shadow.projectedLight.z -
                receiverRayDepth * g_Shadow.projectedLight.w) *
            inverseMajorLength;
        active =
            all(isfinite(float3(
                directionPixels,
                depthPerStep)));
    }
    else
    {
        active = false;
    }

    uint sampleCount = min(
        g_Shadow.traceSampleCount,
        kMaximumTraceSamples);
    active = active && sampleCount != 0u;

    bool xMajor =
        abs(directionPixels.x) >= abs(directionPixels.y);
    int2 traceAxis = xMajor
        ? int2(directionPixels.x < 0.0f ? -1 : 1, 0)
        : int2(0, directionPixels.y < 0.0f ? -1 : 1);
    bool allowEarlyOut =
        g_Shadow.useEarlyOut != 0u &&
        g_Shadow.debugView == 0u;
    bool cacheCandidate = !allowEarlyOut;

    float hardEvidence = 0.0f;
    float softEvidence = 0.0f;
    float occlusion = 0.0f;
    float hitProgress = 0.0f;
    bool leftBounds = false;
    uint hardCount = min(
        g_Shadow.hardShadowSamples,
        sampleCount);
    uint fadeCount = min(
        g_Shadow.fadeOutSamples,
        sampleCount - hardCount);
    uint fadeStart = sampleCount - fadeCount;

    [loop]
    for (uint chunkStart = 0u;
        chunkStart < sampleCount;
        chunkStart += kTraceChunkSamples)
    {
        uint chunkSampleCount = min(
            kTraceChunkSamples,
            sampleCount - chunkStart);

        int2 cacheMinimum = int2(0, 0);
        uint cacheWidth = 0u;
        uint cacheHeight = 0u;
        bool useCache = false;
        if (cacheCandidate)
        {
            if (groupIndex == 0u)
            {
                s_CacheMinimum = int2(
                    kEmptyMinimumBound,
                    kEmptyMinimumBound);
                s_CacheMaximum = int2(
                    kEmptyMaximumBound,
                    kEmptyMaximumBound);
                s_DirectReadCount = 0u;
            }
            GroupMemoryBarrierWithGroupSync();

            int2 localMinimum = int2(
                kEmptyMinimumBound,
                kEmptyMinimumBound);
            int2 localMaximum = int2(
                kEmptyMaximumBound,
                kEmptyMaximumBound);
            uint localDirectReadCount = 0u;
            if (active)
            {
                float firstDistance =
                    float(chunkStart + 1u);
                float lastDistance =
                    float(chunkStart + chunkSampleCount);
                float2 firstGridPosition =
                    pixelCenter +
                    directionPixels * firstDistance -
                    0.5f;
                float2 lastGridPosition =
                    pixelCenter +
                    directionPixels * lastDistance -
                    0.5f;
                localMinimum =
                    int2(floor(min(
                        firstGridPosition,
                        lastGridPosition))) -
                    1;
                localMaximum =
                    int2(floor(max(
                        firstGridPosition,
                        lastGridPosition))) +
                    1;

                localMinimum = max(
                    localMinimum,
                    int2(0, 0));
                localMaximum = min(
                    localMaximum,
                    int2(g_Shadow.textureSize) - 1);
                if (any(localMinimum > localMaximum))
                {
                    localMinimum = int2(
                        kEmptyMinimumBound,
                        kEmptyMinimumBound);
                    localMaximum = int2(
                        kEmptyMaximumBound,
                        kEmptyMaximumBound);
                }

                [loop]
                for (uint prefetchSample = 0u;
                    prefetchSample < chunkSampleCount;
                    ++prefetchSample)
                {
                    float prefetchDistance = float(
                        chunkStart + prefetchSample + 1u);
                    float2 prefetchPosition =
                        pixelCenter +
                        directionPixels * prefetchDistance;
                    float prefetchRayDepth =
                        receiverRayDepth +
                        prefetchDistance * depthPerStep;
                    if (any(prefetchPosition < 0.5f) ||
                        any(prefetchPosition >
                            float2(g_Shadow.textureSize) -
                                0.5f) ||
                        !IsDepthValid(prefetchRayDepth))
                    {
                        break;
                    }

                    float2 prefetchGrid =
                        prefetchPosition - 0.5f;
                    ++localDirectReadCount;
                    float prefetchMinorWeight = xMajor
                        ? frac(prefetchGrid.y)
                        : frac(prefetchGrid.x);
                    if (prefetchMinorWeight >
                        kMinimumInterpolationWeight)
                    {
                        ++localDirectReadCount;
                    }
                    if (g_Shadow
                            .bilinearSamplingOffsetMode != 0u)
                    {
                        int2 prefetchPixel =
                            int2(floor(prefetchGrid));
                        if (IsInsideTexture(
                                prefetchPixel + traceAxis))
                        {
                            ++localDirectReadCount;
                        }
                    }
                }
            }

            int2 waveMinimum = int2(
                WaveActiveMin(localMinimum.x),
                WaveActiveMin(localMinimum.y));
            int2 waveMaximum = int2(
                WaveActiveMax(localMaximum.x),
                WaveActiveMax(localMaximum.y));
            uint waveDirectReadCount =
                WaveActiveSum(localDirectReadCount);
            if (WaveIsFirstLane())
            {
                InterlockedMin(
                    s_CacheMinimum.x,
                    waveMinimum.x);
                InterlockedMin(
                    s_CacheMinimum.y,
                    waveMinimum.y);
                InterlockedMax(
                    s_CacheMaximum.x,
                    waveMaximum.x);
                InterlockedMax(
                    s_CacheMaximum.y,
                    waveMaximum.y);
                InterlockedAdd(
                    s_DirectReadCount,
                    waveDirectReadCount);
            }
            GroupMemoryBarrierWithGroupSync();

            cacheMinimum = s_CacheMinimum;
            int2 cacheMaximum = s_CacheMaximum;
            bool hasCacheBounds =
                all(cacheMinimum <= cacheMaximum);
            cacheWidth = hasCacheBounds
                ? uint(cacheMaximum.x - cacheMinimum.x + 1)
                : 0u;
            cacheHeight = hasCacheBounds
                ? uint(cacheMaximum.y - cacheMinimum.y + 1)
                : 0u;
            bool cacheDimensionsFit =
                cacheWidth <= kDepthCacheSideLimit &&
                cacheHeight <= kDepthCacheSideLimit &&
                cacheHeight > 0u;
            uint cachePixelCount = cacheDimensionsFit
                ? cacheWidth * cacheHeight
                : 0u;
            useCache =
                cachePixelCount > 0u &&
                cachePixelCount * 4u <=
                    s_DirectReadCount * 3u;

            if (useCache)
            {
                for (uint cacheIndex = groupIndex;
                    cacheIndex < cachePixelCount;
                    cacheIndex += 64u)
                {
                    int2 cachePixel =
                        cacheMinimum + int2(
                            int(cacheIndex % cacheWidth),
                            int(cacheIndex / cacheWidth));
                    s_DepthCache[cacheIndex] =
                        t_Depth.Load(
                            int3(cachePixel, 0));
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }

        [loop]
        for (uint chunkSample = 0u;
            chunkSample < chunkSampleCount;
            ++chunkSample)
        {
            uint sampleIndex = chunkStart + chunkSample;
            if (active)
            {
                float distancePixels =
                    float(sampleIndex + 1u);
                float2 samplePosition =
                    pixelCenter +
                    directionPixels * distancePixels;
                if (any(samplePosition < 0.5f) ||
                    any(samplePosition >
                        float2(g_Shadow.textureSize) - 0.5f))
                {
                    leftBounds = true;
                    active = false;
                }
                else
                {
                    float rayDepth =
                        receiverRayDepth +
                        distancePixels * depthPerStep;
                    if (!IsDepthValid(rayDepth))
                    {
                        active = false;
                    }
                    else
                    {
                        float2 sampleGrid =
                            samplePosition - 0.5f;
                        int2 samplePixel =
                            int2(floor(sampleGrid));
                        float minorWeight = xMajor
                            ? frac(sampleGrid.y)
                            : frac(sampleGrid.x);
                        int2 minorPixel =
                            samplePixel +
                            (xMajor
                                ? int2(0, 1)
                                : int2(1, 0));

                        float primaryDepth = LoadTraceDepth(
                            samplePixel,
                            useCache,
                            cacheMinimum,
                            cacheWidth,
                            cacheHeight);
                        float filteredDepth = primaryDepth;
                        bool interpolationEdge = false;
                        if (minorWeight >
                            kMinimumInterpolationWeight)
                        {
                            float minorDepth = LoadTraceDepth(
                                minorPixel,
                                useCache,
                                cacheMinimum,
                                cacheWidth,
                                cacheHeight);
                            interpolationEdge =
                                IsDepthDiscontinuity(
                                    primaryDepth,
                                    minorDepth);
                            filteredDepth = interpolationEdge
                                ? (minorWeight < 0.5f
                                    ? primaryDepth
                                    : minorDepth)
                                : lerp(
                                    primaryDepth,
                                    minorDepth,
                                    minorWeight);
                        }

                        float primaryEvidence = 0.0f;
                        bool primaryHit = false;
                        if (!(interpolationEdge &&
                                g_Shadow.ignoreEdgePixels != 0u))
                        {
                            primaryHit = EvaluateOccluder(
                                filteredDepth,
                                rayDepth,
                                primaryEvidence);
                        }

                        bool offsetHit = false;
                        float offsetEvidence = 0.0f;
                        if (g_Shadow
                                .bilinearSamplingOffsetMode != 0u)
                        {
                            int2 offsetPixel =
                                samplePixel + traceAxis;
                            float offsetDepth = LoadTraceDepth(
                                offsetPixel,
                                useCache,
                                cacheMinimum,
                                cacheWidth,
                                cacheHeight);
                            float offsetDistance =
                                distancePixels +
                                dot(
                                    float2(traceAxis),
                                    directionPixels);
                            float offsetRayDepth =
                                receiverRayDepth +
                                offsetDistance * depthPerStep;
                            bool offsetEdge =
                                IsDepthDiscontinuity(
                                    filteredDepth,
                                    offsetDepth);
                            if (!(offsetEdge &&
                                    g_Shadow.ignoreEdgePixels != 0u))
                            {
                                offsetHit = EvaluateOccluder(
                                    offsetDepth,
                                    offsetRayDepth,
                                    offsetEvidence);
                            }
                        }

                        bool hit = primaryHit || offsetHit;
                        float candidateEvidence = max(
                            primaryEvidence,
                            offsetEvidence);
                        if (hit)
                        {
                            candidateEvidence = saturate(
                                candidateEvidence *
                                g_Shadow.shadowContrast);
                            if (sampleIndex < hardCount)
                            {
                                hardEvidence = max(
                                    hardEvidence,
                                    candidateEvidence);
                            }
                            else
                            {
                                float fadeWeight = 1.0f;
                                if (fadeCount > 0u &&
                                    sampleIndex >= fadeStart)
                                {
                                    uint fadeIndex =
                                        sampleIndex - fadeStart;
                                    fadeWeight =
                                        1.0f -
                                        float(fadeIndex + 1u) /
                                        float(fadeCount + 1u);
                                }
                                softEvidence +=
                                    candidateEvidence * fadeWeight;
                            }

                            occlusion = max(
                                hardEvidence,
                                saturate(
                                    softEvidence * 0.25f));
                            hitProgress = max(
                                hitProgress,
                                float(sampleIndex + 1u) /
                                    float(sampleCount));
                            if (allowEarlyOut &&
                                occlusion >= 0.999f)
                            {
                                active = false;
                            }
                        }
                    }
                }
            }
        }

        if (cacheCandidate)
        {
            // Keep every lane out of the next chunk until all shared-depth
            // reads from this one are complete.
            GroupMemoryBarrierWithGroupSync();
        }
    }

    if (!outputPixelValid)
        return;

    float visibility = saturate(1.0f - occlusion);
    if (g_Shadow.debugView == 1u)
        visibility = occlusion;
    else if (g_Shadow.debugView == 2u)
        visibility = hitProgress;
    else if (g_Shadow.debugView == 3u)
        visibility = leftBounds ? 1.0f : 0.0f;

    u_Visibility[pixel] = visibility;
}
