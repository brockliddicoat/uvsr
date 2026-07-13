#pragma once

#include <cstdint>

namespace uvsr
{
    // Explicit values are part of the settings/debug contract. Keep them stable
    // when adding UI labels, serialized settings, or shader-side debug modes.
    enum class ReconstructiveTemporalPreset : uint32_t
    {
        HeavyTemporal = 0,
        MediumTemporal = 1,
        LightTemporal = 2,
        Custom = 3
    };

    // Temporal presets describe the desired response. This independent profile
    // selects statically compiled implementation cost, so clarity/stability can
    // be tuned without forcing an expensive path on low-throughput GPUs.
    enum class ReconstructiveTemporalPerformanceProfile : uint32_t
    {
        Performance = 0,
        Balanced = 1,
        MaximumQuality = 2
    };

    enum class JitterSequence : uint32_t
    {
        R2 = 0,
        Halton23 = 1
    };

    enum class HistorySampleFilter : uint32_t
    {
        Bilinear = 0,
        CatmullRom = 1
    };

    enum class ReconstructiveTemporalDebugMode : uint32_t
    {
        FinalOutput = 0,
        CurrentJitter = 1,
        MotionVectors = 2,
        DilatedMotionVectors = 3,
        VelocitySourcePixel = 4,
        VelocityConfidence = 5,
        ReprojectedHistory = 6,
        PreviousHistoryUV = 7,
        CurrentDepth = 8,
        ReprojectedPreviousDepth = 9,
        DepthConfidence = 10,
        NormalConfidence = 11,
        MaterialMatch = 12,
        ObjectMatch = 13,
        CombinedHistoryConfidence = 14,
        ExplicitReactiveMask = 15,
        AutomaticReactiveMask = 16,
        FinalReactiveValue = 17,
        ThinGeometryClassification = 18,
        ThinGeometryAccumulatedCoverage = 19,
        Variance = 20,
        ClippingBounds = 21,
        UnclippedHistory = 22,
        ClippedHistory = 23,
        CurrentFrameWeight = 24,
        HistorySampleCount = 25,
        SpatialFallbackContribution = 26,
        ResurrectionEligibility = 27,
        ResurrectionSource = 28,
        SharpeningContribution = 29,
        RejectionReasons = 30,
        FinalNraRtaaOutput = 31
    };

    constexpr uint32_t ReconstructiveTemporalDebugModeCount = 32;

    struct ReconstructiveTemporalAASettings
    {
        bool enabled = true;
        ReconstructiveTemporalPreset preset = ReconstructiveTemporalPreset::MediumTemporal;
        ReconstructiveTemporalPerformanceProfile performanceProfile =
            ReconstructiveTemporalPerformanceProfile::Balanced;

        // Projection jitter. jitterScale is measured in pixels and is bounded
        // to one so the generated offset never exceeds half a pixel per axis.
        JitterSequence jitterSequence = JitterSequence::R2;
        uint32_t jitterPeriod = 8;
        float jitterScale = 1.0f;

        // Motion and reprojection.
        HistorySampleFilter historyFilter = HistorySampleFilter::CatmullRom;
        bool velocityDilationEnabled = true;
        float motionResponseStartPixels = 0.5f;
        float motionResponseEndPixels = 8.0f;

        // Surface validation.
        float absoluteDepthThreshold = 0.01f;
        float relativeDepthThreshold = 0.02f;
        float normalRejectCosine = 0.75f;
        float normalAcceptCosine = 0.95f;
        bool validateMaterialIdentity = true;
        bool validateObjectIdentity = false;

        // Reactive shading. The explicit target is reserved for a producer
        // with concrete transient/unreliable transport; the current opaque PBR
        // producer writes neutral values. The analytical estimate is motion-
        // corroborated so stationary subpixel phases remain accumulatable.
        bool explicitReactiveMaskEnabled = true;
        bool automaticReactiveMaskEnabled = true;
        float automaticReactiveStrength = 0.80f;
        float reactiveLuminanceThreshold = 0.10f;
        float reactiveChromaThreshold = 0.10f;

        // Thin geometry and temporally accumulated coverage.
        bool thinGeometryEnabled = true;
        float thinGeometryDepthRange = 0.02f;
        float thinGeometryContrastThreshold = 0.15f;
        float thinGeometryCoverageResponseMs = 80.0f;
        float thinGeometryMaximumRelaxation = 0.025f;
        bool thinGeometryClusterDiffusion = true;

        // Variance/neighborhood clipping.
        float varianceClipSigma = 1.25f;
        float luminanceClipStrength = 1.0f;
        float chromaClipStrength = 1.0f;
        float thinGeometryClipExpansion = 0.025f;

        // Frame-rate-independent temporal accumulation.
        float stableResponseMs = 125.0f;
        float movingResponseMs = 50.0f;
        float reactiveResponseMs = 8.0f;
        uint32_t maximumHistorySamples = 12;
        uint32_t maximumMovingHistorySamples = 5;

        // Edge-aware current-frame fallback.
        bool spatialFallbackEnabled = true;
        uint32_t spatialFallbackRadius = 1;
        float spatialDepthWeight = 64.0f;
        float spatialNormalWeight = 16.0f;
        float spatialLuminanceWeight = 4.0f;

        // Optional persistent history. Disabled presets keep the count at zero,
        // which is also the resource-allocation invariant for Medium and Light.
        bool resurrectionEnabled = false;
        uint32_t persistentFrameCount = 0;
        uint32_t persistentFrameInterval = 1;
        float maximumResurrectionWeight = 0.20f;
        float resurrectionMatchThreshold = 0.70f;

        // Conservative post-accumulation sharpening.
        bool sharpeningEnabled = true;
        float sharpeningStrength = 0.18f;
        float sharpeningMotionSuppression = 0.75f;
        float sharpeningReactiveSuppression = 0.90f;
        float sharpeningVarianceSuppression = 0.75f;
        float sharpeningHaloClamp = 0.10f;

        // Developer/debug controls are deliberately not overwritten when a
        // quality preset is selected.
        bool freezeJitter = false;
        // Holding a phase is distinct from freezeJitter: freezeJitter forces
        // an exact zero-offset baseline, while this pair repeats one real
        // low-discrepancy sample for phase/debug inspection.
        bool holdJitterPhase = false;
        uint32_t heldJitterPhase = 0;
        bool forceHistoryReset = false;
        ReconstructiveTemporalDebugMode debugMode =
            ReconstructiveTemporalDebugMode::FinalOutput;
    };

    struct ReconstructiveTemporalWeights
    {
        float stableCurrentWeight = 1.0f;
        float movingCurrentWeight = 1.0f;
        float reactiveCurrentWeight = 1.0f;
    };

    struct ReconstructiveTemporalJitter
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    void ApplyReconstructiveTemporalPreset(
        ReconstructiveTemporalAASettings& settings,
        ReconstructiveTemporalPreset preset);

    void SanitizeReconstructiveTemporalSettings(
        ReconstructiveTemporalAASettings& settings);

    [[nodiscard]] ReconstructiveTemporalWeights ComputeReconstructiveTemporalWeights(
        const ReconstructiveTemporalAASettings& settings,
        float deltaSeconds);

    [[nodiscard]] ReconstructiveTemporalJitter GenerateReconstructiveTemporalJitter(
        const ReconstructiveTemporalAASettings& settings,
        uint64_t frameIndex);

    [[nodiscard]] const char* GetReconstructiveTemporalPresetName(
        ReconstructiveTemporalPreset preset) noexcept;
    [[nodiscard]] const char* GetReconstructiveTemporalPerformanceProfileName(
        ReconstructiveTemporalPerformanceProfile profile) noexcept;
    [[nodiscard]] const char* GetJitterSequenceName(JitterSequence sequence) noexcept;
    [[nodiscard]] const char* GetHistorySampleFilterName(
        HistorySampleFilter filter) noexcept;
    [[nodiscard]] const char* GetReconstructiveTemporalDebugModeName(
        ReconstructiveTemporalDebugMode mode) noexcept;
}
