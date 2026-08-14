#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/bindless.h>
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
#endif
#if UVSR_PT_SOLVER == 1
Texture2D<uint2> t_PreviousPathSeed : register(t7);
Texture2D<float4> t_PreviousPathSeedStatistics : register(t8);
#endif
Texture2D<uint> t_PreviousDirectSampleSeed : register(t9);
StructuredBuffer<LightConstants> t_PathTracingLights : register(t13);
StructuredBuffer<InstanceData> t_PathTracingInstances : register(t14);
Texture2D<float4> t_SharedPositionHit : register(t15);
Texture2D<uint2> t_SharedGeometryMaterial : register(t16);
Texture2D<float4> t_SharedNormalAlpha : register(t17);
Texture2D<float4> t_SharedDiffuse : register(t18);
Texture2D<float4> t_SharedSpecular : register(t19);
Texture2D<float4> t_SharedDirectMean : register(t20);
#if UVSR_PT_SOLVER == 2
Texture2D<float4> t_PreviousGiLo : register(t21);
Texture2D<float4> t_PreviousGiNormal : register(t22);
Texture2D<float4> t_PreviousGiReceiver : register(t23);
#endif

RWTexture2D<float4> u_RawMean : register(u0);
RWTexture2D<uint> u_SuccessfulSampleCount : register(u1);
RWTexture2D<float4> u_Display : register(u2);
#if UVSR_PT_RTXDI
RWTexture2D<float4> u_DirectReservoir : register(u3);
RWTexture2D<float4> u_Surface : register(u4);
#endif
#if UVSR_PT_SOLVER == 2
RWTexture2D<float4> u_GiCheckpointReservoir : register(u5);
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
RWTexture2D<float4> u_IndirectMean : register(u15);
#if UVSR_PT_SOLVER == 2
RWTexture2D<float4> u_GiLo : register(u25);
RWTexture2D<float4> u_GiNormal : register(u26);
RWTexture2D<float4> u_GiReceiver : register(u27);
#endif

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

#if UVSR_PT_RTXDI || UVSR_PT_SOLVER == 1 || UVSR_PT_SOLVER == 2
int2 PathTracingPreviousNeighbor(
    uint2 pixel,
    uint2 seed,
    uint domain,
    uint neighborIndex)
{
    // Neighbor identity comes only from seed space, never from a radiance,
    // target, reservoir selection, or prior combined estimate.
    const uint rotation = PathTracingHash(
        seed.x ^ PathTracingHash(seed.y + domain)) & 3u;
    const uint direction = (rotation + neighborIndex) & 3u;
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
    reprojected = g_PathTracing.proposalReprojectionValid != 0u;
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
    if (g_PathTracing.proposalReprojectionValid != 0u)
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
    if (g_PathTracing.proposalReprojectionValid != 0u)
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

#if UVSR_PT_SOLVER == 2
void PathTracingCarryGiProposal(uint2 pixel)
{
    if (g_PathTracing.proposalReprojectionValid != 0u)
    {
        // A rejected camera-motion sample has no current receiver to validate
        // or reproject. Never attach the previous screen-space checkpoint to
        // an unrelated newly exposed surface.
        u_GiCheckpointReservoir[pixel] = 0.0f;
        u_GiLo[pixel] = 0.0f;
        u_GiNormal[pixel] = 0.0f;
        u_GiReceiver[pixel] = 0.0f;
        return;
    }
    u_GiCheckpointReservoir[pixel] =
        t_PreviousGiCheckpointReservoir[int2(pixel)];
    u_GiLo[pixel] = t_PreviousGiLo[int2(pixel)];
    u_GiNormal[pixel] = t_PreviousGiNormal[int2(pixel)];
    u_GiReceiver[pixel] = t_PreviousGiReceiver[int2(pixel)];
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

void PathTracingGenerateSharedCameraRay(
    uint2 pixel,
    out float3 origin,
    out float3 direction)
{
    const float2 windowPixel = g_PathTracing.view.viewportOrigin +
        float2(pixel) + 0.5f;
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

bool PathTracingLoadSharedSurface(
    uint2 pixel,
    out PathTracingSurface surface)
{
    surface = (PathTracingSurface)0;
    const float4 positionHit = t_SharedPositionHit[int2(pixel)];
    const uint2 geometryMaterial =
        t_SharedGeometryMaterial[int2(pixel)];
    const float4 normalAlpha = t_SharedNormalAlpha[int2(pixel)];
    const float4 diffuse = t_SharedDiffuse[int2(pixel)];
    const float4 specular = t_SharedSpecular[int2(pixel)];
    const uint encodedMaterial = geometryMaterial.y;
    if (!(positionHit.w > 0.0f) || encodedMaterial == 0u ||
        !(specular.w > 0.0f) || !all(isfinite(positionHit)) ||
        !all(isfinite(normalAlpha)) || !all(isfinite(diffuse)) ||
        !all(isfinite(specular)))
    {
        return false;
    }

    surface.position = positionHit.xyz;
    surface.previousPosition = positionHit.xyz;
    surface.hitDistance = positionHit.w;
    surface.geometricNormal = PathTracingUnpackUnitVectorHalf(
        geometryMaterial.x);
    surface.shadingNormal = PbrSafeNormalize(
        normalAlpha.xyz,
        surface.geometricNormal);
    surface.materialIndex = encodedMaterial - 1u;
    surface.material.diffuseAlbedo = max(diffuse.rgb, 0.0f);
    surface.preparedMaterial.diffuseColor = max(diffuse.rgb, 0.0f);
    surface.preparedMaterial.specularF0 = saturate(specular.rgb);
    surface.preparedMaterial.alpha = max(normalAlpha.w, UVSR_MIN_ALPHA);
    surface.preparedMaterialValid = 1u;
    return true;
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
                surface.previousPosition,
                true,
                previousCenter,
                reprojected);
        if (previousCenterValid)
        {
            const uint2 spatialSeed = randomStream.seed;
            if (PathTracingFlagIsSet(
                    UVSR_PATH_TRACING_FLAG_TEMPORAL_REUSE))
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
            }

            [loop]
            for (uint neighborIndex = 0u;
                neighborIndex < g_PathTracing.spatialNeighborCount;
                ++neighborIndex)
            {
                const int2 neighbor = PathTracingPreviousNeighbor(
                    uint2(previousCenter),
                    spatialSeed,
                    0x93b21f4du,
                    neighborIndex);
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
    float3 primaryPreviousWorldPosition;
    float3 primaryViewDirection;
    float3 primaryDiffuseColor;
    float3 primarySpecularF0;
    float primaryAlpha;
    float4 stablePlane;
    float4 surfaceSignature;
    float4 primaryNormalRoughness;
    float primaryViewZ;
    // Reconnectable first-secondary-vertex payload for RESTIR GI. Lo excludes
    // the current receiver's first BSDF/pdf factor; unsupported paths remain
    // in the local residual and are never turned into black samples.
    float3 giSecondaryPosition;
    float3 giSecondaryNormal;
    float3 giTailRadiance;
    float giFirstPdf;
    uint giCandidateValid;
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

    if (PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_SHARED_PRIMARY_SURFACE))
    {
        const float4 positionHit = t_SharedPositionHit[int2(pixel)];
        const float4 sharedNormal = t_SharedNormalAlpha[int2(pixel)];
        if (positionHit.w > 0.0f && all(isfinite(positionHit)) &&
            all(isfinite(sharedNormal)))
        {
            normalRoughness = float4(
                PbrSafeNormalize(
                    sharedNormal.xyz,
                    float3(0.0f, 1.0f, 0.0f)),
                saturate(sqrt(max(sharedNormal.w, 0.0f))));
            viewZ = abs(mul(
                float4(positionHit.xyz, 1.0f),
                g_PathTracing.view.matWorldToView).z);
        }
        return;
    }

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
            false,
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
    bool evaluatePrimaryLighting)
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
    const bool sharedPrimary = PathTracingFlagIsSet(
        UVSR_PATH_TRACING_FLAG_SHARED_PRIMARY_SURFACE);
    if (sharedPrimary)
    {
        PathTracingGenerateSharedCameraRay(
            pixel,
            rayOrigin,
            rayDirection);
    }
    else
    {
        PathTracingGenerateCameraRay(
            pixel,
            float2(
                PathTracingRandom(cameraStream),
                PathTracingRandom(cameraStream)),
            rayOrigin,
            rayDirection);
    }
    evaluatePrimaryLighting = evaluatePrimaryLighting && !sharedPrimary;

    float3 throughput = 1.0f;
    float3 firstContinuationWeight = 0.0f;
    float firstContinuationPdf = 0.0f;
    uint stablePlaneIndex = 0u;
    [loop]
    for (uint bounce = 0u; bounce < g_PathTracing.maxBounces; ++bounce)
    {
        PathTracingSurface surface;
        const bool surfaceHit = bounce == 0u && sharedPrimary
            ? PathTracingLoadSharedSurface(pixel, surface)
            : PathTracingTraceSurface(
                t_WorldBvh,
                rayOrigin,
                rayDirection,
                g_PathTracing.rayBias,
                g_PathTracing.maximumRayDistance,
                bounce == 0u &&
                    g_PathTracing.proposalReprojectionValid != 0u,
                surface);
        if (!surfaceHit)
        {
            const bool showPrimaryEnvironment = bounce > 0u ||
                PathTracingFlagIsSet(
                    UVSR_PATH_TRACING_FLAG_SHOW_ENVIRONMENT_BACKGROUND);
            if (showPrimaryEnvironment &&
                (bounce > 0u || evaluatePrimaryLighting))
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
        if (bounce == 1u && firstContinuationPdf > 0.0f)
        {
            const PbrPreparedMaterial secondaryMaterial =
                PathTracingPrepareMaterial(surface);
            // Basic reconnection keeps the cached secondary outgoing radiance.
            // Restrict it to rough, diffuse-dominant tails where changing the
            // outgoing direction is a controlled approximation. Glossy,
            // metallic, and transmissive-looking tails remain local residuals.
            const float diffuseEnergy = PathTracingLuminance(
                secondaryMaterial.diffuseColor);
            const float specularEnergy = PathTracingLuminance(
                secondaryMaterial.specularF0);
            if (secondaryMaterial.alpha >= 0.25f &&
                diffuseEnergy > max(4.0f * specularEnergy, 1.0e-4f))
            {
                result.giSecondaryPosition = surface.position;
                result.giSecondaryNormal = surface.geometricNormal;
                result.giFirstPdf = firstContinuationPdf;
                result.giCandidateValid = 1u;
            }
        }
        if (bounce == 0u)
        {
            result.albedo = max(surface.material.diffuseAlbedo, 0.0f);
            result.geometricNormal = surface.geometricNormal;
            result.shadingNormal = surface.shadingNormal;
            result.primaryWorldPosition = surface.position;
            result.primaryPreviousWorldPosition =
                surface.previousPosition;
            result.primaryViewDirection = viewDirection;
            const PbrPreparedMaterial primaryMaterial =
                PathTracingPrepareMaterial(surface);
            result.primaryDiffuseColor = primaryMaterial.diffuseColor;
            result.primarySpecularF0 = primaryMaterial.specularF0;
            result.primaryAlpha = primaryMaterial.alpha;
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

        if (bounce > 0u || evaluatePrimaryLighting)
        {
            const float3 emissionContribution = throughput *
                max(surface.material.emissiveColor, 0.0f);
            if (bounce == 0u)
                result.primaryBase += emissionContribution;
            else
                result.indirectSuffix += emissionContribution;
        }

        if (g_PathTracing.lightCount > 0u)
        {
            float3 directLighting;
#if UVSR_PT_RTXDI
            if (bounce == 0u)
            {
                directLighting = 0.0f;
                if (evaluatePrimaryLighting)
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
                directLighting = evaluatePrimaryLighting
                    ? PathTracingConventionalDirect(
                        surface,
                        viewDirection,
                        primaryDirectStream)
                    : 0.0f;
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

        // Contributions at the requested final vertex are complete. Sampling
        // another lobe and roulette value cannot produce another traced ray.
        if (bounce + 1u >= g_PathTracing.maxBounces)
            break;

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

        if (bounce == 0u)
        {
            firstContinuationWeight = bsdfSample.weight;
            firstContinuationPdf = bsdfSample.pdf;
            result.firstContinuationIsDiffuse =
                bsdfSample.diffuseBranch != 0u ? 1u : 0u;
        }

        if (bounce == 0u && g_PathTracing.stablePlaneCount > 0u)
        {
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
    if (result.giCandidateValid != 0u)
    {
        const float3 safeWeight = max(
            abs(firstContinuationWeight),
            1.0e-6f);
        result.giTailRadiance = result.indirectSuffix / safeWeight;
        if (!all(isfinite(result.giTailRadiance)) ||
            any(result.giTailRadiance < 0.0f))
        {
            result.giCandidateValid = 0u;
            result.giTailRadiance = 0.0f;
        }
    }
    result.valid = result.valid != 0u &&
        all(isfinite(result.primaryBase)) &&
        all(isfinite(result.indirectSuffix)) ? 1u : 0u;
    return result;
}

#if UVSR_PT_SOLVER == 2
struct PathTracingGiReservoir
{
    float3 secondaryPosition;
    float3 secondaryNormal;
    float3 tailRadiance;
    float selectedTarget;
    float weightSum;
    float representedCount;
    float finalizedWeight;
    uint valid;
};

void PathTracingGiReservoirUpdate(
    inout PathTracingGiReservoir reservoir,
    float3 secondaryPosition,
    float3 secondaryNormal,
    float3 tailRadiance,
    float target,
    float risWeight,
    float representedCount,
    float random)
{
    reservoir.representedCount = min(
        reservoir.representedCount + max(representedCount, 0.0f),
        32.0f);
    if (!(risWeight > 0.0f) || !isfinite(risWeight) ||
        !(target > 0.0f) || !isfinite(target))
    {
        return;
    }
    const float newWeightSum = reservoir.weightSum + risWeight;
    if (!isfinite(newWeightSum))
        return;
    if (reservoir.valid == 0u || random * newWeightSum < risWeight)
    {
        reservoir.secondaryPosition = secondaryPosition;
        reservoir.secondaryNormal = secondaryNormal;
        reservoir.tailRadiance = tailRadiance;
        reservoir.selectedTarget = target;
        reservoir.valid = 1u;
    }
    reservoir.weightSum = newWeightSum;
}

void PathTracingGiReservoirFinalize(
    inout PathTracingGiReservoir reservoir)
{
    reservoir.finalizedWeight = reservoir.valid != 0u &&
            reservoir.representedCount > 0.0f &&
            reservoir.selectedTarget > 0.0f
        ? reservoir.weightSum /
            (reservoir.representedCount * reservoir.selectedTarget)
        : 0.0f;
    if (!isfinite(reservoir.finalizedWeight) ||
        reservoir.finalizedWeight < 0.0f)
    {
        reservoir.finalizedWeight = 0.0f;
        reservoir.valid = 0u;
    }
}

PathTracingSurface PathTracingGiReceiverSurface(
    PathTracingSampleResult sample)
{
    PathTracingSurface surface = (PathTracingSurface)0;
    surface.position = sample.primaryWorldPosition;
    surface.previousPosition = sample.primaryPreviousWorldPosition;
    surface.geometricNormal = sample.geometricNormal;
    surface.shadingNormal = sample.shadingNormal;
    surface.materialIndex = sample.surfaceSignature.w > 0.0f
        ? uint(sample.surfaceSignature.w) - 1u
        : 0u;
    surface.preparedMaterial.diffuseColor = sample.primaryDiffuseColor;
    surface.preparedMaterial.specularF0 = sample.primarySpecularF0;
    surface.preparedMaterial.alpha = sample.primaryAlpha;
    surface.preparedMaterialValid = 1u;
    return surface;
}

float3 PathTracingEvaluateGiTarget(
    PathTracingSampleResult receiver,
    float3 secondaryPosition,
    float3 tailRadiance,
    bool traceVisibility)
{
    const float3 segment = secondaryPosition -
        receiver.primaryWorldPosition;
    const float distanceSquared = dot(segment, segment);
    if (!(distanceSquared > g_PathTracing.rayBias *
            g_PathTracing.rayBias) ||
        !isfinite(distanceSquared) || !all(isfinite(tailRadiance)))
    {
        return 0.0f;
    }
    const float distance = sqrt(distanceSquared);
    const float3 direction = segment / distance;
    if (traceVisibility)
    {
        const float3 origin = PathTracingPrepareRayOrigin(
            receiver.primaryWorldPosition,
            receiver.geometricNormal,
            direction,
            g_PathTracing.rayBias);
        if (PathTracingTraceOcclusion(
                t_WorldBvh,
                origin,
                direction,
                g_PathTracing.rayBias,
                max(distance - 2.0f * g_PathTracing.rayBias,
                    g_PathTracing.rayBias)))
        {
            return 0.0f;
        }
    }
    const PathTracingSurface surface =
        PathTracingGiReceiverSurface(receiver);
    const PbrPreparedSurface preparedSurface =
        PathTracingPrepareSurface(
            surface,
            receiver.primaryViewDirection);
    const PbrBsdfEvaluation bsdf =
        PathTracingEvaluateBsdfPreparedExact(
            surface.preparedMaterial,
            preparedSurface,
            direction);
    const float cosine = max(
        dot(receiver.shadingNormal, direction),
        0.0f);
    // The reconnectable estimator owns only the receiver's diffuse component.
    // Specular transport remains in the local residual and is never shifted.
    const float3 target = max(tailRadiance, 0.0f) *
        max(bsdf.diffuse, 0.0f) * cosine;
    return all(isfinite(target)) ? target : 0.0f;
}

bool PathTracingLoadGiDonor(
    int2 donorPixel,
    out PathTracingGiReservoir donor,
    out float3 donorReceiverPosition,
    out float3 donorReceiverNormal,
    out uint donorMaterial)
{
    donor = (PathTracingGiReservoir)0;
    donorReceiverPosition = 0.0f;
    donorReceiverNormal = 0.0f;
    donorMaterial = 0u;
    const float4 positionWeight =
        t_PreviousGiCheckpointReservoir[donorPixel];
    const float4 lo = t_PreviousGiLo[donorPixel];
    const float4 packedNormals = t_PreviousGiNormal[donorPixel];
    const float4 receiver = t_PreviousGiReceiver[donorPixel];
    if (!(asuint(receiver.w) > 0u) ||
        !all(isfinite(positionWeight)) || !all(isfinite(lo)) ||
        !all(isfinite(packedNormals)) ||
        !all(isfinite(receiver)))
    {
        return false;
    }
    donor.secondaryPosition = positionWeight.xyz;
    donor.finalizedWeight = max(positionWeight.w, 0.0f);
    donor.tailRadiance = max(lo.rgb, 0.0f);
    donor.secondaryNormal = PathTracingDecodeUnitVector(packedNormals.xy);
    donor.representedCount = 1.0f;
    donor.valid = donor.finalizedWeight > 0.0f ? 1u : 0u;
    donorReceiverPosition = receiver.xyz;
    donorReceiverNormal = PathTracingDecodeUnitVector(packedNormals.zw);
    donorMaterial = asuint(receiver.w);
    return true;
}

bool PathTracingGiReceiversAreCompatible(
    PathTracingSampleResult receiver,
    float3 donorPosition,
    float3 donorNormal,
    uint donorMaterial)
{
    const uint receiverMaterial = receiver.surfaceSignature.w > 0.0f
        ? uint(receiver.surfaceSignature.w)
        : 0u;
    if (receiverMaterial == 0u || receiverMaterial != donorMaterial ||
        dot(receiver.geometricNormal, donorNormal) < 0.8f)
    {
        return false;
    }
    const float3 displacement = receiver.primaryWorldPosition -
        donorPosition;
    const float scale = max(length(displacement), 1.0f);
    return abs(dot(displacement, receiver.geometricNormal)) <
        0.1f * scale;
}

float PathTracingGiReconnectionJacobian(
    float3 receiverPosition,
    float3 donorReceiverPosition,
    float3 secondaryPosition,
    float3 secondaryNormal)
{
    const float3 receiverVector = receiverPosition - secondaryPosition;
    const float3 donorVector = donorReceiverPosition - secondaryPosition;
    const float receiverDistanceSquared = dot(
        receiverVector, receiverVector);
    const float donorDistanceSquared = dot(donorVector, donorVector);
    if (!(receiverDistanceSquared > 1.0e-8f) ||
        !(donorDistanceSquared > 1.0e-8f))
    {
        return 0.0f;
    }
    const float receiverCosine = dot(
        secondaryNormal,
        receiverVector * rsqrt(receiverDistanceSquared));
    const float donorCosine = dot(
        secondaryNormal,
        donorVector * rsqrt(donorDistanceSquared));
    if (!(receiverCosine > 1.0e-4f) || !(donorCosine > 1.0e-4f))
        return 0.0f;
    // Cached Lo is a rough diffuse-tail approximation, not a direction-free
    // full path. Limit reconnection to a local outgoing-direction cone and a
    // bounded cosine change so glossy/Fresnel remnants cannot be shifted
    // arbitrarily around x2.
    const float3 receiverDirection = receiverVector *
        rsqrt(receiverDistanceSquared);
    const float3 donorDirection = donorVector *
        rsqrt(donorDistanceSquared);
    const float cosineRatio = receiverCosine / donorCosine;
    if (dot(receiverDirection, donorDirection) < 0.8660254f ||
        cosineRatio < 0.5f || cosineRatio > 2.0f)
    {
        return 0.0f;
    }
    const float denominator = donorCosine * receiverDistanceSquared;
    if (!(denominator > 1.0e-8f))
        return 0.0f;
    const float jacobian = receiverCosine * donorDistanceSquared /
        denominator;
    return isfinite(jacobian) && jacobian >= 0.05f && jacobian <= 20.0f
        ? jacobian
        : 0.0f;
}

void PathTracingCombineGiDonor(
    PathTracingSampleResult receiver,
    PathTracingGiReservoir donor,
    float3 donorReceiverPosition,
    inout PathTracingRandomStream randomStream,
    inout PathTracingGiReservoir combined)
{
    if (donor.valid == 0u)
    {
        combined.representedCount = min(
            combined.representedCount + donor.representedCount,
            32.0f);
        return;
    }
    const float jacobian = PathTracingGiReconnectionJacobian(
        receiver.primaryWorldPosition,
        donorReceiverPosition,
        donor.secondaryPosition,
        donor.secondaryNormal);
    const float3 targetRgb = jacobian > 0.0f
        ? PathTracingEvaluateGiTarget(
            receiver,
            donor.secondaryPosition,
            donor.tailRadiance,
            true)
        : 0.0f;
    const float target = PathTracingLuminance(targetRgb);
    const float risWeight = target * jacobian *
        donor.finalizedWeight * donor.representedCount;
    PathTracingGiReservoirUpdate(
        combined,
        donor.secondaryPosition,
        donor.secondaryNormal,
        donor.tailRadiance,
        target,
        risWeight,
        donor.representedCount,
        PathTracingRandom(randomStream));
}

float3 PathTracingResolveGiCheckpoint(
    uint2 pixel,
    uint2 currentSeed,
    PathTracingSampleResult currentSample,
    bool allowHistoryReuse)
{
    if (!PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REUSE_GI_CHECKPOINT) ||
        !(currentSample.surfaceSignature.w > 0.0f))
    {
        return currentSample.indirectSuffix;
    }
    if (!allowHistoryReuse)
        return currentSample.indirectSuffix;

    PathTracingRandomStream reservoirStream =
        PathTracingCreateRandomStream(currentSeed, 0x47495253u);
    PathTracingGiReservoir reservoir = (PathTracingGiReservoir)0;
    bool localCandidateValid =
        currentSample.giCandidateValid != 0u &&
        currentSample.giFirstPdf > 0.0f;
    float3 localTargetRgb = localCandidateValid
        ? PathTracingEvaluateGiTarget(
            currentSample,
            currentSample.giSecondaryPosition,
            currentSample.giTailRadiance,
            false)
        : 0.0f;
    float localTarget = PathTracingLuminance(localTargetRgb);
    float localWeight = localCandidateValid
        ? localTarget / currentSample.giFirstPdf
        : 0.0f;
    float3 localContribution = localCandidateValid
        ? localTargetRgb / currentSample.giFirstPdf
        : 0.0f;
    if (!isfinite(localWeight) || localWeight < 0.0f ||
        !all(isfinite(localContribution)) ||
        any(localContribution < 0.0f))
    {
        localCandidateValid = false;
        localTargetRgb = 0.0f;
        localTarget = 0.0f;
        localWeight = 0.0f;
        localContribution = 0.0f;
    }
    PathTracingGiReservoirUpdate(
        reservoir,
        localCandidateValid ? currentSample.giSecondaryPosition : 0.0f,
        localCandidateValid ? currentSample.giSecondaryNormal : 0.0f,
        localCandidateValid ? currentSample.giTailRadiance : 0.0f,
        localTarget,
        localWeight,
        1.0f,
        PathTracingRandom(reservoirStream));
    const float3 localResidual = max(
        currentSample.indirectSuffix - localContribution,
        0.0f);

    int2 previousCenter;
    bool reprojected;
    if (!PathTracingResolvePreviousDonorPixel(
            pixel,
            currentSample.primaryPreviousWorldPosition,
            currentSample.surfaceSignature.w > 0.0f,
            previousCenter,
            reprojected))
    {
        return currentSample.indirectSuffix;
    }

    if (PathTracingFlagIsSet(UVSR_PATH_TRACING_FLAG_TEMPORAL_REUSE))
    {
        PathTracingGiReservoir donor;
        float3 donorReceiverPosition;
        float3 donorReceiverNormal;
        uint donorMaterial;
        if (PathTracingLoadGiDonor(
                previousCenter,
                donor,
                donorReceiverPosition,
                donorReceiverNormal,
                donorMaterial) &&
            PathTracingGiReceiversAreCompatible(
                currentSample,
                donorReceiverPosition,
                donorReceiverNormal,
                donorMaterial))
        {
            PathTracingCombineGiDonor(
                currentSample,
                donor,
                donorReceiverPosition,
                reservoirStream,
                reservoir);
        }
    }

    [loop]
    for (uint neighborIndex = 0u;
        neighborIndex < g_PathTracing.spatialNeighborCount;
        ++neighborIndex)
    {
        const int2 neighbor = PathTracingPreviousNeighbor(
            uint2(previousCenter),
            currentSeed,
            0x47494e42u,
            neighborIndex);
        if (all(neighbor == previousCenter))
            continue;
        PathTracingGiReservoir donor;
        float3 donorReceiverPosition;
        float3 donorReceiverNormal;
        uint donorMaterial;
        if (PathTracingLoadGiDonor(
                neighbor,
                donor,
                donorReceiverPosition,
                donorReceiverNormal,
                donorMaterial) &&
            PathTracingGiReceiversAreCompatible(
                currentSample,
                donorReceiverPosition,
                donorReceiverNormal,
                donorMaterial))
        {
            PathTracingCombineGiDonor(
                currentSample,
                donor,
                donorReceiverPosition,
                reservoirStream,
                reservoir);
        }
    }

    if (reservoir.representedCount <= 1.0f)
        return currentSample.indirectSuffix;
    PathTracingGiReservoirFinalize(reservoir);
    if (reservoir.valid == 0u)
        return currentSample.indirectSuffix;
    const float3 selectedTarget = reservoir.valid != 0u
        ? PathTracingEvaluateGiTarget(
            currentSample,
            reservoir.secondaryPosition,
            reservoir.tailRadiance,
            false)
        : 0.0f;
    const float3 estimate = selectedTarget * reservoir.finalizedWeight;
    const float3 resolved = localResidual + estimate;
    return all(isfinite(resolved)) ? resolved : currentSample.indirectSuffix;
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

PathTracingContributionReservoir PathTracingBuildSeedReplayDonors(
    uint2 pixel,
    uint2 currentSeed,
    float3 currentWorldPosition,
    bool currentSurfaceValid)
{
    PathTracingContributionReservoir donors =
        (PathTracingContributionReservoir)0;
    if (!PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REPLAY_PATH_SEEDS))
    {
        return donors;
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
        return donors;
    }

    PathTracingRandomStream reservoirStream =
        PathTracingCreateRandomStream(currentSeed, 0x50545253u);
    if (PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_TEMPORAL_REUSE))
    {
        const float4 temporalStatistics =
            t_PreviousPathSeedStatistics[previousCenter];
        if (PathTracingStoredSeedIsLocal(temporalStatistics))
        {
            PathTracingReplaySeedCandidate(
                pixel,
                t_PreviousPathSeed[previousCenter],
                reservoirStream,
                donors);
        }
    }

    // Camera motion centers every spatial donor on the reprojected prior
    // pixel. Replay always starts from the current pixel and therefore retains
    // no prior radiance. A rotated cardinal order makes 1..4 controls bounded
    // and avoids repeating an interior donor.
    [loop]
    for (uint neighborIndex = 0u;
        neighborIndex < g_PathTracing.spatialNeighborCount;
        ++neighborIndex)
    {
        const int2 neighbor = PathTracingPreviousNeighbor(
            uint2(previousCenter),
            currentSeed,
            0x50544e42u,
            neighborIndex);
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
                    donors);
            }
        }
    }
    return donors;
}

float3 PathTracingResolveSeedReplay(
    uint2 currentSeed,
    float3 currentIndirectSuffix,
    PathTracingContributionReservoir replayDonors)
{
    PathTracingRandomStream reservoirStream =
        PathTracingCreateRandomStream(currentSeed, 0x50544355u);
    PathTracingContributionReservoir combined = replayDonors;
    PathTracingContributionReservoirUpdate(
        combined,
        currentIndirectSuffix,
        PathTracingRandom(reservoirStream));
    return PathTracingContributionReservoirEstimate(combined);
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
    float3 sharedPrimaryMean,
    float3 sharedPrimaryDebug,
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
        if (PathTracingFlagIsSet(
                UVSR_PATH_TRACING_FLAG_SHARED_PRIMARY_SURFACE))
        {
            return sharedPrimaryDebug;
        }
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
        return PathTracingFlagIsSet(
                UVSR_PATH_TRACING_FLAG_SHARED_PRIMARY_SURFACE)
            ? max(sharedPrimaryMean, 0.0f)
            : max(sample.primaryBase, 0.0f) * fireflyScale;
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
            UVSR_PATH_TRACING_FLAG_RECONSTRUCT_PREVIEW))
    {
        if (any(dispatchPixel >= g_PathTracing.dispatchExtent))
            return;
        // Reset always restarts at phase zero. Reconstruct presentation from
        // the four surrounding representatives traced by the preceding sparse
        // dispatch. This replaces conspicuous nearest-tile blocks with a
        // bounded smooth preview; estimator means, counts, variances, and
        // proposal history remain untouched until their pixels are traced.
        const uint2 grid = max(g_PathTracing.schedulingGrid, 1u);
        const uint2 maximumSource =
            ((g_PathTracing.dispatchExtent - 1u) / grid) * grid;
        const uint2 source00 = min((dispatchPixel / grid) * grid,
            maximumSource);
        // Every source representative must remain read-only for the duration
        // of this dispatch. UAV barriers order dispatches, not threads within
        // one dispatch, so rewriting a source here would race neighboring
        // reconstruction reads.
        if (all(dispatchPixel == source00))
            return;
        const uint2 source11 = min(source00 + grid, maximumSource);
        const uint2 source10 = uint2(source11.x, source00.y);
        const uint2 source01 = uint2(source00.x, source11.y);
        const float2 sourceSpan = max(
            float2(source11 - source00),
            1.0f);
        const float2 blend = saturate(
            float2(dispatchPixel - source00) / sourceSpan);
        const float4 row0 = lerp(
            u_Display[source00],
            u_Display[source10],
            blend.x);
        const float4 row1 = lerp(
            u_Display[source01],
            u_Display[source11],
            blend.x);
        u_Display[dispatchPixel] = lerp(row0, row1, blend.y);
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
    const bool sharedPrimary = PathTracingFlagIsSet(
        UVSR_PATH_TRACING_FLAG_SHARED_PRIMARY_SURFACE);
    uint oldCount = u_SuccessfulSampleCount[pixel];
    float3 oldMean = sharedPrimary
        ? u_IndirectMean[pixel].rgb
        : u_RawMean[pixel].rgb;
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
        if (!sharedPrimary && PathTracingFlagIsSet(
                UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
            PathTracingCarryDirectProposal(pixel);
#endif
#if UVSR_PT_SOLVER == 2
        if (PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REUSE_GI_CHECKPOINT))
            PathTracingCarryGiProposal(pixel);
#endif
#if UVSR_PT_SOLVER == 1
        if (PathTracingFlagIsSet(
                UVSR_PATH_TRACING_FLAG_REPLAY_PATH_SEEDS))
            PathTracingCarryReplaySeed(pixel);
#endif
        return;
    }

    // A frame may contain multiple fresh paths. Persistent accumulation uses
    // the pixel's successful-sample count; a non-accumulating frame uses a
    // compact cumulative batch so Samples is a true per-frame mean.
    const bool animateHistoryReset = PathTracingFlagIsSet(
        UVSR_PATH_TRACING_FLAG_ANIMATE_HISTORY_RESET);
    uint runningCount = accumulate ? oldCount : 0u;
    float3 runningMean = accumulate ? oldMean : 0.0f;
    float3 runningVariance = accumulate ? oldVariance : 0.0f;
    float3 runningResidual = accumulate && oldCount > 0u
        ? u_ResidualMean[pixel].rgb
        : 0.0f;
    float3 runningDiffuse = accumulate && oldCount > 0u
        ? u_DiffuseSuffixMean[pixel].rgb
        : 0.0f;
    if (!all(isfinite(runningResidual)))
        runningResidual = 0.0f;
    if (!all(isfinite(runningDiffuse)))
        runningDiffuse = 0.0f;

    uint successfulBatchCount = 0u;
    uint nextFailedAttemptSalt = failedAttemptSalt;
    PathTracingSampleResult lastSample = (PathTracingSampleResult)0;
#if UVSR_PT_SOLVER == 2
    PathTracingSampleResult giHistorySample =
        (PathTracingSampleResult)0;
    bool giHistorySampleValid = false;
#elif UVSR_PT_SOLVER == 1
    PathTracingContributionReservoir replayDonors =
        (PathTracingContributionReservoir)0;
    bool replayDonorsPrepared = false;
#endif
    float3 lastSolvedIndirectSuffix = 0.0f;
    float lastFireflyScale = 1.0f;
    uint2 lastContinuationSeed = 0u;

    [loop]
    for (uint sampleIndex = 0u;
        sampleIndex < g_PathTracing.samplesPerPixel;
        ++sampleIndex)
    {
        const uint sampleSequencePhase = accumulate && !animateHistoryReset
            ? runningCount
            : g_PathTracing.sampleSequencePhase *
                g_PathTracing.samplesPerPixel + sampleIndex;
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
            runningCount,
            accumulate ? 0u : g_PathTracing.schedulingSerialLow,
            accumulate ? 0u : g_PathTracing.schedulingSerialHigh,
            accumulate ? nextFailedAttemptSalt : 0u,
            precomputedNoise);
        const PathTracingSampleResult sample =
            PathTracingIntegrate(pixel, continuationSeed, true);
        if (sample.valid == 0u)
        {
            // Failed samples do not consume the accepted sequence. A bounded
            // exact salt prevents a stationary retry from repeating forever.
            nextFailedAttemptSalt = accumulate
                ? (nextFailedAttemptSalt < 0x00ffffffu
                    ? nextFailedAttemptSalt + 1u
                    : 1u)
                : 0u;
            continue;
        }

        float3 solvedIndirectSuffix = sample.indirectSuffix;
#if UVSR_PT_SOLVER == 2
        solvedIndirectSuffix = PathTracingResolveGiCheckpoint(
            pixel,
            continuationSeed,
            sample,
            successfulBatchCount == 0u);
#elif UVSR_PT_SOLVER == 1
        // Fix one validated donor set from the first successful receiver in
        // this pixel/frame, trace it once, then combine its arithmetic mean
        // with each fresh sample. Invalid attempts do not consume or prepare
        // the donor set. This preserves the prior per-sample mean weighting
        // while removing repeated identical full-path replays.
        if (!replayDonorsPrepared)
        {
            replayDonors = PathTracingBuildSeedReplayDonors(
                pixel,
                continuationSeed,
                sample.primaryPreviousWorldPosition,
                sample.surfaceSignature.w > 0.0f);
            replayDonorsPrepared = true;
        }
        solvedIndirectSuffix = PathTracingResolveSeedReplay(
            continuationSeed,
            sample.indirectSuffix,
            replayDonors);
#endif
        // The local suffix is replaced by the selected/resampled suffix. It is
        // never added a second time beside the reservoir estimate.
        const float3 solverRadiance = max(
            sharedPrimary
                ? solvedIndirectSuffix
                : sample.primaryBase + solvedIndirectSuffix,
            0.0f);
        const float fireflyScale = PathTracingFireflyScale(solverRadiance);
        const float3 accumulatedSample = solverRadiance * fireflyScale;
        const uint previousCount = runningCount;
        const uint newCount = previousCount == 0xffffffffu
            ? previousCount
            : previousCount + 1u;
        const uint averaging = accumulate
            ? g_PathTracing.accumulationAveraging
            : UVSR_SAMPLE_AVERAGING_CUMULATIVE;
        const float meanWeight = previousCount == 0u
            ? 1.0f
            : UvsrSampleMeanWeight(
                averaging,
                g_PathTracing.accumulationEffectiveHistory,
                previousCount,
                newCount);
        const float3 newMean = previousCount == 0u
            ? accumulatedSample
            : lerp(runningMean, accumulatedSample, meanWeight);
        const float3 newVariance = previousCount == 0u
            ? 0.0f
            : UvsrSampleVarianceUpdate(
                averaging,
                previousCount,
                newCount,
                runningVariance,
                runningMean,
                accumulatedSample,
                newMean,
                meanWeight);

        if (PathTracingFlagIsSet(
                UVSR_PATH_TRACING_FLAG_WRITE_STABLE_SIGNALS))
        {
            // Use identical online weights for every signal group so final
            // recomposition remains coherent with the per-frame path batch.
            const float3 residualSample =
                max(sample.primaryBase, 0.0f) * fireflyScale;
            const float3 diffuseSuffixSample =
                sample.firstContinuationIsDiffuse != 0u
                    ? max(solvedIndirectSuffix, 0.0f) * fireflyScale
                    : 0.0f;
            runningResidual = previousCount == 0u
                ? residualSample
                : lerp(runningResidual, residualSample, meanWeight);
            runningDiffuse = previousCount == 0u
                ? diffuseSuffixSample
                : lerp(runningDiffuse, diffuseSuffixSample, meanWeight);
        }

        runningCount = newCount;
        runningMean = newMean;
        runningVariance = newVariance;
        ++successfulBatchCount;
        nextFailedAttemptSalt = 0u;
        lastSample = sample;
        lastSolvedIndirectSuffix = solvedIndirectSuffix;
        lastFireflyScale = fireflyScale;
        lastContinuationSeed = continuationSeed;
#if UVSR_PT_SOLVER == 2
        if (!giHistorySampleValid)
        {
            giHistorySample = sample;
            giHistorySampleValid = true;
        }
#endif
    }

    if (successfulBatchCount == 0u)
    {
        u_ColorVariance[pixel] = float4(
            oldVariance, float(nextFailedAttemptSalt));
        // Preserve previous compatible proposal state when every fresh path is
        // rejected numerically. CPU ping-pong still advances after dispatch.
#if UVSR_PT_RTXDI
        if (!sharedPrimary && PathTracingFlagIsSet(
                UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
            PathTracingCarryDirectProposal(pixel);
#endif
#if UVSR_PT_SOLVER == 2
        if (PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REUSE_GI_CHECKPOINT))
            PathTracingCarryGiProposal(pixel);
#endif
#if UVSR_PT_SOLVER == 1
        if (PathTracingFlagIsSet(
                UVSR_PATH_TRACING_FLAG_REPLAY_PATH_SEEDS))
            PathTracingCarryReplaySeed(pixel);
#endif
        return;
    }

    const float3 rawMean = sharedPrimary
        ? max(t_SharedDirectMean[int2(pixel)].rgb, 0.0f) + runningMean
        : runningMean;
    if (sharedPrimary)
        u_IndirectMean[pixel] = float4(runningMean, 1.0f);
    u_RawMean[pixel] = float4(rawMean, 1.0f);
    u_SuccessfulSampleCount[pixel] = runningCount;
    u_ColorVariance[pixel] = float4(
        runningVariance, float(nextFailedAttemptSalt));
    if (PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_WRITE_STABLE_SIGNALS))
    {
        if (sharedPrimary)
            runningResidual = max(
                t_SharedDirectMean[int2(pixel)].rgb,
                0.0f);
        u_ResidualMean[pixel] = float4(runningResidual, 1.0f);
        u_DiffuseSuffixMean[pixel] = float4(runningDiffuse, 1.0f);
        if (!accumulate || oldCount == 0u)
        {
            // Guides represent the deterministic pixel center, not one of the
            // stochastic paths in this frame's batch.
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
    if (!sharedPrimary && PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REUSE_DIRECT))
    {
        u_DirectReservoir[pixel] =
            PathTracingStoreReservoir(lastSample.directReservoir);
        u_Surface[pixel] = lastSample.surfaceSignature;
        u_DirectSampleSeed[pixel] =
            lastSample.directReservoir.selectedSampleSeed;
    }
#endif
#if UVSR_PT_SOLVER == 2
    if (PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REUSE_GI_CHECKPOINT))
    {
        bool reconnectable = giHistorySampleValid &&
            giHistorySample.giCandidateValid != 0u &&
            giHistorySample.giFirstPdf > 0.0f;
        float localInversePdf = reconnectable
            ? rcp(giHistorySample.giFirstPdf)
            : 0.0f;
        if (!isfinite(localInversePdf) || localInversePdf < 0.0f)
        {
            reconnectable = false;
            localInversePdf = 0.0f;
        }
        const float2 receiverNormal = PathTracingEncodeUnitVector(
            giHistorySample.geometricNormal);
        const float2 secondaryNormal = PathTracingEncodeUnitVector(
            giHistorySample.giSecondaryNormal);
        const uint receiverMaterial =
            giHistorySample.surfaceSignature.w > 0.0f
                ? uint(giHistorySample.surfaceSignature.w)
                : 0u;
        u_GiCheckpointReservoir[pixel] = float4(
            reconnectable ? giHistorySample.giSecondaryPosition : 0.0f,
            localInversePdf);
        u_GiLo[pixel] = float4(
            reconnectable
                ? min(giHistorySample.giTailRadiance, 65504.0f)
                : 0.0f,
            0.0f);
        u_GiNormal[pixel] = float4(
            reconnectable ? secondaryNormal : 0.0f,
            receiverNormal);
        u_GiReceiver[pixel] = float4(
            giHistorySample.primaryWorldPosition,
            asfloat(receiverMaterial));
        // A nonzero receiver material is the exact M=1 marker. Persist only
        // this fresh local trial; combined reservoirs never feed back.
    }
#endif
#if UVSR_PT_SOLVER == 1
    if (PathTracingFlagIsSet(
            UVSR_PATH_TRACING_FLAG_REPLAY_PATH_SEEDS))
    {
        const float localTarget = max(
            PathTracingLuminance(lastSample.indirectSuffix),
            0.0f);
        u_PathSeed[pixel] = lastContinuationSeed;
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
        rawMean,
        sharedPrimary
            ? t_SharedDirectMean[int2(pixel)].rgb
            : 0.0f,
        sharedPrimary
            ? u_Display[pixel].rgb
            : 0.0f,
        runningCount,
        updateRate,
        lastSample,
        lastSolvedIndirectSuffix,
        lastFireflyScale);
    PathTracingWriteDisplay(
        pixel,
        float4(min(max(debugColor, 0.0f), 65504.0f), 1.0f));
}
