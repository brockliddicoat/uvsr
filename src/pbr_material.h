#pragma once

#include "renderer_gpu_scalar.h"
#include <math.h>

// CPU import normalization, not the shader's PbrMaterialParameters layout.
struct PbrImportedMaterialValues
{
    uvsr::gpu_contract::Float3 baseColor{ 1.f, 1.f, 1.f };
    float metalness = 0.f;
    float perceptualRoughness = 0.5f;
    float ior = 1.5f;
    uvsr::gpu_contract::Float3 emissive{};
    float opacity = 1.f;
};

inline float PbrIorToF0(float ior)
{
    const float safeIor = ior < 1.f ? 1.f : ior;
    const float ratio = (safeIor - 1.f) / (safeIor + 1.f);
    return ratio * ratio;
}

inline void ValidatePbrImportedMaterial(PbrImportedMaterialValues& material)
{
    const auto finiteOr = [](float value, float fallback)
    {
        return isfinite(value) ? value : fallback;
    };
    const auto clamp = [](float value, float low, float high)
    {
        return value < low ? low : (value > high ? high : value);
    };
    const auto nonnegative = [](float value) { return value < 0.f ? 0.f : value; };

    material.baseColor.x = clamp(finiteOr(material.baseColor.x, 1.f), 0.f, 1.f);
    material.baseColor.y = clamp(finiteOr(material.baseColor.y, 1.f), 0.f, 1.f);
    material.baseColor.z = clamp(finiteOr(material.baseColor.z, 1.f), 0.f, 1.f);
    material.metalness = clamp(finiteOr(material.metalness, 0.f), 0.f, 1.f);
    material.perceptualRoughness = clamp(
        finiteOr(material.perceptualRoughness, 0.5f), 0.f, 1.f);
    material.ior = clamp(finiteOr(material.ior, 1.5f), 1.f, 3.f);
    material.emissive.x = nonnegative(finiteOr(material.emissive.x, 0.f));
    material.emissive.y = nonnegative(finiteOr(material.emissive.y, 0.f));
    material.emissive.z = nonnegative(finiteOr(material.emissive.z, 0.f));
    material.opacity = clamp(finiteOr(material.opacity, 1.f), 0.f, 1.f);
}
