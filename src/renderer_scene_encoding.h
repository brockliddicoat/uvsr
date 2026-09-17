#pragma once

#include "renderer_scene.h"
#include "renderer_gpu_contract.h"

namespace uvsr
{
    struct RendererSceneBufferDescriptors
    {
        int32_t index = -1;
        int32_t vertex = -1;
    };

    // descriptor slots are resolved by the private backend. no native handles
    // or texture payloads enter these synchronous, nonallocating serializers.
    [[nodiscard]] RendererSceneResult EncodeRendererSceneMaterial(
        const RendererSceneMaterialValues& values, int32_t pickingId,
        ArrayView<const int32_t> textureDescriptors, uint32_t descriptorCapacity,
        MaterialConstants& output) noexcept;
    [[nodiscard]] RendererSceneResult EncodeRendererSceneGeometry(
        const RendererSceneView& scene, uint32_t meshIndex, uint32_t geometryIndex,
        RendererSceneBufferDescriptors descriptors, uint32_t descriptorCapacity,
        GeometryData& output) noexcept;
    [[nodiscard]] RendererSceneResult EncodeRendererSceneAffine(
        const RendererSceneAffine& source, gpu_contract::Float3x4& output) noexcept;
    // the caller supplies the exclusive geometry-count prefix in instance order.
    // failure preserves output; the checked sum must fit the shader's uint32 ABI.
    [[nodiscard]] RendererSceneResult EncodeRendererSceneInstance(
        const RendererSceneView& scene, uint32_t instanceIndex, uint32_t firstGeometryInstanceIndex,
        InstanceData& output) noexcept;
    // UVSR uses explicit ray-visibility textures. no scene-owned native shadow
    // map/channel exists; unused ABI lanes keep their initialized native values.
    [[nodiscard]] RendererSceneResult EncodeRendererSceneLight(
        const RendererSceneView& scene, RendererSceneHandle light, LightConstants& output) noexcept;
}
