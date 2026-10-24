#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    inline constexpr float SpecularDefaultMaxRoughness = 0.22f;
    inline constexpr float SpecularDefaultDepthThickness = 0.05f;
    inline constexpr uint32_t SpecularDefaultMaxTraversalIterations = 128u;
    inline constexpr uint32_t SpecularDefaultMinTraversalOccupancy = 4u;
    inline constexpr uint32_t SpecularMaximumTraversalIterations = 256u;
    inline constexpr uint32_t SpecularMaximumTraversalOccupancy = 64u;
    inline constexpr uint32_t SpecularMaximumMostDetailedMip = 5u;
    inline constexpr float SpecularMinimumTraceAndFilterBias = -2.f;
    inline constexpr float SpecularMaximumTraceAndFilterBias = 2.f;
    inline constexpr float SpecularDefaultRayMaxDistance = 100.f;
    inline constexpr float SpecularDefaultRayBias = 0.002f;
    inline constexpr float SpecularDefaultScreenSpaceConfidence = 0.9f;
    inline constexpr float SpecularDefaultBackfaceRejection = 0.f;
    inline constexpr float SpecularDefaultDepthRejection = 0.01f;

    enum class SpecularSamplesPerQuad : uint32_t
    {
        One,
        Two,
        Four,
        Count
    };

    inline constexpr std::array<std::string_view, 3>
        SpecularSamplesPerQuadLabels = { "1", "2", "4" };

    enum class SpecularDiagnosticView : uint32_t
    {
        FinalImage,
        RayConfidence,
        HitDistance,
        SourcePdf,
        PyramidMip,
        RayRoute,
        Count
    };

    inline constexpr std::array<std::string_view, 6>
        SpecularDiagnosticViewLabels = {
            "Final Image",
            "Ray Confidence",
            "Hit Distance",
            "Source PDF",
            "Pyramid Mip",
            "Ray Route"
        };

    enum class SpecularTechnique : uint32_t
    {
        None,
        StochasticScreenSpace,
        Hybrid
    };

    // These settings describe the screen-space proposal used by both the
    // standalone and Hybrid techniques. Hybrid owns the final result whenever
    // enabled, but deliberately retains this proposal instead of layering a
    // second standalone contribution over it.
    struct SssrSettings
    {
        bool enabled = false;
        bool ratioEstimator = true;
        bool deferredColorLookup = true;
        float maxRoughness = SpecularDefaultMaxRoughness;
        SpecularSamplesPerQuad samplesPerQuad =
            SpecularSamplesPerQuad::One;
        float depthThickness = SpecularDefaultDepthThickness;

        uint32_t maxTraversalIterations =
            SpecularDefaultMaxTraversalIterations;
        uint32_t minTraversalOccupancy =
            SpecularDefaultMinTraversalOccupancy;
        uint32_t mostDetailedMip = 0u;
        float traceAndFilterBias = 0.f;
        SpecularDiagnosticView diagnosticView =
            SpecularDiagnosticView::FinalImage;
    };

    struct HybridReflectionSettings
    {
        bool enabled = false;
        bool rayTracing = true;
        float screenSpaceConfidence =
            SpecularDefaultScreenSpaceConfidence;
        float maxRoughness = SpecularDefaultMaxRoughness;

        float rayMaxDistance = SpecularDefaultRayMaxDistance;
        float rayBias = SpecularDefaultRayBias;
        float backfaceRejection = SpecularDefaultBackfaceRejection;
        float depthRejection = SpecularDefaultDepthRejection;
        bool compactDispatch = true;
        SpecularDiagnosticView diagnosticView =
            SpecularDiagnosticView::FinalImage;
    };

    struct SpecularSettings
    {
        SssrSettings sssr;
        HybridReflectionSettings hybrid;
    };

    [[nodiscard]] inline constexpr bool IsSpecularSamplesPerQuadSupported(
        SpecularSamplesPerQuad value)
    {
        return value == SpecularSamplesPerQuad::One ||
            value == SpecularSamplesPerQuad::Two ||
            value == SpecularSamplesPerQuad::Four;
    }

    [[nodiscard]] inline constexpr uint32_t ResolveSpecularSamplesPerQuad(
        SpecularSamplesPerQuad value)
    {
        switch (value)
        {
        case SpecularSamplesPerQuad::One:
            return 1u;
        case SpecularSamplesPerQuad::Two:
            return 2u;
        case SpecularSamplesPerQuad::Four:
            return 4u;
        default:
            return 1u;
        }
    }

    [[nodiscard]] inline constexpr SpecularTechnique ResolveSpecularTechnique(
        const SpecularSettings& settings)
    {
        if (settings.hybrid.enabled)
            return SpecularTechnique::Hybrid;
        return settings.sssr.enabled
            ? SpecularTechnique::StochasticScreenSpace
            : SpecularTechnique::None;
    }

    [[nodiscard]] inline float ResolveSpecularMaxRoughness(
        const SpecularSettings& settings)
    {
        return ResolveSpecularTechnique(settings) == SpecularTechnique::Hybrid
            ? settings.hybrid.maxRoughness
            : settings.sssr.maxRoughness;
    }

    [[nodiscard]] inline bool IsSpecularDiagnosticViewSupported(
        SpecularDiagnosticView value)
    {
        return static_cast<uint32_t>(value) <
            static_cast<uint32_t>(SpecularDiagnosticView::Count);
    }

    [[nodiscard]] inline SpecularDiagnosticView ResolveSpecularDiagnosticView(
        const SpecularSettings& settings)
    {
        return ResolveSpecularTechnique(settings) == SpecularTechnique::Hybrid
            ? settings.hybrid.diagnosticView
            : settings.sssr.diagnosticView;
    }

    inline void SanitizeSssrSettings(SssrSettings& settings)
    {
        settings.maxRoughness = std::clamp(
            std::isfinite(settings.maxRoughness)
                ? settings.maxRoughness
                : SpecularDefaultMaxRoughness,
            0.f,
            1.f);
        if (!IsSpecularSamplesPerQuadSupported(settings.samplesPerQuad))
            settings.samplesPerQuad = SpecularSamplesPerQuad::One;
        settings.depthThickness = std::max(
            std::isfinite(settings.depthThickness)
                ? settings.depthThickness
                : SpecularDefaultDepthThickness,
            0.f);
        settings.maxTraversalIterations = std::clamp(
            settings.maxTraversalIterations,
            8u,
            SpecularMaximumTraversalIterations);
        settings.minTraversalOccupancy = std::min(
            settings.minTraversalOccupancy,
            SpecularMaximumTraversalOccupancy);
        settings.mostDetailedMip = std::min(
            settings.mostDetailedMip,
            SpecularMaximumMostDetailedMip);
        settings.traceAndFilterBias = std::clamp(
            std::isfinite(settings.traceAndFilterBias)
                ? settings.traceAndFilterBias
                : 0.f,
            SpecularMinimumTraceAndFilterBias,
            SpecularMaximumTraceAndFilterBias);
        if (!IsSpecularDiagnosticViewSupported(settings.diagnosticView))
            settings.diagnosticView = SpecularDiagnosticView::FinalImage;
    }

    inline void SanitizeHybridReflectionSettings(
        HybridReflectionSettings& settings)
    {
        settings.screenSpaceConfidence = std::clamp(
            std::isfinite(settings.screenSpaceConfidence)
                ? settings.screenSpaceConfidence
                : SpecularDefaultScreenSpaceConfidence,
            0.f,
            1.f);
        settings.maxRoughness = std::clamp(
            std::isfinite(settings.maxRoughness)
                ? settings.maxRoughness
                : SpecularDefaultMaxRoughness,
            0.f,
            1.f);
        settings.rayMaxDistance = std::max(
            std::isfinite(settings.rayMaxDistance)
                ? settings.rayMaxDistance
                : SpecularDefaultRayMaxDistance,
            0.001f);
        settings.rayBias = std::max(
            std::isfinite(settings.rayBias)
                ? settings.rayBias
                : SpecularDefaultRayBias,
            0.f);
        settings.backfaceRejection = std::clamp(
            std::isfinite(settings.backfaceRejection)
                ? settings.backfaceRejection
                : SpecularDefaultBackfaceRejection,
            -1.f,
            1.f);
        settings.depthRejection = std::max(
            std::isfinite(settings.depthRejection)
                ? settings.depthRejection
                : SpecularDefaultDepthRejection,
            0.f);
        if (!IsSpecularDiagnosticViewSupported(settings.diagnosticView))
            settings.diagnosticView = SpecularDiagnosticView::FinalImage;
    }

    [[nodiscard]] inline SpecularSettings SanitizeSpecularSettings(
        SpecularSettings settings)
    {
        SanitizeSssrSettings(settings.sssr);
        SanitizeHybridReflectionSettings(settings.hybrid);
        return settings;
    }
}
