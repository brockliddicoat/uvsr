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

#define ENABLE_METAL_ROUGH_RECONSTRUCTION 1
#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/material_bindings.hlsli>
#include <donut/shaders/motion_vectors.hlsli>
#include <donut/shaders/forward_vertex.hlsli>
#include <donut/shaders/binding_helpers.hlsli>
#include "pbr_gbuffer.hlsli"

DECLARE_CBUFFER(GBufferFillConstants, c_GBuffer, GBUFFER_BINDING_VIEW_CONSTANTS, GBUFFER_SPACE_VIEW);
DECLARE_PUSH_CONSTANTS(GBufferPushConstants, g_Push, GBUFFER_BINDING_PUSH_CONSTANTS, GBUFFER_SPACE_INPUT);

uint GetPbrFeatureMask()
{
    uint featureMask = 0;
    if ((g_Material.flags & MaterialFlags_SubsurfaceScattering) != 0)
        featureMask |= PbrFeature_Translucency | PbrFeature_Scattering;
    if (g_Material.transmissionFactor > 0.0f)
        featureMask |= PbrFeature_Refraction;
    if ((g_Material.flags & MaterialFlags_DoubleSided) != 0)
        featureMask |= PbrFeature_DoubleSided;
    return featureMask;
}

void main(
    in float4 i_position : SV_Position,
    in SceneVertex i_vtx,
    in uint i_instance : INSTANCE,
    in bool i_isFrontFace : SV_IsFrontFace,
    out float4 o_channel0 : SV_Target0,
    out float4 o_channel1 : SV_Target1,
    out float4 o_channel2 : SV_Target2,
    out float4 o_channel3 : SV_Target3,
    out float o_materialAmbientOcclusion : SV_Target4,
    out uint2 o_surfaceIDs : SV_Target5,
    out float o_reactiveMask : SV_Target6
#if MOTION_VECTORS
    , out float4 o_motion : SV_Target7
#endif
)
{
    MaterialTextureSample textures = SampleMaterialTexturesAuto(
        i_vtx.texCoord, g_Material.normalTextureTransformScale);
#if WHITE_WORLD
    // Retain base alpha for cutout coverage while preventing sampled RGB from
    // leaking into the white reference material.
    textures.baseOrDiffuse.rgb = 1.0f;
#endif
    MaterialSample surface = EvaluateSceneMaterial(
        i_vtx.normal, i_vtx.tangent, g_Material, textures);

#if ALPHA_TESTED
    if (g_Material.domain != MaterialDomain_Opaque)
        clip(surface.opacity - g_Material.alphaCutoff);
#endif

    if (!i_isFrontFace)
    {
        surface.shadingNormal = -surface.shadingNormal;
        surface.geometryNormal = -surface.geometryNormal;
    }

    float dielectricF0 = (g_Material.flags & MaterialFlags_UseSpecularGlossModel) == 0 &&
        g_Material.specularColor.r > 0.0f
        ? saturate(g_Material.specularColor.r)
        : IorToF0(1.5f);

    PbrGBufferData pbrData = (PbrGBufferData)0;
    pbrData.material.baseColor = surface.baseColor;
    pbrData.material.metalness = surface.metalness;
    pbrData.material.perceptualRoughness = surface.roughness;
    pbrData.material.dielectricF0 = dielectricF0;
    pbrData.material.emissive = surface.emissiveColor;
    pbrData.material.opacity = surface.opacity;
    pbrData.material.featureMask = GetPbrFeatureMask();
    pbrData.shadingNormal = surface.shadingNormal;
    pbrData.geometricNormal = surface.geometryNormal;
    pbrData.ambientOcclusion = surface.occlusion;
    EncodePbrGBuffer(
        pbrData,
        o_channel0,
        o_channel1,
        o_channel2,
        o_channel3,
        o_materialAmbientOcclusion);

    // Material IDs are stable scene identifiers and the instance index is
    // stable for the lifetime of the loaded scene. Writing both with the main
    // G-buffer avoids a second geometry pass and gives temporal validation a
    // real surface identity instead of a transient draw index. The RG32_UINT
    // target preserves both components without truncation. RTAA keeps the full
    // material ID and stores an optional compact instance token in history.
    o_surfaceIDs = uint2(
        uint(max(g_Material.materialID, 0)),
        g_Push.startInstanceLocation + i_instance);

    // This is UVSR's explicit, application-authored reactive contribution.
    // Alpha coverage, emissive shading and transport features are known at
    // material evaluation time and are more trustworthy than a color-only
    // heuristic. The resolve pass combines this with its automatic
    // exposure-normalized luminance/chroma comparison; it does not interpret
    // this mask as unconditional history rejection.
    float emissiveLuminance = dot(max(surface.emissiveColor, 0.0f),
        float3(0.2126f, 0.7152f, 0.0722f));
    float alphaReactivity = saturate((1.0f - surface.opacity) * 4.0f);
    float emissiveReactivity = saturate(log2(1.0f + emissiveLuminance) * 0.25f);
    float featureReactivity = (pbrData.material.featureMask &
        (PbrFeature_Translucency | PbrFeature_Refraction | PbrFeature_Scattering)) != 0u
        ? 1.0f : 0.0f;
    o_reactiveMask = max(alphaReactivity,
        max(emissiveReactivity, featureReactivity));

#if MOTION_VECTORS
    // The alpha channel distinguishes a valid zero velocity from the cleared
    // background and from a previous position behind the camera. Donut's
    // helper returns zero in the latter case, so test the previous clip W here
    // without changing the pinned dependency. XY remains de-jittered
    // current-to-previous motion in pixels; Z remains previous minus current
    // device depth.
    float4 previousClip = mul(float4(i_vtx.prevPos, 1.0f),
        c_GBuffer.viewPrev.matWorldToClip);
    bool validPreviousPosition = previousClip.w > 0.0f && isfinite(previousClip.w);
    o_motion = validPreviousPosition
        ? float4(GetMotionVector(
            i_position.xyz, i_vtx.prevPos, c_GBuffer.view, c_GBuffer.viewPrev), 1.0f)
        : 0.0f;
#endif
}
