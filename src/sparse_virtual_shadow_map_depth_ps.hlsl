#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer_cb.h>

#define MATERIAL_REGISTER_SPACE     GBUFFER_SPACE_MATERIAL
#define MATERIAL_CB_SLOT            GBUFFER_BINDING_MATERIAL_CONSTANTS
#define MATERIAL_DIFFUSE_SLOT       GBUFFER_BINDING_MATERIAL_DIFFUSE_TEXTURE
#define MATERIAL_SPECULAR_SLOT      GBUFFER_BINDING_MATERIAL_SPECULAR_TEXTURE
#define MATERIAL_NORMALS_SLOT       GBUFFER_BINDING_MATERIAL_NORMAL_TEXTURE
#define MATERIAL_EMISSIVE_SLOT      GBUFFER_BINDING_MATERIAL_EMISSIVE_TEXTURE
#define MATERIAL_OCCLUSION_SLOT     GBUFFER_BINDING_MATERIAL_OCCLUSION_TEXTURE
#define MATERIAL_TRANSMISSION_SLOT  GBUFFER_BINDING_MATERIAL_TRANSMISSION_TEXTURE
#define MATERIAL_OPACITY_SLOT       GBUFFER_BINDING_MATERIAL_OPACITY_TEXTURE

#define MATERIAL_SAMPLER_REGISTER_SPACE GBUFFER_SPACE_VIEW
#define MATERIAL_SAMPLER_SLOT           GBUFFER_BINDING_MATERIAL_SAMPLER

#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/material_bindings.hlsli>
#include <donut/shaders/forward_vertex.hlsli>
#include <donut/shaders/binding_helpers.hlsli>

RWTexture2D<uint> u_PhysicalDepth :
    REGISTER_UAV(0, GBUFFER_SPACE_VIEW);

void main(
    in float4 i_position : SV_Position,
    in SceneVertex i_vtx)
{
#if ALPHA_TESTED
    MaterialTextureSample textures = DefaultMaterialTextures();
    if ((g_Material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0)
    {
        textures.baseOrDiffuse =
            t_BaseOrDiffuse.Sample(s_MaterialSampler, i_vtx.texCoord);
    }
    if ((g_Material.flags & MaterialFlags_UseOpacityTexture) != 0)
    {
        textures.opacity =
            t_Opacity.Sample(s_MaterialSampler, i_vtx.texCoord).r;
    }
    MaterialSample material = EvaluateSceneMaterial(
        float3(1.0f, 0.0f, 0.0f),
        float4(0.0f, 1.0f, 0.0f, 0.0f),
        g_Material,
        textures);
    clip(material.opacity - g_Material.alphaCutoff);
#endif

    uint ignored;
    InterlockedMax(
        u_PhysicalDepth[uint2(i_position.xy)],
        asuint(saturate(i_position.z)),
        ignored);
}
