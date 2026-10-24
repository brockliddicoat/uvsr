/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "camera_controllers.h"
#include "renderer_scene_records.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace uvsr
{
namespace
{
    using Vec3 = gpu_contract::Float3;
    struct CameraRotation
    {
        float values[9];
        Vec3 Transform(Vec3 value) const noexcept
        {
            const float lanes[] = {value.x, value.y, value.z};
            float result[3]{};
            for (unsigned row = 0; row < 3; ++row)
                for (unsigned column = 0; column < 3; ++column)
                    result[column] += lanes[row] * values[row * 3 + column];
            return {result[0], result[1], result[2]};
        }
    };

    CameraRotation operator*(const CameraRotation& a, const CameraRotation& b) noexcept
    {
        CameraRotation result{};
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 3; ++column)
                for (unsigned inner = 0; inner < 3; ++inner)
                    result.values[row * 3 + column] += a.values[row * 3 + inner] * b.values[inner * 3 + column];
        return result;
    }

    CameraRotation IdentityRotation() noexcept { return {{1, 0, 0, 0, 1, 0, 0, 0, 1}}; }

    CameraRotation Rotation(Vec3 axis, float radians) noexcept
    {
        const float sine = std::sin(radians), cosine = std::cos(radians);
        const float lanes[] = {axis.x, axis.y, axis.z};
        const float cross[] = {0, axis.z, -axis.y, -axis.z, 0, axis.x, axis.y, -axis.x, 0};
        CameraRotation result;
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 3; ++column)
            {
                const unsigned index = row * 3 + column;
                result.values[index] = (row == column ? cosine : 0.f) + cross[index] * sine
                    + lanes[row] * lanes[column] * (1.f - cosine);
            }
        return result;
    }

    float ClampCameraValue(float value, float lower, float upper) noexcept
    {
        const float raised = value < lower ? lower : value;
        return raised < upper ? raised : upper;
    }
}

bool UvsrThirdPersonCamera::FrameBounds(const RendererSceneBounds& bounds, float verticalFovDegrees,
    float distanceScale, bool resetOrientation) noexcept
{
    const Vec3 minimum = bounds.minimum, maximum = bounds.maximum;
    if (bounds.empty || minimum.x > maximum.x || minimum.y > maximum.y || minimum.z > maximum.z ||
        !std::isfinite(minimum.x) || !std::isfinite(minimum.y) || !std::isfinite(minimum.z) ||
        !std::isfinite(maximum.x) || !std::isfinite(maximum.y) || !std::isfinite(maximum.z))
        return false;
    const Vec3 diagonal = maximum - minimum;
    const float radius = Length(diagonal) * .5f;
    const float distance = radius * distanceScale / sinf(Radians(verticalFovDegrees * .5f));
    if (!std::isfinite(distance) || distance <= 0.f) return false;
    const Vec3 center = minimum + diagonal / 2.f;
    if (resetOrientation)
    {
        // retain the authored yaw/pitch framing basis and its quaternion arithmetic.
        const float sx = std::sin(.5f * Radians(20.f)), cx = std::cos(.5f * Radians(20.f));
        const float sy = std::sin(.5f * Radians(135.f)), cy = std::cos(.5f * Radians(135.f));
        const float w = cy * cx, x = cy * sx, y = sy * cx, z = -sy * sx;
        const Vec3 direction{2.f * (x*z + y*w), 2.f * (y*z - x*w), 1.f - 2.f * (x*x + y*y)};
        const Vec3 up{2.f * (x*y - z*w), 1.f - 2.f * (x*x + z*z), 2.f * (y*z + x*w)};
        LookTo(center + (-distance) * direction, direction, up);
    }
    else
    {
        const Vec3 direction = GetDir(), up = GetUp();
        LookTo(center - direction * distance, direction, up);
    }
    ResetZoomReferenceDistance(distance);
    return true;
}

void CameraController::UpdateWorldToView() noexcept
{
    const CameraRotation basis{{m_CameraRight.x, m_CameraUp.x, m_CameraDir.x,
        m_CameraRight.y, m_CameraUp.y, m_CameraDir.y,
        m_CameraRight.z, m_CameraUp.z, m_CameraDir.z}};
    // retain affine composition's accumulation order and signed-zero behavior.
    const auto linear = IdentityRotation() * basis;
    const Vec3 translated = basis.Transform(-m_CameraPos) + Vec3{};
    m_WorldToView = {{linear.values[0], linear.values[1], linear.values[2], 0,
        linear.values[3], linear.values[4], linear.values[5], 0,
        linear.values[6], linear.values[7], linear.values[8], 0,
        translated.x, translated.y, translated.z, 1}};
}

void CameraController::BaseLookAt(Vec3 position, Vec3 target, Vec3 up) noexcept
{
    m_CameraPos = position;
    m_CameraDir = Normalize(target - position);
    m_CameraUp = Normalize(up);
    m_CameraRight = Normalize(Cross(m_CameraDir, m_CameraUp));
    m_CameraUp = Normalize(Cross(m_CameraRight, m_CameraDir));
    UpdateWorldToView();
}

void CameraController::SetBasis(Vec3 position, Vec3 direction, Vec3 up, Vec3 right) noexcept
{
    m_CameraPos = position; m_CameraDir = direction;
    m_CameraUp = up; m_CameraRight = right;
    UpdateWorldToView();
}

UvsrFirstPersonCamera::UvsrFirstPersonCamera(bool translationEnabled) noexcept
    : m_TranslationEnabled(translationEnabled) { SetMoveSpeed(6.f); }

void UvsrFirstPersonCamera::LookTo(Vec3 position, Vec3 direction, Vec3 up) noexcept
{
    CancelRollLeveling();
    BaseLookAt(position, position + direction, up);
}

void UvsrFirstPersonCamera::SetExactPose(Vec3 position, Vec3 direction, Vec3 up, Vec3 right) noexcept
{
    CancelRollLeveling();
    SetBasis(position, direction, up, right);
}

void UvsrFirstPersonCamera::SetMotionKey(int key, int action) noexcept
{
    Motion motion;
    switch (key)
    {
    case GLFW_KEY_Q: motion = Up; break;
    case GLFW_KEY_E: motion = Down; break;
    case GLFW_KEY_A: motion = Left; break;
    case GLFW_KEY_D: motion = Right; break;
    case GLFW_KEY_W: motion = Forward; break;
    case GLFW_KEY_S: motion = Backward; break;
    case GLFW_KEY_X: case GLFW_KEY_Z: motion = RollLeft; break;
    case GLFW_KEY_C: motion = RollRight; break;
    case GLFW_KEY_LEFT_CONTROL: case GLFW_KEY_RIGHT_CONTROL: motion = Slow; break;
    default: return;
    }
    m_Motion[motion] = action == GLFW_PRESS || action == GLFW_REPEAT;
}

void UvsrFirstPersonCamera::MouseButtonUpdate(int button, int action, int) noexcept
{
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action != GLFW_RELEASE) CancelRollLeveling();
    m_LeftMousePressed = action == GLFW_PRESS;
}

void UvsrFirstPersonCamera::MousePosUpdate(double x, double y) noexcept
{ m_MousePos = {float(x), float(y)}; }

void UvsrFirstPersonCamera::AdvanceMotion(float deltaT) noexcept
{
    gpu_contract::Float2 mouseMove{};
    if (m_LeftMousePressed)
    {
        if (m_IsDragging) mouseMove = {m_MousePos.x - m_MousePosPrev.x, m_MousePos.y - m_MousePosPrev.y};
        m_IsDragging = true;
    }
    else m_IsDragging = false;
    m_MousePosPrev = m_MousePos;
    bool changed = false;
    auto orientation = IdentityRotation();
    if (m_LeftMousePressed && (mouseMove.x != 0 || mouseMove.y != 0))
    {
        const float yaw = m_RotateSpeed * mouseMove.x, pitch = m_RotateSpeed * mouseMove.y;
        orientation = Rotation({0, 1, 0}, -yaw);
        orientation = Rotation(m_CameraRight, -pitch) * orientation;
        changed = true;
    }
    if (m_Motion[RollLeft] || m_Motion[RollRight])
    {
        const float roll = float(m_Motion[RollLeft]) * -m_RotateSpeed * 2.f
            + float(m_Motion[RollRight]) * m_RotateSpeed * 2.f;
        orientation = Rotation(m_CameraDir, roll) * orientation;
        changed = true;
    }
    float step = deltaT * m_MoveSpeed;
    if (m_Motion[Slow]) step *= .1f;
    Vec3 movement{};
    if (m_Motion[Forward]) { changed = true; movement += m_CameraDir * step; }
    if (m_Motion[Backward]) { changed = true; movement += -m_CameraDir * step; }
    if (m_Motion[Left]) { changed = true; movement += -m_CameraRight * step; }
    if (m_Motion[Right]) { changed = true; movement += m_CameraRight * step; }
    if (m_Motion[Up]) { changed = true; movement += m_CameraUp * step; }
    if (m_Motion[Down]) { changed = true; movement += -m_CameraUp * step; }
    if (changed)
    {
        m_CameraPos += movement;
        m_CameraDir = Normalize(orientation.Transform(m_CameraDir));
        m_CameraUp = Normalize(orientation.Transform(m_CameraUp));
        m_CameraRight = Normalize(Cross(m_CameraDir, m_CameraUp));
        UpdateWorldToView();
    }
}

UvsrThirdPersonCamera::UvsrThirdPersonCamera() noexcept : UvsrFirstPersonCamera(false)
{ ResetZoomReferenceDistance(10.f); }

void StaticViewCamera::LookTo(Vec3 position, Vec3 direction, Vec3 up) noexcept
{ BaseLookAt(position, position + direction, up); }
void StaticViewCamera::SetExactPose(Vec3 position, Vec3 direction, Vec3 up, Vec3 right) noexcept
{ SetBasis(position, direction, up, right); }

void UvsrFirstPersonCamera::KeyboardUpdate(int key, int scancode, int action, int mods) noexcept
{
    (void)scancode; (void)mods;
    const bool pressed = action == GLFW_PRESS || action == GLFW_REPEAT;

    if (key == GLFW_KEY_LEFT_SHIFT)
        m_LeftShift = pressed;
    else if (key == GLFW_KEY_RIGHT_SHIFT)
        m_RightShift = pressed;

    if (key == GLFW_KEY_V)
    {
        // GLFW repeats are consumed so the OS repeat cadence cannot
        // turn an elapsed-time trajectory into a frame/input-rate
        // dependent series of restarts. Release is intentionally a
        // no-op; a new physical press is the only restart edge.
        if (action == GLFW_PRESS)
        {
            m_Motion[RollLeft] = m_Motion[RollRight] = false;
            // Consume pointer motion queued before this key event so
            // ResetRoll captures the pose the user actually reached.
            // Later pointer motion still changes the pose and cancels
            // leveling through AdvanceRollLeveling.
            AdvanceMotion(0.f);
            ResetRoll();
        }
        return;
    }

    if (pressed && IsCameraAffectingKey(key))
        CancelRollLeveling();

    switch (key)
    {
    case GLFW_KEY_LEFT: m_LookLeft = pressed; break;
    case GLFW_KEY_RIGHT: m_LookRight = pressed; break;
    case GLFW_KEY_UP: m_LookUp = pressed; break;
    case GLFW_KEY_DOWN: m_LookDown = pressed; break;
    default: break;
    }

    int forwardedAction = action;
    if (!m_TranslationEnabled && IsTranslationKey(key))
        forwardedAction = GLFW_RELEASE;

    // Z keeps its existing release of the roll latch while pixel zoom handles it.
    if (key == GLFW_KEY_Z)
        forwardedAction = GLFW_RELEASE;
    SetMotionKey(key, forwardedAction);
}

void UvsrFirstPersonCamera::ResetRoll() noexcept
{
    gpu_contract::Float3 levelRight;
    gpu_contract::Float3 levelUp;
    gpu_contract::Float3 directionAxis;
    if (!BuildLevelBasis(
            m_CameraDir, levelRight, levelUp, directionAxis))
    {
        CancelRollLeveling();
        return;
    }

    gpu_contract::Float3 projectedUp = m_CameraUp -
        directionAxis * Dot(
            m_CameraUp, directionAxis);
    const float projectedUpLengthSquared =
        LengthSquared(projectedUp);
    if (!std::isfinite(projectedUpLengthSquared) ||
        projectedUpLengthSquared <= 1e-12f)
    {
        // A degenerate imported basis has no meaningful roll. Repair
        // it to the same deterministic finite level frame used by the
        // normal trajectory.
        m_CameraRight = levelRight;
        m_CameraUp = levelUp;
        CancelRollLeveling();
        UpdateWorldToView();
        return;
    }

    projectedUp *= 1.f / std::sqrt(projectedUpLengthSquared);
    const float initialAngle = std::atan2(
        Dot(projectedUp, levelRight),
        Dot(projectedUp, levelUp));
    if (!std::isfinite(initialAngle))
    {
        CancelRollLeveling();
        return;
    }

    m_RollLevelTargetRight = levelRight;
    m_RollLevelTargetUp = levelUp;
    m_RollLevelStartPosition = m_CameraPos;
    m_RollLevelStartDirection = m_CameraDir;
    m_RollLevelInitialAngle = initialAngle;
    m_RollLevelElapsedSeconds = 0.0;

    constexpr float ExactLevelThreshold =
        0.05f * RendererPiF / 180.f;
    if (std::abs(initialAngle) <= ExactLevelThreshold)
    {
        m_CameraRight = levelRight;
        m_CameraUp = levelUp;
        CancelRollLeveling();
        UpdateWorldToView();
        return;
    }

    m_RollLevelingActive = true;
}

void UvsrFirstPersonCamera::Animate(float deltaT) noexcept
{
    const float moveSpeed = m_MoveSpeed;
    m_MoveSpeed *= MovementSpeedMultiplier();
    AdvanceMotion(deltaT);
    m_MoveSpeed = moveSpeed;

    const float yawInput = float(m_LookLeft) - float(m_LookRight);
    const float pitchInput = float(m_LookUp) - float(m_LookDown);
    if ((yawInput != 0.f || pitchInput != 0.f) && deltaT > 0.f)
    {
        constexpr float KeyboardLookSpeed =
            RendererPiF * 0.5f;
        CameraRotation cameraRotation = Rotation(
            gpu_contract::Float3{0.f, 1.f, 0.f},
            yawInput * KeyboardLookSpeed * deltaT);
        cameraRotation = Rotation(
            m_CameraRight,
            pitchInput * KeyboardLookSpeed * deltaT) * cameraRotation;

        m_CameraDir = Normalize(
            cameraRotation.Transform(m_CameraDir));
        m_CameraUp = Normalize(
            cameraRotation.Transform(m_CameraUp));
        m_CameraRight = Normalize(
            Cross(m_CameraDir, m_CameraUp));
        m_CameraUp = Normalize(
            Cross(m_CameraRight, m_CameraDir));
        UpdateWorldToView();
    }

    AdvanceRollLeveling(deltaT);
}

float UvsrFirstPersonCamera::MovementSpeedMultiplier() const noexcept
{
    return m_LeftShift || m_RightShift ? 2.f : 1.f;
}

void UvsrFirstPersonCamera::ReleaseMovementSpeedModifier() noexcept
{
    m_LeftShift = m_RightShift = false;
}

void UvsrFirstPersonCamera::CancelRollLeveling() noexcept
{
    m_RollLevelingActive = false;
    m_RollLevelElapsedSeconds = 0.0;
}

bool UvsrFirstPersonCamera::IsFinite(Vec3 value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool UvsrFirstPersonCamera::BuildLevelBasis(Vec3 direction, Vec3& levelRight, Vec3& levelUp, Vec3& directionAxis) noexcept
{
    const float directionLengthSquared =
        LengthSquared(direction);
    if (!std::isfinite(directionLengthSquared) ||
        directionLengthSquared <= 1e-12f)
    {
        return false;
    }

    directionAxis =
        direction * (1.f / std::sqrt(directionLengthSquared));
    const gpu_contract::Float3 worldUp{0.f, 1.f, 0.f};
    const gpu_contract::Float3 fallbackUp{0.f, 0.f, 1.f};
    const gpu_contract::Float3 referenceUp =
        std::abs(Dot(directionAxis, worldUp)) < 0.999f
            ? worldUp
            : fallbackUp;

    levelRight = Cross(
        directionAxis, referenceUp);
    const float rightLengthSquared =
        LengthSquared(levelRight);
    if (!std::isfinite(rightLengthSquared) ||
        rightLengthSquared <= 1e-12f)
    {
        return false;
    }

    levelRight *= 1.f / std::sqrt(rightLengthSquared);
    levelUp = Normalize(
        Cross(levelRight, directionAxis));
    return IsFinite(levelRight) && IsFinite(levelUp);
}

bool UvsrFirstPersonCamera::IsCameraAffectingKey(int key) noexcept
{
    switch (key)
    {
    case GLFW_KEY_LEFT:
    case GLFW_KEY_RIGHT:
    case GLFW_KEY_UP:
    case GLFW_KEY_DOWN:
    case GLFW_KEY_X:
    case GLFW_KEY_C:
        return true;
    default:
        return IsTranslationKey(key);
    }
}

bool UvsrFirstPersonCamera::IsTranslationKey(int key) noexcept
{
    switch (key)
    {
    case GLFW_KEY_Q:
    case GLFW_KEY_E:
    case GLFW_KEY_A:
    case GLFW_KEY_D:
    case GLFW_KEY_W:
    case GLFW_KEY_S:
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL:
        return true;
    default:
        return false;
    }
}

void UvsrFirstPersonCamera::AdvanceRollLeveling(float deltaT) noexcept
{
    if (!m_RollLevelingActive || deltaT <= 0.f)
        return;

    // A held key or mouse gesture can alter the pose inside input processing's
    // Animate without producing a fresh event this frame. Cancel
    // instead of fighting that input or restoring an obsolete pose.
    if (!(m_CameraPos == m_RollLevelStartPosition) ||
        !(m_CameraDir == m_RollLevelStartDirection))
    {
        CancelRollLeveling();
        return;
    }

    if (!std::isfinite(deltaT))
    {
        FinishRollLeveling();
        return;
    }

    constexpr double Damping = 5.75;
    constexpr double AngularFrequency = 9.5;
    constexpr double SettleRate = 12.0;
    constexpr double Pi = 3.14159265358979323846;
    constexpr double OvershootPeakTime = Pi / AngularFrequency;
    constexpr double MaximumDuration = 1.2;
    constexpr double ExactLevelThreshold =
        0.05 * Pi / 180.0;

    m_RollLevelElapsedSeconds += double(deltaT);
    const double elapsed = m_RollLevelElapsedSeconds;
    if (elapsed >= MaximumDuration)
    {
        FinishRollLeveling();
        return;
    }

    double angle = 0.0;
    if (elapsed <= OvershootPeakTime)
    {
        // Zero initial velocity, one zero crossing, and a stationary
        // opposite-signed peak of exp(-d*pi/w) = 14.94 percent.
        angle = double(m_RollLevelInitialAngle) *
            std::exp(-Damping * elapsed) *
            (std::cos(AngularFrequency * elapsed) +
                (Damping / AngularFrequency) *
                std::sin(AngularFrequency * elapsed));
    }
    else
    {
        // This critically shaped tail begins at the same value and
        // zero derivative as phase one, then approaches level from
        // the overshoot side without a second crossing.
        const double peakAngle =
            -double(m_RollLevelInitialAngle) *
            std::exp(-Damping * OvershootPeakTime);
        const double tailTime = elapsed - OvershootPeakTime;
        angle = peakAngle *
            (1.0 + SettleRate * tailTime) *
            std::exp(-SettleRate * tailTime);
        if (std::abs(angle) <= ExactLevelThreshold)
        {
            FinishRollLeveling();
            return;
        }
    }

    if (!std::isfinite(angle))
    {
        FinishRollLeveling();
        return;
    }

    const float cosine = float(std::cos(angle));
    const float sine = float(std::sin(angle));
    m_CameraUp =
        m_RollLevelTargetUp * cosine +
        m_RollLevelTargetRight * sine;
    m_CameraRight =
        m_RollLevelTargetRight * cosine -
        m_RollLevelTargetUp * sine;
    UpdateWorldToView();
}

void UvsrFirstPersonCamera::FinishRollLeveling() noexcept
{
    m_CameraRight = m_RollLevelTargetRight;
    m_CameraUp = m_RollLevelTargetUp;
    CancelRollLeveling();
    UpdateWorldToView();
}

void UvsrThirdPersonCamera::ResetZoomReferenceDistance(float distance) noexcept
{
    m_ReferenceZoomDistance = std::max(distance, 1e-3f);
    m_BaseWheelStepDistance = std::max(
        m_ReferenceZoomDistance * 0.015f,
        1e-4f);
    m_BaseKeyboardDollySpeed = std::max(
        m_ReferenceZoomDistance * 0.16f,
        1e-3f);
    m_DollyScale = 1.f;
    m_RemainingWheelDistance = 0.f;
    m_KeyboardDollyVelocity = 0.f;
    m_KeyboardStrafeVelocity = 0.f;
    m_KeyboardVerticalVelocity = 0.f;
    m_DollyForward = false;
    m_DollyBackward = false;
    m_StrafeLeft = false;
    m_StrafeRight = false;
    m_MoveUp = false;
    m_MoveDown = false;
}

void UvsrThirdPersonCamera::CancelPendingMotion() noexcept
{
    ReleaseMovementSpeedModifier();
    m_RemainingWheelDistance = 0.f;
    m_KeyboardDollyVelocity = 0.f;
    m_KeyboardStrafeVelocity = 0.f;
    m_KeyboardVerticalVelocity = 0.f;
    m_DollyForward = false;
    m_DollyBackward = false;
    m_StrafeLeft = false;
    m_StrafeRight = false;
    m_MoveUp = false;
    m_MoveDown = false;
}

void UvsrThirdPersonCamera::KeyboardUpdate(int key, int scancode, int action, int mods) noexcept
{
    const bool pressed = action == GLFW_PRESS || action == GLFW_REPEAT;
    if (key == GLFW_KEY_W)
        m_DollyForward = pressed;
    else if (key == GLFW_KEY_S)
        m_DollyBackward = pressed;
    else if (key == GLFW_KEY_A)
        m_StrafeLeft = pressed;
    else if (key == GLFW_KEY_D)
        m_StrafeRight = pressed;
    else if (key == GLFW_KEY_Q)
        m_MoveUp = pressed;
    else if (key == GLFW_KEY_E)
        m_MoveDown = pressed;

    // The parent remains translation-disabled, so it records arrow
    // look and mouse state but filters every movement key.
    UvsrFirstPersonCamera::KeyboardUpdate(
        key, scancode, action, mods);
}

void UvsrThirdPersonCamera::MouseScrollUpdate(double xoffset, double yoffset) noexcept
{
    (void)xoffset;
    if (yoffset == 0.0)
        return;

    CancelRollLeveling();

    // Change sensitivity by a small linear amount per notch. The old
    // multiplicative scale compounded inward until motion became an
    // unusably small fraction of its starting speed.
    constexpr float WheelScaleStep = 0.025f;
    constexpr float MinimumDollyScale = 0.4f;
    constexpr float MaximumDollyScale = 4.f;
    if (yoffset > 0.0)
    {
        m_RemainingWheelDistance +=
            m_BaseWheelStepDistance * m_DollyScale;
        m_DollyScale = std::max(
            MinimumDollyScale,
            m_DollyScale - WheelScaleStep);
    }
    else
    {
        m_DollyScale = std::min(
            MaximumDollyScale,
            m_DollyScale + WheelScaleStep);
        m_RemainingWheelDistance -=
            m_BaseWheelStepDistance * m_DollyScale;
    }
}

void UvsrThirdPersonCamera::Animate(float deltaT) noexcept
{
    if (HasPendingTranslation())
        CancelRollLeveling();
    UvsrFirstPersonCamera::Animate(deltaT);
    const float clampedDeltaT = ClampCameraValue(deltaT, 0.f, 0.1f);
    if (clampedDeltaT <= 0.f)
        return;

    const float dollyInput =
        float(m_DollyForward) - float(m_DollyBackward);
    const float strafeInput =
        float(m_StrafeRight) - float(m_StrafeLeft);
    const float verticalInput =
        float(m_MoveUp) - float(m_MoveDown);

    // W moves inward and gently lowers close-range sensitivity; S
    // restores it. Use a linear, bounded change so holding W reaches a
    // useful cruise speed instead of decaying exponentially to a crawl.
    constexpr float DollyScaleRate = 0.06f;
    constexpr float MinimumDollyScale = 0.4f;
    constexpr float MaximumDollyScale = 4.f;
    if (dollyInput != 0.f)
    {
        m_DollyScale = ClampCameraValue(
            m_DollyScale -
                dollyInput * DollyScaleRate * clampedDeltaT,
            MinimumDollyScale,
            MaximumDollyScale);
    }

    const float targetVelocity = dollyInput *
        m_BaseKeyboardDollySpeed * m_DollyScale;
    // Reach the requested velocity and return to rest in finite time.
    // This keeps the input smooth without an exponential drift tail.
    constexpr float KeyboardAccelerationRate = 5.f;
    constexpr float KeyboardDecelerationRate = 8.f;
    const float velocityRate = m_BaseKeyboardDollySpeed *
        (dollyInput == 0.f
            ? KeyboardDecelerationRate
            : KeyboardAccelerationRate);
    const float velocityDelta = ClampCameraValue(
        targetVelocity - m_KeyboardDollyVelocity,
        -velocityRate * clampedDeltaT,
        velocityRate * clampedDeltaT);
    m_KeyboardDollyVelocity += velocityDelta;

    const float targetStrafeVelocity = strafeInput *
        m_BaseKeyboardDollySpeed * m_DollyScale;
    const float strafeVelocityRate = m_BaseKeyboardDollySpeed *
        (strafeInput == 0.f
            ? KeyboardDecelerationRate
            : KeyboardAccelerationRate);
    const float strafeVelocityDelta = ClampCameraValue(
        targetStrafeVelocity - m_KeyboardStrafeVelocity,
        -strafeVelocityRate * clampedDeltaT,
        strafeVelocityRate * clampedDeltaT);
    m_KeyboardStrafeVelocity += strafeVelocityDelta;

    const float targetVerticalVelocity = verticalInput *
        m_BaseKeyboardDollySpeed * m_DollyScale;
    const float verticalVelocityRate = m_BaseKeyboardDollySpeed *
        (verticalInput == 0.f
            ? KeyboardDecelerationRate
            : KeyboardAccelerationRate);
    const float verticalVelocityDelta = ClampCameraValue(
        targetVerticalVelocity - m_KeyboardVerticalVelocity,
        -verticalVelocityRate * clampedDeltaT,
        verticalVelocityRate * clampedDeltaT);
    m_KeyboardVerticalVelocity += verticalVelocityDelta;

    constexpr float WheelMotionResponse = 14.f;
    const float wheelBlend = 1.f - std::exp(
        -WheelMotionResponse * clampedDeltaT);
    const float wheelMovement =
        m_RemainingWheelDistance * wheelBlend;
    m_RemainingWheelDistance -= wheelMovement;
    if (std::abs(m_RemainingWheelDistance) < 1e-5f)
        m_RemainingWheelDistance = 0.f;

    const float dollyMovement =
        m_KeyboardDollyVelocity * clampedDeltaT * MovementSpeedMultiplier() + wheelMovement;
    const float strafeMovement =
        m_KeyboardStrafeVelocity * clampedDeltaT * MovementSpeedMultiplier();
    const float verticalMovement =
        m_KeyboardVerticalVelocity * clampedDeltaT * MovementSpeedMultiplier();
    if (dollyMovement != 0.f ||
        strafeMovement != 0.f ||
        verticalMovement != 0.f)
    {
        m_CameraPos += m_CameraDir * dollyMovement +
            m_CameraRight * strafeMovement +
            gpu_contract::Float3{0.f, verticalMovement, 0.f};
        UpdateWorldToView();
    }
}

void UvsrThirdPersonCamera::ApplyCollisionPosition(Vec3 position) noexcept
{
    CancelRollLeveling();
    m_CameraPos = position;
    UpdateWorldToView();
}

bool UvsrThirdPersonCamera::HasPendingTranslation() const noexcept
{
    return m_RemainingWheelDistance != 0.f ||
        m_KeyboardDollyVelocity != 0.f ||
        m_KeyboardStrafeVelocity != 0.f ||
        m_KeyboardVerticalVelocity != 0.f ||
        m_DollyForward ||
        m_DollyBackward ||
        m_StrafeLeft ||
        m_StrafeRight ||
        m_MoveUp ||
        m_MoveDown;
}

}
