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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef UVSR_RENDERER_GPU_CONTRACT_H
#define UVSR_RENDERER_GPU_CONTRACT_H

#include "renderer_view_contract.h"

// full-width IDs keep 65535 available; no-hit is outside nonnegative scene indices.
static const UVSR_GPU_UINT RendererInvalidPickId = 0xffffffffu;

static const int MaterialDomain_Opaque = 0;
static const int MaterialDomain_AlphaTested = 1;
static const int MaterialDomain_AlphaBlended = 2;
static const int MaterialDomain_Transmissive = 3;
static const int MaterialDomain_TransmissiveAlphaTested = 4;
static const int MaterialDomain_TransmissiveAlphaBlended = 5;

static const int MaterialFlags_UseSpecularGlossModel = 0x00000001;
static const int MaterialFlags_DoubleSided = 0x00000002;
static const int MaterialFlags_UseMetalRoughOrSpecularTexture = 0x00000004;
static const int MaterialFlags_UseBaseOrDiffuseTexture = 0x00000008;
static const int MaterialFlags_UseEmissiveTexture = 0x00000010;
static const int MaterialFlags_UseNormalTexture = 0x00000020;
static const int MaterialFlags_UseOcclusionTexture = 0x00000040;
static const int MaterialFlags_UseTransmissionTexture = 0x00000080;
static const int MaterialFlags_MetalnessInRedChannel = 0x00000100;
static const int MaterialFlags_UseOpacityTexture = 0x00000200;
static const int MaterialFlags_SubsurfaceScattering = 0x00000400;
static const int MaterialFlags_Hair = 0x00000800;

struct MaterialConstants
{
    UVSR_GPU_FLOAT3 baseOrDiffuseColor;
    int flags;
    UVSR_GPU_FLOAT3 specularColor;
    int materialID;
    UVSR_GPU_FLOAT3 emissiveColor;
    int domain;
    float opacity;
    float roughness;
    float metalness;
    float normalTextureScale;
    float occlusionStrength;
    float alphaCutoff;
    float transmissionFactor;
    int baseOrDiffuseTextureIndex;
    int metalRoughOrSpecularTextureIndex;
    int emissiveTextureIndex;
    int normalTextureIndex;
    int occlusionTextureIndex;
    int transmissionTextureIndex;
    int opacityTextureIndex;
    UVSR_GPU_FLOAT2 normalTextureTransformScale;
    UVSR_GPU_UINT3 padding1;
    float sssScale;
    UVSR_GPU_FLOAT3 sssTransmissionColor;
    float sssAnisotropy;
    UVSR_GPU_FLOAT3 sssScatteringColor;
    float hairMelanin;
    UVSR_GPU_FLOAT3 hairBaseColor;
    float hairMelaninRedness;
    float hairLongitudinalRoughness;
    float hairAzimuthalRoughness;
    float hairIor;
    float hairCuticleAngle;
    UVSR_GPU_FLOAT3 hairDiffuseReflectionTint;
    float hairDiffuseReflectionWeight;
};

// one material allocation serves structured ray reads and aligned raster CB ranges.
// active MaterialConstants fields retain their existing offsets and 208-byte size.
struct RendererMaterialTableEntry
{
    MaterialConstants material;
    UVSR_GPU_UINT4 padding[3];
};

struct GeometryData
{
    UVSR_GPU_UINT numIndices;
    UVSR_GPU_UINT numVertices;
    int indexBufferIndex;
    UVSR_GPU_UINT indexOffset;
    int vertexBufferIndex;
    UVSR_GPU_UINT positionOffset;
    UVSR_GPU_UINT prevPositionOffset;
    UVSR_GPU_UINT texCoord1Offset;
    UVSR_GPU_UINT texCoord2Offset;
    UVSR_GPU_UINT normalOffset;
    UVSR_GPU_UINT tangentOffset;
    UVSR_GPU_UINT curveRadiusOffset;
    UVSR_GPU_UINT materialIndex;
    UVSR_GPU_UINT pad0;
    UVSR_GPU_UINT pad1;
    UVSR_GPU_UINT pad2;
};

static const UVSR_GPU_UINT InstanceFlags_CurveDisjointOrthogonalTriangleStrips =
    0x00000001u;
static const UVSR_GPU_UINT InstanceFlags_CurveLinearSweptSpheres = 0x00000002u;

struct InstanceData
{
    UVSR_GPU_UINT flags;
    UVSR_GPU_UINT firstGeometryInstanceIndex;
    UVSR_GPU_UINT firstGeometryIndex;
    UVSR_GPU_UINT numGeometries;
    UVSR_GPU_FLOAT3X4 transform;
    UVSR_GPU_FLOAT3X4 prevTransform;

    bool IsCurveDOTS()
    {
        return (flags &
            InstanceFlags_CurveDisjointOrthogonalTriangleStrips) != 0;
    }
    bool IsCurveLSS()
    {
        return (flags & InstanceFlags_CurveLinearSweptSpheres) != 0;
    }
};

static const UVSR_GPU_UINT c_SizeOfTriangleIndices = 12u;
static const UVSR_GPU_UINT c_SizeOfPosition = 12u;
static const UVSR_GPU_UINT c_SizeOfTexcoord = 8u;
static const UVSR_GPU_UINT c_SizeOfNormal = 4u;

static const int UVSR_LIGHT_TYPE_NONE = 0;
static const int UVSR_LIGHT_TYPE_DIRECTIONAL = 1;
static const int UVSR_LIGHT_TYPE_SPOT = 2;
static const int UVSR_LIGHT_TYPE_POINT = 3;

struct ShadowConstants
{
    UVSR_GPU_FLOAT4X4 matWorldToUvzwShadow;
    UVSR_GPU_FLOAT2 shadowFadeScale;
    UVSR_GPU_FLOAT2 shadowFadeBias;
    UVSR_GPU_FLOAT2 shadowMapCenterUV;
    float shadowFalloffDistance;
    int shadowMapArrayIndex;
    UVSR_GPU_FLOAT2 shadowMapSizeTexels;
    UVSR_GPU_FLOAT2 shadowMapSizeTexelsInv;
};

struct LightConstants
{
    UVSR_GPU_FLOAT3 direction;
    int lightType;
    UVSR_GPU_FLOAT3 position;
    float radius;
    UVSR_GPU_FLOAT3 color;
    float intensity;
    float angularSizeOrInvRange;
    float innerAngle;
    float outerAngle;
    float outOfBoundsShadow;
    UVSR_GPU_INT4 shadowCascades;
    UVSR_GPU_INT4 perObjectShadows;
    UVSR_GPU_INT4 shadowChannel;
};

struct LightProbeConstants
{
    float diffuseScale;
    float specularScale;
    float mipLevels;
    float padding1;
    UVSR_GPU_UINT diffuseArrayIndex;
    UVSR_GPU_UINT specularArrayIndex;
    UVSR_GPU_UINT2 padding2;
    UVSR_GPU_FLOAT4 frustumPlanes[6];
};

#define UVSR_DEFERRED_MAX_LIGHTS 16
#define UVSR_DEFERRED_MAX_SHADOWS 16
#define UVSR_DEFERRED_MAX_LIGHT_PROBES 16

struct DeferredLightingConstants
{
    RendererViewConstants view;
    UVSR_GPU_FLOAT2 shadowMapTextureSize;
    int enableAmbientOcclusion;
    int padding;
    UVSR_GPU_FLOAT4 ambientColorTop;
    UVSR_GPU_FLOAT4 ambientColorBottom;
    UVSR_GPU_UINT numLights;
    UVSR_GPU_UINT numLightProbes;
    float indirectDiffuseScale;
    float indirectSpecularScale;
    UVSR_GPU_FLOAT2 randomOffset;
    UVSR_GPU_FLOAT2 padding2;
    UVSR_GPU_FLOAT4 noisePattern[4];
    LightConstants lights[UVSR_DEFERRED_MAX_LIGHTS];
    ShadowConstants shadows[UVSR_DEFERRED_MAX_SHADOWS];
    LightProbeConstants lightProbes[UVSR_DEFERRED_MAX_LIGHT_PROBES];
};

#define UVSR_GBUFFER_SPACE_MATERIAL 0
#define UVSR_GBUFFER_BINDING_MATERIAL_CONSTANTS 0
#define UVSR_GBUFFER_BINDING_MATERIAL_DIFFUSE_TEXTURE 0
#define UVSR_GBUFFER_BINDING_MATERIAL_SPECULAR_TEXTURE 1
#define UVSR_GBUFFER_BINDING_MATERIAL_NORMAL_TEXTURE 2
#define UVSR_GBUFFER_BINDING_MATERIAL_EMISSIVE_TEXTURE 3
#define UVSR_GBUFFER_BINDING_MATERIAL_OCCLUSION_TEXTURE 4
#define UVSR_GBUFFER_BINDING_MATERIAL_TRANSMISSION_TEXTURE 5
#define UVSR_GBUFFER_BINDING_MATERIAL_OPACITY_TEXTURE 6
#define UVSR_GBUFFER_SPACE_INPUT 1
#define UVSR_GBUFFER_BINDING_PUSH_CONSTANTS 1
#define UVSR_GBUFFER_BINDING_INSTANCE_BUFFER 10
#define UVSR_GBUFFER_BINDING_VERTEX_BUFFER 11
#define UVSR_GBUFFER_SPACE_VIEW 2
#define UVSR_GBUFFER_BINDING_VIEW_CONSTANTS 2
#define UVSR_GBUFFER_BINDING_MATERIAL_SAMPLER 0

struct GBufferFillConstants
{
    RendererViewConstants view;
};

struct GBufferPushConstants
{
    UVSR_GPU_UINT startInstanceLocation;
    UVSR_GPU_UINT startVertexLocation;
    UVSR_GPU_UINT positionOffset;
    UVSR_GPU_UINT texCoordOffset;
    UVSR_GPU_UINT normalOffset;
    UVSR_GPU_UINT tangentOffset;
};

#ifdef __cplusplus
#define UVSR_GPU_SEMANTIC(name)
#define UVSR_GPU_CENTROID
#else
#define UVSR_GPU_SEMANTIC(name) : name
#define UVSR_GPU_CENTROID centroid
#endif

struct SceneVertex
{
    UVSR_GPU_FLOAT3 pos UVSR_GPU_SEMANTIC(POS);
    UVSR_GPU_FLOAT2 texCoord UVSR_GPU_SEMANTIC(TEXCOORD);
    UVSR_GPU_CENTROID UVSR_GPU_FLOAT3 normal UVSR_GPU_SEMANTIC(NORMAL);
    UVSR_GPU_CENTROID UVSR_GPU_FLOAT4 tangent UVSR_GPU_SEMANTIC(TANGENT);
};

#undef UVSR_GPU_CENTROID
#undef UVSR_GPU_SEMANTIC

#ifndef __cplusplus
struct MaterialSample
{
    float3 shadingNormal;
    float3 geometryNormal;
    float3 diffuseAlbedo;
    float3 specularF0;
    float3 emissiveColor;
    float opacity;
    float occlusion;
    float roughness;
    float3 baseColor;
    float metalness;
    float transmission;
    bool hasMetalRoughParams;
};

struct MaterialTextureSample
{
    float4 baseOrDiffuse;
    float4 metalRoughOrSpecular;
    float4 normal;
    float4 emissive;
    float4 occlusion;
    float4 transmission;
    float opacity;
};
#endif

#endif // UVSR_RENDERER_GPU_CONTRACT_H
