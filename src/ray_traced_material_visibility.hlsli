#ifndef UVSR_RAY_TRACED_MATERIAL_VISIBILITY_HLSLI
#define UVSR_RAY_TRACED_MATERIAL_VISIBILITY_HLSLI

#include "renderer_gpu_contract.h"
#include "ray_traced_material_bindings.h"
#include "ray_material_visibility_contract.h"

StructuredBuffer<GeometryData> t_RayMaterialGeometries :
    register(UVSR_RAY_MATERIAL_GEOMETRY_REGISTER);
StructuredBuffer<MaterialConstants> t_RayMaterials :
    register(UVSR_RAY_MATERIAL_CONSTANTS_REGISTER);
StructuredBuffer<uint> t_RayGeometryIndexMap :
    register(UVSR_RAY_MATERIAL_GEOMETRY_INDEX_REGISTER);
SamplerState s_RayMaterialSampler :
    register(UVSR_RAY_MATERIAL_SAMPLER_REGISTER);

ByteAddressBuffer t_RayMaterialBuffers[] : register(
    UVSR_RAY_MATERIAL_BUFFER_REGISTER,
    UVSR_RAY_MATERIAL_BUFFER_SPACE);
Texture2D<float4> t_RayMaterialTextures[] : register(
    UVSR_RAY_MATERIAL_TEXTURE_REGISTER,
    UVSR_RAY_MATERIAL_TEXTURE_SPACE);

bool RayMaterialTryElementAddress(
    uint baseOffset,
    uint elementIndex,
    uint elementStride,
    uint loadSize,
    uint bufferSize,
    out uint address)
{
    address = 0u;
    if (baseOffset == ~0u || elementStride == 0u ||
        loadSize > bufferSize || baseOffset > bufferSize - loadSize)
    {
        return false;
    }
    const uint remaining = bufferSize - baseOffset - loadSize;
    if (elementIndex > remaining / elementStride)
        return false;
    address = baseOffset + elementIndex * elementStride;
    return true;
}

bool RayMaterialTryResolveBounded(
    uint geometryMapOffset,
    uint compactGeometryIndex,
    uint4 limits,
    out GeometryData geometry,
    out MaterialConstants material)
{
    geometry = (GeometryData)0;
    material = (MaterialConstants)0;
    if (geometryMapOffset >= limits.x ||
        compactGeometryIndex >= limits.x - geometryMapOffset)
    {
        return false;
    }
    const uint globalGeometryIndex = t_RayGeometryIndexMap[
        geometryMapOffset + compactGeometryIndex];
    if (globalGeometryIndex >= limits.y)
        return false;
    geometry = t_RayMaterialGeometries[globalGeometryIndex];
    if (geometry.materialIndex >= limits.z)
        return false;
    material = t_RayMaterials[geometry.materialIndex];
    return true;
}

bool RayMaterialInterpolateTexCoordBounded(
    GeometryData geometry,
    uint primitiveIndex,
    float2 candidateBarycentrics,
    uint descriptorCapacity,
    out float2 texCoord)
{
    texCoord = 0.0f;
    if (geometry.indexBufferIndex < 0 ||
        uint(geometry.indexBufferIndex) >= descriptorCapacity ||
        geometry.vertexBufferIndex < 0 ||
        uint(geometry.vertexBufferIndex) >= descriptorCapacity ||
        geometry.texCoord1Offset == ~0u ||
        primitiveIndex >= geometry.numIndices / 3u)
    {
        return false;
    }

    ByteAddressBuffer indexBuffer = t_RayMaterialBuffers[
        NonUniformResourceIndex(geometry.indexBufferIndex)];
    ByteAddressBuffer vertexBuffer = t_RayMaterialBuffers[
        NonUniformResourceIndex(geometry.vertexBufferIndex)];
    uint indexBufferSize = 0u;
    uint vertexBufferSize = 0u;
    indexBuffer.GetDimensions(indexBufferSize);
    vertexBuffer.GetDimensions(vertexBufferSize);
    uint indexAddress = 0u;
    if (!RayMaterialTryElementAddress(
            geometry.indexOffset,
            primitiveIndex,
            c_SizeOfTriangleIndices,
            c_SizeOfTriangleIndices,
            indexBufferSize,
            indexAddress))
    {
        return false;
    }
    const uint3 indices = indexBuffer.Load3(indexAddress);
    if (any(indices >= geometry.numVertices))
        return false;

    uint3 texCoordAddresses = 0u;
    if (!RayMaterialTryElementAddress(
            geometry.texCoord1Offset, indices.x, c_SizeOfTexcoord,
            c_SizeOfTexcoord, vertexBufferSize, texCoordAddresses.x) ||
        !RayMaterialTryElementAddress(
            geometry.texCoord1Offset, indices.y, c_SizeOfTexcoord,
            c_SizeOfTexcoord, vertexBufferSize, texCoordAddresses.y) ||
        !RayMaterialTryElementAddress(
            geometry.texCoord1Offset, indices.z, c_SizeOfTexcoord,
            c_SizeOfTexcoord, vertexBufferSize, texCoordAddresses.z))
    {
        return false;
    }
    const float2 texCoords[3] = {
        asfloat(vertexBuffer.Load2(texCoordAddresses.x)),
        asfloat(vertexBuffer.Load2(texCoordAddresses.y)),
        asfloat(vertexBuffer.Load2(texCoordAddresses.z))
    };
    const float3 barycentrics = float3(
        1.0f - candidateBarycentrics.x - candidateBarycentrics.y,
        candidateBarycentrics);
    texCoord = texCoords[0] * barycentrics.x +
        texCoords[1] * barycentrics.y +
        texCoords[2] * barycentrics.z;
    return true;
}

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
    float2 candidateBarycentrics,
    bool candidateFrontFace,
    bool requirePathTransportMaterial)
{
    const uint globalGeometryIndex = t_RayGeometryIndexMap[
        geometryMapOffset + compactGeometryIndex];
    const GeometryData geometry =
        t_RayMaterialGeometries[globalGeometryIndex];
    const MaterialConstants material =
        t_RayMaterials[geometry.materialIndex];
    const bool opacityTextureAvailable =
        (material.flags & MaterialFlags_UseOpacityTexture) != 0 &&
        material.opacityTextureIndex >= 0;
    const bool baseAlphaTextureAvailable =
        (material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0 &&
        material.baseOrDiffuseTextureIndex >= 0;
    const RayMaterialCoveragePlan coveragePlan =
        ResolveRayMaterialCoveragePlan(
            candidateFrontFace,
            (material.flags & MaterialFlags_DoubleSided) != 0,
            requirePathTransportMaterial,
            (material.flags & (MaterialFlags_SubsurfaceScattering |
                    MaterialFlags_Hair)) != 0 ||
                material.transmissionFactor > 0.0f,
            material.domain == MaterialDomain_Opaque,
            material.domain == MaterialDomain_AlphaTested,
            opacityTextureAvailable,
            baseAlphaTextureAvailable);
    if (coveragePlan.mode == UVSR_RAY_MATERIAL_COVERAGE_OPAQUE)
        return true;
    if (coveragePlan.mode !=
        UVSR_RAY_MATERIAL_COVERAGE_ALPHA_TESTED)
    {
        return false;
    }

    float2 texCoord = 0.0f;
    if (coveragePlan.alphaSource != UVSR_RAY_MATERIAL_ALPHA_NONE)
    {
        texCoord = RayMaterialInterpolateTexCoord(
            geometry,
            primitiveIndex,
            candidateBarycentrics);
    }
    float textureOpacity = 1.0f;
    if (coveragePlan.alphaSource ==
        UVSR_RAY_MATERIAL_ALPHA_OPACITY_TEXTURE)
    {
        Texture2D<float4> opacityTexture = t_RayMaterialTextures[
            NonUniformResourceIndex(material.opacityTextureIndex)];
        textureOpacity = opacityTexture.SampleLevel(
            s_RayMaterialSampler,
            texCoord,
            0.0f).r;
    }
    else if (coveragePlan.alphaSource ==
        UVSR_RAY_MATERIAL_ALPHA_BASE_TEXTURE)
    {
        Texture2D<float4> baseTexture = t_RayMaterialTextures[
            NonUniformResourceIndex(material.baseOrDiffuseTextureIndex)];
        textureOpacity = baseTexture.SampleLevel(
            s_RayMaterialSampler,
            texCoord,
            0.0f).a;
    }
    return ResolveRayMaterialCandidateCoverage(
        coveragePlan,
        material.opacity,
        textureOpacity,
        material.alphaCutoff,
        true);
}

bool RayMaterialCandidateIsCoveredBounded(
    uint geometryMapOffset,
    uint compactGeometryIndex,
    uint primitiveIndex,
    float2 candidateBarycentrics,
    bool candidateFrontFace,
    bool requirePathTransportMaterial,
    uint4 limits)
{
    GeometryData geometry;
    MaterialConstants material;
    if (!RayMaterialTryResolveBounded(
            geometryMapOffset,
            compactGeometryIndex,
            limits,
            geometry,
            material))
    {
        return false;
    }
    const bool opacityTextureAvailable =
        (material.flags & MaterialFlags_UseOpacityTexture) != 0 &&
        material.opacityTextureIndex >= 0;
    const bool baseAlphaTextureAvailable =
        (material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0 &&
        material.baseOrDiffuseTextureIndex >= 0;
    const RayMaterialCoveragePlan coveragePlan =
        ResolveRayMaterialCoveragePlan(
            candidateFrontFace,
            (material.flags & MaterialFlags_DoubleSided) != 0,
            requirePathTransportMaterial,
            (material.flags & (MaterialFlags_SubsurfaceScattering |
                    MaterialFlags_Hair)) != 0 ||
                material.transmissionFactor > 0.0f,
            material.domain == MaterialDomain_Opaque,
            material.domain == MaterialDomain_AlphaTested,
            opacityTextureAvailable,
            baseAlphaTextureAvailable);
    if (coveragePlan.mode == UVSR_RAY_MATERIAL_COVERAGE_OPAQUE)
        return true;
    if (coveragePlan.mode !=
        UVSR_RAY_MATERIAL_COVERAGE_ALPHA_TESTED)
    {
        return false;
    }

    if ((coveragePlan.alphaSource ==
            UVSR_RAY_MATERIAL_ALPHA_OPACITY_TEXTURE &&
            uint(material.opacityTextureIndex) >= limits.w) ||
        (coveragePlan.alphaSource ==
            UVSR_RAY_MATERIAL_ALPHA_BASE_TEXTURE &&
            uint(material.baseOrDiffuseTextureIndex) >= limits.w))
    {
        return false;
    }
    float2 texCoord = 0.0f;
    if (coveragePlan.alphaSource != UVSR_RAY_MATERIAL_ALPHA_NONE &&
        !RayMaterialInterpolateTexCoordBounded(
            geometry,
            primitiveIndex,
            candidateBarycentrics,
            limits.w,
            texCoord))
    {
        return false;
    }
    float textureOpacity = 1.0f;
    if (coveragePlan.alphaSource ==
        UVSR_RAY_MATERIAL_ALPHA_OPACITY_TEXTURE)
    {
        Texture2D<float4> opacityTexture = t_RayMaterialTextures[
            NonUniformResourceIndex(material.opacityTextureIndex)];
        textureOpacity = opacityTexture.SampleLevel(
            s_RayMaterialSampler, texCoord, 0.0f).r;
    }
    else if (coveragePlan.alphaSource ==
        UVSR_RAY_MATERIAL_ALPHA_BASE_TEXTURE)
    {
        Texture2D<float4> baseTexture = t_RayMaterialTextures[
            NonUniformResourceIndex(material.baseOrDiffuseTextureIndex)];
        textureOpacity = baseTexture.SampleLevel(
            s_RayMaterialSampler, texCoord, 0.0f).a;
    }
    return ResolveRayMaterialCandidateCoverage(
        coveragePlan,
        material.opacity,
        textureOpacity,
        material.alphaCutoff,
        true);
}

#define UVSR_COMMIT_COVERED_RAY_QUERY_CANDIDATE(query) \
    if ((query).CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && \
        RayMaterialCandidateIsCovered( \
            (query).CandidateInstanceContributionToHitGroupIndex(), \
            (query).CandidateGeometryIndex(), \
            (query).CandidatePrimitiveIndex(), \
            (query).CandidateTriangleBarycentrics(), \
            (query).CandidateTriangleFrontFace(), \
            false)) \
    { \
        (query).CommitNonOpaqueTriangleHit(); \
    }

#define UVSR_COMMIT_PATH_TRACING_RAY_QUERY_CANDIDATE(query) \
    if ((query).CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && \
        RayMaterialCandidateIsCoveredBounded( \
            (query).CandidateInstanceContributionToHitGroupIndex(), \
            (query).CandidateGeometryIndex(), \
            (query).CandidatePrimitiveIndex(), \
            (query).CandidateTriangleBarycentrics(), \
            (query).CandidateTriangleFrontFace(), \
            true, \
            g_PathTracing.rayMaterialLimits)) \
    { \
        (query).CommitNonOpaqueTriangleHit(); \
    }

#endif
