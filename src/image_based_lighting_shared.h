#ifndef UVSR_IMAGE_BASED_LIGHTING_SHARED_H
#define UVSR_IMAGE_BASED_LIGHTING_SHARED_H

#ifdef __cplusplus

// This file is consumed by direct DXC as well as C++. Keep standard-library
// includes in C++ translation units because DXC scans shared includes
// before language-condition evaluation.
namespace uvsr
{
    struct ImageBasedLightingScales
    {
        float radiance = 0.f;
        float diffuse = 0.f;
        float specular = 0.f;
    };

    // Exposure belongs to the selected radiance field and therefore remains
    // shared by the background and both lighting derivations. Lobe strengths
    // are deliberately applied afterward: changing one must not alter the
    // visible source, the other lobe, or any prefiltered texture.
    [[nodiscard]] inline ImageBasedLightingScales
        ResolveImageBasedLightingScales(
            float outputScale,
            float exposureStops,
            bool diffuseEnabled,
            float diffuseStrength,
            bool specularEnabled,
            float specularStrength)
    {
        const float safeOutputScale = std::max(
            std::isfinite(outputScale) ? outputScale : 1.f,
            0.f);
        const float safeExposure = std::clamp(
            std::isfinite(exposureStops) ? exposureStops : 0.f,
            -12.f,
            12.f);
        const float radiance =
            safeOutputScale * std::exp2(safeExposure);
        const float safeDiffuseStrength = std::max(
            std::isfinite(diffuseStrength) ? diffuseStrength : 0.f,
            0.f);
        const float safeSpecularStrength = std::max(
            std::isfinite(specularStrength) ? specularStrength : 0.f,
            0.f);
        return {
            radiance,
            diffuseEnabled ? radiance * safeDiffuseStrength : 0.f,
            specularEnabled ? radiance * safeSpecularStrength : 0.f
        };
    }

    [[nodiscard]] inline bool IsImageBasedLightingLobeActive(
        bool enabled,
        float strength)
    {
        return enabled &&
            std::isfinite(strength) &&
            strength > 0.f;
    }

    [[nodiscard]] inline bool IsAmbientFillLobeActive(
        bool ambientFillEnabled,
        bool lobeEnabled,
        float strength)
    {
        return ambientFillEnabled &&
            IsImageBasedLightingLobeActive(lobeEnabled, strength);
    }

    [[nodiscard]] inline constexpr float
        ImageBasedLightingGenerationRoughness(float normalizedMip)
    {
        normalizedMip = std::clamp(normalizedMip, 0.f, 1.f);
        return normalizedMip * normalizedMip;
    }

    [[nodiscard]] inline float ImageBasedLightingReceiverMip(
        float perceptualRoughness,
        float mipLevels)
    {
        return std::sqrt(std::clamp(
            perceptualRoughness, 0.f, 1.f)) *
            std::max(mipLevels - 1.f, 0.f);
    }

    [[nodiscard]] inline float ImageBasedLightingSpecularOcclusion(
        float noV,
        float ambientOcclusion,
        float perceptualRoughness)
    {
        ambientOcclusion = std::clamp(ambientOcclusion, 0.f, 1.f);
        const float exponent = std::exp2(
            -16.f * std::clamp(
                perceptualRoughness, 0.f, 1.f) - 1.f);
        return std::clamp(
            std::pow(
                std::clamp(noV + ambientOcclusion, 0.f, 1.f),
                exponent) -
                1.f + ambientOcclusion,
            0.f,
            1.f);
    }
}

#else

float ImageBasedLightingGenerationRoughness(float normalizedMip)
{
    normalizedMip = saturate(normalizedMip);
    return normalizedMip * normalizedMip;
}

float ImageBasedLightingReceiverMip(
    float perceptualRoughness,
    float mipLevels)
{
    return sqrt(saturate(perceptualRoughness)) *
        max(mipLevels - 1.0f, 0.0f);
}

float ImageBasedLightingSpecularOcclusion(
    float noV,
    float ambientOcclusion,
    float perceptualRoughness)
{
    ambientOcclusion = saturate(ambientOcclusion);
    const float exponent = exp2(
        -16.0f * saturate(perceptualRoughness) - 1.0f);
    return saturate(
        pow(saturate(noV + ambientOcclusion), exponent) -
        1.0f + ambientOcclusion);
}

#endif

#endif // UVSR_IMAGE_BASED_LIGHTING_SHARED_H
