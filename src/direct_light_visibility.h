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
    [[nodiscard]] constexpr bool IsDirectLightReceiverSampleCountSupported(
        uint32_t sampleCount)
    {
        return sampleCount == 1u || sampleCount == 2u ||
            sampleCount == 4u || sampleCount == 8u ||
            sampleCount == 16u;
    }

    // A frame-local, non-owning direct-light modulation for one exact light.
    // R8 visibility is Texture2D at 1x and Texture2DArray at MSAA, where array
    // slice N belongs to raster sample N. Incomplete or unmatched values are
    // neutral.
    struct DirectLightVisibility
    {
        nvrhi::ITexture* texture = nullptr;
        const donut::engine::Light* light = nullptr;
        uint32_t receiverSampleCount = 1u;
        // Optional single-surface raw/denoised pair. MSAA consumers remap
        // each raw receiver sample monotonically through this correction,
        // preserving per-sample endpoints instead of broadcasting one value.
        nvrhi::ITexture* rawClosestTexture = nullptr;
        nvrhi::ITexture* denoisedClosestTexture = nullptr;

        [[nodiscard]] constexpr bool IsComplete() const
        {
            return texture != nullptr && light != nullptr &&
                IsDirectLightReceiverSampleCountSupported(
                    receiverSampleCount);
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
        bool texture2D = false;
        bool texture2DArray = false;
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
        uint32_t receiverSampleCount = 1u)
    {
        const bool topologyCompatible = receiverSampleCount == 1u
            ? properties.texture2D && properties.arraySize == 1u
            : properties.texture2DArray &&
                properties.arraySize == receiverSampleCount;
        return IsDirectLightReceiverSampleCountSupported(
                receiverSampleCount) &&
            properties.r8Unorm && topologyCompatible &&
            properties.width == outputWidth &&
            properties.height == outputHeight &&
            properties.depth == 1u &&
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

    // Monotonic endpoint-preserving correction used after a single-surface
    // denoiser. It maps the raw closest value to its denoised value while
    // retaining every receiver sample's position below or above that pivot.
    [[nodiscard]] constexpr float ApplyClosestVisibilityCorrection(
        float receiverSample,
        float rawClosest,
        float denoisedClosest)
    {
        const float sample = ClampDirectLightVisibility(receiverSample);
        const float raw = ClampDirectLightVisibility(rawClosest);
        const float denoised = ClampDirectLightVisibility(denoisedClosest);
        if (raw == denoised)
            return sample;
        if (sample >= raw)
        {
            const float range = 1.f - raw > 0.000001f
                ? 1.f - raw
                : 0.000001f;
            return ClampDirectLightVisibility(
                denoised + (sample - raw) * (1.f - denoised) / range);
        }
        const float range = raw > 0.000001f ? raw : 0.000001f;
        return ClampDirectLightVisibility(
            denoised - (raw - sample) * denoised / range);
    }
}
