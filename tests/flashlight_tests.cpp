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

    assert(!ShouldRenderFlashlightShadow(0.f));
    assert(ShouldRenderFlashlightShadow(0.01f));
    assert(ShouldRenderFlashlightShadow(1.f));
    assert(!ShouldRenderFlashlightShadow(1.f, false));
    assert(!ShouldRenderFlashlightShadow(
        std::numeric_limits<float>::quiet_NaN()));
    assert(!ShouldSubmitFlashlight(0.f));
    assert(ShouldSubmitFlashlight(1.f));

    assert(DefaultFlashlightEnabled);
    assert(DefaultFlashlightSettings.realisticLens);
    assert(DefaultFlashlightSettings.castShadows);
    assert(std::string_view(FlashlightPublicName) ==
        "flashlight_1");
    assert(Near(DefaultFlashlightSettings.peakIntensityCandela, 600.f));
    assert(DefaultFlashlightSettings.rangeMeters >
        FlashlightShadowNearPlaneMeters);
    assert(Near(
        DefaultFlashlightSettings.cameraLateralOffsetMeters,
        FlashlightDefaultCameraLateralOffsetMeters));
    assert(Near(
        DefaultFlashlightSettings.cameraLateralOffsetMeters,
        0.20f));
    assert(Near(
        DefaultFlashlightSettings.cameraLateralOffsetMeters,
        std::hypot(
            FlashlightCameraRightOffsetMeters,
            FlashlightCameraDownOffsetMeters)));
    assert(FlashlightMinimumCameraLateralOffsetMeters == 0.f);
    assert(Near(
        FlashlightMaximumCameraLateralOffsetMeters,
        0.40f));
    assert(FlashlightMaximumCameraLateralOffsetMeters >
        DefaultFlashlightSettings.cameraLateralOffsetMeters);
    assert(Near(DefaultFlashlightSettings.beamSizeDegrees, 25.f));
    assert(Near(DefaultFlashlightSettings.beamRoundness, 0.70f));
    assert(Near(DefaultFlashlightSettings.edgeSoftness, 0.60f));
    assert(Near(DefaultFlashlightSettings.hotspotSize, 0.40f));
    assert(Near(DefaultFlashlightSettings.hotspotStrength, 0.70f));
    assert(Near(DefaultFlashlightSettings.swayDegrees, 0.20f));
    assert(Near(DefaultFlashlightSettings.aimCorrectionSeconds, 0.05f));
    assert(Near(DefaultFlashlightSettings.colorLinearRed, 1.f));
    assert(Near(DefaultFlashlightSettings.colorLinearGreen, 0.80f));
    assert(Near(DefaultFlashlightSettings.colorLinearBlue, 0.65f));
    assert(FlashlightEmitterRadiusMeters > 0.f);
    assert(FlashlightEmitterRadiusMeters <
        FlashlightShadowNearPlaneMeters);
    assert(FlashlightShadowNearPlaneMeters <
        FlashlightCameraForwardOffsetMeters);
    assert(FlashlightCameraRightOffsetMeters > 0.f);
    assert(FlashlightCameraDownOffsetMeters > 0.f);
    assert(FlashlightAimConvergenceDistanceMeters >
        FlashlightCameraForwardOffsetMeters);
    const FlashlightMountPose flashlightMount =
        ResolveFlashlightMountPose(
            DefaultFlashlightSettings.cameraLateralOffsetMeters);
    assert(flashlightMount.positionRightMeters ==
        FlashlightCameraRightOffsetMeters);
    assert(flashlightMount.positionUpMeters ==
        -FlashlightCameraDownOffsetMeters);
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
        FlashlightCameraRightOffsetMeters *
            FlashlightCameraRightOffsetMeters +
        FlashlightCameraDownOffsetMeters *
            FlashlightCameraDownOffsetMeters);
    assert(Near(
        flashlightMountDistance,
        std::hypot(
            DefaultFlashlightSettings.cameraLateralOffsetMeters,
            FlashlightCameraForwardOffsetMeters)));
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
    constexpr float ShadowTestCasterDistanceMeters = 2.f;
    constexpr float ShadowTestReceiverDistanceMeters = 3.f;
    const float flashlightShadowParallaxMeters =
        flashlightLateralOffset *
        (
            ShadowTestReceiverDistanceMeters -
            ShadowTestCasterDistanceMeters) /
        (
            ShadowTestCasterDistanceMeters -
            FlashlightCameraForwardOffsetMeters);
    assert(Near(
        flashlightShadowParallaxMeters,
        0.1020408f,
        1e-5f));

    const FlashlightMountPose centeredFlashlightMount =
        ResolveFlashlightMountPose(
            FlashlightMinimumCameraLateralOffsetMeters);
    assert(centeredFlashlightMount.positionRightMeters == 0.f);
    assert(centeredFlashlightMount.positionUpMeters == 0.f);
    assert(centeredFlashlightMount.positionForwardMeters ==
        FlashlightCameraForwardOffsetMeters);
    assert(centeredFlashlightMount.directionRight == 0.f);
    assert(centeredFlashlightMount.directionUp == 0.f);
    assert(centeredFlashlightMount.directionForward == 1.f);

    const FlashlightMountPose maximumOffsetFlashlightMount =
        ResolveFlashlightMountPose(
            FlashlightMaximumCameraLateralOffsetMeters);
    const float maximumFlashlightLateralOffset = std::hypot(
        maximumOffsetFlashlightMount.positionRightMeters,
        maximumOffsetFlashlightMount.positionUpMeters);
    assert(Near(
        maximumFlashlightLateralOffset,
        FlashlightMaximumCameraLateralOffsetMeters));
    assert(maximumOffsetFlashlightMount.positionRightMeters > 0.f);
    assert(maximumOffsetFlashlightMount.positionUpMeters < 0.f);
    assert(Near(
        maximumOffsetFlashlightMount.positionRightMeters /
            -maximumOffsetFlashlightMount.positionUpMeters,
        2.f));
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
    assert(maximumConvergenceAngleDegrees <
        FlashlightMinimumBeamSizeDegrees * 0.5f);
    assert(Near(maximumConvergenceAngleDegrees, 3.839f, 1e-3f));
    const float minimumFarAxisConvergenceDistanceMeters =
        FlashlightCameraForwardOffsetMeters +
        FlashlightMaximumCameraLateralOffsetMeters /
            std::tan(
                FlashlightMinimumBeamSizeDegrees * 0.5f *
                (3.14159265358979323846f / 180.f));
    assert(FlashlightAimConvergenceDistanceMeters >
        minimumFarAxisConvergenceDistanceMeters);
    const float maximumFlashlightShadowParallaxMeters =
        maximumFlashlightLateralOffset *
        (
            ShadowTestReceiverDistanceMeters -
            ShadowTestCasterDistanceMeters) /
        (
            ShadowTestCasterDistanceMeters -
            FlashlightCameraForwardOffsetMeters);
    assert(Near(
        maximumFlashlightShadowParallaxMeters,
        0.2040816f,
        1e-5f));
    assert(Near(
        maximumFlashlightShadowParallaxMeters,
        flashlightShadowParallaxMeters * 2.f));
    assert(FlashlightShadowCollisionNearScale > 0.f);
    assert(FlashlightShadowCollisionNearScale < 0.5f);
    assert(DefaultFlashlightSettings.colorLinearRed >=
        DefaultFlashlightSettings.colorLinearGreen);
    assert(DefaultFlashlightSettings.colorLinearGreen >=
        DefaultFlashlightSettings.colorLinearBlue);
    assert(FlashlightShadowMapResolution >= 1024);

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
    invalidSettings.cameraLateralOffsetMeters =
        std::numeric_limits<float>::quiet_NaN();
    invalidSettings.beamSizeDegrees =
        std::numeric_limits<float>::infinity();
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
    assert(sanitized.cameraLateralOffsetMeters ==
        DefaultFlashlightSettings.cameraLateralOffsetMeters);
    assert(sanitized.beamSizeDegrees ==
        DefaultFlashlightSettings.beamSizeDegrees);
    assert(sanitized.beamRoundness ==
        DefaultFlashlightSettings.beamRoundness);
    assert(sanitized.edgeSoftness == 1.f);
    assert(sanitized.hotspotSize == FlashlightMinimumHotspotSize);
    assert(sanitized.hotspotStrength ==
        FlashlightMaximumHotspotStrength);
    assert(sanitized.swayDegrees == 0.f);
    assert(sanitized.aimCorrectionSeconds ==
        FlashlightMinimumAimCorrectionSeconds);

    FlashlightSettings minimumOffsetSettings =
        DefaultFlashlightSettings;
    minimumOffsetSettings.cameraLateralOffsetMeters = -1.f;
    assert(SanitizeFlashlightSettings(minimumOffsetSettings).
        cameraLateralOffsetMeters ==
        FlashlightMinimumCameraLateralOffsetMeters);
    FlashlightSettings maximumOffsetSettings =
        DefaultFlashlightSettings;
    maximumOffsetSettings.cameraLateralOffsetMeters = 1.f;
    assert(SanitizeFlashlightSettings(maximumOffsetSettings).
        cameraLateralOffsetMeters ==
        FlashlightMaximumCameraLateralOffsetMeters);
    maximumOffsetSettings.cameraLateralOffsetMeters =
        std::numeric_limits<float>::infinity();
    assert(SanitizeFlashlightSettings(maximumOffsetSettings).
        cameraLateralOffsetMeters ==
        DefaultFlashlightSettings.cameraLateralOffsetMeters);

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
        const float encodedRadius =
            EncodeFlashlightBeamShapeRadius(roundness);
        assert(encodedRadius <= -(
            UVSR_FLASHLIGHT_SHAPE_RADIUS_TAG +
            UVSR_FLASHLIGHT_MIN_SHAPE_EXPONENT));
        assert(Near(
            DecodeFlashlightBeamShapeExponent(encodedRadius),
            ResolveFlashlightBeamShapeExponent(roundness),
            2e-4f));
    }
    assert(DecodeFlashlightBeamShapeExponent(
        std::numeric_limits<float>::quiet_NaN()) ==
        UVSR_FLASHLIGHT_MIN_SHAPE_EXPONENT);
    for (const float component :
        { -1.f, -0.5f, 0.f, 0.5f, 1.f })
    {
        assert(Near(
            DecodeFlashlightBeamAxisComponent(
                EncodeFlashlightBeamAxisComponent(component)),
            component,
            1.f / UVSR_FLASHLIGHT_AXIS_QUANTIZATION));
    }
    assert(EncodeFlashlightBeamAxisComponent(
        std::numeric_limits<float>::quiet_NaN()) == 0);

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
