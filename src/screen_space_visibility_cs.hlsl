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
#ifndef ENABLE_BOUNCE_REINJECTION
#define ENABLE_BOUNCE_REINJECTION 0
#endif
#ifndef INITIALIZE_BOUNCE_CUMULATIVE
#define INITIALIZE_BOUNCE_CUMULATIVE 0
#endif
#ifndef ENABLE_BOUNCE_METADATA
#define ENABLE_BOUNCE_METADATA 0
#endif
#define VisibilityEstimator_UniformProjectedAngle 0
#define VisibilityEstimator_UniformSolidAngle 1
#define VisibilityEstimator_CosineWeightedSolidAngle 2

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
Texture2DArray<float> t_BlueNoise : register(t7);
Texture2DArray<float> t_FilterAdaptedNoise : register(t8);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_BounceFrontier : register(u0);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_IndirectDiffuse : register(u1);
#else
Texture2D<float4> t_SourceRadiance : register(t2);
Texture2D<float> t_DepthHierarchy : register(t3);
Texture2DArray<float> t_BlueNoise : register(t4);
Texture2DArray<float> t_FilterAdaptedNoise : register(t5);
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_AmbientVisibility : register(u0);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_IndirectDiffuse : register(u1);
#endif

static const float VisibilityPi = 3.14159265358979323846f;
static const float VisibilityHalfPi = 1.57079632679489661923f;
static const float VisibilityEpsilon = 1e-6f;

// Bit i selects radial stratum i. Entry n contains the first n strata in the
// 5-bit reversal sequence, so budgets remain nested while firstbitlow consumes
// the selected set in near-to-far order for correct GI sector ownership.
static const uint ProgressiveRadialPrefixMasks[33] = {
    0x00000000u, 0x00000001u, 0x00010001u, 0x00010101u,
    0x01010101u, 0x01010111u, 0x01110111u, 0x01111111u,
    0x11111111u, 0x11111115u, 0x11151115u, 0x11151515u,
    0x15151515u, 0x15151555u, 0x15551555u, 0x15555555u,
    0x55555555u, 0x55555557u, 0x55575557u, 0x55575757u,
    0x57575757u, 0x57575777u, 0x57775777u, 0x57777777u,
    0x77777777u, 0x7777777fu, 0x777f777fu, 0x777f7f7fu,
    0x7f7f7f7fu, 0x7f7f7fffu, 0x7fff7fffu, 0x7fffffffu,
    0xffffffffu
};

static const uint SchedulerDimension_SliceRotation = 0u;
static const uint SchedulerDimension_SectorPhase = 1u;
// Dimension two was formerly adaptive sample-budget rounding. Keep the
// remaining semantic dimensions stable so removing that feature does not
// silently change the fixed sampler's phase sequence.
static const uint SchedulerDimension_OddSampleSide = 3u;
static const uint SchedulerDimension_RadialNegative = 4u;
static const uint SchedulerDimension_RadialPositive = 5u;

static const uint2 BlueNoiseTemporalSteps[8] = {
    uint2(13u, 29u), uint2(31u, 11u),
    uint2(17u, 27u), uint2(23u, 19u),
    uint2(7u, 25u), uint2(29u, 15u),
    uint2(21u, 31u), uint2(11u, 23u)
};

// The FAST design recommends R2-separated spatial reads when several random
// values are needed in one frame. These integer offsets are the first eight R2
// points quantized to the 64x64 rank-field torus.
static const uint2 FilterAdaptedSemanticOffsets[8] = {
    uint2(0u, 0u), uint2(48u, 36u),
    uint2(32u, 8u), uint2(16u, 45u),
    uint2(1u, 17u), uint2(49u, 54u),
    uint2(33u, 26u), uint2(18u, 63u)
};

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

float SchedulerRandom(uint2 samplingPixel, uint dimension, uint phase)
{
    if (g_Visibility.sampleScheduler == 0u)
        return VisibilityRandom(samplingPixel, dimension, phase);

    if (g_Visibility.sampleScheduler == 2u)
    {
        // The 64x64x32 scalar-uniform volume was optimized offline for a
        // Gaussian spatial filter and alpha=0.35 EMA temporal filter. Every
        // 32-frame cycle receives a global low-discrepancy permutation offset;
        // 2531 is the odd integer nearest 4096 / phi and is coprime to 4096.
        uint frameInVolume = phase & 31u;
        uint cycle = phase >> 5u;
        uint shuffledOffset = (1741u + 2531u * cycle) & 4095u;
        uint2 cycleOffset = uint2(
            shuffledOffset & 63u,
            shuffledOffset >> 6u);
        uint2 coordinate = (samplingPixel + cycleOffset +
            FilterAdaptedSemanticOffsets[dimension & 7u]) & 63u;
        return t_FilterAdaptedNoise.Load(
            int4(coordinate, frameInVolume, 0));
    }

    // Each semantic random dimension owns an independently optimized rank
    // layer. Moving the layer toroidally preserves its spatial spectrum;
    // changing the cycle offset prevents an exact 64-frame repetition.
    uint layer = dimension & 7u;
    uint frameInCycle = phase & 63u;
    uint cycle = phase >> 6u;
    uint cycleHashX = VisibilityHash(
        cycle ^ (dimension * 0x9e3779b9u) ^ 0x68bc21ebu);
    uint cycleHashY = VisibilityHash(
        cycle ^ (dimension * 0x85ebca6bu) ^ 0x02e5be93u);
    uint2 cycleOffset = uint2(cycleHashX, cycleHashY) & 63u;
    uint2 coordinate = (samplingPixel + cycleOffset +
        BlueNoiseTemporalSteps[layer] * frameInCycle) & 63u;
    return t_BlueNoise.Load(int4(coordinate, layer, 0));
}

uint2 SamplingToFullPixel(uint2 samplingPixel)
{
    uint scale = max(g_Visibility.resolutionScale, 1u);
    uint2 fullSize = uint2(g_Visibility.fullResolution);
    return min(samplingPixel * scale + scale / 2u, fullSize - 1u);
}

uint2 FullToSamplingPixel(uint2 fullPixel)
{
    uint scale = max(g_Visibility.resolutionScale, 1u);
    return min(fullPixel / scale,
        uint2(g_Visibility.samplingResolution) - 1u);
}

float ProgressiveRadialSample(uint radialStratum, float rotation)
{
    return (float(radialStratum) + rotation) * (1.0f / 32.0f);
}

uint RotateRadialPrefix(uint mask, uint shift)
{
    shift &= 31u;
    return shift == 0u
        ? mask
        : (mask << shift) | (mask >> (32u - shift));
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

VisibilityInterval BuildProjectedAngleVisibilityInterval(
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
#endif
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchPixel : SV_DispatchThreadID)
{
    if (any(dispatchPixel >= uint2(g_Visibility.samplingResolution)))
        return;

    uint2 receiverPixel = SamplingToFullPixel(dispatchPixel);
    float receiverDepth = t_Depth[receiverPixel];
    float2 receiverPixelCenter = float2(receiverPixel) + 0.5f;
    float4 previousReceiverFrontier = 0.0f;

#if ENABLE_BOUNCE_REINJECTION
    LightingContributionGate bounceContributionGate = MakeLightingContributionGate(
        g_Visibility.knownInactiveLightingSources,
        g_Visibility.minimumBounceContribution,
        g_Visibility.lightingExposureScale);
    float bounceToFinalUpperBound =
        max(g_Visibility.indirectDiffuseIntensity, 0.0f) * UVSR_INV_PI;
#else
    const uint firstBounceSources =
        LightingSource_Direct | LightingSource_Emissive;
    LightingContributionGate firstBounceContributionGate =
        MakeLightingContributionGate(
            g_Visibility.knownInactiveLightingSources,
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
            dispatchPixel, previousReceiverFrontier);
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
            dispatchPixel, previousReceiverFrontier);
        return;
    }

#if ENABLE_BOUNCE_REINJECTION
    // Read the packed receiver transport fact as soon as basic depth/radius
    // eligibility is known. This gate precedes position reconstruction, normal
    // loading/transformation, and slice setup. The same value is passed to
    // every inactive-output path and later resolve, so there is one receiver
    // frontier read per eligible higher-bounce pixel.
    // Bounce frontiers live at the visibility sampling resolution. The
    // dispatch coordinate is therefore the receiver coordinate for this
    // texture even though depth and G-buffer data use receiverPixel.
    previousReceiverFrontier = t_PreviousBounceFrontier[dispatchPixel];
    uint receiverFrontierMetadata = (uint)round(
        max(previousReceiverFrontier.a, 0.0f));
    bool receiverRejectsDiffuseTransport =
        (receiverFrontierMetadata & PbrGiMetadata_DiffuseActive) == 0u;
    if (receiverRejectsDiffuseTransport)
    {
        WriteEmptyVisibilityOutput(
            dispatchPixel, previousReceiverFrontier);
        return;
    }
#if !ENABLE_AO
    if (!giSourcePotential)
    {
        WriteEmptyVisibilityOutput(
            dispatchPixel, previousReceiverFrontier);
        return;
    }
#endif
#endif

    float3 receiverPositionVS;
    if (!ReconstructViewPositionSafe(receiverPixelCenter, receiverDepth, receiverPositionVS))
    {
        WriteEmptyVisibilityOutput(
            dispatchPixel, previousReceiverFrontier);
        return;
    }

    float3 receiverNormalWS = t_Normals[receiverPixel].xyz;
    float3 receiverNormalVS = mul(float4(receiverNormalWS, 0.0f),
        g_Visibility.view.matWorldToView).xyz;
    if (dot(receiverNormalVS, receiverNormalVS) <= VisibilityEpsilon * VisibilityEpsilon)
    {
        WriteEmptyVisibilityOutput(
            dispatchPixel, previousReceiverFrontier);
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
            dispatchPixel, previousReceiverFrontier);
        return;
    }

    uint phase = g_Visibility.frameIndex;
    uint selectedSampleCount = clamp(
        g_Visibility.sampleCount, 1u, 64u);

    float sliceRotation = SchedulerRandom(
        dispatchPixel, SchedulerDimension_SliceRotation, phase);
    float ambientVisibility = 1.0f;
    float3 indirectDiffuse = 0.0f;
    float sliceAzimuth = sliceRotation * VisibilityPi;
    // Every pixel evaluates one coherent stochastic image-plane slice. The
    // estimator-specific measure remains a compile-time contract.
    float3 screenSliceDirection = float3(
        cos(sliceAzimuth), sin(sliceAzimuth), 0.0f);
    float3 slicePlaneNormal = SafeNormalize(
        cross(screenSliceDirection, viewDirection),
        float3(0.0f, 1.0f, 0.0f));
    float3 sliceTangent = SafeNormalize(
        cross(viewDirection, slicePlaneNormal), screenSliceDirection);
    float sectorPhase = SchedulerRandom(
        dispatchPixel, SchedulerDimension_SectorPhase, phase);

#if VISIBILITY_ESTIMATOR != VisibilityEstimator_UniformProjectedAngle
        SliceMeasure sliceMeasure = BuildSliceMeasure(
            viewDirection, sliceTangent, receiverNormalVS);
#else
        float3 projectedNormal = receiverNormalVS - slicePlaneNormal *
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

        uint stepsPerSide = selectedSampleCount >> 1u;
        uint oddSampleSide = SchedulerRandom(
            dispatchPixel,
            SchedulerDimension_OddSampleSide,
            phase) < 0.5f ? 0u : 1u;
        uint sideStepCount[2] = {
            stepsPerSide + (((selectedSampleCount & 1u) != 0u &&
                oddSampleSide == 0u) ? 1u : 0u),
            stepsPerSide + (((selectedSampleCount & 1u) != 0u &&
                oddSampleSide == 1u) ? 1u : 0u)
        };
        float radialSequence[2] = {
            SchedulerRandom(
                dispatchPixel,
                SchedulerDimension_RadialNegative,
                phase),
            SchedulerRandom(
                dispatchPixel,
                SchedulerDimension_RadialPositive,
                phase)
        };
        uint radialShift[2] = {
            min(uint(radialSequence[0] * 32.0f), 31u),
            min(uint(radialSequence[1] * 32.0f), 31u)
        };
        float radialRotation[2] = {
            frac(radialSequence[0] * 32.0f),
            frac(radialSequence[1] * 32.0f)
        };
        uint remainingRadialStrata[2] = {
            sideActive[0]
                ? RotateRadialPrefix(
                    ProgressiveRadialPrefixMasks[
                        min(sideStepCount[0], 32u)],
                    radialShift[0])
                : 0u,
            sideActive[1]
                ? RotateRadialPrefix(
                    ProgressiveRadialPrefixMasks[
                        min(sideStepCount[1], 32u)],
                    radialShift[1])
                : 0u
        };

        [loop]
        while ((remainingRadialStrata[0] | remainingRadialStrata[1]) != 0u)
        {
            [unroll]
            for (uint sideIndex = 0u; sideIndex < 2u; ++sideIndex)
            {
                if (visibilityMask.occludedBits == RadialVisibilityFullMask)
                    break;
                if (!sideActive[sideIndex])
                    continue;
                uint radialMask = remainingRadialStrata[sideIndex];
                if (radialMask == 0u)
                    continue;
                uint radialStratum = uint(firstbitlow(radialMask));
                remainingRadialStrata[sideIndex] = radialMask &
                    (radialMask - 1u);

                float samplingSide = sideIndex == 0u ? -1.0f : 1.0f;
                float normalizedStep = ProgressiveRadialSample(
                    radialStratum, radialRotation[sideIndex]);
                float distributedStep = saturate(normalizedStep);
                if (abs(g_Visibility.stepDistributionExponent - 1.0f) >= 1e-4f)
                {
                    distributedStep =
                        abs(g_Visibility.stepDistributionExponent - 2.0f) < 1e-4f
                        ? distributedStep * distributedStep
                        : pow(distributedStep,
                            g_Visibility.stepDistributionExponent);
                }
                float sampleDistance = distributedStep * sideProjectedRadius[sideIndex];
                sampleDistance = max(sampleDistance, 0.5f);
                float2 samplePixelFloat = receiverPixelCenter +
                    sidePixelDirection[sideIndex] * sampleDistance;
                if (any(samplePixelFloat < g_Visibility.view.viewportOrigin) ||
                    any(samplePixelFloat >= g_Visibility.view.viewportOrigin +
                        g_Visibility.fullResolution))
                {
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

                VisibilityInterval interval;
#if VISIBILITY_ESTIMATOR == VisibilityEstimator_UniformSolidAngle
                interval = BuildGtInterval(
                    directionToSample,
                    backDirection,
                    sliceMeasure);
#elif VISIBILITY_ESTIMATOR == VisibilityEstimator_CosineWeightedSolidAngle
                interval = BuildGtCosineInterval(
                    directionToSample,
                    backDirection,
                    sliceMeasure);
#else
                float frontAngle;
                float backAngle;
                interval = BuildProjectedAngleVisibilityInterval(
                    directionToSample,
                    backDirection,
                    viewDirection,
                    samplingSide,
                    projectedNormalAngle,
                    frontAngle,
                    backAngle);
#endif
                uint candidateBits = MakeStochasticSectorRangeMask(
                    interval, sectorPhase);
                if (candidateBits == 0u)
                    continue;

                uint newlyCoveredBits = AccumulateOccluder(visibilityMask, candidateBits);
                uint newSectorCount = 0u;
#if ENABLE_GI
                newSectorCount = countbits(newlyCoveredBits);
#endif

                // Geometry reads above are shared by AO and GI. Source normal,
                // radiance, and emissive are fetched only for newly claimed
                // sectors and only when the GI consumer is active.
#if ENABLE_GI
                if (newSectorCount == 0u)
                    continue;

                float receiverCosine = saturate(dot(receiverNormalVS, directionToSample));
#if VISIBILITY_ESTIMATOR != VisibilityEstimator_CosineWeightedSolidAngle
                if (!(receiverCosine > 0.0f))
                    continue;
#endif

#if ENABLE_BOUNCE_REINJECTION
                // Only the newest transport frontier is eligible for another
                // bounce. Reject its conservative final-image upper bound
                // before fetching material textures; the sum of angular-sector
                // weights is at most one, so all rejected source pieces remain
                // bounded by the configured cutoff at this receiver.
                uint2 previousSamplingPixel =
                    FullToSamplingPixel(samplePixel);
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
                float signedSourceCosine = dot(sampleNormalVS, -directionToSample);
                float sourceCosine = sourceIsDoubleSided
                    ? abs(signedSourceCosine)
                    : saturate(signedSourceCosine);
                float3 weightedSource = sourceRadiance * sourceCosine;
                if (!any(weightedSource > 0.0f))
                    continue;

#if VISIBILITY_ESTIMATOR == VisibilityEstimator_UniformSolidAngle
                sliceIndirectDiffuse += sourceRadiance *
                    ComputeGtUniformGiSampleWeight(
                        newSectorCount,
                        receiverCosine,
                        sourceCosine);
#elif VISIBILITY_ESTIMATOR == VisibilityEstimator_CosineWeightedSolidAngle
                sliceIndirectDiffuse += sourceRadiance *
                    ComputeGtCosineGiSampleWeight(
                        newSectorCount,
                        sliceMeasure.cosineSliceMass,
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
#if VISIBILITY_ESTIMATOR == VisibilityEstimator_UniformSolidAngle
        ambientVisibility = ResolveGtUniformAmbientVisibility(visibilityMask);
#elif VISIBILITY_ESTIMATOR == VisibilityEstimator_CosineWeightedSolidAngle
        ambientVisibility = ResolveGtCosineAmbientVisibility(
            visibilityMask, sliceMeasure);
#else
        ambientVisibility = GetSliceVisibility(visibilityMask);
#endif
#endif
#if ENABLE_GI
        indirectDiffuse = sliceIndirectDiffuse;
#endif

    // A single uniformly selected slice is an unbiased outer Monte Carlo
    // estimate of the cosine integral. Its projected slice mass can exceed
    // one for tilted normals, so retain that energy through reconstruction;
    // the physical [0,1] bound is applied only after averaging/composition.
    ambientVisibility = max(ambientVisibility, 0.0f);
    if (!isfinite(ambientVisibility))
        ambientVisibility = 1.0f;
#if VISIBILITY_ESTIMATOR == VisibilityEstimator_UniformSolidAngle
    float irradianceNormalization = GetGtUniformIrradianceNormalization();
#elif VISIBILITY_ESTIMATOR == VisibilityEstimator_CosineWeightedSolidAngle
    float irradianceNormalization = GetGtCosineIrradianceNormalization();
#else
    float irradianceNormalization = VisibilityPi;
#endif
    indirectDiffuse = max(
        indirectDiffuse * irradianceNormalization, 0.0f);
    if (!IsFiniteFloat3(indirectDiffuse))
        indirectDiffuse = 0.0f;

#if ENABLE_AO
    u_AmbientVisibility[dispatchPixel] = min(
        ambientVisibility, 65504.0f);
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
}
