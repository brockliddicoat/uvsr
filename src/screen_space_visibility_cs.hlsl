#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "radial_visibility_mask.hlsli"
#include "visibility_estimator_shared.h"
#include "visibility_projection_shared.h"
#include "screen_space_visibility_cb.h"
#include "noise_sampling.hlsli"
#include "sample_accumulation.hlsli"

#ifndef VISIBILITY_ESTIMATOR
#define VISIBILITY_ESTIMATOR 0
#endif
#ifndef ENABLE_AO
#define ENABLE_AO 1
#endif
#ifndef ENABLE_GI
#define ENABLE_GI 1
#endif
#ifndef RUNTIME_SAMPLE_PARITY
#define RUNTIME_SAMPLE_PARITY 0
#endif
#ifndef OUTPUT_PACKED_EDGES
#define OUTPUT_PACKED_EDGES 0
#endif
#ifndef OUTPUT_AO_HIT_DISTANCE
#define OUTPUT_AO_HIT_DISTANCE 0
#endif
#ifndef OUTPUT_GI_HIT_DISTANCE
#define OUTPUT_GI_HIT_DISTANCE 0
#endif

#if OUTPUT_AO_HIT_DISTANCE && !ENABLE_AO
#error OUTPUT_AO_HIT_DISTANCE requires ENABLE_AO.
#endif
#if OUTPUT_GI_HIT_DISTANCE && !ENABLE_GI
#error OUTPUT_GI_HIT_DISTANCE requires ENABLE_GI.
#endif

#if RUNTIME_SAMPLE_PARITY > 2
#error RUNTIME_SAMPLE_PARITY must be 0 (guarded), 1 (even), or 2 (odd).
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
#if ENABLE_GI
Texture2D<float4> t_SourceRadiance : register(t2);
#endif
Texture2DArray<float> t_Noise : register(t3);
Texture2D<uint> t_AttemptMask : register(t4);
#if ENABLE_AO
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_AmbientVisibility : register(u0);
#endif
#if ENABLE_GI
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_IndirectDiffuse : register(u1);
#endif
#if OUTPUT_AO_HIT_DISTANCE
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_AmbientHitDistance : register(u2);
#endif
#if OUTPUT_PACKED_EDGES
VK_IMAGE_FORMAT("r8ui") RWTexture2D<uint> u_PackedEdges : register(u3);
#endif
#if OUTPUT_GI_HIT_DISTANCE
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_IndirectHitDistance : register(u4);
#endif

static const float VisibilityPi = 3.14159265358979323846f;
static const float VisibilityHalfPi = 1.57079632679489661923f;
static const float VisibilityEpsilon = 1e-6f;
static const float VisibilityHitDistanceMaximum = 65472.0f;
static const float VisibilityHitDistanceMiss = 65504.0f;
#if OUTPUT_GI_HIT_DISTANCE
static const float3 VisibilitySignalLuminanceWeights =
    float3(0.2126f, 0.7152f, 0.0722f);
#endif

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
static const uint SchedulerDimension_OddSampleSide = 3u;
static const uint SchedulerDimension_RadialNegative = 4u;
static const uint SchedulerDimension_RadialPositive = 5u;

float VisibilityFastAcos(float value)
{
    // XeGTAO / Lagarde approximation: avoids two native acos operations per
    // radial sample while retaining sufficient precision for 32 mask sectors.
    float x = abs(clamp(value, -1.0f, 1.0f));
    float result = (-0.156583f * x + VisibilityHalfPi) * sqrt(1.0f - x);
    return value >= 0.0f ? result : VisibilityPi - result;
}

float2 VisibilitySliceSinCos(float angle)
{
    return float2(cos(angle), sin(angle));
}

float VisibilityRadialPower(float value, float exponent)
{
    if (abs(exponent - 1.0f) < 1e-4f)
        return value;
    if (abs(exponent - 2.0f) < 1e-4f)
        return value * value;
    return pow(value, exponent);
}

float SchedulerRandom(uint2 samplingPixel, uint dimension, uint phase)
{
    return UVSRSamplePrecomputedNoise(
        t_Noise,
        g_Visibility.noisePattern,
        samplingPixel,
        uint2(g_Visibility.samplingResolution),
        phase,
        0x100u + dimension);
}

uint2 SamplingToFullPixel(uint2 samplingPixel)
{
    uint scale = max(g_Visibility.resolutionScale, 1u);
    uint2 fullSize = uint2(g_Visibility.fullResolution);
    return min(samplingPixel * scale + scale / 2u, fullSize - 1u);
}

uint VisibilitySamplingFootprintAttemptToken(uint2 samplingPixel)
{
    if (!UvsrSampleScheduleEnabled(g_Visibility.sampleSequenceMode))
        return 0u;

    const uint scale = max(g_Visibility.resolutionScale, 1u);
    const uint2 fullSize = uint2(g_Visibility.fullResolution);
    const uint2 footprintBegin = samplingPixel * scale;
    const uint2 footprintEnd = min(footprintBegin + scale, fullSize);
    uint selectedToken = 0u;
    [loop]
    for (uint y = footprintBegin.y; y < footprintEnd.y; ++y)
    {
        [loop]
        for (uint x = footprintBegin.x; x < footprintEnd.x; ++x)
        {
            const uint token = t_AttemptMask[uint2(x, y)];
            if (token != 0u)
            {
                selectedToken = selectedToken == 0u
                    ? token
                    : min(selectedToken, token);
            }
        }
    }
    return selectedToken;
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

#if OUTPUT_PACKED_EDGES
uint PackEdgeContinuity(float4 continuity)
{
    uint4 quantized = uint4(round(saturate(continuity) * 3.0f));
    return ((quantized.x & 3u) << 6u) |
        ((quantized.y & 3u) << 4u) |
        ((quantized.z & 3u) << 2u) |
        (quantized.w & 3u);
}

uint ComputePackedReceiverEdges(
    uint2 samplingPixel,
    uint2 receiverPixel,
    float receiverDepth)
{
    if (!IsValidDepth(receiverDepth))
        return 0u;

    static const int2 offsets[4] = {
        int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
    };
    uint2 samplingMaximum = uint2(g_Visibility.samplingResolution) - 1u;
    float receiverLinearDepth;
    float3 receiverPositionVS;
    if (!ReconstructViewPositionSafe(
            float2(receiverPixel) + 0.5f,
            receiverDepth,
            receiverPositionVS))
    {
        return 0u;
    }
    receiverLinearDepth = abs(receiverPositionVS.z);
    float3 receiverNormal = SafeNormalize(
        t_Normals[receiverPixel].xyz,
        float3(0.0f, 1.0f, 0.0f));
    float4 depthDiscontinuity = 1.0f;
    float4 normalDiscontinuity = 0.0f;
    [unroll]
    for (uint edgeIndex = 0u; edgeIndex < 4u; ++edgeIndex)
    {
        uint2 neighborSamplingPixel = uint2(clamp(
            int2(samplingPixel) + offsets[edgeIndex],
            int2(0, 0), int2(samplingMaximum)));
        uint2 neighborPixel = SamplingToFullPixel(neighborSamplingPixel);
        float neighborDepth = t_Depth[neighborPixel];
        float3 neighborPositionVS;
        if (!IsValidDepth(neighborDepth) ||
            !ReconstructViewPositionSafe(
                float2(neighborPixel) + 0.5f,
                neighborDepth,
                neighborPositionVS))
        {
            depthDiscontinuity[edgeIndex] = 1.0f;
            normalDiscontinuity[edgeIndex] = 1.0f;
            continue;
        }
        float neighborLinearDepth = abs(neighborPositionVS.z);
        depthDiscontinuity[edgeIndex] = saturate(
            abs(neighborLinearDepth - receiverLinearDepth) /
            max(receiverLinearDepth * 0.08f, 0.01f));
        float3 neighborNormal = SafeNormalize(
            t_Normals[neighborPixel].xyz, receiverNormal);
        normalDiscontinuity[edgeIndex] = saturate(
            (1.0f - dot(receiverNormal, neighborNormal)) * 4.0f);
    }

    if (g_Visibility.packedEdgeMode == 2u)
    {
        float horizontalSlope = min(
            depthDiscontinuity.x, depthDiscontinuity.y);
        float verticalSlope = min(
            depthDiscontinuity.z, depthDiscontinuity.w);
        depthDiscontinuity.xy = saturate(
            depthDiscontinuity.xy - horizontalSlope.xx);
        depthDiscontinuity.zw = saturate(
            depthDiscontinuity.zw - verticalSlope.xx);
    }
    float4 continuity = 1.0f - max(
        depthDiscontinuity, normalDiscontinuity);
    return PackEdgeContinuity(continuity);
}
#endif

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

void WriteEmptyVisibilityOutput(uint2 pixel)
{
#if ENABLE_AO
    u_AmbientVisibility[pixel] = 1.0f;
#endif
#if ENABLE_GI
    u_IndirectDiffuse[pixel] = 0.0f;
#endif
#if OUTPUT_AO_HIT_DISTANCE
    u_AmbientHitDistance[pixel] = 0.0f;
#endif
#if OUTPUT_GI_HIT_DISTANCE
    u_IndirectHitDistance[pixel] = 0.0f;
#endif
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchPixel : SV_DispatchThreadID)
{
    if (any(dispatchPixel >= uint2(g_Visibility.samplingResolution)))
        return;
    const uint attemptToken =
        VisibilitySamplingFootprintAttemptToken(dispatchPixel);
    if (UvsrSampleScheduleEnabled(g_Visibility.sampleSequenceMode) &&
        attemptToken == 0u)
        return;

    uint2 receiverPixel = SamplingToFullPixel(dispatchPixel);
    float receiverDepth = t_Depth[receiverPixel];
    float2 receiverPixelCenter = float2(receiverPixel) + 0.5f;

#if OUTPUT_PACKED_EDGES
    u_PackedEdges[dispatchPixel] = ComputePackedReceiverEdges(
        dispatchPixel, receiverPixel, receiverDepth);
#endif

#if ENABLE_GI
    const bool giSourcePotential =
        g_Visibility.sourceRadianceAvailable != 0u;
#if !ENABLE_AO
    if (!giSourcePotential)
    {
        WriteEmptyVisibilityOutput(dispatchPixel);
        return;
    }
#endif
#endif

    if (!IsValidDepth(receiverDepth) || !(g_Visibility.radiusWorld > VisibilityEpsilon))
    {
        WriteEmptyVisibilityOutput(dispatchPixel);
        return;
    }

    float3 receiverPositionVS;
    bool receiverReconstructed = ReconstructViewPositionSafe(
        receiverPixelCenter, receiverDepth, receiverPositionVS);
    if (!receiverReconstructed)
    {
        WriteEmptyVisibilityOutput(dispatchPixel);
        return;
    }

    float3 receiverNormalWS = t_Normals[receiverPixel].xyz;
    float3 receiverNormalVS = mul(float4(receiverNormalWS, 0.0f),
        g_Visibility.view.matWorldToView).xyz;
    if (dot(receiverNormalVS, receiverNormalVS) <= VisibilityEpsilon * VisibilityEpsilon)
    {
        WriteEmptyVisibilityOutput(dispatchPixel);
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
        WriteEmptyVisibilityOutput(dispatchPixel);
        return;
    }

    uint phase = UvsrResolveSampleSequencePhase(
        g_Visibility.sampleSequenceMode,
        attemptToken,
        g_Visibility.sampleSequencePhase);
#if RUNTIME_SAMPLE_PARITY > 0
    // The CPU clamps the count and selects a parity-matched shader. Keeping the
    // number in the cbuffer permits every 1-64 slider value to share one of two
    // compact loop permutations.
    uint selectedSampleCount = g_Visibility.maximumSampleCount;
#else
    // Non-default estimator/consumer/topology combinations retain one guarded
    // Runtime shader instead of multiplying every axis by parity.
    uint selectedSampleCount = clamp(
        g_Visibility.maximumSampleCount, 1u, 64u);
#endif

    float sliceRotation = SchedulerRandom(
        dispatchPixel, SchedulerDimension_SliceRotation, phase);
    float ambientVisibility = 1.0f;
    float3 indirectDiffuse = 0.0f;
#if OUTPUT_AO_HIT_DISTANCE
    float ambientHitDistanceSectorSum = 0.0f;
#endif
#if OUTPUT_GI_HIT_DISTANCE
    float indirectHitDistanceWeightedSum = 0.0f;
    float indirectHitDistanceWeight = 0.0f;
#endif
    // A slice traces both negative and positive sides, so azimuth is sampled
    // once over [0, pi). Sampling [0, 2*pi) repeats the same unoriented axis
    // and aliases the six Activision temporal rotations to three.
    float sliceAzimuth = sliceRotation * VisibilityPi;
    // Every pixel evaluates one coherent stochastic image-plane slice. The
    // estimator-specific measure remains a compile-time contract.
    float2 sliceSinCos = VisibilitySliceSinCos(sliceAzimuth);
    float3 screenSliceDirection = float3(
        sliceSinCos.x, sliceSinCos.y, 0.0f);
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
            float projectedRadiusLength = length(projectedRadiusVector);
            sideProjectionValid[projectionSideIndex] =
                sideProjectionValid[projectionSideIndex] &&
                projectedRadiusLength >= 0.5f &&
                isfinite(projectedRadiusLength);
            sidePixelDirection[projectionSideIndex] =
                sideProjectionValid[projectionSideIndex]
                ? projectedRadiusVector / projectedRadiusLength
                : 0.0f;
            sideProjectedRadius[projectionSideIndex] = projectedRadiusLength;
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

#if RUNTIME_SAMPLE_PARITY == 1
        uint stepsPerSide = selectedSampleCount >> 1u;
        uint sideStepCount[2] = {
            stepsPerSide,
            stepsPerSide
        };
#elif RUNTIME_SAMPLE_PARITY == 2
        uint stepsPerSide = selectedSampleCount >> 1u;
        uint oddSampleSide = SchedulerRandom(
            dispatchPixel,
            SchedulerDimension_OddSampleSide,
            phase) < 0.5f ? 0u : 1u;
        uint sideStepCount[2] = {
            stepsPerSide + (oddSampleSide == 0u ? 1u : 0u),
            stepsPerSide + (oddSampleSide == 1u ? 1u : 0u)
        };
#else
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
#endif
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
                distributedStep = VisibilityRadialPower(
                    distributedStep,
                    g_Visibility.stepDistributionExponent);
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
                uint2 sampleRepresentativePixel = samplePixel;
                if (hasPreviousSample[sideIndex] &&
                    all(sampleRepresentativePixel ==
                        previousSamplePixel[sideIndex]))
                {
                    continue;
                }
                previousSamplePixel[sideIndex] = sampleRepresentativePixel;
                hasPreviousSample[sideIndex] = true;
                float3 samplePositionVS;
                float sampleDepth = t_Depth[sampleRepresentativePixel];
                if (!IsValidDepth(sampleDepth) ||
                    !ReconstructViewPositionSafe(
                        float2(sampleRepresentativePixel) + 0.5f,
                        sampleDepth,
                        samplePositionVS))
                {
                    continue;
                }

                float effectiveThickness = g_Visibility.thicknessWorld;

                float3 frontDelta = samplePositionVS - receiverPositionVS;
                float frontLengthSquared = dot(frontDelta, frontDelta);
                if (!(frontLengthSquared > VisibilityEpsilon * VisibilityEpsilon) ||
                    !isfinite(frontLengthSquared))
                {
                    continue;
                }
#if OUTPUT_AO_HIT_DISTANCE || OUTPUT_GI_HIT_DISTANCE
                const float frontLength = sqrt(frontLengthSquared);
#endif
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
#if ENABLE_GI || OUTPUT_AO_HIT_DISTANCE
                uint newSectorCount = countbits(newlyCoveredBits);
#endif
#if OUTPUT_AO_HIT_DISTANCE
                // Every mask bit has equal measure under the selected
                // estimator. Newly covered bits bind each sector to its first
                // sampled blocker without changing the resolved AO mask.
                ambientHitDistanceSectorSum += float(newSectorCount) *
                    min(frontLength, VisibilityHitDistanceMaximum);
#endif

                // Geometry reads above are shared by AO and GI. Source normal
                // and radiance are fetched only for newly claimed sectors and
                // only when the GI consumer is active.
#if ENABLE_GI
                if (newSectorCount == 0u)
                    continue;

                float receiverCosine = saturate(dot(receiverNormalVS, directionToSample));
#if VISIBILITY_ESTIMATOR != VisibilityEstimator_CosineWeightedSolidAngle
                if (!(receiverCosine > 0.0f))
                    continue;
#endif

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

                float3 sampleNormalWS = t_Normals[samplePixel].xyz;
                float3 sampleNormalVS = mul(float4(sampleNormalWS, 0.0f),
                    g_Visibility.view.matWorldToView).xyz;
                sampleNormalVS = SafeNormalize(sampleNormalVS, 0.0f);
                float signedSourceCosine = dot(sampleNormalVS, -directionToSample);
                float sourceCosine = saturate(signedSourceCosine);
                float3 weightedSource = sourceRadiance * sourceCosine;
                if (!any(weightedSource > 0.0f))
                    continue;

                float3 indirectContribution;
#if VISIBILITY_ESTIMATOR == VisibilityEstimator_UniformSolidAngle
                indirectContribution = sourceRadiance *
                    ComputeGtUniformGiSampleWeight(
                        newSectorCount,
                        receiverCosine,
                        sourceCosine);
#elif VISIBILITY_ESTIMATOR == VisibilityEstimator_CosineWeightedSolidAngle
                indirectContribution = sourceRadiance *
                    ComputeGtCosineGiSampleWeight(
                        newSectorCount,
                        sliceMeasure.cosineSliceMass,
                        sourceCosine);
#else
                float angularCoverage = float(newSectorCount) /
                    float(RadialVisibilitySectorCount);
                indirectContribution = weightedSource * receiverCosine *
                    angularCoverage;
#endif
                sliceIndirectDiffuse += indirectContribution;
#if OUTPUT_GI_HIT_DISTANCE
                // The common irradiance normalization cancels from this
                // first moment. Matching NRD's luminance definition makes the
                // single distance representative of the exact RGB terms that
                // form this aggregate diffuse radiance signal.
                float contributionWeight = dot(
                    indirectContribution,
                    VisibilitySignalLuminanceWeights);
                if (contributionWeight > VisibilityEpsilon &&
                    isfinite(contributionWeight))
                {
                    float contributionHitDistance = min(
                        frontLength,
                        VisibilityHitDistanceMaximum);
                    indirectHitDistanceWeightedSum +=
                        contributionWeight * contributionHitDistance;
                    indirectHitDistanceWeight += contributionWeight;
                }
#endif
#endif
            }

            if (visibilityMask.occludedBits ==
                    RadialVisibilityFullMask ||
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
    u_IndirectDiffuse[dispatchPixel] = float4(
        min(indirectDiffuse, 65504.0f), 0.0f);
#endif
#if OUTPUT_AO_HIT_DISTANCE
    // Raw AO is M * visibleSectorCount / 32, where M is one for the
    // projected and uniform estimators and cosineSliceMass for the cosine
    // estimator. M is common to every sector and cancels from this expected
    // first bounce distance. Uncovered sectors are censored misses at the
    // configured trace reach.
    uint ambientVisibleSectorCount = RadialVisibilitySectorCount -
        countbits(visibilityMask.occludedBits);
    float ambientTraceReach = min(
        g_Visibility.radiusWorld,
        VisibilityHitDistanceMaximum);
    float ambientExpectedHitDistance =
        (ambientHitDistanceSectorSum +
            float(ambientVisibleSectorCount) * ambientTraceReach) /
        float(RadialVisibilitySectorCount);
#if VISIBILITY_ESTIMATOR == VisibilityEstimator_CosineWeightedSolidAngle
    // A zero mass slice contributes no AO sample and therefore has no hit
    // distance data, matching NRD's zero convention for a skipped lobe.
    if (!(sliceMeasure.cosineSliceMass > VisibilityEpsilon) ||
        !isfinite(sliceMeasure.cosineSliceMass))
    {
        ambientExpectedHitDistance = 0.0f;
    }
#endif
    u_AmbientHitDistance[dispatchPixel] =
        ambientExpectedHitDistance;
#endif
#if OUTPUT_GI_HIT_DISTANCE
    float indirectHitDistance = 0.0f;
    if (giSourcePotential &&
        indirectHitDistanceWeight > VisibilityEpsilon &&
        isfinite(indirectHitDistanceWeight) &&
        isfinite(indirectHitDistanceWeightedSum))
    {
        indirectHitDistance = min(
            indirectHitDistanceWeightedSum /
                indirectHitDistanceWeight,
            VisibilityHitDistanceMaximum);
    }
    u_IndirectHitDistance[dispatchPixel] = indirectHitDistance;
#endif
}
