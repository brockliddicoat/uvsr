#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <donut/app/Camera.h>

namespace uvsr
{
    enum class CameraMode
    {
        FirstPerson,
        ThirdPerson,
        Static,
        Pivot
    };

    inline const char* GetCameraModeLabel(CameraMode mode)
    {
        switch (mode)
        {
        case CameraMode::ThirdPerson: return "Freelook";
        case CameraMode::Static: return "Locked";
        default: return "Unavailable";
        }
    }

    inline constexpr std::array<CameraMode, 2> SelectableCameraModes = {
        CameraMode::ThirdPerson,
        CameraMode::Static
    };

    class UvsrFirstPersonCamera : public donut::app::FirstPersonCamera
    {
    public:
        explicit UvsrFirstPersonCamera(bool translationEnabled = true)
            : m_TranslationEnabled(translationEnabled)
        {
            UpdateMoveSpeed();
        }

        void SetExactPose(
            donut::math::float3 position,
            donut::math::float3 direction,
            donut::math::float3 up,
            donut::math::float3 right)
        {
            // Clear Donut's private movement accumulators through its public
            // setter, then restore the captured float32 basis verbatim.
            FirstPersonCamera::LookTo(position, direction, up);
            m_CameraPos = position;
            m_CameraDir = direction;
            m_CameraUp = up;
            m_CameraRight = right;
            UpdateWorldToView();
        }

        void KeyboardUpdate(int key, int scancode, int action, int mods) override
        {
            const bool pressed = action == GLFW_PRESS || action == GLFW_REPEAT;
            switch (key)
            {
            case GLFW_KEY_LEFT: m_LookLeft = pressed; break;
            case GLFW_KEY_RIGHT: m_LookRight = pressed; break;
            case GLFW_KEY_UP: m_LookUp = pressed; break;
            case GLFW_KEY_DOWN: m_LookDown = pressed; break;
            case GLFW_KEY_LEFT_SHIFT: m_LeftSprint = m_TranslationEnabled && pressed; break;
            case GLFW_KEY_RIGHT_SHIFT: m_RightSprint = m_TranslationEnabled && pressed; break;
            default: break;
            }

            int forwardedAction = action;
            if (!m_TranslationEnabled && IsTranslationKey(key))
                forwardedAction = GLFW_RELEASE;

            FirstPersonCamera::KeyboardUpdate(key, scancode, forwardedAction, mods);
            if (m_TranslationEnabled && (m_LeftSprint || m_RightSprint))
            {
                // Donut maps both Shift keys to one latch. Reassert the shared
                // latch when either independently tracked key remains held.
                FirstPersonCamera::KeyboardUpdate(GLFW_KEY_LEFT_SHIFT, 0, GLFW_PRESS, 0);
            }
            UpdateMoveSpeed();
        }

        void Animate(float deltaT) override
        {
            FirstPersonCamera::Animate(deltaT);

            const float yawInput = float(m_LookLeft) - float(m_LookRight);
            const float pitchInput = float(m_LookUp) - float(m_LookDown);
            if ((yawInput == 0.f && pitchInput == 0.f) || deltaT <= 0.f)
                return;

            constexpr float KeyboardLookSpeed = donut::math::PI_f * 0.5f;
            donut::math::affine3 cameraRotation = donut::math::rotation(
                donut::math::float3(0.f, 1.f, 0.f),
                yawInput * KeyboardLookSpeed * deltaT);
            cameraRotation = donut::math::rotation(
                m_CameraRight,
                pitchInput * KeyboardLookSpeed * deltaT) * cameraRotation;

            m_CameraDir = donut::math::normalize(
                cameraRotation.transformVector(m_CameraDir));
            m_CameraUp = donut::math::normalize(
                cameraRotation.transformVector(m_CameraUp));
            m_CameraRight = donut::math::normalize(
                donut::math::cross(m_CameraDir, m_CameraUp));
            m_CameraUp = donut::math::normalize(
                donut::math::cross(m_CameraRight, m_CameraDir));
            UpdateWorldToView();
        }

    private:
        static bool IsTranslationKey(int key)
        {
            switch (key)
            {
            case GLFW_KEY_Q:
            case GLFW_KEY_E:
            case GLFW_KEY_A:
            case GLFW_KEY_D:
            case GLFW_KEY_W:
            case GLFW_KEY_S:
            case GLFW_KEY_LEFT_SHIFT:
            case GLFW_KEY_RIGHT_SHIFT:
            case GLFW_KEY_LEFT_CONTROL:
            case GLFW_KEY_RIGHT_CONTROL:
                return true;
            default:
                return false;
            }
        }

        void UpdateMoveSpeed()
        {
            constexpr float BaseMoveSpeed = 6.f;
            constexpr float DesiredSprintMultiplier = 2.f;
            constexpr float DonutSprintMultiplier = 3.f;
            const bool sprinting = m_TranslationEnabled &&
                (m_LeftSprint || m_RightSprint);
            SetMoveSpeed(sprinting
                ? BaseMoveSpeed * DesiredSprintMultiplier / DonutSprintMultiplier
                : BaseMoveSpeed);
        }

        bool m_TranslationEnabled = true;
        bool m_LookLeft = false;
        bool m_LookRight = false;
        bool m_LookUp = false;
        bool m_LookDown = false;
        bool m_LeftSprint = false;
        bool m_RightSprint = false;
    };

    class UvsrThirdPersonCamera : public UvsrFirstPersonCamera
    {
    public:
        UvsrThirdPersonCamera()
            : UvsrFirstPersonCamera(false)
        {
            ResetZoomReferenceDistance(10.f);
        }

        // Derive a conservative dolly scale from the initial framing, then
        // carry that scale with the free-moving eye. Wheel, W/S dolly, and A/D
        // strafe share a Shift-doubled speed without a fixed pivot limit.
        void ResetZoomReferenceDistance(float distance)
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
            m_DollyForward = false;
            m_DollyBackward = false;
            m_StrafeLeft = false;
            m_StrafeRight = false;
            m_LeftSpeedBoost = false;
            m_RightSpeedBoost = false;
        }

        [[nodiscard]] float GetReferenceZoomDistance() const
        {
            return m_ReferenceZoomDistance;
        }

        [[nodiscard]] float GetBaseWheelStepDistance() const
        {
            return m_BaseWheelStepDistance;
        }

        [[nodiscard]] float GetDollyScale() const
        {
            return m_DollyScale;
        }

        [[nodiscard]] float GetKeyboardDollyVelocity() const
        {
            return m_KeyboardDollyVelocity;
        }

        [[nodiscard]] float GetKeyboardStrafeVelocity() const
        {
            return m_KeyboardStrafeVelocity;
        }

        [[nodiscard]] bool IsSpeedBoosted() const
        {
            return m_LeftSpeedBoost || m_RightSpeedBoost;
        }

        void CancelPendingMotion()
        {
            m_RemainingWheelDistance = 0.f;
            m_KeyboardDollyVelocity = 0.f;
            m_KeyboardStrafeVelocity = 0.f;
            m_DollyForward = false;
            m_DollyBackward = false;
            m_StrafeLeft = false;
            m_StrafeRight = false;
            m_LeftSpeedBoost = false;
            m_RightSpeedBoost = false;
        }

        void KeyboardUpdate(int key, int scancode, int action, int mods) override
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
            else if (key == GLFW_KEY_LEFT_SHIFT)
                m_LeftSpeedBoost = pressed;
            else if (key == GLFW_KEY_RIGHT_SHIFT)
                m_RightSpeedBoost = pressed;

            // The parent remains translation-disabled, so it records arrow
            // look and mouse state but filters every movement key.
            UvsrFirstPersonCamera::KeyboardUpdate(
                key, scancode, action, mods);
        }

        void MouseScrollUpdate(double xoffset, double yoffset) override
        {
            (void)xoffset;
            if (yoffset == 0.0)
                return;

            // Change sensitivity by a small linear amount per notch. The old
            // multiplicative scale compounded inward until motion became an
            // unusably small fraction of its starting speed.
            constexpr float WheelScaleStep = 0.025f;
            constexpr float MinimumDollyScale = 0.4f;
            constexpr float MaximumDollyScale = 4.f;
            const float speedMultiplier = IsSpeedBoosted() ? 2.f : 1.f;
            if (yoffset > 0.0)
            {
                m_RemainingWheelDistance +=
                    m_BaseWheelStepDistance * m_DollyScale * speedMultiplier;
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
                    m_BaseWheelStepDistance * m_DollyScale * speedMultiplier;
            }
        }

        void Animate(float deltaT) override
        {
            UvsrFirstPersonCamera::Animate(deltaT);
            const float clampedDeltaT = donut::math::clamp(deltaT, 0.f, 0.1f);
            if (clampedDeltaT <= 0.f)
                return;

            const float dollyInput =
                float(m_DollyForward) - float(m_DollyBackward);
            const float strafeInput =
                float(m_StrafeRight) - float(m_StrafeLeft);
            const float speedMultiplier = IsSpeedBoosted() ? 2.f : 1.f;

            // W moves inward and gently lowers close-range sensitivity; S
            // restores it. Use a linear, bounded change so holding W reaches a
            // useful cruise speed instead of decaying exponentially to a crawl.
            constexpr float DollyScaleRate = 0.06f;
            constexpr float MinimumDollyScale = 0.4f;
            constexpr float MaximumDollyScale = 4.f;
            if (dollyInput != 0.f)
            {
                m_DollyScale = donut::math::clamp(
                    m_DollyScale -
                        dollyInput * DollyScaleRate * clampedDeltaT,
                    MinimumDollyScale,
                    MaximumDollyScale);
            }

            const float targetVelocity = dollyInput *
                m_BaseKeyboardDollySpeed * m_DollyScale * speedMultiplier;
            // Reach the requested velocity and return to rest in finite time.
            // This keeps the input smooth without an exponential drift tail.
            constexpr float KeyboardAccelerationRate = 5.f;
            constexpr float KeyboardDecelerationRate = 8.f;
            const float velocityRate = m_BaseKeyboardDollySpeed *
                speedMultiplier *
                (dollyInput == 0.f
                    ? KeyboardDecelerationRate
                    : KeyboardAccelerationRate);
            const float velocityDelta = donut::math::clamp(
                targetVelocity - m_KeyboardDollyVelocity,
                -velocityRate * clampedDeltaT,
                velocityRate * clampedDeltaT);
            m_KeyboardDollyVelocity += velocityDelta;

            const float targetStrafeVelocity = strafeInput *
                m_BaseKeyboardDollySpeed * m_DollyScale * speedMultiplier;
            const float strafeVelocityRate = m_BaseKeyboardDollySpeed *
                speedMultiplier *
                (strafeInput == 0.f
                    ? KeyboardDecelerationRate
                    : KeyboardAccelerationRate);
            const float strafeVelocityDelta = donut::math::clamp(
                targetStrafeVelocity - m_KeyboardStrafeVelocity,
                -strafeVelocityRate * clampedDeltaT,
                strafeVelocityRate * clampedDeltaT);
            m_KeyboardStrafeVelocity += strafeVelocityDelta;

            constexpr float WheelMotionResponse = 14.f;
            const float wheelBlend = 1.f - std::exp(
                -WheelMotionResponse * clampedDeltaT);
            const float wheelMovement =
                m_RemainingWheelDistance * wheelBlend;
            m_RemainingWheelDistance -= wheelMovement;
            if (std::abs(m_RemainingWheelDistance) < 1e-5f)
                m_RemainingWheelDistance = 0.f;

            const float dollyMovement =
                m_KeyboardDollyVelocity * clampedDeltaT + wheelMovement;
            const float strafeMovement =
                m_KeyboardStrafeVelocity * clampedDeltaT;
            if (dollyMovement != 0.f || strafeMovement != 0.f)
            {
                m_CameraPos += m_CameraDir * dollyMovement +
                    m_CameraRight * strafeMovement;
                UpdateWorldToView();
            }
        }

        // Collision corrects only the final dolly translation. There is no
        // pivot, orbit target, or latent desired position to fight on the next
        // frame, so the corrected position becomes the next zoom origin.
        void ApplyCollisionPosition(donut::math::float3 position)
        {
            m_CameraPos = position;
            UpdateWorldToView();
        }

    private:
        float m_ReferenceZoomDistance = 10.f;
        float m_BaseWheelStepDistance = 0.15f;
        float m_BaseKeyboardDollySpeed = 1.6f;
        float m_DollyScale = 1.f;
        float m_RemainingWheelDistance = 0.f;
        float m_KeyboardDollyVelocity = 0.f;
        float m_KeyboardStrafeVelocity = 0.f;
        bool m_DollyForward = false;
        bool m_DollyBackward = false;
        bool m_StrafeLeft = false;
        bool m_StrafeRight = false;
        bool m_LeftSpeedBoost = false;
        bool m_RightSpeedBoost = false;
    };

    class StaticViewCamera : public donut::app::BaseCamera
    {
    public:
        void LookTo(
            donut::math::float3 position,
            donut::math::float3 direction,
            donut::math::float3 up)
        {
            BaseLookAt(position, position + direction, up);
        }

        void SetExactPose(
            donut::math::float3 position,
            donut::math::float3 direction,
            donut::math::float3 up,
            donut::math::float3 right)
        {
            m_CameraPos = position;
            m_CameraDir = direction;
            m_CameraUp = up;
            m_CameraRight = right;
            UpdateWorldToView();
        }
    };
}
