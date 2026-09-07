#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace uvsr
{
    // Borrowed scene resources published together for one renderer frame.
    // Generation changes invalidate bindings. Content revision also changes
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

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return tlas && geometryBuffer && materialBuffer &&
                geometryIndexMap && descriptorTable && generation != 0u;
        }

        [[nodiscard]] bool HasSameBindings(
            const RaySceneView& other) const noexcept
        {
            return generation == other.generation &&
                tlas == other.tlas &&
                geometryBuffer == other.geometryBuffer &&
                materialBuffer == other.materialBuffer &&
                geometryIndexMap == other.geometryIndexMap &&
                descriptorTable == other.descriptorTable;
        }
    };
}
