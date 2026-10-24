#pragma once

#include "sky_visibility_application.h"
#include "specular_settings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "visibility_projection_shared.h"

namespace uvsr
{
    inline constexpr float SpecularReferenceMinimumPdf = 1e-6f;

    struct SpecularFloat3
    {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
    };

    [[nodiscard]] inline SpecularFloat3 operator+(
        SpecularFloat3 left,
        SpecularFloat3 right)
    {
        return { left.x + right.x, left.y + right.y, left.z + right.z };
    }

    [[nodiscard]] inline SpecularFloat3 operator-(
        SpecularFloat3 left,
        SpecularFloat3 right)
    {
        return { left.x - right.x, left.y - right.y, left.z - right.z };
    }

    inline SpecularFloat3& operator+=(
        SpecularFloat3& left,
        SpecularFloat3 right)
    {
        left = left + right;
        return left;
    }

    [[nodiscard]] inline SpecularFloat3 operator*(
        SpecularFloat3 value,
        float scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    [[nodiscard]] inline SpecularFloat3 operator*(
        SpecularFloat3 left,
        SpecularFloat3 right)
    {
        return { left.x * right.x, left.y * right.y, left.z * right.z };
    }

    [[nodiscard]] inline bool IsFiniteSpecularReference(
        SpecularFloat3 value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    struct SpecularReuseSample
    {
        SpecularFloat3 radiance;
        // Fresnel is deliberately excluded. UVSR's preintegrated DFG term is
        // applied once after normalized reuse.
        float destinationGgx = 0.f;
        float sourcePdf = 0.f;
        bool valid = false;
    };

    [[nodiscard]] inline SpecularFloat3 ApplySpecularDfgOnce(
        SpecularFloat3 radiance,
        SpecularFloat3 dfg)
    {
        if (!IsFiniteSpecularReference(radiance) ||
            !IsFiniteSpecularReference(dfg))
        {
            return {};
        }
        return radiance * dfg;
    }

    [[nodiscard]] inline SpecularFloat3 ResolveSpecularSkyApplication(
        SpecularFloat3 resolvedLobe,
        SpecularFloat3 analyticEnvironmentLobe,
        SpecularFloat3 encodedSkyEstimate,
        uint32_t skyEstimateKind,
        float environmentScale,
        float specularOcclusion,
        float environmentShare)
    {
        SpecularFloat3 adjustedEnvironmentLobe =
            analyticEnvironmentLobe;
        if (skyEstimateKind == UVSR_SKY_ESTIMATE_SCALAR_VISIBILITY)
        {
            const float scalarVisibility =
                std::isfinite(encodedSkyEstimate.x)
                ? std::clamp(encodedSkyEstimate.x, 0.f, 1.f)
                : 1.f;
            adjustedEnvironmentLobe = adjustedEnvironmentLobe *
                scalarVisibility;
        }
        else if (skyEstimateKind == UVSR_SKY_ESTIMATE_RATIO_MODULATION)
        {
            const SpecularFloat3 ratioModulation =
                IsFiniteSpecularReference(encodedSkyEstimate)
                ? SpecularFloat3{
                    std::clamp(encodedSkyEstimate.x, 0.f, 1.f),
                    std::clamp(encodedSkyEstimate.y, 0.f, 1.f),
                    std::clamp(encodedSkyEstimate.z, 0.f, 1.f) }
                : SpecularFloat3{ 1.f, 1.f, 1.f };
            adjustedEnvironmentLobe = adjustedEnvironmentLobe *
                ratioModulation;
        }
        else if (skyEstimateKind ==
            UVSR_SKY_ESTIMATE_FULL_STOCHASTIC_RADIANCE)
        {
            const float scale = std::isfinite(environmentScale)
                ? std::max(environmentScale, 0.f)
                : 0.f;
            const float occlusion = std::isfinite(specularOcclusion)
                ? std::clamp(specularOcclusion, 0.f, 1.f)
                : 0.f;
            adjustedEnvironmentLobe =
                IsFiniteSpecularReference(encodedSkyEstimate)
                ? SpecularFloat3{
                    std::max(encodedSkyEstimate.x, 0.f),
                    std::max(encodedSkyEstimate.y, 0.f),
                    std::max(encodedSkyEstimate.z, 0.f) } *
                    (scale * occlusion)
                : SpecularFloat3{};
        }
        else
        {
            return resolvedLobe;
        }

        const float share = std::isfinite(environmentShare)
            ? std::clamp(environmentShare, 0.f, 1.f)
            : 1.f;
        if (skyEstimateKind ==
            UVSR_SKY_ESTIMATE_FULL_STOCHASTIC_RADIANCE)
        {
            return resolvedLobe - analyticEnvironmentLobe +
                adjustedEnvironmentLobe * share;
        }
        return resolvedLobe - analyticEnvironmentLobe +
            adjustedEnvironmentLobe;
    }

    [[nodiscard]] inline SpecularFloat3 ResolveSpecularSpatialReuse(
        const SpecularReuseSample* samples,
        size_t sampleCount,
        bool useRatioEstimator,
        SpecularFloat3 preintegratedDfg,
        SpecularFloat3 fallbackRadiance = {},
        float pdfEpsilon = SpecularReferenceMinimumPdf)
    {
        const float epsilon = std::isfinite(pdfEpsilon) && pdfEpsilon > 0.f
            ? pdfEpsilon
            : SpecularReferenceMinimumPdf;
        SpecularFloat3 numerator{};
        float denominator = 0.f;
        uint32_t validCount = 0u;
        for (size_t index = 0u; index < sampleCount; ++index)
        {
            const SpecularReuseSample& sample = samples[index];
            if (!sample.valid || !IsFiniteSpecularReference(sample.radiance))
                continue;

            if (!useRatioEstimator)
            {
                numerator += sample.radiance;
                ++validCount;
                continue;
            }

            if (!std::isfinite(sample.destinationGgx) ||
                !std::isfinite(sample.sourcePdf) ||
                sample.destinationGgx <= 0.f || sample.sourcePdf <= 0.f)
            {
                continue;
            }
            const float weight = sample.destinationGgx /
                std::max(sample.sourcePdf, epsilon);
            if (!std::isfinite(weight) || weight <= 0.f)
                continue;
            numerator += sample.radiance * weight;
            denominator += weight;
        }

        SpecularFloat3 resolved = fallbackRadiance;
        if (useRatioEstimator)
        {
            if (std::isfinite(denominator) && denominator > epsilon)
                resolved = numerator * (1.f / denominator);
        }
        else if (validCount > 0u)
        {
            resolved = numerator * (1.f / float(validCount));
        }
        return ApplySpecularDfgOnce(resolved, preintegratedDfg);
    }

    struct SpecularFootprint
    {
        float elongation = 1.f;
        float unbiasedConeTangent = 0.f;
        float traceConeTangent = 0.f;
        float traceFootprintPixels = 0.f;
        float mipLevel = 0.f;
    };

    [[nodiscard]] inline float ResolveSpecularElongation(
        float perceptualRoughness,
        float normalDotView)
    {
        const float roughness = std::clamp(
            std::isfinite(perceptualRoughness)
                ? perceptualRoughness
                : 0.f,
            0.f,
            1.f);
        const float doubledCosine = std::clamp(
            std::isfinite(normalDotView) ? normalDotView * 2.f : 0.f,
            0.f,
            1.f);
        // Frostbite 2015: apply this factor on the cone tangent. Dividing by
        // the factor expands the major footprint at grazing angles.
        return std::max(
            doubledCosine + (1.f - doubledCosine) * std::sqrt(roughness),
            1e-3f);
    }

    [[nodiscard]] inline SpecularFootprint ResolveSpecularFootprint(
        float perceptualRoughness,
        float hitDistance,
        float normalDotView,
        float projectionPixelsPerMeter,
        float traceAndFilterBias,
        uint32_t mipCount)
    {
        const float roughness = std::clamp(
            std::isfinite(perceptualRoughness)
                ? perceptualRoughness
                : 0.f,
            0.f,
            1.f);
        const float distance = std::max(
            std::isfinite(hitDistance) ? hitDistance : 0.f,
            0.f);
        const float projectionScale = std::max(
            std::isfinite(projectionPixelsPerMeter)
                ? projectionPixelsPerMeter
                : 0.f,
            0.f);
        const float bias = std::clamp(
            std::isfinite(traceAndFilterBias)
                ? traceAndFilterBias
                : 0.f,
            SpecularMinimumTraceAndFilterBias,
            SpecularMaximumTraceAndFilterBias);

        SpecularFootprint result;
        result.elongation = ResolveSpecularElongation(
            roughness, normalDotView);
        result.unbiasedConeTangent = std::max(
            roughness * roughness,
            1e-4f) / result.elongation;
        result.traceConeTangent = result.unbiasedConeTangent *
            std::exp2(-bias);
        result.traceFootprintPixels = distance * projectionScale *
            result.traceConeTangent;

        // The same signed bias narrows the proposal by 2^-bias and restores
        // that amount in the lookup LOD. Roughness is therefore not counted
        // once in ray selection and a second time in filtering.
        const float rawMip = std::log2(std::max(
            result.traceFootprintPixels,
            1.f)) + bias;
        const float maximumMip = mipCount > 0u
            ? float(mipCount - 1u)
            : 0.f;
        result.mipLevel = std::clamp(rawMip, 0.f, maximumMip);
        return result;
    }

    enum class SpecularSampleRoute : uint32_t
    {
        InheritedLighting,
        ScreenSpace,
        RayTracedScreenRadiance,
        Environment
    };

    struct SpecularRoutingInputs
    {
        float screenConfidence = 0.f;
        bool rayQuerySupported = false;
        bool globalRayTraversalAllowed = false;
        bool coherentTlasReady = false;
        bool projectableRayHit = false;
    };

    [[nodiscard]] inline SpecularSampleRoute ResolveSpecularSampleRoute(
        const SpecularSettings& settings,
        SpecularRoutingInputs inputs)
    {
        const SpecularTechnique technique =
            ResolveSpecularTechnique(settings);
        if (technique == SpecularTechnique::None)
            return SpecularSampleRoute::InheritedLighting;

        const float confidence = std::clamp(
            std::isfinite(inputs.screenConfidence)
                ? inputs.screenConfidence
                : 0.f,
            0.f,
            1.f);
        if (technique == SpecularTechnique::StochasticScreenSpace)
            return confidence > 0.f
                ? SpecularSampleRoute::ScreenSpace
                : SpecularSampleRoute::Environment;

        if (confidence >= settings.hybrid.screenSpaceConfidence)
            return SpecularSampleRoute::ScreenSpace;
        const bool canTrace = settings.hybrid.rayTracing &&
            inputs.rayQuerySupported &&
            inputs.globalRayTraversalAllowed &&
            inputs.coherentTlasReady;
        if (!canTrace)
            return confidence > 0.f
                ? SpecularSampleRoute::ScreenSpace
                : SpecularSampleRoute::Environment;
        return inputs.projectableRayHit
            ? SpecularSampleRoute::RayTracedScreenRadiance
            : SpecularSampleRoute::Environment;
    }

    [[nodiscard]] inline bool SpecularRayIsAboveHiZ(
        float rayDepth,
        float hierarchyDepth,
        bool reverseDepth)
    {
        if (!std::isfinite(rayDepth) || !std::isfinite(hierarchyDepth))
            return false;
        return reverseDepth
            ? hierarchyDepth < rayDepth
            : hierarchyDepth > rayDepth;
    }
}
