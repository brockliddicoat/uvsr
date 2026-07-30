#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/utils.hlsli>
#include "sparse_virtual_shadow_map_cb.h"

cbuffer c_Svsm : register(b0)
{
    SparseVirtualShadowMapResolveConstants g_Svsm;
};

Texture2D<float> t_CameraDepth : register(t0);
Texture2DArray<uint> t_DenseDepth : register(t1);

VK_IMAGE_FORMAT("r8") RWTexture2D<float> u_Visibility : register(u0);
VK_IMAGE_FORMAT("r8") RWTexture2D<float> u_Debug : register(u1);

static const float2 c_Poisson16[16] = {
    float2(-0.3935238f, 0.7530643f),
    float2(-0.3022015f, 0.2976640f),
    float2(0.09813362f, 0.1924510f),
    float2(-0.7593753f, 0.5187950f),
    float2(0.2293134f, 0.7607011f),
    float2(0.6505286f, 0.6297367f),
    float2(0.5322764f, 0.2350069f),
    float2(0.8581018f, -0.01624052f),
    float2(-0.6928226f, 0.07119545f),
    float2(-0.3114384f, -0.3017288f),
    float2(0.2837671f, -0.1797430f),
    float2(-0.3093514f, -0.7492560f),
    float2(-0.7386893f, -0.5215692f),
    float2(0.3988827f, -0.6170120f),
    float2(0.8114883f, -0.4580260f),
    float2(0.08265103f, -0.8939569f)
};

bool TryDenseTap(
    uint level,
    float2 virtualPosition,
    float receiverDepth,
    uint tap,
    uint taps,
    out float lit)
{
    float2 offset = taps == 1u
        ? 0.0f
        : c_Poisson16[tap * (16u / taps)] * 3.0f;
    int2 texel = int2(floor(virtualPosition + offset));
    if (any(texel < 0) || any(texel >= 8192))
    {
        lit = 1.0f;
        return false;
    }

    float casterDepth =
        asfloat(t_DenseDepth.Load(int4(texel, level, 0)));
    lit = casterDepth <= receiverDepth + g_Svsm.depthBias
        ? 1.0f
        : 0.0f;
    return true;
}

bool TryDenseClipmap(
    uint level,
    float3 worldPosition,
    out float visibility)
{
    float4 clip = mul(float4(worldPosition, 1.0f),
        g_Svsm.worldToClip[level]);
    if (!(clip.w != 0.0f) || !all(isfinite(clip)))
    {
        visibility = 1.0f;
        return false;
    }

    float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f)
    {
        visibility = 1.0f;
        return false;
    }

    float2 virtualPosition =
        (ndc.xy * float2(0.5f, -0.5f) + 0.5f) * 8192.0f;
    uint taps = g_Svsm.tapCount;
    uint probeCount = min(taps, 4u);
    float probeLit[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int2 centerTexel = int2(floor(virtualPosition));
    int2 pageTexel = centerTexel % 128;
    bool pageSafeFootprint =
        all(pageTexel >= 3) &&
        all(pageTexel <= 124);
    bool adaptive =
        g_Svsm.adaptiveFiltering != 0u &&
        taps > 1u &&
        pageSafeFootprint;
    if (adaptive)
    {
        bool probesAgree = true;
        [loop]
        for (uint probe = 0u; probe < probeCount; ++probe)
        {
            uint probeTap =
                probe * (taps / probeCount);
            if (!TryDenseTap(
                    level,
                    virtualPosition,
                    ndc.z,
                    probeTap,
                    taps,
                    probeLit[probe]))
            {
                visibility = 1.0f;
                return false;
            }
            probesAgree = probesAgree &&
                (probe == 0u ||
                    probeLit[probe] == probeLit[0]);
        }
        if (probesAgree)
        {
            visibility = probeLit[0];
            return true;
        }
    }

    float lit = 0.0f;
    [loop]
    for (uint tap = 0u; tap < taps; ++tap)
    {
        float tapLit = 0.0f;
        bool reusedProbe = false;
        if (adaptive)
        {
            [loop]
            for (uint probe = 0u;
                probe < probeCount;
                ++probe)
            {
                uint probeTap =
                    probe * (taps / probeCount);
                if (tap == probeTap)
                {
                    tapLit = probeLit[probe];
                    reusedProbe = true;
                }
            }
        }
        if (!reusedProbe && !TryDenseTap(
                level,
                virtualPosition,
                ndc.z,
                tap,
                taps,
                tapLit))
        {
            visibility = 1.0f;
            return false;
        }
        lit += tapLit;
    }

    visibility = lit / float(taps);
    return true;
}

float DenseDebugValue(
    uint selectedLevel,
    uint firstLevel,
    float visibility,
    bool missing)
{
    if (g_Svsm.debugView == 0u)
        return visibility;
    if (g_Svsm.debugView == 1u)
        return missing ? 0.0f :
            float(selectedLevel + 1u) / float(SVSM_CLIPMAP_COUNT);
    if (g_Svsm.debugView == 2u ||
        g_Svsm.debugView == 3u ||
        g_Svsm.debugView == 6u)
    {
        return missing ? 0.0f : 1.0f;
    }
    if (g_Svsm.debugView == 4u ||
        g_Svsm.debugView == 5u)
    {
        return 0.0f;
    }
    if (g_Svsm.debugView == 7u)
        return missing ? 0.0f :
            float(selectedLevel + 1u) / float(SVSM_CLIPMAP_COUNT);
    if (g_Svsm.debugView == 8u)
        return missing ? 1.0f :
            float(selectedLevel - firstLevel) /
                float(SVSM_CLIPMAP_COUNT - 1u);
    if (g_Svsm.debugView == 9u)
        return missing ? 1.0f : 0.0f;
    if (g_Svsm.debugView == 10u)
        return float(g_Svsm.tapCount) / 16.0f;
    if (g_Svsm.debugView == 11u)
        return visibility;
    return visibility;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_Svsm.outputSize))
        return;

    float cameraDepth = t_CameraDepth[pixel];
    if (!(cameraDepth > 0.0f) || !isfinite(cameraDepth))
    {
        u_Visibility[pixel] = 1.0f;
        if (g_Svsm.debugView != 0u)
        {
            u_Debug[pixel] = DenseDebugValue(
                0u, 0u, 1.0f, true);
        }
        return;
    }

    float3 worldPosition = ReconstructWorldPosition(
        g_Svsm.cameraView,
        float2(pixel) + 0.5f,
        cameraDepth);
    if (!all(isfinite(worldPosition)))
    {
        u_Visibility[pixel] = 1.0f;
        if (g_Svsm.debugView != 0u)
        {
            u_Debug[pixel] = DenseDebugValue(
                0u, 0u, 1.0f, true);
        }
        return;
    }

    uint firstLevel = min(g_Svsm.resolutionBias,
        uint(SVSM_CLIPMAP_COUNT - 1));
    float visibility = 1.0f;
    [loop]
    for (uint level = firstLevel;
        level < SVSM_CLIPMAP_COUNT;
        ++level)
    {
        if (TryDenseClipmap(level, worldPosition, visibility))
        {
            u_Visibility[pixel] = visibility;
            if (g_Svsm.debugView != 0u)
            {
                u_Debug[pixel] = DenseDebugValue(
                    level,
                    firstLevel,
                    visibility,
                    false);
            }
            return;
        }
    }

    u_Visibility[pixel] = 1.0f;
    if (g_Svsm.debugView != 0u)
    {
        u_Debug[pixel] = DenseDebugValue(
            0u, firstLevel, 1.0f, true);
    }
}
