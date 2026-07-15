#include "local_tone_mapping_cb.h"

#ifndef LOCAL_BLUR_SOURCE_UINT
#define LOCAL_BLUR_SOURCE_UINT 0
#endif

#ifndef LOCAL_BLUR_AXIS
#define LOCAL_BLUR_AXIS 0
#endif

#if LOCAL_BLUR_SOURCE_UINT
Texture3D<uint> t_AccumulatedLog : register(t0);
Texture3D<uint> t_AccumulatedWeight : register(t1);
#else
Texture3D<float2> t_BlurInput : register(t0);
#endif

RWTexture3D<float2> u_BlurOutput : register(u0);

cbuffer c_LocalToneMapping : register(b0)
{
    LocalToneMappingConstants g_Local;
};

float2 LoadNumeratorAndWeight(int3 coordinate)
{
#if LOCAL_BLUR_SOURCE_UINT
    const float weight = float(t_AccumulatedWeight.Load(int4(coordinate, 0)));
    const float translatedLogSum =
        float(t_AccumulatedLog.Load(int4(coordinate, 0))) /
        g_Local.rangeAndFixedPoint.w;
    // Undo the non-negative translation used by the atomic accumulation.
    return float2(
        translatedLogSum + g_Local.rangeAndFixedPoint.x * weight,
        weight);
#else
    return t_BlurInput.Load(int4(coordinate, 0));
#endif
}

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId >= g_Local.gridDimensionsVoxelCount.xyz))
        return;

    const int3 dimensions = int3(g_Local.gridDimensionsVoxelCount.xyz);
    const float sigma = max(
        LOCAL_BLUR_AXIS == 2
            ? g_Local.filterAndReference.y
            : g_Local.filterAndReference.x,
        0.25f);
    const int radius = int(g_Local.blurAxisRadiusAndFlags.y);

    float2 filtered = 0.0f;
    float normalization = 0.0f;
    [loop]
    for (int offset = -radius; offset <= radius; ++offset)
    {
        int3 coordinate = int3(dispatchThreadId);
        coordinate[LOCAL_BLUR_AXIS] = clamp(
            coordinate[LOCAL_BLUR_AXIS] + offset,
            0,
            dimensions[LOCAL_BLUR_AXIS] - 1);
        const float normalizedOffset = float(offset) / sigma;
        const float kernelWeight = exp(-0.5f * normalizedOffset * normalizedOffset);
        filtered += LoadNumeratorAndWeight(coordinate) * kernelWeight;
        normalization += kernelWeight;
    }

    u_BlurOutput[dispatchThreadId] = filtered / max(normalization, 1e-6f);
}
