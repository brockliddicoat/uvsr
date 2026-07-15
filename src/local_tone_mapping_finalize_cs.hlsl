#include "local_tone_mapping_cb.h"

Texture3D<float2> t_FilteredGrid : register(t0);
Texture2D<float2> t_SmoothedReference : register(t1);
RWTexture3D<float2> u_FinalGrid : register(u0);

cbuffer c_LocalToneMapping : register(b0)
{
    LocalToneMappingConstants g_Local;
};

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId >= g_Local.gridDimensionsVoxelCount.xyz))
        return;

    const float2 reference = t_SmoothedReference.Load(int3(0, 0, 0));
    const float2 filtered = t_FilteredGrid.Load(int4(dispatchThreadId, 0));
    const bool referenceValid = reference.y > 0.0f && all(isfinite(reference));
    const bool voxelValid = filtered.y > 1e-6f && all(isfinite(filtered));

    float baseDeltaEV = 0.0f;
    float confidence = 0.0f;
    if (referenceValid && voxelValid)
    {
        const float filteredLogY = clamp(filtered.x / filtered.y,
            g_Local.rangeAndFixedPoint.x, g_Local.rangeAndFixedPoint.y);
        baseDeltaEV = clamp(reference.x - filteredLogY,
            g_Local.rangeAndFixedPoint.x - g_Local.rangeAndFixedPoint.y,
            g_Local.rangeAndFixedPoint.y - g_Local.rangeAndFixedPoint.x);
        confidence = saturate(filtered.y /
            max(g_Local.filterAndReference.w, 1e-6f));
    }

    u_FinalGrid[dispatchThreadId] = float2(baseDeltaEV, confidence);
}
