#pragma once

#include "renderer_vector_math.h"

namespace uvsr
{
    struct RendererSceneBounds;
    enum class CameraMode { FirstPerson, ThirdPerson, Static, Pivot };

    class CameraController
    {
    public:
        virtual ~CameraController() = default;
        virtual void KeyboardUpdate(int, int, int, int) noexcept {}
        virtual void MousePosUpdate(double, double) noexcept {}
        virtual void MouseButtonUpdate(int, int, int) noexcept {}
        virtual void MouseScrollUpdate(double, double) noexcept {}
        virtual void Animate(float) noexcept {}
        void SetMoveSpeed(float value) noexcept { m_MoveSpeed = value; }
        void SetRotateSpeed(float value) noexcept { m_RotateSpeed = value; }
        const gpu_contract::Float4x4& GetWorldToViewMatrix() const noexcept { return m_WorldToView; }
        const gpu_contract::Float3& GetPosition() const noexcept { return m_CameraPos; }
        const gpu_contract::Float3& GetDir() const noexcept { return m_CameraDir; }
        const gpu_contract::Float3& GetUp() const noexcept { return m_CameraUp; }

    protected:
        void BaseLookAt(gpu_contract::Float3 position, gpu_contract::Float3 target,
            gpu_contract::Float3 up) noexcept;
        void SetBasis(gpu_contract::Float3 position, gpu_contract::Float3 direction,
            gpu_contract::Float3 up, gpu_contract::Float3 right) noexcept;
        void UpdateWorldToView() noexcept;
        gpu_contract::Float3 m_CameraPos{};
        gpu_contract::Float3 m_CameraDir{1.f, 0.f, 0.f};
        gpu_contract::Float3 m_CameraUp{0.f, 1.f, 0.f};
        gpu_contract::Float3 m_CameraRight{0.f, 0.f, 1.f};
        float m_MoveSpeed = 1.f;
        float m_RotateSpeed = .005f;

    private:
        gpu_contract::Float4x4 m_WorldToView{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    };

    class UvsrFirstPersonCamera : public CameraController
    {
    public:
        explicit UvsrFirstPersonCamera(bool translationEnabled = true) noexcept;
        void LookTo(gpu_contract::Float3 position, gpu_contract::Float3 direction,
            gpu_contract::Float3 up = {0.f, 1.f, 0.f}) noexcept;
        void SetExactPose(gpu_contract::Float3 position, gpu_contract::Float3 direction,
            gpu_contract::Float3 up, gpu_contract::Float3 right) noexcept;
        void KeyboardUpdate(int key, int scancode, int action, int mods) noexcept override;
        void MouseButtonUpdate(int button, int action, int mods) noexcept override;
        void MousePosUpdate(double xpos, double ypos) noexcept override;
        void ResetRoll() noexcept;
        void Animate(float deltaT) noexcept override;

    protected:
        float MovementSpeedMultiplier() const noexcept;
        void ReleaseMovementSpeedModifier() noexcept;
        void CancelRollLeveling() noexcept;

    private:
        enum Motion { Up, Down, Left, Right, Forward, Backward, RollLeft, RollRight, Slow, MotionCount };
        void SetMotionKey(int key, int action) noexcept;
        void AdvanceMotion(float deltaT) noexcept;
        static bool IsFinite(gpu_contract::Float3 value) noexcept;
        static bool BuildLevelBasis(gpu_contract::Float3 direction,
            gpu_contract::Float3& levelRight, gpu_contract::Float3& levelUp,
            gpu_contract::Float3& directionAxis) noexcept;
        static bool IsCameraAffectingKey(int key) noexcept;
        static bool IsTranslationKey(int key) noexcept;
        void AdvanceRollLeveling(float deltaT) noexcept;
        void FinishRollLeveling() noexcept;
        bool m_Motion[MotionCount]{};
        gpu_contract::Float2 m_MousePos{};
        gpu_contract::Float2 m_MousePosPrev{};
        bool m_LeftMousePressed = false;
        bool m_IsDragging = false;
        bool m_TranslationEnabled = true;
        bool m_LeftShift = false;
        bool m_RightShift = false;
        bool m_LookLeft = false;
        bool m_LookRight = false;
        bool m_LookUp = false;
        bool m_LookDown = false;
        bool m_RollLevelingActive = false;
        double m_RollLevelElapsedSeconds = 0.0;
        float m_RollLevelInitialAngle = 0.f;
        gpu_contract::Float3 m_RollLevelTargetRight{};
        gpu_contract::Float3 m_RollLevelTargetUp{};
        gpu_contract::Float3 m_RollLevelStartPosition{};
        gpu_contract::Float3 m_RollLevelStartDirection{};
    };

    class UvsrThirdPersonCamera : public UvsrFirstPersonCamera
    {
    public:
        UvsrThirdPersonCamera() noexcept;
        void ResetZoomReferenceDistance(float distance) noexcept;
        float GetReferenceZoomDistance() const noexcept { return m_ReferenceZoomDistance; }
        float GetBaseWheelStepDistance() const noexcept { return m_BaseWheelStepDistance; }
        float GetDollyScale() const noexcept { return m_DollyScale; }
        float GetKeyboardDollyVelocity() const noexcept { return m_KeyboardDollyVelocity; }
        float GetKeyboardStrafeVelocity() const noexcept { return m_KeyboardStrafeVelocity; }
        float GetKeyboardVerticalVelocity() const noexcept { return m_KeyboardVerticalVelocity; }
        void CancelPendingMotion() noexcept;
        void KeyboardUpdate(int key, int scancode, int action, int mods) noexcept override;
        void MouseScrollUpdate(double xoffset, double yoffset) noexcept override;
        void Animate(float deltaT) noexcept override;
        void ApplyCollisionPosition(gpu_contract::Float3 position) noexcept;
        [[nodiscard]] bool FrameBounds(const RendererSceneBounds& bounds, float verticalFovDegrees,
            float distanceScale, bool resetOrientation) noexcept;

    private:
        bool HasPendingTranslation() const noexcept;
        float m_ReferenceZoomDistance = 10.f;
        float m_BaseWheelStepDistance = .15f;
        float m_BaseKeyboardDollySpeed = 1.6f;
        float m_DollyScale = 1.f;
        float m_RemainingWheelDistance = 0.f;
        float m_KeyboardDollyVelocity = 0.f;
        float m_KeyboardStrafeVelocity = 0.f;
        float m_KeyboardVerticalVelocity = 0.f;
        bool m_DollyForward = false;
        bool m_DollyBackward = false;
        bool m_StrafeLeft = false;
        bool m_StrafeRight = false;
        bool m_MoveUp = false;
        bool m_MoveDown = false;
    };

    class StaticViewCamera : public CameraController
    {
    public:
        void LookTo(gpu_contract::Float3 position, gpu_contract::Float3 direction,
            gpu_contract::Float3 up) noexcept;
        void SetExactPose(gpu_contract::Float3 position, gpu_contract::Float3 direction,
            gpu_contract::Float3 up, gpu_contract::Float3 right) noexcept;
    };
}
