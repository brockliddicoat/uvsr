#pragma once

#include <donut/core/math/math.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

enum class PbrMaterialFeature : uint8_t
{
    Coat = 1u << 0,
    Anisotropy = 1u << 1,
    Translucency = 1u << 2,
    Refraction = 1u << 3,
    Scattering = 1u << 4,
    ThinFilmIridescence = 1u << 5,
    Absorption = 1u << 6,
    Dispersion = 1u << 7
};

struct PbrMaterialParameters
{
    donut::math::float3 baseColor = 1.f;
    float metalness = 0.f;
    float perceptualRoughness = 0.5f;
    float ior = 1.5f;
    donut::math::float3 emissive = 0.f;
    float opacity = 1.f;
    uint8_t featureMask = 0;
};

inline float PbrIorToF0(float ior)
{
    const float safeIor = std::max(ior, 1.f);
    const float ratio = (safeIor - 1.f) / (safeIor + 1.f);
    return ratio * ratio;
}

inline void ValidatePbrMaterialParameters(PbrMaterialParameters& material)
{
    auto finiteOr = [](float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    };

    material.baseColor.x = std::clamp(finiteOr(material.baseColor.x, 1.f), 0.f, 1.f);
    material.baseColor.y = std::clamp(finiteOr(material.baseColor.y, 1.f), 0.f, 1.f);
    material.baseColor.z = std::clamp(finiteOr(material.baseColor.z, 1.f), 0.f, 1.f);
    material.metalness = std::clamp(finiteOr(material.metalness, 0.f), 0.f, 1.f);
    material.perceptualRoughness = std::clamp(
        finiteOr(material.perceptualRoughness, 0.5f), 0.f, 1.f);
    material.ior = std::clamp(finiteOr(material.ior, 1.5f), 1.f, 3.f);
    material.emissive.x = std::max(finiteOr(material.emissive.x, 0.f), 0.f);
    material.emissive.y = std::max(finiteOr(material.emissive.y, 0.f), 0.f);
    material.emissive.z = std::max(finiteOr(material.emissive.z, 0.f), 0.f);
    material.opacity = std::clamp(finiteOr(material.opacity, 1.f), 0.f, 1.f);
}
