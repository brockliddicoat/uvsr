#pragma once

#include "flashlight_shared.h"

#include <algorithm>
#include <cmath>

namespace uvsr
{
    inline constexpr char FlashlightPublicName[] = "flashlight_1";
    inline constexpr bool DefaultFlashlightEnabled = false;

    struct FlashlightSettings
    {
        bool realisticLens = true;
        bool castShadows = true;
        bool outputHitDistance = false;
        float peakIntensityCandela = 600.f;
        float rangeMeters = 30.f;
        float cameraHorizontalOffsetMeters = 0.17888544f;
        float cameraVerticalOffsetMeters = -0.08944272f;
        float beamSizeDegrees = 16.f;
        float angularSizeDegrees = 2.8641924f;
        float beamRoundness = 0.80f;
        float edgeSoftness = 0.60f;
        float colorLinearRed = 1.f;
        float colorLinearGreen = 0.80f;
        float colorLinearBlue = 0.65f;
        float hotspotSize = 0.40f;
        float hotspotStrength = 0.70f;
        float swayDegrees = 0.20f;
        float aimCorrectionSeconds = 0.05f;
    };

    struct FlashlightLobeSettings
    {
        float spillIntensityCandela = 0.f;
        float spillInnerConeDegrees = 0.f;
        float spillOuterConeDegrees = 0.f;
        float hotspotIntensityCandela = 0.f;
        float hotspotInnerConeDegrees = 0.f;
        float hotspotOuterConeDegrees = 0.f;
    };

    struct FlashlightSwayOffset
    {
        float yawDegrees = 0.f;
        float pitchDegrees = 0.f;
    };

    struct FlashlightMountPose
    {
        float positionRightMeters = 0.f;
        float positionUpMeters = 0.f;
        float positionForwardMeters = 0.f;
        float directionRight = 0.f;
        float directionUp = 0.f;
        float directionForward = 1.f;
    };

    // Donut's positional-light intensity is luminous intensity in lm/sr.
    // These bounds cover compact consumer lights without turning the control
    // into an unbounded scene-light multiplier.
    inline constexpr FlashlightSettings DefaultFlashlightSettings;
    inline constexpr float FlashlightMinimumIntensityCandela = 25.f;
    inline constexpr float FlashlightMaximumIntensityCandela = 4000.f;
    inline constexpr float FlashlightMinimumRangeMeters = 2.f;
    inline constexpr float FlashlightMaximumRangeMeters = 100.f;
    inline constexpr float FlashlightMinimumCameraHorizontalOffsetMeters =
        -0.40f;
    inline constexpr float FlashlightMaximumCameraHorizontalOffsetMeters =
        0.40f;
    inline constexpr float FlashlightMinimumCameraVerticalOffsetMeters =
        -0.40f;
    inline constexpr float FlashlightMaximumCameraVerticalOffsetMeters =
        0.40f;
    inline constexpr float FlashlightMinimumBeamSizeDegrees = 8.f;
    inline constexpr float FlashlightMaximumBeamSizeDegrees = 100.f;
    inline constexpr float FlashlightMinimumAngularSizeDegrees = 0.f;
    inline constexpr float FlashlightMaximumAngularSizeDegrees = 20.f;
    inline constexpr float FlashlightAngularSizeReferenceDistanceMeters = 1.f;
    inline constexpr float FlashlightMinimumCollisionRadiusMeters = 0.1f;
    inline constexpr float FlashlightMinimumHotspotSize = 0.20f;
    inline constexpr float FlashlightMaximumHotspotSize = 0.75f;
    inline constexpr float FlashlightMaximumHotspotStrength = 0.90f;
    inline constexpr float FlashlightMaximumSwayDegrees = 2.f;
    inline constexpr float FlashlightMinimumAimCorrectionSeconds = 0.01f;
    inline constexpr float FlashlightMaximumAimCorrectionSeconds = 0.50f;

    inline constexpr float FlashlightTurnOnSeconds = 0.18f;
    inline constexpr float FlashlightTurnOffSeconds = 0.24f;
    inline constexpr float FlashlightMountReleaseHalfLifeSeconds = 0.08f;
    inline constexpr float FlashlightMountRetractionNearMeters = 0.05f;
    inline constexpr float FlashlightMountRetractionFarMeters = 0.75f;
    inline constexpr float FlashlightMountRetractionRadiusScale = 4.f;
    inline constexpr float FlashlightMountRetractionLengthScale = 2.f;
    // Move the virtual emitter off the optical axis so occluders cannot hide
    // their own projected shadows. Its own emitter-aware collision sphere
    // keeps the offset mount outside nearby geometry. Converged aim keeps the
    // broad beam centered at a practical indoor distance after collision.
    inline constexpr float FlashlightCameraForwardOffsetMeters = 0.04f;
    inline constexpr float FlashlightAimConvergenceDistanceMeters = 6.f;
    inline constexpr float FlashlightMaximumAimLagDegrees = 5.f;
    // Every sway frequency below is an odd tenth of one radian per second.
    // Twenty pi seconds is therefore their shared phase-continuous period.
    inline constexpr float FlashlightSwayPeriodSeconds =
        62.831853071795864f;

    struct FlashlightMountRetractionRange
    {
        float nearDistanceMeters = FlashlightMountRetractionNearMeters;
        float farDistanceMeters = FlashlightMountRetractionFarMeters;
    };

    [[nodiscard]] inline FlashlightMountRetractionRange
        ResolveFlashlightMountRetractionRange(
            float collisionRadiusMeters,
            float mountLengthMeters)
    {
        collisionRadiusMeters =
            std::isfinite(collisionRadiusMeters) &&
                collisionRadiusMeters > 0.f
            ? collisionRadiusMeters
            : FlashlightMinimumCollisionRadiusMeters;
        mountLengthMeters =
            std::isfinite(mountLengthMeters) && mountLengthMeters > 0.f
            ? mountLengthMeters
            : 0.f;

        FlashlightMountRetractionRange result;
        result.nearDistanceMeters = std::max(
            FlashlightMountRetractionNearMeters,
            collisionRadiusMeters * 0.5f);
        result.farDistanceMeters = std::max(
            FlashlightMountRetractionFarMeters,
            std::max(
                collisionRadiusMeters *
                    FlashlightMountRetractionRadiusScale,
                mountLengthMeters *
                    FlashlightMountRetractionLengthScale));
        result.farDistanceMeters = std::max(
            result.farDistanceMeters,
            result.nearDistanceMeters + 1e-4f);
        return result;
    }

    [[nodiscard]] inline float ResolveFlashlightMountRetractionExtension(
        float surfaceClearanceMeters,
        const FlashlightMountRetractionRange& range)
    {
        if (!std::isfinite(surfaceClearanceMeters))
            return 0.f;

        const float denominator = std::max(
            range.farDistanceMeters - range.nearDistanceMeters,
            1e-4f);
        const float normalizedClearance = std::clamp(
            (surfaceClearanceMeters - range.nearDistanceMeters) /
                denominator,
            0.f,
            1.f);
        return normalizedClearance * normalizedClearance *
            (3.f - 2.f * normalizedClearance);
    }

    [[nodiscard]] inline FlashlightMountPose ResolveFlashlightMountPose(
        float cameraHorizontalOffsetMeters,
        float cameraVerticalOffsetMeters)
    {
        const float sanitizedHorizontalOffsetMeters = std::clamp(
            std::isfinite(cameraHorizontalOffsetMeters)
                ? cameraHorizontalOffsetMeters
                : DefaultFlashlightSettings.cameraHorizontalOffsetMeters,
            FlashlightMinimumCameraHorizontalOffsetMeters,
            FlashlightMaximumCameraHorizontalOffsetMeters);
        const float sanitizedVerticalOffsetMeters = std::clamp(
            std::isfinite(cameraVerticalOffsetMeters)
                ? cameraVerticalOffsetMeters
                : DefaultFlashlightSettings.cameraVerticalOffsetMeters,
            FlashlightMinimumCameraVerticalOffsetMeters,
            FlashlightMaximumCameraVerticalOffsetMeters);

        FlashlightMountPose result;
        result.positionRightMeters = sanitizedHorizontalOffsetMeters;
        result.positionUpMeters = sanitizedVerticalOffsetMeters;
        result.positionForwardMeters =
            FlashlightCameraForwardOffsetMeters;

        const float targetRightMeters =
            -result.positionRightMeters;
        const float targetUpMeters =
            -result.positionUpMeters;
        const float targetForwardMeters =
            FlashlightAimConvergenceDistanceMeters -
            result.positionForwardMeters;
        const float targetDistanceMeters = std::sqrt(
            targetRightMeters * targetRightMeters +
            targetUpMeters * targetUpMeters +
            targetForwardMeters * targetForwardMeters);
        result.directionRight =
            targetRightMeters / targetDistanceMeters;
        result.directionUp =
            targetUpMeters / targetDistanceMeters;
        result.directionForward =
            targetForwardMeters / targetDistanceMeters;
        return result;
    }

    [[nodiscard]] inline FlashlightSettings SanitizeFlashlightSettings(
        const FlashlightSettings& settings)
    {
        const auto finiteOr =
            [](float value, float fallback)
            {
                return std::isfinite(value) ? value : fallback;
            };
        FlashlightSettings result = settings;
        result.peakIntensityCandela = std::clamp(
            finiteOr(
                settings.peakIntensityCandela,
                DefaultFlashlightSettings.peakIntensityCandela),
            FlashlightMinimumIntensityCandela,
            FlashlightMaximumIntensityCandela);
        result.rangeMeters = std::clamp(
            finiteOr(
                settings.rangeMeters,
                DefaultFlashlightSettings.rangeMeters),
            FlashlightMinimumRangeMeters,
            FlashlightMaximumRangeMeters);
        result.cameraHorizontalOffsetMeters = std::clamp(
            finiteOr(
                settings.cameraHorizontalOffsetMeters,
                DefaultFlashlightSettings.cameraHorizontalOffsetMeters),
            FlashlightMinimumCameraHorizontalOffsetMeters,
            FlashlightMaximumCameraHorizontalOffsetMeters);
        result.cameraVerticalOffsetMeters = std::clamp(
            finiteOr(
                settings.cameraVerticalOffsetMeters,
                DefaultFlashlightSettings.cameraVerticalOffsetMeters),
            FlashlightMinimumCameraVerticalOffsetMeters,
            FlashlightMaximumCameraVerticalOffsetMeters);
        result.beamSizeDegrees = std::clamp(
            finiteOr(
                settings.beamSizeDegrees,
                DefaultFlashlightSettings.beamSizeDegrees),
            FlashlightMinimumBeamSizeDegrees,
            FlashlightMaximumBeamSizeDegrees);
        result.angularSizeDegrees = std::clamp(
            finiteOr(
                settings.angularSizeDegrees,
                DefaultFlashlightSettings.angularSizeDegrees),
            FlashlightMinimumAngularSizeDegrees,
            FlashlightMaximumAngularSizeDegrees);
        result.beamRoundness = std::clamp(
            finiteOr(
                settings.beamRoundness,
                DefaultFlashlightSettings.beamRoundness),
            0.f,
            1.f);
        result.edgeSoftness = std::clamp(
            finiteOr(
                settings.edgeSoftness,
                DefaultFlashlightSettings.edgeSoftness),
            0.f,
            1.f);
        result.colorLinearRed = std::clamp(
            finiteOr(
                settings.colorLinearRed,
                DefaultFlashlightSettings.colorLinearRed),
            0.f,
            1.f);
        result.colorLinearGreen = std::clamp(
            finiteOr(
                settings.colorLinearGreen,
                DefaultFlashlightSettings.colorLinearGreen),
            0.f,
            1.f);
        result.colorLinearBlue = std::clamp(
            finiteOr(
                settings.colorLinearBlue,
                DefaultFlashlightSettings.colorLinearBlue),
            0.f,
            1.f);
        result.hotspotSize = std::clamp(
            finiteOr(
                settings.hotspotSize,
                DefaultFlashlightSettings.hotspotSize),
            FlashlightMinimumHotspotSize,
            FlashlightMaximumHotspotSize);
        result.hotspotStrength = std::clamp(
            finiteOr(
                settings.hotspotStrength,
                DefaultFlashlightSettings.hotspotStrength),
            0.f,
            FlashlightMaximumHotspotStrength);
        result.swayDegrees = std::clamp(
            finiteOr(
                settings.swayDegrees,
                DefaultFlashlightSettings.swayDegrees),
            0.f,
            FlashlightMaximumSwayDegrees);
        result.aimCorrectionSeconds = std::clamp(
            finiteOr(
                settings.aimCorrectionSeconds,
                DefaultFlashlightSettings.aimCorrectionSeconds),
            FlashlightMinimumAimCorrectionSeconds,
            FlashlightMaximumAimCorrectionSeconds);
        return result;
    }

    [[nodiscard]] inline FlashlightLobeSettings
        ResolveFlashlightLobeSettings(
            const FlashlightSettings& untrustedSettings)
    {
        const FlashlightSettings settings =
            SanitizeFlashlightSettings(untrustedSettings);
        const float spillInnerRatio =
            0.92f - 0.60f * settings.edgeSoftness;

        FlashlightLobeSettings result;
        result.spillOuterConeDegrees = settings.beamSizeDegrees;
        result.spillInnerConeDegrees =
            settings.beamSizeDegrees * spillInnerRatio;
        if (!settings.realisticLens)
        {
            result.spillIntensityCandela =
                settings.peakIntensityCandela;
            return result;
        }

        result.spillIntensityCandela =
            settings.peakIntensityCandela *
            (1.f - settings.hotspotStrength);
        result.hotspotIntensityCandela =
            settings.peakIntensityCandela *
            settings.hotspotStrength;
        result.hotspotOuterConeDegrees =
            settings.beamSizeDegrees * settings.hotspotSize;
        const float hotspotInnerRatio =
            0.90f - 0.48f * settings.edgeSoftness;
        result.hotspotInnerConeDegrees =
            result.hotspotOuterConeDegrees * hotspotInnerRatio;
        return result;
    }

    [[nodiscard]] inline float ResolveFlashlightBeamShapeExponent(
        float beamRoundness)
    {
        beamRoundness = std::clamp(
            std::isfinite(beamRoundness)
                ? beamRoundness
                : DefaultFlashlightSettings.beamRoundness,
            0.f,
            1.f);
        return std::exp2(1.f + 3.f * (1.f - beamRoundness));
    }

    [[nodiscard]] inline float ResolveFlashlightEmitterRadiusMeters(
        float angularSizeDegrees)
    {
        angularSizeDegrees = std::clamp(
            std::isfinite(angularSizeDegrees)
                ? angularSizeDegrees
                : DefaultFlashlightSettings.angularSizeDegrees,
            FlashlightMinimumAngularSizeDegrees,
            FlashlightMaximumAngularSizeDegrees);
        constexpr float DegreesToHalfRadians =
            0.0087266462599716478846f;
        return std::tan(
            angularSizeDegrees * DegreesToHalfRadians) *
            FlashlightAngularSizeReferenceDistanceMeters;
    }

    [[nodiscard]] inline float ResolveFlashlightCollisionRadiusMeters(
        float angularSizeDegrees,
        float cameraCollisionRadiusMeters)
    {
        const float safeCameraRadius =
            std::isfinite(cameraCollisionRadiusMeters) &&
                cameraCollisionRadiusMeters > 0.f
            ? cameraCollisionRadiusMeters
            : FlashlightMinimumCollisionRadiusMeters;
        return std::max(
            std::max(
                safeCameraRadius,
                FlashlightMinimumCollisionRadiusMeters),
            ResolveFlashlightEmitterRadiusMeters(angularSizeDegrees));
    }

    [[nodiscard]] inline FlashlightBeamProfile ResolveFlashlightBeamProfile(
        const FlashlightSettings& untrustedSettings,
        float beamRightX,
        float beamRightY,
        float beamRightZ)
    {
        const FlashlightSettings settings =
            SanitizeFlashlightSettings(untrustedSettings);
        const FlashlightLobeSettings lobes =
            ResolveFlashlightLobeSettings(settings);
        constexpr float DegreesToHalfRadians =
            0.0087266462599716478846f;

        FlashlightBeamProfile profile{};
        profile.beamRightX = beamRightX;
        profile.beamRightY = beamRightY;
        profile.beamRightZ = beamRightZ;
        profile.shapeExponent =
            ResolveFlashlightBeamShapeExponent(settings.beamRoundness);
        profile.spillInnerCosine = std::cos(
            lobes.spillInnerConeDegrees * DegreesToHalfRadians);
        profile.spillOuterCosine = std::cos(
            lobes.spillOuterConeDegrees * DegreesToHalfRadians);
        profile.spillWeight = settings.realisticLens
            ? 1.f - settings.hotspotStrength
            : 1.f;
        profile.hotspotWeight = settings.realisticLens
            ? settings.hotspotStrength
            : 0.f;
        profile.hotspotInnerCosine = std::cos(
            lobes.hotspotInnerConeDegrees * DegreesToHalfRadians);
        profile.hotspotOuterCosine = std::cos(
            lobes.hotspotOuterConeDegrees * DegreesToHalfRadians);
        profile.emitterRadiusMeters =
            ResolveFlashlightEmitterRadiusMeters(
                settings.angularSizeDegrees);
        profile.active = 1.f;
        return profile;
    }

    [[nodiscard]] inline float AdvanceFlashlightMountExtension(
        float current,
        float target,
        float deltaSeconds)
    {
        current = std::isfinite(current)
            ? std::clamp(current, 0.f, 1.f)
            : 0.f;
        target = std::isfinite(target)
            ? std::clamp(target, 0.f, 1.f)
            : 0.f;

        // The collision lookahead already eases retraction spatially before
        // contact, so decreases follow that validated envelope without adding
        // temporal lag. Restoring the authored offset uses a frame-rate-
        // independent half-life to avoid a visible pop.
        if (target <= current)
            return target;
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.f)
            return current;

        const float blend = 1.f - std::exp2(
            -deltaSeconds / FlashlightMountReleaseHalfLifeSeconds);
        return std::clamp(
            current + (target - current) * blend,
            current,
            target);
    }

    [[nodiscard]] inline float AdvanceFlashlightTransition(
        float current,
        bool targetEnabled,
        float deltaSeconds)
    {
        current = std::isfinite(current)
            ? std::clamp(current, 0.f, 1.f)
            : 0.f;
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.f)
            return current;

        const float duration = targetEnabled
            ? FlashlightTurnOnSeconds
            : FlashlightTurnOffSeconds;
        const float direction = targetEnabled ? 1.f : -1.f;
        return std::clamp(
            current + direction * deltaSeconds / duration,
            0.f,
            1.f);
    }

    [[nodiscard]] inline float GetFlashlightEmissionScale(float transition)
    {
        transition = std::isfinite(transition)
            ? std::clamp(transition, 0.f, 1.f)
            : 0.f;
        // Quintic smoothstep keeps the electrical soft-start and decay free
        // from visible slope discontinuities at the fully off/on endpoints.
        return transition * transition * transition *
            (transition * (transition * 6.f - 15.f) + 10.f);
    }

    [[nodiscard]] inline bool ShouldSubmitFlashlight(float transition)
    {
        return GetFlashlightEmissionScale(transition) > 0.f;
    }

    [[nodiscard]] inline float GetFlashlightAimCorrectionBlend(
        float deltaSeconds,
        float correctionSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.f)
            return 0.f;
        const float safeCorrectionSeconds = std::clamp(
            std::isfinite(correctionSeconds)
                ? correctionSeconds
                : DefaultFlashlightSettings.aimCorrectionSeconds,
            FlashlightMinimumAimCorrectionSeconds,
            FlashlightMaximumAimCorrectionSeconds);
        return std::clamp(
            1.f - std::exp2(-deltaSeconds / safeCorrectionSeconds),
            0.f,
            1.f);
    }

    [[nodiscard]] inline float AdvanceFlashlightSwayTime(
        float currentSeconds,
        float deltaSeconds)
    {
        currentSeconds = std::isfinite(currentSeconds)
            ? std::max(currentSeconds, 0.f)
            : 0.f;
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.f)
            return currentSeconds;

        const float next = std::fmod(
            currentSeconds + deltaSeconds,
            FlashlightSwayPeriodSeconds);
        return next >= 0.f
            ? next
            : next + FlashlightSwayPeriodSeconds;
    }

    [[nodiscard]] inline FlashlightSwayOffset ResolveFlashlightSwayOffset(
        float timeSeconds,
        float amplitudeDegrees)
    {
        timeSeconds = std::isfinite(timeSeconds)
            ? timeSeconds
            : 0.f;
        amplitudeDegrees = std::clamp(
            std::isfinite(amplitudeDegrees)
                ? amplitudeDegrees
                : 0.f,
            0.f,
            FlashlightMaximumSwayDegrees);
        return {
            amplitudeDegrees * (
                0.62f * std::sin(timeSeconds * 1.70f) +
                0.23f * std::sin(timeSeconds * 3.10f)),
            amplitudeDegrees * (
                0.52f * std::sin(timeSeconds * 1.30f) +
                0.31f * std::sin(timeSeconds * 2.70f))
        };
    }
}
