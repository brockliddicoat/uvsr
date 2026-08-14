#pragma once

#include <nvrhi/nvrhi.h>

namespace uvsr
{
    struct RayTracedMaterialVisibilityInputs
    {
        nvrhi::IBuffer* geometryBuffer = nullptr;
        nvrhi::IBuffer* materialBuffer = nullptr;
        nvrhi::IBuffer* geometryIndexMap = nullptr;
        // Optional for visibility-only consumers. Path tracing uses the
        // current/previous instance transforms to produce ray-traced motion.
        nvrhi::IBuffer* instanceBuffer = nullptr;
        nvrhi::IDescriptorTable* descriptorTable = nullptr;

        [[nodiscard]] explicit operator bool() const
        {
            return geometryBuffer && materialBuffer && geometryIndexMap &&
                descriptorTable;
        }

        [[nodiscard]] bool operator==(
            const RayTracedMaterialVisibilityInputs& other) const
        {
            return geometryBuffer == other.geometryBuffer &&
                materialBuffer == other.materialBuffer &&
                geometryIndexMap == other.geometryIndexMap &&
                instanceBuffer == other.instanceBuffer &&
                descriptorTable == other.descriptorTable;
        }

        [[nodiscard]] bool operator!=(
            const RayTracedMaterialVisibilityInputs& other) const
        {
            return !(*this == other);
        }
    };
}
