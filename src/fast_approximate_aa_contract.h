#pragma once

#include <nvrhi/nvrhi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace uvsr
{
    inline constexpr nvrhi::Format FastApproximateAaColorFormat =
        nvrhi::Format::RGBA16_FLOAT;

    [[nodiscard]] inline bool IsFastApproximateAaSourceCompatible(
        const nvrhi::TextureDesc& description,
        uint32_t requiredWidth,
        uint32_t requiredHeight,
        bool distinctFromOutput) noexcept
    {
        return distinctFromOutput &&
            requiredWidth > 0u &&
            requiredHeight > 0u &&
            description.width == requiredWidth &&
            description.height == requiredHeight &&
            description.sampleCount == 1u &&
            description.dimension == nvrhi::TextureDimension::Texture2D &&
            description.format == FastApproximateAaColorFormat;
    }

    struct FastApproximateAaViewContract
    {
        uint32_t planarViewCount = 0u;
        bool viewAvailable = false;
        uint32_t baseMipLevel = 0u;
        uint32_t mipLevelCount = 0u;
        uint32_t baseArraySlice = 0u;
        uint32_t arraySliceCount = 0u;
        int32_t minX = 0;
        int32_t minY = 0;
        int32_t maxX = 0;
        int32_t maxY = 0;
    };

    [[nodiscard]] inline constexpr bool IsFastApproximateAaFullImageView(
        const FastApproximateAaViewContract& view,
        uint32_t width,
        uint32_t height) noexcept
    {
        return view.planarViewCount == 1u &&
            view.viewAvailable &&
            view.baseMipLevel == 0u &&
            view.mipLevelCount == 1u &&
            view.baseArraySlice == 0u &&
            view.arraySliceCount == 1u &&
            view.minX == 0 &&
            view.minY == 0 &&
            view.maxX == static_cast<int32_t>(width) &&
            view.maxY == static_cast<int32_t>(height);
    }

    [[nodiscard]] inline float GetFastApproximateAaPerceptualLuma(
        float red,
        float green,
        float blue) noexcept
    {
        const float saturatedRed = std::clamp(red, 0.f, 1.f);
        const float saturatedGreen = std::clamp(green, 0.f, 1.f);
        const float saturatedBlue = std::clamp(blue, 0.f, 1.f);
        return std::sqrt(
            saturatedRed * 0.2126f +
            saturatedGreen * 0.7152f +
            saturatedBlue * 0.0722f);
    }
}
