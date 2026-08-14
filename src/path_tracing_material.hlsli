#ifndef UVSR_PATH_TRACING_MATERIAL_HLSLI
#define UVSR_PATH_TRACING_MATERIAL_HLSLI

#include <donut/shaders/packing.hlsli>

#define ENABLE_METAL_ROUGH_RECONSTRUCTION 1
#include <donut/shaders/scene_material.hlsli>
#include "pbr.hlsli"
#include "ray_traced_material_visibility.hlsli"

struct PathTracingSurface
{
    float3 position;
    float3 previousPosition;
    float3 geometricNormal;
    float3 shadingNormal;
    float3 tangent;
    float tangentSign;
    float2 texCoord;
    float hitDistance;
    uint materialIndex;
    MaterialConstants materialConstants;
    MaterialSample material;
    PbrPreparedMaterial preparedMaterial;
    uint preparedMaterialValid;
};

bool PathTracingLoadPositionAtOffset(
    ByteAddressBuffer vertexBuffer,
    uint positionOffset,
    uint vertexIndex,
    uint vertexBufferSize,
    out float3 position)
{
    position = 0.0f;
    uint address = 0u;
    if (!RayMaterialTryElementAddress(
            positionOffset,
            vertexIndex,
            c_SizeOfPosition,
            c_SizeOfPosition,
            vertexBufferSize,
            address))
    {
        return false;
    }
    position = asfloat(vertexBuffer.Load3(address));
    return true;
}

bool PathTracingLoadPosition(
    ByteAddressBuffer vertexBuffer,
    GeometryData geometry,
    uint vertexIndex,
    uint vertexBufferSize,
    out float3 position)
{
    return PathTracingLoadPositionAtOffset(
        vertexBuffer,
        geometry.positionOffset,
        vertexIndex,
        vertexBufferSize,
        position);
}

float3 PathTracingLoadNormal(
    ByteAddressBuffer vertexBuffer,
    GeometryData geometry,
    uint vertexIndex,
    uint vertexBufferSize,
    float3 fallback)
{
    uint address = 0u;
    if (!RayMaterialTryElementAddress(
            geometry.normalOffset,
            vertexIndex,
            c_SizeOfNormal,
            c_SizeOfNormal,
            vertexBufferSize,
            address))
    {
        return fallback;
    }
    return Unpack_RGB8_SNORM(vertexBuffer.Load(address));
}

float4 PathTracingLoadTangent(
    ByteAddressBuffer vertexBuffer,
    GeometryData geometry,
    uint vertexIndex,
    uint vertexBufferSize)
{
    uint address = 0u;
    if (!RayMaterialTryElementAddress(
            geometry.tangentOffset,
            vertexIndex,
            c_SizeOfNormal,
            c_SizeOfNormal,
            vertexBufferSize,
            address))
    {
        return 0.0f;
    }
    return Unpack_RGBA8_SNORM(vertexBuffer.Load(address));
}

float2 PathTracingLoadTexCoord(
    ByteAddressBuffer vertexBuffer,
    GeometryData geometry,
    uint vertexIndex,
    uint vertexBufferSize)
{
    uint address = 0u;
    if (!RayMaterialTryElementAddress(
            geometry.texCoord1Offset,
            vertexIndex,
            c_SizeOfTexcoord,
            c_SizeOfTexcoord,
            vertexBufferSize,
            address))
    {
        return 0.0f;
    }
    return asfloat(vertexBuffer.Load2(address));
}

MaterialTextureSample PathTracingSampleMaterialTextures(
    MaterialConstants material,
    float2 texCoord,
    uint descriptorCapacity)
{
    MaterialTextureSample textures = DefaultMaterialTextures();
    if ((material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0 &&
        material.baseOrDiffuseTextureIndex >= 0 &&
        uint(material.baseOrDiffuseTextureIndex) < descriptorCapacity)
    {
        textures.baseOrDiffuse = t_RayMaterialTextures[
            NonUniformResourceIndex(material.baseOrDiffuseTextureIndex)]
            .SampleLevel(s_RayMaterialSampler, texCoord, 0.0f);
    }
    if ((material.flags &
            MaterialFlags_UseMetalRoughOrSpecularTexture) != 0 &&
        material.metalRoughOrSpecularTextureIndex >= 0 &&
        uint(material.metalRoughOrSpecularTextureIndex) < descriptorCapacity)
    {
        textures.metalRoughOrSpecular = t_RayMaterialTextures[
            NonUniformResourceIndex(
                material.metalRoughOrSpecularTextureIndex)]
            .SampleLevel(s_RayMaterialSampler, texCoord, 0.0f);
    }
    if ((material.flags & MaterialFlags_UseNormalTexture) != 0 &&
        material.normalTextureIndex >= 0 &&
        uint(material.normalTextureIndex) < descriptorCapacity)
    {
        textures.normal = t_RayMaterialTextures[
            NonUniformResourceIndex(material.normalTextureIndex)]
            .SampleLevel(
                s_RayMaterialSampler,
                texCoord * material.normalTextureTransformScale,
                0.0f);
    }
    if ((material.flags & MaterialFlags_UseEmissiveTexture) != 0 &&
        material.emissiveTextureIndex >= 0 &&
        uint(material.emissiveTextureIndex) < descriptorCapacity)
    {
        textures.emissive = t_RayMaterialTextures[
            NonUniformResourceIndex(material.emissiveTextureIndex)]
            .SampleLevel(s_RayMaterialSampler, texCoord, 0.0f);
    }
    if ((material.flags & MaterialFlags_UseOcclusionTexture) != 0 &&
        material.occlusionTextureIndex >= 0 &&
        uint(material.occlusionTextureIndex) < descriptorCapacity)
    {
        textures.occlusion = t_RayMaterialTextures[
            NonUniformResourceIndex(material.occlusionTextureIndex)]
            .SampleLevel(s_RayMaterialSampler, texCoord, 0.0f);
    }
    if ((material.flags & MaterialFlags_UseTransmissionTexture) != 0 &&
        material.transmissionTextureIndex >= 0 &&
        uint(material.transmissionTextureIndex) < descriptorCapacity)
    {
        textures.transmission = t_RayMaterialTextures[
            NonUniformResourceIndex(material.transmissionTextureIndex)]
            .SampleLevel(s_RayMaterialSampler, texCoord, 0.0f);
    }
    if ((material.flags & MaterialFlags_UseOpacityTexture) != 0 &&
        material.opacityTextureIndex >= 0 &&
        uint(material.opacityTextureIndex) < descriptorCapacity)
    {
        textures.opacity = t_RayMaterialTextures[
            NonUniformResourceIndex(material.opacityTextureIndex)]
            .SampleLevel(s_RayMaterialSampler, texCoord, 0.0f).r;
    }
    return textures;
}

bool PathTracingTraceSurface(
    RaytracingAccelerationStructure worldBvh,
    float3 rayOrigin,
    float3 rayDirection,
    float rayMinimum,
    float rayMaximum,
    bool loadPreviousPosition,
    out PathTracingSurface surface)
{
    surface = (PathTracingSurface)0;
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDirection;
    ray.TMin = rayMinimum;
    ray.TMax = rayMaximum;

    // Force every triangle through the material-aware candidate callback so
    // single-sided opaque and alpha-tested backfaces are both rejected while
    // double-sided geometry remains visible from either side.
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> query;
    query.TraceRayInline(
        worldBvh, RAY_FLAG_FORCE_NON_OPAQUE, 0xffu, ray);
    while (query.Proceed())
    {
        UVSR_COMMIT_PATH_TRACING_RAY_QUERY_CANDIDATE(query);
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return false;

    GeometryData geometry;
    MaterialConstants materialConstants;
    if (!RayMaterialTryResolveBounded(
            query.CommittedInstanceContributionToHitGroupIndex(),
            query.CommittedGeometryIndex(),
            g_PathTracing.rayMaterialLimits,
            geometry,
            materialConstants) ||
        geometry.indexBufferIndex < 0 ||
        uint(geometry.indexBufferIndex) >=
            g_PathTracing.rayMaterialLimits.w ||
        geometry.vertexBufferIndex < 0 ||
        uint(geometry.vertexBufferIndex) >=
            g_PathTracing.rayMaterialLimits.w ||
        query.CommittedPrimitiveIndex() >= geometry.numIndices / 3u)
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
            query.CommittedPrimitiveIndex(),
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
    const float2 hitBarycentrics =
        query.CommittedTriangleBarycentrics();
    const float3 barycentrics = float3(
        1.0f - hitBarycentrics.x - hitBarycentrics.y,
        hitBarycentrics);

    float3 objectPositions[3];
    if (!PathTracingLoadPosition(
            vertexBuffer, geometry, indices.x,
            vertexBufferSize, objectPositions[0]) ||
        !PathTracingLoadPosition(
            vertexBuffer, geometry, indices.y,
            vertexBufferSize, objectPositions[1]) ||
        !PathTracingLoadPosition(
            vertexBuffer, geometry, indices.z,
            vertexBufferSize, objectPositions[2]))
    {
        return false;
    }
    const float3 objectGeometricNormal = PbrSafeNormalize(
        cross(
            objectPositions[1] - objectPositions[0],
            objectPositions[2] - objectPositions[0]),
        float3(0.0f, 1.0f, 0.0f));
    const float3 objectNormal = PbrSafeNormalize(
        PathTracingLoadNormal(
            vertexBuffer, geometry, indices.x,
            vertexBufferSize, objectGeometricNormal) *
                barycentrics.x +
        PathTracingLoadNormal(
            vertexBuffer, geometry, indices.y,
            vertexBufferSize, objectGeometricNormal) *
                barycentrics.y +
        PathTracingLoadNormal(
            vertexBuffer, geometry, indices.z,
            vertexBufferSize, objectGeometricNormal) *
                barycentrics.z,
        objectGeometricNormal);
    const float4 objectTangent =
        PathTracingLoadTangent(
            vertexBuffer, geometry, indices.x, vertexBufferSize) *
            barycentrics.x +
        PathTracingLoadTangent(
            vertexBuffer, geometry, indices.y, vertexBufferSize) *
            barycentrics.y +
        PathTracingLoadTangent(
            vertexBuffer, geometry, indices.z, vertexBufferSize) *
            barycentrics.z;
    const float2 texCoord =
        PathTracingLoadTexCoord(
            vertexBuffer, geometry, indices.x, vertexBufferSize) *
            barycentrics.x +
        PathTracingLoadTexCoord(
            vertexBuffer, geometry, indices.y, vertexBufferSize) *
            barycentrics.y +
        PathTracingLoadTexCoord(
            vertexBuffer, geometry, indices.z, vertexBufferSize) *
            barycentrics.z;

    const float3x4 objectToWorld = query.CommittedObjectToWorld3x4();
    const float3x4 worldToObject = query.CommittedWorldToObject3x4();
    const float3 worldPositions[3] = {
        mul(objectToWorld, float4(objectPositions[0], 1.0f)),
        mul(objectToWorld, float4(objectPositions[1], 1.0f)),
        mul(objectToWorld, float4(objectPositions[2], 1.0f))
    };
    float3 previousPosition = rayOrigin +
        rayDirection * query.CommittedRayT();
    const uint instanceIndex = query.CommittedInstanceID();
    if (loadPreviousPosition &&
        instanceIndex < g_PathTracing.instanceCount)
    {
        const uint previousOffset = geometry.prevPositionOffset != ~0u
            ? geometry.prevPositionOffset
            : geometry.positionOffset;
        float3 previousObjectPositions[3];
        if (PathTracingLoadPositionAtOffset(
                vertexBuffer,
                previousOffset,
                indices.x,
                vertexBufferSize,
                previousObjectPositions[0]) &&
            PathTracingLoadPositionAtOffset(
                vertexBuffer,
                previousOffset,
                indices.y,
                vertexBufferSize,
                previousObjectPositions[1]) &&
            PathTracingLoadPositionAtOffset(
                vertexBuffer,
                previousOffset,
                indices.z,
                vertexBufferSize,
                previousObjectPositions[2]))
        {
            const float3 previousObjectPosition =
                previousObjectPositions[0] * barycentrics.x +
                previousObjectPositions[1] * barycentrics.y +
                previousObjectPositions[2] * barycentrics.z;
            const InstanceData instance =
                t_PathTracingInstances[instanceIndex];
            previousPosition = mul(
                instance.prevTransform,
                float4(previousObjectPosition, 1.0f));
        }
    }
    float3 geometricNormal = PbrSafeNormalize(
        cross(
            worldPositions[1] - worldPositions[0],
            worldPositions[2] - worldPositions[0]),
        float3(0.0f, 1.0f, 0.0f));
    float3 interpolatedNormal = PbrSafeNormalize(
        mul(objectNormal, (float3x3)worldToObject),
        geometricNormal);
    float3 tangent = PbrSafeNormalize(
        mul((float3x3)objectToWorld, objectTangent.xyz),
        0.0f);

    MaterialSample material = EvaluateSceneMaterial(
        interpolatedNormal,
        float4(tangent, objectTangent.w),
        materialConstants,
        PathTracingSampleMaterialTextures(
            materialConstants,
            texCoord,
            g_PathTracing.rayMaterialLimits.w));
    const float3 viewDirection = -rayDirection;
    const bool doubleSided =
        (materialConstants.flags & MaterialFlags_DoubleSided) != 0;
    if (ShouldFlipPbrSurfaceNormals(
            doubleSided,
            query.CommittedTriangleFrontFace(),
            geometricNormal,
            viewDirection))
    {
        geometricNormal = -geometricNormal;
        material.geometryNormal = -material.geometryNormal;
        material.shadingNormal = -material.shadingNormal;
    }
    if (dot(geometricNormal, viewDirection) < 0.0f)
        geometricNormal = -geometricNormal;
    if (dot(material.shadingNormal, geometricNormal) < 0.0f)
        material.shadingNormal = -material.shadingNormal;

    // Normal maps can leave a view-facing triangle with a shading normal just
    // behind the outgoing hemisphere. Project that normal into the tangent
    // plane and nudge it toward the view to prevent zero-BSDF black specks.
    if (dot(geometricNormal, viewDirection) > 0.0f &&
        dot(material.shadingNormal, viewDirection) <= 0.0f)
    {
        const float3 safeView = PbrSafeNormalize(
            viewDirection,
            geometricNormal);
        const float3 tangentNormal = material.shadingNormal -
            safeView * dot(material.shadingNormal, safeView);
        material.shadingNormal = PbrSafeNormalize(
            tangentNormal + safeView * 1.0e-4f,
            geometricNormal);
    }

    surface.position = rayOrigin + rayDirection * query.CommittedRayT();
    surface.previousPosition = all(isfinite(previousPosition))
        ? previousPosition
        : surface.position;
    surface.geometricNormal = geometricNormal;
    surface.shadingNormal = PbrSafeNormalize(
        material.shadingNormal, geometricNormal);
    surface.tangent = tangent;
    surface.tangentSign = objectTangent.w;
    surface.texCoord = texCoord;
    surface.hitDistance = query.CommittedRayT();
    surface.materialIndex = geometry.materialIndex;
    surface.materialConstants = materialConstants;
    surface.material = material;
    surface.preparedMaterialValid = 0u;
    return all(isfinite(surface.position)) &&
        all(isfinite(surface.geometricNormal)) &&
        all(isfinite(surface.shadingNormal));
}

bool PathTracingTraceOcclusion(
    RaytracingAccelerationStructure worldBvh,
    float3 rayOrigin,
    float3 rayDirection,
    float rayMinimum,
    float rayMaximum)
{
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDirection;
    ray.TMin = rayMinimum;
    ray.TMax = rayMaximum;
    if (!(ray.TMax > ray.TMin))
        return false;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_FORCE_NON_OPAQUE> query;
    query.TraceRayInline(
        worldBvh,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
            RAY_FLAG_FORCE_NON_OPAQUE,
        0xffu,
        ray);
    while (query.Proceed())
    {
        UVSR_COMMIT_PATH_TRACING_RAY_QUERY_CANDIDATE(query);
    }
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

float3 PathTracingPrepareRayOrigin(
    float3 position,
    float3 geometricNormal,
    float3 direction,
    float minimumBias)
{
    const float maximumPosition = max(
        abs(position.x), max(abs(position.y), abs(position.z)));
    const float scaleAwareBias = max(
        minimumBias,
        max(maximumPosition, 1.0f) * 2.0e-6f);
    const float orientation =
        dot(geometricNormal, direction) >= 0.0f ? 1.0f : -1.0f;
    return position + geometricNormal * (orientation * scaleAwareBias);
}

float2 PathTracingEncodeUnitVector(float3 direction)
{
    direction /= max(
        abs(direction.x) + abs(direction.y) + abs(direction.z),
        1.0e-6f);
    float2 encoded = direction.xy;
    if (direction.z < 0.0f)
    {
        encoded = (1.0f - abs(encoded.yx)) *
            float2(encoded.x >= 0.0f ? 1.0f : -1.0f,
                encoded.y >= 0.0f ? 1.0f : -1.0f);
    }
    return encoded;
}

float3 PathTracingDecodeUnitVector(float2 encoded)
{
    float3 direction = float3(
        encoded,
        1.0f - abs(encoded.x) - abs(encoded.y));
    if (direction.z < 0.0f)
    {
        direction.xy = (1.0f - abs(direction.yx)) *
            float2(direction.x >= 0.0f ? 1.0f : -1.0f,
                direction.y >= 0.0f ? 1.0f : -1.0f);
    }
    return PbrSafeNormalize(direction, float3(0.0f, 1.0f, 0.0f));
}

uint PathTracingPackUnitVectorHalf(float3 direction)
{
    const float2 encoded = PathTracingEncodeUnitVector(direction);
    return (f32tof16(encoded.x) & 0xffffu) |
        ((f32tof16(encoded.y) & 0xffffu) << 16u);
}

float3 PathTracingUnpackUnitVectorHalf(uint packed)
{
    return PathTracingDecodeUnitVector(float2(
        f16tof32(packed & 0xffffu),
        f16tof32(packed >> 16u)));
}

float3 PathTracingSurfaceSignature(PathTracingSurface surface)
{
    const float2 encodedNormal =
        PathTracingEncodeUnitVector(surface.geometricNormal);
    return float3(encodedNormal, surface.hitDistance);
}

bool PathTracingSurfaceSignaturesAreCompatible(
    float4 currentSignature,
    float4 previousSignature,
    bool requireCameraRelativeDepth)
{
    if (!(currentSignature.w > 0.0f) ||
        currentSignature.w != previousSignature.w)
    {
        return false;
    }
    const float normalDistance = length(
        currentSignature.xy - previousSignature.xy);
    if (!(normalDistance < 0.12f))
        return false;
    if (!requireCameraRelativeDepth)
        return true;

    const float depthScale = max(
        max(abs(currentSignature.z), abs(previousSignature.z)),
        1.0f);
    return abs(currentSignature.z - previousSignature.z) <
            depthScale * 0.025f;
}

#endif // UVSR_PATH_TRACING_MATERIAL_HLSLI
