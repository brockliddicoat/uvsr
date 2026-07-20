//
// Applies SMAA to de-jittered current color in compacted rejected tiles and
// blends that presentation-only result over the resolved temporal output with
// the continuous dilated rejection mask.
//

#pragma pack_matrix(row_major)

#ifndef SMAA_SELECTIVE_INDIRECT_TILES
#error SMAA_SELECTIVE_INDIRECT_TILES must be a compile-time shader define
#endif

cbuffer SmaaConstants : register(b0)
{
    float4 SmaaRtMetrics;
    float4 SmaaSubsampleIndices;
}

#define SMAA_HLSL_4 1
#define SMAA_PRESET_HIGH 1
#define SMAA_RT_METRICS SmaaRtMetrics
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1
#define Sample(SAMPLER, COORD, ...) \
    SampleLevel(SAMPLER, COORD, 0, ##__VA_ARGS__)
#include "third_party/smaa/SMAA.hlsl"
#undef Sample

Texture2D<float4> CurrentColor : register(t0);
Texture2D<float4> ResolvedTemporal : register(t1);
Texture2D<float4> BlendWeights : register(t2);
Texture2D<float> RejectionTexture : register(t3);
#if SMAA_SELECTIVE_INDIRECT_TILES
StructuredBuffer<uint2> ActiveTiles : register(t4);
#endif
RWTexture2D<float4> OutputColor : register(u0);

float GetDilatedRejection(uint2 pixel, uint2 dimensions)
{
    const int2 maximumPixel = int2(dimensions) - 1;
    float rejection = 0.0;
    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            const int2 samplePixel = clamp(
                int2(pixel) + int2(x, y),
                int2(0, 0),
                maximumPixel);
            rejection = max(
                rejection,
                saturate(RejectionTexture.Load(
                    int3(samplePixel, 0))));
        }
    }
    return rejection;
}

[numthreads(8, 8, 1)]
void main(
    uint3 groupId : SV_GroupID,
    uint3 groupThreadId : SV_GroupThreadID)
{
#if SMAA_SELECTIVE_INDIRECT_TILES
    const uint2 tile = ActiveTiles[groupId.x];
#else
    const uint2 tile = groupId.xy;
#endif
    const uint2 pixel = tile * uint2(8, 8) + groupThreadId.xy;
    const uint2 dimensions = uint2(SmaaRtMetrics.zw);
    if (any(pixel >= dimensions))
        return;

    const float4 temporal =
        ResolvedTemporal.Load(int3(pixel, 0));
    const float rejection =
        GetDilatedRejection(pixel, dimensions);
#if !SMAA_SELECTIVE_INDIRECT_TILES
    // The dense path owns every output pixel and does not pre-copy the
    // temporal image. Publish the exact temporal value before the zero-mask
    // fast path so a fully accepted pixel remains bit-for-bit unchanged.
    OutputColor[pixel] = temporal;
#endif
    // Indirect mode has already copied temporal into inactive/zero-mask
    // pixels. Both permutations therefore take the same exact-identity path.
    if (!(rejection > 0.0))
        return;

    const float2 uv =
        (float2(pixel) + 0.5) * SmaaRtMetrics.xy;
    float4 offset;
    SMAANeighborhoodBlendingVS(uv, offset);
    const float4 spatialCurrent =
        SMAANeighborhoodBlendingPS(
            uv,
            offset,
            CurrentColor,
            BlendWeights);
    const float4 current =
        CurrentColor.Load(int3(pixel, 0));
    // Selective SMAA is a presentation correction, not an alternate current
    // resolve. Replacing temporal with spatialCurrent also injected
    // rejection * (current - temporal), so one rejected sample and its 5x5
    // dilation discarded stable history even when SMAA made no spatial
    // change. Apply only the official neighborhood pass's spatial delta to
    // the temporally registered base. A zero SMAA correction is now an exact
    // temporal identity for every rejection value.
    float3 corrected =
        temporal.rgb +
        rejection * (spatialCurrent.rgb - current.rgb);
    const float3 correctionMinimum = min(
        temporal.rgb,
        min(current.rgb, spatialCurrent.rgb));
    const float3 correctionMaximum = max(
        temporal.rgb,
        max(current.rgb, spatialCurrent.rgb));
    // The additive form can otherwise exceed every contributing color when
    // temporal and current differ. Keep the correction inside their finite
    // component envelope before applying the RGBA16F storage limit.
    corrected =
        all(isfinite(corrected)) &&
        all(isfinite(correctionMinimum)) &&
        all(isfinite(correctionMaximum))
            ? clamp(
                corrected,
                correctionMinimum,
                correctionMaximum)
        : temporal.rgb;
    corrected = all(isfinite(corrected))
        ? clamp(corrected, -65504.0, 65504.0)
        : 0.0;
    OutputColor[pixel] = float4(corrected, temporal.a);
}
