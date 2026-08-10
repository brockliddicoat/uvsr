#include "flashlight.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <limits>
#include <string_view>

namespace
{
    bool Near(float left, float right, float tolerance = 1e-5f)
    {
        return std::abs(left - right) <= tolerance;
    }

    float AdvanceForDuration(
        float initial,
        bool targetEnabled,
        float duration,
        int steps)
    {
        float transition = initial;
        for (int step = 0; step < steps; ++step)
        {
            transition = uvsr::AdvanceFlashlightTransition(
                transition,
                targetEnabled,
                duration / float(steps));
        }
        return transition;
    }

    float ResolveBeamApertureDistance(
        float horizontalSlope,
        float verticalSlope,
        float beamRoundness)
    {
        const float exponent =
            uvsr::ResolveFlashlightBeamShapeExponent(
                beamRoundness);
        return std::pow(
            std::pow(std::abs(horizontalSlope), exponent) +
                std::pow(std::abs(verticalSlope), exponent),
            1.f / exponent);
    }
}

int main()
{
    using namespace uvsr;

    assert(AdvanceFlashlightTransition(0.f, true, 0.f) == 0.f);
    assert(AdvanceFlashlightTransition(1.f, false, 0.f) == 1.f);
    assert(AdvanceFlashlightTransition(
        0.5f, true, -1.f) == 0.5f);
    assert(AdvanceFlashlightTransition(
        0.5f,
        true,
        std::numeric_limits<float>::quiet_NaN()) == 0.5f);
    assert(AdvanceFlashlightTransition(
        std::numeric_limits<float>::quiet_NaN(),
        false,
        0.f) == 0.f);

    assert(AdvanceFlashlightTransition(
        0.f, true, FlashlightTurnOnSeconds) == 1.f);
    assert(AdvanceFlashlightTransition(
        1.f, false, FlashlightTurnOffSeconds) == 0.f);
    assert(AdvanceFlashlightTransition(0.f, true, 10.f) == 1.f);
    assert(AdvanceFlashlightTransition(1.f, false, 10.f) == 0.f);

    const float rise30 = AdvanceForDuration(
        0.f, true, 0.1f, 3);
    const float rise60 = AdvanceForDuration(
        0.f, true, 0.1f, 6);
    const float rise144 = AdvanceForDuration(
        0.f, true, 0.1f, 14);
    assert(Near(rise30, rise60));
    assert(Near(rise30, rise144));

    float previousTransition = 0.f;
    float previousEmission = 0.f;
    for (int step = 1; step <= 100; ++step)
    {
        const float transition = float(step) / 100.f;
        const float emission = GetFlashlightEmissionScale(transition);
        assert(transition > previousTransition);
        assert(emission >= previousEmission);
        assert(std::isfinite(emission));
        assert(emission >= 0.f && emission <= 1.f);
        previousTransition = transition;
        previousEmission = emission;
    }
    assert(GetFlashlightEmissionScale(0.f) == 0.f);
    assert(GetFlashlightEmissionScale(1.f) == 1.f);
    assert(GetFlashlightEmissionScale(
        std::numeric_limits<float>::infinity()) == 0.f);

    const float beforeReversal = AdvanceFlashlightTransition(
        0.f, true, FlashlightTurnOnSeconds * 0.75f);
    assert(Near(beforeReversal, 0.75f));
    assert(AdvanceFlashlightTransition(
        beforeReversal, false, 0.f) == beforeReversal);
    const float afterReversal = AdvanceFlashlightTransition(
        beforeReversal, false, 1.f / 60.f);
    assert(afterReversal < beforeReversal);
    assert(afterReversal > 0.f);

    assert(!ShouldSubmitFlashlight(0.f));
    assert(ShouldSubmitFlashlight(1.f));

    assert(!DefaultFlashlightEnabled);
    assert(DefaultFlashlightSettings.realisticLens);
    assert(DefaultFlashlightSettings.castShadows);
    assert(!DefaultFlashlightSettings.outputHitDistance);
    assert(std::string_view(FlashlightPublicName) ==
        "flashlight_1");
    assert(Near(DefaultFlashlightSettings.peakIntensityCandela, 600.f));
    assert(Near(
        DefaultFlashlightSettings.cameraHorizontalOffsetMeters,
        0.17888544f));
    assert(Near(
        DefaultFlashlightSettings.cameraVerticalOffsetMeters,
        -0.08944272f));
    assert(Near(
        std::hypot(
            DefaultFlashlightSettings.cameraHorizontalOffsetMeters,
            DefaultFlashlightSettings.cameraVerticalOffsetMeters),
        0.20f));
    assert(Near(
        FlashlightMinimumCameraHorizontalOffsetMeters,
        -0.40f));
    assert(Near(
        FlashlightMaximumCameraHorizontalOffsetMeters,
        0.40f));
    assert(Near(
        FlashlightMinimumCameraVerticalOffsetMeters,
        -0.40f));
    assert(Near(
        FlashlightMaximumCameraVerticalOffsetMeters,
        0.40f));
    assert(Near(DefaultFlashlightSettings.beamSizeDegrees, 16.f));
    assert(Near(DefaultFlashlightSettings.angularSizeDegrees, 2.8641924f));
    assert(Near(FlashlightMinimumAngularSizeDegrees, 0.f));
    assert(Near(FlashlightMaximumAngularSizeDegrees, 20.f));
    assert(Near(DefaultFlashlightSettings.beamRoundness, 0.80f));
    assert(Near(DefaultFlashlightSettings.edgeSoftness, 0.60f));
    assert(Near(DefaultFlashlightSettings.hotspotSize, 0.40f));
    assert(Near(DefaultFlashlightSettings.hotspotStrength, 0.70f));
    assert(Near(DefaultFlashlightSettings.swayDegrees, 0.20f));
    assert(Near(DefaultFlashlightSettings.aimCorrectionSeconds, 0.05f));
    assert(Near(DefaultFlashlightSettings.colorLinearRed, 1.f));
    assert(Near(DefaultFlashlightSettings.colorLinearGreen, 1.f));
    assert(Near(DefaultFlashlightSettings.colorLinearBlue, 1.f));
    const float defaultEmitterRadius = ResolveFlashlightEmitterRadiusMeters(
        DefaultFlashlightSettings.angularSizeDegrees);
    assert(Near(defaultEmitterRadius, 0.025f, 1e-6f));
    assert(defaultEmitterRadius < FlashlightCameraForwardOffsetMeters);
    assert(ResolveFlashlightEmitterRadiusMeters(0.f) == 0.f);
    assert(ResolveFlashlightEmitterRadiusMeters(
        FlashlightMaximumAngularSizeDegrees) > defaultEmitterRadius);
    const float defaultCollisionRadius =
        ResolveFlashlightCollisionRadiusMeters(
            DefaultFlashlightSettings.angularSizeDegrees,
            0.1f);
    assert(defaultCollisionRadius >= 0.1f);
    assert(defaultCollisionRadius >= defaultEmitterRadius);
    const float maximumCollisionRadius =
        ResolveFlashlightCollisionRadiusMeters(
            FlashlightMaximumAngularSizeDegrees,
            0.1f);
    assert(maximumCollisionRadius >=
        ResolveFlashlightEmitterRadiusMeters(
            FlashlightMaximumAngularSizeDegrees));
    assert(ResolveFlashlightCollisionRadiusMeters(
        0.f,
        std::numeric_limits<float>::quiet_NaN()) ==
            FlashlightMinimumCollisionRadiusMeters);
    assert(FlashlightAimConvergenceDistanceMeters >
        FlashlightCameraForwardOffsetMeters);
    const FlashlightMountPose flashlightMount =
        ResolveFlashlightMountPose(
            DefaultFlashlightSettings.cameraHorizontalOffsetMeters,
            DefaultFlashlightSettings.cameraVerticalOffsetMeters);
    assert(flashlightMount.positionRightMeters ==
        DefaultFlashlightSettings.cameraHorizontalOffsetMeters);
    assert(flashlightMount.positionUpMeters ==
        DefaultFlashlightSettings.cameraVerticalOffsetMeters);
    assert(flashlightMount.positionForwardMeters ==
        FlashlightCameraForwardOffsetMeters);
    assert(flashlightMount.directionRight < 0.f);
    assert(flashlightMount.directionUp > 0.f);
    assert(flashlightMount.directionForward > 0.f);
    const float flashlightMountDirectionLength = std::sqrt(
        flashlightMount.directionRight *
            flashlightMount.directionRight +
        flashlightMount.directionUp *
            flashlightMount.directionUp +
        flashlightMount.directionForward *
            flashlightMount.directionForward);
    assert(Near(flashlightMountDirectionLength, 1.f));
    const float flashlightMountDistance = std::sqrt(
        FlashlightCameraForwardOffsetMeters *
            FlashlightCameraForwardOffsetMeters +
        DefaultFlashlightSettings.cameraHorizontalOffsetMeters *
            DefaultFlashlightSettings.cameraHorizontalOffsetMeters +
        DefaultFlashlightSettings.cameraVerticalOffsetMeters *
            DefaultFlashlightSettings.cameraVerticalOffsetMeters);
    assert(flashlightMountDistance > FlashlightCameraForwardOffsetMeters);
    const float flashlightLateralOffset = std::hypot(
        flashlightMount.positionRightMeters,
        flashlightMount.positionUpMeters);
    const float flashlightMountToTargetDistance =
        std::sqrt(
            flashlightLateralOffset *
                flashlightLateralOffset +
            (
                FlashlightAimConvergenceDistanceMeters -
                FlashlightCameraForwardOffsetMeters) *
                (
                    FlashlightAimConvergenceDistanceMeters -
                    FlashlightCameraForwardOffsetMeters));
    assert(Near(
        flashlightMount.positionRightMeters +
            flashlightMount.directionRight *
                flashlightMountToTargetDistance,
        0.f));
    assert(Near(
        flashlightMount.positionUpMeters +
            flashlightMount.directionUp *
                flashlightMountToTargetDistance,
        0.f));
    assert(Near(
        flashlightMount.positionForwardMeters +
            flashlightMount.directionForward *
                flashlightMountToTargetDistance,
        FlashlightAimConvergenceDistanceMeters));
    const float flashlightConvergenceAngleDegrees =
        std::atan2(
            flashlightLateralOffset,
            FlashlightAimConvergenceDistanceMeters -
                FlashlightCameraForwardOffsetMeters) *
        (180.f / 3.14159265358979323846f);
    assert(flashlightConvergenceAngleDegrees > 0.f);
    assert(Near(flashlightConvergenceAngleDegrees, 1.922f, 1e-3f));
    assert(flashlightConvergenceAngleDegrees <
        FlashlightMinimumBeamSizeDegrees * 0.5f);

    const FlashlightMountPose centeredFlashlightMount =
        ResolveFlashlightMountPose(0.f, 0.f);
    assert(centeredFlashlightMount.positionRightMeters == 0.f);
    assert(centeredFlashlightMount.positionUpMeters == 0.f);
    assert(centeredFlashlightMount.positionForwardMeters ==
        FlashlightCameraForwardOffsetMeters);
    assert(centeredFlashlightMount.directionRight == 0.f);
    assert(centeredFlashlightMount.directionUp == 0.f);
    assert(centeredFlashlightMount.directionForward == 1.f);

    const FlashlightMountPose horizontalOffsetFlashlightMount =
        ResolveFlashlightMountPose(
            FlashlightMaximumCameraHorizontalOffsetMeters,
            0.f);
    assert(horizontalOffsetFlashlightMount.positionRightMeters == 0.40f);
    assert(horizontalOffsetFlashlightMount.positionUpMeters == 0.f);
    assert(horizontalOffsetFlashlightMount.directionRight < 0.f);
    assert(horizontalOffsetFlashlightMount.directionUp == 0.f);

    const FlashlightMountPose verticalOffsetFlashlightMount =
        ResolveFlashlightMountPose(
            0.f,
            FlashlightMaximumCameraVerticalOffsetMeters);
    assert(verticalOffsetFlashlightMount.positionRightMeters == 0.f);
    assert(verticalOffsetFlashlightMount.positionUpMeters == 0.40f);
    assert(verticalOffsetFlashlightMount.directionRight == 0.f);
    assert(verticalOffsetFlashlightMount.directionUp < 0.f);

    const FlashlightMountPose maximumOffsetFlashlightMount =
        ResolveFlashlightMountPose(
            FlashlightMaximumCameraHorizontalOffsetMeters,
            FlashlightMaximumCameraVerticalOffsetMeters);
    const float maximumFlashlightLateralOffset = std::hypot(
        maximumOffsetFlashlightMount.positionRightMeters,
        maximumOffsetFlashlightMount.positionUpMeters);
    assert(Near(
        maximumFlashlightLateralOffset,
        std::hypot(0.40f, 0.40f)));
    assert(maximumOffsetFlashlightMount.positionRightMeters > 0.f);
    assert(maximumOffsetFlashlightMount.positionUpMeters > 0.f);
    const float maximumOffsetDirectionLength = std::sqrt(
        maximumOffsetFlashlightMount.directionRight *
            maximumOffsetFlashlightMount.directionRight +
        maximumOffsetFlashlightMount.directionUp *
            maximumOffsetFlashlightMount.directionUp +
        maximumOffsetFlashlightMount.directionForward *
            maximumOffsetFlashlightMount.directionForward);
    assert(Near(maximumOffsetDirectionLength, 1.f));
    const float maximumConvergenceAngleDegrees = std::atan2(
        maximumFlashlightLateralOffset,
        FlashlightAimConvergenceDistanceMeters -
            FlashlightCameraForwardOffsetMeters) *
        (180.f / 3.14159265358979323846f);
    assert(maximumConvergenceAngleDegrees >
        flashlightConvergenceAngleDegrees);
    assert(maximumConvergenceAngleDegrees <
        DefaultFlashlightSettings.beamSizeDegrees * 0.5f);

    const FlashlightMountPose minimumOffsetFlashlightMount =
        ResolveFlashlightMountPose(-1.f, -1.f);
    assert(minimumOffsetFlashlightMount.positionRightMeters == -0.40f);
    assert(minimumOffsetFlashlightMount.positionUpMeters == -0.40f);
    assert(minimumOffsetFlashlightMount.directionRight > 0.f);
    assert(minimumOffsetFlashlightMount.directionUp > 0.f);

    const FlashlightMountPose maximumClampedFlashlightMount =
        ResolveFlashlightMountPose(1.f, 1.f);
    assert(maximumClampedFlashlightMount.positionRightMeters == 0.40f);
    assert(maximumClampedFlashlightMount.positionUpMeters == 0.40f);

    const FlashlightMountPose invalidHorizontalFlashlightMount =
        ResolveFlashlightMountPose(
            std::numeric_limits<float>::quiet_NaN(),
            0.f);
    assert(invalidHorizontalFlashlightMount.positionRightMeters ==
        DefaultFlashlightSettings.cameraHorizontalOffsetMeters);
    assert(invalidHorizontalFlashlightMount.positionUpMeters == 0.f);
    const FlashlightMountPose invalidVerticalFlashlightMount =
        ResolveFlashlightMountPose(
            0.f,
            std::numeric_limits<float>::infinity());
    assert(invalidVerticalFlashlightMount.positionRightMeters == 0.f);
    assert(invalidVerticalFlashlightMount.positionUpMeters ==
        DefaultFlashlightSettings.cameraVerticalOffsetMeters);

    assert(DefaultFlashlightSettings.colorLinearRed ==
        DefaultFlashlightSettings.colorLinearGreen);
    assert(DefaultFlashlightSettings.colorLinearGreen ==
        DefaultFlashlightSettings.colorLinearBlue);

    FlashlightSettings realisticSettings =
        DefaultFlashlightSettings;
    realisticSettings.realisticLens = true;
    const FlashlightLobeSettings realisticLobes =
        ResolveFlashlightLobeSettings(realisticSettings);
    assert(realisticLobes.spillIntensityCandela > 0.f);
    assert(realisticLobes.hotspotIntensityCandela > 0.f);
    assert(Near(
        realisticLobes.spillIntensityCandela +
            realisticLobes.hotspotIntensityCandela,
        realisticSettings.peakIntensityCandela));
    assert(realisticLobes.spillInnerConeDegrees > 0.f);
    assert(realisticLobes.spillInnerConeDegrees <
        realisticLobes.spillOuterConeDegrees);
    assert(realisticLobes.hotspotInnerConeDegrees > 0.f);
    assert(realisticLobes.hotspotInnerConeDegrees <
        realisticLobes.hotspotOuterConeDegrees);
    assert(realisticLobes.hotspotOuterConeDegrees <
        realisticLobes.spillOuterConeDegrees);

    FlashlightSettings simpleSettings = DefaultFlashlightSettings;
    simpleSettings.realisticLens = false;
    const FlashlightLobeSettings simpleLobes =
        ResolveFlashlightLobeSettings(simpleSettings);
    assert(Near(
        simpleLobes.spillIntensityCandela,
        simpleSettings.peakIntensityCandela));
    assert(simpleLobes.hotspotIntensityCandela == 0.f);
    assert(simpleLobes.hotspotInnerConeDegrees == 0.f);
    assert(simpleLobes.hotspotOuterConeDegrees == 0.f);

    FlashlightSettings invalidSettings = DefaultFlashlightSettings;
    invalidSettings.peakIntensityCandela =
        std::numeric_limits<float>::quiet_NaN();
    invalidSettings.rangeMeters = -1.f;
    invalidSettings.cameraHorizontalOffsetMeters =
        std::numeric_limits<float>::quiet_NaN();
    invalidSettings.cameraVerticalOffsetMeters =
        std::numeric_limits<float>::quiet_NaN();
    invalidSettings.beamSizeDegrees =
        std::numeric_limits<float>::infinity();
    invalidSettings.angularSizeDegrees =
        std::numeric_limits<float>::quiet_NaN();
    invalidSettings.beamRoundness =
        std::numeric_limits<float>::quiet_NaN();
    invalidSettings.edgeSoftness = 2.f;
    invalidSettings.hotspotSize = 0.f;
    invalidSettings.hotspotStrength = 2.f;
    invalidSettings.swayDegrees = -1.f;
    invalidSettings.aimCorrectionSeconds = 0.f;
    const FlashlightSettings sanitized =
        SanitizeFlashlightSettings(invalidSettings);
    assert(sanitized.peakIntensityCandela ==
        DefaultFlashlightSettings.peakIntensityCandela);
    assert(sanitized.rangeMeters == FlashlightMinimumRangeMeters);
    assert(sanitized.cameraHorizontalOffsetMeters ==
        DefaultFlashlightSettings.cameraHorizontalOffsetMeters);
    assert(sanitized.cameraVerticalOffsetMeters ==
        DefaultFlashlightSettings.cameraVerticalOffsetMeters);
    assert(sanitized.beamSizeDegrees ==
        DefaultFlashlightSettings.beamSizeDegrees);
    assert(sanitized.angularSizeDegrees ==
        DefaultFlashlightSettings.angularSizeDegrees);
    assert(sanitized.beamRoundness ==
        DefaultFlashlightSettings.beamRoundness);
    assert(sanitized.edgeSoftness == 1.f);
    assert(sanitized.hotspotSize == FlashlightMinimumHotspotSize);
    assert(sanitized.hotspotStrength ==
        FlashlightMaximumHotspotStrength);
    assert(sanitized.swayDegrees == 0.f);
    assert(sanitized.aimCorrectionSeconds ==
        FlashlightMinimumAimCorrectionSeconds);

    FlashlightSettings minimumOffsetSettings = DefaultFlashlightSettings;
    minimumOffsetSettings.cameraHorizontalOffsetMeters = -1.f;
    minimumOffsetSettings.cameraVerticalOffsetMeters = -1.f;
    const FlashlightSettings minimumOffsetSanitized =
        SanitizeFlashlightSettings(minimumOffsetSettings);
    assert(minimumOffsetSanitized.cameraHorizontalOffsetMeters ==
        FlashlightMinimumCameraHorizontalOffsetMeters);
    assert(minimumOffsetSanitized.cameraVerticalOffsetMeters ==
        FlashlightMinimumCameraVerticalOffsetMeters);

    FlashlightSettings maximumOffsetSettings = DefaultFlashlightSettings;
    maximumOffsetSettings.cameraHorizontalOffsetMeters = 1.f;
    maximumOffsetSettings.cameraVerticalOffsetMeters = 1.f;
    const FlashlightSettings maximumOffsetSanitized =
        SanitizeFlashlightSettings(maximumOffsetSettings);
    assert(maximumOffsetSanitized.cameraHorizontalOffsetMeters ==
        FlashlightMaximumCameraHorizontalOffsetMeters);
    assert(maximumOffsetSanitized.cameraVerticalOffsetMeters ==
        FlashlightMaximumCameraVerticalOffsetMeters);

    maximumOffsetSettings.cameraHorizontalOffsetMeters =
        std::numeric_limits<float>::infinity();
    maximumOffsetSettings.cameraVerticalOffsetMeters =
        -std::numeric_limits<float>::infinity();
    const FlashlightSettings invalidOffsetSanitized =
        SanitizeFlashlightSettings(maximumOffsetSettings);
    assert(invalidOffsetSanitized.cameraHorizontalOffsetMeters ==
        DefaultFlashlightSettings.cameraHorizontalOffsetMeters);
    assert(invalidOffsetSanitized.cameraVerticalOffsetMeters ==
        DefaultFlashlightSettings.cameraVerticalOffsetMeters);

    assert(Near(
        ResolveFlashlightBeamShapeExponent(1.f),
        UVSR_FLASHLIGHT_MIN_SHAPE_EXPONENT));
    assert(Near(
        ResolveFlashlightBeamShapeExponent(0.f),
        UVSR_FLASHLIGHT_MAX_SHAPE_EXPONENT));
    float previousExponent =
        ResolveFlashlightBeamShapeExponent(0.f);
    for (int step = 1; step <= 100; ++step)
    {
        const float exponent =
            ResolveFlashlightBeamShapeExponent(
                float(step) / 100.f);
        assert(std::isfinite(exponent));
        assert(exponent <= previousExponent);
        assert(exponent >=
            UVSR_FLASHLIGHT_MIN_SHAPE_EXPONENT);
        assert(exponent <=
            UVSR_FLASHLIGHT_MAX_SHAPE_EXPONENT);
        previousExponent = exponent;
    }
    for (const float roundness : { 0.f, 0.25f, 0.5f, 1.f })
    {
        FlashlightSettings profileSettings =
            DefaultFlashlightSettings;
        profileSettings.beamRoundness = roundness;
        const FlashlightBeamProfile profile =
            ResolveFlashlightBeamProfile(
                profileSettings,
                1.f,
                0.f,
                0.f);
        assert(Near(
            profile.shapeExponent,
            ResolveFlashlightBeamShapeExponent(roundness),
            2e-4f));
        assert(profile.active == 1.f);
        assert(profile.emitterRadiusMeters > 0.f);
        assert(profile.spillInnerCosine >
            profile.spillOuterCosine);
        assert(Near(
            profile.spillWeight + profile.hotspotWeight,
            1.f));
    }

    for (const float roundness : { 0.f, 0.5f, 1.f })
    {
        assert(Near(
            ResolveBeamApertureDistance(
                1.f,
                0.f,
                roundness),
            1.f));
        assert(Near(
            ResolveBeamApertureDistance(
                0.f,
                1.f,
                roundness),
            1.f));
        assert(Near(
            ResolveBeamApertureDistance(
                0.35f,
                0.65f,
                roundness),
            ResolveBeamApertureDistance(
                -0.35f,
                -0.65f,
                roundness)));
    }
    constexpr float CircleDiagonal =
        0.7071067811865475f;
    assert(Near(
        ResolveBeamApertureDistance(
            CircleDiagonal,
            CircleDiagonal,
            1.f),
        1.f));
    assert(ResolveBeamApertureDistance(
        0.95f,
        0.95f,
        0.f) < 1.f);
    assert(ResolveBeamApertureDistance(
        0.95f,
        0.95f,
        1.f) > 1.f);
    float previousDiagonalDistance =
        ResolveBeamApertureDistance(0.8f, 0.8f, 0.f);
    for (int step = 1; step <= 100; ++step)
    {
        const float diagonalDistance =
            ResolveBeamApertureDistance(
                0.8f,
                0.8f,
                float(step) / 100.f);
        assert(diagonalDistance >=
            previousDiagonalDistance - 1e-6f);
        previousDiagonalDistance = diagonalDistance;
    }

    FlashlightSettings tightEdge =
        DefaultFlashlightSettings;
    tightEdge.edgeSoftness = 0.f;
    FlashlightSettings broadEdge = tightEdge;
    broadEdge.edgeSoftness = 1.f;
    const FlashlightLobeSettings tightLobes =
        ResolveFlashlightLobeSettings(tightEdge);
    const FlashlightLobeSettings broadLobes =
        ResolveFlashlightLobeSettings(broadEdge);
    assert(tightEdge.beamRoundness ==
        broadEdge.beamRoundness);
    assert(tightLobes.spillOuterConeDegrees ==
        broadLobes.spillOuterConeDegrees);
    assert(tightLobes.spillInnerConeDegrees >
        broadLobes.spillInnerConeDegrees);

    const float blendWhole = GetFlashlightAimCorrectionBlend(
        0.1f,
        DefaultFlashlightSettings.aimCorrectionSeconds);
    const float blendHalf = GetFlashlightAimCorrectionBlend(
        0.05f,
        DefaultFlashlightSettings.aimCorrectionSeconds);
    assert(Near(
        blendWhole,
        1.f - (1.f - blendHalf) * (1.f - blendHalf)));
    assert(GetFlashlightAimCorrectionBlend(0.f, 0.1f) == 0.f);
    assert(GetFlashlightAimCorrectionBlend(
        std::numeric_limits<float>::quiet_NaN(),
        0.1f) == 0.f);

    const float swayWhole = AdvanceFlashlightSwayTime(0.f, 1.f);
    float swayPartitioned = 0.f;
    for (int step = 0; step < 60; ++step)
    {
        swayPartitioned = AdvanceFlashlightSwayTime(
            swayPartitioned,
            1.f / 60.f);
    }
    assert(Near(swayWhole, swayPartitioned, 1e-4f));
    constexpr float SwayBoundaryEpsilon = 1e-3f;
    const float wrappedSwayTime = AdvanceFlashlightSwayTime(
        FlashlightSwayPeriodSeconds - SwayBoundaryEpsilon,
        SwayBoundaryEpsilon * 2.f);
    assert(Near(
        wrappedSwayTime,
        SwayBoundaryEpsilon,
        1e-4f));
    const FlashlightSwayOffset beforeSwayWrap =
        ResolveFlashlightSwayOffset(
            FlashlightSwayPeriodSeconds -
                SwayBoundaryEpsilon,
            FlashlightMaximumSwayDegrees);
    const FlashlightSwayOffset afterSwayWrap =
        ResolveFlashlightSwayOffset(
            wrappedSwayTime,
            FlashlightMaximumSwayDegrees);
    assert(std::abs(
        beforeSwayWrap.yawDegrees -
            afterSwayWrap.yawDegrees) < 0.02f);
    assert(std::abs(
        beforeSwayWrap.pitchDegrees -
            afterSwayWrap.pitchDegrees) < 0.02f);
    const FlashlightSwayOffset zeroTimeSway =
        ResolveFlashlightSwayOffset(
            0.f,
            FlashlightMaximumSwayDegrees);
    assert(zeroTimeSway.yawDegrees == 0.f);
    assert(zeroTimeSway.pitchDegrees == 0.f);
    for (int sample = 0; sample < 1000; ++sample)
    {
        const FlashlightSwayOffset offset =
            ResolveFlashlightSwayOffset(
                float(sample) * 0.01f,
                FlashlightMaximumSwayDegrees);
        assert(std::isfinite(offset.yawDegrees));
        assert(std::isfinite(offset.pitchDegrees));
        assert(std::abs(offset.yawDegrees) <=
            FlashlightMaximumSwayDegrees);
        assert(std::abs(offset.pitchDegrees) <=
            FlashlightMaximumSwayDegrees);
    }
    return 0;
}
