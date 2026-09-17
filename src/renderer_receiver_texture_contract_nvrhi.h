#pragma once

#include "renderer_receiver_texture_contract.h"
#include <nvrhi/nvrhi.h>

namespace uvsr
{


    [[nodiscard]] inline constexpr RendererReceiverTextureProperties RendererReceiverTexturePropertiesNvrhi(
        const nvrhi::TextureDesc& descriptor) noexcept
    {
        return {descriptor.width, descriptor.height, descriptor.depth, descriptor.arraySize, descriptor.mipLevels,
            descriptor.sampleQuality, descriptor.sampleCount, descriptor.dimension == nvrhi::TextureDimension::Texture2D};
    }

    [[nodiscard]] inline constexpr bool IsRendererReceiverTextureDescriptorSupported(
        const nvrhi::TextureDesc& descriptor) noexcept
    {
        return IsRendererReceiverTextureDescriptorSupported(RendererReceiverTexturePropertiesNvrhi(descriptor));
    }

    [[nodiscard]] inline constexpr bool
    AreRendererReceiverTextureDescriptorsCompatible(
        const nvrhi::TextureDesc& depth,
        const nvrhi::TextureDesc& material,
        const nvrhi::TextureDesc& normals) noexcept
    {
        return AreRendererReceiverTextureDescriptorsCompatible(RendererReceiverTexturePropertiesNvrhi(depth),
            RendererReceiverTexturePropertiesNvrhi(material), RendererReceiverTexturePropertiesNvrhi(normals));
    }
}
