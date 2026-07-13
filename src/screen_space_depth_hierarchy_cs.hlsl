#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/binding_helpers.hlsli>
#include "screen_space_visibility_cb.h"

// Intel XeGTAO-inspired view-depth prefilter specialized for UVSR. One 8x8
// group covers a 16x16 source tile, writes full-resolution mip 0, and reduces
// four more levels through LDS in one dispatch. The far-depth-anchored smart
// average preserves slopes without expanding thin foreground silhouettes.

cbuffer c_Visibility : register(b0)
{
    ScreenSpaceVisibilityConstants g_Visibility;
};

Texture2D<float> t_Depth : register(t0);
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_DepthHierarchy[5] : register(u0);

groupshared float s_Depth[8][8];

static const float DepthHierarchyFar = 65504.0f;

bool IsValidDepth(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Visibility.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

float LinearizeDepth(uint2 pixel)
{
    if (any(pixel >= uint2(g_Visibility.fullResolution)))
        return DepthHierarchyFar;

    float deviceDepth = t_Depth[pixel];
    if (!IsValidDepth(deviceDepth))
        return DepthHierarchyFar;

    float4x4 projection = g_Visibility.view.matViewToClip;
    float denominator = deviceDepth * projection[2][3] - projection[2][2];
    if (!isfinite(denominator) || abs(denominator) <= 1e-6f)
        return DepthHierarchyFar;
    float viewZ = (projection[3][2] - deviceDepth * projection[3][3]) /
        denominator;
    return min(abs(viewZ), DepthHierarchyFar - 1.0f);
}

float SmartAverage(float z0, float z1, float z2, float z3)
{
    float zFar = max(max(z0, z1), max(z2, z3));
    float depthRange = max(0.75f * g_Visibility.radiusWorld, 1e-5f);
    float falloffRange = max(0.615f * depthRange, 1e-5f);
    float4 depths = float4(z0, z1, z2, z3);
    float4 weights = saturate((depthRange.xxxx - (zFar.xxxx - depths)) /
        falloffRange.xxxx);
    return dot(weights, depths) / max(dot(weights, 1.0f.xxxx), 1e-5f);
}

void StoreMip(uint mipLevel, uint2 coordinate, float depth)
{
    uint divisor = 1u << mipLevel;
    uint2 extent = (uint2(g_Visibility.fullResolution) + divisor - 1u) / divisor;
    if (all(coordinate < extent))
        u_DepthHierarchy[mipLevel][coordinate] = depth;
}

[numthreads(8, 8, 1)]
void main(uint2 groupId : SV_GroupID, uint2 threadId : SV_GroupThreadID)
{
    uint2 tileOrigin = groupId * 16u;
    uint2 source = tileOrigin + threadId * 2u;
    float z00 = LinearizeDepth(source + uint2(0u, 0u));
    float z10 = LinearizeDepth(source + uint2(1u, 0u));
    float z01 = LinearizeDepth(source + uint2(0u, 1u));
    float z11 = LinearizeDepth(source + uint2(1u, 1u));

    StoreMip(0u, source + uint2(0u, 0u), z00);
    StoreMip(0u, source + uint2(1u, 0u), z10);
    StoreMip(0u, source + uint2(0u, 1u), z01);
    StoreMip(0u, source + uint2(1u, 1u), z11);

    float reduced = SmartAverage(z00, z10, z01, z11);
    s_Depth[threadId.y][threadId.x] = reduced;
    StoreMip(1u, groupId * 8u + threadId, reduced);
    GroupMemoryBarrierWithGroupSync();

    if (all(threadId < 4u))
    {
        reduced = SmartAverage(
            s_Depth[threadId.y * 2u + 0u][threadId.x * 2u + 0u],
            s_Depth[threadId.y * 2u + 0u][threadId.x * 2u + 1u],
            s_Depth[threadId.y * 2u + 1u][threadId.x * 2u + 0u],
            s_Depth[threadId.y * 2u + 1u][threadId.x * 2u + 1u]);
    }
    GroupMemoryBarrierWithGroupSync();
    if (all(threadId < 4u))
    {
        s_Depth[threadId.y][threadId.x] = reduced;
        StoreMip(2u, groupId * 4u + threadId, reduced);
    }
    GroupMemoryBarrierWithGroupSync();

    if (all(threadId < 2u))
    {
        reduced = SmartAverage(
            s_Depth[threadId.y * 2u + 0u][threadId.x * 2u + 0u],
            s_Depth[threadId.y * 2u + 0u][threadId.x * 2u + 1u],
            s_Depth[threadId.y * 2u + 1u][threadId.x * 2u + 0u],
            s_Depth[threadId.y * 2u + 1u][threadId.x * 2u + 1u]);
    }
    GroupMemoryBarrierWithGroupSync();
    if (all(threadId < 2u))
    {
        s_Depth[threadId.y][threadId.x] = reduced;
        StoreMip(3u, groupId * 2u + threadId, reduced);
    }
    GroupMemoryBarrierWithGroupSync();

    if (all(threadId == 0u))
    {
        reduced = SmartAverage(s_Depth[0][0], s_Depth[0][1],
            s_Depth[1][0], s_Depth[1][1]);
        StoreMip(4u, groupId, reduced);
    }
}
