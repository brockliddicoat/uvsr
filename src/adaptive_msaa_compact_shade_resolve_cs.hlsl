/*
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, subject to inclusion of this notice.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

/*
 * Copyright (c) 2017-2025 The Forge Interactive Inc.
 * Licensed under the Apache License, Version 2.0. You may obtain a copy at
 * http://www.apache.org/licenses/LICENSE-2.0
 */

// The closest-surface guide resolve follows the key/depth resource flow in
// The Forge Visibility_Buffer sample at commit
// a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b (Apache-2.0). UVSR reconstructs
// its native Donut G-buffer and motion data from an exact stable SurfaceKey.
// Mixed scenes read the dedicated fallback G-buffer only for samples whose
// final class marker proves either immutable snapshot fallback or a CPU-local
// per-view fallback from an otherwise reconstructable snapshot record.

#include "adaptive_msaa_compact_shade_common.hlsli"

VK_IMAGE_FORMAT("r32f")
RWTexture2D<float> u_ResolvedDepth : register(u0);
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_ResolvedDiffuse : register(u1);
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_ResolvedMaterial : register(u2);
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_ResolvedNormals : register(u3);
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_ResolvedEmissive : register(u4);
VK_IMAGE_FORMAT("r16f")
RWTexture2D<float> u_ResolvedAmbientOcclusion : register(u5);
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_ResolvedMotion : register(u6);

void ClearCompactGuides(int2 pixelPosition)
{
    u_ResolvedDepth[pixelPosition] = 0.0f;
    u_ResolvedDiffuse[pixelPosition] = 0.0f;
    u_ResolvedMaterial[pixelPosition] = 0.0f;
    u_ResolvedNormals[pixelPosition] = 0.0f;
    u_ResolvedEmissive[pixelPosition] = 0.0f;
    u_ResolvedAmbientOcclusion[pixelPosition] = 1.0f;
    u_ResolvedMotion[pixelPosition] = 0.0f;
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId >=
        uint2(g_CompactGBuffer.view.viewportSize)))
    {
        return;
    }
    const int2 pixelPosition = int2(dispatchThreadId) +
        int2(g_CompactGBuffer.view.viewportOrigin);

    uint2 keys[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    uint classes[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    float depths[ADAPTIVE_MSAA_COMPACT_SAMPLES];
    uint coveredMask = 0u;
    uint ownerSample = CompactInvalidWord;
    float ownerDepth = 0.0f;
    [loop]
    for (uint sampleIndex = 0u;
        sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
        ++sampleIndex)
    {
        // Every final sample descriptor is fetched exactly once. Coverage is
        // the class/key pair after depth testing, never SV_Coverage or depth.
        keys[sampleIndex] = t_CompactSurfaceOwnership.Load(
            pixelPosition, sampleIndex);
        classes[sampleIndex] = t_CompactVisibilityClass.Load(
            pixelPosition, sampleIndex);
        depths[sampleIndex] = t_CompactDepth.Load(
            pixelPosition, sampleIndex);
        const bool background =
            classes[sampleIndex] == CompactVisibilityClassBackground;
        if (background)
        {
            if (!CompactIsClearedVisibilityKey(keys[sampleIndex]) ||
                !CompactFinite(depths[sampleIndex]) ||
                asuint(depths[sampleIndex]) != 0u)
            {
                ClearCompactGuides(pixelPosition);
                return;
            }
            continue;
        }

        bool reconstruct;
        bool fallback;
        bool localFallback;
        uint fallbackReason;
        if (!CompactDecodeVisibilityClass(
                classes[sampleIndex],
                reconstruct,
                fallback,
                localFallback,
                fallbackReason))
        {
            ClearCompactGuides(pixelPosition);
            return;
        }
        uint keyReason = CompactReasonNone;
        if (reconstruct)
        {
            uint ignoredSlot;
            uint ignoredGeneration;
            keyReason = CompactValidateKey(
                keys[sampleIndex], ignoredSlot, ignoredGeneration);
            if (!CompactValidDeviceDepth(depths[sampleIndex]))
                keyReason = CompactReasonReconstructionFailure;
        }
        else
        {
            keyReason = CompactValidateFallbackOwnershipKey(
                keys[sampleIndex], fallbackReason, localFallback);
            if (!CompactValidFallbackDeviceDepth(depths[sampleIndex]))
                keyReason = CompactReasonReconstructionFailure;
        }
        if (keyReason != CompactReasonNone)
        {
            ClearCompactGuides(pixelPosition);
            return;
        }
        coveredMask |= 1u << sampleIndex;
        if (ownerSample == CompactInvalidWord ||
            depths[sampleIndex] > ownerDepth ||
            (depths[sampleIndex] == ownerDepth &&
                sampleIndex < ownerSample))
        {
            ownerSample = sampleIndex;
            ownerDepth = depths[sampleIndex];
        }
    }
    if (coveredMask == 0u)
    {
        ClearCompactGuides(pixelPosition);
        return;
    }

    // Build one reconstruction record per exact reconstructable key. Local
    // fallback samples remain singleton records and are copied only if they
    // are the deterministic closest owner.
    uint assignedMask = 0u;
    CompactSurface owner = (CompactSurface)0;
    [loop]
    for (uint representative = 0u;
        representative < ADAPTIVE_MSAA_COMPACT_SAMPLES;
        ++representative)
    {
        const uint representativeBit = 1u << representative;
        if ((coveredMask & representativeBit) == 0u ||
            (assignedMask & representativeBit) != 0u)
            continue;

        bool representativeReconstruct;
        bool representativeFallback;
        bool representativeLocalFallback;
        uint representativeFallbackReason;
        if (!CompactDecodeVisibilityClass(
                classes[representative],
                representativeReconstruct,
                representativeFallback,
                representativeLocalFallback,
                representativeFallbackReason))
        {
            ClearCompactGuides(pixelPosition);
            return;
        }
        if (representativeFallback)
        {
            assignedMask |= representativeBit;
            continue;
        }

        uint exactKeyMask = 0u;
        [loop]
        for (uint sampleIndex = 0u;
            sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
            ++sampleIndex)
        {
            if ((coveredMask & (1u << sampleIndex)) != 0u &&
                classes[sampleIndex] ==
                    CompactVisibilityClassReconstruct &&
                CompactSameSurface(
                    keys[representative], keys[sampleIndex]))
            {
                exactKeyMask |= 1u << sampleIndex;
            }
        }
        if (exactKeyMask == 0u)
        {
            ClearCompactGuides(pixelPosition);
            return;
        }

        CompactTriangle sharedTriangle;
        CompactSurface sharedSurface =
            CompactReconstructSurfaceWithTriangle(
            pixelPosition,
            representative,
            exactKeyMask,
            keys[representative],
            depths[representative],
            sharedTriangle);
        if (sharedSurface.valid == 0u)
        {
            ClearCompactGuides(pixelPosition);
            return;
        }
        [loop]
        for (uint sampleIndex = 0u;
            sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
            ++sampleIndex)
        {
            if ((exactKeyMask & (1u << sampleIndex)) == 0u)
                continue;
            CompactSurface sampleSurface = sharedSurface;
            if (sampleIndex != representative)
            {
                sampleSurface = CompactDeriveSampleFromSharedTriangle(
                    pixelPosition,
                    sampleIndex,
                    keys[sampleIndex],
                    depths[sampleIndex],
                    sharedSurface,
                    sharedTriangle);
            }
            if (sampleSurface.valid == 0u)
            {
                // Never substitute a farther valid sample for an invalid or
                // stale final owner and never relabel coverage as background.
                ClearCompactGuides(pixelPosition);
                return;
            }
            if (sampleIndex == ownerSample)
                owner = sampleSurface;
        }
        assignedMask |= exactKeyMask;
    }
    const bool ownerFallback =
        (classes[ownerSample] & CompactVisibilityClassFallbackBit) != 0u;
    if (assignedMask != coveredMask ||
        (!ownerFallback && owner.valid == 0u))
    {
        ClearCompactGuides(pixelPosition);
        return;
    }
    float4 fallbackNormal = 0.0f;
    if (ownerFallback)
    {
        fallbackNormal = t_CompactFallbackNormals.Load(
            pixelPosition, ownerSample);
        // Match the unchanged per-sample guide resolve exactly. A valid
        // fallback SurfaceKey may retain far-plane depth zero for lighting
        // containment, but it cannot manufacture screen-space guide
        // ownership. Never substitute a farther sample after the exact final
        // owner fails this predicate.
        const bool validFallbackGuideOwner =
            CompactFinite(ownerDepth) && ownerDepth > 0.0f &&
            dot(fallbackNormal.xyz, fallbackNormal.xyz) > 1e-12f;
        if (!validFallbackGuideOwner)
        {
            ClearCompactGuides(pixelPosition);
            return;
        }
    }
    u_ResolvedDepth[pixelPosition] = ownerDepth;
    if (ownerFallback)
    {
        u_ResolvedDiffuse[pixelPosition] =
            t_CompactFallbackDiffuse.Load(pixelPosition, ownerSample);
        u_ResolvedMaterial[pixelPosition] =
            t_CompactFallbackMaterial.Load(pixelPosition, ownerSample);
        u_ResolvedNormals[pixelPosition] = fallbackNormal;
        u_ResolvedEmissive[pixelPosition] =
            t_CompactFallbackEmissive.Load(pixelPosition, ownerSample);
        u_ResolvedAmbientOcclusion[pixelPosition] =
            t_CompactFallbackAmbientOcclusion.Load(
                pixelPosition, ownerSample);
        const float4 fallbackMotion = t_CompactFallbackMotion.Load(
            pixelPosition, ownerSample);
        u_ResolvedMotion[pixelPosition] =
            CompactFinite4(fallbackMotion) ? fallbackMotion : 0.0f;
    }
    else
    {
        u_ResolvedDiffuse[pixelPosition] = owner.channel0;
        u_ResolvedMaterial[pixelPosition] = owner.channel1;
        u_ResolvedNormals[pixelPosition] = owner.channel2;
        u_ResolvedEmissive[pixelPosition] = owner.channel3;
        u_ResolvedAmbientOcclusion[pixelPosition] =
            owner.ambientOcclusion;
        u_ResolvedMotion[pixelPosition] = owner.motion;
    }
}
