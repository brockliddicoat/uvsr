#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "path_tracing_cb.h"

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

#include "noise_sampling.hlsli"
#include "path_tracing_material.hlsli"
#include "path_tracing_sampling.hlsli"

static const uint UVSR_PATH_DEBUG_FINAL = 0u;
static const uint UVSR_PATH_DEBUG_ALBEDO = 1u;
static const uint UVSR_PATH_DEBUG_GEOMETRIC_NORMAL = 2u;
static const uint UVSR_PATH_DEBUG_SHADING_NORMAL = 3u;
static const uint UVSR_PATH_DEBUG_SAMPLE_COUNT = 4u;
static const uint UVSR_PATH_DEBUG_RETRY = 5u;
static const uint UVSR_PATH_DEBUG_STABLE_PLANE = 6u;
static const uint UVSR_PATH_DEBUG_DIRECT_RESERVOIR = 7u;
static const uint UVSR_PATH_DEBUG_GI_RESERVOIR = 8u;

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
        const float4 temporalSurface = t_PreviousSurface[int2(pixel)];
        if (PathTracingSurfaceSignaturesAreCompatible(
                surfaceSignature, temporalSurface))
        {
            const PathTracingReservoir temporal =
                PathTracingLoadReservoir(
                    t_PreviousDirectReservoir[int2(pixel)],
                    t_PreviousDirectSampleSeed[int2(pixel)]);
            const uint temporalLight = uint(max(temporal.selected, 0.0f));
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
            pixel, randomStream.seed, 0x93b21f4du);
        if (any(neighbor != int2(pixel)))
        {
            const float4 spatialSurface = t_PreviousSurface[neighbor];
            if (PathTracingSurfaceSignaturesAreCompatible(
                    surfaceSignature, spatialSurface))
            {
                const PathTracingReservoir spatial =
                    PathTracingLoadReservoir(
                        t_PreviousDirectReservoir[neighbor],
                        t_PreviousDirectSampleSeed[neighbor]);
                const uint spatialLight = uint(max(spatial.selected, 0.0f));
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
    float3 currentIndirectSuffix)
{
    if (!PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REPLAY_PATH_SEEDS))
    {
        return currentIndirectSuffix;
    }

    // Choose the previous-frame neighbor independently of every radiance and
    // target before any replay candidate is evaluated.
    const int2 neighbor = PathTracingPreviousNeighbor(
        pixel,
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
        t_PreviousPathSeedStatistics[int2(pixel)];
    if (PathTracingStoredSeedIsLocal(temporalStatistics))
    {
        PathTracingReplaySeedCandidate(
            pixel,
            t_PreviousPathSeed[int2(pixel)],
            reservoirStream,
            reservoir);
    }

    if (any(neighbor != int2(pixel)))
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
    PathTracingSampleResult sample,
    float3 solvedIndirectSuffix)
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
    if (g_PathTracing.debugView == UVSR_PATH_DEBUG_RETRY)
    {
        const float probability = 1.0f /
            (float(successfulSampleCount) + 1.0f);
        return float3(probability, probability * probability, 0.0f);
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
    return PathTracingApplyFireflyFilter(rawMean);
}

void PathTracingWriteDisplay(uint2 pixel, float4 color)
{
    u_Display[pixel] = color;
    if (!PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REPLICATE_PREVIEW))
    {
        return;
    }

    // A history reset during continuous movement always restarts at lattice
    // phase zero. Replicate that freshly traced representative through its
    // disjoint tile so the current frame remains fully visible; later phases
    // replace these coarse values with exact per-pixel samples after motion.
    [loop]
    for (uint y = 0u; y < g_PathTracing.schedulingGrid.y; ++y)
    {
        [loop]
        for (uint x = 0u; x < g_PathTracing.schedulingGrid.x; ++x)
        {
            const uint2 target = pixel + uint2(x, y);
            if (all(target < g_PathTracing.dispatchExtent))
                u_Display[target] = color;
        }
    }
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchPixel : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchPixel * g_PathTracing.schedulingGrid +
        g_PathTracing.schedulingPhase;
    if (any(pixel >= g_PathTracing.dispatchExtent))
        return;

    const bool accumulate = PathTracingFlagIsSet(
        UVSR_PATH_TRACING_FLAG_ACCUMULATE_SAMPLES);
    const bool refreshDebug = PathTracingFlagIsSet(
        UVSR_PATH_TRACING_FLAG_REFRESH_DEBUG);
    const uint oldCount = u_SuccessfulSampleCount[pixel];
    const float retryProbability = oldCount == 0u
        ? 1.0f
        : 1.0f / (float(oldCount) + 1.0f);
    const bool attempt = refreshDebug || !accumulate || oldCount == 0u ||
        PathTracingRetryVariate(
            pixel,
            oldCount,
            g_PathTracing.schedulingSerialLow,
            g_PathTracing.schedulingSerialHigh) < retryProbability;

    if (!attempt)
    {
#if UVSR_PT_RTXDI
        if (PathTracingFlagIsSet(UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
        {
            u_DirectReservoir[pixel] =
                t_PreviousDirectReservoir[int2(pixel)];
            u_Surface[pixel] = t_PreviousSurface[int2(pixel)];
            u_DirectSampleSeed[pixel] =
                t_PreviousDirectSampleSeed[int2(pixel)];
        }
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
        {
            u_PathSeed[pixel] = t_PreviousPathSeed[int2(pixel)];
            u_PathSeedStatistics[pixel] =
                t_PreviousPathSeedStatistics[int2(pixel)];
        }
#endif
        return;
    }

    const float precomputedNoise = UVSRSamplePrecomputedNoise(
        t_Noise,
        g_PathTracing.noisePattern,
        pixel,
        g_PathTracing.dispatchExtent,
        g_PathTracing.sampleSequencePhase,
        0x50545243u);
    const uint2 continuationSeed = PathTracingMakeSampleSeed(
        pixel,
        g_PathTracing.sampleSequencePhase,
        oldCount,
        g_PathTracing.schedulingSerialLow,
        g_PathTracing.schedulingSerialHigh,
        precomputedNoise);
    const PathTracingSampleResult sample =
        PathTracingIntegrate(pixel, continuationSeed, true);
    if (sample.valid == 0u)
    {
        // Preserve the previous compatible reservoir state when numerical
        // rejection prevents a successful sample. CPU ping-pong still
        // advances after this dispatch.
#if UVSR_PT_RTXDI
        if (PathTracingFlagIsSet(UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
        {
            u_DirectReservoir[pixel] =
                t_PreviousDirectReservoir[int2(pixel)];
            u_Surface[pixel] = t_PreviousSurface[int2(pixel)];
            u_DirectSampleSeed[pixel] =
                t_PreviousDirectSampleSeed[int2(pixel)];
        }
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
        {
            u_PathSeed[pixel] = t_PreviousPathSeed[int2(pixel)];
            u_PathSeedStatistics[pixel] =
                t_PreviousPathSeedStatistics[int2(pixel)];
        }
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
        sample.indirectSuffix);
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
        ? (newCount > oldCount ? 1.0f / float(newCount) : 0.0f)
        : 1.0f;
    const float3 oldMean = u_RawMean[pixel].rgb;
    // Firefly handling is a deliberately biased robustness mode. When it is
    // enabled, filter the successful attempt before it enters persistent
    // history so the option has a stable and effective accumulated result.
    const float fireflyScale = PathTracingFireflyScale(solverRadiance);
    const float3 accumulatedSample = solverRadiance * fireflyScale;
    const float3 newMean = accumulate
        ? lerp(oldMean, accumulatedSample, meanWeight)
        : accumulatedSample;

    u_RawMean[pixel] = float4(newMean, 1.0f);
    u_SuccessfulSampleCount[pixel] = newCount;
#if UVSR_PT_SOLVER == 0
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
        const float3 residualMean = accumulate
            ? lerp(previousResidual, residualSample, meanWeight)
            : residualSample;
        const float3 diffuseMean = accumulate
            ? lerp(previousDiffuse, diffuseSuffixSample, meanWeight)
            : diffuseSuffixSample;
        u_ResidualMean[pixel] = float4(residualMean, 1.0f);
        u_DiffuseSuffixMean[pixel] = float4(diffuseMean, 1.0f);
        // A primary miss writes deterministic invalid guides. The resolve
        // recognizes them and returns this pixel's raw mean without sampling
        // neighboring geometry or sky.
        u_PrimaryNormalRoughness[pixel] =
            sample.primaryNormalRoughness;
        u_PrimaryViewZ[pixel] = sample.primaryViewZ;
    }
#endif
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
        sample,
        solvedIndirectSuffix);
    PathTracingWriteDisplay(
        pixel,
        float4(min(max(debugColor, 0.0f), 65504.0f), 1.0f));
}
