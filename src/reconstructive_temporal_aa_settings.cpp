#include "reconstructive_temporal_aa_settings.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    using namespace uvsr;

    float SanitizeFloat(float value, float fallback, float minimum, float maximum)
    {
        if (!std::isfinite(value))
            value = fallback;
        return std::clamp(value, minimum, maximum);
    }

    bool IsValid(ReconstructiveTemporalPreset preset)
    {
        switch (preset)
        {
        case ReconstructiveTemporalPreset::HeavyTemporal:
        case ReconstructiveTemporalPreset::MediumTemporal:
        case ReconstructiveTemporalPreset::LightTemporal:
        case ReconstructiveTemporalPreset::Custom:
            return true;
        }
        return false;
    }

    bool IsValid(JitterSequence sequence)
    {
        return sequence == JitterSequence::R2 || sequence == JitterSequence::Halton23;
    }

    bool IsValid(HistorySampleFilter filter)
    {
        return filter == HistorySampleFilter::Bilinear ||
            filter == HistorySampleFilter::CatmullRom;
    }

    bool IsValid(ReconstructiveTemporalDebugMode mode)
    {
        return static_cast<uint32_t>(mode) < ReconstructiveTemporalDebugModeCount;
    }

    void ApplyCommonPresetValues(ReconstructiveTemporalAASettings& settings)
    {
        settings.jitterSequence = JitterSequence::R2;
        settings.jitterScale = 1.0f;

        settings.historyFilter = HistorySampleFilter::CatmullRom;
        settings.velocityDilationEnabled = true;
        settings.motionResponseStartPixels = 0.5f;
        settings.motionResponseEndPixels = 8.0f;

        settings.absoluteDepthThreshold = 0.01f;
        settings.relativeDepthThreshold = 0.02f;
        settings.normalRejectCosine = 0.75f;
        settings.normalAcceptCosine = 0.95f;
        settings.validateMaterialIdentity = true;
        settings.validateObjectIdentity = false;

        settings.explicitReactiveMaskEnabled = true;
        settings.automaticReactiveMaskEnabled = true;
        settings.reactiveLuminanceThreshold = 0.10f;
        settings.reactiveChromaThreshold = 0.10f;

        settings.thinGeometryEnabled = true;
        settings.thinGeometryDepthRange = 0.02f;
        settings.thinGeometryContrastThreshold = 0.15f;
        settings.thinGeometryCoverageResponseMs = 80.0f;
        settings.thinGeometryClusterDiffusion = true;

        settings.luminanceClipStrength = 1.0f;
        settings.chromaClipStrength = 1.0f;

        settings.spatialFallbackEnabled = true;
        settings.spatialDepthWeight = 64.0f;
        settings.spatialNormalWeight = 16.0f;
        settings.spatialLuminanceWeight = 4.0f;

        settings.persistentFrameInterval = 1;
        settings.maximumResurrectionWeight = 0.20f;
        settings.resurrectionMatchThreshold = 0.70f;

        settings.sharpeningEnabled = true;
        settings.sharpeningMotionSuppression = 0.75f;
        settings.sharpeningReactiveSuppression = 0.90f;
        settings.sharpeningVarianceSuppression = 0.75f;
        settings.sharpeningHaloClamp = 0.10f;
    }

    double Fraction(double value)
    {
        return value - std::floor(value);
    }

    double Halton(uint32_t index, uint32_t base)
    {
        double result = 0.0;
        double fraction = 1.0;
        while (index != 0)
        {
            fraction /= static_cast<double>(base);
            result += fraction * static_cast<double>(index % base);
            index /= base;
        }
        return result;
    }

    float CurrentWeightFromTimeConstant(float deltaSeconds, float responseMs)
    {
        if (std::isnan(deltaSeconds) || deltaSeconds <= 0.0f)
            return 0.0f;
        if (std::isinf(deltaSeconds))
            return 1.0f;

        // expm1 retains precision for high frame rates where delta/tau is
        // small. The shader receives this precomputed result and never pays for
        // a per-pixel exponential.
        const double timeConstantSeconds = static_cast<double>(responseMs) * 0.001;
        const double exponent = -static_cast<double>(deltaSeconds) / timeConstantSeconds;
        const double weight = -std::expm1(exponent);
        return static_cast<float>(std::clamp(weight, 0.0, 1.0));
    }
}

namespace uvsr
{
    void ApplyReconstructiveTemporalPreset(
        ReconstructiveTemporalAASettings& settings,
        ReconstructiveTemporalPreset preset)
    {
        if (!IsValid(preset))
            preset = ReconstructiveTemporalPreset::MediumTemporal;

        settings.preset = preset;
        if (preset == ReconstructiveTemporalPreset::Custom)
        {
            SanitizeReconstructiveTemporalSettings(settings);
            return;
        }

        // Presets define every image-quality setting so switching away from
        // Custom cannot retain a hidden stale value. Enabled state, force reset,
        // zero-jitter freeze, held phase, and the active debug view remain
        // user-controlled.
        ApplyCommonPresetValues(settings);

        switch (preset)
        {
        case ReconstructiveTemporalPreset::HeavyTemporal:
            settings.jitterPeriod = 16;
            settings.automaticReactiveStrength = 0.60f;
            settings.thinGeometryMaximumRelaxation = 0.040f;
            settings.thinGeometryClipExpansion = 0.040f;
            settings.varianceClipSigma = 1.60f;
            settings.stableResponseMs = 250.0f;
            settings.movingResponseMs = 100.0f;
            settings.reactiveResponseMs = 12.0f;
            settings.maximumHistorySamples = 24;
            settings.maximumMovingHistorySamples = 10;
            settings.spatialFallbackRadius = 2;
            settings.resurrectionEnabled = true;
            settings.persistentFrameCount = 2;
            settings.sharpeningStrength = 0.08f;
            break;

        case ReconstructiveTemporalPreset::MediumTemporal:
            settings.jitterPeriod = 8;
            settings.automaticReactiveStrength = 0.80f;
            settings.thinGeometryMaximumRelaxation = 0.025f;
            settings.thinGeometryClipExpansion = 0.025f;
            settings.varianceClipSigma = 1.25f;
            settings.stableResponseMs = 125.0f;
            settings.movingResponseMs = 50.0f;
            settings.reactiveResponseMs = 8.0f;
            settings.maximumHistorySamples = 12;
            settings.maximumMovingHistorySamples = 5;
            settings.spatialFallbackRadius = 1;
            settings.resurrectionEnabled = false;
            settings.persistentFrameCount = 0;
            settings.sharpeningStrength = 0.18f;
            break;

        case ReconstructiveTemporalPreset::LightTemporal:
            settings.jitterPeriod = 8;
            settings.automaticReactiveStrength = 1.00f;
            settings.thinGeometryMaximumRelaxation = 0.012f;
            settings.thinGeometryClipExpansion = 0.012f;
            settings.varianceClipSigma = 0.95f;
            settings.stableResponseMs = 55.0f;
            settings.movingResponseMs = 22.0f;
            settings.reactiveResponseMs = 4.0f;
            settings.maximumHistorySamples = 6;
            settings.maximumMovingHistorySamples = 2;
            settings.spatialFallbackRadius = 1;
            settings.resurrectionEnabled = false;
            settings.persistentFrameCount = 0;
            settings.sharpeningStrength = 0.30f;
            break;

        case ReconstructiveTemporalPreset::Custom:
            break;
        }

        SanitizeReconstructiveTemporalSettings(settings);
    }

    void SanitizeReconstructiveTemporalSettings(
        ReconstructiveTemporalAASettings& settings)
    {
        const ReconstructiveTemporalAASettings defaults;

        // An invalid preset identifier cannot truthfully describe the remaining
        // fields. Mark it Custom rather than relabeling arbitrary values as a
        // production preset.
        if (!IsValid(settings.preset))
            settings.preset = ReconstructiveTemporalPreset::Custom;
        if (!IsValid(settings.jitterSequence))
            settings.jitterSequence = defaults.jitterSequence;
        if (!IsValid(settings.historyFilter))
            settings.historyFilter = defaults.historyFilter;
        if (!IsValid(settings.debugMode))
            settings.debugMode = defaults.debugMode;

        settings.jitterPeriod = std::clamp(settings.jitterPeriod, 2u, 64u);
        settings.heldJitterPhase = std::min(
            settings.heldJitterPhase, settings.jitterPeriod - 1u);
        settings.jitterScale = SanitizeFloat(settings.jitterScale,
            defaults.jitterScale, 0.0f, 1.0f);

        settings.motionResponseStartPixels = SanitizeFloat(
            settings.motionResponseStartPixels, defaults.motionResponseStartPixels,
            0.0f, 256.0f);
        settings.motionResponseEndPixels = SanitizeFloat(
            settings.motionResponseEndPixels, defaults.motionResponseEndPixels,
            0.0f, 256.0f);
        if (settings.motionResponseEndPixels < settings.motionResponseStartPixels)
        {
            std::swap(settings.motionResponseStartPixels,
                settings.motionResponseEndPixels);
        }

        settings.absoluteDepthThreshold = SanitizeFloat(
            settings.absoluteDepthThreshold, defaults.absoluteDepthThreshold,
            0.0f, 1000.0f);
        settings.relativeDepthThreshold = SanitizeFloat(
            settings.relativeDepthThreshold, defaults.relativeDepthThreshold,
            0.0f, 1.0f);
        settings.normalRejectCosine = SanitizeFloat(
            settings.normalRejectCosine, defaults.normalRejectCosine, -1.0f, 1.0f);
        settings.normalAcceptCosine = SanitizeFloat(
            settings.normalAcceptCosine, defaults.normalAcceptCosine, -1.0f, 1.0f);
        if (settings.normalAcceptCosine < settings.normalRejectCosine)
            std::swap(settings.normalAcceptCosine, settings.normalRejectCosine);

        settings.automaticReactiveStrength = SanitizeFloat(
            settings.automaticReactiveStrength, defaults.automaticReactiveStrength,
            0.0f, 4.0f);
        settings.reactiveLuminanceThreshold = SanitizeFloat(
            settings.reactiveLuminanceThreshold, defaults.reactiveLuminanceThreshold,
            0.0f, 16.0f);
        settings.reactiveChromaThreshold = SanitizeFloat(
            settings.reactiveChromaThreshold, defaults.reactiveChromaThreshold,
            0.0f, 16.0f);

        settings.thinGeometryDepthRange = SanitizeFloat(
            settings.thinGeometryDepthRange, defaults.thinGeometryDepthRange,
            0.0f, 1.0f);
        settings.thinGeometryContrastThreshold = SanitizeFloat(
            settings.thinGeometryContrastThreshold,
            defaults.thinGeometryContrastThreshold, 0.0f, 16.0f);
        settings.thinGeometryCoverageResponseMs = SanitizeFloat(
            settings.thinGeometryCoverageResponseMs,
            defaults.thinGeometryCoverageResponseMs, 0.1f, 10000.0f);
        settings.thinGeometryMaximumRelaxation = SanitizeFloat(
            settings.thinGeometryMaximumRelaxation,
            defaults.thinGeometryMaximumRelaxation, 0.0f, 0.25f);

        settings.varianceClipSigma = SanitizeFloat(
            settings.varianceClipSigma, defaults.varianceClipSigma, 0.0f, 8.0f);
        settings.luminanceClipStrength = SanitizeFloat(
            settings.luminanceClipStrength, defaults.luminanceClipStrength,
            0.0f, 4.0f);
        settings.chromaClipStrength = SanitizeFloat(
            settings.chromaClipStrength, defaults.chromaClipStrength, 0.0f, 4.0f);
        settings.thinGeometryClipExpansion = SanitizeFloat(
            settings.thinGeometryClipExpansion, defaults.thinGeometryClipExpansion,
            0.0f, 0.25f);

        settings.stableResponseMs = SanitizeFloat(
            settings.stableResponseMs, defaults.stableResponseMs, 0.1f, 10000.0f);
        settings.movingResponseMs = SanitizeFloat(
            settings.movingResponseMs, defaults.movingResponseMs, 0.1f, 10000.0f);
        settings.reactiveResponseMs = SanitizeFloat(
            settings.reactiveResponseMs, defaults.reactiveResponseMs, 0.1f, 10000.0f);
        settings.maximumHistorySamples = std::clamp(
            settings.maximumHistorySamples, 1u, 255u);
        settings.maximumMovingHistorySamples = std::clamp(
            settings.maximumMovingHistorySamples, 1u, settings.maximumHistorySamples);

        settings.spatialFallbackRadius = std::clamp(
            settings.spatialFallbackRadius, 1u, 2u);
        settings.spatialDepthWeight = SanitizeFloat(
            settings.spatialDepthWeight, defaults.spatialDepthWeight, 0.0f, 1024.0f);
        settings.spatialNormalWeight = SanitizeFloat(
            settings.spatialNormalWeight, defaults.spatialNormalWeight, 0.0f, 1024.0f);
        settings.spatialLuminanceWeight = SanitizeFloat(
            settings.spatialLuminanceWeight, defaults.spatialLuminanceWeight,
            0.0f, 1024.0f);

        settings.persistentFrameInterval = std::clamp(
            settings.persistentFrameInterval, 1u, 2u);
        if (settings.resurrectionEnabled)
        {
            settings.persistentFrameCount = std::clamp(
                settings.persistentFrameCount, 1u, 2u);
        }
        else
        {
            // This invariant lets the resource owner use the count directly;
            // Medium and Light therefore allocate no persistent histories.
            settings.persistentFrameCount = 0;
        }
        settings.maximumResurrectionWeight = SanitizeFloat(
            settings.maximumResurrectionWeight,
            defaults.maximumResurrectionWeight, 0.0f, 0.5f);
        settings.resurrectionMatchThreshold = SanitizeFloat(
            settings.resurrectionMatchThreshold,
            defaults.resurrectionMatchThreshold, 0.0f, 1.0f);

        settings.sharpeningStrength = SanitizeFloat(
            settings.sharpeningStrength, defaults.sharpeningStrength, 0.0f, 1.0f);
        settings.sharpeningMotionSuppression = SanitizeFloat(
            settings.sharpeningMotionSuppression,
            defaults.sharpeningMotionSuppression, 0.0f, 1.0f);
        settings.sharpeningReactiveSuppression = SanitizeFloat(
            settings.sharpeningReactiveSuppression,
            defaults.sharpeningReactiveSuppression, 0.0f, 1.0f);
        settings.sharpeningVarianceSuppression = SanitizeFloat(
            settings.sharpeningVarianceSuppression,
            defaults.sharpeningVarianceSuppression, 0.0f, 1.0f);
        settings.sharpeningHaloClamp = SanitizeFloat(
            settings.sharpeningHaloClamp, defaults.sharpeningHaloClamp, 0.0f, 1.0f);
    }

    ReconstructiveTemporalWeights ComputeReconstructiveTemporalWeights(
        const ReconstructiveTemporalAASettings& settings,
        float deltaSeconds)
    {
        ReconstructiveTemporalAASettings sanitized = settings;
        SanitizeReconstructiveTemporalSettings(sanitized);

        ReconstructiveTemporalWeights weights;
        weights.stableCurrentWeight = CurrentWeightFromTimeConstant(
            deltaSeconds, sanitized.stableResponseMs);
        weights.movingCurrentWeight = CurrentWeightFromTimeConstant(
            deltaSeconds, sanitized.movingResponseMs);
        weights.reactiveCurrentWeight = CurrentWeightFromTimeConstant(
            deltaSeconds, sanitized.reactiveResponseMs);
        return weights;
    }

    ReconstructiveTemporalJitter GenerateReconstructiveTemporalJitter(
        const ReconstructiveTemporalAASettings& settings,
        uint64_t frameIndex)
    {
        ReconstructiveTemporalAASettings sanitized = settings;
        SanitizeReconstructiveTemporalSettings(sanitized);

        // Freeze is intentionally zero jitter, not merely a frozen nonzero
        // sample. This gives developers an unambiguous reconstruction baseline.
        if (sanitized.freezeJitter || sanitized.jitterScale == 0.0f)
            return {};

        const uint32_t phase = sanitized.holdJitterPhase
            ? sanitized.heldJitterPhase
            : static_cast<uint32_t>(
                frameIndex % static_cast<uint64_t>(sanitized.jitterPeriod));
        double x = 0.0;
        double y = 0.0;

        if (sanitized.jitterSequence == JitterSequence::Halton23)
        {
            // Halton index zero is the origin, so use one-based indices and
            // center the [0,1) radical inverses around the pixel center.
            const uint32_t sequenceIndex = phase + 1u;
            x = Halton(sequenceIndex, 2u) - 0.5;
            y = Halton(sequenceIndex, 3u) - 0.5;
        }
        else
        {
            // Additive recurrence using the generalized golden ratio (plastic
            // constant). A half-domain seed avoids beginning at a corner.
            constexpr double PlasticConstant = 1.3247179572447458;
            constexpr double AlphaX = 1.0 / PlasticConstant;
            constexpr double AlphaY = 1.0 / (PlasticConstant * PlasticConstant);
            const double sequenceIndex = static_cast<double>(phase + 1u);
            x = Fraction(0.5 + AlphaX * sequenceIndex) - 0.5;
            y = Fraction(0.5 + AlphaY * sequenceIndex) - 0.5;
        }

        const double scale = static_cast<double>(sanitized.jitterScale);
        ReconstructiveTemporalJitter jitter;
        jitter.x = static_cast<float>(std::clamp(x * scale, -0.5, 0.5));
        jitter.y = static_cast<float>(std::clamp(y * scale, -0.5, 0.5));
        return jitter;
    }

    const char* GetReconstructiveTemporalPresetName(
        ReconstructiveTemporalPreset preset) noexcept
    {
        switch (preset)
        {
        case ReconstructiveTemporalPreset::HeavyTemporal: return "Heavy Temporal";
        case ReconstructiveTemporalPreset::MediumTemporal: return "Medium Temporal";
        case ReconstructiveTemporalPreset::LightTemporal: return "Light Temporal";
        case ReconstructiveTemporalPreset::Custom: return "Custom";
        }
        return "Unknown";
    }

    const char* GetJitterSequenceName(JitterSequence sequence) noexcept
    {
        switch (sequence)
        {
        case JitterSequence::R2: return "R2";
        case JitterSequence::Halton23: return "Halton 2/3";
        }
        return "Unknown";
    }

    const char* GetHistorySampleFilterName(HistorySampleFilter filter) noexcept
    {
        switch (filter)
        {
        case HistorySampleFilter::Bilinear: return "Bilinear";
        case HistorySampleFilter::CatmullRom: return "Catmull-Rom";
        }
        return "Unknown";
    }

    const char* GetReconstructiveTemporalDebugModeName(
        ReconstructiveTemporalDebugMode mode) noexcept
    {
        switch (mode)
        {
        case ReconstructiveTemporalDebugMode::FinalOutput: return "Final Output";
        case ReconstructiveTemporalDebugMode::CurrentJitter: return "Current Jitter";
        case ReconstructiveTemporalDebugMode::MotionVectors: return "Motion Vectors";
        case ReconstructiveTemporalDebugMode::DilatedMotionVectors: return "Dilated Motion Vectors";
        case ReconstructiveTemporalDebugMode::VelocitySourcePixel: return "Velocity Source Pixel";
        case ReconstructiveTemporalDebugMode::VelocityConfidence: return "Velocity Confidence";
        case ReconstructiveTemporalDebugMode::ReprojectedHistory: return "Reprojected History";
        case ReconstructiveTemporalDebugMode::PreviousHistoryUV: return "Previous-History UV";
        case ReconstructiveTemporalDebugMode::CurrentDepth: return "Current Depth";
        case ReconstructiveTemporalDebugMode::ReprojectedPreviousDepth: return "Reprojected Previous Depth";
        case ReconstructiveTemporalDebugMode::DepthConfidence: return "Depth Confidence";
        case ReconstructiveTemporalDebugMode::NormalConfidence: return "Normal Confidence";
        case ReconstructiveTemporalDebugMode::MaterialMatch: return "Material Match";
        case ReconstructiveTemporalDebugMode::ObjectMatch: return "Object Match";
        case ReconstructiveTemporalDebugMode::CombinedHistoryConfidence: return "Combined History Confidence";
        case ReconstructiveTemporalDebugMode::ExplicitReactiveMask: return "Explicit Reactive Mask";
        case ReconstructiveTemporalDebugMode::AutomaticReactiveMask: return "Automatic Reactive Mask";
        case ReconstructiveTemporalDebugMode::FinalReactiveValue: return "Final Reactive Value";
        case ReconstructiveTemporalDebugMode::ThinGeometryClassification: return "Thin-Geometry Classification";
        case ReconstructiveTemporalDebugMode::ThinGeometryAccumulatedCoverage: return "Thin-Geometry Accumulated Coverage";
        case ReconstructiveTemporalDebugMode::Variance: return "Variance";
        case ReconstructiveTemporalDebugMode::ClippingBounds: return "Clipping Bounds";
        case ReconstructiveTemporalDebugMode::UnclippedHistory: return "Unclipped History";
        case ReconstructiveTemporalDebugMode::ClippedHistory: return "Clipped History";
        case ReconstructiveTemporalDebugMode::CurrentFrameWeight: return "Current-Frame Weight";
        case ReconstructiveTemporalDebugMode::HistorySampleCount: return "History Sample Count";
        case ReconstructiveTemporalDebugMode::SpatialFallbackContribution: return "Spatial Fallback Contribution";
        case ReconstructiveTemporalDebugMode::ResurrectionEligibility: return "Resurrection Eligibility";
        case ReconstructiveTemporalDebugMode::ResurrectionSource: return "Resurrection Source";
        case ReconstructiveTemporalDebugMode::SharpeningContribution: return "Sharpening Contribution";
        case ReconstructiveTemporalDebugMode::RejectionReasons: return "Rejection Reasons";
        case ReconstructiveTemporalDebugMode::FinalNraRtaaOutput: return "Final NRA-RTAA Output";
        }
        return "Unknown";
    }
}
