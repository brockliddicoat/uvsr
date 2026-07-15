#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "screen_space_visibility_cb.h"

#ifndef ENABLE_AO
#define ENABLE_AO 1
#endif
#ifndef ENABLE_GI
#define ENABLE_GI 1
#endif
#ifndef SPATIAL_FILTER
#define SPATIAL_FILTER 0
#endif

cbuffer c_Visibility : register(b0)
{
    ScreenSpaceVisibilityConstants g_Visibility;
};

#include "screen_space_visibility_common.hlsli"

Texture2D<float> t_Ambient : register(t0);
Texture2D<float4> t_Indirect : register(t1);
Texture2D<float> t_Depth : register(t2);
Texture2D<float4> t_Normal : register(t3);

VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_Ambient : register(u0);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_Indirect : register(u1);

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

bool ProjectViewPosition(float3 positionVS, out float2 pixelPosition)
{
    float4 clip = mul(
        float4(positionVS, 1.0f), g_Visibility.view.matViewToClip);
    if (!all(isfinite(clip)) || !(clip.w > 1e-6f))
    {
        pixelPosition = 0.0f;
        return false;
    }
    pixelPosition = clip.xy / clip.w *
        g_Visibility.view.clipToWindowScale +
        g_Visibility.view.clipToWindowBias;
    return all(isfinite(pixelPosition)) &&
        all(pixelPosition >= g_Visibility.view.viewportOrigin) &&
        all(pixelPosition < g_Visibility.view.viewportOrigin +
            g_Visibility.fullResolution);
}

float JointWeight(
    uint2 samplingPixel,
    float spatialWeight,
    float centerDepth,
    float3 centerPositionVS,
    float3 centerNormalWS,
    float3 centerNormalVS)
{
    uint2 guidePixel = SamplingToFullPixel(samplingPixel);
    float sampleDeviceDepth = t_Depth[guidePixel];
    if (!IsValidDepth(sampleDeviceDepth))
        return 0.0f;
    float sampleDepth = LinearViewDepth(sampleDeviceDepth);
    float3 samplePositionVS;
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
    float normalWeight = pow(
        saturate(dot(centerNormalWS, sampleNormalWS)), 16.0f);
    return spatialWeight * depthWeight * normalWeight;
}

void AccumulateTap(
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
    float weight = JointWeight(
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
    float centerDepth = LinearViewDepth(centerDeviceDepth);
    float3 centerPositionVS;
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

#if SPATIAL_FILTER == 0
    {
        // Compact joint bilateral: four taps for reduced-resolution
        // upsampling, or a symmetric 3x3 kernel for full-resolution cleanup.
        if (scale > 1u)
        {
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
                    AccumulateTap(coordinate, spatialWeight,
                        centerDepth, centerPositionVS,
                        centerNormalWS, centerNormalVS, totalWeight,
                        ambientSum, indirectSum);
                }
            }
        }
        else
        {
            [unroll]
            for (int y = -1; y <= 1; ++y)
            {
                [unroll]
                for (int x = -1; x <= 1; ++x)
                {
                    float spatialWeight = (x == 0 && y == 0)
                        ? 1.0f : ((x == 0 || y == 0) ? 0.5f : 0.25f);
                    AccumulateTap(samplingCenter + int2(x, y),
                        spatialWeight, centerDepth, centerPositionVS,
                        centerNormalWS, centerNormalVS,
                        totalWeight, ambientSum, indirectSum);
                }
            }
        }
    }
#else
    {
        // Follow SSRT3's diffuse denoiser structure: distribute taps in the
        // receiver's tangent plane, project them back to screen space, apply a
        // Gaussian with sigma=0.9*radius, then multiply by depth/normal
        // bilateral weights. Full resolution uses 16 taps; reduced modes use
        // one of four disjoint four-tap subsets selected by pixel parity.
        static const float2 disk[16] = {
            float2(0.0000f, 0.0000f), float2(0.5278f, -0.0859f),
            float2(-0.0401f, 0.5361f), float2(-0.6704f, -0.1799f),
            float2(0.2357f, 0.6917f), float2(0.7060f, 0.4242f),
            float2(-0.4639f, 0.6505f), float2(-0.8337f, 0.3061f),
            float2(-0.3318f, -0.7527f), float2(0.1261f, -0.8651f),
            float2(0.6249f, -0.6332f), float2(0.9386f, -0.0930f),
            float2(0.4387f, 0.8729f), float2(-0.1637f, 0.9525f),
            float2(-0.7296f, -0.5993f), float2(-0.9694f, -0.2104f)
        };
        uint tapCount = scale == 1u ? 16u : 4u;
        uint sampleOffset = scale == 1u ? 0u :
            ((pixel.x & 1u) + (pixel.y & 1u) * 2u) * 4u;
        float3 frameAxis = abs(centerNormalVS.z) < 0.999f
            ? float3(0.0f, 0.0f, 1.0f)
            : float3(0.0f, 1.0f, 0.0f);
        float3 tangentVS = SafeNormal(
            cross(frameAxis, centerNormalVS),
            float3(1.0f, 0.0f, 0.0f));
        float3 bitangentVS = SafeNormal(
            cross(centerNormalVS, tangentVS),
            float3(0.0f, 1.0f, 0.0f));
        float4x4 projection = g_Visibility.view.matViewToClip;
        float clipW = centerPositionVS.z * projection[2][3] +
            projection[3][3];
        float footprintX = abs(clipW /
            max(abs(projection[0][0] *
                g_Visibility.view.clipToWindowScale.x), 1e-6f));
        float footprintY = abs(clipW /
            max(abs(projection[1][1] *
                g_Visibility.view.clipToWindowScale.y), 1e-6f));
        float worldRadius = max(g_Visibility.spatialRadius *
            0.5f * (footprintX + footprintY), 1e-5f);
        float sigma = max(0.9f * worldRadius, 1e-5f);
        [loop]
        for (uint tap = 0u; tap < tapCount; ++tap)
        {
            float2 tangentOffset = disk[(sampleOffset + tap) & 15u] *
                worldRadius;
            float3 targetPositionVS = centerPositionVS +
                tangentVS * tangentOffset.x +
                bitangentVS * tangentOffset.y;
            float2 targetPixelPosition;
            if (!ProjectViewPosition(targetPositionVS, targetPixelPosition))
                continue;
            float2 targetSamplingPosition = targetPixelPosition /
                float(scale) - 0.5f;
            int2 coordinate = int2(round(targetSamplingPosition));
            float spatialWeight = exp(-dot(tangentOffset, tangentOffset) /
                (2.0f * sigma * sigma));
            AccumulateTap(coordinate, spatialWeight,
                centerDepth, centerPositionVS,
                centerNormalWS, centerNormalVS, totalWeight,
                ambientSum, indirectSum);
        }
    }
#endif

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
