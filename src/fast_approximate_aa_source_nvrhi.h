#pragma once

#include "fast_approximate_aa_contract.h"
#include <nvrhi/nvrhi.h>

namespace uvsr
{
    inline constexpr nvrhi::Format FastApproximateAaColorFormat = nvrhi::Format::RGBA16_FLOAT;

    [[nodiscard]] inline bool IsFastApproximateAaSourceCompatible(
        const nvrhi::TextureDesc& description, uint32_t width, uint32_t height, bool distinct) noexcept
    {
        const FastApproximateAaSourceProperties properties{description.width, description.height,
            description.sampleCount, description.dimension == nvrhi::TextureDimension::Texture2D,
            description.format == FastApproximateAaColorFormat};
        return IsFastApproximateAaSourceCompatible(properties, width, height, distinct);
    }
}
