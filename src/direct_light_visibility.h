#pragma once

#include "renderer_scene.h"

namespace uvsr
{
    struct DirectLightVisibilityTextureProperties
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

    [[nodiscard]] constexpr bool IsDirectLightVisibilityTextureCompatible(
        const DirectLightVisibilityTextureProperties& properties,
        uint32_t outputWidth,
        uint32_t outputHeight)
    {
        return properties.r8Unorm && properties.texture2D && properties.arraySize == 1u &&
            properties.width == outputWidth &&
            properties.height == outputHeight &&
            properties.depth == 1u &&
            properties.mipLevels == 1u &&
            properties.sampleCount == 1u &&
            properties.shaderResource;
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
        bool sameLight)
    {
        const float accumulated = ClampDirectLightVisibility(
            accumulatedVisibility);
        return sameLight
            ? (accumulated < ClampDirectLightVisibility(producerVisibility)
                ? accumulated
                : ClampDirectLightVisibility(producerVisibility))
            : accumulated;
    }


}
