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

Texture2D<float> t_CurrentAmbient : register(t0);
Texture2D<float4> t_CurrentIndirect : register(t1);
Texture2D<float> t_CurrentDepth : register(t2);
Texture2D<float4> t_CurrentNormal : register(t3);
Texture2D<float4> t_Motion : register(t4);
Texture2D<float> t_PreviousAmbient : register(t5);
Texture2D<float4> t_PreviousIndirect : register(t6);
Texture2D<float> t_PreviousDepth : register(t7);
Texture2D<float4> t_PreviousNormal : register(t8);

VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_AmbientHistory : register(u0);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_IndirectHistory : register(u1);
VK_IMAGE_FORMAT("r32f") RWTexture2D<float> u_DepthHistory : register(u2);
VK_IMAGE_FORMAT("rgba8") RWTexture2D<float4> u_NormalHistory : register(u3);

uint2 SamplingToFullPixel(uint2 samplingPixel)
{
    uint scale = max(g_Visibility.resolutionScale, 1u);
    uint2 fullSize = uint2(g_Visibility.fullResolution);
    return min(samplingPixel * scale + scale / 2u, fullSize - 1u);
}

float3 SafeNormal(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12f && isfinite(lengthSquared)
        ? value * rsqrt(lengthSquared)
        : fallback;
}

void CurrentNeighborhoodBounds(
    uint2 pixel,
    out float ambientMinimum,
    out float ambientMaximum,
    out float3 indirectMinimum,
    out float3 indirectMaximum)
{
    uint2 maximumPixel = uint2(g_Visibility.samplingResolution) - 1u;
    static const int2 offsets[4] = {
        int2(1, 1), int2(1, -1), int2(-1, 1), int2(-1, -1)
    };
    ambientMinimum = 65504.0f;
    ambientMaximum = 0.0f;
    indirectMinimum = 65504.0f;
    indirectMaximum = 0.0f;
    [unroll]
    for (uint index = 0u; index < 4u; ++index)
    {
        uint2 samplePixel = uint2(clamp(
            int2(pixel) + offsets[index],
            int2(0, 0), int2(maximumPixel)));
#if ENABLE_AO
        float ambient = max(t_CurrentAmbient[samplePixel], 0.0f);
        if (!isfinite(ambient))
            ambient = 1.0f;
        ambientMinimum = min(ambientMinimum, ambient);
        ambientMaximum = max(ambientMaximum, ambient);
#endif
#if ENABLE_GI
        float3 indirect = t_CurrentIndirect[samplePixel].rgb;
        if (any(!isfinite(indirect)))
            indirect = 0.0f;
        indirect = max(indirect, 0.0f);
        indirectMinimum = min(indirectMinimum, indirect);
        indirectMaximum = max(indirectMaximum, indirect);
#endif
    }
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(g_Visibility.samplingResolution)))
        return;

    uint2 fullPixel = SamplingToFullPixel(pixel);
    float currentDepth = t_CurrentDepth[fullPixel];
    float3 currentNormal = SafeNormal(
        t_CurrentNormal[fullPixel].xyz, float3(0.0f, 1.0f, 0.0f));
    float currentAmbient = 1.0f;
    float3 currentIndirect = 0.0f;
#if ENABLE_AO
    currentAmbient = max(t_CurrentAmbient[pixel], 0.0f);
    if (!isfinite(currentAmbient))
        currentAmbient = 1.0f;
#endif
#if ENABLE_GI
    currentIndirect = max(t_CurrentIndirect[pixel].rgb, 0.0f);
    if (any(!isfinite(currentIndirect)))
        currentIndirect = 0.0f;
#endif

    float4 motion = t_Motion[fullPixel];
    float2 previousFullPosition = float2(fullPixel) + 0.5f + motion.xy;
    bool validHistory = g_Visibility.historyValid != 0u &&
        motion.a > 0.5f && all(isfinite(motion)) &&
        all(previousFullPosition >= 0.0f) &&
        all(previousFullPosition < g_Visibility.fullResolution);

    uint2 previousPixel = pixel;
    float similarity = 0.0f;
    if (validHistory)
    {
        float2 previousSamplingPosition = previousFullPosition /
            float(max(g_Visibility.resolutionScale, 1u));
        previousPixel = min(uint2(previousSamplingPosition),
            uint2(g_Visibility.samplingResolution) - 1u);
        float previousDepth = t_PreviousDepth[previousPixel];
        float expectedPreviousDepth = currentDepth + motion.z;
        if (isfinite(previousDepth) && isfinite(expectedPreviousDepth))
        {
            float relativeDepthError = abs(previousDepth - expectedPreviousDepth) /
                max(max(abs(previousDepth), abs(expectedPreviousDepth)), 1e-4f);
            float3 previousNormal = SafeNormal(
                t_PreviousNormal[previousPixel].xyz * 2.0f - 1.0f,
                currentNormal);
            float depthSimilarity = saturate(
                1.0f - relativeDepthError * 32.0f);
            float normalSimilarity = smoothstep(
                0.75f, 0.95f, dot(currentNormal, previousNormal));
            similarity = depthSimilarity * normalSimilarity;
        }
    }

    float ambientMinimum;
    float ambientMaximum;
    float3 indirectMinimum;
    float3 indirectMaximum;
    CurrentNeighborhoodBounds(
        pixel,
        ambientMinimum,
        ambientMaximum,
        indirectMinimum,
        indirectMaximum);

    // Match SSRT3's temporal contract: invalid reprojection selects the
    // current frame; valid history uses the configured response, and history
    // moves 25% toward the current four-diagonal neighborhood clamp.
    float currentWeight = lerp(
        1.0f, saturate(g_Visibility.temporalResponse), similarity);
#if ENABLE_AO
    float previousAmbient = t_PreviousAmbient[previousPixel];
    if (!validHistory || !(similarity > 0.0f) || !isfinite(previousAmbient))
        previousAmbient = currentAmbient;
    previousAmbient = lerp(
        previousAmbient,
        clamp(previousAmbient, ambientMinimum, ambientMaximum),
        0.25f);
    float resolvedAmbient = lerp(
        previousAmbient, currentAmbient, currentWeight);
    u_AmbientHistory[pixel] = isfinite(resolvedAmbient)
        ? min(max(resolvedAmbient, 0.0f), 65504.0f)
        : currentAmbient;
#endif
#if ENABLE_GI
    float3 previousIndirect = max(
        t_PreviousIndirect[previousPixel].rgb, 0.0f);
    if (!validHistory || !(similarity > 0.0f) ||
        any(!isfinite(previousIndirect)))
    {
        previousIndirect = currentIndirect;
    }
    previousIndirect = lerp(
        previousIndirect,
        clamp(previousIndirect, indirectMinimum, indirectMaximum),
        0.25f);
    float3 resolvedIndirect = lerp(
        previousIndirect, currentIndirect, currentWeight);
    if (any(!isfinite(resolvedIndirect)))
        resolvedIndirect = currentIndirect;
    u_IndirectHistory[pixel] = float4(
        min(max(resolvedIndirect, 0.0f), 65504.0f), 0.0f);
#endif
    u_DepthHistory[pixel] = currentDepth;
    u_NormalHistory[pixel] = float4(
        currentNormal * 0.5f + 0.5f, 1.0f);
}
