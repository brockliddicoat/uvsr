#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace uvsr
{


    [[nodiscard]] inline constexpr bool
    IsRendererReceiverTextureDescriptorSupported(
        const nvrhi::TextureDesc& descriptor) noexcept
    {
        if (descriptor.width == 0u || descriptor.height == 0u ||
            descriptor.depth != 1u || descriptor.arraySize != 1u ||
            descriptor.mipLevels != 1u || descriptor.sampleQuality != 0u ||
            descriptor.sampleCount != 1u)
        {
            return false;
        }

        return descriptor.dimension == nvrhi::TextureDimension::Texture2D;
    }

    [[nodiscard]] inline constexpr bool
    AreRendererReceiverTextureDescriptorsCompatible(
        const nvrhi::TextureDesc& depth,
        const nvrhi::TextureDesc& material,
        const nvrhi::TextureDesc& normals) noexcept
    {
        if (!IsRendererReceiverTextureDescriptorSupported(depth) ||
            !IsRendererReceiverTextureDescriptorSupported(material) ||
            !IsRendererReceiverTextureDescriptorSupported(normals))
        {
            return false;
        }

        return material.width == depth.width &&
            material.height == depth.height &&
            normals.width == depth.width &&
            normals.height == depth.height;
    }
}
