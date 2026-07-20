#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "screen_space_visibility_cb.h"

#ifndef VISIBILITY_GROUP_SIZE_X
#define VISIBILITY_GROUP_SIZE_X 8
#endif
#ifndef VISIBILITY_GROUP_SIZE_Y
#define VISIBILITY_GROUP_SIZE_Y 8
#endif
#ifndef FUSED_PACKED_EDGE_RECONSTRUCTION
// 0 = exact legacy 2x2 guide math, 1 = packed-edge 2x2.
#define FUSED_PACKED_EDGE_RECONSTRUCTION 0
#endif
#ifndef ENABLE_AO_POWER
#define ENABLE_AO_POWER 0
#endif
#ifndef FILTER_EXPONENTIAL_MODE
#define FILTER_EXPONENTIAL_MODE 0
#endif
#ifndef FILTER_NORMAL_POWER_MODE
#define FILTER_NORMAL_POWER_MODE 0
#endif

cbuffer c_Visibility : register(b0)
{
    ScreenSpaceVisibilityConstants g_Visibility;
};

Texture2D<float> t_Ambient : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_Normal : register(t2);
Texture2D<float4> t_BaseLighting : register(t3);
Texture2D<float4> t_GBufferDiffuse : register(t4);
Texture2D<float4> t_GBufferEmissive : register(t5);
Texture2D<float> t_MaterialAmbientOcclusion : register(t6);
Texture2D<float4> t_GBufferMaterial : register(t7);
#if FUSED_PACKED_EDGE_RECONSTRUCTION
Texture2D<uint> t_PackedEdges : register(t8);
#endif

VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_Output : register(u0);

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
    float depthDelta =
        planeDistance + 0.5f * abs(sampleDepth - centerDepth);
    float depthWeight = exp(-planeDistance / depthScale) *
        exp(-abs(sampleDepth - centerDepth) / (depthScale * 2.0f));
    float normalBase = saturate(dot(centerNormalWS, sampleNormalWS));
    float normalWeight = pow(normalBase, 16.0f);
    return spatialWeight * depthWeight * normalWeight;
}

#if FUSED_PACKED_EDGE_RECONSTRUCTION
float PackedEdgeComponent(uint packedEdges, uint component)
{
    uint shift = component == 0u ? 6u
        : component == 1u ? 4u
        : component == 2u ? 2u : 0u;
    return float((packedEdges >> shift) & 3u) * (1.0f / 3.0f);
}

float SymmetricPackedEdge(
    int2 from,
    int2 to,
    uint direction,
    int2 maximumCoordinate)
{
    int2 clampedFrom = clamp(from, int2(0, 0), maximumCoordinate);
    int2 clampedTo = clamp(to, int2(0, 0), maximumCoordinate);
    if (all(clampedFrom == clampedTo))
        return 1.0f;
    uint opposite = direction ^ 1u;
    return min(
        PackedEdgeComponent(
            t_PackedEdges[uint2(clampedFrom)], direction),
        PackedEdgeComponent(
            t_PackedEdges[uint2(clampedTo)], opposite));
}

float PackedEdgeConnectivity(
    int2 anchor,
    int2 target,
    int2 maximumCoordinate)
{
    int2 current = clamp(anchor, int2(0, 0), maximumCoordinate);
    int2 clampedTarget = clamp(target, int2(0, 0), maximumCoordinate);
    float connectivity = 1.0f;
    int horizontalDirection = clampedTarget.x < current.x ? -1 : 1;
    [unroll]
    for (uint step = 0u; step < 3u; ++step)
    {
        if (current.x == clampedTarget.x)
            break;
        int2 next = current + int2(horizontalDirection, 0);
        connectivity *= SymmetricPackedEdge(
            current, next, horizontalDirection < 0 ? 0u : 1u,
            maximumCoordinate);
        current = clamp(next, int2(0, 0), maximumCoordinate);
    }
    int verticalDirection = clampedTarget.y < current.y ? -1 : 1;
    [unroll]
    for (uint step = 0u; step < 3u; ++step)
    {
        if (current.y == clampedTarget.y)
            break;
        int2 next = current + int2(0, verticalDirection);
        connectivity *= SymmetricPackedEdge(
            current, next, verticalDirection < 0 ? 2u : 3u,
            maximumCoordinate);
        current = clamp(next, int2(0, 0), maximumCoordinate);
    }
    return connectivity;
}
#endif

float ResolveAmbient(uint2 pixel, float3 centerNormalWS)
{
#if FUSED_PACKED_EDGE_RECONSTRUCTION
    uint scale = max(g_Visibility.resolutionScale, 1u);
    float2 samplingPosition = (float2(pixel) + 0.5f) /
        float(scale) - 0.5f;
    int2 maximumCoordinate = int2(g_Visibility.samplingResolution) - 1;
    int2 anchor = clamp(int2(round(samplingPosition)),
        int2(0, 0), maximumCoordinate);
    int2 base = int2(floor(samplingPosition));
    static const uint width = 2u;
    float totalWeight = 0.0f;
    float ambientSum = 0.0f;
    [unroll]
    for (uint y = 0u; y < width; ++y)
    {
        [unroll]
        for (uint x = 0u; x < width; ++x)
        {
            int2 coordinate = base + int2(x, y);
            uint2 sourcePixel = uint2(clamp(
                coordinate, int2(0, 0), maximumCoordinate));
            float2 delta = float2(coordinate) - samplingPosition;
            float spatialWeight = max(
                (1.0f - abs(delta.x)) *
                (1.0f - abs(delta.y)), 0.0f);
            float weight = spatialWeight * PackedEdgeConnectivity(
                anchor, int2(sourcePixel), maximumCoordinate);
            totalWeight += weight;
            ambientSum += max(t_Ambient[sourcePixel], 0.0f) * weight;
        }
    }
    float ambient = totalWeight > 1e-6f && isfinite(totalWeight)
        ? ambientSum * rcp(totalWeight)
        : max(t_Ambient[uint2(anchor)], 0.0f);
    ambient = isfinite(ambient)
        ? min(max(ambient, 0.0f), 65504.0f)
        : 1.0f;
    return f16tof32(f32tof16(ambient));
#else
    float centerDeviceDepth = t_Depth[pixel];
    if (!IsValidDepth(centerDeviceDepth))
        return 1.0f;

    float centerDepth = LinearViewDepth(centerDeviceDepth);
    float3 centerPositionVS;
    if (!ReconstructViewPosition(
            float2(pixel) + 0.5f,
            centerDeviceDepth,
            centerPositionVS))
    {
        return 1.0f;
    }
    float3 centerNormalVS = SafeNormal(
        mul(float4(centerNormalWS, 0.0f),
            g_Visibility.view.matWorldToView).xyz,
        float3(0.0f, 1.0f, 0.0f));

    uint scale = max(g_Visibility.resolutionScale, 1u);
    float2 samplingPosition = (float2(pixel) + 0.5f) /
        float(scale) - 0.5f;
    int2 base = int2(floor(samplingPosition));
    int2 maximumCoordinate = int2(g_Visibility.samplingResolution) - 1;
    float totalWeight = 0.0f;
    float ambientSum = 0.0f;
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
            uint2 samplingPixel = uint2(clamp(
                coordinate, int2(0, 0), maximumCoordinate));
            float weight = JointWeight(
                samplingPixel,
                spatialWeight,
                centerDepth,
                centerPositionVS,
                centerNormalWS,
                centerNormalVS);
            totalWeight += weight;
            ambientSum += max(t_Ambient[samplingPixel], 0.0f) * weight;
        }
    }

    float ambient;
    if (!(totalWeight > 1e-6f) || !isfinite(totalWeight))
    {
        uint2 fallbackPixel = uint2(clamp(
            int2(round(samplingPosition)),
            int2(0, 0), maximumCoordinate));
        ambient = max(t_Ambient[fallbackPixel], 0.0f);
    }
    else
    {
        ambient = ambientSum * rcp(max(totalWeight, 1e-6f));
    }
    ambient = isfinite(ambient)
        ? min(max(ambient, 0.0f), 65504.0f)
        : 1.0f;

    // The legacy path stores the resolved value to R16_FLOAT and reads it in
    // the composition pass. Preserve that quantization explicitly so pass
    // fusion does not silently become a precision change.
    return f16tof32(f32tof16(ambient));
#endif
}

[numthreads(VISIBILITY_GROUP_SIZE_X, VISIBILITY_GROUP_SIZE_Y, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(g_Visibility.fullResolution)))
        return;

    float3 normalWS = SafeNormal(
        t_Normal[pixel].xyz, float3(0.0f, 1.0f, 0.0f));
    float ambientVisibility = saturate(ResolveAmbient(pixel, normalWS));
#if ENABLE_AO_POWER
    float poweredAmbientVisibility = pow(
        ambientVisibility, max(g_Visibility.ambientPower, 0.01f));
#else
    float poweredAmbientVisibility = ambientVisibility;
#endif
    float adjustedAmbientVisibility = saturate(
        1.0f - g_Visibility.ambientStrength *
            (1.0f - poweredAmbientVisibility));

    float3 baseColor = max(t_GBufferDiffuse[pixel].rgb, 0.0f);
    float metalness = saturate(t_GBufferEmissive[pixel].a);
    float3 diffuseReflectance = baseColor * (1.0f - metalness);
    float4 packedMaterial = t_GBufferMaterial[pixel];
    float perceptualRoughness = saturate(t_Normal[pixel].w);
    float ior = lerp(1.0f, 3.0f, saturate(packedMaterial.b));
    float dielectricF0 = (ior - 1.0f) / max(ior + 1.0f, 1e-4f);
    dielectricF0 *= dielectricF0;
    float3 specularF0 = lerp(dielectricF0.xxx, baseColor, metalness);
    float materialAmbientOcclusion = saturate(
        t_MaterialAmbientOcclusion[pixel]);
    float hemisphere = normalWS.y * 0.5f + 0.5f;
    float3 approximateAmbientIrradiance = lerp(
        g_Visibility.ambientColorBottom,
        g_Visibility.ambientColorTop,
        hemisphere);
    float3 approximateFallbackIndirect = approximateAmbientIrradiance *
        diffuseReflectance * materialAmbientOcclusion *
        adjustedAmbientVisibility;
    float roughSpecularEnergy = lerp(
        1.0f, 0.55f, perceptualRoughness);
    float3 approximateFallbackSpecular = approximateAmbientIrradiance *
        specularF0 * roughSpecularEnergy * materialAmbientOcclusion *
        adjustedAmbientVisibility;

    float3 finalComposite = t_BaseLighting[pixel].rgb +
        approximateFallbackIndirect + approximateFallbackSpecular;
    if (any(!isfinite(finalComposite)))
        finalComposite = 0.0f;
    finalComposite = max(finalComposite, 0.0f);
    u_Output[pixel] = float4(min(finalComposite, 65504.0f), 0.0f);
}
