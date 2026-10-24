#pragma pack_matrix(row_major)

#include "temporal_aa_options_shared.h"
#include "temporal_aa_common.hlsli"

#ifndef TAA_RUNTIME_BEHAVIOR
#define TAA_RUNTIME_BEHAVIOR 0
#endif

Texture2D<float4> VelocityBuffer : register(t0);
Texture2D<float3> CurrentColor : register(t1);
Texture2D<float3> PreviousColor : register(t2);
Texture2D<float> CurrentDepth : register(t3);
Texture2D<float> PreviousDepth : register(t4);
RWTexture2D<float3> OutputColor : register(u0);
RWTexture2D<float> OutputDepth : register(u1);
SamplerState LinearSampler : register(s0);

cbuffer Constants : register(b1)
{
    float4x4 Projection;
    float2 ReciprocalBufferDimensions;
    float TemporalBlendFactor;
    float ReciprocalSpeedLimiter;
    float2 CurrentJitter;
    float2 CurrentToPreviousJitter;
    uint2 BufferDimensions;
    uint HistoryValid;
    uint DispatchGroupYOffset;
    float SourceDepthPairQuantizationError;
    float MaximumHistoryWeight;
    uint TemporalBehaviorFlags;
    uint TemporalBehaviorPadding;
}

static const uint kTileWidth = 18u;
static const uint kTileHeight = 10u;
static const uint kTilePixelCount = kTileWidth * kTileHeight;
static const uint kThreadCount = 64u;
static const float kFiniteHalfLimit = 65504.0f;
static const float kMaximumVelocitySquared = 64.0f * 64.0f;

bool TemporalBehaviorEnabled(uint flag)
{
#if TAA_RUNTIME_BEHAVIOR
    return (TemporalBehaviorFlags & flag) != 0u;
#else
    // Keep the normal Minimum profile fully foldable. The second packaged
    // shader variant reads the same uniform flags only when a Developer Option
    // changes one of these policies.
    static const uint kMinimumBehaviorFlags =
        UVSR_TAA_BEHAVIOR_MOVING_POINT_DEPTH |
        UVSR_TAA_BEHAVIOR_IMMEDIATE_HISTORY_WEIGHT |
        UVSR_TAA_BEHAVIOR_SQUARED_MOTION_TRUST |
        UVSR_TAA_BEHAVIOR_TIGHT_RECTIFICATION |
        UVSR_TAA_BEHAVIOR_LINEAR_BLEND_DOMAIN;
    return (kMinimumBehaviorFlags & flag) != 0u;
#endif
}

float RgbLuminance(float3 rgb)
{
    return dot(rgb, float3(0.212671f, 0.715160f, 0.072169f));
}

float3 CompressLuminance(float3 rgb)
{
    float denominator = 1.0f + RgbLuminance(rgb);
    denominator = abs(denominator) > 1e-5f
        ? denominator
        : denominator < 0.0f
            ? -1e-5f
            : 1e-5f;
    return rgb / denominator;
}

float3 ExpandLuminance(float3 rgb)
{
    float denominator = 1.0f - RgbLuminance(rgb);
    denominator = abs(denominator) > 1e-5f
        ? denominator
        : denominator < 0.0f
            ? -1e-5f
            : 1e-5f;
    return rgb / denominator;
}

float3 ClipColor(
    float3 color,
    float3 minimumColor,
    float3 maximumColor,
    float dilation)
{
    float3 center = (minimumColor + maximumColor) * 0.5f;
    float3 halfExtent =
        (maximumColor - minimumColor) * 0.5f * dilation + 0.001f;
    float3 displacement = color - center;
    float3 units = abs(displacement / halfExtent);
    float maximumUnit = max(
        max(units.x, units.y),
        max(units.z, 1.0f));
    return center + displacement / maximumUnit;
}

// One uint2 stores the already-half-precision current RGB plus device depth.
// The 18x10 tile therefore occupies 1,440 bytes of LDS.
groupshared uint2 g_PackedCurrent[kTilePixelCount];

float3 SanitizeCurrentColor(float3 value)
{
    // R11G11B10_FLOAT is unsigned. Minimum cost is an explicit quality trade:
    // signed experimental radiance remains available in Full Quality.
    return all(isfinite(value))
        ? clamp(value, 0.0f, kFiniteHalfLimit)
        : 0.0f;
}

uint2 PackCurrent(float3 color, float depth)
{
    float3 safeColor = SanitizeCurrentColor(color);
    float safeDepth = isfinite(depth)
        ? saturate(depth)
        : 0.0f;
    return uint2(
        f32tof16(safeColor.x) |
            (f32tof16(safeColor.y) << 16u),
        f32tof16(safeColor.z) |
            (f32tof16(safeDepth) << 16u));
}

float3 LoadCurrentColor(uint index)
{
    uint2 packed = g_PackedCurrent[index];
    return float3(
        f16tof32(packed.x & 0xffffu),
        f16tof32(packed.x >> 16u),
        f16tof32(packed.y & 0xffffu));
}

float LoadCurrentDepth(uint index)
{
    return f16tof32(g_PackedCurrent[index].y >> 16u);
}

bool IsDepthValid(float depth)
{
    // UVSR's temporal path currently consumes infinite reverse-Z depth.
    return isfinite(depth) && depth > 0.0f && depth <= 1.0f;
}

bool IsMotionValid(float4 motion)
{
    return motion.w > 0.5f && all(isfinite(motion));
}

bool IsLinearHistoryFootprintInBounds(float2 pixelPosition)
{
    return all(pixelPosition >= 0.0f) &&
        all(pixelPosition <= float2(BufferDimensions) - 1.0f);
}

void GetPairBounds(
    uint leftIndex,
    out float3 minimumColor,
    out float3 maximumColor)
{
    float3 values[7];
    values[0] = LoadCurrentColor(leftIndex);
    values[1] = LoadCurrentColor(leftIndex + 1u);
    values[2] = LoadCurrentColor(leftIndex + 2u);
    values[3] =
        LoadCurrentColor(leftIndex - kTileWidth - 1u);
    values[4] =
        LoadCurrentColor(leftIndex - kTileWidth + 1u);
    values[5] =
        LoadCurrentColor(leftIndex + kTileWidth - 1u);
    values[6] =
        LoadCurrentColor(leftIndex + kTileWidth + 1u);

    minimumColor = values[0];
    maximumColor = values[0];
    [unroll]
    for (uint index = 1u; index < 7u; ++index)
    {
        minimumColor = min(minimumColor, values[index]);
        maximumColor = max(maximumColor, values[index]);
    }
}

void WriteCurrent(uint2 pixel, float3 color, float depth)
{
    OutputColor[pixel] = color;
    OutputDepth[pixel] = depth;
}

struct PreparedPixel
{
    uint2 pixel;
    float3 current;
    float currentDepth;
    float2 historyUv;
    float velocitySquared;
    uint inBounds;
    uint useHistory;
};

PreparedPixel PreparePixel(uint2 pixel, uint tileIndex)
{
    PreparedPixel prepared;
    prepared.pixel = pixel;
    prepared.current = LoadCurrentColor(tileIndex);
    prepared.currentDepth = LoadCurrentDepth(tileIndex);
    prepared.historyUv = 0.0f;
    prepared.velocitySquared = 0.0f;
    prepared.inBounds = all(pixel < BufferDimensions) ? 1u : 0u;
    prepared.useHistory = 0u;
    if (prepared.inBounds == 0u ||
        HistoryValid == 0u ||
        !IsDepthValid(prepared.currentDepth))
    {
        return prepared;
    }

    float4 motion = VelocityBuffer.Load(int3(pixel, 0));
    if (!IsMotionValid(motion))
        return prepared;

    prepared.velocitySquared = dot(motion.xy, motion.xy);
    float2 historyColorPixel = float2(pixel) + motion.xy;
    float2 historyDepthPixel =
        historyColorPixel + CurrentToPreviousJitter;
    float expectedPreviousDepth =
        prepared.currentDepth + motion.z;
    if (prepared.velocitySquared >= kMaximumVelocitySquared ||
        !IsDepthValid(expectedPreviousDepth) ||
        !IsLinearHistoryFootprintInBounds(historyColorPixel) ||
        !IsLinearHistoryFootprintInBounds(historyDepthPixel))
    {
        return prepared;
    }

    [branch]
    if (TemporalBehaviorEnabled(
            UVSR_TAA_BEHAVIOR_MOVING_POINT_DEPTH))
    {
        // Minimum's default policy keeps nominally stationary history without
        // sampling the jittered previous-depth grid. Moving pixels pay one
        // point read.
        if (prepared.velocitySquared > 1e-4f ||
            abs(motion.z) > 1e-6f)
        {
            int2 depthPixel = clamp(
                int2(floor(historyDepthPixel + 0.5f)),
                int2(0, 0),
                int2(BufferDimensions) - 1);
            float previousDepth =
                PreviousDepth.Load(int3(depthPixel, 0));
            float depthTolerance = max(
                max(5e-4f, SourceDepthPairQuantizationError),
                max(abs(previousDepth), abs(expectedPreviousDepth)) *
                    0.01f);
            if (!IsDepthValid(previousDepth) ||
                abs(previousDepth - expectedPreviousDepth) >
                    depthTolerance)
            {
                return prepared;
            }
        }
    }
    else
    {
        float4 previousDepths = PreviousDepth.Gather(
            LinearSampler,
            (historyDepthPixel + 0.5f) *
                ReciprocalBufferDimensions);
        UvsrTemporalReverseZFootprint footprint =
            UvsrTemporalReduceReverseZFootprint(previousDepths);
        if (UvsrTemporalDepthAccepted(
                expectedPreviousDepth,
                motion.z,
                SourceDepthPairQuantizationError,
                footprint,
                Projection,
                1e-3f) == 0.0f)
        {
            return prepared;
        }
    }

    prepared.historyUv =
        (historyColorPixel + 0.5f) *
        ReciprocalBufferDimensions;
    prepared.useHistory = 1u;
    return prepared;
}

void ResolvePreparedPixel(
    PreparedPixel prepared,
    float3 minimumColor,
    float3 maximumColor)
{
    if (prepared.inBounds == 0u)
        return;

    if (prepared.useHistory == 0u)
    {
        WriteCurrent(
            prepared.pixel,
            prepared.current,
            prepared.currentDepth);
        return;
    }

    float3 history = PreviousColor.SampleLevel(
        LinearSampler,
        prepared.historyUv,
        0.0f);
    float motionTrust = TemporalBehaviorEnabled(
            UVSR_TAA_BEHAVIOR_SQUARED_MOTION_TRUST)
        ? saturate(
            1.0f -
            prepared.velocitySquared / kMaximumVelocitySquared)
        : saturate(
            1.0f -
            sqrt(prepared.velocitySquared) / 64.0f);
    if (all(isfinite(history)))
    {
        history = TemporalBehaviorEnabled(
                UVSR_TAA_BEHAVIOR_TIGHT_RECTIFICATION)
            ? clamp(history, minimumColor, maximumColor)
            : ClipColor(
                history,
                minimumColor,
                maximumColor,
                lerp(1.0f, 4.0f, motionTrust * motionTrust));
    }
    else
    {
        history = prepared.current;
    }

    float historyWeight = min(
        MaximumHistoryWeight,
        clamp(TemporalBlendFactor, 0.0f, 2.0f) *
            MaximumHistoryWeight *
            motionTrust);
    float3 resolved = TemporalBehaviorEnabled(
            UVSR_TAA_BEHAVIOR_LINEAR_BLEND_DOMAIN)
        ? lerp(
            prepared.current,
            history,
            historyWeight)
        : ExpandLuminance(lerp(
            CompressLuminance(prepared.current),
            CompressLuminance(history),
            historyWeight));
    resolved = SanitizeCurrentColor(resolved);
    WriteCurrent(
        prepared.pixel,
        resolved,
        prepared.currentDepth);
}

[numthreads(8, 8, 1)]
void main(
    uint3 groupId : SV_GroupID,
    uint3 groupThreadId : SV_GroupThreadID,
    uint groupIndex : SV_GroupIndex)
{
    int2 groupOrigin =
        int2(groupId.xy * uint2(16u, 8u));
    int2 maximumPixel = int2(BufferDimensions) - 1;

    for (uint index = groupIndex;
        index < kTilePixelCount;
        index += kThreadCount)
    {
        int2 tilePosition = int2(
            int(index % kTileWidth) - 1,
            int(index / kTileWidth) - 1);
        int2 sourcePixel = clamp(
            groupOrigin + tilePosition,
            int2(0, 0),
            maximumPixel);
        g_PackedCurrent[index] = PackCurrent(
            CurrentColor.Load(int3(sourcePixel, 0)),
            CurrentDepth.Load(int3(sourcePixel, 0)));
    }

    GroupMemoryBarrierWithGroupSync();

    uint pairX = groupThreadId.x * 2u;
    uint tileIndex =
        pairX + 1u +
        (groupThreadId.y + 1u) * kTileWidth;
    uint2 leftPixel =
        uint2(groupOrigin) +
        uint2(pairX, groupThreadId.y);

    PreparedPixel left = PreparePixel(
        leftPixel,
        tileIndex);
    PreparedPixel right = PreparePixel(
        leftPixel + uint2(1u, 0u),
        tileIndex + 1u);

    // Pair-coherent early rejection avoids all neighborhood work when neither
    // pixel can consume history.
    if ((left.useHistory | right.useHistory) == 0u)
    {
        ResolvePreparedPixel(left, 0.0f, 0.0f);
        ResolvePreparedPixel(right, 0.0f, 0.0f);
        return;
    }

    float3 minimumColor;
    float3 maximumColor;
    GetPairBounds(
        tileIndex,
        minimumColor,
        maximumColor);
    ResolvePreparedPixel(
        left,
        minimumColor,
        maximumColor);
    ResolvePreparedPixel(
        right,
        minimumColor,
        maximumColor);
}
