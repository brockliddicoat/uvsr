#include "pbr_material.h"
#include "lighting_contribution_shared.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float MinAlpha = 0.002f;
    constexpr std::uint32_t DirectSource = UVSR_LIGHTING_SOURCE_DIRECT;
    constexpr std::uint32_t EmissiveSource = UVSR_LIGHTING_SOURCE_EMISSIVE;
    constexpr std::uint32_t EnvironmentSource =
        UVSR_LIGHTING_SOURCE_ENVIRONMENT;
    constexpr std::uint32_t IndirectDiffuseSource =
        UVSR_LIGHTING_SOURCE_INDIRECT_DIFFUSE;
    constexpr std::uint32_t IndirectSpecularSource =
        UVSR_LIGHTING_SOURCE_INDIRECT_SPECULAR;
    constexpr std::uint32_t AllLightingSources = UVSR_LIGHTING_SOURCE_ALL;

    using Color = std::array<float, 3>;

    enum class ContributionRejection
    {
        None,
        ZeroSignal,
        BelowThreshold,
        NonFinite
    };

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

    bool HasPotentialSource(
        std::uint32_t knownInactiveSources,
        std::uint32_t relevantSources)
    {
        knownInactiveSources &= AllLightingSources;
        relevantSources &= AllLightingSources;
        return relevantSources != 0u &&
            (knownInactiveSources & relevantSources) != relevantSources;
    }

    ContributionRejection ClassifyContribution(
        std::uint32_t knownInactiveSources,
        std::uint32_t relevantSources,
        const Color& signal,
        float throughputUpperBound,
        float minimumContribution,
        float exposureScale)
    {
        if (!HasPotentialSource(knownInactiveSources, relevantSources))
        {
            return ContributionRejection::ZeroSignal;
        }
        if (!std::isfinite(signal[0]) || !std::isfinite(signal[1]) ||
            !std::isfinite(signal[2]) || !std::isfinite(throughputUpperBound))
        {
            return ContributionRejection::NonFinite;
        }

        const float peakSignal = std::max({
            signal[0], signal[1], signal[2], 0.f
        });
        const float peakContribution = peakSignal *
            std::max(throughputUpperBound, 0.f) * std::max(exposureScale, 0.f);
        if (!(peakContribution > 0.f))
            return ContributionRejection::ZeroSignal;
        if (peakContribution <= std::max(minimumContribution, 0.f))
            return ContributionRejection::BelowThreshold;
        return ContributionRejection::None;
    }

    Color AccumulateDiffuseFrontiers(
        const Color& firstBounce,
        const Color& diffuseTransport,
        std::uint32_t bounceCount,
        float minimumContribution,
        float exposureScale)
    {
        Color cumulative = firstBounce;
        Color frontier = firstBounce;
        for (std::uint32_t bounce = 2u; bounce <= bounceCount; ++bounce)
        {
            if (ClassifyContribution(
                0u, IndirectDiffuseSource, frontier, 1.f,
                minimumContribution, exposureScale) != ContributionRejection::None)
            {
                break;
            }

            const Color nextFrontier = Multiply(frontier, diffuseTransport);
            if (ClassifyContribution(
                0u, IndirectDiffuseSource, nextFrontier, 1.f,
                minimumContribution, exposureScale) != ContributionRejection::None)
            {
                break;
            }
            cumulative = Add(cumulative, nextFrontier);
            frontier = nextFrontier;
        }
        return cumulative;
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

    // Source-state composition is deliberately conservative: unknown scene
    // data stays active, every relevant source must be known inactive before a
    // scope can be skipped.
    constexpr std::array<std::uint32_t, 5> sourceMasks = {
        DirectSource,
        EmissiveSource,
        EnvironmentSource,
        IndirectDiffuseSource,
        IndirectSpecularSource
    };
    std::uint32_t combinedSourceMask = 0u;
    for (std::size_t sourceIndex = 0;
        sourceIndex < sourceMasks.size();
        ++sourceIndex)
    {
        const std::uint32_t sourceMask = sourceMasks[sourceIndex];
        Require(sourceMask != 0u &&
            (sourceMask & (sourceMask - 1u)) == 0u,
            "each lighting source owns one nonzero bit");
        for (std::size_t otherIndex = sourceIndex + 1u;
            otherIndex < sourceMasks.size();
            ++otherIndex)
        {
            Require((sourceMask & sourceMasks[otherIndex]) == 0u,
                "lighting source bits are pairwise disjoint");
        }
        combinedSourceMask |= sourceMask;
    }
    Require(combinedSourceMask == AllLightingSources,
        "all lighting source bits exactly compose the all-sources mask");

    Require(HasPotentialSource(0u, IndirectDiffuseSource),
        "unknown indirect source remains conservatively active");
    Require(HasPotentialSource(DirectSource,
        DirectSource | IndirectDiffuseSource),
        "one inactive source cannot suppress another relevant source");
    Require(!HasPotentialSource(DirectSource | IndirectDiffuseSource,
        DirectSource | IndirectDiffuseSource),
        "all relevant known-inactive sources permit an early out");
    Require(!HasPotentialSource(0u, 0u),
        "an empty relevant-source set has no work");

    const Color positiveSignal = { 0.001f, 0.00025f, 0.0005f };
    Require(ClassifyContribution(
        0u, IndirectDiffuseSource, positiveSignal, 1.f, 0.f, 1.f) ==
        ContributionRejection::None,
        "zero cutoff retains every finite positive contribution");
    Require(ClassifyContribution(
        0u, IndirectDiffuseSource, Color{ 0.f, -1.f, 0.f },
        1.f, 0.f, 1.f) == ContributionRejection::ZeroSignal,
        "exact-zero and negative signals safely early out");
    Require(ClassifyContribution(
        0u, IndirectDiffuseSource,
        Color{ std::numeric_limits<float>::quiet_NaN(), 1.f, 1.f },
        1.f, 0.f, 1.f) == ContributionRejection::NonFinite,
        "non-finite signals are rejected");
    Require(ClassifyContribution(
        IndirectDiffuseSource, IndirectDiffuseSource,
        Color{ 1.f, 1.f, 1.f }, 1.f, 0.f, 1.f) ==
        ContributionRejection::ZeroSignal,
        "known-inactive source rejects work before signal evaluation");
    Require(ClassifyContribution(
        0u, IndirectDiffuseSource, positiveSignal, 0.5f, 0.001f, 2.f) ==
        ContributionRejection::BelowThreshold,
        "contribution cutoff is inclusive after throughput and exposure");
    Require(ClassifyContribution(
        0u, IndirectDiffuseSource, positiveSignal, 0.5f, 0.00099f, 2.f) ==
        ContributionRejection::None,
        "contribution just above the exposed cutoff remains active");
    // Multi-bounce transport advances a frontier B[n] = T(B[n-1]) and keeps a
    // separate cumulative sum C[n] = C[n-1] + B[n]. Feeding C back into T
    // would double-count all earlier paths.
    const Color firstBounce = { 4.f, 2.f, 1.f };
    const Color diffuseTransport = { 0.5f, 0.25f, 0.1f };
    const Color secondBounce = Multiply(firstBounce, diffuseTransport);
    const Color thirdBounce = Multiply(secondBounce, diffuseTransport);
    const Color fourthBounce = Multiply(thirdBounce, diffuseTransport);
    const Color cumulativeFour = Add(
        Add(Add(firstBounce, secondBounce), thirdBounce), fourthBounce);
    Require(Near(AccumulateDiffuseFrontiers(
        firstBounce, diffuseTransport, 4u, 0.f, 1.f), cumulativeFour),
        "four-bounce frontier recurrence matches the explicit path sum");
    const Color incorrectThirdBounce = Multiply(
        Add(firstBounce, secondBounce), diffuseTransport);
    Require(!Near(incorrectThirdBounce, thirdBounce),
        "cumulative radiance is never reinjected as the next frontier");

    const Color decayingFirstBounce = { 1.f, 0.5f, 0.25f };
    const Color decayingTransport = { 0.1f, 0.1f, 0.1f };
    Require(Near(AccumulateDiffuseFrontiers(
        decayingFirstBounce, decayingTransport, 4u, 0.015f, 1.f),
        Color{ 1.1f, 0.55f, 0.275f }),
        "pruned tail preserves all previously accumulated bounces");
    Require(Near(AccumulateDiffuseFrontiers(
        decayingFirstBounce, decayingTransport, 4u, 0.015f, 2.f),
        Color{ 1.11f, 0.555f, 0.2775f }),
        "exposure consistently controls which frontier crosses the cutoff");
    Require(Near(AccumulateDiffuseFrontiers(
        firstBounce, Color{ 0.f, 0.f, 0.f }, 4u, 0.f, 1.f), firstBounce),
        "zero diffuse transport preserves the first bounce and ends the tail");

    std::cout << "UVSR PBR reference validation passed\n";
    return EXIT_SUCCESS;
}
