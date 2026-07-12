#pragma pack_matrix(row_major)

#include <donut/shaders/forward_cb.h>

#define MATERIAL_REGISTER_SPACE     FORWARD_SPACE_MATERIAL
#define MATERIAL_CB_SLOT            FORWARD_BINDING_MATERIAL_CONSTANTS
#define MATERIAL_DIFFUSE_SLOT       FORWARD_BINDING_MATERIAL_DIFFUSE_TEXTURE
#define MATERIAL_SPECULAR_SLOT      FORWARD_BINDING_MATERIAL_SPECULAR_TEXTURE
#define MATERIAL_NORMALS_SLOT       FORWARD_BINDING_MATERIAL_NORMAL_TEXTURE
#define MATERIAL_EMISSIVE_SLOT      FORWARD_BINDING_MATERIAL_EMISSIVE_TEXTURE
#define MATERIAL_OCCLUSION_SLOT     FORWARD_BINDING_MATERIAL_OCCLUSION_TEXTURE
#define MATERIAL_TRANSMISSION_SLOT  FORWARD_BINDING_MATERIAL_TRANSMISSION_TEXTURE
#define MATERIAL_OPACITY_SLOT       FORWARD_BINDING_MATERIAL_OPACITY_TEXTURE

#define MATERIAL_SAMPLER_REGISTER_SPACE FORWARD_SPACE_SHADING
#define MATERIAL_SAMPLER_SLOT           FORWARD_BINDING_MATERIAL_SAMPLER

#define ENABLE_METAL_ROUGH_RECONSTRUCTION 1
#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/material_bindings.hlsli>
#include <donut/shaders/forward_vertex.hlsli>
#include <donut/shaders/shadows.hlsli>
#include <donut/shaders/binding_helpers.hlsli>
#include "pbr_lighting.hlsli"

DECLARE_CBUFFER(ForwardShadingViewConstants, g_ForwardView,
    FORWARD_BINDING_VIEW_CONSTANTS, FORWARD_SPACE_VIEW);
DECLARE_CBUFFER(ForwardShadingLightConstants, g_ForwardLight,
    FORWARD_BINDING_LIGHT_CONSTANTS, FORWARD_SPACE_SHADING);

Texture2DArray t_ShadowMapArray : REGISTER_SRV(
    FORWARD_BINDING_SHADOW_MAP_TEXTURE, FORWARD_SPACE_SHADING);
TextureCubeArray t_DiffuseLightProbe : REGISTER_SRV(
    FORWARD_BINDING_DIFFUSE_LIGHT_PROBE_TEXTURE, FORWARD_SPACE_SHADING);
TextureCubeArray t_SpecularLightProbe : REGISTER_SRV(
    FORWARD_BINDING_SPECULAR_LIGHT_PROBE_TEXTURE, FORWARD_SPACE_SHADING);
Texture2D t_EnvironmentBrdf : REGISTER_SRV(
    FORWARD_BINDING_ENVIRONMENT_BRDF_TEXTURE, FORWARD_SPACE_SHADING);

SamplerState s_ShadowSampler : REGISTER_SAMPLER(
    FORWARD_BINDING_SHADOW_MAP_SAMPLER, FORWARD_SPACE_SHADING);
SamplerState s_LightProbeSampler : REGISTER_SAMPLER(
    FORWARD_BINDING_LIGHT_PROBE_SAMPLER, FORWARD_SPACE_SHADING);
SamplerState s_BrdfSampler : REGISTER_SAMPLER(
    FORWARD_BINDING_ENVIRONMENT_BRDF_SAMPLER, FORWARD_SPACE_SHADING);

float3 GetForwardIncidentVector(float4 directionOrPosition, float3 surfacePosition)
{
    return directionOrPosition.w > 0.0f
        ? normalize(surfacePosition - directionOrPosition.xyz)
        : directionOrPosition.xyz;
}

float EvaluateForwardVisibility(LightConstants light, float3 surfaceWorldPosition)
{
    float2 cascadeVisibility = 0.0f;
    [loop]
    for (int cascade = 0; cascade < 4; ++cascade)
    {
        if (light.shadowCascades[cascade] < 0)
            break;

        float2 thisCascade = EvaluateShadowGather16(
            t_ShadowMapArray,
            s_ShadowSampler,
            g_ForwardLight.shadows[light.shadowCascades[cascade]],
            surfaceWorldPosition,
            g_ForwardLight.shadowMapTextureSize);
        cascadeVisibility = saturate(
            cascadeVisibility + thisCascade * (1.0001f - cascadeVisibility.y));
        if (cascadeVisibility.y == 1.0f)
            break;
    }

    cascadeVisibility.x += (1.0f - cascadeVisibility.y) * light.outOfBoundsShadow;
    float visibility = cascadeVisibility.x;

    [loop]
    for (int object = 0; object < 4; ++object)
    {
        if (light.perObjectShadows[object] < 0)
            continue;

        float2 objectVisibility = EvaluateShadowGather16(
            t_ShadowMapArray,
            s_ShadowSampler,
            g_ForwardLight.shadows[light.perObjectShadows[object]],
            surfaceWorldPosition,
            g_ForwardLight.shadowMapTextureSize);
        visibility *= saturate(objectVisibility.x + (1.0f - objectVisibility.y));
    }

    return saturate(visibility);
}

void main(
    in float4 i_position : SV_Position,
    in SceneVertex i_vtx,
    in bool i_isFrontFace : SV_IsFrontFace,
    VK_LOCATION_INDEX(0, 0) out float4 o_color : SV_Target0
#if TRANSMISSIVE_MATERIAL
    , VK_LOCATION_INDEX(0, 1) out float4 o_backgroundBlendFactor : SV_Target1
#endif
)
{
    MaterialTextureSample textures = SampleMaterialTexturesAuto(
        i_vtx.texCoord, g_Material.normalTextureTransformScale);
    MaterialSample sampledMaterial = EvaluateSceneMaterial(
        i_vtx.normal, i_vtx.tangent, g_Material, textures);

    if (!i_isFrontFace)
    {
        sampledMaterial.shadingNormal = -sampledMaterial.shadingNormal;
        sampledMaterial.geometryNormal = -sampledMaterial.geometryNormal;
    }

    if (g_Material.domain != MaterialDomain_Opaque)
        clip(sampledMaterial.opacity - g_Material.alphaCutoff);

    PbrMaterialParameters material = (PbrMaterialParameters)0;
    material.baseColor = max(sampledMaterial.baseColor, 0.0f);
    material.metalness = saturate(sampledMaterial.metalness);
    material.perceptualRoughness = saturate(sampledMaterial.roughness);
    material.dielectricF0 = (g_Material.flags & MaterialFlags_UseSpecularGlossModel) == 0 &&
        g_Material.specularColor.r > 0.0f
        ? saturate(g_Material.specularColor.r)
        : IorToF0(1.5f);
    material.emissive = max(sampledMaterial.emissiveColor, 0.0f);
    material.opacity = saturate(sampledMaterial.opacity);

    float3 surfaceWorldPosition = i_vtx.pos;
    float3 viewIncident = GetForwardIncidentVector(
        g_ForwardView.view.cameraDirectionOrPosition,
        surfaceWorldPosition);
    PbrSurfaceInteraction surface;
    surface.position = surfaceWorldPosition;
    surface.shadingNormal = sampledMaterial.shadingNormal;
    surface.geometricNormal = sampledMaterial.geometryNormal;
    surface.viewDirection = -viewIncident;

    float3 directDiffuse = 0.0f;
    float3 directSpecular = 0.0f;
    [loop]
    for (uint lightIndex = 0; lightIndex < g_ForwardLight.numLights; ++lightIndex)
    {
        LightConstants light = g_ForwardLight.lights[lightIndex];
        float visibility = EvaluateForwardVisibility(light, surfaceWorldPosition);
        PbrLightSample lightSample = SamplePbrLight(light, surfaceWorldPosition, visibility);
        PbrDirectLighting direct = EvaluateDirectLight(material, surface, lightSample);
        directDiffuse += direct.diffuse;
        directSpecular += direct.specular;
    }

    // Material occlusion is kept out of the BSDF and direct-light visibility.
    // Here it only modulates the renderer's approximate sky indirect diffuse.
    float hemisphere = sampledMaterial.shadingNormal.y * 0.5f + 0.5f;
    float3 approximateIndirectIrradiance = lerp(
        g_ForwardLight.ambientColorBottom.rgb,
        g_ForwardLight.ambientColorTop.rgb,
        hemisphere);
    float3 diffuseReflectance = material.baseColor * (1.0f - material.metalness);
    float3 indirectDiffuse = approximateIndirectIrradiance * diffuseReflectance * sampledMaterial.occlusion;

    float3 diffuse = directDiffuse + indirectDiffuse;
    float3 specular = directSpecular;
    float3 finalLinearHdr = max(diffuse + specular + material.emissive, 0.0f);
    if (any(isnan(finalLinearHdr)) || any(isinf(finalLinearHdr)))
        finalLinearHdr = 0.0f;

#if TRANSMISSIVE_MATERIAL
    float NoV = saturate(dot(
        PbrSafeNormalize(sampledMaterial.shadingNormal, sampledMaterial.geometryNormal),
        surface.viewDirection));
    float3 specularF0 = lerp(material.dielectricF0.xxx, material.baseColor, material.metalness);
    float3 fresnel = FresnelSchlick(NoV, specularF0);

    o_color.rgb = diffuse * (1.0f - sampledMaterial.transmission)
        + specular + material.emissive;
    o_color.a = 1.0f;

    float backgroundScalar = sampledMaterial.transmission *
        (1.0f - max(fresnel.r, max(fresnel.g, fresnel.b)));
    if (g_Material.domain == MaterialDomain_TransmissiveAlphaBlended)
        backgroundScalar *= 1.0f - material.opacity;

    o_backgroundBlendFactor.rgb = backgroundScalar *
        material.baseColor * (1.0f - material.metalness);
    o_backgroundBlendFactor.a = 1.0f;
#else
    o_color.rgb = finalLinearHdr;
    if (g_Material.domain == MaterialDomain_AlphaTested)
    {
        o_color.a = saturate((material.opacity - g_Material.alphaCutoff)
            / max(fwidth(material.opacity) * 1.4142f, 0.0001f) + 0.5f);
    }
    else
        o_color.a = material.opacity;
#endif
}
