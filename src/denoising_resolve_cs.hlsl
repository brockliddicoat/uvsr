#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"
#include "denoising_cb.h"

#ifndef DENOISING_SIGNAL_CLASS
#define DENOISING_SIGNAL_CLASS 0
#endif

#define DENOISING_RADIANCE 0
#define DENOISING_SHADOW 1
#define DENOISING_SCALAR_RADIANCE 2

#define DENOISER_METHOD_RELAX_RADIANCE 1

cbuffer c_Denoising : register(b0)
{
    DenoisingConstants g_Denoising;
};

#if DENOISING_SIGNAL_CLASS == DENOISING_RADIANCE || DENOISING_SIGNAL_CLASS == DENOISING_SCALAR_RADIANCE
Texture2D<float4> t_DenoisedSignal : register(t0);
#else
Texture2D<float> t_DenoisedSignal : register(t0);
#endif
Texture2D<float> t_DenoiserViewZ : register(t1);
Texture2D<float4> t_DenoiserNormalRoughness : register(t2);
Texture2D<float> t_FullDepth : register(t3);
Texture2D<float4> t_FullNormalRoughness : register(t4);
Texture2D<float4> t_RawFullSignal : register(t5);

#if DENOISING_SIGNAL_CLASS == DENOISING_RADIANCE
RWTexture2D<float4> u_ResolvedSignal : register(u0);
#elif DENOISING_SIGNAL_CLASS == DENOISING_SCALAR_RADIANCE
RWTexture2D<float> u_ResolvedSignal : register(u0);
#else
RWTexture2D<float> u_ResolvedSignal : register(u0);
#endif

bool IsValidDepth(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Denoising.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

float3 YCoCgToLinear(float3 color)
{
    float t = color.x - color.z;
    return max(float3(t + color.y, color.x + color.z, t - color.y), 0.0f);
}

float4 GetRaw(uint2 pixel)
{
    uint2 sourceSize = uint2(g_Denoising.sourceResolution);
    uint2 fullSize = uint2(g_Denoising.fullResolution);
    uint2 sourcePixel = min(uint2((float2(pixel) + 0.5f) *
        float2(sourceSize) / float2(fullSize)), sourceSize - 1u);
    float4 raw = t_RawFullSignal[sourcePixel];
    return any(!isfinite(raw)) ? 0.0f : raw;
}

float4 DecodeDenoised(int2 pixel)
{
#if DENOISING_SIGNAL_CLASS == DENOISING_RADIANCE || DENOISING_SIGNAL_CLASS == DENOISING_SCALAR_RADIANCE
    float4 packed = t_DenoisedSignal[pixel];
    float3 color = g_Denoising.method == DENOISER_METHOD_RELAX_RADIANCE
        ? packed.rgb : YCoCgToLinear(packed.rgb);
    return float4(min(max(color, 0.0f), 65504.0f), packed.a);
#else
    float packedShadow = saturate(t_DenoisedSignal[pixel]);
    return (packedShadow * packedShadow).xxxx;
#endif
}

void StoreResolved(uint2 pixel, float4 value)
{
#if DENOISING_SIGNAL_CLASS == DENOISING_RADIANCE
    u_ResolvedSignal[pixel] = float4(
        min(max(value.rgb, 0.0f), 65504.0f), 0.0f);
#elif DENOISING_SIGNAL_CLASS == DENOISING_SCALAR_RADIANCE
    u_ResolvedSignal[pixel] = saturate(value.x);
#else
    u_ResolvedSignal[pixel] = saturate(value.x);
#endif
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    uint2 fullSize = uint2(g_Denoising.fullResolution);
    if (any(pixel >= fullSize))
        return;

    float depth = t_FullDepth[pixel];
    float4 fullGuide = t_FullNormalRoughness[pixel];
    float normalLengthSquared = dot(fullGuide.xyz, fullGuide.xyz);
    if (!IsValidDepth(depth) ||
        !(normalLengthSquared > 1e-12f) ||
        !isfinite(normalLengthSquared))
    {
        StoreResolved(pixel, GetRaw(pixel));
        return;
    }

    float3 fullNormal = fullGuide.xyz * rsqrt(normalLengthSquared);
    float3 positionVS = ReconstructViewPosition(
        g_Denoising.view, float2(pixel) + 0.5f, depth);
    float fullViewZ = abs(positionVS.z);
    if (!isfinite(fullViewZ) || !(fullViewZ > 0.0f) ||
        !(fullViewZ < g_Denoising.denoisingRange))
    {
        StoreResolved(pixel, GetRaw(pixel));
        return;
    }

    float2 lowPosition = (float2(pixel) + 0.5f) *
        g_Denoising.denoiserResolution /
        g_Denoising.fullResolution - 0.5f;
    int2 basePixel = int2(floor(lowPosition));
    float2 fraction = frac(lowPosition);
    int2 lowMaximum = int2(g_Denoising.denoiserResolution) - 1;

    float4 signalSum = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            int2 lowPixel = clamp(basePixel + int2(x, y), 0, lowMaximum);
            float bilinearWeight =
                (x == 0 ? 1.0f - fraction.x : fraction.x) *
                (y == 0 ? 1.0f - fraction.y : fraction.y);
            float lowViewZ = t_DenoiserViewZ[lowPixel];
            float3 lowNormal = t_DenoiserNormalRoughness[lowPixel].xyz;
            float lowNormalLengthSquared = dot(lowNormal, lowNormal);
            if (!(lowViewZ > 0.0f) || !isfinite(lowViewZ) ||
                !(lowNormalLengthSquared > 1e-12f) ||
                !isfinite(lowNormalLengthSquared))
            {
                continue;
            }
            lowNormal *= rsqrt(lowNormalLengthSquared);
            float normalWeight = pow(
                saturate(dot(fullNormal, lowNormal)), 32.0f);
            float depthTolerance = max(fullViewZ * 0.02f, 0.01f);
            float depthWeight = exp2(
                -abs(lowViewZ - fullViewZ) / depthTolerance);
            float weight = bilinearWeight * normalWeight * depthWeight;
            float4 signal = DecodeDenoised(lowPixel);
            if (any(!isfinite(signal)))
                continue;
            signalSum += signal * weight;
            weightSum += weight;
        }
    }

    if (!(weightSum > 1e-6f))
    {
        StoreResolved(pixel, GetRaw(pixel));
        return;
    }
    StoreResolved(pixel, signalSum / weightSum);
}
