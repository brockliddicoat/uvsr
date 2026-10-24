#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace uvsr
{
    // borrowed scene resources published together for one renderer frame.
    // generation changes invalidate bindings. content revision also changes
    // after in place acceleration structure updates and invalidates history.
    struct RaySceneView
    {
        nvrhi::rt::IAccelStruct* tlas = nullptr;
        nvrhi::IBuffer* geometryBuffer = nullptr;
        nvrhi::IBuffer* materialBuffer = nullptr;
        nvrhi::IBuffer* geometryIndexMap = nullptr;
        nvrhi::IDescriptorTable* descriptorTable = nullptr;
        std::uint64_t generation = 0u;
        std::uint64_t contentRevision = 0u;
        // source identity is independent of acceleration rebuild generations.
        std::uint64_t sceneGeneration = 0u;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return tlas && geometryBuffer && materialBuffer &&
                geometryIndexMap && descriptorTable && generation != 0u && sceneGeneration != 0u;
        }

        [[nodiscard]] bool HasSameBindings(
            const RaySceneView& other) const noexcept
        {
            return generation == other.generation &&
                sceneGeneration == other.sceneGeneration &&
                tlas == other.tlas &&
                geometryBuffer == other.geometryBuffer &&
                materialBuffer == other.materialBuffer &&
                geometryIndexMap == other.geometryIndexMap &&
                descriptorTable == other.descriptorTable;
        }
    };
}
