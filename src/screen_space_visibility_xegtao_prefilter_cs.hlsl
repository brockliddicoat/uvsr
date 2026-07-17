// SPDX-License-Identifier: MIT
// Intel XeGTAO 1.30 prefilter entry-point adapter, derived from archived
// GameTechDev/XeGTAO commit a5b1686c7ea37788eeb3576b5be47f7c03db532c.
// See screen_space_visibility_xegtao.hlsli for attribution and deviations.

#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "screen_space_visibility_xegtao.hlsli"

Texture2D<float> t_RawDepth : register(t0);
SamplerState s_PointClamp : register(s0);

VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_WorkingDepthMip0 : register(u0);
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_WorkingDepthMip1 : register(u1);
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_WorkingDepthMip2 : register(u2);
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_WorkingDepthMip3 : register(u3);
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_WorkingDepthMip4 : register(u4);

groupshared lpfloat g_XeGtaoScratchDepths[8][8];

lpfloat XeGTAO_ConvertAndClampViewDepth(float deviceDepth)
{
    // The UVSR hierarchy is R16_FLOAT, matching XeGTAO_ClampDepth's fp16 path.
    return (lpfloat)clamp(XeGTAO_ScreenSpaceToViewSpaceDepth(
        deviceDepth, g_XeGTAO), 0.0f, XE_GTAO_R16F_MAX);
}

lpfloat XeGTAO_LoadClampedViewDepth(
    uint2 viewportCoordinate,
    uint2 sourceDimensions)
{
    if (sourceDimensions.x == 0u || sourceDimensions.y == 0u ||
        g_XeGTAO.ViewportSize.x <= 0 || g_XeGTAO.ViewportSize.y <= 0)
        return (lpfloat)XE_GTAO_R16F_MAX;

    uint2 viewportMaximum = uint2(g_XeGTAO.ViewportSize - 1);
    uint2 clampedViewportCoordinate = min(
        viewportCoordinate, viewportMaximum);
    uint2 sourceCoordinate = min(
        g_XeGTAO.ViewportOrigin + clampedViewportCoordinate,
        sourceDimensions - 1u);
    return XeGTAO_ConvertAndClampViewDepth(
        t_RawDepth.Load(int3(sourceCoordinate, 0)).x);
}

void XeGTAO_StoreDepth(
    RWTexture2D<float> outputDepth,
    uint2 coordinate,
    float depth)
{
    uint width;
    uint height;
    outputDepth.GetDimensions(width, height);
    if (all(coordinate < uint2(width, height)))
        outputDepth[coordinate] = depth;
}

[numthreads(8, 8, 1)]
void main(
    uint2 dispatchThreadId : SV_DispatchThreadID,
    uint2 groupThreadId : SV_GroupThreadID)
{
    // Every lane must reach each group barrier. Interior lanes retain XeGTAO's
    // single GatherRed for the 2x2 source quad. Only invalid/padded/odd-edge
    // lanes use clamped loads so they cannot cross UVSR's source viewport.
    uint2 baseCoordinate = dispatchThreadId;
    uint2 pixelCoordinate = baseCoordinate * 2u;
    uint sourceWidth;
    uint sourceHeight;
    t_RawDepth.GetDimensions(sourceWidth, sourceHeight);
    uint2 sourceDimensions = uint2(sourceWidth, sourceHeight);
    uint2 viewportSize = uint2(max(g_XeGTAO.ViewportSize, int2(0, 0)));

    lpfloat depth0;
    lpfloat depth1;
    lpfloat depth2;
    lpfloat depth3;
    bool fullSourceQuad = sourceWidth > 0u && sourceHeight > 0u &&
        g_XeGTAO.ViewportSize.x > 0 && g_XeGTAO.ViewportSize.y > 0 &&
        all(pixelCoordinate + 1u < viewportSize) &&
        all(g_XeGTAO.ViewportOrigin + pixelCoordinate + 1u <
            sourceDimensions);
    [branch]
    if (fullSourceQuad)
    {
        float2 gatherPosition =
            (float2)(g_XeGTAO.ViewportOrigin + pixelCoordinate) /
            (float2)sourceDimensions;
        float4 deviceDepths = t_RawDepth.GatherRed(
            s_PointClamp, gatherPosition, int2(1, 1));
        // Gather component order is identical to XeGTAO_PrefilterDepths16x16.
        depth0 = XeGTAO_ConvertAndClampViewDepth(deviceDepths.w);
        depth1 = XeGTAO_ConvertAndClampViewDepth(deviceDepths.z);
        depth2 = XeGTAO_ConvertAndClampViewDepth(deviceDepths.x);
        depth3 = XeGTAO_ConvertAndClampViewDepth(deviceDepths.y);
    }
    else
    {
        depth0 = XeGTAO_LoadClampedViewDepth(
            pixelCoordinate + uint2(0u, 0u), sourceDimensions);
        depth1 = XeGTAO_LoadClampedViewDepth(
            pixelCoordinate + uint2(1u, 0u), sourceDimensions);
        depth2 = XeGTAO_LoadClampedViewDepth(
            pixelCoordinate + uint2(0u, 1u), sourceDimensions);
        depth3 = XeGTAO_LoadClampedViewDepth(
            pixelCoordinate + uint2(1u, 1u), sourceDimensions);
    }

    // Fill UVSR's padded mip-0 texels with the clamped logical edge. The main
    // pass can then use ordinary point-clamp sampling against the physical
    // hierarchy and obtain the same boundary value as upstream's unpadded SRV.
    XeGTAO_StoreDepth(u_WorkingDepthMip0,
        pixelCoordinate + uint2(0u, 0u), (float)depth0);
    XeGTAO_StoreDepth(u_WorkingDepthMip0,
        pixelCoordinate + uint2(1u, 0u), (float)depth1);
    XeGTAO_StoreDepth(u_WorkingDepthMip0,
        pixelCoordinate + uint2(0u, 1u), (float)depth2);
    XeGTAO_StoreDepth(u_WorkingDepthMip0,
        pixelCoordinate + uint2(1u, 1u), (float)depth3);

    lpfloat reducedDepth = XeGTAO_DepthMipFilter(
        depth0, depth1, depth2, depth3, g_XeGTAO);
    XeGTAO_StoreDepth(
        u_WorkingDepthMip1, baseCoordinate, (float)reducedDepth);
    g_XeGtaoScratchDepths[groupThreadId.x][groupThreadId.y] = reducedDepth;

    GroupMemoryBarrierWithGroupSync();

    if (all((groupThreadId % 2u) == 0u))
    {
        lpfloat mipDepth = XeGTAO_DepthMipFilter(
            g_XeGtaoScratchDepths[groupThreadId.x + 0u][groupThreadId.y + 0u],
            g_XeGtaoScratchDepths[groupThreadId.x + 1u][groupThreadId.y + 0u],
            g_XeGtaoScratchDepths[groupThreadId.x + 0u][groupThreadId.y + 1u],
            g_XeGtaoScratchDepths[groupThreadId.x + 1u][groupThreadId.y + 1u],
            g_XeGTAO);
        XeGTAO_StoreDepth(
            u_WorkingDepthMip2, baseCoordinate / 2u, (float)mipDepth);
        g_XeGtaoScratchDepths[groupThreadId.x][groupThreadId.y] = mipDepth;
    }

    GroupMemoryBarrierWithGroupSync();

    if (all((groupThreadId % 4u) == 0u))
    {
        lpfloat mipDepth = XeGTAO_DepthMipFilter(
            g_XeGtaoScratchDepths[groupThreadId.x + 0u][groupThreadId.y + 0u],
            g_XeGtaoScratchDepths[groupThreadId.x + 2u][groupThreadId.y + 0u],
            g_XeGtaoScratchDepths[groupThreadId.x + 0u][groupThreadId.y + 2u],
            g_XeGtaoScratchDepths[groupThreadId.x + 2u][groupThreadId.y + 2u],
            g_XeGTAO);
        XeGTAO_StoreDepth(
            u_WorkingDepthMip3, baseCoordinate / 4u, (float)mipDepth);
        g_XeGtaoScratchDepths[groupThreadId.x][groupThreadId.y] = mipDepth;
    }

    GroupMemoryBarrierWithGroupSync();

    if (all((groupThreadId % 8u) == 0u))
    {
        lpfloat mipDepth = XeGTAO_DepthMipFilter(
            g_XeGtaoScratchDepths[groupThreadId.x + 0u][groupThreadId.y + 0u],
            g_XeGtaoScratchDepths[groupThreadId.x + 4u][groupThreadId.y + 0u],
            g_XeGtaoScratchDepths[groupThreadId.x + 0u][groupThreadId.y + 4u],
            g_XeGtaoScratchDepths[groupThreadId.x + 4u][groupThreadId.y + 4u],
            g_XeGTAO);
        XeGTAO_StoreDepth(
            u_WorkingDepthMip4, baseCoordinate / 8u, (float)mipDepth);
    }
}
