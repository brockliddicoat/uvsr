#include "camera_controllers.h"

#include <array>
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

    bool NearlyEqual(float3 actual, float3 expected, float tolerance = 1e-4f)
    {
        return lengthSquared(actual - expected) <= tolerance * tolerance;
    }

    bool IsFinite(float3 value)
    {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    void BuildLevelBasis(
        float3 direction,
        float3& levelRight,
        float3& levelUp)
    {
        const float3 directionAxis = normalize(direction);
        const float3 worldUp(0.f, 1.f, 0.f);
        const float3 fallbackUp(0.f, 0.f, 1.f);
        const float3 referenceUp =
            std::abs(dot(directionAxis, worldUp)) < 0.999f
                ? worldUp
                : fallbackUp;
        levelRight = normalize(cross(directionAxis, referenceUp));
        levelUp = normalize(cross(levelRight, directionAxis));
    }

    template <typename CameraType>
    void SetRolledPose(
        CameraType& camera,
        float3 position,
        float3 direction,
        float rollAngle)
    {
        float3 levelRight;
        float3 levelUp;
        BuildLevelBasis(direction, levelRight, levelUp);
        const float cosine = std::cos(rollAngle);
        const float sine = std::sin(rollAngle);
        const float3 rolledUp =
            levelUp * cosine + levelRight * sine;
        const float3 rolledRight =
            levelRight * cosine - levelUp * sine;
        camera.SetExactPose(
            position, direction, rolledUp, rolledRight);
    }

    template <typename CameraType>
    float SignedRollToLevel(const CameraType& camera)
    {
        const float3 directionAxis = normalize(camera.GetDir());
        float3 levelRight;
        float3 levelUp;
        BuildLevelBasis(directionAxis, levelRight, levelUp);
        const float3 projectedUp = normalize(
            camera.GetUp() -
            directionAxis * dot(camera.GetUp(), directionAxis));
        return std::atan2(
            dot(projectedUp, levelRight),
            dot(projectedUp, levelUp));
    }

    template <typename CameraType>
    void AnimateFrames(
        CameraType& camera,
        int frameCount,
        float frameTime)
    {
        for (int frame = 0; frame < frameCount; ++frame)
            camera.Animate(frameTime);
    }

    void AnimateFrames(UvsrThirdPersonCamera& camera, int frameCount)
    {
        constexpr float FrameTime = 1.f / 60.f;
        AnimateFrames(camera, frameCount, FrameTime);
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
    firstPerson.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_RELEASE, 0);

    const float firstPersonVerticalStart = firstPerson.GetPosition().y;
    firstPerson.KeyboardUpdate(GLFW_KEY_Q, 0, GLFW_PRESS, 0);
    firstPerson.Animate(0.25f);
    firstPerson.KeyboardUpdate(GLFW_KEY_Q, 0, GLFW_RELEASE, 0);
    passed &= Check(
        firstPerson.GetPosition().y > firstPersonVerticalStart,
        "Q moves the first-person camera upward");
    firstPerson.KeyboardUpdate(GLFW_KEY_E, 0, GLFW_PRESS, 0);
    firstPerson.Animate(0.25f);
    firstPerson.KeyboardUpdate(GLFW_KEY_E, 0, GLFW_RELEASE, 0);
    passed &= Check(
        NearlyEqual(firstPerson.GetPosition().y, firstPersonVerticalStart),
        "E moves the first-person camera downward");

    const float3 beforeRetiredSpace = firstPerson.GetPosition();
    firstPerson.KeyboardUpdate(GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
    firstPerson.Animate(0.25f);
    firstPerson.KeyboardUpdate(GLFW_KEY_SPACE, 0, GLFW_RELEASE, 0);
    passed &= Check(
        all(firstPerson.GetPosition() == beforeRetiredSpace),
        "Space no longer moves the first-person camera");

    for (const int shiftKey :
        { GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT })
    {
        UvsrFirstPersonCamera shiftedFirstPerson(true);
        shiftedFirstPerson.LookTo(float3(0.f), float3(1.f, 0.f, 0.f));
        shiftedFirstPerson.KeyboardUpdate(
            shiftKey, 0, GLFW_PRESS, 0);
        shiftedFirstPerson.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_PRESS, 0);
        shiftedFirstPerson.Animate(1.f);
        shiftedFirstPerson.KeyboardUpdate(GLFW_KEY_W, 0, GLFW_RELEASE, 0);
        shiftedFirstPerson.KeyboardUpdate(
            shiftKey, 0, GLFW_RELEASE, 0);
        passed &= Check(
            NearlyEqual(shiftedFirstPerson.GetPosition().x, 6.f) &&
                NearlyEqual(shiftedFirstPerson.GetPosition().y, 0.f) &&
                NearlyEqual(shiftedFirstPerson.GetPosition().z, 0.f),
            "both Shift keys remain inert instead of moving vertically or "
            "restoring Donut sprint");
    }

    const float3 firstDirectionBeforeArrow = firstPerson.GetDir();
    firstPerson.KeyboardUpdate(GLFW_KEY_LEFT, 0, GLFW_PRESS, 0);
    firstPerson.Animate(0.25f);
    firstPerson.KeyboardUpdate(GLFW_KEY_LEFT, 0, GLFW_RELEASE, 0);
    passed &= Check(lengthSquared(firstPerson.GetDir() - firstDirectionBeforeArrow) > 0.01f,
        "arrow keys rotate the first-person view");

    UvsrFirstPersonCamera rollCamera(true);
    rollCamera.LookTo(
        float3(0.f),
        float3(0.f, 0.f, 1.f),
        float3(0.f, 1.f, 0.f));
    const float3 upBeforeReservedZoomKey = rollCamera.GetUp();
    rollCamera.KeyboardUpdate(GLFW_KEY_Z, 0, GLFW_PRESS, 0);
    rollCamera.Animate(0.25f);
    rollCamera.KeyboardUpdate(GLFW_KEY_Z, 0, GLFW_RELEASE, 0);
    passed &= Check(
        lengthSquared(rollCamera.GetUp() - upBeforeReservedZoomKey) <
            1e-8f,
        "Z is reserved for pixel zoom and no longer rolls the camera");

    rollCamera.KeyboardUpdate(GLFW_KEY_X, 0, GLFW_PRESS, 0);
    rollCamera.Animate(0.25f);
    rollCamera.KeyboardUpdate(GLFW_KEY_X, 0, GLFW_RELEASE, 0);
    passed &= Check(
        lengthSquared(rollCamera.GetUp() - upBeforeReservedZoomKey) >
            1e-6f,
        "X performs the former Z roll-left camera action");
    const float3 positionBeforeRollReset = rollCamera.GetPosition();
    const float3 directionBeforeRollReset = rollCamera.GetDir();
    const float3 upBeforeRollReset = rollCamera.GetUp();
    rollCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    rollCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_RELEASE, 0);
    passed &= Check(
        all(rollCamera.GetPosition() == positionBeforeRollReset) &&
            all(rollCamera.GetDir() == directionBeforeRollReset) &&
            all(rollCamera.GetUp() == upBeforeRollReset),
        "V starts roll leveling without snapping the captured pose");
    rollCamera.Animate(1.2f);
    passed &= Check(
        all(rollCamera.GetPosition() == positionBeforeRollReset) &&
            all(rollCamera.GetDir() == directionBeforeRollReset) &&
            lengthSquared(rollCamera.GetUp() - float3(0.f, 1.f, 0.f)) <
                1e-6f,
        "V settles X/C roll exactly without moving or redirecting the camera");

    constexpr float InitialRollAngle = 40.f * PI_f / 180.f;
    constexpr float RollPeakTime = PI_f / 9.5f;
    const float3 rollTestPosition(3.f, -2.f, 7.f);
    const float3 rollTestDirection(0.f, 0.f, 1.f);
    float3 rollTestLevelRight;
    float3 rollTestLevelUp;
    BuildLevelBasis(
        rollTestDirection, rollTestLevelRight, rollTestLevelUp);

    UvsrFirstPersonCamera trajectoryCamera(true);
    SetRolledPose(
        trajectoryCamera,
        rollTestPosition,
        rollTestDirection,
        InitialRollAngle);
    trajectoryCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    int crossingCount = 0;
    int crossingFrame = 0;
    float previousNonzeroRoll = InitialRollAngle;
    float overshootPeak = 0.f;
    float rollAtPeakSample = InitialRollAngle;
    float rollAfterPeak = InitialRollAngle;
    bool trajectoryPoseInvariant = true;
    for (int frame = 1; frame <= 120; ++frame)
    {
        trajectoryCamera.Animate(0.01f);
        const float roll = SignedRollToLevel(trajectoryCamera);
        trajectoryPoseInvariant =
            trajectoryPoseInvariant &&
            all(trajectoryCamera.GetPosition() == rollTestPosition) &&
            all(trajectoryCamera.GetDir() == rollTestDirection);
        if (std::abs(roll) > 1e-6f)
        {
            if (roll * previousNonzeroRoll < 0.f)
            {
                ++crossingCount;
                crossingFrame = frame;
            }
            previousNonzeroRoll = roll;
        }
        overshootPeak = std::min(overshootPeak, roll);
        if (frame == 33)
            rollAtPeakSample = roll;
        if (frame == 60)
            rollAfterPeak = roll;
    }
    passed &= Check(
        crossingCount == 1 &&
            crossingFrame >= 22 &&
            crossingFrame <= 24,
        "the analytic roll response crosses level exactly once");
    passed &= Check(
        std::abs(overshootPeak / InitialRollAngle) >= 0.145f &&
            std::abs(overshootPeak / InitialRollAngle) <= 0.155f,
        "the single roll overshoot is controlled to roughly fifteen percent");
    passed &= Check(
        rollAtPeakSample < 0.f &&
            rollAfterPeak < 0.f &&
            std::abs(rollAfterPeak) < std::abs(rollAtPeakSample),
        "the post-overshoot tail converges toward level without recrossing");
    passed &= Check(
        trajectoryPoseInvariant,
        "the complete roll trajectory preserves position and view direction");
    passed &= Check(
        all(trajectoryCamera.GetUp() == rollTestLevelUp),
        "roll leveling finishes at the exact captured level target");

    UvsrFirstPersonCamera peakCamera(true);
    SetRolledPose(
        peakCamera,
        rollTestPosition,
        rollTestDirection,
        InitialRollAngle);
    peakCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    peakCamera.Animate(RollPeakTime);
    passed &= Check(
        NearlyEqual(
            std::abs(SignedRollToLevel(peakCamera) / InitialRollAngle),
            std::exp(-5.75f * RollPeakTime),
            2e-4f),
        "the analytic response reaches its expected stationary overshoot peak");

    UvsrFirstPersonCamera rollAt30Hz(true);
    UvsrFirstPersonCamera rollAt60Hz(true);
    UvsrFirstPersonCamera rollAt144Hz(true);
    UvsrFirstPersonCamera rollAtIrregularRate(true);
    SetRolledPose(
        rollAt30Hz, rollTestPosition, rollTestDirection, InitialRollAngle);
    SetRolledPose(
        rollAt60Hz, rollTestPosition, rollTestDirection, InitialRollAngle);
    SetRolledPose(
        rollAt144Hz, rollTestPosition, rollTestDirection, InitialRollAngle);
    SetRolledPose(
        rollAtIrregularRate,
        rollTestPosition,
        rollTestDirection,
        InitialRollAngle);
    rollAt30Hz.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    rollAt60Hz.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    rollAt144Hz.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    rollAtIrregularRate.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    AnimateFrames(rollAt30Hz, 15, 1.f / 30.f);
    AnimateFrames(rollAt60Hz, 30, 1.f / 60.f);
    AnimateFrames(rollAt144Hz, 72, 1.f / 144.f);
    constexpr std::array<float, 7> IrregularFrameTimes = {
        0.017f, 0.041f, 0.099f, 0.013f, 0.137f, 0.071f, 0.122f
    };
    for (float frameTime : IrregularFrameTimes)
        rollAtIrregularRate.Animate(frameTime);
    passed &= Check(
        NearlyEqual(
            rollAt30Hz.GetUp(), rollAt60Hz.GetUp(), 2e-5f) &&
        NearlyEqual(
            rollAt30Hz.GetUp(), rollAt144Hz.GetUp(), 2e-5f) &&
        NearlyEqual(
            rollAt30Hz.GetUp(), rollAtIrregularRate.GetUp(), 2e-5f),
        "roll leveling is frame-rate invariant at 30, 60, 144, and irregular Hz");

    UvsrFirstPersonCamera stationaryTrackpadCamera(true);
    SetRolledPose(
        stationaryTrackpadCamera,
        rollTestPosition,
        rollTestDirection,
        InitialRollAngle);
    stationaryTrackpadCamera.MousePosUpdate(640.0, 360.0);
    stationaryTrackpadCamera.MouseButtonUpdate(
        GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    stationaryTrackpadCamera.Animate(0.01f);
    stationaryTrackpadCamera.KeyboardUpdate(
        GLFW_KEY_V, 0, GLFW_PRESS, 0);
    for (int frame = 0; frame < 120; ++frame)
    {
        // Touchpads may replay the held cursor position every frame even when
        // the pointer has not actually moved.
        stationaryTrackpadCamera.MousePosUpdate(640.0, 360.0);
        stationaryTrackpadCamera.Animate(0.01f);
    }
    stationaryTrackpadCamera.MouseButtonUpdate(
        GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    passed &= Check(
        all(stationaryTrackpadCamera.GetPosition() == rollTestPosition) &&
            all(stationaryTrackpadCamera.GetDir() == rollTestDirection) &&
            all(stationaryTrackpadCamera.GetUp() == rollTestLevelUp),
        "a held stationary trackpad does not cancel V roll leveling");

    UvsrFirstPersonCamera queuedTrackpadCamera(true);
    SetRolledPose(
        queuedTrackpadCamera,
        rollTestPosition,
        rollTestDirection,
        InitialRollAngle);
    queuedTrackpadCamera.MousePosUpdate(640.0, 360.0);
    queuedTrackpadCamera.MouseButtonUpdate(
        GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    queuedTrackpadCamera.Animate(0.01f);
    queuedTrackpadCamera.MousePosUpdate(672.0, 376.0);
    queuedTrackpadCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    const float3 queuedTrackpadDirection = queuedTrackpadCamera.GetDir();
    for (int frame = 0; frame < 120; ++frame)
    {
        queuedTrackpadCamera.MousePosUpdate(672.0, 376.0);
        queuedTrackpadCamera.Animate(0.01f);
    }
    queuedTrackpadCamera.MouseButtonUpdate(
        GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    passed &= Check(
        !NearlyEqual(
            queuedTrackpadDirection,
            rollTestDirection,
            1e-4f) &&
            all(queuedTrackpadCamera.GetPosition() == rollTestPosition) &&
            all(queuedTrackpadCamera.GetDir() == queuedTrackpadDirection) &&
            NearlyEqual(
                dot(
                    queuedTrackpadCamera.GetUp(),
                    float3(0.f, 1.f, 0.f)),
                std::sqrt(
                    1.f - queuedTrackpadDirection.y *
                        queuedTrackpadDirection.y),
                2e-5f),
        "V consumes a pre-key trackpad delta before leveling its new pose");

    UvsrFirstPersonCamera movingTrackpadCamera(true);
    SetRolledPose(
        movingTrackpadCamera,
        rollTestPosition,
        rollTestDirection,
        InitialRollAngle);
    movingTrackpadCamera.MousePosUpdate(640.0, 360.0);
    movingTrackpadCamera.MouseButtonUpdate(
        GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    movingTrackpadCamera.Animate(0.01f);
    movingTrackpadCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    movingTrackpadCamera.Animate(0.12f);
    const float3 directionBeforeTrackpadMotion =
        movingTrackpadCamera.GetDir();
    movingTrackpadCamera.MousePosUpdate(672.0, 376.0);
    movingTrackpadCamera.Animate(0.01f);
    const float3 directionAfterTrackpadMotion = movingTrackpadCamera.GetDir();
    const float3 upAfterTrackpadMotion = movingTrackpadCamera.GetUp();
    movingTrackpadCamera.MouseButtonUpdate(
        GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    movingTrackpadCamera.Animate(0.5f);
    passed &= Check(
        !NearlyEqual(
            directionAfterTrackpadMotion,
            directionBeforeTrackpadMotion,
            1e-4f) &&
            all(movingTrackpadCamera.GetDir() ==
                directionAfterTrackpadMotion) &&
            all(movingTrackpadCamera.GetUp() == upAfterTrackpadMotion),
        "actual held-trackpad motion cancels pending roll leveling");

    UvsrFirstPersonCamera repeatControl(true);
    UvsrFirstPersonCamera repeatCamera(true);
    UvsrFirstPersonCamera restartCamera(true);
    SetRolledPose(
        repeatControl, rollTestPosition, rollTestDirection, InitialRollAngle);
    SetRolledPose(
        repeatCamera, rollTestPosition, rollTestDirection, InitialRollAngle);
    SetRolledPose(
        restartCamera, rollTestPosition, rollTestDirection, InitialRollAngle);
    repeatControl.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    repeatCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    restartCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    repeatControl.Animate(0.12f);
    repeatCamera.Animate(0.12f);
    restartCamera.Animate(0.12f);
    repeatCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_REPEAT, 0);
    restartCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    repeatCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_RELEASE, 0);
    restartCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_RELEASE, 0);
    repeatControl.Animate(0.18f);
    repeatCamera.Animate(0.18f);
    restartCamera.Animate(0.18f);
    passed &= Check(
        NearlyEqual(repeatCamera.GetUp(), repeatControl.GetUp(), 1e-6f),
        "V repeat and release events are consumed without restarting leveling");
    passed &= Check(
        !NearlyEqual(restartCamera.GetUp(), repeatControl.GetUp(), 1e-3f),
        "a new V press restarts leveling from the current roll");

    UvsrFirstPersonCamera inputCanceledCamera(true);
    SetRolledPose(
        inputCanceledCamera,
        rollTestPosition,
        rollTestDirection,
        InitialRollAngle);
    inputCanceledCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    inputCanceledCamera.Animate(0.12f);
    inputCanceledCamera.KeyboardUpdate(
        GLFW_KEY_LEFT, 0, GLFW_PRESS, 0);
    inputCanceledCamera.Animate(0.05f);
    inputCanceledCamera.KeyboardUpdate(
        GLFW_KEY_LEFT, 0, GLFW_RELEASE, 0);
    const float3 inputCanceledDirection = inputCanceledCamera.GetDir();
    const float3 inputCanceledUp = inputCanceledCamera.GetUp();
    inputCanceledCamera.Animate(0.5f);
    passed &= Check(
        all(inputCanceledCamera.GetDir() == inputCanceledDirection) &&
            all(inputCanceledCamera.GetUp() == inputCanceledUp),
        "other camera-affecting input cancels pending roll leveling");

    UvsrFirstPersonCamera replacedPoseCamera(true);
    SetRolledPose(
        replacedPoseCamera,
        rollTestPosition,
        rollTestDirection,
        InitialRollAngle);
    replacedPoseCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    replacedPoseCamera.Animate(0.12f);
    const float replacementRoll = -23.f * PI_f / 180.f;
    SetRolledPose(
        replacedPoseCamera,
        float3(-4.f, 6.f, 2.f),
        rollTestDirection,
        replacementRoll);
    const float3 replacedPosition = replacedPoseCamera.GetPosition();
    const float3 replacedDirection = replacedPoseCamera.GetDir();
    const float3 replacedUp = replacedPoseCamera.GetUp();
    replacedPoseCamera.Animate(0.7f);
    passed &= Check(
        all(replacedPoseCamera.GetPosition() == replacedPosition) &&
            all(replacedPoseCamera.GetDir() == replacedDirection) &&
            all(replacedPoseCamera.GetUp() == replacedUp),
        "exact-pose replacement cancels pending roll leveling");

    const float3 nearVerticalDirection =
        normalize(float3(1e-5f, 1.f, -2e-5f));
    const float3 nearVerticalPosition(8.f, 5.f, -3.f);
    UvsrFirstPersonCamera nearVerticalCamera(true);
    SetRolledPose(
        nearVerticalCamera,
        nearVerticalPosition,
        nearVerticalDirection,
        55.f * PI_f / 180.f);
    nearVerticalCamera.KeyboardUpdate(GLFW_KEY_V, 0, GLFW_PRESS, 0);
    bool nearVerticalStable = true;
    constexpr std::array<float, 5> NearVerticalFrameTimes = {
        0.003f, 0.011f, 0.007f, 0.019f, 0.005f
    };
    for (int frame = 0; frame < 140; ++frame)
    {
        nearVerticalCamera.Animate(
            NearVerticalFrameTimes[frame % NearVerticalFrameTimes.size()]);
        nearVerticalStable =
            nearVerticalStable &&
            IsFinite(nearVerticalCamera.GetDir()) &&
            IsFinite(nearVerticalCamera.GetUp()) &&
            std::abs(dot(
                nearVerticalCamera.GetDir(),
                nearVerticalCamera.GetUp())) < 1e-4f &&
            NearlyEqual(length(nearVerticalCamera.GetUp()), 1.f, 1e-4f);
    }
    float3 nearVerticalLevelRight;
    float3 nearVerticalLevelUp;
    BuildLevelBasis(
        nearVerticalDirection,
        nearVerticalLevelRight,
        nearVerticalLevelUp);
    passed &= Check(
        nearVerticalStable &&
            all(nearVerticalCamera.GetPosition() == nearVerticalPosition) &&
            all(nearVerticalCamera.GetDir() == nearVerticalDirection) &&
            NearlyEqual(
                nearVerticalCamera.GetUp(), nearVerticalLevelUp, 2e-5f),
        "roll leveling remains finite and exact near a vertical view direction");

    UvsrThirdPersonCamera thirdPersonRollCamera;
    const float3 thirdPersonRollPosition(-3.f, 4.f, -12.f);
    SetRolledPose(
        thirdPersonRollCamera,
        thirdPersonRollPosition,
        rollTestDirection,
        InitialRollAngle);
    thirdPersonRollCamera.ResetZoomReferenceDistance(10.f);
    thirdPersonRollCamera.KeyboardUpdate(
        GLFW_KEY_V, 0, GLFW_PRESS, 0);
    bool thirdPersonRollPositionInvariant = true;
    for (int frame = 0; frame < 120; ++frame)
    {
        thirdPersonRollCamera.Animate(0.01f);
        thirdPersonRollPositionInvariant =
            thirdPersonRollPositionInvariant &&
            all(thirdPersonRollCamera.GetPosition() ==
                thirdPersonRollPosition);
    }
    passed &= Check(
        thirdPersonRollPositionInvariant &&
            all(thirdPersonRollCamera.GetDir() == rollTestDirection) &&
            all(thirdPersonRollCamera.GetUp() == rollTestLevelUp),
        "third-person roll leveling inherits the curve without moving the eye");

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

    UvsrThirdPersonCamera verticalCamera;
    verticalCamera.LookTo(
        float3(2.f, 3.f, 4.f),
        float3(0.f, 0.f, 1.f),
        float3(1.f, 0.f, 0.f));
    verticalCamera.ResetZoomReferenceDistance(10.f);
    const float3 verticalStart = verticalCamera.GetPosition();
    verticalCamera.KeyboardUpdate(GLFW_KEY_Q, 0, GLFW_PRESS, 0);
    AnimateFrames(verticalCamera, 60);
    verticalCamera.KeyboardUpdate(GLFW_KEY_Q, 0, GLFW_RELEASE, 0);
    passed &= Check(
        verticalCamera.GetPosition().y > verticalStart.y &&
            NearlyEqual(verticalCamera.GetPosition().x, verticalStart.x) &&
            NearlyEqual(verticalCamera.GetPosition().z, verticalStart.z),
        "Freelook Q moves world-up even when the camera is rolled");
    AnimateFrames(verticalCamera, 180);
    const float heightBeforeDown = verticalCamera.GetPosition().y;
    verticalCamera.KeyboardUpdate(GLFW_KEY_E, 0, GLFW_PRESS, 0);
    AnimateFrames(verticalCamera, 60);
    verticalCamera.KeyboardUpdate(GLFW_KEY_E, 0, GLFW_RELEASE, 0);
    passed &= Check(
        verticalCamera.GetPosition().y < heightBeforeDown,
        "Freelook E moves world-down");

    verticalCamera.CancelPendingMotion();
    passed &= Check(
        verticalCamera.GetKeyboardVerticalVelocity() == 0.f,
        "canceling Freelook motion clears vertical velocity");

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
    filteredTranslation.KeyboardUpdate(GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
    AnimateFrames(filteredTranslation, 60);
    filteredTranslation.KeyboardUpdate(GLFW_KEY_SPACE, 0, GLFW_RELEASE, 0);
    passed &= Check(all(filteredTranslation.GetPosition() == filteredStart),
        "Freelook rejects retired Space vertical translation input");
    for (const int shiftKey :
        { GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT })
    {
        filteredTranslation.KeyboardUpdate(
            shiftKey, 0, GLFW_PRESS, 0);
        AnimateFrames(filteredTranslation, 60);
        filteredTranslation.KeyboardUpdate(
            shiftKey, 0, GLFW_RELEASE, 0);
        passed &= Check(all(filteredTranslation.GetPosition() == filteredStart),
            "Freelook rejects both retired Shift translation inputs");
    }

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
