#pragma pack_matrix(row_major)

#include "specular_reflections_cb.h"

cbuffer c_Specular : register(b0)
{
    SpecularReflectionConstants g_Specular;
};

#include "specular_reflections_shared.hlsli"

Texture2D<float> t_Depth : register(t0);
Texture2D<float4> t_SceneColor : register(t1);

RWTexture2D<float> u_DepthMipZero : register(u0);
RWTexture2D<float4> u_SceneColorMipZero : register(u1);

[numthreads(8, 8, 1)]
void Prepare(uint2 pixel : SV_DispatchThreadID)
{
    if (!SpecularInViewport(pixel))
        return;
    const float depth = t_Depth[pixel];
    const float4 sceneColor = t_SceneColor[pixel];
    u_DepthMipZero[pixel] = isfinite(depth) ? depth : 0.0f;
    u_SceneColorMipZero[pixel] = all(isfinite(sceneColor))
        ? sceneColor
        : 0.0f;
}
