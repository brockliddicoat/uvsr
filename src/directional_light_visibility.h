#pragma once

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
    enum class DirectionalLightVisibilityEncoding : uint32_t
    {
        ScalarR8Unorm,
        RgbRgba16Float
    };

    // A frame-local, non-owning direct-light modulation for one exact light.
    // Screen-space producers expose scalar R8 visibility. Correlated ratio
    // estimators expose RGB floating-point modulation because material response
    // can vary by channel across the emitter. Incomplete or unmatched values
    // are neutral white.
    struct DirectionalLightVisibility
    {
        nvrhi::ITexture* texture = nullptr;
        const donut::engine::Light* light = nullptr;
        DirectionalLightVisibilityEncoding encoding =
            DirectionalLightVisibilityEncoding::ScalarR8Unorm;

        [[nodiscard]] constexpr bool IsComplete() const
        {
            return texture != nullptr && light != nullptr;
        }
    };

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
        bool rgba16Float = false;
        bool texture2D = false;
        bool shaderResource = false;
    };

    // UVSR currently has two independent directional-shadow producers. Named
    // slots keep their ownership explicit while allowing both to be absent,
    // either one to be active, or both to be combined by the lighting pass.
    struct DirectionalLightVisibilities
    {
        DirectionalLightVisibility screenSpace;
        DirectionalLightVisibility ratioEstimator;
    };

    [[nodiscard]] constexpr bool
        IsDirectionalLightVisibilityTextureCompatible(
            const DirectionalLightVisibilityTextureProperties& properties,
            uint32_t outputWidth,
            uint32_t outputHeight,
            DirectionalLightVisibilityEncoding encoding =
                DirectionalLightVisibilityEncoding::ScalarR8Unorm)
    {
        const bool formatCompatible =
            encoding ==
                DirectionalLightVisibilityEncoding::ScalarR8Unorm
                ? properties.r8Unorm
                : properties.rgba16Float;
        return formatCompatible &&
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
            ? (accumulated < ClampDirectionalLightVisibility(
                    producerVisibility)
                ? accumulated
                : ClampDirectionalLightVisibility(producerVisibility))
            : accumulated;
    }
}
