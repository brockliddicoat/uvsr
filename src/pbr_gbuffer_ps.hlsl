#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"
#include "pbr_gbuffer.hlsli"

cbuffer c_Material : register(b0, space0)
{
    MaterialConstants g_Material;
};

Texture2D t_BaseOrDiffuse : register(t0, space0);
Texture2D t_MetalRoughOrSpecular : register(t1, space0);
Texture2D t_Normal : register(t2, space0);
Texture2D t_Emissive : register(t3, space0);
Texture2D t_Occlusion : register(t4, space0);
Texture2D t_Transmission : register(t5, space0);
Texture2D t_Opacity : register(t6, space0);
SamplerState s_MaterialSampler : register(s0, space2);

ConstantBuffer<GBufferFillConstants> c_GBuffer : register(b2, space2);
ConstantBuffer<GBufferPushConstants> g_Push : register(b1, space1);

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

float3 GetGBufferViewDirection(
    float4 directionOrPosition,
    float3 surfacePosition)
{
    return directionOrPosition.w > 0.0f
        ? directionOrPosition.xyz - surfacePosition
        : -directionOrPosition.xyz;
}

void main(
    in float4 i_position : SV_Position,
    in SceneVertex i_vtx,
    in bool i_isFrontFace : SV_IsFrontFace,
    out float4 o_channel0 : SV_Target0,
    out float4 o_channel1 : SV_Target1,
    out float4 o_channel2 : SV_Target2,
    out float4 o_channel3 : SV_Target3,
    out float o_materialAmbientOcclusion : SV_Target4
#if MOTION_VECTORS
    , out float4 o_motion : SV_Target5
#endif
)
{
    MaterialTextureSample textures = SampleMaterialTexturesAuto(
        g_Material,
        t_BaseOrDiffuse,
        t_MetalRoughOrSpecular,
        t_Normal,
        t_Emissive,
        t_Occlusion,
        t_Transmission,
        t_Opacity,
        s_MaterialSampler,
        i_vtx.texCoord,
        g_Material.normalTextureTransformScale);
#if WHITE_WORLD
    // Retain base alpha for cutout coverage while preventing sampled RGB from
    // leaking into the white reference material.
    textures.baseOrDiffuse.rgb = 1.0f;
#endif
    MaterialSample surface = EvaluateSceneMaterial(
        i_vtx.normal, i_vtx.tangent, g_Material, textures);

    float3 viewDirection = GetGBufferViewDirection(
        c_GBuffer.view.cameraDirectionOrPosition,
        i_vtx.pos);
    bool isDoubleSided =
        (g_Material.flags & MaterialFlags_DoubleSided) != 0;
    if (ShouldFlipPbrSurfaceNormals(
        isDoubleSided,
        i_isFrontFace,
        surface.geometryNormal,
        viewDirection))
    {
        surface.shadingNormal = -surface.shadingNormal;
        surface.geometryNormal = -surface.geometryNormal;
    }
    // Donut's material geometry normal is an interpolated vertex normal. Keep
    // that as the degenerate fallback, but store the actual raster triangle
    // plane normal for hemisphere tests and future ray-origin construction.
    // World-position derivatives lie in the same transformed triangle plane
    // represented by the BLAS, including non-uniform instance transforms.
    const PbrContractSurfaceNormals orientedNormals =
        ResolvePbrTriangleSurfaceNormals(
            ddx(i_vtx.pos),
            ddy(i_vtx.pos),
            surface.geometryNormal,
            surface.shadingNormal,
            viewDirection);
    const float3 triangleNormal = orientedNormals.geometricNormal;
    surface.shadingNormal = orientedNormals.shadingNormal;

#if ALPHA_TESTED
    if (g_Material.domain != MaterialDomain_Opaque)
        clip(surface.opacity - g_Material.alphaCutoff);
#endif

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
    pbrData.geometricNormal = triangleNormal;
    pbrData.ambientOcclusion = surface.occlusion;
    EncodePbrGBuffer(
        pbrData,
        o_channel0,
        o_channel1,
        o_channel2,
        o_channel3,
        o_materialAmbientOcclusion);

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
