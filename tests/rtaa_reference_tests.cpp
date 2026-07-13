#include "reconstructive_temporal_aa_settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <string>

using namespace uvsr;

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "NRA-RTAA reference validation failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    void RequireNear(
        float actual,
        float expected,
        const std::string& message,
        float tolerance = 1e-6f)
    {
        if (std::isfinite(actual) && std::abs(actual - expected) <= tolerance)
            return;

        Fail(message + " (expected " + std::to_string(expected) +
            ", got " + std::to_string(actual) + ")");
    }

    constexpr float ReferenceEpsilon = 1e-6f;

    float Saturate(float value)
    {
        return std::clamp(value, 0.0f, 1.0f);
    }

    float Lerp(float first, float second, float amount)
    {
        return first + (second - first) * amount;
    }

    float SmoothRange(float value, float lower, float upper)
    {
        return Saturate((value - lower) /
            std::max(upper - lower, ReferenceEpsilon));
    }

    struct Float3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Float2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    Float2 operator+(const Float2& first, const Float2& second)
    {
        return { first.x + second.x, first.y + second.y };
    }

    Float2 operator-(const Float2& first, const Float2& second)
    {
        return { first.x - second.x, first.y - second.y };
    }

    float Length(const Float2& value)
    {
        return std::sqrt(value.x * value.x + value.y * value.y);
    }

    Float3 operator+(const Float3& first, const Float3& second)
    {
        return { first.x + second.x, first.y + second.y,
            first.z + second.z };
    }

    Float3 operator-(const Float3& first, const Float3& second)
    {
        return { first.x - second.x, first.y - second.y,
            first.z - second.z };
    }

    Float3 operator*(const Float3& value, float scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    float Dot(const Float3& first, const Float3& second)
    {
        return first.x * second.x + first.y * second.y +
            first.z * second.z;
    }

    Float3 Cross(const Float3& first, const Float3& second)
    {
        return {
            first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x
        };
    }

    float Length(const Float3& value)
    {
        return std::sqrt(std::max(Dot(value, value), 0.0f));
    }

    bool IsFinite(const Float3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    bool IsNonnegative(const Float3& value)
    {
        return value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f;
    }

    Float3 SanitizeHdr(const Float3& color)
    {
        if (!IsFinite(color))
            return {};
        return {
            std::max(color.x, 0.0f),
            std::max(color.y, 0.0f),
            std::max(color.z, 0.0f)
        };
    }

    Float3 RgbToCompressedYCoCg(const Float3& input)
    {
        const Float3 rgb = SanitizeHdr(input);
        const float luminance = 0.25f * rgb.x + 0.50f * rgb.y +
            0.25f * rgb.z;
        const float compression = 1.0f / (1.0f + luminance);
        return {
            std::log2(1.0f + luminance),
            0.5f * (rgb.x - rgb.z) * compression,
            (-0.25f * rgb.x + 0.5f * rgb.y - 0.25f * rgb.z) *
                compression
        };
    }

    Float3 CompressedYCoCgToRgb(const Float3& value)
    {
        const float luminance = std::max(std::exp2(value.x) - 1.0f, 0.0f);
        const float chromaScale = 1.0f + luminance;
        const float orange = value.y * chromaScale;
        const float green = value.z * chromaScale;
        return SanitizeHdr({
            luminance + orange - green,
            luminance + green,
            luminance - orange - green
        });
    }

    Float3 ClipLineToBox(
        const Float3& history,
        const Float3& center,
        const Float3& lowerBound,
        const Float3& upperBound)
    {
        const Float3 delta = history - center;
        float scale = 1.0f;
        const std::array<float, 3> differences = {
            delta.x, delta.y, delta.z
        };
        const std::array<float, 3> lower = {
            lowerBound.x, lowerBound.y, lowerBound.z
        };
        const std::array<float, 3> upper = {
            upperBound.x, upperBound.y, upperBound.z
        };
        const std::array<float, 3> centerValues = {
            center.x, center.y, center.z
        };

        for (size_t component = 0; component < differences.size(); ++component)
        {
            if (differences[component] > ReferenceEpsilon)
            {
                scale = std::min(scale,
                    (upper[component] - centerValues[component]) /
                    differences[component]);
            }
            else if (differences[component] < -ReferenceEpsilon)
            {
                scale = std::min(scale,
                    (lower[component] - centerValues[component]) /
                    differences[component]);
            }
        }

        return center + delta * Saturate(scale);
    }

    float ReferenceNeighborhoodReactive(
        const Float3& unclippedHistory,
        const Float3& historyInsideBroadNeighborhood,
        float lumaThreshold,
        float chromaThreshold,
        float strength)
    {
        const Float3 residual = unclippedHistory -
            historyInsideBroadNeighborhood;
        const float lumaReactive = SmoothRange(std::abs(residual.x),
            lumaThreshold, lumaThreshold * 2.0f);
        const float chromaReactive = SmoothRange(std::sqrt(
            residual.y * residual.y + residual.z * residual.z),
            chromaThreshold, chromaThreshold * 2.0f);
        return Saturate(std::max(lumaReactive, chromaReactive) *
            std::max(strength, 0.0f));
    }

    float ReferenceMotionGatedAutomaticReactive(
        const Float3& unclippedHistory,
        const Float3& historyInsideBroadNeighborhood,
        float lumaThreshold,
        float chromaThreshold,
        float strength,
        float motionFactor)
    {
        return ReferenceNeighborhoodReactive(unclippedHistory,
            historyInsideBroadNeighborhood, lumaThreshold,
            chromaThreshold, strength) * Saturate(motionFactor);
    }

    struct HistoryCoordinates
    {
        Float2 resolvedColor;
        Float2 rawAuxiliary;
    };

    HistoryCoordinates ImmediateHistoryCoordinates(
        Float2 currentPixelCenter,
        Float2 deJitteredMotion,
        Float2 currentJitter,
        Float2 historyJitter)
    {
        const Float2 resolvedColor = currentPixelCenter + deJitteredMotion;
        return { resolvedColor,
            resolvedColor - currentJitter + historyJitter };
    }

    HistoryCoordinates PersistentHistoryCoordinates(
        Float2 unjitteredHistoryProjection,
        Float2 currentJitter,
        Float2 historyJitter)
    {
        return { unjitteredHistoryProjection + currentJitter,
            unjitteredHistoryProjection + historyJitter };
    }

    bool ReferenceStaticSurfaceForResurrection(
        Float2 actualMotion,
        Float2 cameraOnlyMotion,
        float depthAgreement)
    {
        return Length(actualMotion - cameraOnlyMotion) <= 0.125f &&
            depthAgreement > 0.5f;
    }

    struct VelocitySample
    {
        float depth = 0.0f;
        float motionX = 0.0f;
        float motionY = 0.0f;
    };

    struct VelocityOwner
    {
        bool valid = false;
        uint32_t sampleIndex = 4;
        VelocitySample sample;
    };

    bool IsValidReverseDepth(float depth)
    {
        return std::isfinite(depth) && depth > 0.0f && depth <= 1.0f;
    }

    bool IsValidDeviceDepth(float depth, bool reverseZ)
    {
        if (!std::isfinite(depth))
            return false;
        return reverseZ
            ? depth > 0.0f && depth <= 1.0f
            : depth >= 0.0f && depth < 1.0f;
    }

    float ClearDeviceDepth(bool reverseZ)
    {
        return reverseZ ? 0.0f : 1.0f;
    }

    float ReferencePreparedReactive(
        bool centerDepthValid,
        float explicitReactive,
        bool /* staticMaterialFeaturePresent */)
    {
        return centerDepthValid ? Saturate(explicitReactive) : 0.0f;
    }

    float ReferenceAuthoredThin(
        bool centerDepthValid,
        float diffuseAlpha,
        bool thinFeaturePresent)
    {
        if (!centerDepthValid)
            return 0.0f;
        const float featureThin = thinFeaturePresent ? 1.0f : 0.0f;
        const float alphaThin = Saturate((1.0f -
            Saturate(diffuseAlpha)) * 2.0f);
        return std::max(featureThin, alphaThin);
    }

    VelocityOwner SelectNearestReverseZVelocity(
        const std::array<VelocitySample, 9>& neighborhood)
    {
        VelocityOwner owner;
        owner.sample = neighborhood[4];
        owner.valid = IsValidReverseDepth(owner.sample.depth);
        if (owner.valid)
            return owner;

        for (uint32_t index = 0; index < neighborhood.size(); ++index)
        {
            const VelocitySample& candidate = neighborhood[index];
            if (!IsValidReverseDepth(candidate.depth))
                continue;
            if (!owner.valid || candidate.depth > owner.sample.depth)
            {
                owner.valid = true;
                owner.sampleIndex = index;
                owner.sample = candidate;
            }
        }
        return owner;
    }

    float ReferenceDepthConfidence(
        float actualViewDepth,
        float expectedViewDepth,
        float absoluteThreshold,
        float relativeThreshold,
        float toleranceScale = 1.0f)
    {
        if (!std::isfinite(actualViewDepth) ||
            !std::isfinite(expectedViewDepth) ||
            actualViewDepth <= 0.0f || expectedViewDepth <= 0.0f)
        {
            return 0.0f;
        }

        const float threshold = std::max(absoluteThreshold +
            relativeThreshold * std::max(actualViewDepth, expectedViewDepth),
            ReferenceEpsilon) * std::clamp(toleranceScale, 1.0f, 2.0f);
        const float error = std::abs(actualViewDepth - expectedViewDepth);
        return 1.0f - SmoothRange(error, threshold, threshold * 2.0f);
    }

    float ReferenceNormalConfidence(
        float normalDot,
        float rejectCosine,
        float acceptCosine)
    {
        return SmoothRange(normalDot, rejectCosine, acceptCosine);
    }

    float ReferenceGeometricConfidence(
        bool historyValid,
        float velocityConfidence,
        float depthConfidence,
        float normalConfidence,
        bool materialMatch,
        bool objectMatch,
        float /* previousMetadataConfidence */)
    {
        if (!historyValid)
            return 0.0f;

        // The previous metadata confidence is diagnostic state, not a factor.
        // Multiplying it here would make reset's zero confidence a fixed point.
        return Saturate(velocityConfidence * depthConfidence *
            normalConfidence * (materialMatch ? 1.0f : 0.0f) *
            (objectMatch ? 1.0f : 0.0f));
    }

    bool ReferenceMaterialMatch(
        bool validationEnabled,
        uint32_t currentHash,
        uint32_t previousHash)
    {
        return !validationEnabled || currentHash == previousHash;
    }

    struct AccumulationStep
    {
        float currentWeight = 1.0f;
        float historyCount = 1.0f;
        float maximumSamples = 1.0f;
    };

    AccumulationStep StepAccumulation(
        const ReconstructiveTemporalAASettings& settings,
        float deltaSeconds,
        float motionFactor,
        float reactive,
        float historyConfidence,
        float historyCount)
    {
        const ReconstructiveTemporalWeights weights =
            ComputeReconstructiveTemporalWeights(settings, deltaSeconds);
        motionFactor = Saturate(motionFactor);
        reactive = Saturate(reactive);
        historyConfidence = Saturate(historyConfidence);

        float baseCurrentWeight = Lerp(weights.stableCurrentWeight,
            weights.movingCurrentWeight, motionFactor);
        baseCurrentWeight = Lerp(baseCurrentWeight,
            weights.reactiveCurrentWeight, reactive);

        const float maximumSamples = std::max(1.0f, std::round(Lerp(
            static_cast<float>(std::max(settings.maximumHistorySamples, 1u)),
            static_cast<float>(std::max(
                settings.maximumMovingHistorySamples, 1u)),
            motionFactor)));
        const float sampleLimitedWeight = 1.0f /
            (std::min(historyCount, maximumSamples) + 1.0f);
        float currentWeight = std::max(Saturate(baseCurrentWeight),
            sampleLimitedWeight);
        currentWeight = Lerp(currentWeight, 1.0f,
            1.0f - historyConfidence);

        const bool historyAccepted = historyConfidence > 0.05f &&
            currentWeight < 0.999f;
        float newHistoryCount = historyAccepted
            ? std::min(historyCount + 1.0f, maximumSamples)
            : 1.0f;
        newHistoryCount = Lerp(newHistoryCount, 1.0f,
            Saturate(reactive * 0.5f));
        return { currentWeight, newHistoryCount, maximumSamples };
    }

    struct CoverageAccumulationStep
    {
        float color = 0.0f;
        float currentWeight = 1.0f;
        float historyCount = 1.0f;
        float reactive = 0.0f;
    };

    CoverageAccumulationStep StepCoverageAccumulation(
        const ReconstructiveTemporalAASettings& settings,
        float deltaSeconds,
        float historyColor,
        float currentColor,
        float historyCount,
        float historyConfidence,
        float stableCoverage)
    {
        const Float3 black = RgbToCompressedYCoCg({ 0.0f, 0.0f, 0.0f });
        const Float3 white = RgbToCompressedYCoCg({ 1.0f, 1.0f, 1.0f });
        const Float3 history = RgbToCompressedYCoCg(
            { historyColor, historyColor, historyColor });
        const Float3 broadCenter = (black + white) * 0.5f;
        const Float3 broadHistory = ClipLineToBox(
            history, broadCenter, black, white);
        const float reactive = ReferenceNeighborhoodReactive(
            history, broadHistory, settings.reactiveLuminanceThreshold,
            settings.reactiveChromaThreshold,
            settings.automaticReactiveStrength);

        const ReconstructiveTemporalWeights weights =
            ComputeReconstructiveTemporalWeights(settings, deltaSeconds);
        const float maximumSamples = static_cast<float>(
            std::max(settings.maximumHistorySamples, 1u));
        const float sampleLimitedWeight = 1.0f /
            (std::min(historyCount, maximumSamples) + 1.0f);
        float currentWeight = std::max(
            weights.stableCurrentWeight, sampleLimitedWeight);

        currentWeight = Lerp(currentWeight, 1.0f,
            1.0f - Saturate(historyConfidence));
        // Verified stationary coverage selects the count-driven weight after
        // the confidence safety gate so foreground/background phases cannot be
        // biased by their necessarily asymmetric validation confidence.
        currentWeight = Lerp(currentWeight, sampleLimitedWeight,
            Saturate(stableCoverage));
        const float outputColor = Lerp(
            historyColor, currentColor, currentWeight);
        const bool historyAccepted = historyConfidence > 0.05f &&
            currentWeight < 0.999f;
        const float newHistoryCount = historyAccepted
            ? std::min(historyCount + 1.0f, maximumSamples) : 1.0f;
        return { outputColor, currentWeight, newHistoryCount, reactive };
    }

    float ReferenceCoverageOverrideSignal(
        bool coverageTransition,
        float currentEdge,
        bool borrowedSource,
        float previousCoverageLock,
        float currentNeighborMatch,
        float stationaryMotionGate)
    {
        const float candidate = std::max({ currentEdge,
            borrowedSource ? 1.0f : 0.0f, previousCoverageLock });
        const float support = std::max(currentNeighborMatch,
            previousCoverageLock);
        return (coverageTransition ? 1.0f : 0.0f) * candidate * support *
            Saturate(stationaryMotionGate);
    }

    float ReferenceUncoveredCoverageHistory(
        float clippedHistory,
        float unclippedHistory,
        bool currentDepthValid,
        bool previousCoverageLock,
        float stationaryMotionGate,
        float reactive)
    {
        const float preservation = currentDepthValid ? 0.0f :
            (previousCoverageLock ? 1.0f : 0.0f) * stationaryMotionGate *
            (1.0f - Saturate(reactive));
        return Lerp(clippedHistory, unclippedHistory,
            Saturate(preservation));
    }

    float PackReactiveAndMotion(float reactive, float motionFactor)
    {
        const uint32_t reactiveBits = static_cast<uint32_t>(
            std::round(Saturate(reactive) * 15.0f));
        const uint32_t motionBits = static_cast<uint32_t>(
            std::round(Saturate(motionFactor) * 15.0f));
        return static_cast<float>((motionBits << 4u) | reactiveBits) / 255.0f;
    }

    float UnpackReactive(float packed)
    {
        const uint32_t bits = static_cast<uint32_t>(
            std::round(Saturate(packed) * 255.0f));
        return static_cast<float>(bits & 15u) / 15.0f;
    }

    float UnpackMotion(float packed)
    {
        const uint32_t bits = static_cast<uint32_t>(
            std::round(Saturate(packed) * 255.0f));
        return static_cast<float>((bits >> 4u) & 15u) / 15.0f;
    }

    float QuantizeThinLock(float lifetime)
    {
        return std::round(Saturate(lifetime) * 255.0f) / 255.0f;
    }

    float StepCoverageLockLifetime(
        float previousLifetime,
        bool currentThin,
        uint32_t jitterPeriod,
        bool currentDepthValid = false,
        float geometricConfidence = 0.0f,
        float stationaryMotionGate = 1.0f,
        float reactive = 0.0f)
    {
        const float lockFrames =
            static_cast<float>(std::max(jitterPeriod, 1u)) + 2.0f;
        const float decay = std::ceil(255.0f / lockFrames) / 255.0f;
        const bool canInherit = !currentDepthValid ||
            geometricConfidence > 0.5f;
        const float unquantized = (currentThin ? 1.0f :
            std::max(previousLifetime - decay, 0.0f) *
                (canInherit ? 1.0f : 0.0f)) *
            Saturate(stationaryMotionGate) * (1.0f - Saturate(reactive));
        return QuantizeThinLock(unquantized);
    }

    float InterpolatedStationaryThinLock(
        const std::array<float, 4>& lifetimes,
        const std::array<float, 4>& motionFactors,
        const std::array<float, 4>& bilinearWeights)
    {
        float interpolatedLifetime = 0.0f;
        for (size_t index = 0; index < lifetimes.size(); ++index)
        {
            if (motionFactors[index] < 0.10f)
                interpolatedLifetime +=
                    lifetimes[index] * bilinearWeights[index];
        }
        return interpolatedLifetime > (1.0f / 255.0f)
            ? Saturate(interpolatedLifetime) : 0.0f;
    }

    float ReferenceExpiredCoverageAlpha(
        bool currentDepthValid,
        float previousCoverageLifetime,
        float accumulatedCoverage)
    {
        return !currentDepthValid && previousCoverageLifetime <= 0.0f
            ? 0.0f : accumulatedCoverage;
    }

    float ReferencePreviousCoverageLifetime(
        bool historyValid,
        float pointLifetime,
        float footprintLifetime)
    {
        return historyValid
            ? (pointLifetime > 0.0f ? pointLifetime : footprintLifetime)
            : 0.0f;
    }

    uint32_t ReferenceResolveVariant(
        ReconstructiveTemporalPerformanceProfile profile,
        HistorySampleFilter configuredFilter,
        bool resurrectionEnabled)
    {
        const bool maximumQuality = profile ==
            ReconstructiveTemporalPerformanceProfile::MaximumQuality;
        const bool catmullRom = maximumQuality &&
            configuredFilter == HistorySampleFilter::CatmullRom;
        const bool resurrection = maximumQuality && resurrectionEnabled;
        return (catmullRom ? 1u : 0u) |
            (resurrection ? 2u : 0u) |
            (static_cast<uint32_t>(profile) << 2u);
    }

    float ReferenceFallbackContribution(bool enabled, float historyConfidence)
    {
        if (!enabled)
            return 0.0f;
        // Full fallback below 0.25 fades out completely by 0.75, keeping the
        // spatial path out of pixels that already have useful temporal history.
        return 1.0f - SmoothRange(historyConfidence, 0.25f, 0.75f);
    }

    float ReferenceThinClipExpansion(
        float thinCoverage,
        float maximumExpansion,
        float reactive)
    {
        // Reactive shading continuously suppresses thin relaxation and a fully
        // reactive (severe mismatch) sample turns it off.
        const float shadingGate = 1.0f - Saturate(reactive);
        return Saturate(thinCoverage * maximumExpansion) * shadingGate;
    }

    float ReferenceThinConfidenceRelaxation(
        float historyConfidence,
        float hardGeometricConfidence,
        float thinCoverage,
        float maximumRelaxation,
        float previousConfidence,
        float reactive = 0.0f)
    {
        historyConfidence = Saturate(historyConfidence);
        hardGeometricConfidence = Saturate(hardGeometricConfidence);
        const float thinRelaxation = Saturate(thinCoverage) *
            Saturate(maximumRelaxation) * (1.0f - Saturate(reactive));
        const float boost = hardGeometricConfidence * thinRelaxation *
            Saturate(previousConfidence) * (1.0f - historyConfidence);
        return Saturate(historyConfidence + boost);
    }

    uint32_t DispatchGroupCount(uint32_t dimension)
    {
        constexpr uint32_t ThreadGroupSize = 8;
        return (dimension + ThreadGroupSize - 1u) / ThreadGroupSize;
    }

    bool AllFinite(const ReconstructiveTemporalAASettings& settings)
    {
        const std::array<float, 30> values = {
            settings.jitterScale,
            settings.motionResponseStartPixels,
            settings.motionResponseEndPixels,
            settings.absoluteDepthThreshold,
            settings.relativeDepthThreshold,
            settings.normalRejectCosine,
            settings.normalAcceptCosine,
            settings.automaticReactiveStrength,
            settings.reactiveLuminanceThreshold,
            settings.reactiveChromaThreshold,
            settings.thinGeometryDepthRange,
            settings.thinGeometryContrastThreshold,
            settings.thinGeometryCoverageResponseMs,
            settings.thinGeometryMaximumRelaxation,
            settings.varianceClipSigma,
            settings.luminanceClipStrength,
            settings.chromaClipStrength,
            settings.thinGeometryClipExpansion,
            settings.stableResponseMs,
            settings.movingResponseMs,
            settings.reactiveResponseMs,
            settings.spatialDepthWeight,
            settings.spatialNormalWeight,
            settings.spatialLuminanceWeight,
            settings.maximumResurrectionWeight,
            settings.resurrectionMatchThreshold,
            settings.sharpeningStrength,
            settings.sharpeningMotionSuppression,
            settings.sharpeningReactiveSuppression,
            settings.sharpeningVarianceSuppression
        };

        for (float value : values)
        {
            if (!std::isfinite(value))
                return false;
        }
        return std::isfinite(settings.sharpeningHaloClamp);
    }

    void CheckPreset(
        ReconstructiveTemporalPreset preset,
        uint32_t jitterPeriod,
        float stableResponseMs,
        float movingResponseMs,
        float reactiveResponseMs,
        uint32_t maximumHistorySamples,
        uint32_t maximumMovingHistorySamples,
        float varianceClipSigma,
        float automaticReactiveStrength,
        float thinGeometryMaximumRelaxation,
        uint32_t spatialFallbackRadius,
        bool resurrectionEnabled,
        uint32_t persistentFrameCount,
        float sharpeningStrength)
    {
        ReconstructiveTemporalAASettings settings;
        settings.enabled = false;
        settings.freezeJitter = true;
        settings.holdJitterPhase = true;
        settings.heldJitterPhase = 1;
        settings.forceHistoryReset = true;
        settings.debugMode = ReconstructiveTemporalDebugMode::Variance;
        ApplyReconstructiveTemporalPreset(settings, preset);

        Require(settings.preset == preset, "preset identifier");
        Require(!settings.enabled, "preset preserves enabled state");
        Require(settings.freezeJitter, "preset preserves frozen-jitter state");
        Require(settings.holdJitterPhase && settings.heldJitterPhase == 1,
            "preset preserves held jitter phase state");
        Require(settings.forceHistoryReset, "preset preserves force-reset request");
        Require(settings.debugMode == ReconstructiveTemporalDebugMode::Variance,
            "preset preserves debug mode");
        Require(settings.jitterSequence == JitterSequence::R2, "preset R2 jitter");
        Require(settings.jitterPeriod == jitterPeriod, "preset jitter period");
        Require(settings.historyFilter == HistorySampleFilter::CatmullRom,
            "preset Catmull-Rom history filter");
        Require(settings.explicitReactiveMaskEnabled,
            "preset consumes the optional explicit application reactive mask");
        RequireNear(settings.stableResponseMs, stableResponseMs,
            "preset stable response");
        RequireNear(settings.movingResponseMs, movingResponseMs,
            "preset moving response");
        RequireNear(settings.reactiveResponseMs, reactiveResponseMs,
            "preset reactive response");
        Require(settings.maximumHistorySamples == maximumHistorySamples,
            "preset history sample cap");
        Require(settings.maximumMovingHistorySamples == maximumMovingHistorySamples,
            "preset moving-history sample cap");
        RequireNear(settings.varianceClipSigma, varianceClipSigma,
            "preset variance sigma");
        RequireNear(settings.automaticReactiveStrength, automaticReactiveStrength,
            "preset automatic reactive strength");
        RequireNear(settings.thinGeometryMaximumRelaxation,
            thinGeometryMaximumRelaxation, "preset thin-geometry relaxation");
        Require(settings.spatialFallbackRadius == spatialFallbackRadius,
            "preset spatial fallback radius");
        Require(settings.resurrectionEnabled == resurrectionEnabled,
            "preset resurrection state");
        Require(settings.persistentFrameCount == persistentFrameCount,
            "preset persistent frame count");
        RequireNear(settings.sharpeningStrength, sharpeningStrength,
            "preset sharpening strength");
    }

    void TestPresets()
    {
        const ReconstructiveTemporalAASettings defaults;
        Require(defaults.enabled, "RTAA is enabled by default");
        Require(defaults.preset == ReconstructiveTemporalPreset::MediumTemporal,
            "Medium Temporal is the default preset");

        CheckPreset(
            ReconstructiveTemporalPreset::HeavyTemporal,
            16, 250.0f, 100.0f, 12.0f, 24, 10, 1.60f, 0.60f, 0.040f,
            2, true, 2, 0.08f);
        CheckPreset(
            ReconstructiveTemporalPreset::MediumTemporal,
            8, 125.0f, 50.0f, 8.0f, 12, 5, 1.25f, 0.80f, 0.025f,
            1, false, 0, 0.18f);
        CheckPreset(
            ReconstructiveTemporalPreset::LightTemporal,
            8, 55.0f, 22.0f, 4.0f, 6, 2, 0.95f, 1.00f, 0.012f,
            1, false, 0, 0.30f);

        ReconstructiveTemporalAASettings heavy;
        ApplyReconstructiveTemporalPreset(
            heavy, ReconstructiveTemporalPreset::HeavyTemporal);
        Require(heavy.persistentFrameInterval == 1,
            "Heavy resurrection samples every frame by default");
    }

    void TestPerformanceProfiles()
    {
        static_assert(static_cast<uint32_t>(
            ReconstructiveTemporalPerformanceProfile::Performance) == 0u);
        static_assert(static_cast<uint32_t>(
            ReconstructiveTemporalPerformanceProfile::Balanced) == 1u);
        static_assert(static_cast<uint32_t>(
            ReconstructiveTemporalPerformanceProfile::MaximumQuality) == 2u);

        const ReconstructiveTemporalAASettings defaults;
        Require(defaults.performanceProfile ==
            ReconstructiveTemporalPerformanceProfile::Balanced,
            "Balanced is the default RTAA performance profile");
        Require(std::strcmp(GetReconstructiveTemporalPerformanceProfileName(
            ReconstructiveTemporalPerformanceProfile::Performance),
            "Performance") == 0, "stable Performance profile name");
        Require(std::strcmp(GetReconstructiveTemporalPerformanceProfileName(
            ReconstructiveTemporalPerformanceProfile::Balanced),
            "Balanced") == 0, "stable Balanced profile name");
        Require(std::strcmp(GetReconstructiveTemporalPerformanceProfileName(
            ReconstructiveTemporalPerformanceProfile::MaximumQuality),
            "Maximum Quality") == 0,
            "stable Maximum Quality profile name");
        Require(std::strcmp(GetReconstructiveTemporalPerformanceProfileName(
            static_cast<ReconstructiveTemporalPerformanceProfile>(999u)),
            "Unknown") == 0, "invalid performance profile has a safe name");

        ReconstructiveTemporalAASettings independent;
        independent.performanceProfile =
            ReconstructiveTemporalPerformanceProfile::Performance;
        ApplyReconstructiveTemporalPreset(
            independent, ReconstructiveTemporalPreset::HeavyTemporal);
        Require(independent.performanceProfile ==
            ReconstructiveTemporalPerformanceProfile::Performance,
            "temporal presets preserve the independent performance profile");

        independent.performanceProfile =
            static_cast<ReconstructiveTemporalPerformanceProfile>(999u);
        SanitizeReconstructiveTemporalSettings(independent);
        Require(independent.performanceProfile ==
            ReconstructiveTemporalPerformanceProfile::Balanced,
            "invalid performance profile sanitizes to Balanced");

        std::set<uint32_t> reachableVariants;
        for (ReconstructiveTemporalPerformanceProfile profile : {
            ReconstructiveTemporalPerformanceProfile::Performance,
            ReconstructiveTemporalPerformanceProfile::Balanced,
            ReconstructiveTemporalPerformanceProfile::MaximumQuality })
        {
            for (HistorySampleFilter filter : {
                HistorySampleFilter::Bilinear,
                HistorySampleFilter::CatmullRom })
            {
                for (bool resurrection : { false, true })
                {
                    reachableVariants.insert(ReferenceResolveVariant(
                        profile, filter, resurrection));
                }
            }
        }
        Require(reachableVariants == std::set<uint32_t>{
            0u, 4u, 8u, 9u, 10u, 11u },
            "profile/filter/resurrection selection reaches exactly six compiled variants");
    }

    void TestLightTemporalWeights()
    {
        ReconstructiveTemporalAASettings light;
        ApplyReconstructiveTemporalPreset(
            light, ReconstructiveTemporalPreset::LightTemporal);

        struct WeightReference
        {
            float framesPerSecond;
            float stable;
            float moving;
            float reactive;
        };

        constexpr std::array<WeightReference, 4> References = {{
            { 30.0f, 0.45450444f, 0.78022510f, 0.99975961f },
            { 60.0f, 0.26142329f, 0.53119844f, 0.98449612f },
            { 90.0f, 0.18292157f, 0.39652491f, 0.93782347f },
            { 120.0f, 0.14059514f, 0.31530917f, 0.87548554f }
        }};

        float previousMovingWeight = 1.0f;
        for (const WeightReference& reference : References)
        {
            const ReconstructiveTemporalWeights weights =
                ComputeReconstructiveTemporalWeights(
                    light, 1.0f / reference.framesPerSecond);
            RequireNear(weights.stableCurrentWeight, reference.stable,
                "Light stable time-constant weight", 2e-6f);
            RequireNear(weights.movingCurrentWeight, reference.moving,
                "Light moving time-constant weight", 2e-6f);
            RequireNear(weights.reactiveCurrentWeight, reference.reactive,
                "Light reactive time-constant weight", 2e-6f);
            Require(weights.stableCurrentWeight < weights.movingCurrentWeight &&
                weights.movingCurrentWeight < weights.reactiveCurrentWeight,
                "shorter response time increases current-frame contribution");
            Require(weights.movingCurrentWeight < previousMovingWeight,
                "current-frame contribution decreases as frame rate increases");
            previousMovingWeight = weights.movingCurrentWeight;
        }

        const ReconstructiveTemporalWeights atSixty =
            ComputeReconstructiveTemporalWeights(light, 1.0f / 60.0f);
        RequireNear(atSixty.movingCurrentWeight, 0.53f,
            "Light moving response is approximately half current frame at 60 FPS",
            0.002f);

        const ReconstructiveTemporalWeights zeroDelta =
            ComputeReconstructiveTemporalWeights(light, 0.0f);
        Require(zeroDelta.stableCurrentWeight == 0.0f &&
            zeroDelta.movingCurrentWeight == 0.0f &&
            zeroDelta.reactiveCurrentWeight == 0.0f,
            "zero delta produces zero new time-domain contribution");

        const ReconstructiveTemporalWeights infiniteDelta =
            ComputeReconstructiveTemporalWeights(
                light, std::numeric_limits<float>::infinity());
        Require(infiniteDelta.stableCurrentWeight == 1.0f &&
            infiniteDelta.movingCurrentWeight == 1.0f &&
            infiniteDelta.reactiveCurrentWeight == 1.0f,
            "infinite positive delta converges fully to the current frame");
    }

    void TestSanitization()
    {
        ReconstructiveTemporalAASettings settings;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();

        settings.preset = static_cast<ReconstructiveTemporalPreset>(999u);
        settings.jitterSequence = static_cast<JitterSequence>(999u);
        settings.historyFilter = static_cast<HistorySampleFilter>(999u);
        settings.debugMode = static_cast<ReconstructiveTemporalDebugMode>(999u);
        settings.jitterPeriod = 0;
        settings.jitterScale = infinity;
        settings.motionResponseStartPixels = 100.0f;
        settings.motionResponseEndPixels = -10.0f;
        settings.absoluteDepthThreshold = nan;
        settings.relativeDepthThreshold = infinity;
        settings.normalRejectCosine = 0.9f;
        settings.normalAcceptCosine = -0.9f;
        settings.automaticReactiveStrength = nan;
        settings.reactiveLuminanceThreshold = infinity;
        settings.reactiveChromaThreshold = nan;
        settings.thinGeometryDepthRange = infinity;
        settings.thinGeometryContrastThreshold = nan;
        settings.thinGeometryCoverageResponseMs = -infinity;
        settings.thinGeometryMaximumRelaxation = infinity;
        settings.varianceClipSigma = nan;
        settings.luminanceClipStrength = infinity;
        settings.chromaClipStrength = nan;
        settings.thinGeometryClipExpansion = infinity;
        settings.stableResponseMs = nan;
        settings.movingResponseMs = -infinity;
        settings.reactiveResponseMs = infinity;
        settings.maximumHistorySamples = 0;
        settings.maximumMovingHistorySamples = 999;
        settings.spatialFallbackRadius = 999;
        settings.spatialDepthWeight = nan;
        settings.spatialNormalWeight = infinity;
        settings.spatialLuminanceWeight = nan;
        settings.resurrectionEnabled = true;
        settings.persistentFrameCount = 999;
        settings.persistentFrameInterval = 0;
        settings.maximumResurrectionWeight = infinity;
        settings.resurrectionMatchThreshold = nan;
        settings.sharpeningStrength = infinity;
        settings.sharpeningMotionSuppression = nan;
        settings.sharpeningReactiveSuppression = infinity;
        settings.sharpeningVarianceSuppression = nan;
        settings.sharpeningHaloClamp = infinity;

        SanitizeReconstructiveTemporalSettings(settings);

        Require(AllFinite(settings), "sanitization makes every float finite");
        Require(settings.preset == ReconstructiveTemporalPreset::Custom,
            "invalid preset becomes Custom rather than mislabeling values");
        Require(settings.jitterSequence == JitterSequence::R2,
            "invalid jitter sequence uses R2");
        Require(settings.historyFilter == HistorySampleFilter::CatmullRom,
            "invalid history filter uses Catmull-Rom");
        Require(settings.debugMode == ReconstructiveTemporalDebugMode::FinalOutput,
            "invalid debug mode uses final output");
        Require(settings.jitterPeriod == 2, "jitter period minimum is two");
        Require(settings.jitterScale >= 0.0f && settings.jitterScale <= 1.0f,
            "jitter scale is bounded");
        Require(settings.motionResponseStartPixels <= settings.motionResponseEndPixels,
            "motion-response interval is ordered");
        Require(settings.normalRejectCosine <= settings.normalAcceptCosine,
            "normal confidence interval is ordered");
        Require(settings.maximumHistorySamples == 1 &&
            settings.maximumMovingHistorySamples == 1,
            "history counts are nonzero and moving count cannot exceed total");
        Require(settings.spatialFallbackRadius == 2,
            "spatial radius is limited to the optional 5x5 kernel");
        Require(settings.persistentFrameCount == 2,
            "persistent history count is capped at two");
        Require(settings.persistentFrameInterval == 1,
            "persistent frame interval is at least one");
        Require(settings.maximumResurrectionWeight <= 0.5f,
            "resurrection contribution is conservative");

        settings.resurrectionEnabled = false;
        settings.persistentFrameCount = 2;
        SanitizeReconstructiveTemporalSettings(settings);
        Require(settings.persistentFrameCount == 0,
            "disabled resurrection cannot retain allocatable histories");
    }

    void TestJitterSequence(JitterSequence sequence, const char* label)
    {
        ReconstructiveTemporalAASettings settings;
        settings.jitterSequence = sequence;
        settings.jitterPeriod = 16;
        settings.jitterScale = 1.0f;

        float minimumX = 1.0f;
        float minimumY = 1.0f;
        float maximumX = -1.0f;
        float maximumY = -1.0f;
        float sumX = 0.0f;
        float sumY = 0.0f;

        for (uint64_t frame = 0; frame < settings.jitterPeriod; ++frame)
        {
            const ReconstructiveTemporalJitter first =
                GenerateReconstructiveTemporalJitter(settings, frame);
            const ReconstructiveTemporalJitter second =
                GenerateReconstructiveTemporalJitter(settings, frame);
            Require(first.x == second.x && first.y == second.y,
                std::string(label) + " jitter is deterministic");
            Require(std::isfinite(first.x) && std::isfinite(first.y),
                std::string(label) + " jitter is finite");
            Require(std::abs(first.x) <= 0.5f && std::abs(first.y) <= 0.5f,
                std::string(label) + " jitter remains within half a pixel");

            minimumX = std::min(minimumX, first.x);
            minimumY = std::min(minimumY, first.y);
            maximumX = std::max(maximumX, first.x);
            maximumY = std::max(maximumY, first.y);
            sumX += first.x;
            sumY += first.y;
        }

        Require(minimumX < 0.0f && maximumX > 0.0f &&
            minimumY < 0.0f && maximumY > 0.0f,
            std::string(label) + " sequence is centered around zero");
        Require(std::abs(sumX / static_cast<float>(settings.jitterPeriod)) < 0.15f &&
            std::abs(sumY / static_cast<float>(settings.jitterPeriod)) < 0.15f,
            std::string(label) + " cycle has no large directional bias");

        const ReconstructiveTemporalJitter phaseZero =
            GenerateReconstructiveTemporalJitter(settings, 0);
        const ReconstructiveTemporalJitter wrapped =
            GenerateReconstructiveTemporalJitter(settings, settings.jitterPeriod);
        Require(phaseZero.x == wrapped.x && phaseZero.y == wrapped.y,
            std::string(label) + " phase wraps at jitterPeriod");

        settings.jitterScale = 0.25f;
        for (uint64_t frame = 0; frame < settings.jitterPeriod; ++frame)
        {
            const ReconstructiveTemporalJitter scaled =
                GenerateReconstructiveTemporalJitter(settings, frame);
            Require(std::abs(scaled.x) <= 0.125f && std::abs(scaled.y) <= 0.125f,
                std::string(label) + " jitter scale is applied in pixel units");
        }

        settings.freezeJitter = true;
        const ReconstructiveTemporalJitter frozen =
            GenerateReconstructiveTemporalJitter(settings, 7);
        Require(frozen.x == 0.0f && frozen.y == 0.0f,
            std::string(label) + " frozen mode is zero jitter");
    }

    void TestJitter()
    {
        TestJitterSequence(JitterSequence::R2, "R2");
        TestJitterSequence(JitterSequence::Halton23, "Halton 2/3");

        for (uint32_t period : { 8u, 16u })
        {
            ReconstructiveTemporalAASettings zeroMean;
            zeroMean.jitterSequence = JitterSequence::R2;
            zeroMean.jitterPeriod = period;
            zeroMean.jitterScale = 1.0f;
            double sumX = 0.0;
            double sumY = 0.0;
            for (uint32_t phase = 0; phase < period; ++phase)
            {
                const ReconstructiveTemporalJitter jitter =
                    GenerateReconstructiveTemporalJitter(zeroMean, phase);
                sumX += static_cast<double>(jitter.x);
                sumY += static_cast<double>(jitter.y);
            }
            Require(std::abs(sumX) <= 1e-6 && std::abs(sumY) <= 1e-6,
                "finite R2 production cycle has a strict zero centroid");
        }

        ReconstructiveTemporalAASettings settings;
        settings.jitterPeriod = 1000;
        settings.heldJitterPhase = 999;
        SanitizeReconstructiveTemporalSettings(settings);
        Require(settings.jitterPeriod == 64, "jitter period maximum is 64");
        Require(settings.heldJitterPhase == 63,
            "held jitter phase is clamped inside the active sequence");
        const ReconstructiveTemporalJitter first =
            GenerateReconstructiveTemporalJitter(settings, 5);
        const ReconstructiveTemporalJitter wrapped =
            GenerateReconstructiveTemporalJitter(settings, 69);
        Require(first.x == wrapped.x && first.y == wrapped.y,
            "sanitized 64-position sequence wraps exactly");

        settings.holdJitterPhase = true;
        settings.heldJitterPhase = 7;
        const ReconstructiveTemporalJitter held =
            GenerateReconstructiveTemporalJitter(settings, 123456);
        ReconstructiveTemporalAASettings referenceSettings = settings;
        referenceSettings.holdJitterPhase = false;
        const ReconstructiveTemporalJitter heldReference =
            GenerateReconstructiveTemporalJitter(referenceSettings, 7);
        Require(held.x == heldReference.x && held.y == heldReference.y,
            "held jitter phase repeats a selected nonzero sequence sample");
    }

    void TestResolvedAndRawHistoryCoordinates()
    {
        const Float2 currentPixelCenter = { 100.5f, 60.5f };
        const Float2 currentJitter = { 0.45f, -0.40f };
        const Float2 immediateJitter = { -0.45f, 0.40f };
        const Float2 motion = { 2.25f, -1.75f };
        const Float2 currentNoOffset = currentPixelCenter - currentJitter;
        const Float2 historyNoOffset = currentNoOffset + motion;

        const HistoryCoordinates immediate = ImmediateHistoryCoordinates(
            currentPixelCenter, motion, currentJitter, immediateJitter);
        const HistoryCoordinates projected = PersistentHistoryCoordinates(
            historyNoOffset, currentJitter, immediateJitter);

        RequireNear(immediate.resolvedColor.x, projected.resolvedColor.x,
            "immediate and projected resolved-color X coordinates agree");
        RequireNear(immediate.resolvedColor.y, projected.resolvedColor.y,
            "immediate and projected resolved-color Y coordinates agree");
        RequireNear(immediate.rawAuxiliary.x, projected.rawAuxiliary.x,
            "immediate and projected raw-auxiliary X coordinates agree");
        RequireNear(immediate.rawAuxiliary.y, projected.rawAuxiliary.y,
            "immediate and projected raw-auxiliary Y coordinates agree");
        RequireNear(immediate.rawAuxiliary.x,
            immediate.resolvedColor.x - currentJitter.x + immediateJitter.x,
            "raw auxiliary X applies previous-current jitter exactly once");
        RequireNear(immediate.rawAuxiliary.y,
            immediate.resolvedColor.y - currentJitter.y + immediateJitter.y,
            "raw auxiliary Y applies previous-current jitter exactly once");
        Require(std::abs(immediate.rawAuxiliary.x -
            immediate.resolvedColor.x) > 0.5f &&
            std::abs(immediate.rawAuxiliary.y -
                immediate.resolvedColor.y) > 0.5f,
            "opposing jitters keep resolved color and raw validation coordinates distinct");

        const Float2 olderJitter = { -0.30f, 0.35f };
        const HistoryCoordinates persistent = PersistentHistoryCoordinates(
            historyNoOffset, currentJitter, olderJitter);
        RequireNear(persistent.resolvedColor.x,
            historyNoOffset.x + currentJitter.x,
            "persistent resolved color preserves the current subpixel phase");
        RequireNear(persistent.rawAuxiliary.y,
            historyNoOffset.y + olderJitter.y,
            "persistent raw auxiliary uses its producing history jitter");

        Require(ReferenceStaticSurfaceForResurrection(
            { 2.0f, -1.0f }, { 2.08f, -1.08f }, 1.0f),
            "camera-only motion agreement permits static-surface resurrection");
        Require(!ReferenceStaticSurfaceForResurrection(
            { 2.0f, -1.0f }, { 2.2f, -1.0f }, 1.0f),
            "subpixel independent motion cannot resurrect older static-world color");
        Require(!ReferenceStaticSurfaceForResurrection(
            { 3.0f, -1.0f }, { 2.0f, -1.0f }, 1.0f),
            "independent object motion rejects persistent resurrection");
        Require(!ReferenceStaticSurfaceForResurrection(
            { 2.0f, -1.0f }, { 2.0f, -1.0f }, 0.25f),
            "depth-changing object motion rejects persistent resurrection");
    }

    void TestReverseZVelocityOwnership()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const std::array<VelocitySample, 9> neighborhood = {{
            { nan, 500.0f, 500.0f },
            { 0.20f, 120.0f, -90.0f },
            { 0.93f, 1.25f, -0.75f },
            { 0.00f, 300.0f, 300.0f },
            { 0.00f, 0.10f, 0.20f },
            { 0.80f, -2.00f, 0.50f },
            { 1.10f, 900.0f, 900.0f },
            { -0.1f, 700.0f, 700.0f },
            { 0.70f, 4.00f, -3.00f }
        }};

        const VelocityOwner owner =
            SelectNearestReverseZVelocity(neighborhood);
        Require(owner.valid, "reverse-Z dilation finds a valid owner");
        Require(owner.sampleIndex == 2,
            "reverse-Z dilation selects the largest valid depth in the 3x3");
        RequireNear(owner.sample.depth, 0.93f,
            "reverse-Z dilation retains nearest-surface depth");
        RequireNear(owner.sample.motionX, 1.25f,
            "velocity comes from the nearest surface, not the fastest vector");
        RequireNear(owner.sample.motionY, -0.75f,
            "both velocity components belong to the selected surface");

        std::array<VelocitySample, 9> validCenter = neighborhood;
        validCenter[4] = { 0.45f, 0.10f, 0.20f };
        const VelocityOwner centerOwner =
            SelectNearestReverseZVelocity(validCenter);
        Require(centerOwner.valid && centerOwner.sampleIndex == 4,
            "valid center motion is retained without a 3x3 dilation scan");
        RequireNear(centerOwner.sample.motionX, 0.10f,
            "a closer neighbor cannot steal valid center motion ownership");

        std::array<VelocitySample, 9> centerWins = {};
        centerWins[4] = { 0.8f, 2.0f, 3.0f };
        centerWins[5] = { 0.8f, 200.0f, 300.0f };
        const VelocityOwner tiedOwner =
            SelectNearestReverseZVelocity(centerWins);
        Require(tiedOwner.valid && tiedOwner.sampleIndex == 4,
            "equal-depth neighbor cannot steal center velocity ownership");

        const std::array<VelocitySample, 9> background = {};
        Require(!SelectNearestReverseZVelocity(background).valid,
            "reverse-Z clear depth never becomes a velocity owner");
    }

    void TestDepthSentinelsAndStableClassification()
    {
        for (bool reverseZ : { false, true })
        {
            const float clearDepth = ClearDeviceDepth(reverseZ);
            Require(!IsValidDeviceDepth(clearDepth, reverseZ),
                "the convention-specific clear depth stays invalid in history");
            Require(IsValidDeviceDepth(0.5f, reverseZ),
                "mid-range geometry depth remains valid in both conventions");
        }

        RequireNear(ReferencePreparedReactive(true, 0.0f, true), 0.0f,
            "static material feature presence is not temporal reactivity");
        RequireNear(ReferencePreparedReactive(true, 0.75f, false), 0.75f,
            "an explicit application-authored signal remains reactive");
        RequireNear(ReferencePreparedReactive(false, 1.0f, false), 0.0f,
            "cleared background does not read stale reactive classification");

        RequireNear(ReferenceAuthoredThin(false, 0.0f, true), 0.0f,
            "clear sky cannot become authored thin geometry");
        RequireNear(ReferenceAuthoredThin(true, 0.5f, false), 1.0f,
            "depth-writing partial alpha can author thin coverage");
        RequireNear(ReferenceAuthoredThin(true, 1.0f, true), 1.0f,
            "an explicit thin feature remains classified on valid geometry");
    }

    void TestConfidenceBootstrap()
    {
        const float resetConfidence = ReferenceGeometricConfidence(
            false, 1.0f, 1.0f, 1.0f, true, true, 1.0f);
        RequireNear(resetConfidence, 0.0f,
            "reset frame rejects nonexistent history");

        const float freshAfterReset = ReferenceGeometricConfidence(
            true, 0.85f, 0.95f, 0.90f, true, true, resetConfidence);
        const float freshWithPriorOne = ReferenceGeometricConfidence(
            true, 0.85f, 0.95f, 0.90f, true, true, 1.0f);
        Require(freshAfterReset > 0.05f,
            "first valid frame bootstraps confidence from fresh geometry");
        RequireNear(freshAfterReset, freshWithPriorOne,
            "prior zero confidence cannot form a multiplicative fixed point");

        ReconstructiveTemporalAASettings settings;
        ApplyReconstructiveTemporalPreset(
            settings, ReconstructiveTemporalPreset::MediumTemporal);
        const AccumulationStep reset = StepAccumulation(
            settings, 1.0f / 60.0f, 0.0f, 0.0f, resetConfidence, 42.0f);
        RequireNear(reset.currentWeight, 1.0f,
            "reset frame contributes only the current sample");
        RequireNear(reset.historyCount, 1.0f,
            "reset frame establishes a one-sample bootstrap history");

        const AccumulationStep secondFrame = StepAccumulation(
            settings, 1.0f / 60.0f, 0.0f, 0.0f,
            freshAfterReset, reset.historyCount);
        Require(secondFrame.currentWeight < 0.999f,
            "freshly validated history accumulates on the second frame");
        RequireNear(secondFrame.historyCount, 2.0f,
            "bootstrap history advances instead of remaining stuck at one");
    }

    void TestDepthNormalAndMaterialValidation()
    {
        RequireNear(ReferenceDepthConfidence(
            1.0f, 1.008f, 0.01f, 0.0f), 1.0f,
            "absolute depth threshold accepts sub-threshold error");
        RequireNear(ReferenceDepthConfidence(
            1.0f, 1.015f, 0.01f, 0.0f), 0.5f,
            "absolute depth confidence fades between one and two thresholds",
            2e-5f);
        RequireNear(ReferenceDepthConfidence(
            1.0f, 1.015f, 0.01f, 0.0f, 2.0f), 1.0f,
            "bounded grazing tolerance accepts a one-texel planar depth error",
            2e-5f);
        RequireNear(ReferenceDepthConfidence(
            1.0f, 1.021f, 0.01f, 0.0f), 0.0f,
            "absolute depth threshold rejects a disocclusion");

        RequireNear(ReferenceDepthConfidence(
            100.0f, 101.5f, 0.01f, 0.02f), 1.0f,
            "relative depth threshold scales for distant surfaces");
        RequireNear(ReferenceDepthConfidence(
            100.0f, 105.0f, 0.01f, 0.02f), 0.0f,
            "absolute plus relative threshold still rejects large distant error");
        RequireNear(ReferenceDepthConfidence(
            std::numeric_limits<float>::infinity(), 1.0f, 0.01f, 0.02f),
            0.0f, "non-finite depth is always rejected");

        RequireNear(ReferenceNormalConfidence(1.0f, 0.70f, 0.95f), 1.0f,
            "aligned normals retain history");
        RequireNear(ReferenceNormalConfidence(0.825f, 0.70f, 0.95f), 0.5f,
            "normal confidence transitions smoothly");
        RequireNear(ReferenceNormalConfidence(0.60f, 0.70f, 0.95f), 0.0f,
            "opposing or incompatible normals reject history");

        Require(ReferenceMaterialMatch(true, 37u, 37u),
            "identical material IDs validate");
        Require(!ReferenceMaterialMatch(true, 37u, 38u),
            "same-depth material boundary rejects history");
        Require(ReferenceMaterialMatch(false, 37u, 38u),
            "disabled material validation is an explicit bypass");

        RequireNear(ReferenceGeometricConfidence(
            true, 1.0f, 1.0f, 0.0f, true, true, 1.0f), 0.0f,
            "normal rejection is a hard geometric veto");
        RequireNear(ReferenceGeometricConfidence(
            true, 1.0f, 1.0f, 1.0f, false, true, 1.0f), 0.0f,
            "material rejection is a hard geometric veto");
    }

    void TestHdrLineBoxClipping()
    {
        const Float3 hdr = { 12.0f, 3.0f, 1.0f };
        const Float3 roundTrip = CompressedYCoCgToRgb(
            RgbToCompressedYCoCg(hdr));
        RequireNear(roundTrip.x, hdr.x, "HDR transform preserves red", 2e-5f);
        RequireNear(roundTrip.y, hdr.y, "HDR transform preserves green", 2e-5f);
        RequireNear(roundTrip.z, hdr.z, "HDR transform preserves blue", 2e-5f);

        const Float3 center = RgbToCompressedYCoCg({ 4.0f, 3.0f, 2.0f });
        const Float3 lower = center - Float3{ 0.45f, 0.10f, 0.10f };
        const Float3 upper = center + Float3{ 0.45f, 0.10f, 0.10f };
        const Float3 history = RgbToCompressedYCoCg({ 64.0f, 1.0f, 0.25f });
        const Float3 clipped = ClipLineToBox(
            history, center, lower, upper);

        Require(clipped.x >= lower.x - ReferenceEpsilon &&
            clipped.x <= upper.x + ReferenceEpsilon &&
            clipped.y >= lower.y - ReferenceEpsilon &&
            clipped.y <= upper.y + ReferenceEpsilon &&
            clipped.z >= lower.z - ReferenceEpsilon &&
            clipped.z <= upper.z + ReferenceEpsilon,
            "line clipping intersects every variance-box component");
        Require(Length(clipped - history) > 0.01f,
            "outlying HDR history is clipped");

        const Float3 originalDirection = history - center;
        const Float3 clippedDirection = clipped - center;
        Require(Length(Cross(originalDirection, clippedDirection)) <=
            2e-5f * std::max(Length(originalDirection), 1.0f),
            "line/box clipping preserves history chroma direction");

        const Float3 clippedRgb = CompressedYCoCgToRgb(clipped);
        Require(IsFinite(clippedRgb) && IsNonnegative(clippedRgb),
            "clipped HDR radiance is finite and nonnegative");
        Require(std::max(clippedRgb.x,
            std::max(clippedRgb.y, clippedRgb.z)) > 1.0f,
            "history clipping retains native HDR range above one");

        const Float3 negativeRoundTrip = CompressedYCoCgToRgb(
            RgbToCompressedYCoCg({ -10.0f, 2.0f, -4.0f }));
        Require(IsFinite(negativeRoundTrip) && IsNonnegative(negativeRoundTrip),
            "negative Catmull-Rom lobes cannot produce negative radiance");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const Float3 nonFiniteRoundTrip = CompressedYCoCgToRgb(
            RgbToCompressedYCoCg({ nan, 3.0f, 2.0f }));
        Require(IsFinite(nonFiniteRoundTrip) && IsNonnegative(nonFiniteRoundTrip),
            "non-finite history sanitizes to finite nonnegative radiance");
        RequireNear(Length(nonFiniteRoundTrip), 0.0f,
            "one non-finite HDR channel invalidates the complete sample");
    }

    void TestAdaptiveAccumulationAtFrameRates()
    {
        ReconstructiveTemporalAASettings settings;
        ApplyReconstructiveTemporalPreset(
            settings, ReconstructiveTemporalPreset::MediumTemporal);

        constexpr std::array<float, 3> FrameRates = { 30.0f, 60.0f, 120.0f };
        std::array<float, FrameRates.size()> stableWeights = {};
        for (size_t rateIndex = 0; rateIndex < FrameRates.size(); ++rateIndex)
        {
            const float deltaSeconds = 1.0f / FrameRates[rateIndex];
            float stableCount = 1.0f;
            AccumulationStep stable;
            for (uint32_t frame = 0; frame < 256u; ++frame)
            {
                stable = StepAccumulation(settings, deltaSeconds,
                    0.0f, 0.0f, 1.0f, stableCount);
                stableCount = stable.historyCount;
            }
            stableWeights[rateIndex] = stable.currentWeight;
            RequireNear(stable.maximumSamples,
                static_cast<float>(settings.maximumHistorySamples),
                "stable sample cap uses the preset maximum");
            RequireNear(stableCount,
                static_cast<float>(settings.maximumHistorySamples),
                "stable accumulation cannot exceed its sample cap");
            Require(stable.currentWeight >=
                1.0f / (stable.maximumSamples + 1.0f),
                "stable accumulation enforces its sample-limited weight");

            float movingCount = 1.0f;
            AccumulationStep moving;
            for (uint32_t frame = 0; frame < 256u; ++frame)
            {
                moving = StepAccumulation(settings, deltaSeconds,
                    1.0f, 0.0f, 1.0f, movingCount);
                movingCount = moving.historyCount;
            }
            RequireNear(moving.maximumSamples,
                static_cast<float>(settings.maximumMovingHistorySamples),
                "moving sample cap adapts below the stable cap");
            RequireNear(movingCount,
                static_cast<float>(settings.maximumMovingHistorySamples),
                "moving accumulation cannot exceed its adaptive cap");

            const AccumulationStep disoccluded = StepAccumulation(
                settings, deltaSeconds, 0.0f, 0.0f, 0.0f, stableCount);
            RequireNear(disoccluded.currentWeight, 1.0f,
                "complete disocclusion forces full current contribution");
            RequireNear(disoccluded.historyCount, 1.0f,
                "validation failure resets accumulated sample count");
        }

        Require(stableWeights[0] > stableWeights[1] &&
            stableWeights[1] > stableWeights[2],
            "time-domain current weight adapts consistently at 30/60/120 FPS");
        RequireNear(stableWeights[2],
            1.0f / (static_cast<float>(settings.maximumHistorySamples) + 1.0f),
            "120 FPS stable accumulation is bounded by the sample cap");
    }

    void TestNeighborhoodAwareCoverageAccumulation()
    {
        ReconstructiveTemporalAASettings settings;
        ApplyReconstructiveTemporalPreset(
            settings, ReconstructiveTemporalPreset::MediumTemporal);

        // A jittered black/white coverage edge is represented by the broad
        // current neighborhood. Its history delta is therefore spatially
        // explained and must not be treated as animated shading.
        const Float3 black = RgbToCompressedYCoCg({ 0.0f, 0.0f, 0.0f });
        const Float3 white = RgbToCompressedYCoCg({ 1.0f, 1.0f, 1.0f });
        const Float3 broadCenter = (black + white) * 0.5f;
        const Float3 explainedHistory = ClipLineToBox(
            white, broadCenter, black, white);
        RequireNear(ReferenceNeighborhoodReactive(
            white, explainedHistory, settings.reactiveLuminanceThreshold,
            settings.reactiveChromaThreshold,
            settings.automaticReactiveStrength), 0.0f,
            "jittered coverage inside the neighborhood is not reactive");

        const Float3 flatBlackHistory = ClipLineToBox(
            white, black, black, black);
        RequireNear(flatBlackHistory.x, black.x,
            "uniform stationary shading changes are corrected by clipping");
        const float flatRegionReactiveCandidate = ReferenceNeighborhoodReactive(
            white, flatBlackHistory, settings.reactiveLuminanceThreshold,
            settings.reactiveChromaThreshold,
            settings.automaticReactiveStrength);
        Require(flatRegionReactiveCandidate > 0.5f,
            "a flat-region history residual remains an automatic-reactive candidate");
        RequireNear(ReferenceMotionGatedAutomaticReactive(
            white, flatBlackHistory, settings.reactiveLuminanceThreshold,
            settings.reactiveChromaThreshold,
            settings.automaticReactiveStrength, 0.0f), 0.0f,
            "stationary Nyquist or uniform jitter phases cannot become reactive");
        Require(ReferenceMotionGatedAutomaticReactive(
            white, flatBlackHistory, settings.reactiveLuminanceThreshold,
            settings.reactiveChromaThreshold,
            settings.automaticReactiveStrength, 1.0f) > 0.5f,
            "motion corroborates a large automatic-reactive residual");

        Require(ReferenceCoverageOverrideSignal(true, 0.0f, true,
            0.0f, 1.0f, 1.0f) > 0.99f,
            "a diagonal-only borrowed surface supplies coverage evidence");
        Require(ReferenceCoverageOverrideSignal(true, 0.0f, false,
            1.0f, 0.0f, 1.0f) > 0.99f,
            "a separate thin lock fully validates an uncovered jitter phase");
        RequireNear(ReferenceCoverageOverrideSignal(true, 1.0f, true,
            1.0f, 1.0f, 0.0f), 0.0f,
            "moving disocclusions cannot use the stationary coverage override");

        RequireNear(ReferenceUncoveredCoverageHistory(
            0.0f, 1.0f, false, true, 1.0f, 0.0f), 1.0f,
            "lock trust does not multiply an already averaged coverage color");
        RequireNear(ReferenceUncoveredCoverageHistory(
            0.0f, 1.0f, false, true, 0.0f, 0.0f), 0.0f,
            "moving uncovered phases cannot retain stale radiance");
        RequireNear(ReferenceUncoveredCoverageHistory(
            0.0f, 1.0f, false, true, 1.0f, 1.0f), 0.0f,
            "reactive uncovered phases cannot retain stale radiance");

        const float packedMetadata = PackReactiveAndMotion(0.73f, 0.61f);
        RequireNear(UnpackReactive(packedMetadata), 11.0f / 15.0f,
            "metadata low nibble preserves reactive shading", 1e-6f);
        RequireNear(UnpackMotion(packedMetadata), 9.0f / 15.0f,
            "metadata high nibble independently preserves motion", 1e-6f);
        RequireNear(UnpackReactive(PackReactiveAndMotion(0.80f, 0.0f)),
            UnpackReactive(PackReactiveAndMotion(0.80f, 1.0f)),
            "motion cannot alter AgX reactive suppression", 1e-6f);
        RequireNear(QuantizeThinLock(0.61f), 156.0f / 255.0f,
            "thin-lock lifetime retains the full independent R8 channel", 1e-6f);

        RequireNear(InterpolatedStationaryThinLock(
            { 0.0f, 0.8f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f },
            { 0.1f, 0.9f, 0.0f, 0.0f }), 0.72f,
            "fractional reprojection finds a lock outside the floor texel");
        RequireNear(InterpolatedStationaryThinLock(
            { 0.0f, 0.8f, 0.0f, 0.0f },
            { 0.0f, 0.2f, 0.0f, 0.0f },
            { 0.1f, 0.9f, 0.0f, 0.0f }), 0.0f,
            "a moving adjacent lock cannot validate filtered stale color");
        RequireNear(InterpolatedStationaryThinLock(
            { 0.0f, 0.8f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f },
            { 1.0f, 0.0f, 0.0f, 0.0f }), 0.0f,
            "an exact-center sample rejects a zero-weight neighboring lock");
        const float translatedLock0 = InterpolatedStationaryThinLock(
            { 1.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f },
            { 0.75f, 0.25f, 0.0f, 0.0f });
        const float translatedLock1 = InterpolatedStationaryThinLock(
            { 0.0f, 1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f },
            { 0.75f, 0.25f, 0.0f, 0.0f });
        RequireNear(translatedLock0 + translatedLock1, 1.0f,
            "fractional lock reprojection conserves support instead of duplicating it");
        RequireNear(ReferencePreviousCoverageLifetime(
            false, 0.0f, 1.0f), 0.0f,
            "a non-pure background pixel cannot read a lock before history exists");
        RequireNear(ReferencePreviousCoverageLifetime(
            true, 0.25f, 1.0f), 0.25f,
            "a point lock avoids the conditional 2x2 fallback lookup");

        float historyColor = 0.0f;
        float historyCount = 1.0f;
        float lowPhase = 0.0f;
        float highPhase = 1.0f;
        for (uint32_t frame = 0; frame < 256u; ++frame)
        {
            const float currentColor = (frame & 1u) != 0u ? 1.0f : 0.0f;
            const float phaseConfidence = currentColor > 0.5f
                ? 0.90f : 0.85f * 0.90f;
            const CoverageAccumulationStep step = StepCoverageAccumulation(
                settings, 1.0f / 30.0f, historyColor, currentColor,
                historyCount, phaseConfidence, 1.0f);
            RequireNear(step.reactive, 0.0f,
                "alternating edge remains neighborhood-explained");
            historyColor = step.color;
            historyCount = step.historyCount;
            if (frame >= 254u)
            {
                if (currentColor == 0.0f)
                    lowPhase = historyColor;
                else
                    highPhase = historyColor;
            }
        }

        RequireNear(historyCount,
            static_cast<float>(settings.maximumHistorySamples),
            "coverage accumulation reaches the stable sample cap");
        Require(lowPhase > 0.45f && highPhase < 0.55f,
            "slow-GPU coverage accumulation stays near the analytical average");
        RequireNear((lowPhase + highPhase) * 0.5f, 0.5f,
            "asymmetric acceptance confidence does not bias coverage duty cycle",
            0.025f);
        Require(highPhase - lowPhase < 0.08f,
            "alternating coverage converges instead of following rotating samples");

        for (uint32_t hitsPerPeriod : { 1u, 2u })
        {
            historyColor = 0.0f;
            historyCount = 1.0f;
            float lockLifetime = 0.0f;
            float sum = 0.0f;
            float maximumFrameDelta = 0.0f;
            float previousColor = 0.0f;
            for (uint32_t frame = 0; frame < 1024u; ++frame)
            {
                const bool visible = frame % 8u < hitsPerPeriod;
                const bool previousLock = lockLifetime > 0.0f;
                const float phaseConfidence = visible
                    ? 0.90f : 0.85f * 0.90f;
                const CoverageAccumulationStep step = StepCoverageAccumulation(
                    settings, 1.0f / 30.0f, historyColor,
                    visible ? 1.0f : 0.0f, historyCount,
                    phaseConfidence, visible || previousLock ? 1.0f : 0.0f);
                historyColor = step.color;
                historyCount = step.historyCount;
                lockLifetime = StepCoverageLockLifetime(
                    lockLifetime, visible, 8u);
                if (frame >= 768u)
                {
                    sum += historyColor;
                    maximumFrameDelta = std::max(maximumFrameDelta,
                        std::abs(historyColor - previousColor));
                }
                previousColor = historyColor;
            }

            const float expectedDuty =
                static_cast<float>(hitsPerPeriod) / 8.0f;
            RequireNear(sum / 256.0f, expectedDuty,
                "sparse subpixel coverage converges to its analytical duty cycle",
                0.015f);
            Require(maximumFrameDelta < 0.08f,
                "one-of-eight and two-of-eight coverage do not pulse between phases");
        }

        for (uint32_t period : { 8u, 16u, 17u, 28u, 29u, 64u })
        {
            float expiringLock = 1.0f;
            for (uint32_t frame = 0; frame + 1u < period; ++frame)
                expiringLock = StepCoverageLockLifetime(
                    expiringLock, false, period);
            Require(expiringLock > 0.0f,
                "lock lifetime spans every uncovered jitter phase");
            for (uint32_t frame = period - 1u;
                frame < period + 3u; ++frame)
            {
                expiringLock = StepCoverageLockLifetime(
                    expiringLock, false, period);
            }
            RequireNear(expiringLock, 0.0f,
                "every supported jitter period releases a disappeared lock");
        }

        const float occluderClearedLock = StepCoverageLockLifetime(
            1.0f, false, 8u, true, 0.0f);
        RequireNear(occluderClearedLock, 0.0f,
            "a hard-rejected valid occluder cannot inherit an older thin lock");
        RequireNear(ReferenceExpiredCoverageAlpha(
            false, occluderClearedLock, 0.75f), 0.0f,
            "one cleanup frame clears expired coverage alpha for the sky fast path");
    }

    void TestFallbackAndThinRelaxationGates()
    {
        RequireNear(ReferenceFallbackContribution(true, 0.0f), 1.0f,
            "fully rejected history receives full spatial fallback");
        RequireNear(ReferenceFallbackContribution(true, 0.25f), 1.0f,
            "very-low-confidence history receives full spatial fallback");
        RequireNear(ReferenceFallbackContribution(true, 0.50f), 0.5f,
            "fallback fades inside the low-confidence interval");
        RequireNear(ReferenceFallbackContribution(true, 0.75f), 0.0f,
            "useful temporal history disables spatial fallback");
        RequireNear(ReferenceFallbackContribution(true, 0.95f), 0.0f,
            "high-confidence history is never spatially blurred");
        RequireNear(ReferenceFallbackContribution(false, 0.0f), 0.0f,
            "disabled fallback cannot affect rejected pixels");

        const float relaxed = ReferenceThinClipExpansion(1.0f, 0.025f, 0.0f);
        const float changing = ReferenceThinClipExpansion(1.0f, 0.025f, 0.70f);
        const float severe = ReferenceThinClipExpansion(1.0f, 0.025f, 1.0f);
        RequireNear(relaxed, 0.025f,
            "stable thin geometry receives the bounded clip expansion");
        Require(changing > 0.0f && changing < relaxed,
            "shading reactivity suppresses thin clip expansion continuously");
        RequireNear(severe, 0.0f,
            "severe reactivity disables thin clip expansion");
        RequireNear(ReferenceThinClipExpansion(0.0f, 0.025f, 0.0f), 0.0f,
            "non-thin pixels never receive thin clip expansion");

        const float baseConfidence = 0.50f;
        const float relaxedConfidence = ReferenceThinConfidenceRelaxation(
            baseConfidence, baseConfidence, 1.0f, 0.025f, 1.0f);
        Require(relaxedConfidence > baseConfidence &&
            relaxedConfidence <= baseConfidence + 0.025f,
            "validated thin geometry receives a small bounded confidence relaxation");
        RequireNear(ReferenceThinConfidenceRelaxation(
            0.0f, 0.0f, 1.0f, 0.025f, 1.0f), 0.0f,
            "thin relaxation cannot revive a hard geometric rejection");
        RequireNear(ReferenceThinConfidenceRelaxation(
            baseConfidence, baseConfidence, 0.0f, 0.025f, 1.0f),
            baseConfidence,
            "non-thin geometry receives no confidence relaxation");
        RequireNear(ReferenceThinConfidenceRelaxation(
            baseConfidence, baseConfidence, 1.0f, 0.025f, 0.0f),
            baseConfidence,
            "untrusted prior coverage cannot relax current confidence");
        RequireNear(ReferenceThinConfidenceRelaxation(
            baseConfidence, baseConfidence, 1.0f, 0.025f, 1.0f, 1.0f),
            baseConfidence,
            "reactive shading cannot use thin confidence relaxation");
    }

    void TestNativeOddDispatchCoverage()
    {
        struct DispatchCase
        {
            uint32_t width;
            uint32_t height;
            uint32_t groupsX;
            uint32_t groupsY;
        };

        constexpr std::array<DispatchCase, 5> Cases = {{
            { 1u, 1u, 1u, 1u },
            { 13u, 11u, 2u, 2u },
            { 1920u, 1080u, 240u, 135u },
            { 1919u, 1079u, 240u, 135u },
            { 2561u, 1441u, 321u, 181u }
        }};

        constexpr uint32_t ThreadGroupSize = 8u;
        for (const DispatchCase& testCase : Cases)
        {
            const uint32_t groupsX = DispatchGroupCount(testCase.width);
            const uint32_t groupsY = DispatchGroupCount(testCase.height);
            Require(groupsX == testCase.groupsX && groupsY == testCase.groupsY,
                "8x8 dispatch uses exact ceil division for native dimensions");
            Require(groupsX * ThreadGroupSize >= testCase.width &&
                groupsY * ThreadGroupSize >= testCase.height,
                "dispatch covers every native-resolution pixel");
            Require(groupsX * ThreadGroupSize - testCase.width < ThreadGroupSize &&
                groupsY * ThreadGroupSize - testCase.height < ThreadGroupSize,
                "odd-dimension overdispatch is less than one thread group");

            uint64_t guardedPixelCount = 0;
            for (uint32_t y = 0; y < groupsY * ThreadGroupSize; ++y)
            {
                for (uint32_t x = 0; x < groupsX * ThreadGroupSize; ++x)
                {
                    if (x < testCase.width && y < testCase.height)
                        ++guardedPixelCount;
                }
            }
            Require(guardedPixelCount ==
                static_cast<uint64_t>(testCase.width) * testCase.height,
                "bounds guard writes every native pixel exactly once");
        }

        Require(DispatchGroupCount(0u) == 0u,
            "zero-sized dimension cannot dispatch work accidentally");
    }

    void TestNamesAndDebugAbi()
    {
        static_assert(static_cast<uint32_t>(ReconstructiveTemporalDebugMode::FinalOutput) == 0);
        static_assert(static_cast<uint32_t>(ReconstructiveTemporalDebugMode::CurrentJitter) == 1);
        static_assert(static_cast<uint32_t>(ReconstructiveTemporalDebugMode::RejectionReasons) == 30);
        static_assert(static_cast<uint32_t>(ReconstructiveTemporalDebugMode::FinalNraRtaaOutput) == 31);
        static_assert(ReconstructiveTemporalDebugModeCount == 32);

        Require(std::strcmp(GetReconstructiveTemporalPresetName(
            ReconstructiveTemporalPreset::HeavyTemporal), "Heavy Temporal") == 0,
            "stable Heavy preset name");
        Require(std::strcmp(GetJitterSequenceName(JitterSequence::Halton23),
            "Halton 2/3") == 0, "stable Halton name");
        Require(std::strcmp(GetHistorySampleFilterName(
            HistorySampleFilter::CatmullRom), "Catmull-Rom") == 0,
            "stable Catmull-Rom name");

        std::set<std::string> names;
        for (uint32_t value = 0; value < ReconstructiveTemporalDebugModeCount; ++value)
        {
            const char* name = GetReconstructiveTemporalDebugModeName(
                static_cast<ReconstructiveTemporalDebugMode>(value));
            Require(name && name[0] != '\0', "every debug ABI value has a name");
            Require(std::strcmp(name, "Unknown") != 0,
                "every requested debug mode is recognized");
            Require(names.insert(name).second, "debug names are unique and stable");
        }

        Require(std::strcmp(GetReconstructiveTemporalDebugModeName(
            ReconstructiveTemporalDebugMode::FinalNraRtaaOutput),
            "Final NRA-RTAA Output") == 0,
            "stable final NRA-RTAA debug name");
        Require(std::strcmp(GetReconstructiveTemporalDebugModeName(
            static_cast<ReconstructiveTemporalDebugMode>(999u)), "Unknown") == 0,
            "invalid debug mode has a safe name");
    }
}

int main()
{
    TestPresets();
    TestPerformanceProfiles();
    TestLightTemporalWeights();
    TestSanitization();
    TestJitter();
    TestResolvedAndRawHistoryCoordinates();
    TestReverseZVelocityOwnership();
    TestDepthSentinelsAndStableClassification();
    TestConfidenceBootstrap();
    TestDepthNormalAndMaterialValidation();
    TestHdrLineBoxClipping();
    TestAdaptiveAccumulationAtFrameRates();
    TestNeighborhoodAwareCoverageAccumulation();
    TestFallbackAndThinRelaxationGates();
    TestNativeOddDispatchCoverage();
    TestNamesAndDebugAbi();

    std::cout << "UVSR NRA-RTAA reference validation passed\n";
    return EXIT_SUCCESS;
}
