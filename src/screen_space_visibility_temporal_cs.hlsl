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

Texture2D<float> t_RawAmbientVisibility : register(t0);
Texture2D<float4> t_RawIndirectDiffuse : register(t1);
Texture2D<float> t_HistoryAmbientVisibility : register(t2);
Texture2D<float4> t_HistoryIndirectDiffuse : register(t3);
Texture2D<float> t_CurrentDepth : register(t4);
Texture2D<float4> t_CurrentNormal : register(t5);
Texture2D<float> t_HistoryDepth : register(t6);
Texture2D<float4> t_HistoryNormal : register(t7);
Texture2D<float4> t_MotionVectors : register(t8);
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_HistoryAmbientVisibility : register(u0);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_HistoryIndirectDiffuse : register(u1);
VK_IMAGE_FORMAT("r32f") RWTexture2D<float> u_HistoryDepth : register(u2);
VK_IMAGE_FORMAT("rgba8") RWTexture2D<float4> u_HistoryNormal : register(u3);
VK_IMAGE_FORMAT("r8") RWTexture2D<float> u_HistoryValidity : register(u4);

float3 SafeNormal(float3 value)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12f && isfinite(lengthSquared)
        ? value * rsqrt(lengthSquared)
        : 0.0f;
}

bool ValidDepth(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Visibility.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

bool PositiveViewDepth(float deviceDepth, out float viewDepth)
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

uint2 RepresentativePixel(uint2 samplingPixel)
{
    uint2 cellOrigin = samplingPixel * g_Visibility.resolutionScale;
    uint2 fullMaximum = uint2(g_Visibility.fullResolution) - 1u;
    uint2 selectedPixel = min(cellOrigin, fullMaximum);
    float selectedDepth = t_CurrentDepth[selectedPixel];
    if (g_Visibility.resolutionScale == 1u)
        return selectedPixel;
    bool foundValid = ValidDepth(selectedDepth);

    [loop]
    for (uint y = 0u; y < g_Visibility.resolutionScale; ++y)
    {
        [loop]
        for (uint x = 0u; x < g_Visibility.resolutionScale; ++x)
        {
            uint2 candidate = cellOrigin + uint2(x, y);
            if (any(candidate >= uint2(g_Visibility.fullResolution)))
                continue;
            float depth = t_CurrentDepth[candidate];
            if (!ValidDepth(depth))
                continue;
            bool closer = !foundValid || (g_Visibility.reverseDepth != 0u
                ? depth > selectedDepth : depth < selectedDepth);
            if (closer)
            {
                selectedPixel = candidate;
                selectedDepth = depth;
                foundValid = true;
            }
        }
    }
    return selectedPixel;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(g_Visibility.samplingResolution)))
        return;

    float currentAmbient = ENABLE_AO != 0
        ? saturate(t_RawAmbientVisibility[pixel]) : 1.0f;
    float3 currentGi = ENABLE_GI != 0
        ? max(t_RawIndirectDiffuse[pixel].rgb, 0.0f) : 0.0f;
    if (any(!isfinite(currentGi)))
        currentGi = 0.0f;

    float ambientMinimum = currentAmbient;
    float ambientMaximum = currentAmbient;
    float3 giMinimum = currentGi;
    float3 giMaximum = currentGi;

    uint2 fullPixel = RepresentativePixel(pixel);
    float currentDepth = t_CurrentDepth[fullPixel];
    float3 currentNormal = SafeNormal(t_CurrentNormal[fullPixel].xyz);
    float3 motion = t_MotionVectors[fullPixel].xyz;
    float2 previousFullCenter = float2(fullPixel) + 0.5f + motion.xy;
    float2 previousUv = previousFullCenter / g_Visibility.fullResolution;

    bool inside = all(previousFullCenter >= 0.5f) &&
        all(previousFullCenter <= g_Visibility.fullResolution - 0.5f);
    bool historyAccepted = g_Visibility.temporalEnabled != 0u &&
        g_Visibility.historyValid != 0u && inside && ValidDepth(currentDepth) &&
        dot(currentNormal, currentNormal) > 0.0f;

    float historyAmbient = currentAmbient;
    float3 historyGi = currentGi;
    if (historyAccepted)
    {
        int2 previousHistoryPixel = clamp(
            int2(previousUv * g_Visibility.samplingResolution),
            0,
            int2(g_Visibility.samplingResolution) - 1);
        float previousDepth = t_HistoryDepth[previousHistoryPixel];
        float3 previousNormal = SafeNormal(
            t_HistoryNormal[previousHistoryPixel].xyz * 2.0f - 1.0f);
        float expectedPreviousDepth = currentDepth + motion.z;
        float previousViewDepth;
        float expectedPreviousViewDepth;
        bool depthAccepted = ValidDepth(previousDepth) &&
            ValidDepth(expectedPreviousDepth) &&
            PositiveViewDepth(previousDepth, previousViewDepth) &&
            PositiveViewDepth(expectedPreviousDepth, expectedPreviousViewDepth);
        float depthTolerance = g_Visibility.depthRejection *
            max(expectedPreviousViewDepth, 0.1f);
        depthAccepted = depthAccepted &&
            abs(previousViewDepth - expectedPreviousViewDepth) <= depthTolerance;
        bool normalAccepted = dot(currentNormal, previousNormal) >=
            g_Visibility.normalRejection;
        historyAccepted = depthAccepted && normalAccepted;

        if (historyAccepted)
        {
            // Build the current-frame clamp only after reprojection passes its
            // cheap bounds/depth/normal tests. Rejected and first-frame pixels
            // avoid all four neighboring AO/GI signal reads.
            static const int2 ClampOffsets[4] = {
                int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
            };
            [unroll]
            for (uint offsetIndex = 0u; offsetIndex < 4u; ++offsetIndex)
            {
                int2 neighbor = clamp(int2(pixel) + ClampOffsets[offsetIndex], 0,
                    int2(g_Visibility.samplingResolution) - 1);
                if (ENABLE_AO != 0)
                {
                    float ambient = saturate(t_RawAmbientVisibility[neighbor]);
                    ambientMinimum = min(ambientMinimum, ambient);
                    ambientMaximum = max(ambientMaximum, ambient);
                }
                if (ENABLE_GI != 0)
                {
                    float3 gi = max(t_RawIndirectDiffuse[neighbor].rgb, 0.0f);
                    giMinimum = min(giMinimum, gi);
                    giMaximum = max(giMaximum, gi);
                }
            }

            if (ENABLE_AO != 0)
            {
                historyAmbient = t_HistoryAmbientVisibility[previousHistoryPixel];
                historyAmbient = clamp(historyAmbient, ambientMinimum, ambientMaximum);
            }
            if (ENABLE_GI != 0)
            {
                historyGi = t_HistoryIndirectDiffuse[previousHistoryPixel].rgb;
                historyGi = clamp(historyGi, giMinimum, giMaximum);
            }
        }
    }

    float motionRejection = saturate(length(
        motion.xy / max(g_Visibility.fullResolution, 1.0f)) * 96.0f);
    float aoHistoryWeight = ENABLE_AO != 0 && historyAccepted
        ? g_Visibility.aoTemporalResponse * (1.0f - motionRejection)
        : 0.0f;
    float giHistoryWeight = ENABLE_GI != 0 && historyAccepted
        ? g_Visibility.giTemporalResponse * (1.0f - motionRejection)
        : 0.0f;

    float accumulatedAmbient = lerp(currentAmbient, historyAmbient, aoHistoryWeight);
    float3 accumulatedGi = lerp(currentGi, historyGi, giHistoryWeight);
#if ENABLE_AO
    u_HistoryAmbientVisibility[pixel] = saturate(accumulatedAmbient);
#endif
#if ENABLE_GI
    u_HistoryIndirectDiffuse[pixel] = float4(
        min(max(accumulatedGi, 0.0f), 65504.0f), 1.0f);
#endif
    u_HistoryDepth[pixel] = currentDepth;
    // Eight-bit packed history normals are ample for the default 0.85 dot
    // rejection and cut this full-resolution history surface to one quarter.
    u_HistoryNormal[pixel] = float4(currentNormal * 0.5f + 0.5f, 1.0f);
    u_HistoryValidity[pixel] = max(aoHistoryWeight, giHistoryWeight);
}
