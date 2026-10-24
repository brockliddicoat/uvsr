#pragma pack_matrix(row_major)

// Hierarchical traversal and wave compaction are adapted from AMD FidelityFX
// SDK 1.1.4 SSSR and Hybrid Reflections.
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "specular_reflections_cb.h"

cbuffer c_Specular : register(b0)
{
    SpecularReflectionConstants g_Specular;
};

#include "specular_reflections_shared.hlsli"

Texture2D<float> t_DepthHierarchy : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_Diffuse : register(t2);
Texture2D<float4> t_Material : register(t3);
Texture2D<float4> t_Normals : register(t4);
Texture2D<float4> t_Emissive : register(t5);
Texture2D<float> t_MaterialAmbientOcclusion : register(t6);
Texture2D<float4> t_SceneColorPyramid : register(t7);
TextureCubeArray<float4> t_SpecularEnvironment : register(t8);
SamplerState s_LinearClamp : register(s0);

RWTexture2D<float4> u_HitUvPdfDistance : register(u0);
RWTexture2D<float4> u_HitDirectionFootprint : register(u1);
RWTexture2D<uint4> u_HitMetadata : register(u2);
RWTexture2D<float> u_HitConfidence : register(u3);
RWTexture2D<float4> u_SourceRadiance : register(u4);
RWStructuredBuffer<uint> u_LowConfidenceRays : register(u5);
RWByteAddressBuffer u_RayCounter : register(u6);

void SpecularWriteInvalid(uint2 pixel)
{
    u_HitUvPdfDistance[pixel] = 0.0f;
    u_HitDirectionFootprint[pixel] = 0.0f;
    u_HitMetadata[pixel] = 0u;
    u_HitConfidence[pixel] = 0.0f;
    if (g_Specular.deferredColorLookup == 0u)
        u_SourceRadiance[pixel] = 0.0f;
}

float3 SpecularSampleEnvironment(float3 direction, float roughness)
{
    float3 radiance = t_SpecularEnvironment.SampleLevel(
        s_LinearClamp,
        float4(
            direction,
            g_Specular.specularEnvironmentArrayIndex),
        SpecularEnvironmentMip(roughness)).rgb *
        g_Specular.specularEnvironmentScale;
    return SpecularClampHalfRadiance(radiance);
}

float2 SpecularMipResolution(uint mip)
{
    const uint2 baseResolution = uint2(
        g_Specular.renderWidth,
        g_Specular.renderHeight);
    return float2(max(baseResolution >> mip, 1u));
}

void SpecularInitialAdvance(
    float3 origin,
    float3 direction,
    float3 inverseDirection,
    float2 mipResolution,
    float2 mipResolutionInverse,
    float2 floorOffset,
    float2 uvOffset,
    out float3 position,
    out float currentT)
{
    const float2 mipPosition = mipResolution * origin.xy;
    float2 plane = floor(mipPosition) + floorOffset;
    plane = plane * mipResolutionInverse + uvOffset;
    const float2 t = (plane - origin.xy) * inverseDirection.xy;
    currentT = min(t.x, t.y);
    position = origin + currentT * direction;
}

bool SpecularAdvance(
    float3 origin,
    float3 direction,
    float3 inverseDirection,
    float2 mipPosition,
    float2 mipResolutionInverse,
    float2 floorOffset,
    float2 uvOffset,
    float surfaceDepth,
    inout float3 position,
    inout float currentT)
{
    float2 xyPlane = floor(mipPosition) + floorOffset;
    xyPlane = xyPlane * mipResolutionInverse + uvOffset;
    const float3 boundary = float3(xyPlane, surfaceDepth);
    float3 t = (boundary - origin) * inverseDirection;
    const bool zPlaneReachable = g_Specular.reverseDepth != 0u
        ? direction.z < 0.0f
        : direction.z > 0.0f;
    if (!zPlaneReachable)
        t.z = SpecularFloatMaximum;
    const float nextT = min(min(t.x, t.y), t.z);
    const bool aboveSurface = g_Specular.reverseDepth != 0u
        ? surfaceDepth < position.z
        : surfaceDepth > position.z;
    const bool skippedTile = asuint(nextT) != asuint(t.z) && aboveSurface;
    currentT = aboveSurface ? nextT : currentT;
    position = origin + currentT * direction;
    return skippedTile;
}

float3 SpecularHierarchicalTrace(
    float3 origin,
    float3 direction,
    bool mirror,
    out bool validHit)
{
    const float3 inverseDirection = float3(
        abs(direction.x) > 1e-12f
            ? rcp(direction.x)
            : SpecularFloatMaximum,
        abs(direction.y) > 1e-12f
            ? rcp(direction.y)
            : SpecularFloatMaximum,
        abs(direction.z) > 1e-12f
            ? rcp(direction.z)
            : SpecularFloatMaximum);
    const int mostDetailedMip = mirror
        ? 0
        : int(g_Specular.mostDetailedMip);
    int currentMip = mostDetailedMip;
    float2 mipResolution = SpecularMipResolution(uint(currentMip));
    float2 mipResolutionInverse = rcp(mipResolution);
    float2 uvOffset = 0.001f * exp2(float(mostDetailedMip)) *
        float2(g_Specular.inverseRenderWidth,
            g_Specular.inverseRenderHeight);
    uvOffset = float2(
        direction.x < 0.0f ? -uvOffset.x : uvOffset.x,
        direction.y < 0.0f ? -uvOffset.y : uvOffset.y);
    const float2 floorOffset = float2(
        direction.x < 0.0f ? 0.0f : 1.0f,
        direction.y < 0.0f ? 0.0f : 1.0f);
    float currentT;
    float3 position;
    SpecularInitialAdvance(
        origin,
        direction,
        inverseDirection,
        mipResolution,
        mipResolutionInverse,
        floorOffset,
        uvOffset,
        position,
        currentT);

    uint iterations = 0u;
    bool lowOccupancy = false;
    while (iterations < g_Specular.maxTraversalIterations &&
        currentMip >= mostDetailedMip &&
        currentMip < int(g_Specular.depthMipLevels) &&
        !lowOccupancy)
    {
        if (any(position.xy < 0.0f) || any(position.xy > 1.0f) ||
            position.z < 0.0f || position.z > 1.0f)
        {
            break;
        }
        const int2 mipPixel = clamp(
            int2(mipResolution * position.xy),
            int2(0, 0),
            int2(mipResolution) - 1);
        const float surfaceDepth = t_DepthHierarchy.Load(
            int3(mipPixel, currentMip));
        lowOccupancy = !mirror &&
            WaveActiveCountBits(true) <=
                g_Specular.minTraversalOccupancy;
        const bool skippedTile = SpecularAdvance(
            origin,
            direction,
            inverseDirection,
            float2(mipPixel),
            mipResolutionInverse,
            floorOffset,
            uvOffset,
            surfaceDepth,
            position,
            currentT);
        currentMip += skippedTile ? 1 : -1;
        mipResolution *= skippedTile ? 0.5f : 2.0f;
        mipResolutionInverse *= skippedTile ? 2.0f : 0.5f;
        ++iterations;
    }
    validHit = currentMip < mostDetailedMip &&
        iterations < g_Specular.maxTraversalIterations &&
        all(position.xy >= 0.0f) && all(position.xy <= 1.0f) &&
        position.z >= 0.0f && position.z <= 1.0f;
    return position;
}

float SpecularValidateScreenHit(
    float3 hit,
    float2 sourceUv,
    float3 rayDirection,
    float3 rayOrigin,
    out float hitDistance,
    out uint2 hitPixel)
{
    hitDistance = g_Specular.rayMaxDistance;
    hitPixel = 0u;
    if (any(hit.xy < 0.0f) || any(hit.xy > 1.0f))
        return 0.0f;
    const float2 movement = abs(hit.xy - sourceUv) *
        float2(g_Specular.renderWidth, g_Specular.renderHeight);
    if (movement.x < 2.0f && movement.y < 2.0f)
        return 0.0f;

    hitPixel = min(
        uint2(hit.xy * float2(
            g_Specular.renderWidth,
            g_Specular.renderHeight)),
        uint2(g_Specular.renderWidth - 1.0f,
            g_Specular.renderHeight - 1.0f));
    const float actualDepth = t_Depth[hitPixel];
    const float4 hitNormalChannels = t_Normals[hitPixel];
    if (!(dot(hitNormalChannels.xyz, hitNormalChannels.xyz) > 1e-12f))
        return 0.0f;
    const float3 rayHitWorld = ReconstructWorldPosition(
        g_Specular.view,
        float2(hitPixel) + 0.5f,
        hit.z);
    const float3 surfaceWorld = ReconstructWorldPosition(
        g_Specular.view,
        float2(hitPixel) + 0.5f,
        actualDepth);
    if (!all(isfinite(rayHitWorld)) || !all(isfinite(surfaceWorld)))
        return 0.0f;
    hitDistance = length(surfaceWorld - rayOrigin);
    const float depthError = length(surfaceWorld - rayHitWorld);
    const float allowedDepthError = g_Specular.depthThickness +
        hitDistance * g_Specular.depthRejection;
    if (depthError > max(allowedDepthError, 1e-5f))
        return 0.0f;

    const PbrGBufferSurfaceNormals hitNormals =
        DecodePbrGBufferSurfaceNormals(
            hitNormalChannels,
            t_Material[hitPixel]);
    if (dot(hitNormals.geometricNormal, rayDirection) >
        g_Specular.backfaceRejection)
    {
        return 0.0f;
    }
    const float2 borderSize = 0.05f * float2(
        g_Specular.renderHeight / g_Specular.renderWidth,
        1.0f);
    const float2 border = smoothstep(0.0f, borderSize, hit.xy) *
        (1.0f - smoothstep(1.0f - borderSize, 1.0f, hit.xy));
    float confidence = 1.0f - smoothstep(
        0.0f,
        max(allowedDepthError, 1e-5f),
        depthError);
    confidence *= confidence * border.x * border.y;
    return saturate(confidence);
}

[numthreads(8, 8, 1)]
void Trace(uint2 pixel : SV_DispatchThreadID)
{
    if (!SpecularInViewport(pixel))
        return;
    SpecularWriteInvalid(pixel);
    if (!SpecularSelectRay(pixel))
        return;

    const float4 normalChannels = t_Normals[pixel];
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
        return;
    float4 channels[4];
    channels[0] = t_Diffuse[pixel];
    channels[1] = t_Material[pixel];
    channels[2] = normalChannels;
    channels[3] = t_Emissive[pixel];
    const PbrGBufferData gbuffer = DecodePbrGBuffer(
        channels,
        t_MaterialAmbientOcclusion[pixel]);
    const float roughness = gbuffer.material.perceptualRoughness;
    if (roughness > g_Specular.maxRoughness)
        return;

    const float depth = t_Depth[pixel];
    const float2 pixelCenter = float2(pixel) + 0.5f;
    const float3 worldPosition = ReconstructWorldPosition(
        g_Specular.view,
        pixelCenter,
        depth);
    if (!all(isfinite(worldPosition)))
        return;
    const float3 viewDirection = -GetIncidentVector(
        g_Specular.view.cameraDirectionOrPosition,
        worldPosition);
    PbrSurfaceInteraction interaction;
    interaction.position = worldPosition;
    interaction.shadingNormal = gbuffer.shadingNormal;
    interaction.geometricNormal = gbuffer.geometricNormal;
    interaction.viewDirection = viewDirection;
    const PbrPreparedSurface surface = PreparePbrSurface(interaction);
    float3 rayDirection;
    float sourcePdf;
    if (!SpecularSampleReflectionDirection(
            surface,
            roughness,
            pixel,
            rayDirection,
            sourcePdf))
    {
        return;
    }

    const float2 sourceUv = pixelCenter * float2(
        g_Specular.inverseRenderWidth,
        g_Specular.inverseRenderHeight);
    bool endpointValid;
    const float3 endpoint = SpecularProjectClippedRayEndpoint(
        worldPosition,
        worldPosition + rayDirection * g_Specular.rayMaxDistance,
        endpointValid);
    const float3 origin = float3(sourceUv, depth);
    const float3 screenDirection = endpoint - origin;
    bool hitValid = false;
    float3 hit = float3(-1.0f, -1.0f, depth);
    if (endpointValid && dot(screenDirection, screenDirection) > 1e-12f)
    {
        hit = SpecularHierarchicalTrace(
            origin,
            screenDirection,
            roughness < 0.001f,
            hitValid);
    }

    float hitDistance = g_Specular.rayMaxDistance;
    uint2 hitPixel = 0u;
    const float confidence = hitValid
        ? SpecularValidateScreenHit(
            hit,
            sourceUv,
            rayDirection,
            worldPosition,
            hitDistance,
            hitPixel)
        : 0.0f;
    const bool screenHit = confidence > 0.0f;
    if (!screenHit)
    {
        hit.xy = -1.0f;
        hitDistance = g_Specular.rayMaxDistance;
    }
    const float footprint = SpecularTraceFootprintPixels(
        roughness,
        hitDistance,
        surface.shadingNoV,
        SpecularProjectionPixelsPerMeter(worldPosition));
    const uint path = screenHit
        ? SpecularHitPathScreen
        : SpecularHitPathEnvironment;
    u_HitUvPdfDistance[pixel] = float4(
        hit.xy,
        sourcePdf,
        hitDistance);
    u_HitDirectionFootprint[pixel] = float4(
        rayDirection,
        footprint);
    u_HitMetadata[pixel] = uint4(
        SpecularMakeHitFlags(path),
        screenHit ? SpecularPackPixel(hitPixel) : 0u,
        0xffffffffu,
        0xffffffffu);
    u_HitConfidence[pixel] = confidence;

    if (g_Specular.deferredColorLookup == 0u)
    {
        const float3 environment = SpecularSampleEnvironment(
            rayDirection,
            roughness);
        float3 radiance = environment;
        if (screenHit)
        {
            const float3 screenRadiance = max(
                t_SceneColorPyramid.SampleLevel(
                    s_LinearClamp,
                    hit.xy,
                    SpecularMipFromTraceFootprint(footprint)).rgb,
                0.0f);
            radiance = lerp(environment, screenRadiance, confidence);
        }
        u_SourceRadiance[pixel] = float4(
            SpecularClampHalfRadiance(radiance),
            roughness);
    }

    const bool deferRay = g_Specular.reflectionTechnique == 2u &&
        g_Specular.useRayTracing != 0u &&
        confidence < g_Specular.screenSpaceConfidence;
    const bool compactRay = deferRay &&
        g_Specular.compactRayTracing != 0u;
    const uint waveCount = WaveActiveCountBits(compactRay);
    const uint waveOffset = WavePrefixCountBits(compactRay);
    uint listBase = 0u;
    if (WaveIsFirstLane() && waveCount > 0u)
        u_RayCounter.InterlockedAdd(0u, waveCount, listBase);
    listBase = WaveReadLaneFirst(listBase);
    if (compactRay)
    {
        u_LowConfidenceRays[listBase + waveOffset] =
            pixel.y * uint(g_Specular.renderWidth) + pixel.x;
    }
}
