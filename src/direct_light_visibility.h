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
    enum class DirectLightVisibilityEncoding : uint32_t
    {
        ScalarR8Unorm,
        RgbRgba16Float
    };

    // A frame-local, non-owning direct-light modulation for one exact light.
    // Scalar producers expose R8 visibility. Correlated ratio estimators expose
    // RGB floating-point modulation because material response can vary by
    // channel across an emitter. Incomplete or unmatched values are neutral.
    struct DirectLightVisibility
    {
        nvrhi::ITexture* texture = nullptr;
        const donut::engine::Light* light = nullptr;
        DirectLightVisibilityEncoding encoding =
            DirectLightVisibilityEncoding::ScalarR8Unorm;

        [[nodiscard]] constexpr bool IsComplete() const
        {
            return texture != nullptr && light != nullptr;
        }
    };

    struct DirectLightVisibilityTextureProperties
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

    // Slot zero belongs to the finite flashlight producer. Slot one belongs
    // to the primary directional sun. Either slot may be absent.
    struct DirectLightVisibilities
    {
        DirectLightVisibility flashlight;
        DirectLightVisibility sun;
    };

    [[nodiscard]] constexpr bool IsDirectLightVisibilityTextureCompatible(
        const DirectLightVisibilityTextureProperties& properties,
        uint32_t outputWidth,
        uint32_t outputHeight,
        DirectLightVisibilityEncoding encoding =
            DirectLightVisibilityEncoding::ScalarR8Unorm)
    {
        const bool formatCompatible =
            encoding == DirectLightVisibilityEncoding::ScalarR8Unorm
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

    [[nodiscard]] constexpr bool TargetsDirectLight(
        const DirectLightVisibility& visibility,
        const donut::engine::Light* light)
    {
        return visibility.IsComplete() && visibility.light == light;
    }

    [[nodiscard]] constexpr float ClampDirectLightVisibility(float visibility)
    {
        return visibility < 0.f
            ? 0.f
            : (visibility > 1.f ? 1.f : visibility);
    }

    [[nodiscard]] constexpr float ComposeDirectLightVisibility(
        float accumulatedVisibility,
        float producerVisibility,
        bool pointerIdenticalLight)
    {
        const float accumulated = ClampDirectLightVisibility(
            accumulatedVisibility);
        return pointerIdenticalLight
            ? (accumulated < ClampDirectLightVisibility(producerVisibility)
                ? accumulated
                : ClampDirectLightVisibility(producerVisibility))
            : accumulated;
    }
}
