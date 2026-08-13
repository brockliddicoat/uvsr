#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
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
#ifndef UVSR_PT_SOLVER
#define UVSR_PT_SOLVER 0
#endif

#if UVSR_PT_SOLVER < 0 || UVSR_PT_SOLVER > 2
#error Unsupported UVSR_PT_SOLVER
#endif

RaytracingAccelerationStructure t_WorldBvh : register(t0);
TextureCube<float4> t_Environment : register(t1);
Texture2DArray<float> t_Noise : register(t2);
#if UVSR_PT_RTXDI
Texture2D<float4> t_PreviousDirectReservoir : register(t3);
Texture2D<float4> t_PreviousSurface : register(t4);
#endif
#if UVSR_PT_SOLVER == 2
Texture2D<float4> t_PreviousGiCheckpointReservoir : register(t5);
Texture2D<uint> t_PreviousGiCheckpointCount : register(t6);
#endif
#if UVSR_PT_SOLVER == 1
Texture2D<uint2> t_PreviousPathSeed : register(t7);
Texture2D<float4> t_PreviousPathSeedStatistics : register(t8);
#endif
Texture2D<uint> t_PreviousDirectSampleSeed : register(t9);
StructuredBuffer<LightConstants> t_PathTracingLights : register(t13);

RWTexture2D<float4> u_RawMean : register(u0);
RWTexture2D<uint> u_SuccessfulSampleCount : register(u1);
RWTexture2D<float4> u_Display : register(u2);
#if UVSR_PT_RTXDI
RWTexture2D<float4> u_DirectReservoir : register(u3);
RWTexture2D<float4> u_Surface : register(u4);
#endif
#if UVSR_PT_SOLVER == 2
RWTexture2D<float4> u_GiCheckpointReservoir : register(u5);
RWTexture2D<uint> u_GiCheckpointCount : register(u6);
#endif
#if UVSR_PT_SOLVER == 1
RWTexture2D<uint2> u_PathSeed : register(u7);
RWTexture2D<float4> u_PathSeedStatistics : register(u8);
#endif
// Stable transport ABI: these slots are declared for every specialization and
// always bound by the CPU. Inactive solver/method combinations bind safe 1x1
// resources and never access them.
RWTexture2D<float4> u_ResidualMean : register(u9);
RWTexture2D<float4> u_DiffuseSuffixMean : register(u10);
RWTexture2D<float4> u_PrimaryNormalRoughness : register(u11);
RWTexture2D<float> u_PrimaryViewZ : register(u12);
RWTexture2D<uint> u_DirectSampleSeed : register(u13);
RWTexture2D<float4> u_ColorVariance : register(u14);

#include "noise_sampling.hlsli"
#include "path_tracing_material.hlsli"
#include "path_tracing_sampling.hlsli"

static const uint UVSR_PATH_DEBUG_FINAL = 0u;
static const uint UVSR_PATH_DEBUG_ALBEDO = 1u;
static const uint UVSR_PATH_DEBUG_GEOMETRIC_NORMAL = 2u;
static const uint UVSR_PATH_DEBUG_SHADING_NORMAL = 3u;
static const uint UVSR_PATH_DEBUG_SAMPLE_COUNT = 4u;
static const uint UVSR_PATH_DEBUG_UPDATE_RATE = 5u;
static const uint UVSR_PATH_DEBUG_STABLE_PLANE = 6u;
static const uint UVSR_PATH_DEBUG_DIRECT_RESERVOIR = 7u;
static const uint UVSR_PATH_DEBUG_GI_RESERVOIR = 8u;
static const uint UVSR_PATH_DEBUG_PRIMARY_TRANSPORT = 9u;
static const uint UVSR_PATH_DEBUG_INDIRECT_TRANSPORT = 10u;

bool PathTracingFlagIsSet(uint flag)
{
    return (g_PathTracing.flags & flag) != 0u;
}

#if UVSR_PT_RTXDI || UVSR_PT_SOLVER == 1
int2 PathTracingPreviousNeighbor(
    uint2 pixel,
    uint2 seed,
    uint domain)
{
    // Neighbor identity comes only from seed space, never from a radiance,
    // target, reservoir selection, or prior combined estimate.
    const uint direction = PathTracingHash(
        seed.x ^ PathTracingHash(seed.y + domain)) & 3u;
    const int2 offsets[4] = {
        int2(-1, 0),
        int2(1, 0),
        int2(0, -1),
        int2(0, 1)
    };
    return clamp(
        int2(pixel) + offsets[direction],
        int2(0, 0),
        int2(g_PathTracing.dispatchExtent) - 1);
}
#endif

bool PathTracingResolvePreviousDonorPixel(
    uint2 currentPixel,
    float3 currentWorldPosition,
    bool currentSurfaceValid,
    out int2 previousPixel,
    out bool reprojected)
{
    previousPixel = int2(currentPixel);
    reprojected = g_PathTracing.previousViewValid != 0u;
    if (!reprojected)
        return true;
    if (!currentSurfaceValid || !all(isfinite(currentWorldPosition)))
        return false;

    const float4 previousClip = mul(
        float4(currentWorldPosition, 1.0f),
        g_PathTracing.previousView.matWorldToClipNoOffset);
    if (!all(isfinite(previousClip)) ||
        !(previousClip.w > 1.0e-6f) ||
        previousClip.z < 0.0f ||
        previousClip.z > previousClip.w)
    {
        return false;
    }

    const float2 previousNdc = previousClip.xy / previousClip.w;
    const float2 previousWindow = previousNdc *
            g_PathTracing.previousView.clipToWindowScale +
        g_PathTracing.previousView.clipToWindowBias;
    const float2 previousLocal = previousWindow -
        g_PathTracing.previousView.viewportOrigin;
    if (!all(isfinite(previousLocal)) ||
        any(previousLocal < 0.0f) ||
        any(previousLocal >= g_PathTracing.previousView.viewportSize))
    {
        return false;
    }

    const int2 candidate = int2(floor(previousLocal));
    if (any(candidate < 0) ||
        any(candidate >= int2(g_PathTracing.dispatchExtent)))
    {
        return false;
    }
    previousPixel = candidate;
    return true;
}

#if UVSR_PT_RTXDI
void PathTracingCarryDirectProposal(uint2 pixel)
{
    if (g_PathTracing.previousViewValid != 0u)
    {
        // A failed camera-motion sample has no current world position to
        // reproject. Invalidate its proposal instead of copying an unrelated
        // screen-space identity into the new history.
        u_DirectReservoir[pixel] = 0.0f;
        u_Surface[pixel] = 0.0f;
        u_DirectSampleSeed[pixel] = 0u;
        return;
    }
    u_DirectReservoir[pixel] =
        t_PreviousDirectReservoir[int2(pixel)];
    u_Surface[pixel] = t_PreviousSurface[int2(pixel)];
    u_DirectSampleSeed[pixel] =
        t_PreviousDirectSampleSeed[int2(pixel)];
}
#endif

#if UVSR_PT_SOLVER == 1
void PathTracingCarryReplaySeed(uint2 pixel)
{
    if (g_PathTracing.previousViewValid != 0u)
    {
        u_PathSeed[pixel] = 0u;
        u_PathSeedStatistics[pixel] = 0.0f;
        return;
    }
    u_PathSeed[pixel] = t_PreviousPathSeed[int2(pixel)];
    u_PathSeedStatistics[pixel] =
        t_PreviousPathSeedStatistics[int2(pixel)];
}
#endif

void PathTracingGenerateCameraRay(
    uint2 pixel,
    float2 jitter,
    out float3 origin,
    out float3 direction)
{
    const float2 windowPixel = g_PathTracing.view.viewportOrigin +
        float2(pixel) + jitter;
    const float2 uv = (windowPixel - g_PathTracing.view.viewportOrigin) *
        g_PathTracing.view.viewportSizeInv;
    const float depth = PathTracingFlagIsSet(
        UVSR_PATH_TRACING_FLAG_REVERSE_DEPTH) ? 1.0f : 0.0f;
    const float4 clipPosition = float4(
        uv.x * 2.0f - 1.0f,
        1.0f - uv.y * 2.0f,
        depth,
        1.0f);
    const float4 worldPositionH = mul(
        clipPosition,
        g_PathTracing.view.matClipToWorldNoOffset);
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

float3 PathTracingConventionalDirect(
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
PathTracingReservoir PathTracingBuildDirectReservoir(
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

    if (PathTracingFlagIsSet(UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
    {
        int2 previousCenter;
        bool reprojected;
        const bool previousCenterValid =
            PathTracingResolvePreviousDonorPixel(
                pixel,
                surface.position,
                true,
                previousCenter,
                reprojected);
        if (previousCenterValid)
        {
            const float4 temporalSurface =
                t_PreviousSurface[previousCenter];
            if (PathTracingSurfaceSignaturesAreCompatible(
                    surfaceSignature,
                    temporalSurface,
                    !reprojected))
            {
                const PathTracingReservoir temporal =
                    PathTracingLoadReservoir(
                        t_PreviousDirectReservoir[previousCenter],
                        t_PreviousDirectSampleSeed[previousCenter]);
                const uint temporalLight = uint(max(
                    temporal.selected, 0.0f));
                const float target = PathTracingLuminance(
                    PathTracingEvaluateUnshadowedLight(
                        surface,
                        viewDirection,
                        temporalLight,
                        temporal.selectedSampleSeed));
                PathTracingReservoirCombine(
                    reservoir,
                    temporal,
                    target,
                    PathTracingRandom(randomStream));
            }

            const int2 neighbor = PathTracingPreviousNeighbor(
                uint2(previousCenter),
                randomStream.seed,
                0x93b21f4du);
            if (any(neighbor != previousCenter))
            {
                const float4 spatialSurface =
                    t_PreviousSurface[neighbor];
                if (PathTracingSurfaceSignaturesAreCompatible(
                        surfaceSignature,
                        spatialSurface,
                        true))
                {
                    const PathTracingReservoir spatial =
                        PathTracingLoadReservoir(
                            t_PreviousDirectReservoir[neighbor],
                            t_PreviousDirectSampleSeed[neighbor]);
                    const uint spatialLight = uint(max(
                        spatial.selected, 0.0f));
                    const float target = PathTracingLuminance(
                        PathTracingEvaluateUnshadowedLight(
                            surface,
                            viewDirection,
                            spatialLight,
                            spatial.selectedSampleSeed));
                    PathTracingReservoirCombine(
                        reservoir,
                        spatial,
                        target,
                        PathTracingRandom(randomStream));
                }
            }
        }
    }
    return reservoir;
}

float3 PathTracingEvaluateDirectReservoir(
    PathTracingSurface surface,
    float3 viewDirection,
    PathTracingReservoir reservoir)
{
    if (!PathTracingReservoirIsValid(reservoir))
        return 0.0f;
    const uint lightIndex = uint(max(reservoir.selected, 0.0f));
    const float3 selectedContribution =
        PathTracingEvaluateSelectedLightPrepared(
            surface,
            viewDirection,
            lightIndex,
            reservoir.selectedSampleSeed,
            1.0f);
    return selectedContribution *
        PathTracingReservoirNormalization(reservoir);
}
#endif

struct PathTracingSampleResult
{
    // The solver always returns primaryBase + exactly one local or resampled
    // indirectSuffix. Keeping the split explicit prevents double counting.
    float3 primaryBase;
    float3 indirectSuffix;
    float3 albedo;
    float3 geometricNormal;
    float3 shadingNormal;
    float3 primaryWorldPosition;
    float4 stablePlane;
    float4 surfaceSignature;
    float4 primaryNormalRoughness;
    float primaryViewZ;
#if UVSR_PT_RTXDI
    PathTracingReservoir directReservoir;
#endif
    uint firstContinuationIsDiffuse;
    uint valid;
};

void PathTracingLoadPrimaryResolveGuide(
    uint2 pixel,
    out float4 normalRoughness,
    out float viewZ)
{
    normalRoughness = 0.0f;
    viewZ = 0.0f;

    // Reconstruction guides describe the deterministic pixel center. They do
    // not replace the stochastic radiance ray and therefore cannot bias the
    // path estimator. PathTracingTraceSurface preserves the same alpha-tested
    // material-visibility contract as the primary transport query.
    float3 rayOrigin;
    float3 rayDirection;
    PathTracingGenerateCameraRay(
        pixel,
        float2(0.5f, 0.5f),
        rayOrigin,
        rayDirection);
    PathTracingSurface surface;
    if (!PathTracingTraceSurface(
            t_WorldBvh,
            rayOrigin,
            rayDirection,
            g_PathTracing.rayBias,
            g_PathTracing.maximumRayDistance,
            surface))
    {
        return;
    }

    const PbrPreparedMaterial material =
        PathTracingPrepareMaterial(surface);
    const float centerViewZ = abs(mul(
        float4(surface.position, 1.0f),
        g_PathTracing.view.matWorldToView).z);
    const float centerRoughness = sqrt(max(material.alpha, 0.0f));
    if (!all(isfinite(surface.shadingNormal)) ||
        dot(surface.shadingNormal, surface.shadingNormal) <= 0.25f ||
        !isfinite(centerRoughness) ||
        !isfinite(centerViewZ) || !(centerViewZ > 0.0f))
    {
        return;
    }

    normalRoughness = float4(
        surface.shadingNormal,
        saturate(centerRoughness));
    viewZ = centerViewZ;
}

PathTracingSampleResult PathTracingIntegrate(
    uint2 pixel,
    uint2 continuationSeed,
    bool buildPrimaryDirectReservoir)
{
    PathTracingSampleResult result = (PathTracingSampleResult)0;
    result.valid = 1u;
    PathTracingRandomStream cameraStream =
        PathTracingCreateRandomStream(continuationSeed, 0x43414d45u);
    PathTracingRandomStream continuationStream =
        PathTracingCreateRandomStream(continuationSeed, 0x53554646u);
    // Primary direct lighting owns a disjoint random domain, so enabling
    // RTXDI or changing its candidate count cannot shift BSDF, NEE at later
    // bounces, Russian roulette, or any other indirect-suffix dimension.
    PathTracingRandomStream primaryDirectStream =
        PathTracingCreateRandomStream(continuationSeed, 0x44495245u);
    float3 rayOrigin;
    float3 rayDirection;
    PathTracingGenerateCameraRay(
        pixel,
        float2(
            PathTracingRandom(cameraStream),
            PathTracingRandom(cameraStream)),
        rayOrigin,
        rayDirection);

    float3 throughput = 1.0f;
    uint stablePlaneIndex = 0u;
    [loop]
    for (uint bounce = 0u; bounce < g_PathTracing.maxBounces; ++bounce)
    {
        PathTracingSurface surface;
        if (!PathTracingTraceSurface(
                t_WorldBvh,
                rayOrigin,
                rayDirection,
                g_PathTracing.rayBias,
                g_PathTracing.maximumRayDistance,
                surface))
        {
            const bool showPrimaryEnvironment = bounce > 0u ||
                PathTracingFlagIsSet(
                    UVSR_PATH_TRACING_FLAG_SHOW_ENVIRONMENT_BACKGROUND);
            if (showPrimaryEnvironment)
            {
                const float3 contribution = throughput *
                    PathTracingSampleEnvironment(rayDirection);
                if (bounce == 0u)
                    result.primaryBase += contribution;
                else
                    result.indirectSuffix += contribution;
            }
            break;
        }

        const float3 viewDirection = -rayDirection;
        if (bounce == 0u)
        {
            result.albedo = max(surface.material.diffuseAlbedo, 0.0f);
            result.geometricNormal = surface.geometricNormal;
            result.shadingNormal = surface.shadingNormal;
            result.primaryWorldPosition = surface.position;
            const PbrPreparedMaterial primaryMaterial =
                PathTracingPrepareMaterial(surface);
            const float primaryViewZ = abs(mul(
                float4(surface.position, 1.0f),
                g_PathTracing.view.matWorldToView).z);
            const float primaryRoughness = sqrt(max(
                primaryMaterial.alpha,
                0.0f));
            if (all(isfinite(surface.shadingNormal)) &&
                dot(surface.shadingNormal, surface.shadingNormal) > 0.25f &&
                isfinite(primaryRoughness) &&
                isfinite(primaryViewZ) && primaryViewZ > 0.0f)
            {
                result.primaryNormalRoughness = float4(
                    surface.shadingNormal,
                    saturate(primaryRoughness));
                result.primaryViewZ = primaryViewZ;
            }
            const float3 signature =
                PathTracingSurfaceSignature(surface);
            result.surfaceSignature = float4(
                signature,
                float(surface.materialIndex + 1u));
        }

        const float3 emissionContribution = throughput *
            max(surface.material.emissiveColor, 0.0f);
        if (bounce == 0u)
            result.primaryBase += emissionContribution;
        else
            result.indirectSuffix += emissionContribution;

        if (g_PathTracing.lightCount > 0u)
        {
            float3 directLighting;
#if UVSR_PT_RTXDI
            if (bounce == 0u)
            {
                directLighting = 0.0f;
                if (buildPrimaryDirectReservoir)
                {
                    result.directReservoir =
                        PathTracingBuildDirectReservoir(
                            pixel,
                            surface,
                            viewDirection,
                            result.surfaceSignature,
                            primaryDirectStream);
                    directLighting = PathTracingEvaluateDirectReservoir(
                        surface,
                        viewDirection,
                        result.directReservoir);
                }
            }
            else
            {
                directLighting = PathTracingConventionalDirect(
                    surface,
                    viewDirection,
                    continuationStream);
            }
#else
            if (bounce == 0u)
            {
                directLighting = PathTracingConventionalDirect(
                    surface,
                    viewDirection,
                    primaryDirectStream);
            }
            else
            {
                directLighting = PathTracingConventionalDirect(
                    surface,
                    viewDirection,
                    continuationStream);
            }
#endif
            const float3 directContribution = throughput * directLighting;
            if (bounce == 0u)
                result.primaryBase += directContribution;
            else
                result.indirectSuffix += directContribution;
        }

        const PathTracingBsdfSample bsdfSample = PathTracingSampleBsdf(
            surface,
            viewDirection,
            float3(
                PathTracingRandom(continuationStream),
                PathTracingRandom(continuationStream),
                PathTracingRandom(continuationStream)),
            false);
        if (bsdfSample.valid == 0u)
            break;

        if (bounce == 0u && g_PathTracing.stablePlaneCount > 0u)
        {
            result.firstContinuationIsDiffuse =
                bsdfSample.diffuseBranch != 0u ? 1u : 0u;
            stablePlaneIndex = bsdfSample.diffuseBranch != 0u
                ? 0u
                : min(1u, g_PathTracing.stablePlaneCount - 1u);
        }

        throughput *= bsdfSample.weight;
        if (!all(isfinite(throughput)))
        {
            result.valid = 0u;
            break;
        }
        if (bounce + 1u >= g_PathTracing.russianRouletteStart)
        {
            const float survivalProbability = clamp(
                max(throughput.r, max(throughput.g, throughput.b)),
                0.05f,
                0.95f);
            if (PathTracingRandom(continuationStream) >=
                survivalProbability)
                break;
            throughput /= survivalProbability;
        }
        rayOrigin = PathTracingPrepareRayOrigin(
            surface.position,
            surface.geometricNormal,
            bsdfSample.direction,
            g_PathTracing.rayBias);
        rayDirection = bsdfSample.direction;
    }

    if (g_PathTracing.stablePlaneCount > 0u)
    {
        const float stableEnergy = PathTracingLuminance(
            result.primaryBase + result.indirectSuffix);
        result.stablePlane = float4(0.0f, 0.0f, 0.0f,
            (float(stablePlaneIndex) + 0.5f) /
                float(g_PathTracing.stablePlaneCount));
        result.stablePlane[stablePlaneIndex] = stableEnergy;
    }
    result.valid = result.valid != 0u &&
        all(isfinite(result.primaryBase)) &&
        all(isfinite(result.indirectSuffix)) ? 1u : 0u;
    return result;
}

#if UVSR_PT_SOLVER == 2
float3 PathTracingResolveGiCheckpoint(
    uint2 pixel,
    uint2 currentSeed,
    float3 currentIndirectSuffix)
{
    if (!PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REUSE_GI_CHECKPOINT))
    {
        return currentIndirectSuffix;
    }

    PathTracingRandomStream reservoirStream =
        PathTracingCreateRandomStream(currentSeed, 0x47495253u);
    PathTracingContributionReservoir reservoir =
        (PathTracingContributionReservoir)0;
    PathTracingContributionReservoirUpdate(
        reservoir,
        currentIndirectSuffix,
        PathTracingRandom(reservoirStream));

    // The persistent payload is always one local checkpoint (M=1), never
    // this combined estimate. Same-pixel, same-epoch reuse requires no
    // cross-pixel transform, visibility replay, or Jacobian.
    const uint previousCount =
        t_PreviousGiCheckpointCount[int2(pixel)];
    const float4 previousLocal =
        t_PreviousGiCheckpointReservoir[int2(pixel)];
    if (previousCount == 1u && all(isfinite(previousLocal)))
    {
        PathTracingContributionReservoirUpdate(
            reservoir,
            previousLocal.rgb,
            PathTracingRandom(reservoirStream));
    }
    return PathTracingContributionReservoirEstimate(reservoir);
}
#endif

#if UVSR_PT_SOLVER == 1
bool PathTracingStoredSeedIsLocal(float4 statistics)
{
    return statistics.w > 0.5f &&
        abs(statistics.z - 1.0f) < 0.25f &&
        all(isfinite(statistics));
}

void PathTracingReplaySeedCandidate(
    uint2 pixel,
    uint2 seed,
    inout PathTracingRandomStream reservoirStream,
    inout PathTracingContributionReservoir reservoir)
{
    const PathTracingSampleResult replay =
        PathTracingIntegrate(pixel, seed, false);
    if (replay.valid != 0u)
    {
        PathTracingContributionReservoirUpdate(
            reservoir,
            replay.indirectSuffix,
            PathTracingRandom(reservoirStream));
    }
}

float3 PathTracingResolveSeedReplay(
    uint2 pixel,
    uint2 currentSeed,
    float3 currentIndirectSuffix,
    float3 currentWorldPosition,
    bool currentSurfaceValid)
{
    if (!PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REPLAY_PATH_SEEDS))
    {
        return currentIndirectSuffix;
    }

    int2 previousCenter;
    bool reprojected;
    if (!PathTracingResolvePreviousDonorPixel(
            pixel,
            currentWorldPosition,
            currentSurfaceValid,
            previousCenter,
            reprojected))
    {
        return currentIndirectSuffix;
    }

    // Choose the previous-frame neighbor independently of every radiance and
    // target before any replay candidate is evaluated. Camera motion centers
    // both donors on the reprojected prior pixel; replay itself always starts
    // from the current pixel and therefore retains no prior radiance.
    const int2 neighbor = PathTracingPreviousNeighbor(
        uint2(previousCenter),
        currentSeed,
        0x50544e42u);
    PathTracingRandomStream reservoirStream =
        PathTracingCreateRandomStream(currentSeed, 0x50545253u);
    PathTracingContributionReservoir reservoir =
        (PathTracingContributionReservoir)0;
    PathTracingContributionReservoirUpdate(
        reservoir,
        currentIndirectSuffix,
        PathTracingRandom(reservoirStream));

    const float4 temporalStatistics =
        t_PreviousPathSeedStatistics[previousCenter];
    if (PathTracingStoredSeedIsLocal(temporalStatistics))
    {
        PathTracingReplaySeedCandidate(
            pixel,
            t_PreviousPathSeed[previousCenter],
            reservoirStream,
            reservoir);
    }

    if (any(neighbor != previousCenter))
    {
        const float4 neighborStatistics =
            t_PreviousPathSeedStatistics[neighbor];
        if (PathTracingStoredSeedIsLocal(neighborStatistics))
        {
            PathTracingReplaySeedCandidate(
                pixel,
                t_PreviousPathSeed[neighbor],
                reservoirStream,
                reservoir);
        }
    }
    return PathTracingContributionReservoirEstimate(reservoir);
}
#endif

float3 PathTracingApplyFireflyFilter(float3 value)
{
    value = max(value, 0.0f);
    if (!PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_FILTER_FIREFLIES))
    {
        return value;
    }
    const float luminance = PathTracingLuminance(value);
    return luminance > g_PathTracing.fireflyThreshold
        ? value * (g_PathTracing.fireflyThreshold / luminance)
        : value;
}

float PathTracingFireflyScale(float3 value)
{
    if (!PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_FILTER_FIREFLIES))
    {
        return 1.0f;
    }
    const float luminance = PathTracingLuminance(max(value, 0.0f));
    return luminance > g_PathTracing.fireflyThreshold
        ? g_PathTracing.fireflyThreshold / luminance
        : 1.0f;
}

float3 PathTracingDebugColor(
    float3 rawMean,
    uint successfulSampleCount,
    float updateRate,
    PathTracingSampleResult sample,
    float3 solvedIndirectSuffix,
    float fireflyScale)
{
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_ALBEDO)
        return sample.albedo;
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_GEOMETRIC_NORMAL)
        return sample.geometricNormal * 0.5f + 0.5f;
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_SHADING_NORMAL)
        return sample.shadingNormal * 0.5f + 0.5f;
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_SAMPLE_COUNT)
    {
        const float value = saturate(
            log2(float(successfulSampleCount) + 1.0f) / 16.0f);
        return value.xxx;
    }
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_UPDATE_RATE)
    {
        return float3(
            updateRate,
            updateRate * updateRate,
            0.0f);
    }
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_STABLE_PLANE)
        return sample.stablePlane.rgb;
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_DIRECT_RESERVOIR)
    {
#if UVSR_PT_RTXDI
        const float4 directReservoir =
            PathTracingStoreReservoir(sample.directReservoir);
        const float selected = g_PathTracing.lightCount > 0u
            ? directReservoir.x / float(g_PathTracing.lightCount)
            : 0.0f;
        return float3(
            selected,
            saturate(log2(directReservoir.y + 1.0f) / 16.0f),
            saturate(directReservoir.w / 64.0f));
#else
        return 0.0f;
#endif
    }
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_GI_RESERVOIR)
        return PathTracingApplyFireflyFilter(solvedIndirectSuffix);
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_PRIMARY_TRANSPORT)
        return max(sample.primaryBase, 0.0f) * fireflyScale;
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_INDIRECT_TRANSPORT)
        return max(solvedIndirectSuffix, 0.0f) * fireflyScale;
    return PathTracingApplyFireflyFilter(rawMean);
}

uint PathTracingAccumulationCycleModulo(uint interval)
{
    if (interval <= 1u)
        return 0u;
    const uint twoTo32Modulo =
        ((0xffffffffu % interval) + 1u) % interval;
    const uint highContribution =
        (g_PathTracing.schedulingSerialHigh % interval) *
            twoTo32Modulo;
    return (
        (g_PathTracing.schedulingSerialLow % interval) +
        highContribution) % interval;
}

void PathTracingWriteDisplay(uint2 pixel, float4 color)
{
    u_Display[pixel] = color;
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchPixel : SV_DispatchThreadID)
{
    if (PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REPLICATE_PREVIEW))
    {
        if (any(dispatchPixel >= g_PathTracing.dispatchExtent))
            return;
        // Reset always restarts at phase zero. Read the representative traced
        // by the preceding sparse dispatch and initialize only presentation;
        // no radiance, count, variance, or proposal history is fabricated.
        const uint2 source =
            (dispatchPixel / g_PathTracing.schedulingGrid) *
                g_PathTracing.schedulingGrid;
        if (all(dispatchPixel == source))
            return;
        u_Display[dispatchPixel] = u_Display[source];
        return;
    }

    const uint2 pixel = dispatchPixel * g_PathTracing.schedulingGrid +
        g_PathTracing.schedulingPhase;
    if (any(pixel >= g_PathTracing.dispatchExtent))
        return;

    const bool accumulate = PathTracingFlagIsSet(
        UVSR_PATH_TRACING_FLAG_ACCUMULATE_SAMPLES);
    const bool refreshDebug = PathTracingFlagIsSet(
        UVSR_PATH_TRACING_FLAG_REFRESH_DEBUG);
    uint oldCount = u_SuccessfulSampleCount[pixel];
    float3 oldMean = u_RawMean[pixel].rgb;
    const float4 oldVarianceState = u_ColorVariance[pixel];
    float3 oldVariance = oldVarianceState.rgb;
    uint failedAttemptSalt =
        isfinite(oldVarianceState.a) && oldVarianceState.a >= 0.0f
            ? min((uint)oldVarianceState.a, 0x00ffffffu)
            : 0u;
    if (oldCount > 0u &&
        (!all(isfinite(oldMean)) || !all(isfinite(oldVariance))))
    {
        // Repair a single poisoned pixel locally. The next attempt starts a
        // fresh finite estimator without waiting for a global history reset.
        oldCount = 0u;
        oldMean = 0.0f;
        oldVariance = 0.0f;
        failedAttemptSalt = 0u;
    }
    const uint updateInterval = UvsrSampleUpdateInterval(
        g_PathTracing.accumulationScheduling,
        g_PathTracing.accumulationAveraging,
        g_PathTracing.accumulationEffectiveHistory,
        g_PathTracing.accumulationMinimumSamples,
        g_PathTracing.accumulationTargetRelativeError,
        g_PathTracing.accumulationMinimumUpdateRate,
        oldCount,
        oldMean,
        oldVariance);
    const float updateRate = rcp(float(updateInterval));
    const uint scheduleCycleModulo =
        PathTracingAccumulationCycleModulo(updateInterval);
    const uint updatePhase = updateInterval > 1u
        ? PathTracingHash(
            pixel.x ^ PathTracingHash(pixel.y + 0x9e3779b9u)) %
                updateInterval
        : 0u;
    const bool scheduledUpdate = updateInterval == 1u ||
        (scheduleCycleModulo + updatePhase) % updateInterval == 0u;
    const bool attempt = refreshDebug || !accumulate || oldCount == 0u ||
        scheduledUpdate;

    if (!attempt)
    {
#if UVSR_PT_RTXDI
        if (PathTracingFlagIsSet(UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
            PathTracingCarryDirectProposal(pixel);
#endif
#if UVSR_PT_SOLVER == 2
        if (PathTracingFlagIsSet(
                UVSR_PATH_TRACING_FLAG_REUSE_GI_CHECKPOINT))
        {
            u_GiCheckpointReservoir[pixel] =
                t_PreviousGiCheckpointReservoir[int2(pixel)];
            u_GiCheckpointCount[pixel] =
                t_PreviousGiCheckpointCount[int2(pixel)];
        }
#endif
#if UVSR_PT_SOLVER == 1
        if (PathTracingFlagIsSet(
                UVSR_PATH_TRACING_FLAG_REPLAY_PATH_SEEDS))
            PathTracingCarryReplaySeed(pixel);
#endif
        return;
    }

    // Accumulation consumes the precomputed sequence by this pixel's own
    // successful-sample index. It therefore keeps advancing even when the
    // cosmetic Animate Samples switch is off, and a skipped adaptive frame
    // cannot consume a sequence element.
    const bool animateHistoryReset = PathTracingFlagIsSet(
        UVSR_PATH_TRACING_FLAG_ANIMATE_HISTORY_RESET);
    const uint sampleSequencePhase = accumulate && !animateHistoryReset
        ? oldCount
        : g_PathTracing.sampleSequencePhase;
    const float precomputedNoise = UVSRSamplePrecomputedNoise(
        t_Noise,
        g_PathTracing.noisePattern,
        pixel,
        g_PathTracing.dispatchExtent,
        sampleSequencePhase,
        0x50545243u);
    const uint2 continuationSeed = PathTracingMakeSampleSeed(
        pixel,
        sampleSequencePhase,
        oldCount,
        accumulate ? 0u : g_PathTracing.schedulingSerialLow,
        accumulate ? 0u : g_PathTracing.schedulingSerialHigh,
        accumulate ? failedAttemptSalt : 0u,
        precomputedNoise);
    const PathTracingSampleResult sample =
        PathTracingIntegrate(pixel, continuationSeed, true);
    if (sample.valid == 0u)
    {
        // Keep accepted samples indexed only by successful count, but advance
        // a separate exactly represented retry salt so a rejected stationary
        // attempt cannot replay the same seed forever. Adaptive skips return
        // above without touching this value.
        const uint nextFailedAttemptSalt = accumulate
            ? (failedAttemptSalt < 0x00ffffffu
                ? failedAttemptSalt + 1u
                : 1u)
            : 0u;
        u_ColorVariance[pixel] = float4(
            oldVariance, float(nextFailedAttemptSalt));
        // Preserve the previous compatible reservoir state when numerical
        // rejection prevents a successful sample. CPU ping-pong still
        // advances after this dispatch.
#if UVSR_PT_RTXDI
        if (PathTracingFlagIsSet(UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
            PathTracingCarryDirectProposal(pixel);
#endif
#if UVSR_PT_SOLVER == 2
        if (PathTracingFlagIsSet(
                UVSR_PATH_TRACING_FLAG_REUSE_GI_CHECKPOINT))
        {
            u_GiCheckpointReservoir[pixel] =
                t_PreviousGiCheckpointReservoir[int2(pixel)];
            u_GiCheckpointCount[pixel] =
                t_PreviousGiCheckpointCount[int2(pixel)];
        }
#endif
#if UVSR_PT_SOLVER == 1
        if (PathTracingFlagIsSet(
                UVSR_PATH_TRACING_FLAG_REPLAY_PATH_SEEDS))
            PathTracingCarryReplaySeed(pixel);
#endif
        return;
    }

    float3 solvedIndirectSuffix = sample.indirectSuffix;
#if UVSR_PT_SOLVER == 2
    solvedIndirectSuffix = PathTracingResolveGiCheckpoint(
        pixel,
        continuationSeed,
        sample.indirectSuffix);
#elif UVSR_PT_SOLVER == 1
    solvedIndirectSuffix = PathTracingResolveSeedReplay(
        pixel,
        continuationSeed,
        sample.indirectSuffix,
        sample.primaryWorldPosition,
        sample.surfaceSignature.w > 0.0f);
#endif
    // The local suffix is replaced by the selected/resampled suffix. It is
    // never added a second time beside the reservoir estimate.
    const float3 solverRadiance = max(
        sample.primaryBase + solvedIndirectSuffix,
        0.0f);

    const uint newCount = accumulate
        ? (oldCount == 0xffffffffu ? oldCount : oldCount + 1u)
        : 1u;
    const float meanWeight = accumulate
        ? UvsrSampleMeanWeight(
            g_PathTracing.accumulationAveraging,
            g_PathTracing.accumulationEffectiveHistory,
            oldCount,
            newCount)
        : 1.0f;
    // Firefly handling is a deliberately biased robustness mode. When it is
    // enabled, filter the successful attempt before it enters persistent
    // history so the option has a stable and effective accumulated result.
    const float fireflyScale = PathTracingFireflyScale(solverRadiance);
    const float3 accumulatedSample = solverRadiance * fireflyScale;
    const float3 newMean = oldCount == 0u || !accumulate
        ? accumulatedSample
        : lerp(oldMean, accumulatedSample, meanWeight);
    const float3 newVariance = oldCount == 0u || !accumulate
        ? 0.0f
        : UvsrSampleVarianceUpdate(
            g_PathTracing.accumulationAveraging,
            oldCount,
            newCount,
            oldVariance,
            oldMean,
            accumulatedSample,
            newMean,
            meanWeight);

    u_RawMean[pixel] = float4(newMean, 1.0f);
    u_SuccessfulSampleCount[pixel] = newCount;
    // A successful sample consumes the retry and starts the next successful
    // sequence element without carrying failure state forward.
    u_ColorVariance[pixel] = float4(newVariance, 0.0f);
    if (PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_WRITE_STABLE_SIGNALS))
    {
        // Apply the same biased firefly scalar before every online mean. The
        // resolve derives specular as raw-residual-diffuse, so FP16 storage of
        // the two explicit means cannot break final recomposition.
        const float3 residualSample =
            max(sample.primaryBase, 0.0f) * fireflyScale;
        const float3 diffuseSuffixSample =
            sample.firstContinuationIsDiffuse != 0u
                ? max(solvedIndirectSuffix, 0.0f) * fireflyScale
                : 0.0f;
        const float3 previousResidual = u_ResidualMean[pixel].rgb;
        const float3 previousDiffuse = u_DiffuseSuffixMean[pixel].rgb;
        const float3 residualMean = oldCount == 0u || !accumulate ||
                !all(isfinite(previousResidual))
            ? residualSample
            : lerp(previousResidual, residualSample, meanWeight);
        const float3 diffuseMean = oldCount == 0u || !accumulate ||
                !all(isfinite(previousDiffuse))
            ? diffuseSuffixSample
            : lerp(previousDiffuse, diffuseSuffixSample, meanWeight);
        u_ResidualMean[pixel] = float4(residualMean, 1.0f);
        u_DiffuseSuffixMean[pixel] = float4(diffuseMean, 1.0f);
        if (!accumulate || oldCount == 0u)
        {
            // Initialize once from a deterministic center ray. Later jittered
            // samples update radiance only, so silhouette guides cannot toggle
            // between hit and miss while the accumulated mean converges.
            float4 primaryNormalRoughness;
            float primaryViewZ;
            PathTracingLoadPrimaryResolveGuide(
                pixel,
                primaryNormalRoughness,
                primaryViewZ);
            u_PrimaryNormalRoughness[pixel] = primaryNormalRoughness;
            u_PrimaryViewZ[pixel] = primaryViewZ;
        }
    }
#if UVSR_PT_RTXDI
    if (PathTracingFlagIsSet(UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
    {
        u_DirectReservoir[pixel] =
            PathTracingStoreReservoir(sample.directReservoir);
        u_Surface[pixel] = sample.surfaceSignature;
        u_DirectSampleSeed[pixel] =
            sample.directReservoir.selectedSampleSeed;
    }
#endif
#if UVSR_PT_SOLVER == 2
    if (PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REUSE_GI_CHECKPOINT))
    {
        const float localTarget = max(
            PathTracingLuminance(sample.indirectSuffix),
            0.0f);
        u_GiCheckpointReservoir[pixel] = float4(
            sample.indirectSuffix,
            localTarget);
        // Persist only this frame's local checkpoint, including a finite
        // successful black checkpoint. Combined estimates never feed back.
        u_GiCheckpointCount[pixel] = 1u;
    }
#endif
#if UVSR_PT_SOLVER == 1
    if (PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REPLAY_PATH_SEEDS))
    {
        const float localTarget = max(
            PathTracingLuminance(sample.indirectSuffix),
            0.0f);
        u_PathSeed[pixel] = continuationSeed;
        // {local weight sum, selected target, local M, valid}. The combined
        // reservoir is deliberately never persisted or recursively reused.
        u_PathSeedStatistics[pixel] = float4(
            localTarget,
            localTarget,
            1.0f,
            1.0f);
    }
#endif
    const float3 debugColor = PathTracingDebugColor(
        newMean,
        newCount,
        updateRate,
        sample,
        solvedIndirectSuffix,
        fireflyScale);
    PathTracingWriteDisplay(
        pixel,
        float4(min(max(debugColor, 0.0f), 65504.0f), 1.0f));
}
