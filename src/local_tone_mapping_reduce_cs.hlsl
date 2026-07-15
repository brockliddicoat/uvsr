#include "local_tone_mapping_cb.h"

Texture3D<uint> t_AccumulatedLog : register(t0);
Texture3D<uint> t_AccumulatedWeight : register(t1);
Texture2D<float2> t_PreviousReference : register(t2);
RWTexture2D<float2> u_NextReference : register(u0);

cbuffer c_LocalToneMapping : register(b0)
{
    LocalToneMappingConstants g_Local;
};

groupshared float2 s_Partial[256];

[numthreads(256, 1, 1)]
void main(
    uint groupIndex : SV_GroupIndex,
    uint3 groupId : SV_GroupID)
{
    // Exactly one group is dispatched. Each lane walks a strided subset of the
    // compact grid, so the reduction requires neither float atomics nor a CPU
    // readback regardless of grid dimensions.
    float2 partial = 0.0f;
    for (uint linearIndex = groupIndex;
        linearIndex < g_Local.gridDimensionsVoxelCount.w;
        linearIndex += 256u)
    {
        const uint x = linearIndex % g_Local.gridDimensionsVoxelCount.x;
        const uint yz = linearIndex / g_Local.gridDimensionsVoxelCount.x;
        const uint y = yz % g_Local.gridDimensionsVoxelCount.y;
        const uint z = yz / g_Local.gridDimensionsVoxelCount.y;
        const uint3 coordinate = uint3(x, y, z);
        partial.x += float(t_AccumulatedLog.Load(int4(coordinate, 0)));
        partial.y += float(t_AccumulatedWeight.Load(int4(coordinate, 0)));
    }

    s_Partial[groupIndex] = partial;
    GroupMemoryBarrierWithGroupSync();

    for (uint width = 128u; width > 0u; width >>= 1u)
    {
        if (groupIndex < width)
            s_Partial[groupIndex] += s_Partial[groupIndex + width];
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex != 0u)
        return;

    const float totalWeight = s_Partial[0].y;
    if (!(totalWeight > 0.0f) || !all(isfinite(s_Partial[0])))
    {
        // Zero confidence tells Finalize to emit a neutral grid. Do not retain
        // a stale scalar reference when this frame contains no valid samples.
        u_NextReference[uint2(0u, 0u)] = float2(0.0f, 0.0f);
        return;
    }

    const float currentReference = clamp(
        g_Local.rangeAndFixedPoint.x +
            s_Partial[0].x /
                (totalWeight * g_Local.rangeAndFixedPoint.w),
        g_Local.rangeAndFixedPoint.x,
        g_Local.rangeAndFixedPoint.y);
    const float2 previous = t_PreviousReference.Load(int3(0, 0, 0));
    const bool resetHistory = g_Local.blurAxisRadiusAndFlags.z != 0u ||
        !(previous.y > 0.0f) || !all(isfinite(previous));
    const float smoothedReference = resetHistory
        ? currentReference
        : lerp(previous.x, currentReference,
            saturate(g_Local.filterAndReference.z));
    u_NextReference[uint2(0u, 0u)] = float2(smoothedReference, 1.0f);
}
