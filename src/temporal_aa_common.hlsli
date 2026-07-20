//
// Temporal reprojection contract for MiniEngine TAA.
//
// Motion.xy is current-to-previous, finite, full-resolution pixels. The
// producer has already removed projection jitter, so resolved history color is
// sampled at currentPixel + motion.xy. Raw previous depth remains on its
// producing jittered grid and is sampled at that color coordinate plus
// previousJitter - currentJitter exactly once.
//
// UVSR uses infinite reverse-Z. Finite device depth in (0, 1] is geometry;
// zero is cleared background and is never temporal history.
//

#ifndef UVSR_TEMPORAL_AA_COMMON_HLSLI
#define UVSR_TEMPORAL_AA_COMMON_HLSLI

static const float UvsrTemporalFarViewDepth = 65504.0;
// Motion.z is written to RGBA16F before temporal AA reads it. For a normal
// binary16 value q, q * 2^-11 is at least half of the local
// ULP (and is exact at exponent boundaries). Subnormals have a fixed 2^-24
// spacing, so their maximum round-to-nearest error is 2^-25. These constants
// therefore bound the device-depth error represented by the stored delta
// without guessing a scene-space tolerance.
static const float UvsrTemporalFp16RelativeHalfUlp = 0.00048828125;
static const float UvsrTemporalFp16SubnormalHalfUlp =
    0.0000000298023223876953125;
// Require the reconstructed previous device depth to retain the ten relative
// fraction bits supplied by binary16. If cancellation between current depth
// and motion.z makes its half-ULP uncertainty exceed one part in 2^10 of the
// result, depth ownership is numerically unknowable and history must fail
// closed instead of turning that uncertainty into a broad silhouette window.
static const float UvsrTemporalMinimumRelativeDeviceDepthPrecision =
    0.0009765625;
float UvsrTemporalMotionValidity(float4 packedMotion)
{
    return float(
        packedMotion.w > 0.5 &&
        all(isfinite(packedMotion)));
}

float UvsrTemporalDeviceDepthValidity(float deviceDepth)
{
    return float(
        deviceDepth > 0.0 &&
        deviceDepth <= 1.0 &&
        isfinite(deviceDepth));
}

float UvsrTemporalLinearViewDepth(
    float deviceDepth,
    float4x4 projection)
{
    if (UvsrTemporalDeviceDepthValidity(deviceDepth) == 0.0)
        return UvsrTemporalFarViewDepth;

    const float denominator =
        deviceDepth * projection[2][3] - projection[2][2];
    if (abs(denominator) <= 1e-8 || !isfinite(denominator))
        return UvsrTemporalFarViewDepth;

    const float viewZ =
        (projection[3][2] - deviceDepth * projection[3][3]) /
        denominator;
    return isfinite(viewZ)
        ? min(abs(viewZ), UvsrTemporalFarViewDepth)
        : UvsrTemporalFarViewDepth;
}

float UvsrTemporalFp16DeviceDepthDeltaError(
    float quantizedDeviceDepthDelta)
{
    if (!isfinite(quantizedDeviceDepthDelta))
        return 0.0;

    return max(
        abs(quantizedDeviceDepthDelta) *
            UvsrTemporalFp16RelativeHalfUlp,
        UvsrTemporalFp16SubnormalHalfUlp);
}

float UvsrTemporalDeviceDepthError(
    float quantizedDeviceDepthDelta,
    float sourceDepthPairQuantizationError)
{
    if (!isfinite(quantizedDeviceDepthDelta) ||
        !isfinite(sourceDepthPairQuantizationError) ||
        sourceDepthPairQuantizationError < 0.0)
    {
        return UvsrTemporalFarViewDepth;
    }

    // The source depth endpoints are quantized independently after motion.z
    // is produced. The CPU supplies their combined format-specific error:
    // one full UNORM step for D24/D16 and zero for float32 depth.
    return UvsrTemporalFp16DeviceDepthDeltaError(
            quantizedDeviceDepthDelta) +
        sourceDepthPairQuantizationError;
}

float UvsrTemporalDeviceDepthPrecisionValidity(
    float expectedPreviousDeviceDepth,
    float quantizedDeviceDepthDelta,
    float sourceDepthPairQuantizationError)
{
    if (UvsrTemporalDeviceDepthValidity(
            expectedPreviousDeviceDepth) == 0.0 ||
        !isfinite(quantizedDeviceDepthDelta) ||
        !isfinite(sourceDepthPairQuantizationError) ||
        sourceDepthPairQuantizationError < 0.0)
    {
        return 0.0;
    }

    const float deviceDepthError =
        UvsrTemporalDeviceDepthError(
            quantizedDeviceDepthDelta,
            sourceDepthPairQuantizationError);
    return float(
        deviceDepthError <=
            expectedPreviousDeviceDepth *
                UvsrTemporalMinimumRelativeDeviceDepthPrecision);
}

float UvsrTemporalDeviceDepthViewAllowance(
    float expectedPreviousDeviceDepth,
    float quantizedDeviceDepthDelta,
    float sourceDepthPairQuantizationError,
    float4x4 projection)
{
    if (UvsrTemporalDeviceDepthValidity(
            expectedPreviousDeviceDepth) == 0.0 ||
        !isfinite(quantizedDeviceDepthDelta) ||
        !isfinite(sourceDepthPairQuantizationError) ||
        sourceDepthPairQuantizationError < 0.0)
    {
        return 0.0;
    }

    // Infinite reverse-Z becomes nearer as device depth increases. Only the
    // nearer end of the combined binary16-motion and source-endpoint
    // quantization interval can make an expected depth look spuriously farther
    // than the real previous surface, so only that one-sided uncertainty
    // belongs in MiniEngine's one-sided disocclusion allowance.
    const float nearestPlausibleDeviceDepth = min(
        expectedPreviousDeviceDepth +
            UvsrTemporalDeviceDepthError(
                quantizedDeviceDepthDelta,
                sourceDepthPairQuantizationError),
        1.0);
    const float quantizedExpectedViewDepth =
        UvsrTemporalLinearViewDepth(
            expectedPreviousDeviceDepth,
            projection);
    const float nearestPlausibleViewDepth =
        UvsrTemporalLinearViewDepth(
            nearestPlausibleDeviceDepth,
            projection);
    return max(
        quantizedExpectedViewDepth - nearestPlausibleViewDepth,
        0.0);
}

float UvsrTemporalDeviceDepthFartherViewAllowance(
    float expectedPreviousDeviceDepth,
    float quantizedDeviceDepthDelta,
    float sourceDepthPairQuantizationError,
    float4x4 projection)
{
    if (UvsrTemporalDeviceDepthValidity(
            expectedPreviousDeviceDepth) == 0.0 ||
        !isfinite(quantizedDeviceDepthDelta) ||
        !isfinite(sourceDepthPairQuantizationError) ||
        sourceDepthPairQuantizationError < 0.0)
    {
        return 0.0;
    }

    const float deviceDepthError =
        UvsrTemporalDeviceDepthError(
            quantizedDeviceDepthDelta,
            sourceDepthPairQuantizationError);
    if (deviceDepthError >= expectedPreviousDeviceDepth)
        return UvsrTemporalFarViewDepth;

    const float farthestPlausibleDeviceDepth =
        expectedPreviousDeviceDepth - deviceDepthError;
    const float quantizedExpectedViewDepth =
        UvsrTemporalLinearViewDepth(
            expectedPreviousDeviceDepth,
            projection);
    const float farthestPlausibleViewDepth =
        UvsrTemporalLinearViewDepth(
            farthestPlausibleDeviceDepth,
            projection);
    return max(
        farthestPlausibleViewDepth - quantizedExpectedViewDepth,
        0.0);
}

float2 UvsrTemporalHistoryColorPixel(
    float2 currentPixel,
    float2 currentToPreviousMotionPixels)
{
    return currentPixel + currentToPreviousMotionPixels;
}

float2 UvsrTemporalHistoryDepthPixel(
    float2 currentPixel,
    float2 currentToPreviousMotionPixels,
    float2 previousMinusCurrentJitterPixels)
{
    return UvsrTemporalHistoryColorPixel(
        currentPixel,
        currentToPreviousMotionPixels) +
        previousMinusCurrentJitterPixels;
}

// Point sampling is legal while the requested texel center remains inside the
// viewport. The strict half-pixel endpoints deliberately reject exactly
// -0.5 and dimension-0.5 rather than relying on clamp addressing.
float UvsrTemporalPointPositionInBounds(
    float2 pixel,
    uint2 dimensions)
{
    const float2 windowPosition = pixel + 0.5;
    const bool2 lowerInside = windowPosition > 0.0;
    const bool2 upperInside =
        windowPosition < float2(dimensions);
    return float(
        lowerInside.x &
        lowerInside.y &
        upperInside.x &
        upperInside.y);
}

// Linear sampling and Gather inspect a footprint around the requested center.
// Reject subpixel coordinates beyond the first/last real texel center so a
// clamp sampler cannot manufacture temporal depth or color outside the image.
// Exact first/last centers remain valid; duplicated zero-weight Gather lanes
// at those centers do not enlarge the effective footprint.
float UvsrTemporalLinearFootprintInBounds(
    float2 pixel,
    uint2 dimensions)
{
    const bool2 lowerInside = pixel >= 0.0;
    const bool2 upperInside =
        pixel <= float2(dimensions - 1u);
    return float(
        lowerInside.x &
        lowerInside.y &
        upperInside.x &
        upperInside.y);
}

// Compatibility name for point-sampled temporal consumers. New call sites
// should state their sampling footprint explicitly.
float UvsrTemporalPositionInBounds(
    float2 pixel,
    uint2 dimensions)
{
    return UvsrTemporalPointPositionInBounds(pixel, dimensions);
}

struct UvsrTemporalReverseZFootprint
{
    float farthestValidDeviceDepth;
    float nearestValidDeviceDepth;
    uint validMask;
    uint backgroundMask;
};

UvsrTemporalReverseZFootprint
    UvsrTemporalReduceReverseZFootprint(float4 deviceDepths)
{
    UvsrTemporalReverseZFootprint result;
    result.validMask = 0u;
    result.backgroundMask = 0u;
    result.farthestValidDeviceDepth = 1.0;
    result.nearestValidDeviceDepth = 0.0;

    [unroll]
    for (uint lane = 0u; lane < 4u; ++lane)
    {
        const float depth = deviceDepths[lane];
        const bool finite = isfinite(depth);
        const bool background = finite && depth == 0.0;
        const bool valid =
            finite &&
            depth > 0.0 &&
            depth <= 1.0;
        if (background)
            result.backgroundMask |= 1u << lane;
        if (valid)
        {
            result.validMask |= 1u << lane;
            result.farthestValidDeviceDepth =
                min(result.farthestValidDeviceDepth, depth);
            result.nearestValidDeviceDepth =
                max(result.nearestValidDeviceDepth, depth);
        }
    }
    return result;
}

float UvsrTemporalFootprintHasConsistentGeometry(
    UvsrTemporalReverseZFootprint footprint)
{
    // A reverse-Z depth Gather is a discrete four-texel ownership test, not a
    // value to interpolate. Every lane must belong to finite geometry. In
    // particular, a mixed geometry/background footprint cannot authorize a
    // bilinear history-color sample from the other side of a silhouette.
    // Requiring the full valid mask also fails closed on corrupt/invalid depth
    // without changing legal all-geometry footprints.
    return float(
        footprint.validMask == 0xfu &&
        footprint.backgroundMask == 0u);
}

float UvsrTemporalDepthAccepted(
    float expectedPreviousDeviceDepth,
    float quantizedDeviceDepthDelta,
    float sourceDepthPairQuantizationError,
    UvsrTemporalReverseZFootprint footprint,
    float4x4 projection,
    float baseViewDepthAllowance)
{
    if (UvsrTemporalDeviceDepthValidity(
            expectedPreviousDeviceDepth) == 0.0 ||
        !isfinite(quantizedDeviceDepthDelta) ||
        UvsrTemporalDeviceDepthPrecisionValidity(
            expectedPreviousDeviceDepth,
            quantizedDeviceDepthDelta,
            sourceDepthPairQuantizationError) == 0.0 ||
        UvsrTemporalFootprintHasConsistentGeometry(footprint) == 0.0)
    {
        return 0.0;
    }

    const float expectedViewDepth =
        UvsrTemporalLinearViewDepth(
            expectedPreviousDeviceDepth,
            projection);
    const float historyViewDepth =
        UvsrTemporalLinearViewDepth(
            footprint.farthestValidDeviceDepth,
            projection);
    const float quantizationViewAllowance =
        UvsrTemporalDeviceDepthViewAllowance(
            expectedPreviousDeviceDepth,
            quantizedDeviceDepthDelta,
            sourceDepthPairQuantizationError,
            projection);
    return step(
        expectedViewDepth,
        historyViewDepth +
            baseViewDepthAllowance +
            quantizationViewAllowance);
}

#endif
