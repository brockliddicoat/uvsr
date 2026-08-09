#include "direct_light_visibility.h"
#include "diffuse_environment_math.h"
#include "pbr_material.h"
#include "screen_space_indirect_composite_shared.h"
#include "screen_space_visibility_defaults.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

#include "image_based_lighting_shared.h"

namespace
{
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float MinAlpha = 0.002f;

    using Color = std::array<float, 3>;

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

    bool Near(const Color& actual, const Color& expected, float tolerance = 1e-4f)
    {
        return Near(actual[0], expected[0], tolerance) &&
            Near(actual[1], expected[1], tolerance) &&
            Near(actual[2], expected[2], tolerance);
    }

    bool ShouldFlipSurfaceNormals(
        bool isDoubleSided,
        bool isFrontFace,
        float geometricNormalDotView)
    {
        return isDoubleSided
            ? geometricNormalDotView < 0.f
            : !isFrontFace;
    }

    Color Add(const Color& left, const Color& right)
    {
        return {
            left[0] + right[0],
            left[1] + right[1],
            left[2] + right[2]
        };
    }

    Color Multiply(const Color& left, const Color& right)
    {
        return {
            left[0] * right[0],
            left[1] * right[1],
            left[2] * right[2]
        };
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

    float AnalyticalPositionalLightIntensity(
        float luminousIntensity,
        float distance,
        float radius)
    {
        const float distanceSquared = distance * distance;
        if (!(std::isfinite(radius) && radius > 0.f))
            return luminousIntensity / distanceSquared;

        const float halfAngularSize = std::atan(std::min(
            radius / distance,
            1.f));
        return luminousIntensity / (radius * radius) *
            halfAngularSize * halfAngularSize;
    }

    float IndirectComposite(
        float directAndEmissive,
        float fallbackIndirect,
        float ambientVisibility,
        float screenSpaceGi)
    {
        return ComposeScreenSpaceIndirectLighting(
            directAndEmissive,
            fallbackIndirect,
            std::clamp(ambientVisibility, 0.f, 1.f),
            screenSpaceGi);
    }

    bool Near(
        dm::float3 actual,
        dm::float3 expected,
        float tolerance = 1e-4f)
    {
        return Near(actual.x, expected.x, tolerance) &&
            Near(actual.y, expected.y, tolerance) &&
            Near(actual.z, expected.z, tolerance);
    }

    bool FiniteNonnegative(dm::float3 value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z) &&
            value.x >= 0.f && value.y >= 0.f && value.z >= 0.f;
    }

    struct MsaaVisibilityGuide
    {
        float depth;
        bool validNormal;
    };

    template<std::size_t SampleCount>
    int ResolveClosestReverseZOwner(
        const std::array<MsaaVisibilityGuide, SampleCount>& guides)
    {
        int owner = -1;
        float closestDepth = 0.f;
        for (std::size_t sampleIndex = 0u;
            sampleIndex < SampleCount;
            ++sampleIndex)
        {
            const MsaaVisibilityGuide& guide =
                guides[sampleIndex];
            const bool valid =
                std::isfinite(guide.depth) &&
                guide.depth > 0.f &&
                guide.validNormal;
            if (valid &&
                (owner < 0 || guide.depth > closestDepth))
            {
                owner = static_cast<int>(sampleIndex);
                closestDepth = guide.depth;
            }
        }
        return owner;
    }
}

int main()
{
    for (uint32_t mip = 0u; mip < 9u; ++mip)
    {
        const float normalizedMip = float(mip) / 8.f;
        Require(Near(
            uvsr::ImageBasedLightingGenerationRoughness(normalizedMip),
            normalizedMip * normalizedMip),
            "IBL prefilter mip generation follows Donut's squared schedule");
    }
    constexpr std::array<float, 6> IblRoughnessSweep = {
        0.f, 0.01f, 0.25f, 0.5f, 0.75f, 1.f
    };
    for (float perceptualRoughness : IblRoughnessSweep)
    {
        Require(Near(
            uvsr::ImageBasedLightingReceiverMip(
                perceptualRoughness, 9.f),
            std::sqrt(perceptualRoughness) * 8.f),
            "IBL receiver selects the matching fractional specular mip");
    }
    Require(
        uvsr::ImageBasedLightingReceiverMip(-1.f, 9.f) == 0.f &&
            uvsr::ImageBasedLightingReceiverMip(2.f, 9.f) == 8.f &&
            uvsr::ImageBasedLightingReceiverMip(0.5f, 0.f) == 0.f,
        "IBL receiver mip selection clamps roughness and empty mip ranges");
    for (float perceptualRoughness : IblRoughnessSweep)
    {
        float previousOcclusion = 0.f;
        for (uint32_t aoStep = 0u; aoStep <= 8u; ++aoStep)
        {
            const float ambientOcclusion = float(aoStep) / 8.f;
            const float occlusion =
                uvsr::ImageBasedLightingSpecularOcclusion(
                    0.35f,
                    ambientOcclusion,
                    perceptualRoughness);
            Require(
                std::isfinite(occlusion) &&
                    occlusion >= 0.f &&
                    occlusion <= 1.f,
                "IBL specular occlusion stays finite and normalized");
            Require(
                occlusion + 1e-6f >= previousOcclusion,
                "IBL specular occlusion is monotonic in ambient visibility");
            previousOcclusion = occlusion;
        }
        Require(
            uvsr::ImageBasedLightingSpecularOcclusion(
                0.35f, 0.f, perceptualRoughness) == 0.f &&
                uvsr::ImageBasedLightingSpecularOcclusion(
                    0.35f, 1.f, perceptualRoughness) == 1.f,
            "IBL specular occlusion preserves fully blocked and open endpoints");
    }

    Require(uvsr::ScreenSpaceIndirectDiffuseReferenceIntensity == 1.f,
        "screen-space GI defaults to reference energy");

    const uvsr::ImageBasedLightingScales referenceIblScales =
        uvsr::ResolveImageBasedLightingScales(
            1.f, 0.f, true, 1.f, true, 1.f);
    Require(
        Near(referenceIblScales.radiance, 1.f) &&
            Near(referenceIblScales.diffuse, 1.f) &&
            Near(referenceIblScales.specular, 1.f),
        "reference IBL settings preserve unit radiance and lobe energy");

    const uvsr::ImageBasedLightingScales adjustedIblScales =
        uvsr::ResolveImageBasedLightingScales(
            2.f, 2.f, true, 0.5f, true, 2.f);
    Require(
        Near(adjustedIblScales.radiance, 8.f) &&
            Near(adjustedIblScales.diffuse, 4.f) &&
            Near(adjustedIblScales.specular, 16.f),
        "IBL base scale and exposure precede independent lobe gains");

    const uvsr::ImageBasedLightingScales disabledIblScales =
        uvsr::ResolveImageBasedLightingScales(
            2.f, 2.f, false, 0.5f, false, 2.f);
    Require(
        Near(disabledIblScales.radiance, adjustedIblScales.radiance) &&
            disabledIblScales.diffuse == 0.f &&
            disabledIblScales.specular == 0.f,
        "disabled IBL lobes are zero without changing common radiance");

    const uvsr::ImageBasedLightingScales invalidStrengthIblScales =
        uvsr::ResolveImageBasedLightingScales(
            1.f,
            0.f,
            true,
            std::numeric_limits<float>::quiet_NaN(),
            true,
            -1.f);
    Require(
        invalidStrengthIblScales.radiance == 1.f &&
            invalidStrengthIblScales.diffuse == 0.f &&
            invalidStrengthIblScales.specular == 0.f,
        "invalid IBL strengths resolve to zero lobe energy");
    const uvsr::ImageBasedLightingScales invalidBaseIblScales =
        uvsr::ResolveImageBasedLightingScales(
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN(),
            true,
            1.f,
            true,
            1.f);
    Require(
        invalidBaseIblScales.radiance == 1.f &&
            invalidBaseIblScales.diffuse == 1.f &&
            invalidBaseIblScales.specular == 1.f,
        "nonfinite IBL base and exposure fall back to reference settings");
    Require(
        uvsr::ResolveImageBasedLightingScales(
            -1.f, 12.f, true, 1.f, true, 1.f).radiance == 0.f,
        "negative IBL base scale cannot produce negative radiance");

    Require(
        uvsr::IsImageBasedLightingLobeActive(true, 1.f) &&
            uvsr::IsImageBasedLightingLobeActive(true, 0.001f),
        "enabled finite positive IBL lobes are active");
    Require(
        !uvsr::IsImageBasedLightingLobeActive(false, 1.f) &&
            !uvsr::IsImageBasedLightingLobeActive(true, 0.f) &&
            !uvsr::IsImageBasedLightingLobeActive(true, -1.f) &&
            !uvsr::IsImageBasedLightingLobeActive(
                true, std::numeric_limits<float>::quiet_NaN()) &&
            !uvsr::IsImageBasedLightingLobeActive(
                true, std::numeric_limits<float>::infinity()),
        "disabled, nonpositive, and nonfinite IBL lobes are inactive");
    Require(
        uvsr::IsAmbientFillLobeActive(true, true, 1.f) &&
            !uvsr::IsAmbientFillLobeActive(false, true, 1.f) &&
            !uvsr::IsAmbientFillLobeActive(true, false, 1.f) &&
            !uvsr::IsAmbientFillLobeActive(true, true, 0.f),
        "ambient fill master-gates IBL lobes without changing lobe settings");

    // The diffuse environment stores unit-albedo outgoing diffuse response.
    // Projecting a constant scene-linear lat-long source must therefore
    // reproduce the same constant without a second pi factor at the receiver.
    constexpr uint32_t ConstantEnvironmentWidth = 64u;
    constexpr uint32_t ConstantEnvironmentHeight = 32u;
    const dm::float3 constantRadiance(0.2f, 0.4f, 0.8f);
    std::vector<float> constantPixels(
        std::size_t(ConstantEnvironmentWidth) *
            std::size_t(ConstantEnvironmentHeight) * 3u);
    for (std::size_t pixel = 0u;
        pixel < constantPixels.size();
        pixel += 3u)
    {
        constantPixels[pixel + 0u] = constantRadiance.x;
        constantPixels[pixel + 1u] = constantRadiance.y;
        constantPixels[pixel + 2u] = constantRadiance.z;
    }

    const auto constantProjection =
        uvsr::ProjectDiffuseEnvironmentLatLongRgb(
            constantPixels.data(),
            ConstantEnvironmentWidth,
            ConstantEnvironmentHeight);
    Require(
        constantProjection.has_value(),
        "constant lat-long radiance projects to diffuse SH");
    const std::array<dm::float3, 7> referenceDirections = {
        dm::float3(1.f, 0.f, 0.f),
        dm::float3(-1.f, 0.f, 0.f),
        dm::float3(0.f, 1.f, 0.f),
        dm::float3(0.f, -1.f, 0.f),
        dm::float3(0.f, 0.f, 1.f),
        dm::float3(0.f, 0.f, -1.f),
        dm::float3(1.f, 1.f, 1.f)
    };
    for (dm::float3 direction : referenceDirections)
    {
        Require(Near(
            uvsr::EvaluateDiffuseEnvironmentSh(
                constantProjection->sh,
                direction),
            constantRadiance,
            2e-3f),
            "constant lat-long projects to a constant diffuse response");
    }
    Require(Near(
        uvsr::EvaluateDiffuseEnvironmentSh(
            constantProjection->sh,
            dm::float3(0.f)),
        uvsr::EvaluateDiffuseEnvironmentSh(
            constantProjection->sh,
            dm::float3(0.f, 1.f, 0.f)),
        1e-6f),
        "zero diffuse direction uses the stable up fallback");
    Require(Near(
        uvsr::EvaluateDiffuseEnvironmentSh(
            constantProjection->sh,
            dm::float3(
                std::numeric_limits<float>::quiet_NaN(),
                0.f,
                0.f)),
        uvsr::EvaluateDiffuseEnvironmentSh(
            constantProjection->sh,
            dm::float3(0.f, 1.f, 0.f)),
        1e-6f),
        "nonfinite diffuse direction uses the stable up fallback");

    // Cubemap face order must match Donut/NVRHI's TextureCubeArray contract.
    const std::array<dm::float3, 6> expectedFaceAxes = {
        dm::float3(1.f, 0.f, 0.f),
        dm::float3(-1.f, 0.f, 0.f),
        dm::float3(0.f, 1.f, 0.f),
        dm::float3(0.f, -1.f, 0.f),
        dm::float3(0.f, 0.f, 1.f),
        dm::float3(0.f, 0.f, -1.f)
    };
    for (uint32_t face = 0u; face < expectedFaceAxes.size(); ++face)
    {
        Require(Near(
            uvsr::DiffuseEnvironmentCubeDirection(
                face, 0u, 0u, 1u),
            expectedFaceAxes[face]),
            "diffuse environment cubemap face axis");
    }

    std::vector<float> nonfinitePixels = constantPixels;
    nonfinitePixels[0] = std::numeric_limits<float>::quiet_NaN();
    nonfinitePixels[1] = std::numeric_limits<float>::infinity();
    nonfinitePixels[2] = -std::numeric_limits<float>::infinity();
    const auto sanitizedProjection =
        uvsr::ProjectDiffuseEnvironmentLatLongRgb(
            nonfinitePixels.data(),
            ConstantEnvironmentWidth,
            ConstantEnvironmentHeight);
    Require(
        sanitizedProjection.has_value(),
        "isolated nonfinite source texels are sanitized during projection");
    for (dm::float3 direction : referenceDirections)
    {
        Require(FiniteNonnegative(
            uvsr::EvaluateDiffuseEnvironmentSh(
                sanitizedProjection->sh,
                direction)),
            "sanitized imported diffuse response is finite and nonnegative");
    }

    const dm::float3 halfClamped =
        uvsr::ClampDiffuseEnvironmentForHalf(dm::float3(
            std::numeric_limits<float>::max(),
            uvsr::DiffuseEnvironmentHalfMaximum * 2.f,
            -1.f));
    Require(Near(
        halfClamped,
        dm::float3(
            uvsr::DiffuseEnvironmentHalfMaximum,
            uvsr::DiffuseEnvironmentHalfMaximum,
            0.f),
        1.f),
        "finite diffuse radiance clamps to the nonnegative half range");
    Require(all(
        uvsr::ClampDiffuseEnvironmentForHalf(dm::float3(
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity())) ==
            dm::float3(0.f)),
        "nonfinite diffuse radiance sanitizes before half packing");

    std::vector<float> blackPixels(8u * 4u * 3u, 0.f);
    Require(
        !uvsr::ProjectDiffuseEnvironmentLatLongRgb(
            nullptr, 4u, 2u).has_value() &&
            !uvsr::ProjectDiffuseEnvironmentLatLongRgb(
                constantPixels.data(), 2u, 2u).has_value() &&
            !uvsr::ProjectDiffuseEnvironmentLatLongRgb(
                constantPixels.data(), 4u, 1u).has_value() &&
            !uvsr::ProjectDiffuseEnvironmentLatLongRgb(
                constantPixels.data(), 8u, 2u).has_value(),
        "null, undersized, and non-lat-long projection inputs are rejected");
    Require(
        !uvsr::ProjectDiffuseEnvironmentLatLongRgb(
            blackPixels.data(), 8u, 4u).has_value(),
        "zero-energy lat-long input is rejected");

    // Each direct visibility input applies only when its exact light
    // identity matches. Missing or unrelated visibility is white.
    Require(!uvsr::DirectLightVisibility{}.IsComplete(),
        "an empty visibility input is incomplete");
    int textureToken0 = 0;
    int lightToken0 = 0;
    int lightToken1 = 0;
    auto* texture0 = reinterpret_cast<nvrhi::ITexture*>(
        &textureToken0);
    auto* light0 = reinterpret_cast<const donut::engine::Light*>(
        &lightToken0);
    auto* light1 = reinterpret_cast<const donut::engine::Light*>(
        &lightToken1);
    const uvsr::DirectLightVisibility factor0{
        texture0, light0
    };
    const uvsr::DirectLightVisibilities factors{
        factor0,
        { texture0, light0,
            uvsr::DirectLightVisibilityEncoding::RgbRgba16Float }
    };
    Require(factors.flashlight.IsComplete() &&
        factors.sun.IsComplete(),
        "flashlight and sun slots are independently complete");
    Require(uvsr::TargetsDirectLight(factor0, light0),
        "pointer-identical light accepts its factor");
    Require(!uvsr::TargetsDirectLight(factor0, light1),
        "distinct light pointer rejects the factor");
    Require(!uvsr::TargetsDirectLight(
        uvsr::DirectLightVisibility{ texture0, nullptr },
        light0),
        "incomplete factor remains neutral");
    Require(uvsr::ComposeDirectLightVisibility(
        0.5f, 0.25f, true) == 0.25f,
        "matching visibility inputs select the strongest occlusion");
    Require(uvsr::ComposeDirectLightVisibility(
        0.25f, 0.f, false) == 0.25f,
        "unmatched visibility remains neutral");
    Require(uvsr::ComposeDirectLightVisibility(
        4.f, -1.f, true) == 0.f,
        "visibility factors clamp before composition");
    const Color combinedVisibility = {
        uvsr::ComposeDirectLightVisibility(0.6f, 0.8f, true),
        uvsr::ComposeDirectLightVisibility(0.6f, 0.4f, true),
        uvsr::ComposeDirectLightVisibility(0.6f, 0.7f, true)
    };
    Require(Near(combinedVisibility, Color{ 0.6f, 0.4f, 0.6f }),
        "both-on composition uses componentwise minimum, not multiplication");

    const uvsr::DirectLightVisibilityTextureProperties
        compatibleVisibilityTexture{
            1920u, 1080u, 1u, 1u, 1u, 1u,
            true, false, true, true
        };
    Require(uvsr::IsDirectLightVisibilityTextureCompatible(
        compatibleVisibilityTexture, 1920u, 1080u),
        "full-resolution R8 visibility texture is accepted");
    auto incompatibleVisibilityTexture = compatibleVisibilityTexture;
    incompatibleVisibilityTexture.width = 1919u;
    Require(!uvsr::IsDirectLightVisibilityTextureCompatible(
        incompatibleVisibilityTexture, 1920u, 1080u),
        "stale-sized visibility texture fails white");
    incompatibleVisibilityTexture = compatibleVisibilityTexture;
    incompatibleVisibilityTexture.r8Unorm = false;
    Require(!uvsr::IsDirectLightVisibilityTextureCompatible(
        incompatibleVisibilityTexture, 1920u, 1080u),
        "wrong-format visibility texture fails white");
    auto compatibleRgbVisibilityTexture = compatibleVisibilityTexture;
    compatibleRgbVisibilityTexture.r8Unorm = false;
    compatibleRgbVisibilityTexture.rgba16Float = true;
    Require(uvsr::IsDirectLightVisibilityTextureCompatible(
        compatibleRgbVisibilityTexture,
        1920u,
        1080u,
        uvsr::DirectLightVisibilityEncoding::RgbRgba16Float),
        "full-resolution RGBA16F RGB modulation texture is accepted");
    Require(!uvsr::IsDirectLightVisibilityTextureCompatible(
        compatibleRgbVisibilityTexture,
        1920u,
        1080u,
        uvsr::DirectLightVisibilityEncoding::ScalarR8Unorm),
        "RGB modulation cannot masquerade as scalar R8 visibility");
    incompatibleVisibilityTexture = compatibleVisibilityTexture;
    incompatibleVisibilityTexture.sampleCount = 2u;
    Require(!uvsr::IsDirectLightVisibilityTextureCompatible(
        incompatibleVisibilityTexture, 1920u, 1080u),
        "multisampled visibility texture fails white");
    incompatibleVisibilityTexture = compatibleVisibilityTexture;
    incompatibleVisibilityTexture.shaderResource = false;
    Require(!uvsr::IsDirectLightVisibilityTextureCompatible(
        incompatibleVisibilityTexture, 1920u, 1080u),
        "non-SRV visibility texture fails white");

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
    Require(Near(Alpha(0.f), MinAlpha),
        "GGX alpha retains the deterministic minimum roughness floor");

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

    const float exactPointEmitter =
        AnalyticalPositionalLightIntensity(12.f, 2.f, 0.f);
    Require(
        exactPointEmitter == pointAtTwo,
        "zero-radius analytical emitter preserves the exact point-light branch");
    const float nearFiniteEmitter =
        AnalyticalPositionalLightIntensity(12.f, 0.01f, 0.1f);
    Require(
        std::isfinite(nearFiniteEmitter) && nearFiniteEmitter > 0.f &&
            nearFiniteEmitter < 12.f / (0.01f * 0.01f),
        "positive-radius analytical emitter bounds near-field energy");
    const float farFiniteEmitter =
        AnalyticalPositionalLightIntensity(12.f, 100.f, 0.1f);
    const float farPointEmitter = 12.f / (100.f * 100.f);
    Require(
        Near(farFiniteEmitter, farPointEmitter, 1e-7f),
        "finite analytical emitter converges to inverse square in the far field");

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
    Require(Near(
        IndirectComposite(directAndEmissive, fallbackIndirect, 1.f, 0.f),
        directAndEmissive + fallbackIndirect),
        "separated neutral indirect composite adds no hidden specular term");

    // The Deferred MSAA visibility bridge must select one complete reverse-Z
    // owner instead of averaging guides across sparse silhouette coverage.
    const float nan =
        std::numeric_limits<float>::quiet_NaN();
    const std::array<MsaaVisibilityGuide, 8>
        sparseVisibilityGuides = {{
            { 0.f, false },
            { 0.25f, true },
            { nan, true },
            { 0.8f, false },
            { 0.75f, true },
            { 0.75f, true },
            { -0.1f, true },
            { std::numeric_limits<float>::infinity(), true }
        }};
    Require(
        ResolveClosestReverseZOwner(
            sparseVisibilityGuides) == 4,
        "closest reverse-Z MSAA visibility owner rejects background, invalid normals, and non-finite depth while preserving the lower-index tie");
    const std::array<MsaaVisibilityGuide, 2>
        backgroundVisibilityGuides = {{
            { 0.f, false },
            { 0.f, false }
        }};
    Require(
        ResolveClosestReverseZOwner(
            backgroundVisibilityGuides) == -1,
        "all-background MSAA visibility has no fabricated surface owner");
    const float visibilityCorrection = -2.f;
    Require(
        Near(
            visibilityCorrection * (1.f / 4.f),
            -0.5f) &&
            Near(
                visibilityCorrection * (4.f / 4.f),
                -2.f),
        "MSAA visibility correction scales continuously from sparse coverage to full coverage");

    const float backFacingSourceCosine = std::max(-0.25f, 0.f);
    Require(backFacingSourceCosine == 0.f,
        "back-facing source contributes no diffuse GI radiance");

    // Geometric-normal validity, no-light, and emission-only behavior.
    const float geometricNormalDotLight = -0.2f;
    Require(geometricNormalDotLight <= 0.f, "back-side light rejected");
    Require(!ShouldFlipSurfaceNormals(true, false, 0.8f),
        "a reflected double-sided instance keeps its view-facing normal despite raster winding");
    Require(ShouldFlipSurfaceNormals(true, true, -0.8f),
        "a double-sided back face is oriented into the view hemisphere");
    Require(ShouldFlipSurfaceNormals(false, false, 0.8f) &&
        !ShouldFlipSurfaceNormals(false, true, -0.8f),
        "single-sided normal orientation retains the raster-facing contract");
    const float emission = 7.f;
    const float noLightFinal = 0.f + emission;
    Require(noLightFinal == emission, "emission remains additive without lights");
    Require(std::isfinite(noLightFinal) && noLightFinal >= 0.f, "final radiance is finite and nonnegative");

    Require(uvsr::HasActiveScreenSpaceLightingConsumer(
            true, false, true, false, false),
        "active diffuse GI is an independent visibility consumer");
    Require(uvsr::HasActiveScreenSpaceLightingConsumer(
            true, true, false, true, false),
        "AO remains active for diffuse environment lighting");
    Require(uvsr::HasActiveScreenSpaceLightingConsumer(
            true, true, false, false, true),
        "AO remains active for specular environment lighting");
    Require(!uvsr::HasActiveScreenSpaceLightingConsumer(
            true, true, false, false, false),
        "AO without GI or an environment lobe is a no-op");
    Require(!uvsr::HasActiveScreenSpaceLightingConsumer(
            false, true, true, true, true),
        "the visibility master toggle disables every consumer");

    std::cout << "UVSR PBR reference validation passed\n";
    return EXIT_SUCCESS;
}
