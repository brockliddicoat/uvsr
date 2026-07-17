#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "screen_space_visibility_cb.h"

cbuffer c_Visibility : register(b0)
{
    ScreenSpaceVisibilityConstants g_Visibility;
};

Texture2D<float> t_DeviceDepth : register(t0);
VK_IMAGE_FORMAT("r32ui") RWTexture2D<uint> u_PackedLinearDepth : register(u0);

static const uint ActivisionGuideOffsetMask = 3u;

uint PackLinearDepthAndGuideOffset(float linearDepth, uint guideOffset)
{
    // Linear view depths are positive, finite FP32 values. Replacing the two
    // least-significant mantissa bits changes the stored value by at most
    // three ULPs while retaining the 2x2 source-pixel identity at zero extra
    // memory cost. Consumers mask those bits before decoding the depth.
    return (asuint(linearDepth) & ~ActivisionGuideOffsetMask) |
        (guideOffset & ActivisionGuideOffsetMask);
}

bool IsValidDepth(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Visibility.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

float LinearViewDepth(float deviceDepth)
{
    float4x4 projection = g_Visibility.view.matViewToClip;
    float denominator = deviceDepth * projection[2][3] - projection[2][2];
    if (!isfinite(denominator) || abs(denominator) <= 1e-6f)
        return 65504.0f;
    float viewZ = (projection[3][2] -
        deviceDepth * projection[3][3]) / denominator;
    float result = abs(viewZ);
    return isfinite(result) && result > 0.0f ? result : 65504.0f;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    uint2 samplingSize = uint2(g_Visibility.samplingResolution);
    if (any(pixel >= samplingSize))
        return;

    // The PS4 profiles are hard-locked to half resolution by the execution
    // plan, so exactly two bits identify every source texel in the 2x2 block.
    const uint scale = 2u;
    uint2 fullSize = uint2(g_Visibility.fullResolution);
    uint2 firstFullPixel = pixel * scale;
    float nearestDepth = 65504.0f;
    uint nearestOffset = 0u;
    [unroll]
    for (uint y = 0u; y < scale; ++y)
    {
        [unroll]
        for (uint x = 0u; x < scale; ++x)
        {
            uint2 fullPixel = min(firstFullPixel + uint2(x, y), fullSize - 1u);
            float deviceDepth = t_DeviceDepth[fullPixel];
            if (IsValidDepth(deviceDepth))
            {
                float linearDepth = LinearViewDepth(deviceDepth);
                if (linearDepth < nearestDepth)
                {
                    nearestDepth = linearDepth;
                    nearestOffset = x | (y << 1u);
                }
            }
        }
    }
    u_PackedLinearDepth[pixel] = PackLinearDepthAndGuideOffset(
        nearestDepth, nearestOffset);
}
