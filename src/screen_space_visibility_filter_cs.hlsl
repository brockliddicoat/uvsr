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
#ifndef FILTER_EXACT_FAST_MATH
#define FILTER_EXACT_FAST_MATH 0
#endif
#ifndef FILTER_POSITION_MODE
#define FILTER_POSITION_MODE FILTER_EXACT_FAST_MATH
#endif
#ifndef FILTER_EXPONENTIAL_MODE
#define FILTER_EXPONENTIAL_MODE FILTER_EXACT_FAST_MATH
#endif
#ifndef FILTER_NORMAL_POWER_MODE
#define FILTER_NORMAL_POWER_MODE FILTER_EXACT_FAST_MATH
#endif
#ifndef FILTER_GAUSSIAN_MODE
#define FILTER_GAUSSIAN_MODE FILTER_EXACT_FAST_MATH
#endif
#ifndef PACKED_EDGE_RECONSTRUCTION
// 0 = legacy guide reconstruction, 1 = packed-edge 2x2.
#define PACKED_EDGE_RECONSTRUCTION 0
#endif
#ifndef PACKED_EDGE_CONTROLLED_LEAKAGE
#define PACKED_EDGE_CONTROLLED_LEAKAGE 0
#endif
#ifndef VISIBILITY_GROUP_SIZE_X
#define VISIBILITY_GROUP_SIZE_X 8
#endif
#ifndef VISIBILITY_GROUP_SIZE_Y
#define VISIBILITY_GROUP_SIZE_Y 8
#endif
#define SELECTED_FILTER_POSITION_MODE FILTER_POSITION_MODE
#define SELECTED_FILTER_EXPONENTIAL_MODE FILTER_EXPONENTIAL_MODE
#define SELECTED_FILTER_NORMAL_POWER_MODE FILTER_NORMAL_POWER_MODE
#define SELECTED_FILTER_GAUSSIAN_MODE FILTER_GAUSSIAN_MODE

cbuffer c_Visibility : register(b0)
{
    ScreenSpaceVisibilityConstants g_Visibility;
};

Texture2D<float> t_Ambient : register(t0);
Texture2D<float4> t_Indirect : register(t1);
Texture2D<float> t_Depth : register(t2);
Texture2D<float4> t_Normal : register(t3);
#if PACKED_EDGE_RECONSTRUCTION
Texture2D<uint> t_PackedEdges : register(t4);
#endif

VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_Ambient : register(u0);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_Indirect : register(u1);

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

bool ReconstructViewPositionAndDepth(
    float2 pixelPosition,
    float deviceDepth,
    out float3 positionVS,
    out float linearDepth)
{
    float4x4 projection = g_Visibility.view.matViewToClip;
    float denominator = deviceDepth * projection[2][3] - projection[2][2];
    if (SELECTED_FILTER_POSITION_MODE == 2u)
    {
        denominator = denominator < 0.0f
            ? min(denominator, -1e-6f)
            : max(denominator, 1e-6f);
    }
    else if (!isfinite(denominator) || abs(denominator) <= 1e-6f)
    {
        positionVS = 0.0f;
        linearDepth = 65504.0f;
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
    linearDepth = abs(viewZ);
    return all(isfinite(positionVS)) && isfinite(linearDepth);
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

uint2 SamplingToFullPixel(uint2 samplingPixel)
{
    uint scale = max(g_Visibility.resolutionScale, 1u);
    return min(samplingPixel * scale + scale / 2u,
        uint2(g_Visibility.fullResolution) - 1u);
}

uint2 ReconstructionGuidePixel(uint2 samplingPixel)
{
    return SamplingToFullPixel(samplingPixel);
}

float JointWeight(
    uint2 samplingPixel,
    float spatialWeight,
    float centerDepth,
    float3 centerPositionVS,
    float3 centerNormalWS,
    float3 centerNormalVS)
{
    uint2 guidePixel = ReconstructionGuidePixel(samplingPixel);
    float sampleDeviceDepth = t_Depth[guidePixel];
    if (!IsValidDepth(sampleDeviceDepth))
        return 0.0f;
    float sampleDepth;
    float3 samplePositionVS;
    bool samplePositionValid;
    if (SELECTED_FILTER_POSITION_MODE > 0u)
    {
        samplePositionValid = ReconstructViewPositionAndDepth(
            float2(guidePixel) + 0.5f,
            sampleDeviceDepth,
            samplePositionVS,
            sampleDepth);
    }
    else
    {
        sampleDepth = LinearViewDepth(sampleDeviceDepth);
        samplePositionValid = ReconstructViewPosition(
            float2(guidePixel) + 0.5f,
            sampleDeviceDepth,
            samplePositionVS);
    }
    if (!samplePositionValid)
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
    float depthWeight;
    if (SELECTED_FILTER_EXPONENTIAL_MODE == 1u)
    {
        depthWeight = exp(-depthDelta / depthScale);
    }
    else if (SELECTED_FILTER_EXPONENTIAL_MODE == 2u)
    {
        depthWeight = exp2(
            -depthDelta / depthScale * 1.4426950408889634f);
    }
    else
    {
        depthWeight = exp(-planeDistance / depthScale) *
            exp(-abs(sampleDepth - centerDepth) /
                (depthScale * 2.0f));
    }
    float normalBase = saturate(dot(centerNormalWS, sampleNormalWS));
    float normalWeight;
    if (SELECTED_FILTER_NORMAL_POWER_MODE == 1u)
    {
        normalWeight = normalBase;
        normalWeight *= normalWeight;
        normalWeight *= normalWeight;
        normalWeight *= normalWeight;
        normalWeight *= normalWeight;
    }
    else if (SELECTED_FILTER_NORMAL_POWER_MODE == 2u)
    {
        normalWeight = normalBase > 0.0f
            ? exp2(log2(normalBase) * 16.0f)
            : 0.0f;
    }
    else
    {
        normalWeight = pow(normalBase, 16.0f);
    }
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

#if PACKED_EDGE_RECONSTRUCTION
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
    uint fromPacked = t_PackedEdges[uint2(clampedFrom)];
    uint toPacked = t_PackedEdges[uint2(clampedTo)];
    return min(
        PackedEdgeComponent(fromPacked, direction),
        PackedEdgeComponent(toPacked, opposite));
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
        uint direction = horizontalDirection < 0 ? 0u : 1u;
        connectivity *= SymmetricPackedEdge(
            current, next, direction, maximumCoordinate);
        current = clamp(next, int2(0, 0), maximumCoordinate);
    }
    int verticalDirection = clampedTarget.y < current.y ? -1 : 1;
    [unroll]
    for (uint step = 0u; step < 3u; ++step)
    {
        if (current.y == clampedTarget.y)
            break;
        int2 next = current + int2(0, verticalDirection);
        uint direction = verticalDirection < 0 ? 2u : 3u;
        connectivity *= SymmetricPackedEdge(
            current, next, direction, maximumCoordinate);
        current = clamp(next, int2(0, 0), maximumCoordinate);
    }
#if PACKED_EDGE_CONTROLLED_LEAKAGE
    connectivity = max(connectivity, 1.0f / 255.0f);
#endif
    return connectivity;
}
#endif

[numthreads(VISIBILITY_GROUP_SIZE_X, VISIBILITY_GROUP_SIZE_Y, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(g_Visibility.fullResolution)))
        return;

#if PACKED_EDGE_RECONSTRUCTION
    uint packedScale = max(g_Visibility.resolutionScale, 1u);
    float2 packedSamplingPosition = (float2(pixel) + 0.5f) /
        float(packedScale) - 0.5f;
    int2 packedMaximum = int2(g_Visibility.samplingResolution) - 1;
    int2 packedAnchor = clamp(
        int2(round(packedSamplingPosition)),
        int2(0, 0), packedMaximum);
    int2 packedBase = int2(floor(packedSamplingPosition));
    static const uint packedWidth = 2u;
    float packedTotalWeight = 0.0f;
    float packedAmbientSum = 0.0f;
    float3 packedIndirectSum = 0.0f;
    [unroll]
    for (uint packedY = 0u; packedY < packedWidth; ++packedY)
    {
        [unroll]
        for (uint packedX = 0u; packedX < packedWidth; ++packedX)
        {
            int2 packedCoordinate = packedBase +
                int2(packedX, packedY);
            uint2 packedPixel = uint2(clamp(
                packedCoordinate, int2(0, 0), packedMaximum));
            float2 packedDelta = float2(packedCoordinate) -
                packedSamplingPosition;
            float packedSpatialWeight = max(
                (1.0f - abs(packedDelta.x)) *
                (1.0f - abs(packedDelta.y)), 0.0f);
            float packedWeight = packedSpatialWeight *
                PackedEdgeConnectivity(
                    packedAnchor,
                    int2(packedPixel),
                    packedMaximum);
            packedTotalWeight += packedWeight;
#if ENABLE_AO
            packedAmbientSum += max(
                t_Ambient[packedPixel], 0.0f) * packedWeight;
#endif
#if ENABLE_GI
            packedIndirectSum += max(
                t_Indirect[packedPixel].rgb, 0.0f) * packedWeight;
#endif
        }
    }
    if (!(packedTotalWeight > 1e-6f) ||
        !isfinite(packedTotalWeight))
    {
        packedTotalWeight = 1.0f;
        uint2 packedFallback = uint2(packedAnchor);
#if ENABLE_AO
        packedAmbientSum = max(t_Ambient[packedFallback], 0.0f);
#endif
#if ENABLE_GI
        packedIndirectSum = max(t_Indirect[packedFallback].rgb, 0.0f);
#endif
    }
    float packedInverseWeight = rcp(packedTotalWeight);
#if ENABLE_AO
    u_Ambient[pixel] = min(max(
        packedAmbientSum * packedInverseWeight, 0.0f), 65504.0f);
#endif
#if ENABLE_GI
    u_Indirect[pixel] = float4(min(max(
        packedIndirectSum * packedInverseWeight, 0.0f), 65504.0f), 0.0f);
#endif
    return;
#endif

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
    float centerDepth;
    bool centerPositionValid;
    if (SELECTED_FILTER_POSITION_MODE > 0u)
    {
        centerPositionValid = ReconstructViewPositionAndDepth(
            float2(pixel) + 0.5f,
            centerDeviceDepth,
            centerPositionVS,
            centerDepth);
    }
    else
    {
        centerDepth = LinearViewDepth(centerDeviceDepth);
        centerPositionValid = ReconstructViewPosition(
            float2(pixel) + 0.5f,
            centerDeviceDepth,
            centerPositionVS);
    }
    if (!centerPositionValid)
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
        // sigma = 0.9 * radius makes radius cancel from the Gaussian term:
        // exp(-dot(radius*p, radius*p) / (2*(0.9*radius)^2)).
        // These FP32 constants are the offline-rounded values for the fixed
        // disk above, eliminating a transcendental evaluation per tap.
        static const float diskGaussianWeightExact[16] = {
            1.0f, 0.8381876f, 0.8366060f, 0.7427413f,
            0.7191886f, 0.657865942f, 0.6743235f, 0.6145380f,
            0.6585701f, 0.623884737f, 0.613518655f, 0.5774419f,
            0.5548024f, 0.5618185f, 0.5767801f, 0.54475987f
        };
        static const min16float diskGaussianWeightApproximate[16] = {
            1.000h, 0.838h, 0.837h, 0.743h,
            0.719h, 0.658h, 0.674h, 0.615h,
            0.659h, 0.624h, 0.614h, 0.577h,
            0.555h, 0.562h, 0.577h, 0.545h
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
            uint weightIndex = (sampleOffset + tap) & 15u;
            float spatialWeight;
            if (SELECTED_FILTER_GAUSSIAN_MODE == 1u)
            {
                spatialWeight = diskGaussianWeightExact[weightIndex];
            }
            else if (SELECTED_FILTER_GAUSSIAN_MODE == 2u)
            {
                spatialWeight = float(
                    diskGaussianWeightApproximate[weightIndex]);
            }
            else
            {
                spatialWeight =
                    exp(-dot(tangentOffset, tangentOffset) /
                        (2.0f * sigma * sigma));
            }
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
