/*
* Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// UVSR modification and exact provenance:
// The PBR material, alpha, normal, G-buffer, and motion body is reproduced
// from src/pbr_gbuffer_ps.hlsl at exact UVSR base
// 578496047c4753077e36f39a68df8c2c323cee58. Its Donut G-buffer structure and
// bindings derive from donut/shaders/passes/gbuffer_ps.hlsl and
// donut/include/donut/shaders/gbuffer_cb.h at pinned Donut commit
// bc1ea24b0486f1c00d89327fe16c0b4dd11c5937. UVSR appends only the exact
// uint2(handle, draw-local SV_PrimitiveID) visibility identity.

#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer_cb.h>
#include "adaptive_msaa_raster_cb.h"

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

#ifndef COMPACT_FALLBACK_GBUFFER
#define COMPACT_FALLBACK_GBUFFER 0
#endif

DECLARE_CBUFFER(
    GBufferFillConstants,
    c_GBuffer,
    GBUFFER_BINDING_VIEW_CONSTANTS,
    GBUFFER_SPACE_VIEW);
DECLARE_PUSH_CONSTANTS(
    AdaptiveMsaaGBufferPushConstants,
    g_Push,
    GBUFFER_BINDING_PUSH_CONSTANTS,
    GBUFFER_SPACE_INPUT);

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
    nointerpolation in uint i_instance : INSTANCE,
    nointerpolation in uint i_geometryInstanceHandle
        : GEOMETRY_INSTANCE_HANDLE,
    in bool i_isFrontFace : SV_IsFrontFace,
    in uint i_primitiveIndex : SV_PrimitiveID,
    out float4 o_channel0 : SV_Target0,
    out float4 o_channel1 : SV_Target1,
    out float4 o_channel2 : SV_Target2,
    out float4 o_channel3 : SV_Target3,
    out float o_materialAmbientOcclusion : SV_Target4
#if MOTION_VECTORS
    , out float4 o_motion : SV_Target5
    , out uint2 o_surfaceKey : SV_Target6
#if COMPACT_FALLBACK_GBUFFER
    , out uint o_visibilityClass : SV_Target7
#endif
#else
    , out uint2 o_surfaceKey : SV_Target5
#if COMPACT_FALLBACK_GBUFFER
    , out uint o_visibilityClass : SV_Target6
#endif
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

    float dielectricF0 =
        (g_Material.flags & MaterialFlags_UseSpecularGlossModel) == 0 &&
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

#if MOTION_VECTORS
    // The alpha channel distinguishes a valid zero velocity from the cleared
    // background and from a previous position behind the camera. Donut's
    // helper returns zero in the latter case, so test the previous clip W here
    // without changing the pinned dependency. XY remains de-jittered
    // current-to-previous motion in pixels; Z remains previous minus current
    // device depth.
    float4 previousClip = mul(
        float4(i_vtx.prevPos, 1.0f),
        c_GBuffer.viewPrev.matWorldToClip);
    bool validPreviousPosition =
        previousClip.w > 0.0f && isfinite(previousClip.w);
    o_motion = validPreviousPosition
        ? float4(GetMotionVector(
            i_position.xyz,
            i_vtx.prevPos,
            c_GBuffer.view,
            c_GBuffer.viewPrev), 1.0f)
        : 0.0f;
#endif

    o_surfaceKey = uint2(i_geometryInstanceHandle, i_primitiveIndex);
#if COMPACT_FALLBACK_GBUFFER
    o_visibilityClass = g_Push.visibilityClass;
#endif
}
