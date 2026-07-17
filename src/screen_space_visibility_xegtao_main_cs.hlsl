// SPDX-License-Identifier: MIT
// Intel XeGTAO 1.30 High-preset entry-point adapter, derived from archived
// GameTechDev/XeGTAO commit a5b1686c7ea37788eeb3576b5be47f7c03db532c.
// See screen_space_visibility_xegtao.hlsli for attribution and deviations.

#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "screen_space_visibility_xegtao.hlsli"

Texture2D<float> t_WorkingDepth : register(t0);
Texture2D<float4> t_WorldNormals : register(t1);
Texture2D<uint> t_HilbertLut : register(t2);
SamplerState s_PointClamp : register(s0);

// The R16_FLOAT AO adapter retains Intel's /1.5 convention and explicit R8
// quantization while remaining directly sampleable by UVSR's float consumer.
VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_WorkingAo : register(u0);
VK_IMAGE_FORMAT("r8") RWTexture2D<unorm float> u_WorkingEdges : register(u1);

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (g_XeGTAO.ViewportSize.x <= 0 || g_XeGTAO.ViewportSize.y <= 0 ||
        any(pixel >= uint2(g_XeGTAO.ViewportSize)))
        return;

    lpfloat3 viewspaceNormal;
    if (!XeGTAO_LoadViewspaceNormal(
            pixel, g_XeGTAO, t_WorldNormals, viewspaceNormal))
    {
        XeGTAO_OutputUnoccluded(pixel, u_WorkingAo, u_WorkingEdges);
        return;
    }

    lpfloat2 noise = XeGTAO_SpatioTemporalNoise(
        pixel, g_XeGTAO.NoiseIndex, t_HilbertLut);

#if XE_GTAO_QUALITY == 0
    XeGTAO_MainPass(pixel, 1u, 2u, noise, viewspaceNormal, g_XeGTAO,
        t_WorkingDepth, s_PointClamp, u_WorkingAo, u_WorkingEdges);
#elif XE_GTAO_QUALITY == 1
    XeGTAO_MainPass(pixel, 2u, 2u, noise, viewspaceNormal, g_XeGTAO,
        t_WorkingDepth, s_PointClamp, u_WorkingAo, u_WorkingEdges);
#elif XE_GTAO_QUALITY == 2
    XeGTAO_MainPass(pixel, 3u, 3u, noise, viewspaceNormal, g_XeGTAO,
        t_WorkingDepth, s_PointClamp, u_WorkingAo, u_WorkingEdges);
#elif XE_GTAO_QUALITY == 3
    XeGTAO_MainPass(pixel, 9u, 3u, noise, viewspaceNormal, g_XeGTAO,
        t_WorkingDepth, s_PointClamp, u_WorkingAo, u_WorkingEdges);
#else
#error XE_GTAO_QUALITY must be 0 (Low), 1 (Medium), 2 (High), or 3 (Ultra)
#endif
}
