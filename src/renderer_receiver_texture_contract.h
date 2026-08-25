#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace uvsr
{
    [[nodiscard]] inline constexpr bool
    IsRendererReceiverSampleCountSupported(uint32_t sampleCount) noexcept
    {
        return sampleCount == 1u || sampleCount == 2u ||
            sampleCount == 4u || sampleCount == 8u ||
            sampleCount == 16u;
    }

    [[nodiscard]] inline constexpr bool
    IsRendererReceiverTextureDescriptorSupported(
        const nvrhi::TextureDesc& descriptor) noexcept
    {
        if (descriptor.width == 0u || descriptor.height == 0u ||
            descriptor.depth != 1u || descriptor.arraySize != 1u ||
            descriptor.mipLevels != 1u ||
            !IsRendererReceiverSampleCountSupported(
                descriptor.sampleCount))
        {
            return false;
        }

        return descriptor.sampleCount == 1u
            ? descriptor.dimension == nvrhi::TextureDimension::Texture2D
            : descriptor.dimension == nvrhi::TextureDimension::Texture2DMS;
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
            material.sampleCount == depth.sampleCount &&
            material.sampleQuality == depth.sampleQuality &&
            normals.width == depth.width &&
            normals.height == depth.height &&
            normals.sampleCount == depth.sampleCount &&
            normals.sampleQuality == depth.sampleQuality;
    }
}
