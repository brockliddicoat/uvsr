#include "renderer_scene_encoding.h"
#include "renderer_scene_encoding_fixture.h"

#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error scene encoding tests require exception-disabled compilation
#endif

namespace
{
    using namespace uvsr;

    void Require(bool condition, const char* reason)
    {
        if (condition) return;
        fprintf(stderr, "scene encoding check failed: %s\n", reason); exit(1);
    }

    void CheckMaterialEncoding()
    {
        RendererSceneMaterialValues values;
        auto& material = values;
        for (auto& texture : material.textures) texture = 0;
        material.roughness = 0.3f;
        material.normalTextureTransformScale = {2, 3};
        material.subsurface.scale = 0.7f;
        material.hair.cuticleAngle = 0.6f;
        material.baseOrDiffuseColor = {0.25f, 0.5f, 0.75f};
        material.specularColor = {0.1f, 0.2f, 0.3f};
        material.emissiveColor = {0.3f, 0.7f, 0.9f}; material.emissiveIntensity = 2.5f;
        material.opacity = 0.35f; material.alphaCutoff = 0.65f; material.metalness = 0.2f;
        material.normalTextureScale = 0.7f; material.occlusionStrength = 0.8f; material.transmissionFactor = 0.3f;
        material.subsurface.transmissionColor = {0.1f, 0.3f, 0.7f};
        material.subsurface.scatteringColor = {0.4f, 0.8f, 0.6f}; material.subsurface.anisotropy = 0.2f;
        material.hair.baseColor = {0.2f, 0.4f, 0.6f}; material.hair.melanin = 0.1f;
        material.hair.melaninRedness = 0.8f; material.hair.longitudinalRoughness = 0.35f;
        material.hair.azimuthalRoughness = 0.2f; material.hair.diffuseReflectionWeight = 0.7f;
        material.hair.diffuseReflectionTint = {0.3f, 0.6f, 0.9f}; material.hair.ior = 1.7f;
        int32_t descriptors[2]{-1, -1};
        uint32_t comparisons = 0;
        for (uint32_t domain = 0; domain < uint32_t(RendererMaterialDomain::Count); ++domain)
            for (uint32_t mask = 0; mask < 128; ++mask)
                for (uint32_t extra = 0; extra < 4; ++extra)
                {
                    material.domain = RendererMaterialDomain(domain);
                    material.enableBaseOrDiffuseTexture = (mask & 1) != 0;
                    material.enableMetalRoughOrSpecularTexture = (mask & 2) != 0;
                    material.enableNormalTexture = (mask & 4) != 0;
                    material.enableEmissiveTexture = (mask & 8) != 0;
                    material.enableOcclusionTexture = (mask & 16) != 0;
                    material.enableTransmissionTexture = (mask & 32) != 0;
                    material.enableOpacityTexture = (mask & 64) != 0;
                    material.enableSubsurfaceScattering = (extra & 1) != 0;
                    material.enableHair = (extra & 2) != 0;
                    material.useSpecularGlossModel = (mask & 1) != 0;
                    material.doubleSided = (mask & 2) != 0;
                    material.metalnessInRedChannel = (mask & 4) != 0;
                    MaterialConstants actual{}, expected{};
                    ReadMaterialEncodingReference(domain, mask, extra, expected);
                    Require(EncodeRendererSceneMaterial(values, 9876, {descriptors, 2}, 0, actual).Succeeded() &&
                        memcmp(&actual, &expected, sizeof(actual)) == 0, "material bytes match the captured serializer");
                    ++comparisons;
                }
        descriptors[0] = 7; descriptors[1] = 3;
        MaterialConstants encoded{};
        Require(EncodeRendererSceneMaterial(values, 65535, {descriptors, 2}, 8, encoded).Succeeded() &&
            encoded.materialID == 65535 && encoded.baseOrDiffuseTextureIndex == 7 && encoded.opacityTextureIndex == 7 &&
            encoded.normalTextureIndex == 7 && encoded.emissiveTextureIndex == 7,
            "canonical texture IDs resolve descriptors and retain full-width picking identity");
        const auto saved = encoded;
        const auto rejects = [&](RendererSceneError error)
        {
            Require(EncodeRendererSceneMaterial(values, 0, {descriptors, 2}, 8, encoded).error == error &&
                memcmp(&encoded, &saved, sizeof(encoded)) == 0, "material failure preserves output");
        };
        descriptors[0] = 8; rejects(RendererSceneError::Reference);
        descriptors[0] = -2; rejects(RendererSceneError::Reference);
        descriptors[0] = 7;
        values.textures[0] = 2; rejects(RendererSceneError::Reference); values.textures[0] = 0;
        values.domain = RendererMaterialDomain::Count; rejects(RendererSceneError::Value);
        values.domain = RendererMaterialDomain::Opaque;
        values.emissiveIntensity = HUGE_VALF; rejects(RendererSceneError::Value);
        values.emissiveIntensity = 1;
        values.textures[0] = InvalidSceneIndex;
        Require(EncodeRendererSceneMaterial(values, 0, {descriptors, 2}, 8, encoded).Succeeded() &&
            encoded.baseOrDiffuseTextureIndex == -1 && !(encoded.flags & MaterialFlags_UseBaseOrDiffuseTexture),
            "absent texture clears the use flag even when enabled");
        printf("material encoding: %u captured byte comparisons, descriptor and atomic rejection cases passed\n", comparisons);
    }

    void CheckGeometryEncoding()
    {
        RendererSceneMesh mesh;
        mesh.bufferGroupIndex = 0; mesh.geometries = {0, 1}; mesh.indexOffset = 5; mesh.vertexOffset = 2;
        RendererSceneGeometry geometry;
        geometry.materialIndex = 1; geometry.indexOffsetInMesh = 3; geometry.vertexOffsetInMesh = 1;
        geometry.indexCount = 6; geometry.vertexCount = 3;
        RendererSceneBufferGroup buffers;
        buffers.indexBytes = 64; buffers.vertexBytes = 640;
        auto& attributes = buffers.attributes;
        attributes[uint32_t(RendererSceneVertexAttribute::Position)] = {16, 240};
        attributes[uint32_t(RendererSceneVertexAttribute::PreviousPosition)] = {256, 144};
        attributes[uint32_t(RendererSceneVertexAttribute::TexCoord0)] = {400, 96};
        attributes[uint32_t(RendererSceneVertexAttribute::Normal)] = {512, 96};
        attributes[uint32_t(RendererSceneVertexAttribute::Tangent)] = {608, 32};
        RendererSceneMaterial materials[2]{};
        RendererSceneView view;
        view.generation = 1; view.meshes = {&mesh, 1}; view.geometries = {&geometry, 1};
        view.bufferGroups = {&buffers, 1}; view.materials = {materials, 2};
        GeometryData encoded{};
        Require(EncodeRendererSceneGeometry(view, 0, 0, {2, 5}, 8, encoded).Succeeded() &&
            encoded.numIndices == 6 && encoded.numVertices == 3 && encoded.indexBufferIndex == 2 &&
            encoded.vertexBufferIndex == 5 && encoded.indexOffset == 32 && encoded.positionOffset == 52 &&
            encoded.prevPositionOffset == 292 && encoded.texCoord1Offset == 424 && encoded.texCoord2Offset == UINT32_MAX &&
            encoded.normalOffset == 524 && encoded.tangentOffset == 620 && encoded.curveRadiusOffset == UINT32_MAX &&
            encoded.materialIndex == 1, "geometry element/byte offsets and canonical material index match fixed layout");
        const auto saved = encoded;
        const auto rejects = [&](RendererSceneError error)
        {
            Require(EncodeRendererSceneGeometry(view, 0, 0, {2, 5}, 8, encoded).error == error &&
                memcmp(&encoded, &saved, sizeof(encoded)) == 0, "geometry failure preserves output");
        };
        buffers.indexBytes = 55; rejects(RendererSceneError::Range); buffers.indexBytes = 64;
        buffers.vertexBytes = uint64_t(UINT32_MAX) + 1; rejects(RendererSceneError::Range); buffers.vertexBytes = 640;
        mesh.vertexOffset = UINT32_MAX; rejects(RendererSceneError::Range); mesh.vertexOffset = 2;
        attributes[uint32_t(RendererSceneVertexAttribute::TexCoord0)].size = 40;
        rejects(RendererSceneError::Range); attributes[uint32_t(RendererSceneVertexAttribute::TexCoord0)].size = 96;
        geometry.materialIndex = 2; rejects(RendererSceneError::Reference); geometry.materialIndex = 1;
        mesh.geometries.first = 1; rejects(RendererSceneError::Reference); mesh.geometries.first = 0;
        view.generation = 0; rejects(RendererSceneError::Generation); view.generation = 1;
        Require(EncodeRendererSceneGeometry(view, 0, 0, {-1, 5}, 8, encoded).error == RendererSceneError::Reference &&
            EncodeRendererSceneGeometry(view, 0, 0, {2, 8}, 8, encoded).error == RendererSceneError::Reference &&
            memcmp(&encoded, &saved, sizeof(encoded)) == 0, "geometry descriptor failure preserves output");
    }

    void CheckInstanceEncoding()
    {
        RendererSceneNode node;
        node.leafKind = RendererSceneLeafKind::Instance;
        node.leafIndex = 0;
        node.world = {{1, 2, 3, 4, 5, 6, 7, 8, 9}, {10, 11, 12}};
        node.previousWorld = {{9, 8, 7, 6, 5, 4, 3, 2, 1}, {-1, -2, -3}};
        RendererSceneInstance instance;
        instance.nodeIndex = instance.meshIndex = 0;
        RendererSceneMesh mesh;
        mesh.geometries = {1, 2};
        RendererSceneGeometry geometries[3];
        RendererSceneView view;
        view.generation = 7;
        view.nodes = {&node, 1}; view.instances = {&instance, 1};
        view.meshes = {&mesh, 1}; view.geometries = {geometries, 3};
        const float current[]{1, 4, 7, 10, 2, 5, 8, 11, 3, 6, 9, 12};
        const float previous[]{9, 6, 3, -1, 8, 5, 2, -2, 7, 4, 1, -3};
        InstanceData encoded;
        for (uint32_t type = 0; type < uint32_t(RendererSceneMeshType::Count); ++type)
        {
            mesh.type = RendererSceneMeshType(type);
            const uint32_t flags = type == 2 ? InstanceFlags_CurveDisjointOrthogonalTriangleStrips :
                type == 3 ? InstanceFlags_CurveLinearSweptSpheres : 0;
            Require(EncodeRendererSceneInstance(view, 0, 17, encoded).Succeeded() &&
                encoded.flags == flags && encoded.firstGeometryIndex == 1 && encoded.firstGeometryInstanceIndex == 17 &&
                encoded.numGeometries == 2 && memcmp(&encoded.transform, current, sizeof(current)) == 0 &&
                memcmp(&encoded.prevTransform, previous, sizeof(previous)) == 0, "all instance flags, prefixes and transform packing");
        }
        const auto saved = encoded;
        const auto rejects = [&](RendererSceneError error, uint32_t index = 0, uint32_t prefix = 17)
        {
            Require(EncodeRendererSceneInstance(view, index, prefix, encoded).error == error &&
                memcmp(&encoded, &saved, sizeof(encoded)) == 0, "instance encoding failure preserves output");
        };
        rejects(RendererSceneError::Reference, 1);
        rejects(RendererSceneError::Range, 0, UINT32_MAX);
        instance.nodeIndex = 1; rejects(RendererSceneError::Reference); instance.nodeIndex = 0;
        instance.meshIndex = 1; rejects(RendererSceneError::Reference); instance.meshIndex = 0;
        node.leafIndex = 1; rejects(RendererSceneError::Reference); node.leafIndex = 0;
        mesh.geometries.first = 2; rejects(RendererSceneError::Range); mesh.geometries.first = 1;
        mesh.type = RendererSceneMeshType::Count; rejects(RendererSceneError::Value); mesh.type = RendererSceneMeshType::Triangles;
        node.world.linear[0] = NAN; rejects(RendererSceneError::Value); node.world.linear[0] = 1;
        node.previousWorld.translation[2] = double(FLT_MAX) * 2; rejects(RendererSceneError::Value); node.previousWorld.translation[2] = -3;
        view.instances.data = nullptr; rejects(RendererSceneError::Reference); view.instances.data = &instance;
        view.generation = 0; rejects(RendererSceneError::Generation); view.generation = 7;
        mesh.geometries = {0, 0};
        Require(EncodeRendererSceneInstance(view, 0, UINT32_MAX, encoded).Succeeded() &&
            encoded.numGeometries == 0 && encoded.firstGeometryIndex == UINT32_MAX,
            "empty mesh preserves the no-geometry sentinel without overflowing the prefix");
    }

 }

void CheckRendererSceneLights();

int main()
{
    CheckMaterialEncoding();
    CheckGeometryEncoding();
    CheckInstanceEncoding();
    CheckRendererSceneLights();
    FinishSceneEncodingReference();
    return 0;
}
