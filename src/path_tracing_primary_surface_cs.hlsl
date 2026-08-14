#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/bindless.h>
#include <donut/shaders/motion_vectors.hlsli>
#include "path_tracing_cb.h"
#include "sample_accumulation.hlsli"

cbuffer c_PathTracing : register(b0)
{
    PathTracingConstants g_PathTracing;
};

#ifndef UVSR_PT_RTXDI
#define UVSR_PT_RTXDI 0
#endif
#ifndef UVSR_PT_NEE_MODE
#define UVSR_PT_NEE_MODE 0
#endif

RaytracingAccelerationStructure t_WorldBvh : register(t0);
TextureCube<float4> t_Environment : register(t1);
Texture2DArray<float> t_Noise : register(t2);
#if UVSR_PT_RTXDI
Texture2D<float4> t_PreviousDirectReservoir : register(t3);
Texture2D<float4> t_PreviousSurface : register(t4);
Texture2D<uint> t_PreviousDirectSampleSeed : register(t9);
#endif
StructuredBuffer<LightConstants> t_PathTracingLights : register(t13);
StructuredBuffer<InstanceData> t_PathTracingInstances : register(t14);
Texture2D<float4> t_IndirectMean : register(t15);
Texture2D<uint2> t_PreviousPrimarySignature : register(t24);

RWTexture2D<float4> u_RawMean : register(u0);
RWTexture2D<float4> u_Display : register(u2);
#if UVSR_PT_RTXDI
RWTexture2D<float4> u_DirectReservoir : register(u3);
RWTexture2D<float4> u_Surface : register(u4);
RWTexture2D<uint> u_DirectSampleSeed : register(u13);
#endif
RWTexture2D<float4> u_ResidualMean : register(u9);
RWTexture2D<float4> u_SharedPositionHit : register(u16);
RWTexture2D<uint2> u_SharedGeometryMaterial : register(u17);
RWTexture2D<float4> u_SharedNormalAlpha : register(u18);
RWTexture2D<float4> u_SharedDiffuse : register(u19);
RWTexture2D<float4> u_SharedSpecular : register(u20);
RWTexture2D<float4> u_DirectMean : register(u21);
RWTexture2D<uint> u_DirectSampleCount : register(u22);
RWTexture2D<float4> u_PathMotion : register(u23);
RWTexture2D<float> u_PathDepth : register(u24);

#include "noise_sampling.hlsli"
#include "path_tracing_material.hlsli"
#include "path_tracing_sampling.hlsli"

static const uint UVSR_PATH_DEBUG_FINAL = 0u;
static const uint UVSR_PATH_DEBUG_DIRECT_RESERVOIR = 7u;

bool PathTracingPrimaryFlagIsSet(uint flag)
{
    return (g_PathTracing.flags & flag) != 0u;
}

void PathTracingGenerateSharedCameraRay(
    uint2 pixel,
    out float3 origin,
    out float3 direction)
{
    const float2 windowPixel = g_PathTracing.view.viewportOrigin +
        float2(pixel) + 0.5f;
    const float2 uv = (windowPixel - g_PathTracing.view.viewportOrigin) *
        g_PathTracing.view.viewportSizeInv;
    const float depth = PathTracingPrimaryFlagIsSet(
        UVSR_PATH_TRACING_FLAG_REVERSE_DEPTH) ? 1.0f : 0.0f;
    const float4 clipPosition = float4(
        uv.x * 2.0f - 1.0f,
        1.0f - uv.y * 2.0f,
        depth,
        1.0f);
    const float4 worldPositionH = mul(
        clipPosition,
        g_PathTracing.view.matClipToWorld);
    const float3 worldPosition = worldPositionH.xyz /
        max(abs(worldPositionH.w), 1.0e-8f);
    if (g_PathTracing.view.cameraDirectionOrPosition.w > 0.0f)
    {
        origin = g_PathTracing.view.cameraDirectionOrPosition.xyz;
        direction = PbrSafeNormalize(
            worldPosition - origin,
            float3(0.0f, 0.0f, 1.0f));
    }
    else
    {
        origin = worldPosition;
        direction = PbrSafeNormalize(
            g_PathTracing.view.cameraDirectionOrPosition.xyz,
            float3(0.0f, 0.0f, 1.0f));
    }
}

float3 PathTracingPrimaryConventionalDirect(
    PathTracingSurface surface,
    float3 viewDirection,
    inout PathTracingRandomStream randomStream)
{
    if (g_PathTracing.lightCount == 0u)
        return 0.0f;
    const uint candidateCount = max(g_PathTracing.neeCandidateCount, 1u);
    float3 estimate = 0.0f;
    [loop]
    for (uint candidate = 0u; candidate < candidateCount; ++candidate)
    {
        float selectionPdf;
        const uint lightIndex = PathTracingSelectLight(
            surface,
            viewDirection,
            PathTracingRandom(randomStream),
            selectionPdf);
        const uint sampleSeed = PathTracingRandomUint(randomStream);
        estimate += PathTracingEvaluateSelectedLightPrepared(
            surface,
            viewDirection,
            lightIndex,
            sampleSeed,
            selectionPdf);
    }
    return estimate / float(candidateCount);
}

#if UVSR_PT_RTXDI
int2 PathTracingPrimaryNeighbor(
    uint2 pixel,
    uint2 seed,
    uint neighborIndex)
{
    const uint rotation = PathTracingHash(
        seed.x ^ PathTracingHash(seed.y + 0x93b21f4du)) & 3u;
    const int2 offsets[4] = {
        int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
    };
    return clamp(
        int2(pixel) + offsets[(rotation + neighborIndex) & 3u],
        int2(0, 0),
        int2(g_PathTracing.dispatchExtent) - 1);
}

bool PathTracingPrimaryPreviousPixel(
    uint2 pixel,
    float3 previousWorldPosition,
    out int2 previousPixel,
    out bool reprojected)
{
    previousPixel = int2(pixel);
    reprojected = g_PathTracing.proposalReprojectionValid != 0u;
    if (!reprojected)
        return true;
    const float4 previousClip = mul(
        float4(previousWorldPosition, 1.0f),
        g_PathTracing.previousView.matWorldToClip);
    if (!all(isfinite(previousClip)) || !(previousClip.w > 1.0e-6f))
        return false;
    const float3 previousNdc = previousClip.xyz / previousClip.w;
    if (previousNdc.z < 0.0f || previousNdc.z > 1.0f)
        return false;
    const float2 previousWindow = previousNdc.xy *
            g_PathTracing.previousView.clipToWindowScale +
        g_PathTracing.previousView.clipToWindowBias;
    const float2 previousLocal = previousWindow -
        g_PathTracing.previousView.viewportOrigin;
    if (!all(isfinite(previousLocal)) || any(previousLocal < 0.0f) ||
        any(previousLocal >= g_PathTracing.previousView.viewportSize))
    {
        return false;
    }
    previousPixel = int2(floor(previousLocal));
    return all(previousPixel >= 0) &&
        all(previousPixel < int2(g_PathTracing.dispatchExtent));
}

PathTracingReservoir PathTracingPrimaryBuildDirectReservoir(
    uint2 pixel,
    PathTracingSurface surface,
    float3 viewDirection,
    float4 surfaceSignature,
    inout PathTracingRandomStream randomStream)
{
    PathTracingReservoir reservoir = (PathTracingReservoir)0;
    const uint candidateCount = max(g_PathTracing.neeCandidateCount, 1u);
    [loop]
    for (uint candidate = 0u; candidate < candidateCount; ++candidate)
    {
        float selectionPdf;
        const uint lightIndex = PathTracingSelectLight(
            surface,
            viewDirection,
            PathTracingRandom(randomStream),
            selectionPdf);
        const uint sampleSeed = PathTracingRandomUint(randomStream);
        const float target = PathTracingLuminance(
            PathTracingEvaluateUnshadowedLight(
                surface, viewDirection, lightIndex, sampleSeed));
        const float candidateWeight = selectionPdf > 0.0f &&
                isfinite(selectionPdf)
            ? target / selectionPdf
            : 0.0f;
        PathTracingReservoirUpdate(
            reservoir,
            float(lightIndex),
            sampleSeed,
            target,
            candidateWeight,
            1.0f,
            PathTracingRandom(randomStream));
    }

    if (!PathTracingPrimaryFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
    {
        return reservoir;
    }

    int2 previousCenter;
    bool reprojected;
    if (!PathTracingPrimaryPreviousPixel(
            pixel,
            surface.previousPosition,
            previousCenter,
            reprojected))
    {
        return reservoir;
    }

    const uint2 spatialSeed = randomStream.seed;
    if (PathTracingPrimaryFlagIsSet(
            UVSR_PATH_TRACING_FLAG_TEMPORAL_REUSE))
    {
        const float4 previousSignature =
            t_PreviousSurface[previousCenter];
        if (PathTracingSurfaceSignaturesAreCompatible(
                surfaceSignature,
                previousSignature,
                !reprojected))
        {
            const PathTracingReservoir temporal = PathTracingLoadReservoir(
                t_PreviousDirectReservoir[previousCenter],
                t_PreviousDirectSampleSeed[previousCenter]);
            const uint lightIndex = uint(max(temporal.selected, 0.0f));
            const float target = PathTracingLuminance(
                PathTracingEvaluateUnshadowedLight(
                    surface,
                    viewDirection,
                    lightIndex,
                    temporal.selectedSampleSeed));
            PathTracingReservoirCombine(
                reservoir,
                temporal,
                target,
                PathTracingRandom(randomStream));
        }
    }

    [loop]
    for (uint neighborIndex = 0u;
        neighborIndex < g_PathTracing.spatialNeighborCount;
        ++neighborIndex)
    {
        const int2 neighbor = PathTracingPrimaryNeighbor(
            uint2(previousCenter), spatialSeed, neighborIndex);
        if (all(neighbor == previousCenter))
            continue;
        const float4 previousSignature = t_PreviousSurface[neighbor];
        if (!PathTracingSurfaceSignaturesAreCompatible(
                surfaceSignature,
                previousSignature,
                true))
        {
            continue;
        }
        const PathTracingReservoir spatial = PathTracingLoadReservoir(
            t_PreviousDirectReservoir[neighbor],
            t_PreviousDirectSampleSeed[neighbor]);
        const uint lightIndex = uint(max(spatial.selected, 0.0f));
        const float target = PathTracingLuminance(
            PathTracingEvaluateUnshadowedLight(
                surface,
                viewDirection,
                lightIndex,
                spatial.selectedSampleSeed));
        PathTracingReservoirCombine(
            reservoir,
            spatial,
            target,
            PathTracingRandom(randomStream));
    }
    return reservoir;
}

float3 PathTracingPrimaryEvaluateDirectReservoir(
    PathTracingSurface surface,
    float3 viewDirection,
    PathTracingReservoir reservoir)
{
    if (!PathTracingReservoirIsValid(reservoir))
        return 0.0f;
    const float3 selected = PathTracingEvaluateSelectedLightPrepared(
        surface,
        viewDirection,
        uint(max(reservoir.selected, 0.0f)),
        reservoir.selectedSampleSeed,
        1.0f);
    return selected * PathTracingReservoirNormalization(reservoir);
}
#endif

float3 PathTracingPrimaryClamp(float3 value)
{
    if (!all(isfinite(value)))
        return 0.0f;
    value = max(value, 0.0f);
    if (!PathTracingPrimaryFlagIsSet(
            UVSR_PATH_TRACING_FLAG_FILTER_FIREFLIES))
    {
        return value;
    }
    const float luminance = PathTracingLuminance(value);
    if (!isfinite(luminance))
        return 0.0f;
    return luminance > g_PathTracing.fireflyThreshold
        ? value * (g_PathTracing.fireflyThreshold / luminance)
        : value;
}

float PathTracingPrimaryDeviceDepth(float3 worldPosition)
{
    const float4 clip = mul(
        float4(worldPosition, 1.0f),
        g_PathTracing.view.matWorldToClip);
    return clip.w > 1.0e-6f && all(isfinite(clip))
        ? saturate(clip.z / clip.w)
        : (PathTracingPrimaryFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REVERSE_DEPTH) ? 0.0f : 1.0f);
}

float4 PathTracingPrimaryMotion(
    uint2 pixel,
    float deviceDepth,
    float3 previousWorldPosition,
    float3 currentGeometricNormal,
    uint currentMaterial)
{
    if (g_PathTracing.previousViewValid == 0u ||
        !PathTracingPrimaryFlagIsSet(
            UVSR_PATH_TRACING_FLAG_PRIMARY_SIGNATURE_HISTORY) ||
        !all(isfinite(previousWorldPosition)))
    {
        return 0.0f;
    }
    const float4 previousClip = mul(
        float4(previousWorldPosition, 1.0f),
        g_PathTracing.previousView.matWorldToClip);
    if (!all(isfinite(previousClip)) || !(previousClip.w > 1.0e-6f))
        return 0.0f;
    const float3 previousNdc = previousClip.xyz / previousClip.w;
    if (previousNdc.z < 0.0f || previousNdc.z > 1.0f)
        return 0.0f;
    const float2 previousWindow = previousNdc.xy *
            g_PathTracing.previousView.clipToWindowScale +
        g_PathTracing.previousView.clipToWindowBias;
    const float2 previousLocal = previousWindow -
        g_PathTracing.previousView.viewportOrigin;
    if (any(previousLocal < 0.0f) ||
        any(previousLocal >= g_PathTracing.previousView.viewportSize))
    {
        return 0.0f;
    }
    const int2 previousPixel = int2(floor(previousLocal));
    if (any(previousPixel < 0) ||
        any(previousPixel >= int2(g_PathTracing.dispatchExtent)))
    {
        return 0.0f;
    }
    const uint2 previousSignature =
        t_PreviousPrimarySignature[previousPixel];
    if (previousSignature.y == 0u ||
        previousSignature.y != currentMaterial)
    {
        return 0.0f;
    }
    const float3 previousNormal = PathTracingUnpackUnitVectorHalf(
        previousSignature.x);
    if (dot(currentGeometricNormal, previousNormal) < 0.8f)
        return 0.0f;
    const float3 svPosition = float3(
        g_PathTracing.view.viewportOrigin + float2(pixel) + 0.5f,
        deviceDepth);
    const float3 motion = GetMotionVector(
        svPosition,
        previousWorldPosition,
        g_PathTracing.view,
        g_PathTracing.previousView);
    return all(isfinite(motion)) ? float4(motion, 1.0f) : 0.0f;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_PathTracing.dispatchExtent))
        return;

    float3 rayOrigin;
    float3 rayDirection;
    PathTracingGenerateSharedCameraRay(pixel, rayOrigin, rayDirection);

    const uint samplePhase = g_PathTracing.sampleSequencePhase;
    const float precomputedNoise = UVSRSamplePrecomputedNoise(
        t_Noise,
        g_PathTracing.noisePattern,
        pixel,
        g_PathTracing.dispatchExtent,
        samplePhase,
        0x5052494du);
    const uint2 sampleSeed = PathTracingMakeSampleSeed(
        pixel,
        samplePhase,
        u_DirectSampleCount[pixel],
        g_PathTracing.schedulingSerialLow,
        g_PathTracing.schedulingSerialHigh,
        0u,
        precomputedNoise);
    PathTracingRandomStream directStream =
        PathTracingCreateRandomStream(sampleSeed, 0x44495245u);

    PathTracingSurface surface;
    const bool hit = PathTracingTraceSurface(
        t_WorldBvh,
        rayOrigin,
        rayDirection,
        g_PathTracing.rayBias,
        g_PathTracing.maximumRayDistance,
        true,
        surface);

    // Shared primary owns the current surface/direct history. Initialize each
    // pixel so misses and no-light frames cannot expose stale ping-pong state.
#if UVSR_PT_RTXDI
    if (PathTracingPrimaryFlagIsSet(UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
    {
        u_DirectReservoir[pixel] = 0.0f;
        u_Surface[pixel] = 0.0f;
        u_DirectSampleSeed[pixel] = 0u;
    }
#endif

    float3 directSample = 0.0f;
#if UVSR_PT_RTXDI
    PathTracingReservoir directReservoir = (PathTracingReservoir)0;
#endif
    float deviceDepth = PathTracingPrimaryFlagIsSet(
        UVSR_PATH_TRACING_FLAG_REVERSE_DEPTH) ? 0.0f : 1.0f;
    float4 motion = 0.0f;
    if (hit)
    {
        const PbrPreparedMaterial material =
            PathTracingPrepareMaterial(surface);
        const float3 viewDirection = -rayDirection;
#if UVSR_PT_RTXDI
        const float3 signature = PathTracingSurfaceSignature(surface);
        const float4 surfaceSignature = float4(
            signature,
            float(surface.materialIndex + 1u));
#endif
        directSample = max(surface.material.emissiveColor, 0.0f);
#if UVSR_PT_RTXDI
        if (g_PathTracing.lightCount > 0u)
        {
            directReservoir = PathTracingPrimaryBuildDirectReservoir(
                pixel,
                surface,
                viewDirection,
                surfaceSignature,
                directStream);
            directSample += PathTracingPrimaryEvaluateDirectReservoir(
                surface,
                viewDirection,
                directReservoir);
            if (PathTracingPrimaryFlagIsSet(
                    UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
            {
                u_DirectReservoir[pixel] =
                    PathTracingStoreReservoir(directReservoir);
                u_Surface[pixel] = surfaceSignature;
                u_DirectSampleSeed[pixel] =
                    directReservoir.selectedSampleSeed;
            }
        }
#else
        directSample += PathTracingPrimaryConventionalDirect(
            surface,
            viewDirection,
            directStream);
#endif
        deviceDepth = PathTracingPrimaryDeviceDepth(surface.position);
        motion = PathTracingPrimaryMotion(
            pixel,
            deviceDepth,
            surface.previousPosition,
            surface.geometricNormal,
            surface.materialIndex + 1u);
        u_SharedPositionHit[pixel] = float4(
            surface.position,
            surface.hitDistance);
        u_SharedGeometryMaterial[pixel] = uint2(
            PathTracingPackUnitVectorHalf(surface.geometricNormal),
            surface.materialIndex + 1u);
        u_SharedNormalAlpha[pixel] = float4(
            surface.shadingNormal,
            material.alpha);
        u_SharedDiffuse[pixel] = float4(material.diffuseColor, 1.0f);
        u_SharedSpecular[pixel] = float4(material.specularF0, 1.0f);
    }
    else
    {
        if (PathTracingPrimaryFlagIsSet(
                UVSR_PATH_TRACING_FLAG_SHOW_ENVIRONMENT_BACKGROUND))
        {
            directSample = PathTracingSampleEnvironment(rayDirection);
        }
        u_SharedPositionHit[pixel] = 0.0f;
        u_SharedGeometryMaterial[pixel] = 0u;
        u_SharedNormalAlpha[pixel] = 0.0f;
        u_SharedDiffuse[pixel] = 0.0f;
        u_SharedSpecular[pixel] = 0.0f;
    }

    directSample = PathTracingPrimaryClamp(directSample);
    const bool accumulate = PathTracingPrimaryFlagIsSet(
        UVSR_PATH_TRACING_FLAG_ACCUMULATE_SAMPLES);
    uint oldCount = accumulate ? u_DirectSampleCount[pixel] : 0u;
    float3 oldMean = oldCount > 0u
        ? u_DirectMean[pixel].rgb
        : 0.0f;
    if (oldCount > 0u && !all(isfinite(oldMean)))
    {
        oldCount = 0u;
        oldMean = 0.0f;
    }
    const uint newCount = oldCount == 0xffffffffu
        ? oldCount
        : oldCount + 1u;
    const float meanWeight = oldCount == 0u
        ? 1.0f
        : UvsrSampleMeanWeight(
            g_PathTracing.accumulationAveraging,
            g_PathTracing.accumulationEffectiveHistory,
            oldCount,
            newCount);
    const float3 directMean = oldCount == 0u
        ? directSample
        : lerp(oldMean, directSample, meanWeight);
    u_DirectMean[pixel] = float4(directMean, 1.0f);
    u_DirectSampleCount[pixel] = newCount;
    u_PathMotion[pixel] = motion;
    u_PathDepth[pixel] = deviceDepth;

    float3 indirectMean = t_IndirectMean[int2(pixel)].rgb;
    if (!all(isfinite(indirectMean)))
        indirectMean = 0.0f;
    indirectMean = max(indirectMean, 0.0f);
    float3 rawMean = directMean + indirectMean;
    if (!all(isfinite(rawMean)))
        rawMean = 0.0f;
    rawMean = max(rawMean, 0.0f);
    u_RawMean[pixel] = float4(rawMean, 1.0f);
    if (PathTracingPrimaryFlagIsSet(
            UVSR_PATH_TRACING_FLAG_WRITE_STABLE_SIGNALS))
    {
        // Stable reconstruction consumes a full-resolution current direct
        // component even when indirect transport is sparse or adaptively
        // skipped.
        u_ResidualMean[pixel] = float4(directMean, 1.0f);
    }

    // The transport pass owns diagnostic presentation. Leaving Display
    // untouched here prevents the full-resolution primary pass from erasing
    // completed sparse diagnostic phases with Final Image every frame.
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_FINAL)
    {
        const float3 composite = PathTracingPrimaryClamp(
            rawMean);
        u_Display[pixel] = float4(
            min(max(composite, 0.0f), 65504.0f),
            1.0f);
    }
#if UVSR_PT_RTXDI
    else if (g_PathTracing.debugView ==
        UVSR_PATH_DEBUG_DIRECT_RESERVOIR)
    {
        const float4 storedReservoir =
            PathTracingStoreReservoir(directReservoir);
        const float selected = g_PathTracing.lightCount > 0u
            ? storedReservoir.x / float(g_PathTracing.lightCount)
            : 0.0f;
        u_Display[pixel] = float4(
            selected,
            saturate(log2(storedReservoir.y + 1.0f) / 16.0f),
            saturate(storedReservoir.w / 64.0f),
            1.0f);
    }
#endif
}
