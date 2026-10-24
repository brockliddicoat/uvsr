#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
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

Texture2D<float> t_AmbientVisibility : register(t0);
Texture2D<float4> t_IndirectDiffuse : register(t1);
Texture2D<float> t_Depth : register(t2);
Texture2D<float4> t_Normal : register(t3);
Texture2D<float4> t_RawDebug : register(t4);

VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_AmbientVisibility : register(u0);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_IndirectDiffuse : register(u1);
VK_IMAGE_FORMAT("rgba8") RWTexture2D<float4> u_Debug : register(u2);

bool ValidDepth(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Visibility.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

float3 SafeNormal(float3 value)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12f && isfinite(lengthSquared)
        ? value * rsqrt(lengthSquared)
        : 0.0f;
}

bool ViewDepth(float deviceDepth, out float viewDepth)
{
    float4x4 projection = g_Visibility.view.matViewToClip;
    float denominator = deviceDepth * projection[2][3] - projection[2][2];
    if (!isfinite(denominator) || abs(denominator) <= 1e-6f)
    {
        viewDepth = 0.0f;
        return false;
    }
    viewDepth = abs((projection[3][2] - deviceDepth * projection[3][3]) /
        denominator);
    return isfinite(viewDepth);
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(g_Visibility.samplingResolution)))
        return;

    uint2 centerFullPixel = pixel;
    float centerDeviceDepth = t_Depth[centerFullPixel];
    if (!ValidDepth(centerDeviceDepth))
    {
#if ENABLE_AO
        u_AmbientVisibility[pixel] = 1.0f;
#endif
#if ENABLE_GI
        u_IndirectDiffuse[pixel] = 0.0f;
#endif
        if (g_Visibility.debugMode >= 7u && g_Visibility.debugMode <= 16u)
            u_Debug[pixel] = 0.0f;
        return;
    }

    float centerViewDepth;
    if (!ViewDepth(centerDeviceDepth, centerViewDepth))
        centerViewDepth = 0.0f;
    float3 centerNormal = SafeNormal(t_Normal[centerFullPixel].xyz);

    int radius = g_Visibility.spatialEnabled != 0u
        ? int(g_Visibility.spatialRadius) : 0;
    const bool traversalDebugActive = g_Visibility.debugMode >= 7u &&
        g_Visibility.debugMode <= 16u;
    float centerAmbient = ENABLE_AO != 0
        ? saturate(t_AmbientVisibility[pixel]) : 1.0f;
    float3 centerGi = ENABLE_GI != 0
        ? max(t_IndirectDiffuse[pixel].rgb, 0.0f) : 0.0f;
    float3 centerDebug = traversalDebugActive ? t_RawDebug[pixel].rgb : 0.0f;
    float ambientSum = centerAmbient;
    float3 giSum = centerGi;
    float3 debugSum = centerDebug;
    float weightSum = 1.0f;

    // Restore the complete radius-one neighborhood. Diagonal chair rails and
    // other thin geometry cannot rely on temporal rotation alone for stable
    // edge-aware reconstruction.
    static const int2 FilterDirections[8] = {
        int2(-1, -1), int2(0, -1), int2(1, -1),
        int2(-1,  0),                 int2(1,  0),
        int2(-1,  1), int2(0,  1), int2(1,  1)
    };
    [loop]
    for (int distance = 1; distance <= radius; ++distance)
    {
        [unroll]
        for (uint directionIndex = 0u; directionIndex < 8u; ++directionIndex)
        {
            int2 neighbor = int2(pixel) + FilterDirections[directionIndex] * distance;
            if (any(neighbor < 0) || any(neighbor >= int2(g_Visibility.samplingResolution)))
                continue;
            uint2 neighborFullPixel = uint2(neighbor);
            float neighborDeviceDepth = t_Depth[neighborFullPixel];
            if (!ValidDepth(neighborDeviceDepth))
                continue;

            float neighborViewDepth;
            if (!ViewDepth(neighborDeviceDepth, neighborViewDepth))
            {
                continue;
            }
            float relativeDepthDifference = abs(neighborViewDepth - centerViewDepth) /
                max(abs(centerViewDepth), 0.1f);
            if (relativeDepthDifference > g_Visibility.depthRejection)
                continue;

            float3 neighborNormal = SafeNormal(t_Normal[neighborFullPixel].xyz);
            float normalAgreement = dot(centerNormal, neighborNormal);
            if (normalAgreement < g_Visibility.normalRejection)
                continue;

            float spatialDistanceSquared = float(distance * distance);
            float spatialWeight = exp2(-0.72134752f * spatialDistanceSquared /
                max(float(radius * radius), 1.0f));
            float depthWeight = exp2(-1.44269504f * relativeDepthDifference /
                max(g_Visibility.depthRejection, 1e-6f));
            float normalWeight = saturate((normalAgreement - g_Visibility.normalRejection) /
                max(1.0f - g_Visibility.normalRejection, 1e-4f));
            float weight = spatialWeight * depthWeight * max(normalWeight, 0.05f);

            ambientSum += (ENABLE_AO != 0
                ? saturate(t_AmbientVisibility[neighbor]) : 1.0f) * weight;
            if (ENABLE_GI != 0)
                giSum += max(t_IndirectDiffuse[neighbor].rgb, 0.0f) * weight;
            if (traversalDebugActive)
                debugSum += t_RawDebug[neighbor].rgb * weight;
            weightSum += weight;
        }
    }

#if ENABLE_AO
    u_AmbientVisibility[pixel] = saturate(ambientSum / weightSum);
#endif
#if ENABLE_GI
    u_IndirectDiffuse[pixel] = float4(
        min(max(giSum / weightSum, 0.0f), 65504.0f), 1.0f);
#endif
    if (traversalDebugActive)
        u_Debug[pixel] = float4(max(debugSum / weightSum, 0.0f), 1.0f);
}
