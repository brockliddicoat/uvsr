//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
//
// The base pair resolver follows Microsoft MiniEngine commit
// 357ade6ec6ff0d9dcadc48f35c7a28e37c0cdf7a. UVSR adaptations cover NVRHI,
// RGBA16F motion validity, arbitrary dimensions, infinite reverse-Z depth, and
// the compile-time experiment dimensions declared below.
//

#pragma pack_matrix(row_major)

#include "taa_miniengine_options_shared.h"
#include "temporal_aa_common.hlsli"
#include <donut/shaders/view_cb.h>

#ifndef TAA_PIXEL_SHADER
#define TAA_PIXEL_SHADER 0
#endif

#ifndef TAA_MOTION_SOURCE
#error TAA_MOTION_SOURCE must be a compile-time shader define
#endif
#ifndef TAA_CURRENT_RECONSTRUCTION
#error TAA_CURRENT_RECONSTRUCTION must be a compile-time shader define
#endif
#ifndef TAA_INTERIOR_WEIGHTING
#error TAA_INTERIOR_WEIGHTING must be a compile-time shader define
#endif
#ifndef TAA_HISTORY_FILTER
#error TAA_HISTORY_FILTER must be a compile-time shader define
#endif
#ifndef TAA_RECTIFICATION
#error TAA_RECTIFICATION must be a compile-time shader define
#endif
#ifndef TAA_EXPORT_SELECTIVE
#error TAA_EXPORT_SELECTIVE must be a compile-time shader define
#endif
#ifndef TAA_SAMPLE_RESURRECTION
#error TAA_SAMPLE_RESURRECTION must be a compile-time shader define
#endif
#ifndef TAA_COMPUTE_KERNEL
#error TAA_COMPUTE_KERNEL must be a compile-time shader define
#endif
#ifndef TAA_LDS_LAYOUT
#error TAA_LDS_LAYOUT must be a compile-time shader define
#endif
#ifndef TAA_SHARED_WORK_REUSE
#error TAA_SHARED_WORK_REUSE must be a compile-time shader define
#endif
#ifndef TAA_EARLY_HISTORY_REJECTION
#error TAA_EARLY_HISTORY_REJECTION must be a compile-time shader define
#endif
#ifndef TAA_FUSED_OUTPUT
#error TAA_FUSED_OUTPUT must be a compile-time shader define
#endif
#ifndef TAA_DEVELOPER_DEBUG
#error TAA_DEVELOPER_DEBUG must be a compile-time shader define
#endif

// Resurrection must inspect exact-zero immediate confidence. An early return
// would remove precisely the pixels this mode exists to recover, so even an
// accidentally requested early-rejection permutation resolves to the safe
// non-early algorithm at compile time.
#if TAA_EARLY_HISTORY_REJECTION && \
    TAA_SAMPLE_RESURRECTION == UVSR_TAA_SAMPLE_RESURRECTION_OFF
#define TAA_EFFECTIVE_EARLY_HISTORY_REJECTION 1
#else
#define TAA_EFFECTIVE_EARLY_HISTORY_REJECTION 0
#endif

#if TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED || \
    TAA_EXPORT_SELECTIVE
static const uint kColorBorder = 2;
#else
static const uint kColorBorder = 1;
#endif

#if TAA_MOTION_SOURCE == UVSR_TAA_MOTION_CLOSEST_CROSS || \
    TAA_MOTION_SOURCE == UVSR_TAA_MOTION_CENTER_FIRST_EDGE_DILATION || \
    TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
#define TAA_NEEDS_LDS_MOTION 1
#else
#define TAA_NEEDS_LDS_MOTION 0
#endif

static const uint kOutputTileWidth = 16;
static const uint kOutputTileHeight = 8;
#if TAA_PIXEL_SHADER
#define kColorPitch (BufferDim.x + 4u)
#define kColorRows (BufferDim.y + 4u)
#define kCorePitch kColorPitch
#define kCoreRows kColorRows
#else
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
#endif
static const float kFarViewDepth = 65504.0;
static const float kFiniteHdrLimit = 65504.0;
// Stable Interior is a clarity adjustment, not a second rejection path.
// Seven-eighths history is a conservative absolute floor: enough current
// signal to reduce interior blur without exposing the eight jitter phases.
static const float kStableInteriorFloor =
    UVSR_TAA_STABLE_INTERIOR_FLOOR;

#if TAA_PIXEL_SHADER
static float4 PixelOutTemporal;
static float PixelOutDepth;
static float2 PixelOutMoments;
static float2 PixelOutDebug;
static float4 PixelOutSmaaCurrent;
static float PixelOutSmaaRejection;
static float4 PixelOutFusedScene;
#else
RWTexture2D<float4> OutTemporal : register(u0);
RWTexture2D<float> OutDepth : register(u1);
RWTexture2D<float2> OutMoments : register(u2);
#if TAA_DEVELOPER_DEBUG
RWTexture2D<float2> OutDebug : register(u3);
#endif
RWTexture2D<float4> OutSmaaCurrent : register(u4);
RWTexture2D<float> OutSmaaRejection : register(u5);
#if TAA_FUSED_OUTPUT
RWTexture2D<float4> OutFusedScene : register(u6);
#endif
#endif

Texture2D<float4> VelocityBuffer : register(t0);
Texture2D<float3> InColor : register(t1);
Texture2D<float4> InTemporal : register(t2);
Texture2D<float> CurDepth : register(t3);
Texture2D<float> PreDepth : register(t4);
Texture2D<float2> InMoments : register(t5);
#if TAA_SAMPLE_RESURRECTION
Texture2D<float4> PersistentColor0 : register(t6);
Texture2D<float> PersistentDepth0 : register(t7);
Texture2D<float4> PersistentColor1 : register(t8);
Texture2D<float> PersistentDepth1 : register(t9);
#endif

SamplerState LinearSampler : register(s0);

#if !TAA_PIXEL_SHADER
groupshared float ldsDepth[kCorePixelCount];
#if TAA_SHARED_WORK_REUSE
groupshared float ldsViewDepth[kCorePixelCount];
#if TAA_COMPUTE_KERNEL == UVSR_TAA_KERNEL_16X8_ONE_PIXEL && \
    (TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED || \
        TAA_EXPORT_SELECTIVE)
groupshared float3 ldsReconstructedCurrent[
    kOutputTileWidth * kOutputTileHeight];
#endif
#if TAA_COMPUTE_KERNEL == UVSR_TAA_KERNEL_16X8_ONE_PIXEL && \
    TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
groupshared float2 ldsLocalLumaRange[
    kOutputTileWidth * kOutputTileHeight];
#endif
#if TAA_COMPUTE_KERNEL == UVSR_TAA_KERNEL_16X8_ONE_PIXEL && \
    !TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
groupshared float3 ldsReusedBoxMin[
    kOutputTileWidth * kOutputTileHeight];
groupshared float3 ldsReusedBoxMax[
    kOutputTileWidth * kOutputTileHeight];
#endif
#endif
groupshared float ldsR[kColorPixelCount];
groupshared float ldsG[kColorPixelCount];
groupshared float ldsB[kColorPixelCount];
#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
groupshared float ldsLuma[kCorePixelCount];
#endif
#if TAA_NEEDS_LDS_MOTION
#if TAA_LDS_LAYOUT == UVSR_TAA_LDS_SPLIT_PACKED
groupshared uint2 ldsPackedMotion[kCorePixelCount];
#else
groupshared float ldsMotionX[kCorePixelCount];
groupshared float ldsMotionY[kCorePixelCount];
groupshared float ldsMotionZ[kCorePixelCount];
groupshared float ldsMotionValidity[kCorePixelCount];
#endif
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
    uint2 DepthQuantizationPadding;
#if TAA_SAMPLE_RESURRECTION
    // The resolved persistent color and raw persistent depth occupy different
    // grids. These captured views preserve the producing camera and jitter so
    // resurrection can use NRA-RTAA v1's repaired world reprojection: resolved
    // color uses the current phase once, while raw depth uses the stored phase
    // already embedded in MatWorldToClip.
    PlanarViewConstants CurrentView;
    PlanarViewConstants ImmediateHistoryView;
    PlanarViewConstants PersistentHistoryView0;
    PlanarViewConstants PersistentHistoryView1;
    uint PersistentValidMask;
    uint3 PersistentPadding;
#endif
}

struct MotionSelection
{
    float3 velocity;
    // Motion dilation may replace currentDeviceDepth with the depth owned by
    // the selected motion sample. Resurrection must instead reconstruct the
    // immutable center pixel's geometry so a borrowed foreground depth is
    // never paired with the center ray across a silhouette.
    float centerDeviceDepth;
    float currentDeviceDepth;
    float currentViewDepth;
    float valid;
};

struct HistorySample
{
    float3 color;
    float weight;
};

#if TAA_SAMPLE_RESURRECTION
struct ResurrectionCandidate
{
    float3 color;
    float confidence;
    float match;
    uint sourceIndex;
};
#endif

struct CatmullRomCross
{
    float2 centerPosition;
    float2 leftPosition;
    float2 rightPosition;
    float2 northPosition;
    float2 southPosition;
    float centerWeight;
    float leftWeight;
    float rightWeight;
    float northWeight;
    float southWeight;
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
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_PER_PIXEL_YCOCG || \
    TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
    return RgbToYCoCg(rgb);
#else
    return rgb;
#endif
}

float3 WorkingToRgb(float3 working)
{
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_PER_PIXEL_YCOCG || \
    TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
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

#if !TAA_PIXEL_SHADER
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
#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
#endif
}

#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
void StoreLuma(uint coreIdx, float3 rgb)
{
    // Stable Interior evaluates overlapping 3x3 neighborhoods for both
    // horizontally paired outputs. Cache luma once per core-tile sample and
    // use the same Rec.709 signal in every color-space specialization.
    ldsLuma[coreIdx] = RGBToLuminance(SanitizeHdr(rgb));
}
#endif
#endif

float3 LoadWorkingColor(uint colorIdx)
{
#if TAA_PIXEL_SHADER
    int2 pixel = int2(
        int(colorIdx % kColorPitch) - 2,
        int(colorIdx / kColorPitch) - 2);
    pixel = clamp(pixel, int2(0, 0), int2(BufferDim) - 1);
    return RgbToWorking(SanitizeHdr(
        InColor.Load(int3(pixel, 0))));
#else
    return float3(
        ldsR[colorIdx],
        ldsG[colorIdx],
        ldsB[colorIdx]);
#endif
}

float LoadDepth(uint colorIdx)
{
#if TAA_PIXEL_SHADER
    int2 pixel = int2(
        int(colorIdx % kColorPitch) - 2,
        int(colorIdx / kColorPitch) - 2);
    pixel = clamp(pixel, int2(0, 0), int2(BufferDim) - 1);
    return CurDepth.Load(int3(pixel, 0));
#else
    return ldsDepth[ColorIndexToCoreIndex(colorIdx)];
#endif
}

#if TAA_NEEDS_LDS_MOTION
#if !TAA_PIXEL_SHADER
void StoreMotion(uint coreIdx, float4 packedMotion)
{
    // Sanitize once while loading the tile. Invalid NaN/Inf motion must not
    // contaminate later lerps or divergence math even when its validity
    // multiplier is zero (IEEE NaN multiplied by zero remains NaN).
    float valid = MotionValidity(packedMotion);
    float3 safeMotion = valid > 0.0
        ? packedMotion.xyz
        : 0.0;
#if TAA_LDS_LAYOUT == UVSR_TAA_LDS_SPLIT_PACKED
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
#endif

float4 LoadMotion(uint colorIdx)
{
#if TAA_PIXEL_SHADER
    int2 pixel = int2(
        int(colorIdx % kColorPitch) - 2,
        int(colorIdx / kColorPitch) - 2);
    pixel = clamp(pixel, int2(0, 0), int2(BufferDim) - 1);
    float4 motion = VelocityBuffer.Load(int3(pixel, 0));
    float valid = MotionValidity(motion);
    return float4(
        valid > 0.0 ? motion.xyz : 0.0,
        valid);
#elif TAA_LDS_LAYOUT == UVSR_TAA_LDS_SPLIT_PACKED
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
    // MiniEngine history color, depth, and moments all use linear sampling or
    // Gather. Require their real footprint instead of accepting a clamped
    // half-pixel excursion beyond the viewport.
    return UvsrTemporalLinearFootprintInBounds(ST, BufferDim);
}

// These are MiniEngine's local reversible blend-domain transforms, not a
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
    // This is the original shared horizontal-pair MiniEngine neighborhood.
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
    cross.centerWeight = w12.x * w12.y;
    cross.leftWeight = wx.x * w12.y;
    cross.rightWeight = wx.w * w12.y;
    cross.northWeight = w12.x * wy.x;
    cross.southWeight = w12.x * wy.w;
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

#if TAA_SHARED_WORK_REUSE && !TAA_PIXEL_SHADER
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
#if TAA_SHARED_WORK_REUSE && !TAA_PIXEL_SHADER
    return ldsViewDepth[ColorIndexToCoreIndex(colorIdx)];
#else
    return LinearViewDepth(LoadDepth(colorIdx));
#endif
}

MotionSelection SelectMotion(uint2 ST, uint ldsIdx)
{
    MotionSelection selection;
    selection.velocity = 0.0;
    selection.centerDeviceDepth = LoadDepth(ldsIdx);
    selection.currentDeviceDepth = selection.centerDeviceDepth;
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

float VelocityCoherence(float4 center, uint neighborIdx)
{
#if TAA_NEEDS_LDS_MOTION
    float4 neighbor = LoadMotion(neighborIdx);
    // StoreMotion already made invalid vectors finite and normalized W to
    // zero or one, so coherence needs no repeated finite-value tests.
    float valid = center.w * neighbor.w;
    float divergence = length(center.xy - neighbor.xy);
    return valid * (1.0 - smoothstep(0.25, 2.0, divergence));
#else
    return 1.0;
#endif
}

#if TAA_SHARED_WORK_REUSE && !TAA_PIXEL_SHADER && \
    TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
void GetLocalLumaRangesForAdjacent(
    uint leftIdx,
    out float2 leftRange,
    out float2 rightRange)
{
    // The two 3x3 luma neighborhoods share one 4x3 core footprint. Luma was
    // already transformed and sanitized during tile loading, so this performs
    // twelve scalar LDS reads rather than eighteen repeated color transforms.
    uint leftCoreIdx = ColorIndexToCoreIndex(leftIdx);
    float footprint[12];
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 2; ++x)
        {
            footprint[(y + 1) * 4 + x + 1] =
                ldsLuma[uint(
                    int(leftCoreIdx) + x +
                    y * int(kCorePitch))];
        }
    }

    leftRange = float2(kFiniteHdrLimit, -kFiniteHdrLimit);
    rightRange = leftRange;
    [unroll]
    for (uint y = 0u; y < 3u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 3u; ++x)
        {
            float leftLuma = footprint[y * 4u + x];
            float rightLuma = footprint[y * 4u + x + 1u];
            leftRange.x = min(leftRange.x, leftLuma);
            leftRange.y = max(leftRange.y, leftLuma);
            rightRange.x = min(rightRange.x, rightLuma);
            rightRange.y = max(rightRange.y, rightLuma);
        }
    }
}
#endif

float LocalLumaCoherence(
    uint ldsIdx,
    float currentLuma,
    float2 reusedLumaRange)
{
#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
    // Include the actual current reconstruction in the range. Direct is an
    // exact identity. De-Jittered can differ from the raw LDS center, and
    // omitting it made confidence describe one footprint while blending
    // another.
    float minimumLuma = currentLuma;
    float maximumLuma = currentLuma;
#if TAA_SHARED_WORK_REUSE && !TAA_PIXEL_SHADER
    minimumLuma = min(minimumLuma, reusedLumaRange.x);
    maximumLuma = max(maximumLuma, reusedLumaRange.y);
#else
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
#if TAA_PIXEL_SHADER
            float luma = RGBToLuminance(WorkingToRgb(
                LoadWorkingColor(uint(
                    int(ldsIdx) + x +
                    y * int(kColorPitch)))));
#else
            uint coreIdx = ColorIndexToCoreIndex(ldsIdx);
            float luma = ldsLuma[
                uint(int(coreIdx) + x + y * int(kCorePitch))];
#endif
            minimumLuma = min(minimumLuma, luma);
            maximumLuma = max(maximumLuma, luma);
        }
    }
#endif
    float relativeRange =
        (maximumLuma - minimumLuma) / max(abs(currentLuma), 0.1);
    return 1.0 - smoothstep(0.05, 0.5, relativeRange);
#else
    return 1.0;
#endif
}

float TemporalLumaCoherence(float currentLuma, float2 previousMoments)
{
#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
    if (!all(isfinite(previousMoments)))
        return 0.0;

    float previousMean = clamp(
        previousMoments.x,
        -kFiniteHdrLimit,
        kFiniteHdrLimit);
    float previousSecondMoment = clamp(
        previousMoments.y,
        0.0,
        kFiniteHdrLimit);
    float previousVariance = max(
        previousSecondMoment - previousMean * previousMean,
        0.0);
    float lumaScale = max(
        max(abs(currentLuma), abs(previousMean)),
        0.1);
    float tolerance =
        sqrt(previousVariance) + 0.02 * lumaScale + 0.001;
    float normalizedDifference =
        abs(currentLuma - previousMean) / tolerance;

    // Phase-varying specular energy must preserve MiniEngine's baseline
    // history instead of repeatedly turning the clarity adjustment on and off.
    return 1.0 - smoothstep(1.0, 4.0, normalizedDifference);
#else
    return 1.0;
#endif
}

float StableInteriorScore(
    uint ldsIdx,
    float currentLuma,
    float2 previousMoments,
    float expectedPreviousDepth,
    float temporalDepth,
    float4 depthSupport,
    float2 reusedLumaRange)
{
#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
    // Stable means coherent in every cardinal direction. An average can
    // remain high with one discontinuous neighbor, which mislabels a straight
    // silhouette as an interior and reintroduces phase-dependent edge weight.
    float depthCoherence = min(
        min(depthSupport.x, depthSupport.y),
        min(depthSupport.z, depthSupport.w));

    // Reuse the center motion for all four divergence comparisons.
    float4 centerMotion = LoadMotion(ldsIdx);
    float velocityCoherence = min(
        min(
            VelocityCoherence(centerMotion, ldsIdx - 1),
            VelocityCoherence(centerMotion, ldsIdx + 1)),
        min(
            VelocityCoherence(
                centerMotion,
                ldsIdx - kColorPitch),
            VelocityCoherence(
                centerMotion,
                ldsIdx + kColorPitch)));

    float lumaCoherence =
        LocalLumaCoherence(
            ldsIdx,
            currentLuma,
            reusedLumaRange) *
        TemporalLumaCoherence(currentLuma, previousMoments);
    float depthAgreementError =
        abs(expectedPreviousDepth - temporalDepth) /
        max(expectedPreviousDepth, 1e-3);
    float historyDepthAgreement =
        1.0 - smoothstep(0.002, 0.05, depthAgreementError);

    // Product is continuous, remains in [0, 1], and requires all four requested
    // coherence categories to agree. The local-luma term also contains the
    // temporal-moment guard, so phase-varying HDR edges cannot be mistaken for
    // a stable interior. Low confidence does not reject edge history; it
    // leaves MiniEngine's accepted weight unchanged.
    return saturate(
        depthCoherence *
        velocityCoherence *
        lumaCoherence *
        historyDepthAgreement);
#else
    return 1.0;
#endif
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
    // four existing outer depth operations is a Gather and is reduced before
    // comparison.
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

#if TAA_SAMPLE_RESURRECTION
bool DeviceDepthToViewDepth(
    float deviceDepth,
    PlanarViewConstants view,
    out float viewDepth)
{
    float denominator =
        deviceDepth * view.matViewToClip[2][3] -
        view.matViewToClip[2][2];
    if (!isfinite(denominator) || abs(denominator) <= 1e-8)
    {
        viewDepth = kFarViewDepth;
        return false;
    }

    viewDepth = abs(
        (view.matViewToClip[3][2] -
            deviceDepth * view.matViewToClip[3][3]) /
        denominator);
    return isfinite(viewDepth);
}

bool ReconstructWorldPosition(
    float2 pixelCenter,
    float deviceDepth,
    PlanarViewConstants view,
    out float3 worldPosition)
{
    float2 clipXY =
        pixelCenter * view.windowToClipScale +
        view.windowToClipBias;
    float4 world = mul(
        float4(clipXY, deviceDepth, 1.0),
        view.matClipToWorld);
    if (!all(isfinite(world)) || abs(world.w) <= 1e-8)
    {
        worldPosition = 0.0;
        return false;
    }

    worldPosition = world.xyz / world.w;
    return all(isfinite(worldPosition));
}

bool ProjectWorldToHistory(
    float3 worldPosition,
    PlanarViewConstants view,
    out float2 resolvedColorCenter,
    out float2 rawDepthCenter,
    out float expectedDeviceDepth)
{
    // This is the repaired NRA-RTAA v1 coordinate split. Resolved history
    // retains the current subpixel phase exactly once. Raw depth is projected
    // through the stored jittered view and therefore receives no extra offset.
    float4 clipNoOffset = mul(
        float4(worldPosition, 1.0),
        view.matWorldToClipNoOffset);
    float4 clipWithOffset = mul(
        float4(worldPosition, 1.0),
        view.matWorldToClip);
    if (!all(isfinite(clipNoOffset)) ||
        !all(isfinite(clipWithOffset)) ||
        clipNoOffset.w <= 1e-8 ||
        clipWithOffset.w <= 1e-8)
    {
        resolvedColorCenter = 0.0;
        rawDepthCenter = 0.0;
        expectedDeviceDepth = 0.0;
        return false;
    }

    float2 noOffsetNdc =
        clipNoOffset.xy / clipNoOffset.w;
    float2 rawNdc =
        clipWithOffset.xy / clipWithOffset.w;
    resolvedColorCenter =
        noOffsetNdc * view.clipToWindowScale +
        view.clipToWindowBias +
        CurrentJitter;
    rawDepthCenter =
        rawNdc * view.clipToWindowScale +
        view.clipToWindowBias;
    expectedDeviceDepth =
        clipWithOffset.z / clipWithOffset.w;
    return all(isfinite(resolvedColorCenter)) &&
        all(isfinite(rawDepthCenter)) &&
        isfinite(expectedDeviceDepth);
}

float PixelCenterInBounds(float2 pixelCenter)
{
    bool2 lowerInside = pixelCenter >= 0.5;
    bool2 upperInside =
        pixelCenter <= float2(BufferDim) - 0.5;
    return float(
        lowerInside.x & lowerInside.y &
        upperInside.x & upperInside.y);
}

float PersistentDepthConfidence(
    float storedDeviceDepth,
    float expectedDeviceDepth,
    PlanarViewConstants storedView)
{
    float storedViewDepth;
    float expectedViewDepth;
    float valid =
        DeviceDepthValidity(storedDeviceDepth) *
        DeviceDepthValidity(expectedDeviceDepth) *
        float(DeviceDepthToViewDepth(
            storedDeviceDepth,
            storedView,
            storedViewDepth)) *
        float(DeviceDepthToViewDepth(
            expectedDeviceDepth,
            storedView,
            expectedViewDepth));
    float relativeError =
        abs(storedViewDepth - expectedViewDepth) /
        max(min(storedViewDepth, expectedViewDepth), 1e-3);
    return valid *
        (1.0 - smoothstep(0.002, 0.02, relativeError));
}

ResurrectionCandidate SamplePersistentCandidate(
    Texture2D<float4> colorTexture,
    Texture2D<float> depthTexture,
    PlanarViewConstants storedView,
    float3 currentWorld,
    float slotValid,
    uint sourceIndex)
{
    ResurrectionCandidate candidate =
        (ResurrectionCandidate)0;
    candidate.sourceIndex = sourceIndex;
    if (slotValid == 0.0)
        return candidate;

    float2 colorCenter;
    float2 rawDepthCenter;
    float expectedDeviceDepth;
    if (!ProjectWorldToHistory(
            currentWorld,
            storedView,
            colorCenter,
            rawDepthCenter,
            expectedDeviceDepth) ||
        PixelCenterInBounds(colorCenter) == 0.0 ||
        PixelCenterInBounds(rawDepthCenter) == 0.0)
    {
        return candidate;
    }

    // Point-load raw depth. Linear depth sampling across a reverse-Z
    // foreground/background boundary can fabricate a surface that never
    // existed and was the old experiment's most dangerous false acceptance.
    int2 rawDepthPixel = clamp(
        int2(floor(rawDepthCenter)),
        int2(0, 0),
        int2(BufferDim) - 1);
    float storedDeviceDepth =
        depthTexture.Load(int3(rawDepthPixel, 0));
    float depthConfidence = PersistentDepthConfidence(
        storedDeviceDepth,
        expectedDeviceDepth,
        storedView);
    if (depthConfidence == 0.0)
        return candidate;

    HistorySample recovered = RecoverHistory(
        colorTexture.SampleLevel(
            LinearSampler,
            colorCenter * RcpBufferDim,
            0));
    candidate.color = recovered.color;
    candidate.confidence =
        recovered.weight * depthConfidence;
    return candidate;
}
#endif

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
    // Five real bilinear history-color fetches in the cross-shaped
    // approximation of optimized 9-tap Catmull-Rom. The four corner fetches
    // are removed. Directional current-depth coherence suppresses the wider
    // lobes at silhouettes. The scalar history-depth support comes from the
    // existing 2x2 gather and prevents a moving old silhouette from
    // contaminating the wider outer taps. Four additional discrete depth
    // Gathers match the four actual outer color footprints; this preserves the
    // existing four-operation cost while preventing reverse-Z interpolation
    // from fabricating support between geometry and background.
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
    float centerWeight = cross.centerWeight;
    float leftWeight = cross.leftWeight * leftSupport;
    float rightWeight = cross.rightWeight * rightSupport;
    float northWeight = cross.northWeight * northSupport;
    float southWeight = cross.southWeight * southSupport;

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

    float normalization = centerWeight +
        leftWeight + rightWeight + northWeight + southWeight;
    float4 reconstructed = (
        center * centerWeight +
        left * leftWeight +
        right * rightWeight +
        north * northWeight +
        south * southWeight) /
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
    float3 antiRingMin = min(
        centerHistory.color,
        min(min(leftHistory.color, rightHistory.color),
            min(northHistory.color, southHistory.color)));
    float3 antiRingMax = max(
        centerHistory.color,
        max(max(leftHistory.color, rightHistory.color),
            max(northHistory.color, southHistory.color)));
    result.color = ClipColor(result.color, antiRingMin, antiRingMax);
    return result;
#endif
}

float FarthestReverseZDeviceDepth(float4 depths)
{
    return UvsrTemporalReduceReverseZFootprint(
        depths).farthestValidDeviceDepth;
}

#if TAA_EXPORT_SELECTIVE
float3 GetSelectiveSmaaCurrent(
    uint ldsIdx,
    float3 reusedDeJittered)
{
#if TAA_SHARED_WORK_REUSE && !TAA_PIXEL_SHADER && \
    TAA_COMPUTE_KERNEL == UVSR_TAA_KERNEL_8X8_TWO_PIXELS
    float3 smaaCurrentWorking = reusedDeJittered;
#elif TAA_SHARED_WORK_REUSE && !TAA_PIXEL_SHADER && \
    TAA_COMPUTE_KERNEL == UVSR_TAA_KERNEL_16X8_ONE_PIXEL
    float3 smaaCurrentWorking = reusedDeJittered;
#else
    float3 smaaCurrentWorking =
        ReconstructDeJitteredCurrent(ldsIdx);
#endif
    // Selective SMAA is presentation-only and must follow an unjittered pixel
    // center even at silhouettes. ReconstructDeJitteredCurrent and its shared
    // adjacent variant already apply their positive-footprint anti-ringing;
    // returning to the raw jittered center here made rejected edges swim.
    return WorkingToRgb(smaaCurrentWorking);
}

void WriteSelectiveSmaa(
    uint2 ST,
    float3 current,
    float acceptedHistoryWeight)
{
#if TAA_PIXEL_SHADER
    PixelOutSmaaCurrent = float4(current, 1.0);
#else
    OutSmaaCurrent[ST] = float4(current, 1.0);
#endif
    // MiniEngine confidence starts at 0.5 and reaches 0.8 after four accepted
    // contributions. Smoothly remap that per-pixel recurrence to an exact
    // zero instead of combining a binary reprojection step with the global
    // frame count. The old binary mask made a phase-varying silhouette switch
    // an entire dilated region between spatial current and temporal output.
    float rejection = 1.0 - smoothstep(
        UVSR_TAA_SELECTIVE_HISTORY_MINIMUM,
        UVSR_TAA_SELECTIVE_HISTORY_TRUSTED,
        saturate(acceptedHistoryWeight));
    rejection = saturate(
        (rejection - UVSR_TAA_SELECTIVE_REJECTION_FLOOR) /
        (1.0 - UVSR_TAA_SELECTIVE_REJECTION_FLOOR));
#if TAA_PIXEL_SHADER
    PixelOutSmaaRejection = rejection;
#else
    OutSmaaRejection[ST] = rejection;
#endif
}
#endif

void WriteDepthOutput(uint2 ST, float value)
{
#if TAA_PIXEL_SHADER
    PixelOutDepth = value;
#else
    OutDepth[ST] = value;
#endif
}

void WriteMomentOutput(uint2 ST, float2 value)
{
#if TAA_PIXEL_SHADER
    PixelOutMoments = value;
#else
    OutMoments[ST] = value;
#endif
}

void WriteDebugOutput(uint2 ST, float2 value)
{
#if TAA_DEVELOPER_DEBUG
#if TAA_PIXEL_SHADER
    PixelOutDebug = value;
#else
    OutDebug[ST] = value;
#endif
#endif
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
#if TAA_PIXEL_SHADER
    PixelOutTemporal = premultiplied;
#else
    OutTemporal[ST] = premultiplied;
#endif
#if TAA_FUSED_OUTPUT
    // The separate resolve reads RGBA16F history, so reproduce that
    // quantization before dividing by alpha. This keeps fused and separate
    // outputs image-equivalent instead of silently changing precision.
    float4 quantized = RoundTripHalf(premultiplied);
    float4 fused = float4(
        SanitizeHdr(
            quantized.rgb / max(quantized.a, 1e-6)),
        1.0);
#if TAA_PIXEL_SHADER
    PixelOutFusedScene = fused;
#else
    OutFusedScene[ST] = fused;
#endif
#endif
}

void WriteRejectedCurrent(
    uint2 ST,
    uint ldsIdx,
    float3 currentRgb,
    float currentLuma,
    float3 reusedDeJittered)
{
    WriteTemporalColor(ST, currentRgb, 0.5);
    WriteDepthOutput(ST, CurDepth[ST]);
#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
    WriteMomentOutput(ST, float2(
        currentLuma,
        min(currentLuma * currentLuma, kFiniteHdrLimit)));
#endif
    WriteDebugOutput(ST, float2(0.0, 0.0));
#if TAA_EXPORT_SELECTIVE
    WriteSelectiveSmaa(
        ST,
        GetSelectiveSmaaCurrent(
            ldsIdx,
            reusedDeJittered),
        0.0);
#endif
}

void ApplyTemporalBlend(
    uint2 ST,
    uint ldsIdx,
    float3 boxMin,
    float3 boxMax,
    uint pairFillIdx,
    uint pairHoleIdx,
    float3 reusedDeJittered,
    float2 reusedLumaRange)
{
    if (any(ST >= BufferDim))
        return;

#if TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED || \
    TAA_EXPORT_SELECTIVE || \
    TAA_SAMPLE_RESURRECTION || \
    TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE || \
    TAA_HISTORY_FILTER != UVSR_TAA_HISTORY_BILINEAR
    // Compute the LDS-only silhouette support once. De-Jittered, Stable
    // Interior, and the wider history filters share it.
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
    TAA_SHARED_WORK_REUSE && !TAA_PIXEL_SHADER
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
    float currentLuma = clamp(
        RGBToLuminance(currentRgb),
        -kFiniteHdrLimit,
        kFiniteHdrLimit);

    // History validity is uniform for the dispatch. On camera cuts and
    // globally invalid history, return current before any history depth,
    // color, or moment texture access. This is a data-state branch, not an
    // algorithm-option branch; every shipping option remains a static PSO.
    [branch]
    if (HistoryValid == 0u)
    {
        WriteRejectedCurrent(
            ST,
            ldsIdx,
            currentRgb,
            currentLuma,
            reusedDeJittered);
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
    float speedFactor =
        saturate(
            1.0 -
            length(motion.velocity.xy) *
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
            currentRgb,
            currentLuma,
            reusedDeJittered);
        return;
    }

    // One history-depth gather is retained for every color-filter
    // specialization. The five-tap color mode never adds five depth fetches.
    float4 historyDeviceDepths = PreDepth.Gather(
        LinearSampler,
        STtoUV(historyDepthPixel));
    UvsrTemporalReverseZFootprint historyDepthFootprint =
        UvsrTemporalReduceReverseZFootprint(
            historyDeviceDepths);
    float temporalDeviceDepth =
        historyDepthFootprint.farthestValidDeviceDepth;
    float expectedPreviousDepth =
        LinearViewDepth(expectedPreviousDeviceDepth);
    float rawTemporalDepth = LinearViewDepth(temporalDeviceDepth);
#if TAA_HISTORY_FILTER == UVSR_TAA_HISTORY_FIVE_TAP_CATMULL_ROM
    float historyDepthSupport = HistoryDepthFootprintCoherence(
        historyDepthFootprint);
#else
    float historyDepthSupport = 1.0;
#endif

    float reprojectionAcceptance =
        UvsrTemporalDepthAccepted(
            expectedPreviousDeviceDepth,
            motion.velocity.z,
            SourceDepthPairQuantizationError,
            historyDepthFootprint,
            Projection,
            1e-3) *
        float(HistoryValid != 0u) *
        motion.valid *
        historyPositionValid *
        expectedPreviousDepthValid;
    float hardAcceptance =
        speedFactor * reprojectionAcceptance;

#if TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
    // Motion validity, viewport bounds, the central history-depth footprint,
    // and speed rejection are all known before history color, outer five-tap
    // depth, moments, or rectification work. Only the exact-zero case exits;
    // partial confidence keeps the identical continuous path.
    [branch]
    if (hardAcceptance == 0.0)
    {
        WriteRejectedCurrent(
            ST,
            ldsIdx,
            currentRgb,
            currentLuma,
            reusedDeJittered);
        return;
    }
#endif

#if TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
    // Bounds are deliberately delayed until after the exact-zero exit.
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_PAIR_RGB
    GetBBoxForPair(
        pairFillIdx,
        pairHoleIdx,
        boxMin,
        boxMax);
#else
    GetBBoxForPixel(ldsIdx, boxMin, boxMax);
#endif
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
    float baseHistoryWeight = min(
        history.weight * hardAcceptance,
        MaximumHistoryWeight);
    float resurrectionPresentationTrust = 0.0;
    float resurrectionDebugValue = 0.0;

#if TAA_SAMPLE_RESURRECTION
    // Copy NRA-RTAA v1's repaired resurrection contract closely: reconstruct
    // the current world point, verify that actual current-to-previous motion
    // agrees with camera-only motion, and independently project/validate each
    // older color/depth pair in its captured view. The old screen-aligned
    // sample and 0.05-pixel motion cutoff made camera turns a guaranteed no-op.
    bool resurrectionRequested =
        baseHistoryWeight < 0.2 &&
        motion.valid > 0.0 &&
        centerDepthValid > 0.0 &&
        PersistentValidMask != 0u;
    if (resurrectionRequested)
        resurrectionDebugValue = 0.25;

    float3 currentWorld = 0.0;
    bool currentWorldValid =
        resurrectionRequested &&
        ReconstructWorldPosition(
            float2(ST) + 0.5,
            motion.centerDeviceDepth,
            CurrentView,
            currentWorld);
    float staticSurfaceForResurrection = 0.0;
    if (currentWorldValid &&
        expectedPreviousDepthValid > 0.0)
    {
        float2 cameraOnlyColorCenter;
        float2 cameraOnlyDepthCenter;
        float cameraOnlyDeviceDepth;
        if (ProjectWorldToHistory(
                currentWorld,
                ImmediateHistoryView,
                cameraOnlyColorCenter,
                cameraOnlyDepthCenter,
                cameraOnlyDeviceDepth))
        {
            float cameraMotionError = length(
                cameraOnlyColorCenter -
                (float2(ST) + 0.5 + motion.velocity.xy));
            float cameraDepthAgreement =
                PersistentDepthConfidence(
                    expectedPreviousDeviceDepth,
                    cameraOnlyDeviceDepth,
                    ImmediateHistoryView);
            staticSurfaceForResurrection =
                cameraMotionError <= 0.125 &&
                cameraDepthAgreement > 0.5
                    ? 1.0
                    : 0.0;
        }
    }

    [branch]
    if (currentWorldValid &&
        staticSurfaceForResurrection > 0.0)
    {
        ResurrectionCandidate candidate0 =
            SamplePersistentCandidate(
            PersistentColor0,
            PersistentDepth0,
            PersistentHistoryView0,
            currentWorld,
            float((PersistentValidMask & 1u) != 0u),
            1u);
#if TAA_SAMPLE_RESURRECTION == \
    UVSR_TAA_SAMPLE_RESURRECTION_TWO_OLDER_FRAMES
        ResurrectionCandidate candidate1 =
            SamplePersistentCandidate(
            PersistentColor1,
            PersistentDepth1,
            PersistentHistoryView1,
            currentWorld,
            float((PersistentValidMask & 2u) != 0u),
            2u);
#else
        ResurrectionCandidate candidate1 =
            (ResurrectionCandidate)0;
        candidate1.sourceIndex = 2u;
#endif

        float3 candidateRange = max(
            boxMax - boxMin,
            abs(currentWorking) * 0.05 + 0.01);
        float3 candidate0Working =
            RgbToWorking(candidate0.color);
        float3 candidate1Working =
            RgbToWorking(candidate1.color);
        float3 candidate0Units =
            abs(candidate0Working - currentWorking) /
            candidateRange;
        float3 candidate1Units =
            abs(candidate1Working - currentWorking) /
            candidateRange;
        candidate0.match = saturate(
            1.0 - max(
                candidate0Units.x,
                max(candidate0Units.y, candidate0Units.z)));
        candidate1.match = saturate(
            1.0 - max(
                candidate1Units.x,
                max(candidate1Units.y, candidate1Units.z)));
        candidate0.confidence *= candidate0.match;
        candidate1.confidence *= candidate1.match;

        ResurrectionCandidate best = candidate0;
        if (candidate1.confidence > candidate0.confidence)
            best = candidate1;

        static const float kResurrectionMatchThreshold = 0.70;
        static const float kResurrectionMaximumWeight = 0.20;
        if (best.confidence > baseHistoryWeight &&
            best.match >= kResurrectionMatchThreshold)
        {
            // Older support is clipped more aggressively than immediate
            // history, matching v1's half-range rectification rule.
            float3 boxCenter = (boxMin + boxMax) * 0.5;
            float3 aggressiveMin =
                lerp(boxCenter, boxMin, 0.5);
            float3 aggressiveMax =
                lerp(boxCenter, boxMax, 0.5);
            float3 clippedWorking = ClipColor(
                RgbToWorking(best.color),
                aggressiveMin,
                aggressiveMax);
            history.color = WorkingToRgb(clippedWorking);
            float resurrectedWeight = min(
                best.confidence,
                kResurrectionMaximumWeight);
            baseHistoryWeight = resurrectedWeight;
            resurrectionPresentationTrust = best.confidence;
            resurrectionDebugValue =
                (best.sourceIndex == 1u ? 0.50 : 0.75) +
                resurrectedWeight;
        }
    }
#endif

    float3 temporalWorking = RgbToWorking(history.color);

    // Rectification must contain the current sample that is actually blended.
    // This is an identity for Direct and aligns De-Jittered with its bounds.
    boxMin = min(boxMin, currentWorking);
    boxMax = max(boxMax, currentWorking);

#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_PER_PIXEL_YCOCG || \
    TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG
    // Expand the plain-YCoCg range before line clipping. This controls
    // chroma anti-ringing without the hue shifts of component clamping.
    float3 boxRange = boxMax - boxMin;
    boxMin -= boxRange * 0.125 + 0.001;
    boxMax += boxRange * 0.125 + 0.001;
#endif

    temporalWorking = ClipColor(
        temporalWorking,
        boxMin,
        boxMax,
        lerp(1.0, 4.0, speedFactor * speedFactor));
    float3 temporalRgb = WorkingToRgb(temporalWorking);

    float finalHistoryWeight = baseHistoryWeight;

    float2 previousMoments = 0.0;
#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
    // Stable consumes the moment read that was previously dead shipping work.
    // Other compile-time permutations neither sample nor write moment history;
    // selecting Stable resets it before first use.
    previousMoments = InMoments.SampleLevel(
        LinearSampler,
        STtoUV(historyColorPixel),
        0);
#endif
    float stableInteriorScore = StableInteriorScore(
        ldsIdx,
        currentLuma,
        previousMoments,
        expectedPreviousDepth,
        rawTemporalDepth,
        depthSupport,
        reusedLumaRange);
    stableInteriorScore *= historyPositionValid;
#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
    float reductionTarget =
        min(baseHistoryWeight, kStableInteriorFloor);
    finalHistoryWeight = lerp(
        baseHistoryWeight,
        reductionTarget,
        stableInteriorScore);
#endif
    // MiniEngine's temporal blend factor is exposed as a normalized strength.
    // Apply it last so it can only reduce history that survived every
    // ownership, bounds, reverse-Z, disocclusion, and clarity gate.
    finalHistoryWeight *= saturate(TemporalBlendFactor);

    float3 temporalColor = ITM(lerp(
        TM(currentRgb),
        TM(temporalRgb),
        finalHistoryWeight));

#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
    float2 currentMoments = float2(
        currentLuma,
        min(currentLuma * currentLuma, kFiniteHdrLimit));
    float2 outputMoments = lerp(
        currentMoments,
        previousMoments,
        finalHistoryWeight);
    WriteMomentOutput(ST, outputMoments);
#endif

    // Optional clarity weighting must not feed back into MiniEngine's stored
    // confidence. Encoding finalHistoryWeight here caused a recursive collapse
    // to ~13% history even for a perfectly stable pixel.
    float storedWeight =
        saturate(rcp(2.0 - baseHistoryWeight));
    storedWeight = min(storedWeight, MaximumHistoryWeight);
    storedWeight = f16tof32(f32tof16(storedWeight));

    WriteTemporalColor(ST, temporalColor, storedWeight);
    WriteDepthOutput(ST, CurDepth[ST]);
    // Developer diagnostics share one RG16 target: stable-interior score in X
    // and compact resurrection status/source/contribution in Y.
    WriteDebugOutput(
        ST,
        float2(
#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
            stableInteriorScore,
#else
            0.0,
#endif
            resurrectionDebugValue));
#if TAA_EXPORT_SELECTIVE
    // Selective SMAA consumes de-jittered current only as a presentation
    // source. It is deliberately separate from OutTemporal and can never be
    // sampled by the next temporal frame.
    WriteSelectiveSmaa(
        ST,
        GetSelectiveSmaaCurrent(
            ldsIdx,
            reusedDeJittered),
        max(baseHistoryWeight, resurrectionPresentationTrust));
#endif
}

#if !TAA_PIXEL_SHADER
#if TAA_COMPUTE_KERNEL == UVSR_TAA_KERNEL_8X8_TWO_PIXELS
[numthreads(8, 8, 1)]
#else
[numthreads(16, 8, 1)]
#endif
void main(
    uint3 DTid : SV_DispatchThreadID,
    uint GI : SV_GroupIndex,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
#if TAA_COMPUTE_KERNEL == UVSR_TAA_KERNEL_8X8_TWO_PIXELS
    const uint threadCount = 64;
#else
    const uint threadCount = 128;
#endif
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

#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
        [unroll]
        for (uint localY = 0; localY < 2; ++localY)
        {
            [unroll]
            for (uint localX = 0; localX < 2; ++localX)
            {
                int relativeX =
                    int(x + localX) - int(kColorBorder);
                int relativeY =
                    int(y + localY) - int(kColorBorder);
                if (relativeX >= -int(kCoreBorder) &&
                    relativeY >= -int(kCoreBorder) &&
                    relativeX <
                        int(kOutputTileWidth + kCoreBorder) &&
                    relativeY <
                        int(kOutputTileHeight + kCoreBorder))
                {
                    uint colorIdx =
                        topLeftIdx + localX +
                        localY * kColorPitch;
                    uint coreIdx =
                        uint(relativeX + int(kCoreBorder)) +
                        uint(relativeY + int(kCoreBorder)) *
                            kCorePitch;
                    StoreLuma(
                        coreIdx,
                        WorkingToRgb(LoadWorkingColor(colorIdx)));
                }
            }
        }
#endif
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

#if TAA_COMPUTE_KERNEL == UVSR_TAA_KERNEL_8X8_TWO_PIXELS
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
    float2 reusedLumaRange0 = 0.0;
    float2 reusedLumaRange1 = 0.0;
#if TAA_SHARED_WORK_REUSE && \
    (TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED || \
        TAA_EXPORT_SELECTIVE)
    ReconstructDeJitteredAdjacent(
        idx0,
        reusedDeJittered0,
        reusedDeJittered1);
#endif
#if TAA_SHARED_WORK_REUSE && \
    TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
    GetLocalLumaRangesForAdjacent(
        idx0,
        reusedLumaRange0,
        reusedLumaRange1);
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
        reusedLumaRange0);
    ApplyTemporalBlend(
        st1,
        idx1,
        pairMin,
        pairMax,
        idx0,
        idx1,
        reusedDeJittered1,
        reusedLumaRange1);
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
        reusedLumaRange0);
    ApplyTemporalBlend(
        st1,
        idx1,
        boxMin1,
        boxMax1,
        idx0,
        idx1,
        reusedDeJittered1,
        reusedLumaRange1);
#endif
#else
    uint idx =
        GTid.x +
        GTid.y * kColorPitch +
        kColorBorder * kColorPitch +
        kColorBorder;
    uint2 st = uint2(groupOrigin) + GTid.xy;
    float3 reusedDeJittered = 0.0;
    float2 reusedLumaRange = 0.0;
#if TAA_SHARED_WORK_REUSE && \
    (TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED || \
        TAA_EXPORT_SELECTIVE || \
        TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE || \
        !TAA_EFFECTIVE_EARLY_HISTORY_REJECTION)
    if ((GTid.x & 1u) == 0u)
    {
        uint pairIdx =
            GTid.x +
            GTid.y * kColorPitch +
            kColorBorder * kColorPitch +
            kColorBorder;
#if TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED || \
    TAA_EXPORT_SELECTIVE
        float3 leftReconstruction;
        float3 rightReconstruction;
        ReconstructDeJitteredAdjacent(
            pairIdx,
            leftReconstruction,
            rightReconstruction);
        uint pairOutputIndex =
            GTid.y * kOutputTileWidth + GTid.x;
        ldsReconstructedCurrent[pairOutputIndex] =
            leftReconstruction;
        ldsReconstructedCurrent[pairOutputIndex + 1u] =
            rightReconstruction;
#endif
#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
        float2 leftLumaRange;
        float2 rightLumaRange;
        GetLocalLumaRangesForAdjacent(
            pairIdx,
            leftLumaRange,
            rightLumaRange);
        uint pairLumaIndex =
            GTid.y * kOutputTileWidth + GTid.x;
        ldsLocalLumaRange[pairLumaIndex] =
            leftLumaRange;
        ldsLocalLumaRange[pairLumaIndex + 1u] =
            rightLumaRange;
#endif
#if !TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
        float3 leftBoxMin;
        float3 leftBoxMax;
        float3 rightBoxMin;
        float3 rightBoxMax;
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_PAIR_RGB
        GetBBoxForPair(
            pairIdx,
            pairIdx + 1u,
            leftBoxMin,
            leftBoxMax);
        rightBoxMin = leftBoxMin;
        rightBoxMax = leftBoxMax;
#else
        GetBBoxForAdjacentPixels(
            pairIdx,
            leftBoxMin,
            leftBoxMax,
            rightBoxMin,
            rightBoxMax);
#endif
        uint pairBoxIndex =
            GTid.y * kOutputTileWidth + GTid.x;
        ldsReusedBoxMin[pairBoxIndex] = leftBoxMin;
        ldsReusedBoxMax[pairBoxIndex] = leftBoxMax;
        ldsReusedBoxMin[pairBoxIndex + 1u] = rightBoxMin;
        ldsReusedBoxMax[pairBoxIndex + 1u] = rightBoxMax;
#endif
    }
    GroupMemoryBarrierWithGroupSync();
    uint outputIndex =
        GTid.y * kOutputTileWidth + GTid.x;
#if TAA_CURRENT_RECONSTRUCTION == UVSR_TAA_CURRENT_DEJITTERED || \
    TAA_EXPORT_SELECTIVE
    reusedDeJittered =
        ldsReconstructedCurrent[outputIndex];
#endif
#if TAA_INTERIOR_WEIGHTING == UVSR_TAA_INTERIOR_STABLE
    reusedLumaRange = ldsLocalLumaRange[outputIndex];
#endif
#endif
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_PAIR_RGB
    uint pairOffset = GTid.x & ~1u;
    uint pairIdx0 =
        pairOffset +
        GTid.y * kColorPitch +
        kColorBorder * kColorPitch +
        kColorBorder;
    float3 pairMin;
    float3 pairMax;
#if TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
    pairMin = 0.0;
    pairMax = 0.0;
#else
#if TAA_SHARED_WORK_REUSE
    pairMin = ldsReusedBoxMin[outputIndex];
    pairMax = ldsReusedBoxMax[outputIndex];
#else
    GetBBoxForPair(
        pairIdx0,
        pairIdx0 + 1,
        pairMin,
        pairMax);
#endif
#endif
    ApplyTemporalBlend(
        st,
        idx,
        pairMin,
        pairMax,
        pairIdx0,
        pairIdx0 + 1,
        reusedDeJittered,
        reusedLumaRange);
#else
    float3 boxMin;
    float3 boxMax;
#if TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
    boxMin = 0.0;
    boxMax = 0.0;
#else
#if TAA_SHARED_WORK_REUSE
    boxMin = ldsReusedBoxMin[outputIndex];
    boxMax = ldsReusedBoxMax[outputIndex];
#else
    GetBBoxForPixel(idx, boxMin, boxMax);
#endif
#endif
    ApplyTemporalBlend(
        st,
        idx,
        boxMin,
        boxMax,
        idx,
        idx,
        reusedDeJittered,
        reusedLumaRange);
#endif
#endif
}
#else
struct TaaPixelOutputs
{
    float4 temporal : SV_Target0;
    float depth : SV_Target1;
    float2 moments : SV_Target2;
    float2 debugValue : SV_Target3;
    float4 selectiveCurrent : SV_Target4;
    float selectiveRejection : SV_Target5;
    float4 fusedScene : SV_Target6;
};

TaaPixelOutputs main(float4 position : SV_Position)
{
    uint2 ST = uint2(position.xy);
    PixelOutTemporal = 0.0;
    PixelOutDepth = 0.0;
    PixelOutMoments = 0.0;
    PixelOutDebug = 0.0;
    PixelOutSmaaCurrent = 0.0;
    PixelOutSmaaRejection = 0.0;
    PixelOutFusedScene = 0.0;

    uint ldsIdx =
        ST.x + 2u +
        (ST.y + 2u) * kColorPitch;
#if TAA_RECTIFICATION == UVSR_TAA_RECTIFICATION_PAIR_RGB
    uint pairX = ST.x & ~1u;
    uint pairFillIdx =
        pairX + 2u +
        (ST.y + 2u) * kColorPitch;
    uint pairHoleIdx = pairFillIdx + 1u;
    float3 boxMin = 0.0;
    float3 boxMax = 0.0;
#if !TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
    GetBBoxForPair(
        pairFillIdx,
        pairHoleIdx,
        boxMin,
        boxMax);
#endif
    ApplyTemporalBlend(
        ST,
        ldsIdx,
        boxMin,
        boxMax,
        pairFillIdx,
        pairHoleIdx,
        0.0,
        0.0);
#else
    float3 boxMin = 0.0;
    float3 boxMax = 0.0;
#if !TAA_EFFECTIVE_EARLY_HISTORY_REJECTION
    GetBBoxForPixel(ldsIdx, boxMin, boxMax);
#endif
    ApplyTemporalBlend(
        ST,
        ldsIdx,
        boxMin,
        boxMax,
        ldsIdx,
        ldsIdx,
        0.0,
        0.0);
#endif

    TaaPixelOutputs output;
    output.temporal = PixelOutTemporal;
    output.depth = PixelOutDepth;
    output.moments = PixelOutMoments;
    output.debugValue = PixelOutDebug;
    output.selectiveCurrent = PixelOutSmaaCurrent;
    output.selectiveRejection = PixelOutSmaaRejection;
    output.fusedScene = PixelOutFusedScene;
    return output;
}
#endif
