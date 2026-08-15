#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "screen_space_visibility_cb.h"

#ifndef ENABLE_AO
#define ENABLE_AO 1
#endif
#ifndef ENABLE_GI
#define ENABLE_GI 1
#endif
cbuffer c_Visibility : register(b0)
{
    ScreenSpaceVisibilityConstants g_Visibility;
};

#if ENABLE_AO
Texture2D<float> t_Ambient : register(t0);
#endif
#if ENABLE_GI
Texture2D<float4> t_Indirect : register(t1);
#endif
Texture2D<float> t_Depth : register(t2);
Texture2D<float4> t_Normal : register(t3);

#if ENABLE_AO
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_Ambient : register(u0);
#endif
#if ENABLE_GI
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_Indirect : register(u1);
#endif

float3 SafeNormal(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12f && isfinite(lengthSquared)
        ? value * rsqrt(lengthSquared)
        : fallback;
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
    return abs(viewZ);
}

bool ReconstructViewPosition(
    float2 pixelPosition,
    float deviceDepth,
    out float3 positionVS)
{
    float4x4 projection = g_Visibility.view.matViewToClip;
    float denominator = deviceDepth * projection[2][3] - projection[2][2];
    if (!isfinite(denominator) || abs(denominator) <= 1e-6f)
    {
        positionVS = 0.0f;
        return false;
    }

    float viewZ = (projection[3][2] -
        deviceDepth * projection[3][3]) / denominator;
    float clipW = viewZ * projection[2][3] + projection[3][3];
    float2 ndc = (pixelPosition - g_Visibility.view.clipToWindowBias) /
        g_Visibility.view.clipToWindowScale;
    positionVS = float3(
        (ndc.x * clipW - viewZ * projection[2][0] - projection[3][0]) /
            projection[0][0],
        (ndc.y * clipW - viewZ * projection[2][1] - projection[3][1]) /
            projection[1][1],
        viewZ);
    return all(isfinite(positionVS));
}

uint2 SamplingToFullPixel(uint2 samplingPixel)
{
    uint scale = max(g_Visibility.resolutionScale, 1u);
    return min(samplingPixel * scale + scale / 2u,
        uint2(g_Visibility.fullResolution) - 1u);
}

uint2 UpsampleGuidePixel(uint2 samplingPixel)
{
    return SamplingToFullPixel(samplingPixel);
}

float GuideWeight(
    uint2 samplingPixel,
    float spatialWeight,
    float centerDepth,
    float3 centerPositionVS,
    float3 centerNormalWS,
    float3 centerNormalVS)
{
    uint2 guidePixel = UpsampleGuidePixel(samplingPixel);
    float sampleDeviceDepth = t_Depth[guidePixel];
    if (!IsValidDepth(sampleDeviceDepth))
        return 0.0f;
    float3 samplePositionVS;
    float sampleDepth = LinearViewDepth(sampleDeviceDepth);
    if (!ReconstructViewPosition(
            float2(guidePixel) + 0.5f,
            sampleDeviceDepth,
            samplePositionVS))
    {
        return 0.0f;
    }
    float3 sampleNormalWS = SafeNormal(
        t_Normal[guidePixel].xyz, centerNormalWS);
    float depthScale = max(centerDepth * 0.02f, 0.01f);
    float planeDistance = abs(dot(
        samplePositionVS - centerPositionVS, centerNormalVS));
    float depthWeight = exp(-planeDistance / depthScale) *
        exp(-abs(sampleDepth - centerDepth) / (depthScale * 2.0f));
    float normalBase = saturate(dot(centerNormalWS, sampleNormalWS));
    float normalWeight = pow(normalBase, 16.0f);
    return spatialWeight * depthWeight * normalWeight;
}

void AccumulateUpsampleTap(
    int2 samplingCoordinate,
    float spatialWeight,
    float centerDepth,
    float3 centerPositionVS,
    float3 centerNormalWS,
    float3 centerNormalVS,
    inout float totalWeight,
    inout float ambientSum,
    inout float3 indirectSum)
{
    int2 maximumCoordinate = int2(g_Visibility.samplingResolution) - 1;
    uint2 samplingPixel = uint2(clamp(
        samplingCoordinate, int2(0, 0), maximumCoordinate));
    float weight = GuideWeight(
        samplingPixel,
        spatialWeight,
        centerDepth,
        centerPositionVS,
        centerNormalWS,
        centerNormalVS);
    totalWeight += weight;
#if ENABLE_AO
    ambientSum += max(t_Ambient[samplingPixel], 0.0f) * weight;
#endif
#if ENABLE_GI
    indirectSum += max(t_Indirect[samplingPixel].rgb, 0.0f) * weight;
#endif
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(g_Visibility.fullResolution)))
        return;

    float centerDeviceDepth = t_Depth[pixel];
    if (!IsValidDepth(centerDeviceDepth))
    {
#if ENABLE_AO
        u_Ambient[pixel] = 1.0f;
#endif
#if ENABLE_GI
        u_Indirect[pixel] = 0.0f;
#endif
        return;
    }

    uint scale = max(g_Visibility.resolutionScale, 1u);
    float2 samplingPosition = (float2(pixel) + 0.5f) /
        float(scale) - 0.5f;
    int2 samplingCenter = int2(round(samplingPosition));
    float3 centerPositionVS;
    float centerDepth = LinearViewDepth(centerDeviceDepth);
    if (!ReconstructViewPosition(
            float2(pixel) + 0.5f,
            centerDeviceDepth,
            centerPositionVS))
    {
#if ENABLE_AO
        u_Ambient[pixel] = 1.0f;
#endif
#if ENABLE_GI
        u_Indirect[pixel] = 0.0f;
#endif
        return;
    }
    float3 centerNormalWS = SafeNormal(
        t_Normal[pixel].xyz, float3(0.0f, 1.0f, 0.0f));
    float3 centerNormalVS = SafeNormal(
        mul(float4(centerNormalWS, 0.0f),
            g_Visibility.view.matWorldToView).xyz,
        float3(0.0f, 1.0f, 0.0f));

    float totalWeight = 0.0f;
    float ambientSum = 0.0f;
    float3 indirectSum = 0.0f;

    // Reduced-resolution visibility always uses this single four-tap
    // depth/normal-guided upsample. Full-resolution signals bypass this pass.
    int2 base = int2(floor(samplingPosition));
    [unroll]
    for (uint y = 0u; y < 2u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 2u; ++x)
        {
            int2 coordinate = base + int2(x, y);
            float2 delta = float2(coordinate) - samplingPosition;
            float spatialWeight = max(
                (1.0f - abs(delta.x)) *
                (1.0f - abs(delta.y)), 0.0f);
            AccumulateUpsampleTap(coordinate, spatialWeight,
                centerDepth, centerPositionVS,
                centerNormalWS, centerNormalVS, totalWeight,
                ambientSum, indirectSum);
        }
    }
    if (!(totalWeight > 1e-6f) || !isfinite(totalWeight))
    {
        int2 maximumCoordinate = int2(g_Visibility.samplingResolution) - 1;
        uint2 fallbackPixel = uint2(clamp(
            samplingCenter, int2(0, 0), maximumCoordinate));
        totalWeight = 1.0f;
#if ENABLE_AO
        ambientSum = max(t_Ambient[fallbackPixel], 0.0f);
#endif
#if ENABLE_GI
        indirectSum = max(t_Indirect[fallbackPixel].rgb, 0.0f);
#endif
    }
    float inverseWeight = rcp(max(totalWeight, 1e-6f));
#if ENABLE_AO
    float ambient = ambientSum * inverseWeight;
    u_Ambient[pixel] = isfinite(ambient)
        ? min(max(ambient, 0.0f), 65504.0f)
        : 1.0f;
#endif
#if ENABLE_GI
    float3 indirect = indirectSum * inverseWeight;
    if (any(!isfinite(indirect)))
        indirect = 0.0f;
    u_Indirect[pixel] = float4(
        min(max(indirect, 0.0f), 65504.0f), 0.0f);
#endif
}
