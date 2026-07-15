#include "local_tone_mapping_cb.h"

Texture2D<float4> t_DisplaySource : register(t0);
RWTexture3D<uint> u_AccumulatedLog : register(u0);
RWTexture3D<uint> u_AccumulatedWeight : register(u1);

cbuffer c_LocalToneMapping : register(b0)
{
    LocalToneMappingConstants g_Local;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy *
        g_Local.inputDimensionsStrideCell.z;
    if (any(pixel >= g_Local.inputDimensionsStrideCell.xy))
        return;

    float3 rgb = t_DisplaySource.Load(int3(pixel, 0)).rgb;
    if (!all(isfinite(rgb)))
        return;

    rgb = max(rgb, 0.0f);
    const float luminance = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
    if (!isfinite(luminance))
        return;

    const float logY = clamp(log2(max(luminance, 1e-6f)),
        g_Local.rangeAndFixedPoint.x, g_Local.rangeAndFixedPoint.y);
    const float normalizedRange = saturate(
        (logY - g_Local.rangeAndFixedPoint.x) *
        g_Local.rangeAndFixedPoint.z);

    const uint3 cell = uint3(
        min(pixel.x / g_Local.inputDimensionsStrideCell.w,
            g_Local.gridDimensionsVoxelCount.x - 1u),
        min(pixel.y / g_Local.inputDimensionsStrideCell.w,
            g_Local.gridDimensionsVoxelCount.y - 1u),
        min((uint)round(normalizedRange *
            float(g_Local.gridDimensionsVoxelCount.z - 1u)),
            g_Local.gridDimensionsVoxelCount.z - 1u));

    // Translate the signed analysis range to [0, 32] EV before conversion.
    // This keeps every atomic operand non-negative and portable on D3D12.
    const uint fixedLog = (uint)round(
        (logY - g_Local.rangeAndFixedPoint.x) *
        g_Local.rangeAndFixedPoint.w);

    InterlockedAdd(u_AccumulatedLog[cell], fixedLog);
    InterlockedAdd(u_AccumulatedWeight[cell], 1u);
}
