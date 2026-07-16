#include "camera_controllers.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace donut::math;
using namespace uvsr;

namespace
{
    bool Check(bool condition, const char* description)
    {
        if (!condition)
            std::fprintf(stderr, "FAILED: %s\n", description);
        return condition;
    }

    bool NearlyEqual(float actual, float expected, float tolerance = 1e-4f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    void AnimateFrames(UvsrThirdPersonCamera& camera, int frameCount)
    {
        constexpr float FrameTime = 1.f / 60.f;
        for (int frame = 0; frame < frameCount; ++frame)
            camera.Animate(FrameTime);
    }
}

int main()
{
    bool passed = true;

    passed &= Check(std::string(GetCameraModeLabel(CameraMode::ThirdPerson)) == "Freelook",
        "the interactive camera mode is labeled Freelook");
    passed &= Check(std::string(GetCameraModeLabel(CameraMode::Static)) == "Locked",
        "the noninteractive camera mode is labeled Locked");
    passed &= Check(
        SelectableCameraModes.size() == 2 &&
        SelectableCameraModes[0] == CameraMode::ThirdPerson &&
        SelectableCameraModes[1] == CameraMode::Static,
        "only Freelook and Locked are selectable camera modes");

    const float3 simplifiedPosition(11.f, 7.7f, -2.2f);
    const float3 simplifiedDirection(-0.707106769f, 0.f, 0.707106769f);
    const float3 simplifiedUp(0.f, 1.f, 0.f);
    const float3 simplifiedRight(-0.707106769f, 0.f, -0.707106769f);
    UvsrThirdPersonCamera simplifiedFreelookCamera;
    simplifiedFreelookCamera.SetExactPose(
        simplifiedPosition,
        simplifiedDirection,
        simplifiedUp,
        simplifiedRight);
    const affine3& simplifiedView = simplifiedFreelookCamera.GetWorldToViewMatrix();
    passed &= Check(
        all(simplifiedFreelookCamera.GetPosition() == simplifiedPosition) &&
        all(simplifiedFreelookCamera.GetDir() == simplifiedDirection) &&
        all(simplifiedFreelookCamera.GetUp() == simplifiedUp),
        "Freelook preserves the simplified camera pose exactly");
    passed &= Check(
        all(simplifiedView.m_linear.row0 ==
            float3(simplifiedRight.x, simplifiedUp.x, simplifiedDirection.x)) &&
        all(simplifiedView.m_linear.row1 ==
            float3(simplifiedRight.y, simplifiedUp.y, simplifiedDirection.y)) &&
        all(simplifiedView.m_linear.row2 ==
            float3(simplifiedRight.z, simplifiedUp.z, simplifiedDirection.z)),
        "Freelook preserves the simplified right/up/direction framing basis");

    StaticViewCamera simplifiedLockedCamera;
    simplifiedLockedCamera.SetExactPose(
        simplifiedPosition,
        simplifiedDirection,
        simplifiedUp,
        simplifiedRight);
    passed &= Check(simplifiedLockedCamera.GetWorldToViewMatrix() == simplifiedView,
        "Locked reproduces the simplified Freelook spawn matrix exactly");

    UvsrFirstPersonCamera firstPerson(true);
    firstPerson.LookTo(float3(0.f), float3(1.f, 0.f, 0.f));
    firstPerson.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_PRESS, 0);
    firstPerson.Animate(1.f);
    passed &= Check(NearlyEqual(firstPerson.GetPosition().x, 6.f),
        "first-person base movement is 6 units per second");

    firstPerson.KeyboardUpdate(GLFW_KEY_LEFT_SHIFT, 0, GLFW_PRESS, 0);
    const float sprintStart = firstPerson.GetPosition().x;
    firstPerson.Animate(1.f);
    passed &= Check(NearlyEqual(firstPerson.GetPosition().x - sprintStart, 12.f),
        "first-person Shift sprint is 12 units per second");
    firstPerson.KeyboardUpdate(GLFW_KEY_LEFT_SHIFT, 0, GLFW_RELEASE, 0);
    firstPerson.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_RELEASE, 0);

    const float3 firstDirectionBeforeArrow = firstPerson.GetDir();
    firstPerson.KeyboardUpdate(GLFW_KEY_LEFT, 0, GLFW_PRESS, 0);
    firstPerson.Animate(0.25f);
    firstPerson.KeyboardUpdate(GLFW_KEY_LEFT, 0, GLFW_RELEASE, 0);
    passed &= Check(lengthSquared(firstPerson.GetDir() - firstDirectionBeforeArrow) > 0.01f,
        "arrow keys rotate the first-person view");

    UvsrFirstPersonCamera pivot(false);
    pivot.LookTo(float3(2.f, 3.f, 4.f), float3(1.f, 0.f, 0.f));
    const float3 pivotPosition = pivot.GetPosition();
    const float3 pivotDirection = pivot.GetDir();
    pivot.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_PRESS, 0);
    pivot.KeyboardUpdate(GLFW_KEY_UP, 0, GLFW_PRESS, 0);
    pivot.Animate(0.25f);
    pivot.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_RELEASE, 0);
    pivot.KeyboardUpdate(GLFW_KEY_UP, 0, GLFW_RELEASE, 0);
    passed &= Check(all(pivot.GetPosition() == pivotPosition),
        "pivot camera rejects translation input");
    passed &= Check(lengthSquared(pivot.GetDir() - pivotDirection) > 0.01f,
        "pivot camera accepts look input");

    UvsrThirdPersonCamera thirdPerson;
    thirdPerson.LookTo(float3(0.f, 0.f, -10.f), float3(0.f, 0.f, 1.f));
    thirdPerson.ResetZoomReferenceDistance(10.f);
    const float3 positionBeforeZoom = thirdPerson.GetPosition();
    const float wheelStep = thirdPerson.GetBaseWheelStepDistance();
    thirdPerson.MouseScrollUpdate(0.0, 1.0);
    AnimateFrames(thirdPerson, 1);
    const float firstWheelFrameTravel = dot(
        thirdPerson.GetPosition() - positionBeforeZoom,
        thirdPerson.GetDir());
    passed &= Check(
        firstWheelFrameTravel > 0.f &&
        firstWheelFrameTravel < wheelStep * 0.5f,
        "third-person wheel dolly begins with a small damped movement");
    AnimateFrames(thirdPerson, 180);
    const float settledWheelTravel = dot(
        thirdPerson.GetPosition() - positionBeforeZoom,
        thirdPerson.GetDir());
    passed &= Check(NearlyEqual(settledWheelTravel, wheelStep, 2e-4f),
        "third-person wheel dolly settles to the requested small step");
    passed &= Check(thirdPerson.GetDollyScale() < 1.f,
        "third-person inward wheel dolly lowers close-range sensitivity");

    UvsrThirdPersonCamera keyboardDolly;
    keyboardDolly.LookTo(float3(0.f, 0.f, -10.f), float3(0.f, 0.f, 1.f));
    keyboardDolly.ResetZoomReferenceDistance(10.f);
    keyboardDolly.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_PRESS, 0);
    const float3 keyboardStart = keyboardDolly.GetPosition();
    AnimateFrames(keyboardDolly, 1);
    const float firstKeyboardStep = dot(
        keyboardDolly.GetPosition() - keyboardStart,
        keyboardDolly.GetDir());
    const float3 afterFirstKeyboardFrame = keyboardDolly.GetPosition();
    AnimateFrames(keyboardDolly, 1);
    const float secondKeyboardStep = dot(
        keyboardDolly.GetPosition() - afterFirstKeyboardFrame,
        keyboardDolly.GetDir());
    passed &= Check(
        firstKeyboardStep > 0.f && secondKeyboardStep > firstKeyboardStep,
        "third-person W dolly accelerates smoothly instead of jumping");
    AnimateFrames(keyboardDolly, 58);
    passed &= Check(NearlyEqual(keyboardDolly.GetDollyScale(), 0.94f, 2e-3f),
        "third-person sustained W dolly lowers sensitivity gently and linearly");
    const float velocityBeforeRelease = keyboardDolly.GetKeyboardDollyVelocity();
    const float3 releasePosition = keyboardDolly.GetPosition();
    keyboardDolly.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_RELEASE, 0);
    AnimateFrames(keyboardDolly, 1);
    passed &= Check(
        dot(keyboardDolly.GetPosition() - releasePosition,
            keyboardDolly.GetDir()) > 0.f &&
        keyboardDolly.GetKeyboardDollyVelocity() < velocityBeforeRelease,
        "third-person W dolly decelerates smoothly on release");
    AnimateFrames(keyboardDolly, 180);
    passed &= Check(
        std::abs(keyboardDolly.GetKeyboardDollyVelocity()) < 1e-4f,
        "third-person keyboard dolly reaches rest in finite time");

    const float3 beforeBackwardDolly = keyboardDolly.GetPosition();
    keyboardDolly.KeyboardUpdate(GLFW_KEY_S, 0, GLFW_PRESS, 0);
    AnimateFrames(keyboardDolly, 60);
    keyboardDolly.KeyboardUpdate(GLFW_KEY_S, 0, GLFW_RELEASE, 0);
    passed &= Check(
        dot(keyboardDolly.GetPosition() - beforeBackwardDolly,
            keyboardDolly.GetDir()) < 0.f,
        "third-person S dollies smoothly backward");

    UvsrThirdPersonCamera sustainedDolly;
    sustainedDolly.LookTo(float3(0.f, 0.f, -10.f), float3(0.f, 0.f, 1.f));
    sustainedDolly.ResetZoomReferenceDistance(10.f);
    sustainedDolly.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_PRESS, 0);
    AnimateFrames(sustainedDolly, 900);
    passed &= Check(
        NearlyEqual(sustainedDolly.GetDollyScale(), 0.4f) &&
        NearlyEqual(sustainedDolly.GetKeyboardDollyVelocity(), 0.64f, 1e-3f),
        "third-person sustained W dolly holds the doubled minimum cruise speed");

    const auto measureSteadyFreelookSpeed = [](
        int movementKey,
        int shiftKey,
        bool strafe)
    {
        UvsrThirdPersonCamera camera;
        camera.LookTo(float3(0.f, 0.f, -10.f), float3(0.f, 0.f, 1.f));
        camera.ResetZoomReferenceDistance(10.f);
        if (shiftKey != GLFW_KEY_UNKNOWN)
            camera.KeyboardUpdate(shiftKey, 0, GLFW_PRESS, 0);
        camera.KeyboardUpdate(movementKey, 0, GLFW_PRESS, 0);
        AnimateFrames(camera, 120);
        return std::abs(strafe
            ? camera.GetKeyboardStrafeVelocity()
            : camera.GetKeyboardDollyVelocity());
    };

    const float normalForwardSpeed = measureSteadyFreelookSpeed(
        GLFW_KEY_W,
        GLFW_KEY_UNKNOWN,
        false);
    const float boostedForwardSpeed = measureSteadyFreelookSpeed(
        GLFW_KEY_W,
        GLFW_KEY_LEFT_SHIFT,
        false);
    const float normalBackwardSpeed = measureSteadyFreelookSpeed(
        GLFW_KEY_S,
        GLFW_KEY_UNKNOWN,
        false);
    const float boostedBackwardSpeed = measureSteadyFreelookSpeed(
        GLFW_KEY_S,
        GLFW_KEY_RIGHT_SHIFT,
        false);
    passed &= Check(
        NearlyEqual(boostedForwardSpeed, normalForwardSpeed * 2.f, 2e-3f) &&
        NearlyEqual(boostedBackwardSpeed, normalBackwardSpeed * 2.f, 2e-3f),
        "either Shift key doubles steady Freelook W/S dolly speed");

    const float normalLeftSpeed = measureSteadyFreelookSpeed(
        GLFW_KEY_A,
        GLFW_KEY_UNKNOWN,
        true);
    const float boostedLeftSpeed = measureSteadyFreelookSpeed(
        GLFW_KEY_A,
        GLFW_KEY_LEFT_SHIFT,
        true);
    const float normalRightSpeed = measureSteadyFreelookSpeed(
        GLFW_KEY_D,
        GLFW_KEY_UNKNOWN,
        true);
    const float boostedRightSpeed = measureSteadyFreelookSpeed(
        GLFW_KEY_D,
        GLFW_KEY_RIGHT_SHIFT,
        true);
    passed &= Check(
        NearlyEqual(boostedLeftSpeed, normalLeftSpeed * 2.f, 2e-3f) &&
        NearlyEqual(boostedRightSpeed, normalRightSpeed * 2.f, 2e-3f),
        "either Shift key doubles steady Freelook A/D strafe speed");

    const auto measureWheelTravel = [](int shiftKey)
    {
        UvsrThirdPersonCamera camera;
        camera.LookTo(float3(0.f, 0.f, -10.f), float3(0.f, 0.f, 1.f));
        camera.ResetZoomReferenceDistance(10.f);
        if (shiftKey != GLFW_KEY_UNKNOWN)
            camera.KeyboardUpdate(shiftKey, 0, GLFW_PRESS, 0);
        const float3 start = camera.GetPosition();
        camera.MouseScrollUpdate(0.0, 1.0);
        AnimateFrames(camera, 180);
        return dot(camera.GetPosition() - start, camera.GetDir());
    };
    const float normalWheelTravel = measureWheelTravel(GLFW_KEY_UNKNOWN);
    const float leftShiftWheelTravel = measureWheelTravel(GLFW_KEY_LEFT_SHIFT);
    const float rightShiftWheelTravel = measureWheelTravel(GLFW_KEY_RIGHT_SHIFT);
    passed &= Check(
        NearlyEqual(leftShiftWheelTravel, normalWheelTravel * 2.f, 4e-4f) &&
        NearlyEqual(rightShiftWheelTravel, normalWheelTravel * 2.f, 4e-4f),
        "either Shift key doubles Freelook wheel travel");

    UvsrThirdPersonCamera dualShiftBoost;
    dualShiftBoost.KeyboardUpdate(GLFW_KEY_LEFT_SHIFT, 0, GLFW_PRESS, 0);
    dualShiftBoost.KeyboardUpdate(GLFW_KEY_RIGHT_SHIFT, 0, GLFW_PRESS, 0);
    dualShiftBoost.KeyboardUpdate(GLFW_KEY_LEFT_SHIFT, 0, GLFW_RELEASE, 0);
    passed &= Check(dualShiftBoost.IsSpeedBoosted(),
        "releasing one Shift key keeps Freelook boosted while the other is held");
    dualShiftBoost.KeyboardUpdate(GLFW_KEY_RIGHT_SHIFT, 0, GLFW_RELEASE, 0);
    passed &= Check(!dualShiftBoost.IsSpeedBoosted(),
        "Freelook boost ends after both Shift keys are released");

    UvsrThirdPersonCamera clearedBoost;
    clearedBoost.KeyboardUpdate(GLFW_KEY_LEFT_SHIFT, 0, GLFW_PRESS, 0);
    clearedBoost.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_PRESS, 0);
    AnimateFrames(clearedBoost, 10);
    clearedBoost.ResetZoomReferenceDistance(12.f);
    passed &= Check(
        !clearedBoost.IsSpeedBoosted() &&
        clearedBoost.GetKeyboardDollyVelocity() == 0.f,
        "resetting Freelook motion clears Shift boost and dolly velocity");
    clearedBoost.KeyboardUpdate(GLFW_KEY_RIGHT_SHIFT, 0, GLFW_PRESS, 0);
    clearedBoost.KeyboardUpdate(GLFW_KEY_D, 0, GLFW_PRESS, 0);
    AnimateFrames(clearedBoost, 10);
    clearedBoost.CancelPendingMotion();
    passed &= Check(
        !clearedBoost.IsSpeedBoosted() &&
        clearedBoost.GetKeyboardStrafeVelocity() == 0.f,
        "canceling Freelook motion clears Shift boost and strafe velocity");

    const float3 strafeDirection(0.f, 0.f, 1.f);
    const float3 strafeUp(0.f, 1.f, 0.f);
    const float3 strafeRight = normalize(cross(strafeDirection, strafeUp));

    UvsrThirdPersonCamera strafeRightCamera;
    strafeRightCamera.LookTo(float3(2.f, 3.f, 4.f), strafeDirection, strafeUp);
    strafeRightCamera.ResetZoomReferenceDistance(10.f);
    const float3 strafeRightStart = strafeRightCamera.GetPosition();
    strafeRightCamera.KeyboardUpdate(GLFW_KEY_D, 0, GLFW_PRESS, 0);
    AnimateFrames(strafeRightCamera, 60);
    const float3 strafeRightDelta =
        strafeRightCamera.GetPosition() - strafeRightStart;
    passed &= Check(
        dot(strafeRightDelta, strafeRight) > 0.f &&
        std::abs(dot(strafeRightDelta, strafeDirection)) < 1e-5f &&
        std::abs(dot(strafeRightDelta, strafeUp)) < 1e-5f,
        "Freelook D strafes camera-right without forward or vertical drift");

    const float strafeVelocityBeforeRelease =
        strafeRightCamera.GetKeyboardStrafeVelocity();
    const float3 strafeReleasePosition = strafeRightCamera.GetPosition();
    strafeRightCamera.KeyboardUpdate(GLFW_KEY_D, 0, GLFW_RELEASE, 0);
    AnimateFrames(strafeRightCamera, 1);
    passed &= Check(
        dot(strafeRightCamera.GetPosition() - strafeReleasePosition,
            strafeRight) > 0.f &&
        strafeRightCamera.GetKeyboardStrafeVelocity() > 0.f &&
        strafeRightCamera.GetKeyboardStrafeVelocity() <
            strafeVelocityBeforeRelease,
        "Freelook strafe decelerates smoothly after D is released");
    AnimateFrames(strafeRightCamera, 180);
    passed &= Check(
        std::abs(strafeRightCamera.GetKeyboardStrafeVelocity()) < 1e-4f,
        "Freelook strafe reaches rest in finite time");

    UvsrThirdPersonCamera strafeLeftCamera;
    strafeLeftCamera.LookTo(float3(2.f, 3.f, 4.f), strafeDirection, strafeUp);
    strafeLeftCamera.ResetZoomReferenceDistance(10.f);
    const float3 strafeLeftStart = strafeLeftCamera.GetPosition();
    strafeLeftCamera.KeyboardUpdate(GLFW_KEY_A, 0, GLFW_PRESS, 0);
    AnimateFrames(strafeLeftCamera, 60);
    const float3 strafeLeftDelta =
        strafeLeftCamera.GetPosition() - strafeLeftStart;
    passed &= Check(
        dot(strafeLeftDelta, strafeRight) < 0.f &&
        std::abs(dot(strafeLeftDelta, strafeDirection)) < 1e-5f &&
        std::abs(dot(strafeLeftDelta, strafeUp)) < 1e-5f,
        "Freelook A strafes camera-left without forward or vertical drift");

    const float3 strafeResetPosition = strafeLeftCamera.GetPosition();
    strafeLeftCamera.ResetZoomReferenceDistance(12.f);
    AnimateFrames(strafeLeftCamera, 30);
    passed &= Check(
        all(strafeLeftCamera.GetPosition() == strafeResetPosition) &&
        strafeLeftCamera.GetKeyboardStrafeVelocity() == 0.f,
        "resetting the Freelook movement reference cancels pending strafe motion");

    UvsrThirdPersonCamera filteredTranslation;
    filteredTranslation.LookTo(float3(2.f, 3.f, 4.f), float3(0.f, 0.f, 1.f));
    const float3 filteredStart = filteredTranslation.GetPosition();
    filteredTranslation.KeyboardUpdate(GLFW_KEY_Q, 0, GLFW_PRESS, 0);
    AnimateFrames(filteredTranslation, 60);
    filteredTranslation.KeyboardUpdate(GLFW_KEY_Q, 0, GLFW_RELEASE, 0);
    passed &= Check(all(filteredTranslation.GetPosition() == filteredStart),
        "Freelook rejects Q vertical translation input");
    filteredTranslation.KeyboardUpdate(GLFW_KEY_E, 0, GLFW_PRESS, 0);
    AnimateFrames(filteredTranslation, 60);
    filteredTranslation.KeyboardUpdate(GLFW_KEY_E, 0, GLFW_RELEASE, 0);
    passed &= Check(all(filteredTranslation.GetPosition() == filteredStart),
        "Freelook rejects E vertical translation input");

    UvsrThirdPersonCamera unlimitedDolly;
    unlimitedDolly.LookTo(float3(0.f, 0.f, -10.f), float3(0.f, 0.f, 1.f));
    unlimitedDolly.ResetZoomReferenceDistance(10.f);
    for (int notch = 0; notch < 80; ++notch)
        unlimitedDolly.MouseScrollUpdate(0.0, 1.0);
    AnimateFrames(unlimitedDolly, 240);
    passed &= Check(NearlyEqual(unlimitedDolly.GetDollyScale(), 0.4f),
        "third-person inward dolly reaches a practical sensitivity floor");
    const float3 positionAtMinimumScale = unlimitedDolly.GetPosition();
    for (int notch = 0; notch < 10; ++notch)
        unlimitedDolly.MouseScrollUpdate(0.0, 1.0);
    AnimateFrames(unlimitedDolly, 240);
    passed &= Check(
        dot(unlimitedDolly.GetPosition() - positionAtMinimumScale,
            unlimitedDolly.GetDir()) > 0.02f,
        "third-person dolly remains unbounded at minimum sensitivity");

    const float collisionReferenceDistance = unlimitedDolly.GetReferenceZoomDistance();
    const float collisionWheelStep = unlimitedDolly.GetBaseWheelStepDistance();
    const float collisionDollyScale = unlimitedDolly.GetDollyScale();
    const float3 collisionDirection = unlimitedDolly.GetDir();
    unlimitedDolly.ApplyCollisionPosition(float3(2.f, 3.f, 4.f));
    passed &= Check(all(unlimitedDolly.GetPosition() == float3(2.f, 3.f, 4.f)),
        "third-person collision can override only the rendered eye position");
    passed &= Check(
        NearlyEqual(unlimitedDolly.GetReferenceZoomDistance(), collisionReferenceDistance) &&
        NearlyEqual(unlimitedDolly.GetBaseWheelStepDistance(), collisionWheelStep) &&
        NearlyEqual(unlimitedDolly.GetDollyScale(), collisionDollyScale) &&
        all(unlimitedDolly.GetDir() == collisionDirection),
        "third-person collision preserves look and dolly state");

    UvsrThirdPersonCamera thirdPersonLook;
    thirdPersonLook.LookTo(float3(0.f), float3(0.f, 0.f, 1.f));
    const float3 thirdDirectionBeforeArrow = thirdPersonLook.GetDir();
    thirdPersonLook.KeyboardUpdate(GLFW_KEY_RIGHT, 0, GLFW_PRESS, 0);
    thirdPersonLook.Animate(0.25f);
    thirdPersonLook.KeyboardUpdate(GLFW_KEY_RIGHT, 0, GLFW_RELEASE, 0);
    passed &= Check(lengthSquared(
        thirdPersonLook.GetDir() - thirdDirectionBeforeArrow) > 0.01f,
        "arrow keys rotate the third-person free-look view");

    StaticViewCamera staticCamera;
    staticCamera.LookTo(float3(5.f, 6.f, 7.f), float3(0.f, 0.f, 1.f), float3(0.f, 1.f, 0.f));
    const float3 staticPosition = staticCamera.GetPosition();
    const float3 staticDirection = staticCamera.GetDir();
    staticCamera.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_PRESS, 0);
    staticCamera.MousePosUpdate(900.0, 600.0);
    staticCamera.MouseButtonUpdate(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    staticCamera.Animate(1.f);
    passed &= Check(all(staticCamera.GetPosition() == staticPosition),
        "static camera ignores movement input");
    passed &= Check(all(staticCamera.GetDir() == staticDirection),
        "static camera ignores look input");

    return passed ? 0 : 1;
}
