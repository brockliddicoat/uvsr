/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "renderer_scene_encoding.h"
#include "renderer_scene_light.h"
#include <math.h>
#include <float.h>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error scene encoding requires exception-disabled compilation
#endif

static_assert(sizeof(MaterialConstants) == 208 && sizeof(GeometryData) == 64);
static_assert(sizeof(InstanceData) == 112 && sizeof(uvsr::gpu_contract::Float3x4) == 48);
static_assert(sizeof(RendererMaterialTableEntry) == 256 && offsetof(RendererMaterialTableEntry, material) == 0);

namespace uvsr
{
    namespace
    {
        bool ValidDescriptor(int32_t slot, uint32_t capacity) noexcept
        { return slot >= -1 && (slot < 0 || uint32_t(slot) < capacity); }

        bool AttributeOffset(const RendererSceneBufferGroup& buffers, RendererSceneVertexAttribute attribute,
            uint64_t firstVertex, uint32_t vertexCount, uint32_t stride, uint32_t& output) noexcept
        {
            const auto range = buffers.attributes[uint32_t(attribute)];
            output = UINT32_MAX;
            if (!range.size) return true;
            if (range.offset > buffers.vertexBytes || range.size > buffers.vertexBytes - range.offset ||
                firstVertex > range.size / stride || vertexCount > range.size / stride - firstVertex)
                return false;
            const uint64_t offset = range.offset + firstVertex * stride;
            if (offset > UINT32_MAX) return false;
            output = uint32_t(offset);
            return true;
        }
    }

    RendererSceneResult EncodeRendererSceneAffine(const RendererSceneAffine& source,
        gpu_contract::Float3x4& output) noexcept
    {
        gpu_contract::Float3x4 encoded{};
        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                const double value = column == 3 ? source.translation[row] : source.linear[column * 3 + row];
                if (!isfinite(value) || value > FLT_MAX || value < -FLT_MAX)
                    return {RendererSceneError::Value};
                encoded.values[row * 4 + column] = float(value);
            }
        }
        output = encoded;
        return {};
    }

    RendererSceneResult EncodeRendererSceneLight(const RendererSceneView& scene,
        RendererSceneHandle handle, LightConstants& output) noexcept
    {
        if (!handle || handle.generation != scene.generation) return {RendererSceneError::Generation};
        const auto* light = FindRendererSceneLight(scene, handle);
        if (!light || light->kind >= RendererSceneLightKind::Count) return {RendererSceneError::Reference, handle.index};
        const auto& values = light->values;
        const float scalars[]{values.color.x, values.color.y, values.color.z, values.irradiance, values.angularSize,
            values.intensity, values.radius, values.range, values.innerAngle, values.outerAngle};
        for (float value : scalars)
            if (!isfinite(value)) return {RendererSceneError::Value, handle.index};
        RendererSceneLightFrame frame;
        const auto result = GetRendererSceneLightFrame(scene, handle, frame);
        if (!result.Succeeded()) return result;
        LightConstants encoded{};
        encoded.color = values.color;
        encoded.outOfBoundsShadow = 1;
        for (uint32_t lane = 0; lane < 4; ++lane)
            encoded.shadowCascades[lane] = encoded.perObjectShadows[lane] = encoded.shadowChannel[lane] = -1;
        constexpr float radiansPerDegree = 3.141592654f / 180.f;
        if (light->kind == RendererSceneLightKind::Directional)
        {
            encoded.lightType = UVSR_LIGHT_TYPE_DIRECTIONAL;
            // native directional encoding normalizes the getter a second time.
            const double length = sqrt(frame.direction[0] * frame.direction[0] +
                frame.direction[1] * frame.direction[1] + frame.direction[2] * frame.direction[2]);
            encoded.direction = {float(frame.direction[0] / length), float(frame.direction[1] / length), float(frame.direction[2] / length)};
            const float angularSize = values.angularSize < 0 ? 0 : values.angularSize > 90 ? 90 : values.angularSize;
            encoded.angularSizeOrInvRange = angularSize * radiansPerDegree;
            encoded.intensity = values.irradiance;
        }
        else
        {
            for (double value : frame.position)
                if (value > FLT_MAX || value < -FLT_MAX) return {RendererSceneError::Value, handle.index};
            encoded.position = {float(frame.position[0]), float(frame.position[1]), float(frame.position[2])};
            encoded.lightType = light->kind == RendererSceneLightKind::Spot ? UVSR_LIGHT_TYPE_SPOT : UVSR_LIGHT_TYPE_POINT;
            encoded.radius = values.radius;
            encoded.angularSizeOrInvRange = values.range <= 0 ? 0 : 1.f / values.range;
            encoded.intensity = values.intensity;
            if (light->kind == RendererSceneLightKind::Spot)
            {
                encoded.direction = {float(frame.direction[0]), float(frame.direction[1]), float(frame.direction[2])};
                encoded.innerAngle = values.innerAngle * radiansPerDegree;
                encoded.outerAngle = values.outerAngle * radiansPerDegree;
            }
        }
        output = encoded;
        return {};
    }

    RendererSceneResult EncodeRendererSceneInstance(const RendererSceneView& scene, uint32_t instanceIndex,
        uint32_t firstGeometryInstanceIndex, InstanceData& output) noexcept
    {
        if (!scene.generation) return {RendererSceneError::Generation};
        if (!scene.instances.IsValid() || !scene.nodes.IsValid() || !scene.meshes.IsValid() ||
            !scene.geometries.IsValid() || instanceIndex >= scene.instances.count)
            return {RendererSceneError::Reference};
        const auto& instance = scene.instances.data[instanceIndex];
        if (instance.meshIndex >= scene.meshes.count || instance.nodeIndex >= scene.nodes.count)
            return {RendererSceneError::Reference};
        const auto& mesh = scene.meshes.data[instance.meshIndex];
        if (mesh.type >= RendererSceneMeshType::Count) return {RendererSceneError::Value};
        if (mesh.geometries.first > scene.geometries.count ||
            mesh.geometries.count > scene.geometries.count - mesh.geometries.first ||
            mesh.geometries.count > UINT32_MAX - firstGeometryInstanceIndex)
            return {RendererSceneError::Range};
        const auto& node = scene.nodes.data[instance.nodeIndex];
        if (node.leafKind != RendererSceneLeafKind::Instance || node.leafIndex != instanceIndex)
            return {RendererSceneError::Reference};
        InstanceData encoded{};
        encoded.firstGeometryInstanceIndex = firstGeometryInstanceIndex;
        encoded.firstGeometryIndex = mesh.geometries.count ? mesh.geometries.first : UINT32_MAX;
        encoded.numGeometries = mesh.geometries.count;
        if (mesh.type == RendererSceneMeshType::CurveDisjointOrthogonalTriangleStrips)
            encoded.flags = InstanceFlags_CurveDisjointOrthogonalTriangleStrips;
        else if (mesh.type == RendererSceneMeshType::CurveLinearSweptSpheres)
            encoded.flags = InstanceFlags_CurveLinearSweptSpheres;
        auto result = EncodeRendererSceneAffine(node.world, encoded.transform);
        if (result.Succeeded()) result = EncodeRendererSceneAffine(node.previousWorld, encoded.prevTransform);
        if (!result.Succeeded()) return result;
        output = encoded;
        return {};
    }

    RendererSceneResult EncodeRendererSceneMaterial(const RendererSceneMaterialValues& values, int32_t pickingId,
        ArrayView<const int32_t> textureDescriptors, uint32_t descriptorCapacity, MaterialConstants& output) noexcept
    {
        if (values.domain >= RendererMaterialDomain::Count) return {RendererSceneError::Value};
        if (!textureDescriptors.IsValid() || textureDescriptors.count > UINT32_MAX) return {RendererSceneError::Capacity};
        MaterialConstants encoded{};
        const bool enabled[]{values.enableBaseOrDiffuseTexture, values.enableMetalRoughOrSpecularTexture,
            values.enableNormalTexture, values.enableEmissiveTexture, values.enableOcclusionTexture,
            values.enableTransmissionTexture, values.enableOpacityTexture};
        constexpr int flags[]{MaterialFlags_UseBaseOrDiffuseTexture, MaterialFlags_UseMetalRoughOrSpecularTexture,
            MaterialFlags_UseNormalTexture, MaterialFlags_UseEmissiveTexture, MaterialFlags_UseOcclusionTexture,
            MaterialFlags_UseTransmissionTexture, MaterialFlags_UseOpacityTexture};
        int* slots[]{&encoded.baseOrDiffuseTextureIndex, &encoded.metalRoughOrSpecularTextureIndex,
            &encoded.normalTextureIndex, &encoded.emissiveTextureIndex, &encoded.occlusionTextureIndex,
            &encoded.transmissionTextureIndex, &encoded.opacityTextureIndex};
        for (uint32_t slot = 0; slot < uint32_t(RendererSceneMaterialTextureSlot::Count); ++slot)
        {
            const uint32_t texture = values.textures[slot];
            *slots[slot] = -1;
            if (texture == InvalidSceneIndex) continue;
            if (texture >= textureDescriptors.count) return {RendererSceneError::Reference, slot};
            const int32_t descriptor = textureDescriptors.data[texture];
            if (!ValidDescriptor(descriptor, descriptorCapacity)) return {RendererSceneError::Reference, slot};
            *slots[slot] = descriptor;
            if (enabled[slot]) encoded.flags |= flags[slot];
        }
        if (values.useSpecularGlossModel) encoded.flags |= MaterialFlags_UseSpecularGlossModel;
        if (values.doubleSided) encoded.flags |= MaterialFlags_DoubleSided;
        if (values.metalnessInRedChannel) encoded.flags |= MaterialFlags_MetalnessInRedChannel;
        encoded.domain = int(values.domain);
        encoded.materialID = pickingId;
        encoded.baseOrDiffuseColor = values.baseOrDiffuseColor;
        encoded.specularColor = values.specularColor;
        encoded.emissiveColor = {values.emissiveColor.x * values.emissiveIntensity,
            values.emissiveColor.y * values.emissiveIntensity, values.emissiveColor.z * values.emissiveIntensity};
        if (!isfinite(encoded.emissiveColor.x) || !isfinite(encoded.emissiveColor.y) || !isfinite(encoded.emissiveColor.z))
            return {RendererSceneError::Value};
        encoded.roughness = values.roughness;
        encoded.metalness = values.metalness;
        encoded.normalTextureScale = values.normalTextureScale;
        encoded.occlusionStrength = values.occlusionStrength;
        encoded.transmissionFactor = values.transmissionFactor;
        encoded.normalTextureTransformScale = values.normalTextureTransformScale;
        encoded.opacity = values.domain == RendererMaterialDomain::AlphaBlended ||
            values.domain == RendererMaterialDomain::TransmissiveAlphaBlended ? values.opacity : 1.f;
        // the retained native switch falls through for blended domains, producing
        // -1 rather than its intermediate zero. preserve that observable policy.
        encoded.alphaCutoff = values.domain == RendererMaterialDomain::AlphaTested ||
            values.domain == RendererMaterialDomain::TransmissiveAlphaTested ? values.alphaCutoff : -1.f;
        if (values.enableSubsurfaceScattering)
        {
            encoded.flags |= MaterialFlags_SubsurfaceScattering;
            encoded.sssTransmissionColor = values.subsurface.transmissionColor;
            encoded.sssScatteringColor = values.subsurface.scatteringColor;
            encoded.sssScale = values.subsurface.scale;
            encoded.sssAnisotropy = values.subsurface.anisotropy;
        }
        if (values.enableHair)
        {
            encoded.flags |= MaterialFlags_Hair;
            encoded.hairBaseColor = values.hair.baseColor;
            encoded.hairMelanin = values.hair.melanin;
            encoded.hairMelaninRedness = values.hair.melaninRedness;
            encoded.hairLongitudinalRoughness = values.hair.longitudinalRoughness;
            encoded.hairAzimuthalRoughness = values.hair.azimuthalRoughness;
            encoded.hairIor = values.hair.ior;
            encoded.hairCuticleAngle = values.hair.cuticleAngle;
            encoded.hairDiffuseReflectionWeight = values.hair.diffuseReflectionWeight;
            encoded.hairDiffuseReflectionTint = values.hair.diffuseReflectionTint;
        }
        output = encoded;
        return {};
    }

    RendererSceneResult EncodeRendererSceneGeometry(const RendererSceneView& scene, uint32_t meshIndex,
        uint32_t geometryIndex, RendererSceneBufferDescriptors descriptors, uint32_t descriptorCapacity,
        GeometryData& output) noexcept
    {
        if (!scene.generation) return {RendererSceneError::Generation};
        if (!scene.meshes.IsValid() || !scene.geometries.IsValid() || !scene.bufferGroups.IsValid() ||
            meshIndex >= scene.meshes.count || geometryIndex >= scene.geometries.count)
            return {RendererSceneError::Reference};
        const auto& mesh = scene.meshes.data[meshIndex];
        if (geometryIndex < mesh.geometries.first || geometryIndex - mesh.geometries.first >= mesh.geometries.count ||
            mesh.bufferGroupIndex >= scene.bufferGroups.count)
            return {RendererSceneError::Reference};
        const auto& geometry = scene.geometries.data[geometryIndex];
        const auto& buffers = scene.bufferGroups.data[mesh.bufferGroupIndex];
        if (geometry.materialIndex >= scene.materials.count || !ValidDescriptor(descriptors.index, descriptorCapacity) ||
            !ValidDescriptor(descriptors.vertex, descriptorCapacity) || (buffers.indexBytes && descriptors.index < 0) ||
            (buffers.vertexBytes && descriptors.vertex < 0))
            return {RendererSceneError::Reference};
        // this shader ABI has 32-bit byte offsets and ByteAddressBuffer dimensions.
        // the canonical scene retains wider capacities for other backend layouts.
        if (buffers.indexBytes > UINT32_MAX || buffers.vertexBytes > UINT32_MAX)
            return {RendererSceneError::Range};
        const uint64_t indexOffset = (uint64_t(mesh.indexOffset) + geometry.indexOffsetInMesh) * sizeof(uint32_t);
        if (indexOffset > buffers.indexBytes || uint64_t(geometry.indexCount) * sizeof(uint32_t) > buffers.indexBytes - indexOffset)
            return {RendererSceneError::Range};
        GeometryData encoded{};
        encoded.numIndices = geometry.indexCount;
        encoded.numVertices = geometry.vertexCount;
        encoded.indexBufferIndex = descriptors.index;
        encoded.indexOffset = uint32_t(indexOffset);
        encoded.vertexBufferIndex = descriptors.vertex;
        encoded.materialIndex = geometry.materialIndex;
        const uint64_t vertex = uint64_t(mesh.vertexOffset) + geometry.vertexOffsetInMesh;
        if (!AttributeOffset(buffers, RendererSceneVertexAttribute::Position, vertex, geometry.vertexCount, 12, encoded.positionOffset) ||
            !AttributeOffset(buffers, RendererSceneVertexAttribute::PreviousPosition, vertex, geometry.vertexCount, 12, encoded.prevPositionOffset) ||
            !AttributeOffset(buffers, RendererSceneVertexAttribute::TexCoord0, vertex, geometry.vertexCount, 8, encoded.texCoord1Offset) ||
            !AttributeOffset(buffers, RendererSceneVertexAttribute::TexCoord1, vertex, geometry.vertexCount, 8, encoded.texCoord2Offset) ||
            !AttributeOffset(buffers, RendererSceneVertexAttribute::Normal, vertex, geometry.vertexCount, 4, encoded.normalOffset) ||
            !AttributeOffset(buffers, RendererSceneVertexAttribute::Tangent, vertex, geometry.vertexCount, 4, encoded.tangentOffset) ||
            !AttributeOffset(buffers, RendererSceneVertexAttribute::CurveRadius, vertex, geometry.vertexCount, 4, encoded.curveRadiusOffset))
            return {RendererSceneError::Range};
        output = encoded;
        return {};
    }
}
