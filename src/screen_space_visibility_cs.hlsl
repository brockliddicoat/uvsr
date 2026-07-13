#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/binding_helpers.hlsli>
#include "pbr.hlsli"
#include "radial_visibility_mask.hlsli"
#include "visibility_estimator_shared.h"
#include "visibility_projection_shared.h"
#include "screen_space_visibility_cb.h"

#ifndef VISIBILITY_ESTIMATOR
#define VISIBILITY_ESTIMATOR 0
#endif
#ifndef ENABLE_AO
#define ENABLE_AO 1
#endif
#ifndef ENABLE_GI
#define ENABLE_GI 1
#endif
#ifndef ENABLE_TRAVERSAL_DEBUG
#define ENABLE_TRAVERSAL_DEBUG 0
#endif
#ifndef ENABLE_BOUNCE_REINJECTION
#define ENABLE_BOUNCE_REINJECTION 0
#endif
#ifndef INITIALIZE_BOUNCE_CUMULATIVE
#define INITIALIZE_BOUNCE_CUMULATIVE 0
#endif
#ifndef ENABLE_BOUNCE_METADATA
#define ENABLE_BOUNCE_METADATA 0
#endif
#ifndef ENABLE_BOUNCE_STATISTICS
#define ENABLE_BOUNCE_STATISTICS 0
#endif
#ifndef STATIC_SLICE_COUNT
#define STATIC_SLICE_COUNT 0
#endif

#define VisibilityEstimator_PaperAngular 0
#define VisibilityEstimator_GTUniform 1

cbuffer c_Visibility : register(b0)
{
    ScreenSpaceVisibilityConstants g_Visibility;
};

Texture2D<float> t_Depth : register(t0);
Texture2D<float4> t_Normals : register(t1);
#if ENABLE_BOUNCE_REINJECTION
Texture2D<float4> t_PreviousBounceFrontier : register(t2);
Texture2D<float> t_DepthHierarchy : register(t3);
Texture2D<float4> t_GBufferDiffuse : register(t4);
Texture2D<float4> t_Emissive : register(t5);
Texture2D<float> t_MaterialAmbientOcclusion : register(t6);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_BounceFrontier : register(u0);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_IndirectDiffuse : register(u1);
#if ENABLE_BOUNCE_STATISTICS
RWBuffer<uint> u_HigherBounceReceiverStatistics : register(u2);
#endif
#else
Texture2D<float4> t_SourceRadiance : register(t2);
Texture2D<float> t_DepthHierarchy : register(t3);
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_AmbientVisibility : register(u0);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_IndirectDiffuse : register(u1);
VK_IMAGE_FORMAT("rgba8") RWTexture2D<float4> u_Debug : register(u2);
#endif

static const float VisibilityPi = 3.14159265358979323846f;
static const float VisibilityHalfPi = 1.57079632679489661923f;
static const float VisibilityEpsilon = 1e-6f;

uint VisibilityHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float VisibilityRandom(uint2 pixel, uint dimension, uint phase)
{
    uint value = pixel.x * 0x9e3779b9u ^ pixel.y * 0x85ebca6bu;
    value ^= dimension * 0xc2b2ae35u ^ phase * 0x27d4eb2fu;
    return float(VisibilityHash(value) & 0x00ffffffu) / 16777216.0f;
}

float VisibilityFastAcos(float value)
{
    // XeGTAO / Lagarde approximation: avoids two native acos operations per
    // radial sample while retaining sufficient precision for 32 mask sectors.
    float x = abs(clamp(value, -1.0f, 1.0f));
    float result = (-0.156583f * x + VisibilityHalfPi) * sqrt(1.0f - x);
    return value >= 0.0f ? result : VisibilityPi - result;
}

float AzimuthTemporalPhase(uint frameIndex)
{
    static const float phases[6] = {
        1.0f / 6.0f, 5.0f / 6.0f, 1.0f / 2.0f,
        2.0f / 3.0f, 1.0f / 3.0f, 0.0f
    };
    return phases[frameIndex % 6u];
}

float RadialTemporalPhase(uint frameIndex)
{
    static const float phases[4] = { 0.0f, 0.5f, 0.25f, 0.75f };
    return phases[(frameIndex / 6u) % 4u];
}

bool IsFiniteFloat3(float3 value)
{
    return all(isfinite(value));
}

float3 SafeNormalize(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    if (!(lengthSquared > VisibilityEpsilon * VisibilityEpsilon) || !isfinite(lengthSquared))
        return fallback;
    return value * rsqrt(lengthSquared);
}

bool IsValidDepth(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Visibility.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

bool ReconstructViewPositionSafe(float2 pixelPosition, float depth, out float3 positionVS)
{
    float4x4 projection = g_Visibility.view.matViewToClip;
    float denominator = depth * projection[2][3] - projection[2][2];
    if (!isfinite(denominator) || abs(denominator) <= VisibilityEpsilon)
    {
        positionVS = 0.0f;
        return false;
    }

    float viewZ = (projection[3][2] - depth * projection[3][3]) / denominator;
    float clipW = viewZ * projection[2][3] + projection[3][3];
    float2 ndc = (pixelPosition - g_Visibility.view.clipToWindowBias) /
        g_Visibility.view.clipToWindowScale;
    positionVS = float3(
        (ndc.x * clipW - viewZ * projection[2][0] - projection[3][0]) /
            projection[0][0],
        (ndc.y * clipW - viewZ * projection[2][1] - projection[3][1]) /
            projection[1][1],
        viewZ);
    return IsFiniteFloat3(positionVS);
}

bool ReconstructViewPositionFromLinearDepth(
    float2 pixelPosition,
    float linearDepth,
    out float3 positionVS)
{
    if (!(linearDepth > 0.0f) || linearDepth >= 65503.0f ||
        !isfinite(linearDepth) || g_Visibility.orthographicProjection != 0u)
    {
        positionVS = 0.0f;
        return false;
    }

    float4x4 projection = g_Visibility.view.matViewToClip;
    float projectionHandedness = projection[2][3] >= 0.0f ? 1.0f : -1.0f;
    float viewZ = linearDepth * projectionHandedness;
    float clipW = viewZ * projection[2][3] + projection[3][3];
    if (!isfinite(clipW) || abs(clipW) <= VisibilityEpsilon)
    {
        positionVS = 0.0f;
        return false;
    }

    float2 ndc = (pixelPosition - g_Visibility.view.clipToWindowBias) /
        g_Visibility.view.clipToWindowScale;
    positionVS = float3(
        (ndc.x * clipW - viewZ * projection[2][0] - projection[3][0]) /
            projection[0][0],
        (ndc.y * clipW - viewZ * projection[2][1] - projection[3][1]) /
            projection[1][1],
        viewZ);
    return IsFiniteFloat3(positionVS);
}

bool ProjectClippedViewEndpoint(
    float4 receiverClipPosition,
    float3 endpointPositionVS,
    out float2 pixelPosition)
{
    float4 endpointClipPosition = mul(
        float4(endpointPositionVS, 1.0f),
        g_Visibility.view.matViewToClip);
    VisibilityProjectionClipResult clipResult =
        ComputeVisibilityProjectionEndpointClip(
            receiverClipPosition.z,
            receiverClipPosition.w,
            endpointClipPosition.z,
            endpointClipPosition.w,
            g_Visibility.reverseDepth != 0u);
    if (clipResult.valid == 0u)
    {
        pixelPosition = 0.0f;
        return false;
    }

    float4 clipPosition = lerp(
        receiverClipPosition,
        endpointClipPosition,
        clipResult.endpointScale);
    float2 ndc = clipPosition.xy / clipPosition.w;
    pixelPosition = ndc * g_Visibility.view.clipToWindowScale +
        g_Visibility.view.clipToWindowBias;
    return all(isfinite(pixelPosition));
}

VisibilityInterval BuildPaperVisibilityInterval(
    float3 frontDirection,
    float3 backDirection,
    float3 viewDirection,
    float samplingSide,
    float projectedNormalAngle,
    out float frontAngle,
    out float backAngle)
{
    frontAngle = VisibilityFastAcos(dot(frontDirection, viewDirection));
    backAngle = VisibilityFastAcos(dot(backDirection, viewDirection));

    float front01 = saturate(
        (samplingSide * -frontAngle - projectedNormalAngle + VisibilityHalfPi) /
        VisibilityPi);
    float back01 = saturate(
        (samplingSide * -backAngle - projectedNormalAngle + VisibilityHalfPi) /
        VisibilityPi);
    return MakeVisibilityInterval(min(front01, back01), max(front01, back01));
}

void WriteEmptyVisibilityOutput(
    uint2 pixel,
    bool traversalDebugActive,
    float4 previousFrontier)
{
#if ENABLE_BOUNCE_REINJECTION
    u_BounceFrontier[pixel] = 0.0f;
#if INITIALIZE_BOUNCE_CUMULATIVE
    // Bounce two initializes C2 from B1 even when this receiver cannot launch
    // another path. Later bounces leave the existing cumulative value intact.
    u_IndirectDiffuse[pixel] = previousFrontier;
#endif
#else
#if ENABLE_AO
    u_AmbientVisibility[pixel] = 1.0f;
#endif
#if ENABLE_GI
    u_IndirectDiffuse[pixel] = 0.0f;
#endif
#if ENABLE_TRAVERSAL_DEBUG
    if (traversalDebugActive)
        u_Debug[pixel] = 0.0f;
#endif
#endif
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchPixel : SV_DispatchThreadID)
{
    if (any(dispatchPixel >= uint2(g_Visibility.samplingResolution)))
        return;

    uint2 receiverPixel = dispatchPixel;
    float receiverDepth = t_Depth[receiverPixel];
    float2 receiverPixelCenter = float2(receiverPixel) + 0.5f;
    float4 previousReceiverFrontier = 0.0f;

    const bool traversalDebugActive = ENABLE_TRAVERSAL_DEBUG != 0;
#if ENABLE_BOUNCE_REINJECTION
    const bool forceBounceActivity =
        g_Visibility.debugMode >= 2u && g_Visibility.debugMode <= 3u;
    LightingContributionGate bounceContributionGate = MakeLightingContributionGate(
        g_Visibility.knownInactiveLightingSources,
        forceBounceActivity ? LightingSource_IndirectDiffuse : 0u,
        g_Visibility.minimumBounceContribution,
        g_Visibility.lightingExposureScale);
    float bounceToFinalUpperBound = forceBounceActivity
        ? 1.0f
        : max(g_Visibility.indirectDiffuseIntensity, 0.0f) * UVSR_INV_PI;
#else
    const uint firstBounceSources =
        LightingSource_Direct | LightingSource_Emissive;
    LightingContributionGate firstBounceContributionGate =
        MakeLightingContributionGate(
            g_Visibility.knownInactiveLightingSources,
            g_Visibility.debugMode != 0u ? firstBounceSources : 0u,
            0.0f,
            1.0f);
#endif

#if ENABLE_GI
#if ENABLE_BOUNCE_REINJECTION
    const bool giSourcePotential = LightingHasPotentialSource(
        bounceContributionGate, LightingSource_IndirectDiffuse);
#else
    const bool giSourcePotential = LightingHasPotentialSource(
        firstBounceContributionGate, firstBounceSources);
#endif
#if !ENABLE_AO && !ENABLE_BOUNCE_REINJECTION
    if (!giSourcePotential)
    {
        WriteEmptyVisibilityOutput(
            dispatchPixel, traversalDebugActive, previousReceiverFrontier);
        return;
    }
#endif
#endif

    if (!IsValidDepth(receiverDepth) || !(g_Visibility.radiusWorld > VisibilityEpsilon))
    {
        // The first-bounce frontier is guaranteed empty at an ineligible
        // depth, so bounce two can initialize its cumulative output from zero
        // without performing a frontier read that cannot carry energy.
        WriteEmptyVisibilityOutput(
            dispatchPixel, traversalDebugActive, previousReceiverFrontier);
        return;
    }

#if ENABLE_BOUNCE_REINJECTION
    // Read the packed receiver transport fact as soon as basic depth/radius
    // eligibility is known. This gate precedes position reconstruction, normal
    // loading/transformation, and slice setup. The same value is passed to
    // every inactive-output path and later resolve, so there is one receiver
    // frontier read per eligible higher-bounce pixel.
    previousReceiverFrontier = t_PreviousBounceFrontier[receiverPixel];
    uint receiverFrontierMetadata = (uint)round(
        max(previousReceiverFrontier.a, 0.0f));
    bool receiverRejectsDiffuseTransport =
        (receiverFrontierMetadata & PbrGiMetadata_DiffuseActive) == 0u;
#if ENABLE_BOUNCE_STATISTICS
    uint waveEligibleReceiverCount = WaveActiveCountBits(true);
    uint waveRejectedReceiverCount = WaveActiveCountBits(
        receiverRejectsDiffuseTransport);
    if (WaveIsFirstLane())
    {
        InterlockedAdd(
            u_HigherBounceReceiverStatistics[0],
            waveEligibleReceiverCount);
        InterlockedAdd(
            u_HigherBounceReceiverStatistics[1],
            waveRejectedReceiverCount);
    }
#endif
    if (receiverRejectsDiffuseTransport)
    {
        WriteEmptyVisibilityOutput(
            dispatchPixel, traversalDebugActive, previousReceiverFrontier);
        return;
    }
#if !ENABLE_AO
    if (!giSourcePotential)
    {
        WriteEmptyVisibilityOutput(
            dispatchPixel, traversalDebugActive, previousReceiverFrontier);
        return;
    }
#endif
#endif

    float3 receiverPositionVS;
    if (!ReconstructViewPositionSafe(receiverPixelCenter, receiverDepth, receiverPositionVS))
    {
        WriteEmptyVisibilityOutput(
            dispatchPixel, traversalDebugActive, previousReceiverFrontier);
        return;
    }

    float3 receiverNormalWS = t_Normals[receiverPixel].xyz;
    float3 receiverNormalVS = mul(float4(receiverNormalWS, 0.0f),
        g_Visibility.view.matWorldToView).xyz;
    if (dot(receiverNormalVS, receiverNormalVS) <= VisibilityEpsilon * VisibilityEpsilon)
    {
        WriteEmptyVisibilityOutput(
            dispatchPixel, traversalDebugActive, previousReceiverFrontier);
        return;
    }
    receiverNormalVS = SafeNormalize(receiverNormalVS, float3(0.0f, 0.0f, -1.0f));

    float3 viewDirection;
    if (g_Visibility.orthographicProjection != 0u)
    {
        float3 incidentWS = g_Visibility.view.cameraDirectionOrPosition.xyz;
        float3 incidentVS = mul(float4(incidentWS, 0.0f),
            g_Visibility.view.matWorldToView).xyz;
        viewDirection = SafeNormalize(-incidentVS, float3(0.0f, 0.0f, -1.0f));
    }
    else
    {
        viewDirection = SafeNormalize(-receiverPositionVS, float3(0.0f, 0.0f, -1.0f));
    }

    // Reuse the receiver's known pixel/depth to form its homogeneous clip
    // point. Row-vector projection stores clip w in matrix column three.
    float4 receiverPositionH = float4(receiverPositionVS, 1.0f);
    float receiverClipW = dot(receiverPositionH, float4(
        g_Visibility.view.matViewToClip[0][3],
        g_Visibility.view.matViewToClip[1][3],
        g_Visibility.view.matViewToClip[2][3],
        g_Visibility.view.matViewToClip[3][3]));
    float2 receiverNdc = (receiverPixelCenter -
        g_Visibility.view.clipToWindowBias) /
        g_Visibility.view.clipToWindowScale;
    float4 receiverClipPosition = float4(
        receiverNdc * receiverClipW,
        receiverDepth * receiverClipW,
        receiverClipW);
    if (!all(isfinite(receiverClipPosition)) ||
        !(receiverClipPosition.w > VisibilityProjectionEpsilon))
    {
        WriteEmptyVisibilityOutput(
            dispatchPixel, traversalDebugActive, previousReceiverFrontier);
        return;
    }

    uint phase = g_Visibility.freezeSamplingPhase != 0u ? 0u : g_Visibility.frameIndex;
    float sliceRotation = frac(VisibilityRandom(receiverPixel, 0u, 0u) +
        AzimuthTemporalPhase(phase));
    float radialRandom = frac(VisibilityRandom(receiverPixel, 1u, 0u) +
        RadialTemporalPhase(phase));
    float ambientVisibilitySum = 0.0f;
    float3 indirectDiffuseSum = 0.0f;
    uint validSampleCount = 0u;
    uint newlyCoveredSectorCount = 0u;
    uint alreadyCoveredSectorCount = 0u;
    uint accumulatedMaskPopulation = 0u;
    uint gtEndpointOrderFailureCount = 0u;
    float frontAngleSum = 0.0f;
    float backAngleSum = 0.0f;
    float thicknessAngleSum = 0.0f;
    float3 debugSliceOrientation = 0.0f;
    float3 debugProjectedNormal = receiverNormalVS;
    float3 debugSourceNormal = 0.0f;

#if STATIC_SLICE_COUNT
    static const uint activeSliceCount = 1u;
    [unroll]
#else
    uint activeSliceCount = max(g_Visibility.sliceCount, 1u);
    [loop]
#endif
    for (uint sliceIndex = 0u; sliceIndex < activeSliceCount; ++sliceIndex)
    {
#if STATIC_SLICE_COUNT
        float slicePhase = sliceRotation;
#else
        float slicePhase = (float(sliceIndex) + sliceRotation) /
            float(activeSliceCount);
#endif
        float sliceAzimuth = slicePhase * VisibilityPi;
        // Both estimators consume the same coherent image-plane slice. Their
        // projected-normal representation and measure remain compile-time
        // contracts rather than a runtime hybrid.
        float3 screenSliceDirection = float3(
            cos(sliceAzimuth), sin(sliceAzimuth), 0.0f);
        float3 slicePlaneNormal = SafeNormalize(
            cross(screenSliceDirection, viewDirection),
            float3(0.0f, 1.0f, 0.0f));
        float3 sliceTangent = SafeNormalize(
            cross(viewDirection, slicePlaneNormal), screenSliceDirection);
        float sectorPhase = VisibilityRandom(
            receiverPixel, 2u + sliceIndex, phase);

        float3 projectedNormal;
#if VISIBILITY_ESTIMATOR == VisibilityEstimator_GTUniform
        SliceMeasure sliceMeasure = BuildSliceMeasure(
            viewDirection, sliceTangent, receiverNormalVS);
        projectedNormal = sliceMeasure.valid != 0u
            ? sliceMeasure.cosGamma * sliceMeasure.V -
                sliceMeasure.sinGamma * sliceMeasure.S
            : viewDirection;
#else
        projectedNormal = receiverNormalVS - slicePlaneNormal *
            dot(receiverNormalVS, slicePlaneNormal);
        float projectedNormalLengthSquared = dot(projectedNormal, projectedNormal);
        float projectedNormalAngle = 0.0f;
        if (projectedNormalLengthSquared > VisibilityEpsilon * VisibilityEpsilon &&
            isfinite(projectedNormalLengthSquared))
        {
            projectedNormal *= rsqrt(projectedNormalLengthSquared);
            float handedSign = dot(projectedNormal, sliceTangent) >= 0.0f ? 1.0f : -1.0f;
            projectedNormalAngle = -handedSign * VisibilityFastAcos(
                dot(projectedNormal, viewDirection));
        }
        else
        {
            projectedNormal = viewDirection;
        }
#endif

        if (traversalDebugActive && sliceIndex == 0u)
        {
            debugSliceOrientation = sliceTangent;
            debugProjectedNormal = projectedNormal;
        }

        float2 sidePixelDirection[2];
        float sideProjectedRadius[2];
        bool sideProjectionValid[2];
        [unroll]
        for (uint projectionSideIndex = 0u; projectionSideIndex < 2u; ++projectionSideIndex)
        {
            float projectionSide = projectionSideIndex == 0u ? -1.0f : 1.0f;
            float2 radiusEndpointPixel;
            // Clip the full-radius endpoint analytically in homogeneous space,
            // then perform one perspective divide. This handles forward and
            // reversed near planes without iterative radius shortening.
            sideProjectionValid[projectionSideIndex] =
                ProjectClippedViewEndpoint(
                    receiverClipPosition,
                    receiverPositionVS + sliceTangent *
                        (projectionSide * g_Visibility.radiusWorld),
                    radiusEndpointPixel);
            float2 projectedRadiusVector = radiusEndpointPixel - receiverPixelCenter;
            sideProjectedRadius[projectionSideIndex] = length(projectedRadiusVector);
            sideProjectionValid[projectionSideIndex] =
                sideProjectionValid[projectionSideIndex] &&
                sideProjectedRadius[projectionSideIndex] >= 0.5f &&
                isfinite(sideProjectedRadius[projectionSideIndex]);
            sidePixelDirection[projectionSideIndex] = sideProjectionValid[projectionSideIndex]
                ? projectedRadiusVector / sideProjectedRadius[projectionSideIndex]
                : 0.0f;
        }

        RadialVisibilityMask visibilityMask = MakeEmptyRadialVisibilityMask();
        float3 sliceIndirectDiffuse = 0.0f;
        bool sideActive[2] = {
            sideProjectionValid[0], sideProjectionValid[1]
        };
        bool hasPreviousSample[2] = { false, false };
        uint2 previousSamplePixel[2] = {
            uint2(0u, 0u), uint2(0u, 0u)
        };

        // sampleCount is total stochastic samples per pixel, not steps per
        // side. Divide it between the two horizons and alternate the odd sample
        // so low counts remain spatially and temporally unbiased.
        uint stepsPerSide = g_Visibility.sampleCount >> 1u;
        uint oddSampleSide = VisibilityHash(
            receiverPixel.x ^ (receiverPixel.y << 16u) ^ phase) & 1u;
        uint maximumStepsOnSide = stepsPerSide + (g_Visibility.sampleCount & 1u);
        float jitter = lerp(0.5f, radialRandom, g_Visibility.radialJitter);
        float inverseStepsOnSide = rcp(float(max(maximumStepsOnSide, 1u)));

        [loop]
        for (uint stepIndex = 0u; stepIndex < maximumStepsOnSide; ++stepIndex)
        {
            float normalizedStep = max(
                (float(stepIndex) + jitter) * inverseStepsOnSide,
                0.5f * inverseStepsOnSide);
            float distributedStep = saturate(normalizedStep);
            if (abs(g_Visibility.stepDistributionExponent - 1.0f) >= 1e-4f)
            {
                distributedStep = abs(g_Visibility.stepDistributionExponent - 2.0f) < 1e-4f
                    ? distributedStep * distributedStep
                    : pow(distributedStep, g_Visibility.stepDistributionExponent);
            }
            [unroll]
            for (uint sideIndex = 0u; sideIndex < 2u; ++sideIndex)
            {
                if (visibilityMask.occludedBits == RadialVisibilityFullMask)
                    break;
                if (!sideActive[sideIndex])
                    continue;
                uint sideStepCount = stepsPerSide +
                    ((g_Visibility.sampleCount & 1u) != 0u && sideIndex == oddSampleSide ? 1u : 0u);
                if (stepIndex >= sideStepCount)
                    continue;

                float samplingSide = sideIndex == 0u ? -1.0f : 1.0f;
                float sampleDistance = distributedStep * sideProjectedRadius[sideIndex];
                // Preserve one unique-pixel opportunity per stochastic step.
                // A constant one-pixel floor collapsed several early quadratic
                // steps onto the same texel and silently reduced thin-surface SPP.
                sampleDistance = max(sampleDistance, float(stepIndex + 1u));
                float2 samplePixelFloat = receiverPixelCenter +
                    sidePixelDirection[sideIndex] * sampleDistance;
                if (any(samplePixelFloat < g_Visibility.view.viewportOrigin) ||
                    any(samplePixelFloat >= g_Visibility.view.viewportOrigin +
                        g_Visibility.fullResolution))
                {
                    sideActive[sideIndex] = false;
                    continue;
                }

                uint2 samplePixel = uint2(samplePixelFloat);
                if (hasPreviousSample[sideIndex] &&
                    all(samplePixel == previousSamplePixel[sideIndex]))
                {
                    continue;
                }
                previousSamplePixel[sideIndex] = samplePixel;
                hasPreviousSample[sideIndex] = true;
                float3 samplePositionVS;
                bool reconstructed = false;
                if (g_Visibility.useDepthHierarchy != 0u &&
                    g_Visibility.orthographicProjection == 0u)
                {
                    uint depthMip = sampleDistance >= 111.4305f ? 4u
                        : sampleDistance >= 55.7152f ? 3u
                        : sampleDistance >= 27.8576f ? 2u
                        : sampleDistance >= 13.9288f ? 1u : 0u;
                    float sampleViewDepth = t_DepthHierarchy.Load(int3(
                        samplePixel >> depthMip, depthMip));
                    if (sampleViewDepth > 0.0f && sampleViewDepth < 65503.0f &&
                        isfinite(sampleViewDepth))
                    {
                        reconstructed = ReconstructViewPositionFromLinearDepth(
                            float2(samplePixel) + 0.5f,
                            sampleViewDepth,
                            samplePositionVS);
                    }
                }
                if (!reconstructed)
                {
                    float sampleDepth = t_Depth[samplePixel];
                    if (!IsValidDepth(sampleDepth) || !ReconstructViewPositionSafe(
                        float2(samplePixel) + 0.5f, sampleDepth, samplePositionVS))
                    {
                        continue;
                    }
                }

                float effectiveThickness = g_Visibility.thicknessWorld;
                if (g_Visibility.distanceScaledThickness != 0u)
                {
                    effectiveThickness *= 1.0f + abs(samplePositionVS.z) *
                        g_Visibility.thicknessDistanceScale;
                }

                float3 frontDelta = samplePositionVS - receiverPositionVS;
                float frontLengthSquared = dot(frontDelta, frontDelta);
                if (!(frontLengthSquared > VisibilityEpsilon * VisibilityEpsilon) ||
                    !isfinite(frontLengthSquared))
                {
                    continue;
                }
                float3 directionToSample = frontDelta * rsqrt(frontLengthSquared);
                float3 backDelta = ComputeBackDelta(
                    receiverPositionVS,
                    samplePositionVS,
                    viewDirection,
                    effectiveThickness,
                    g_Visibility.orthographicProjection != 0u);
                float3 backDirection = SafeNormalize(
                    backDelta, directionToSample);

                float frontAngle = 0.0f;
                float backAngle = 0.0f;
                VisibilityInterval interval;
#if VISIBILITY_ESTIMATOR == VisibilityEstimator_GTUniform
                GtIntervalBuildResult gtInterval = BuildGtIntervalDebug(
                    directionToSample,
                    backDirection,
                    sliceMeasure);
                interval = gtInterval.interval;
#if ENABLE_TRAVERSAL_DEBUG
                gtEndpointOrderFailureCount +=
                    gtInterval.endpointOrderValid == 0u ? 1u : 0u;
                frontAngle = VisibilityFastAcos(
                    dot(directionToSample, viewDirection));
                backAngle = VisibilityFastAcos(
                    dot(backDirection, viewDirection));
#endif
#else
                interval = BuildPaperVisibilityInterval(
                    directionToSample,
                    backDirection,
                    viewDirection,
                    samplingSide,
                    projectedNormalAngle,
                    frontAngle,
                    backAngle);
#endif
                uint candidateBits = g_Visibility.sectorHitCriterion ==
                    SectorHitCriterion_Round
                    ? MakeStochasticSectorRangeMask(interval, sectorPhase)
                    : MakeSectorRangeMask(interval, g_Visibility.sectorHitCriterion);
                if (candidateBits == 0u)
                    continue;

                uint existingBits = visibilityMask.occludedBits;
                uint newlyCoveredBits = AccumulateOccluder(visibilityMask, candidateBits);
                uint newSectorCount = 0u;
#if ENABLE_GI || ENABLE_TRAVERSAL_DEBUG
                newSectorCount = countbits(newlyCoveredBits);
#endif

                if (traversalDebugActive)
                {
                    uint alreadyCovered = countbits(candidateBits & existingBits);
                    ++validSampleCount;
                    newlyCoveredSectorCount += newSectorCount;
                    alreadyCoveredSectorCount += alreadyCovered;
                    frontAngleSum += frontAngle;
                    backAngleSum += backAngle;
                    thicknessAngleSum += abs(backAngle - frontAngle);
                }

                // Geometry reads above are shared by AO and GI. Source normal,
                // radiance, and emissive are fetched only for newly claimed
                // sectors and only when the GI consumer is active.
#if ENABLE_GI
                if (newSectorCount == 0u)
                    continue;

                float receiverCosine = saturate(dot(receiverNormalVS, directionToSample));
                if (!(receiverCosine > 0.0f))
                    continue;

#if ENABLE_BOUNCE_REINJECTION
                // Only the newest transport frontier is eligible for another
                // bounce. Reject its conservative final-image upper bound
                // before fetching material textures; the sum of angular-sector
                // weights is at most one, so all rejected source pieces remain
                // bounded by the configured cutoff at this receiver.
                uint2 previousSamplingPixel = samplePixel;
                float4 previousFrontierSample =
                    t_PreviousBounceFrontier[previousSamplingPixel];
                float3 previousFrontier = max(previousFrontierSample.rgb, 0.0f);
                uint sourceRejection = LightingClassifyContribution(
                    bounceContributionGate,
                    LightingSource_IndirectDiffuse,
                    previousFrontier,
                    bounceToFinalUpperBound);
                if (!LightingShouldEvaluate(sourceRejection))
                    continue;

                float3 sourceBaseColor = max(
                    t_GBufferDiffuse[samplePixel].rgb, 0.0f);
                float sourceMetalness = saturate(t_Emissive[samplePixel].a);
                float sourceMaterialAo = saturate(
                    t_MaterialAmbientOcclusion[samplePixel]);
                float3 sourceDiffuseReflectance = sourceBaseColor *
                    (1.0f - sourceMetalness);
                float3 transportedFrontier = previousFrontier *
                    sourceDiffuseReflectance * sourceMaterialAo;
                sourceRejection = LightingClassifyContribution(
                    bounceContributionGate,
                    LightingSource_IndirectDiffuse,
                    transportedFrontier,
                    bounceToFinalUpperBound);
                if (!LightingShouldEvaluate(sourceRejection))
                    continue;

                float3 sourceRadiance = transportedFrontier * UVSR_INV_PI;
                uint sourceFrontierMetadata = (uint)round(
                    max(previousFrontierSample.a, 0.0f));
                bool sourceIsDoubleSided =
                    (sourceFrontierMetadata &
                        PbrGiMetadata_SurfaceDoubleSided) != 0u;
#else
                if (!giSourcePotential)
                    continue;
                float4 sourceSample = t_SourceRadiance[samplePixel];
                float3 sourceRadiance = max(sourceSample.rgb, 0.0f);
                // First-bounce thresholding is intentionally exact-only. The
                // uniform source gate above already handled scene activity, so
                // avoid the general threshold classifier in this hot loop.
                if (!IsFiniteFloat3(sourceRadiance) ||
                    !any(sourceRadiance > 0.0f))
                    continue;
                uint sourceMetadata = (uint)round(max(sourceSample.a, 0.0f));
                bool sourceIsDoubleSided =
                    (sourceMetadata &
                        PbrGiMetadata_OutgoingDoubleSided) != 0u;
#endif

                float3 sampleNormalWS = t_Normals[samplePixel].xyz;
                float3 sampleNormalVS = mul(float4(sampleNormalWS, 0.0f),
                    g_Visibility.view.matWorldToView).xyz;
                sampleNormalVS = SafeNormalize(sampleNormalVS, 0.0f);
                if (traversalDebugActive)
                    debugSourceNormal = sampleNormalVS;
                float signedSourceCosine = dot(sampleNormalVS, -directionToSample);
                float sourceCosine = sourceIsDoubleSided
                    ? abs(signedSourceCosine)
                    : saturate(signedSourceCosine);
                float3 weightedSource = sourceRadiance * sourceCosine;
                if (!any(weightedSource > 0.0f))
                    continue;

#if VISIBILITY_ESTIMATOR == VisibilityEstimator_GTUniform
                sliceIndirectDiffuse += sourceRadiance *
                    ComputeGtUniformGiSampleWeight(
                        newSectorCount,
                        receiverCosine,
                        sourceCosine);
#else
                float angularCoverage = float(newSectorCount) /
                    float(RadialVisibilitySectorCount);
                sliceIndirectDiffuse += weightedSource * receiverCosine *
                    angularCoverage;
#endif
#endif
            }

            if (visibilityMask.occludedBits == RadialVisibilityFullMask ||
                (!sideActive[0] && !sideActive[1]))
            {
                break;
            }
        }

#if ENABLE_AO
#if VISIBILITY_ESTIMATOR == VisibilityEstimator_GTUniform
        ambientVisibilitySum += ResolveGtUniformAmbientVisibility(visibilityMask);
#else
        ambientVisibilitySum += GetSliceVisibility(visibilityMask);
#endif
#endif
#if ENABLE_GI
        indirectDiffuseSum += sliceIndirectDiffuse;
#endif
        if (traversalDebugActive)
            accumulatedMaskPopulation += CountOccludedSectors(visibilityMask);
    }

#if STATIC_SLICE_COUNT
    static const float inverseSliceCount = 1.0f;
#else
    float inverseSliceCount = 1.0f / float(activeSliceCount);
#endif
    float ambientVisibility = saturate(ambientVisibilitySum * inverseSliceCount);
    // PaperAngular preserves Algorithm 1's pi normalization. GTUniform bits
    // are equal mass under p(omega)=1/(2*pi), retain the explicit receiver
    // cosine, and therefore require 2*pi. These are compile-time formulations,
    // not selectable pieces of one hybrid estimator.
#if VISIBILITY_ESTIMATOR == VisibilityEstimator_GTUniform
    float irradianceNormalization = GetGtUniformIrradianceNormalization();
#else
    float irradianceNormalization = VisibilityPi;
#endif
    float3 indirectDiffuse = max(indirectDiffuseSum *
        (irradianceNormalization * inverseSliceCount), 0.0f);
    if (!IsFiniteFloat3(indirectDiffuse))
        indirectDiffuse = 0.0f;

#if STATIC_SLICE_COUNT
    float maximumSamples = float(g_Visibility.sampleCount);
    float maximumSectors = float(RadialVisibilitySectorCount);
#else
    float maximumSamples = float(activeSliceCount * g_Visibility.sampleCount);
    float maximumSectors = float(activeSliceCount * RadialVisibilitySectorCount);
#endif
    float sampleNormalization = max(maximumSamples, 1.0f);
    float sectorNormalization = max(maximumSectors, 1.0f);
    float angleNormalization = max(float(validSampleCount) * VisibilityPi, VisibilityPi);

    float3 debugValue = 0.0f;
    if (traversalDebugActive && g_Visibility.debugMode == 5u)
        debugValue = receiverNormalVS * 0.5f + 0.5f;
    else if (g_Visibility.debugMode == 6u)
        debugValue = debugSourceNormal * 0.5f + 0.5f;
    else if (g_Visibility.debugMode == 7u)
        debugValue = float(validSampleCount).xxx / sampleNormalization;
    else if (g_Visibility.debugMode == 8u)
        debugValue = float3(
            float(newlyCoveredSectorCount) / sectorNormalization,
            float(alreadyCoveredSectorCount) / max(
                float(newlyCoveredSectorCount + alreadyCoveredSectorCount), 1.0f),
            0.0f);
    else if (g_Visibility.debugMode == 9u)
        debugValue = float(accumulatedMaskPopulation).xxx / sectorNormalization;
    else if (g_Visibility.debugMode == 10u)
        debugValue = debugSliceOrientation * 0.5f + 0.5f;
    else if (g_Visibility.debugMode == 11u)
        debugValue = debugProjectedNormal * 0.5f + 0.5f;
    else if (g_Visibility.debugMode == 12u)
        debugValue = (frontAngleSum / angleNormalization).xxx;
    else if (g_Visibility.debugMode == 13u)
        debugValue = (backAngleSum / angleNormalization).xxx;
    else if (g_Visibility.debugMode == 14u)
        debugValue = (thicknessAngleSum / angleNormalization).xxx;
    else if (g_Visibility.debugMode == 15u)
        debugValue = float3(
            float(gtEndpointOrderFailureCount) /
                max(float(validSampleCount), 1.0f),
            0.0f,
            0.0f);

#if ENABLE_AO
    u_AmbientVisibility[dispatchPixel] = ambientVisibility;
#endif
#if ENABLE_GI
#if ENABLE_BOUNCE_REINJECTION
    float frontierMetadataValue = previousReceiverFrontier.a;
    u_BounceFrontier[dispatchPixel] = float4(
        min(indirectDiffuse, 65504.0f), frontierMetadataValue);
#if INITIALIZE_BOUNCE_CUMULATIVE
    float3 cumulativeIndirectDiffuse = max(
        previousReceiverFrontier.rgb, 0.0f) + indirectDiffuse;
    u_IndirectDiffuse[dispatchPixel] = float4(
        min(cumulativeIndirectDiffuse, 65504.0f), frontierMetadataValue);
#else
    float4 cumulativeSample = u_IndirectDiffuse[dispatchPixel];
    float3 cumulativeIndirectDiffuse = max(cumulativeSample.rgb, 0.0f) +
        indirectDiffuse;
    u_IndirectDiffuse[dispatchPixel] = float4(
        min(cumulativeIndirectDiffuse, 65504.0f), cumulativeSample.a);
#endif
#else
    float receiverFrontierMetadataValue = 0.0f;
#if ENABLE_BOUNCE_METADATA
    uint receiverSourceMetadata = (uint)round(max(
        t_SourceRadiance[receiverPixel].a, 0.0f));
    uint receiverFrontierMetadata = receiverSourceMetadata &
        (PbrGiMetadata_SurfaceDoubleSided |
            PbrGiMetadata_DiffuseActive);
    receiverFrontierMetadataValue = float(receiverFrontierMetadata);
#endif
    u_IndirectDiffuse[dispatchPixel] = float4(
        min(indirectDiffuse, 65504.0f), receiverFrontierMetadataValue);
#endif
#endif
#if ENABLE_TRAVERSAL_DEBUG
    if (traversalDebugActive)
        u_Debug[dispatchPixel] = float4(saturate(debugValue), 1.0f);
#endif
}
