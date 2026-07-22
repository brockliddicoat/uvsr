#pragma once

#include <array>
#include <cstdint>

namespace nvrhi
{
    class ITexture;
}

namespace donut::engine
{
    class Light;
}

namespace uvsr
{
    // A frame-local, non-owning visibility factor for one exact light.
    // Producers expose only this common R8_UNORM contract; they never need to
    // know which other visibility producers are active. Deferred lighting
    // multiplies every complete factor whose light pointer is identical to the
    // light being evaluated. Incomplete or unmatched factors are neutral white.
    struct DirectionalLightVisibility
    {
        nvrhi::ITexture* texture = nullptr;
        const donut::engine::Light* light = nullptr;

        [[nodiscard]] constexpr bool IsComplete() const
        {
            return texture != nullptr && light != nullptr;
        }
    };

    // UVSR currently has two independent directional visibility producers.
    // Keeping the fixed ordinary-SRV interface at exactly that capacity avoids
    // bindless requirements and an extra full-screen composition dispatch.
    constexpr uint32_t DirectionalLightVisibilityCount = 2u;
    using DirectionalLightVisibilitySet = std::array<
        DirectionalLightVisibility,
        DirectionalLightVisibilityCount>;

    // Renderer adapters translate their native texture descriptors into this
    // portable contract before a producer result is accepted. Every rejected
    // texture is equivalent to a missing white factor.
    struct DirectionalLightVisibilityTextureProperties
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t depth = 0u;
        uint32_t arraySize = 0u;
        uint32_t mipLevels = 0u;
        uint32_t sampleCount = 0u;
        bool r8Unorm = false;
        bool texture2D = false;
        bool shaderResource = false;
    };

    [[nodiscard]] constexpr bool
        IsDirectionalLightVisibilityTextureCompatible(
            const DirectionalLightVisibilityTextureProperties& properties,
            uint32_t outputWidth,
            uint32_t outputHeight)
    {
        return properties.r8Unorm &&
            properties.texture2D &&
            properties.width == outputWidth &&
            properties.height == outputHeight &&
            properties.depth == 1u &&
            properties.arraySize == 1u &&
            properties.mipLevels == 1u &&
            properties.sampleCount == 1u &&
            properties.shaderResource;
    }

    [[nodiscard]] constexpr bool TargetsDirectionalLight(
        const DirectionalLightVisibility& visibility,
        const donut::engine::Light* light)
    {
        return visibility.IsComplete() && visibility.light == light;
    }

    [[nodiscard]] constexpr float ClampDirectionalLightVisibility(
        float visibility)
    {
        return visibility < 0.f
            ? 0.f
            : (visibility > 1.f ? 1.f : visibility);
    }

    [[nodiscard]] constexpr float ComposeDirectionalLightVisibility(
        float accumulatedVisibility,
        float producerVisibility,
        bool pointerIdenticalLight)
    {
        const float accumulated = ClampDirectionalLightVisibility(
            accumulatedVisibility);
        return pointerIdenticalLight
            ? accumulated * ClampDirectionalLightVisibility(
                producerVisibility)
            : accumulated;
    }
}
