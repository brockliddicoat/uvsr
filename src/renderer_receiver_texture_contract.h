#pragma once

#include <stdint.h>

namespace uvsr
{
    struct RendererReceiverTextureProperties
    {
        uint32_t width = 0, height = 0, depth = 0, arraySize = 0, mipLevels = 0;
        uint32_t sampleQuality = 0, sampleCount = 0;
        bool texture2D = false;
    };

    [[nodiscard]] inline constexpr bool IsRendererReceiverTextureDescriptorSupported(
        const RendererReceiverTextureProperties& descriptor) noexcept
    {
        return descriptor.width != 0 && descriptor.height != 0 && descriptor.depth == 1 &&
            descriptor.arraySize == 1 && descriptor.mipLevels == 1 && descriptor.sampleQuality == 0 &&
            descriptor.sampleCount == 1 && descriptor.texture2D;
    }

    [[nodiscard]] inline constexpr bool AreRendererReceiverTextureDescriptorsCompatible(
        const RendererReceiverTextureProperties& depth, const RendererReceiverTextureProperties& material,
        const RendererReceiverTextureProperties& normals) noexcept
    {
        return IsRendererReceiverTextureDescriptorSupported(depth) &&
            IsRendererReceiverTextureDescriptorSupported(material) &&
            IsRendererReceiverTextureDescriptorSupported(normals) &&
            material.width == depth.width && material.height == depth.height &&
            normals.width == depth.width && normals.height == depth.height;
    }
}
