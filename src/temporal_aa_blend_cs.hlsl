//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
//
// Portions are adapted from Microsoft DirectX Graphics Samples and distributed
// under third_party/microsoft_directx_graphics_samples/LICENSE.txt. UVSR
// adaptations cover NVRHI, RGBA16F motion validity, arbitrary dimensions,
// infinite reverse-Z depth, and the compile-time experiment dimensions below.
//

#pragma pack_matrix(row_major)

#include "temporal_aa_options_shared.h"
#include "temporal_aa_common.hlsli"

#ifndef TAA_MOTION_SOURCE
#error TAA_MOTION_SOURCE must be a compile-time shader define
#endif
#ifndef TAA_CURRENT_RECONSTRUCTION
#error TAA_CURRENT_RECONSTRUCTION must be a compile-time shader define
#endif
#ifndef TAA_HISTORY_FILTER
#error TAA_HISTORY_FILTER must be a compile-time shader define
#endif
#ifndef TAA_RECTIFICATION
#error TAA_RECTIFICATION must be a compile-time shader define
#endif
#ifndef TAA_OPTIMIZED_COMPUTE
#error TAA_OPTIMIZED_COMPUTE must be a compile-time shader define
#endif
#ifndef TAA_FUSED_OUTPUT
#error TAA_FUSED_OUTPUT must be a compile-time shader define
#endif
#if TAA_OPTIMIZED_COMPUTE
#define TAA_LDS_LAYOUT UVSR_TAA_LDS_PACKED
#else
#define TAA_LDS_LAYOUT UVSR_TAA_LDS_LEGACY
#endif
#define TAA_SHARED_WORK_REUSE TAA_OPTIMIZED_COMPUTE
#define TAA_EFFECTIVE_EARLY_HISTORY_REJECTION \
    TAA_OPTIMIZED_COMPUTE

#if TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED
static const uint kColorBorder = 2;
#else
static const uint kColorBorder = 1;
#endif

#if TAA_MOTION_SOURCE == UVSR_TAA_MOTION_CLOSEST_CROSS || \
    TAA_MOTION_SOURCE == UVSR_TAA_MOTION_CENTER_FIRST_EDGE_DILATION
#define TAA_NEEDS_LDS_MOTION 1
#else
#define TAA_NEEDS_LDS_MOTION 0
#endif

static const uint kOutputTileWidth = 16;
static const uint kOutputTileHeight = 8;
static const uint kColorPitch =
    kOutputTileWidth + 2 * kColorBorder;
static const uint kColorRows =
    kOutputTileHeight + 2 * kColorBorder;
static const uint kColorPixelCount =
    kColorPitch * kColorRows;
#if TAA_LDS_LAYOUT == UVSR_TAA_LDS_LEGACY
static const uint kCoreBorder = kColorBorder;
static const uint kCorePitch = kColorPitch;
static const uint kCoreRows = kColorRows;
#else
// Depth, luma, and motion use only a one-pixel cardinal/diagonal footprint.
// De-jittered color alone owns the two-pixel reconstruction border.
static const uint kCoreBorder = 1;
static const uint kCorePitch =
    kOutputTileWidth + 2 * kCoreBorder;
static const uint kCoreRows =
    kOutputTileHeight + 2 * kCoreBorder;
#endif
static const uint kCorePixelCount =
    kCorePitch * kCoreRows;
static const float kFarViewDepth = 65504.0;
static const float kFiniteHdrLimit = 65504.0;

RWTexture2D<float4> OutTemporal : register(u0);
RWTexture2D<float> OutDepth : register(u1);
#if TAA_FUSED_OUTPUT
RWTexture2D<float4> OutFusedScene : register(u2);
#endif

Texture2D<float4> VelocityBuffer : register(t0);
Texture2D<float3> InColor : register(t1);
Texture2D<float4> InTemporal : register(t2);
Texture2D<float> CurDepth : register(t3);
Texture2D<float> PreDepth : register(t4);

SamplerState LinearSampler : register(s0);

groupshared float ldsDepth[kCorePixelCount];
#if TAA_SHARED_WORK_REUSE
groupshared float ldsViewDepth[kCorePixelCount];
#endif
groupshared float ldsR[kColorPixelCount];
groupshared float ldsG[kColorPixelCount];
groupshared float ldsB[kColorPixelCount];
#if TAA_NEEDS_LDS_MOTION
#if TAA_LDS_LAYOUT == UVSR_TAA_LDS_PACKED
groupshared uint2 ldsPackedMotion[kCorePixelCount];
#else
groupshared float ldsMotionX[kCorePixelCount];
groupshared float ldsMotionY[kCorePixelCount];
groupshared float ldsMotionZ[kCorePixelCount];
groupshared float ldsMotionValidity[kCorePixelCount];
#endif
#endif

cbuffer CB1 : register(b1)
{
    float4x4 Projection;
    float2 RcpBufferDim;
    float TemporalBlendFactor;
    float RcpSpeedLimiter;
    // Donut PlanarView signed full-resolution pixel offset for this frame.
    // De-Jittered current reconstruction samples input at ST + CurrentJitter.
    float2 CurrentJitter;
    // PreviousJitter - CurrentJitter in full-resolution pixels. G-buffer
    // motion has already removed this delta, so it is added only when sampling
    // the raw jittered previous-depth grid, never history color.
    float2 CurrentToPreviousJitter;
    uint2 BufferDim;
    uint HistoryValid;
    uint DispatchGroupYOffset;
    // Combined current/previous endpoint quantization error for the actual
    // G-buffer depth format. This is one UNORM step for D24/D16 and zero for
    // float32 depth. It is a numeric integration constant, not a shader
    // option, so temporal permutations remain branch-free.
    float SourceDepthPairQuantizationError;
    // Logical N-frame horizon expressed as N/(N+1). This caps only accepted
    // history; it cannot revive invalid motion, failed reverse-Z depth, or
    // out-of-bounds reprojection.
    float MaximumHistoryWeight;
    uint TemporalBehaviorFlags;
    uint TemporalBehaviorPadding;
}

struct MotionSelection
{
    float3 velocity;
    float currentDeviceDepth;
    float currentViewDepth;
    float valid;
};

struct HistorySample
{
    float3 color;
    float weight;
};

struct CatmullRomCross
{
    float2 centerPosition;
    float2 leftPosition;
    float2 rightPosition;
    float2 northPosition;
    float2 southPosition;
    float2 northWestPosition;
    float2 northEastPosition;
    float2 southWestPosition;
    float2 southEastPosition;
    float centerWeight;
    float leftWeight;
    float rightWeight;
    float northWeight;
    float southWeight;
    float northWestWeight;
    float northEastWeight;
    float southWestWeight;
    float southEastWeight;
};

float3 SanitizeHdr(float3 rgb)
{
    return all(isfinite(rgb))
        ? clamp(rgb, -kFiniteHdrLimit, kFiniteHdrLimit)
        : 0.0;
}

float MotionValidity(float4 packedMotion)
{
    return UvsrTemporalMotionValidity(packedMotion);
}

float3 RgbToYCoCg(float3 rgb)
{
    return float3(
        0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,
        0.5 * rgb.r - 0.5 * rgb.b,
        -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b);
}

float3 YCoCgToRgb(float3 ycocg)
{
    return float3(
        ycocg.x + ycocg.y - ycocg.z,
        ycocg.x + ycocg.z,
        ycocg.x - ycocg.y - ycocg.z);
}

float3 RgbToWorking(float3 rgb)
{
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
    return RgbToYCoCg(rgb);
#else
    return rgb;
#endif
}

float3 WorkingToRgb(float3 working)
{
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
    return SanitizeHdr(YCoCgToRgb(working));
#else
    return SanitizeHdr(working);
#endif
}

float RGBToLuminance(float3 rgb)
{
    return dot(rgb, float3(0.212671, 0.715160, 0.072169));
}

uint ColorIndexToCoreIndex(uint colorIdx)
{
#if TAA_LDS_LAYOUT == UVSR_TAA_LDS_LEGACY
    return colorIdx;
#else
    uint colorX = colorIdx % kColorPitch;
    uint colorY = colorIdx / kColorPitch;
    int relativeX = int(colorX) - int(kColorBorder);
    int relativeY = int(colorY) - int(kColorBorder);
    return uint(relativeX + int(kCoreBorder)) +
        uint(relativeY + int(kCoreBorder)) * kCorePitch;
#endif
}

void StoreWorkingColor(
    uint colorIdx,
    float3 rgb)
{
    // The YCoCg permutation transforms exactly once per shared-tile sample.
    // Neighborhood lookups read the transformed LDS value directly.
    float3 safeRgb = SanitizeHdr(rgb);
    float3 working = RgbToWorking(safeRgb);
    ldsR[colorIdx] = working.x;
    ldsG[colorIdx] = working.y;
    ldsB[colorIdx] = working.z;
}

float3 LoadWorkingColor(uint colorIdx)
{
    return float3(
        ldsR[colorIdx],
        ldsG[colorIdx],
        ldsB[colorIdx]);
}

float LoadDepth(uint colorIdx)
{
    return ldsDepth[ColorIndexToCoreIndex(colorIdx)];
}

#if TAA_NEEDS_LDS_MOTION
void StoreMotion(uint coreIdx, float4 packedMotion)
{
    // Sanitize once while loading the tile. Invalid NaN/Inf motion must not
    // contaminate later lerps or divergence math even when its validity
    // multiplier is zero (IEEE NaN multiplied by zero remains NaN).
    float valid = MotionValidity(packedMotion);
    float3 safeMotion = valid > 0.0
        ? packedMotion.xyz
        : 0.0;
#if TAA_LDS_LAYOUT == UVSR_TAA_LDS_PACKED
    // The source texture is RGBA16F. Round-tripping those already-FP16 values
    // through half bit patterns is lossless while halving the motion LDS
    // footprint relative to four float arrays.
    ldsPackedMotion[coreIdx] = uint2(
        f32tof16(safeMotion.x) |
            (f32tof16(safeMotion.y) << 16u),
        f32tof16(safeMotion.z) |
            (f32tof16(valid) << 16u));
#else
    ldsMotionX[coreIdx] = safeMotion.x;
    ldsMotionY[coreIdx] = safeMotion.y;
    ldsMotionZ[coreIdx] = safeMotion.z;
    ldsMotionValidity[coreIdx] = valid;
#endif
}

float4 LoadMotion(uint colorIdx)
{
#if TAA_LDS_LAYOUT == UVSR_TAA_LDS_PACKED
    uint2 packed =
        ldsPackedMotion[ColorIndexToCoreIndex(colorIdx)];
    return float4(
        f16tof32(packed.x & 0xffffu),
        f16tof32(packed.x >> 16u),
        f16tof32(packed.y & 0xffffu),
        f16tof32(packed.y >> 16u));
#else
    uint coreIdx = ColorIndexToCoreIndex(colorIdx);
    return float4(
        ldsMotionX[coreIdx],
        ldsMotionY[coreIdx],
        ldsMotionZ[coreIdx],
        ldsMotionValidity[coreIdx]);
#endif
}
#endif

float2 STtoUV(float2 ST)
{
    return (ST + 0.5) * RcpBufferDim;
}

float HistoryPositionInBounds(float2 ST)
{
    // Temporal AA history color and depth use linear sampling or Gather. Require
    // their real footprint instead of accepting a clamped half-pixel excursion
    // beyond the viewport.
    return UvsrTemporalLinearFootprintInBounds(ST, BufferDim);
}

// These are Temporal AA's local reversible blend-domain transforms, not a
// display tonemapper. Normal scene radiance follows the reference equations.
// The finite guard prevents experimental negative HDR values from producing
// NaNs while leaving the ordinary nonnegative path unchanged.
float3 TM(float3 rgb)
{
    float denominator = 1.0 + RGBToLuminance(rgb);
    denominator = abs(denominator) > 1e-5
        ? denominator
        : denominator < 0.0
            ? -1e-5
            : 1e-5;
    return SanitizeHdr(rgb / denominator);
}

float3 ITM(float3 rgb)
{
    float denominator = 1.0 - RGBToLuminance(rgb);
    denominator = abs(denominator) > 1e-5
        ? denominator
        : denominator < 0.0
            ? -1e-5
            : 1e-5;
    return SanitizeHdr(rgb / denominator);
}

float3 ClipColor(
    float3 color,
    float3 boxMin,
    float3 boxMax,
    float dilation = 1.0)
{
    float3 boxCenter = (boxMax + boxMin) * 0.5;
    float3 halfDim = (boxMax - boxMin) * 0.5 * dilation + 0.001;
    float3 displacement = color - boxCenter;
    float3 units = abs(displacement / halfDim);
    float maxUnit = max(max(units.x, units.y), max(units.z, 1.0));
    return boxCenter + displacement / maxUnit;
}

void GetBBoxForPair(
    uint fillIdx,
    uint holeIdx,
    out float3 boxMin,
    out float3 boxMax)
{
    // This is the original shared horizontal-pair Temporal AA neighborhood.
    boxMin = boxMax = LoadWorkingColor(fillIdx);
    float3 a = LoadWorkingColor(fillIdx - kColorPitch - 1);
    float3 b = LoadWorkingColor(fillIdx - kColorPitch + 1);
    boxMin = min(boxMin, min(a, b));
    boxMax = max(boxMax, max(a, b));
    a = LoadWorkingColor(fillIdx + kColorPitch - 1);
    b = LoadWorkingColor(fillIdx + kColorPitch + 1);
    boxMin = min(boxMin, min(a, b));
    boxMax = max(boxMax, max(a, b));
    a = LoadWorkingColor(holeIdx);
    b = LoadWorkingColor(holeIdx - fillIdx + holeIdx);
    boxMin = min(boxMin, min(a, b));
    boxMax = max(boxMax, max(a, b));
}

void GetBBoxForPixel(
    uint ldsIdx,
    out float3 boxMin,
    out float3 boxMax)
{
    boxMin = float3(kFiniteHdrLimit, kFiniteHdrLimit, kFiniteHdrLimit);
    boxMax = -boxMin;
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
    float3 sum = 0.0;
    float3 sumSquares = 0.0;
#endif
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float3 value = LoadWorkingColor(
                uint(int(ldsIdx) + x + y * int(kColorPitch)));
            boxMin = min(boxMin, value);
            boxMax = max(boxMax, value);
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
            sum += value;
            sumSquares += value * value;
#endif
        }
    }
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
    // Variance-aware YCoCg keeps the ordinary per-pixel min/max envelope as
    // a hard anti-ringing limit, then tightens it continuously around the
    // local distribution. The actual current reconstruction is added by the
    // caller after de-jittering, so a legitimate current subpixel sample can
    // never be excluded by statistics computed from the raw LDS grid.
    float3 mean = sum / 9.0;
    float3 variance = max(sumSquares / 9.0 - mean * mean, 0.0);
    float3 sigma = sqrt(variance);
    float3 varianceMin = mean - 1.5 * sigma - 0.001;
    float3 varianceMax = mean + 1.5 * sigma + 0.001;
    boxMin = max(boxMin, varianceMin);
    boxMax = min(boxMax, varianceMax);
#endif
}

#if TAA_SHARED_WORK_REUSE
void GetBBoxForAdjacentPixels(
    uint leftIdx,
    out float3 leftMin,
    out float3 leftMax,
    out float3 rightMin,
    out float3 rightMax)
{
    // The two 3x3 neighborhoods occupy one shared 4x3 footprint. Keep each
    // pixel's independent bounds while reducing LDS loads from 18 to 12.
    float3 footprint[12];
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 2; ++x)
        {
            footprint[(y + 1) * 4 + (x + 1)] =
                LoadWorkingColor(uint(
                    int(leftIdx) + x + y * int(kColorPitch)));
        }
    }

    leftMin = float3(
        kFiniteHdrLimit,
        kFiniteHdrLimit,
        kFiniteHdrLimit);
    leftMax = -leftMin;
    rightMin = leftMin;
    rightMax = leftMax;
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
    float3 leftSum = 0.0;
    float3 leftSumSquares = 0.0;
    float3 rightSum = 0.0;
    float3 rightSumSquares = 0.0;
#endif
    [unroll]
    for (uint y = 0; y < 3; ++y)
    {
        [unroll]
        for (uint x = 0; x < 3; ++x)
        {
            float3 leftValue = footprint[y * 4 + x];
            float3 rightValue = footprint[y * 4 + x + 1];
            leftMin = min(leftMin, leftValue);
            leftMax = max(leftMax, leftValue);
            rightMin = min(rightMin, rightValue);
            rightMax = max(rightMax, rightValue);
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
            leftSum += leftValue;
            leftSumSquares += leftValue * leftValue;
            rightSum += rightValue;
            rightSumSquares += rightValue * rightValue;
#endif
        }
    }
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
    float3 leftMean = leftSum / 9.0;
    float3 leftSigma = sqrt(max(
        leftSumSquares / 9.0 - leftMean * leftMean,
        0.0));
    leftMin = max(
        leftMin,
        leftMean - 1.5 * leftSigma - 0.001);
    leftMax = min(
        leftMax,
        leftMean + 1.5 * leftSigma + 0.001);

    float3 rightMean = rightSum / 9.0;
    float3 rightSigma = sqrt(max(
        rightSumSquares / 9.0 - rightMean * rightMean,
        0.0));
    rightMin = max(
        rightMin,
        rightMean - 1.5 * rightSigma - 0.001);
    rightMax = min(
        rightMax,
        rightMean + 1.5 * rightSigma + 0.001);
#endif
}
#endif

float4 CatmullRomWeights(float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return float4(
        -0.5 * t + t2 - 0.5 * t3,
        1.0 - 2.5 * t2 + 1.5 * t3,
        0.5 * t + 2.0 * t2 - 1.5 * t3,
        -0.5 * t2 + 0.5 * t3);
}

CatmullRomCross GetCatmullRomCross(float2 pixelPosition)
{
    float2 base = floor(pixelPosition);
    float2 phase = pixelPosition - base;
    float4 wx = CatmullRomWeights(phase.x);
    float4 wy = CatmullRomWeights(phase.y);
    float2 w12 = float2(wx.y + wx.z, wy.y + wy.z);
    float2 offset12 = float2(wx.z, wy.z) / max(w12, 1e-5);
    float2 center = base + offset12;

    CatmullRomCross cross;
    cross.centerPosition = center;
    cross.leftPosition = float2(base.x - 1.0, center.y);
    cross.rightPosition = float2(base.x + 2.0, center.y);
    cross.northPosition = float2(center.x, base.y - 1.0);
    cross.southPosition = float2(center.x, base.y + 2.0);
    cross.northWestPosition = base - 1.0;
    cross.northEastPosition =
        float2(base.x + 2.0, base.y - 1.0);
    cross.southWestPosition =
        float2(base.x - 1.0, base.y + 2.0);
    cross.southEastPosition = base + 2.0;
    cross.centerWeight = w12.x * w12.y;
    cross.leftWeight = wx.x * w12.y;
    cross.rightWeight = wx.w * w12.y;
    cross.northWeight = w12.x * wy.x;
    cross.southWeight = w12.x * wy.w;
    cross.northWestWeight = wx.x * wy.x;
    cross.northEastWeight = wx.w * wy.x;
    cross.southWestWeight = wx.x * wy.w;
    cross.southEastWeight = wx.w * wy.w;
    return cross;
}

float3 ReconstructDeJitteredCurrent(uint ldsIdx)
{
    // Donut's signed PlanarView jitter maps an unjittered output center to
    // current input at center + CurrentJitter. This is the only place current
    // jitter is applied. Catmull-Rom is exact identity for zero jitter because
    // its phase-zero weights are exactly (0, 1, 0, 0).
    int centerX = int(ldsIdx % kColorPitch);
    int centerY = int(ldsIdx / kColorPitch);
    float2 samplePosition = float2(centerX, centerY) + CurrentJitter;
    int2 base = int2(floor(samplePosition));
    float2 phase = samplePosition - float2(base);
    float4 wx = CatmullRomWeights(phase.x);
    float4 wy = CatmullRomWeights(phase.y);

    float3 result = 0.0;
    float3 antiRingMin =
        float3(kFiniteHdrLimit, kFiniteHdrLimit, kFiniteHdrLimit);
    float3 antiRingMax = -antiRingMin;
    [unroll]
    for (int y = 0; y < 4; ++y)
    {
        [unroll]
        for (int x = 0; x < 4; ++x)
        {
            int2 position = base + int2(x - 1, y - 1);
            float3 value = LoadWorkingColor(
                uint(position.x + position.y * int(kColorPitch)));
            result += value * wx[x] * wy[y];
            // Only the positive central 2x2 reconstruction footprint defines
            // the anti-ringing range. Loop coordinates are unrolled, while
            // uniform jitter collapses a zero axis to one column or row.
            // Distant negative-lobe taps cannot legitimize an HDR specular
            // outlier or a color from the far side of a silhouette.
            bool inPositiveX =
                x == 1 ||
                (x == 2 && CurrentJitter.x != 0.0);
            bool inPositiveY =
                y == 1 ||
                (y == 2 && CurrentJitter.y != 0.0);
            if (inPositiveX && inPositiveY)
            {
                antiRingMin = min(antiRingMin, value);
                antiRingMax = max(antiRingMax, value);
            }
        }
    }
    float3 clipped = ClipColor(result, antiRingMin, antiRingMax);
    // Preserve the identity bit-for-bit for callers that intentionally use a
    // zero-jitter phase; the conditional is data selection, not an option
    // branch. Every shipping option remains compile-time specialized.
    return all(CurrentJitter == 0.0)
        ? LoadWorkingColor(ldsIdx)
        : clipped;
}

#if TAA_SHARED_WORK_REUSE
void ReconstructDeJitteredAdjacent(
    uint leftIdx,
    out float3 leftResult,
    out float3 rightResult)
{
    // Adjacent output pixels have the same fractional jitter phase. Their two
    // 4x4 Catmull-Rom footprints therefore form one 5x4 LDS footprint: twenty
    // loads instead of thirty-two, with independent anti-ringing ranges.
    int centerX = int(leftIdx % kColorPitch);
    int centerY = int(leftIdx / kColorPitch);
    float2 samplePosition =
        float2(centerX, centerY) + CurrentJitter;
    int2 base = int2(floor(samplePosition));
    float2 phase = samplePosition - float2(base);
    float4 wx = CatmullRomWeights(phase.x);
    float4 wy = CatmullRomWeights(phase.y);

    float3 footprint[20];
    [unroll]
    for (int y = 0; y < 4; ++y)
    {
        [unroll]
        for (int x = 0; x < 5; ++x)
        {
            int2 position = base + int2(x - 1, y - 1);
            footprint[y * 5 + x] = LoadWorkingColor(uint(
                position.x + position.y * int(kColorPitch)));
        }
    }

    leftResult = 0.0;
    rightResult = 0.0;
    float3 leftMin =
        float3(kFiniteHdrLimit, kFiniteHdrLimit, kFiniteHdrLimit);
    float3 leftMax = -leftMin;
    float3 rightMin = leftMin;
    float3 rightMax = leftMax;
    [unroll]
    for (int y = 0; y < 4; ++y)
    {
        [unroll]
        for (int x = 0; x < 4; ++x)
        {
            float weight = wx[x] * wy[y];
            float3 leftValue = footprint[y * 5 + x];
            float3 rightValue = footprint[y * 5 + x + 1];
            leftResult += leftValue * weight;
            rightResult += rightValue * weight;

            bool inPositiveX =
                x == 1 ||
                (x == 2 && CurrentJitter.x != 0.0);
            bool inPositiveY =
                y == 1 ||
                (y == 2 && CurrentJitter.y != 0.0);
            if (inPositiveX && inPositiveY)
            {
                leftMin = min(leftMin, leftValue);
                leftMax = max(leftMax, leftValue);
                rightMin = min(rightMin, rightValue);
                rightMax = max(rightMax, rightValue);
            }
        }
    }
    leftResult = ClipColor(leftResult, leftMin, leftMax);
    rightResult = ClipColor(rightResult, rightMin, rightMax);
    if (all(CurrentJitter == 0.0))
    {
        leftResult = LoadWorkingColor(leftIdx);
        rightResult = LoadWorkingColor(leftIdx + 1u);
    }
}
#endif

float3 ReconstructCurrent(uint ldsIdx)
{
#if TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DIRECT
    return LoadWorkingColor(ldsIdx);
#else
    return ReconstructDeJitteredCurrent(ldsIdx);
#endif
}

float LinearViewDepth(float deviceDepth)
{
    return UvsrTemporalLinearViewDepth(
        deviceDepth,
        Projection);
}

float DeviceDepthValidity(float deviceDepth)
{
    return UvsrTemporalDeviceDepthValidity(deviceDepth);
}

float LoadViewDepth(uint colorIdx)
{
#if TAA_SHARED_WORK_REUSE
    return ldsViewDepth[ColorIndexToCoreIndex(colorIdx)];
#else
    return LinearViewDepth(LoadDepth(colorIdx));
#endif
}

MotionSelection SelectMotion(uint2 ST, uint ldsIdx)
{
    MotionSelection selection;
    selection.velocity = 0.0;
    selection.currentDeviceDepth = LoadDepth(ldsIdx);
    selection.currentViewDepth = LoadViewDepth(ldsIdx);
    selection.valid = 0.0;

#if TAA_NEEDS_LDS_MOTION
    float4 packedMotion = LoadMotion(ldsIdx);
    // LDS motion was sanitized once by StoreMotion.
    float centerMotionValid = packedMotion.w;
#else
    float4 packedMotion = VelocityBuffer.Load(int3(ST, 0));
    float centerMotionValid = MotionValidity(packedMotion);
#endif
    float centerValid =
        centerMotionValid *
        DeviceDepthValidity(selection.currentDeviceDepth);
    selection.velocity = centerValid > 0.0
        ? packedMotion.xyz
        : 0.0;
    selection.valid = centerValid;

#if TAA_MOTION_SOURCE != UVSR_TAA_MOTION_CENTER
    // Candidates are center, north, south, west, east. Comparing linear view
    // depth makes the nearest-surface rule explicit and correct for reverse-Z.
    // Invalid motion and background depth never become candidates. Strictly
    // smaller comparison preserves center-first tie behavior.
    const int offsets[5] = {
        0,
        -int(kColorPitch),
        int(kColorPitch),
        -1,
        1
    };
    // The extra unit lets a finite surface that clamps to kFarViewDepth win
    // the initial comparison while preserving strict center-first ties.
    float closestViewDepth = kFarViewDepth + 1.0;
    float3 closestVelocity = 0.0;
    float closestDeviceDepth = 0.0;
    float closestValid = 0.0;
    [unroll]
    for (uint index = 0; index < 5; ++index)
    {
        uint candidateIdx = uint(int(ldsIdx) + offsets[index]);
        float candidateDepth = LoadDepth(candidateIdx);
        float4 candidateMotion = LoadMotion(candidateIdx);
        float valid =
            candidateMotion.w *
            DeviceDepthValidity(candidateDepth);
        float3 safeCandidateMotion = valid > 0.0
            ? candidateMotion.xyz
            : 0.0;
        float safeCandidateDepth = valid > 0.0
            ? candidateDepth
            : 0.0;
        float candidateViewDepth = LoadViewDepth(candidateIdx);
        float choose = valid * float(candidateViewDepth < closestViewDepth);
        closestViewDepth = lerp(
            closestViewDepth,
            candidateViewDepth,
            choose);
        closestVelocity = lerp(
            closestVelocity,
            safeCandidateMotion,
            choose);
        closestDeviceDepth = lerp(
            closestDeviceDepth,
            safeCandidateDepth,
            choose);
        closestValid = max(closestValid, choose);
    }

#if TAA_MOTION_SOURCE == UVSR_TAA_MOTION_CLOSEST_CROSS
    selection.velocity = closestVelocity;
    selection.currentDeviceDepth = closestDeviceDepth;
    selection.currentViewDepth = closestViewDepth;
    selection.valid = closestValid;
#else
    // Center-first edge dilation is deliberately narrower than Closest Cross.
    // It retains valid center ownership on coherent surfaces and borrows the
    // nearest cross motion only at a real depth/background silhouette. A flat
    // surface with invalid center motion stays rejected instead of acquiring a
    // neighbor's unrelated object motion.
    float centerDepthValid =
        DeviceDepthValidity(selection.currentDeviceDepth);
    float centerViewDepth = selection.currentViewDepth;
    float relativeDepthDifference =
        abs(centerViewDepth - closestViewDepth) /
        max(min(centerViewDepth, closestViewDepth), 1e-3);
    float hasSilhouette =
        closestValid *
        max(
            1.0 - centerDepthValid,
            smoothstep(0.005, 0.02, relativeDepthDifference));
    float nearestIsForeground =
        float(closestViewDepth + 1e-3 < centerViewDepth);
    float borrowClosest = hasSilhouette * max(
        1.0 - centerValid,
        nearestIsForeground);
    // The smooth edge detector may decide when dilation activates, but motion
    // ownership itself is discrete. Interpolating independently owned vectors
    // creates a synthetic reprojection that belongs to neither surface and
    // visibly accelerates swimming through the threshold region.
    bool useClosest = borrowClosest >= 0.5;
    selection.velocity = useClosest
        ? closestVelocity
        : selection.velocity;
    selection.currentDeviceDepth = useClosest
        ? closestDeviceDepth
        : selection.currentDeviceDepth;
    selection.currentViewDepth = useClosest
        ? closestViewDepth
        : selection.currentViewDepth;
    selection.valid = useClosest
        ? closestValid
        : selection.valid;
#endif
#endif
    return selection;
}

float DepthCoherence(
    float centerView,
    float centerValid,
    float neighborDepth,
    float neighborView)
{
    float valid = centerValid * DeviceDepthValidity(neighborDepth);
    float relativeDifference =
        abs(centerView - neighborView) /
        max(min(centerView, neighborView), 1e-3);
    return valid * (1.0 - smoothstep(0.005, 0.05, relativeDifference));
}

float4 GetCrossDepthSupport(
    uint ldsIdx,
    out float centerView,
    out float centerValid)
{
    float centerDepth = LoadDepth(ldsIdx);
    centerView = LoadViewDepth(ldsIdx);
    centerValid = DeviceDepthValidity(centerDepth);
    return float4(
        DepthCoherence(
            centerView,
            centerValid,
            LoadDepth(ldsIdx - 1),
            LoadViewDepth(ldsIdx - 1)),
        DepthCoherence(
            centerView,
            centerValid,
            LoadDepth(ldsIdx + 1),
            LoadViewDepth(ldsIdx + 1)),
        DepthCoherence(
            centerView,
            centerValid,
            LoadDepth(ldsIdx - kColorPitch),
            LoadViewDepth(ldsIdx - kColorPitch)),
        DepthCoherence(
            centerView,
            centerValid,
            LoadDepth(ldsIdx + kColorPitch),
            LoadViewDepth(ldsIdx + kColorPitch)));
}

float GetCurrentReconstructionDepthSupport(
    uint ldsIdx,
    float4 crossSupport,
    float centerView,
    float centerValid)
{
    // The positive Catmull-Rom footprint is the 2x2 cell containing the
    // jittered sample: center, one X neighbor, one Y neighbor, and (when both
    // axes are nonzero) their diagonal. A cardinal-only gate misses the
    // diagonal and can blend another surface into the current color while
    // motion and depth still belong to center.
    float xActive = float(CurrentJitter.x != 0.0);
    float yActive = float(CurrentJitter.y != 0.0);
    int xOffset = CurrentJitter.x < 0.0 ? -1 : 1;
    int yOffset =
        CurrentJitter.y < 0.0
            ? -int(kColorPitch)
            : int(kColorPitch);
    float xSupport = lerp(
        1.0,
        CurrentJitter.x < 0.0 ? crossSupport.x : crossSupport.y,
        xActive);
    float ySupport = lerp(
        1.0,
        CurrentJitter.y < 0.0 ? crossSupport.z : crossSupport.w,
        yActive);

    float diagonalSupport = DepthCoherence(
        centerView,
        centerValid,
        LoadDepth(uint(int(ldsIdx) + xOffset + yOffset)),
        LoadViewDepth(uint(int(ldsIdx) + xOffset + yOffset)));
    diagonalSupport = lerp(
        1.0,
        diagonalSupport,
        xActive * yActive);
    return min(min(xSupport, ySupport), diagonalSupport);
}

float HistoryDepthFootprintCoherence(
    UvsrTemporalReverseZFootprint footprint)
{
    float valid =
        UvsrTemporalFootprintHasConsistentGeometry(footprint);
    float nearestViewDepth =
        LinearViewDepth(footprint.nearestValidDeviceDepth);
    float farthestViewDepth =
        LinearViewDepth(footprint.farthestValidDeviceDepth);
    float relativeRange =
        abs(farthestViewDepth - nearestViewDepth) /
        max(nearestViewDepth, 1e-3);
    return valid *
        (1.0 - smoothstep(0.005, 0.05, relativeRange));
}

float HistoryTapDepthCoherence(
    float2 historyColorPixel,
    float expectedDeviceDepth,
    float quantizedDeviceDepthDelta,
    float expectedDepthValid)
{
    // Color history is unjittered, while depth history stores the raw
    // previous jittered grid. Match each outer color tap's discrete four-texel
    // footprint by adding the same previous-minus-current jitter delta used by
    // the central history-depth gather. Interpolating raw reverse-Z depth can
    // fabricate a surface at a foreground/background boundary, so each of the
    // outer depth operation is a Gather and is reduced before comparison.
    float2 historyDepthPixel =
        historyColorPixel + CurrentToPreviousJitter;
    float4 tapDeviceDepths = PreDepth.Gather(
        LinearSampler,
        STtoUV(historyDepthPixel));
    UvsrTemporalReverseZFootprint tapFootprint =
        UvsrTemporalReduceReverseZFootprint(tapDeviceDepths);
    float valid =
        expectedDepthValid *
        HistoryPositionInBounds(historyColorPixel) *
        HistoryPositionInBounds(historyDepthPixel);
    float tapViewDepth = LinearViewDepth(
        tapFootprint.farthestValidDeviceDepth);
    float expectedViewDepth =
        LinearViewDepth(expectedDeviceDepth);
    float relativeDifference =
        abs(expectedViewDepth - tapViewDepth) /
        max(min(expectedViewDepth, tapViewDepth), 1e-3);
    return valid *
        UvsrTemporalDepthAccepted(
            expectedDeviceDepth,
            quantizedDeviceDepthDelta,
            SourceDepthPairQuantizationError,
            tapFootprint,
            Projection,
            1e-3) *
        HistoryDepthFootprintCoherence(tapFootprint) *
        (1.0 - smoothstep(0.005, 0.05, relativeDifference));
}

HistorySample RecoverHistory(float4 premultiplied)
{
    HistorySample history;
    // Negative Catmull-Rom lobes can overshoot confidence even when every
    // source alpha is in [0, 1]. Recover color using the filtered raw alpha,
    // but never let that overshoot extrapolate the temporal blend.
    float rawWeight = max(premultiplied.w, 0.0);
    history.weight = saturate(rawWeight);
    history.color =
        SanitizeHdr(premultiplied.rgb / max(rawWeight, 1e-6));
    return history;
}


HistorySample SampleHistory(
    float2 historyPixel,
    uint ldsIdx,
    float4 depthSupport,
    float historyDepthSupport,
    float expectedPreviousDeviceDepth,
    float quantizedDeviceDepthDelta,
    float expectedPreviousDepthValid)
{
#if TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_BILINEAR
    return RecoverHistory(InTemporal.SampleLevel(
        LinearSampler,
        STtoUV(historyPixel),
        0));
#elif TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_ONE_SAMPLE_BICUBIC
    // Exactly one real history-color sample. Historical cardinal colors are
    // estimated from the current LDS cross differences; no additional history
    // sample is hidden in this specialization.
    HistorySample baseHistory = RecoverHistory(InTemporal.SampleLevel(
        LinearSampler,
        STtoUV(historyPixel),
        0));
    CatmullRomCross cross = GetCatmullRomCross(historyPixel);
    float3 currentWest = WorkingToRgb(LoadWorkingColor(ldsIdx - 1));
    float3 currentEast = WorkingToRgb(LoadWorkingColor(ldsIdx + 1));
    float3 currentNorth =
        WorkingToRgb(LoadWorkingColor(ldsIdx - kColorPitch));
    float3 currentSouth =
        WorkingToRgb(LoadWorkingColor(ldsIdx + kColorPitch));
    float3 rawCurrentCenter =
        WorkingToRgb(LoadWorkingColor(ldsIdx));
    // Neighbor estimates are raw-LDS spatial differences. Subtracting a
    // De-Jittered center from raw neighbors injects the reconstruction delta
    // into every cardinal estimate and makes the eight phases move faster.
    float3 estimatedWest =
        baseHistory.color + currentWest - rawCurrentCenter;
    float3 estimatedEast =
        baseHistory.color + currentEast - rawCurrentCenter;
    float3 estimatedNorth =
        baseHistory.color + currentNorth - rawCurrentCenter;
    float3 estimatedSouth =
        baseHistory.color + currentSouth - rawCurrentCenter;

    float centerWeight = cross.centerWeight;
    float westWeight = cross.leftWeight * depthSupport.x;
    float eastWeight = cross.rightWeight * depthSupport.y;
    float northWeight = cross.northWeight * depthSupport.z;
    float southWeight = cross.southWeight * depthSupport.w;
    float normalization = centerWeight +
        westWeight + eastWeight + northWeight + southWeight;
    float3 reconstructed = (
        baseHistory.color * centerWeight +
        estimatedWest * westWeight +
        estimatedEast * eastWeight +
        estimatedNorth * northWeight +
        estimatedSouth * southWeight) /
        max(abs(normalization), 1e-5);

    // The estimated colors are not real history samples. A neighbor rejected
    // by current-depth support must collapse to the real center sample before
    // it participates in anti-ringing bounds; otherwise a zero-weight
    // silhouette estimate can still legitimize an unrelated HDR extreme.
    float3 supportedWest = lerp(
        baseHistory.color,
        estimatedWest,
        depthSupport.x);
    float3 supportedEast = lerp(
        baseHistory.color,
        estimatedEast,
        depthSupport.y);
    float3 supportedNorth = lerp(
        baseHistory.color,
        estimatedNorth,
        depthSupport.z);
    float3 supportedSouth = lerp(
        baseHistory.color,
        estimatedSouth,
        depthSupport.w);
    float3 antiRingMin = min(
        baseHistory.color,
        min(min(supportedWest, supportedEast),
            min(supportedNorth, supportedSouth)));
    float3 antiRingMax = max(
        baseHistory.color,
        max(max(supportedWest, supportedEast),
            max(supportedNorth, supportedSouth)));
    baseHistory.color = ClipColor(
        reconstructed,
        antiRingMin,
        antiRingMax);
    return baseHistory;
#else
    // The 5x option uses the cardinal cross approximation. The 9x option adds
    // all four corner bilinear taps, completing the separable optimized 4x4
    // Catmull-Rom kernel. Every real outer color footprint receives its own
    // discrete reverse-Z depth Gather; no interpolated depth is allowed to
    // fabricate support across a silhouette.
    float2 exactHistoryPixel = round(historyPixel);
    if (all(abs(historyPixel - exactHistoryPixel) < 1e-5))
    {
        int2 exactCoordinate = clamp(
            int2(exactHistoryPixel),
            int2(0, 0),
            int2(BufferDim) - 1);
        return RecoverHistory(
            InTemporal.Load(int3(exactCoordinate, 0)));
    }
    CatmullRomCross cross = GetCatmullRomCross(historyPixel);
    float leftSupport =
        depthSupport.x *
        historyDepthSupport *
        HistoryTapDepthCoherence(
            cross.leftPosition,
            expectedPreviousDeviceDepth,
            quantizedDeviceDepthDelta,
            expectedPreviousDepthValid);
    float rightSupport =
        depthSupport.y *
        historyDepthSupport *
        HistoryTapDepthCoherence(
            cross.rightPosition,
            expectedPreviousDeviceDepth,
            quantizedDeviceDepthDelta,
            expectedPreviousDepthValid);
    float northSupport =
        depthSupport.z *
        historyDepthSupport *
        HistoryTapDepthCoherence(
            cross.northPosition,
            expectedPreviousDeviceDepth,
            quantizedDeviceDepthDelta,
            expectedPreviousDepthValid);
    float southSupport =
        depthSupport.w *
        historyDepthSupport *
        HistoryTapDepthCoherence(
            cross.southPosition,
            expectedPreviousDeviceDepth,
            quantizedDeviceDepthDelta,
            expectedPreviousDepthValid);
#if TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_NINE_TAP_CATMULL_ROM
    float northWestSupport =
        min(depthSupport.x, depthSupport.z) *
        historyDepthSupport *
        HistoryTapDepthCoherence(
            cross.northWestPosition,
            expectedPreviousDeviceDepth,
            quantizedDeviceDepthDelta,
            expectedPreviousDepthValid);
    float northEastSupport =
        min(depthSupport.y, depthSupport.z) *
        historyDepthSupport *
        HistoryTapDepthCoherence(
            cross.northEastPosition,
            expectedPreviousDeviceDepth,
            quantizedDeviceDepthDelta,
            expectedPreviousDepthValid);
    float southWestSupport =
        min(depthSupport.x, depthSupport.w) *
        historyDepthSupport *
        HistoryTapDepthCoherence(
            cross.southWestPosition,
            expectedPreviousDeviceDepth,
            quantizedDeviceDepthDelta,
            expectedPreviousDepthValid);
    float southEastSupport =
        min(depthSupport.y, depthSupport.w) *
        historyDepthSupport *
        HistoryTapDepthCoherence(
            cross.southEastPosition,
            expectedPreviousDeviceDepth,
            quantizedDeviceDepthDelta,
            expectedPreviousDepthValid);
#endif
    float centerWeight = cross.centerWeight;
    float leftWeight = cross.leftWeight * leftSupport;
    float rightWeight = cross.rightWeight * rightSupport;
    float northWeight = cross.northWeight * northSupport;
    float southWeight = cross.southWeight * southSupport;
#if TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_NINE_TAP_CATMULL_ROM
    float northWestWeight =
        cross.northWestWeight * northWestSupport;
    float northEastWeight =
        cross.northEastWeight * northEastSupport;
    float southWestWeight =
        cross.southWestWeight * southWestSupport;
    float southEastWeight =
        cross.southEastWeight * southEastSupport;
#endif

    float4 center = InTemporal.SampleLevel(
        LinearSampler, STtoUV(cross.centerPosition), 0);
    float4 left = InTemporal.SampleLevel(
        LinearSampler, STtoUV(cross.leftPosition), 0);
    float4 right = InTemporal.SampleLevel(
        LinearSampler, STtoUV(cross.rightPosition), 0);
    float4 north = InTemporal.SampleLevel(
        LinearSampler, STtoUV(cross.northPosition), 0);
    float4 south = InTemporal.SampleLevel(
        LinearSampler, STtoUV(cross.southPosition), 0);
#if TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_NINE_TAP_CATMULL_ROM
    float4 northWest = InTemporal.SampleLevel(
        LinearSampler, STtoUV(cross.northWestPosition), 0);
    float4 northEast = InTemporal.SampleLevel(
        LinearSampler, STtoUV(cross.northEastPosition), 0);
    float4 southWest = InTemporal.SampleLevel(
        LinearSampler, STtoUV(cross.southWestPosition), 0);
    float4 southEast = InTemporal.SampleLevel(
        LinearSampler, STtoUV(cross.southEastPosition), 0);
#endif

    float normalization = centerWeight +
        leftWeight + rightWeight + northWeight + southWeight;
#if TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_NINE_TAP_CATMULL_ROM
    normalization += northWestWeight + northEastWeight +
        southWestWeight + southEastWeight;
#endif
    float4 reconstructed = (
        center * centerWeight +
        left * leftWeight +
        right * rightWeight +
        north * northWeight +
        south * southWeight
#if TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_NINE_TAP_CATMULL_ROM
        + northWest * northWestWeight +
        northEast * northEastWeight +
        southWest * southWestWeight +
        southEast * southEastWeight
#endif
        ) /
        max(abs(normalization), 1e-5);
    HistorySample result = RecoverHistory(reconstructed);
    float2 centerDepthPosition =
        cross.centerPosition + CurrentToPreviousJitter;
    result.weight *=
        HistoryPositionInBounds(cross.centerPosition) *
        HistoryPositionInBounds(centerDepthPosition);

    HistorySample centerHistory = RecoverHistory(center);
    HistorySample leftHistory = RecoverHistory(left);
    HistorySample rightHistory = RecoverHistory(right);
    HistorySample northHistory = RecoverHistory(north);
    HistorySample southHistory = RecoverHistory(south);
    // A rejected depth tap must not enlarge the anti-ringing range even after
    // its reconstruction weight has reached zero.
    leftHistory.color = lerp(
        centerHistory.color,
        leftHistory.color,
        leftSupport);
    rightHistory.color = lerp(
        centerHistory.color,
        rightHistory.color,
        rightSupport);
    northHistory.color = lerp(
        centerHistory.color,
        northHistory.color,
        northSupport);
    southHistory.color = lerp(
        centerHistory.color,
        southHistory.color,
        southSupport);
#if TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_NINE_TAP_CATMULL_ROM
    HistorySample northWestHistory = RecoverHistory(northWest);
    HistorySample northEastHistory = RecoverHistory(northEast);
    HistorySample southWestHistory = RecoverHistory(southWest);
    HistorySample southEastHistory = RecoverHistory(southEast);
    northWestHistory.color = lerp(
        centerHistory.color,
        northWestHistory.color,
        northWestSupport);
    northEastHistory.color = lerp(
        centerHistory.color,
        northEastHistory.color,
        northEastSupport);
    southWestHistory.color = lerp(
        centerHistory.color,
        southWestHistory.color,
        southWestSupport);
    southEastHistory.color = lerp(
        centerHistory.color,
        southEastHistory.color,
        southEastSupport);
#endif
    float3 antiRingMin = min(
        centerHistory.color,
        min(min(leftHistory.color, rightHistory.color),
            min(northHistory.color, southHistory.color)));
    float3 antiRingMax = max(
        centerHistory.color,
        max(max(leftHistory.color, rightHistory.color),
            max(northHistory.color, southHistory.color)));
#if TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_NINE_TAP_CATMULL_ROM
    antiRingMin = min(
        antiRingMin,
        min(min(northWestHistory.color, northEastHistory.color),
            min(southWestHistory.color, southEastHistory.color)));
    antiRingMax = max(
        antiRingMax,
        max(max(northWestHistory.color, northEastHistory.color),
            max(southWestHistory.color, southEastHistory.color)));
#endif
    result.color = ClipColor(result.color, antiRingMin, antiRingMax);
    return result;
#endif
}

float FarthestReverseZDeviceDepth(float4 depths)
{
    return UvsrTemporalReduceReverseZFootprint(
        depths).farthestValidDeviceDepth;
}

void WriteDepthOutput(uint2 ST, float value)
{
    OutDepth[ST] = value;
}

float4 RoundTripHalf(float4 value)
{
    return float4(
        f16tof32(f32tof16(value.x)),
        f16tof32(f32tof16(value.y)),
        f16tof32(f32tof16(value.z)),
        f16tof32(f32tof16(value.w)));
}

void WriteTemporalColor(
    uint2 ST,
    float3 resolvedColor,
    float storedWeight)
{
    float4 premultiplied =
        float4(resolvedColor, 1.0) * storedWeight;
    OutTemporal[ST] = premultiplied;
#if TAA_FUSED_OUTPUT
    // The separate resolve reads RGBA16F history, so reproduce that
    // quantization before dividing by alpha. This keeps fused and separate
    // outputs image-equivalent instead of silently changing precision.
    float4 quantized = RoundTripHalf(premultiplied);
    float4 fused = float4(
        SanitizeHdr(
            quantized.rgb / max(quantized.a, 1e-6)),
        1.0);
    OutFusedScene[ST] = fused;
#endif
}

void WriteRejectedCurrent(
    uint2 ST,
    uint ldsIdx,
    float3 currentRgb)
{
    WriteTemporalColor(ST, currentRgb, 0.5);
    WriteDepthOutput(ST, LoadDepth(ldsIdx));
}

bool TemporalBehaviorEnabled(uint flag)
{
    return (TemporalBehaviorFlags & flag) != 0u;
}

struct DelayedPairBounds
{
    float3 minimum0;
    float3 maximum0;
    float3 minimum1;
    float3 maximum1;
    uint ready;
};

void ApplyTemporalBlend(
    uint2 ST,
    uint ldsIdx,
    float3 boxMin,
    float3 boxMax,
    uint pairFillIdx,
    uint pairHoleIdx,
    float3 reusedDeJittered,
    inout DelayedPairBounds delayedPairBounds,
    uint delayedPairSlot,
    bool shareDelayedPairBounds)
{
    if (any(ST >= BufferDim))
        return;

#if TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED || \
    TAA_HISTORY_FILTER != UVSR_TAA_HISTORY_BILINEAR
    // Compute the LDS-only silhouette support once. De-Jittered current and
    // the wider history filters share it.
    float centerViewDepth;
    float centerDepthValid;
    float4 depthSupport = GetCrossDepthSupport(
        ldsIdx,
        centerViewDepth,
        centerDepthValid);
#else
    float4 depthSupport = 1.0;
#endif

#if TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED && \
    TAA_SHARED_WORK_REUSE
    float3 currentWorking = reusedDeJittered;
#else
    float3 currentWorking = ReconstructCurrent(ldsIdx);
#endif
#if TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED
    // Motion and depth still describe the raw center surface. Continuously
    // return to Direct at a depth discontinuity instead of blending a
    // reconstructed color from a different surface identity.
    float reconstructionSupport =
        GetCurrentReconstructionDepthSupport(
            ldsIdx,
            depthSupport,
            centerViewDepth,
            centerDepthValid);
    currentWorking = lerp(
        LoadWorkingColor(ldsIdx),
        currentWorking,
        reconstructionSupport);
#endif
    float3 currentRgb = WorkingToRgb(currentWorking);

    // History validity is uniform for the dispatch. On camera cuts and
    // globally invalid history, return current before any history depth,
    // or color access. This is a data-state branch, not an algorithm-option
    // branch; every shipping option remains a static PSO.
    [branch]
    if (HistoryValid == 0u)
    {
        WriteRejectedCurrent(
            ST,
            ldsIdx,
            currentRgb);
        return;
    }

    MotionSelection motion = SelectMotion(ST, ldsIdx);

    float expectedPreviousDeviceDepth =
        motion.currentDeviceDepth + motion.velocity.z;
    float2 historyColorPixel =
        UvsrTemporalHistoryColorPixel(
            float2(ST),
            motion.velocity.xy);
    float2 historyDepthPixel =
        UvsrTemporalHistoryDepthPixel(
            float2(ST),
            motion.velocity.xy,
            CurrentToPreviousJitter);
    float historyPositionValid =
        HistoryPositionInBounds(historyColorPixel) *
        HistoryPositionInBounds(historyDepthPixel);
    float expectedPreviousDepthValid =
        DeviceDepthValidity(expectedPreviousDeviceDepth) *
        UvsrTemporalDeviceDepthPrecisionValidity(
            expectedPreviousDeviceDepth,
            motion.velocity.z,
            SourceDepthPairQuantizationError);
    float velocitySquared =
        dot(motion.velocity.xy, motion.velocity.xy);
    float speedFactor = TemporalBehaviorEnabled(
            UVSR_TAA_BEHAVIOR_SQUARED_MOTION_TRUST)
        ? saturate(
            1.0 -
            velocitySquared *
                RcpSpeedLimiter * RcpSpeedLimiter)
        : saturate(
            1.0 -
            sqrt(velocitySquared) *
                RcpSpeedLimiter);
    float preliminaryAcceptance =
        motion.valid *
        historyPositionValid *
        expectedPreviousDepthValid *
        speedFactor;

    // Motion, both history coordinates, legal reverse-Z depth, and the speed
    // limiter are all known before any previous-frame texture access. This
    // mandatory shared gate is correctness-neutral: an exact-zero candidate
    // could never contribute history later in the resolver.
    [branch]
    if (preliminaryAcceptance == 0.0)
    {
        WriteRejectedCurrent(
            ST,
            ldsIdx,
            currentRgb);
        return;
    }

    float historyDepthSupport = 1.0;
    float reprojectionAcceptance = 0.0;
    [branch]
    if (TemporalBehaviorEnabled(
            UVSR_TAA_BEHAVIOR_MOVING_POINT_DEPTH))
    {
        const bool stationary =
            velocitySquared <= 1e-4 &&
            abs(motion.velocity.z) <= 1e-6;
        if (stationary)
        {
            // Minimum's strongest stability trade: a nominally stationary
            // silhouette keeps history instead of allowing projection jitter
            // to alternate a four-texel footprint between geometry and clear.
            reprojectionAcceptance = 1.0;
        }
        else
        {
            int2 depthPixel = clamp(
                int2(floor(historyDepthPixel + 0.5)),
                int2(0, 0),
                int2(BufferDim) - 1);
            float previousDeviceDepth =
                PreDepth.Load(int3(depthPixel, 0));
            float depthTolerance = max(
                max(5e-4, SourceDepthPairQuantizationError),
                max(
                    abs(previousDeviceDepth),
                    abs(expectedPreviousDeviceDepth)) *
                    0.01);
            reprojectionAcceptance =
                UvsrTemporalDeviceDepthValidity(
                    previousDeviceDepth) *
                float(
                    abs(
                        previousDeviceDepth -
                        expectedPreviousDeviceDepth) <=
                    depthTolerance);
        }
    }
    else
    {
        // The robust policy validates the complete bilinear/Gather footprint.
        float4 historyDeviceDepths = PreDepth.Gather(
            LinearSampler,
            STtoUV(historyDepthPixel));
        UvsrTemporalReverseZFootprint historyDepthFootprint =
            UvsrTemporalReduceReverseZFootprint(
                historyDeviceDepths);
#if TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_FIVE_TAP_CATMULL_ROM || \
    TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_NINE_TAP_CATMULL_ROM
        historyDepthSupport = HistoryDepthFootprintCoherence(
            historyDepthFootprint);
#endif
        reprojectionAcceptance =
            UvsrTemporalDepthAccepted(
                expectedPreviousDeviceDepth,
                motion.velocity.z,
                SourceDepthPairQuantizationError,
                historyDepthFootprint,
                Projection,
                1e-3);
    }

    reprojectionAcceptance *=
        float(HistoryValid != 0u) *
        motion.valid *
        historyPositionValid *
        expectedPreviousDepthValid;
    float hardAcceptance =
        speedFactor * reprojectionAcceptance;

#if TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
    // Motion validity, viewport bounds, the central history-depth footprint,
    // and speed rejection are all known before history color, outer five-tap
    // depth, or rectification work. Only the exact-zero case exits; partial
    // confidence keeps the identical continuous path.
    [branch]
    if (hardAcceptance == 0.0)
    {
        WriteRejectedCurrent(
            ST,
            ldsIdx,
            currentRgb);
        return;
    }
#endif

#if TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
    // Bounds are deliberately delayed until after the exact-zero exit.
    if (shareDelayedPairBounds)
    {
        if (delayedPairBounds.ready == 0u)
        {
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_PAIR_RGB
            GetBBoxForPair(
                pairFillIdx,
                pairHoleIdx,
                delayedPairBounds.minimum0,
                delayedPairBounds.maximum0);
            delayedPairBounds.minimum1 =
                delayedPairBounds.minimum0;
            delayedPairBounds.maximum1 =
                delayedPairBounds.maximum0;
#else
            GetBBoxForAdjacentPixels(
                pairFillIdx,
                delayedPairBounds.minimum0,
                delayedPairBounds.maximum0,
                delayedPairBounds.minimum1,
                delayedPairBounds.maximum1);
#endif
            delayedPairBounds.ready = 1u;
        }
        boxMin = delayedPairSlot == 0u
            ? delayedPairBounds.minimum0
            : delayedPairBounds.minimum1;
        boxMax = delayedPairSlot == 0u
            ? delayedPairBounds.maximum0
            : delayedPairBounds.maximum1;
    }
    else
    {
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_PAIR_RGB
        GetBBoxForPair(
            pairFillIdx,
            pairHoleIdx,
            boxMin,
            boxMax);
#else
        GetBBoxForPixel(ldsIdx, boxMin, boxMax);
#endif
    }
#endif

    HistorySample history =
        SampleHistory(
            historyColorPixel,
            ldsIdx,
            depthSupport,
            historyDepthSupport,
            expectedPreviousDeviceDepth,
            motion.velocity.z,
            expectedPreviousDepthValid);
    float historyConfidence =
        TemporalBehaviorEnabled(
            UVSR_TAA_BEHAVIOR_IMMEDIATE_HISTORY_WEIGHT)
            ? MaximumHistoryWeight
            : history.weight;
    float baseHistoryWeight = min(
        historyConfidence * hardAcceptance,
        MaximumHistoryWeight);

    float3 temporalWorking = RgbToWorking(history.color);

    // Rectification must contain the current sample that is actually blended.
    // This is an identity for Direct and aligns De-Jittered with its bounds.
    boxMin = min(boxMin, currentWorking);
    boxMax = max(boxMax, currentWorking);

#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
    // Expand the plain-YCoCg range before line clipping. This controls
    // chroma anti-ringing without the hue shifts of component clamping.
    float3 boxRange = boxMax - boxMin;
    boxMin -= boxRange * 0.125 + 0.001;
    boxMax += boxRange * 0.125 + 0.001;
#endif

    temporalWorking = TemporalBehaviorEnabled(
            UVSR_TAA_BEHAVIOR_TIGHT_RECTIFICATION)
        ? clamp(temporalWorking, boxMin, boxMax)
        : ClipColor(
            temporalWorking,
            boxMin,
            boxMax,
            lerp(1.0, 4.0, speedFactor * speedFactor));
    float3 temporalRgb = WorkingToRgb(temporalWorking);

    // Strength is applied only after every ownership, bounds, reverse-Z, and
    // disocclusion gate. Values above 100% can reinforce accepted
    // history but cannot resurrect a rejected sample or exceed the logical
    // frame-horizon cap.
    float finalHistoryWeight = min(
        baseHistoryWeight * clamp(TemporalBlendFactor, 0.0, 2.0),
        MaximumHistoryWeight);

    float3 temporalColor = TemporalBehaviorEnabled(
            UVSR_TAA_BEHAVIOR_LINEAR_BLEND_DOMAIN)
        ? lerp(
            currentRgb,
            temporalRgb,
            finalHistoryWeight)
        : ITM(lerp(
            TM(currentRgb),
            TM(temporalRgb),
            finalHistoryWeight));

    // Presentation strength must not feed back into Temporal AA's stored
    // confidence; only accepted history advances the recurrence.
    float storedWeight =
        saturate(rcp(2.0 - baseHistoryWeight));
    storedWeight = min(storedWeight, MaximumHistoryWeight);
    storedWeight = f16tof32(f32tof16(storedWeight));

    WriteTemporalColor(ST, temporalColor, storedWeight);
    WriteDepthOutput(ST, LoadDepth(ldsIdx));
}

[numthreads(8, 8, 1)]
void main(
    uint3 DTid : SV_DispatchThreadID,
    uint GI : SV_GroupIndex,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint threadCount = 64;
    const int2 groupOrigin =
        int2(
            Gid.x * kOutputTileWidth,
            (Gid.y + DispatchGroupYOffset) *
                kOutputTileHeight);

    const uint colorBlockColumns = kColorPitch / 2;
    const uint colorBlockRows = kColorRows / 2;
    const uint colorBlockCount =
        colorBlockColumns * colorBlockRows;
    for (uint i = GI; i < colorBlockCount; i += threadCount)
    {
        uint x = (i % colorBlockColumns) * 2;
        uint y = (i / colorBlockColumns) * 2;
        uint topLeftIdx = x + y * kColorPitch;
        int2 tileTopLeft =
            groupOrigin - int(kColorBorder) + int2(x, y);
        int2 gatherBoundary = tileTopLeft + 1;
        float2 uv = RcpBufferDim * float2(gatherBoundary);

        float4 r4 = InColor.GatherRed(LinearSampler, uv);
        float4 g4 = InColor.GatherGreen(LinearSampler, uv);
        float4 b4 = InColor.GatherBlue(LinearSampler, uv);
        StoreWorkingColor(topLeftIdx, float3(r4.w, g4.w, b4.w));
        StoreWorkingColor(
            topLeftIdx + 1,
            float3(r4.z, g4.z, b4.z));
        StoreWorkingColor(
            topLeftIdx + kColorPitch,
            float3(r4.x, g4.x, b4.x));
        StoreWorkingColor(
            topLeftIdx + 1 + kColorPitch,
            float3(r4.y, g4.y, b4.y));
    }

    // Split layouts load the one-pixel depth/motion core independently from
    // the wider color reconstruction tile. Legacy retains the same core size
    // as color, preserving the original footprint for comparison.
    const uint coreBlockColumns = kCorePitch / 2;
    const uint coreBlockRows = kCoreRows / 2;
    const uint coreBlockCount =
        coreBlockColumns * coreBlockRows;
    for (uint i = GI; i < coreBlockCount; i += threadCount)
    {
        uint x = (i % coreBlockColumns) * 2;
        uint y = (i / coreBlockColumns) * 2;
        uint topLeftIdx = x + y * kCorePitch;
        int2 tileTopLeft =
            groupOrigin - int(kCoreBorder) + int2(x, y);
        int2 gatherBoundary = tileTopLeft + 1;
        float2 uv = RcpBufferDim * float2(gatherBoundary);

        float4 depths = CurDepth.Gather(LinearSampler, uv);
        ldsDepth[topLeftIdx + 0] = depths.w;
        ldsDepth[topLeftIdx + 1] = depths.z;
        ldsDepth[topLeftIdx + kCorePitch] = depths.x;
        ldsDepth[topLeftIdx + 1 + kCorePitch] = depths.y;
#if TAA_SHARED_WORK_REUSE
        ldsViewDepth[topLeftIdx + 0] =
            LinearViewDepth(depths.w);
        ldsViewDepth[topLeftIdx + 1] =
            LinearViewDepth(depths.z);
        ldsViewDepth[topLeftIdx + kCorePitch] =
            LinearViewDepth(depths.x);
        ldsViewDepth[topLeftIdx + 1 + kCorePitch] =
            LinearViewDepth(depths.y);
#endif

#if TAA_NEEDS_LDS_MOTION
        int2 maximumPixel = int2(BufferDim) - 1;
        int2 p00 = clamp(tileTopLeft, int2(0, 0), maximumPixel);
        int2 p10 = clamp(
            tileTopLeft + int2(1, 0),
            int2(0, 0),
            maximumPixel);
        int2 p01 = clamp(
            tileTopLeft + int2(0, 1),
            int2(0, 0),
            maximumPixel);
        int2 p11 = clamp(
            tileTopLeft + int2(1, 1),
            int2(0, 0),
            maximumPixel);
        StoreMotion(
            topLeftIdx,
            VelocityBuffer.Load(int3(p00, 0)));
        StoreMotion(
            topLeftIdx + 1,
            VelocityBuffer.Load(int3(p10, 0)));
        StoreMotion(
            topLeftIdx + kCorePitch,
            VelocityBuffer.Load(int3(p01, 0)));
        StoreMotion(
            topLeftIdx + 1 + kCorePitch,
            VelocityBuffer.Load(int3(p11, 0)));
#endif
    }

    GroupMemoryBarrierWithGroupSync();

    uint idx0 =
        GTid.x * 2 +
        GTid.y * kColorPitch +
        kColorBorder * kColorPitch +
        kColorBorder;
    uint idx1 = idx0 + 1;
    uint2 st0 = uint2(groupOrigin) +
        uint2(GTid.x * 2, GTid.y);
    uint2 st1 = st0 + uint2(1, 0);
    float3 reusedDeJittered0 = 0.0;
    float3 reusedDeJittered1 = 0.0;
    DelayedPairBounds delayedPairBounds =
        (DelayedPairBounds)0;
#if TAA_SHARED_WORK_REUSE && \
    TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED
    ReconstructDeJitteredAdjacent(
        idx0,
        reusedDeJittered0,
        reusedDeJittered1);
#endif
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_PAIR_RGB
    float3 pairMin;
    float3 pairMax;
#if TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
    pairMin = 0.0;
    pairMax = 0.0;
#else
    GetBBoxForPair(idx0, idx1, pairMin, pairMax);
#endif
    ApplyTemporalBlend(
        st0,
        idx0,
        pairMin,
        pairMax,
        idx0,
        idx1,
        reusedDeJittered0,
        delayedPairBounds,
        0u,
        true);
    ApplyTemporalBlend(
        st1,
        idx1,
        pairMin,
        pairMax,
        idx0,
        idx1,
        reusedDeJittered1,
        delayedPairBounds,
        1u,
        true);
#else
    float3 boxMin0;
    float3 boxMax0;
#if TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
    boxMin0 = 0.0;
    boxMax0 = 0.0;
    float3 boxMin1 = 0.0;
    float3 boxMax1 = 0.0;
#else
#if TAA_SHARED_WORK_REUSE
    float3 boxMin1;
    float3 boxMax1;
    GetBBoxForAdjacentPixels(
        idx0,
        boxMin0,
        boxMax0,
        boxMin1,
        boxMax1);
#else
    GetBBoxForPixel(idx0, boxMin0, boxMax0);
#endif

#if !TAA_SHARED_WORK_REUSE
    float3 boxMin1;
    float3 boxMax1;
    GetBBoxForPixel(idx1, boxMin1, boxMax1);
#endif
#endif
    ApplyTemporalBlend(
        st0,
        idx0,
        boxMin0,
        boxMax0,
        idx0,
        idx1,
        reusedDeJittered0,
        delayedPairBounds,
        0u,
        true);
    ApplyTemporalBlend(
        st1,
        idx1,
        boxMin1,
        boxMax1,
        idx0,
        idx1,
        reusedDeJittered1,
        delayedPairBounds,
        1u,
        true);
#endif
}
