#pragma once

#include <nvrhi/nvrhi.h>

namespace uvsr
{
    struct RayTracedMaterialVisibilityInputs
    {
        nvrhi::IBuffer* geometryBuffer = nullptr;
        nvrhi::IBuffer* materialBuffer = nullptr;
        nvrhi::IBuffer* geometryIndexMap = nullptr;
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
                descriptorTable == other.descriptorTable;
        }

        [[nodiscard]] bool operator!=(
            const RayTracedMaterialVisibilityInputs& other) const
        {
            return !(*this == other);
        }
    };
}
