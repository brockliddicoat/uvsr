#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"
#include "denoising_cb.h"

#ifndef DENOISING_OUTPUT_FORMAT
#define DENOISING_OUTPUT_FORMAT 0
#endif

#define DENOISING_OUTPUT_R8_UNORM 0
#define DENOISING_OUTPUT_R16_FLOAT 1
#define DENOISING_OUTPUT_R32_FLOAT 2
#define DENOISING_OUTPUT_RGBA16_FLOAT 3
#define DENOISING_OUTPUT_RGBA32_FLOAT 4

#define DENOISING_SPATIAL_GAUSSIAN_BILATERAL 1

cbuffer c_Denoising : register(b0)
{
    DenoisingConstants g_Denoising;
};

Texture2D<float4> t_RawSignal : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_NormalRoughness : register(t2);

#if DENOISING_OUTPUT_FORMAT == DENOISING_OUTPUT_R8_UNORM
RWTexture2D<float> u_Output : register(u0);
#elif DENOISING_OUTPUT_FORMAT == DENOISING_OUTPUT_R16_FLOAT
RWTexture2D<float> u_Output : register(u0);
#elif DENOISING_OUTPUT_FORMAT == DENOISING_OUTPUT_R32_FLOAT
RWTexture2D<float> u_Output : register(u0);
#elif DENOISING_OUTPUT_FORMAT == DENOISING_OUTPUT_RGBA16_FLOAT
RWTexture2D<float4> u_Output : register(u0);
#elif DENOISING_OUTPUT_FORMAT == DENOISING_OUTPUT_RGBA32_FLOAT
RWTexture2D<float4> u_Output : register(u0);
#else
#error Unsupported DENOISING_OUTPUT_FORMAT
#endif

bool IsValidDepth(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Denoising.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

bool TryNormalize(float3 value, out float3 normalized)
{
    float lengthSquared = dot(value, value);
    if (!(lengthSquared > 1e-12f) || !isfinite(lengthSquared))
    {
        normalized = 0.0f;
        return false;
    }
    normalized = value * rsqrt(lengthSquared);
    return all(isfinite(normalized));
}

int2 ClampFullPixel(int2 pixel)
{
    return clamp(pixel, int2(0, 0),
        int2(g_Denoising.fullResolution) - 1);
}

int2 ClampSourcePixel(int2 pixel)
{
    return clamp(pixel, int2(0, 0),
        int2(g_Denoising.sourceResolution) - 1);
}

float2 FullToSourcePosition(float2 fullPixel)
{
    return (fullPixel + 0.5f) * g_Denoising.sourceResolution /
        g_Denoising.fullResolution - 0.5f;
}

float4 LoadRaw(int2 sourcePixel)
{
    float4 value = t_RawSignal[ClampSourcePixel(sourcePixel)];
    return any(!isfinite(value)) ? 0.0f : value;
}

void StoreOutput(uint2 pixel, float4 value)
{
    if (any(!isfinite(value)))
        value = 0.0f;
#if DENOISING_OUTPUT_FORMAT == DENOISING_OUTPUT_R8_UNORM || \
    DENOISING_OUTPUT_FORMAT == DENOISING_OUTPUT_R16_FLOAT || \
    DENOISING_OUTPUT_FORMAT == DENOISING_OUTPUT_R32_FLOAT
    u_Output[pixel] = value.x;
#else
    u_Output[pixel] = value;
#endif
}

float JointGuideWeight(
    int2 fullPixel,
    float centerViewZ,
    float3 centerPositionVS,
    float3 centerNormalWS,
    float3 centerNormalVS)
{
    float depth = t_Depth[fullPixel];
    if (!IsValidDepth(depth))
        return 0.0f;

    float3 samplePositionVS = ReconstructViewPosition(
        g_Denoising.view, float2(fullPixel) + 0.5f, depth);
    if (any(!isfinite(samplePositionVS)))
        return 0.0f;

    float3 sampleNormalWS;
    if (!TryNormalize(t_NormalRoughness[fullPixel].xyz, sampleNormalWS))
        return 0.0f;

    float depthScale = max(centerViewZ * 0.02f, 0.01f);
    float planeDistance = abs(dot(
        samplePositionVS - centerPositionVS, centerNormalVS));
    float depthDistance = abs(abs(samplePositionVS.z) - centerViewZ);
    float depthWeight = exp(-planeDistance / depthScale) *
        exp(-depthDistance / (depthScale * 2.0f));
    float normalWeight = pow(
        saturate(dot(centerNormalWS, sampleNormalWS)), 16.0f);
    return depthWeight * normalWeight;
}

void AccumulateTap(
    int2 fullPixel,
    float spatialWeight,
    float centerViewZ,
    float3 centerPositionVS,
    float3 centerNormalWS,
    float3 centerNormalVS,
    inout float totalWeight,
    inout float4 signalSum)
{
    fullPixel = ClampFullPixel(fullPixel);
    float guideWeight = JointGuideWeight(
        fullPixel,
        centerViewZ,
        centerPositionVS,
        centerNormalWS,
        centerNormalVS);
    float weight = spatialWeight * guideWeight;
    if (!(weight > 0.0f) || !isfinite(weight))
        return;

    int2 sourcePixel = ClampSourcePixel(int2(round(
        FullToSourcePosition(float2(fullPixel)))));
    signalSum += LoadRaw(sourcePixel) * weight;
    totalWeight += weight;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    uint2 fullSize = uint2(g_Denoising.fullResolution);
    if (any(pixel >= fullSize))
        return;

    int2 fallbackSourcePixel = ClampSourcePixel(int2(round(
        FullToSourcePosition(float2(pixel)))));
    float4 fallback = LoadRaw(fallbackSourcePixel);
    float centerDepth = t_Depth[pixel];
    if (!IsValidDepth(centerDepth))
    {
        StoreOutput(pixel, fallback);
        return;
    }

    float3 centerPositionVS = ReconstructViewPosition(
        g_Denoising.view, float2(pixel) + 0.5f, centerDepth);
    float centerViewZ = abs(centerPositionVS.z);
    float3 centerNormalWS;
    if (any(!isfinite(centerPositionVS)) ||
        !(centerViewZ > 0.0f) ||
        !(centerViewZ < g_Denoising.denoisingRange) ||
        !TryNormalize(t_NormalRoughness[pixel].xyz, centerNormalWS))
    {
        StoreOutput(pixel, fallback);
        return;
    }

    float3 centerNormalVS;
    if (!TryNormalize(mul(
            float4(centerNormalWS, 0.0f),
            g_Denoising.view.matWorldToView).xyz,
            centerNormalVS))
    {
        StoreOutput(pixel, fallback);
        return;
    }

    float radius = clamp(g_Denoising.spatialRadius, 1.0f, 8.0f);
    float totalWeight = 0.0f;
    float4 signalSum = 0.0f;
    if (g_Denoising.spatialMethod ==
        DENOISING_SPATIAL_GAUSSIAN_BILATERAL)
    {
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
        float sigma = max(radius * 0.9f, 1e-3f);
        [unroll]
        for (uint tap = 0u; tap < 16u; ++tap)
        {
            float2 offset = disk[tap] * radius;
            float spatialWeight = exp(-dot(offset, offset) /
                (2.0f * sigma * sigma));
            AccumulateTap(
                int2(round(float2(pixel) + offset)),
                spatialWeight,
                centerViewZ,
                centerPositionVS,
                centerNormalWS,
                centerNormalVS,
                totalWeight,
                signalSum);
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
                float2 offset = float2(x, y) * radius;
                AccumulateTap(
                    int2(round(float2(pixel) + offset)),
                    spatialWeight,
                    centerViewZ,
                    centerPositionVS,
                    centerNormalWS,
                    centerNormalVS,
                    totalWeight,
                    signalSum);
            }
        }
    }

    if (!(totalWeight > 1e-6f) || !isfinite(totalWeight))
    {
        StoreOutput(pixel, fallback);
        return;
    }
    StoreOutput(pixel, signalSum / totalWeight);
}
