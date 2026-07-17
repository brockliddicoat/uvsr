// SPDX-License-Identifier: MIT
// Intel XeGTAO 1.30 sharp-denoiser entry-point adapter, derived from archived
// GameTechDev/XeGTAO commit a5b1686c7ea37788eeb3576b5be47f7c03db532c.
// See screen_space_visibility_xegtao.hlsli for attribution and deviations.

#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "screen_space_visibility_xegtao.hlsli"

Texture2D<float> t_WorkingAo : register(t0);
Texture2D<float> t_WorkingEdges : register(t1);
SamplerState s_PointClamp : register(s0);

VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_OutputAo : register(u0);

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void main(uint2 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelBase = dispatchThreadId * uint2(2u, 1u);
    if (g_XeGTAO.ViewportSize.x <= 0 || g_XeGTAO.ViewportSize.y <= 0 ||
        pixelBase.x >= (uint)g_XeGTAO.ViewportSize.x ||
        pixelBase.y >= (uint)g_XeGTAO.ViewportSize.y)
        return;

    XeGTAO_Denoise(pixelBase, g_XeGTAO,
        t_WorkingAo, t_WorkingEdges, s_PointClamp, u_OutputAo,
        XE_GTAO_DENOISE_FINAL != 0);
}
