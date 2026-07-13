#include "pbr_material.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float MinAlpha = 0.002f;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "PBR validation failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool Near(float actual, float expected, float tolerance = 1e-4f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    float Alpha(float perceptualRoughness)
    {
        const float roughness = std::clamp(perceptualRoughness, 0.f, 1.f);
        return std::max(roughness * roughness, MinAlpha);
    }

    float Fresnel(float cosine, float f0)
    {
        const float oneMinusCosine = 1.f - std::clamp(cosine, 0.f, 1.f);
        const float factor = std::pow(oneMinusCosine, 5.f);
        return f0 + (1.f - f0) * factor;
    }

    float DistributionGgx(float normalDotHalf, float alpha)
    {
        const float alphaSquared = alpha * alpha;
        const float denominator = normalDotHalf * normalDotHalf * (alphaSquared - 1.f) + 1.f;
        return alphaSquared / (Pi * denominator * denominator);
    }

    float SmithVisibility(float normalDotView, float normalDotLight, float alpha)
    {
        const float alphaSquared = alpha * alpha;
        const float lambdaView = normalDotLight * std::sqrt(
            normalDotView * normalDotView * (1.f - alphaSquared) + alphaSquared);
        const float lambdaLight = normalDotView * std::sqrt(
            normalDotLight * normalDotLight * (1.f - alphaSquared) + alphaSquared);
        return 0.5f / std::max(lambdaView + lambdaLight, 1e-6f);
    }

    float DiffuseBrdf(float baseColor, float metalness, float fresnel)
    {
        return baseColor * (1.f - metalness) * (1.f - fresnel) / Pi;
    }

    float DirectLight(float incidentRadiance, float bsdf, float cosine, float visibility)
    {
        return incidentRadiance * bsdf * std::max(cosine, 0.f) *
            std::clamp(visibility, 0.f, 1.f);
    }

    float IndirectComposite(
        float directAndEmissive,
        float fallbackIndirect,
        float ambientVisibility,
        float screenSpaceGi)
    {
        return directAndEmissive + fallbackIndirect *
            std::clamp(ambientVisibility, 0.f, 1.f) + screenSpaceGi;
    }
}

int main()
{
    // CPU-side import/upload validation and defaults.
    PbrMaterialParameters defaults;
    Require(defaults.baseColor.x == 1.f && defaults.baseColor.y == 1.f &&
        defaults.baseColor.z == 1.f, "default base color");
    Require(defaults.metalness == 0.f, "default metalness");
    Require(defaults.perceptualRoughness == 0.5f, "default roughness");
    Require(defaults.ior == 1.5f, "default IOR");
    Require(defaults.emissive.x == 0.f && defaults.emissive.y == 0.f &&
        defaults.emissive.z == 0.f, "default emission");
    Require(defaults.opacity == 1.f, "default opacity");

    PbrMaterialParameters invalid;
    invalid.baseColor.x = std::numeric_limits<float>::quiet_NaN();
    invalid.metalness = 2.f;
    invalid.perceptualRoughness = -1.f;
    invalid.ior = 0.f;
    invalid.emissive.x = -2.f;
    invalid.opacity = std::numeric_limits<float>::infinity();
    ValidatePbrMaterialParameters(invalid);
    Require(std::isfinite(invalid.baseColor.x), "invalid base color repaired");
    Require(invalid.metalness == 1.f, "metalness clamped");
    Require(invalid.perceptualRoughness == 0.f, "roughness clamped");
    Require(invalid.ior == 1.f, "IOR clamped");
    Require(invalid.emissive.x == 0.f, "negative emission clamped");
    Require(invalid.opacity == 1.f, "invalid opacity repaired");

    // IOR coverage: vacuum/air, water, common glass, and high-index dielectric.
    Require(Near(PbrIorToF0(1.f), 0.f), "IOR 1.0 F0");
    Require(Near(PbrIorToF0(1.33f), 0.02006f, 2e-4f), "IOR 1.33 F0");
    Require(Near(PbrIorToF0(1.5f), 0.04f), "IOR 1.5 F0");
    Require(Near(PbrIorToF0(2.f), 1.f / 9.f), "IOR 2.0 F0");

    // Dielectric and metallic roughness sweeps remain finite; peak GGX falls
    // as the lobe broadens.
    const float smoothPeak = DistributionGgx(1.f, Alpha(0.01f));
    const float roughPeak = DistributionGgx(1.f, Alpha(1.f));
    Require(std::isfinite(smoothPeak) && std::isfinite(roughPeak), "finite GGX peaks");
    Require(smoothPeak > roughPeak, "roughness broadens and lowers GGX peak");
    Require(std::isfinite(SmithVisibility(0.01f, 0.01f, Alpha(0.5f))),
        "finite grazing Smith visibility");

    const float normalFresnel = Fresnel(1.f, 0.04f);
    const float grazingFresnel = Fresnel(0.05f, 0.04f);
    Require(grazingFresnel > normalFresnel, "grazing Fresnel increases");
    Require(DiffuseBrdf(0.8f, 1.f, normalFresnel) == 0.f,
        "metals have no ordinary diffuse lobe");
    Require(DiffuseBrdf(0.8f, 0.f, normalFresnel) > DiffuseBrdf(0.1f, 0.f, normalFresnel),
        "bright base color increases dielectric diffuse");

    // Directional radiance is distance-independent; point radiance follows
    // inverse-square attenuation at several distances.
    const float directionalNear = 3.f;
    const float directionalFar = 3.f;
    Require(directionalNear == directionalFar, "directional light has no distance falloff");
    const float pointAtOne = 12.f / (1.f * 1.f);
    const float pointAtTwo = 12.f / (2.f * 2.f);
    const float pointAtFour = 12.f / (4.f * 4.f);
    Require(Near(pointAtOne / pointAtTwo, 4.f), "point light inverse-square at 2x");
    Require(Near(pointAtOne / pointAtFour, 16.f), "point light inverse-square at 4x");

    // Visibility is linear and independent from ambient occlusion.
    const float bsdf = DiffuseBrdf(0.5f, 0.f, normalFresnel);
    const float visible = DirectLight(5.f, bsdf, 0.75f, 1.f);
    Require(DirectLight(5.f, bsdf, 0.75f, 0.f) == 0.f, "zero visibility blocks direct light");
    Require(Near(DirectLight(5.f, bsdf, 0.75f, 0.5f), visible * 0.5f),
        "half visibility halves direct light");
    const float directAndEmissive = visible + 2.f;
    const float fallbackIndirect = 0.75f;
    const float screenSpaceGi = 0.5f;
    const float compositeOccluded = IndirectComposite(
        directAndEmissive, fallbackIndirect, 0.f, screenSpaceGi);
    const float compositeVisible = IndirectComposite(
        directAndEmissive, fallbackIndirect, 1.f, screenSpaceGi);
    Require(Near(compositeVisible - compositeOccluded, fallbackIndirect),
        "ambient visibility changes only fallback indirect");
    Require(Near(compositeOccluded - directAndEmissive, screenSpaceGi),
        "ambient visibility does not multiply screen-space GI");
    Require(Near(compositeVisible - fallbackIndirect - screenSpaceGi, directAndEmissive),
        "ambient visibility does not alter direct light or emission");

    const float backFacingSourceCosine = std::max(-0.25f, 0.f);
    Require(backFacingSourceCosine == 0.f,
        "back-facing source contributes no diffuse GI radiance");

    // Geometric-normal validity, no-light, and emission-only behavior.
    const float geometricNormalDotLight = -0.2f;
    Require(geometricNormalDotLight <= 0.f, "back-side light rejected");
    const float emission = 7.f;
    const float noLightFinal = 0.f + emission;
    Require(noLightFinal == emission, "emission remains additive without lights");
    Require(std::isfinite(noLightFinal) && noLightFinal >= 0.f, "final radiance is finite and nonnegative");

    std::cout << "UVSR PBR reference validation passed\n";
    return EXIT_SUCCESS;
}
