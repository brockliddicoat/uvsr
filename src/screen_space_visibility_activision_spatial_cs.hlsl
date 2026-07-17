#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "screen_space_visibility_cb.h"

#ifndef ACTIVISION_PACKED_GATHER
#define ACTIVISION_PACKED_GATHER 0
#endif

cbuffer c_Visibility : register(b0)
{
    ScreenSpaceVisibilityConstants g_Visibility;
};

SamplerState s_PointClamp : register(s0);
Texture2D<float> t_RawAmbient : register(t0);
Texture2D<uint> t_PackedLinearDepth : register(t1);
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_SpatialAmbient : register(u0);

static const uint ActivisionGuideOffsetMask = 3u;

float LoadLinearDepth(uint2 pixel)
{
    return asfloat(
        t_PackedLinearDepth[pixel] & ~ActivisionGuideOffsetMask);
}

bool ValidLinearDepth(float depth)
{
    return isfinite(depth) && depth > 0.0f && depth < 65503.0f;
}

float EstimateDepthSlope(
    float centerDepth,
    float negativeDepth,
    float positiveDepth)
{
    bool negativeValid = ValidLinearDepth(negativeDepth);
    bool positiveValid = ValidLinearDepth(positiveDepth);
    if (negativeValid && positiveValid)
        return 0.5f * (positiveDepth - negativeDepth);
    if (positiveValid)
        return positiveDepth - centerDepth;
    if (negativeValid)
        return centerDepth - negativeDepth;
    return 0.0f;
}

void AccumulateTap(
    float ambient,
    float sampleDepth,
    int2 offset,
    float centerDepth,
    float2 depthSlope,
    inout float weightedAmbient,
    inout float totalWeight)
{
    if (!isfinite(ambient) || !ValidLinearDepth(sampleDepth))
        return;

    // Activision slides 94-95 disclose a first-derivative correction and a
    // linear relative-depth threshold. The shipping second-derivative
    // formulation and constants were not published, so this UVSR adapter uses
    // only the disclosed first-order plane prediction rather than inventing a
    // curvature term that could leak across silhouettes.
    float predictedDepth = centerDepth + dot(depthSlope, float2(offset));
    float relativeError = abs(sampleDepth - predictedDepth) /
        max(centerDepth, 1e-4f);
    // Slide 94's relative soft threshold reaches zero at a 10% depth delta.
    // Keep that ramp linear; squaring it narrows the disclosed filter and is
    // an unsupported change to its response.
    float weight = saturate(1.0f - relativeError * 10.0f);
    weightedAmbient += max(ambient, 0.0f) * weight;
    totalWeight += weight;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    uint2 size = uint2(g_Visibility.samplingResolution);
    if (any(pixel >= size))
        return;

    uint2 maximumPixel = size - 1u;
    float centerDepth = LoadLinearDepth(pixel);
    float centerAmbient = t_RawAmbient[pixel];
    if (!ValidLinearDepth(centerDepth) || !isfinite(centerAmbient))
    {
        u_SpatialAmbient[pixel] = 1.0f;
        return;
    }

    uint2 leftPixel = uint2(clamp(
        int2(pixel) + int2(-1, 0), int2(0, 0), int2(maximumPixel)));
    uint2 rightPixel = uint2(clamp(
        int2(pixel) + int2(1, 0), int2(0, 0), int2(maximumPixel)));
    uint2 topPixel = uint2(clamp(
        int2(pixel) + int2(0, -1), int2(0, 0), int2(maximumPixel)));
    uint2 bottomPixel = uint2(clamp(
        int2(pixel) + int2(0, 1), int2(0, 0), int2(maximumPixel)));
    float leftDepth = LoadLinearDepth(leftPixel);
    float rightDepth = LoadLinearDepth(rightPixel);
    float topDepth = LoadLinearDepth(topPixel);
    float bottomDepth = LoadLinearDepth(bottomPixel);
    // Invalid prepared-depth sentinels must not participate in the plane
    // estimate. A central difference is preferred; a single valid neighbor
    // supplies the corresponding one-sided derivative at image/background
    // boundaries.
    float2 depthSlope = float2(
        EstimateDepthSlope(centerDepth, leftDepth, rightDepth),
        EstimateDepthSlope(centerDepth, topDepth, bottomDepth));
    if (any(!isfinite(depthSlope)))
        depthSlope = 0.0f;

    // The 4x4 footprint moves one pixel each frame: [-2,+1] on odd frames,
    // [-1,+2] on even frames, matching the presentation's top/left versus
    // bottom/right alternation.
    int footprintStart = (g_Visibility.frameIndex & 1u) != 0u ? -2 : -1;
    float weightedAmbient = 0.0f;
    float totalWeight = 0.0f;

#if ACTIVISION_PACKED_GATHER
    // Four AO gathers plus four matching depth gathers replace 32 scalar
    // texture instructions. The component order is D3D's LL, LR, UR, UL.
    static const int2 componentOffsets[4] = {
        int2(0, 1), int2(1, 1), int2(1, 0), int2(0, 0)
    };
    [unroll]
    for (uint blockY = 0u; blockY < 2u; ++blockY)
    {
        [unroll]
        for (uint blockX = 0u; blockX < 2u; ++blockX)
        {
            int2 blockBase = int2(pixel) + footprintStart +
                int2(blockX * 2u, blockY * 2u);
            float2 gatherUv = (float2(blockBase) + 1.0f) / float2(size);
            float4 ambientValues = t_RawAmbient.GatherRed(
                s_PointClamp, gatherUv);
            uint4 packedDepthValues = t_PackedLinearDepth.GatherRed(
                s_PointClamp, gatherUv);
            [unroll]
            for (uint component = 0u; component < 4u; ++component)
            {
                int2 sampleOffset = footprintStart +
                    int2(blockX * 2u, blockY * 2u) +
                    componentOffsets[component];
                AccumulateTap(
                    ambientValues[component],
                    asfloat(packedDepthValues[component] &
                        ~ActivisionGuideOffsetMask),
                    sampleOffset, centerDepth, depthSlope,
                    weightedAmbient, totalWeight);
            }
        }
    }
#else
    [unroll]
    for (uint y = 0u; y < 4u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 4u; ++x)
        {
            int2 offset = footprintStart + int2(x, y);
            uint2 samplePixel = uint2(clamp(
                int2(pixel) + offset, int2(0, 0), int2(maximumPixel)));
            AccumulateTap(
                t_RawAmbient[samplePixel], LoadLinearDepth(samplePixel),
                offset, centerDepth, depthSlope,
                weightedAmbient, totalWeight);
        }
    }
#endif

    float result = totalWeight > 1e-5f
        ? weightedAmbient / totalWeight : max(centerAmbient, 0.0f);
    u_SpatialAmbient[pixel] = isfinite(result)
        ? min(result, 65504.0f) : max(centerAmbient, 0.0f);
}
