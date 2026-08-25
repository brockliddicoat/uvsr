#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"

cbuffer c_Probe : register(b0)
{
    PlanarViewConstants g_View;
    MaterialConstants g_Material;
    ShadowConstants g_Shadow;
    LightConstants g_Light;
    LightProbeConstants g_Probe;
    GBufferPushConstants g_Push;
};

ConstantBuffer<DeferredLightingConstants> g_Deferred : register(b1);
ConstantBuffer<GBufferFillConstants> g_GBuffer : register(b2);

Texture2D t_Material0 : register(t0);
Texture2D t_Material1 : register(t1);
Texture2D t_Material2 : register(t2);
Texture2D t_Material3 : register(t3);
Texture2D t_Material4 : register(t4);
Texture2D t_Material5 : register(t5);
Texture2D t_Material6 : register(t6);
Texture2DArray t_Shadow : register(t7);
StructuredBuffer<GeometryData> t_Geometry : register(t8);
StructuredBuffer<InstanceData> t_Instance : register(t9);
StructuredBuffer<MaterialConstants> t_Materials : register(t10);
SamplerState s_Material : register(s0);
SamplerComparisonState s_Shadow : register(s1);
RWStructuredBuffer<float4> u_Output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    MaterialTextureSample textures = DefaultMaterialTextures();
    MaterialSample material = EvaluateSceneMaterial(
        float3(0.0f, 0.0f, 1.0f),
        float4(1.0f, 0.0f, 0.0f, 1.0f),
        g_Material,
        textures);
    float3 world = ReconstructWorldPosition(g_View, 0.5f, 0.5f);
    float3 view = ReconstructViewPosition(g_View, 0.5f, 0.5f);
    float3 incident = GetIncidentVector(
        g_View.cameraDirectionOrPosition,
        world);
    float3 motion = GetMotionVector(world, world, g_View, g_View);
    float4 packed = Unpack_RGBA8_SNORM(g_Push.startInstanceLocation);
    float2 shadow = EvaluateShadowPoisson(
        t_Shadow,
        s_Shadow,
        g_Shadow,
        world,
        float2(1.0f, 0.0f),
        3.0f);
    GeometryData geometry = t_Geometry[0];
    InstanceData instance = t_Instance[0];
    MaterialConstants structuredMaterial = t_Materials[0];
    float contractTouch = float(
        geometry.materialIndex + instance.firstGeometryIndex) +
        instance.prevTransform[0][0] + structuredMaterial.opacity +
        g_Deferred.lightProbes[15].frustumPlanes[5].w +
        g_GBuffer.viewPrev.cameraDirectionOrPosition.w;
    u_Output[dispatchThreadId.x] = float4(
        material.baseColor + world + view + incident + motion + packed.xyz,
        shadow.x + shadow.y + packed.w + float(g_Light.lightType) +
            g_Probe.diffuseScale + contractTouch);
}

float4 material_main(SceneVertex vertex) : SV_Target0
{
    MaterialTextureSample textures = SampleMaterialTexturesAuto(
        g_Material,
        t_Material0,
        t_Material1,
        t_Material2,
        t_Material3,
        t_Material4,
        t_Material5,
        t_Material6,
        s_Material,
        vertex.texCoord,
        g_Material.normalTextureTransformScale);
    MaterialSample material = EvaluateSceneMaterial(
        vertex.normal,
        vertex.tangent,
        g_Material,
        textures);
    return float4(material.baseColor, material.opacity);
}
