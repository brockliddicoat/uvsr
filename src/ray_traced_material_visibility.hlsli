#ifndef UVSR_RAY_TRACED_MATERIAL_VISIBILITY_HLSLI
#define UVSR_RAY_TRACED_MATERIAL_VISIBILITY_HLSLI

#include <donut/shaders/bindless.h>
#include <donut/shaders/binding_helpers.hlsli>

StructuredBuffer<GeometryData> t_RayMaterialGeometries : register(t10);
StructuredBuffer<MaterialConstants> t_RayMaterials : register(t11);
StructuredBuffer<uint> t_RayGeometryIndexMap : register(t12);
SamplerState s_RayMaterialSampler : register(s0);

VK_BINDING(0, 1)
ByteAddressBuffer t_RayMaterialBuffers[] : register(t0, space1);
VK_BINDING(1, 1)
Texture2D<float4> t_RayMaterialTextures[] : register(t0, space2);

float2 RayMaterialInterpolateTexCoord(
    GeometryData geometry,
    uint primitiveIndex,
    float2 candidateBarycentrics)
{
    if (geometry.indexBufferIndex < 0 ||
        geometry.vertexBufferIndex < 0 ||
        geometry.texCoord1Offset == ~0u)
    {
        return 0.0f;
    }

    ByteAddressBuffer indexBuffer = t_RayMaterialBuffers[
        NonUniformResourceIndex(geometry.indexBufferIndex)];
    ByteAddressBuffer vertexBuffer = t_RayMaterialBuffers[
        NonUniformResourceIndex(geometry.vertexBufferIndex)];
    const uint3 indices = indexBuffer.Load3(
        geometry.indexOffset +
            primitiveIndex * c_SizeOfTriangleIndices);
    const float2 texCoords[3] = {
        asfloat(vertexBuffer.Load2(
            geometry.texCoord1Offset +
                indices.x * c_SizeOfTexcoord)),
        asfloat(vertexBuffer.Load2(
            geometry.texCoord1Offset +
                indices.y * c_SizeOfTexcoord)),
        asfloat(vertexBuffer.Load2(
            geometry.texCoord1Offset +
                indices.z * c_SizeOfTexcoord))
    };
    const float3 barycentrics = float3(
        1.0f - candidateBarycentrics.x - candidateBarycentrics.y,
        candidateBarycentrics);
    return texCoords[0] * barycentrics.x +
        texCoords[1] * barycentrics.y +
        texCoords[2] * barycentrics.z;
}

bool RayMaterialCandidateIsCovered(
    uint geometryMapOffset,
    uint compactGeometryIndex,
    uint primitiveIndex,
    float2 candidateBarycentrics)
{
    const uint globalGeometryIndex = t_RayGeometryIndexMap[
        geometryMapOffset + compactGeometryIndex];
    const GeometryData geometry =
        t_RayMaterialGeometries[globalGeometryIndex];
    const MaterialConstants material =
        t_RayMaterials[geometry.materialIndex];
    if (material.domain != MaterialDomain_AlphaTested)
        return false;

    const float2 texCoord = RayMaterialInterpolateTexCoord(
        geometry,
        primitiveIndex,
        candidateBarycentrics);
    float opacity = material.opacity;
    if ((material.flags & MaterialFlags_UseOpacityTexture) != 0 &&
        material.opacityTextureIndex >= 0)
    {
        Texture2D<float4> opacityTexture = t_RayMaterialTextures[
            NonUniformResourceIndex(material.opacityTextureIndex)];
        opacity *= opacityTexture.SampleLevel(
            s_RayMaterialSampler,
            texCoord,
            0.0f).r;
    }
    else if ((material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0 &&
        material.baseOrDiffuseTextureIndex >= 0)
    {
        Texture2D<float4> baseTexture = t_RayMaterialTextures[
            NonUniformResourceIndex(material.baseOrDiffuseTextureIndex)];
        opacity *= baseTexture.SampleLevel(
            s_RayMaterialSampler,
            texCoord,
            0.0f).a;
    }
    return saturate(opacity) >= material.alphaCutoff;
}

#define UVSR_COMMIT_COVERED_RAY_QUERY_CANDIDATE(query) \
    if ((query).CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && \
        RayMaterialCandidateIsCovered( \
            (query).CandidateInstanceContributionToHitGroupIndex(), \
            (query).CandidateGeometryIndex(), \
            (query).CandidatePrimitiveIndex(), \
            (query).CandidateTriangleBarycentrics())) \
    { \
        (query).CommitNonOpaqueTriangleHit(); \
    }

#endif
