#pragma pack_matrix(row_major)
#include "renderer_gpu_contract.h"

RaytracingAccelerationStructure t_Scene : register(t0);
StructuredBuffer<InstanceData> t_Instances : register(t1);
StructuredBuffer<uint> t_GeometryMap : register(t2);
StructuredBuffer<GeometryData> t_Geometry : register(t3);
StructuredBuffer<RendererMaterialTableEntry> t_Materials : register(t4);
RWStructuredBuffer<uint4> u_Result : register(u0);

[numthreads(2, 1, 1)]
void main(uint index : SV_DispatchThreadID)
{
    const InstanceData instance = t_Instances[index];
    const float4 local = float4(index * 3.0 + 0.25, 0.25, 0.0, 1.0);
    const float3 current = mul(instance.transform, local);
    const float3 previous = mul(instance.prevTransform, local);
    RayDesc ray;
    ray.Origin = current + float3(0, 0, 10);
    ray.Direction = float3(0, 0, -1);
    ray.TMin = 0;
    ray.TMax = 20;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(t_Scene, 0, 0xff, ray);
    while (query.Proceed()) {}
    uint4 result = uint4(0xffffffffu, 0xffffffffu, 0xffffffffu, asuint(current.x - previous.x));
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        const uint geometryIndex = t_GeometryMap[query.CommittedInstanceContributionToHitGroupIndex() + query.CommittedGeometryIndex()];
        result.x = query.CommittedInstanceID();
        result.y = t_Materials[t_Geometry[geometryIndex].materialIndex].material.materialID;
        result.z = asuint(query.CommittedRayT());
    }
    u_Result[index] = result;
}
